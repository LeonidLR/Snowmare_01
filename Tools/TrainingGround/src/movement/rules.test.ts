// Training Ground / movement: pure rule tests (node --test; `npm test`). The user's decisions in
// docs/design/FORMATION_SECTORS.md are the expectations; conventions per FORMATION_CONTRACT.md (UE yaw, right = +90).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { configValues, validateConfigFile, withValues } from './config.ts';
import type { FormationConfigFile } from './config.ts';
import { angleDiff, clockToYaw, dirOf, dist, localToWorld, normYaw, yawOf } from './geometry.ts';
import type { Segment, Vec2 } from './geometry.ts';
import { validateLayout } from './layout.ts';
import { FormationSectorRules, FormationShapeRules, FormationTerrainRules, FormationWallRules, rawClass, wallRoom } from './rules.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const CONFIG_FILE: FormationConfigFile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/FormationLab/formation_config.json'), 'utf8'));
const C = configValues(CONFIG_FILE);
const near = (a: number, b: number, eps = 1e-6) => assert.ok(Math.abs(a - b) <= eps, `${a} != ${b}`);
const nearV = (a: Vec2, b: Vec2, eps = 1e-6) => { near(a[0], b[0], eps); near(a[1], b[1], eps); };

test('conventions: UE yaw, 3 o\'clock is heading + 90 (right = +Y)', () => {
  near(clockToYaw(0, 3), 90);
  near(clockToYaw(0, 9), -90);
  nearV(localToWorld([0, 0], 0, [0, 100]), [0, 100]);
  nearV(localToWorld([0, 0], 90, [100, 0]), [0, 100]);
  nearV(localToWorld([0, 0], 90, [0, 100]), [-100, 0]);
  near(yawOf([0, 1]), 90);
  near(normYaw(270), -90);
});

test('config file: valid, ranges hold, withValues clamps and reports', () => {
  assert.deepEqual(validateConfigFile(CONFIG_FILE), []);
  const { file, errors } = withValues(CONFIG_FILE, { 'pace.patrol': 5, 'nope.key': 1 });
  assert.equal(file.params['pace.patrol'].value, CONFIG_FILE.params['pace.patrol'].max);
  assert.equal(errors.length, 2);
  assert.equal(CONFIG_FILE.params['pace.patrol'].value, 0.8, 'input not mutated');
});

test('Slots: diamond (4) - side men 2.6 m right / left 2.8 m back, rear 5.6 m straight back', () => {
  const s = FormationShapeRules.Slots('diamond', 4, [1000, 500], 0, C);
  assert.deepEqual(s.map((x) => x.role), ['K', 'P', 'L', 'Z']);
  nearV(s[0].pos, [1000, 500]);
  nearV(s[1].pos, [720, 760]);
  nearV(s[2].pos, [720, 240]);
  nearV(s[3].pos, [440, 500]);
  // Heading +Y (yaw 90): right is -X.
  const r = FormationShapeRules.Slots('diamond', 4, [0, 0], 90, C);
  nearV(r[1].pos, [-260, -280]);
});

test('Slots: wedge = first three of the diamond; pair partner behind the right shoulder; wall -> []', () => {
  assert.deepEqual(FormationShapeRules.Slots('wedge', 3, [0, 0], 0, C).map((x) => x.role), ['K', 'P', 'L']);
  const pair = FormationShapeRules.Slots('diamond', 2, [0, 0], 0, C);
  assert.deepEqual(pair.map((x) => x.role), ['K', 'Z']);
  nearV(pair[1].pos, [-220, 180]);
  assert.deepEqual(FormationShapeRules.Slots('wall', 4, [0, 0], 0, C), []);
});

test('Slots: echelon right puts the LEFT man second (minimal travel), echelon left the RIGHT man', () => {
  const r = FormationShapeRules.Slots('echelon_right', 4, [0, 0], 0, C);
  const byRole = Object.fromEntries(r.map((x) => [x.role, x.pos]));
  nearV(byRole.L, [-180, 180]);
  nearV(byRole.P, [-360, 360]);
  nearV(byRole.Z, [-540, 540]);
  const l = Object.fromEntries(FormationShapeRules.Slots('echelon_left', 4, [0, 0], 0, C).map((x) => [x.role, x.pos]));
  nearV(l.P, [-180, -180]);
  nearV(l.L, [-360, -360]);
});

