// Training Ground / level generator - geometry for the chokepoint / threat analysis (chokepoints.ts): the ground obstacles of a
// layout as thin EDGES with a height (walls as their rectangles, covers as theirs, the canal's open edges, the sides of its
// crossings, the map edge) in a bucket grid, with the three queries the analysis needs - the nearest obstacle, a ray, and a
// height-aware line of sight (an obstacle blocks only when it is taller than the sight line where the line crosses it) - and a
// small grid path search (A* in a window with a blocked patch; a multi-source flood). Pure TS, deterministic, no DOM.
import type { Vec2 } from '../movement/geometry.ts';
import { pointInPoly } from '../movement/geometry.ts';
import type { NavGrid } from '../movement/nav.ts';
import { coverFootprintM2, coverPoly, LAYERS, sizeClassOf } from './layers.ts';
import type { SizeClassRule } from './layers.ts';
import type { GenLayout } from './schema.ts';

export interface Obstacle {
  /** cm above the ground (0 = an edge with nothing standing on it: the canal's lip, a bridge's side, the map edge). */
  height: number;
  /** Structural: walls, buildings, large covers, the canal, the map edge. False = clutter (small / medium covers). */
  hard: boolean;
  /** "wall:<id>" | "cover:<id>" | "cut" | "crossing:<id>" | "bounds". */
  src: string;
  /** The wall kind / cover kind / 'canal_edge' / 'bridge_side' / 'map_edge'. */
  kind: string;
  building?: string;
}
export interface ObstacleItem { obstacle: Obstacle; edges: [Vec2, Vec2][] }

/** A thick wall as its rectangle (flat ends half a thickness beyond a / b, as geometry.ts wallEndFaces); a thin one as one edge. */
export function wallEdges(a: Vec2, b: Vec2, thickness: number): [Vec2, Vec2][] {
  const L = Math.hypot(b[0] - a[0], b[1] - a[1]), half = thickness / 2;
  if (L < 1e-6) return [];
  if (half < 0.5) return [[a, b]];
  const u: Vec2 = [(b[0] - a[0]) / L, (b[1] - a[1]) / L], n: Vec2 = [-u[1], u[0]];
  const c = (p: Vec2, ku: number, kn: number): Vec2 => [p[0] + u[0] * ku * half + n[0] * kn * half, p[1] + u[1] * ku * half + n[1] * kn * half];
  const q = [c(a, -1, 1), c(b, 1, 1), c(b, 1, -1), c(a, -1, -1)];
  return q.map((p, i) => [p, q[(i + 1) % 4]] as [Vec2, Vec2]);
}

const polyEdges = (p: readonly Vec2[]): [Vec2, Vec2][] => p.map((a, i) => [a, p[(i + 1) % p.length]] as [Vec2, Vec2]);
const cross = (ax: number, ay: number, bx: number, by: number): number => ax * by - ay * bx;

/** The pieces of segment ab that lie inside (keepInside) / outside every polygon of `polys`. */
export function clipSegment(a: Vec2, b: Vec2, polys: readonly (readonly Vec2[])[], keepInside: boolean): [Vec2, Vec2][] {
  const ts = [0, 1];
  const dx = b[0] - a[0], dy = b[1] - a[1];
  for (const poly of polys) for (let i = 0; i < poly.length; i++) {
    const p = poly[i], q = poly[(i + 1) % poly.length];
    const ex = q[0] - p[0], ey = q[1] - p[1], den = cross(dx, dy, ex, ey);
    if (Math.abs(den) < 1e-9) continue;
    const t = cross(p[0] - a[0], p[1] - a[1], ex, ey) / den, u = cross(p[0] - a[0], p[1] - a[1], dx, dy) / den;
    if (t > 1e-9 && t < 1 - 1e-9 && u >= 0 && u <= 1) ts.push(t);
  }
  ts.sort((x, y) => x - y);
  const out: [Vec2, Vec2][] = [];
  for (let i = 1; i < ts.length; i++) {
    if (ts[i] - ts[i - 1] < 1e-6) continue;
    const m = (ts[i] + ts[i - 1]) / 2, mp: Vec2 = [a[0] + dx * m, a[1] + dy * m];
    const inside = polys.some((poly) => pointInPoly(mp, poly));
    if (inside === keepInside) out.push([[a[0] + dx * ts[i - 1], a[1] + dy * ts[i - 1]], [a[0] + dx * ts[i], a[1] + dy * ts[i]]]);
  }
  return out;
}

