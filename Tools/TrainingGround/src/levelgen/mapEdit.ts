// Training Ground «Управление картой» (T-022, designer 2026-11-19): MOVING map elements on the plan - whole buildings with everything that
// belongs to them, fence / wall segments (or a whole fence ring), obstacles / covers, bridges / ramps, ladders - and the helpers of the
// gesture (nudge steps for the arrow keys, grid snap). Every move is a pure layout -> layout edit (the input is never changed), so the page
// pushes it as ONE undo step into the history (regenHistory.ts) and «Принять» writes it through the existing accept path. Positions are UE
// cm, X forward (up on the plan), Y right. A building's rotation is kept: a move is a pure translation (rotation.ts convention: footprint +
// room bounds are the UNROTATED rectangles centred on the same point, every other point is a world point - both just shift by the delta).
// Leaf module (no layers.ts import: layers.ts calls these from editLayout); the overlap checks are in mapChecks.ts. Pure TS, no DOM.
import type { Vec2 } from '../movement/geometry.ts';
import { distToWall, pointInPoly, segmentsIntersect } from '../movement/geometry.ts';
import { footprintBBox, footprintPoly, inFootprint } from './rotation.ts';
import type { GenBuilding, GenCover, GenLadder, GenLayout, GenWall, MantleEdge } from './schema.ts';

/** The arrow-key steps (cm): plain / Shift / Ctrl. The page prints them next to the tool. */
export const NUDGE_CM = { fine: 10, coarse: 100, huge: 1000 } as const;
/** The grid-snap steps the page offers (cm); 1000 = the 10 m grid lines of the plan. */
export const SNAP_STEPS_CM: readonly number[] = [50, 100, 500, 1000];
export const DEFAULT_SNAP_CM = 100;
/** A wall / fence shorter than this is not a wall (end-point drag refuses it), cm. */
export const MIN_WALL_LEN_CM = 50;
/** A ladder's foot stays within this distance of its building's outline (it stands against the wall), cm. */
export const LADDER_REACH_CM = 400;

export interface MapMoveResult {
  ok: boolean;
  layout: GenLayout;
  message: string;
  /** What the move carried along, by kind (the page tells the designer: "стен 12, лестниц 1, ..."). */
  carried?: Record<string, number>;
}

const refuse = (layout: GenLayout, message: string): MapMoveResult => ({ ok: false, layout, message });
const r2 = (v: number): number => { const r = Math.round(v * 100) / 100; return Object.is(r, -0) ? 0 : r; };
const tp = (p: readonly number[], d: Vec2): Vec2 => [r2(p[0] + d[0]), r2(p[1] + d[1])];
const roundDelta = (d: Vec2): Vec2 => [Math.round(d[0]), Math.round(d[1])];
const isZero = (d: Vec2): boolean => d[0] === 0 && d[1] === 0;

// ---- gesture helpers ---------------------------------------------------------------------------------------------------------------

/** The delta of one arrow-key press (ArrowUp = +X = up on the plan, ArrowRight = +Y); null for any other key. Plain 10 cm, Shift 1 m, Ctrl 10 m. */
export function nudgeDelta(key: string, mod: { shift?: boolean; ctrl?: boolean } = {}): Vec2 | null {
  const s = mod.ctrl ? NUDGE_CM.huge : mod.shift ? NUDGE_CM.coarse : NUDGE_CM.fine;
  switch (key) {
    case 'ArrowUp': return [s, 0];
    case 'ArrowDown': return [-s, 0];
    case 'ArrowRight': return [0, s];
    case 'ArrowLeft': return [0, -s];
    default: return null;
  }
}

/** The delta that lands `ref` (the entity's reference point) on the grid when moved by `delta`; step <= 0 = no snap (only rounded to whole cm). */
export function snapMoveDelta(ref: Vec2, delta: Vec2, step: number): Vec2 {
  if (!(step > 0)) return roundDelta(delta);
  const sx = Math.round((ref[0] + delta[0]) / step) * step, sy = Math.round((ref[1] + delta[1]) / step) * step;
  return roundDelta([sx - ref[0], sy - ref[1]]);
}

// ---- layers a building owns ---------------------------------------------------------------------------------------------------------

