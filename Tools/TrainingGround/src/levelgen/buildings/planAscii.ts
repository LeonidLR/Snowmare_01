// Training Ground / level generator v3 - ASCII view of a PLANNED building storey (S2_ROOMS_PLAN.md review gate after S2.3; docs/design/levelgen/
// S2_3_PLANS.md): walls, door / window / portal glyphs and a letter per room, 50 cm per character, drawn from the generated building (rooms,
// partitions, openings in the building frame, unrotated) - exactly what the generator emitted, not a re-plan. PURE.
// Glyphs: '#' exterior wall, '+ - |' partitions, openings replace the wall over their width: E exterior door, w window, S balcony-slot window,
// U flat entry (closed), d room door, e enfilade door, P stair-pad portal (no leaf), b balcony door. Rooms: K stair core, L landing / corridor, O lobby,
// lower-case letter = a flat that has a second entry (a loop), upper-case = a dead-end flat (one entry), r room (office), s service, e end suite.
import type { Vec2 } from '../../movement/geometry.ts';
import type { BuildingUnit, GenBuilding, GenRoom, WallOpening } from '../schema.ts';

const STEP = 50;
/** Flat labels: a loop flat gets a lower-case letter, a dead-end flat an upper-case one, none of them a glyph letter (E w S U d e P b K L O r s). */
const LOOP_LETTERS = 'afghjkmnqtvxyz', DEAD_LETTERS = 'ACFGHJMNQRTVXYZ';
const GLYPH: Record<string, string> = { entry: 'E', unit_entry: 'U', room: 'd', enfilade: 'e', portal: 'P', balcony: 'b' };

/** The glyph of an opening (the ASCII view and the page's plan overlay share it, so the legend of S2_3_PLANS.md reads the same on both). */
export function openingGlyph(op: Pick<WallOpening, 'kind' | 'role' | 'slot'>): string {
  if (op.slot === 'balcony' || op.slot === 'escape') return 'S';
  if (op.kind === 'window') return 'w';
  return GLYPH[op.role ?? 'room'] ?? 'd';
}

/** The letter of the i-th flat of a storey: lower case = a flat with a second entry (a loop), upper case = a dead-end flat. */
export const flatLetter = (i: number, dead: boolean): string => (dead ? DEAD_LETTERS[i % DEAD_LETTERS.length] : LOOP_LETTERS[i % LOOP_LETTERS.length]);

/**
 * The building turned to read well: the long side across, the street (the wall of the main entry, `wall_door_300`) at the bottom, so the court /
 * core wall is on top. No main entry on a long wall: the long side across only. Coordinates start at 0.
 */
function canonical(b: GenBuilding): GenBuilding {
  const [[fx0, fy0], [fx1, fy1]] = b.footprint, f0: Vec2 = [fx0, fy0], f1: Vec2 = [fx1, fy1];
  const a: 0 | 1 = fx1 - fx0 >= fy1 - fy0 ? 0 : 1, d: 0 | 1 = a === 0 ? 1 : 0;
  const main = (b.openings ?? []).find((o) => o.module === 'wall_door_300' && o.floor === 0);
  const onLong = main && (Math.abs(main.pos[d] - f0[d]) < 2 || Math.abs(main.pos[d] - f1[d]) < 2);
  const streetMax = !!main && onLong && Math.abs(main.pos[d] - f1[d]) < Math.abs(main.pos[d] - f0[d]);
  const T2 = (p: Vec2): Vec2 => [p[a] - f0[a], !onLong ? p[d] - f0[d] : streetMax ? p[d] - f0[d] : f1[d] - p[d]];
  const op = (o: WallOpening): WallOpening => ({ ...o, pos: T2(o.pos) });
  const box = (lo: Vec2, hi: Vec2): { min: Vec2; max: Vec2 } => {
    const p = T2(lo), q = T2(hi);
    return { min: [Math.min(p[0], q[0]), Math.min(p[1], q[1])], max: [Math.max(p[0], q[0]), Math.max(p[1], q[1])] };
  };
  const fp = box(f0, f1);
  return {
    ...b,
    footprint: [fp.min, fp.max],
    openings: (b.openings ?? []).map(op),
    partitions: (b.partitions ?? []).map((pt) => ({ ...pt, start: T2(pt.start), end: T2(pt.end), openings: (pt.openings ?? []).map(op) })),
    rooms: (b.rooms ?? []).map((r) => ({ ...r, bounds: box(r.bounds.min, r.bounds.max) })),
  };
}