/**
 * The ground-level obstacles of a layout: every ground wall (hard, its height), every cover (hard when its size class is large -
 * a silo is a building to walk round, the rest is clutter), the canal's edges where no crossing spans them and the sides of the
 * crossings over the canal (hard, height 0: a drop, not a screen), the map edge (hard, height 0).
 */
export function layoutObstacles(l: GenLayout, sizes: SizeClassRule = LAYERS.size_classes): ObstacleItem[] {
  const out: ObstacleItem[] = [];
  for (const w of l.walls) {
    if ((w.floor ?? 0) !== 0) continue;
    out.push({ obstacle: { height: w.height, hard: true, src: `wall:${w.id}`, kind: w.kind ?? 'wall', building: w.building }, edges: wallEdges(w.a, w.b, w.thickness) });
  }
  for (const c of l.covers) {
    const large = sizeClassOf(c.size[2], coverFootprintM2(c), c.kind, sizes) === 'large';
    out.push({ obstacle: { height: c.size[2], hard: large, src: `cover:${c.id}`, kind: c.kind }, edges: polyEdges(coverPoly(c)) });
  }
  if (l.cut) {
    const crossings = l.cut.crossings.map((x) => x.poly);
    const lip = polyEdges(l.cut.poly).flatMap(([a, b]) => clipSegment(a, b, crossings, false));
    if (lip.length) out.push({ obstacle: { height: 0, hard: true, src: 'cut', kind: 'canal_edge' }, edges: lip });
    for (const x of l.cut.crossings) {
      const sides = polyEdges(x.poly).flatMap(([a, b]) => clipSegment(a, b, [l.cut!.poly], true)).filter(([a, b]) => {
        // a side runs ACROSS the canal: keep the edges whose middle is well inside the trench (the two ends on the banks lie on its lip)
        const m: Vec2 = [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2], n: Vec2 = [-(b[1] - a[1]), b[0] - a[0]], k = Math.hypot(n[0], n[1]) || 1;
        return pointInPoly([m[0] + (n[0] / k) * 20, m[1] + (n[1] / k) * 20], l.cut!.poly) && pointInPoly([m[0] - (n[0] / k) * 20, m[1] - (n[1] / k) * 20], l.cut!.poly);
      });
      if (sides.length) out.push({ obstacle: { height: 0, hard: true, src: `crossing:${x.id}`, kind: 'bridge_side' }, edges: sides });
    }
  }
  const [x0, y0] = l.bounds.min, [x1, y1] = l.bounds.max;
  out.push({ obstacle: { height: 0, hard: true, src: 'bounds', kind: 'map_edge' }, edges: polyEdges([[x0, y0], [x1, y0], [x1, y1], [x0, y1]]) });
  return out;
}

export interface Hit { dist: number; obs: number; point: Vec2 }

/** The obstacle edges in a bucket grid. `obs` in the answers indexes `obstacles`. */
export class ObstacleIndex {
  readonly obstacles: Obstacle[] = [];
  private readonly ex: Float64Array;
  private readonly eo: Int32Array;
  private readonly cells: Int32Array[];
  private readonly stamp: Int32Array;
  private tick = 0;
  private readonly minX: number;
  private readonly minY: number;
  private readonly w: number;
  private readonly h: number;
  readonly cell: number;

