// Training Ground / movement: the pure formation rules (FORMATION_CONTRACT.md 2). Same names and maths as the C++ ports
// (FormationShapeRules / FormationSectorRules / FormationTerrainRules / FormationWallRules); the golden vectors exported by
// `npm run golden` prove the two agree. Design: docs/design/FORMATION_SECTORS.md.
// Angles in the config are relative to the leader's heading (or the wall's outward normal for wall.*), positive = right.

import type { FormationConfig } from './config.ts';
import { cfg } from './config.ts';
import type { Segment, Vec2 } from './geometry.ts';
import { add, closestOnSegment, dirOf, dist, localToWorld, normYaw, raycastWalls, scale, sub, yawOf } from './geometry.ts';
import type { Formation } from './layout.ts';

/** K commander (leader), P right, L left, Z rear (with 2 members: the pair partner behind the right shoulder). */
export type Role = 'K' | 'P' | 'L' | 'Z';
export type Mode = 'walk' | 'run' | 'halt' | 'wall';
export type TerrainClass = 'open' | 'wall_left' | 'wall_right' | 'corridor' | 'at_wall';

export interface Slot {
  role: Role;
  pos: Vec2;
}

export interface WallSlot extends Slot {
  yaw: number;
}

/** Roles present for a member count, in slot order (index 0 = the leader). */
export function rolesFor(memberCount: number): Role[] {
  if (memberCount <= 1) return ['K'];
  if (memberCount === 2) return ['K', 'Z'];
  if (memberCount === 3) return ['K', 'P', 'L'];
  return ['K', 'P', 'L', 'Z'];
}

// ---------------------------------------------------------------------------------------------------------------------
// FormationShapeRules::Slots

/** Local offset (forward, right) of a role, cm. */
function localSlot(formation: Formation, role: Role, n: number, c: FormationConfig): Vec2 {
  if (role === 'K') return [0, 0];
  switch (formation) {
    case 'diamond':
    case 'wedge': {
      if (n === 2) return [-cfg(c, 'pair.back_offset'), cfg(c, 'pair.side_offset')];
      const side = cfg(c, 'slot.side_offset'), back = cfg(c, 'slot.back_offset');
      if (role === 'P') return [-back, side];
      if (role === 'L') return [-back, -side];
      return [-cfg(c, 'diamond.rear_back'), 0];
    }
    case 'blizzard': {
      const side = cfg(c, 'blizzard.side_offset'), back = cfg(c, 'blizzard.back_offset');
      if (n === 2) return [-back, side];
      if (role === 'P') return [-back, side];
      if (role === 'L') return [-back, -side];
      return [-2 * back, 0];
    }
    case 'column': {
      const base = cfg(c, 'column.base_back'), sp = cfg(c, 'column.spacing'), st = cfg(c, 'column.stagger');
      if (n === 2) return [-base, st];
      if (role === 'P') return [-base, st];
      if (role === 'L') return [-base - sp, -st];
      return [-base - 2 * sp, 0];
    }
    case 'line': {
      const s = cfg(c, 'line.spacing');
      if (n === 2) return [0, s];
      if (role === 'P') return [0, s];
      if (role === 'L') return [0, -s];
      return [0, 2 * s];
    }
    case 'echelon_right':
    case 'echelon_left': {
      const i = echelonIndex(formation, role, n);
      const step = cfg(c, 'echelon.step');
      return [-step * i, (formation === 'echelon_right' ? 1 : -1) * step * i];
    }
    case 'hedgehog': {
      const r = cfg(c, 'hedgehog.radius');
      if (n === 2) return [-2 * r, 0];
      if (n === 3) {
        // Triangle round the centre (-r, 0): K at 0 deg, P at +120, L at -120.
        const a = (role === 'P' ? 120 : -120) * (Math.PI / 180);
        return [-r + r * Math.cos(a), r * Math.sin(a)];
      }
      if (role === 'P') return [-r, r];
      if (role === 'L') return [-r, -r];
      return [-2 * r, 0];
    }
    case 'wall':
      return [0, 0]; // not used: Slots('wall') returns [] (FormationWallRules.Layout owns the wall)
  }
}

/** Echelon order: towards the open side the far-side man goes second (minimal travel from the diamond). */
function echelonIndex(formation: Formation, role: Role, n: number): number {
  const order: Role[] = n === 2 ? ['K', 'Z'] : formation === 'echelon_right' ? ['K', 'L', 'P', 'Z'] : ['K', 'P', 'L', 'Z'];
  return Math.max(0, order.indexOf(role));
}

