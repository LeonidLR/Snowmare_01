// Training Ground / level generator v3 - STRUCTURE CLASSES and WINDOW VAULT DATA of a schema /2 layout (lead addendum 2026-11-21, designer:
// data only - nothing in the game reads it yet; the window-climb animations come next, destruction later). Two pure passes over a finalized layout:
//  - annotateStructure: every wall segment (layout walls[]), partition and opening gets `structure` (load_bearing | partition | annex | facade) and
//    `material` (concrete_panel | brick | plaster | wood | sheet_metal) by the archetype rules of Content/Data/LevelGen/structure_classes.json:
//    the wall's role is 'perimeter' (a building's outer wall), 'core' (a wall of a stair core room, a lane wall), 'cross' (between two flats or a
//    flat and a common room) or 'inner'; a wall of no building by its kind (free_walls); an opening takes its host wall's class.
//  - annotateWindowVault: every window gets `sill_cm`, `width_cm`, `vault_onto` (ground | balcony | roof | none) and `vaultable`: width >= the
//    minimum, the sill at one of our vault / mantle heights (obstacle_heights.json classes in building_plans.json constants.window_vault) and
//    something to land on outside - the ground on storey 0, a balcony at the window, or a roof flush with the storey; an upper window over nothing
//    is not vaultable (a 3 m fall).
// Rooms are found in the building frame (toBuildingFrame), so a rotated building is classified like an axis-aligned one. Deterministic, no draws.
import structureCfg from '../../../../Content/Data/LevelGen/structure_classes.json' with { type: 'json' };
import plansCfg from '../../../../Content/Data/LevelGen/building_plans.json' with { type: 'json' };
import { baySectionTemplate, BAY_SECTIONS } from './buildings/kitPlan.ts';
import type { Vec2 } from '../movement/geometry.ts';
import { classifyObstacleHeight } from './obstacleHeights.ts';
import type { ObstacleHeightClass } from './obstacleHeights.ts';
import { toBuildingFrame } from './rotation.ts';
import type { GenBuilding, GenLayout, GenRoom, WallOpening } from './schema.ts';
import { LEVEL_HEIGHT, OPENING_DEFAULTS } from './schema.ts';

export type StructureClass = 'load_bearing' | 'partition' | 'annex' | 'facade';
export type WallMaterial = 'concrete_panel' | 'brick' | 'plaster' | 'wood' | 'sheet_metal';
export type WallRole = 'perimeter' | 'core' | 'cross' | 'inner';
export const STRUCTURE_CLASSES = structureCfg;
const RULES = structureCfg.rules as unknown as Record<string, Record<WallRole, [string, string]>>;
const FREE = structureCfg.free_walls as unknown as Record<string, [string, string]>;
const VAULT = plansCfg.constants.window_vault;
function fixedBayWallRole(b: GenBuilding, thickness: number | undefined): 'inner' | 'cross' | undefined {
  if (!b.plan?.template || !baySectionTemplate(b.plan.template) || thickness === undefined) return undefined;
  return thickness === BAY_SECTIONS.walls_cm.partition ? 'inner' : 'cross';
}

/** The class / material of a wall of a building of `archetype` in `role`. */
export function structureOf(archetype: string | undefined, role: WallRole): { structure: StructureClass; material: WallMaterial } {
  const [structure, material] = (RULES[archetype ?? 'default'] ?? RULES.default)[role];
  return { structure: structure as StructureClass, material: material as WallMaterial };
}

const inBounds = (r: GenRoom, p: Vec2): boolean => p[0] >= r.bounds.min[0] && p[0] <= r.bounds.max[0] && p[1] >= r.bounds.min[1] && p[1] <= r.bounds.max[1];

/**
 * The role of an interior wall a..b (world) on `floor` of `b`: a lane wall or a wall of a stair core room is 'core'; a wall between two flats or a
 * flat and a common room is 'cross'; everything else 'inner'. The rooms are probed 20 cm to each side of the wall's middle (building frame).
 */
export function interiorRole(b: GenBuilding, id: string, a: Vec2, e: Vec2, floor: number): WallRole {
  if (/_lanewall_f\d+$/.test(id)) return 'core';
  const A = toBuildingFrame(b, a), E = toBuildingFrame(b, e);
  const L = Math.hypot(E[0] - A[0], E[1] - A[1]) || 1, n: Vec2 = [-(E[1] - A[1]) / L, (E[0] - A[0]) / L], m: Vec2 = [(A[0] + E[0]) / 2, (A[1] + E[1]) / 2];
  const rooms = (b.rooms ?? []).filter((r) => r.floor === floor);
  const side = (k: number): GenRoom | undefined => rooms.find((r) => inBounds(r, [m[0] + n[0] * 20 * k, m[1] + n[1] * 20 * k]));
  const s1 = side(1), s2 = side(-1);
  if ([s1, s2].some((r) => r && (r.kind === 'stairwell' || r.use === 'landing'))) return 'core';
  if (s1 && s2 && (s1.unit !== s2.unit) && (s1.unit !== undefined || s2.unit !== undefined) && s1.unit !== 'suite' && s2.unit !== 'suite') return 'cross';
  return 'inner';
}

