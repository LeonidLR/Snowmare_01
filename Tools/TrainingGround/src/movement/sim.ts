// Training Ground / movement: deterministic squad movement simulator (0 tokens, no Unreal). The leader walks a layout route,
// the terrain rules pick the formation, followers walk to their slots on the nav grid and turn to their look yaws.
// It tests the formation LOGIC (rules, classifier, sectors, pace); engine pathing / crowd avoidance stay Unreal's.
// Speeds / pace / turn rate come from formation_config.json; the heading smoothing mirrors FSquadFormationConfig (3.5 / s).

import type { FormationConfig } from './config.ts';
import { cfg } from './config.ts';
import type { Segment, Vec2 } from './geometry.ts';
import { add, angleDiff, clamp, closestOnSegment, deltaYaw, dirOf, dist, dot, len, localToWorld, normYaw, raycastWalls, rng, scale, segmentBlocked, sub, yawOf } from './geometry.ts';
import type { Formation, Layout, LayoutRoute } from './layout.ts';
import { wallSegments } from './layout.ts';
import { AGENT_RADIUS, NavGrid } from './nav.ts';
import type { GuardLevel, Mode, Role, TerrainClass, WallSide } from './rules.ts';
import {
  CLASSIFY_START, CORNER_START, FormationAssignRules, FormationBypassRules, FormationCoverRules, FormationCornerRules, FormationGuardRules, FormationSectorRules,
  FormationShapeRules, FormationTerrainRules, FormationWallRules, GUARD_START, rolesFor, wallRoom,
} from './rules.ts';

export const SIM_DT = 1 / 30;
const HEADING_SMOOTHING = 3.5; // FSquadFormationConfig::RotationSmoothing
const STOP_RADIUS = 25;
const WALL_TURN_DIST = 100; // cm: a member walking to his wall-layout slot turns to the layout yaw this close to it
const FOLLOW_GAIN = 2.0; // 1/s: a 1 m gap to the (moving) slot adds 2 m/s on top of the slot's own velocity
const REPATH_INTERVAL = 0.2;
const TRAIL_STEP = 25; // cm between the leader's trail points (the trail column, v2.1)
const TRAIL_KEEP = 1500; // cm of trail kept
const SEPARATION_MARGIN = 10; // cm beyond slot.min_spacing the separation aims for
const SEPARATION_SPEED = 60; // cm/s: members closer than slot.min_spacing drift apart (stands in for the engine's avoidance)
const REASSIGN_GAIN = 100; // cm: on the way, places are reassigned when the minimal assignment is this much cheaper (v2.2)
const WALK_LOOK_DEG = 35; // the leader's look within this of his travel = looking where he walks (exempt from ClearLook; the check allows 45)

export interface MemberFrame {
  /** Who he is (K the leader, P, L, Z). */
  role: Role;
  /** v2.1 fix: the place he holds in the formation (its slot and sectors), reassigned by minimal travel on every change. */
  place: Role;
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
  /** v2.1: this member looks round a wall end (FormationCornerRules.Watch overrides his sector). */
  cornerWatch: boolean;
  /** v2.2: he holds the rear (the place farthest back; FormationCoverRules.Allocate). */
  rearGuard: boolean;
}

/** v2.1: a bypass decision (FormationBypassRules.ChooseSide) taken when a route leg starts and a wall blocks it. */
export interface BypassFrame {
  wall: string;
  wallOn: WallSide;
  reason: 'shortest' | 'shielded' | 'too_long';
  detour: number;
}

/** v2.2: a wall slide in progress (FormationWallRules.Slide). */
export interface SlideFrame {
  /** Yaw of the wall's outward normal. */
  normalYaw: number;
  /** Travel direction along the wall. */
  travelYaw: number;
  /** Places from the leading to the trailing end (the order the slide must keep). */
  order: Role[];
}

