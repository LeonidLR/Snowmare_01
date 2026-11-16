// Training Ground / level generator v2, stage 2 (board 066): START + 2-4 GOALS visited in FREE order on a (big) map. The goals are
// placed by rules (levelgen_v2.json `objectives`): on the ground layer, reachable from the start on the nav grid, at least
// min_from_start_m from the start and min_spacing_m from each other (farthest-point sampling with a seeded pick among the near-best),
// not inside a LOCKED building unless allowed, not in the canal, not at the map edge. Candidates: the ground rooms of enterable
// buildings (kind room), the open block zones (yard), the street centres (street). The start is the map's spawn or (option) an edge
// street end. Saved in the map JSON as `objectives` (schema.ts ObjectiveSet), drawn on the 2D plan; checkObjectives walks the start
// to every goal and every goal to every other (any order works) and finds the shortest visiting order. Pure and deterministic.

import type { Vec2 } from '../movement/geometry.ts';
import { dist, pointInPoly, rng } from '../movement/geometry.ts';
import { wallSegments } from '../movement/layout.ts';
import { NavGrid } from '../movement/nav.ts';
import { frameOf, lotSeed, V2 } from './blockTemplates.ts';
import { toUE } from './profile.ts';
import { fromUE } from './profile.ts';
import type { GenLayout, ObjectiveGoal, ObjectiveSet } from './schema.ts';
import { OBJECTIVES_VERSION, rect, toFormationLayout } from './schema.ts';

const C = V2.objectives;
const YARD_PIECES = new Set(['open_square', 'junkyard_field', 'one_entry_compound', 'tank_farm', 'alley', 'ladder_overwatch', 'warehouse', 'dead_end']);

export interface PlaceObjectivesOptions {
  layout: GenLayout;
  seed: number;
  /** 2-4 (default: levelgen_v2.json objectives.default_count by the expansion factor). */
  count?: number;
  /** 'spawn' (default): the map's spawn; 'edge': an edge street end picked by the seed. */
  start?: 'spawn' | 'edge';
  /** Goals may stand in a locked building (default levelgen_v2.json allow_in_locked). */
  allowLocked?: boolean;
}

export interface GoalReport { id: string; reachable: boolean; length_m: number }
export interface ObjectivesCheck {
  ok: boolean;
  goals: GoalReport[];
  /** Every goal reaches every other one (free order). */
  pairwise: boolean;
  /** The shortest order start -> all goals (ids) and its length. */
  bestOrder: { order: string[]; length_m: number };
  minSpacing_m: number;
}
export interface PlaceObjectivesResult { ok: boolean; layout: GenLayout; objectives: ObjectiveSet | null; check: ObjectivesCheck | null; candidates: number; message: string }

/** The default goal count for a map: by its expansion factor (1 = not expanded). */
export function defaultGoalCount(l: GenLayout): number {
  const f = l.expansion?.steps.at(-1)?.factor ?? 1;
  const table = C.default_count as Record<string, number>;
  return table[String(f)] ?? C.count_min;
}

/** The ground nav grid of a layout (the evaluator's: toFormationLayout + NavGrid). */
export function groundNav(l: GenLayout): NavGrid {
  const f = toFormationLayout(l, 'ground');
  return new NavGrid(wallSegments(f), f.bounds.min, f.bounds.max);
}

const pathLength = (g: NavGrid, a: Vec2, b: Vec2): number => {
  if (!g.walkable(a) || !g.walkable(b)) return Infinity;
  const p = g.path(a, b);
  return p ? p.slice(1).reduce((s, q, i) => s + dist(p[i], q), 0) : Infinity;
};

/** The grid cells reachable from a point (8-neighbour flood, no corner cutting - the A* moves): one pass for every candidate. */
export function reachableMask(g: NavGrid, from: Vec2): Uint8Array {
  const mask = new Uint8Array(g.w * g.h);
  let [si, sj] = g.cellOf(from);
  if (!g.inside(si, sj)) return mask;
  if (g.blocked[sj * g.w + si]) {
    let found = false;
    for (let r = 1; r < 6 && !found; r++) for (let dj = -r; dj <= r && !found; dj++) for (let di = -r; di <= r && !found; di++) {
      const i = si + di, j = sj + dj;
      if (g.inside(i, j) && !g.blocked[j * g.w + i]) { si = i; sj = j; found = true; }
    }
    if (!found) return mask;
  }
  const queue = new Int32Array(g.w * g.h);
  let head = 0, tail = 0;
  queue[tail++] = sj * g.w + si; mask[sj * g.w + si] = 1;
  while (head < tail) {
    const k = queue[head++], ki = k % g.w, kj = (k - ki) / g.w;
    for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) {
      if (!di && !dj) continue;
      const ni = ki + di, nj = kj + dj;
      if (!g.inside(ni, nj)) continue;
      const nk = nj * g.w + ni;
      if (mask[nk] || g.blocked[nk]) continue;
      if (di && dj && (g.blocked[kj * g.w + ni] || g.blocked[nj * g.w + ki])) continue;
      mask[nk] = 1; queue[tail++] = nk;
    }
  }
  return mask;
}
const inMask = (g: NavGrid, mask: Uint8Array, p: Vec2): boolean => {
  const [i, j] = g.cellOf(p);
  for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) if (g.inside(i + di, j + dj) && mask[(j + dj) * g.w + i + di]) return true;
  return false;
};

