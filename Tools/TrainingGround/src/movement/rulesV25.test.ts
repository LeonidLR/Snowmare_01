// Training Ground / movement: v2.5 tests - the covered approach to the wall (FORMATION_CONTRACT.md 15). New user rule 2026-10-26: the
// squad on patrol is ordered to the wall ("Formation: Wall") and everyone used to drop his sector and dash to the wall, the rear wide
// open. Now, until they are settled at the wall: one covers the withdrawal (the rear, away from the wall), one a flank, the leader runs
// to the wall and looks along it; each takes his wall-layout look as he arrives, the rear cover holds until everyone is in.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { Segment, Vec2 } from './geometry.ts';
import { angleDiff, closestOnSegment, distToWall, sub, yawOf } from './geometry.ts';
import { configValues } from './config.ts';
import { readConfigFile, readLayout } from './cli/paths.ts';
import { runSeries } from './bot.ts';
import { approachOk, LIMITS } from './checks.ts';
import type { Role, WallSlot } from './rules.ts';
import { FormationWallRules } from './rules.ts';

const C = configValues(readConfigFile());
const face: Segment = { a: [0, -1500], b: [0, 1500], thickness: 40 }; // outward normal +X (yaw 0)
const layout = (n: number): WallSlot[] => FormationWallRules.Layout(face, [65, 0], n, 500, 500, C);
/** Members at those places (else at their slots), looking as on patrol: the leader towards the wall, P / L scanning, Z back. */
const LOOK: Record<Role, number> = { K: 180, P: -135, L: 135, Z: 0 };
const at = (slots: WallSlot[], where: Partial<Record<Role, Vec2 | 'slot'>>, look: Partial<Record<Role, number>> = {}) =>
  slots.map((s) => ({ role: s.role, pos: (where[s.role] === 'slot' || !where[s.role] ? s.pos : where[s.role]) as Vec2, yaw: look[s.role] ?? LOOK[s.role] }));
const lookOf = (r: ReturnType<typeof FormationWallRules.Approach>, role: Role) => r.looks.find((l) => l.role === role)!;

test('v2.5: three men - the leader runs (his travel), the man farthest back holds the rear, the other a flank', () => {
  const slots = layout(3);
  const start = FormationWallRules.Approach([face], slots, 0, at(slots, { K: [400, 0], P: [680, 260], Z: [960, 0] }), null, C);
  assert.equal(start.state.runner, 'K');
  assert.equal(start.state.rear, 'Z');
  assert.deepEqual(start.state.flanks, [{ role: 'P', side: 1 }]);
  assert.equal(start.state.alongSide, -1, 'the leader will look along the wall the other way');
  assert.deepEqual(lookOf(start, 'K'), { role: 'K', duty: 'run', yaw: null });
  assert.deepEqual(lookOf(start, 'Z'), { role: 'Z', duty: 'rear', yaw: 0 });
  assert.deepEqual(lookOf(start, 'P'), { role: 'P', duty: 'flank', yaw: 90 });
  // The leader at his slot: along the wall (-90), the others still on their way.
  const runnerIn = FormationWallRules.Approach([face], slots, 0, at(slots, { K: 'slot', P: [520, 200], Z: [800, 0] }), start.state, C);
  assert.deepEqual(lookOf(runnerIn, 'K'), { role: 'K', duty: 'along', yaw: -90 });
  // P at his slot: his layout look (4 o'clock); Z still holds the rear.
  const flankIn = FormationWallRules.Approach([face], slots, 0, at(slots, { K: 'slot', P: 'slot', Z: [600, 0] }), start.state, C);
  assert.equal(lookOf(flankIn, 'P').duty, 'layout');
  assert.equal(lookOf(flankIn, 'P').yaw, slots[1].yaw);
  assert.equal(lookOf(flankIn, 'Z').duty, 'rear');
  assert.equal(lookOf(flankIn, 'K').duty, 'along', 'the first arrival keeps looking along the wall until everyone is in');
  // Everyone in: the plain wall layout, for good.
  const done = FormationWallRules.Approach([face], slots, 0, at(slots, {}), flankIn.state, C);
  assert.equal(done.state.done, true);
  done.looks.forEach((l, i) => assert.deepEqual(l, { role: slots[i].role, duty: 'layout', yaw: slots[i].yaw }));
  const after = FormationWallRules.Approach([face], slots, 0, at(slots, { Z: [600, 0] }), done.state, C);
  assert.ok(after.looks.every((l) => l.duty === 'layout'), 'done is sticky (no flicker when somebody shifts)');
});

test('v2.5: the roles are held - nobody is reshuffled when the positions change on the way', () => {
  const slots = layout(3);
  const start = FormationWallRules.Approach([face], slots, 0, at(slots, { K: [400, 0], P: [680, 260], Z: [960, 0] }), null, C);
  // P now farther back than Z: the roles stay.
  const later = FormationWallRules.Approach([face], slots, 0, at(slots, { K: [300, 0], P: [990, 260], Z: [500, 0] }), start.state, C);
  assert.deepEqual(later.state, start.state);
  assert.equal(lookOf(later, 'Z').duty, 'rear');
});