  constructor(items: readonly ObstacleItem[], min: Vec2, max: Vec2, cell = 500) {
    this.cell = cell;
    this.minX = min[0] - cell; this.minY = min[1] - cell;
    this.w = Math.max(1, Math.ceil((max[0] - min[0]) / cell) + 2);
    this.h = Math.max(1, Math.ceil((max[1] - min[1]) / cell) + 2);
    const n = items.reduce((s, it) => s + it.edges.length, 0);
    this.ex = new Float64Array(n * 4); this.eo = new Int32Array(n); this.stamp = new Int32Array(n);
    const lists: number[][] = Array.from({ length: this.w * this.h }, () => []);
    let e = 0;
    items.forEach((it) => {
      const oi = this.obstacles.length;
      this.obstacles.push(it.obstacle);
      for (const [a, b] of it.edges) {
        this.ex[e * 4] = a[0]; this.ex[e * 4 + 1] = a[1]; this.ex[e * 4 + 2] = b[0]; this.ex[e * 4 + 3] = b[1]; this.eo[e] = oi;
        const i0 = this.ci(Math.min(a[0], b[0])), i1 = this.ci(Math.max(a[0], b[0])), j0 = this.cj(Math.min(a[1], b[1])), j1 = this.cj(Math.max(a[1], b[1]));
        for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) lists[j * this.w + i].push(e);
        e++;
      }
    });
    this.cells = lists.map((x) => Int32Array.from(x));
  }

  private ci(x: number): number { return Math.min(this.w - 1, Math.max(0, Math.floor((x - this.minX) / this.cell))); }
  private cj(y: number): number { return Math.min(this.h - 1, Math.max(0, Math.floor((y - this.minY) / this.cell))); }

  /** The nearest obstacle edge within maxDist (hardOnly: structural obstacles only). Null: none. */
  nearest(p: Vec2, maxDist: number, hardOnly = false): Hit | null {
    const pi = this.ci(p[0]), pj = this.cj(p[1]), rings = Math.ceil(maxDist / this.cell) + 1;
    let best = maxDist, bo = -1, bx = 0, by = 0;
    const tick = ++this.tick;
    for (let r = 0; r <= rings; r++) {
      if (bo >= 0 && best <= (r - 1) * this.cell) break;
      for (let j = pj - r; j <= pj + r; j++) {
        if (j < 0 || j >= this.h) continue;
        const edgeRow = j === pj - r || j === pj + r;
        for (let i = pi - r; i <= pi + r; i += edgeRow ? 1 : 2 * r || 1) {
          if (i < 0 || i >= this.w) continue;
          const list = this.cells[j * this.w + i];
          for (let k = 0; k < list.length; k++) {
            const e = list[k];
            if (this.stamp[e] === tick) continue;
            this.stamp[e] = tick;
            const o = this.eo[e];
            if (hardOnly && !this.obstacles[o].hard) continue;
            const ax = this.ex[e * 4], ay = this.ex[e * 4 + 1], dx = this.ex[e * 4 + 2] - ax, dy = this.ex[e * 4 + 3] - ay;
            const l2 = dx * dx + dy * dy;
            const t = l2 === 0 ? 0 : Math.max(0, Math.min(1, ((p[0] - ax) * dx + (p[1] - ay) * dy) / l2));
            const qx = ax + dx * t, qy = ay + dy * t, d = Math.hypot(p[0] - qx, p[1] - qy);
            if (d < best || (d === best && bo >= 0 && o < bo)) { best = d; bo = o; bx = qx; by = qy; }
          }
        }
      }
    }
    return bo >= 0 ? { dist: best, obs: bo, point: [bx, by] } : null;
  }

  /** The first obstacle a ray (origin, unit dir) meets within maxDist. Null: none. */
  ray(o: Vec2, d: Vec2, maxDist: number, hardOnly = false): Hit | null {
    let best = maxDist, bo = -1;
    const tick = ++this.tick;
    this.walk(o[0], o[1], o[0] + d[0] * maxDist, o[1] + d[1] * maxDist, (ck, tExit) => {
      const list = this.cells[ck];
      for (let k = 0; k < list.length; k++) {
        const e = list[k];
        if (this.stamp[e] === tick) continue;
        this.stamp[e] = tick;
        const ob = this.eo[e];
        if (hardOnly && !this.obstacles[ob].hard) continue;
        const ax = this.ex[e * 4], ay = this.ex[e * 4 + 1], ex = this.ex[e * 4 + 2] - ax, ey = this.ex[e * 4 + 3] - ay;
        const den = cross(d[0], d[1], ex, ey);
        if (Math.abs(den) < 1e-9) continue;
        const t = cross(ax - o[0], ay - o[1], ex, ey) / den, u = cross(ax - o[0], ay - o[1], d[0], d[1]) / den;
        if (t >= 0 && u >= 0 && u <= 1 && (t < best || (t === best && bo >= 0 && ob < bo))) { best = t; bo = ob; }
      }
      return bo >= 0 && best <= tExit * maxDist;
    });
    return bo >= 0 ? { dist: best, obs: bo, point: [o[0] + d[0] * best, o[1] + d[1] * best] } : null;
  }

  /**
   * A clear sight line from p at height hp to q at height hq (cm): no obstacle taller than the line where the line crosses it.
   * `skipBuilding`: the walls of that building do not count (the shooter's own roof edge / window wall).
   */
  los(p: Vec2, hp: number, q: Vec2, hq: number, skipBuilding?: string): boolean {
    const low = Math.min(hp, hq), dx = q[0] - p[0], dy = q[1] - p[1];
    const tick = ++this.tick;
    let clear = true;
    this.walk(p[0], p[1], q[0], q[1], (ck) => {
      const list = this.cells[ck];
      for (let k = 0; k < list.length; k++) {
        const e = list[k];
        if (this.stamp[e] === tick) continue;
        this.stamp[e] = tick;
        const ob = this.obstacles[this.eo[e]];
        if (ob.height <= low || (skipBuilding !== undefined && ob.building === skipBuilding)) continue;
        const ax = this.ex[e * 4], ay = this.ex[e * 4 + 1], ex = this.ex[e * 4 + 2] - ax, ey = this.ex[e * 4 + 3] - ay;
        const den = cross(dx, dy, ex, ey);
        if (Math.abs(den) < 1e-9) continue;
        const t = cross(ax - p[0], ay - p[1], ex, ey) / den, u = cross(ax - p[0], ay - p[1], dx, dy) / den;
        if (t > 0 && t < 1 && u >= 0 && u <= 1 && ob.height > hp + (hq - hp) * t) { clear = false; return true; }
      }
      return false;
    });
    return clear;
  }

  /** The cells a segment crosses, in order (Amanatides-Woo); visit(cell, tExit 0..1) returns true to stop. */
  private walk(x0: number, y0: number, x1: number, y1: number, visit: (cell: number, tExit: number) => boolean): void {
    let i = this.ci(x0), j = this.cj(y0);
    const i1 = this.ci(x1), j1 = this.cj(y1), dx = x1 - x0, dy = y1 - y0;
    const si = dx > 0 ? 1 : -1, sj = dy > 0 ? 1 : -1;
    const tdx = dx !== 0 ? Math.abs(this.cell / dx) : Infinity, tdy = dy !== 0 ? Math.abs(this.cell / dy) : Infinity;
    let tx = dx !== 0 ? (this.minX + (i + (dx > 0 ? 1 : 0)) * this.cell - x0) / dx : Infinity;
    let ty = dy !== 0 ? (this.minY + (j + (dy > 0 ? 1 : 0)) * this.cell - y0) / dy : Infinity;
    for (let guard = 0; guard < this.w + this.h + 4; guard++) {
      if (visit(j * this.w + i, Math.min(tx, ty))) return;
      if (i === i1 && j === j1) return;
      if (tx < ty) { tx += tdx; i += si; } else { ty += tdy; j += sj; }
      if (i < 0 || j < 0 || i >= this.w || j >= this.h) return;
    }
  }
}

