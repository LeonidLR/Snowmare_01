// Training Ground / movement: the custom formation rules (node --test; FORMATION_CONTRACT.md 17): slots, guard, look, placement, menu.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { configValues } from './config.ts';
import type { Segment, Vec2 } from './geometry.ts';
import { angleDiff, dist, normYaw } from './geometry.ts';
import { readConfigFile, readLayout } from './cli/paths.ts';
import { runSeries } from './bot.ts';
import { CUSTOM_DRAG_MIN, CUSTOM_MAX_MEMBERS, CUSTOM_MAX_RADIUS, CUSTOM_SAMPLE_SHAPE, CUSTOM_SLOT_COUNT, FormationCustomRules as R } from './custom.ts';
import type { CustomShape } from './custom.ts';
import { guardHudText, menuLabelEn } from './labels.ts';
import { FormationMenuRules as M, MENU_ITEMS, MENU_START, menuLabel } from './menu.ts';
import type { MenuItemId, MenuState } from './menu.ts';
import type { GuardState } from './rules.ts';
import { FormationSectorRules, GUARD_START } from './rules.ts';

const C = configValues(readConfigFile());
const SAMPLE = CUSTOM_SAMPLE_SHAPE;
const near = (a: Vec2, b: Vec2, tol = 1e-6) => assert.ok(dist(a, b) <= tol, `${a} vs ${b}`);
const walls = {
  none: [] as Segment[],
  left: [{ a: [-2000, -300], b: [2000, -300], thickness: 40 }] as Segment[], // through the L slot (right -300)
  right: [{ a: [-2000, 300], b: [2000, 300], thickness: 40 }] as Segment[],
  behind: [{ a: [-200, -600], b: [-200, 600], thickness: 20 }] as Segment[],
  far_left: [{ a: [-2000, -700], b: [2000, -700], thickness: 40 }] as Segment[],
  near_left: [{ a: [-2000, -200], b: [2000, -200], thickness: 40 }] as Segment[], // 180 cm from the surface: inside look.wall_dist
};

test('constants and the sample shape (17 / 17.8)', () => {
  assert.deepEqual([CUSTOM_MAX_RADIUS, CUSTOM_DRAG_MIN, CUSTOM_SLOT_COUNT, CUSTOM_MAX_MEMBERS], [4000, 50, 3, 4]);
  assert.deepEqual(SAMPLE.members.map((m) => [m.squad, m.fwd, m.right, m.look]), [[0, 0, 0, 0], [1, -250, 300, 60], [2, -250, -300, -60], [3, -450, 0, 180]]);
});

// --- 17.2 CustomSlots

test('CustomSlots: the leader is the anchor - every member on his stored place in the leader\'s frame, fixed, looks = heading + look', () => {
  const s = R.CustomSlots(SAMPLE, [0, 1, 2, 3], [100, 50], 0, C);
  assert.deepEqual(s.map((x) => [x.squad, x.role, x.fixed]), [[0, 'K', true], [1, 'P', true], [2, 'L', true], [3, 'Z', true]]);
  near(s[0].pos, [100, 50]); near(s[1].pos, [-150, 350]); near(s[2].pos, [-150, -250]); near(s[3].pos, [-350, 50]);
  assert.deepEqual(s.map((x) => x.yaw), [0, 60, -60, 180]);
  // turned to face +Y (yaw 90): forward = +Y, right = -X; looks rotate with the heading, normalised to (-180, 180]
  const t = R.CustomSlots(SAMPLE, [0, 1, 2, 3], [100, 50], 90, C);
  near(t[1].pos, [100 - 300, 50 - 250]); near(t[2].pos, [100 + 300, 50 - 250]); near(t[3].pos, [100, 50 - 450]);
  assert.deepEqual(t.map((x) => x.yaw), [90, 150, 30, -90]);
});

test('CustomSlots: another stored member leads - he takes place 0, the anchor steps into the place the leader had', () => {
  const s = R.CustomSlots(SAMPLE, [1, 0, 2], [0, 0], 0, C); // squad 1 leads; the anchor (squad 0) is behind him
  assert.deepEqual(s.map((x) => [x.squad, x.role]), [[1, 'K'], [0, 'P'], [2, 'Z']]);
  near(s[0].pos, [0, 0]); assert.equal(s[0].yaw, 0);
  near(s[1].pos, [-250, 300]); assert.equal(s[1].yaw, 60); // the leader's old place and look
  near(s[2].pos, [-250, -300]); assert.equal(s[2].yaw, -60); // his own place
  // the anchor dead: the leader still takes place 0, nobody takes the vacated place
  const d = R.CustomSlots(SAMPLE, [1, 2, 3], [0, 0], 0, C);
  assert.deepEqual(d.map((x) => [x.squad, x.fixed]), [[1, true], [2, true], [3, true]]);
  near(d[0].pos, [0, 0]); near(d[1].pos, [-250, -300]); near(d[2].pos, [-450, 0]);
});

