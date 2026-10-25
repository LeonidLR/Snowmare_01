// Training Ground / movement: the model client seam (FORMATION_CONTRACT.md 4 advice, 6 tuner). The bot and the tuner talk to a
// ModelClient; this file has the prompts, the mock (no key, no network, deterministic) and the hook for the real clients.
// The real Claude API / Jev clients are wired by the coordinator's local agent on the user's PC (the keys live there):
// register them with registerRealClient(); the key is read from the environment and never printed.

import type { ConfigParam } from './config.ts';
import type { SeriesSummary } from './checks.ts';
import { rng } from './geometry.ts';

export type Purpose = 'advice' | 'propose';

export interface ModelRequest {
  purpose: Purpose;
  model: string;
  system: string;
  prompt: string;
  maxTokens: number;
  /** The structured content of the prompt (the mock reads this instead of parsing text). */
  payload: AdvicePayload | ProposePayload;
}

export interface ModelReply {
  text: string;
  usage: { input_tokens: number; output_tokens: number };
  /** Spend of this call in USD as computed by the client from the usage (0 for the mock). */
  costUsd: number;
}

export interface ModelClient {
  name: string;
  complete(req: ModelRequest): Promise<ModelReply>;
}

export interface AdvicePayload {
  kind: 'advice';
  layout: string;
  summary: SeriesSummary;
  params: Record<string, number>;
}

export interface ProposePayload {
  kind: 'propose';
  layout: string;
  round: number;
  best: { score: number; summary: SeriesSummary; params: Record<string, number> };
  history: { round: number; score: number; changed: Record<string, number> }[];
  tunable: Record<string, Pick<ConfigParam, 'value' | 'min' | 'max' | 'unit' | 'doc'>>;
}

/** Models per contract 6: proposals on the strong model, advice / summaries on the cheaper one. */
export const MODELS = { propose: 'claude-opus-5-5', advice: 'claude-sonnet-5-5' } as const;

/** Parameters the tuner may not touch (owned by the game, not by the formation logic). */
// bypass.max_detour: the user's 30 % (decision 2026-10-25) - not a tuning lever.
export const FIXED_PARAMS = ['sight.fov', 'pace.walk_speed', 'pace.run_speed', 'bypass.max_detour'];

const SYSTEM = [
  'You tune the squad formation logic of a tactical game (Operation: Cold Silence).',
  'A deterministic simulator walked the squad along test routes and scored nine checks:',
  '1 wall stare, 2 rear cover with 4 men, 3 formation flicker, 4 expected formation per zone, 5 slots / spacing, 6 rear man pace,',
  '7 squad split by a wall, 8 passing a wall on the side hidden from a known threat, 9 the corner man looking round a wall end.',
  'Design rules you must keep: diamond (4) / wedge (3); the side men scan 12->3 and 9->12 while walking; the rear man walks',
  'backwards looking at 6; nobody looks into a wall; a wall on one side -> echelon to the open side; a narrow passage -> column;',
  'the squad never splits round a wall (a cut formation folds to the mirror echelon or the column on the leader\'s path).',
].join(' ');

export function advicePrompt(p: AdvicePayload): ModelRequest {
  return {
    purpose: 'advice', model: MODELS.advice, maxTokens: 600, payload: p, system: SYSTEM,
    prompt: [
      `Layout ${p.layout}. Series summary (JSON):`,
      JSON.stringify(p.summary),
      'Current parameters (JSON):',
      JSON.stringify(p.params),
      'Give at most 10 short lines of tuning advice: which parameter, which direction, why (cite the check).',
      'Plain lines, no preamble.',
    ].join('\n'),
  };
}

export function proposePrompt(p: ProposePayload): ModelRequest {
  return {
    purpose: 'propose', model: MODELS.propose, maxTokens: 800, payload: p, system: SYSTEM,
    prompt: [
      `Layout ${p.layout}, tuning round ${p.round}. Best so far: score ${p.best.score} (1 = all checks green).`,
      'Best series summary (JSON):', JSON.stringify(p.best.summary),
      'Best parameters (JSON):', JSON.stringify(p.best.params),
      'Previous rounds (changed params and the score they got):', JSON.stringify(p.history),
      'Tunable parameters with ranges (JSON):', JSON.stringify(p.tunable),
      'Propose the next parameter set. Change at most 4 parameters, stay inside min..max.',
      'Answer with strict JSON only: {"params": {"<key>": <number>, ...}, "reason": "<one sentence>"}',
    ].join('\n'),
  };
}

