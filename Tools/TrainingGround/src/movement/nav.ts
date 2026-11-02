// Training Ground / movement: a walkable grid over a layout and A* paths (stand-in for the navmesh; the Unreal map bakes the
// real one). Cells blocked where a wall widened by the agent radius covers the cell centre.

import type { Segment, Vec2 } from './geometry.ts';
import { distToWall, pointInWall, segSegDist, segmentBlocked } from './geometry.ts';

export const AGENT_RADIUS = 35;

/**
 * The walls bucketed in coarse cells for path()'s segment tests: `blocked(p, q)` gives the SAME answer as `segmentBlocked(p, q, walls,
 * radius)` (every wall whose band - half its thickness + the radius - can touch the segment lies in one of the cells the segment
 * crosses, so only those are tested), at a fraction of the cost on a layout with hundreds of walls. Ends outside the index fall back to
 * the full scan.
 */
class WallIndex {
  private readonly cell = 500;
  private readonly min: Vec2;
  private readonly w: number;
  private readonly h: number;
  private readonly cells: Int32Array[];
  private readonly reach: Float64Array;
  private readonly stamp: Int32Array;
  private readonly walls: readonly Segment[];
  private readonly radius: number;
  private tick = 0;

  constructor(walls: readonly Segment[], min: Vec2, max: Vec2, radius: number) {
    this.walls = walls;
    this.radius = radius;
    this.min = [min[0] - 2 * this.cell, min[1] - 2 * this.cell];
    this.w = Math.max(1, Math.ceil((max[0] - min[0]) / this.cell) + 4);
    this.h = Math.max(1, Math.ceil((max[1] - min[1]) / this.cell) + 4);
    this.reach = new Float64Array(walls.length);
    const lists: number[][] = Array.from({ length: this.w * this.h }, () => []);
    walls.forEach((wall, wi) => {
      const reach = radius + (wall.thickness ?? 0) / 2;
      this.reach[wi] = reach;
      const i0 = this.ix(Math.min(wall.a[0], wall.b[0]) - reach), i1 = this.ix(Math.max(wall.a[0], wall.b[0]) + reach);
      const j0 = this.jy(Math.min(wall.a[1], wall.b[1]) - reach), j1 = this.jy(Math.max(wall.a[1], wall.b[1]) + reach);
      for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) lists[j * this.w + i].push(wi);
    });
    this.cells = lists.map((l) => Int32Array.from(l));
    this.stamp = new Int32Array(walls.length);
  }

  private ix(x: number): number { return Math.min(this.w - 1, Math.max(0, Math.floor((x - this.min[0]) / this.cell))); }
  private jy(y: number): number { return Math.min(this.h - 1, Math.max(0, Math.floor((y - this.min[1]) / this.cell))); }
  private insideRaw(p: Vec2): boolean {
    return p[0] >= this.min[0] && p[1] >= this.min[1] && p[0] < this.min[0] + this.w * this.cell && p[1] < this.min[1] + this.h * this.cell;
  }

  blocked(p: Vec2, q: Vec2): boolean {
    if (!this.insideRaw(p) || !this.insideRaw(q)) return segmentBlocked(p, q, this.walls, this.radius);
    const dx = q[0] - p[0], dy = q[1] - p[1];
    this.tick++;
    const tick = this.tick, stamp = this.stamp, walls = this.walls, reach = this.reach;
    let i = this.ix(p[0]), j = this.jy(p[1]);
    const i1 = this.ix(q[0]), j1 = this.jy(q[1]);
    const stepI = dx > 0 ? 1 : -1, stepJ = dy > 0 ? 1 : -1;
    const tdx = dx !== 0 ? Math.abs(this.cell / dx) : Infinity, tdy = dy !== 0 ? Math.abs(this.cell / dy) : Infinity;
    const cx = this.min[0] + (i + (dx > 0 ? 1 : 0)) * this.cell, cy = this.min[1] + (j + (dy > 0 ? 1 : 0)) * this.cell;
    let tx = dx !== 0 ? (cx - p[0]) / dx : Infinity, ty = dy !== 0 ? (cy - p[1]) / dy : Infinity;
    for (let guard = 0; guard < this.w + this.h + 2; guard++) {
      const list = this.cells[j * this.w + i];
      for (let k = 0; k < list.length; k++) {
        const wi = list[k];
        if (stamp[wi] === tick) continue;
        stamp[wi] = tick;
        const wall = walls[wi];
        if (segSegDist(p, q, wall.a, wall.b) < reach[wi]) return true;
      }
      if (i === i1 && j === j1) break;
      if (tx < ty) { tx += tdx; i += stepI; } else { ty += tdy; j += stepJ; }
      if (i < 0 || j < 0 || i >= this.w || j >= this.h) break;
    }
    return false;
  }
}

