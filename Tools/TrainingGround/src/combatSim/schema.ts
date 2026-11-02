// Training Ground / combat simulator: the batch output `codex.combat_sim/1` (docs/design/levelgen/COMBAT_SIM.md "Schema") - what the CLI
// writes to <out>/combatsim.json and the Training Ground panel reads. Metric names follow the Unreal combat-lab report (src/combatLab/
// report.ts Aggregate) wherever the two measure the same thing, so a batch can be compared with an Unreal batch (calibrate.ts). The
// aggregate is per RUN (a 10-minute run by default) - multiply by the runs to compare with an Unreal batch total. Pure.

import type { Vec2 } from '../movement/geometry.ts';
import type { ContactKind, ContactTrigger, CoordinatedAttempt, EventDiagnostic, RunMetrics, RunResult, SimContact, SpotDiagnostic, SpotRejectReason } from './sim.ts';
import type { Party } from './rules.ts';

export const COMBAT_SIM_SCHEMA = 'codex.combat_sim/1';

/** Means per run (and the totals) over the batch's seeds. */
export interface CombatSimAggregate {
  runs: number;
  /** Game minutes per run and in all. */
  minutesPerRun: number;
  minutes: number;
  /** Per-run means. */
  contacts: number;
  byKind: Record<ContactKind, number>;
  byTrigger: Record<ContactTrigger, number>;
  unwatchedStrikes: number;
  neverLooked: number;
  /** Over every contact somebody looked at (s). */
  reactionMean: number;
  /** Seconds to the first sight of an enemy, mean over the runs that had one (-1 = none). */
  firstSightMeanSec: number;
  fights: number;
  fightSeconds: number;
  fightSecondsPerFight: number;
  formationChanges: number;
  wallHugs: number;
  coverOrders: number;
  coverTaken: number;
  coverSeconds: number;
  knockdowns: number;
  damage: number;
  absorbed: number;
  rounds: number;
  enemiesSpawned: number;
  enemiesKilled: number;
  routeLaps: number;
  routeProgress: number;
  distanceM: number;
  underFireSeconds: number;
  unwatchedHits: number;
  pressure: RunMetrics['pressure'];
  /** Events by what came of them (totals over the batch). */
  eventOutcomes: Record<EventDiagnostic['outcome'], number>;
  /** Coordinated attempts by outcome, the missing parties tallied (totals). */
  coordinatedOutcomes: Record<CoordinatedAttempt['outcome'], number>;
  coordinatedMissing: Record<Party, number>;
  /** Spot rejection reasons tallied over every ambush event's look at every spot (totals). */
  spotRejections: Record<SpotRejectReason, number>;
  /** Contacts per run as a distribution: how many runs had 0, 1, 2, ... contacts. */
  contactsHistogram: number[];
  ambushHistogram: number[];
}

export interface SpotSummary extends SpotDiagnostic {
  /** Across the batch: chosen in this many runs. */
  runs_chosen: number;
  /** The main reason it was rejected (the most frequent), or 'never_in_band'. */
  main_reason: SpotRejectReason | 'never_in_band' | 'chosen' | 'no_wall';
}

export interface HeatCell { i: number; j: number; count: number; kinds: Record<ContactKind, number> }

export interface CombatSimHeat {
  /** Cell size, cm, over the layout bounds (the cell (i, j) covers [min + i * cell, min + (i + 1) * cell)). */
  cell_cm: number;
  origin: Vec2;
  cells: HeatCell[];
  /** Contacts per layout zone id ("" = outside every zone). */
  zones: Record<string, number>;
}

export interface CombatSimRunRow {
  seed: number;
  contacts: number;
  patrol: number;
  ambush: number;
  coordinated: number;
  sprung: number;
  unwatched: number;
  fights: number;
  fight_seconds: number;
  knockdowns: number;
  absorbed: number;
  rounds: number;
  laps: number;
  progress: number;
  events: number;
}

export interface CombatSimBatch {
  schema: typeof COMBAT_SIM_SCHEMA;
  created_utc: string;
  /** What was simulated. */
  layout: { name: string; file?: string; seed?: number; profile?: string; members: number; track_seconds: number; track_meters: number };
  pressure: unknown;
  constants: Record<string, unknown>;
  cheats: { immortal: boolean; infinite_ammo: boolean };
  duration_sec: number;
  seeds: number[];
  /** The wall time of the batch, ms. */
  wall_ms: number;
  aggregate: CombatSimAggregate;
  runs: CombatSimRunRow[];
  /** Diagnostics (the important part): per hiding spot, the coordinated attempts, the events, the heat map. */
  spots: SpotSummary[];
  coordinated: CoordinatedAttempt[];
  /** Every event of every run (seed-tagged), capped at max_events (the first runs first). */
  events: (EventDiagnostic & { seed: number })[];
  heat: CombatSimHeat;
  /** The first runs' contacts (seed-tagged) for the panel's map, capped. */
  contacts: (SimContact & { seed: number })[];
  report: string[];
}