/** The building a layer name belongs to: 'roof:B' / 'floor:B:n' -> 'B'; 'ground' / 'cut' -> undefined. */
export const buildingOfLayer = (layer: string): string | undefined => (layer.startsWith('roof:') ? layer.slice(5) : layer.startsWith('floor:') ? layer.split(':')[1] : undefined);
const ownsLayer = (id: string) => (layer: string | undefined): boolean => !!layer && buildingOfLayer(layer) === id;

/** The building a ladder is attached to (the one of its `to` layer, else of `from`); undefined = a ground / canal ladder. */
export function ladderBuilding(k: Pick<GenLadder, 'from' | 'to'>): string | undefined {
  return buildingOfLayer(k.to) ?? buildingOfLayer(k.from);
}

/** The same fence family regex as regionRegen.wallFamily (copied: regionRegen imports half the generator; mapChecks.test pins them equal). */
export function fenceFamily(w: Pick<GenWall, 'id' | 'building' | 'kind'>): string | undefined {
  if (w.building || w.kind === 'cut_wall') return undefined;
  return (/^(F_.+?)_[NSEW]_\d+$/.exec(w.id) ?? /^(W_[a-z]+)_[a-z]$/.exec(w.id))?.[1];
}

// ---- bounds -------------------------------------------------------------------------------------------------------------------------

const inBounds = (l: GenLayout, p: readonly number[], pad = 0): boolean => p[0] >= l.bounds.min[0] - pad && p[0] <= l.bounds.max[0] + pad && p[1] >= l.bounds.min[1] - pad && p[1] <= l.bounds.max[1] + pad;

// ---- buildings ------------------------------------------------------------------------------------------------------------------------

/** The building translated by `d`: footprint + room bounds (the unrotated rectangles) and every world point; `rotation` is untouched. */
export function translateBuilding(b: GenBuilding, d: Vec2): GenBuilding {
  const rect = (r: readonly (readonly number[])[]): [Vec2, Vec2] => [tp(r[0], d), tp(r[1], d)];
  const opening = <T extends { pos: Vec2 }>(o: T): T => ({ ...o, pos: tp(o.pos, d) });
  const nb: GenBuilding = {
    ...b,
    footprint: rect(b.footprint),
    doors: b.doors.map(opening),
    stairs: b.stairs.map((s) => ({ ...s, pos: tp(s.pos, d), top: tp(s.top, d), ...(s.mid ? { mid: tp(s.mid, d) } : {}) })),
    roof_holes: b.roof_holes.map((p) => tp(p, d)),
    floor_holes: b.floor_holes.map(opening),
  };
  if (b.openings) nb.openings = b.openings.map(opening);
  if (b.partitions) nb.partitions = b.partitions.map((pt) => ({ ...pt, start: tp(pt.start, d), end: tp(pt.end, d), ...(pt.openings ? { openings: pt.openings.map(opening) } : {}) }));
  if (b.parapets) nb.parapets = b.parapets.map((pp) => ({ ...pp, start: tp(pp.start, d), end: tp(pp.end, d) }));
  if (b.hatches) nb.hatches = b.hatches.map(opening);
  if (b.rooms) nb.rooms = b.rooms.map((rm) => { const r = rect([rm.bounds.min, rm.bounds.max]); return { ...rm, bounds: { min: r[0], max: r[1] } }; });
  if (b.floors_detail) nb.floors_detail = b.floors_detail.map((fd) => ({ ...fd,
    ...(fd.gaps ? { gaps: fd.gaps.map((g) => ({ ...g, poly: g.poly.map((p) => tp(p, d)) })) } : {}),
    ...(fd.rooms ? { rooms: fd.rooms.map((rm) => { const r = rect([rm.bounds.min, rm.bounds.max]); return { ...rm, bounds: { min: r[0], max: r[1] } }; }) } : {}) }));
  if (b.mantle_edges) nb.mantle_edges = b.mantle_edges.map((m) => moveMantle(m, d));
  return nb;
}

const moveMantle = (m: MantleEdge, d: Vec2): MantleEdge => ({ ...m, start: tp(m.start, d), end: tp(m.end, d) });
const moveLadderBy = (k: GenLadder, d: Vec2): GenLadder => ({ ...k, pos: tp(k.pos, d), foot: tp(k.foot, d), top: tp(k.top, d) });