export class NavGrid {
  readonly cell: number;
  readonly min: Vec2;
  readonly w: number;
  readonly h: number;
  readonly blocked: Uint8Array;
  readonly walls: readonly Segment[];
  /** A* scratch (path): per-cell cost / parent / stamps, re-used across searches by a generation counter. */
  private scratch?: { g: Float64Array; from: Int32Array; stamp: Int32Array; closed: Int32Array; gen: number };
  private index?: WallIndex;
  private readonly max: Vec2;

  constructor(walls: readonly Segment[], min: Vec2, max: Vec2, cell = 50, radius = AGENT_RADIUS) {
    this.cell = cell;
    this.min = min;
    this.max = max;
    this.walls = walls;
    this.w = Math.ceil((max[0] - min[0]) / cell);
    this.h = Math.ceil((max[1] - min[1]) / cell);
    this.blocked = new Uint8Array(this.w * this.h);
    // Per wall only the cells of its bounding box widened by the radius + half its thickness (+ a cell): the same cells as testing every
    // cell against every wall (a cell centre within the radius of the wall's surface lies inside that box), far cheaper on a big layout.
    for (const wall of walls) {
      const reach = radius + (wall.thickness ?? 0) / 2 + cell;
      const i0 = Math.max(0, Math.floor((Math.min(wall.a[0], wall.b[0]) - reach - min[0]) / cell));
      const i1 = Math.min(this.w - 1, Math.floor((Math.max(wall.a[0], wall.b[0]) + reach - min[0]) / cell));
      const j0 = Math.max(0, Math.floor((Math.min(wall.a[1], wall.b[1]) - reach - min[1]) / cell));
      const j1 = Math.min(this.h - 1, Math.floor((Math.max(wall.a[1], wall.b[1]) + reach - min[1]) / cell));
      for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
        const k = j * this.w + i;
        if (!this.blocked[k] && distToWall(this.center(i, j), wall) < radius) this.blocked[k] = 1;
      }
    }
  }

  /** segmentBlocked(a, b, walls, AGENT_RADIUS) through the wall index (the same answer, far cheaper on a big layout). */
  wallsBlock(a: Vec2, b: Vec2): boolean {
    return (this.index ??= new WallIndex(this.walls, this.min, this.max, AGENT_RADIUS)).blocked(a, b);
  }

  /** The cell under p is inside the grid and not blocked (the cell test alone: no exact wall distance). */
  cellFree(p: Vec2): boolean {
    const [i, j] = this.cellOf(p);
    return this.inside(i, j) && !this.blocked[j * this.w + i];
  }

  /**
   * No blocked cell lies on the straight line a -> b, the two end cells excepted (a goal beside a wall, a man standing in a widened
   * band): a fast stand-in for segmentBlocked on the grid (Amanatides-Woo cell walk). Ends outside the grid = not clear.
   */
  lineClear(a: Vec2, b: Vec2): boolean {
    let [i, j] = this.cellOf(a);
    const [i1, j1] = this.cellOf(b);
    if (!this.inside(i, j) || !this.inside(i1, j1)) return false;
    const dx = b[0] - a[0], dy = b[1] - a[1];
    const stepI = dx > 0 ? 1 : -1, stepJ = dy > 0 ? 1 : -1;
    const tdx = dx !== 0 ? Math.abs(this.cell / dx) : Infinity, tdy = dy !== 0 ? Math.abs(this.cell / dy) : Infinity;
    const cx = this.min[0] + (i + (dx > 0 ? 1 : 0)) * this.cell, cy = this.min[1] + (j + (dy > 0 ? 1 : 0)) * this.cell;
    let tx = dx !== 0 ? (cx - a[0]) / dx : Infinity, ty = dy !== 0 ? (cy - a[1]) / dy : Infinity;
    let first = true;
    for (let guard = 0; guard < this.w + this.h + 2; guard++) {
      if (i === i1 && j === j1) return true;
      if (!first && this.blocked[j * this.w + i]) return false;
      first = false;
      if (tx < ty) { tx += tdx; i += stepI; } else { ty += tdy; j += stepJ; }
      if (!this.inside(i, j)) return false;
    }
    return true;
  }

  center(i: number, j: number): Vec2 {
    return [this.min[0] + (i + 0.5) * this.cell, this.min[1] + (j + 0.5) * this.cell];
  }

  cellOf(p: Vec2): [number, number] {
    return [Math.floor((p[0] - this.min[0]) / this.cell), Math.floor((p[1] - this.min[1]) / this.cell)];
  }

  inside(i: number, j: number): boolean {
    return i >= 0 && j >= 0 && i < this.w && j < this.h;
  }

  /** Walkable = inside the bounds and not in a wall (radius-widened). */
  walkable(p: Vec2, radius = AGENT_RADIUS): boolean {
    const [i, j] = this.cellOf(p);
    return this.inside(i, j) && !pointInWall(p, this.walls, radius);
  }

  /** A* path from a to b (world points, a first), smoothed by line of sight; null when unreachable. */
  path(a: Vec2, b: Vec2): Vec2[] | null {
    const index = this.index ??= new WallIndex(this.walls, this.min, this.max, AGENT_RADIUS);
    if (!index.blocked(a, b)) return [a, b];
    const [si, sj] = this.nearestFree(...this.cellOf(a));
    const [gi, gj] = this.nearestFree(...this.cellOf(b));
    if (si < 0 || gi < 0) return null;
    const W = this.w, start = sj * W + si, goal = gj * W + gi;
    // The search's scratch arrays live with the grid and are re-stamped per search (a generation counter) instead of re-allocated and
    // filled: the same search, the same path, a fraction of the cost on a big grid with many searches (the combat simulator's enemies).
    const s = this.scratch ??= { g: new Float64Array(W * this.h), from: new Int32Array(W * this.h), stamp: new Int32Array(W * this.h), closed: new Int32Array(W * this.h), gen: 0 };
    const gen = ++s.gen;
    const { g, from, stamp, closed } = s;
    const open = new MinHeap();
    const hcost = (k: number) => Math.hypot((k % W) - gi, Math.floor(k / W) - gj);
    g[start] = 0;
    from[start] = -1;
    stamp[start] = gen;
    open.push(start, hcost(start));
    while (open.size) {
      const k = open.pop();
      if (k === goal) break;
      if (closed[k] === gen) continue;
      closed[k] = gen;
      const ki = k % W, kj = Math.floor(k / W);
      for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) {
        if (!di && !dj) continue;
        const ni = ki + di, nj = kj + dj;
        if (!this.inside(ni, nj)) continue;
        const nk = nj * W + ni;
        if (this.blocked[nk] || closed[nk] === gen) continue;
        if (di && dj && (this.blocked[kj * W + ni] || this.blocked[nj * W + ki])) continue; // no corner cutting
        const ng = g[k] + (di && dj ? Math.SQRT2 : 1);
        if (stamp[nk] !== gen || ng < g[nk]) { g[nk] = ng; from[nk] = k; stamp[nk] = gen; open.push(nk, ng + hcost(nk)); }
      }
    }
    if (stamp[goal] !== gen && goal !== start) return null;
    const cells: Vec2[] = [];
    for (let k = goal; k !== -1 && k !== start; k = from[k]) cells.push(this.center(k % W, Math.floor(k / W)));
    cells.reverse();
    const raw = [a, ...cells, b];
    // String pulling: keep only the points needed for line of sight.
    const out: Vec2[] = [a];
    let i = 0;
    while (i < raw.length - 1) {
      let j = raw.length - 1;
      while (j > i + 1 && index.blocked(raw[i], raw[j])) j--;
      out.push(raw[j]);
      i = j;
    }
    return out;
  }

  private nearestFree(i: number, j: number): [number, number] {
    for (let r = 0; r < 6; r++) for (let dj = -r; dj <= r; dj++) for (let di = -r; di <= r; di++) {
      const ni = i + di, nj = j + dj;
      if (this.inside(ni, nj) && !this.blocked[nj * this.w + ni]) return [ni, nj];
    }
    return [-1, -1];
  }
}

class MinHeap {
  private keys: number[] = [];
  private pri: number[] = [];
  get size(): number { return this.keys.length; }
  push(k: number, p: number): void {
    this.keys.push(k); this.pri.push(p);
    let i = this.keys.length - 1;
    while (i > 0) {
      const parent = (i - 1) >> 1;
      if (this.pri[parent] <= this.pri[i]) break;
      this.swap(i, parent); i = parent;
    }
  }
  pop(): number {
    const top = this.keys[0];
    const lk = this.keys.pop()!, lp = this.pri.pop()!;
    if (this.keys.length) {
      this.keys[0] = lk; this.pri[0] = lp;
      let i = 0;
      for (;;) {
        const l = 2 * i + 1, r = l + 1;
        let m = i;
        if (l < this.keys.length && this.pri[l] < this.pri[m]) m = l;
        if (r < this.keys.length && this.pri[r] < this.pri[m]) m = r;
        if (m === i) break;
        this.swap(i, m); i = m;
      }
    }
    return top;
  }
  private swap(a: number, b: number): void {
    const k = this.keys[a], p = this.pri[a];
    this.keys[a] = this.keys[b]; this.pri[a] = this.pri[b];
    this.keys[b] = k; this.pri[b] = p;
  }
}
