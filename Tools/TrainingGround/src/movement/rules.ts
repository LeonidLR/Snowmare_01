// Training Ground / movement: the pure formation rules (FORMATION_CONTRACT.md 2). Same names and maths as the C++ ports
// (FormationShapeRules / FormationSectorRules / FormationTerrainRules / FormationWallRules); the golden vectors exported by
// `npm run golden` prove the two agree. Design: docs/design/FORMATION_SECTORS.md.
// Angles in the config are relative to the leader's heading (or the wall's outward normal for wall.*), positive = right.

import type { FormationConfig } from './config.ts';
import { cfg } from './config.ts';
import type { Segment, Vec2 } from './geometry.ts';
import { add, closestOnSegment, dirOf, dist, distToWall, localToWorld, normYaw, raycastWalls, scale, segmentBlocked, sub, yawOf } from './geometry.ts';
import type { Formation } from './layout.ts';

/** K commander (leader), P right, L left, Z rear (with 2 members: the pair partner behind the right shoulder). */
export type Role = 'K' | 'P' | 'L' | 'Z';
/** v1.2: 'forward' = the menu's "All forward" (10:30) - walking, everyone looks at 12 whatever the formation. */
export type Mode = 'walk' | 'run' | 'halt' | 'wall' | 'forward';
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

/**
 * v1.2: the hedgehog round a centre point (the heat source): the leader stands hedgehog.radius in front of the centre along yaw,
 * every member on the ring with his back to the centre, looking out (K yaw, P yaw + 90 / L yaw - 90 / Z yaw + 180 with 4;
 * 3: +-120; 2: K and Z opposite). The integration moves the leader to slot 0 so the centre is the fire.
 */
function HedgehogSlots(center: Vec2, memberCount: number, yaw: number, config: FormationConfig): (Slot & { yaw: number })[] {
  const leaderPos = add(center, scale(dirOf(yaw), cfg(config, 'hedgehog.radius')));
  return Slots('hedgehog', memberCount, leaderPos, yaw, config).map((s) => ({
    ...s, yaw: FormationSectorRules.LookYaw(s.role, 'hedgehog', 'halt', memberCount, 0, yaw, null, config),
  }));
}

/**
 * v2.1: the trail column - the column's slots (column.base_back + i x column.spacing back, column.stagger aside) measured ALONG
 * the leader's path instead of straight back, so the column bends round a corner and passes a wall on his side. trail = the
 * leader's recent positions, oldest first, the last one = the leader now; a trail shorter than the column is extended straight
 * back from its oldest point (along its oldest segment, or against leaderYaw for a single point). Each slot carries the trail's
 * direction there (yaw) - the reference for its look (LookYaw's leaderYaw). Who stands where is FormationAssignRules'
 * job (minimal travel on every formation change), not the slot order's.
 */
function TrailSlots(trail: readonly Vec2[], leaderYaw: number, memberCount: number, config: FormationConfig): (Slot & { yaw: number })[] {
  const leaderPos = trail[trail.length - 1];
  // Segments newest -> oldest, each with its travel direction.
  const segs: { from: Vec2; to: Vec2; len: number; yaw: number }[] = [];
  for (let i = trail.length - 1; i > 0; i--) {
    const L = dist(trail[i - 1], trail[i]);
    if (L > 1e-6) segs.push({ from: trail[i], to: trail[i - 1], len: L, yaw: yawOf(sub(trail[i], trail[i - 1])) });
  }
  const tailYaw = segs.length ? segs[segs.length - 1].yaw : leaderYaw;
  const tail = segs.length ? segs[segs.length - 1].to : leaderPos;
  const back = (d: number): { pos: Vec2; yaw: number } => {
    let left = d;
    for (const sg of segs) {
      if (left <= sg.len) return { pos: add(sg.from, scale(sub(sg.to, sg.from), left / sg.len)), yaw: sg.yaw };
      left -= sg.len;
    }
    return { pos: add(tail, scale(dirOf(tailYaw), -left)), yaw: tailYaw };
  };
  return rolesFor(memberCount).map((role) => {
    if (role === 'K') return { role, pos: leaderPos, yaw: leaderYaw };
    const local = localSlot('column', role, memberCount, config);
    const at = back(-local[0]);
    return { role, pos: add(at.pos, scale(dirOf(at.yaw + 90), local[1])), yaw: at.yaw };
  });
}

