// Training Ground / movement: red team v2.5 triage (docs/design/FORMATION_REDTEAM_V25.md) - regression cases for the fixes. The 2026-10-28
// run (4 rounds, 6 kept layouts, 12 distinct failures) found: a flicker at the arrival when the click lay > classify.arrive_dist from the
// leader's wall slot (13_4), a follower crossing the leader's way after a turn (13_4 / 15_4 / 16_4: the runtime's yield was missing from the
// simulator), and a T stem's free tip end-on never read as a wall ahead (16_4: the rays saw only the long faces).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import type { Segment, Vec2 } from './geometry.ts';
import { raycastWalls, wallEndFaces } from './geometry.ts';
import { configValues } from './config.ts';
import { readConfigFile, REDTEAM_DIR } from './cli/paths.ts';
import type { Layout } from './layout.ts';
import { reproduces } from './redteam.ts';
import { runSeries } from './bot.ts';
import { renderRegression } from './redteam.ts';
import { CLASSIFY_START, FormationTerrainRules } from './rules.ts';
import { YIELD_CLEAR, yieldFromLeaderWay } from './sim.ts';

const C = configValues(readConfigFile());
const kept = (id: string): Layout => JSON.parse(fs.readFileSync(path.join(REDTEAM_DIR, `${id}.json`), 'utf8')) as Layout;
const near = (a: Vec2, b: Vec2, tol = 1e-6) => Math.hypot(a[0] - b[0], a[1] - b[1]) <= tol;

test('red team v2.5 (16_4): a thick wall has flat end faces at its body tips; a ray along its axis hits the near one', () => {
  const stem: Segment = { a: [120, 0], b: [2000, 0], thickness: 40 };
  const [ea, eb] = wallEndFaces(stem);
  assert.ok(near(ea.a, [100, 20]) && near(ea.b, [100, -20]) && ea.thickness === 0, 'the a end: 20 cm before a, across the wall');
  assert.ok(near(eb.a, [2020, 20]) && near(eb.b, [2020, -20]));
  assert.deepEqual(wallEndFaces({ a: [0, 0], b: [100, 0], thickness: 0 }), [], 'a thin wall has no end faces');
  assert.equal(raycastWalls([0, 0], [1, 0], [stem], 300), 100, 'end-on: the end face 1 m ahead (the long faces are parallel to the ray)');
  assert.equal(raycastWalls([0, 0], [-1, 0], [stem], 300), Infinity);
  // The long faces still win where they are nearer (a ray across the wall).
  assert.equal(raycastWalls([500, -300], [0, 1], [stem], 400), 280);
});

test('red team v2.5 (16_4): arriving end-on at a T stem\'s free tip is an arrival at a wall (at_wall), walking at it is not', () => {
  const walls: Segment[] = [{ a: [120, 0], b: [2000, 0], thickness: 40 }];
  assert.equal(FormationTerrainRules.Classify(walls, [0, 0], 0, [0, 0], CLASSIFY_START, 0, C).current, 'at_wall');
  assert.equal(FormationTerrainRules.Classify(walls, [0, 0], 0, [1000, 0], CLASSIFY_START, 0, C).current, 'open', 'still walking on: no wall layout');
});

test('red team v2.5 (13_4): the runtime\'s yield - a follower on the leader\'s way (or whose place is on it) steps aside, own side first', () => {
  // The leader walks +X to (1000, 0); a follower 3 m ahead, 30 cm to the right (+Y), walks to a place behind the leader.
  const aside = yieldFromLeaderWay([0, 0], [1000, 0], [300, 30], [-200, 0]);
  assert.equal(aside.length, 2);
  assert.ok(near(aside[0], [300, YIELD_CLEAR]) && near(aside[1], [300, -YIELD_CLEAR]), 'beside him, his own (right) side first');
  // His place on the way, he himself beside it: the point beside his place.
  const placeOnWay = yieldFromLeaderWay([0, 0], [1000, 0], [400, -300], [450, -10]);
  assert.ok(near(placeOnWay[0], [450, -YIELD_CLEAR]), 'the place\'s side (left) first');
  assert.deepEqual(yieldFromLeaderWay([0, 0], [1000, 0], [-200, 0], [-300, 260]), [], 'behind the leader and off his way: no yield');
  assert.deepEqual(yieldFromLeaderWay([0, 0], [1000, 0], [800, 0], [-300, 0]).length, 0, 'beyond 600 + 80 cm ahead: not his next stretch');
  assert.deepEqual(yieldFromLeaderWay([0, 0], [0.5, 0], [10, 0], [10, 0]), [], 'no way (standing)');
});

test('red team v2.5: the kept layouts no longer reproduce the fixed failures (sim integration + the end face)', () => {
  // 13_4: wall -> wedge -> wall at the arrival (the click 55 cm from the leader's wall slot) - the flicker and the covered approach's reset.
  for (const members of [2, 3, 4]) assert.equal(reproduces(kept('13_4'), { check: 3, choice: 'auto', members, route: 'R1', seed: 1 }, C), false, `13_4 #3 n${members}`);
  assert.equal(reproduces(kept('13_4'), { check: 14, choice: 'auto', members: 4, route: 'R1', seed: 1 }, C), false, '13_4 #14 n4');
  // 13_4 / 15_4 / 16_4: a follower crossing the leader's way after a 90 deg turn (the yield).
  assert.equal(reproduces(kept('13_4'), { check: 5, choice: 'diamond', members: 4, route: 'R1', seed: 1 }, C), false, '13_4 #5 diamond');
  for (const choice of ['auto', 'diamond', 'all_forward'] as const) {
    assert.equal(reproduces(kept('15_4'), { check: 5, choice, members: 4, route: 'R1', seed: 2 }, C), false, `15_4 #5 ${choice}`);
    assert.equal(reproduces(kept('16_4'), { check: 5, choice, members: 4, route: 'R1', seed: 1 }, C), false, `16_4 #5 ${choice}`);
  }
});

test('red team v2.5 (16_4): the squad takes the wall layout at the stem tip and holds it while standing (zone Z1 wants wall)', () => {
  const layout = kept('16_4');
  for (const members of [2, 3, 4]) {
    const { runs } = runSeries({ layout, config: C, members: [members], seeds: 1, seedBase: 1 });
    const z1 = runs[0].expect.find((e) => e.zone === 'Z1')!;
    assert.ok(z1.settled > 0 && z1.pct >= 80, `n${members}: Z1 wall ${z1.pct} % of ${z1.settled} s standing`);
  }
});

test('red team v2.5: the regression table lists checks 13 and 14 (they were counted but not shown)', () => {
  const f = { check: 14, choice: 'auto' as const, members: 4, route: 'R1', seed: 1, t: null, what: 'x' };
  const text = renderRegression([{ file: 'x.json', known: 1, fixed: [], fresh: [], runs: 1, current: [f], fixedChecks: [], knownChecks: [14] }],
    { dir: 'd', configHash: 'h', date: 'now' });
  assert.match(text, /\| 14 covered approach to the wall \| 1 \| 0 \| 0 \|/);
});
