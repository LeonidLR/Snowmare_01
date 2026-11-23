// Legacy stair repair (stairRepair.ts) on the accepted X4 layout: the PmcTraversalSmoke class (b) failures of 2026-11-23
// (docs/port/gemini/outbox/100-x4-stairs-diagnosis.report.md section 4) are the validator's gap_overshoot / stair_in_other_building
// codes of the pre-2026-11-18 file; the repair clears them and moves nothing else.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { validateBuilding, validateBuildings } from './buildingTraversal.ts';
import { generateLayout } from './generate.ts';
import type { Profile } from './profile.ts';
import type { GenBuilding, GenLayout, GenRoute, Stair } from './schema.ts';
import { EXACT_GAP_HALF_W_CM, STAIR_REPAIR_CODES, STAIR_REPAIR_CODES_FULL, exactStairGapPoly, repairLegacyStairs } from './stairRepair.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const X4 = 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x4.json';
const loadX4 = (): GenLayout => JSON.parse(fs.readFileSync(path.join(ROOT, X4), 'utf8').replace(/^﻿/, '')) as GenLayout;
const bld = (l: GenLayout, id: string): GenBuilding => l.buildings.find((b) => b.id === id)!;
const codesOf = (l: GenLayout, id: string): string[] => validateBuilding(l, bld(l, id)).issues.map((i) => i.code);
const errorSet = (l: GenLayout): Set<string> => new Set(validateBuildings(l).buildings.flatMap((b) => b.issues.filter((i) => i.severity === 'error').map((i) => `${b.id}:${i.code}:${i.ref ?? ''}`)));
const BLOCKS = ['B_x1_8_t7_block', 'B_x1_16_t7_block', 'B_x2_25_t8_block'];

test('X4 as accepted: the three block flights 0 -> 1 have a slab gap past their top, B_hall_n_stair_0 stands in B_office_w', () => {
  const l = loadX4();
  for (const id of BLOCKS) assert.ok(codesOf(l, id).includes('gap_overshoot'), `${id}: gap_overshoot`);
  assert.ok(codesOf(l, 'B_hall_n').includes('stair_in_other_building'), 'B_hall_n: stair_in_other_building');
  // the smoke's evidence in numbers: B_x1_8's floor-1 gap is 80 cm wide and ends 40 cm past the flight's top (y -6771)
  const g = bld(l, 'B_x1_8_t7_block').floors_detail![0].gaps![0];
  assert.equal(Math.max(...g.poly.map((p) => p[1])), -6731);
  assert.equal(Math.max(...g.poly.map((p) => p[0])) - Math.min(...g.poly.map((p) => p[0])), 80);
});

