// Training Ground / levelgen: orphan fence cleanup and wall crossing validation (task 085/085b).
// Tests layoutValidator and regionRegen geometry rules for block fence removal on re-generation.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import type { Vec2 } from '../movement/geometry.ts';
import { segmentsIntersect, segmentSegmentIntersection } from '../movement/geometry.ts';
import { cleanConflictingFences, validateWallCrossings } from './layoutValidator.ts';
import { fenceClosedRing } from './regionRegen.ts';
import type { GenBuilding, GenLayout, GenWall } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');

// Helper to load X6 fixture or repo accepted file
function loadX6Layout(): GenLayout {
  const fixturePath = path.join(path.dirname(fileURLToPath(import.meta.url)), 'fixtures/depot_x6_orphan_fence.json');
  if (fs.existsSync(fixturePath)) {
    return JSON.parse(fs.readFileSync(fixturePath, 'utf8')) as GenLayout;
  }
  const acceptedPath = path.join(ROOT, 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x6.json');
  return JSON.parse(fs.readFileSync(acceptedPath, 'utf8')) as GenLayout;
}

test('validateWallCrossings on real accepted X6: finds exactly the 3 F_x1_8_t1003 conflicts', () => {
  const layout = loadX6Layout();
  const conflicts = validateWallCrossings(layout);

  // Exactly 3 crossing conflicts found on the real X6 layout
  assert.equal(conflicts.length, 3, `Expected exactly 3 conflicts, found ${conflicts.length}`);

  // All 3 conflicts must involve the orphan macro-fence family F_x1_8_t1003
  for (const c of conflicts) {
    assert.equal(c.kind, 'wall_wall');
    assert.ok(
      c.familyA === 'F_x1_8_t1003' || c.familyB === 'F_x1_8_t1003',
      `Conflict does not involve F_x1_8_t1003: ${c.familyA} vs ${c.familyB}`
    );
  }

  // Conflict 1: F_x1_8_t1003_S_0 vs F_x1_8_4_1_1_t1003_E_0
  const c1 = conflicts.find((c) => c.wallA.id === 'F_x1_8_t1003_S_0' && c.wallB?.id === 'F_x1_8_4_1_1_t1003_E_0');
  assert.ok(c1, 'Expected crossing between F_x1_8_t1003_S_0 and F_x1_8_4_1_1_t1003_E_0');
  assert.ok(Math.abs(c1.point[0] - 7993.59) < 1, `Point X mismatch: ${c1.point[0]}`);
  assert.ok(Math.abs(c1.point[1] - 16335.92) < 1, `Point Y mismatch: ${c1.point[1]}`);

  // Conflict 2: F_x1_8_t1003_E_0 vs F_x1_8_5_1_1_t1003_N_0
  const c2 = conflicts.find((c) => c.wallA.id === 'F_x1_8_t1003_E_0' && c.wallB?.id === 'F_x1_8_5_1_1_t1003_N_0');
  assert.ok(c2, 'Expected crossing between F_x1_8_t1003_E_0 and F_x1_8_5_1_1_t1003_N_0');
  assert.ok(Math.abs(c2.point[0] - 9508.62) < 1, `Point X mismatch: ${c2.point[0]}`);
  assert.ok(Math.abs(c2.point[1] - 17009.24) < 1, `Point Y mismatch: ${c2.point[1]}`);

  // Conflict 3: F_x1_8_t1003_E_0 vs F_x1_8_5_1_1_t1003_W_0
  const c3 = conflicts.find((c) => c.wallA.id === 'F_x1_8_t1003_E_0' && c.wallB?.id === 'F_x1_8_5_1_1_t1003_W_0');
  assert.ok(c3, 'Expected crossing between F_x1_8_t1003_E_0 and F_x1_8_5_1_1_t1003_W_0');
  assert.ok(Math.abs(c3.point[0] - 9089.37) < 1, `Point X mismatch: ${c3.point[0]}`);
  assert.ok(Math.abs(c3.point[1] - 16905.49) < 1, `Point Y mismatch: ${c3.point[1]}`);
});

test('cleanConflictingFences removes F_x1_8_t1003 and leaves 0 conflicts on X6', () => {
  const layout = loadX6Layout();
  const cleanResult = cleanConflictingFences(layout);

  // Successfully identified and removed the orphan family
  assert.deepEqual(cleanResult.removedFamilies, ['F_x1_8_t1003']);
  assert.equal(cleanResult.removedWalls.length, 6);
  assert.ok(cleanResult.removedWalls.every((id) => id.startsWith('F_x1_8_t1003_')));

  // Remaining layout has 0 crossing conflicts
  const remaining = validateWallCrossings(cleanResult.layout);
  assert.equal(remaining.length, 0, `Expected 0 remaining conflicts, found ${remaining.length}`);
});

test('geometry check with real X6 numbers: parent fence centre in alley gap', () => {
  // Real coordinates of F_x1_8_t1003 walls from gen_ref_01_02_depot_1_x6.json
  const parentFenceWalls: GenWall[] = [
    { id: 'F_x1_8_t1003_N_0', a: [11357.14, 16631.24], b: [11162.32, 17418.49], height: 135, thickness: 10, kind: 'fence' },
    { id: 'F_x1_8_t1003_S_0', a: [8118.83, 15829.84], b: [7924.01, 16617.09], height: 135, thickness: 10, kind: 'fence' },
    { id: 'F_x1_8_t1003_W_0', a: [11357.14, 16631.24], b: [10531.06, 16426.8], height: 135, thickness: 10, kind: 'fence' },
    { id: 'F_x1_8_t1003_W_1', a: [10239.85, 16354.73], b: [9236.13, 16106.34], height: 135, thickness: 10, kind: 'fence' },
    { id: 'F_x1_8_t1003_W_2', a: [8944.91, 16034.27], b: [8118.83, 15829.84], height: 135, thickness: 10, kind: 'fence' },
    { id: 'F_x1_8_t1003_E_0', a: [11162.32, 17418.49], b: [7924.01, 16617.09], height: 135, thickness: 10, kind: 'fence' },
  ];

  // Centre of the parent fence bounding box
  const minX = Math.min(...parentFenceWalls.flatMap((w) => [w.a[0], w.b[0]]));
  const maxX = Math.max(...parentFenceWalls.flatMap((w) => [w.a[0], w.b[0]]));
  const minY = Math.min(...parentFenceWalls.flatMap((w) => [w.a[1], w.b[1]]));
  const maxY = Math.max(...parentFenceWalls.flatMap((w) => [w.a[1], w.b[1]]));
  const centre: Vec2 = [(minX + maxX) / 2, (minY + maxY) / 2];

  // The centre is [9640.58, 16624.16]
  assert.ok(Math.abs(centre[0] - 9640.58) < 1);
  assert.ok(Math.abs(centre[1] - 16624.16) < 1);

  // Subdivided parcels in X6: LOT_x1_8_4_1_1 (Y: 15638..16461) and LOT_x1_8_5_1_1 (Y: 16672..17600)
  // The gap between 16461 and 16672 is 211 cm wide
  const lot4Rect = { x0: 7396, x1: 9418, y0: 15747, y1: 16461 };
  const lot5Rect = { x0: 7400, x1: 11162, y0: 16672, y1: 17418 };

  const inLot4 = centre[0] >= lot4Rect.x0 && centre[0] <= lot4Rect.x1 && centre[1] >= lot4Rect.y0 && centre[1] <= lot4Rect.y1;
  const inLot5 = centre[0] >= lot5Rect.x0 && centre[0] <= lot5Rect.x1 && centre[1] >= lot5Rect.y0 && centre[1] <= lot5Rect.y1;

  // Confirm that centre is NOT inside either lot (it fell into the alley gap!)
  assert.equal(inLot4, false, 'Fence centre must not be in lot 4');
  assert.equal(inLot5, false, 'Fence centre must not be in lot 5');

  // Real parcel fence from lot 4
  const parcel4Fence: GenWall = {
    id: 'F_x1_8_4_1_1_t1003_E_0',
    a: [9418.85, 16298.6],
    b: [7410.54, 16351.19],
    height: 135,
    thickness: 10,
    kind: 'fence',
  };

  // Geometric crossing between parent fence and new parcel fence
  const crossingWall = parentFenceWalls.find((pw) => segmentsIntersect(pw.a, pw.b, parcel4Fence.a, parcel4Fence.b));
  assert.ok(crossingWall, 'Parent fence must geometrically cross parcel fence');
  assert.equal(crossingWall.id, 'F_x1_8_t1003_S_0');

  const pt = segmentSegmentIntersection(crossingWall.a, crossingWall.b, parcel4Fence.a, parcel4Fence.b);
  assert.ok(pt);
  assert.ok(Math.abs(pt[0] - 7993.59) < 1);
  assert.ok(Math.abs(pt[1] - 16335.92) < 1);
});

test('parent fence that does not cross fresh geometry is preserved', () => {
  // A perimeter fence around an independent block that does not cross any fresh walls
  const independentFence: GenWall = {
    id: 'F_perimeter_0',
    a: [0, 0],
    b: [1000, 0],
    height: 135,
    thickness: 10,
    kind: 'fence',
  };
  const freshWall: GenWall = {
    id: 'F_fresh_0',
    a: [2000, 2000],
    b: [3000, 2000],
    height: 135,
    thickness: 10,
    kind: 'fence',
  };

  const layout: GenLayout = {
    walls: [independentFence, freshWall],
    buildings: [],
    zones: [],
    spawn_points: [],
  };

  const conflicts = validateWallCrossings(layout);
  assert.equal(conflicts.length, 0);

  const cleanResult = cleanConflictingFences(layout);
  assert.equal(cleanResult.removedFamilies.length, 0);
  assert.equal(cleanResult.layout.walls.length, 2);
});

test('wall vs building footprint crossing is detected', () => {
  const building: GenBuilding = {
    id: 'B_warehouse',
    footprint: [
      [1000, 1000],
      [3000, 2000],
    ],
    height: 400,
    floor: 0,
    roof_type: 'flat',
  };

  // A fence cutting through the building
  const cuttingFence: GenWall = {
    id: 'F_intruder',
    a: [500, 1500],
    b: [3500, 1500],
    height: 135,
    thickness: 10,
    kind: 'fence',
  };

  // A fence outside the building
  const outsideFence: GenWall = {
    id: 'F_outside',
    a: [500, 500],
    b: [3500, 500],
    height: 135,
    thickness: 10,
    kind: 'fence',
  };

  const layout: GenLayout = {
    walls: [cuttingFence, outsideFence],
    buildings: [building],
    zones: [],
    spawn_points: [],
  };

  const conflicts = validateWallCrossings(layout);
  assert.equal(conflicts.length, 1);
  assert.equal(conflicts[0].kind, 'wall_building');
  assert.equal(conflicts[0].wallA.id, 'F_intruder');
  assert.equal(conflicts[0].building?.id, 'B_warehouse');
});

// --- Tests for 085b review points (1-4) ---

test('085b (1): rotated 14 deg open fence spanning 2 lot centres is kept (not deleted as non-closed)', () => {
  // An open fence turned ~14 deg: spans across the bounding box of 2 lot centres, but is not a closed ring
  const openRotatedFence: GenWall[] = [
    { id: 'F_open_1', a: [0, 0], b: [2000, 500], height: 135, thickness: 10, kind: 'fence' },
    { id: 'F_open_2', a: [2000, 500], b: [3000, 1500], height: 135, thickness: 10, kind: 'fence' },
  ];

  // Closed ring check returns null for open fence
  const ring = fenceClosedRing(openRotatedFence);
  assert.equal(ring, null, 'Open fence must not be recognized as a closed polygon ring');

  // Even if an axis-aligned bbox would enclose lot centres [1000, 400] and [1500, 450],
  // the closed ring requirement ensures it is not treated as enclosing subdivided lots.
  const closedFence: GenWall[] = [
    { id: 'F_c_1', a: [0, 0], b: [2000, 0], height: 135, thickness: 10, kind: 'fence' },
    { id: 'F_c_2', a: [2000, 0], b: [2000, 1000], height: 135, thickness: 10, kind: 'fence' },
    { id: 'F_c_3', a: [2000, 1000], b: [0, 1000], height: 135, thickness: 10, kind: 'fence' },
    { id: 'F_c_4', a: [0, 1000], b: [0, 0], height: 135, thickness: 10, kind: 'fence' },
  ];
  const closedRing = fenceClosedRing(closedFence);
  assert.ok(closedRing, 'Closed fence must return polygon vertices');
  assert.equal(closedRing.length, 4);
});

test('085b (3): endpoint tolerance - fence butted into a building wall is NOT a crossing', () => {
  const building: GenBuilding = {
    id: 'B_office',
    footprint: [
      [1000, 1000],
      [3000, 2000],
    ],
    height: 400,
    floor: 0,
    roof_type: 'flat',
  };

  // A fence butting into the building west wall (X=1000, Y in 1000..2000), penetrating 10 cm within wall thickness (30 cm)
  const buttedFence: GenWall = {
    id: 'F_butted',
    a: [200, 1500],
    b: [1010, 1500], // penetrates 10 cm past X=1000
    height: 135,
    thickness: 10,
    kind: 'fence',
  };

  const layout: GenLayout = {
    walls: [buttedFence],
    buildings: [building],
    zones: [],
    spawn_points: [],
  };

  const conflicts = validateWallCrossings(layout);
  assert.equal(conflicts.length, 0, 'Fence butted into building wall must not be flagged as a crossing');
});

test('085b (3): endpoint tolerance - T-joint wall overshoot within thickness is NOT a crossing', () => {
  // Continuous wall along X
  const throughWall: GenWall = {
    id: 'W_main',
    a: [0, 1000],
    b: [2000, 1000],
    height: 200,
    thickness: 20,
    kind: 'wall',
  };

  // Wall butting into W_main at Y=1000 with 10 cm overshoot (less than thickness 20 cm)
  const tWall: GenWall = {
    id: 'W_branch',
    a: [1000, 0],
    b: [1000, 1010],
    height: 200,
    thickness: 20,
    kind: 'wall',
  };

  const layout: GenLayout = {
    walls: [throughWall, tWall],
    buildings: [],
    zones: [],
    spawn_points: [],
  };

  const conflicts = validateWallCrossings(layout);
  assert.equal(conflicts.length, 0, 'T-joint with overshoot within wall thickness must not be flagged');
});

test('085b (4): cleanConflictingFences victim must be a fence family, never a non-fence wall', () => {
  // A non-fence wall crossing a fence
  const wallFamilyWall: GenWall = {
    id: 'W_compound_n',
    a: [0, 1000],
    b: [2000, 1000],
    height: 250,
    thickness: 30,
    kind: 'wall',
  };
  const fenceWall: GenWall = {
    id: 'F_orphan_N_0',
    a: [1000, 0],
    b: [1000, 2000],
    height: 135,
    thickness: 10,
    kind: 'fence',
  };

  const layout: GenLayout = {
    walls: [wallFamilyWall, fenceWall],
    buildings: [],
    zones: [],
    spawn_points: [],
  };

  const conflicts = validateWallCrossings(layout);
  assert.equal(conflicts.length, 1);

  // Clean must pick the fence family as victim and NEVER remove the solid wall
  const cleanResult = cleanConflictingFences(layout);
  assert.deepEqual(cleanResult.removedFamilies, ['F_orphan']);
  assert.equal(cleanResult.removedWalls.length, 1);
  assert.equal(cleanResult.removedWalls[0], 'F_orphan_N_0');
  assert.equal(cleanResult.layout.walls.length, 1);
  assert.equal(cleanResult.layout.walls[0].id, 'W_compound_n');
});

test('085b (4): cleanConflictingFences between two non-fence walls removes neither', () => {
  const wall1: GenWall = {
    id: 'W_brick_0',
    a: [0, 1000],
    b: [2000, 1000],
    height: 250,
    thickness: 30,
    kind: 'wall',
  };
  const wall2: GenWall = {
    id: 'W_stone_0',
    a: [1000, 0],
    b: [1000, 2000],
    height: 250,
    thickness: 30,
    kind: 'wall',
  };

  const layout: GenLayout = {
    walls: [wall1, wall2],
    buildings: [],
    zones: [],
    spawn_points: [],
  };

  const conflicts = validateWallCrossings(layout);
  assert.equal(conflicts.length, 1);

  // Since neither is a fence, cleanConflictingFences does not delete either wall family
  const cleanResult = cleanConflictingFences(layout);
  assert.equal(cleanResult.removedFamilies.length, 0);
  assert.equal(cleanResult.layout.walls.length, 2);
});
