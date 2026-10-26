// Training Ground / movement: the pure formation rules (FORMATION_CONTRACT.md 2). Same names and maths as the C++ ports
// (FormationShapeRules / FormationSectorRules / FormationTerrainRules / FormationWallRules); the golden vectors exported by
// `npm run golden` prove the two agree. Design: docs/design/FORMATION_SECTORS.md.
// Angles in the config are relative to the leader's heading (or the wall's outward normal for wall.*), positive = right.

import type { FormationConfig } from './config.ts';
import { cfg } from './config.ts';
import type { Segment, Vec2 } from './geometry.ts';
import { add, closestOnSegment, dirOf, dist, distToWall, dot, localToWorld, normYaw, raycastWalls, scale, segmentBlocked, sub, yawOf } from './geometry.ts';
import type { Formation } from './layout.ts';

/**
 * K commander (leader), P right, L left, Z rear (with 2 members: the pair partner behind the right shoulder). v2.2 (user
 * 2026-10-25): three = K, P, Z - the triad has a rear man walking backwards like the four, P sweeps both flanks.
 */
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
  if (memberCount === 3) return ['K', 'P', 'Z'];
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
      // v2.2: the triad's rear man straight behind at triad.rear_back (the diamond's at diamond.rear_back).
      return [-cfg(c, n === 3 ? 'triad.rear_back' : 'diamond.rear_back'), 0];
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
      return [-base - (n === 3 ? 1 : 2) * sp, 0]; // v2.2: with three the rear man closes up behind P
    }
    case 'line': {
      const s = cfg(c, 'line.spacing');
      if (n === 2) return [0, s];
      if (role === 'P') return [0, s];
      if (role === 'L' || n === 3) return [0, -s]; // v2.2: with three Z stands on the left
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
        // Triangle round the centre (-r, 0): K at 0 deg, P at +120, Z at -120.
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

/**
 * Echelon order: towards the open side the far-side man goes second (minimal travel from the diamond). v2.2: three = K, P, Z
 * both ways (Z last, the rear man; who actually stands where is FormationAssignRules' minimal travel).
 */
function echelonIndex(formation: Formation, role: Role, n: number): number {
  const order: Role[] = n === 2 ? ['K', 'Z'] : n === 3 ? ['K', 'P', 'Z']
    : formation === 'echelon_right' ? ['K', 'L', 'P', 'Z'] : ['K', 'P', 'L', 'Z'];
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
 * v2.3 (red team #7): the stagger aside shrinks (5 cm steps, down to 0 = on the trail) until the slot keeps
 * column.stagger_clear from every wall surface and the line from its trail point to it is clear - a 60 cm stagger in a
 * 1.2-2 m wall gap crossed obliquely put the slot inside the wall's clearance (no wall given -> the full stagger, as v2.1).
 */
function TrailSlots(trail: readonly Vec2[], leaderYaw: number, memberCount: number, config: FormationConfig, walls: readonly Segment[] = []): (Slot & { yaw: number })[] {
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
    const side = dirOf(at.yaw + 90), clear = cfg(config, 'column.stagger_clear');
    const fits = (p: Vec2) => walls.every((w) => distToWall(p, w) >= clear - 1e-6) && !segmentBlocked(at.pos, p, walls, 0);
    let st = Math.abs(local[1]);
    while (st > 0 && !fits(add(at.pos, scale(side, Math.sign(local[1]) * st)))) st = Math.max(0, st - 5);
    return { role, pos: add(at.pos, scale(side, Math.sign(local[1]) * st)), yaw: at.yaw };
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
    if (n === 3) return cfg(c, 'wall.flank_deg'); // v2.2: the triad's Z on the left end looks 8 (by position, not role)
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
      // v2.2: with three the last man (Z) takes the rear sector (sector_4), so 5-7 o'clock stays watched.
      const keys = n === 3 ? ['echelon.sector_lead', 'echelon.sector_2', 'echelon.sector_4']
        : ['echelon.sector_lead', 'echelon.sector_2', 'echelon.sector_3', 'echelon.sector_4'];
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

/**
 * v2.1.1 (user decision 2026-10-25, "6А"): nobody looks into a wall - in ANY formation, the manual ones too. A wall within
 * look.wall_dist of the member (surface distance) blocks the directions within look.wall_angle of the direction to its closest
 * point; a look yaw inside a blocked span turns to the nearest free direction (an edge of the span), so a scan sweeping into
 * the wall stops at the open edge of his sector. Of the free edges he takes the one he reaches from his current facing
 * (currentYaw) sweeping across the FEWEST wall directions (v2.3: counted; was yes / no), then the nearest to the wanted yaw
 * (else two edges about as near flip-flop and his look swings through the wall). v2.3 (red team, coordinator 2026-10-25): a free
 * arc narrower than look.min_free_deg does not count (neither for the wanted yaw nor an edge); `held` (his previous result, null
 * when ClearLook did not apply last frame) stays a candidate look.hold_deg better than its distance to the wanted yaw while no
 * wall is within look.wall_angle - look.hold_margin_deg of it (hysteresis); a free but narrow wanted yaw / edge still competes
 * look.narrow_penalty worse (+360 per wall swept as for the edges); enclosed -> the longest clear ray (see below). Ties: the earlier wall, the left edge
 * first (the first strictly better edge wins). The integration applies it to the patrol / halt / "all forward" looks - not to
 * the wall layout, the corner watcher (he looks at the corner on purpose) or a man looking where he walks (within 45 deg).
 */
function ClearLook(walls: readonly Segment[], pos: Vec2, yaw: number, currentYaw: number, config: FormationConfig, held: number | null = null): number {
  const reach = cfg(config, 'look.wall_dist'), half = cfg(config, 'look.wall_angle'), minFree = cfg(config, 'look.min_free_deg');
  const centers: number[] = [];
  for (const w of walls) {
    const q = closestOnSegment(pos, w.a, w.b).point;
    if (Math.max(0, dist(pos, q) - (w.thickness ?? 0) / 2) > reach) continue;
    if (dist(pos, q) < 1e-6) continue;
    centers.push(yawOf(sub(q, pos)));
  }
  const blocked = (a: number) => centers.some((c) => Math.abs(normYaw(a - c)) < half - 1e-6);
  // v2.3: the free arc a direction lies in, measured in whole degrees both ways (a sliver between two wall spans is no place
  // to look: as the man moves it opens and closes and his look jumps across the walls).
  const freeWidth = (a: number, dir: 1 | -1 | 0 = 0) => {
    if (blocked(a)) return 0;
    let w = 0;
    for (const sgn of dir === 0 ? [1, -1] : [dir]) for (let k = 1; k <= 360 && !blocked(a + sgn * k); k++) w++;
    return w;
  };
  // v2.3: how many wall directions a turn from currentYaw to `to` (the short way) sweeps across - fewest first.
  const swept = (to: number) => {
    const turn = normYaw(to - currentYaw);
    return centers.filter((c) => { const k = normYaw(c - currentYaw); return Math.sign(k) === Math.sign(turn) && Math.abs(k) < Math.abs(turn) - 1e-6; }).length;
  };
  // The wanted yaw is taken when it lies in a free arc at least look.min_free_deg wide (v2.3: a sliver between two wall spans
  // does not count - as the man moves it opens and closes and his look swings across the walls to it).
  const lookable = (a: number, dir: 1 | -1 | 0 = 0) => !blocked(a) && freeWidth(a, dir) >= minFree;
  if (lookable(yaw)) return normYaw(yaw);
  // v2.3 hysteresis: his previous result (held) stays a candidate, look.hold_deg better than its distance to the wanted yaw,
  // while no wall direction is within look.wall_angle - look.hold_margin_deg of it (else a wall at the reach's edge or an arc
  // at the width threshold flips his look between two far directions every few steps - red team 2_8, 11_8, 8_2).
  let best: number | null = null, bestScore = Infinity;
  const holdDeg = cfg(config, 'look.hold_deg'), holdHalf = half - cfg(config, 'look.hold_margin_deg');
  if (held !== null && holdDeg > 0 && !centers.some((c) => Math.abs(normYaw(held - c)) < holdHalf - 1e-6)) {
    best = normYaw(held); bestScore = Math.abs(normYaw(held - yaw)) - holdDeg;
  }
  // v2.3 part 4: a free but NARROW direction (the wanted yaw, an edge) still competes, look.narrow_penalty worse - so a narrow
  // arc he reaches without sweeping across a wall beats a wide one behind a sweep (entering a corridor P swung 110 deg across
  // the end of its wall to the far edge and back - red team 8_5, 4_8), while a wide arc without a sweep still wins.
  const narrowPenalty = cfg(config, 'look.narrow_penalty');
  if (narrowPenalty > 0 && !blocked(yaw)) {
    const score = narrowPenalty + 360 * swept(yaw);
    if (score < bestScore - 1e-9) { bestScore = score; best = normYaw(yaw); }
  }
  for (const c of centers) for (const [edge, out] of [[c - half, -1], [c + half, 1]] as [number, 1 | -1][]) {
    if (blocked(edge)) continue;
    const wide = lookable(edge, out);
    if (!wide && narrowPenalty <= 0) continue;
    const score = Math.abs(normYaw(edge - yaw)) + 360 * swept(edge) + (wide ? 0 : narrowPenalty);
    if (score < bestScore - 1e-9) { bestScore = score; best = normYaw(edge); }
  }
  if (best !== null) return best;
  // v2.3: enclosed (no free arc of look.min_free_deg): look along the longest clear ray (every 5 deg from the wanted yaw, up to
  // 2 x look.wall_dist; ties: the nearest the wanted yaw) - the least bad direction, not the wanted one into a wall.
  let bestRay = -1;
  best = normYaw(yaw);
  for (let k = 0; k <= 36; k++) for (const sgn of k === 0 || k === 36 ? [1] : [1, -1]) {
    const a = yaw + sgn * 5 * k;
    const r = Math.min(2 * reach, raycastWalls(pos, dirOf(a), walls, 2 * reach));
    if (r > bestRay + 1e-6) { bestRay = r; best = normYaw(a); }
  }
  return best;
}

/**
 * v2.3 (coordinator 2026-10-26, a clarification of user decision 1A): true when this role's look in this formation is a user
 * decision that the coverage allocator must keep - the integration passes LookYaw's look to FormationCoverRules.Allocate as a
 * FIXED look and the allocator only shares out what is left. In a NARROW column (passageWidth < column.narrow_width; null = no
 * passage = wide) with 3+ members: K (12 + his search), P (+column.narrow_fwd_deg, forward over the shoulder) and L
 * (-column.narrow_back_deg, back at a slant); Z is not decided - he is the allocator's rear guard (straight back). Wide column,
 * other formations: false. The wall layout (L 8 / K 6 / P 4) and the pair (3 -> 6 -> 9) never reach the allocator (it runs for
 * 3+ members walking / halted, not in the wall layout), so their looks stay as decided.
 */
function Decided(role: Role, formation: Formation, memberCount: number, passageWidth: number | null, config: FormationConfig): boolean {
  return formation === 'column' && memberCount >= 3 && role !== 'Z' && passageWidth !== null && passageWidth < cfg(config, 'column.narrow_width');
}

export const FormationSectorRules = { LookYaw, ClearLook, Decided };

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
 * wall END beside him counts as well as a wall along him - the side rays of the classifier slip past a 40 cm end. Walls across
 * the heading (isAcrossHeading, the classifier's classify.side_ignore_perp_deg band) are skipped. An open side
 * counts as probe_side. The trail column's sectors use it per member (a wall beside a man -> the narrow column's sectors,
 * nobody's herringbone look into it).
 */
function SideWidth(walls: readonly Segment[], pos: Vec2, yaw: number, c: FormationConfig): number | null {
  const reach = cfg(c, 'classify.probe_side'), ignore = cfg(c, 'classify.side_ignore_perp_deg');
  const h = dirOf(yaw);
  let right = Infinity, left = Infinity;
  for (const w of walls) {
    // Coordinator 2026-10-25 (design consistency with Classify): a wall ACROSS the heading is ahead / behind, never a side wall
    // (a man's look into its end is ClearLook's job, v2.1.1).
    if (isAcrossHeading(w, yaw, ignore)) continue;
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
    // v2.2: the triad is K, P, Z - the man on the LEFT end (Z) looks 8, as L did: the user's wall rule is by position.
    return [{ role: 'K', pos: k, yaw: y(0) }, { role: 'P', pos: at(1), yaw: y(-flank) }, { role: 'Z', pos: at(-1), yaw: y(flank) }];
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

export const FormationWallRules = { Layout, BreakRoomTie, Slide };

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
    case 'line': return chain(memberCount === 3 ? ['Z', 'K', 'P'] : ['L', 'K', 'P', 'Z']);
    case 'hedgehog': return chain(['K', 'P', 'Z', 'L'], true);
    case 'diamond':
    case 'wedge':
    case 'blizzard': return memberCount >= 4 ? [['P', 'Z'], ['L', 'Z']] : [['P', 'Z']];
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

/** v2.1.1: formations whose far end swings metres on a turn (the trail column takes the turn for them). */
const WIDE: readonly Formation[] = ['echelon_right', 'echelon_left', 'line'];

/**
 * v2.1.1: the sharpest heading change on the leader's path within turn.lookahead ahead of him: the largest |delta| between his
 * heading and the direction of a path piece starting within that distance (path[0] = the leader now; the caller's nav path then
 * the route's next points), degrees 0..180. 0 for a straight path or no path.
 */
function TurnAhead(path: readonly Vec2[], heading: number, config: FormationConfig): number {
  const look = cfg(config, 'turn.lookahead');
  let walked = 0, worst = 0;
  for (let k = 1; k < path.length && walked <= look; k++) {
    const L = dist(path[k - 1], path[k]);
    if (L < 1e-6) continue;
    worst = Math.max(worst, Math.abs(normYaw(yawOf(sub(path[k], path[k - 1])) - heading)));
    walked += L;
  }
  return worst;
}

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
 * v2.1.1 (user decision 2026-10-25, "5Б"): turnAhead = TurnAhead of the leader's path (deg); above turn.fold_deg a WIDE
 * formation (the echelons, the line - their far end would swing metres round the turn) takes the trail column for the turn
 * and opens again after hold_distance of path once the turn is behind (the same hysteresis).
 */
function Resolve(walls: readonly Segment[], requested: Formation, memberCount: number, leaderPos: Vec2, leaderYaw: number,
  ahead: { pos: Vec2; yaw: number } | null, turnAhead: number, state: GuardState, stepDistance: number,
  config: FormationConfig): GuardState {
  const whole = (f: Formation) => BlockedRoles(walls, f, memberCount, leaderPos, leaderYaw, config).length === 0 &&
    (!ahead || BlockedRoles(walls, f, memberCount, ahead.pos, ahead.yaw, config).length === 0);
  let raw: GuardLevel = 0;
  // v2.1.1 (user "5Б"): a sharp turn ahead folds a wide formation into the trail column (no long swing of its far end).
  if (WIDE.includes(requested) && turnAhead > cfg(config, 'turn.fold_deg')) raw = 2;
  else if (requested !== 'wall' && !whole(requested)) {
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

export const FormationGuardRules = { ChainPairs, BlockedRoles, Resolve, Effective, TurnAhead };

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
// FormationCoverRules (v2.2, user 2026-10-25: "the last man is responsible for the rear; recompute all variants")

/** deg: the rear sector the rear guard always keeps in his cone - 5-7 o'clock = 6 o'clock +- 30 (the rear-cover check's). */
export const REAR_SECTOR = 30;

export interface CoverMember {
  /** The place (its slot): K first. */
  role: Role;
  /** The place's slot (not the man's position: the allocation must not jitter with his steps). */
  pos: Vec2;
  /** How far back the place is along the leader's path / heading (cm; the largest = the rear guard). */
  back: number;
}

export interface CoverShare {
  role: Role;
  /** The share, degrees relative to the heading (from <= to, within -180..180). */
  from: number;
  to: number;
  /** The centre of the first gap he took (his halt look, relative). */
  primary: number;
}

export interface CoverResult {
  rearGuard: Role | null;
  /** World look yaws per place. */
  looks: { role: Role; yaw: number }[];
  shares: CoverShare[];
}

/** Relative directions blocked by walls (within look.wall_dist of any place's slot; +-look.wall_angle round the closest point). */
function blockedSpans(walls: readonly Segment[], members: readonly CoverMember[], heading: number, c: FormationConfig): number[] {
  const reach = cfg(c, 'look.wall_dist');
  const centers: number[] = [];
  for (const m of members) for (const w of walls) {
    const q = closestOnSegment(m.pos, w.a, w.b).point;
    if (dist(m.pos, q) < 1e-6 || Math.max(0, dist(m.pos, q) - (w.thickness ?? 0) / 2) > reach) continue;
    centers.push(normYaw(yawOf(sub(q, m.pos)) - heading));
  }
  return centers;
}

/**
 * v2.2 general coverage (replaces the per-formation sector tables for walk / halt with 3+ members; the pair, run, "all
 * forward", the wall layout and the hedgehog ring keep their rules). Angles relative to the heading, sampled per whole degree
 * (-179..180) so the parity is exact:
 * 1. K keeps the front: walk = the leader's search +-scan.amp_lead (as LookYaw), halt = 12. He guarantees +-(fov / 2 - amp_lead)
 *    on the move (the whole cone standing).
 * 2. The REAR GUARD = the place with the largest `back` (ties: the later in the list) whose look is not fixed (a corner watcher
 *    hands the rear to the next man back): centre 6 o'clock, shifted by
 *    rear.wall_shift_deg towards the open side when walls are within look.wall_dist on ONE side of his slot (an echelon along a
 *    wall on the left -> about 5 o'clock); on the move he searches +-rear.share_half round it, cut so that 5-7 o'clock (REAR_SECTOR)
 *    stays inside his cone at every moment (shift + search <= fov / 2 - REAR_SECTOR); he guarantees his cone minus the search;
 *    standing he holds the centre. He walks backwards (the follower's backward cap).
 * 3. Open = not blocked: a direction within look.wall_angle of the closest point of a wall within look.wall_dist of any slot.
 *    The open degrees nobody guarantees (K, the rear guard, the fixed looks' full cones) form gaps; the others take them greedily: the widest gap first, given to the free member whose
 *    bearing from the leader is nearest its centre (ties: the earlier gap from -179, the earlier member). Gaps left when nobody
 *    is free join the nearest share (the union through the front: the triad's P then sweeps 3 -> 12 -> 9) unless the gap is no
 *    wider than coverage.max_gap_deg (left open: stretching a sweep for it opens more than it closes) or the union would cross a
 *    blocked direction (nobody sweeps across a wall). A member with no gap
 *    doubles the share of K or the rear guard nearest his bearing.
 * 4. Walk: each other member sweeps his share - centre the middle, amplitude min(half its width, coverage.max_sweep, and for a
 *    share one cone can hold, (fov + coverage.max_gap_deg - width) / 2 so its far side stays within max_gap), period
 *    scan.period x max(1, amplitude / scan.amp_side), scan.dwell_exp; shares in assignment order alternate phase 0 / pi. When a
 *    share runs across the front, K's search (+-scan.amp_lead) runs against that sweep (same period, opposite sign).
 *    Halt: K 12, the rear guard his centre, the others the centre of the first gap they took.
 * `fixed` = looks set by a higher rule (the corner watcher, v2.1): kept as given; the direction counts as covered and the duty
 * he leaves (K's front, the rear) becomes a gap for the others.
 * Fewer than 3 members -> null (LookYaw).
 */
function Allocate(walls: readonly Segment[], leaderPos: Vec2, heading: number, members: readonly CoverMember[], mode: 'walk' | 'halt',
  t: number, c: FormationConfig, fixed: readonly { role: Role; yaw: number }[] = []): CoverResult | null {
  if (members.length < 3) return null;
  const exp = cfg(c, 'scan.dwell_exp');
  const lead = cfg(c, 'scan.amp_lead'), rearHalf = cfg(c, 'rear.share_half'), wallAngle = cfg(c, 'look.wall_angle');
  // Rear guard: the largest back (ties -> the later) among the members whose look is NOT fixed (a corner watcher hands the rear
  // to the next man back); all fixed -> the farthest back anyway (the rear then is a gap like any other).
  const isFixed = (r: Role) => fixed.some((f) => f.role === r);
  let rear = members[1];
  for (const m of members.slice(1)) if (m.back >= rear.back - 1e-6) rear = m;
  const freeBack = members.slice(1).filter((m) => !isFixed(m.role));
  if (freeBack.length && isFixed(rear.role)) { rear = freeBack[0]; for (const m of freeBack) if (m.back >= rear.back - 1e-6) rear = m; }
  // Shift towards the open side when walls stand on one side of his slot only.
  const reach = cfg(c, 'look.wall_dist');
  const h = dirOf(heading);
  let wallLeft = false, wallRight = false;
  for (const w of walls) {
    const q = closestOnSegment(rear.pos, w.a, w.b).point;
    if (Math.max(0, dist(rear.pos, q) - (w.thickness ?? 0) / 2) > reach) continue;
    if (h[0] * (q[1] - rear.pos[1]) - h[1] * (q[0] - rear.pos[0]) >= 0) wallRight = true; else wallLeft = true;
  }
  const shift = cfg(c, 'rear.wall_shift_deg');
  const rearCentre = wallLeft && !wallRight ? 180 - shift : wallRight && !wallLeft ? -180 + shift : 180;
  const blocked = blockedSpans(walls, members, heading, c);
  // Fixed looks (a corner watcher, the leader's aim in cover, a combat target): his cone counts as covered (+-fov / 2); his own
  // duty (K's front, the rear) does not - it goes to the others like any gap.
  const fixedOf = (role: Role) => fixed.find((f) => f.role === role);
  const kFixed = fixedOf(members[0].role), rearFixed = fixedOf(rear.role);
  const fixedRel = fixed.map((f) => normYaw(f.yaw - heading));
  // What K and the rear guard GUARANTEE: their cone minus their own search swing (walk); standing, the full cone.
  const fovHalf = cfg(c, 'sight.fov') / 2;
  // The rear guard keeps 5-7 o'clock (180 +- REAR_SECTOR) inside his cone at every moment: shift + search <= fov / 2 - REAR_SECTOR.
  const rearAmp = Math.max(0, Math.min(rearHalf, fovHalf - REAR_SECTOR - Math.abs(normYaw(rearCentre - 180))));
  const kSure = mode === 'halt' ? fovHalf : fovHalf - lead, rearSure = mode === 'halt' ? fovHalf : fovHalf - rearAmp;
  const isCovered = (r: number) => (!kFixed && Math.abs(r) <= kSure) || (!rearFixed && Math.abs(normYaw(r - rearCentre)) <= rearSure) ||
    fixedRel.some((f) => Math.abs(normYaw(r - f)) <= cfg(c, 'sight.fov') / 2) || blocked.some((b) => Math.abs(normYaw(r - b)) < wallAngle);
  // Gaps: runs of uncovered whole degrees, circular; as [start, end] relative (end may exceed 180 when it wraps).
  const deg = Array.from({ length: 360 }, (_, i) => i - 179);
  const free = deg.map((r) => !isCovered(r));
  const gaps: { from: number; to: number }[] = [];
  if (free.every(Boolean)) gaps.push({ from: -179, to: 180 });
  else if (free.some(Boolean)) {
    // Walk the circle once from a covered degree; a gap's `to` runs past 180 when it wraps.
    const i0 = free.findIndex((f) => !f);
    let cur: { from: number; to: number } | null = null;
    for (let k = 1; k <= 360; k++) {
      const i = (i0 + k) % 360;
      if (!free[i]) { cur = null; continue; }
      if (cur) cur.to += 1; else { cur = { from: deg[i], to: deg[i] }; gaps.push(cur); }
    }
    gaps.sort((a, b) => a.from - b.from);
  }
  const width = (g: { from: number; to: number }) => g.to - g.from + 1;
  const centreOf = (g: { from: number; to: number }) => normYaw((g.from + g.to) / 2);
  const bearing = (m: CoverMember) => normYaw(yawOf(sub(m.pos, leaderPos)) - heading);
  const others = members.slice(1).filter((m) => m !== rear && !fixedOf(m.role));
  const shares: CoverShare[] = [];
  const open = [...gaps];
  const freeMembers = [...others];
  while (freeMembers.length && open.length) {
    let best: { gi: number; mi: number; w: number; d: number } | null = null;
    open.forEach((g, gi) => freeMembers.forEach((m, mi) => {
      const w = width(g), d = Math.abs(normYaw(centreOf(g) - bearing(m)));
      if (!best || w > best.w || (w === best.w && d < best.d - 1e-9)) best = { gi, mi, w, d };
    }));
    const b = best!;
    const g = open.splice(b.gi, 1)[0], m = freeMembers.splice(b.mi, 1)[0];
    shares.push({ role: m.role, from: g.from, to: g.to, primary: centreOf(g) });
  }
  // Gaps nobody took join the nearest share (relative min / max: the union runs through the front, never through the rear).
  for (const g of open) {
    if (!shares.length) break;
    if (width(g) <= cfg(c, 'coverage.max_gap_deg')) continue; // small enough to leave open (stretching a sweep costs more)
    const near = shares.reduce((a, s2) => (Math.abs(normYaw(centreOf(g) - (s2.from + s2.to) / 2)) < Math.abs(normYaw(centreOf(g) - (a.from + a.to) / 2)) - 1e-9 ? s2 : a));
    if (g.to > 180 || near.to > 180) continue; // a gap through the rear stays with the rear guard
    const lo = Math.min(near.from, g.from), hi = Math.max(near.to, g.to);
    let crossesWall = false;
    for (let r = Math.ceil(lo); r <= hi && !crossesWall; r++) crossesWall = blocked.some((b) => Math.abs(normYaw(r - b)) < wallAngle);
    if (crossesWall) continue; // never sweep across a wall to reach it
    near.from = lo; near.to = hi;
  }
  // Members without a gap double K's or the rear guard's share, whichever is nearer their bearing.
  for (const m of freeMembers) {
    const toRear = Math.abs(normYaw(bearing(m) - rearCentre)) < Math.abs(bearing(m));
    const ctr = toRear ? rearCentre : 0, half = toRear ? rearSure : kSure;
    shares.push({ role: m.role, from: ctr - half, to: ctr + half, primary: normYaw(ctr) });
  }
  const looks: { role: Role; yaw: number }[] = [];
  const period = cfg(c, 'scan.period'), maxSweep = cfg(c, 'coverage.max_sweep'), sideAmp = cfg(c, 'scan.amp_side');
  const maxGap = cfg(c, 'coverage.max_gap_deg');
  const sweepOf = (sh: CoverShare) => {
    // A share one cone can hold within coverage.max_gap: sweep only as far as keeps its far side within that; a share too
    // wide for one cone (the triad's two flanks): the full sweep (user decision: P 3 -> 12 -> 9).
    const w = sh.to - sh.from, mid = (sh.from + sh.to) / 2;
    const amp = w <= 2 * fovHalf + maxGap ? Math.min(w / 2, maxSweep, Math.max(0, (2 * fovHalf + maxGap - w) / 2)) : Math.min(w / 2, maxSweep);
    return { mid, amp, period: period * Math.max(1, amp / sideAmp), phase: shares.indexOf(sh) % 2 ? Math.PI : 0 };
  };
  // A share across the front (the triad's P: 3 -> 12 -> 9): K's search runs against that sweep (K left while he looks right),
  // so the flank he has left is never also left by K. Otherwise K searches on his own (as LookYaw).
  const front = shares.find((x) => x.from < -lead && x.to > lead);
  let kRel = 0;
  if (mode !== 'halt') {
    if (front) { const sw = sweepOf(front); kRel = -lead * dwell(Math.sin((2 * Math.PI * t) / sw.period + sw.phase), exp); }
    else kRel = scanValue(0, lead, period, 1.3, t, exp);
  }
  looks.push({ role: members[0].role, yaw: kFixed ? normYaw(kFixed.yaw) : normYaw(heading + kRel) });
  // The rear guard searches 5-7 o'clock (+-rear.share_half round his centre) on the move; standing he holds the centre.
  const rearRel = mode === 'halt' ? rearCentre : scanValue(rearCentre, rearAmp, period, Math.PI / 2, t, exp);
  looks.push({ role: rear.role, yaw: rearFixed ? normYaw(rearFixed.yaw) : normYaw(heading + rearRel) });
  for (const f of fixed) if (f.role !== members[0].role && f.role !== rear.role) looks.push({ role: f.role, yaw: normYaw(f.yaw) });
  others.forEach((m) => {
    const sh = shares.find((x) => x.role === m.role)!;
    const sw = sweepOf(sh);
    const rel = mode === 'halt' ? sh.primary : scanValue(sw.mid, sw.amp, sw.period, sw.phase, t, exp);
    looks.push({ role: m.role, yaw: normYaw(heading + rel) });
  });
  return { rearGuard: rear.role, looks, shares };
}

/**
 * v2.2 (user playtest 2026-10-25: in group cover at a wall corner the leader holds the new corner, the two followers looked the
 * same way and a wide gap opened): coverage round a STANDING group with looks decided elsewhere. `fixed` = members whose look a
 * higher rule sets (the leader's corner aim in cover, a combat target, an explicit facing order): each counts as covering his
 * whole cone. The `free` members (roles, in priority order) fill the rest, absolute yaws sampled per whole degree (-179..180):
 * open = not within look.wall_angle of the closest point of a wall within look.wall_dist of any member;
 * 1. with 3+ members in all, one free member covers the REAR = the direction opposite the fixed looks' mean (the first fixed
 *    look if they cancel; with no fixed look: no rear rule) - the free member whose bearing from the group centre is nearest
 *    it; if that direction is blocked, the nearest open degree;
 * 2. the others, greedily: the widest uncovered open gap first, to the free member whose bearing from the group centre is
 *    nearest its middle (ties: the earlier gap from -179, the earlier member); he looks at the middle;
 * 3. a free member with no gap left looks outward along his bearing from the centre (the nearest open degree to it).
 * Every assigned cone (+-fov / 2) counts as covered for the next. Returns the free members' world yaws.
 */
function AllocateStatic(walls: readonly Segment[], members: readonly Slot[], fixed: readonly { role: Role; yaw: number }[],
  free: readonly Role[], c: FormationConfig): { role: Role; yaw: number }[] {
  const fovHalf = cfg(c, 'sight.fov') / 2, reach = cfg(c, 'look.wall_dist'), wallAngle = cfg(c, 'look.wall_angle');
  const centre: Vec2 = members.length ? scale(members.reduce<Vec2>((a, m) => add(a, m.pos), [0, 0]), 1 / members.length) : [0, 0];
  const blockedYaws: number[] = [];
  for (const m of members) for (const w of walls) {
    const q = closestOnSegment(m.pos, w.a, w.b).point;
    if (dist(m.pos, q) < 1e-6 || Math.max(0, dist(m.pos, q) - (w.thickness ?? 0) / 2) > reach) continue;
    blockedYaws.push(yawOf(sub(q, m.pos)));
  }
  const isBlocked = (a: number) => blockedYaws.some((b) => Math.abs(normYaw(a - b)) < wallAngle);
  const cones: number[] = fixed.map((f) => f.yaw);
  const isCovered = (a: number) => cones.some((y) => Math.abs(normYaw(a - y)) <= fovHalf);
  const deg = Array.from({ length: 360 }, (_, i) => i - 179);
  const nearestOpen = (a: number) => {
    let best = a, bestD = Infinity;
    for (const d of deg) if (!isBlocked(d)) { const dd = Math.abs(normYaw(d - a)); if (dd < bestD - 1e-9) { bestD = dd; best = d; } }
    return normYaw(best);
  };
  const bearing = (r: Role) => { const m = members.find((x) => x.role === r); return m && dist(m.pos, centre) > 1e-6 ? yawOf(sub(m.pos, centre)) : 0; };
  const out: { role: Role; yaw: number }[] = [];
  const left = [...free];
  const take = (r: Role, yaw: number) => { out.push({ role: r, yaw: normYaw(yaw) }); cones.push(yaw); left.splice(left.indexOf(r), 1); };
  if (members.length >= 3 && fixed.length && left.length) {
    const sx = fixed.reduce((a, f) => a + Math.cos((f.yaw * Math.PI) / 180), 0), sy = fixed.reduce((a, f) => a + Math.sin((f.yaw * Math.PI) / 180), 0);
    const front = Math.hypot(sx, sy) > 1e-6 ? (Math.atan2(sy, sx) * 180) / Math.PI : fixed[0].yaw;
    const rear = normYaw(front + 180);
    const who = left.reduce((a, r) => (Math.abs(normYaw(bearing(r) - rear)) < Math.abs(normYaw(bearing(a) - rear)) - 1e-9 ? r : a));
    take(who, isBlocked(rear) ? nearestOpen(rear) : rear);
  }
  while (left.length) {
    const open = deg.map((d) => !isBlocked(d) && !isCovered(d));
    const gaps: { from: number; to: number }[] = [];
    if (open.every(Boolean)) gaps.push({ from: -179, to: 180 });
    else if (open.some(Boolean)) {
      const i0 = open.findIndex((o) => !o);
      let cur: { from: number; to: number } | null = null;
      for (let k = 1; k <= 360; k++) {
        const i = (i0 + k) % 360;
        if (!open[i]) { cur = null; continue; }
        if (cur) cur.to += 1; else { cur = { from: deg[i], to: deg[i] }; gaps.push(cur); }
      }
      gaps.sort((a, b) => a.from - b.from);
    }
    if (!gaps.length) { for (const r of [...left]) take(r, nearestOpen(bearing(r))); break; }
    let best: { g: { from: number; to: number }; r: Role; w: number; d: number } | null = null;
    for (const g of gaps) for (const r of left) {
      const w = g.to - g.from + 1, mid = normYaw((g.from + g.to) / 2), d = Math.abs(normYaw(mid - bearing(r)));
      if (!best || w > best.w || (w === best.w && d < best.d - 1e-9)) best = { g, r, w, d };
    }
    take(best!.r, normYaw((best!.g.from + best!.g.to) / 2));
  }
  return free.map((r) => out.find((o) => o.role === r)!);
}

export const FormationCoverRules = { Allocate, AllocateStatic };

// ---------------------------------------------------------------------------------------------------------------------
// FormationWallRules::Slide (v2.2, user approved on an animation 2026-10-25: "Все отлично")

/** cm: a destination closer than this along the wall is not a slide (the layout just stays). */
export const SLIDE_MIN = 50;

export interface SlideResult {
  /** The destination lies at the same wall, on the squad's side, within slide.max_off_wall, along it by >= SLIDE_MIN. */
  active: boolean;
  /** Where the leader's move along the wall ends (his start slot moved along the wall; the wall offset kept). */
  leaderTarget: Vec2;
  /** World targets (the start layout moved along the wall by what the leader has covered) and looks, per place. */
  targets: WallSlot[];
  /** The leading end (looks ahead along the wall) and the trailing end (looks back; the rear guard). */
  lead: Role | null;
  rearGuard: Role | null;
  /** Yaw of the wall's outward normal (from the wall to the squad). */
  normalYaw: number;
  /** Places from the leading end to the trailing end (the order the slide keeps). */
  order: Role[];
}

/**
 * The squad stands in the wall layout (`startSlots`, K first, as FormationWallRules.Layout placed it) and the leader's next
 * destination lies at the SAME wall: within slide.max_off_wall of its surface, on the squad's side, its projection on the wall
 * inside it, at least SLIDE_MIN along it. Then nobody re-forms: everyone moves sideways together along the wall at the leader's
 * pace - each target is his start slot moved along the wall by the leader's progress (leaderPos - his start slot, along the
 * wall only), so the order and the offset from the wall are kept and nobody passes anybody. Looks: the leading end along the
 * wall in the travel direction turned slide.lead_out_deg outward, the trailing end back along the wall turned as far outward
 * (he is the rear guard), K (when not an end) away from the wall with his search (+-scan.amp_lead, as LookYaw), the members in
 * between FormationCoverRules.AllocateStatic (the ends and K fixed). Not active -> the targets are the start slots (the caller
 * forms the normal formation). On arrival the caller forms the normal wall layout again; at the wall's end the corner watcher
 * (the leading end) applies as anywhere.
 */
function Slide(wall: Segment, startSlots: readonly WallSlot[], leaderPos: Vec2, destination: Vec2, t: number,
  config: FormationConfig): SlideResult {
  const k0 = startSlots[0].pos;
  const L = dist(wall.a, wall.b);
  const u: Vec2 = L > 1e-6 ? scale(sub(wall.b, wall.a), 1 / L) : [1, 0];
  const foot = closestOnSegment(k0, wall.a, wall.b).point;
  let nrm = sub(k0, foot);
  if (Math.hypot(nrm[0], nrm[1]) < 1e-6) nrm = [-u[1], u[0]];
  const N = scale(nrm, 1 / Math.hypot(nrm[0], nrm[1]));
  const normalYaw = yawOf(N);
  const destFoot = closestOnSegment(destination, wall.a, wall.b);
  const off = dist(destination, destFoot.point) - (wall.thickness ?? 0) / 2;
  const alongDest = dot(sub(destination, k0), u);
  const sameSide = dot(sub(destination, destFoot.point), N) >= 0;
  const inside = destFoot.t > 0 && destFoot.t < 1;
  const active = off <= cfg(config, 'slide.max_off_wall') && sameSide && inside && Math.abs(alongDest) >= SLIDE_MIN;
  const leaderTarget = add(k0, scale(u, alongDest));
  if (!active) {
    return { active, leaderTarget: k0, targets: startSlots.map((s) => ({ ...s })), lead: null, rearGuard: null, normalYaw, order: startSlots.map((s) => s.role) };
  }
  const D = scale(u, Math.sign(alongDest));
  const moved = dot(sub(leaderPos, k0), D);
  const shift = scale(D, Math.max(0, Math.min(Math.abs(alongDest), moved)));
  const order = [...startSlots].sort((a, b) => dot(b.pos, D) - dot(a.pos, D)).map((s) => s.role);
  const lead = order[0], trail = order[order.length - 1];
  const out = cfg(config, 'slide.lead_out_deg');
  const dYaw = yawOf(D);
  // "Outward" = turned from the wall direction towards the normal.
  const towardN = (y: number) => normYaw(y + Math.sign(normYaw(normalYaw - y) || 1) * out);
  const pos = (r: Role) => add(startSlots.find((s) => s.role === r)!.pos, shift);
  const looks = new Map<Role, number>();
  looks.set(lead, towardN(dYaw));
  looks.set(trail, towardN(normYaw(dYaw + 180)));
  const kRole = startSlots[0].role;
  if (!looks.has(kRole)) {
    looks.set(kRole, normYaw(normalYaw + scanValue(0, cfg(config, 'scan.amp_lead'), cfg(config, 'scan.period'), 1.3, t, cfg(config, 'scan.dwell_exp'))));
  }
  const middle = order.filter((r) => !looks.has(r));
  if (middle.length) {
    const fixed = [...looks.entries()].map(([role, yaw]) => ({ role, yaw }));
    const members = startSlots.map((s) => ({ role: s.role, pos: pos(s.role) }));
    for (const l of FormationCoverRules.AllocateStatic([wall], members, fixed, middle, config)) looks.set(l.role, l.yaw);
  }
  return {
    active, leaderTarget, normalYaw, order, lead, rearGuard: startSlots.length >= 2 ? trail : null,
    targets: startSlots.map((s) => ({ role: s.role, pos: pos(s.role), yaw: looks.get(s.role)! })),
  };
}

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