test('X4 repaired: no navmesh-breaking stair code left, no new error, the passing flights stay where they were', () => {
  const l = loadX4();
  const before = JSON.stringify(l);
  const { layout: r, changes } = repairLegacyStairs(l);
  assert.equal(JSON.stringify(l), before, 'the input layout is not mutated');
  for (const b of validateBuildings(r).buildings) {
    const left = b.issues.filter((i) => STAIR_REPAIR_CODES.has(i.code));
    assert.deepEqual(left.map((i) => i.message), [], `${b.id}: no repaired code left`);
  }
  const was = errorSet(l);
  for (const e of errorSet(r)) assert.ok(was.has(e), `no new error: ${e}`);
  // the blocks: the gap is exactly the flight 0 -> 1 (ends at its top, 160 cm wide), the flights do not move
  for (const id of BLOCKS) {
    const b0 = bld(l, id), b1 = bld(r, id);
    assert.deepEqual(b1.stairs, b0.stairs, `${id}: flights unchanged`);
    const st = b1.stairs.find((s) => s.from === 0)!;
    assert.deepEqual(b1.floors_detail![0].gaps![0].poly, exactStairGapPoly(st), `${id}: exact gap`);
    assert.equal(Math.max(...b1.floors_detail![0].gaps![0].poly.map((p) => p[1])), st.top[1], `${id}: the gap ends at the top`);
  }
  // B_hall_n: the flight slid out of the office pocket, >= 100 cm of hall floor in front of its foot, the run unchanged
  const h0 = bld(l, 'B_hall_n').stairs[0], h1 = bld(r, 'B_hall_n').stairs[0];
  const officeN = bld(l, 'B_office_w').footprint[1][1];
  assert.ok(h1.pos[1] - 100 >= officeN, `B_hall_n_stair_0 foot ${h1.pos[1]} keeps 100 cm clear of B_office_w (y ${officeN})`);
  assert.equal(h1.pos[0], h0.pos[0]);
  assert.equal(h1.top[1] - h1.pos[1], h0.top[1] - h0.pos[1], 'same run and direction');
  assert.ok(h1.top[1] + 200 <= bld(l, 'B_hall_n').footprint[1][1], 'the roof landing stays inside the hall');
  // only the broken buildings changed (13 passing flights keep their place, the class (a) STAIR_in_8 gets its exact gap)
  const touched = new Set(changes.map((c) => c.building));
  assert.deepEqual([...touched].sort(), ['B_hall_n', 'B_ruin', ...BLOCKS].sort());
  for (const b of l.buildings) if (!touched.has(b.id)) assert.equal(JSON.stringify(bld(r, b.id)), JSON.stringify(b), `${b.id} untouched`);
  assert.ok(!changes.some((c) => c.kind === 'unresolved'), 'nothing unresolved');
});

test('repair is idempotent and a current layout is left alone', () => {
  const once = repairLegacyStairs(loadX4()).layout;
  const twice = repairLegacyStairs(once);
  assert.deepEqual(twice.changes, []);
  assert.equal(JSON.stringify(twice.layout), JSON.stringify(once));
  // today's generator already follows the rules (exact gaps 2026-11-18, the core clear of neighbours): nothing to repair
  const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
  const fresh = generateLayout({ profile, seed: 1, params: loadX4().params });
  const r = repairLegacyStairs(fresh);
  assert.deepEqual(r.changes, []);
  assert.equal(r.layout, fresh);
});

test('route points linked via a moved flight follow it', () => {
  const l = loadX4();
  const h = bld(l, 'B_hall_n').stairs[0];
  const route = { ...l.routes[0], id: 'R_probe', mandatory: false, points: [[5000, -2000], h.pos, h.top, [4000, 0]], links: [{ at: 2, via: h.id }] } as GenRoute;
  const withRoute: GenLayout = { ...l, routes: [...l.routes, route] };
  const r = repairLegacyStairs(withRoute).layout;
  const moved = bld(r, 'B_hall_n').stairs[0];
  const p = r.routes.find((x) => x.id === 'R_probe')!.points;
  assert.deepEqual(p, [[5000, -2000], moved.pos, moved.top, [4000, 0]]);
});

test('full: the 200 cm approach boxes too (every legacy flight slides), still no new error', () => {
  const l = loadX4();
  const { layout: r } = repairLegacyStairs(l, { full: true });
  for (const b of validateBuildings(r).buildings) {
    assert.deepEqual(b.issues.filter((i) => STAIR_REPAIR_CODES_FULL.has(i.code)).map((i) => i.message), [], b.id);
  }
  const was = errorSet(l);
  for (const e of errorSet(r)) assert.ok(was.has(e), `no new error: ${e}`);
});

test('exact gap: foot -> top along the flight, the flight width + 5 cm each side across', () => {
  const st: Stair = { id: 's', pos: [100, 0], top: [529, 0], from: 0, to: 1, kind: 'stairwell' };
  assert.deepEqual(exactStairGapPoly(st), [[100, -EXACT_GAP_HALF_W_CM], [529, -EXACT_GAP_HALF_W_CM], [529, EXACT_GAP_HALF_W_CM], [100, EXACT_GAP_HALF_W_CM]]);
  assert.equal(EXACT_GAP_HALF_W_CM, 150 / 2 + 5);
});