/** Writes structure / material onto every wall, partition and opening of the layout (mutates; returns it). */
export function annotateStructure(l: GenLayout): GenLayout {
  const byId = new Map(l.buildings.map((b) => [b.id, b]));
  for (const w of l.walls) {
    const b = w.building ? byId.get(w.building) : undefined;
    let cls;
    if (!b) { const [structure, material] = FREE[w.kind ?? 'default'] ?? FREE.default; cls = { structure: structure as StructureClass, material: material as WallMaterial }; }
    else if (w.kind === 'interior' || w.kind === 'partition') cls = structureOf(b.archetype, fixedBayWallRole(b, w.thickness) ?? interiorRole(b, w.id, w.a, w.b, w.floor ?? 0));
    else cls = structureOf(b.archetype, 'perimeter');
    w.structure = cls.structure;
    w.material = cls.material;
  }
  for (const b of l.buildings) {
    const outer = structureOf(b.archetype, 'perimeter');
    for (const op of b.openings ?? []) { op.structure = outer.structure; op.material = outer.material; }
    for (const pt of b.partitions ?? []) {
      if (!pt.start || !pt.end) continue;
      const cls = structureOf(b.archetype, fixedBayWallRole(b, pt.thickness) ?? interiorRole(b, pt.id, pt.start, pt.end, pt.floor));
      pt.structure = cls.structure;
      pt.material = cls.material;
      for (const op of pt.openings ?? []) { op.structure = cls.structure; op.material = cls.material; }
    }
  }
  return l;
}

/** The outward unit normal (building frame) of the footprint edge nearest to `p`. */
function outwardOf(b: GenBuilding, p: Vec2): Vec2 {
  const [[x0, y0], [x1, y1]] = b.footprint;
  const c: [number, Vec2][] = [[Math.abs(p[0] - x0), [-1, 0]], [Math.abs(p[0] - x1), [1, 0]], [Math.abs(p[1] - y0), [0, -1]], [Math.abs(p[1] - y1), [0, 1]]];
  return c.reduce((best, x) => (x[0] < best[0] ? x : best))[1];
}

/** What a body climbing out of window `op` of `b` lands on (see the header). */
export function vaultOnto(l: GenLayout, b: GenBuilding, op: WallOpening): 'ground' | 'balcony' | 'roof' | 'none' {
  if ((op.floor ?? 0) === 0) return 'ground';
  const p = toBuildingFrame(b, op.pos), n = outwardOf(b, p), half = (op.width ?? OPENING_DEFAULTS.window.width) / 2;
  const out: Vec2 = [p[0] + n[0] * VAULT.probe_out_cm, p[1] + n[1] * VAULT.probe_out_cm];
  for (const bal of b.balconies ?? []) {
    if (bal.floor !== op.floor) continue;
    const [[ax, ay], [bx, by]] = bal.rect;
    const x0 = Math.min(ax, bx) - half, x1 = Math.max(ax, bx) + half, y0 = Math.min(ay, by) - half, y1 = Math.max(ay, by) + half;
    if (out[0] >= x0 && out[0] <= x1 && out[1] >= y0 && out[1] <= y1) return 'balcony';
  }
  // another building's roof flush with the window's storey (a step at most): the probe point in its footprint, world frame
  const world = toBuildingFrame({ footprint: b.footprint, rotation: -(b.rotation ?? 0) }, out); // building frame -> world (the inverse turn)
  const z = (op.floor ?? 0) * LEVEL_HEIGHT;
  for (const o of l.buildings) {
    if (o.id === b.id || !o.roof_walkable) continue;
    const q = toBuildingFrame(o, world);
    const [[ox0, oy0], [ox1, oy1]] = o.footprint;
    if (q[0] >= ox0 && q[0] <= ox1 && q[1] >= oy0 && q[1] <= oy1 && Math.abs(o.floors * LEVEL_HEIGHT - z) <= VAULT.roof_step_cm) return 'roof';
  }
  return 'none';
}

/** Is a sill of `sill` cm one of our vault / mantle heights (building_plans.json constants.window_vault.sill_classes)? */
export function sillVaultable(sill: number): boolean {
  const cls = classifyObstacleHeight(sill);
  return !!cls && (VAULT.sill_classes as string[]).includes(cls as ObstacleHeightClass);
}

/** Writes sill_cm / width_cm / vault_onto / vaultable onto every window of the layout's buildings (mutates; returns it). */
export function annotateWindowVault(l: GenLayout): GenLayout {
  for (const b of l.buildings) {
    for (const op of [...(b.openings ?? []), ...(b.partitions ?? []).flatMap((pt) => pt.openings ?? [])]) {
      if (op.kind !== 'window') continue;
      const sill = op.sill_height ?? OPENING_DEFAULTS.window.sill, width = op.width ?? OPENING_DEFAULTS.window.width;
      const onto = vaultOnto(l, b, op);
      op.sill_cm = sill;
      op.width_cm = width;
      op.vault_onto = onto;
      op.vaultable = width >= VAULT.min_width_cm && sillVaultable(sill) && onto !== 'none';
    }
  }
  return l;
}
