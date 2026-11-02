// Training Ground / combat simulator: a BATCH of seeds over one layout -> the `codex.combat_sim/1` document (schema.ts). Pure (the CLI and
// the panel's worker both call it); the world and the squad track are built once and shared by every seed.

import type { FormationConfig } from '../movement/config.ts';
import type { GenLayout } from '../levelgen/schema.ts';
import type { PressureConfig, SimCheats, SimConstants } from './config.ts';
import { DEFAULT_SIM_CONSTANTS, parsePressureConfig, withBotRoute } from './config.ts';
import { renderReport } from './report.ts';
import type { CombatSimBatch } from './schema.ts';
import { COMBAT_SIM_SCHEMA, aggregateRuns, heatMap, runRow, summariseSpots } from './schema.ts';
import type { RunResult } from './sim.ts';
import { runCombatSim } from './sim.ts';
import type { SquadTrack } from './squadTrack.ts';
import { buildSquadTrack } from './squadTrack.ts';
import { SimWorld } from './world.ts';

export interface BatchOptions {
  layout: GenLayout;
  /** The level JSON "pressure" block (absent = the game's defaults with the pressure ON). */
  pressure?: unknown;
  /** The level JSON "bot_route" block (its fight_cover tuning), optional. */
  botRoute?: unknown;
  formationConfig: FormationConfig;
  seeds: number[];
  durationSec?: number;
  members?: number;
  constants?: Partial<SimConstants>;
  cheats?: Partial<SimCheats>;
  /** A cached track for this layout / member count (buildSquadTrack otherwise). */
  track?: SquadTrack;
  layoutFile?: string;
  maxEvents?: number;
  maxContacts?: number;
  onProgress?: (done: number, total: number) => void;
}

export interface BatchResult { batch: CombatSimBatch; results: RunResult[]; world: SimWorld; track: SquadTrack; config: PressureConfig }

/** The pressure config a batch runs with: the block as given, the pressure forced ON (a layout without one = the defaults). */
export function batchPressureConfig(pressure: unknown): PressureConfig {
  const block = pressure && typeof pressure === 'object' ? { enabled: true, ...(pressure as Record<string, unknown>) } : { enabled: true };
  return parsePressureConfig(block);
}

export function runBatch(o: BatchOptions): BatchResult {
  const t0 = Date.now();
  const config = batchPressureConfig(o.pressure);
  const world = new SimWorld(o.layout, config);
  const members = o.members ?? o.track?.members ?? 3;
  const track = o.track && o.track.members === members && o.track.layout === o.layout.name ? o.track : buildSquadTrack(world, o.formationConfig, { members });
  const duration = o.durationSec ?? 600;
  const constants: SimConstants = withBotRoute({ ...DEFAULT_SIM_CONSTANTS, ...(o.constants ?? {}) }, o.botRoute);
  const results: RunResult[] = [];
  o.seeds.forEach((seed, i) => {
    results.push(runCombatSim({ world, track, seed, durationSec: duration, constants, cheats: o.cheats }));
    o.onProgress?.(i + 1, o.seeds.length);
  });
  const aggregate = aggregateRuns(results);
  const cheats: SimCheats = { immortal: true, infiniteAmmo: true, ...(o.cheats ?? {}) };
  const maxEvents = o.maxEvents ?? 400, maxContacts = o.maxContacts ?? 400;
  const events: CombatSimBatch['events'] = [];
  const contacts: CombatSimBatch['contacts'] = [];
  for (const r of results) {
    for (const e of r.events) if (events.length < maxEvents) events.push({ ...e, seed: r.metrics.seed });
    for (const c of r.contacts) if (contacts.length < maxContacts) contacts.push({ ...c, seed: r.metrics.seed });
  }
  const body: Omit<CombatSimBatch, 'report'> = {
    schema: COMBAT_SIM_SCHEMA,
    created_utc: new Date().toISOString(),
    layout: { name: o.layout.name, file: o.layoutFile, seed: o.layout.seed, profile: o.layout.profile, members, track_seconds: track.lapSeconds, track_meters: track.lapMeters },
    pressure: o.pressure ?? null,
    constants: constants as unknown as Record<string, unknown>,
    cheats: { immortal: cheats.immortal, infinite_ammo: cheats.infiniteAmmo },
    duration_sec: duration,
    seeds: [...o.seeds],
    wall_ms: Date.now() - t0,
    aggregate,
    runs: results.map(runRow),
    spots: summariseSpots(results),
    coordinated: results.flatMap((r) => r.coordinated).slice(0, 400),
    events,
    heat: heatMap(results, o.layout.bounds, (p) => world.zoneAt(p)),
    contacts,
  };
  return { batch: { ...body, report: renderReport(body) }, results, world, track, config };
}

/** 1..n */
export const seedRange = (n: number, first = 1): number[] => Array.from({ length: Math.max(0, n) }, (_, i) => first + i);
