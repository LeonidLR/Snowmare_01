// Training Ground / movement: v2.1 rule tests (FORMATION_CONTRACT.md 10) - the wall guard (the squad never splits), the bypass
// side with a known threat, the corner watch (slicing the pie). User decisions 2026-10-25 in docs/design/FORMATION_SECTORS.md.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { angleDiff, segmentBlocked, yawOf } from './geometry.ts';
import type { Segment, Vec2 } from './geometry.ts';
import { RunCollector } from './checks.ts';
import { configValues } from './config.ts';
import { readConfigFile, readLayout } from './cli/paths.ts';
import { runSeries } from './bot.ts';
import {
  CORNER_START, FormationBypassRules, FormationCornerRules, FormationGuardRules, FormationShapeRules, FormationTerrainRules,
  GUARD_START, wallSideOf,
} from './rules.ts';
import type { GuardState } from './rules.ts';
import type { Frame, MemberFrame } from './sim.ts';
import { SIM_DT } from './sim.ts';
import { validateLayout } from './layout.ts';

const C = configValues(readConfigFile());
const G = FormationGuardRules;
const hold = C['classify.hold_distance'];
// The user's case (lab lane B, 2026-10-25): a long wall 3 m to the LEFT of the leader, heading +X.
const wallLeft: Segment[] = [{ a: [-3000, -300], b: [3000, -300], thickness: 40 }];

test('guard: manual echelon LEFT along a wall on the left is cut (its tail behind the wall) -> the mirror echelon right', () => {
  assert.deepEqual(G.BlockedRoles(wallLeft, 'echelon_left', 4, [0, 0], 0, C), ['P', 'L', 'Z']);
  assert.deepEqual(G.BlockedRoles(wallLeft, 'echelon_right', 4, [0, 0], 0, C), []);
  const st = G.Resolve(wallLeft, 'echelon_left', 4, [0, 0], 0, null, GUARD_START, 0, C);
  assert.equal(st.requested, 'echelon_left', 'the request is never cleared');
  assert.equal(st.effective, 'echelon_right');
  assert.equal(st.level, 1);
});

test('guard: walls on both sides cut both echelons -> the trail column (level 2); a column is never cut further', () => {
  const corridor: Segment[] = [{ a: [-3000, -250], b: [3000, -250], thickness: 40 }, { a: [-3000, 250], b: [3000, 250], thickness: 40 }];
  const st = G.Resolve(corridor, 'echelon_left', 4, [0, 0], 0, null, GUARD_START, 0, C);
  assert.equal(st.effective, 'column');
  assert.equal(st.level, 2);
  assert.deepEqual(G.Effective(st), { formation: 'column', trail: true });
  assert.equal(G.Resolve(corridor, 'diamond', 4, [0, 0], 0, null, GUARD_START, 0, C).effective, 'column', 'any cut formation -> column');
  assert.equal(G.Resolve(corridor, 'wall', 4, [0, 0], 0, null, GUARD_START, 0, C).effective, 'wall', 'the wall layout is never guarded');
});

test('guard: the fallback comes at once, the request comes back only after hold_distance of whole path', () => {
  let st: GuardState = G.Resolve(wallLeft, 'echelon_left', 4, [0, 0], 0, null, GUARD_START, 0, C);
  assert.equal(st.level, 1);
  // The wall is gone: the echelon left is whole again, but it waits for the path hold (no flicker at a short gap).
  st = G.Resolve([], 'echelon_left', 4, [0, 0], 0, null, st, hold / 2, C);
  assert.equal(st.effective, 'echelon_right'); assert.equal(st.candidate, 0);
  st = G.Resolve([], 'echelon_left', 4, [0, 0], 0, null, st, 0, C);
  assert.equal(st.effective, 'echelon_right', 'standing still adds no path');
  st = G.Resolve([], 'echelon_left', 4, [0, 0], 0, null, st, hold / 2, C);
  assert.equal(st.effective, 'echelon_left', 'back by itself');
  assert.equal(st.level, 0);
  // A new request starts over at its own level.
  const other = G.Resolve(wallLeft, 'diamond', 4, [0, 0], 0, null, { ...st, level: 2, effective: 'column' }, 0, C);
  assert.equal(other.effective, 'diamond');
});