// ---- a walkable grid: path search in a window, with a blocked patch; a multi-source flood ----------------------------------------

class Heap {
  private k: number[] = [];
  private p: number[] = [];
  get size(): number { return this.k.length; }
  push(key: number, pri: number): void {
    this.k.push(key); this.p.push(pri);
    let i = this.k.length - 1;
    while (i > 0) { const up = (i - 1) >> 1; if (this.p[up] <= this.p[i]) break; this.swap(i, up); i = up; }
  }
  pop(): number {
    const top = this.k[0], lk = this.k.pop()!, lp = this.p.pop()!;
    if (this.k.length) {
      this.k[0] = lk; this.p[0] = lp;
      let i = 0;
      for (;;) {
        const l = 2 * i + 1, r = l + 1;
        let m = i;
        if (l < this.k.length && this.p[l] < this.p[m]) m = l;
        if (r < this.k.length && this.p[r] < this.p[m]) m = r;
        if (m === i) break;
        this.swap(i, m); i = m;
      }
    }
    return top;
  }
  private swap(a: number, b: number): void { const k = this.k[a], p = this.p[a]; this.k[a] = this.k[b]; this.p[a] = this.p[b]; this.k[b] = k; this.p[b] = p; }
}

/** The simulator's ground grid (simNav.ts buildSimNav) with extra cells blocked (the canal off its crossings). */
export class WalkGrid {
  readonly w: number;
  readonly h: number;
  readonly cell: number;
  readonly min: Vec2;
  readonly blocked: Uint8Array;
  private readonly g: Float32Array;
  private readonly seen: Int32Array;
  private readonly patch: Int32Array;
  private readonly done: Int32Array;
  private gen = 0;
  private patchGen = 0;

