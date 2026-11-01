// Training Ground / movement: the custom formation (FORMATION_CONTRACT.md 17) - the player places the squad's shape himself and the
// squad keeps it: slots in the leader's frame, rotating with the heading, the looks the player's. Pure rules, same names and maths as
// the C++ port (FormationCustomRules); the golden vectors formation_Custom*.json prove the two agree.

import type { FormationConfig } from './config.ts';
import { cfg } from './config.ts';
import type { Segment, Vec2 } from './geometry.ts';
import { closestOnSegment, dirOf, dist, localToWorld, normYaw, segmentBlocked, sub, yawOf } from './geometry.ts';
import type { GuardLevel, GuardState, Role } from './rules.ts';
import { FormationSectorRules, rolesFor } from './rules.ts';

/** Constants (17; the same in TS and C++, not tuned). */
// cm: the farthest a member may stand from the anchor. 2026-11-01 (game designer, bug_05: the 10 m reach "ruins everything"): only a generous
// sanity reach - a mis-click on the far horizon, the wall fold's leader -> slot lines and the shared camera frame; 15-30 m shapes are fine.
export const CUSTOM_MAX_RADIUS = 4000;
export const CUSTOM_DRAG_MIN = 50; // cm: a drag shorter than this is a plain click (the default look)
export const CUSTOM_SLOT_COUNT = 3; // custom_1 .. custom_3
export const CUSTOM_MAX_MEMBERS = 4;
export const CUSTOM_ROW_SPACING = 100; // px: the three menu discs below the ring
export const CUSTOM_ROW_Y = 280; // px: their centre line below the menu centre (screen y down)
export const CUSTOM_ROW_RADIUS = 40; // px: a disc

/** One stored member (17.1): his place in the ANCHOR's frame and his look relative to the shape's heading. */
export interface CustomMember {
  /** The operative's SquadIndex (unique within a shape). */
  squad: number;
  /** Forward along the heading, cm. */
  fwd: number;
  /** Right of the heading (heading + 90), cm. */
  right: number;
  /** Look relative to the heading, deg in (-180, 180]. */
  look: number;
}

/** The slot storage (17.1): members[0] = the anchor (0, 0, 0), the others by squad ascending; empty = no shape. */
export interface CustomShape { members: CustomMember[] }

export interface CustomSlot {
  squad: number;
  role: Role;
  pos: Vec2;
  /** World yaw of his look. */
  yaw: number;
  /** true = a stored place (his look is the player's); false = an extra standing behind the shape. */
  fixed: boolean;
}

/** The bot's sample shape (17.8): the anchor, P 250 back / 300 right looking 60, L 250 back / 300 left looking -60, Z 450 back looking 180. */
export const CUSTOM_SAMPLE_SHAPE: CustomShape = {
  members: [
    { squad: 0, fwd: 0, right: 0, look: 0 },
    { squad: 1, fwd: -250, right: 300, look: 60 },
    { squad: 2, fwd: -250, right: -300, look: -60 },
    { squad: 3, fwd: -450, right: 0, look: 180 },
  ],
};

/** Round half away from zero to 0.1 (never -0). */
function round1(v: number): number {
  const r = (Math.sign(v) * Math.round(Math.abs(v) * 10)) / 10;
  return r === 0 ? 0 : r;
}

/** 17.1: fwd / right / look stored rounded to 0.1 cm / 0.1 deg (round half away from zero); the look stays in (-180, 180]. */
function RoundShape(shape: CustomShape): CustomShape {
  return { members: shape.members.map((m) => ({ squad: m.squad, fwd: round1(m.fwd), right: round1(m.right), look: normYaw(round1(m.look)) })) };
}

/**
 * 17.2: the world slots of the shape. `present` = the squad indices of the living members in the formation, the LEADER first, then the
 * followers in squad order. The leader takes place 0; the operative stored at place 0 (when present and not the leader) takes the place
 * the leader had in the shape (an extra when the leader is not in it); everybody else his own place; a present member the shape lacks is
 * an extra (a stored member not present is skipped). Output order: the leader, then the others in `present` order; roles by rolesFor of
 * that count. Placed: the stored place in the leader's frame, look = leaderYaw + look, fixed. Extras (k = 1, 2, ...) stand in a column
 * behind the shape: fwd = the smallest fwd given out - k x column.spacing, looking back, not fixed. Empty shape -> [].
 */