const sum = (xs: number[]) => xs.reduce((a, b) => a + b, 0);
const r2 = (v: number) => Math.round(v * 100) / 100;
const mean = (xs: number[]) => (xs.length ? r2(sum(xs) / xs.length) : 0);

export const SPOT_REASONS: SpotRejectReason[] = ['too_close', 'too_far', 'near_squad', 'wall_clearance', 'out_of_bounds', 'off_nav', 'not_concealed', 'unreachable', 'behind'];
const EVENT_OUTCOMES: EventDiagnostic['outcome'][] = ['patrol', 'ambush', 'coordinated', 'ambush_fallback_patrol', 'coordinated_fallback_ambush', 'coordinated_fallback_patrol', 'no_spot', 'no_room'];
const COORD_OUTCOMES: CoordinatedAttempt['outcome'][] = ['ok', 'no_room', 'no_layout', 'unreachable'];
const PARTIES: Party[] = ['flank_left', 'flank_right', 'front', 'rear', 'single'];

export function aggregateRuns(results: RunResult[]): CombatSimAggregate {
  const ms = results.map((r) => r.metrics);
  const n = Math.max(1, ms.length);
  const perRun = (f: (m: RunMetrics) => number) => r2(sum(ms.map(f)) / n);
  const contacts = results.flatMap((r) => r.contacts);
  const reactions = contacts.filter((c) => c.reaction_sec >= 0).map((c) => c.reaction_sec);
  const firstSights = ms.map((m) => m.first_sight_sec).filter((v) => v >= 0);
  const hist = (f: (m: RunMetrics) => number): number[] => { const out: number[] = []; for (const m of ms) { const k = f(m); while (out.length <= k) out.push(0); out[k]++; } return out; };
  const eventOutcomes = Object.fromEntries(EVENT_OUTCOMES.map((k) => [k, 0])) as Record<EventDiagnostic['outcome'], number>;
  for (const r of results) for (const e of r.events) eventOutcomes[e.outcome]++;
  const coordinatedOutcomes = Object.fromEntries(COORD_OUTCOMES.map((k) => [k, 0])) as Record<CoordinatedAttempt['outcome'], number>;
  const coordinatedMissing = Object.fromEntries(PARTIES.map((k) => [k, 0])) as Record<Party, number>;
  for (const r of results) for (const c of r.coordinated) { coordinatedOutcomes[c.outcome]++; for (const p of c.missing) coordinatedMissing[p]++; }
  const spotRejections = Object.fromEntries(SPOT_REASONS.map((k) => [k, 0])) as Record<SpotRejectReason, number>;
  for (const r of results) for (const s of r.spots) for (const [k, v] of Object.entries(s.rejected)) spotRejections[k as SpotRejectReason] += v ?? 0;
  const pressureKeys = Object.keys(ms[0]?.pressure ?? { events: 0, events_delayed: 0, sprung_by_watch: 0, sprung_by_hold: 0, relocations: 0, runs_spotted: 0, no_route: 0, followers: 0, recycled: 0, lost: 0 }) as (keyof RunMetrics['pressure'])[];
  const pressure = Object.fromEntries(pressureKeys.map((k) => [k, perRun((m) => m.pressure[k])])) as RunMetrics['pressure'];
  const fights = perRun((m) => m.fights), fightSeconds = perRun((m) => m.fight_seconds);
  return {
    runs: ms.length,
    minutesPerRun: r2((ms[0]?.duration_sec ?? 0) / 60),
    minutes: r2(sum(ms.map((m) => m.duration_sec)) / 60),
    contacts: perRun((m) => m.contacts),
    byKind: { patrol: perRun((m) => m.by_kind.patrol), ambush: perRun((m) => m.by_kind.ambush), coordinated: perRun((m) => m.by_kind.coordinated) },
    byTrigger: { seen: perRun((m) => m.by_trigger.seen), engaged: perRun((m) => m.by_trigger.engaged), sprung: perRun((m) => m.by_trigger.sprung) },
    unwatchedStrikes: perRun((m) => m.unwatched_strikes),
    neverLooked: perRun((m) => m.never_looked),
    reactionMean: mean(reactions),
    firstSightMeanSec: firstSights.length ? mean(firstSights) : -1,
    fights, fightSeconds, fightSecondsPerFight: fights > 0 ? r2(fightSeconds / fights) : 0,
    formationChanges: perRun((m) => m.formation_changes),
    wallHugs: perRun((m) => m.wall_hugs),
    coverOrders: perRun((m) => m.cover_orders),
    coverTaken: perRun((m) => m.cover_taken),
    coverSeconds: perRun((m) => m.cover_seconds),
    knockdowns: perRun((m) => m.knockdowns),
    damage: perRun((m) => m.damage_taken),
    absorbed: perRun((m) => m.damage_absorbed),
    rounds: perRun((m) => m.rounds),
    enemiesSpawned: perRun((m) => m.enemies_spawned),
    enemiesKilled: perRun((m) => m.enemies_killed),
    routeLaps: perRun((m) => m.route.laps),
    routeProgress: perRun((m) => m.route.progress),
    distanceM: perRun((m) => m.route.distance_m),
    underFireSeconds: perRun((m) => m.under_fire_seconds),
    unwatchedHits: perRun((m) => m.unwatched_hits),
    pressure,
    eventOutcomes,
    coordinatedOutcomes,
    coordinatedMissing,
    spotRejections,
    contactsHistogram: hist((m) => m.contacts),
    ambushHistogram: hist((m) => m.by_kind.ambush + m.by_kind.coordinated),
  };
}