test('CustomSlots: a dead member is skipped, the others keep their places, the roles follow the output order', () => {
  const s = R.CustomSlots(SAMPLE, [0, 2, 3], [0, 0], 0, C);
  assert.deepEqual(s.map((x) => [x.squad, x.role]), [[0, 'K'], [2, 'P'], [3, 'Z']]);
  near(s[1].pos, [-250, -300]); near(s[2].pos, [-450, 0]);
  assert.deepEqual(R.CustomSlots(SAMPLE, [2], [10, 20], 0, C).map((x) => [x.squad, x.role, x.pos[0], x.pos[1]]), [[2, 'K', 10, 20]], 'alone: place 0');
});

test('CustomSlots: a member the shape lacks is an extra behind the shape, column.spacing apart, looking back, not fixed', () => {
  const sp = C['column.spacing'];
  const pair: CustomShape = { members: [{ squad: 0, fwd: 0, right: 0, look: 0 }, { squad: 2, fwd: -312.4, right: 187.6, look: 33.3 }] };
  const a = R.CustomSlots(pair, [0, 1, 2, 3], [0, 0], 0, C);
  assert.deepEqual(a.map((x) => [x.squad, x.fixed]), [[0, true], [1, false], [2, true], [3, false]]);
  near(a[2].pos, [-312.4, 187.6]);
  near(a[1].pos, [-312.4 - sp, 0]); near(a[3].pos, [-312.4 - 2 * sp, 0]); assert.equal(a[1].yaw, 180); assert.equal(a[3].yaw, 180);
  // the stored member dead: minFwd = 0 (the leader alone), extras start right behind him
  const b = R.CustomSlots(pair, [0, 1, 3], [0, 0], 0, C);
  near(b[1].pos, [-sp, 0]); near(b[2].pos, [-2 * sp, 0]);
  // the extras turn with the heading: yaw = heading + 180
  assert.equal(R.CustomSlots(pair, [0, 1], [0, 0], 90, C)[1].yaw, -90);
  // the leader is not stored: place 0 for him, the anchor (present) is an extra, squad 1 keeps his own place
  const c = R.CustomSlots(SAMPLE, [4, 0, 1], [0, 0], 0, C);
  assert.deepEqual(c.map((x) => [x.squad, x.fixed]), [[4, true], [0, false], [1, true]]);
  near(c[1].pos, [-250 - sp, 0]);
});

test('CustomSlots: present beyond 4 is cut; an empty shape or no one present gives []', () => {
  assert.deepEqual(R.CustomSlots(SAMPLE, [0, 1, 2, 3, 4], [0, 0], 0, C).map((x) => x.squad), [0, 1, 2, 3]);
  assert.deepEqual(R.CustomSlots({ members: [] }, [0, 1], [0, 0], 0, C), []);
  assert.deepEqual(R.CustomSlots(SAMPLE, [], [0, 0], 0, C), []);
});

// --- 17.3 BlockedRoles / 17.4 Resolve

test('BlockedRoles: the roles whose line leader -> slot passes a wall body (clearance), the leader never; no chain pairs', () => {
  const slots = R.CustomSlots(SAMPLE, [0, 1, 2, 3], [0, 0], 0, C);
  assert.deepEqual(R.BlockedRoles(walls.none, slots, [0, 0], C), []);
  assert.deepEqual(R.BlockedRoles(walls.left, slots, [0, 0], C), ['L'], 'the wall runs through the left slot');
  assert.deepEqual(R.BlockedRoles(walls.right, slots, [0, 0], C), ['P']);
  assert.deepEqual(R.BlockedRoles(walls.behind, slots, [0, 0], C), ['P', 'L', 'Z'], 'slot order');
  assert.deepEqual(R.BlockedRoles(walls.far_left, slots, [0, 0], C), [], 'a wall past every slot');
  // a wall between P and L that touches neither line to the leader: not a chain break here
  const between: Segment[] = [{ a: [-250, -100], b: [-250, 100], thickness: 20 }];
  assert.deepEqual(R.BlockedRoles(between, R.CustomSlots(SAMPLE, [0, 1, 2], [0, 0], 0, C), [0, 0], C), []);
});