function CustomSlots(shape: CustomShape, present: readonly number[], leaderPos: Vec2, leaderYaw: number, config: FormationConfig): CustomSlot[] {
  const who = present.slice(0, CUSTOM_MAX_MEMBERS);
  if (shape.members.length === 0 || who.length === 0) return [];
  const leader = who[0];
  const anchor = shape.members[0].squad;
  const placeOfSquad = new Map<number, number>();
  shape.members.forEach((m, j) => { if (!placeOfSquad.has(m.squad)) placeOfSquad.set(m.squad, j); });
  const leaderPlace = placeOfSquad.get(leader);
  const placeOf = (squad: number): number | null => {
    if (squad === leader) return 0;
    if (squad === anchor) return leaderPlace ?? null; // the anchor steps into the leader's place (an extra when the leader is not stored)
    return placeOfSquad.get(squad) ?? null;
  };
  const roles = rolesFor(who.length);
  const places = who.map(placeOf);
  const minFwd = Math.min(0, ...places.filter((p): p is number => p !== null).map((p) => shape.members[p].fwd));
  const spacing = cfg(config, 'column.spacing');
  let extra = 0;
  return who.map((squad, i) => {
    const p = places[i];
    if (p !== null) {
      const m = shape.members[p];
      return { squad, role: roles[i], pos: localToWorld(leaderPos, leaderYaw, [m.fwd, m.right]), yaw: normYaw(leaderYaw + m.look), fixed: true };
    }
    extra++;
    return { squad, role: roles[i], pos: localToWorld(leaderPos, leaderYaw, [minFwd - extra * spacing, 0]), yaw: normYaw(leaderYaw + 180), fixed: false };
  });
}

/**
 * 17.3: the roles (slot order, the leader never) whose straight line leaderPos -> slot passes within guard.clearance of a wall body.
 * No chain pairs: the custom shape has no neighbour order, every slot must see the leader.
 */
function BlockedRoles(walls: readonly Segment[], slots: readonly CustomSlot[], leaderPos: Vec2, config: FormationConfig): Role[] {
  const r = cfg(config, 'guard.clearance');
  return slots.slice(1).filter((s) => segmentBlocked(leaderPos, s.pos, walls, r)).map((s) => s.role);
}

/**
 * 17.4: the wall guard of the custom shape (10.1 / 13.6): requested 'custom'; effective 'custom' at level 0, 'column' (the trail column,
 * FormationShapeRules.TrailSlots) at level 2; no level 1, no turn fold. Raw level 2 (cause 'wall') when BlockedRoles is not empty at the
 * leader or at `ahead`, or a member is cut off (allInSight false); else 0. Hysteresis as 10.1: a new request or raw >= the level in
 * force -> the raw level at once; else (level 2 waiting for 0) after classify.hold_distance of the leader's path (stepDistance per call),
 * keeping the level and its cause meanwhile. The caller does not call it for an empty shape.
 */
function Resolve(walls: readonly Segment[], shape: CustomShape, present: readonly number[], leaderPos: Vec2, leaderYaw: number,
  ahead: { pos: Vec2; yaw: number } | null, state: GuardState, stepDistance: number, config: FormationConfig, allInSight = true): GuardState {
  const blockedAt = (pos: Vec2, yaw: number) => BlockedRoles(walls, CustomSlots(shape, present, pos, yaw, config), pos, config).length > 0;
  const raw: GuardLevel = blockedAt(leaderPos, leaderYaw) || (ahead !== null && blockedAt(ahead.pos, ahead.yaw)) || !allInSight ? 2 : 0;
  const at = (level: GuardLevel, candidate: GuardLevel | null = null, candidateDistance = 0): GuardState =>
    ({ requested: 'custom', level, effective: level === 0 ? 'custom' : 'column', candidate, candidateDistance, cause: level === 0 ? null : 'wall' });
  if (state.requested !== 'custom' || raw >= state.level) return at(raw);
  const walked = (state.candidate === 0 ? state.candidateDistance : 0) + Math.max(0, stepDistance);
  if (walked >= cfg(config, 'classify.hold_distance')) return at(0);
  return { ...at(2, 0, walked), cause: state.cause ?? 'wall' }; // level 2 held with the cause it had
}