export interface Frame {
  t: number;
  mode: Mode;
  /** v2.1: the formation in effect (after the wall guard). */
  formation: Formation;
  /** v2.1: the formation asked for (the manual choice, or the terrain's); differs from `formation` while the guard falls back. */
  requested: Formation;
  /** v2.1: 0 = as requested, 1 = the mirror echelon, 2 = the column (FormationGuardRules.Resolve). */
  guardLevel: GuardLevel;
  terrain: TerrainClass;
  heading: number;
  leaderDistance: number;
  members: MemberFrame[];
  /** v2.1: the bypass decisions taken so far on this run. */
  bypasses: readonly BypassFrame[];
  /** v2.2: the wall slide in progress, else null. */
  slide: SlideFrame | null;
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
  place: Role;
  pos: Vec2;
  yaw: number;
  jitter: number;
  path: Vec2[] | null;
  repathIn: number;
  prevSlot: Vec2 | null;
  clearHeld: number | null; // v2.3: the last ClearLook result (its hysteresis input); null while ClearLook does not apply
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

  // Leader path: planned per route leg when the leader starts it (planLeg: the bypass side round a blocking wall, then A*).
  const points = o.route.points;
  const threats = o.route.threats ?? [];
  const bypasses: BypassFrame[] = [];
  const planLeg = (from: Vec2, to: Vec2, now: number): Vec2[] => {
    const blocking = walls.map((w, i) => ({ w, i })).filter(({ w }) => segmentBlocked(from, to, [w], AGENT_RADIUS));
    let via: Vec2[] = [from, to];
    if (blocking.length) {
      // The first wall along the leg decides the side (one wall per leg; A* handles the rest).
      const along = (w: Segment) => dot(sub(closestOnSegment(from, w.a, w.b).point, from), sub(to, from));
      const { w, i } = blocking.reduce((a, b) => (along(b.w) < along(a.w) ? b : a));
      const known = threats.filter((th) => th.since_s <= now).map((th) => ({ pos: th.pos, ageS: now - th.since_s }));
      const choice = FormationBypassRules.ChooseSide(w, from, to, known, c);
      bypasses.push({ wall: o.layout.walls[i].id, wallOn: choice.wallOn, reason: choice.reason, detour: choice.detour });
      via = choice.path;
    }
    const out: Vec2[] = [from];
    for (let k = 1; k < via.length; k++) out.push(...(nav.path(via[k - 1], via[k]) ?? [via[k - 1], via[k]]).slice(1));
    return out;
  };
  let routeLeg = 1;
  let legs: Vec2[] = planLeg(points[0], points[1], 0);
  const finalPoint = points[points.length - 1];

  let heading = yawOf(sub(legs[1] ?? add(legs[0], [1, 0]), legs[0]));
  const startSlots = FormationShapeRules.Slots(n >= 4 ? 'diamond' : 'wedge', n, legs[0], heading, c);
  const spawnAt = (slot: Vec2): Vec2 => {
    const p = add(slot, [(random() - 0.5) * 60, (random() - 0.5) * 60]);
    if (nav.walkable(p)) return p;
    return fallbackSlot(nav, walls, legs[0], slot) ?? legs[0];
  };
  const members: Member[] = rolesFor(n).map((role, i) => ({
    role,
    place: role,
    pos: i === 0 ? legs[0] : spawnAt(startSlots[i].pos),
    yaw: heading,
    jitter: i === 0 ? 1 : 1 + (random() - 0.5) * 0.24,
    path: null,
    repathIn: 0,
    prevSlot: null,
    clearHeld: null,
  }));

