// Training Ground / level generator: the CRITICS of a candidate (docs/design/levelgen/LEVEL_GENERATOR.md section 5).
//   - the Claude API critic (offline = not in the browser; Node only): sees the candidate's top-down render, the reference's
//     un-skewed image and the evaluation metrics, answers "fits / does not fit" the reference's tactical profile with reasons
//     and fixes - strict JSON. Budget-capped in USD like the red team (--budget-usd): a call is skipped when the ledger's spend
//     plus the estimated cost would exceed the budget. Default model: Claude Opus 5.5 (the claude-api skill's default; Fable 5.1
//     by --model). The Messages API over plain fetch, as clients.ts (the repo has no SDK dependency); the key is never printed.
//   - the Jev (TypeSafe System One) critic: typed judgments over the metrics only - fits (a Noul probability), quality (a Score
//     1-5), the weakest aspect (a Choice) - for mass ranking of many candidates at no USD cost (no known per-token price).
//     Without TYPESAFE_API_KEY an OFFLINE heuristic answers and says so (Scripts/Tools/typesafe_triage.py does the same).
//   - the mock critic: deterministic, from the evaluation alone (tests, no network).
import type { Env } from '../movement/clients.ts';
import { apiKey, costOf, realEnv } from '../movement/clients.ts';
import type { Evaluation } from './evaluate.ts';
import type { Profile } from './profile.ts';

export type CriticClient = 'claude' | 'jev' | 'mock';

export interface CriticVerdict {
  client: CriticClient;
  model: string;
  /** The verdict; null when no judgment was made (budget, no key, error). */
  fits: boolean | null;
  /** 0..1 (Claude: self-reported; Jev: the Noul probability; mock: heuristic). */
  confidence: number | null;
  /** Jev: quality 1..5 (null for the others). */
  quality: number | null;
  reasons: string[];
  fixes: string[];
  /** The weakest aspect (Jev's choice / Claude's first fix / the mock's lowest metric). */
  weakest: string | null;
  costUsd: number;
  usage: { input_tokens: number; output_tokens: number };
  /** The heuristic answered instead of a model. */
  offline: boolean;
  /** Why no judgment / what went wrong. */
  note?: string;
  at: string;
}

/** The USD ledger the callers keep (Saved/TrainingGround/levelgen/critic_ledger.json): cumulative spend across UI / CLI calls. */
export interface CriticLedger { spentUsd: number; calls: { at: string; layout: string; client: CriticClient; model: string; costUsd: number; fits: boolean | null }[] }

export const emptyLedger = (): CriticLedger => ({ spentUsd: 0, calls: [] });

export const CRITIC_MODELS = { default: 'claude-opus-5-5', strong: 'claude-fable-5-1', cheap: 'claude-sonnet-5-5' } as const;

export interface ClaudeCriticOptions {
  layoutName: string;
  evaluation: Evaluation;
  profile: Profile;
  /** Image bytes, base64 (no data: prefix): the candidate's top-down render (PNG) and the reference's un-skewed map (JPEG by default). */
  candidatePng: string;
  referencePng: string | null;
  referenceMime?: 'image/png' | 'image/jpeg';
  budgetUsd: number;
  ledger: CriticLedger;
  model?: string;
  env?: Env;
  timeoutMs?: number;
}