/**
 * 17.5: the player's look is kept unless it points INTO a wall (a wall whose surface is within look.wall_dist of pos, its closest point
 * >= 1e-6 away, and the direction to that point within look.wall_angle of yaw, strictly): then FormationSectorRules.ClearLook (13.1 / 13.4;
 * no limit, no anticipation), with `held` his previous result. Not blocked -> normYaw(yaw) (no sliver / hold logic: his look stays his).
 */
function Look(walls: readonly Segment[], pos: Vec2, yaw: number, currentYaw: number, config: FormationConfig, held: number | null = null): number {
  const reach = cfg(config, 'look.wall_dist'), half = cfg(config, 'look.wall_angle');
  const blocked = walls.some((w) => {
    const q = closestOnSegment(pos, w.a, w.b).point;
    if (Math.max(0, dist(pos, q) - (w.thickness ?? 0) / 2) > reach || dist(pos, q) < 1e-6) return false;
    return Math.abs(normYaw(yaw - yawOf(sub(q, pos)))) < half - 1e-6;
  });
  return blocked ? FormationSectorRules.ClearLook(walls, pos, yaw, currentYaw, config, held) : normYaw(yaw);
}

/** 17.6: the look of a placement drag: ghost -> cursor; a drag shorter than CUSTOM_DRAG_MIN is a plain click (the squad's heading). */
function DragLook(ghostPos: Vec2, cursorPos: Vec2, defaultYaw: number): number {
  return dist(ghostPos, cursorPos) < CUSTOM_DRAG_MIN ? normYaw(defaultYaw) : yawOf(sub(cursorPos, ghostPos));
}

export type CanPlaceResult = 'ok' | 'too_far' | 'too_close';

/**
 * 17.6: may `squad`'s ghost stand at `pos`? The ghosts after the placement (his moved to pos, added when missing): too_close when another
 * ghost is within slot.min_spacing of pos (distance < spacing); else too_far when any ghost (pos's included) is farther than
 * CUSTOM_MAX_RADIUS from the anchor's ghost (squad == anchorSquad: the anchor moves, the others must stay within reach; no anchor ghost
 * -> no reach test).
 */
function CanPlace(ghosts: readonly { squad: number; pos: Vec2 }[], anchorSquad: number, squad: number, pos: Vec2, config: FormationConfig): CanPlaceResult {
  const after = ghosts.some((g) => g.squad === squad)
    ? ghosts.map((g) => (g.squad === squad ? { squad, pos } : g))
    : [...ghosts, { squad, pos }];
  if (after.some((g) => g.squad !== squad && dist(g.pos, pos) < cfg(config, 'slot.min_spacing'))) return 'too_close';
  const anchor = after.find((g) => g.squad === anchorSquad);
  if (anchor && after.some((g) => dist(g.pos, anchor.pos) > CUSTOM_MAX_RADIUS)) return 'too_far';
  return 'ok';
}

/**
 * 17.6: the shape from the placed ghosts: the anchor ghost's pos / yaw is the frame, every ghost -> fwd / right (his pos in that frame)
 * and look = normYaw(yaw - anchorYaw); the anchor first (forced 0, 0, 0), the others by squad ascending, at most CUSTOM_MAX_MEMBERS
 * (the anchor included); RoundShape applied. No anchor ghost -> an empty shape.
 */
function ShapeFrom(ghosts: readonly { squad: number; pos: Vec2; yaw: number }[], anchorSquad: number): CustomShape {
  const anchor = ghosts.find((g) => g.squad === anchorSquad);
  if (!anchor) return { members: [] };
  const f = dirOf(anchor.yaw), r: Vec2 = [-f[1], f[0]];
  const others = ghosts.filter((g) => g.squad !== anchorSquad).sort((a, b) => a.squad - b.squad).slice(0, CUSTOM_MAX_MEMBERS - 1);
  return RoundShape({
    members: [
      { squad: anchorSquad, fwd: 0, right: 0, look: 0 },
      ...others.map((g) => {
        const d = sub(g.pos, anchor.pos);
        return { squad: g.squad, fwd: d[0] * f[0] + d[1] * f[1], right: d[0] * r[0] + d[1] * r[1], look: normYaw(g.yaw - anchor.yaw) };
      }),
    ],
  });
}

export const FormationCustomRules = { CustomSlots, BlockedRoles, Resolve, Look, DragLook, CanPlace, ShapeFrom, RoundShape };