  let leg = 1;
  let t = 0;
  let travelled = 0;
  let terrain: TerrainClass | null = null;
  let classify = CLASSIFY_START;
  let guard = GUARD_START;
  let corner = CORNER_START;
  // v2.1: the leader's recent path (oldest first, >= TRAIL_STEP apart, up to TRAIL_KEEP long) for the trail column.
  let trail: Vec2[] = [legs[0]];
  let lastTravelled = 0;
  let placesFor = ''; // the formation the current places were assigned for (formation + trail flag)
  let formation: Formation = o.manualFormation ?? (n >= 4 ? 'diamond' : 'wedge');
  let haltLeft = o.haltSeconds ?? 3;
  let arrived = false;
  let leaderWallSlot: Vec2 | null = null;
  // v2.2 wall slide: the stops along the wall after the route's end, the wall the layout stands at, the slide in progress.
  const slideStops = o.route.slide ?? [];
  let slideNext = 0;
  let layoutWall: Segment | null = null;
  let wallClick: Vec2 = finalPoint;
  let lastTargets: { role: Role; pos: Vec2; yaw: number }[] = [];
  let slide: { wall: Segment; start: { role: Role; pos: Vec2; yaw: number }[]; dest: Vec2; to: Vec2 } | null = null;
  let slideFrame: SlideFrame | null = null;
  const maxTime = 600;