  constructor(nav: NavGrid, alsoBlocked?: (p: Vec2) => boolean) {
    this.w = nav.w; this.h = nav.h; this.cell = nav.cell; this.min = nav.min;
    this.blocked = Uint8Array.from(nav.blocked);
    if (alsoBlocked) for (let j = 0; j < this.h; j++) for (let i = 0; i < this.w; i++) if (!this.blocked[j * this.w + i] && alsoBlocked(this.center(i, j))) this.blocked[j * this.w + i] = 1;
    this.g = new Float32Array(this.w * this.h); this.seen = new Int32Array(this.w * this.h); this.patch = new Int32Array(this.w * this.h); this.done = new Int32Array(this.w * this.h);
  }

  center(i: number, j: number): Vec2 { return [this.min[0] + (i + 0.5) * this.cell, this.min[1] + (j + 0.5) * this.cell]; }
  cellOf(p: Vec2): [number, number] { return [Math.floor((p[0] - this.min[0]) / this.cell), Math.floor((p[1] - this.min[1]) / this.cell)]; }
  inside(i: number, j: number): boolean { return i >= 0 && j >= 0 && i < this.w && j < this.h; }
  free(p: Vec2): boolean { const [i, j] = this.cellOf(p); return this.inside(i, j) && !this.blocked[j * this.w + i]; }

  /** Start a new blocked patch (cleared by the next call) and block the cells within `radius` cm of the points. */
  setPatch(points: readonly Vec2[], radius: number): void {
    this.patchGen++;
    const r = Math.ceil(radius / this.cell);
    for (const p of points) {
      const [ci, cj] = this.cellOf(p);
      for (let j = cj - r; j <= cj + r; j++) for (let i = ci - r; i <= ci + r; i++) {
        if (!this.inside(i, j)) continue;
        const c = this.center(i, j);
        if (Math.hypot(c[0] - p[0], c[1] - p[1]) <= radius) this.patch[j * this.w + i] = this.patchGen;
      }
    }
  }
  clearPatch(): void { this.patchGen++; }

  private nearFree(p: Vec2, usePatch: boolean): number {
    const [ci, cj] = this.cellOf(p);
    for (let r = 0; r < 5; r++) for (let j = cj - r; j <= cj + r; j++) for (let i = ci - r; i <= ci + r; i++) {
      if (Math.max(Math.abs(i - ci), Math.abs(j - cj)) !== r || !this.inside(i, j)) continue;
      const k = j * this.w + i;
      if (!this.blocked[k] && !(usePatch && this.patch[k] === this.patchGen)) return k;
    }
    return -1;
  }