/**
 * World slots (the leader's own slot included, index 0) for a formation. 'wall' returns [] - use FormationWallRules.Layout.
 * With 3 members 'diamond' is the wedge; with 2 the partner Z stands behind the right shoulder.
 */
function Slots(formation: Formation, memberCount: number, leaderPos: Vec2, leaderYaw: number, config: FormationConfig): Slot[] {
  if (formation === 'wall') return [];
  return rolesFor(memberCount).map((role) => ({
    role,
    pos: localToWorld(leaderPos, leaderYaw, localSlot(formation, role, memberCount, config)),
  }));
}

export const FormationShapeRules = { Slots };

// ---------------------------------------------------------------------------------------------------------------------
// FormationSectorRules::LookYaw

/** Scan shaping with dwell at the ends: sign(s) * |s|^exp. */
export function dwell(s: number, exp: number): number {
  return Math.sign(s) * Math.pow(Math.abs(s), exp);
}

function scanValue(center: number, amp: number, period: number, phase: number, t: number, exp: number): number {
  return center + amp * dwell(Math.sin((2 * Math.PI * t) / period + phase), exp);
}

/** Sector centre / scan of a role relative to the reference yaw (heading, or the wall normal in mode 'wall'), degrees. */
function relativeLook(role: Role, formation: Formation, mode: Mode, n: number, t: number, passageWidth: number | null,
  c: FormationConfig): number {
  if (mode === 'run') return 0;
  if (mode === 'wall' || formation === 'wall') {
    if (n === 2) return role === 'K' ? 45 : -45;
    if (role === 'K') return 0;
    if (role === 'L') return cfg(c, 'wall.flank_deg');
    if (role === 'P') return -cfg(c, 'wall.flank_deg');
    return -cfg(c, 'wall.end_deg'); // 4th at the right end by default (FormationWallRules.Layout decides the real end)
  }
  const exp = cfg(c, 'scan.dwell_exp');
  switch (formation) {
    case 'diamond':
    case 'wedge':
    case 'blizzard': {
      if (n === 2 && role === 'Z') {
        const from = cfg(c, 'pair.scan_from'), to = cfg(c, 'pair.scan_to');
        const center = (from + to) / 2, amp = (to - from) / 2;
        return mode === 'halt' ? center : scanValue(center, amp, cfg(c, 'pair.scan_period'), 0, t, exp);
      }
      if (role === 'Z') return 180;
      const period = cfg(c, 'scan.period');
      if (role === 'K') return mode === 'halt' ? 0 : scanValue(0, cfg(c, 'scan.amp_lead'), period, 1.3, t, exp);
      const sign = role === 'P' ? 1 : -1;
      if (mode === 'halt') return sign * cfg(c, 'halt.side_deg');
      return scanValue(sign * cfg(c, 'scan.center_side'), cfg(c, 'scan.amp_side'), period, role === 'P' ? 0 : Math.PI, t, exp);
    }
    case 'column':
      if (role === 'K') return 0;
      if (role === 'Z') return 180;
      // User decision 2026-10-25 (Q1 = A): in a NARROW passage the side men do not look into the walls - P forward over the
      // shoulder, L back at a slant; the +-70 deg herringbone only in a wide column (manual, or no passage at all).
      if (passageWidth !== null && passageWidth < cfg(c, 'column.narrow_width')) {
        return role === 'P' ? cfg(c, 'column.narrow_fwd_deg') : -cfg(c, 'column.narrow_back_deg');
      }
      return (role === 'P' ? 1 : -1) * cfg(c, 'column.side_deg');
    case 'line': {
      const inner = cfg(c, 'line.fan_inner'), outer = cfg(c, 'line.fan_outer');
      if (n === 2) return role === 'K' ? -inner : inner;
      if (n === 3) return role === 'K' ? 0 : role === 'P' ? outer : -outer;
      return role === 'K' ? -inner : role === 'P' ? inner : role === 'L' ? -outer : outer;
    }
    case 'echelon_right':
    case 'echelon_left': {
      const keys = ['echelon.sector_lead', 'echelon.sector_2', 'echelon.sector_3', 'echelon.sector_4'];
      const i = echelonIndex(formation, role, n);
      return (formation === 'echelon_right' ? 1 : -1) * cfg(c, keys[i]);
    }
    case 'hedgehog':
      if (role === 'K') return 0;
      if (n === 2) return 180;
      if (n === 3) return role === 'P' ? 120 : -120;
      return role === 'P' ? 90 : role === 'L' ? -90 : 180;
  }
}