  while (t < maxTime) {
    const leader = members[0];
    // --- leader
    let leaderVel: Vec2 = [0, 0];
    if (slide) {
      const to = sub(slide.to, leader.pos), d = len(to), step = leaderSpeed * SIM_DT;
      const move = d <= step ? to : scale(to, step / d);
      leader.pos = add(leader.pos, move); travelled += len(move); leaderVel = scale(move, 1 / SIM_DT);
      if (d <= step) {
        // Arrived at the stop: the normal wall layout again, facing the wall.
        wallClick = slide.to; slide = null; slideFrame = null; arrived = true; haltLeft = o.haltSeconds ?? 3;
      }
    } else if (!arrived) {
      let step = leaderSpeed * SIM_DT;
      while (step > 0 && leg < legs.length) {
        const to = sub(legs[leg], leader.pos);
        const d = len(to);
        if (d <= step) {
          leader.pos = legs[leg]; step -= d; travelled += d; leg++;
          if (leg >= legs.length && routeLeg + 1 < points.length) { routeLeg++; legs = planLeg(points[routeLeg - 1], points[routeLeg], t); leg = 1; }
          continue;
        }
        const move = scale(to, step / d);
        leader.pos = add(leader.pos, move);
        travelled += step;
        leaderVel = scale(move, 1 / SIM_DT);
        step = 0;
      }
      if (leg >= legs.length) arrived = true;
    }
    if (dist(trail[trail.length - 1], leader.pos) >= TRAIL_STEP) {
      trail.push(leader.pos);
      let keep = 0, k = trail.length - 1;
      while (k > 0 && keep < TRAIL_KEEP) { keep += dist(trail[k - 1], trail[k]); k--; }
      if (k > 0) trail = trail.slice(k);
    }
    if (slideFrame) heading = slideFrame.travelYaw;
    else if (layoutWall && slideNext > 0 && arrived) heading = normYaw(yawOf(sub(closestOnSegment(leader.pos, layoutWall.a, layoutWall.b).point, leader.pos)));
    else if (len(leaderVel) > 1) {
      const target = yawOf(leaderVel);
      heading = normYaw(heading + deltaYaw(heading, target) * clamp(HEADING_SMOOTHING * SIM_DT, 0, 1));
    }

    // The leader's pose `d` cm further along his path (the rest of this leg, then the route's next points), for the guard.
    const aheadPose = (d: number): { pos: Vec2; yaw: number } => {
      const path: Vec2[] = [leader.pos, ...legs.slice(leg), ...points.slice(routeLeg + 1)];
      let left = d, yaw = heading;
      for (let k = 1; k < path.length; k++) {
        const L = dist(path[k - 1], path[k]);
        if (L < 1e-6) continue;
        yaw = yawOf(sub(path[k], path[k - 1]));
        if (left <= L) return { pos: add(path[k - 1], scale(sub(path[k], path[k - 1]), left / L)), yaw };
        left -= L;
      }
      return { pos: path[path.length - 1], yaw };
    };

    // --- terrain / formation
    // v1.2: the leader's move ends at the route's last point (at_wall only on arrival there); in the wall layout his move goes
    // on to his own wall slot, which is then his destination (the layout must not drop on that last step).
    const destination = formation === 'wall' && leaderWallSlot ? leaderWallSlot : finalPoint;
    const stepDistance = travelled - lastTravelled;
    const slidePhase: boolean = slideNext > 0 && layoutWall !== null; // after the first slide began: the squad stays at the wall
    let requested: Formation = 'wall';
    let passage: number | null = null;
    if (!slidePhase) {
      classify = FormationTerrainRules.Classify(walls, leader.pos, heading, destination, classify, stepDistance, c);
      passage = classify.passageWidth; // v2: held with the corridor class (the column's tail still inside)
      terrain = classify.current!;
      requested = o.manualFormation ?? FormationTerrainRules.FormationFor(terrain, n, !!o.blizzard);
      // v2.1: the wall guard - no formation may cut the squad in two (mirror echelon, else the column; the request stays).
      const pathAhead: Vec2[] = arrived ? [leader.pos] : [leader.pos, ...legs.slice(leg), ...points.slice(routeLeg + 1)];
      guard = FormationGuardRules.Resolve(walls, requested, n, leader.pos, heading, arrived ? null : aheadPose(cfg(c, 'guard.lookahead')),
        FormationGuardRules.TurnAhead(pathAhead, heading, c), guard, stepDistance, c);
      formation = guard.effective!;
    } else { formation = 'wall'; terrain = 'at_wall'; }
    lastTravelled = travelled;
    const mode: Mode = slide ? 'walk' : arrived ? (formation === 'wall' ? 'wall' : 'halt') : run ? 'run' : o.allForward ? 'forward' : 'walk';

    // --- targets and look yaws
    let targets: { role: Role; pos: Vec2; yaw: number; decided?: boolean }[]; // decided: a user-decided look (v2.3, Decided)
    let slideLead: Role | null = null, slideRear: Role | null = null;
    if (slide) {
      const res = FormationWallRules.Slide(slide.wall, slide.start, leader.pos, slide.dest, t, c);
      targets = res.targets;
      slideLead = res.lead; slideRear = res.rearGuard;
    } else if (formation === 'wall') {
      const wall: Segment | null = slidePhase ? layoutWall : wallAhead(walls, leader.pos, heading);
      if (wall) {
        const room = wallRoom(wall, leader.pos);
        const z = members.find((m) => m.place === 'Z');
        // Tie: the 4th takes the side he reaches with less travel (FormationWallRules.BreakRoomTie, as the C++ caller).
        let rooms = room;
        // v2.2: at a stop after a wall slide the 4th (the pair partner) keeps his end when it has room for him (re-choosing the roomier end would
        // send him through the line); else the normal choice.
        const right = dirOf(yawOf(sub(leader.pos, closestOnSegment(leader.pos, wall.a, wall.b).point)) + 270);
        const zSide = z ? dot(sub(z.pos, leader.pos), right) : 0;
        if (slidePhase && z && (n >= 4 || n === 2) && (zSide >= 0 ? room.roomRight : room.roomLeft) >= 2 * cfg(c, 'wall.spacing')) {
          rooms = zSide >= 0 ? { roomLeft: 0, roomRight: 1 } : { roomLeft: 1, roomRight: 0 };
        } else if (z && n >= 4) {
          const endPos = (l: number, r: number) => FormationWallRules.Layout(wall, leader.pos, n, l, r, c).find((s) => s.role === 'Z')!.pos;
          rooms = FormationWallRules.BreakRoomTie(room.roomLeft, room.roomRight, dist(z.pos, endPos(1, 0)), dist(z.pos, endPos(0, 1)));
        }
        targets = FormationWallRules.Layout(wall, leaderWallSlot ? wallClick : leader.pos, n, rooms.roomLeft, rooms.roomRight, c);
        leaderWallSlot = targets[0].pos;
        layoutWall = wall;
      } else {
        targets = FormationShapeRules.Slots(n >= 4 ? 'diamond' : 'wedge', n, leader.pos, heading, c)
          .map((s) => ({ ...s, yaw: heading }));
      }
    } else if (FormationGuardRules.Effective(guard).trail) {
      const path = dist(trail[trail.length - 1], leader.pos) > 1e-6 ? [...trail, leader.pos] : trail;
      // Each man's sectors by his own surroundings (the corridor's held width, else a wall beside his slot -> narrow), relative
      // to the trail's direction at his slot; the rear man keeps the squad's 6 o'clock (the leader's heading).
      targets = FormationShapeRules.TrailSlots(path, heading, n, c, walls).map((s) => {
        const width = passage ?? FormationTerrainRules.SideWidth(walls, s.pos, s.yaw, c);
        return { ...s, yaw: FormationSectorRules.LookYaw(s.role, 'column', mode, n, t, s.role === 'Z' ? heading : s.yaw, width, c),
          // 1A is a corridor decision: only the classifier's held corridor width counts - a slot's side width at a doorway
          // crossed obliquely is not a corridor (P's "forward over the shoulder" aimed at the door's wall end 0.5 m away).
          decided: FormationSectorRules.Decided(s.role, 'column', n, passage, c) };
      });
    } else {
      targets = FormationShapeRules.Slots(formation, n, leader.pos, heading, c).map((s) => ({
        ...s, yaw: FormationSectorRules.LookYaw(s.role, formation, mode, n, t, heading, passage, c),
        decided: FormationSectorRules.Decided(s.role, formation, n, passage, c),
      }));
    }
    // v2.1: slicing the pie - one member looks round the end of the wall the squad passes (patrol only; not in the wall layout).
    // v2.1 fix: a new formation in effect (fold, unfold, mirror, any switch) -> the followers take its places by minimal travel.
    // v2.2: also on the way when a turn makes the minimal assignment REASSIGN_GAIN cheaper than the places held (a tight
    // formation pivoting 90 deg sent a flank man across the squad). Not during a wall slide (the order is kept).
    const key = `${formation}|${FormationGuardRules.Effective(guard).trail}`;
    {
      const followers = members.slice(1);
      const places = followers.map((m) => targets.find((x) => x.role === m.place)).filter((x) => !!x);
      if (places.length === followers.length && !slide) {
        const pick = FormationAssignRules.AssignPlaces(followers.map((m) => m.pos), places.map((x) => x!.pos));
        const held = followers.reduce((acc, m, i) => acc + dist(m.pos, places[i]!.pos), 0);
        const best = followers.reduce((acc, m, i) => acc + (pick[i] >= 0 ? dist(m.pos, places[pick[i]]!.pos) : 0), 0);
        if (key !== placesFor || held - best > REASSIGN_GAIN) {
          const labels = places.map((x) => x!.role);
          followers.forEach((m, i) => { if (pick[i] >= 0 && m.place !== labels[pick[i]]) { m.place = labels[pick[i]]; m.path = null; } });
        }
      }
      placesFor = key;
    }
    const watch = FormationCornerRules.Watch(walls, leader.pos, heading, members.map((m) => ({ role: m.place, pos: m.pos })), corner, c);
    corner = watch.state;
    // The corner watch on patrol, and on a wall slide (the leading end at the wall's end) - not in the standing wall layout.
    const looks = mode === 'walk' && (formation !== 'wall' || slide) ? watch.looks : [];
    if (looks.length) targets = targets.map((x) => ({ ...x, yaw: looks.find((l) => l.role === x.role)?.yaw ?? x.yaw }));
    // v2.2 (user 2026-10-25): with 3+ members walking / halted the looks come from the coverage allocator - K the front, the
    // place farthest back = the rear guard (6 o'clock), the others split the open arc. The pair, run, "all forward", the wall
    // layout and the STANDING hedgehog ring keep their rules (walking, the hedgehog is just a shape).
    let rearGuard: Role | null = slideRear;
    if (n >= 3 && (mode === 'walk' || (mode === 'halt' && formation !== 'hedgehog')) && formation !== 'wall') {
      const trailOn = FormationGuardRules.Effective(guard).trail;
      const hv = dirOf(heading);
      // v2.3 (1A clarified): the looks the user decided for the formation (the narrow column's K / P / L) are fixed looks too.
      const fixed = [...looks, ...targets.filter((x) => x.decided && !looks.some((l) => l.role === x.role)).map((x) => ({ role: x.role, yaw: x.yaw }))];
      const cover = FormationCoverRules.Allocate(walls, leader.pos, heading, targets.map((x, k) => ({
        role: x.role, pos: x.pos,
        back: trailOn ? k : -((x.pos[0] - leader.pos[0]) * hv[0] + (x.pos[1] - leader.pos[1]) * hv[1]),
      })), mode, t, c, fixed);
      if (cover) {
        rearGuard = cover.rearGuard;
        targets = targets.map((x) => ({ ...x, yaw: cover.looks.find((l) => l.role === x.role)?.yaw ?? x.yaw }));
      }
    }


    // --- members
    const frames: MemberFrame[] = [];
    for (let i = 0; i < members.length; i++) {
      const m = members[i];
      const target = targets.find((x) => x.role === m.place) ?? { role: m.place, pos: m.pos, yaw: m.yaw };
      let slot = target.pos;
      let slotFallback = false, slotInvalid = false;
      if (i > 0 || formation === 'wall') {
        if (!nav.walkable(slot)) {
          slotFallback = true;
          const alt = fallbackSlot(nav, walls, leader.pos, slot);
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
      // v2.1.1 (user "6А"): nobody looks into a wall, manual formations too - not the wall layout, the corner watcher, running,
      // or the LEADER looking where he walks (he must see his way; followers follow him - their step direction jitters along a
      // wall they hug, which made an exemption for them flip on and off).
      const watching = looks.some((l) => l.role === m.place);
      const walking = i === 0 && len(leaderVel) > 1 ? yawOf(leaderVel) : null;
      const exempt = formation === 'wall' || mode === 'run' || mode === 'wall' || watching || (walking !== null && angleDiff(target.yaw, walking) <= WALK_LOOK_DEG);
      const free = exempt ? target.yaw : FormationSectorRules.ClearLook(walls, m.pos, target.yaw, m.yaw, c, m.clearHeld);
      m.clearHeld = exempt ? null : free;
      const desired = (run && !arrived) || toWallSlot ? (len(moveDir) > 0.5 ? yawOf(moveDir) : heading) : free;
      m.yaw = normYaw(m.yaw + deltaYaw(m.yaw, desired) * clamp(turnSpeed * SIM_DT, 0, 1));
      if (i === 0 && formation !== 'wall') moveDir = scale(leaderVel, SIM_DT);
      const moving = len(moveDir) > 0.5;
      const backwards = moving && angleDiff(yawOf(moveDir), m.yaw) > 120;
      frames.push({ role: m.role, place: m.place, pos: m.pos, yaw: m.yaw, slot, slotFallback, slotInvalid, backwards, moveYaw: moving ? yawOf(moveDir) : null, cornerWatch: looks.some((l) => l.role === m.place), rearGuard: m.place === rearGuard });
    }
    // Light separation standing in for the engine's crowd avoidance (the game's RVO / detour crowd): two members closer than
    // slot.min_spacing drift apart at up to SEPARATION_SPEED; the leader is never pushed (he walks his path).
    const minSp = cfg(c, 'slot.min_spacing') + SEPARATION_MARGIN;
    for (let a = 0; a < members.length; a++) for (let b = a + 1; b < members.length; b++) {
      const A = members[a], B = members[b], dv = sub(B.pos, A.pos), d = len(dv);
      if (d >= minSp || d < 1e-6) continue;
      const push = Math.min((minSp - d) / 2, SEPARATION_SPEED * SIM_DT), u = scale(dv, 1 / d);
      const movers = a === 0 ? [[B, 2]] as const : [[A, -1], [B, 1]] as const;
      for (const [m, k] of movers) {
        const next = add(m.pos, scale(u, push * k));
        if (nav.walkable(next, AGENT_RADIUS * 0.8)) m.pos = next;
      }
    }
    frames.forEach((fr, i) => { fr.pos = members[i].pos; });
    o.onFrame({ t, mode, formation, requested, guardLevel: guard.level, terrain, heading, leaderDistance: travelled, members: frames, bypasses, slide: slideFrame });
    lastTargets = targets;
    void slideLead;

    t += SIM_DT;
    if (arrived) {
      haltLeft -= SIM_DT;
      if (haltLeft <= 0) {
        // v2.2: the next stop along the same wall -> a slide (if the rule takes it), else the run ends.
        const dest = slideStops[slideNext];
        if (!dest || !layoutWall || formation !== 'wall') break;
        const start = lastTargets.map((x) => ({ ...x }));
        const plan = FormationWallRules.Slide(layoutWall, start, start[0].pos, dest, t, c);
        slideNext++;
        if (!plan.active) break;
        slide = { wall: layoutWall, start, dest, to: plan.leaderTarget };
        slideFrame = { normalYaw: plan.normalYaw, travelYaw: yawOf(sub(plan.leaderTarget, start[0].pos)), order: plan.order };
        arrived = false;
      }
    }
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

/**
 * SquadFormation::ComputeFallbackSlots: the slot offset turned round the leader in 30 deg steps, at 100 % and 60 %.
 * v2.1: a candidate must also be on the leader's side - the straight line leader -> candidate crosses no wall (a walkable point
 * behind a wall splits the squad: FORMATION_CONTRACT.md 7 / 10).
 */
function fallbackSlot(nav: NavGrid, walls: readonly Segment[], leader: Vec2, slot: Vec2): Vec2 | null {
  const off = sub(slot, leader);
  const r = len(off), base = yawOf(off);
  const cands: Vec2[] = [];
  for (let k = 1; k <= 6; k++) for (const s of [1, -1]) for (const f of [1, 0.6]) {
    cands.push(localToWorld(leader, base + s * 30 * k, [Math.max(r * f, 120), 0]));
  }
  cands.sort((a, b) => dist(a, slot) - dist(b, slot));
  return cands.find((p) => nav.walkable(p) && !segmentBlocked(leader, p, walls, 0)) ?? null;
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
  let ff: Vec2 = [0, 0];
  const want = detour ? top : Math.min(top, d * FOLLOW_GAIN);
  if (detour) v = scale(dir, top);
  else {
    ff = scale(len(slotVel) > top ? scale(slotVel, top / len(slotVel)) : slotVel, jitter);
    v = add(ff, scale(dir, want));
  }
  let speed = len(v);
  if (speed < 1e-6) return m.pos;
  let cap = top;
  if (!run && angleDiff(yawOf(v), m.yaw) > 120) cap = Math.min(cap, walk * cfg(c, 'move.backward_mult'));
  if (speed > cap) {
    // v2.1.1: keep the formation's own speed first (the slot's velocity), close the gap with the speed to spare - scaling the
    // sum down let a big sideways correction (a fold, a swing) steal forward speed, so the man dropped behind his slot.
    const fu = ff[0] * dir[0] + ff[1] * dir[1], fl = len(ff);
    const disc = fu * fu - fl * fl + cap * cap;
    if (detour || fl >= cap || disc < 0) v = scale(detour ? v : ff, cap / Math.max(1e-6, len(detour ? v : ff)));
    else v = add(ff, scale(dir, Math.min(want, -fu + Math.sqrt(disc))));
    speed = len(v);
  }
  let step = scale(v, SIM_DT);
  if (detour && len(step) >= wd) { step = to; m.path!.splice(1, 1); }
  const next = add(m.pos, step);
  // Never step into a wall; a member already off the walkable area may step back onto it.
  return nav.walkable(next, AGENT_RADIUS * 0.8) || !nav.walkable(m.pos, AGENT_RADIUS * 0.8) ? next : m.pos;
}
