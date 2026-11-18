// Building traversal validator (BUILDING_GENERATOR.md section 5 layer 1) + the switchback stair core (4.2, causes A / B).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { validateBuilding, validateBuildings } from './buildingTraversal.ts';
import { expandLayout } from './expand.ts';
import { generateLayout, planStairCore, STAIR_APPROACH, STAIR_FLIGHT_W } from './generate.ts';
import type { Profile } from './profile.ts';
import type { GenBuilding, GenLayout } from './schema.ts';
import { LEVEL_HEIGHT, STAIR_RUN_PER_RISE } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;

test('switchback core: one storey per flight, lanes alternate, free approach boxes, gaps end at the tops', () => {
  const core = planStairCore('B_t', [[0, 0], [10, 16]], 3)!;
  assert.ok(core, 'a 10 x 16 m block fits a 3-flight core');
  assert.deepEqual(core.stairs.map((s) => [s.from, s.to]), [[0, 1], [1, 2], [2, 3]]);
  const run = STAIR_RUN_PER_RISE * (LEVEL_HEIGHT / 100);
  for (const s of core.stairs) assert.ok(Math.abs(Math.hypot(s.top[0] - s.pos[0], s.top[1] - s.pos[1]) - run) < 1e-6, `${s.id} run`);
  // flight n+1 starts where flight n arrives, on the other lane
  for (let n = 0; n + 1 < core.stairs.length; n++) {
    const a = core.stairs[n], b = core.stairs[n + 1];
    assert.ok(Math.abs(a.top[1] - b.pos[1]) < 1e-6, 'the next flight starts at the same along-position');
    assert.ok(Math.abs(Math.abs(a.top[0] - b.pos[0]) - 1.6) < 1e-6, 'on the neighbouring lane');
  }
  // the gap of a flight is exactly its footprint (no overshoot past the top)
  const g = core.flightRect(core.stairs[0]);
  assert.ok(Math.abs(g[1][1] - core.stairs[0].top[1]) < 1e-6, 'the gap ends at the top');
  assert.ok(g[1][0] - g[0][0] <= STAIR_FLIGHT_W + 0.1 + 1e-6, 'as wide as the flight');
  // foot >= STAIR_APPROACH from the end wall: never a corner pocket
  assert.ok(core.stairs[0].pos[1] >= STAIR_APPROACH, 'the first foot leaves a free approach box');
  assert.equal(planStairCore('B_s', [[0, 0], [6, 6]], 1), null, 'a 6 x 6 m shed is too short for a 35-degree flight + 2 approach boxes');
});

test('the generated depot (base and x6) has no building that fails traversal', () => {
  for (const seed of [1, 2]) {
    const base = generateLayout({ profile, seed });
    for (const l of [base, expandLayout({ layout: base, factor: 6, seed: seed + 10, profile }).layout]) {
      const r = validateBuildings(l);
      const bad = r.buildings.filter((b) => !b.ok);
      assert.equal(bad.length, 0, `seed ${seed}: ${bad.map((b) => `${b.id}: ${b.issues.map((i) => i.message).join('; ')}`).join(' || ')}`);
      // the halls reach their roof by stairs (no ladder needed)
      for (const b of r.buildings.filter((x) => x.set_piece === 'industrial_hall' || x.set_piece === 'warehouse')) {
        const hb = l.buildings.find((x) => x.id === b.id)!;
        if (hb.stairs.some((s) => s.kind === 'stairwell')) assert.equal(b.roof, 'reachable', `${b.id} roof by stairs`);
      }
    }
  }
});

// A minimal two-storey building in UE cm (axis-aligned), one stairwell flight 0 -> 1 placed by hand.
function twoStorey(over: Partial<GenBuilding> = {}): { l: GenLayout; b: GenBuilding } {
  const b = {
    id: 'B_x', footprint: [[0, 0], [1000, 1600]], height: 600, floors: 2, enterable: true, roof_walkable: false, set_piece: 'facade',
    doors: [{ pos: [500, 1600], floor: 0 }], openings: [{ id: 'd0', kind: 'door', pos: [500, 1600], floor: 0, width: 120 }],
    partitions: [], hatches: [], parapets: [], mantle_edges: [], roof_holes: [], floor_holes: [],
    rooms: [{ id: 'r0', floor: 0, bounds: { min: [0, 0], max: [1000, 1600] }, kind: 'room', door_ids: ['d0'] },
      { id: 'r1', floor: 1, bounds: { min: [0, 0], max: [1000, 1600] }, kind: 'room' }],
    stairs: [{ id: 'st', pos: [100, 300], from: 0, to: 1, kind: 'stairwell', top: [100, 729] }],
    floors_detail: [{ floor: 1, height_z: 300, gaps: [{ id: 'g', kind: 'stairwell', poly: [[20, 300], [180, 300], [180, 729], [20, 729]] }], rooms: [] }],
    ...over,
  } as unknown as GenBuilding;
  return { l: { buildings: [b], walls: [], ladders: [] } as unknown as GenLayout, b };
}