const SYSTEM = [
  'You are the level-design critic of a tactical squad game (Operation: Cold Silence, Unreal Engine; a Gorky 17 tribute: real time + tactical pause,',
  'a 3-4 operative squad moving in formations - column / wedge / diamond, wall layouts along facades, corner peeks, covers, ladders onto roofs).',
  'Height levels are 3 m steps (-1 the cut floor, 0 the ground, +1 / +2 floors and low roofs, +3 = 9 m the roofs of the tall halls, for the view over the blocks).',
  'A generator produced an ORIGINAL greybox layout from a reference profile read off the game designer\'s annotated reference map. Judge whether the',
  'candidate FITS the reference\'s tactical profile: its scale and proportions, the placement of obstacles, barricades, enterable buildings, ladders,',
  'the dividing cut with few crossings, the long marksman lanes against the short-sightline compounds, the density of covers per zone, a mandatory',
  'route through both halves with variants. It must NOT be a copy of the reference layout.',
  'Game designer decisions you must respect (not defects): factory chimneys are tall obstacles, never climbable towers; a route marked intent "kill_zone"',
  '(the variant along the cut floor) is a DELIBERATE killing zone - its exposure and few exits are intended; the junkyard is an ambush field the route',
  'passes, not a route; the mandatory route ends in the SW dead end. Answer strict JSON only:',
  '{"fits": true|false, "confidence": 0..1, "reasons": ["<short, concrete, cite a metric or a place>", ...], "fixes": ["<one change each>", ...]}',
  '- at most 6 reasons and 4 fixes, no preamble.',
].join(' ');

/** The text part of the critic prompt (shared by the real call and the dry run). */
export function criticPrompt(o: Pick<ClaudeCriticOptions, 'layoutName' | 'evaluation' | 'profile'>): string {
  const ev = o.evaluation;
  const tac = ev.tactical;
  const brief = {
    layout: o.layoutName, score: ev.score, route: { completable: ev.route.completable, fail: ev.route.fail, length_m: ev.route.length_m, layers: ev.route.layers },
    variants: ev.variants.map((v) => ({ id: v.id, ok: v.completable, length_m: v.length_m, fail: v.fail, ...(v.intent ? { intent: v.intent, intended: true } : {}) })),
    mechanics: ev.mechanics, sightlines: ev.sightlines, roof_views: ev.roofViews, kill_zones_intended: ev.killZones, ambush: ev.ambush, counts: ev.counts, similarity: ev.similarity,
    tactical: {
      score: tac.score,
      rooms: `${tac.roomsReachable}/${tac.roomsTotal}`,
      unreachable_rooms: tac.unreachableRooms,
      blocked_windows: tac.blockedWindows.map((w) => `${w.id}: ${w.reason}`),
      firing_lines: { openingsTotal: tac.firingLines.openingsTotal, openingsWithLos: tac.firingLines.openingsWithLos, count: tac.firingLines.count, maxLength_m: tac.firingLines.maxLength_m },
      route_cover: tac.routeCover,
      multi_level_routes: tac.multiLevelRoutes,
      mantle: tac.mantle,
      buildings: tac.buildings.filter((b) => b.isCombatBuilding).map((b) => ({ id: b.id, score: b.score, rooms: `${b.roomsReachable}/${b.roomsTotal}`, roofReachable: b.roofReachable, blocked: b.blockedWindows.length })),
    },
  };
  const ref = { id: o.profile.id, size_m: o.profile.bounds.max, targets: o.profile.targets, set_pieces: o.profile.set_pieces, densities: o.profile.densities.covers_per_100m2,
    sightlines: o.profile.sightlines.stats_m, route_m: o.profile.route.main.length_m, variants: o.profile.route.variants.length, ladders: o.profile.ladders.length };
  return [
    'Image 1 = the candidate (top-down greybox render: blue = the mandatory route, light blue = variants, yellow = ladders, white = buildings with a green inner outline when the roof is walkable, dark blue band = the cut).',
    o.evaluation && 'Image 2 = the reference map, un-skewed to the same top-down frame (another game\'s map; structure and densities only).',
    'Candidate metrics (JSON):', JSON.stringify(brief),
    'Reference profile targets (JSON):', JSON.stringify(ref),
    'Does the candidate fit the reference\'s tactical profile? Judge scale, structure, verticality, cover densities, sightlines, the route. Strict JSON.',
  ].filter(Boolean).join('\n');
}

