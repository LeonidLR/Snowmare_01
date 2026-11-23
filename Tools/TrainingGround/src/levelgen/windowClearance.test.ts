// Window clearances (BUILDING_GENERATOR.md 3 step 7 / 4.4, S2 planning pass bugs): no window onto the stair core, no partition end
// in a window's 60 cm jamb - the shared geometry (windowClearance.ts), the generator's step 6c and the validator warnings.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { TRAVERSAL, TRAVERSAL_SEVERITY, traversalOk, validateBuildings } from './buildingTraversal.ts';
import { expandLayout } from './expand.ts';
import { generateLayout, STAIR_FLIGHT_W } from './generate.ts';
import type { Profile } from './profile.ts';
import type { GenLayout } from './schema.ts';
import type { Box } from './windowClearance.ts';
import { CORE_FLIGHT_W_CM, WINDOW_JAMB_CM, windowOntoCore, windowPartitionClash, windowProbe } from './windowClearance.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
const NEW_CODES = new Set(['window_onto_stair', 'window_partition_clash']);

const countCodes = (l: GenLayout) => {
  const issues = validateBuildings(l).buildings.flatMap((b) => b.issues);
  return { onto: issues.filter((i) => i.code === 'window_onto_stair').length, clash: issues.filter((i) => i.code === 'window_partition_clash').length };
};

test('window clearance geometry: a window beside a flight / its approach is onto the core, the far wall is not; the 60 cm jamb', () => {
  const fp: Box = [[0, 0], [1000, 1600]]; // cm; a flight 0 -> 1 along the west wall, lane 20..180 cm
  const flights = [{ pos: [100, 300], top: [100, 729], from: 0, to: 1 }];
  assert.deepEqual(windowProbe(fp, [0, 500], 120, 1), [[0, 440], [50, 560]], 'the probe: 50 cm behind the wall, as wide as the window');
  assert.equal(windowOntoCore(fp, [0, 500], 120, 0, flights, [], 1), true, 'beside the run');
  assert.equal(windowOntoCore(fp, [0, 200], 120, 0, flights, [], 1), true, 'beside the foot approach box');
  assert.equal(windowOntoCore(fp, [0, 500], 120, 1, flights, [], 1), true, 'storey 1: beside the slab gap');
  assert.equal(windowOntoCore(fp, [0, 500], 120, 2, flights, [], 1), false, 'storey 2: no flight there');
  assert.equal(windowOntoCore(fp, [1000, 500], 120, 0, flights, [], 1), false, 'the far wall');
  assert.equal(windowOntoCore(fp, [0, 1300], 120, 0, flights, [], 1), false, 'past the landing (729 + 200)');
  // metres (the generator) give the same answer
  assert.equal(windowOntoCore([[0, 0], [10, 16]], [0, 5], 1.2, 0, [{ pos: [1, 3], top: [1, 7.29], from: 0, to: 1 }], [], 0.01), true);
  // a partition end on the south wall at x = 500 (20 cm thick): clear from 60 + 60 + 10 = 130 cm off the window centre
  const parts = [{ start: [500, 800], end: [500, 1600], thickness: 20 }];
  assert.equal(windowPartitionClash(fp, [500, 1600], 120, parts, 1), 0, 'the end inside the window');
  assert.equal(windowPartitionClash(fp, [629, 1600], 120, parts, 1), 0, 'a 59 cm jamb');
  assert.equal(windowPartitionClash(fp, [500 + 60 + 10 + WINDOW_JAMB_CM, 1600], 120, parts, 1), -1, 'a 60 cm jamb is enough');
  assert.equal(windowPartitionClash(fp, [500, 0], 120, parts, 1), -1, 'the end on the other wall does not count');
});

test('one flight width: windowClearance == generate.ts STAIR_FLIGHT_W == the validator', () => {
  assert.equal(CORE_FLIGHT_W_CM, STAIR_FLIGHT_W * 100);
  assert.equal(CORE_FLIGHT_W_CM, TRAVERSAL.flightWidth);
  assert.equal(TRAVERSAL_SEVERITY.window_onto_stair, 'warning');
  assert.equal(TRAVERSAL_SEVERITY.window_partition_clash, 'warning');
});

test('generator: depot seed 1 and the x4 s7 -> x6 s8 chain have no window onto a stair and no partition end in a window', () => {
  // before the fix (S2 planning pass, 2026-11-19): seed 1 2 / 0, x4 17 / 9, x6 25 / 20 (window_onto_stair / window_partition_clash)
  const base = generateLayout({ profile, seed: 1 });
  const x4 = expandLayout({ layout: base, factor: 4, seed: 7, profile }).layout;
  const x6 = expandLayout({ layout: x4, factor: 6, seed: 8, profile }).layout;
  for (const [name, l] of [['seed 1', base], ['x4 s7', x4], ['x6 s8', x6]] as const) {
    assert.deepEqual(countCodes(l), { onto: 0, clash: 0 }, name);
    assert.equal(validateBuildings(l).failed, 0, `${name}: dropping / sliding windows never fails a building`);
    // every window keeps its mantle edge at its own position
    for (const b of l.buildings) {
      for (const op of (b.openings ?? []).filter((o) => o.kind === 'window')) {
        const me = (b.mantle_edges ?? []).find((m) => m.id === `me_${op.id}`);
        if (!me) continue;
        assert.ok(Math.abs((me.start[0] + me.end[0]) / 2 - op.pos[0]) < 0.5 && Math.abs((me.start[1] + me.end[1]) / 2 - op.pos[1]) < 0.5, `${op.id}: mantle edge follows`);
      }
      const ids = new Set((b.openings ?? []).map((o) => o.id));
      for (const me of (b.mantle_edges ?? []).filter((m) => m.kind === 'window')) assert.ok(ids.has(me.id.slice(3)), `${me.id}: no orphan mantle edge`);
    }
  }
});

test('legacy accepted layouts: the new codes are warnings only - every building passes / fails as before; old files show them', (t) => {
  const dir = path.join(ROOT, 'Content/Data/LevelGen/accepted');
  let warned = 0;
  for (const f of fs.readdirSync(dir).filter((x) => x.endsWith('.json') && !x.endsWith('.meta.json'))) {
    const l = JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8')) as GenLayout;
    for (const b of validateBuildings(l).buildings) {
      assert.equal(b.ok, traversalOk(b.issues.filter((i) => !NEW_CODES.has(i.code))), `${f} ${b.id}`);
      warned += b.issues.filter((i) => NEW_CODES.has(i.code)).length;
    }
  }
  t.diagnostic(`window warnings on the accepted layouts: ${warned}`);
  const old = path.join(dir, 'gen_ref_01_02_depot_1_x6.json');
  if (fs.existsSync(old)) assert.ok(countCodes(JSON.parse(fs.readFileSync(old, 'utf8')) as GenLayout).clash > 0, 'the pre-fix X6 keeps showing its partition clashes');
});