/**
 * World yaw a member looks at. timeInPatrol drives the patrol scans (side men 12 -> 3 / 9 -> 12 in counter-phase with dwell,
 * the pair partner 3 -> 6 -> 9); mode 'run' = everyone on the heading; mode 'halt' = sector centres; mode 'wall': leaderYaw is
 * the wall's outward normal (FormationWallRules.Layout gives the full 4-man wall answer). passageWidth: the free width of the
 * passage the leader is in (FormationTerrainRules.PassageWidth; null = not between two walls) - a column narrower than
 * column.narrow_width uses the narrow sectors (v1.1).
 */
function LookYaw(role: Role, formation: Formation, mode: Mode, memberCount: number, timeInPatrol: number, leaderYaw: number,
  passageWidth: number | null, config: FormationConfig): number {
  return normYaw(leaderYaw + relativeLook(role, formation, mode, memberCount, timeInPatrol, passageWidth, config));
}

export const FormationSectorRules = { LookYaw };

// ---------------------------------------------------------------------------------------------------------------------
// FormationTerrainRules::Classify

/** Probe directions relative to the heading, degrees: side rays (forward-side, side, back-side). */
const SIDE_RAYS = [60, 90, 120];
const AHEAD_RAYS = [0, -20, 20];

/**
 * v1.1 (coordinator 2026-10-25, Q3): a wall segment whose direction is within `ignorePerpDeg` of perpendicular to the heading is
 * a wall AHEAD (or behind), not beside - the side probes ignore it (else a dead end ahead read as side walls and flipped the
 * diamond into a column / echelon just before arriving).
 */
export function isAcrossHeading(w: Segment, yaw: number, ignorePerpDeg: number): boolean {
  const along = Math.abs(normYaw(yawOf(sub(w.b, w.a)) - yaw)); // 0..180
  const offParallel = Math.min(along, 180 - along); // 0 = parallel to the heading, 90 = perpendicular
  return offParallel >= 90 - ignorePerpDeg;
}

/** Free lateral distance to a wall on one side (min over the side rays, measured across the heading), or Infinity. */
export function sideClearance(walls: readonly Segment[], pos: Vec2, yaw: number, side: 1 | -1, reach: number, ignorePerpDeg = 0): number {
  const beside = ignorePerpDeg > 0 ? walls.filter((w) => !isAcrossHeading(w, yaw, ignorePerpDeg)) : walls;
  let best = Infinity;
  for (const a of SIDE_RAYS) {
    const s = Math.sin((a * Math.PI) / 180);
    const hit = raycastWalls(pos, dirOf(yaw + side * a), beside, reach / s);
    if (Number.isFinite(hit)) best = Math.min(best, hit * s);
  }
  return best;
}

/** The raw class of the spot (no hysteresis). v1.1: the ahead / at_wall rays first, then the side rays (walls across ignored). */
export function rawClass(walls: readonly Segment[], pos: Vec2, yaw: number, c: FormationConfig): TerrainClass {
  const atWall = cfg(c, 'classify.at_wall_dist');
  for (const a of AHEAD_RAYS) if (Number.isFinite(raycastWalls(pos, dirOf(yaw + a), walls, atWall))) return 'at_wall';
  const reach = cfg(c, 'classify.probe_side');
  const ignore = cfg(c, 'classify.side_ignore_perp_deg');
  const right = sideClearance(walls, pos, yaw, 1, reach, ignore);
  const left = sideClearance(walls, pos, yaw, -1, reach, ignore);
  const rightBlocked = right <= reach, leftBlocked = left <= reach;
  if (rightBlocked && leftBlocked) return left + right < cfg(c, 'classify.corridor_width') ? 'corridor' : 'open';
  if (leftBlocked) return 'wall_left';
  if (rightBlocked) return 'wall_right';
  return 'open';
}

/**
 * v1.1: the free width between the walls on both sides (left + right clearance, walls across the heading ignored), or null
 * when a side is open (not in a passage). Feeds LookYaw's narrow / wide column.
 */
function PassageWidth(walls: readonly Segment[], pos: Vec2, yaw: number, c: FormationConfig): number | null {
  const reach = cfg(c, 'classify.probe_side'), ignore = cfg(c, 'classify.side_ignore_perp_deg');
  const right = sideClearance(walls, pos, yaw, 1, reach, ignore);
  const left = sideClearance(walls, pos, yaw, -1, reach, ignore);
  return right <= reach && left <= reach ? left + right : null;
}

/**
 * Terrain class at the leader with hysteresis: a class is kept for at least classify.hold_time before it may change
 * (previousClass null = no history, the raw class at once).
 */
function Classify(walls: readonly Segment[], leaderPos: Vec2, leaderYaw: number, previousClass: TerrainClass | null,
  timeInClass: number, config: FormationConfig): TerrainClass {
  const raw = rawClass(walls, leaderPos, leaderYaw, config);
  if (previousClass === null || raw === previousClass) return raw;
  return timeInClass < cfg(config, 'classify.hold_time') ? previousClass : raw;
}

