// Training Ground / movement: a walkable grid over a layout and A* paths (stand-in for the navmesh; the Unreal map bakes the
// real one). Cells blocked where a wall widened by the agent radius covers the cell centre.

import type { Segment, Vec2 } from './geometry.ts';
import { distToWall, pointInWall, segmentBlocked } from './geometry.ts';

export const AGENT_RADIUS = 35;

export class NavGrid {
  readonly cell: number;
  readonly min: Vec2;
  readonly w: number;
  readonly h: number;
  readonly blocked: Uint8Array;
  readonly walls: readonly Segment[];

  constructor(walls: readonly Segment[], min: Vec2, max: Vec2, cell = 50, radius = AGENT_RADIUS) {
    this.cell = cell;
    this.min = min;
    this.walls = walls;
    this.w = Math.ceil((max[0] - min[0]) / cell);
    this.h = Math.ceil((max[1] - min[1]) / cell);
    this.blocked = new Uint8Array(this.w * this.h);
    for (let j = 0; j < this.h; j++) for (let i = 0; i < this.w; i++) {
      const c = this.center(i, j);
      for (const wall of walls) if (distToWall(c, wall) < radius) { this.blocked[j * this.w + i] = 1; break; }
    }
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
    if (!segmentBlocked(a, b, this.walls, AGENT_RADIUS)) return [a, b];
    const [si, sj] = this.nearestFree(...this.cellOf(a));
    const [gi, gj] = this.nearestFree(...this.cellOf(b));
    if (si < 0 || gi < 0) return null;
    const W = this.w, start = sj * W + si, goal = gj * W + gi;
    const g = new Float64Array(W * this.h).fill(Infinity);
    const from = new Int32Array(W * this.h).fill(-1);
    const open = new MinHeap();
    const hcost = (k: number) => Math.hypot((k % W) - gi, Math.floor(k / W) - gj);
    g[start] = 0;
    open.push(start, hcost(start));
    const closed = new Uint8Array(W * this.h);
    while (open.size) {
      const k = open.pop();
      if (k === goal) break;
      if (closed[k]) continue;
      closed[k] = 1;
      const ki = k % W, kj = Math.floor(k / W);
      for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) {
        if (!di && !dj) continue;
        const ni = ki + di, nj = kj + dj;
        if (!this.inside(ni, nj)) continue;
        const nk = nj * W + ni;
        if (this.blocked[nk] || closed[nk]) continue;
        if (di && dj && (this.blocked[kj * W + ni] || this.blocked[nj * W + ki])) continue; // no corner cutting
        const ng = g[k] + (di && dj ? Math.SQRT2 : 1);
        if (ng < g[nk]) { g[nk] = ng; from[nk] = k; open.push(nk, ng + hcost(nk)); }
      }
    }
    if (from[goal] < 0 && goal !== start) return null;
    const cells: Vec2[] = [];
    for (let k = goal; k !== -1 && k !== start; k = from[k]) cells.push(this.center(k % W, Math.floor(k / W)));
    cells.reverse();
    const raw = [a, ...cells, b];
    // String pulling: keep only the points needed for line of sight.
    const out: Vec2[] = [a];
    let i = 0;
    while (i < raw.length - 1) {
      let j = raw.length - 1;
      while (j > i + 1 && segmentBlocked(raw[i], raw[j], this.walls, AGENT_RADIUS)) j--;
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
    [this.keys[a], this.keys[b]] = [this.keys[b], this.keys[a]];
    [this.pri[a], this.pri[b]] = [this.pri[b], this.pri[a]];
  }
}
