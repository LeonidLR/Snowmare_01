// Training Ground / movement: v2.3 tests - the red-team rule fixes (FORMATION_CONTRACT.md 13). Coordinator task 2026-10-25:
// fix the rule bugs the red team (Content/Data/FormationLab/redteam) found, v2.2 behaviour the user approved unchanged.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { Segment } from './geometry.ts';
import { distToWall } from './geometry.ts';
import { configValues } from './config.ts';
import { readConfigFile } from './cli/paths.ts';
import { FormationSectorRules, FormationShapeRules } from './rules.ts';

const C = configValues(readConfigFile());
const L = FormationSectorRules.ClearLook;
const near = (a: number, b: number, eps = 1e-6) => assert.ok(Math.abs(a - b) <= eps, `${a} != ${b}`);

// Red team 2_8 (zigzag corridor): walls left-ahead (-45 deg, 1.9 m) and left-behind (-160 deg, 1.5 m) leave a sliver near -100.
const sliver: Segment[] = [{ a: [-290, -558], b: [558, 290], thickness: 40 }, { a: [-278, 325], b: [-4, -427], thickness: 40 }];

test('v2.3: ClearLook - a free arc narrower than look.min_free_deg is no place to look (red team 2_8: P swung across the wall to it)', () => {
  assert.ok(C['look.min_free_deg'] >= 10);
  for (const want of [-100, -95]) {
    const got = L(sliver, [0, 0], want, 10, C);
    assert.ok(Math.abs(got - want) > 30, `want ${want} in the sliver -> ${got}`);
    near(got, 10, 1e-6); // the edge of the wide front arc he already looks into (no sweep across the -45 wall)
  }
  assert.equal(L(sliver, [0, 0], 30, 10, C), 30, 'a look in the wide arc stays');
  // With the rule off (0) the sliver counts again - the v2.2 behaviour.
  assert.equal(L(sliver, [0, 0], -100, 10, { ...C, 'look.min_free_deg': 0 }), -100);
});

test('v2.3: ClearLook - a free wanted yaw is kept even when the turn sweeps a side wall (the rear guard in a corridor keeps 6 o\'clock)', () => {
  const corridor: Segment[] = [{ a: [-2000, -60], b: [2000, -60], thickness: 20 }, { a: [-2000, 60], b: [2000, 60], thickness: 20 }];
  for (const cur of [0, 60, -60]) assert.equal(L(corridor, [0, 0], 180, cur, C), 180);
  assert.equal(L(corridor, [0, 0], 0, 180, C), 0);
});

test('v2.3: ClearLook - enclosed on all sides: the longest clear ray, ties the nearest the wanted yaw (was: the wanted yaw into a wall)', () => {
  const pocket: Segment[] = [{ a: [-100, -100], b: [100, -100], thickness: 20 }, { a: [100, -100], b: [100, 100], thickness: 20 },
    { a: [100, 100], b: [-100, 100], thickness: 20 }, { a: [-100, 100], b: [-100, -100], thickness: 20 }];
  assert.equal(L(pocket, [0, 0], 0, 0, C), 45, 'the diagonals tie; +45 is reached first from 0 (+ before -)');
  assert.equal(L(pocket, [0, 0], 30, 30, C), 45);
  assert.equal(L(pocket, [0, 0], 100, 100, C), 135);
  assert.equal(L(pocket, [0, 0], -170, -170, C), -135);
});

test('v2.3: ClearLook hysteresis - the held result stays while it is clear of the walls; a wall closing on it drops it', () => {
  // The rear man: wanted 180 blocked by a wall behind (1.5 m), he held -150 (free) -> keeps it rather than jump to the far edge.
  const behind: Segment[] = [{ a: [-170, -2000], b: [-170, 2000], thickness: 20 }];
  const half = C['look.wall_angle'];
  const fresh = L(behind, [0, 0], 180, -150, C);
  near(Math.abs(fresh), 180 - half, 1e-6); // no held: the nearest edge (+-125)
  assert.equal(L(behind, [0, 0], 180, -150, C, -120), -120, 'held -120 (55 deg off the wall) is kept');
  // A held look within look.wall_angle - look.hold_margin_deg of a wall is dropped.
  near(Math.abs(L(behind, [0, 0], 180, -150, C, -140)), 180 - half, 1e-6);
  // A free wanted yaw always wins over the held one; hold 0 = the v2.2 memoryless rule.
  assert.equal(L(behind, [0, 0], 0, -150, C, -120), 0);
  near(Math.abs(L(behind, [0, 0], 180, -150, { ...C, 'look.hold_deg': 0 }, -120)), 180 - half, 1e-6);
});

test('v2.3: the trail column stagger shrinks in a narrow passage so every slot keeps column.stagger_clear (red team #7)', () => {
  const trail: [number, number][] = [[-3000, 0], [0, 0]];
  const free = FormationShapeRules.TrailSlots(trail, 0, 4, C);
  assert.deepEqual(free, FormationShapeRules.TrailSlots(trail, 0, 4, C, []), 'no walls -> v2.1 slots');
  const corridor: Segment[] = [{ a: [-4000, -100], b: [1000, -100], thickness: 20 }, { a: [-4000, 100], b: [1000, 100], thickness: 20 }];
  const tight = FormationShapeRules.TrailSlots(trail, 0, 4, C, corridor);
  const clear = C['column.stagger_clear'];
  for (const s of tight) {
    for (const w of corridor) assert.ok(distToWall(s.pos, w) >= clear - 1e-6, `${s.role} ${s.pos} too close`);
    const f = free.find((x) => x.role === s.role)!;
    assert.equal(s.pos[0], f.pos[0], 'only the stagger changes');
    assert.ok(Math.abs(s.pos[1]) <= Math.abs(f.pos[1]) + 1e-9 && Math.sign(s.pos[1]) * Math.sign(f.pos[1]) >= 0);
  }
  // 90 cm surface room - 45 clearance = 45 cm, rounded down to the 5 cm step from 60: 45.
  assert.equal(Math.abs(tight.find((s) => s.role === 'P')!.pos[1]), 45);
  // A wide corridor: unchanged.
  const wide: Segment[] = [{ a: [-4000, -300], b: [1000, -300], thickness: 20 }, { a: [-4000, 300], b: [1000, 300], thickness: 20 }];
  assert.deepEqual(FormationShapeRules.TrailSlots(trail, 0, 4, C, wide), free);
});