test('guard: the lookahead folds before a passage entrance (whole here, cut 5 m ahead)', () => {
  const entrance: Segment[] = [{ a: [300, -250], b: [3000, -250], thickness: 40 }, { a: [300, 250], b: [3000, 250], thickness: 40 }];
  const ahead = { pos: [C['guard.lookahead'], 0] as Vec2, yaw: 0 };
  assert.equal(G.Resolve(entrance, 'line', 4, [0, 0], 0, null, GUARD_START, 0, C).effective, 'line');
  assert.equal(G.Resolve(entrance, 'line', 4, [0, 0], 0, ahead, GUARD_START, 0, C).effective, 'column');
});

test('guard: chain neighbours - echelon in its order, column K-P-L-Z, diamond P-Z / L-Z, none for a pair', () => {
  assert.deepEqual(G.ChainPairs('echelon_right', 4), [['K', 'L'], ['L', 'P'], ['P', 'Z']]);
  assert.deepEqual(G.ChainPairs('column', 3), [['K', 'P'], ['P', 'L']]);
  assert.deepEqual(G.ChainPairs('diamond', 4), [['P', 'Z'], ['L', 'Z']]);
  assert.deepEqual(G.ChainPairs('hedgehog', 4), [['K', 'P'], ['P', 'Z'], ['Z', 'L'], ['L', 'K']]);
  assert.deepEqual(G.ChainPairs('line', 2), []);
});

test('trail column: straight trail = the straight column; round a corner the slots follow the path, the echelon order kept', () => {
  const straight = FormationShapeRules.TrailSlots([[-2000, 0], [0, 0]], 0, 4, 'column', C);
  const column = FormationShapeRules.Slots('column', 4, [0, 0], 0, C);
  for (let i = 0; i < 4; i++) { assert.equal(straight[i].role, column[i].role); assert.ok(Math.hypot(straight[i].pos[0] - column[i].pos[0], straight[i].pos[1] - column[i].pos[1]) < 1e-6); }
  // A right turn 4 m behind the leader (along +X, then down to him along -Y) round the end of a wall: the column bends with
  // the path - the tail is on the first leg and no two neighbours have the wall between them.
  const corner: Segment[] = [{ a: [-2000, 300], b: [-100, 300], thickness: 40 }];
  const bent = FormationShapeRules.TrailSlots([[-2000, 400], [0, 400], [0, 0]], -90, 4, 'column', C);
  const z = bent.find((s) => s.role === 'Z')!;
  assert.ok(Math.abs(z.pos[1] - 400) < 1e-6 && z.pos[0] < 0, `Z on the first leg (${z.pos})`);
  for (const [a, b] of FormationGuardRules.ChainPairs('column', 4)) {
    const A = bent.find((s) => s.role === a)!, B = bent.find((s) => s.role === b)!;
    assert.ok(!segmentBlocked(A.pos, B.pos, corner, 0), `${a}-${b} cut`);
  }
  const fromEchelon = FormationShapeRules.TrailSlots([[-2000, 0], [0, 0]], 0, 4, 'echelon_right', C);
  assert.ok(fromEchelon.find((s) => s.role === 'L')!.pos[0] > fromEchelon.find((s) => s.role === 'P')!.pos[0], 'echelon right folds L second');
});

test('SideWidth: a wall END beside a man counts (the side rays slip past it); open both sides -> null', () => {
  const end: Segment[] = [{ a: [20, 110], b: [20, 1500], thickness: 40 }];
  assert.ok(FormationTerrainRules.SideWidth(end, [0, 0], 0, C)! < C['column.narrow_width']);
  assert.equal(FormationTerrainRules.SideWidth([], [0, 0], 0, C), null);
});

test('bypass: wall side convention (UE: +Y = right) and the shortest side without a threat', () => {
  const w: Segment = { a: [500, -100], b: [1500, 400], thickness: 40 };
  assert.equal(wallSideOf([[0, 0], [2000, 0]], { a: [500, 300], b: [1500, 300] }), 'right');
  assert.equal(wallSideOf([[0, 0], [2000, 0]], { a: [500, -300], b: [1500, -300] }), 'left');
  const free = FormationBypassRules.ChooseSide(w, [0, 0], [2000, 0], [], C);
  assert.equal(free.reason, 'shortest'); assert.equal(free.wallOn, free.shortest); assert.equal(free.detour, 0);
  // Round the near end (-Y, the closer one) = the squad passes left of it... the wall is on its right.
  assert.equal(free.shortest, 'right');
  for (let i = 1; i < free.path.length; i++) assert.ok(!segmentBlocked(free.path[i - 1], free.path[i], [w], 0));
});