/** Rough input-token estimate: text / 4 + an image ~ (w * h) / 750 with our renders ~ 1000 x 750. */
export function estimateCostUsd(model: string, promptChars: number, images: number, maxOutput: number): number {
  const input = Math.ceil(promptChars / 4) + images * 1100 + 400;
  return costOf(model, input, maxOutput);
}

/** Removes anything key-shaped from a message before it can reach a log. */
const scrub = (text: string, key: string) => text.split(key).join('***').replace(/sk-ant-[A-Za-z0-9_-]+/g, '***').slice(0, 300);

export async function claudeCritic(o: ClaudeCriticOptions): Promise<CriticVerdict> {
  const env = o.env ?? realEnv;
  const model = o.model ?? CRITIC_MODELS.default;
  const at = new Date().toISOString();
  const base: CriticVerdict = { client: 'claude', model, fits: null, confidence: null, quality: null, reasons: [], fixes: [], weakest: null, costUsd: 0, usage: { input_tokens: 0, output_tokens: 0 }, offline: false, at };
  const key = apiKey(env, 'ANTHROPIC_API_KEY');
  if (!key) return { ...base, note: 'no ANTHROPIC_API_KEY in the environment' };
  const prompt = criticPrompt(o);
  const maxOutput = 1200;
  const estimate = estimateCostUsd(model, SYSTEM.length + prompt.length, o.referencePng ? 2 : 1, maxOutput + 1500);
  if (o.ledger.spentUsd + estimate > o.budgetUsd) return { ...base, note: `budget $${o.budgetUsd}: spent $${o.ledger.spentUsd.toFixed(4)}, the next call (~$${estimate.toFixed(3)}) would exceed it` };
  const image = (data: string, media_type = 'image/png') => ({ type: 'image', source: { type: 'base64', media_type, data } });
  const content: unknown[] = [image(o.candidatePng)];
  if (o.referencePng) content.push(image(o.referencePng, o.referenceMime ?? 'image/jpeg'));
  content.push({ type: 'text', text: prompt });
  // Opus 5.5: thinking is adaptive and cannot be disabled - effort low keeps it short; thinking tokens count against max_tokens.
  const body = JSON.stringify({ model, max_tokens: maxOutput + 4000, system: SYSTEM, messages: [{ role: 'user', content }], output_config: { effort: 'low' } });
  try {
    const res = await env.fetch('https://api.anthropic.com/v1/messages', {
      method: 'POST', headers: { 'x-api-key': key, 'anthropic-version': '2023-06-01', 'content-type': 'application/json' }, body,
      signal: AbortSignal.timeout(o.timeoutMs ?? 180_000),
    });
    const text = await res.text();
    if (res.status === 401) return { ...base, note: 'Anthropic API: 401 unauthorized - ANTHROPIC_API_KEY is invalid or revoked' };
    if (!res.ok) return { ...base, note: `Anthropic API: HTTP ${res.status} ${scrub(text, key)}` };
    const data = JSON.parse(text) as { content?: { type: string; text?: string }[]; usage?: { input_tokens?: number; output_tokens?: number }; stop_reason?: string };
    const usage = { input_tokens: data.usage?.input_tokens ?? 0, output_tokens: data.usage?.output_tokens ?? 0 };
    const costUsd = costOf(model, usage.input_tokens, usage.output_tokens);
    if (data.stop_reason === 'refusal') return { ...base, usage, costUsd, note: 'Anthropic API: the model refused the request' };
    const out = (data.content ?? []).filter((b) => b.type === 'text').map((b) => b.text ?? '').join('');
    const parsed = parseVerdict(out);
    return { ...base, ...parsed, usage, costUsd, note: parsed.fits === null ? `unparseable reply: ${out.slice(0, 160)}` : undefined };
  } catch (e) {
    return { ...base, note: `network: ${scrub(e instanceof Error ? e.message : String(e), key)}` };
  }
}