/**
 * The WHOLE building moves by `delta` (cm): its footprint + storeys + openings + partitions + parapets + hatches + stairs + holes +
 * rooms + mantle edges, the walls that belong to it (`building`), its ladders (a ladder that joins two buildings stays and is
 * reported), its top-level mantle edges, the route points that stand on its floors / roof, the goals inside it, and the spawn /
 * ambush points that stand inside its (old) footprint (the enemy placed in this building). Covers are NOT carried (a crate field the
 * building is dragged over would be dragged along); the ones standing inside stay and are counted in the message. The rotation is kept. Refused when the footprint would leave the
 * map's bounds. NOT moved (the page lists them as stale): the props file, the tactical map, the analysis, the PMC plan.
 */
export function moveBuilding(l: GenLayout, id: string, delta: Vec2): MapMoveResult {
  const b = l.buildings.find((x) => x.id === id);
  if (!b) return refuse(l, `нет здания ${id}`);
  const d = roundDelta(delta);
  if (isZero(d)) return refuse(l, 'нулевое смещение');
  const moved = translateBuilding(b, d);
  const bb = footprintBBox(moved);
  if (!inBounds(l, bb[0]) || !inBounds(l, bb[1])) return refuse(l, `здание ${id} вышло бы за границы карты`);
  const mine = ownsLayer(id);
  const inside = (p: Vec2) => inFootprint(b, p, 0);
  const carried: Record<string, number> = {};
  const bump = (k: string, n = 1) => { if (n) carried[k] = (carried[k] ?? 0) + n; };
  const skipped: string[] = [];

  const walls = l.walls.map((w) => { if (w.building !== id) return w; bump('стен'); return { ...w, a: tp(w.a, d), b: tp(w.b, d) }; });
  const ladders = l.ladders.map((k) => {
    const owners = new Set([buildingOfLayer(k.from), buildingOfLayer(k.to)].filter((x): x is string => !!x));
    if (!owners.has(id)) return k;
    if (owners.size > 1) { skipped.push(k.id); return k; }
    bump('лестниц');
    return moveLadderBy(k, d);
  });
  const next: GenLayout = { ...l, buildings: l.buildings.map((x) => (x.id === id ? moved : x)), walls, ladders };
  if (l.mantle_edges) next.mantle_edges = l.mantle_edges.map((m) => { if (m.building !== id) return m; bump('кромок перелаза'); return moveMantle(m, d); });
  next.ambush_points = l.ambush_points.map((a) => { if (!inside(a.pos)) return a; bump('точек засады'); return { ...a, pos: tp(a.pos, d) }; });
  next.spawn_points = l.spawn_points.map((s) => { if (!inside(s.pos)) return s; bump('точек появления'); return { ...s, pos: tp(s.pos, d) }; });
  next.routes = l.routes.map((r) => {
    if (!r.layers || !r.layers.some(mine)) return r;
    bump('точек маршрута', r.layers.filter(mine).length);
    return { ...r, points: r.points.map((p, i) => (mine(r.layers![i]) ? tp(p, d) : p)) };
  });
  if (l.objectives && l.objectives.goals.some((g) => g.building === id)) {
    next.objectives = { ...l.objectives, goals: l.objectives.goals.map((g) => { if (g.building !== id) return g; bump('целей'); return { ...g, pos: tp(g.pos, d) }; }) };
  }
  const stayed = l.covers.filter((c) => inside(c.pos)).length;
  const note = (stayed ? `; укрытий внутри остаётся на месте: ${stayed}` : '') + (skipped.length ? `; лестница ${skipped.join(', ')} соединяет два здания — осталась на месте` : '');
  const what = Object.entries(carried).map(([k, v]) => `${k} ${v}`).join(', ');
  return { ok: true, layout: next, carried, message: `здание ${id} перемещено на (${d[0]}, ${d[1]}) см${what ? ` вместе с: ${what}` : ''}${note}` };
}

// ---- ladders --------------------------------------------------------------------------------------------------------------------------

/** Distance from a point to the outline of a polygon, cm. */
const distToOutline = (p: Vec2, poly: readonly Vec2[]): number => Math.min(...poly.map((a, i) => distToWall(p, { a, b: poly[(i + 1) % poly.length], thickness: 0 })));

/**
 * What keeps a ladder attached to its building: its position stays inside the footprint (a hatch ladder) or within LADDER_REACH_CM of
 * the building's outline (a ladder against the wall), and the top link point stays on the roof / floor (inside the footprint). Empty = attached.
 */
