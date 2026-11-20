// Training Ground / level generator: carrying derived files (props, tactical map) when a building moves on the plan (task 092).
// Docs: docs/port/gemini/inbox/092-tg-move-building-carries-props.md; T-022 persistence rules; TACTICAL_MAP.md.
// Pure functions: input is never mutated.

import type { Vec2 } from '../movement/geometry.ts';
import { buildingOfLayer } from './mapEdit.ts';
import { inFootprint } from './rotation.ts';
import type { GenBuilding } from './schema.ts';
import type { LevelPropsFile, PropObject } from './props/schema.ts';
import type { TacticalDropSpot, TacticalHidingSpot, TacticalLadderRoof, TacticalMapFile, TacticalOverwatchRoof, TacticalWindow, Vec3 } from './tacticalMap.ts';

const clone = <T>(v: T): T => JSON.parse(JSON.stringify(v)) as T;
const r0 = (v: number): number => { const x = Math.round(v); return Object.is(x, -0) ? 0 : x; };

/** Re-measures straight cables of generators to their turrets if any of them moved. */
function refreshCables(file: LevelPropsFile, movedIds: ReadonlySet<string>): void {
  const byId = new Map<string, PropObject>();
  for (const l of file.layers) for (const o of l.objects) byId.set(o.id, o);
  for (const g of byId.values()) {
    if (!g.links || g.links.length === 0) continue;
    if (!movedIds.has(g.id) && !g.links.some((id) => movedIds.has(id))) continue;
    g.cable_cm = g.links.map((id) => {
      const t = byId.get(id);
      return t ? Math.round(Math.hypot(g.pos[0] - t.pos[0], g.pos[1] - t.pos[1])) : 0;
    });
  }
}

/** Whether a prop object stands on or inside the given building before it was moved. */
export function isPropOnBuilding(o: PropObject, buildingBefore: GenBuilding): boolean {
  if (buildingOfLayer(o.walk_layer) === buildingBefore.id) return true;
  return inFootprint(buildingBefore, o.pos, 0);
}

/**
 * Returns a new props file where every prop standing on or inside `buildingBefore`'s footprint (any storey, roof)
 * or attached to a moved prop moves by `delta` (UE cm; [dx, dy]).
 * Props outside the building are byte-identical.
 * Locked props inside the building move along and retain their locked status.
 * Hand overrides of moved props follow.
 * Generator cables are re-measured.
 * Input `props` is NEVER mutated.
 */
export function carryPropsWithBuilding(
  props: LevelPropsFile,
  buildingBefore: GenBuilding,
  delta: Vec2,
): LevelPropsFile {
  const d: Vec2 = [Math.round(delta[0]), Math.round(delta[1])];
  if (d[0] === 0 && d[1] === 0) return clone(props);

  const next = clone(props);
  const targetIds = new Set<string>();

  // Pass 1: find props standing on / inside the building footprint or roof/floor layers
  for (const layer of next.layers) {
    for (const obj of layer.objects) {
      if (isPropOnBuilding(obj, buildingBefore)) {
        targetIds.add(obj.id);
      }
    }
  }

  // Pass 2: cascade to attached props (e.g. mine or loot attached to a moved container/crate)
  let added = true;
  while (added) {
    added = false;
    for (const layer of next.layers) {
      for (const obj of layer.objects) {
        if (obj.attach && targetIds.has(obj.attach) && !targetIds.has(obj.id)) {
          targetIds.add(obj.id);
          added = true;
        }
      }
    }
  }

  if (targetIds.size === 0) return next;

  // Move all identified props
  for (const layer of next.layers) {
    for (const obj of layer.objects) {
      if (targetIds.has(obj.id)) {
        obj.pos = [r0(obj.pos[0] + d[0]), r0(obj.pos[1] + d[1])];
      }
    }
  }

  // Hand overrides follow moved props
  if (next.overrides) {
    for (const op of next.overrides) {
      if (op.op === 'move' && targetIds.has(op.id)) {
        op.pos = [r0(op.pos[0] + d[0]), r0(op.pos[1] + d[1])];
      }
    }
  }

  refreshCables(next, targetIds);
  return next;
}

/**
 * Returns a new tactical map where every tactical spot, overwatch spot, firing gap (window),
 * drop spot, and ladder-only roof belonging to `buildingId` moves by `delta` (UE cm; [dx, dy]).
 * Spots belonging to other buildings or terrain are byte-identical.
 * Input `tactical` is NEVER mutated.
 */
export function carryTacticalWithBuilding(
  tactical: TacticalMapFile,
  buildingId: string,
  delta: Vec2,
): TacticalMapFile {
  const d: Vec2 = [Math.round(delta[0]), Math.round(delta[1])];
  if (d[0] === 0 && d[1] === 0) return clone(tactical);

  const next = clone(tactical);
  const shift3 = (p: Vec3): Vec3 => [r0(p[0] + d[0]), r0(p[1] + d[1]), p[2]];

  // Windows / firing gaps
  next.windows = next.windows.map((w: TacticalWindow) => {
    if (w.building !== buildingId) return w;
    return {
      ...w,
      pos: shift3(w.pos),
      stand: shift3(w.stand),
    };
  });

  // Overwatch roofs & spots
  next.overwatch_roofs = next.overwatch_roofs.map((r: TacticalOverwatchRoof) => {
    if (r.building !== buildingId) return r;
    return {
      ...r,
      spots: r.spots.map((s) => ({
        ...s,
        pos: shift3(s.pos),
      })),
    };
  });

  // Ladder-only roofs
  next.ladder_only_roofs = next.ladder_only_roofs.map((r: TacticalLadderRoof) => {
    if (r.building !== buildingId) return r;
    return {
      ...r,
      center: shift3(r.center),
      poly: r.poly.map((p) => [r0(p[0] + d[0]), r0(p[1] + d[1])] as Vec2),
      ladder_tops: r.ladder_tops.map((t) => shift3(t)),
    };
  });

  // Drop spots (jumpers from the roof of this building)
  next.drop_spots = next.drop_spots.map((ds: TacticalDropSpot) => {
    const isRoofOfBuilding = ds.surface === `roof:${buildingId}` ||
      ds.layer === `roof:${buildingId}` ||
      buildingOfLayer(ds.surface) === buildingId ||
      buildingOfLayer(ds.layer) === buildingId;
    if (!isRoofOfBuilding) return ds;
    return {
      ...ds,
      edge: shift3(ds.edge),
      land: shift3(ds.land),
    };
  });

  // Hiding spots
  next.hiding_spots = next.hiding_spots.map((hs: TacticalHidingSpot) => {
    const isOfBuilding = (hs as { building?: string }).building === buildingId ||
      buildingOfLayer(hs.layer) === buildingId ||
      hs.cover.startsWith(`wall:${buildingId}_`) ||
      hs.cover.startsWith(`wall:${buildingId}:`);
    if (!isOfBuilding) return hs;
    return {
      ...hs,
      pos: shift3(hs.pos),
    };
  });

  return next;
}