test('Slots: column herringbone staggers right / left / centre', () => {
  const s = Object.fromEntries(FormationShapeRules.Slots('column', 4, [0, 0], 0, C).map((x) => [x.role, x.pos]));
  nearV(s.P, [-180, 60]);
  nearV(s.L, [-330, -60]);
  nearV(s.Z, [-480, 0]);
});

const sweep = (role: 'K' | 'P' | 'L' | 'Z', n: number, heading = 0) => {
  const ys: number[] = [];
  for (let t = 0; t < 12; t += 0.01) ys.push(normYaw(FormationSectorRules.LookYaw(role, 'diamond', 'walk', n, t, heading, null, C) - heading));
  return ys;
};

test('LookYaw patrol: P scans 12 -> 3, L 9 -> 12 (mirror), K light search, Z looks at 6', () => {
  const p = sweep('P', 4), l = sweep('L', 4), k = sweep('K', 4), z = sweep('Z', 4);
  near(Math.min(...p), 0, 0.5); near(Math.max(...p), 90, 0.5);
  near(Math.min(...l), -90, 0.5); near(Math.max(...l), 0, 0.5);
  assert.ok(Math.max(...k.map(Math.abs)) <= C['scan.amp_lead'] + 1e-9);
  assert.ok(z.every((v) => Math.abs(Math.abs(v) - 180) < 1e-9));
  // Mirror (counter-phase): when P is out at 3 o'clock, L is out at 9.
  const t = C['scan.period'] / 4;
  near(FormationSectorRules.LookYaw('P', 'diamond', 'walk', 4, t, 0, null, C), 90, 1e-6);
  near(FormationSectorRules.LookYaw('L', 'diamond', 'walk', 4, t, 0, null, C), -90, 1e-6);
});

test('LookYaw: pair partner sweeps 3 -> 6 -> 9 (user decision), through 6 never through 12', () => {
  const z = sweep('Z', 2).map((v) => (v < 0 ? v + 360 : v));
  near(Math.min(...z), 90, 0.5);
  near(Math.max(...z), 270, 0.5);
  assert.ok(z.every((v) => v >= 90 - 1e-6 && v <= 270 + 1e-6));
});

test('LookYaw: run = everyone on the heading; halt = sector centres 12 / 2 / 10 / 6', () => {
  for (const r of ['K', 'P', 'L', 'Z'] as const) near(FormationSectorRules.LookYaw(r, 'diamond', 'run', 4, 1.7, 30, null, C), 30);
  near(FormationSectorRules.LookYaw('P', 'diamond', 'halt', 4, 1.7, 0, null, C), 60);
  near(FormationSectorRules.LookYaw('L', 'diamond', 'halt', 4, 1.7, 0, null, C), -60);
  near(Math.abs(FormationSectorRules.LookYaw('Z', 'diamond', 'halt', 4, 1.7, 0, null, C)), 180);
});

test('LookYaw: echelon right - every gun on the open right side, the last man covers the rear-right (150)', () => {
  const ys = (['K', 'L', 'P', 'Z'] as const).map((r) => FormationSectorRules.LookYaw(r, 'echelon_right', 'walk', 4, 0, 0, null, C));
  assert.deepEqual(ys, [10, 45, 80, 150]); // user Q2 = A: the rear sector 150 (5-7 o'clock watched)
  const left = (['K', 'P', 'L', 'Z'] as const).map((r) => FormationSectorRules.LookYaw(r, 'echelon_left', 'walk', 4, 0, 0, null, C));
  assert.deepEqual(left, [-10, -45, -80, -150]);
});

// A wall along X at y = 200 (thickness 40), the squad on the y < 200 side: outward normal = -Y (yaw -90).
const WALL: Segment = { a: [-1000, 200], b: [1000, 200], thickness: 40 };

test('WallLayout (3): nobody looks into the wall; L 8, K 6 (away), P 4', () => {
  const s = FormationWallRules.Layout(WALL, [0, 0], 3, 1000, 1000, C);
  const byRole = Object.fromEntries(s.map((x) => [x.role, x]));
  nearV(byRole.K.pos, [0, 200 - 20 - 45]);
  near(byRole.K.yaw, -90);
  // Facing the wall (+Y) the right hand is -X: P stands at -X, L at +X.
  assert.ok(byRole.P.pos[0] < 0 && byRole.L.pos[0] > 0);
  near(byRole.P.yaw, normYaw(-90 - 60));
  near(byRole.L.yaw, normYaw(-90 + 60));
  for (const m of s) assert.ok(angleDiff(m.yaw, -90) < 90, `${m.role} looks into the wall`);
  // In clock terms relative to "facing the wall" (12 = +Y): L 8, K 6, P 4.
  const clock = (yaw: number) => ((normYaw(yaw - 90) + 360) % 360) / 30;
  near(clock(byRole.L.yaw), 8); near(clock(byRole.K.yaw), 6); near(clock(byRole.P.yaw), 4);
});