const resolve = (w: Segment[], state: GuardState, step = 0, o: { ahead?: { pos: Vec2; yaw: number } | null; sight?: boolean; present?: number[] } = {}) =>
  R.Resolve(w, SAMPLE, o.present ?? [0, 1, 2, 3], [0, 0], 0, o.ahead ?? null, state, step, C, o.sight ?? true);

test('Resolve: whole -> custom at level 0; a wall through a slot, the lookahead pose or a man cut off -> the trail column (level 2, cause wall)', () => {
  assert.deepEqual(resolve(walls.none, GUARD_START), { requested: 'custom', level: 0, effective: 'custom', candidate: null, candidateDistance: 0, cause: null });
  const cut = { requested: 'custom', level: 2, effective: 'column', candidate: null, candidateDistance: 0, cause: 'wall' };
  assert.deepEqual(resolve(walls.left, GUARD_START), cut);
  assert.deepEqual(resolve(walls.none, GUARD_START, 0, { sight: false }), cut);
  // the shape is whole here but cut at the pose ahead (a wall across the lookahead)
  const entrance: Segment[] = [{ a: [300, -250], b: [3000, -250], thickness: 40 }, { a: [300, 250], b: [3000, 250], thickness: 40 }];
  assert.equal(resolve(entrance, GUARD_START).level, 0);
  assert.deepEqual(resolve(entrance, GUARD_START, 0, { ahead: { pos: [500, 0], yaw: 0 } }), cut);
});

test('Resolve: the hold - level 2 waits classify.hold_distance of path for 0; a new wall resets; a new request starts over', () => {
  const hold = C['classify.hold_distance'];
  const held: GuardState = { requested: 'custom', level: 2, effective: 'column', candidate: null, candidateDistance: 0, cause: 'wall' };
  let s = resolve(walls.none, held, 100);
  assert.deepEqual(s, { requested: 'custom', level: 2, effective: 'column', candidate: 0, candidateDistance: 100, cause: 'wall' });
  s = resolve(walls.none, s, hold - 100 - 1);
  assert.equal(s.level, 2); assert.equal(s.candidateDistance, hold - 1);
  s = resolve(walls.none, s, 1);
  assert.deepEqual(s, { requested: 'custom', level: 0, effective: 'custom', candidate: null, candidateDistance: 0, cause: null });
  // a candidate distance of another candidate does not count; a negative step is 0
  assert.equal(resolve(walls.none, { ...held, candidate: null, candidateDistance: 400 }, 100).candidateDistance, 100);
  assert.equal(resolve(walls.none, { ...held, candidate: 0, candidateDistance: 400 }, -50).candidateDistance, 400);
  // cut again while waiting: raw 2 >= level 2 -> the raw level at once, the wait is dropped
  assert.deepEqual(resolve(walls.left, { ...held, candidate: 0, candidateDistance: 450 }, 100), held);
  // another request in the state: the raw level now (level 0 at once, no wait)
  assert.equal(resolve(walls.none, { ...held, requested: 'column' }, 10).level, 0);
  // a sharp turn alone does not fold it, and there is no mirror level
  assert.equal([0, 1, 2].includes(resolve(walls.none, GUARD_START).level), true);
});

// --- 17.5 Look

test('Look: the player\'s look is kept (normalised) unless it points into a wall within look.wall_dist / look.wall_angle', () => {
  assert.equal(R.Look(walls.none, [0, 0], 370, 0, C), 10);
  assert.equal(R.Look(walls.near_left, [0, 0], 0, 0, C), 0, 'along the wall');
  assert.equal(R.Look(walls.near_left, [0, 0], -30, 0, C), -30, 'outside the wall angle (60 deg off the wall direction -90)');
  assert.equal(R.Look(walls.far_left, [0, 0], -90, 0, C), -90, 'the wall is beyond look.wall_dist');
  assert.equal(R.Look(walls.left, [0, 0], -90, 0, C), -90, 'the wall surface (280 cm) is beyond look.wall_dist');
  // into the wall: ClearLook's answer, which is a free direction
  const blocked = R.Look(walls.near_left, [0, 0], -90, 0, C);
  assert.equal(blocked, FormationSectorRules.ClearLook(walls.near_left, [0, 0], -90, 0, C, null));
  assert.ok(angleDiff(blocked, -90) >= C['look.wall_angle'] - 1e-6);
  // the held look reaches ClearLook
  assert.equal(R.Look(walls.near_left, [0, 0], -90, 0, C, -20), FormationSectorRules.ClearLook(walls.near_left, [0, 0], -90, 0, C, -20));
});

