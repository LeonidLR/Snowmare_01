// Training Ground / movement: the red team (FORMATION_CONTRACT.md 11, user decision 2026-10-25). The model ADVERSARIALLY designs
// small layouts and routes aimed at breaking the formation rules; each is validated (schema, audit, start clearance, the leader
// reaches the end in the sim), the bot walks it (auto + every manual menu formation x members x seeds, 0 tokens), and every
// layout that broke at least one gated check is kept as a regression layout with `expected_fail` entries (a minimal
// reproduction per distinct (check, choice, members) failure: route, seed, time). Pure: no file system (cli/redteam.ts writes).
// The model sees only a distilled brief and the previous round's distilled results, never raw runs.

import type { CallRecord } from './train.ts';
import type { ModelClient, ModelRequest, RedteamPayload } from './ai.ts';
import { MODELS } from './ai.ts';
import { runSeries } from './bot.ts';
import type { RunMetrics } from './checks.ts';
import { LIMITS } from './checks.ts';
import type { FormationConfig } from './config.ts';
import { dist, pointInWall, round, segmentBlocked, sub, yawOf } from './geometry.ts';
import type { Layout } from './layout.ts';
import { LAYOUT_SCHEMA, wallSegments } from './layout.ts';
import { auditLayout } from './levelAudit.ts';
import type { MenuChoice } from './menu.ts';
import { MENU_ITEMS } from './menu.ts';
import { AGENT_RADIUS } from './nav.ts';
import { FormationShapeRules } from './rules.ts';
import { buildNav, simulate } from './sim.ts';

/** The attack families the brief asks the model to cover (user decision 2026-10-25). */
export const ATTACK_FAMILIES: { id: string; doc: string }[] = [
  { id: 'oblique_walls', doc: 'walls at 5-40 deg to the route, converging on a flank' },
  { id: 'stubs_pillars', doc: 'short wall stubs and pillars (20-150 cm) near the slots' },
  { id: 'gap_vs_width', doc: 'gaps just wider / narrower than the formation (diamond ~5.2 m wide, column ~1 m)' },
  { id: 'zigzag_corridor', doc: 'zigzag corridors, bends of 20-60 deg' },
  { id: 't_l_junction', doc: 'T and L junctions, turning into a side passage' },
  { id: 'dead_end_after_turn', doc: 'a dead end right after a turn' },
  { id: 'wall_ends_at_turn', doc: 'a wall ending exactly where the route turns' },
  { id: 'threat_behind_thin_wall', doc: 'a known threat behind a thin free-standing wall (bypass side, check 8)' },
  { id: 'u_turn', doc: 'U-turns round a wall end or inside a corridor' },
  { id: 'long_wall_periodic_gaps', doc: 'very long walls with periodic gaps (flicker, split through a gap)' },
  { id: 'hug_then_leave', doc: 'routes hugging a wall (0.6-1.5 m off) then leaving it at an angle' },
];

/** Every menu choice the bot walks per layout: auto + the 8 hold-G items (the `--manual all` path). */
export const ALL_CHOICES: MenuChoice[] = ['auto', ...MENU_ITEMS.map((i) => i.id)];

/** A manual formation ignores the terrain on purpose: only checks 1, 5, 7 gate it (renderManualMatrix). */
export const gated = (choice: MenuChoice, check: number): boolean => choice === 'auto' || [1, 5, 7].includes(check);

/** Checks whose failure breaks a promise the rules make unconditionally (geometry) vs. ones that hinge on written expectations. */
export const RULE_BUG_CHECKS = [1, 2, 5, 6, 7];
export const looksLike = (check: number): 'rule bug?' | 'design question' => (RULE_BUG_CHECKS.includes(check) ? 'rule bug?' : 'design question');

export const CHECK_SHORT: Record<number, string> = {
  1: 'wall stare', 2: 'rear cover (4 men)', 3: 'flicker', 4: 'expected formation per zone', 5: 'slots / spacing', 6: 'rear man pace',
  7: 'squad split by a wall', 8: 'bypass side', 9: 'corner watch',
};

/** Size limits of a red-team layout (keeps the nav grid and the series small). */
export const REDTEAM_LIMITS = { span: 10000, walls: 40, routes: 4, points: 12, maxSimTime: 590 } as const;

export interface Failure {
  check: number;
  choice: MenuChoice;
  members: number;
  route: string;
  seed: number;
  /** Sim time of the worst case, s (null for the per-run percentages: rear cover, zone expectation). */
  t: number | null;
  what: string;
}

export const failureKey = (f: Pick<Failure, 'check' | 'choice' | 'members'>): string => `#${f.check} ${f.choice} n${f.members}`;