test('WallLayout (4): the 4th at the roomier end, his neighbour turns to the centre', () => {
  const right = Object.fromEntries(FormationWallRules.Layout(WALL, [0, 0], 4, 100, 900, C).map((x) => [x.role, x]));
  near(right.Z.pos[0], -200); // right hand = -X, two spacings out
  near(right.Z.yaw, normYaw(-90 - 75));
  near(right.P.yaw, normYaw(-90 - 30));
  near(right.L.yaw, normYaw(-90 + 60));
  const left = Object.fromEntries(FormationWallRules.Layout(WALL, [0, 0], 4, 900, 100, C).map((x) => [x.role, x]));
  near(left.Z.pos[0], 200);
  near(left.Z.yaw, normYaw(-90 + 75));
  near(left.L.yaw, normYaw(-90 + 30));
  near(left.P.yaw, normYaw(-90 - 60));
  for (const m of [...Object.values(right), ...Object.values(left)]) assert.ok(angleDiff(m.yaw, -90) < 90);
  const room = wallRoom(WALL, [300, 0]);
  near(room.roomRight, 1300); near(room.roomLeft, 700);
});

test('Classify: open, wall on one side -> echelon to the open side, corridor -> column, at_wall', () => {
  const none: Segment[] = [];
  assert.equal(rawClass(none, [0, 0], 0, null, C), 'open');
  const leftWall: Segment[] = [{ a: [-2000, -300], b: [2000, -300], thickness: 40 }];
  assert.equal(rawClass(leftWall, [0, 0], 0, null, C), 'wall_left');
  assert.equal(FormationTerrainRules.FormationFor('wall_left', 4, false), 'echelon_right');
  const rightWall: Segment[] = [{ a: [-2000, 300], b: [2000, 300], thickness: 40 }];
  assert.equal(rawClass(rightWall, [0, 0], 0, null, C), 'wall_right');
  assert.equal(FormationTerrainRules.FormationFor('wall_right', 3, false), 'echelon_left');
  assert.equal(rawClass([...leftWall, ...rightWall], [0, 0], 0, null, C), 'corridor');
  assert.equal(FormationTerrainRules.FormationFor('corridor', 4, false), 'column');
  const wide: Segment[] = [{ a: [-2000, -380], b: [2000, -380], thickness: 40 }, { a: [-2000, 380], b: [2000, 380], thickness: 40 }];
  assert.equal(rawClass(wide, [0, 0], 0, null, C), 'corridor', 'v1.2 (user Q3 = A): a 7.6 m street is a passage too -> column (herringbone)');
  const ahead: Segment[] = [{ a: [120, -500], b: [120, 500], thickness: 20 }];
  assert.equal(rawClass(ahead, [0, 0], 0, [0, 0], C), 'at_wall', 'arriving');
  assert.equal(rawClass(ahead, [0, 0], 0, null, C), 'open', 'v1.2: no destination = walking past - never at_wall');
  assert.equal(FormationTerrainRules.FormationFor('open', 4, false), 'diamond');
  assert.equal(FormationTerrainRules.FormationFor('open', 3, false), 'wedge');
});

test('Classify: a 2 m gap in a wall is bridged by the slanted probes; hysteresis holds a class for hold_time', () => {
  const gapped: Segment[] = [{ a: [-2000, -300], b: [-100, -300], thickness: 40 }, { a: [100, -300], b: [2000, -300], thickness: 40 }];
  assert.equal(rawClass(gapped, [0, 0], 0, null, C), 'wall_left');
  const hold = C['classify.hold_time'];
  assert.equal(FormationTerrainRules.Classify([], [0, 0], 0, null, 'wall_left', hold * 0.5, C), 'wall_left');
  assert.equal(FormationTerrainRules.Classify([], [0, 0], 0, null, 'wall_left', hold, C), 'open');
  assert.equal(FormationTerrainRules.Classify([], [0, 0], 0, null, null, 0, C), 'open');
});