/** Parses the critic's strict JSON (tolerates text round it). */
export function parseVerdict(text: string): Pick<CriticVerdict, 'fits' | 'confidence' | 'reasons' | 'fixes' | 'weakest'> {
  const start = text.indexOf('{'), end = text.lastIndexOf('}');
  if (start < 0 || end <= start) return { fits: null, confidence: null, reasons: [], fixes: [], weakest: null };
  try {
    const d = JSON.parse(text.slice(start, end + 1)) as { fits?: unknown; confidence?: unknown; reasons?: unknown; fixes?: unknown };
    const strs = (v: unknown) => (Array.isArray(v) ? v.filter((x) => typeof x === 'string').map((x) => String(x).slice(0, 300)) : []);
    const fixes = strs(d.fixes).slice(0, 4);
    return { fits: typeof d.fits === 'boolean' ? d.fits : null, confidence: typeof d.confidence === 'number' ? Math.max(0, Math.min(1, d.confidence)) : null, reasons: strs(d.reasons).slice(0, 6), fixes, weakest: fixes[0] ?? null };
  } catch { return { fits: null, confidence: null, reasons: [], fixes: [], weakest: null }; }
}

// ---- Jev ------------------------------------------------------------------------------------------------------------------

const JEV_URL = 'https://api.typesafe.ai/v1/systemone';
export const JEV_ASPECTS = ['route', 'scale', 'verticality', 'covers', 'sightlines', 'ambush', 'similarity'] as const;

/** The typed questions (one request, independent judgments over the same state; the typesafe-ai skill's guidance). */
export function jevQuestions(): Record<string, unknown> {
  return {
    fits: { type: 'noul', instructions: 'The candidate greybox layout fits the reference profile\'s tactical pattern: a completable mandatory route through both halves, long marksman lanes with short-sightline compounds between them, ladders and roofs (the tall halls\' roofs on +3 = 9 m for the view), cover densities near the targets, the counts inside their target ranges. A variant marked as an intended kill zone (the cut floor) is a deliberate design choice, not a defect; chimneys are obstacles, never towers.' },
    // a Score's criteria = the ordered levels (a list; the API rejects an object), each a concrete situation
    quality: { type: 'score', instructions: 'How good is this candidate as a level-design greybox for a tactical squad game (level 1 = worst, 5 = best)?', criteria: [
      'unusable: the mandatory route fails or the layout is empty', 'poor: the route works but most targets are missed and the mechanics are barely exercised',
      'fair: the route works, about half the targets are inside their ranges, several mechanics are exercised', 'good: the route and the variants work, most targets inside, a varied formation mix and ambush ground',
      'excellent: everything inside the targets, strong verticality, a rich formation mix, many concealed ambush spots'] },
    weakest: { type: 'choice', instructions: 'Which aspect of the candidate is the weakest against the reference profile (fix it first)?',
      criteria: Object.fromEntries(JEV_ASPECTS.map((a) => [a, ({ route: 'the mandatory route / variants (completability, length)', scale: 'the map size and proportions', verticality: 'ladders, roofs, stairwells, floors',
        covers: 'the cover densities per zone', sightlines: 'the lanes and short-sightline share', ambush: 'the concealed ambush spots', similarity: 'the overall similarity score' } as Record<string, string>)[a]])) },
  };
}

