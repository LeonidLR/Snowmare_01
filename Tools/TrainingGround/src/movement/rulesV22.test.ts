// Training Ground / movement: v2.2 tests - the triad K, P, Z and the general coverage allocator (FORMATION_CONTRACT.md 11):
// the rear guard, the open arc split, fixed looks, the static allocation round a standing group. User decisions 2026-10-25.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { angleDiff, normYaw } from './geometry.ts';
import type { Segment, Vec2 } from './geometry.ts';
import { configValues } from './config.ts';
import { readConfigFile, readLayout } from './cli/paths.ts';
import { runSeries } from './bot.ts';
import type { CoverMember, Role } from './rules.ts';
import { FormationCoverRules, FormationShapeRules, FormationWallRules } from './rules.ts';
import type { Formation } from './layout.ts';

const C = configValues(readConfigFile());
const FOV = C['sight.fov'];
const near = (a: number, b: number, eps = 1e-6) => assert.ok(Math.abs(a - b) <= eps, `${a} != ${b}`);
const A = FormationCoverRules.Allocate;
const placesOf = (f: Formation, n: number): CoverMember[] =>
  FormationShapeRules.Slots(f, n, [0, 0], 0, C).map((s) => ({ role: s.role, pos: s.pos, back: -s.pos[0] }));
const yawOf = (r: ReturnType<typeof A>, role: Role) => r!.looks.find((l) => l.role === role)!.yaw;

/** Largest arc (deg, 1 deg steps) no cone covers and no wall blocks (walls within look.wall_dist, +-45 deg) - check 10's measure. */
function largestGap(walls: readonly Segment[], members: { pos: Vec2; yaw: number }[]): number {
  const blocked: number[] = [];
  for (const m of members) for (const w of walls) {
    const ab: Vec2 = [w.b[0] - w.a[0], w.b[1] - w.a[1]], L2 = ab[0] ** 2 + ab[1] ** 2;
    const k = Math.max(0, Math.min(1, ((m.pos[0] - w.a[0]) * ab[0] + (m.pos[1] - w.a[1]) * ab[1]) / L2));
    const q: Vec2 = [w.a[0] + ab[0] * k, w.a[1] + ab[1] * k], d = Math.hypot(q[0] - m.pos[0], q[1] - m.pos[1]);
    if (d - (w.thickness ?? 0) / 2 <= C['look.wall_dist'] && d > 1e-6) blocked.push((Math.atan2(q[1] - m.pos[1], q[0] - m.pos[0]) * 180) / Math.PI);
  }
  const open = Array.from({ length: 360 }, (_, i) => i - 179).map((a) => !members.some((m) => angleDiff(m.yaw, a) <= FOV / 2) && !blocked.some((b) => angleDiff(b, a) < 45));
  if (open.every(Boolean)) return 360;
  const i0 = open.findIndex((o) => !o);
  let run = 0, gap = 0;
  for (let k = 1; k <= 360; k++) { if (open[(i0 + k) % 360]) { run++; gap = Math.max(gap, run); } else run = 0; }
  return gap;
}

test('rear guard: the place farthest back holds 6 o\'clock in every formation of 3 and 4 (the pair keeps its own rule)', () => {
  for (const n of [3, 4]) for (const f of ['diamond', 'wedge', 'column', 'echelon_right', 'echelon_left', 'line', 'blizzard'] as Formation[]) {
    const r = A([], [0, 0], 0, placesOf(f, n), 'halt', 0, C)!;
    assert.equal(r.rearGuard, 'Z', `${f} ${n}`);
    near(Math.abs(yawOf(r, 'Z')), 180);
    near(yawOf(r, 'K'), 0);
  }
  assert.equal(A([], [0, 0], 0, placesOf('diamond', 2), 'walk', 0, C), null);
});

test('rear guard: walls on one side shift him to the open side (wall left -> 5 o\'clock) and 5-7 o\'clock stays in his cone', () => {
  const wallLeft: Segment[] = [{ a: [-3000, -200], b: [3000, -200], thickness: 40 }]; // close to every slot of a column
  const m = placesOf('column', 4);
  const halt = A(wallLeft, [0, 0], 0, m, 'halt', 0, C)!;
  near(yawOf(halt, 'Z'), 180 - C['rear.wall_shift_deg']);
  for (let t = 0; t < 8; t += 0.1) {
    const z = yawOf(A(wallLeft, [0, 0], 0, m, 'walk', t, C)!, 'Z');
    assert.ok(angleDiff(z, 180) <= FOV / 2 - 30 + 1e-6, `t=${t.toFixed(1)}: rear sector out of his cone (${z})`);
  }
});