test('layout validation: the sample layout is valid; broken files name the element', () => {
  const sample = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/FormationLab/sample_small.json'), 'utf8'));
  assert.deepEqual(validateLayout(sample), []);
  const bad = structuredClone(sample);
  bad.walls[0].thickness = 0;
  bad.routes[0].expect = [{ zone: 'NOPE', formation: 'diamond' }];
  bad.walls[1].id = bad.walls[0].id;
  const errors = validateLayout(bad);
  assert.ok(errors.some((e) => e.includes('thickness')));
  assert.ok(errors.some((e) => e.includes('unknown zone "NOPE"')));
  assert.ok(errors.some((e) => e.includes('duplicate id')));
  assert.equal(validateLayout({}).length > 3, true);
});

test('geometry sanity: dirOf / dist', () => {
  nearV(dirOf(90), [0, 1]);
  near(dist([0, 0], [3, 4]), 5);
});

test('v1.1 (Q3): a dead end ahead does not read as side walls; walls across the heading are ignored by the side probes', async () => {
  const { isAcrossHeading } = await import('./rules.ts');
  const deadEnd: Segment[] = [{ a: [300, -2000], b: [300, 2000], thickness: 40 }];
  assert.equal(rawClass(deadEnd, [0, 0], 0, null, C), 'open', 'v1 said corridor here (both 60 deg rays hit the wall ahead)');
  assert.equal(rawClass([...deadEnd, { a: [-2000, -300], b: [250, -300], thickness: 40 }], [0, 0], 0, null, C), 'wall_left');
  assert.equal(rawClass([{ a: [120, -500], b: [120, 500], thickness: 20 }], [0, 0], 0, [0, 0], C), 'at_wall', 'at_wall still first (arriving)');
  const band = C['classify.side_ignore_perp_deg'];
  assert.equal(isAcrossHeading({ a: [0, -100], b: [0, 100] }, 0, band), true);
  assert.equal(isAcrossHeading({ a: [-100, 0], b: [100, 0] }, 0, band), false);
  assert.equal(isAcrossHeading({ a: [0, 0], b: [Math.cos(Math.PI / 180 * 65), Math.sin(Math.PI / 180 * 65)] }, 0, band), true, '65 deg off = 25 off perpendicular');
  assert.equal(isAcrossHeading({ a: [0, 0], b: [Math.cos(Math.PI / 180 * 55), Math.sin(Math.PI / 180 * 55)] }, 0, band), false, '55 deg off = 35 off perpendicular');
  assert.equal(isAcrossHeading({ a: [0, -100], b: [0, 100] }, 90, band), false, 'same wall, heading along it');
});

test('v1.1 (user Q1 = A): a narrow column looks forward over the shoulder / back at a slant; the herringbone only when wide', async () => {
  const { FormationTerrainRules: T } = await import('./rules.ts');
  const look = (role: 'K' | 'P' | 'L' | 'Z', width: number | null) => FormationSectorRules.LookYaw(role, 'column', 'walk', 4, 1, 0, width, C);
  assert.deepEqual([look('K', 460), look('P', 460), look('L', 460), Math.abs(look('Z', 460))], [0, 15, -165, 180]);
  assert.deepEqual([look('P', 760), look('L', 760)], [70, -70], 'wider than column.narrow_width: herringbone');
  assert.deepEqual([look('P', null), look('L', null)], [70, -70], 'no passage (a manual column in the open): herringbone');
  const corridor: Segment[] = [{ a: [-2000, -250], b: [2000, -250], thickness: 40 }, { a: [-2000, 250], b: [2000, 250], thickness: 40 }];
  near(T.PassageWidth(corridor, [0, 0], 0, C)!, 460);
  assert.equal(T.PassageWidth([corridor[0]], [0, 0], 0, C), null);
  assert.equal(T.PassageWidth([], [0, 0], 0, C), null);
});

test('v1.2: leader search 11-1 o\'clock (+-30); "All forward" (mode forward) = everyone on 12 in any formation', () => {
  assert.equal(C['scan.amp_lead'], 30);
  const k = sweep('K', 4);
  near(Math.max(...k), 30, 0.5); near(Math.min(...k), -30, 0.5);
  for (const f of ['diamond', 'column', 'echelon_right', 'line', 'hedgehog'] as const) {
    for (const r of ['K', 'P', 'L', 'Z'] as const) near(FormationSectorRules.LookYaw(r, f, 'forward', 4, 1.3, 40, 460, C), 40);
  }
});