export function jevState(ev: Evaluation, profile: Profile): Record<string, unknown> {
  return {
    candidate: { score: ev.score, route_completable: ev.route.completable, route_fail: ev.route.fail ?? null, route_length_m: ev.route.length_m, variants_ok: ev.variants.filter((v) => v.completable).length, variants: ev.variants.length,
      mechanics: ev.mechanics, sightlines: ev.sightlines, roof_views: ev.roofViews, intended_kill_zones: ev.killZones.map((k) => `${k.route}: ${k.in_zone_m} m, ${k.ways_out} ways out (intended)`),
      ambush: ev.ambush, counts: ev.counts, similarity: ev.similarity.score, misses: ev.similarity.metrics.filter((m) => m.score < 1).map((m) => `${m.key} ${m.value} (target ${m.target?.join('..')})`),
      tactical: {
        score: ev.tactical.score,
        rooms_reachable: `${ev.tactical.roomsReachable}/${ev.tactical.roomsTotal}`,
        unreachable_rooms: ev.tactical.unreachableRooms.length,
        blocked_windows: ev.tactical.blockedWindows.length,
        firing_lines: ev.tactical.firingLines.count,
        route_cover_share: ev.tactical.routeCover.share,
        multi_level_routes: ev.tactical.multiLevelRoutes.count,
        mantle_edges: ev.tactical.mantle.valid,
      } },
    reference: { id: profile.id, size_m: profile.bounds.max, targets: profile.targets, set_pieces: profile.set_pieces },
  };
}

export async function jevCritic(o: { evaluation: Evaluation; profile: Profile; env?: Env; timeoutMs?: number }): Promise<CriticVerdict> {
  const env = o.env ?? realEnv;
  const at = new Date().toISOString();
  const key = apiKey(env, 'TYPESAFE_API_KEY');
  if (!key) return { ...mockCritic(o.evaluation), client: 'jev', model: 'offline-heuristic', offline: true, note: 'no TYPESAFE_API_KEY: the offline heuristic answered', at };
  const body = JSON.stringify({ state: jevState(o.evaluation, o.profile), model: 'jev-latest', questions: jevQuestions() });
  try {
    const res = await env.fetch(JEV_URL, { method: 'POST', headers: { Authorization: `Bearer ${key}`, 'Content-Type': 'application/json' }, body, signal: AbortSignal.timeout(o.timeoutMs ?? 60_000) });
    const text = await res.text();
    if (!res.ok) return { ...mockCritic(o.evaluation), client: 'jev', model: 'offline-heuristic', offline: true, note: `Jev API: HTTP ${res.status} ${scrub(text, key)} - the offline heuristic answered`, at };
    const data = JSON.parse(text) as { answers?: Record<string, { noul?: number; probability?: number; value?: number; score?: number; choice?: string; selected?: string; confidence?: number }>; usage?: { input_tokens?: number; output_tokens?: number } };
    const a = data.answers ?? {};
    const p = a.fits?.noul ?? a.fits?.probability ?? a.fits?.value ?? null;
    const q = a.quality?.score ?? a.quality?.value ?? null;
    const weakest = a.weakest?.choice ?? a.weakest?.selected ?? null;
    return { client: 'jev', model: 'jev-latest', fits: p === null ? null : p >= 0.5, confidence: p, quality: q, weakest,
      reasons: [p !== null ? `Jev: fits with probability ${p.toFixed(2)}` : 'Jev: no fits answer', q !== null ? `quality ${q.toFixed(1)} / 5` : '', weakest ? `weakest aspect: ${weakest}` : ''].filter(Boolean),
      fixes: weakest ? [`improve ${weakest} first`] : [], costUsd: 0, usage: { input_tokens: data.usage?.input_tokens ?? 0, output_tokens: data.usage?.output_tokens ?? 0 }, offline: false, at };
  } catch (e) {
    return { ...mockCritic(o.evaluation), client: 'jev', model: 'offline-heuristic', offline: true, note: `Jev API: ${scrub(e instanceof Error ? e.message : String(e), key)} - the offline heuristic answered`, at };
  }
}

// ---- mock / offline heuristic ---------------------------------------------------------------------------------------------