test('v2.5: the pair - the leader runs and looks along the wall, the partner covers the rear', () => {
  const slots = layout(2);
  const r = FormationWallRules.Approach([face], slots, 0, at(slots, { K: [400, 0], Z: [680, 260] }), null, C);
  assert.equal(r.state.rear, 'Z');
  assert.deepEqual(r.state.flanks, []);
  assert.equal(r.state.alongSide, -1, 'the other way from the partner\'s side');
  assert.equal(lookOf(r, 'Z').yaw, 0);
  const inn = FormationWallRules.Approach([face], slots, 0, at(slots, { K: 'slot', Z: [680, 260] }), r.state, C);
  assert.deepEqual(lookOf(inn, 'K'), { role: 'K', duty: 'along', yaw: -90 });
});

test('v2.5: four - the rear, both flanks (one each side), the leader along the wall', () => {
  const slots = layout(4);
  const r = FormationWallRules.Approach([face], slots, 0, at(slots, { K: [400, 0], P: [680, 260], L: [680, -260], Z: [960, 0] }), null, C);
  assert.equal(r.state.rear, 'Z');
  assert.deepEqual([...r.state.flanks].sort((a, b) => a.side - b.side), [{ role: 'L', side: -1 }, { role: 'P', side: 1 }]);
  assert.equal(lookOf(r, 'P').yaw, 90);
  assert.equal(lookOf(r, 'L').yaw, -90);
});

test('v2.5: nobody looks into a wall on the approach (a side wall: the flank look turns out of it)', () => {
  const corner: Segment[] = [face, { a: [0, -250], b: [1500, -250], thickness: 40 }];
  const slots = layout(4);
  const r = FormationWallRules.Approach(corner, slots, 0, at(slots, { K: [400, 0], P: [680, 260], L: [500, -150], Z: [960, 0] }), null, C);
  for (const l of r.looks) {
    if (l.yaw === null) continue;
    const pos = at(slots, { K: [400, 0], P: [680, 260], L: [500, -150], Z: [960, 0] }).find((m) => m.role === l.role)!.pos;
    for (const w of corner) {
      if (distToWall(pos, w) > LIMITS.stareDist) continue;
      assert.ok(angleDiff(l.yaw, yawOf(sub(closestOnSegment(pos, w.a, w.b).point, pos))) > LIMITS.stareAngle, `${l.role} ${l.yaw}`);
    }
  }
});

test('v2.5: the lab dead ends - check 14 green for 2-4 men (the rear always held, a flank / both flanks)', () => {
  const lab = readLayout('formation_lab_01');
  const s = runSeries({ layout: lab, config: C, members: [2, 3, 4], seeds: 2, seedBase: 1, routes: ['R_A_open', 'R_F_deadend', 'R_G_end'] });
  const line = s.summary.checks.find((c) => c.id === 14)!;
  assert.equal(line.fail, 0, line.worst);
  assert.ok(line.pass >= 18, `${line.pass} runs with an approach`);
  for (const r of s.runs) assert.ok(approachOk(r), `${r.route} n${r.members}`);
});

test('v2.5: the rear cover is the man already looking back (the patrol rear guard: no gap while he turns); all facing the wall -> the farthest back', () => {
  const slots = layout(3);
  const where = { K: [400, 0] as Vec2, P: [990, 260] as Vec2, Z: [700, 0] as Vec2 };
  assert.equal(FormationWallRules.Approach([face], slots, 0, at(slots, where), null, C).state.rear, 'Z', 'Z looks back, P is farther');
  assert.equal(FormationWallRules.Approach([face], slots, 0, at(slots, where, { P: 180, Z: 180 }), null, C).state.rear, 'P', 'nobody looks back: the farthest');
});

test('v2.5: the squad never splits first - a man whose way to his slot passes a wall goes round it facing his run, his role kept', () => {
  const corner: Segment[] = [face, { a: [0, -250], b: [1500, -250], thickness: 40 }];
  const slots = layout(4);
  const r = FormationWallRules.Approach(corner, slots, 0, at(slots, { K: [400, 0], P: [680, 260], L: [680, -400], Z: [960, 0] }), null, C);
  assert.deepEqual(lookOf(r, 'L'), { role: 'L', duty: 'run', yaw: null }, 'L is beyond the side wall');
  assert.ok(r.state.flanks.some((f) => f.role === 'L'), 'his flank role is kept');
  const back = FormationWallRules.Approach(corner, slots, 0, at(slots, { K: 'slot', P: [520, 200], L: [500, -150], Z: [800, 0] }), r.state, C);
  assert.equal(lookOf(back, 'L').duty, 'flank', 'round the wall: his flank look again');
});