/** Formation for a terrain class: open -> diamond (4) / wedge (3 or fewer), a wall on one side -> echelon to the open side. */
function FormationFor(terrain: TerrainClass, memberCount: number): Formation {
  switch (terrain) {
    case 'open': return memberCount >= 4 ? 'diamond' : 'wedge';
    case 'wall_left': return 'echelon_right';
    case 'wall_right': return 'echelon_left';
    case 'corridor': return 'column';
    case 'at_wall': return 'wall';
  }
}

export const FormationTerrainRules = { Classify, FormationFor, PassageWidth };

// ---------------------------------------------------------------------------------------------------------------------
// FormationWallRules::Layout

/**
 * Members along a wall facing the open side (user decision: nobody looks into the wall). K on the clicked slot looks along
 * the outward normal N; L (left when facing the wall) N + wall.flank_deg (8 o'clock), P N - wall.flank_deg (4 o'clock).
 * 4 members: the 4th (Z) at the end with more room (roomRight >= roomLeft -> right; the caller breaks a tie by the 4th man's
 * travel), looking N -/+ wall.end_deg, his neighbour turned to the centre (N -/+ wall.neighbour_deg).
 * 2 members: the partner on the roomier side; K N +/- 45, partner N -/+ 45.
 */
function Layout(wall: Segment, clickedSlot: Vec2, memberCount: number, roomLeft: number, roomRight: number,
  config: FormationConfig): WallSlot[] {
  const c = closestOnSegment(clickedSlot, wall.a, wall.b).point;
  let n = sub(clickedSlot, c);
  if (Math.hypot(n[0], n[1]) < 1e-6) {
    const ab = sub(wall.b, wall.a);
    n = [-ab[1], ab[0]];
  }
  const normalYaw = yawOf(n);
  const N = dirOf(normalYaw);
  const k = add(c, scale(N, (wall.thickness ?? 0) / 2 + cfg(config, 'wall.face_offset')));
  const right = dirOf(normalYaw + 180 + 90); // right hand when facing the wall (yaw of -N, + 90)
  const sp = cfg(config, 'wall.spacing');
  const at = (steps: number): Vec2 => add(k, scale(right, steps * sp));
  const flank = cfg(config, 'wall.flank_deg'), nb = cfg(config, 'wall.neighbour_deg'), end = cfg(config, 'wall.end_deg');
  const y = (rel: number) => normYaw(normalYaw + rel);
  const rightEnd = roomRight >= roomLeft;
  if (memberCount <= 1) return [{ role: 'K', pos: k, yaw: y(0) }];
  if (memberCount === 2) {
    return rightEnd
      ? [{ role: 'K', pos: k, yaw: y(45) }, { role: 'Z', pos: at(1), yaw: y(-45) }]
      : [{ role: 'K', pos: k, yaw: y(-45) }, { role: 'Z', pos: at(-1), yaw: y(45) }];
  }
  if (memberCount === 3) {
    return [{ role: 'K', pos: k, yaw: y(0) }, { role: 'P', pos: at(1), yaw: y(-flank) }, { role: 'L', pos: at(-1), yaw: y(flank) }];
  }
  return rightEnd
    ? [{ role: 'K', pos: k, yaw: y(0) }, { role: 'P', pos: at(1), yaw: y(-nb) }, { role: 'L', pos: at(-1), yaw: y(flank) },
      { role: 'Z', pos: at(2), yaw: y(-end) }]
    : [{ role: 'K', pos: k, yaw: y(0) }, { role: 'P', pos: at(1), yaw: y(-flank) }, { role: 'L', pos: at(-1), yaw: y(nb) },
      { role: 'Z', pos: at(-2), yaw: y(end) }];
}

/** Free wall length beyond a point on each side (left / right when facing the wall), cm - the room Layout compares. */
export function wallRoom(wall: Segment, clickedSlot: Vec2): { roomLeft: number; roomRight: number } {
  const c = closestOnSegment(clickedSlot, wall.a, wall.b).point;
  const n = sub(clickedSlot, c);
  const right = dirOf(yawOf(n) + 270);
  const toA = sub(wall.a, c), toB = sub(wall.b, c);
  const sideOf = (v: Vec2) => v[0] * right[0] + v[1] * right[1];
  const ra = sideOf(toA), rb = sideOf(toB);
  return { roomRight: Math.max(ra, rb, 0), roomLeft: Math.max(-ra, -rb, 0) };
}

export const FormationWallRules = { Layout };

/** Distance helper re-exported for the checks. */
export { dist };