export function ladderAttachProblems(l: GenLayout, k: GenLadder): string[] {
  const bid = ladderBuilding(k);
  if (!bid) return [];
  const b = l.buildings.find((x) => x.id === bid);
  if (!b) return [`здание ${bid} не найдено`];
  const out: string[] = [];
  const poly = footprintPoly(b);
  // a hatch ladder stands INSIDE the building (it climbs from the top floor to the roof); an outside ladder stands against the wall
  if (!inFootprint(b, k.pos, 0) && distToOutline(k.pos, poly) > LADDER_REACH_CM) out.push(`основание дальше ${LADDER_REACH_CM / 100} м от стены здания ${bid}`);
  if (k.to.startsWith('roof:') || k.to.startsWith('floor:')) if (!inFootprint(b, k.top, 10)) out.push(`верх лестницы вне крыши / этажа здания ${bid}`);
  return out;
}

/** A ladder moves alone (its building constraint holds: it stays against the wall of ITS building and its top stays on the roof / floor). */
export function moveLadder(l: GenLayout, id: string, delta: Vec2): MapMoveResult {
  const k = l.ladders.find((x) => x.id === id);
  if (!k) return refuse(l, `нет лестницы ${id}`);
  if (!ladderBuilding(k)) return refuse(l, `лестница ${id} — канал / земля: её не двигают`);
  const d = roundDelta(delta);
  if (isZero(d)) return refuse(l, 'нулевое смещение');
  const nk = moveLadderBy(k, d);
  const next: GenLayout = { ...l, ladders: l.ladders.map((x) => (x.id === id ? nk : x)) };
  if (!inBounds(l, nk.pos) || !inBounds(l, nk.top)) return refuse(l, `лестница ${id} вышла бы за границы карты`);
  const problems = ladderAttachProblems(next, nk);
  if (problems.length) return refuse(l, `лестница ${id}: ${problems.join('; ')} — перемещение отклонено`);
  return { ok: true, layout: next, message: `лестница ${id} перемещена на (${d[0]}, ${d[1]}) см` };
}

// ---- crossings (bridges / ramps over the cut) ---------------------------------------------------------------------------------------------

/** The two polygons overlap (an edge crosses an edge, or one holds a corner of the other). */
export function polysOverlap(a: readonly Vec2[], b: readonly Vec2[]): boolean {
  if (a.some((p) => pointInPoly(p, b)) || b.some((p) => pointInPoly(p, a))) return true;
  for (let i = 0; i < a.length; i++) for (let j = 0; j < b.length; j++) if (segmentsIntersect(a[i], a[(i + 1) % a.length], b[j], b[(j + 1) % b.length])) return true;
  return false;
}

/** A bridge / ramp moves as a whole; it must still overlap the cut (a crossing over nothing is no crossing). */
export function moveCrossing(l: GenLayout, id: string, delta: Vec2): MapMoveResult {
  const cut = l.cut;
  const x = cut?.crossings.find((c) => c.id === id);
  if (!cut || !x) return refuse(l, `нет перехода ${id}`);
  const d = roundDelta(delta);
  if (isZero(d)) return refuse(l, 'нулевое смещение');
  const poly = x.poly.map((p) => tp(p, d));
  if (poly.some((p) => !inBounds(l, p))) return refuse(l, `переход ${id} вышел бы за границы карты`);
  if (!polysOverlap(poly, cut.poly)) return refuse(l, `переход ${id} должен перекрывать канал — перемещение отклонено`);
  return { ok: true, layout: { ...l, cut: { ...cut, crossings: cut.crossings.map((c) => (c.id === id ? { ...c, poly } : c)) } }, message: `${x.kind === 'ramp' ? 'пандус' : 'мост'} ${id} перемещён на (${d[0]}, ${d[1]}) см` };
}

// ---- free walls / fences and covers -----------------------------------------------------------------------------------------------------------

/**
 * A free wall / fence segment moves by `delta`; with `family` the whole fence ring it belongs to (F_<ring>_<side>_<n>) moves with it.
 * A wall that belongs to a building is refused (the page moves the whole building instead: layers.ts redirects). Mantle edges that name
 * the wall (`wall`) move with it.
 */