/** The ASCII plan of storey `floor` of a planned (axis-aligned) building, turned by `canonical` (long side across, the street at the bottom). */
export function buildingToAscii(b0: GenBuilding, floor: number): string {
  const b = canonical(b0);
  const [[x0, y0], [x1, y1]] = b.footprint;
  const W = Math.round((x1 - x0) / STEP), H = Math.round((y1 - y0) / STEP);
  const g: string[][] = Array.from({ length: H + 1 }, () => new Array<string>(W + 1).fill(' '));
  const col = (x: number): number => Math.round((x - x0) / STEP), row = (y: number): number => Math.round((y - y0) / STEP);
  const bits = Array.from({ length: H + 1 }, () => new Array<number>(W + 1).fill(0)); // 1 = horizontal wall, 2 = vertical wall, 4 = exterior
  const wall = (a: Vec2, e: Vec2, exterior: boolean): void => {
    const horizontal = Math.abs(a[1] - e[1]) < 1;
    for (let c = Math.min(col(a[0]), col(e[0])); c <= Math.max(col(a[0]), col(e[0])); ++c) {
      for (let r = Math.min(row(a[1]), row(e[1])); r <= Math.max(row(a[1]), row(e[1])); ++r) bits[r][c] |= (horizontal ? 1 : 2) | (exterior ? 4 : 0);
    }
  };
  wall([x0, y0], [x1, y0], true); wall([x1, y0], [x1, y1], true); wall([x0, y1], [x1, y1], true); wall([x0, y0], [x0, y1], true);
  for (const pt of b.partitions ?? []) if (pt.floor === floor && pt.start && pt.end) wall(pt.start, pt.end, false);
  for (let r = 0; r <= H; ++r) for (let c = 0; c <= W; ++c) {
    const v = bits[r][c];
    if (v) g[r][c] = v & 4 ? '#' : v === 3 ? '+' : v === 1 ? '-' : '|';
  }
  // openings: along the wall they sit on (a partition's direction, or the footprint edge)
  const entries: { op: WallOpening; horizontal: boolean }[] = [];
  for (const op of b.openings ?? []) {
    if (op.floor !== floor || op.kind === 'loophole') continue;
    entries.push({ op, horizontal: Math.abs(op.pos[1] - y0) < 2 || Math.abs(op.pos[1] - y1) < 2 });
  }
  for (const pt of b.partitions ?? []) {
    if (pt.floor !== floor || !pt.start || !pt.end) continue;
    for (const op of pt.openings ?? []) if (op.kind !== 'loophole') entries.push({ op, horizontal: Math.abs(pt.start[1] - pt.end[1]) < 1 });
  }
  for (const { op, horizontal } of entries) {
    const half = (op.width ?? 150) / 2, ch = openingGlyph(op);
    for (let t = -half + STEP / 2; t < half; t += STEP) {
      const r = row(horizontal ? op.pos[1] : op.pos[1] + t), c = col(horizontal ? op.pos[0] + t : op.pos[0]);
      if (r >= 0 && r <= H && c >= 0 && c <= W) g[r][c] = ch;
    }
  }
  const units = new Map<string, BuildingUnit>((b.units ?? []).map((u) => [u.id, u]));
  const letters = new Map<string, string>();
  const label = (r: GenRoom): string => {
    if (r.kind === 'stairwell') return 'K';
    if (r.kind === 'corridor') return 'L';
    if (r.kind === 'lobby') return 'O';
    if (r.kind === 'service') return 's';
    const u = r.unit ? units.get(r.unit) : undefined;
    if (u) {
      if (!letters.has(u.column)) letters.set(u.column, String(letters.size));
      const i = Number(letters.get(u.column)!);
      return flatLetter(i, !!u.dead_end);
    }
    return r.unit === 'suite' ? 'e' : 'r';
  };
  for (const r of [...(b.rooms ?? [])].filter((x) => x.floor === floor).sort((p, q) => (p.id < q.id ? -1 : 1))) {
    const mid: Vec2 = [(r.bounds.min[0] + r.bounds.max[0]) / 2, (r.bounds.min[1] + r.bounds.max[1]) / 2];
    const ch = label(r);
    const rr = row(mid[1]), cc = col(mid[0]);
    if (g[rr][cc] === ' ') g[rr][cc] = ch;
  }
  return g.map((r) => r.join('').replace(/\s+$/, '')).join('\n');
}