/** Parses a propose reply: the params object (unknown / fixed keys dropped). Throws on malformed JSON. */
export function parsePropose(text: string, tunable: Record<string, unknown>): { params: Record<string, number>; reason: string } {
  const start = text.indexOf('{'), end = text.lastIndexOf('}');
  if (start < 0 || end <= start) throw new Error('no JSON object in the model reply');
  const data = JSON.parse(text.slice(start, end + 1));
  const params: Record<string, number> = {};
  for (const [k, v] of Object.entries(data.params ?? {})) {
    if (k in tunable && typeof v === 'number' && Number.isFinite(v)) params[k] = v;
  }
  return { params, reason: typeof data.reason === 'string' ? data.reason : '' };
}

/** Deterministic stand-in: heuristics on the failing checks + a seeded nudge; no key, no network, cost 0. */
export function mockClient(seed = 1): ModelClient {
  const random = rng(seed);
  return {
    name: 'mock',
    async complete(req: ModelRequest): Promise<ModelReply> {
      const usage = { input_tokens: Math.ceil((req.system.length + req.prompt.length) / 4), output_tokens: 0 };
      if (req.payload.kind === 'advice') {
        const lines = mockAdvice(req.payload);
        const text = lines.join('\n');
        return { text, usage: { ...usage, output_tokens: Math.ceil(text.length / 4) }, costUsd: 0 };
      }
      const p = req.payload;
      const failing = new Set(p.best.summary.checks.filter((c) => !c.info && c.fail > 0).map((c) => c.id));
      const out: Record<string, number> = {};
      // Rounds without improvement since the best -> bigger steps (the mock has no memory but the history).
      const stale = 1 + p.history.filter((h) => h.score <= p.best.score).length;
      const nudge = (key: string, delta: number) => {
        const t = p.tunable[key];
        if (t) out[key] = Math.max(t.min, Math.min(t.max, (p.best.params[key] ?? t.value) + delta * stale));
      };
      // One lever per round (so the effect of each change stays visible): the failing checks in turn.
      const levers: Record<number, () => void> = {
        6: () => { nudge('pace.patrol', -0.05); nudge('move.backward_mult', 0.05); },
        3: () => nudge('classify.hold_distance', 100),
        4: () => nudge('classify.probe_side', 50),
        1: () => nudge('wall.flank_deg', -5),
        2: () => nudge('echelon.sector_4', 8),
        5: () => nudge('slot.side_offset', 20),
      };
      const order = [6, 2, 1, 3, 4, 5].filter((id) => failing.has(id));
      if (order.length) levers[order[(p.round - 1) % order.length]]();
      else {
        // All green: explore one parameter a little (seeded).
        const keys = Object.keys(p.tunable);
        const k = keys[Math.floor(random() * keys.length)];
        const t = p.tunable[k];
        nudge(k, (random() - 0.5) * 0.1 * (t.max - t.min));
      }
      const text = JSON.stringify({ params: out, reason: `mock: failing checks ${[...failing].join(',') || 'none'}` });
      return { text, usage: { ...usage, output_tokens: Math.ceil(text.length / 4) }, costUsd: 0 };
    },
  };
}

function mockAdvice(p: AdvicePayload): string[] {
  const lines: string[] = [];
  for (const c of p.summary.checks) {
    if (c.info || c.fail === 0) continue;
    const hint: Record<number, string> = {
      1: 'wall stare: lower wall.flank_deg or echelon sectors near walls',
      2: 'rear cover: the rear man must keep 6 o\'clock; check scan.amp_side / column.side_deg',
      3: 'flicker: raise classify.hold_distance',
      4: 'zone mismatch: tune classify.probe_side / classify.side_ignore_perp_deg',
      5: 'slots: raise slot.min_spacing margins or the offsets',
      6: 'pace: lower pace.patrol or raise move.backward_mult',
    };
    lines.push(`(mock) check ${c.id} failing ${c.fail}: ${hint[c.id] ?? 'see the report'}`);
  }
  return lines.length ? lines : ['(mock) all checks green - no change suggested'];
}

/** Real clients, registered by the local wiring (Claude API / Jev). */
const realClients = new Map<string, () => ModelClient | null>();

export function registerRealClient(name: string, factory: () => ModelClient | null): void {
  realClients.set(name, factory);
}

/**
 * Resolves --client: 'mock' -> the mock; 'claude' / 'jev' -> the registered real client (null when its key is missing or it is
 * not wired yet - the caller then reports "no model advice").
 */
export function resolveClient(name: string, seed = 1): { client: ModelClient | null; note: string } {
  if (name === 'mock') return { client: mockClient(seed), note: 'mock client (no network)' };
  if (name === 'none') return { client: null, note: 'no client requested' };
  const factory = realClients.get(name);
  if (!factory) return { client: null, note: `client "${name}" is not wired in this checkout (local wiring pending)` };
  const client = factory();
  return client ? { client, note: `${name} client` } : { client: null, note: `${name}: no API key in the environment` };
}
