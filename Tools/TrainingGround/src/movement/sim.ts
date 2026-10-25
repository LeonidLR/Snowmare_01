// Training Ground / movement: deterministic squad movement simulator (0 tokens, no Unreal). The leader walks a layout route,
// the terrain rules pick the formation, followers walk to their slots on the nav grid and turn to their look yaws.
// It tests the formation LOGIC (rules, classifier, sectors, pace); engine pathing / crowd avoidance stay Unreal's.
// Speeds / pace / turn rate come from formation_config.json; the heading smoothing mirrors FSquadFormationConfig (3.5 / s).

import type { FormationConfig } from './config.ts';
import { cfg } from './config.ts';
import type { Segment, Vec2 } from './geometry.ts';
import { add, angleDiff, clamp, deltaYaw, dirOf, dist, len, localToWorld, normYaw, raycastWalls, rng, scale, sub, yawOf } from './geometry.ts';
import type { Formation, Layout, LayoutRoute } from './layout.ts';
import { wallSegments } from './layout.ts';
import { AGENT_RADIUS, NavGrid } from './nav.ts';
import type { Mode, Role, TerrainClass } from './rules.ts';
import { FormationSectorRules, FormationShapeRules, FormationTerrainRules, FormationWallRules, rolesFor, wallRoom } from './rules.ts';

export const SIM_DT = 1 / 30;
const HEADING_SMOOTHING = 3.5; // FSquadFormationConfig::RotationSmoothing
const STOP_RADIUS = 25;
const WALL_TURN_DIST = 100; // cm: a member walking to his wall-layout slot turns to the layout yaw this close to it
const FOLLOW_GAIN = 2.0; // 1/s: a 1 m gap to the (moving) slot adds 2 m/s on top of the slot's own velocity
const REPATH_INTERVAL = 0.2;

export interface MemberFrame {
  role: Role;
  pos: Vec2;
  yaw: number;
  slot: Vec2;
  /** The slot the formation asked for was inside a wall / off the walkable area (a fallback was used). */
  slotFallback: boolean;
  /** No walkable slot found at all. */
  slotInvalid: boolean;
  /** Moving backwards relative to his facing (> 120 deg). */
  backwards: boolean;
  /** Travel direction this tick (null when standing). */
  moveYaw: number | null;
}

export interface Frame {
  t: number;
  mode: Mode;
  formation: Formation;
  terrain: TerrainClass;
  heading: number;
  leaderDistance: number;
  members: MemberFrame[];
}

export interface SimOptions {
  layout: Layout;
  route: LayoutRoute;
  memberCount: number;
  config: FormationConfig;
  seed: number;
  /** Fixed formation (the hold-G manual choice); undefined = automatic by terrain. */
  manualFormation?: Formation;
  /** v1.2 (user Q4 = A): the weather is a blizzard - open ground picks the blizzard tight diamond. */
  blizzard?: boolean;
  /** v1.2: the menu's "All forward" - walking, everyone looks at 12 (LookYaw mode 'forward'). */
  allForward?: boolean;
  /** Seconds the squad stays after the leader reaches the end of the route. */
  haltSeconds?: number;
  /** Shared grid (built once per layout). */
  nav?: NavGrid;
  onFrame: (frame: Frame) => void;
}

interface Member {
  role: Role;
  pos: Vec2;
  yaw: number;
  jitter: number;
  path: Vec2[] | null;
  repathIn: number;
  prevSlot: Vec2 | null;
}

/** Builds the nav grid for a layout (reuse it across runs). */
export const buildNav = (layout: Layout): NavGrid => new NavGrid(wallSegments(layout), layout.bounds.min, layout.bounds.max);