interface Candidate { pos: Vec2; kind: ObjectiveGoal['kind']; building?: string; zone?: string }

/** Goal candidates on the ground layer (rooms, yards, streets), before the distance / reachability rules. */
export function goalCandidates(l: GenLayout, allowLocked: boolean): Candidate[] {
  const locks = l.regen?.locks ?? [];
  const inBuilding = (p: Vec2) => l.buildings.find((b) => rect.contains(b.footprint, p));
  const edge = C.edge_margin_m * 100;
  const inBounds = (p: Vec2) => p[0] > l.bounds.min[0] + edge && p[0] < l.bounds.max[0] - edge && p[1] > l.bounds.min[1] + edge && p[1] < l.bounds.max[1] - edge;
  const inCut = (p: Vec2) => !!l.cut && pointInPoly(p, l.cut.poly);
  const out: Candidate[] = [];
  for (const b of l.buildings) {
    if (!b.enterable) continue;
    if (!allowLocked && locks.some((k) => pointInPoly(rect.centre(b.footprint), k.poly))) continue;
    const rooms = (b.rooms ?? []).filter((r) => r.floor === 0);
    for (const r of rooms.length ? rooms : [{ bounds: { min: b.footprint[0], max: b.footprint[1] } }]) {
      out.push({ pos: [(r.bounds.min[0] + r.bounds.max[0]) / 2, (r.bounds.min[1] + r.bounds.max[1]) / 2], kind: 'room', building: b.id });
    }
  }
  for (const z of l.zones) {
    if (!z.set_piece || !YARD_PIECES.has(z.set_piece)) continue;
    const c: Vec2 = [z.poly.reduce((s, p) => s + p[0], 0) / z.poly.length, z.poly.reduce((s, p) => s + p[1], 0) / z.poly.length];
    if (!inBuilding(c)) out.push({ pos: c, kind: 'yard', zone: z.id });
  }
  for (const z of l.zones) {
    if (!z.id.startsWith('Z_ST_')) continue;
    const c: Vec2 = [z.poly.reduce((s, p) => s + p[0], 0) / z.poly.length, z.poly.reduce((s, p) => s + p[1], 0) / z.poly.length];
    out.push({ pos: c, kind: 'street', zone: z.id });
  }
  return out.filter((c) => inBounds(c.pos) && !inCut(c.pos) && (c.kind === 'room' || !inBuilding(c.pos)));
}

/** Edge-street ends: a start at the map edge (3 m in), facing inwards. */
function edgeStarts(l: GenLayout): { pos: Vec2; yaw: number }[] {
  const frame = frameOf(l);
  const inset = C.start_edge_inset_m;
  const out: { pos: Vec2; yaw: number }[] = [];
  for (const z of l.zones) {
    if (!z.id.startsWith('Z_ST_')) continue;
    const m = z.poly.map((p) => fromUE(p, frame));
    const x0 = Math.min(...m.map((p) => p[0])), x1 = Math.max(...m.map((p) => p[0])), y0 = Math.min(...m.map((p) => p[1])), y1 = Math.max(...m.map((p) => p[1]));
    const cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    // profile yaw 0 = east, 90 = south; the UE yaw is profile + 90 (generate.ts yawToUE)
    if (x0 <= 0.5) out.push({ pos: toUE([inset, cy], frame), yaw: 90 });
    if (x1 >= frame[0] - 0.5) out.push({ pos: toUE([frame[0] - inset, cy], frame), yaw: 270 });
    if (y0 <= 0.5) out.push({ pos: toUE([cx, inset], frame), yaw: 180 });
    if (y1 >= frame[1] - 0.5) out.push({ pos: toUE([cx, frame[1] - inset], frame), yaw: 0 });
  }
  return out;
}

