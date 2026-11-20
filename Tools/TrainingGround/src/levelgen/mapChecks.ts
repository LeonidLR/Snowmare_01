// Training Ground «Управление картой» (T-022): the checks the page runs after EVERY move on the plan (and after any other edit), on top of
// what it already does (layoutValidator wall crossings, the route evaluation, the objectives check): the geometric overlaps a move
// can make - two buildings on each other, a building off the map / on the cut walls, a ladder or a bridge torn off its building / the
// canal, a cover inside a building or on a wall - plus the schema's own validateGenLayout (what «Принять» refuses). The checks are
// stateless; the page compares the lists BEFORE and AFTER a move (newProblems) so an old defect is not blamed on the move, and a new
// one is shown at once (markers on the plan + the list) and asked about when accepting - never silently accepted. Pure TS.
import type { Vec2 } from '../movement/geometry.ts';
import { pointInPoly, segmentsIntersect } from '../movement/geometry.ts';
import { coverPoly } from './layers.ts';
import { ladderAttachProblems, polysOverlap } from './mapEdit.ts';
import { footprintBBox, footprintPoly } from './rotation.ts';
import type { GenLayout } from './schema.ts';
import { CUT_BUILDING_MARGIN_CM, CUT_WALL_DEFAULT_THICKNESS_CM, validateGenLayout } from './schema.ts';

export type IssueKind = 'building_overlap' | 'building_bounds' | 'building_cut' | 'ladder_detached' | 'bridge_detached' | 'cover_in_building' | 'cover_on_wall' | 'cover_overlap';
export interface MapIssue {
  /** Stable across edits ("building_overlap:B1:B2"): the before / after comparison keys on it. */
  id: string;
  kind: IssueKind;
  /** 'error': the move is wrong (stands on / through something); 'warn': probably unintended. */
  severity: 'error' | 'warn';
  message: string;
  /** Where to mark it on the plan (UE cm). */
  at: Vec2;
}
export interface MapValidation { issues: MapIssue[]; /** validateGenLayout's messages: what the accept path refuses. */ schema: string[] }

const centre = (p: readonly Vec2[]): Vec2 => [p.reduce((s, q) => s + q[0], 0) / p.length, p.reduce((s, q) => s + q[1], 0) / p.length];
const bbox = (p: readonly Vec2[]) => ({ x0: Math.min(...p.map((q) => q[0])), x1: Math.max(...p.map((q) => q[0])), y0: Math.min(...p.map((q) => q[1])), y1: Math.max(...p.map((q) => q[1])) });
const bboxHit = (a: ReturnType<typeof bbox>, b: ReturnType<typeof bbox>): boolean => a.x0 < b.x1 && a.x1 > b.x0 && a.y0 < b.y1 && a.y1 > b.y0;
const mid = (a: Vec2, b: Vec2): Vec2 => [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];

/** The segment a-b touches the polygon (crosses an edge or an end lies inside). */
const segHitsPoly = (a: Vec2, b: Vec2, poly: readonly Vec2[]): boolean => pointInPoly(a, poly) || pointInPoly(b, poly) || poly.some((p, i) => segmentsIntersect(a, b, p, poly[(i + 1) % poly.length]));