test('v1.2: at_wall only on arrival - walking past / up to a wall is not the wall layout; arriving within arrive_dist is, at once', () => {
  const ahead: Segment[] = [{ a: [120, -500], b: [120, 500], thickness: 20 }];
  const T = FormationTerrainRules;
  assert.equal(T.Classify(ahead, [0, 0], 0, [1000, 0], 'open', 5, C), 'open', 'destination far away: just passing');
  assert.equal(T.Classify(ahead, [0, 0], 0, [C['classify.arrive_dist'] - 1, 0], 'open', 0.1, C), 'at_wall', 'arriving: no hold for the event');
  assert.equal(T.Classify(ahead, [0, 0], 0, [C['classify.arrive_dist'] + 1, 0], 'open', 5, C), 'open');
});

test('v1.2: hedgehog round the heat source - centre = the fire, everyone on the ring, back to it, looking out', () => {
  const fire: Vec2 = [500, -300], r = C['hedgehog.radius'];
  for (const n of [2, 3, 4]) {
    const s = FormationShapeRules.HedgehogSlots(fire, n, 30, C);
    assert.equal(s.length, n);
    for (const m of s) {
      near(dist(m.pos, fire), r, 1e-6);
      near(angleDiff(m.yaw, yawOf([m.pos[0] - fire[0], m.pos[1] - fire[1]])), 0, 1e-6); // facing away from the centre
    }
    near(s[0].yaw, 30);
  }
});

test('v1.2 (documents): a column of THREE - the wide herringbone keeps the triad rear gap; the narrow column covers it with L', () => {
  const fov = C['sight.fov'];
  const watchesRear = (width: number | null) => (['K', 'P', 'L'] as const)
    .some((r) => angleDiff(FormationSectorRules.LookYaw(r, 'column', 'walk', 3, 0, 0, width, C), 180) <= fov / 2 - 30);
  assert.equal(watchesRear(null), false, 'wide column of three: 5-7 o\'clock open (triad gap rule)');
  assert.equal(watchesRear(460), true, 'narrow column of three: L back at a slant (-165, user Q1 = A) covers 5-7 o\'clock');
});

test('BreakRoomTie (as the C++ caller): a tie goes to the side with less travel; otherwise unchanged', () => {
  assert.deepEqual(FormationWallRules.BreakRoomTie(500, 500, 100, 300), { roomLeft: 501, roomRight: 500 });
  assert.deepEqual(FormationWallRules.BreakRoomTie(500, 500, 300, 100), { roomLeft: 500, roomRight: 500 });
  assert.deepEqual(FormationWallRules.BreakRoomTie(400, 500, 100, 300), { roomLeft: 400, roomRight: 500 });
});

test('v1.2 (user Q3 = A): any passage between two walls is a column - narrow sectors below column.narrow_width, herringbone above', () => {
  const street = (half: number): Segment[] => [{ a: [-2000, -half], b: [2000, -half], thickness: 40 }, { a: [-2000, half], b: [2000, half], thickness: 40 }];
  for (const half of [250, 380, 410]) assert.equal(rawClass(street(half), [0, 0], 0, null, C), 'corridor', `${2 * half} cm`);
  assert.equal(rawClass(street(450), [0, 0], 0, null, C), 'open', 'walls further than probe_side on both sides: open ground');
  const width = FormationTerrainRules.PassageWidth(street(380), [0, 0], 0, C)!;
  assert.deepEqual([FormationSectorRules.LookYaw('P', 'column', 'walk', 4, 0, 0, width, C), FormationSectorRules.LookYaw('L', 'column', 'walk', 4, 0, 0, width, C)], [70, -70]);
  assert.ok(!('classify.corridor_width' in C), 'the old width limit is gone from the config');
});

test('v1.2 (user Q4 = A): the blizzard tight diamond on OPEN ground in a blizzard only; walls / passages keep their rules', () => {
  const F = FormationTerrainRules.FormationFor;
  assert.equal(F('open', 4, true), 'blizzard');
  assert.equal(F('open', 3, true), 'blizzard');
  assert.equal(F('open', 4, false), 'diamond');
  for (const t of ['wall_left', 'wall_right', 'corridor', 'at_wall'] as const) assert.equal(F(t, 4, true), F(t, 4, false));
});

