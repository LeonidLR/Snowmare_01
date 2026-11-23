// Training Ground / level generator v2 - ROTATED BUILDINGS (designer 2026-11-16, item 4: «Макс. угол поворота зданий»).
// Convention (schema GenBuilding.rotation): a building with `rotation` (degrees, UE yaw sense: +X towards +Y) stands rotated by that
// angle about the centre of its `footprint`. `footprint`, `rooms[].bounds` / `floors_detail[].rooms[].bounds` and the schema /2
// balconies, fire escapes, snow ramps and breach panels stay in the UNROTATED building frame (so rect.centre(footprint) is the world
// centre); every other point of the building (doors, openings + their yaw, partitions, parapets, hatches, stairs, roof / floor holes,
// floor gaps, mantle edges) and its walls, ladders, covers are stored in WORLD coordinates - the one list is
// building_coord_fields.json (buildingCoordFields.ts). A file without `rotation` loads as 0 (axis-aligned), byte for byte as before.
// The generator rotates a whole block TEMPLATE rigidly (rotateContent) about the template's centre, after shrinking it to fit its
// lot (fitScale); consumers that need the footprint as a shape use footprintPoly / inFootprint / footprintBBox. Constants:
// levelgen_v2.json rotation. Pure TS.
import cfg from '../../../../Content/Data/LevelGen/levelgen_v2.json' with { type: 'json' };
import type { Vec2 } from '../movement/geometry.ts';
import type { GenBuilding, GenLayout } from './schema.ts';
import { mapBuildingCoords, unsupportedCoord } from './buildingCoordFields.ts';

export const ROTATION = cfg.rotation;

const RAD = Math.PI / 180;

/** p rotated by `deg` about `pivot` (UE yaw sense). */
export function rotPoint(p: readonly number[], pivot: readonly number[], deg: number): Vec2 {
  if (!deg) return [p[0], p[1]];
  const c = Math.cos(deg * RAD), s = Math.sin(deg * RAD), dx = p[0] - pivot[0], dy = p[1] - pivot[1];
  return [pivot[0] + dx * c - dy * s, pivot[1] + dx * s + dy * c];
}
/** A direction vector rotated by `deg`. */
export const rotVec = (v: readonly number[], deg: number): Vec2 => rotPoint(v, [0, 0], deg);
/** An angle normalised to (-180, 180]. */
export const normDeg = (a: number): number => { let x = ((a + 180) % 360 + 360) % 360 - 180; if (x === -180) x = 180; return x; };

const rectCentre = (r: readonly (readonly number[])[]): Vec2 => [(r[0][0] + r[1][0]) / 2, (r[0][1] + r[1][1]) / 2];
const rectCorners = (r: readonly (readonly number[])[]): Vec2[] => [[r[0][0], r[0][1]], [r[1][0], r[0][1]], [r[1][0], r[1][1]], [r[0][0], r[1][1]]];

/** The building's rotation (0 when absent). */
export const rotationOf = (b: Pick<GenBuilding, 'rotation'>): number => (typeof b.rotation === 'number' && Number.isFinite(b.rotation) ? b.rotation : 0);

/** The footprint's four world corners (the rectangle rotated about its centre). */
export function footprintPoly(b: Pick<GenBuilding, 'footprint' | 'rotation'>): Vec2[] {
  const r = rotationOf(b), c = rectCentre(b.footprint);
  return rectCorners(b.footprint).map((p) => rotPoint(p, c, r));
}

/** A world rectangle of the building's frame (a room's bounds) as its four world corners. */
export function buildingRectPoly(b: Pick<GenBuilding, 'footprint' | 'rotation'>, r: readonly (readonly number[])[]): Vec2[] {
  const deg = rotationOf(b), c = rectCentre(b.footprint);
  return rectCorners(r).map((p) => rotPoint(p, c, deg));
}

/** The axis-aligned bounding box of the rotated footprint ([min, max]); the footprint itself when not rotated. */
export function footprintBBox(b: Pick<GenBuilding, 'footprint' | 'rotation'>): [Vec2, Vec2] {
  if (!rotationOf(b)) return [[b.footprint[0][0], b.footprint[0][1]], [b.footprint[1][0], b.footprint[1][1]]];
  const p = footprintPoly(b);
  return [[Math.min(...p.map((q) => q[0])), Math.min(...p.map((q) => q[1]))], [Math.max(...p.map((q) => q[0])), Math.max(...p.map((q) => q[1]))]];
}

/** A world point in the building's own (unrotated) frame: rect tests against `footprint` / room bounds work there. */
export const toBuildingFrame = (b: Pick<GenBuilding, 'footprint' | 'rotation'>, p: readonly number[]): Vec2 => rotPoint(p, rectCentre(b.footprint), -rotationOf(b));