/** Deterministic judgment from the evaluation alone: fits when the route completes, similarity >= 0.75, and combat buildings have no unreachable rooms/roofs or blocked windows. */
export function mockCritic(ev: Evaluation): CriticVerdict {
  const misses = ev.similarity.metrics.filter((m) => m.score < 1);
  const tac = ev.tactical;
  const roofUnreachable = tac.buildings.filter((b) => !b.roofReachable).map((b) => b.id);
  const tacticalOk = tac.unreachableRooms.length === 0 && tac.blockedWindows.length === 0 && roofUnreachable.length === 0;
  const fits = ev.route.completable && ev.similarity.score >= 0.75 && ev.variants.every((v) => v.completable) && tacticalOk;
  const reasons = [
    ev.route.completable ? `the mandatory route completes (${ev.route.length_m} m)` : `the mandatory route FAILS: ${ev.route.fail}`,
    ...ev.variants.filter((v) => !v.completable).map((v) => `variant ${v.id} fails: ${v.fail}`),
    ...ev.killZones.map((k) => `${k.route} is an intended kill zone (${k.in_zone_m} m, ${k.ways_out} ways out) - by design`),
    ...(tac.unreachableRooms.length ? [`unreachable rooms: ${tac.unreachableRooms.slice(0, 3).join(', ')}`] : []),
    ...(tac.blockedWindows.length ? [`${tac.blockedWindows.length} blocked window(s) opening into a wall`] : []),
    ...(roofUnreachable.length ? [`unreachable roof(s): ${roofUnreachable.join(', ')}`] : []),
    `tactical ${tac.score} (rooms ${tac.roomsReachable}/${tac.roomsTotal}, firing lines ${tac.firingLines.count} up to ${tac.firingLines.maxLength_m} m, cover ${Math.round(tac.routeCover.share * 100)}%, mantle ${tac.mantle.valid})`,
    `similarity ${ev.similarity.score}${misses.length ? `; off target: ${misses.map((m) => `${m.key} ${m.value}`).join(', ')}` : ''}`,
    `ambush ${ev.ambush.per100m} concealed spots / 100 m`, `sightlines p50 ${ev.sightlines.p50_m} m, lanes ${ev.sightlines.lanes_over_80m}`,
  ].slice(0, 6);
  const worst = [...misses].sort((a, b) => a.score - b.score)[0];
  const fixes: string[] = [];
  if (tac.unreachableRooms.length) fixes.push(`connect unreachable rooms (${tac.unreachableRooms[0]}) via doors/stairs`);
  if (tac.blockedWindows.length) fixes.push(`clear wall obstruction in front of ${tac.blockedWindows[0].id}`);
  if (roofUnreachable.length) fixes.push(`add stair/ladder/hatch to roof of ${roofUnreachable[0]}`);
  if (worst) fixes.push(`bring ${worst.key} (${worst.value}) into ${worst.target?.join('..')}`);
  const quality = !ev.route.completable || !tacticalOk ? 1 : ev.similarity.score >= 0.9 && ev.ambush.per100m >= 2 ? 5 : ev.similarity.score >= 0.75 ? 4 : ev.similarity.score >= 0.5 ? 3 : 2;
  return { client: 'mock', model: 'mock', fits, confidence: ev.route.completable ? Math.min(1, 0.5 + ev.similarity.score / 2) : 0.1, quality, reasons,
    fixes: fixes.slice(0, 4), weakest: fixes[0] ?? worst?.key ?? null, costUsd: 0, usage: { input_tokens: 0, output_tokens: 0 }, offline: true, at: new Date().toISOString() };
}

/** One line per verdict for the report. */
export function renderVerdict(v: CriticVerdict): string {
  const head = v.fits === null ? 'NO VERDICT' : v.fits ? 'FITS' : 'DOES NOT FIT';
  return `${v.client} (${v.model}${v.offline ? ', offline' : ''}): ${head}${v.confidence !== null ? ` ${Math.round(v.confidence * 100)} %` : ''}${v.quality !== null ? `, quality ${v.quality}/5` : ''}, $${v.costUsd.toFixed(4)}${v.note ? ` - ${v.note}` : ''}${v.reasons.length ? ` | ${v.reasons.join('; ')}` : ''}${v.fixes.length ? ` | fix: ${v.fixes.join('; ')}` : ''}`;
}