export const FormationShapeRules = { Slots, HedgehogSlots, TrailSlots };

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
  if (mode === 'run' || mode === 'forward') return 0;
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
export function rawClass(walls: readonly Segment[], pos: Vec2, yaw: number, destination: Vec2 | null, c: FormationConfig): TerrainClass {
  // v1.2: at_wall only on ARRIVAL - the leader's move ends (he is within classify.arrive_dist of his destination) with a wall
  // ahead within at_wall_dist; walking past a wall or turning at a corridor bend never triggers the wall layout.
  if (isArriving(pos, destination, c)) {
    const atWall = cfg(c, 'classify.at_wall_dist');
    for (const a of AHEAD_RAYS) if (Number.isFinite(raycastWalls(pos, dirOf(yaw + a), walls, atWall))) return 'at_wall';
  }
  const reach = cfg(c, 'classify.probe_side');
  const ignore = cfg(c, 'classify.side_ignore_perp_deg');
  const right = sideClearance(walls, pos, yaw, 1, reach, ignore);
  const left = sideClearance(walls, pos, yaw, -1, reach, ignore);
  const rightBlocked = right <= reach, leftBlocked = left <= reach;
  // v1.2 (user Q3 = A): ANY passage between two walls (both sides blocked within probe_side) is a corridor -> column; the
  // narrow / wide column sectors follow PassageWidth (column.narrow_width). The old width limit (classify.corridor_width) is gone.
  if (rightBlocked && leftBlocked) return 'corridor';
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

/** v1.2: the leader is arriving = his move's destination is set and he is within classify.arrive_dist of it. */
export function isArriving(pos: Vec2, destination: Vec2 | null, c: FormationConfig): boolean {
  return destination !== null && dist(pos, destination) <= cfg(c, 'classify.arrive_dist');
}

/** Classifier state carried tick to tick (v2): the class in force and a looser class waiting for its path distance. */
export interface ClassifyState {
  current: TerrainClass | null;
  candidate: TerrainClass | null;
  /** Leader path walked while `candidate` has been the raw class, cm. */
  candidateDistance: number;
  /**
   * Width of the passage while `current` is a corridor (PassageWidth when measured; HELD with the class after the leader has left
   * the passage, so the column's tail keeps the narrow sectors until the column ends), else null. LookYaw's passageWidth.
   */
  passageWidth: number | null;
}

export const CLASSIFY_START: ClassifyState = { current: null, candidate: null, candidateDistance: 0, passageWidth: null };

/** How tight a class is: open 0, a wall on one side 1, a passage 2, at a wall 3. */
export function classRank(c: TerrainClass): number {
  return c === 'open' ? 0 : c === 'corridor' ? 2 : c === 'at_wall' ? 3 : 1;
}

/**
 * v2 (FORMATION_CONTRACT.md 8, coordinator 2026-10-25): hysteresis in cm of the LEADER'S PATH, asymmetric.
 * - Tighter at once: a raw class of higher rank (a wall beside, a passage, the arrival at a wall) replaces the current one
 *   immediately - the squad must not walk a diamond into a corridor. Leaving at_wall (a new move) is immediate too.
 * - Looser only after classify.hold_distance: a lower (or equal-rank, e.g. wall_left -> wall_right) raw class becomes current
 *   only when it has been the raw class for hold_distance of the leader's path (stepDistance per tick; standing still adds
 *   nothing). A gap in a wall shorter than that never flips the echelon; leaving a passage, the column holds until its tail
 *   (~5 m) is out. Pace-independent (v1's seconds shrank in distance at a slow patrol pace).
 * destination (v1.2) = where the leader's current move ends (null = none). Returns the next state (pure).
 */
function Classify(walls: readonly Segment[], leaderPos: Vec2, leaderYaw: number, destination: Vec2 | null, state: ClassifyState,
  stepDistance: number, config: FormationConfig): ClassifyState {
  const raw = rawClass(walls, leaderPos, leaderYaw, destination, config);
  const measured = raw === 'corridor' ? PassageWidth(walls, leaderPos, leaderYaw, config) : null;
  const next = (current: TerrainClass, candidate: TerrainClass | null, candidateDistance: number): ClassifyState => ({
    current, candidate, candidateDistance,
    passageWidth: current !== 'corridor' ? null : raw === 'corridor' ? measured : state.passageWidth,
  });
  const current = state.current;
  if (current === null || raw === current) return next(raw, null, 0);
  if (current === 'at_wall' || classRank(raw) > classRank(current)) return next(raw, null, 0);
  const walked = (state.candidate === raw ? state.candidateDistance : 0) + Math.max(0, stepDistance);
  if (walked >= cfg(config, 'classify.hold_distance')) return next(raw, null, 0);
  return next(current, raw, walked);
}

/**
 * Formation for a terrain class: open -> diamond (4) / wedge (3 or fewer), or the blizzard tight diamond in a blizzard (v1.2,
 * user Q4 = A: weather = blizzard on OPEN ground only - walls / passages keep their rules); a wall on one side -> echelon to
 * the open side; a passage -> column; arrival at a wall -> the wall layout. The menu choice (hold G) overrides all of it.
 */
function FormationFor(terrain: TerrainClass, memberCount: number, blizzard: boolean): Formation {
  switch (terrain) {
    case 'open': return blizzard ? 'blizzard' : memberCount >= 4 ? 'diamond' : 'wedge';
    case 'wall_left': return 'echelon_right';
    case 'wall_right': return 'echelon_left';
    case 'corridor': return 'column';
    case 'at_wall': return 'wall';
  }
}

/**
 * v2.1: the free width round a man when at least ONE side has a wall body within classify.probe_side, null when both sides are
 * open. Measured to each wall's closest point (body distance; the side = which side of the heading that point lies), so a
 * wall END beside him counts as well as a wall along him - the side rays of the classifier slip past a 40 cm end. An open side
 * counts as probe_side. The trail column's sectors use it per member (a wall beside a man -> the narrow column's sectors,
 * nobody's herringbone look into it).
 */
function SideWidth(walls: readonly Segment[], pos: Vec2, yaw: number, c: FormationConfig): number | null {
  const reach = cfg(c, 'classify.probe_side');
  const h = dirOf(yaw);
  let right = Infinity, left = Infinity;
  for (const w of walls) {
    const q = closestOnSegment(pos, w.a, w.b).point;
    const d = Math.max(0, dist(pos, q) - (w.thickness ?? 0) / 2);
    if (d > reach) continue;
    if (h[0] * (q[1] - pos[1]) - h[1] * (q[0] - pos[0]) >= 0) right = Math.min(right, d); else left = Math.min(left, d);
  }
  if (right > reach && left > reach) return null;
  return Math.min(right, reach) + Math.min(left, reach);
}

export const FormationTerrainRules = { Classify, FormationFor, PassageWidth, SideWidth };

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

/**
 * Design "tie -> the side he reaches with less travel" (C++ FormationWallRules::BreakRoomTie): when the rooms are equal and the
 * 4th man's travel to the left end is shorter, RoomLeft gets 1 cm more so Layout puts him left (Layout sends a tie right).
 */
function BreakRoomTie(roomLeft: number, roomRight: number, travelLeft: number, travelRight: number): { roomLeft: number; roomRight: number } {
  return roomLeft === roomRight && travelLeft < travelRight ? { roomLeft: roomLeft + 1, roomRight } : { roomLeft, roomRight };
}

export const FormationWallRules = { Layout, BreakRoomTie };

// ---------------------------------------------------------------------------------------------------------------------
// FormationGuardRules (v2.1, FORMATION_CONTRACT.md 10): no formation may cut the squad in two with a wall

/**
 * Neighbours in a formation's chain (pairs of roles, the leader's own segments to every slot come on top): echelon in its
 * order, column K-P-L-Z, line L-K-P-Z, diamond / wedge / blizzard P-Z and L-Z, hedgehog round the ring. Roles the member count
 * lacks are skipped (the chain closes over them). 'wall' has none (FormationWallRules.Layout stands along the wall face).
 */
function ChainPairs(formation: Formation, memberCount: number): [Role, Role][] {
  const have = rolesFor(memberCount);
  const chain = (order: Role[], ring = false): [Role, Role][] => {
    const o = order.filter((r) => have.includes(r));
    const pairs: [Role, Role][] = o.slice(1).map((r, i) => [o[i], r]);
    if (ring && o.length > 2) pairs.push([o[o.length - 1], o[0]]);
    return pairs;
  };
  if (memberCount <= 2) return [];
  switch (formation) {
    case 'echelon_right': return chain(['K', 'L', 'P', 'Z']);
    case 'echelon_left': return chain(['K', 'P', 'L', 'Z']);
    case 'column': return chain(['K', 'P', 'L', 'Z']);
    case 'line': return chain(['L', 'K', 'P', 'Z']);
    case 'hedgehog': return chain(['K', 'P', 'Z', 'L'], true);
    case 'diamond':
    case 'wedge':
    case 'blizzard': return memberCount >= 4 ? [['P', 'Z'], ['L', 'Z']] : [];
    case 'wall': return [];
  }
}

/**
 * The roles whose slot a wall cuts off: the segment from the leader to the slot, or a chain segment (ChainPairs) touching it,
 * crosses a wall body widened by guard.clearance. Empty = the formation is whole here. 'wall' is never blocked.
 */
function BlockedRoles(walls: readonly Segment[], formation: Formation, memberCount: number, leaderPos: Vec2, leaderYaw: number,
  config: FormationConfig): Role[] {
  if (formation === 'wall') return [];
  const r = cfg(config, 'guard.clearance');
  const slots = Slots(formation, memberCount, leaderPos, leaderYaw, config);
  const at = (role: Role) => slots.find((s) => s.role === role)!.pos;
  const blocked = new Set<Role>();
  for (const s of slots.slice(1)) if (segmentBlocked(leaderPos, s.pos, walls, r)) blocked.add(s.role);
  for (const [a, b] of ChainPairs(formation, memberCount)) {
    if (!segmentBlocked(at(a), at(b), walls, r)) continue;
    if (a !== 'K') blocked.add(a);
    if (b !== 'K') blocked.add(b);
  }
  return slots.map((s) => s.role).filter((role) => blocked.has(role));
}

/** 0 = the requested formation, 1 = the mirror echelon, 2 = the trail column (FormationShapeRules.TrailSlots). */
export type GuardLevel = 0 | 1 | 2;

export interface GuardState {
  /** The formation asked for (the menu's manual choice, or FormationFor of the terrain); null before the first tick. */
  requested: Formation | null;
  level: GuardLevel;
  /** The formation in effect (= requested at level 0). */
  effective: Formation | null;
  /** A looser level waiting for its path distance (as ClassifyState.candidate), and the leader path walked meanwhile, cm. */
  candidate: GuardLevel | null;
  candidateDistance: number;
}

export const GUARD_START: GuardState = { requested: null, level: 0, effective: null, candidate: null, candidateDistance: 0 };

const mirrorOf = (f: Formation): Formation | null => (f === 'echelon_right' ? 'echelon_left' : f === 'echelon_left' ? 'echelon_right' : null);

function formationAt(requested: Formation, level: GuardLevel): Formation {
  return level === 0 ? requested : level === 1 ? mirrorOf(requested) ?? 'column' : 'column';
}

/** The formation in effect at a guard state, and whether its slots are the trail column's (level 2). */
function Effective(state: GuardState): { formation: Formation | null; trail: boolean } {
  return { formation: state.effective, trail: state.level === 2 };
}

/**
 * v2.1 (user decision 2026-10-25: "either the echelon right, or the herringbone, round the wall right or left"): the formation in
 * effect for a requested one. When a wall cuts the requested formation (BlockedRoles not empty): an echelon takes its mirror if
 * the mirror is whole, anything else (or a cut mirror, or a cut straight column) the TRAIL column: the column whose slots follow
 * the leader's own path (FormationShapeRules.TrailSlots), so it passes the wall - and bends round a corner - on his side (narrow /
 * herringbone sectors by PassageWidth, relative to the trail's direction at each slot). It is the last resort, whole by
 * construction (the leader walked it); 'wall' is never guarded.
 * Anticipation: `ahead` = the leader's pose guard.lookahead further along his path (the caller's path; null = none / standing):
 * a formation counts as whole only when it is whole both here and there, so the squad folds BEFORE a passage entrance (the far
 * man walks into the column outside the passage instead of round its wall end) and opens only when the way ahead is clear too.
 * Hysteresis as the classifier (v2): a fallback (higher level) at once; back towards the request (lower level) only after it has
 * been whole for classify.hold_distance of the leader's path (stepDistance per tick), unless the fallback itself is cut - then
 * at once. A new request (menu pick, terrain class change) starts over at its own level. The request itself is never cleared.
 */
function Resolve(walls: readonly Segment[], requested: Formation, memberCount: number, leaderPos: Vec2, leaderYaw: number,
  ahead: { pos: Vec2; yaw: number } | null, state: GuardState, stepDistance: number, config: FormationConfig): GuardState {
  const whole = (f: Formation) => BlockedRoles(walls, f, memberCount, leaderPos, leaderYaw, config).length === 0 &&
    (!ahead || BlockedRoles(walls, f, memberCount, ahead.pos, ahead.yaw, config).length === 0);
  let raw: GuardLevel = 0;
  if (requested !== 'wall' && !whole(requested)) {
    const mirror = mirrorOf(requested);
    raw = mirror && whole(mirror) ? 1 : 2;
  }
  const at = (level: GuardLevel, candidate: GuardLevel | null = null, candidateDistance = 0): GuardState =>
    ({ requested, level, effective: formationAt(requested, level), candidate, candidateDistance });
  if (state.requested !== requested || raw >= state.level) return at(raw);
  if (state.level !== 2 && !whole(formationAt(requested, state.level))) return at(raw);
  const walked = (state.candidate === raw ? state.candidateDistance : 0) + Math.max(0, stepDistance);
  if (walked >= cfg(config, 'classify.hold_distance')) return at(raw);
  return at(state.level, raw, walked);
}

export const FormationGuardRules = { ChainPairs, BlockedRoles, Resolve, Effective };

// ---------------------------------------------------------------------------------------------------------------------
// FormationBypassRules (v2.1, FORMATION_CONTRACT.md 10.2): which side of a wall the leader - and with him the whole squad - passes

/** A known threat (an enemy seen, or heard / last known position) and the seconds since that contact. */
export interface Threat {
  pos: Vec2;
  ageS: number;
}

/** Wall on the squad's left or right as it goes by (passing round the wall's left end = the wall on the right). */
export type WallSide = 'left' | 'right';

export interface BypassChoice {
  wallOn: WallSide;
  /** The chosen path's length over the shortest one's, minus 1 (0 = the shortest). */
  detour: number;
  /** Which side the shortest path takes. */
  shortest: WallSide;
  /** shortest = no known threat, or the wall hides it on neither / the shortest side; shielded = the detour to hide was taken;
   *  too_long = a side would hide the threat but its detour exceeds bypass.max_detour. */
  reason: 'shortest' | 'shielded' | 'too_long';
  /** The chosen path round the wall, leader to destination (corners BYPASS_RADIUS off the wall body). */
  path: Vec2[];
}

/**
 * cm: the bypass paths round a wall keep this far off its body (corners) - room for the column to wrap the end without its
 * members cutting the corner (and the corner man's distance from the corner when slicing the pie).
 */
export const BYPASS_RADIUS = 150;
/** cm: shielding is sampled along a candidate path at this step. */
const BYPASS_SAMPLE = 50;

/** Winding number of the closed polygon `poly` round point p. */
function winding(poly: readonly Vec2[], p: Vec2): number {
  let w = 0;
  for (let i = 0; i < poly.length; i++) {
    const a = poly[i], b = poly[(i + 1) % poly.length];
    const side = (b[0] - a[0]) * (p[1] - a[1]) - (p[0] - a[0]) * (b[1] - a[1]);
    if (a[1] <= p[1]) { if (b[1] > p[1] && side > 0) w++; } else if (b[1] <= p[1] && side < 0) w--;
  }
  return w;
}

/**
 * Which side a path from its first to its last point passes a wall (the path must not cross the wall body): the wall end
 * farther off the straight line first -> last is the reference R; the straight line has R on its left or right, and the path
 * keeps that side unless the loop path + straight line back winds round R (it went round the other way). For the bot's
 * check 8 too (the leader's real trajectory).
 */
export function wallSideOf(path: readonly Vec2[], wall: Segment): WallSide {
  const P = path[0], D = path[path.length - 1];
  const d = sub(D, P);
  const off = (q: Vec2) => d[0] * (q[1] - P[1]) - d[1] * (q[0] - P[0]);
  const R = Math.abs(off(wall.a)) >= Math.abs(off(wall.b)) ? wall.a : wall.b;
  // UE frame (x forward, y right): a positive cross (d x (R - P)) puts R on the right of the line.
  const straight: WallSide = off(R) > 0 ? 'right' : 'left';
  if (winding(path, R) === 0) return straight;
  return straight === 'right' ? 'left' : 'right';
}

const polyLength = (p: readonly Vec2[]) => p.slice(1).reduce((acc, q, i) => acc + dist(p[i], q), 0);

function samplePolyline(p: readonly Vec2[], step: number): Vec2[] {
  const out: Vec2[] = [p[0]];
  for (let i = 1; i < p.length; i++) {
    const L = dist(p[i - 1], p[i]);
    for (let s = step; s < L; s += step) out.push(add(p[i - 1], scale(sub(p[i], p[i - 1]), s / L)));
    out.push(p[i]);
  }
  return out;
}

/**
 * User decisions 2026-10-25 ("B", "the squad never splits"): the side of `wall` the leader passes on his way to `destination`.
 * Candidates: the straight line and the paths over one or two of the wall's four corners (BYPASS_RADIUS off the body), no leg
 * coming closer to the body than BYPASS_RADIUS (or than its own end point is); the shortest per side. No known threat (ageS within bypass.threat_memory_s) -> the shortest side. With threats:
 * the side whose path has the larger share of samples (BYPASS_SAMPLE) hidden from every known threat by this wall is the
 * shielded one; it is taken when its length is at most (1 + bypass.max_detour) x the shortest, else the shortest stays.
 */
function ChooseSide(wall: Segment, leaderPos: Vec2, destination: Vec2, threats: readonly Threat[], config: FormationConfig): BypassChoice {
  const L = dist(wall.a, wall.b);
  const u: Vec2 = L > 1e-6 ? scale(sub(wall.b, wall.a), 1 / L) : [1, 0];
  const n: Vec2 = [-u[1], u[0]];
  const m = (wall.thickness ?? 0) / 2 + BYPASS_RADIUS;
  const corners: Vec2[] = [
    add(add(wall.a, scale(u, -m)), scale(n, m)), add(add(wall.a, scale(u, -m)), scale(n, -m)),
    add(add(wall.b, scale(u, m)), scale(n, m)), add(add(wall.b, scale(u, m)), scale(n, -m)),
  ];
  // A leg keeps BYPASS_RADIUS off the body (less only as close as its own end - the leader / the destination - already is).
  const body = (p: Vec2) => Math.max(0, distToWall(p, wall) - (wall.thickness ?? 0) / 2);
  const clear = (p: Vec2, q: Vec2) => !segmentBlocked(p, q, [wall], Math.max(0, Math.min(BYPASS_RADIUS, body(p), body(q)) - 1));
  const candidates: Vec2[][] = [[leaderPos, destination]];
  for (const c1 of corners) {
    candidates.push([leaderPos, c1, destination]);
    for (const c2 of corners) if (c2 !== c1) candidates.push([leaderPos, c1, c2, destination]);
  }
  const best: Record<WallSide, { path: Vec2[]; len: number } | null> = { left: null, right: null };
  for (const path of candidates) {
    if (!path.slice(1).every((q, i) => clear(path[i], q))) continue;
    const side = wallSideOf(path, wall), len = polyLength(path);
    if (!best[side] || len < best[side]!.len - 1e-6) best[side] = { path, len };
  }
  const sides = (['left', 'right'] as WallSide[]).filter((s) => best[s]);
  if (sides.length === 0) return { wallOn: 'left', detour: 0, shortest: 'left', reason: 'shortest', path: [leaderPos, destination] };
  const shortest = sides.reduce((a, b) => (best[b]!.len < best[a]!.len - 1e-6 ? b : a));
  const minLen = best[shortest]!.len;
  const pick = (s: WallSide, reason: BypassChoice['reason']): BypassChoice =>
    ({ wallOn: s, detour: minLen > 0 ? best[s]!.len / minLen - 1 : 0, shortest, reason, path: best[s]!.path });
  const known = threats.filter((t) => t.ageS >= 0 && t.ageS <= cfg(config, 'bypass.threat_memory_s'));
  if (known.length === 0 || sides.length < 2) return pick(shortest, 'shortest');
  const hidden = (s: WallSide) => {
    const pts = samplePolyline(best[s]!.path, BYPASS_SAMPLE);
    return pts.filter((p) => known.every((t) => segmentBlocked(p, t.pos, [wall], 0))).length / pts.length;
  };
  const other: WallSide = shortest === 'left' ? 'right' : 'left';
  if (hidden(other) <= hidden(shortest)) return pick(shortest, 'shortest');
  return best[other]!.len <= (1 + cfg(config, 'bypass.max_detour')) * minLen + 1e-6 ? pick(other, 'shielded') : pick(shortest, 'too_long');
}

export const FormationBypassRules = { ChooseSide };

// ---------------------------------------------------------------------------------------------------------------------
// FormationCornerRules (v2.1, FORMATION_CONTRACT.md 10.3): slicing the pie at the end of a wall the squad passes along

/** cm: the corner man's look grazes the wall end this far beyond its body. */
export const CORNER_GRAZE = 50;
/** cm: a wall end within this of another wall is a joint (a corner of a room / corridor), not a free end to look round. */
const CORNER_JOINT = 20;

/** One watched wall end: the wall (index into the walls array), its end (0 = a, 1 = b) and the member looking round it. */
export interface CornerWatch {
  wall: number;
  end: 0 | 1;
  /** Chosen once per corner: the member closest to the end when the watch starts (not the other side's watcher). */
  role: Role;
}

/** At most one watched end on each side of the leader (a corridor exit has two corners). */
export interface CornerState {
  left: CornerWatch | null;
  right: CornerWatch | null;
}

export const CORNER_START: CornerState = { left: null, right: null };

/** Leader distance along the heading to a free wall end beside him that the wall runs back from, or null. */
export function cornerAhead(walls: readonly Segment[], i: number, end: 0 | 1, leaderPos: Vec2, leaderYaw: number, c: FormationConfig): number | null {
  const w = walls[i];
  if (isAcrossHeading(w, leaderYaw, cfg(c, 'classify.side_ignore_perp_deg'))) return null;
  const E = end === 0 ? w.a : w.b, O = end === 0 ? w.b : w.a;
  const h = dirOf(leaderYaw);
  const s = (E[0] - leaderPos[0]) * h[0] + (E[1] - leaderPos[1]) * h[1];
  const back = (O[0] - leaderPos[0]) * h[0] + (O[1] - leaderPos[1]) * h[1];
  const lateral = Math.abs(h[0] * (E[1] - leaderPos[1]) - h[1] * (E[0] - leaderPos[0]));
  if (back >= s || lateral > cfg(c, 'classify.probe_side')) return null;
  for (let j = 0; j < walls.length; j++) {
    if (j === i) continue;
    const o = walls[j];
    const d = dist(E, closestOnSegment(E, o.a, o.b).point);
    if (d <= (o.thickness ?? 0) / 2 + (w.thickness ?? 0) / 2 + CORNER_JOINT) return null;
  }
  return s;
}

/**
 * User decision 2026-10-25 ("corner: yes"): while the squad passes along a wall and nears its free end (the leader within
 * corner.lead_dist before it, until corner.hold_dist past it along his heading), ONE member per side of the leader - the one
 * closest to the end when the watch starts - looks round it: before he is past the end, at the corner itself (CORNER_GRAZE
 * beyond the body); past it, at the point corner.peek_dist behind the wall's far face at the end (the area the wall hid). The
 * nearest such end on each side wins (a corridor exit: the left and the right corner, two different men).
 * Returns the next state and the watchers' look yaws: each overrides that member's patrol sector only; a combat target or an
 * explicit facing order keeps priority (the integration).
 */
function Watch(walls: readonly Segment[], leaderPos: Vec2, leaderYaw: number, members: readonly Slot[], state: CornerState,
  config: FormationConfig): { state: CornerState; looks: { role: Role; yaw: number }[] } {
  const lead = cfg(config, 'corner.lead_dist'), hold = cfg(config, 'corner.hold_dist');
  const h = dirOf(leaderYaw);
  const endOf = (w: CornerWatch) => (w.end === 0 ? walls[w.wall].a : walls[w.wall].b);
  const sideOf = (E: Vec2): 'left' | 'right' => (h[0] * (E[1] - leaderPos[1]) - h[1] * (E[0] - leaderPos[0]) > 0 ? 'right' : 'left');
  const keep = (w: CornerWatch | null, side: 'left' | 'right'): CornerWatch | null => {
    if (!w || w.wall >= walls.length || !members.some((m) => m.role === w.role)) return null;
    const s = cornerAhead(walls, w.wall, w.end, leaderPos, leaderYaw, config);
    return s === null || s < -hold || s > lead || sideOf(endOf(w)) !== side ? null : w;
  };
  const next: CornerState = { left: keep(state.left, 'left'), right: keep(state.right, 'right') };
  for (const side of ['left', 'right'] as const) {
    if (next[side]) continue;
    let bestS = Infinity, found: { wall: number; end: 0 | 1 } | null = null;
    for (let i = 0; i < walls.length; i++) for (const end of [0, 1] as const) {
      const s = cornerAhead(walls, i, end, leaderPos, leaderYaw, config);
      if (s === null || s <= 0 || s > lead || s >= bestS) continue;
      if (sideOf(end === 0 ? walls[i].a : walls[i].b) !== side) continue;
      bestS = s; found = { wall: i, end };
    }
    if (!found) continue;
    const E = found.end === 0 ? walls[found.wall].a : walls[found.wall].b;
    const other = next[side === 'left' ? 'right' : 'left']?.role;
    const free = members.filter((m) => m.role !== other);
    if (!free.length) continue;
    const closest = free.reduce((a, b) => (dist(b.pos, E) < dist(a.pos, E) - 1e-6 ? b : a));
    next[side] = { ...found, role: closest.role };
  }
  const looks: { role: Role; yaw: number }[] = [];
  for (const side of ['left', 'right'] as const) {
    const cw = next[side];
    if (!cw) continue;
    const w = walls[cw.wall];
    const E = endOf(cw), O = cw.end === 0 ? w.b : w.a;
    const watcher = members.find((m) => m.role === cw.role)!;
    const L = dist(E, O);
    const u: Vec2 = L > 1e-6 ? scale(sub(E, O), 1 / L) : h;
    const nRaw: Vec2 = [-u[1], u[0]];
    const squadSide = (leaderPos[0] - E[0]) * nRaw[0] + (leaderPos[1] - E[1]) * nRaw[1] >= 0 ? 1 : -1;
    const graze = add(E, scale(u, (w.thickness ?? 0) / 2 + CORNER_GRAZE));
    const past = (watcher.pos[0] - E[0]) * u[0] + (watcher.pos[1] - E[1]) * u[1] >= 0;
    const target = past ? add(graze, scale(nRaw, -squadSide * cfg(config, 'corner.peek_dist'))) : graze;
    looks.push({ role: cw.role, yaw: normYaw(yawOf(sub(target, watcher.pos))) });
  }
  return { state: next, looks };
}

export const FormationCornerRules = { Watch };

// ---------------------------------------------------------------------------------------------------------------------
// FormationAssignRules (v2.1 fix, coordinator 2026-10-25): who takes which place when the formation changes

/**
 * Minimal total travel (sum of straight distances) from the followers' positions to the new formation's follower places -
 * an optimal assignment never has two paths crossing, so nobody walks through anybody on a fold, unfold, mirror or any switch.
 * `members` = the followers (the leader keeps place K and is not passed), `places` = the slots to fill (same count; roles are
 * the place labels: the sectors belong to the place). Returns, per member in order, the index into `places`.
 * Search order and tie rule = GroupCoverRules::AssignMinimalTravel (C++ may call it): depth-first, member-major, places in
 * index order; a complete assignment replaces the best only when cheaper by more than 1e-3 cm (ties keep the earlier one, so
 * with no movement needed the identity wins).
 */
function AssignPlaces(members: readonly Vec2[], places: readonly Vec2[]): number[] {
  const n = Math.min(members.length, places.length);
  let best: number[] = members.map((_, i) => (i < n ? i : -1)), bestCost = Infinity;
  const used = places.map(() => false), current: number[] = members.map(() => -1);
  const search = (m: number, cost: number) => {
    if (cost >= bestCost - 1e-3) return;
    if (m === n) { bestCost = cost; best = [...current]; return; }
    for (let k = 0; k < places.length; k++) {
      if (used[k]) continue;
      used[k] = true; current[m] = k;
      search(m + 1, cost + dist(members[m], places[k]));
      used[k] = false; current[m] = -1;
    }
  };
  search(0, 0);
  return best;
}

export const FormationAssignRules = { AssignPlaces };

/** Distance helper re-exported for the checks. */
export { dist };