test('Look: no sliver / hold logic when the look is free - ClearLook would move a look in a narrow free arc, Look keeps it', () => {
  const sliver: Segment[] = [{ a: [-290, -558], b: [558, 290], thickness: 40 }, { a: [-278, 325], b: [-4, -427], thickness: 40 }];
  let kept = 0;
  for (const cur of [0, 60, 180, -60]) for (let y = -180; y < 180; y++) { // currentYaw 0: a turn to -103 sweeps a wall direction, ClearLook leaves it
    const look = R.Look(sliver, [0, 0], y, cur, C, null);
    const clear = FormationSectorRules.ClearLook(sliver, [0, 0], y, cur, C, null);
    if (angleDiff(look, clear) > 1e-6) { assert.equal(look, normYaw(y), `yaw ${y}`); kept++; }
  }
  assert.ok(kept > 0, 'the scene has free-but-narrow looks ClearLook would move');
  // a held look is ignored while the wanted one is free
  assert.equal(R.Look(sliver, [0, 0], 20, 20, C, 100), 20);
  assert.equal(R.Look(sliver, [0, 0], -103, 0, C, 100), -103);
});

// --- 17.6 placement

test('DragLook: a click (< 50 cm) looks along the default yaw, a longer drag along ghost -> cursor', () => {
  assert.equal(R.DragLook([100, 100], [100, 100], 37), 37);
  assert.equal(R.DragLook([100, 100], [149.9, 100], -135), -135);
  assert.equal(R.DragLook([100, 100], [150, 100], -135), 0, 'exactly CUSTOM_DRAG_MIN is a drag');
  assert.equal(R.DragLook([100, 100], [100, 250], 0), 90);
  assert.equal(R.DragLook([0, 0], [-300, 0], 0), 180);
  assert.equal(R.DragLook([0, 0], [0, 0], 190), -170, 'the default is normalised');
});

test('CanPlace: too_close within slot.min_spacing of another ghost, too_far past CUSTOM_MAX_RADIUS from the anchor, in that order', () => {
  const ghosts = [{ squad: 0, pos: [0, 0] as Vec2 }, { squad: 1, pos: [-250, 300] as Vec2 }, { squad: 2, pos: [-250, -300] as Vec2 }];
  assert.equal(R.CanPlace(ghosts, 0, 3, [-450, 0], C), 'ok', 'a new ghost is added');
  assert.equal(R.CanPlace(ghosts, 0, 3, [-250, 300 + 79], C), 'too_close');
  assert.equal(R.CanPlace(ghosts, 0, 3, [-250, 300 + 81], C), 'ok');
  assert.equal(R.CanPlace(ghosts, 0, 1, [-250, 330], C), 'ok', 'his own old place is not "another ghost"');
  assert.equal(R.CanPlace(ghosts, 0, 1, [1500, 0], C), 'ok', '15 m from the anchor (bug_05: the old 10 m reach is gone)');
  assert.equal(R.CanPlace(ghosts, 0, 1, [4000, 0], C), 'ok', 'exactly the radius');
  assert.equal(R.CanPlace(ghosts, 0, 1, [4000.1, 0], C), 'too_far');
  assert.equal(R.CanPlace(ghosts, 0, 1, [10, 10], C), 'too_close', 'next to the anchor');
  assert.equal(R.CanPlace(ghosts, 0, 1, [0, 20], C), 'too_close', 'too_close wins over a far ghost elsewhere');
  // the anchor moves: the others must stay within reach of his new place
  assert.equal(R.CanPlace(ghosts, 0, 0, [600, 0], C), 'ok');
  assert.equal(R.CanPlace(ghosts, 0, 0, [0, 1500], C), 'ok');
  assert.equal(R.CanPlace(ghosts, 0, 0, [0, 4500], C), 'too_far');
  assert.equal(R.CanPlace(ghosts, 0, 0, [-250, 250], C), 'too_close');
  // no anchor ghost -> no reach test
  assert.equal(R.CanPlace([{ squad: 1, pos: [0, 0] }], 0, 2, [5000, 5000], C), 'ok');
  assert.equal(R.CanPlace([], 0, 1, [5000, 5000], C), 'ok');
  assert.equal(R.CanPlace([], 0, 0, [5000, 5000], C), 'ok', 'the first ghost, alone');
  // the anchor placed later sees the others that are too far
  assert.equal(R.CanPlace([{ squad: 1, pos: [2000, 0] }], 0, 0, [0, 0], C), 'ok');
  assert.equal(R.CanPlace([{ squad: 1, pos: [5000, 0] }], 0, 0, [0, 0], C), 'too_far');
});

