// Training Ground / combat simulator: the WORLD a run plays in, built once per layout (docs/design/levelgen/COMBAT_SIM.md "Model"):
// the obstacle boxes the pressure reads (every ground wall / fence / cover >= 50 cm tall as an oriented box - UCombatPressureSubsystem::
// GatherObstacles), the 2.5D sight blockers with their heights (the elevated-vision rule of levelgen/evaluate.ts: a line of sight is blocked
// only where an obstacle's top reaches the line's height there), a cell grid over them for fast line-of-sight tests, the enemies' ground
// nav grid (the lab's A*; tall covers block it too), the bounds (the navmesh bounds shrunk by the wall clearance), the hiding-spot
// candidates (AmbushSpotRules) and the zones for the heat map. Pure (no DOM / Node).

import type { Segment, Vec2 } from '../movement/geometry.ts';
import { dirOf, dist, pointInPoly } from '../movement/geometry.ts';
import type { Layout } from '../movement/layout.ts';
import { AGENT_RADIUS, NavGrid } from '../movement/nav.ts';
import type { GenCover, GenLayout } from '../levelgen/schema.ts';
import { LEVELGEN_SCHEMA, levelZ, toFormationLayout } from '../levelgen/schema.ts';
import type { PressureConfig } from './config.ts';
import type { AmbushSpot, Bounds2, PressureBox } from './rules.ts';
import { AmbushSpotRules } from './rules.ts';

/** A sight blocker: a vertical rectangle from `bottom` to `top` over the segment a-b (cm). */
export interface SightSeg { a: Vec2; b: Vec2; bottom: number; top: number }

/** Covers at least this tall block a standing man's sight and the enemies' walk (levelgen/evaluate.ts). */
export const TALL_COVER_CM = 140;
/** Obstacle meshes shorter than this are floor slabs / kerbs to the pressure (GatherObstacles). */
const MIN_OBSTACLE_CM = 50;

/** A uniform cell grid over the sight blockers: a line-of-sight test walks the cells the line crosses and tests only their segments. */
export class SightGrid {
  readonly cell: number;
  readonly min: Vec2;
  readonly w: number;
  readonly h: number;
  readonly segs: readonly SightSeg[];
  private readonly cells: Int32Array[];
  private readonly stamp: Int32Array;
  private tick = 0;

  constructor(segs: readonly SightSeg[], min: Vec2, max: Vec2, cell = 500) {
    this.cell = cell;
    // a margin: eyes / enemies may stand a little outside the layout bounds
    this.min = [min[0] - 2 * cell, min[1] - 2 * cell];
    this.w = Math.max(1, Math.ceil((max[0] - min[0]) / cell) + 4);
    this.h = Math.max(1, Math.ceil((max[1] - min[1]) / cell) + 4);
    this.segs = segs;
    const lists: number[][] = Array.from({ length: this.w * this.h }, () => []);
    segs.forEach((s, si) => {
      const i0 = this.ix(Math.min(s.a[0], s.b[0])), i1 = this.ix(Math.max(s.a[0], s.b[0]));
      const j0 = this.jy(Math.min(s.a[1], s.b[1])), j1 = this.jy(Math.max(s.a[1], s.b[1]));
      for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) lists[j * this.w + i].push(si);
    });
    this.cells = lists.map((l) => Int32Array.from(l));
    this.stamp = new Int32Array(segs.length);
  }

  private ix(x: number): number { return Math.min(this.w - 1, Math.max(0, Math.floor((x - this.min[0]) / this.cell))); }
  private jy(y: number): number { return Math.min(this.h - 1, Math.max(0, Math.floor((y - this.min[1]) / this.cell))); }

  /**
   * True when nothing blocks the straight line from the eye (ax, ay, az) to the target (bx, by, bz): a blocker crossing the line's
   * ground track blocks only when the line's height there lies within [bottom, top] (an eye on a roof sees over a lower wall).
   */
  clear(ax: number, ay: number, az: number, bx: number, by: number, bz: number): boolean {
    const dx = bx - ax, dy = by - ay;
    const L = Math.hypot(dx, dy);
    if (L < 1e-6) return true;
    this.tick++;
    const stamp = this.stamp, tick = this.tick, segs = this.segs;
    // Amanatides-Woo walk through the cells along the line.
    let i = this.ix(ax), j = this.jy(ay);
    const i1 = this.ix(bx), j1 = this.jy(by);
    const stepI = dx > 0 ? 1 : -1, stepJ = dy > 0 ? 1 : -1;
    const tdx = dx !== 0 ? Math.abs(this.cell / dx) : Infinity, tdy = dy !== 0 ? Math.abs(this.cell / dy) : Infinity;
    const cx = this.min[0] + (i + (dx > 0 ? 1 : 0)) * this.cell, cy = this.min[1] + (j + (dy > 0 ? 1 : 0)) * this.cell;
    let tx = dx !== 0 ? (cx - ax) / dx : Infinity, ty = dy !== 0 ? (cy - ay) / dy : Infinity;
    for (let guard = 0; guard < this.w + this.h + 2; guard++) {
      const list = this.cells[j * this.w + i];
      for (let k = 0; k < list.length; k++) {
        const si = list[k];
        if (stamp[si] === tick) continue;
        stamp[si] = tick;
        const s = segs[si];
        const ex = s.b[0] - s.a[0], ey = s.b[1] - s.a[1];
        const denom = dx * ey - dy * ex;
        if (Math.abs(denom) < 1e-9) continue;
        const ox = s.a[0] - ax, oy = s.a[1] - ay;
        const t = (ox * ey - oy * ex) / denom; // along the line 0..1
        const u = (ox * dy - oy * dx) / denom; // along the blocker 0..1
        if (t <= 1e-6 || t >= 1 - 1e-6 || u < 0 || u > 1) continue;
        const z = az + (bz - az) * t;
        if (z >= s.bottom && z <= s.top) return false;
      }
      if (i === i1 && j === j1) break;
      if (tx < ty) { tx += tdx; i += stepI; } else { ty += tdy; j += stepJ; }
      if (i < 0 || j < 0 || i >= this.w || j >= this.h) break;
    }
    return true;
  }
}

