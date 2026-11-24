import { test } from 'node:test';
import assert from 'node:assert/strict';
import { bayPlanAscii, planBaySection, type BayPlan } from './bayPlan.ts';
import { BAY_SECTIONS } from './kitPlan.ts';
import { bayStairGeom } from './bayStair.ts';

const isOverlap = (a: BayPlan['cells'][number], b: BayPlan['cells'][number]) =>
  Math.max(a.u0, b.u0) < Math.min(a.u1, b.u1) && Math.max(a.v0, b.v0) < Math.min(a.v1, b.v1);

for (const [id, length, bedrooms] of [
  ['block_1_464', 2600, 1], ['block_1_464_short', 2250, 0],
] as const) {
  test(`${id}: fixed rooms tile the section and reach all four landing doors`, () => {
    const plan = planBaySection(id);
    assert.ok(plan);
    assert.deepEqual(plan, planBaySection(id));
    assert.equal(plan.length, length);
    assert.equal(plan.depth, 1400);

    let area = 0;
    for (const [index, cell] of plan.cells.entries()) {
      assert.ok(0 <= cell.u0 && cell.u0 < cell.u1 && cell.u1 <= length, cell.id);
      assert.ok(0 <= cell.v0 && cell.v0 < cell.v1 && cell.v1 <= plan.depth, cell.id);
      assert.ok(!plan.cells.slice(index + 1).some((other) => isOverlap(cell, other)), cell.id);
      area += (cell.u1 - cell.u0) * (cell.v1 - cell.v0);
    }
    assert.equal(area, length * plan.depth);
    assert.equal(plan.cells.filter((cell) => cell.room === 'bedroom').length, bedrooms);
    const bedroom = plan.cells.find((cell) => cell.room === 'bedroom');
    if (bedroom) {
      assert.equal(bedroom.flat, 2);
      assert.equal(bedroom.u1 - bedroom.u0 - (BAY_SECTIONS.walls_cm.bearing + BAY_SECTIONS.walls_cm.exterior) / 2,
        BAY_SECTIONS.rooms.bedroom.clear_width_cm);
    }
    for (const living of plan.cells.filter((cell) => cell.room === 'living')) {
      assert.equal(living.u1 - living.u0, 450, living.id);
    }
    for (const cell of plan.cells.filter((item) => item.room === 'hall')) {
      assert.ok(cell.v1 - cell.v0 - BAY_SECTIONS.walls_cm.bearing >= BAY_SECTIONS.corridor_clear_min_cm, cell.id);
    }

    const byId = new Map(plan.cells.map((cell) => [cell.id, cell]));
    for (const door of plan.doors) {
      const a = byId.get(door.a)!;
      const b = byId.get(door.b)!;
      const width = door.role === 'service' ? BAY_SECTIONS.doors_cm.bath : BAY_SECTIONS.doors_cm[door.role];
      const [min, max] = door.axis === 'v' ? [Math.max(a.v0, b.v0), Math.min(a.v1, b.v1)] : [Math.max(a.u0, b.u0), Math.min(a.u1, b.u1)];
      assert.ok(min <= door.centre - width / 2 && door.centre + width / 2 <= max, `${door.a} -> ${door.b}`);
    }
    for (const room of plan.cells.filter((cell) => cell.flat && cell.room !== 'hall')) {
      assert.ok(plan.doors.some((door) => door.a === `F${room.flat}-hall` && door.b === room.id), room.id);
    }

    const entries = plan.doors.filter((door) => door.role === 'flat');
    assert.equal(entries.length, 4);
    const [left, right] = [plan.landing.u0, plan.landing.u1].map((u) =>
      entries.filter((door) => door.at === u).map((door) => door.centre).sort((a, b) => a - b));
    for (const pair of [left, right]) {
      assert.equal(pair.length, 2);
      assert.equal(pair[1] - pair[0], BAY_SECTIONS.flat_door_spacing_cm);
      assert.ok(pair[1] - pair[0] - BAY_SECTIONS.doors_cm.flat >= BAY_SECTIONS.door_jamb_clear_min_cm);
    }
    assert.notEqual(left[0], right[0]);
    assert.notEqual(left[1], right[1]);
    const [header, ...rows] = bayPlanAscii(plan).split('\n');
    assert.match(header, /court at top/);
    assert.equal(rows.length, 28);
    assert.ok(rows.every((row) => row.length === length / 50 && !row.includes(' ')));
  });
}

test('legacy and unknown templates leave the bay planner unselected', () => {
  assert.equal(planBaySection('block_khrushchevka'), undefined);
  assert.equal(planBaySection('unknown'), undefined);
});

test('the two 205 cm lanes turn at the court and leave the spine landing clear', () => {
  const p = planBaySection('block_1_464')!;
  const g = bayStairGeom(p);
  const { core, walls_cm: walls } = BAY_SECTIONS;
  const halfWidth = core.flight_clear_cm / 2;
  assert.equal(g.uIn[1] - g.uIn[0], core.floor_landing_clear_cm[0]);
  assert.ok(g.lane[0] - halfWidth >= g.uIn[0]);
  assert.ok(g.lane[1] + halfWidth <= g.uIn[1]);
  assert.equal(g.lane[1] - g.lane[0] - 2 * halfWidth, walls.partition);
  assert.equal(g.vLanding, p.landing.v0);
  assert.ok(g.vFar < g.vMid && g.vMid < g.vLanding);
  assert.equal(g.vLanding - g.vMid, g.run);
  assert.ok(Math.abs(Math.atan(150 / g.run) * 180 / Math.PI - core.slope_deg) < 0.15);
  assert.ok(g.vMid - g.vFar >= 195, 'court half-landing leaves a full passage');
  assert.equal(p.landing.v1 - p.landing.v0 - walls.bearing, core.floor_landing_clear_cm[1]);
  assert.equal(p.landing.v1 - walls.bearing / 2 - p.spine, core.landing_past_spine_cm);
});