/** The gated failures of one run (same pass rules as checks.summarize). */
export function runFailures(r: RunMetrics, choice: MenuChoice): Failure[] {
  const out: Failure[] = [];
  const add = (check: number, t: number | null, what: string) => {
    if (gated(choice, check)) out.push({ check, choice, members: r.members, route: r.route, seed: r.seed, t, what });
  };
  if (r.wallStare.events > 0) add(1, r.wallStare.worst?.t ?? null, r.wallStare.worst?.what ?? 'wall stare');
  if (r.members >= 4 && r.rearCover && r.rearCover.walkTime > 0 && r.rearCover.pct < LIMITS.rearCoverPct) add(2, null, `5-7 o'clock watched ${r.rearCover.pct} %`);
  if (r.flicker.per100m > LIMITS.flickerPer100m) add(3, r.flicker.worst?.t ?? null, r.flicker.worst?.what ?? 'flicker');
  for (const e of r.expect) if (e.pct < LIMITS.expectPct) add(4, null, `${e.zone} wants ${e.formation}: ${e.pct} %${e.settled === 0 ? ' (never settled)' : ''}`);
  if (r.slots.invalidTicks > 0 || r.slots.spacingEvents > 0) add(5, r.slots.worst?.t ?? null, r.slots.worst?.what ?? 'slots');
  if (r.pace && r.pace.p95 > LIMITS.paceBehind) add(6, r.pace.worst?.t ?? null, `p95 ${r.pace.p95} cm; ${r.pace.worst?.what ?? ''}`.trim());
  if (r.split.events > 0) add(7, r.split.worst?.t ?? null, r.split.worst?.what ?? 'split');
  for (const b of r.bypass) if (b.got !== b.want) add(8, null, `${b.wall}: wall on the ${b.got}, wants ${b.want}`);
  if (r.corner.missed > 0) add(9, r.corner.worst?.t ?? null, r.corner.worst?.what ?? 'corner');
  return out;
}

/** The minimal reproduction per distinct key: the earliest failing time (a timed one before an untimed one). */
export function minimalFailures(all: Failure[]): Failure[] {
  const best = new Map<string, Failure>();
  const rank = (f: Failure) => (f.t === null ? 1e9 : f.t);
  for (const f of all) {
    const k = failureKey(f), cur = best.get(k);
    if (!cur || rank(f) < rank(cur)) best.set(k, f);
  }
  return [...best.values()].sort((a, b) => a.check - b.check || ALL_CHOICES.indexOf(a.choice) - ALL_CHOICES.indexOf(b.choice) || a.members - b.members);
}

export interface EvalOptions { config: FormationConfig; members: number[]; seeds: number; seedBase: number; choices?: MenuChoice[] }

/** Walks a layout with every choice x members x seeds; returns the run count and the minimal failures. */
export function evaluateLayout(layout: Layout, o: EvalOptions): { runs: number; failures: Failure[] } {
  const nav = buildNav(layout);
  const all: Failure[] = [];
  let runs = 0;
  for (const choice of o.choices ?? ALL_CHOICES) {
    const s = runSeries({ layout, config: o.config, members: o.members, seeds: o.seeds, seedBase: o.seedBase, manual: choice, nav });
    runs += s.runs.length;
    for (const r of s.runs) all.push(...runFailures(r, choice));
  }
  return { runs, failures: minimalFailures(all) };
}

/** Does this one run (route, choice, members, seed) still fail the check? (cheap: one simulation) */
export function reproduces(layout: Layout, f: Pick<Failure, 'check' | 'choice' | 'members' | 'route' | 'seed'>, config: FormationConfig): boolean {
  const s = runSeries({ layout, config, members: [f.members], seeds: 1, seedBase: f.seed, manual: f.choice, routes: [f.route] });
  return s.runs.some((r) => runFailures(r, f.choice).some((x) => x.check === f.check));
}

/**
 * Validates and normalizes one model-designed layout: the name is ours; schema / ground_z / zones / spawn get benign defaults.
 * Errors: schema + audit errors, size limits, a wall in / between the squad's start slots round a route's first point (a start
 * artifact, not a rule bug; off-bounds slots are fine - the sim spawns them on the nearest walkable cell), the leader not reaching the end of a route in the sim.
 */