test('triad: K the front, Z the rear, P sweeps both flanks 3 -> 12 -> 9 and K searches against him; halt K 12, P 3, Z 6', () => {
  const m = placesOf('wedge', 3);
  const halt = A([], [0, 0], 0, m, 'halt', 0, C)!;
  assert.deepEqual([yawOf(halt, 'K'), yawOf(halt, 'P'), Math.abs(yawOf(halt, 'Z'))].map((v) => Math.round(v)), [0, 90, 180]);
  const share = A([], [0, 0], 0, m, 'walk', 0, C)!.shares.find((s) => s.role === 'P')!;
  assert.ok(share.from < -60 && share.to > 60, 'on the move one share across the front (standing K and Z hold their whole cones)');
  let maxP = -Infinity, minP = Infinity;
  for (let t = 0; t < 12; t += 0.05) {
    const r = A([], [0, 0], 0, m, 'walk', t, C)!;
    const p = yawOf(r, 'P'), k = yawOf(r, 'K');
    maxP = Math.max(maxP, p); minP = Math.min(minP, p);
    if (Math.abs(p) > 45) assert.ok(Math.sign(k) !== Math.sign(p) || Math.abs(k) < 1e-6, `t=${t.toFixed(2)}: K ${k} follows P ${p}`);
  }
  near(maxP, C['coverage.max_sweep'], 0.5); near(minP, -C['coverage.max_sweep'], 0.5);
});

test('diamond: P the right flank, L the left - each covers his whole share at every moment (gap <= coverage.max_gap_deg)', () => {
  const m = placesOf('diamond', 4);
  for (let t = 0; t < 10; t += 0.1) {
    const r = A([], [0, 0], 0, m, 'walk', t, C)!;
    assert.ok(yawOf(r, 'P') > 0 && yawOf(r, 'L') < 0);
    assert.ok(largestGap([], m.map((x) => ({ pos: x.pos, yaw: yawOf(r, x.role) }))) <= C['coverage.max_gap_deg'], `t=${t.toFixed(1)}`);
  }
});

test('fixed looks: a corner watcher counts as covering his cone; the front he leaves goes to the others', () => {
  const m = placesOf('wedge', 3);
  const free = A([], [0, 0], 0, m, 'halt', 0, C)!;
  const fixedK = A([], [0, 0], 0, m, 'halt', 0, C, [{ role: 'K', yaw: -100 }])!;
  near(yawOf(fixedK, 'K'), -100);
  assert.notEqual(Math.round(yawOf(fixedK, 'P')), Math.round(yawOf(free, 'P')));
  assert.ok(largestGap([], m.map((x) => ({ pos: x.pos, yaw: yawOf(fixedK, x.role) }))) <= C['coverage.max_gap_deg']);
});

test('static (playtest 2026-10-25): three in cover at a wall end, the leader aiming along the wall - the gap closes', () => {
  // A wall along +X ending at the origin; the group stands on its +Y face (the wall layout), the leader aims past the wall
  // end along the wall (yaw 180). Before: the two followers looked the same way he did.
  const wall: Segment = { a: [0, 0], b: [3000, 0], thickness: 40 };
  const slots = FormationWallRules.Layout(wall, [400, 300], 3, 400, 2600, C);
  const members = slots.map((s) => ({ role: s.role, pos: s.pos }));
  const fixed = [{ role: 'K' as Role, yaw: 180 }];
  const before = largestGap([wall], members.map((m) => ({ pos: m.pos, yaw: 180 })));
  const looks = FormationCoverRules.AllocateStatic([wall], members, fixed, ['P', 'Z'], C);
  const after = largestGap([wall], members.map((m) => ({ pos: m.pos, yaw: m.role === 'K' ? 180 : looks.find((l) => l.role === m.role)!.yaw })));
  assert.ok(before > 120, `before ${before}`);
  assert.ok(after <= C['coverage.max_gap_deg'], `after ${after}`);
  // One of them covers the rear (opposite the leader's aim = along the wall the other way, +X), nobody looks into the wall.
  assert.ok(looks.some((l) => angleDiff(l.yaw, 0) <= 30), JSON.stringify(looks));
  for (const l of looks) assert.ok(angleDiff(l.yaw, -90) >= 45, `${l.role} into the wall (${l.yaw})`);
});