/** The geometric problems of a layout (see the file header). Deterministic, in file order. */
export function mapIssues(l: GenLayout): MapIssue[] {
  const out: MapIssue[] = [];
  const bs = l.buildings.map((b) => { const poly = footprintPoly(b); return { b, poly, box: bbox(poly) }; });

  // buildings: on each other, off the map, on the cut walls
  for (let i = 0; i < bs.length; i++) for (let j = i + 1; j < bs.length; j++) {
    if (!bboxHit(bs[i].box, bs[j].box) || !polysOverlap(bs[i].poly, bs[j].poly)) continue;
    const [x, y] = [bs[i].b.id, bs[j].b.id].sort();
    out.push({ id: `building_overlap:${x}:${y}`, kind: 'building_overlap', severity: 'error', message: `здания ${bs[i].b.id} и ${bs[j].b.id} пересекаются`, at: mid(centre(bs[i].poly), centre(bs[j].poly)) });
  }
  for (const { b, box } of bs) {
    if (box.x0 < l.bounds.min[0] || box.x1 > l.bounds.max[0] || box.y0 < l.bounds.min[1] || box.y1 > l.bounds.max[1]) out.push({ id: `building_bounds:${b.id}`, kind: 'building_bounds', severity: 'error', message: `здание ${b.id} выходит за границы карты`, at: centre(footprintPoly(b)) });
  }
  if (l.cut && l.cut.poly.length >= 3) {
    const wallTs = l.walls.filter((w) => w.kind === 'cut_wall').map((w) => w.thickness);
    const t = wallTs.length ? Math.max(...wallTs) : CUT_WALL_DEFAULT_THICKNESS_CM;
    const reach = t + CUT_BUILDING_MARGIN_CM, c = bbox(l.cut.poly);
    const band = { x0: c.x0 - reach, x1: c.x1 + reach, y0: c.y0 - reach, y1: c.y1 + reach };
    for (const { b } of bs) { const f = footprintBBox(b); if (bboxHit({ x0: f[0][0], x1: f[1][0], y0: f[0][1], y1: f[1][1] }, band)) out.push({ id: `building_cut:${b.id}`, kind: 'building_cut', severity: 'error', message: `здание ${b.id} стоит на стенах канала (нужен отступ ${(reach) / 100} м от канала)`, at: centre(footprintPoly(b)) }); }
  }

  // ladders / bridges still where they belong
  for (const k of l.ladders) {
    const p = ladderAttachProblems(l, k);
    if (p.length) out.push({ id: `ladder_detached:${k.id}`, kind: 'ladder_detached', severity: 'error', message: `лестница ${k.id}: ${p.join('; ')}`, at: k.pos });
  }
  if (l.cut) for (const x of l.cut.crossings) if (!polysOverlap(x.poly, l.cut.poly)) out.push({ id: `bridge_detached:${x.id}`, kind: 'bridge_detached', severity: 'error', message: `переход ${x.id} не перекрывает канал`, at: centre(x.poly) });

  // covers: inside a building, on a wall, on another cover
  const covers = l.covers.map((c) => { const poly = coverPoly(c); return { c, poly, box: bbox(poly) }; });
  for (const { c, poly, box } of covers) for (const bd of bs) {
    if (!bboxHit(box, bd.box) || !polysOverlap(poly, bd.poly)) continue;
    out.push({ id: `cover_in_building:${c.id}:${bd.b.id}`, kind: 'cover_in_building', severity: 'warn', message: `укрытие ${c.id} заходит в здание ${bd.b.id}`, at: c.pos });
  }
  const ground = l.walls.filter((w) => (w.floor ?? 0) === 0 && !w.building && w.kind !== 'cut_wall');
  for (const { c, poly, box } of covers) for (const w of ground) {
    const half = Math.max(w.thickness, 1) / 2;
    if (Math.max(w.a[0], w.b[0]) + half < box.x0 || Math.min(w.a[0], w.b[0]) - half > box.x1 || Math.max(w.a[1], w.b[1]) + half < box.y0 || Math.min(w.a[1], w.b[1]) - half > box.y1) continue;
    if (segHitsPoly(w.a, w.b, poly)) out.push({ id: `cover_on_wall:${c.id}:${w.id}`, kind: 'cover_on_wall', severity: 'warn', message: `укрытие ${c.id} стоит на стене ${w.id}`, at: c.pos });
  }
  for (let i = 0; i < covers.length; i++) for (let j = i + 1; j < covers.length; j++) {
    if (!bboxHit(covers[i].box, covers[j].box) || !polysOverlap(covers[i].poly, covers[j].poly)) continue;
    const [x, y] = [covers[i].c.id, covers[j].c.id].sort();
    out.push({ id: `cover_overlap:${x}:${y}`, kind: 'cover_overlap', severity: 'warn', message: `укрытия ${x} и ${y} стоят друг на друге`, at: mid(covers[i].c.pos, covers[j].c.pos) });
  }
  return out;
}

/** mapIssues + the schema's checks (the costlier half: validateGenLayout flattens the ground layer). */
export function validateMapEdit(l: GenLayout): MapValidation {
  return { issues: mapIssues(l), schema: validateGenLayout(l) };
}

/** What `next` has that `prev` did not (by issue id / schema message): the problems a move CAUSED. */
export function newProblems(prev: MapValidation, next: MapValidation): MapValidation {
  const had = new Set(prev.issues.map((i) => i.id)), hadS = new Set(prev.schema);
  return { issues: next.issues.filter((i) => !had.has(i.id)), schema: next.schema.filter((m) => !hadS.has(m)) };
}

/** One line for the move's status message. */
export function describeProblems(v: MapValidation, max = 4): string {
  const lines = [...v.issues.map((i) => i.message), ...v.schema.map((m) => `проверка карты: ${m}`)];
  return lines.length ? `${lines.slice(0, max).join('; ')}${lines.length > max ? `; ещё ${lines.length - max}` : ''}` : '';
}