/** The per-spot diagnostics merged over the runs (the spots are the same for every seed of a layout). */
export function summariseSpots(results: RunResult[]): SpotSummary[] {
  if (!results.length) return [];
  const n = results[0].spots.length;
  const out: SpotSummary[] = [];
  for (let i = 0; i < n; i++) {
    const base = results[0].spots[i];
    const s: SpotSummary = { ...base, rejected: {}, considered: 0, chosen: 0, sprung_by_watch: 0, sprung_by_hold: 0, relocated: 0, never_struck: 0, runs_chosen: 0, main_reason: 'never_in_band' };
    for (const r of results) {
      const d = r.spots[i];
      s.considered += d.considered; s.chosen += d.chosen; s.sprung_by_watch += d.sprung_by_watch; s.sprung_by_hold += d.sprung_by_hold; s.relocated += d.relocated; s.never_struck += d.never_struck;
      if (d.chosen > 0) s.runs_chosen++;
      for (const [k, v] of Object.entries(d.rejected)) s.rejected[k as SpotRejectReason] = (s.rejected[k as SpotRejectReason] ?? 0) + (v ?? 0);
    }
    if (s.chosen > 0) s.main_reason = 'chosen';
    else if (s.no_wall) s.main_reason = 'no_wall';
    else {
      // the most frequent reason AFTER the band (too_far / too_close alone mean the squad never came near)
      const inBand = Object.entries(s.rejected).filter(([k]) => k !== 'too_far' && k !== 'too_close') as [SpotRejectReason, number][];
      const top = (inBand.length ? inBand : (Object.entries(s.rejected) as [SpotRejectReason, number][])).sort((a, b) => b[1] - a[1])[0];
      s.main_reason = top ? top[0] : 'never_in_band';
    }
    out.push(s);
  }
  return out;
}

export function heatMap(results: RunResult[], bounds: { min: Vec2; max: Vec2 }, zoneAt: (p: Vec2) => string | null, cellCm = 2000): CombatSimHeat {
  const cells = new Map<string, HeatCell>();
  const zones: Record<string, number> = {};
  for (const r of results) for (const c of r.contacts) {
    const i = Math.floor((c.position[0] - bounds.min[0]) / cellCm), j = Math.floor((c.position[1] - bounds.min[1]) / cellCm);
    const key = `${i},${j}`;
    let cell = cells.get(key);
    if (!cell) { cell = { i, j, count: 0, kinds: { patrol: 0, ambush: 0, coordinated: 0 } }; cells.set(key, cell); }
    cell.count++; cell.kinds[c.kind]++;
    const z = c.zone ?? zoneAt(c.position) ?? '';
    zones[z] = (zones[z] ?? 0) + 1;
  }
  return { cell_cm: cellCm, origin: bounds.min, cells: [...cells.values()].sort((a, b) => b.count - a.count), zones };
}

export function runRow(r: RunResult): CombatSimRunRow {
  const m = r.metrics;
  return { seed: m.seed, contacts: m.contacts, patrol: m.by_kind.patrol, ambush: m.by_kind.ambush, coordinated: m.by_kind.coordinated, sprung: m.by_trigger.sprung, unwatched: m.unwatched_strikes, fights: m.fights, fight_seconds: m.fight_seconds, knockdowns: m.knockdowns, absorbed: m.damage_absorbed, rounds: m.rounds, laps: m.route.laps, progress: m.route.progress, events: m.pressure.events };
}

/** Problems of a batch file (empty = valid `codex.combat_sim/1`). */
export function validateCombatSimBatch(v: unknown): string[] {
  const e: string[] = [];
  if (!v || typeof v !== 'object') return ['expected a JSON object'];
  const b = v as Partial<CombatSimBatch>;
  if (b.schema !== COMBAT_SIM_SCHEMA) e.push(`schema must be "${COMBAT_SIM_SCHEMA}"`);
  if (!b.layout || typeof b.layout.name !== 'string') e.push('layout.name: expected a string');
  if (!Array.isArray(b.seeds) || !b.seeds.every((s) => Number.isInteger(s))) e.push('seeds: expected integers');
  if (typeof b.duration_sec !== 'number') e.push('duration_sec: expected a number');
  if (!b.aggregate || typeof b.aggregate.contacts !== 'number' || !b.aggregate.byKind) e.push('aggregate: expected the per-run means');
  if (!Array.isArray(b.runs)) e.push('runs: expected an array');
  if (!Array.isArray(b.spots)) e.push('spots: expected an array');
  if (!Array.isArray(b.coordinated)) e.push('coordinated: expected an array');
  if (!b.heat || !Array.isArray(b.heat.cells)) e.push('heat.cells: expected an array');
  if (!Array.isArray(b.report)) e.push('report: expected lines');
  return e;
}