/** The four sides of a cover's yawed footprint. */
export function coverOutline(c: GenCover): Vec2[] {
  const hx = c.size[0] / 2, hy = c.size[1] / 2, d = dirOf(c.yaw), r: Vec2 = [-d[1], d[0]];
  const corner = (sx: number, sy: number): Vec2 => [c.pos[0] + d[0] * hx * sx + r[0] * hy * sy, c.pos[1] + d[1] * hx * sx + r[1] * hy * sy];
  return [corner(1, 1), corner(-1, 1), corner(-1, -1), corner(1, -1)];
}

/** A wall segment as the pressure's oriented box (the builder's cube: the segment's length along its axis, the thickness across). */
export function wallBox(a: Vec2, b: Vec2, thickness: number, height: number): PressureBox {
  const d: Vec2 = [b[0] - a[0], b[1] - a[1]];
  const L = Math.hypot(d[0], d[1]) || 1;
  return { center: [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2], axisX: [d[0] / L, d[1] / L], halfSize: [L / 2, Math.max(1, thickness) / 2], heightCm: height };
}

export const coverBox = (c: GenCover): PressureBox => ({ center: c.pos, axisX: dirOf(c.yaw), halfSize: [c.size[0] / 2, c.size[1] / 2], heightCm: c.size[2] });

/** A plain lab layout (codex.formation_layout/1) as a flat generated layout: walls only, the first route mandatory. */
export function labToGenLayout(l: Layout, extras: { towers?: { pos: Vec2; size: [number, number, number] }[]; ambushPoints?: Vec2[] } = {}): GenLayout {
  const covers: GenCover[] = (extras.towers ?? []).map((t, i) => ({ id: `tower_${i}`, pos: t.pos, yaw: 0, size: t.size, kind: 'crate' }));
  return {
    schema: LEVELGEN_SCHEMA, name: l.name, profile: 'lab', seed: 0, params: { size_m: [(l.bounds.max[1] - l.bounds.min[1]) / 100, (l.bounds.max[0] - l.bounds.min[0]) / 100], density: 1, variants: 0, verticality: 0 },
    bounds: l.bounds, ground_z: l.ground_z, spawn: l.spawn, end: l.routes[0]?.points[l.routes[0].points.length - 1] ?? l.spawn.pos,
    walls: l.walls.map((w) => ({ ...w, kind: 'wall' as const })), buildings: [], ladders: [], covers,
    zones: l.zones.map((z) => ({ ...z })), routes: l.routes.map((r, i) => ({ ...r, mandatory: i === 0 })),
    spawn_points: [], ambush_points: (extras.ambushPoints ?? []).map((p, i) => ({ id: `amb_${i}`, pos: p })), patrol_routes: [],
  };
}