test('ShapeFrom: the anchor ghost is the frame; the anchor first (0, 0, 0), the others by squad, at most 4; rounded to 0.1', () => {
  // anchor squad 3 facing +Y (yaw 90) at (1000, 2000); squad 0 beside him
  const shape = R.ShapeFrom([
    { squad: 2, pos: [1000, 2000 + 250], yaw: 0 }, { squad: 3, pos: [1000, 2000], yaw: 90 }, { squad: 0, pos: [1000 + 300, 2000 - 100], yaw: 180 },
  ], 3);
  assert.deepEqual(shape.members.map((m) => m.squad), [3, 0, 2]);
  assert.deepEqual(shape.members[0], { squad: 3, fwd: 0, right: 0, look: 0 });
  // facing +Y: forward = +Y, right = -X. Squad 0 at (+300, -100) -> fwd -100, right -300; squad 2 at +250 along Y -> fwd 250
  assert.ok(Math.abs(shape.members[1].fwd + 100) < 0.05 && Math.abs(shape.members[1].right + 300) < 0.05);
  assert.ok(Math.abs(shape.members[2].fwd - 250) < 0.05 && Math.abs(shape.members[2].right) < 0.05);
  assert.equal(shape.members[1].look, 90); assert.equal(shape.members[2].look, -90);
  // the cut at CUSTOM_MAX_MEMBERS (anchor included) keeps the lowest squads; no anchor ghost -> empty
  const five = [0, 1, 2, 3, 4].map((squad) => ({ squad, pos: [-100 * squad, 0] as Vec2, yaw: 0 }));
  assert.deepEqual(R.ShapeFrom(five, 4).members.map((m) => m.squad), [4, 0, 1, 2]);
  assert.deepEqual(R.ShapeFrom(five, 9), { members: [] });
  // the look wraps across +-180
  const wrap = R.ShapeFrom([{ squad: 0, pos: [0, 0], yaw: 170 }, { squad: 1, pos: [0, 100], yaw: -170 }], 0);
  assert.equal(wrap.members[1].look, 20);
});

test('RoundShape: 0.1 cm / 0.1 deg, half away from zero, no -0, the look stays in (-180, 180]', () => {
  const r = R.RoundShape({ members: [{ squad: 0, fwd: 0.04, right: -0.04, look: -0.04 }, { squad: 1, fwd: 0.05, right: -0.05, look: 123.46 },
    { squad: 2, fwd: -312.449, right: 1000.06, look: -179.96 }] });
  assert.deepEqual(r.members.map((m) => [m.fwd, m.right, m.look]), [[0, 0, 0], [0.1, -0.1, 123.5], [-312.4, 1000.1, 180]]);
  assert.ok(r.members.every((m) => !Object.is(m.fwd, -0) && !Object.is(m.right, -0) && !Object.is(m.look, -0)));
  assert.deepEqual(R.RoundShape(SAMPLE), SAMPLE, 'already rounded');
});

// --- 17.7 the menu

const DZ = 40;
const centre = (k: number): Vec2 => [(k - 1) * 100, 280];

test('menu: three discs below the ring (100 / 280 / r 40), CustomItemAt = the first disc with hypot <= radius; MENU_ITEMS stays the 8-item ring', () => {
  assert.equal(MENU_ITEMS.length, 8);
  assert.ok(MENU_ITEMS.every((i) => !i.id.startsWith('custom')));
  for (let k = 0; k < 3; k++) {
    const [x, y] = centre(k);
    assert.equal(M.CustomItemAt(x, y), `custom_${k + 1}`);
    assert.equal(M.CustomItemAt(x + 40, y), `custom_${k + 1}`, 'on the rim');
    assert.equal(M.CustomItemAt(x, y - 40.1), null);
  }
  assert.equal(M.CustomItemAt(-100 + 39, 280), 'custom_1');
  assert.equal(M.CustomItemAt(-20, 280), 'custom_2');
  assert.equal(M.CustomItemAt(50, 280), null, 'between the discs');
  assert.equal(M.CustomItemAt(0, 0), null);
  // HitAt: the discs win, everything else is the ring (a point below the ring but outside the discs is still the 6 o'clock sector)
  assert.equal(M.HitAt(0, 280, DZ), 'custom_2');
  assert.equal(M.HitAt(50, 280, DZ), 'column');
  assert.equal(M.HitAt(0, 100, DZ), 'column');
  assert.equal(M.HitAt(0, 0, DZ), null);
  assert.equal(M.ItemAt(0, 280, DZ), 'column', 'ItemAt itself is unchanged');
});