test('bypass: a known threat -> the side the wall hides it from, if the detour <= bypass.max_detour; forgotten threats ignored', () => {
  const w: Segment = { a: [500, -100], b: [1900, 200], thickness: 40 };
  const left = FormationBypassRules.ChooseSide(w, [0, 0], [2600, 0], [{ pos: [1200, -900], ageS: 0 }], C);
  assert.equal(left.wallOn, 'left'); assert.equal(left.reason, 'shielded'); assert.ok(left.detour <= C['bypass.max_detour']);
  const old = FormationBypassRules.ChooseSide(w, [0, 0], [2600, 0], [{ pos: [1200, -900], ageS: C['bypass.threat_memory_s'] + 1 }], C);
  assert.equal(old.reason, 'shortest');
  // A lopsided wall square to the route: the shielding side is ~70 % longer -> too long, the shortest side stays.
  const square: Segment = { a: [900, -200], b: [900, 1100], thickness: 40 };
  const far = FormationBypassRules.ChooseSide(square, [0, 0], [1700, 0], [{ pos: [1300, -900], ageS: 0 }], C);
  assert.equal(far.reason, 'too_long'); assert.equal(far.wallOn, far.shortest);
});

test('corner: nearing a free wall end on the left, the closest member looks at the corner; past it, behind the wall', () => {
  const walls: Segment[] = [{ a: [-3000, -300], b: [200, -300], thickness: 40 }];
  const members = [{ role: 'K' as const, pos: [0, 0] as Vec2 }, { role: 'L' as const, pos: [-280, -200] as Vec2 }, { role: 'P' as const, pos: [-280, 200] as Vec2 }];
  const a = FormationCornerRules.Watch(walls, [0, 0], 0, members, CORNER_START, C);
  assert.equal(a.state.left?.role, 'K', 'the leader is the closest to the end');
  assert.equal(a.state.right, null);
  const look = a.looks[0];
  assert.ok(angleDiff(look.yaw, yawOf([200 + 20 + 50, -300])) < 1, 'at the corner, grazing its end');
  // The leader past the end: he looks behind the wall's far face (the area it hid), until hold_dist past.
  const past = [{ ...members[0], pos: [400, 0] as Vec2 }, members[1], members[2]];
  const b = FormationCornerRules.Watch(walls, [400, 0], 0, past, a.state, C);
  assert.equal(b.looks[0].role, 'K');
  assert.ok(b.looks[0].yaw < -90, `looks back-left behind the wall (${b.looks[0].yaw})`);
  const gone = FormationCornerRules.Watch(walls, [200 + C['corner.hold_dist'] + 10, 0], 0, past, b.state, C);
  assert.deepEqual(gone.state, CORNER_START);
});

test('corner: a corridor exit has two corners - two different men; a room corner (joint) is not a free end', () => {
  const exit: Segment[] = [{ a: [-3000, -250], b: [100, -250], thickness: 40 }, { a: [-3000, 250], b: [250, 250], thickness: 40 }];
  const members = [{ role: 'K' as const, pos: [0, 0] as Vec2 }, { role: 'P' as const, pos: [-150, 60] as Vec2 }, { role: 'L' as const, pos: [-300, -60] as Vec2 }];
  const w = FormationCornerRules.Watch(exit, [0, 0], 0, members, CORNER_START, C);
  assert.ok(w.state.left && w.state.right);
  assert.notEqual(w.state.left!.role, w.state.right!.role);
  const room: Segment[] = [{ a: [-3000, -300], b: [200, -300], thickness: 40 }, { a: [200, -300], b: [200, -1500], thickness: 40 }];
  assert.deepEqual(FormationCornerRules.Watch(room, [0, 0], 0, members, CORNER_START, C).state, CORNER_START);
});