export function validateCandidate(data: unknown, name: string, config: FormationConfig): { layout: Layout | null; errors: string[] } {
  if (!data || typeof data !== 'object' || Array.isArray(data)) return { layout: null, errors: ['layout: expected a JSON object'] };
  const l = structuredClone(data) as Record<string, unknown>;
  l.schema = LAYOUT_SCHEMA;
  l.name = name;
  l.ground_z ??= 0;
  l.zones ??= [];
  const routes0 = Array.isArray(l.routes) ? (l.routes as { points?: unknown }[]) : [];
  if (!l.spawn && Array.isArray(routes0[0]?.points)) l.spawn = { pos: (routes0[0].points as unknown[])[0], yaw: 0 };
  for (const k of ['comment', 'redteam', 'expected_fail', 'fixed_entries', 'fixed_in']) delete l[k];
  const issues = auditLayout(l, config);
  const errors = issues.filter((i) => i.severity === 'error').map((i) => `${i.where}: ${i.message}`);
  if (errors.length) return { layout: null, errors };
  const layout = l as unknown as Layout;
  const span = Math.max(layout.bounds.max[0] - layout.bounds.min[0], layout.bounds.max[1] - layout.bounds.min[1]);
  if (span > REDTEAM_LIMITS.span) errors.push(`bounds: span ${span} cm > ${REDTEAM_LIMITS.span}`);
  if (layout.walls.length > REDTEAM_LIMITS.walls) errors.push(`walls: ${layout.walls.length} > ${REDTEAM_LIMITS.walls}`);
  if (!layout.routes.length || layout.routes.length > REDTEAM_LIMITS.routes) errors.push(`routes: 1..${REDTEAM_LIMITS.routes} expected`);
  for (const r of layout.routes) if (r.points.length > REDTEAM_LIMITS.points) errors.push(`${r.id}: ${r.points.length} points > ${REDTEAM_LIMITS.points}`);
  if (errors.length) return { layout: null, errors };
  const nav = buildNav(layout);
  const segs = wallSegments(layout);
  for (const r of layout.routes) {
    const p0 = r.points[0];
    const heading = yawOf(sub(r.points[1], p0));
    const bad = FormationShapeRules.Slots('diamond', 4, p0, heading, config).slice(1)
      .filter((s) => pointInWall(s.pos, segs, AGENT_RADIUS) || segmentBlocked(p0, s.pos, segs, 0));
    if (bad.length) errors.push(`${r.id}: the squad starts round points[0] in a diamond (5.6 m back, 2.6 m aside) - slot ${bad.map((s) => s.role).join(', ')} is in / behind a wall; start in the open`);
    else if (dist(p0, r.points[r.points.length - 1]) < 1 && r.points.length < 3) errors.push(`${r.id}: the route does not go anywhere`);
    else {
      const t = simulate({ layout, route: r, memberCount: 4, config, seed: 1, nav, onFrame: () => {} });
      if (t >= REDTEAM_LIMITS.maxSimTime) errors.push(`${r.id}: the leader does not reach the end in the sim (${Math.round(t)} s) - unreachable or far too long`);
    }
  }
  return errors.length ? { layout: null, errors } : { layout, errors: [] };
}

// ---------------------------------------------------------------------------------------------------------------------
// The prompt

const SYSTEM = [
  'You are the red team for the squad formation rules of a tactical game (Operation: Cold Silence). A deterministic 2D simulator',
  'walks a squad (2-4 men) along routes in small layouts you design; a bot scores fixed checks. Your job: design layouts that make',
  'the CURRENT rules fail a check - real rule weaknesses the user would see in the game, not start-position artifacts.',
  'Units cm, deg, s. UE axes: X forward, Y RIGHT (left = -Y), yaw = atan2(dy, dx) clockwise from above; clock sectors relative to the',
  "leader's heading (12 = heading, 3 = right, 9 = left).",
  'RULES (intent): 4 men = diamond (K leader 0,0; P 2.6 m right 2.8 m back; L 2.6 m left 2.8 m back; Z 5.6 m straight back, walks',
  'backwards looking at 6); 3 = wedge (K P L, the 5-7 o\'clock gap kept by design); 2 = pair (partner behind the right shoulder).',
  'Patrol (walk) = 80 % speed, P scans 12->3, L 9->12; run = all face 12. Terrain picks the formation automatically: open -> diamond;',
  'a wall on one side (within 4 m) -> echelon away from it (wall on the left -> echelon_right); any passage between two walls -> column',
  '(narrow < 6 m: P forward over the shoulder, L back at a slant, Z back); arrival at a wall straight ahead -> the wall layout (all',
  'along the wall facing out). Switching is held by the leader\'s PATH: tighter at once, looser only after 5 m with it. The squad',
  'NEVER splits round a wall: a formation a wall cuts (checked at the leader and 5 m ahead) folds to the mirror echelon, else a trail',
  'column along the leader\'s own path; before a turn > 25 deg within 8 m an echelon / line folds into the column. Passing a',
  'free-standing wall: the shortest side, or with a known threat the side the wall hides it from if <= 30 % longer. Slicing the pie:',
  'passing a free wall end beside him, the man nearest looks round it (3 m before to 2 m after). Nobody looks into a wall closer',
  'than 2.6 m (any formation, manual too). On every formation change the men take the new places by minimal total walk.',
  'Manual hold-G menu items (each walked too): line, diamond, echelon_right, blizzard, column, hedgehog, echelon_left, all_forward.',
  'CHECKS (a failure = a run where): 1 a member faces a wall < 200 cm within 45 deg for >= 0.3 s (not looking where he walks);',
  "2 with 4 men nobody watches 5-7 o'clock for >= 10 % of patrol; 3 > 1 unexpected formation change per 100 m (a change undone",
  'within 10 m, or against the zone expectation); 4 in a zone with an expectation the requested formation matches < 80 % of settled',
  'time; 5 a slot inside a wall / unwalkable, or two men < min spacing for 0.5 s; 6 the rear man > 150 cm behind his slot (p95,',
  "walk, 4 men); 7 a wall between the leader and a member's slot / position, or between chain neighbours, for >= 0.3 s; 8 an",
  'expect_bypass wall passed on the wrong side; 9 passing a free wall end nobody looked round it. Manual items are gated only by 1, 5, 7.',
].join('\n');

