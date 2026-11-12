import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { rng } from '../movement/geometry.ts';
import type { Draft } from './generate.ts';
import { addBuilding, finalizeDraftToLayout, generateLayout } from './generate.ts';
import type { Profile } from './profile.ts';
import { DEFAULT_PARAMS, LEVEL_HEIGHT, OPENING_DEFAULTS, isMantleable, validateGenLayout } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;

test('generated depot layouts include windows, rooms, partitions, parapets, hatches, and mantle edges', () => {
  const l = generateLayout({ profile, seed: 1 });
  assert.deepEqual(validateGenLayout(l), []);

  // Windows on facades + matching mantle edges
  const allWindows = l.buildings.flatMap((b) => (b.openings ?? []).filter((o) => o.kind === 'window'));
  assert.ok(allWindows.length >= 10, `expected >= 10 windows across depot facades, got ${allWindows.length}`);
  for (const w of allWindows) {
    assert.equal(w.width, OPENING_DEFAULTS.window.width);
    assert.equal(w.sill_height, OPENING_DEFAULTS.window.sill);
    assert.equal(w.top_height, OPENING_DEFAULTS.window.top);
  }

  // Interior BSP room partitions in B_office_s and B_shed and B_ruin
  const office = l.buildings.find((b) => b.id === 'B_office_s')!;
  assert.ok((office.partitions?.length ?? 0) >= 1, 'B_office_s must be partitioned into rooms');
  assert.ok((office.rooms?.length ?? 0) >= 2, 'B_office_s must have >= 2 rooms');
  for (const r of office.rooms ?? []) {
    assert.ok(r.bounds.min[0] < r.bounds.max[0] && r.bounds.min[1] < r.bounds.max[1]);
    assert.ok((r.door_ids?.length ?? 0) >= 1, `room ${r.id} must have at least one door_id`);
  }

  const ruin = l.buildings.find((b) => b.id === 'B_ruin')!;
  assert.equal(ruin.partitions?.length, 2, 'B_ruin has 1 partition per floor (2 floors)');
  assert.equal(ruin.rooms?.length, 4, 'B_ruin has 2 rooms per floor (4 total)');
  assert.ok((ruin.floors_detail?.length ?? 0) >= 1, 'B_ruin has floor 1 floors_detail');
  assert.ok((ruin.floors_detail?.[0].gaps?.length ?? 0) >= 1, 'B_ruin floor 1 tracks gaps (stair/floor_hole)');

  // Parapets on flat walkable roofs + parapet mantle edges
  for (const b of l.buildings.filter((x) => x.roof_walkable)) {
    assert.equal(b.parapets?.length, 4, `${b.id}: 4 roof parapet segments`);
    for (const p of b.parapets ?? []) {
      assert.equal(p.height_cm, 100);
    }
    const paraMantles = (b.mantle_edges ?? []).filter((me) => me.kind === 'parapet');
    assert.equal(paraMantles.length, 4, `${b.id}: 4 parapet mantle edges`);
  }

  // Roof hatches + hatch ladder linkage
  const hall = l.buildings.find((b) => b.id === 'B_hall_n')!;
  assert.equal(hall.hatches?.length, 1, 'B_hall_n has roof hatch');
  const hatch = hall.hatches![0];
  assert.ok(hatch.ladder_id, 'hatch links to a ladder');
  assert.ok(l.ladders.some((ld) => ld.id === hatch.ladder_id && ld.to === `roof:${hall.id}`));

  // Closed facade & chimneys have no enterable rooms
  const officeW = l.buildings.find((b) => b.id === 'B_office_w')!;
  assert.equal(officeW.rooms?.length ?? 0, 0, 'closed facade B_office_w has no rooms');
  for (const ch of l.buildings.filter((b) => b.set_piece === 'chimney')) {
    assert.equal(ch.rooms?.length ?? 0, 0, 'chimney has no rooms');
  }

  // Global mantle edges validated 50..150 cm
  assert.ok((l.mantle_edges?.length ?? 0) >= 20, 'layout aggregates mantle edges');
  for (const me of l.mantle_edges ?? []) {
    assert.ok(isMantleable(me.height_cm), `mantle edge ${me.id} height ${me.height_cm} in 50..150 cm`);
  }
});

test('addBuilding supports fortified checkpoint and multi-storey partitioned bunker specs', () => {
  const random = rng(42);
  const d: Draft = {
    walls: [], buildings: [], ladders: [], covers: [], zones: [],
    routes: [{ id: 'R_main', pace: 'walk', points: [[4, 15], [36, 15]], mandatory: true }],
    spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [],
  };

  addBuilding(d, 'B_cp', [[10, 4], [24, 12]], {
    height: 2 * LEVEL_HEIGHT,
    floors: 2,
    enterable: true,
    roof_walkable: true,
    stairwell: true,
    partitionRooms: true,
    roofHatch: true,
    set_piece: 'checkpoint',
    doorSides: ['south'],
    windowSides: ['north'],
    loopholeSides: ['south', 'east', 'west'],
  }, random);

  const layout = finalizeDraftToLayout(d, [40, 30], {
    name: 'test_fortified',
    profile: 'test',
    seed: 42,
    params: { ...DEFAULT_PARAMS, size_m: [40, 30] },
    start: [4, 15],
    end: [36, 15],
  });

  assert.deepEqual(validateGenLayout(layout), []);
  const cp = layout.buildings.find((b) => b.id === 'B_cp')!;
  const loopholes = (cp.openings ?? []).filter((o) => o.kind === 'loophole');
  assert.ok(loopholes.length >= 4, `checkpoint must emit loopholes, got ${loopholes.length}`);
  for (const lp of loopholes) {
    assert.equal(lp.width, OPENING_DEFAULTS.loophole.width);
    assert.equal(lp.sill_height, OPENING_DEFAULTS.loophole.sill);
    assert.equal(lp.top_height, OPENING_DEFAULTS.loophole.top);
  }
  assert.ok((cp.partitions?.length ?? 0) >= 2, '2 storeys => >= 2 partitions');
  assert.ok((cp.rooms?.length ?? 0) >= 4, '2 storeys x >= 2 rooms');
  assert.equal(cp.floors_detail?.length, 1, 'floor 1 detail present');
  assert.equal(cp.hatches?.length, 1, 'roof hatch present');
  assert.equal(cp.hatches?.[0].floor, 1, 'hatch accessed from top floor 1');
});