test('check 7: a wall between the leader and a member for >= 0.3 s is a split; shorter is not', () => {
  const layout = readLayout('sample_small');
  const route = layout.routes[0];
  // A wall of the sample layout between the leader and Z's position (synthetic frames).
  const w = layout.walls[0];
  const mid: Vec2 = [(w.a[0] + w.b[0]) / 2, (w.a[1] + w.b[1]) / 2];
  const dir: Vec2 = [w.b[0] - w.a[0], w.b[1] - w.a[1]], L = Math.hypot(dir[0], dir[1]);
  const n: Vec2 = [-dir[1] / L, dir[0] / L];
  const K: Vec2 = [mid[0] + n[0] * 300, mid[1] + n[1] * 300], Z: Vec2 = [mid[0] - n[0] * 300, mid[1] - n[1] * 300];
  const member = (role: 'K' | 'Z', pos: Vec2): MemberFrame => ({ role, pos, yaw: 0, slot: role === 'K' ? pos : K, slotFallback: false, slotInvalid: false, backwards: false, moveYaw: null, cornerWatch: false });
  const frame = (t: number): Frame => ({ t, mode: 'halt', formation: 'diamond', requested: 'diamond', guardLevel: 0, terrain: 'open', heading: 0, leaderDistance: 0, members: [member('K', K), member('Z', Z)], bypasses: [] });
  const run = (seconds: number) => {
    const col = new RunCollector(layout, route, 2, 1, C);
    for (let t = 0; t < seconds; t += SIM_DT) col.onFrame(frame(t));
    return col.finish().split.events;
  };
  assert.equal(run(0.2), 0);
  assert.equal(run(0.5), 1);
});

test('lab: the user\'s case (manual echelon left, lane B) and every lane in auto never split; bypass sides as expected', () => {
  const lab = readLayout('formation_lab_01');
  const manual = runSeries({ layout: lab, config: C, members: [4], seeds: 1, seedBase: 1, routes: ['R_B_walls'], manual: 'echelon_left' }).summary;
  assert.equal(manual.checks.find((c) => c.id === 7)!.fail, 0, manual.checks.find((c) => c.id === 7)!.worst);
  const g = runSeries({ layout: lab, config: C, members: [2, 4], seeds: 1, seedBase: 1, routes: ['R_G_free', 'R_G_threat_right', 'R_G_threat_left', 'R_G_far'] }).summary;
  const byId = Object.fromEntries(g.checks.map((c) => [c.id, c]));
  assert.equal(byId[8].fail, 0, byId[8].worst);
  assert.ok(byId[8].pass === 8);
  assert.equal(byId[7].fail, 0, byId[7].worst);
  assert.equal(byId[9].fail, 0, byId[9].worst);
});

test('layout validation: threats and expect_bypass', () => {
  const lab = readLayout('formation_lab_01');
  assert.deepEqual(validateLayout(lab), []);
  const bad = structuredClone(lab);
  bad.routes[0].threats = [{ pos: [0, 0], since_s: -1 }];
  bad.routes[0].expect_bypass = [{ wall: 'NOPE', wall_on: 'up' as 'left' }];
  const e = validateLayout(bad).join('\n');
  assert.match(e, /since_s/); assert.match(e, /unknown wall "NOPE"/); assert.match(e, /wall_on must be left \| right/);
});

test('in-game text is English: menu labels and the guard HUD line ("Echelon left -> Column (wall)")', async () => {
  const { guardHudText, menuLabelEn } = await import('./labels.ts');
  const { MENU_ITEMS } = await import('./menu.ts');
  for (const n of [2, 3, 4]) for (const id of ['auto', ...MENU_ITEMS.map((i) => i.id)] as const) assert.match(menuLabelEn(id, n), /^[A-Za-z ]+$/);
  assert.equal(guardHudText('echelon_left', 'column', 2, 4), 'Echelon left -> Column (wall)');
  assert.equal(guardHudText('echelon_left', 'echelon_right', 1, 4), 'Echelon left -> Echelon right (wall)');
  assert.equal(guardHudText('diamond', 'diamond', 0, 4), 'Diamond');
  assert.equal(guardHudText('wedge', 'wedge', 0, 2), 'Pair');
});
