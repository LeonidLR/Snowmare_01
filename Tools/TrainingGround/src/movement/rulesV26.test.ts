// Training Ground / movement: v2.6 tests - the game designer's answers 2026-11-01 to the red team v2.5 questions (FORMATION_CONTRACT.md 18,
// FORMATION_REDTEAM_V25.md Q1-Q3): Q1 = b - a post / a short stub beside the path is no side wall (sideWalls / obstacleGroups,
// classify.side_min_length / side_join_gap); Q2 = b - the rear guard lagging behind his slot turns round and catches up facing forwards
// (FormationSectorRules.RearCatchUp, move.rear_turn_lag / rear_turn_back); Q3 = keep (the 5 m hold, no change).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { Segment } from './geometry.ts';
import { angleDiff } from './geometry.ts';
import { configValues } from './config.ts';
import { readConfigFile, readLayout, REDTEAM_DIR } from './cli/paths.ts';
import { runSeries } from './bot.ts';
import type { Frame } from './sim.ts';
import { buildNav, simulate } from './sim.ts';
import { FormationSectorRules, FormationTerrainRules, obstacleGroups, rawClass, sideWalls } from './rules.ts';

const C = configValues(readConfigFile());
const OFF = { ...C, 'classify.side_min_length': 0 };
const w = (ax: number, ay: number, bx: number, by: number, thickness: number): Segment => ({ a: [ax, ay], b: [bx, by], thickness });

test('v2.6 Q1: the designer\'s thresholds - 1.5 m along the path, pieces within 50 cm are one obstacle', () => {
  assert.equal(C['classify.side_min_length'], 150);
  assert.equal(C['classify.side_join_gap'], 50);
});

test('v2.6 Q1: a pillar / a short stub beside the path is no side wall; a 2 m wall is', () => {
  const pillar = [w(-30, -240, 30, -240, 40)]; // red team 14_1: 60 x 40 cm, 2.4 m left
  assert.equal(rawClass(pillar, [0, 0], 0, null, C), 'open');
  assert.equal(rawClass(pillar, [0, 0], 0, null, OFF), 'wall_left', 'side_min_length 0 = off (v2.5.1)');
  assert.equal(rawClass([w(-40, 250, 40, 250, 40)], [0, 0], 0, null, C), 'open', '1.2 m stub on the right');
  assert.equal(rawClass([w(-80, -250, 80, -250, 40)], [0, 0], 0, null, C), 'wall_left', '2 m wall');
  assert.equal(sideWalls(pillar, 90, C).length, 0, 'walking along Y the pillar is 40 cm long');
});

test('v2.6 Q1: touching pieces are one long wall, a row of posts is not', () => {
  const pieces = [0, 1, 2, 3, 4].map((k) => w(-250 + 100 * k, -260, -150 + 100 * k, -260, 20));
  assert.deepEqual(obstacleGroups(pieces, 50), [0, 0, 0, 0, 0]);
  assert.equal(rawClass(pieces, [0, 0], 0, null, C), 'wall_left');
  const posts = [-200, -60, 80, 220].map((x) => w(x, 260, x, 261, 30));
  assert.deepEqual(obstacleGroups(posts, 50), [0, 1, 2, 3]);
  assert.equal(rawClass(posts, [0, 0], 0, null, C), 'open');
  // A short stub on a long wall (a T joint) belongs to the long obstacle.
  const tee = [w(-2000, 380, 2000, 380, 40), w(0, 360, 0, 260, 40)];
  assert.equal(sideWalls(tee, 0, C).length, 2);
});

test('v2.6 Q1: a long wall and a pillar opposite - an echelon, not a column (PassageWidth: none)', () => {
  const walls = [w(-2000, -250, 2000, -250, 40), w(-30, 240, 30, 240, 40)];
  assert.equal(rawClass(walls, [0, 0], 0, null, C), 'wall_left');
  assert.equal(FormationTerrainRules.PassageWidth(walls, [0, 0], 0, C), null);
  assert.equal(rawClass(walls, [0, 0], 0, null, OFF), 'corridor');
});

test('v2.6 Q2: RearCatchUp - out beyond move.rear_turn_lag, back only within move.rear_turn_back (no flicker)', () => {
  assert.equal(C['move.rear_turn_lag'], 150);
  assert.equal(C['move.rear_turn_back'], 50);
  const lags = [40, 120, 151, 200, 160, 120, 80, 140, 100, 60, 50, 45, 70, 110, 149, 40];
  let turned = false, changes = 0;
  const seen = lags.map((lag) => { const next = FormationSectorRules.RearCatchUp(lag, turned, false, C); if (next !== turned) changes++; turned = next; return turned; });
  assert.deepEqual(seen, [false, false, true, true, true, true, true, true, true, true, false, false, false, false, false, false]);
  assert.equal(changes, 2);
});