const SCHEMA = [
  'LAYOUT JSON (schema codex.formation_layout/1; extra keys are ignored):',
  '{"bounds":{"min":[x,y],"max":[x,y]},"walls":[{"id":"W1","a":[x,y],"b":[x,y],"height":300,"thickness":40}],',
  ' "zones":[{"id":"Z1","kind":"open|corridor|wall_run|gap_run|corner|wall_end","poly":[[x,y],[x,y],[x,y],...]}],',
  ' "routes":[{"id":"R1","pace":"walk|run","points":[[x,y],...],',
  '   "expect":[{"zone":"Z1","formation":"diamond|wedge|column|line|echelon_left|echelon_right|wall|blizzard"}],',
  '   "threats":[{"pos":[x,y],"since_s":0}], "expect_bypass":[{"wall":"W1","wall_on":"left|right"}]}]}',
  `Limits: bounds span <= ${REDTEAM_LIMITS.span} cm, <= ${REDTEAM_LIMITS.walls} walls (straight segments; a gap = two segments; a corner = two`,
  `segments sharing an end), 1-${REDTEAM_LIMITS.routes} routes of 2-${REDTEAM_LIMITS.points} points, thickness 20-60, height 300. Every route point >= 80 cm from`,
  'any wall body and inside bounds; the squad STARTS in a diamond round points[0] (5.6 m behind, 2.6 m aside, opposite the first',
  'leg) - keep that area open. Routes must be walkable (agent radius 35 cm). zones / expect / threats / expect_bypass are optional:',
  'add an expect only where the rules above clearly define the formation (a wrong expectation is your error, not a rule bug);',
  'expect_bypass only on a wall the route leg runs straight into. Prefer pace walk (most checks are patrol checks).',
].join('\n');

export function redteamPrompt(p: RedteamPayload, effort: ModelRequest['effort'] = 'medium'): ModelRequest {
  const fam = p.families.map((f) => `- ${f.id}: ${f.doc} [tried ${f.tried}, broke ${f.broke}${f.checks.length ? `, checks ${f.checks.join(',')}` : ''}]`);
  const prev = p.previous.map((x) => `- ${x.id} ${x.family}: ${x.valid ? (x.failures.length ? x.failures.join('; ') : 'no failure') : `INVALID (${x.note})`}`);
  const lines = [
    SCHEMA, '',
    `Red-team round ${p.round}. Attack families (cover the least tried ones; push the ones that broke something further):`, ...fam, '',
    p.found.length ? `Distinct failures already found (check choice members) - aim at NEW ones: ${p.found.join(', ')}` : 'No failures found yet.',
    ...(prev.length ? ['Previous round (distilled):', ...prev] : []),
    ...(p.replyError ? ['', `Your previous reply could not be used: ${p.replyError}`] : []),
  ];
  if (p.fix) {
    lines.push('', `FIX these ${p.fix.length} invalid layouts of this round (keep the attack idea, fix the errors). Same order:`);
    for (const f of p.fix) lines.push(`- #${f.index + 1} ${f.family}: ${f.errors.slice(0, 4).join(' | ')}`, `  ${JSON.stringify(f.layout)}`);
  } else lines.push('', `Design ${p.count} NEW small layouts, each aimed at specific checks, different families.`);
  lines.push(
    'Answer with strict JSON only, no prose: {"layouts":[{"family":"<family id>","target_checks":[7],"intent":"<one sentence: what should break and why>","layout":{...}}]}',
  );
  return {
    purpose: 'redteam', model: MODELS.redteam, maxTokens: 2000 + 2200 * (p.fix ? p.fix.length : p.count), payload: p, system: SYSTEM,
    prompt: lines.join('\n'), effort, timeoutMs: 600_000,
  };
}

export interface DesignedLayout { family: string; targets: number[]; intent: string; layout: unknown }