/** The simulation world of one layout (shared by every seed). */
export class SimWorld {
  readonly layout: GenLayout;
  readonly config: PressureConfig;
  /** The pressure's obstacle boxes (GatherObstacles): ground walls + every cover >= 50 cm. */
  readonly boxes: readonly PressureBox[];
  readonly sight: SightGrid;
  /** The enemies' ground nav (walls + tall covers). */
  readonly nav: NavGrid;
  /** The squad's lab layout of the ground layer (walls only, the formation simulator's world). */
  readonly ground: Layout;
  readonly groundNav: NavGrid;
  /** The navmesh bounds shrunk by the wall clearance (null = no bounds). */
  readonly bounds: Bounds2 | null;
  readonly hideSpots: readonly AmbushSpot[];
  readonly hideClearanceCm: number;

  constructor(layout: GenLayout, config: PressureConfig) {
    this.layout = layout;
    this.config = config;
    const groundWalls = layout.walls.filter((w) => (w.floor ?? 0) === 0);
    const boxes: PressureBox[] = groundWalls.filter((w) => w.height >= MIN_OBSTACLE_CM).map((w) => wallBox(w.a, w.b, w.thickness, w.height));
    for (const c of layout.covers) if (c.size[2] >= MIN_OBSTACLE_CM) boxes.push(coverBox(c));
    this.boxes = boxes;
    const sight: SightSeg[] = [];
    for (const w of layout.walls) {
      const bottom = levelZ(w.floor ?? 0);
      sight.push({ a: w.a, b: w.b, bottom, top: bottom + w.height });
    }
    for (const c of layout.covers) {
      if (c.size[2] < TALL_COVER_CM) continue;
      const o = coverOutline(c);
      for (let i = 0; i < 4; i++) sight.push({ a: o[i], b: o[(i + 1) % 4], bottom: 0, top: c.size[2] });
    }
    this.sight = new SightGrid(sight, layout.bounds.min, layout.bounds.max);
    this.ground = toFormationLayout(layout, 'ground');
    const groundSegs: Segment[] = this.ground.walls.map((w) => ({ a: w.a, b: w.b, thickness: w.thickness }));
    this.groundNav = new NavGrid(groundSegs, layout.bounds.min, layout.bounds.max);
    const navSegs: Segment[] = [...groundSegs];
    for (const c of layout.covers) {
      if (c.size[2] < TALL_COVER_CM) continue;
      const o = coverOutline(c);
      for (let i = 0; i < 4; i++) navSegs.push({ a: o[i], b: o[(i + 1) % 4], thickness: 0 });
    }
    this.nav = new NavGrid(navSegs, layout.bounds.min, layout.bounds.max);
    const c = config.wallClearanceCm;
    this.bounds = { min: [layout.bounds.min[0] + c, layout.bounds.min[1] + c], max: [layout.bounds.max[0] - c, layout.bounds.max[1] - c] };
    this.hideClearanceCm = AmbushSpotRules.hideClearanceCm(config.wallClearanceCm, config.spots);
    // Authored: the level JSON's points + the layout's own ambush points (the builder's CodexAmbushPoint actors), merged by the rules.
    const authored: Vec2[] = [...config.authoredPoints];
    for (const a of layout.ambush_points) if (!authored.some((q) => dist(q, a.pos) <= config.spots.mergeCm)) authored.push(a.pos);
    this.hideSpots = AmbushSpotRules.candidates(boxes, authored, config.spots);
  }

  /** The pressure's ProjectFeet on the ground: on the walkable ground inside the bounds. */
  projectFeet(p: Vec2): boolean {
    return this.nav.walkable(p, AGENT_RADIUS) && this.inBounds(p, -this.config.wallClearanceCm);
  }

  inBounds(p: Vec2, margin = 0): boolean {
    const b = this.layout.bounds;
    return p[0] >= b.min[0] - margin && p[0] <= b.max[0] + margin && p[1] >= b.min[1] - margin && p[1] <= b.max[1] + margin;
  }

  /** Line of sight between two points at heights (cm over the ground). */
  los(a: Vec2, az: number, b: Vec2, bz: number): boolean {
    return this.sight.clear(a[0], a[1], az, b[0], b[1], bz);
  }

  /** The zone a point lies in (the layout's first match), or null. */
  zoneAt(p: Vec2): string | null {
    for (const z of this.layout.zones) if (pointInPoly(p, z.poly)) return z.id;
    return null;
  }

  /** The ground path (A*), or null when unreachable. */
  path(a: Vec2, b: Vec2): Vec2[] | null {
    return this.nav.path(a, b);
  }
}