test('menu: Move highlights the discs; Release on an empty slot -> edit (choice unchanged), on a filled one -> picked, edit asked -> edit', () => {
  let s: MenuState = M.Open({ ...MENU_START, choice: 'column' }).state;
  s = M.Move(s, 100, 280, DZ);
  assert.equal(s.highlighted, 'custom_3');
  let r = M.Release(s, 4);
  assert.equal(r.effect, 'edit'); assert.deepEqual(r.state, { open: false, choice: 'column', highlighted: null });
  r = M.Release(s, 4, [false, false, true]);
  assert.equal(r.effect, 'picked'); assert.equal(r.state.choice, 'custom_3');
  r = M.Release(s, 4, [true, true, true], true);
  assert.equal(r.effect, 'edit'); assert.equal(r.state.choice, 'column');
  r = M.Release(s, 4, [true, true, false]);
  assert.equal(r.effect, 'edit', 'only his own slot counts');
  // one operative: disabled whatever the slot holds
  r = M.Release(s, 1, [true, true, true]);
  assert.equal(r.effect, 'disabled'); assert.equal(r.state.choice, 'column');
  r = M.Release(s, 1, [false, false, false], true);
  assert.equal(r.effect, 'disabled');
  // edit and filled change nothing off the discs
  const ring = M.Move(M.Open(MENU_START).state, 0, 100, DZ);
  assert.deepEqual(M.Release(ring, 4, [true, true, true], true), M.Release(ring, 4));
  assert.equal(M.Release(M.Open(MENU_START).state, 4, [true, true, true], true).effect, 'auto');
  assert.equal(M.IsEnabled('custom_1', 1), false); assert.equal(M.IsEnabled('custom_2', 2), true);
});

test('menu: Resolve custom_k -> the custom formation with customSlot k (null for every other choice); too few members -> Auto', () => {
  for (const k of [1, 2, 3]) assert.deepEqual(M.Resolve(`custom_${k}` as MenuItemId, 3), { manualFormation: 'custom', allForward: false, customSlot: k });
  assert.deepEqual(M.Resolve('custom_2', 1), { manualFormation: null, allForward: false, customSlot: null });
  assert.deepEqual(M.Resolve('column', 4), { manualFormation: 'column', allForward: false, customSlot: null });
  assert.deepEqual(M.Resolve('diamond', 3), { manualFormation: 'wedge', allForward: false, customSlot: null });
  assert.deepEqual(M.Resolve('auto', 4), { manualFormation: null, allForward: false, customSlot: null });
});

test('labels: Custom 1..3 (English; empty slot; the HUD while folded), Своё 1..3 (Russian)', () => {
  assert.equal(menuLabelEn('custom_1', 4), 'Custom 1'); assert.equal(menuLabelEn('custom_3', 4, true), 'Custom 3 (empty)');
  assert.equal(menuLabel('custom_2', 4), 'Своё 2');
  assert.equal(guardHudText('custom', 'column', 2, 4, 'wall', 2), 'Custom 2 -> Column (wall)');
  assert.equal(guardHudText('custom', 'custom', 0, 4, null, 2), 'Custom 2');
});

// --- 17.8 the bot

test('bot: the custom formation walks the lab with CUSTOM_SAMPLE_SHAPE; checks 1 (wall stare), 5 (slots / spacing) and 7 (split) hold', () => {
  const layout = readLayout('sample_small');
  const { runs, summary } = runSeries({ layout, config: C, members: [2, 3, 4], seeds: 2, seedBase: 1, manual: 'custom_1', customShape: SAMPLE });
  assert.ok(runs.length > 0);
  for (const id of [1, 5, 7]) assert.equal(summary.checks.find((c) => c.id === id)!.fail, 0, `check ${id}`);
});