/** The point lies inside the (rotated) footprint grown by `margin` cm (negative = shrunk). */
export function inFootprint(b: Pick<GenBuilding, 'footprint' | 'rotation'>, p: readonly number[], margin = 0): boolean {
  const q = toBuildingFrame(b, p), f = b.footprint;
  return q[0] >= f[0][0] - margin && q[0] <= f[1][0] + margin && q[1] >= f[0][1] - margin && q[1] <= f[1][1] + margin;
}

/** The point lies inside a rectangle of the building's frame (a room's bounds) grown by `margin`. */
export function inBuildingRect(b: Pick<GenBuilding, 'footprint' | 'rotation'>, r: readonly (readonly number[])[], p: readonly number[], margin = 0): boolean {
  const q = toBuildingFrame(b, p);
  return q[0] >= r[0][0] - margin && q[0] <= r[1][0] + margin && q[1] >= r[0][1] - margin && q[1] <= r[1][1] + margin;
}

/**
 * The largest scale k (<= 1) at which a w x h rectangle rotated by `deg` fits inside the unrotated w x h rectangle (same centre):
 * the rotated box's bounding box is (w|cos| + h|sin|) x (w|sin| + h|cos|).
 */
export function fitScale(w: number, h: number, deg: number): number {
  const c = Math.abs(Math.cos(deg * RAD)), s = Math.abs(Math.sin(deg * RAD));
  return Math.min(1, w / (w * c + h * s), h / (w * s + h * c));
}

/**
 * The seeded angle of one template: uniform in +-maxDeg * scale; below rotation.min_deg -> 0. Consumes one draw of `random` only
 * when maxDeg * scale > 0 (an axis-aligned run uses no extra random numbers: the old maps stay byte-identical).
 */
export function drawAngle(random: () => number, maxDeg: number, scale = 1): number {
  const m = Math.max(0, Math.min(ROTATION.max_deg_limit, maxDeg)) * Math.max(0, Math.min(1, scale));
  if (m <= 0) return 0;
  const a = (random() * 2 - 1) * m;
  return Math.abs(a) < ROTATION.min_deg ? 0 : Math.round(a * 10) / 10;
}

/** The objects a block template produces (blockTemplates.ts TemplateContent), as far as rotation is concerned. */
export type RotatableContent = Pick<GenLayout, 'walls' | 'buildings' | 'ladders' | 'covers' | 'zones' | 'ambush_points'> & { mantle_edges: NonNullable<GenLayout['mantle_edges']> };

/**
 * The content rotated rigidly by `deg` about `pivot` (UE cm): every point moves, yaws turn, and each building's fields follow
 * BUILDING_COORD_FIELDS (building_coord_fields.json): world points / yaws / directions turn about `pivot`, building-frame fields
 * (footprint, room bounds, balconies, fire escapes, snow ramps, breach panels) move with the footprint centre, and `rotation` grows by
 * `deg` (see the convention above). A copy; 0 = the same objects.
 */
export function rotateContent<T extends RotatableContent>(content: T, pivot: Vec2, deg: number): T {
  if (!deg) return content;
  const P = (p: readonly number[]): Vec2 => { const q = rotPoint(p, pivot, deg); return [round2(q[0]), round2(q[1])]; };
  const Y = (y: number) => round2(normDeg(y + deg));
  const buildings = content.buildings.map((b) => {
    const c0 = rectCentre(b.footprint), c1 = P(c0), d: Vec2 = [c1[0] - c0[0], c1[1] - c0[1]];
    const T = (p: readonly number[]): Vec2 => [round2(p[0] + d[0]), round2(p[1] + d[1])];
    const nb = mapBuildingCoords(b, {
      point: (p, frame) => (frame === 'world' ? P(p) : T(p)),
      rect: (r, frame, path) => (frame === 'building' ? [T(r[0]), T(r[1])] : unsupportedCoord(path, 'rect', frame)),
      direction: (v, frame, path) => (frame === 'world' ? rotVec(v, deg) : unsupportedCoord(path, 'direction', frame)),
      yaw: (y, frame, path) => (frame === 'world' ? Y(y) : unsupportedCoord(path, 'yaw', frame)),
    });
    nb.rotation = round2(normDeg(rotationOf(b) + deg));
    return nb;
  });
  return {
    ...content,
    walls: content.walls.map((w) => ({ ...w, a: P(w.a), b: P(w.b) })),
    buildings,
    ladders: content.ladders.map((x) => ({ ...x, pos: P(x.pos), foot: P(x.foot), top: P(x.top), yaw: Y(x.yaw) })),
    covers: content.covers.map((c) => ({ ...c, pos: P(c.pos), yaw: Y(c.yaw) })),
    zones: content.zones.map((z) => ({ ...z, poly: z.poly.map(P) })),
    ambush_points: content.ambush_points.map((a) => ({ ...a, pos: P(a.pos) })),
    mantle_edges: content.mantle_edges.map((m) => ({ ...m, start: P(m.start), end: P(m.end), normal: rotVec(m.normal, deg) })),
  };
}

const round2 = (v: number): number => { const r = Math.round(v * 100) / 100; return Object.is(r, -0) ? 0 : r; };