test('validator: a clean flight passes; cause A (gap past the top) and cause B (corner pocket) are caught', () => {
  const ok = twoStorey();
  assert.deepEqual(validateBuilding(ok.l, ok.b).issues.map((i) => i.code), []);
  const a = twoStorey({ floors_detail: [{ floor: 1, height_z: 300, gaps: [{ id: 'g', kind: 'stairwell', poly: [[60, 260], [140, 260], [140, 769], [60, 769]] }], rooms: [] }] } as Partial<GenBuilding>);
  assert.ok(validateBuilding(a.l, a.b).issues.some((i) => i.code === 'gap_overshoot'), 'the 40 cm overshoot of the old generator');
  const bPocket = twoStorey({ stairs: [{ id: 'st', pos: [100, 120], from: 0, to: 1, kind: 'stairwell', top: [100, 549] }] } as Partial<GenBuilding>);
  assert.ok(validateBuilding(bPocket.l, bPocket.b).issues.some((i) => i.code === 'stair_foot_no_floor'), 'a foot 120 cm from the end wall has no approach box');
  const steep = twoStorey({ stairs: [{ id: 'st', pos: [100, 300], from: 0, to: 1, kind: 'stairwell', top: [100, 500] }] } as Partial<GenBuilding>);
  assert.ok(validateBuilding(steep.l, steep.b).issues.some((i) => i.code === 'stair_steep'));
  const noStair = twoStorey({ stairs: [] } as Partial<GenBuilding>);
  assert.ok(validateBuilding(noStair.l, noStair.b).issues.some((i) => i.code === 'unreachable_level'), 'storey 1 without a way up');
});

test('one source of truth: builder STAIR_W and head-room hole match the generator flight width and gap margin', () => {
  const py = fs.readFileSync(path.join(ROOT, 'Scripts/Editor/build_levelgen_map.py'), 'utf8');
  const stairW = Number(/^STAIR_W = ([0-9.]+)/m.exec(py)?.[1]);
  assert.equal(stairW, STAIR_FLIGHT_W * 100, 'build_levelgen_map.py STAIR_W (cm) == generate.ts STAIR_FLIGHT_W (m) x 100');
  assert.match(py, /hw = STAIR_W \/ 2\.0 \+ 5\.0/, 'the hole margin (5 cm) == the gap margin of StairCore.flightRect (0.05 m)');
  const core = planStairCore('B_m', [[0, 0], [10, 16]], 1)!;
  const g = core.flightRect(core.stairs[0]);
  assert.ok(Math.abs((g[1][0] - g[0][0]) - (STAIR_FLIGHT_W + 0.1)) < 1e-9, 'flightRect = flight + 5 cm each side');
});

test('the audit is reproduced on the accepted pre-fix X6 / X4 layouts (skipped once they are regenerated)', (t) => {
  for (const name of ['gen_ref_01_02_depot_1_x6', 'gen_ref_01_02_depot_x4']) {
    const p = path.join(ROOT, 'Content/Data/LevelGen/accepted', `${name}.json`);
    if (!fs.existsSync(p)) { t.diagnostic(`${name}: not in this checkout`); continue; }
    const l = JSON.parse(fs.readFileSync(p, 'utf8')) as GenLayout;
    // a pre-fix layout still has a hall flight over several storeys (the 13 m ramp of the 2026-11-18 audit)
    if (!l.buildings.some((b) => b.stairs.some((s) => s.kind === 'stairwell' && s.to - s.from > 1))) { t.diagnostic(`${name}: regenerated, skipped`); continue; }
    const r = validateBuildings(l);
    const codes = new Set(r.buildings.flatMap((b) => b.issues.map((i) => i.code)));
    t.diagnostic(`${name}: ${r.failed} of ${r.checked} buildings fail traversal`);
    assert.ok(r.failed > 0, `${name}: the pre-fix layout must fail`);
    assert.ok(codes.has('stair_foot_no_floor') && codes.has('gap_overshoot'), `${name}: causes B and A are both found (${[...codes].join(', ')})`);
  }
});
