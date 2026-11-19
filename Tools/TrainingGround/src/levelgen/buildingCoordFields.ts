// The one table of a building's coordinate-bearing fields (S2_ROOMS_PLAN.md R4, section 7 item 5): which GenBuilding paths carry
// points / rects / directions / yaws and in which frame ('building' = its own unrotated frame, 'world' = layout coordinates).
// Data: Content/Data/LevelGen/building_coord_fields.json (also read by Scripts/Editor/build_levelgen_map.py _building_local).
// mapBuildingCoords drives the per-field transforms of finalizeDraftToLayout (generate.ts) and rotateContent (rotation.ts).
// Pure TS.
import table from '../../../../Content/Data/LevelGen/building_coord_fields.json' with { type: 'json' };
import type { Vec2 } from '../movement/geometry.ts';

export type CoordKind = 'point' | 'point_list' | 'rect' | 'bounds' | 'direction' | 'yaw';
export type CoordFrame = 'building' | 'world';

/** One row of the table: a path into GenBuilding ("partitions[].openings[].pos"), its kind and frame. */
export interface CoordField { path: string; kind: CoordKind; frame: CoordFrame }

const KINDS: readonly CoordKind[] = ['point', 'point_list', 'rect', 'bounds', 'direction', 'yaw'];
const FRAMES: readonly CoordFrame[] = ['building', 'world'];

function checked(rows: readonly { path: string; kind: string; frame: string }[]): CoordField[] {
  for (const r of rows) {
    if (!KINDS.includes(r.kind as CoordKind)) throw new Error(`building_coord_fields.json: ${r.path}: unknown kind '${r.kind}'`);
    if (!FRAMES.includes(r.frame as CoordFrame)) throw new Error(`building_coord_fields.json: ${r.path}: unknown frame '${r.frame}'`);
  }
  return rows as CoordField[];
}

/** The table (building_coord_fields.json `fields`). */
export const BUILDING_COORD_FIELDS: readonly CoordField[] = checked(table.fields);
/** Number pairs on a building that are sizes, not positions (the coverage walker skips them). */
export const BUILDING_NON_COORD_PATHS: readonly string[] = table.not_coordinates.map((x) => x.path);

/** Per-kind maps of one transform; a kind / frame the transform cannot express throws (rotateContent: no world rect). */
export interface CoordMaps {
  point(p: Vec2, frame: CoordFrame, path: string): Vec2;
  /** [min, max] corners in, [min, max] corners out (normalise when the map can flip an axis). */
  rect(r: [Vec2, Vec2], frame: CoordFrame, path: string): [Vec2, Vec2];
  direction(v: Vec2, frame: CoordFrame, path: string): Vec2;
  yaw(deg: number, frame: CoordFrame, path: string): number;
}

type Obj = Record<string, unknown>;

/** Apply `fn` to the value at `keys` under `node` (arrays at '[]' keys mapped element-wise); absent values are skipped. In place. */
function visit(node: Obj, keys: readonly string[], fn: (v: unknown) => unknown): void {
  const [head, ...rest] = keys;
  const many = head.endsWith('[]'), key = many ? head.slice(0, -2) : head;
  const v = node[key];
  if (v === undefined || v === null) return;
  if (!rest.length) { node[key] = fn(v); return; }
  if (many) { for (const el of v as Obj[]) visit(el, rest, fn); } else visit(v as Obj, rest, fn);
}

/** The value under `fn` for one table row (yaw: non-numbers kept, like an opening without a yaw). */
function mapValue(f: CoordField, m: CoordMaps): (v: unknown) => unknown {
  switch (f.kind) {
    case 'point': return (v) => m.point(v as Vec2, f.frame, f.path);
    case 'point_list': return (v) => (v as Vec2[]).map((p) => m.point(p, f.frame, f.path));
    case 'rect': return (v) => m.rect(v as [Vec2, Vec2], f.frame, f.path);
    case 'bounds': return (v) => { const b = v as { min: Vec2; max: Vec2 }; const r = m.rect([b.min, b.max], f.frame, f.path); return { ...b, min: r[0], max: r[1] }; };
    case 'direction': return (v) => m.direction(v as Vec2, f.frame, f.path);
    case 'yaw': return (v) => (typeof v === 'number' ? m.yaw(v, f.frame, f.path) : v);
  }
}

/** A tree copy of plain data: every array / object copied, shared sub-objects become separate copies (each mapped once). */
function copyTree(v: unknown): unknown {
  if (Array.isArray(v)) return v.map(copyTree);
  if (v && typeof v === 'object') { const o: Obj = {}; for (const [k, x] of Object.entries(v)) o[k] = copyTree(x); return o; }
  return v;
}

/**
 * A deep copy of the building with every table field mapped by `m` (key order kept: fields are replaced in place). Non-coordinate
 * data is copied unchanged; a sub-object shared by two paths (a room in both `rooms` and `floors_detail[].rooms`) is copied per path.
 */
export function mapBuildingCoords<T extends object>(b: T, m: CoordMaps, fields: readonly CoordField[] = BUILDING_COORD_FIELDS): T {
  const out = copyTree(b) as Obj;
  for (const f of fields) visit(out, f.path.split('.'), mapValue(f, m));
  return out as unknown as T;
}

/** A transform's refusal of a kind / frame pair it has no rule for (the table grew a field the transform must learn). */
export function unsupportedCoord(path: string, kind: CoordKind, frame: CoordFrame): never {
  throw new Error(`building coordinate ${path}: no rule for ${kind} in the ${frame} frame`);
}
