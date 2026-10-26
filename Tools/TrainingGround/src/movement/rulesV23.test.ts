// Training Ground / movement: v2.3 tests - the red-team rule fixes (FORMATION_CONTRACT.md 13). Coordinator task 2026-10-25:
// fix the rule bugs the red team (Content/Data/FormationLab/redteam) found, v2.2 behaviour the user approved unchanged.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { Segment } from './geometry.ts';
import { distToWall } from './geometry.ts';
import { configValues } from './config.ts';
import { readConfigFile, readLayout } from './cli/paths.ts';
import { runSeries } from './bot.ts';
import { FormationCoverRules, FormationGuardRules, FormationSectorRules, FormationShapeRules } from './rules.ts';

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

test('v2.3 (1A clarified): in a NARROW column K, P, L keep the user-decided looks; the allocator gives Z the rear and nothing else', () => {
  const D = FormationSectorRules.Decided, narrow = C['column.narrow_width'] - 1;
  assert.deepEqual((['K', 'P', 'L', 'Z'] as const).map((r) => D(r, 'column', 4, narrow, C)), [true, true, true, false]);
  assert.deepEqual((['K', 'P', 'Z'] as const).map((r) => D(r, 'column', 3, narrow, C)), [true, true, false]);
  assert.equal(D('P', 'column', 4, C['column.narrow_width'] + 1, C), false, 'a wide column: the allocator shares the arc');
  assert.equal(D('P', 'column', 4, null, C), false, 'no passage = wide');
  assert.equal(D('P', 'column', 2, narrow, C), false, 'the pair keeps LookYaw anyway (no allocator)');
  for (const f of ['line', 'diamond', 'echelon_right', 'wedge', 'wall'] as const) assert.equal(D('P', f, 4, narrow, C), false);
  // The allocator with those fixed looks in a 2 m corridor: P / L exactly as LookYaw, Z the rear guard at 6 o'clock (halt).
  const corridor: Segment[] = [{ a: [-3000, -100], b: [1000, -100], thickness: 20 }, { a: [-3000, 100], b: [1000, 100], thickness: 20 }];
  const slots = FormationShapeRules.Slots('column', 4, [0, 0], 0, C);
  const fixed = slots.filter((s) => D(s.role, 'column', 4, 200, C)).map((s) => ({ role: s.role, yaw: FormationSectorRules.LookYaw(s.role, 'column', 'halt', 4, 0, 0, 200, C) }));
  near(fixed.find((x) => x.role === 'P')!.yaw, C['column.narrow_fwd_deg']);
  near(fixed.find((x) => x.role === 'L')!.yaw, -C['column.narrow_back_deg']);
  const r = FormationCoverRules.Allocate(corridor, [0, 0], 0, slots.map((s) => ({ role: s.role, pos: s.pos, back: -s.pos[0] })), 'halt', 0, C, fixed)!;
  assert.equal(r.rearGuard, 'Z');
  for (const x of fixed) near(r.looks.find((l) => l.role === x.role)!.yaw, x.yaw);
  near(Math.abs(r.looks.find((l) => l.role === 'Z')!.yaw), 180);
});

test('v2.3 narrow tier: entering a corridor, a narrow arc he reaches without a sweep beats the wide one across the wall end (red team 8_5)', () => {
  // P 1.6 m before a 4 m corridor, 60 cm right of its axis; its right wall's end is ahead-right (about 59 deg, 1.6 m).
  const entrance: Segment[] = [{ a: [84, 140], b: [1484, 140], thickness: 40 }, { a: [84, -260], b: [2484, -260], thickness: 40 }];
  const got = L(entrance, [0, 0], 15, -25, C);
  assert.ok(got < 10 && got > -20, `the near (narrow) edge, not the far one across the wall end: ${got}`);
  assert.ok(L(entrance, [0, 0], 15, -25, { ...C, 'look.narrow_penalty': 0 }) > 100, 'without the tier: the far edge (the part-3 stare)');
  // A wide arc without a sweep still beats a narrow one (the sliver of 2_8).
  near(L(sliver, [0, 0], -100, 10, C), 10, 1e-6);
});

test('v2.3 sim: two unwalkable slots never get the same fallback point (red team 6_8: hedgehog of three in a 3 m corridor)', () => {
  const layout = readLayout('redteam/6_8');
  const s = runSeries({ layout, config: C, members: [3], seeds: 1, seedBase: 1, manual: 'hedgehog' }).summary;
  const spacing = s.checks.find((c) => c.id === 5)!;
  assert.equal(spacing.fail, 0, spacing.worst ?? '');
});

test('v2.3 guard: a member cut off from the leader -> the trail column at once, released by the usual hold (red team 4_3)', () => {
  const G = FormationGuardRules;
  const start = { requested: 'blizzard' as const, level: 0 as const, effective: 'blizzard' as const, candidate: null, candidateDistance: 0 };
  const open = G.Resolve([], 'blizzard', 4, [0, 0], 0, null, 0, start, 20, C);
  assert.equal(open.level, 0, 'nobody cut off: as requested');
  const cut = G.Resolve([], 'blizzard', 4, [0, 0], 0, null, 0, start, 20, C, false);
  assert.equal(cut.level, 2);
  assert.equal(cut.effective, 'column');
  // In sight again: back to the request only after classify.hold_distance of the leader's path.
  let st = G.Resolve([], 'blizzard', 4, [0, 0], 0, null, 0, cut, C['classify.hold_distance'] / 2, C);
  assert.equal(st.level, 2);
  st = G.Resolve([], 'blizzard', 4, [0, 0], 0, null, 0, st, C['classify.hold_distance'] / 2 + 1, C);
  assert.equal(st.level, 0);
  assert.equal(G.Resolve([], 'wall', 4, [0, 0], 0, null, 0, { ...start, requested: 'wall', effective: 'wall' }, 20, C, false).level, 0, 'the wall layout is never guarded');
});