/** Runs one route; returns the total simulated time. */
export function simulate(o: SimOptions): number {
  const c = o.config;
  const walls = wallSegments(o.layout);
  const nav = o.nav ?? buildNav(o.layout);
  const random = rng(o.seed * 7919 + o.route.id.length * 31);
  const n = o.memberCount;
  const run = o.route.pace === 'run';
  const leaderSpeed = run ? cfg(c, 'pace.run_speed') : cfg(c, 'pace.walk_speed') * cfg(c, 'pace.patrol');
  const turnSpeed = cfg(c, 'facing.turn_speed');

  // Leader path: the route points, with A* detours where a straight leg is blocked.
  const legs: Vec2[] = [o.route.points[0]];
  for (let i = 1; i < o.route.points.length; i++) {
    const p = nav.path(o.route.points[i - 1], o.route.points[i]) ?? [o.route.points[i - 1], o.route.points[i]];
    legs.push(...p.slice(1));
  }

  let heading = yawOf(sub(legs[1] ?? add(legs[0], [1, 0]), legs[0]));
  const startSlots = FormationShapeRules.Slots(n >= 4 ? 'diamond' : 'wedge', n, legs[0], heading, c);
  const spawnAt = (slot: Vec2): Vec2 => {
    const p = add(slot, [(random() - 0.5) * 60, (random() - 0.5) * 60]);
    if (nav.walkable(p)) return p;
    return fallbackSlot(nav, legs[0], slot) ?? legs[0];
  };
  const members: Member[] = rolesFor(n).map((role, i) => ({
    role,
    pos: i === 0 ? legs[0] : spawnAt(startSlots[i].pos),
    yaw: heading,
    jitter: i === 0 ? 1 : 1 + (random() - 0.5) * 0.24,
    path: null,
    repathIn: 0,
    prevSlot: null,
  }));

  let leg = 1;
  let t = 0;
  let travelled = 0;
  let terrain: TerrainClass | null = null;
  let timeInClass = 0;
  let formation: Formation = o.manualFormation ?? (n >= 4 ? 'diamond' : 'wedge');
  let haltLeft = o.haltSeconds ?? 3;
  let arrived = false;
  let leaderWallSlot: Vec2 | null = null;
  const maxTime = 600;

  while (t < maxTime) {
    const leader = members[0];
    // --- leader
    let leaderVel: Vec2 = [0, 0];
    if (!arrived) {
      let step = leaderSpeed * SIM_DT;
      while (step > 0 && leg < legs.length) {
        const to = sub(legs[leg], leader.pos);
        const d = len(to);
        if (d <= step) { leader.pos = legs[leg]; step -= d; travelled += d; leg++; continue; }
        const move = scale(to, step / d);
        leader.pos = add(leader.pos, move);
        travelled += step;
        leaderVel = scale(move, 1 / SIM_DT);
        step = 0;
      }
      if (leg >= legs.length) arrived = true;
    }
    if (len(leaderVel) > 1) {
      const target = yawOf(leaderVel);
      heading = normYaw(heading + deltaYaw(heading, target) * clamp(HEADING_SMOOTHING * SIM_DT, 0, 1));
    }

    // --- terrain / formation
    // v1.2: the leader's move ends at the route's last point (at_wall only on arrival there); in the wall layout his move goes
    // on to his own wall slot, which is then his destination (the layout must not drop on that last step).
    const destination = formation === 'wall' && leaderWallSlot ? leaderWallSlot : legs[legs.length - 1];
    const next = FormationTerrainRules.Classify(walls, leader.pos, heading, destination, terrain, timeInClass, c);
    const passage = FormationTerrainRules.PassageWidth(walls, leader.pos, heading, c);
    if (next !== terrain) { terrain = next; timeInClass = 0; } else timeInClass += SIM_DT;
    if (!o.manualFormation) formation = FormationTerrainRules.FormationFor(terrain, n, !!o.blizzard);
    const mode: Mode = arrived ? (formation === 'wall' ? 'wall' : 'halt') : run ? 'run' : o.allForward ? 'forward' : 'walk';

    // --- targets and look yaws
    let targets: { role: Role; pos: Vec2; yaw: number }[];
    if (formation === 'wall') {
      const wall = wallAhead(walls, leader.pos, heading);
      if (wall) {
        const room = wallRoom(wall, leader.pos);
        const z = members.find((m) => m.role === 'Z');
        // Tie: the 4th takes the side he reaches with less travel (FormationWallRules.BreakRoomTie, as the C++ caller).
        let rooms = room;
        if (z && n >= 4) {
          const endPos = (l: number, r: number) => FormationWallRules.Layout(wall, leader.pos, n, l, r, c).find((s) => s.role === 'Z')!.pos;
          rooms = FormationWallRules.BreakRoomTie(room.roomLeft, room.roomRight, dist(z.pos, endPos(1, 0)), dist(z.pos, endPos(0, 1)));
        }
        targets = FormationWallRules.Layout(wall, leaderWallSlot ? legs[legs.length - 1] : leader.pos, n, rooms.roomLeft, rooms.roomRight, c);
        leaderWallSlot = targets[0].pos;
      } else {
        targets = FormationShapeRules.Slots(n >= 4 ? 'diamond' : 'wedge', n, leader.pos, heading, c)
          .map((s) => ({ ...s, yaw: heading }));
      }
    } else {
      targets = FormationShapeRules.Slots(formation, n, leader.pos, heading, c).map((s) => ({
        ...s, yaw: FormationSectorRules.LookYaw(s.role, formation, mode, n, t, heading, passage, c),
      }));
    }

    // --- members
    const frames: MemberFrame[] = [];
    for (let i = 0; i < members.length; i++) {
      const m = members[i];
      const target = targets.find((x) => x.role === m.role) ?? { role: m.role, pos: m.pos, yaw: m.yaw };
      let slot = target.pos;
      let slotFallback = false, slotInvalid = false;
      if (i > 0 || formation === 'wall') {
        if (!nav.walkable(slot)) {
          slotFallback = true;
          const alt = fallbackSlot(nav, leader.pos, slot);
          if (alt) slot = alt; else { slotInvalid = true; slot = m.pos; }
        }
      }
      let moveDir: Vec2 = [0, 0];
      if (i > 0 || formation === 'wall') {
        const before = m.pos;
        m.pos = stepTowards(m, slot, nav, c, run, i === 0 ? 1 : m.jitter);
        moveDir = sub(m.pos, before);
      }
      // Facing: run = travel direction for everyone; walking to a wall-layout slot = travel direction until within
      // WALL_TURN_DIST of it, then the layout yaw (as an operative walking into cover; FORMATION_CONTRACT.md 7);
      // otherwise the rule's look yaw.
      const toWallSlot = formation === 'wall' && dist(m.pos, slot) > WALL_TURN_DIST && len(moveDir) > 0.5;
      const desired = (run && !arrived) || toWallSlot ? (len(moveDir) > 0.5 ? yawOf(moveDir) : heading) : target.yaw;
      m.yaw = normYaw(m.yaw + deltaYaw(m.yaw, desired) * clamp(turnSpeed * SIM_DT, 0, 1));
      if (i === 0 && formation !== 'wall') moveDir = scale(leaderVel, SIM_DT);
      const moving = len(moveDir) > 0.5;
      const backwards = moving && angleDiff(yawOf(moveDir), m.yaw) > 120;
      frames.push({ role: m.role, pos: m.pos, yaw: m.yaw, slot, slotFallback, slotInvalid, backwards, moveYaw: moving ? yawOf(moveDir) : null });
    }
    o.onFrame({ t, mode, formation, terrain, heading, leaderDistance: travelled, members: frames });

    t += SIM_DT;
    if (arrived) { haltLeft -= SIM_DT; if (haltLeft <= 0) break; }
  }
  return t;
}