  /**
   * The length (cm) of the shortest 8-neighbour path a -> b that stays inside the box of a and b grown by `margin` cm (usePatch:
   * the current patch is blocked too). Null: no way inside the box.
   */
  pathLength(a: Vec2, b: Vec2, margin: number, usePatch: boolean): number | null {
    const s = this.nearFree(a, usePatch), t = this.nearFree(b, usePatch);
    if (s < 0 || t < 0) return null;
    const W = this.w, m = Math.ceil(margin / this.cell);
    const [ai, aj] = this.cellOf(a), [bi, bj] = this.cellOf(b);
    const i0 = Math.max(0, Math.min(ai, bi) - m), i1 = Math.min(W - 1, Math.max(ai, bi) + m), j0 = Math.max(0, Math.min(aj, bj) - m), j1 = Math.min(this.h - 1, Math.max(aj, bj) + m);
    const gen = ++this.gen, ti = t % W, tj = (t - ti) / W;
    const hc = (k: number) => { const di = Math.abs((k % W) - ti), dj = Math.abs(Math.floor(k / W) - tj); return Math.max(di, dj) + (Math.SQRT2 - 1) * Math.min(di, dj); };
    const open = new Heap();
    this.g[s] = 0; this.seen[s] = gen; open.push(s, hc(s));
    const done = this.done;
    while (open.size) {
      const k = open.pop();
      if (k === t) return this.g[k] * this.cell;
      if (done[k] === gen) continue;
      done[k] = gen;
      const ki = k % W, kj = (k - ki) / W;
      for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) {
        if (!di && !dj) continue;
        const ni = ki + di, nj = kj + dj;
        if (ni < i0 || ni > i1 || nj < j0 || nj > j1) continue;
        const nk = nj * W + ni;
        if (this.blocked[nk] || (usePatch && this.patch[nk] === this.patchGen) || done[nk] === gen) continue;
        if (di && dj) { const s1 = kj * W + ni, s2 = nj * W + ki; if (this.blocked[s1] || this.blocked[s2] || (usePatch && (this.patch[s1] === this.patchGen || this.patch[s2] === this.patchGen))) continue; }
        const ng = this.g[k] + (di && dj ? Math.SQRT2 : 1);
        if (this.seen[nk] !== gen || ng < this.g[nk]) { this.g[nk] = ng; this.seen[nk] = gen; open.push(nk, ng + hc(nk)); }
      }
    }
    return null;
  }

  /**
   * Walking distance (cm) from the nearest of `sources` to every cell within maxDist: dist (Infinity = not reached) and the index
   * of the source it belongs to (-1). 8-neighbour Dijkstra, no corner cutting.
   */
  flood(sources: readonly Vec2[], maxDist: number): { dist: Float32Array; src: Int32Array } {
    const W = this.w, n = W * this.h, lim = maxDist / this.cell;
    const dist = new Float32Array(n).fill(Infinity), src = new Int32Array(n).fill(-1);
    const open = new Heap();
    sources.forEach((p, si) => {
      const [i, j] = this.cellOf(p);
      if (!this.inside(i, j)) return;
      const k = j * W + i;
      if (this.blocked[k] || dist[k] === 0) return;
      dist[k] = 0; src[k] = si; open.push(k, 0);
    });
    const done = new Uint8Array(n);
    while (open.size) {
      const k = open.pop();
      if (done[k]) continue;
      done[k] = 1;
      const ki = k % W, kj = (k - ki) / W;
      for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) {
        if (!di && !dj) continue;
        const ni = ki + di, nj = kj + dj;
        if (!this.inside(ni, nj)) continue;
        const nk = nj * W + ni;
        if (this.blocked[nk] || done[nk]) continue;
        if (di && dj && (this.blocked[kj * W + ni] || this.blocked[nj * W + ki])) continue;
        const nd = dist[k] + (di && dj ? Math.SQRT2 : 1);
        if (nd <= lim && nd < dist[nk]) { dist[nk] = nd; src[nk] = src[k]; open.push(nk, nd); }
      }
    }
    for (let k = 0; k < n; k++) if (dist[k] !== Infinity) dist[k] *= this.cell;
    return { dist, src };
  }
}