test('v2.6 red team 14_1: the pillars and the stub beside the path - no echelon, flicker (#3) and rear pace (#6) green', () => {
  const layout = readLayout(`${REDTEAM_DIR}/14_1.json`);
  const s = runSeries({ layout, config: C, members: [2, 3, 4], seeds: 2, seedBase: 1 });
  for (const id of [3, 6]) {
    const line = s.summary.checks.find((c) => c.id === id)!;
    assert.equal(line.fail, 0, `check ${id}: ${line.worst}`);
  }
  const nav = buildNav(layout);
  for (const n of [2, 3, 4]) {
    const formations = new Set<string>();
    simulate({ layout, route: layout.routes[0], memberCount: n, config: C, seed: 1, nav, onFrame: (f) => { formations.add(f.formation); } });
    assert.ok(![...formations].some((f) => f.startsWith('echelon')), `n${n}: ${[...formations].join(', ')}`);
  }
});

test('v2.6 Q2: the lagging rear guard turns round, faces his run while he catches up, then walks backwards again', () => {
  // 14_1 with the Q1 filter off: the echelon swings Z's slot over the 1.5 m stub, he falls 2 m behind (red team v2.5 #6, p95 203 cm).
  const layout = readLayout(`${REDTEAM_DIR}/14_1.json`);
  const nav = buildNav(layout);
  const frames: Frame[] = [];
  simulate({ layout, route: layout.routes[0], memberCount: 4, config: OFF, seed: 2, nav, onFrame: (f) => frames.push(f) });
  const rear = frames.map((f) => f.members.find((m) => m.rearGuard && m.role !== 'K'));
  const on = rear.map((m) => !!m?.rearTurned);
  const first = on.indexOf(true);
  assert.ok(first > 0, 'he turned round');
  const last = on.lastIndexOf(true);
  assert.ok(on.slice(first, last + 1).every(Boolean), 'one turn, no flicker');
  // Turned (after his body has come round, 0.5 s) he faces his run - never walking backwards.
  for (let k = first + 15; k <= last; k++) {
    const m = rear[k]!;
    if (m.moveYaw !== null) assert.ok(angleDiff(m.yaw, m.moveYaw) < 120 && !m.backwards, `t=${frames[k].t.toFixed(2)} yaw ${m.yaw} move ${m.moveYaw}`);
  }
  assert.ok(rear.slice(last + 1).some((m) => m?.backwards), 'back at his place he walks backwards again (the rear guard)');
  // The pace: with the turn round he keeps up (v2.5.1: p95 203 cm).
  const s = runSeries({ layout, config: OFF, members: [4], seeds: 1, seedBase: 2 });
  assert.ok(s.runs[0].pace!.p95 <= 150, `p95 ${s.runs[0].pace!.p95}`);
  const without = runSeries({ layout, config: { ...OFF, 'move.rear_turn_lag': 400, 'move.rear_turn_back': 200 }, members: [4], seeds: 1, seedBase: 2 });
  assert.ok(without.runs[0].pace!.p95 > s.runs[0].pace!.p95);
});

test('v2.6 Q2: a team-mate on his way delays the turn (no head-on overtaking); once turned he stays turned', () => {
  // His slot 4 m ahead; a mate 2 m ahead on the line (within slot.min_spacing + REAR_WAY_MARGIN): blocked; 1.15 m aside or behind him: clear.
  assert.equal(FormationSectorRules.RearWayBlocked([0, 0], [400, 0], [[200, 0]], C), true);
  assert.equal(FormationSectorRules.RearWayBlocked([0, 0], [400, 0], [[200, 115]], C), false);
  assert.equal(FormationSectorRules.RearWayBlocked([0, 0], [400, 0], [[-60, 0]], C), false, 'behind him');
  assert.equal(FormationSectorRules.RearCatchUp(250, false, true, C), false, 'blocked: he does not start the turn');
  assert.equal(FormationSectorRules.RearCatchUp(250, true, true, C), true, 'already turned: he keeps facing forwards');
  assert.equal(FormationSectorRules.RearCatchUp(250, false, false, C), true);
});

test('v2.6 Q2: red team 4_3 / 8_2 - a manual hedgehog re-forming after a corridor: no head-on overtaking (check 5 green)', () => {
  for (const [file, choice, n] of [['4_3', 'hedgehog', 3], ['8_2', 'hedgehog', 3], ['8_2', 'blizzard', 4]] as const) {
    const layout = readLayout(`${REDTEAM_DIR}/${file}.json`);
    const s = runSeries({ layout, config: C, members: [n], seeds: 2, seedBase: 1, manual: choice });
    const line = s.summary.checks.find((c) => c.id === 5)!;
    assert.equal(line.fail, 0, `${file} ${choice} n${n}: ${line.worst}`);
  }
});