/** The wall the leader faces (forward ray within 3 m), for the wall layout. */
function wallAhead(walls: readonly Segment[], pos: Vec2, heading: number): Segment | null {
  let best: Segment | null = null, bestD = 300;
  for (const w of walls) {
    const d = raycastWalls(pos, dirOf(heading), [w], 300);
    if (d < bestD) { bestD = d; best = w; }
  }
  return best;
}

/** SquadFormation::ComputeFallbackSlots: the slot offset turned round the leader in 30 deg steps, at 100 % and 60 %. */
function fallbackSlot(nav: NavGrid, leader: Vec2, slot: Vec2): Vec2 | null {
  const off = sub(slot, leader);
  const r = len(off), base = yawOf(off);
  const cands: Vec2[] = [];
  for (let k = 1; k <= 6; k++) for (const s of [1, -1]) for (const f of [1, 0.6]) {
    cands.push(localToWorld(leader, base + s * 30 * k, [Math.max(r * f, 120), 0]));
  }
  cands.sort((a, b) => dist(a, slot) - dist(b, slot));
  return cands.find((p) => nav.walkable(p)) ?? null;
}

/**
 * One movement step of a member towards his (moving) slot: feed-forward of the slot's velocity plus a proportional
 * correction (FOLLOW_GAIN), capped by the member's top speed x catchup_mult and, walking backwards, by
 * walk_speed x backward_mult. Detours (no line of sight to the slot) follow the nav path at the capped speed.
 * Note: the game's ComputeFollowerSpeed has no feed-forward (it slows down proportionally inside SlowRadius even while the
 * slot moves), which makes followers trail ~2 m behind a walking leader - see the bot report notes.
 */
function stepTowards(m: Member, slot: Vec2, nav: NavGrid, c: FormationConfig, run: boolean, jitter: number): Vec2 {
  const slotVel: Vec2 = m.prevSlot ? scale(sub(slot, m.prevSlot), 1 / SIM_DT) : [0, 0];
  m.prevSlot = slot;
  const d = dist(m.pos, slot);
  if (d < STOP_RADIUS && len(slotVel) < 1) { m.path = null; return m.pos; }
  m.repathIn -= SIM_DT;
  if (!m.path || m.repathIn <= 0) {
    m.path = nav.path(m.pos, slot);
    m.repathIn = REPATH_INTERVAL;
  }
  const detour = !!m.path && m.path.length > 2;
  const waypoint = m.path && m.path.length > 1 ? m.path[1] : slot;
  const to = sub(waypoint, m.pos);
  const wd = len(to);
  if (wd < 1e-6) { m.path?.splice(1, 1); return m.pos; }
  const dir = scale(to, 1 / wd);
  const walk = cfg(c, 'pace.walk_speed');
  const top = (run ? cfg(c, 'pace.run_speed') : walk) * cfg(c, 'move.catchup_mult');
  let v: Vec2;
  if (detour) v = scale(dir, top);
  else {
    const ff = len(slotVel) > top ? scale(slotVel, top / len(slotVel)) : slotVel;
    v = add(scale(ff, jitter), scale(dir, Math.min(top, d * FOLLOW_GAIN)));
  }
  let speed = len(v);
  if (speed < 1e-6) return m.pos;
  let cap = top;
  if (!run && angleDiff(yawOf(v), m.yaw) > 120) cap = Math.min(cap, walk * cfg(c, 'move.backward_mult'));
  if (speed > cap) { v = scale(v, cap / speed); speed = cap; }
  let step = scale(v, SIM_DT);
  if (detour && len(step) >= wd) { step = to; m.path!.splice(1, 1); }
  const next = add(m.pos, step);
  // Never step into a wall; a member already off the walkable area may step back onto it.
  return nav.walkable(next, AGENT_RADIUS * 0.8) || !nav.walkable(m.pos, AGENT_RADIUS * 0.8) ? next : m.pos;
}