/** Place the start + goals (see the file header). The map is returned with its `objectives` block set (ok) or unchanged. */
export function placeObjectives(o: PlaceObjectivesOptions): PlaceObjectivesResult {
  const l = o.layout;
  const count = Math.max(C.count_min, Math.min(C.count_max, Math.round(o.count ?? defaultGoalCount(l))));
  const random = rng(lotSeed(o.seed, 'objectives'));
  const g = groundNav(l);
  let start: ObjectiveSet['start'] = { pos: l.spawn.pos, yaw: l.spawn.yaw, layer: 'ground', source: 'spawn' };
  if (o.start === 'edge') {
    const ends = edgeStarts(l).filter((e) => g.walkable(e.pos));
    if (ends.length) { const e = ends[Math.floor(random() * ends.length)]; start = { pos: e.pos, yaw: e.yaw, layer: 'ground', source: 'edge' }; }
  }
  const fail = (message: string, candidates = 0): PlaceObjectivesResult => ({ ok: false, layout: l, objectives: null, check: null, candidates, message });
  if (!g.walkable(start.pos)) return fail('старт в стене / вне карты');
  const minStart = C.min_from_start_m * 100, minGap = C.min_spacing_m * 100;
  const cands = goalCandidates(l, o.allowLocked ?? C.allow_in_locked).filter((c) => dist(c.pos, start.pos) >= minStart && g.walkable(c.pos));
  // reachability: one flood from the start; the chosen goals are then walked with A* (checkObjectives)
  const mask = reachableMask(g, start.pos);
  const reach = cands.filter((c) => inMask(g, mask, c.pos));
  if (reach.length < C.count_min) return fail(`мало достижимых мест для целей: ${reach.length}`, reach.length);
  const chosen: Candidate[] = [];
  while (chosen.length < count) {
    const score = (c: Candidate) => Math.min(dist(c.pos, start.pos), ...chosen.map((q) => dist(c.pos, q.pos)));
    const pool = reach.filter((c) => !chosen.includes(c) && chosen.every((q) => dist(c.pos, q.pos) >= minGap));
    if (!pool.length) break;
    const best = Math.max(...pool.map(score));
    const near = pool.filter((c) => score(c) >= best * (1 - C.candidate_jitter));
    chosen.push(near[Math.floor(random() * near.length)]);
  }
  if (chosen.length < C.count_min) return fail(`не удалось развести ${count} целей на ${C.min_spacing_m} м (поставлено ${chosen.length})`, reach.length);
  const objectives: ObjectiveSet = {
    version: OBJECTIVES_VERSION, seed: o.seed, order: 'free', start,
    goals: chosen.map((c, i) => ({ id: `GOAL_${i + 1}`, pos: [Math.round(c.pos[0]), Math.round(c.pos[1])], layer: 'ground', kind: c.kind, ...(c.building ? { building: c.building } : {}), ...(c.zone ? { zone: c.zone } : {}) })),
  };
  const layout: GenLayout = { ...l, objectives };
  const check = checkObjectives(layout, g);
  if (!check.ok) return fail('цели недостижимы после расстановки', reach.length);
  const short = chosen.length < count ? ` (просили ${count}: больше не разводится на ${C.min_spacing_m} м)` : '';
  return { ok: true, layout, objectives, check, candidates: reach.length,
    message: `старт (${start.source === 'spawn' ? 'точка появления' : 'край карты'}) + целей ${chosen.length}${short}; кратчайший обход ${check.bestOrder.order.join(' → ')} = ${check.bestOrder.length_m} м` };
}

/** Walk the start to every goal and every goal to every other; the shortest free-order visit. `nav` = a prebuilt ground grid. */
export function checkObjectives(l: GenLayout, nav?: NavGrid): ObjectivesCheck {
  const ob = l.objectives;
  if (!ob) return { ok: false, goals: [], pairwise: false, bestOrder: { order: [], length_m: 0 }, minSpacing_m: 0 };
  const g = nav ?? groundNav(l);
  const pts = [ob.start.pos, ...ob.goals.map((x) => x.pos)];
  const n = pts.length;
  const D: number[][] = Array.from({ length: n }, () => Array(n).fill(0));
  for (let i = 0; i < n; i++) for (let j = i + 1; j < n; j++) { const d = pathLength(g, pts[i], pts[j]); D[i][j] = d; D[j][i] = d; }
  const goals = ob.goals.map((x, i) => ({ id: x.id, reachable: Number.isFinite(D[0][i + 1]), length_m: Math.round(D[0][i + 1] / 10) / 10 }));
  let pairwise = true;
  for (let i = 1; i < n; i++) for (let j = i + 1; j < n; j++) if (!Number.isFinite(D[i][j])) pairwise = false;
  // the shortest visiting order (<= 4 goals: 24 permutations)
  let best = { order: [] as number[], len: Infinity };
  const perm = (rest: number[], cur: number[], at: number, len: number) => {
    if (len >= best.len) return;
    if (!rest.length) { best = { order: cur, len }; return; }
    for (const k of rest) perm(rest.filter((x) => x !== k), [...cur, k], k, len + D[at][k]);
  };
  perm(ob.goals.map((_, i) => i + 1), [], 0, 0);
  let minSpacing = Infinity;
  for (let i = 1; i < n; i++) for (let j = i + 1; j < n; j++) minSpacing = Math.min(minSpacing, dist(pts[i], pts[j]));
  return {
    ok: goals.every((x) => x.reachable) && pairwise,
    goals,
    pairwise,
    bestOrder: { order: best.order.map((k) => ob.goals[k - 1].id), length_m: Number.isFinite(best.len) ? Math.round(best.len / 10) / 10 : Infinity },
    minSpacing_m: Number.isFinite(minSpacing) ? Math.round(minSpacing / 10) / 10 : 0,
  };
}
