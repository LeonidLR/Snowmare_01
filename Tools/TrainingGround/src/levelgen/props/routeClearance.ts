// Training Ground / level generator: the mandatory-route clearance of generated props (task 088, PROPS_LAYERS.md +
// 088-props-route-clearance.md). Pure (no fs): autogen.ts runs it on a fresh file, the browser's re-roll on the re-rolled layers.
// Never place an autogen mine or tripwire within route_clear_m (config props_autogen.json; 6 m = the 4.5 m spot range + 1.5 m
// Medic-Sapper bonus, TripwireRules / DeployableRules) and keep a free corridor of route_corridor_m for crates / barrels.
// Reuses 087 helpers: mandatorySegments and routeDistOnLayer. The locked pmc_charges layer is never touched (listed in the stats
// when a charge lands inside). A mine / loot attached to a dropped crate goes with it.

import type { GenLayout } from '../schema.ts';
import type { LevelPropsFile, PropObject } from './schema.ts';
import { mandatorySegments, routeDistOnLayer, type RouteSeg } from '../pmcPosts.ts';
import { clearanceCm, type PropsAutogenConfig } from './autogenConfig.ts';

export interface RouteClearanceStats {
  droppedCrates: number;
  droppedBarrels: number;
  droppedMines: number;
  droppedLoot: number;
  droppedTotal: number;
  pmcChargesInside: string[];
}

export interface RouteClearanceOptions {
  /** Only these layer ids are cleaned (a re-roll of some layers); absent = every unlocked layer (the autogen). */
  only?: ReadonlySet<string>;
  /** Hand-locked objects (`locked: true`) stay even inside the clearance (the designer's pins); the autogen drops them like any other. */
  keepLockedObjects?: boolean;
}

/** Drops the objects of `file`'s unlocked layers that violate the route clearance; mutates `file`, returns what it dropped. */
export function applyRouteClearance(
  file: LevelPropsFile,
  layout: GenLayout,
  cfg: PropsAutogenConfig,
  opts: RouteClearanceOptions = {},
): RouteClearanceStats {
  const segs: RouteSeg[] = mandatorySegments(layout);
  const droppedCrateIds = new Set<string>();
  const stats: RouteClearanceStats = { droppedCrates: 0, droppedBarrels: 0, droppedMines: 0, droppedLoot: 0, droppedTotal: 0, pmcChargesInside: [] };
  if (segs.length === 0) return stats;

  const routeClearCm = clearanceCm(cfg, 'mines', 'route_clear_m');
  const crateCorridorCm = clearanceCm(cfg, 'crates', 'route_corridor_m');
  const barrelCorridorCm = clearanceCm(cfg, 'barrels', 'route_corridor_m');
  const inScope = (id: string) => !opts.only || opts.only.has(id);
  const pinned = (o: PropObject) => opts.keepLockedObjects === true && o.locked === true;

  for (const l of file.layers) {
    if (!inScope(l.id)) continue;
    if (l.kind === 'crates' && !l.locked) {
      const kept: PropObject[] = [];
      for (const obj of l.objects) {
        const d = routeDistOnLayer(obj.pos, segs, obj.walk_layer || 'ground');
        if (d < crateCorridorCm && !pinned(obj)) {
          droppedCrateIds.add(obj.id);
          stats.droppedCrates++;
        } else {
          kept.push(obj);
        }
      }
      l.objects = kept;
    } else if (l.kind === 'barrels' && !l.locked) {
      const kept: PropObject[] = [];
      for (const obj of l.objects) {
        const d = routeDistOnLayer(obj.pos, segs, obj.walk_layer || 'ground');
        if (d < barrelCorridorCm && !pinned(obj)) {
          stats.droppedBarrels++;
        } else {
          kept.push(obj);
        }
      }
      l.objects = kept;
    }
  }

  for (const l of file.layers) {
    if (l.id === 'pmc_charges') {
      for (const obj of l.objects) {
        const d = routeDistOnLayer(obj.pos, segs, obj.walk_layer || 'ground');
        if (d < routeClearCm) stats.pmcChargesInside.push(`${obj.id} (${(d / 100).toFixed(2)}m)`);
      }
    } else if (!inScope(l.id)) {
      continue;
    } else if (l.kind === 'mines' && !l.locked) {
      const kept: PropObject[] = [];
      for (const obj of l.objects) {
        if (obj.attach && droppedCrateIds.has(obj.attach)) {
          stats.droppedMines++;
          continue;
        }
        const d = routeDistOnLayer(obj.pos, segs, obj.walk_layer || 'ground');
        if (d < routeClearCm && !pinned(obj)) {
          stats.droppedMines++;
        } else {
          kept.push(obj);
        }
      }
      l.objects = kept;
    } else if (l.kind === 'loot' && !l.locked) {
      const kept: PropObject[] = [];
      for (const obj of l.objects) {
        if (obj.attach && droppedCrateIds.has(obj.attach)) {
          stats.droppedLoot++;
          continue;
        }
        kept.push(obj);
      }
      l.objects = kept;
    }
  }

  stats.droppedTotal = stats.droppedCrates + stats.droppedBarrels + stats.droppedMines + stats.droppedLoot;
  return stats;
}