test('static: no fixed look and no walls -> the free members face outward from the group, deterministic', () => {
  const members = [{ role: 'K' as Role, pos: [0, 0] as Vec2 }, { role: 'P' as Role, pos: [-200, 150] as Vec2 }, { role: 'Z' as Role, pos: [-200, -150] as Vec2 }];
  const a = FormationCoverRules.AllocateStatic([], members, [], ['K', 'P', 'Z'], C);
  const b = FormationCoverRules.AllocateStatic([], members, [], ['K', 'P', 'Z'], C);
  assert.deepEqual(a, b);
  assert.ok(largestGap([], members.map((m) => ({ pos: m.pos, yaw: a.find((l) => l.role === m.role)!.yaw }))) <= C['coverage.max_gap_deg']);
  assert.equal(normYaw(a.length), 3);
});

test('lab: auto with 3 and 4 keeps the rear (check 2) and the coverage (check 10) on every route', () => {
  const lab = readLayout('formation_lab_01');
  const s = runSeries({ layout: lab, config: C, members: [3, 4], seeds: 1, seedBase: 1 }).summary;
  for (const id of [1, 2, 10]) assert.equal(s.checks.find((c) => c.id === id)!.fail, 0, s.checks.find((c) => c.id === id)!.worst);
});

test('wall slide: a destination along the same wall -> everyone moves sideways together, order and wall offset kept', () => {
  const wall: Segment = { a: [0, 0], b: [3000, 0], thickness: 40 };
  const start = FormationWallRules.Layout(wall, [1500, 300], 4, 1500, 1500, C);
  const k0 = start[0].pos;
  // 6 m along the wall (+X), 1 m off it: a slide.
  const dest: Vec2 = [2100, 100];
  const plan = FormationWallRules.Slide(wall, start, k0, dest, 0, C);
  assert.equal(plan.active, true);
  near(plan.leaderTarget[0], 2100); near(plan.leaderTarget[1], k0[1]);
  // Halfway: everyone moved 3 m along the wall, the offsets kept.
  const mid = FormationWallRules.Slide(wall, start, [k0[0] + 300, k0[1]], dest, 1, C);
  mid.targets.forEach((s, i) => { near(s.pos[0], start[i].pos[0] + 300); near(s.pos[1], start[i].pos[1]); });
  // The order from the leading end (+X) to the trailing end; the leading end looks ahead (+X) turned outward, the trailing
  // end back (-X) turned outward and is the rear guard; K faces away from the wall.
  const byX = [...start].sort((a, b) => b.pos[0] - a.pos[0]).map((s) => s.role);
  assert.deepEqual(plan.order, byX);
  const look = (r: Role) => plan.targets.find((s) => s.role === r)!.yaw;
  near(look(plan.lead!), C['slide.lead_out_deg']);
  near(Math.abs(look(plan.rearGuard!)), 180 - C['slide.lead_out_deg']);
  assert.equal(plan.rearGuard, byX[byX.length - 1]);
  assert.ok(angleDiff(look('K'), 90) <= C['scan.amp_lead'] + 1e-6, 'K away from the wall');
  for (const s of plan.targets) assert.ok(angleDiff(s.yaw, -90) >= 45, `${s.role} into the wall`);
});

test('wall slide: not a slide -> too far off the wall, the other side, beyond its end, or barely along it', () => {
  const wall: Segment = { a: [0, 0], b: [3000, 0], thickness: 40 };
  const start = FormationWallRules.Layout(wall, [1500, 300], 3, 1500, 1500, C);
  const k0 = start[0].pos;
  for (const dest of [[2100, 20 + C['slide.max_off_wall'] + 50], [2100, -150], [3600, 100], [k0[0] + 30, 100]] as Vec2[]) {
    assert.equal(FormationWallRules.Slide(wall, start, k0, dest, 0, C).active, false, JSON.stringify(dest));
  }
});

test('lab: the slide route both ways - order kept, nobody split, the open half covered', () => {
  const lab = readLayout('formation_lab_01');
  const s = runSeries({ layout: lab, config: C, members: [2, 3, 4], seeds: 1, seedBase: 1, routes: ['R_A_slide'] }).summary;
  for (const id of [5, 7, 11]) assert.equal(s.checks.find((c) => c.id === id)!.fail, 0, s.checks.find((c) => c.id === id)!.worst);
  assert.equal(s.checks.find((c) => c.id === 11)!.pass, 3);
});