export function moveWalls(l: GenLayout, id: string, delta: Vec2, family = false): MapMoveResult {
  const w = l.walls.find((x) => x.id === id);
  if (!w) return refuse(l, `нет стены ${id}`);
  if (w.building) return refuse(l, `стена ${id} принадлежит зданию ${w.building}: двигайте здание целиком`);
  const d = roundDelta(delta);
  if (isZero(d)) return refuse(l, 'нулевое смещение');
  const fam = family ? fenceFamily(w) : undefined;
  const ids = new Set(l.walls.filter((x) => (fam ? fenceFamily(x) === fam : x.id === id)).map((x) => x.id));
  const walls = l.walls.map((x) => (ids.has(x.id) ? { ...x, a: tp(x.a, d), b: tp(x.b, d) } : x));
  if (walls.some((x) => ids.has(x.id) && (!inBounds(l, x.a) || !inBounds(l, x.b)))) return refuse(l, 'за границы карты');
  const next: GenLayout = { ...l, walls };
  if (l.mantle_edges) next.mantle_edges = l.mantle_edges.map((m) => (m.wall && ids.has(m.wall) ? moveMantle(m, d) : m));
  return { ok: true, layout: next, carried: { 'сегментов': ids.size }, message: `${fam ? `забор ${fam} (${ids.size} сегм.)` : id}: перемещено на (${d[0]}, ${d[1]}) см` };
}

/** One end of a free wall / fence moves to `pos` (the length changes; at least MIN_WALL_LEN_CM stays). */
export function moveWallEnd(l: GenLayout, id: string, end: 'a' | 'b', pos: Vec2): MapMoveResult {
  const w = l.walls.find((x) => x.id === id);
  if (!w) return refuse(l, `нет стены ${id}`);
  if (w.building) return refuse(l, `стена ${id} принадлежит зданию ${w.building}`);
  const p: Vec2 = [Math.round(pos[0]), Math.round(pos[1])];
  if (!inBounds(l, p)) return refuse(l, 'за границы карты');
  const other = end === 'a' ? w.b : w.a;
  if (Math.hypot(p[0] - other[0], p[1] - other[1]) < MIN_WALL_LEN_CM) return refuse(l, `${id}: короче ${MIN_WALL_LEN_CM} см — это уже не стена`);
  if (p[0] === w[end][0] && p[1] === w[end][1]) return refuse(l, 'нулевое смещение');
  return { ok: true, layout: { ...l, walls: l.walls.map((x) => (x.id === id ? { ...x, [end]: p } : x)) }, message: `${id}: конец ${end.toUpperCase()} → (${p[0]}, ${p[1]})` };
}

/** A cover moves (its yaw / size are kept). */
export function moveCover(l: GenLayout, id: string, delta: Vec2): MapMoveResult {
  const c = l.covers.find((x) => x.id === id);
  if (!c) return refuse(l, `нет укрытия ${id}`);
  const d = roundDelta(delta);
  if (isZero(d)) return refuse(l, 'нулевое смещение');
  const pos = tp(c.pos, d);
  if (!inBounds(l, pos)) return refuse(l, 'за границы карты');
  return { ok: true, layout: { ...l, covers: l.covers.map((x): GenCover => (x.id === id ? { ...x, pos } : x)) }, message: `${id}: перемещено на (${d[0]}, ${d[1]}) см` };
}

/** The entity's reference point for the grid snap (a building: its centre; a wall: its middle; a cover / ladder: its position; a bridge: its centroid). */
export function snapReference(l: GenLayout, key: string): Vec2 | null {
  const i = key.indexOf(':'), type = key.slice(0, i), id = key.slice(i + 1);
  if (type === 'building') { const b = l.buildings.find((x) => x.id === id); return b ? [(b.footprint[0][0] + b.footprint[1][0]) / 2, (b.footprint[0][1] + b.footprint[1][1]) / 2] : null; }
  if (type === 'wall') { const w = l.walls.find((x) => x.id === id); return w ? [(w.a[0] + w.b[0]) / 2, (w.a[1] + w.b[1]) / 2] : null; }
  if (type === 'cover') return l.covers.find((x) => x.id === id)?.pos ?? null;
  if (type === 'ladder') return l.ladders.find((x) => x.id === id)?.pos ?? null;
  if (type === 'crossing') { const x = l.cut?.crossings.find((c) => c.id === id); return x ? [x.poly.reduce((s, p) => s + p[0], 0) / x.poly.length, x.poly.reduce((s, p) => s + p[1], 0) / x.poly.length] : null; }
  return null;
}

// ---- derived files (task 092) -------------------------------------------------------------------------------------------------------------
export { carryPropsWithBuilding, carryTacticalWithBuilding, isPropOnBuilding } from './moveDerived.ts';
