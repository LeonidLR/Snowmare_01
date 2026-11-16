// Training Ground / level generator v2 - the SQUAD SIMULATOR's walkability («Прогнать отряд (симулятор)», evaluate.ts), designer bug
// 2026-11-16 (evening): the squad walked THROUGH obstacles after a full region re-roll. The simulator's ground NavGrid was built from
// the layout's walls only, so the covers (wrecks, jersey barriers, sandbags, crates, barrels, silos, boilers) were no obstacles to the
// leader's A* path, the followers' steps or the separation. The UE navmesh is carved by those meshes; the simulator now is too:
//   - buildSimNav: the ground walls + every cover (>= sim_nav.cover_min_height_cm) as its four edges and a spine along its long side
//     (the whole rectangle is solid; a thin edge ring alone would leave a walkable pocket inside a big cover);
//   - snapToWalkable / prepareSimLeg: a route point that falls inside an obstacle is moved to the nearest walkable spot (within
//     sim_nav.snap_max_m), and a pair of points the covers cut apart completely is joined by the walls-only path (a vault over the
//     cover, counted) - so the simulator never falls back to a straight line through a wall.
// The route CHECK (checkLayoutRoutes, the re-roll / expansion gate) keeps the walls-only grid: the covers move with every re-roll and
// the generator places them off the route; the simulator is the one that has to walk round them. Constants: levelgen_v2.json sim_nav.
import cfg from '../../../../Content/Data/LevelGen/levelgen_v2.json' with { type: 'json' };
import type { Segment, Vec2 } from '../movement/geometry.ts';
import { dirOf, dist } from '../movement/geometry.ts';
import { wallSegments } from '../movement/layout.ts';
import { NavGrid } from '../movement/nav.ts';
import type { GenCover, GenLayout } from './schema.ts';
import { toFormationLayout } from './schema.ts';

export const SIM_NAV = cfg.sim_nav;

/**
 * A cover as obstacle segments: its four edges (thin lines) + a spine along its long side as thick as the short side minus nothing
 * (from short/2 inside one end to short/2 inside the other): every point of the rectangle is on an edge, inside the spine's capsule,
 * or in a corner sliver within (1 - 1/sqrt 2) * short/2 of an edge - covered by the agent radius for covers up to ~2.4 m wide.
 */
export function coverObstacles(c: GenCover): Segment[] {
  const d = dirOf(c.yaw), r: Vec2 = [-d[1], d[0]];
  const hx = c.size[0] / 2, hy = c.size[1] / 2;
  const at = (u: number, v: number): Vec2 => [c.pos[0] + d[0] * u + r[0] * v, c.pos[1] + d[1] * u + r[1] * v];
  const corners = [at(hx, hy), at(-hx, hy), at(-hx, -hy), at(hx, -hy)];
  const edges: Segment[] = corners.map((a, i) => ({ a, b: corners[(i + 1) % 4], thickness: 0 }));
  const long = Math.max(hx, hy), short = Math.min(hx, hy);
  const half = Math.max(0, long - short);
  const spine: Segment = hx >= hy ? { a: at(-half, 0), b: at(half, 0), thickness: 2 * short } : { a: at(0, -half), b: at(0, half), thickness: 2 * short };
  return [...edges, spine];
}

/** The covers that block the simulator (all of them by default: sim_nav.cover_min_height_cm = 0). */
export const blockingCovers = (l: GenLayout): GenCover[] => l.covers.filter((c) => c.size[2] >= SIM_NAV.cover_min_height_cm);

/** The simulator's ground grid: the ground walls + the blocking covers (rebuilt from the layout it is given, every walk). */
export function buildSimNav(l: GenLayout): NavGrid {
  const f = toFormationLayout(l, 'ground');
  return new NavGrid([...wallSegments(f), ...blockingCovers(l).flatMap(coverObstacles)], f.bounds.min, f.bounds.max);
}

/** p itself when walkable, else the nearest walkable grid-cell centre within sim_nav.snap_max_m (null: none). */
export function snapToWalkable(nav: NavGrid, p: Vec2, maxCm = SIM_NAV.snap_max_m * 100): Vec2 | null {
  if (nav.walkable(p)) return p;
  const [ci, cj] = nav.cellOf(p);
  const rings = Math.ceil(maxCm / nav.cell) + 1;
  let best: Vec2 | null = null, bestD = Infinity;
  for (let rr = 0; rr <= rings; rr++) {
    // a ring's nearest cell is >= (rr - 1) cells away: once that exceeds the best found, no later ring can beat it
    if (best && (rr - 1) * nav.cell > bestD) break;
    for (let dj = -rr; dj <= rr; dj++) for (let di = -rr; di <= rr; di++) {
      if (Math.max(Math.abs(di), Math.abs(dj)) !== rr) continue;
      const i = ci + di, j = cj + dj;
      if (!nav.inside(i, j) || nav.blocked[j * nav.w + i]) continue;
      const q = nav.center(i, j), dq = dist(p, q);
      if (dq <= maxCm && dq < bestD && nav.walkable(q)) { best = q; bestD = dq; }
    }
  }
  return best;
}

export interface SimLeg { points: Vec2[]; snapped: number; vaults: number }

/**
 * A route leg ready for the simulator on `nav` (buildSimNav): every point snapped out of the obstacles (snapToWalkable), duplicates
 * dropped; two consecutive points the covers cut apart completely get the walls-only path (`wallsNav`) in between - a vault over the
 * cover, counted in `vaults` (the generator's routes never need it on the reference maps).
 */
export function prepareSimLeg(nav: NavGrid, points: readonly Vec2[], wallsNav?: NavGrid): SimLeg {
  let snapped = 0, vaults = 0;
  const pts: Vec2[] = [];
  for (const p of points) {
    const s = snapToWalkable(nav, p) ?? p;
    if (s !== p) snapped++;
    if (!pts.length || dist(pts[pts.length - 1], s) > 1) pts.push(s);
  }
  if (!wallsNav || pts.length < 2) return { points: pts, snapped, vaults };
  const out: Vec2[] = [pts[0]];
  for (let k = 1; k < pts.length; k++) {
    if (!nav.path(pts[k - 1], pts[k])) {
      const around = wallsNav.path(pts[k - 1], pts[k]);
      if (around) { out.push(...around.slice(1, -1)); vaults++; }
    }
    out.push(pts[k]);
  }
  return { points: out, snapped, vaults };
}