/** Parses the model's reply: the `layouts` array (a bare array or a single layout object are accepted too). Throws on bad JSON. */
export function parseRedteam(text: string): DesignedLayout[] {
  const start = text.search(/[[{]/), end = Math.max(text.lastIndexOf('}'), text.lastIndexOf(']'));
  if (start < 0 || end <= start) throw new Error('no JSON in the model reply');
  const data = JSON.parse(text.slice(start, end + 1));
  const list: unknown[] = Array.isArray(data) ? data : Array.isArray(data?.layouts) ? data.layouts : [data];
  return list.map((x) => {
    const o = (x ?? {}) as Record<string, unknown>;
    const isWrapper = o.layout && typeof o.layout === 'object';
    return {
      family: typeof o.family === 'string' ? o.family : 'unknown',
      targets: Array.isArray(o.target_checks) ? o.target_checks.filter((v): v is number => typeof v === 'number') : [],
      intent: typeof o.intent === 'string' ? o.intent.slice(0, 300) : '',
      layout: isWrapper ? o.layout : o,
    };
  });
}

// ---------------------------------------------------------------------------------------------------------------------
// The loop

export interface RedteamOptions {
  client: ModelClient | null;
  config: FormationConfig;
  rounds: number;
  budgetUsd: number;
  /** Layouts asked for per round. */
  perRound: number;
  /** Stop after this many rounds in a row without a new distinct failure (0 = never). */
  patience: number;
  members: number[];
  seeds: number;
  seedBase: number;
  /** Menu choices walked per layout (default: auto + every item). */
  choices?: MenuChoice[];
  /** File numbering: round r is saved as `<roundBase + r>_<n>.json` (the CLI continues after the files already there). */
  roundBase: number;
  dryRun: boolean;
  /** Thinking effort of the design calls (default medium). */
  effort?: ModelRequest['effort'];
  log?: (line: string) => void;
  /** Called after every round with the result so far (the CLI saves the kept layouts and the call log: an interrupted run keeps them). */
  onRound?: (res: RedteamResult) => void;
}

export interface RedteamCall extends CallRecord { attempt: 'design' | 'retry' }

export interface KeptLayout {
  /** `<round>_<n>` (the file name without .json). */
  id: string;
  family: string;
  intent: string;
  targets: number[];
  layout: Layout;
  failures: Failure[];
}

export interface RoundRecord { round: number; tried: number; valid: number; invalid: number; fixed: number; failing: number; newDistinct: number; runs: number; error?: string }

export interface RedteamResult {
  rounds: RoundRecord[];
  calls: RedteamCall[];
  spendUsd: number;
  stopReason: string;
  kept: KeptLayout[];
  /** Distinct (check, choice, members) failures with their first example. */
  distinct: { key: string; failure: Failure; id: string }[];
  families: Record<string, { tried: number; broke: number; checks: number[] }>;
  dryRequest?: { model: string; system: string; prompt: string; estInputTokens: number };
}

export async function redteam(o: RedteamOptions): Promise<RedteamResult> {
  const log = o.log ?? (() => {});
  const res: RedteamResult = { rounds: [], calls: [], spendUsd: 0, stopReason: '', kept: [], distinct: [], families: {} };
  for (const f of ATTACK_FAMILIES) res.families[f.id] = { tried: 0, broke: 0, checks: [] };
  const found = new Set<string>();
  let previous: RedteamPayload['previous'] = [];

  const payload = (r: number, extra: Partial<RedteamPayload> = {}): RedteamPayload => ({
    kind: 'redteam', round: r, count: o.perRound,
    families: ATTACK_FAMILIES.map((f) => ({ ...f, ...res.families[f.id] })),
    found: [...found].slice(0, 60), previous, ...extra,
  });

  if (o.dryRun) {
    const req = redteamPrompt(payload(1), o.effort);
    res.dryRequest = { model: req.model, system: req.system, prompt: req.prompt, estInputTokens: Math.ceil((req.system.length + req.prompt.length) / 4) };
    res.stopReason = 'dry run (no API call)';
    return res;
  }
  if (!o.client) { res.stopReason = 'no model client'; return res; }

  let lastCost = 0;
  let quiet = 0;
  // One budget-capped call; null when the budget stops it or it fails (the error is logged per call).
  const call = async (r: number, attempt: 'design' | 'retry', p: RedteamPayload): Promise<{ list: DesignedLayout[] } | { error: string } | null> => {
    if (res.spendUsd >= o.budgetUsd || res.spendUsd + lastCost > o.budgetUsd) return null;
    const req = redteamPrompt(p, o.effort);
    try {
      const reply = await o.client!.complete(req);
      lastCost = Math.max(lastCost, reply.costUsd);
      res.spendUsd = round(res.spendUsd + reply.costUsd, 6);
      const rec: RedteamCall = { round: r, attempt, purpose: req.purpose, model: req.model, inputTokens: reply.usage.input_tokens, outputTokens: reply.usage.output_tokens, costUsd: reply.costUsd };
      res.calls.push(rec);
      log(`[redteam] round ${r} ${attempt}: ${req.model} ${reply.usage.input_tokens} in / ${reply.usage.output_tokens} out, $${reply.costUsd.toFixed(4)} (total $${res.spendUsd.toFixed(4)})`);
      try { return { list: parseRedteam(reply.text) }; } catch (e) {
        rec.error = `parse: ${e instanceof Error ? e.message : String(e)}`.slice(0, 200);
        return { error: rec.error };
      }
    } catch (e) {
      const error = (e instanceof Error ? e.message : String(e)).slice(0, 300);
      res.calls.push({ round: r, attempt, purpose: req.purpose, model: req.model, inputTokens: 0, outputTokens: 0, costUsd: 0, error });
      log(`[redteam] round ${r} ${attempt}: ${error}`);
      return { error };
    }
  };

  for (let r = 1; r <= o.rounds; r++) {
    const fileRound = o.roundBase + r;
    let got = await call(r, 'design', payload(r));
    if (!got) { res.stopReason = `budget $${o.budgetUsd}: the next call (~$${lastCost.toFixed(3)}) would exceed it`; break; }
    if ('error' in got) {
      // One retry with the error fed back.
      const again = await call(r, 'retry', payload(r, { replyError: got.error }));
      if (!again) { res.stopReason = `budget $${o.budgetUsd} reached`; break; }
      got = again;
    }
    if ('error' in got) {
      res.rounds.push({ round: r, tried: 0, valid: 0, invalid: 0, fixed: 0, failing: 0, newDistinct: 0, runs: 0, error: got.error });
      o.onRound?.(res);
      if (o.patience > 0 && ++quiet >= o.patience) { res.stopReason = `no new distinct failure for ${o.patience} rounds`; break; }
      continue;
    }
    const designs = got.list.slice(0, o.perRound);
    const checked = designs.map((d, i) => ({ d, ...validateCandidate(d.layout, `redteam_${fileRound}_${i + 1}`, o.config), fixed: false }));
    const bad = checked.map((c, i) => ({ c, i })).filter(({ c }) => !c.layout);
    let fixed = 0;
    if (bad.length) {
      const fix = bad.map(({ c, i }) => ({ index: i, family: c.d.family, errors: c.errors, layout: c.d.layout }));
      const again = await call(r, 'retry', payload(r, { fix }));
      if (again && 'list' in again) {
        again.list.slice(0, bad.length).forEach((d, k) => {
          const i = bad[k].i;
          const v = validateCandidate(d.layout, `redteam_${fileRound}_${i + 1}`, o.config);
          if (v.layout) {
            checked[i] = { d: { ...d, family: d.family === 'unknown' ? checked[i].d.family : d.family, intent: d.intent || checked[i].d.intent }, ...v, fixed: true };
            fixed++;
          } else checked[i].errors = v.errors;
        });
      }
    }
    const rec: RoundRecord = { round: r, tried: designs.length, valid: 0, invalid: 0, fixed, failing: 0, newDistinct: 0, runs: 0 };
    const nextPrevious: RedteamPayload['previous'] = [];
    checked.forEach((c, i) => {
      const id = `${fileRound}_${i + 1}`;
      const family = c.d.family;
      const fam = (res.families[family] ??= { tried: 0, broke: 0, checks: [] });
      fam.tried++;
      if (!c.layout) {
        rec.invalid++;
        nextPrevious.push({ id, family, valid: false, note: c.errors.slice(0, 2).join(' | ').slice(0, 200), failures: [] });
        log(`[redteam] ${id} ${family}: invalid - ${c.errors[0]}`);
        return;
      }
      rec.valid++;
      const ev = evaluateLayout(c.layout, { config: o.config, members: o.members, seeds: o.seeds, seedBase: o.seedBase, choices: o.choices });
      rec.runs += ev.runs;
      const fresh = ev.failures.filter((f) => !found.has(failureKey(f)));
      for (const f of fresh) { found.add(failureKey(f)); res.distinct.push({ key: failureKey(f), failure: f, id }); }
      rec.newDistinct += fresh.length;
      if (ev.failures.length) {
        rec.failing++;
        fam.broke++;
        for (const f of ev.failures) if (!fam.checks.includes(f.check)) fam.checks.push(f.check);
        fam.checks.sort((a, b) => a - b);
        res.kept.push({ id, family, intent: c.d.intent, targets: c.d.targets, layout: c.layout, failures: ev.failures });
      }
      nextPrevious.push({
        id, family, valid: true, note: '',
        failures: ev.failures.slice(0, 6).map((f) => `${failureKey(f)} ${f.route}${f.t !== null ? ` t=${f.t}` : ''}: ${f.what}`.slice(0, 140)),
      });
      log(`[redteam] ${id} ${family}: ${ev.runs} runs, ${ev.failures.length} distinct failures (${fresh.length} new)`);
    });
    previous = nextPrevious;
    res.rounds.push(rec);
    o.onRound?.(res);
    if (rec.newDistinct === 0) quiet++; else quiet = 0;
    if (o.patience > 0 && quiet >= o.patience) { res.stopReason = `no new distinct failure for ${o.patience} rounds`; break; }
    if (r === o.rounds) res.stopReason = `${o.rounds} rounds done`;
  }
  return res;
}

// ---------------------------------------------------------------------------------------------------------------------
// Output: the kept regression layouts and the report

/** The regression file for a kept layout: the layout + `comment` + `redteam` meta + `expected_fail` (minimal reproductions). */
export function keptFile(k: KeptLayout, meta: { client: string; configHash: string; members: number[]; seeds: number; seed: number; date: string }): Record<string, unknown> {
  const kinds = [...new Set(k.failures.map((f) => f.check))].map((c) => `#${c} ${CHECK_SHORT[c]}`).join(', ');
  const first = k.failures[0];
  const intent = (k.intent || 'no intent given').trim().replace(/([^.!?])$/, '$1.');
  const comment = `Red team ${k.id} (${k.family}${k.targets.length ? `, aimed at #${k.targets.join(',#')}` : ''}): ${intent} ` +
    `Broke ${k.failures.length} (check, choice, members): ${kinds}. First: ${failureKey(first)} ${first.route} s${first.seed}${first.t !== null ? ` t=${first.t}s` : ''} - ${first.what}.`;
  // The layout's own comment (if any) gives way to ours.
  const { schema, name, comment: _own, ...rest } = k.layout as unknown as Record<string, unknown>;
  void _own;
  return {
    schema, name, comment, ...rest,
    redteam: { id: k.id, family: k.family, intent: k.intent, target_checks: k.targets, client: meta.client, config: meta.configHash, members: meta.members, seeds: meta.seeds, seed: meta.seed, date: meta.date },
    expected_fail: k.failures.map((f) => ({ ...f, reason: `known red-team failure (${looksLike(f.check)}): open until the rules are fixed or the layout is judged wrong` })),
  };
}

/** redteam_report.md (<= 30 lines): the only red-team output a coding agent reads. */
export function renderRedteamReport(res: RedteamResult, h: { client: string; budgetUsd: number; date: string; members: number[]; seeds: number; dir: string }): string {
  const tried = res.rounds.reduce((a, r) => a + r.tried, 0), valid = res.rounds.reduce((a, r) => a + r.valid, 0);
  const invalid = res.rounds.reduce((a, r) => a + r.invalid, 0), fixed = res.rounds.reduce((a, r) => a + r.fixed, 0);
  const runs = res.rounds.reduce((a, r) => a + r.runs, 0);
  const retries = res.calls.filter((c) => c.attempt === 'retry').length, errors = res.calls.filter((c) => c.error).length;
  const byCheck = new Map<number, { n: number; ex: { key: string; failure: Failure; id: string } }>();
  for (const d of res.distinct) {
    const cur = byCheck.get(d.failure.check);
    if (cur) cur.n++; else byCheck.set(d.failure.check, { n: 1, ex: d });
  }
  const rows = [...byCheck.entries()].sort((a, b) => b[1].n - a[1].n).slice(0, 9).map(([c, { n, ex }]) => {
    const f = ex.failure;
    return `| ${c} ${CHECK_SHORT[c]} | ${n} | ${looksLike(c)} | ${ex.id}.json ${f.route} ${f.choice} n${f.members} s${f.seed}${f.t !== null ? ` t=${f.t}s` : ''}: ${f.what.slice(0, 70)} |`;
  });
  const fam = Object.entries(res.families).filter(([, v]) => v.tried).map(([k, v]) => `${k} ${v.broke}/${v.tried}`).join(', ');
  const lines = [
    '# Formation red team report',
    `client ${h.client} | rounds ${res.rounds.length} | calls ${res.calls.length} (${retries} retries, ${errors} errors) | spend $${res.spendUsd.toFixed(4)} of $${h.budgetUsd} | ${h.date}`,
    `stop: ${res.stopReason}`,
    ...(res.dryRequest ? [`dry run: first request ${res.dryRequest.model}, ~${res.dryRequest.estInputTokens} input tokens (redteam_dry_request.txt)`] : []),
    `layouts: tried ${tried} | valid ${valid} (${fixed} after the retry) | invalid ${invalid} (discarded) | failing ${res.kept.length} -> ${h.dir}`,
    `runs ${runs} (auto + 8 manual x members ${h.members.join(',')} x seeds ${h.seeds}) | distinct failures (check, choice, members): ${res.distinct.length}`,
    `per round new distinct: ${res.rounds.map((r) => r.newDistinct).join(' ') || '-'}`,
    '',
    '| check | distinct | looks like | example (file route choice members seed time: what) |',
    '|---|---|---|---|',
    ...(rows.length ? rows : ['| - | 0 | - | no failure found |']),
    '',
    `families (broke / tried): ${fam || '-'}`,
    "rule bug? = a geometry promise the rules make unconditionally (1, 2, 5, 6, 7); design question = hinges on the model's expectations / thresholds (3, 4, 8, 9).",
    'regression: npm run bot:movement -- --layouts redteam (known failures are expected_fail; not gating npm test). Calls: redteam_calls.jsonl.',
  ];
  return lines.slice(0, 30).join('\n') + '\n';
}

// ---------------------------------------------------------------------------------------------------------------------
// Regression over the kept layouts

export interface ExpectedFail extends Failure { reason: string }

export interface RegressionRow {
  file: string;
  /** Still failing / now passing (the rules fixed it) / failing but not expected. */
  known: number;
  fixed: string[];
  fresh: Failure[];
  runs: number;
  /** The current minimal failures (all keys). */
  current: Failure[];
  /** The checks of the expected entries now passing / still failing (the report's per-check summary). */
  fixedChecks: number[];
  knownChecks: number[];
}

/** Re-walks a kept layout (full matrix) and compares with its expected_fail: known / fixed (now passing) / new. */
export function regressLayout(file: string, data: Record<string, unknown>, o: EvalOptions): RegressionRow {
  const v = validateCandidate(data, String(data.name ?? file), o.config);
  if (!v.layout) throw new Error(`${file}: ${v.errors.join('; ')}`);
  const expected = (Array.isArray(data.expected_fail) ? data.expected_fail : []) as ExpectedFail[];
  const ev = evaluateLayout(v.layout, o);
  const now = new Set(ev.failures.map(failureKey));
  const want = new Set(expected.map(failureKey));
  const still = expected.filter((e) => now.has(failureKey(e))), gone = expected.filter((e) => !now.has(failureKey(e)));
  return {
    file, runs: ev.runs, current: ev.failures,
    known: still.length, knownChecks: still.map((e) => e.check),
    fixed: gone.map(failureKey), fixedChecks: gone.map((e) => e.check),
    fresh: ev.failures.filter((f) => !want.has(failureKey(f))),
  };
}

/**
 * The kept file refreshed against the current rules (`tag`, e.g. "v2.2"): expected_fail = the still-failing entries with their
 * current minimal reproduction + the failures that appeared with these rules (`since: tag`); the entries now passing move to
 * `fixed_entries` (with `fixed_in`); a layout with nothing left failing stays as a regression guard with `fixed_in: tag`.
 */
export function refreshKept(data: Record<string, unknown>, row: RegressionRow, tag: string): Record<string, unknown> {
  const old = (Array.isArray(data.expected_fail) ? data.expected_fail : []) as ExpectedFail[];
  const fixedEntries = [
    ...((Array.isArray(data.fixed_entries) ? data.fixed_entries : []) as Record<string, unknown>[]),
    ...old.filter((e) => row.fixed.includes(failureKey(e))).map(({ check, choice, members, route, seed }) => ({ check, choice, members, route, seed, fixed_in: tag })),
  ];
  // v2.3: a still-failing entry keeps its `since` (which rules introduced it) and its `triage` note (layout artefact / design
  // question) across refreshes - the v2.3 refresh dropped the v2.2 tags of the regressions still open.
  const oldByKey = new Map(old.map((e) => [failureKey(e), e as ExpectedFail & { since?: string; triage?: string }]));
  const expected = row.current.map((f) => {
    const prev = oldByKey.get(failureKey(f));
    if (!prev) return { ...f, since: tag, reason: `appeared with ${tag} (${looksLike(f.check)}): a new failure on this layout - review` };
    return { ...f, ...(prev.since ? { since: prev.since } : {}), ...(prev.triage ? { triage: prev.triage } : {}),
      reason: `still failing under ${tag} (${looksLike(f.check)}): open until the rules are fixed or the layout is judged wrong` };
  });
  const out: Record<string, unknown> = { ...data, expected_fail: expected };
  if (fixedEntries.length) out.fixed_entries = fixedEntries;
  if (expected.length) delete out.fixed_in; else out.fixed_in = tag;
  return out;
}

export function renderRegression(rows: RegressionRow[], h: { dir: string; configHash: string; date: string }): string {
  const known = rows.reduce((a, r) => a + r.known, 0), fixed = rows.reduce((a, r) => a + r.fixed.length, 0), fresh = rows.reduce((a, r) => a + r.fresh.length, 0);
  const lines = [
    `# Red-team regression (${h.dir})`,
    `layouts ${rows.length} | runs ${rows.reduce((a, r) => a + r.runs, 0)} | config ${h.configHash} | ${h.date}`,
    `expected_fail still failing ${known} | fixed (clear their expected_fail) ${fixed} | NEW failures ${fresh}`,
    '',
    '| check | still failing | fixed | new |',
    '|---|---|---|---|',
    ...[1, 2, 3, 4, 5, 6, 7, 8, 9].map((c) => {
      const n = (xs: number[]) => xs.filter((x) => x === c).length;
      return { c, k: rows.reduce((a, r) => a + n(r.knownChecks), 0), f: rows.reduce((a, r) => a + n(r.fixedChecks), 0), w: rows.reduce((a, r) => a + n(r.fresh.map((x) => x.check)), 0) };
    }).filter((x) => x.k || x.f || x.w).map((x) => `| ${x.c} ${CHECK_SHORT[x.c]} | ${x.k} | ${x.f} | ${x.w} |`),
    '',
    ...rows.filter((r) => r.fixed.length || r.fresh.length).slice(0, 12).map((r) =>
      `- ${r.file}: ${r.fixed.length ? `fixed ${r.fixed.slice(0, 4).join(', ')}` : ''}${r.fixed.length && r.fresh.length ? '; ' : ''}${r.fresh.length ? `new ${r.fresh.slice(0, 3).map((f) => `${failureKey(f)} ${f.route}${f.t !== null ? ` t=${f.t}` : ''}`).join(', ')}` : ''}`),
  ];
  return lines.slice(0, 30).join('\n') + '\n';
}
