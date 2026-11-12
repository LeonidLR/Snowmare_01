// Unit tests for level generator combat buildings schema extensions (docs/port/gemini/inbox/027-levelgen-combat-buildings.md item 1)
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import type { GenBuilding, GenLayout, WallOpening } from './schema.ts';
import {
  isMantleable,
  LEVELGEN_SCHEMA,
  MANTLE_KINDS,
  MAX_MANTLE_HEIGHT_CM,
  MIN_MANTLE_HEIGHT_CM,
  OPENING_DEFAULTS,
  OPENING_KINDS,
  unifyOpenings,
  validateGenLayout,
  WALL_KINDS,
} from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');

test('combat buildings schema: constants and defaults', () => {
  assert.ok(WALL_KINDS.includes('partition'));
  assert.ok(WALL_KINDS.includes('parapet'));
  assert.ok(WALL_KINDS.includes('interior'));

  assert.deepEqual(OPENING_KINDS, ['door', 'window', 'loophole']);
  assert.equal(OPENING_DEFAULTS.door.sill, 0);
  assert.equal(OPENING_DEFAULTS.window.sill, 90);
  assert.equal(OPENING_DEFAULTS.window.top, 210);

  // Loophole narrow firing slit: width 20-40 cm, sill 110-140 cm
  assert.ok(OPENING_DEFAULTS.loophole.width >= 20 && OPENING_DEFAULTS.loophole.width <= 40);
  assert.ok(OPENING_DEFAULTS.loophole.sill >= 110 && OPENING_DEFAULTS.loophole.sill <= 140);
  assert.ok(OPENING_DEFAULTS.loophole.top > OPENING_DEFAULTS.loophole.sill);

  // Mantle edge bounds: 50..150 cm
  assert.equal(MIN_MANTLE_HEIGHT_CM, 50);
  assert.equal(MAX_MANTLE_HEIGHT_CM, 150);
  assert.equal(isMantleable(49), false);
  assert.equal(isMantleable(50), true);
  assert.equal(isMantleable(100), true);
  assert.equal(isMantleable(150), true);
  assert.equal(isMantleable(151), false);
  assert.ok(MANTLE_KINDS.includes('window'));
  assert.ok(MANTLE_KINDS.includes('parapet'));
});

test('unifyOpenings: backward compatibility with legacy doors', () => {
  const legacyBuilding: GenBuilding = {
    id: 'b_legacy',
    footprint: [[0, 0], [1000, 1000]],
    height: 600,
    floors: 2,
    enterable: true,
    roof_walkable: true,
    doors: [
      { pos: [500, 0], floor: 0, wall: 'w0' },
      { pos: [1000, 500], floor: 1, wall: 'w1' },
    ],
    stairs: [],
    roof_holes: [],
    floor_holes: [],
    set_piece: 'warehouse',
  };

  const unified = unifyOpenings(legacyBuilding);
  assert.equal(unified.length, 2);
  assert.equal(unified[0].kind, 'door');
  assert.deepEqual(unified[0].pos, [500, 0]);
  assert.equal(unified[0].sill_height, 0);

  // When openings are explicitly provided, they take precedence
  const modernBuilding: GenBuilding = {
    ...legacyBuilding,
    openings: [
      { kind: 'window', pos: [200, 0], floor: 0, width: 120, sill_height: 90, top_height: 210 },
      { kind: 'loophole', pos: [800, 0], floor: 0, width: 30, sill_height: 120, top_height: 150 },
    ],
  };
  const modernUnified = unifyOpenings(modernBuilding);
  assert.equal(modernUnified.length, 2);
  assert.equal(modernUnified[0].kind, 'window');
  assert.equal(modernUnified[1].kind, 'loophole');
});

test('validateGenLayout: validates combat building schema extensions', () => {
  const validLayout: GenLayout = {
    schema: LEVELGEN_SCHEMA,
    name: 'combat_test',
    profile: 'ref_01_02_depot',
    seed: 42,
    params: { size_m: [50, 50], density: 1, variants: 1, verticality: 2 },
    bounds: { min: [-2500, -2500], max: [2500, 2500] },
    ground_z: 0,
    spawn: { pos: [-2000, 0], yaw: 0 },
    end: [2000, 0],
    walls: [],
    buildings: [
      {
        id: 'b_combat',
        footprint: [[-500, -500], [500, 500]],
        height: 600,
        floors: 2,
        enterable: true,
        roof_walkable: true,
        doors: [{ pos: [-500, 0], floor: 0 }],
        openings: [
          { kind: 'door', pos: [-500, 0], floor: 0, width: 120, sill_height: 0, top_height: 210 },
          { kind: 'window', pos: [0, -500], floor: 0, width: 120, sill_height: 90, top_height: 210 },
          { kind: 'loophole', pos: [0, 500], floor: 1, width: 30, sill_height: 120, top_height: 150 },
        ],
        partitions: [
          { id: 'part_1', start: [0, -500], end: [0, 500], floor: 0, thickness: 20 },
        ],
        floors_detail: [
          {
            floor: 1,
            height_z: 300,
            gaps: [{ id: 'stair_gap', poly: [[100, 100], [300, 100], [300, 300], [100, 300]], kind: 'stairwell' }],
          },
        ],
        hatches: [
          { id: 'hatch_roof', pos: [200, 200], floor: 1, size: [100, 100] },
        ],
        parapets: [
          { start: [-500, -500], end: [500, -500], height_cm: 100, thickness_cm: 20 },
        ],
        mantle_edges: [
          { id: 'me_window', start: [-60, -500], end: [60, -500], floor: 0, base_z: 0, height_cm: 90, normal: [0, -1], kind: 'window' },
        ],
        stairs: [],
        roof_holes: [],
        floor_holes: [],
        set_piece: 'warehouse',
      },
    ],
    ladders: [],
    covers: [],
    zones: [],
    routes: [
      { id: 'r_main', mandatory: true, points: [[-2000, 0], [2000, 0]], pace: 'walk' },
    ],
    spawn_points: [],
    ambush_points: [],
    patrol_routes: [],
    mantle_edges: [
      { id: 'me_global_fence', start: [-1000, 100], end: [-800, 100], floor: 0, base_z: 0, height_cm: 100, normal: [0, 1], kind: 'fence' },
    ],
  };

  const errors = validateGenLayout(validLayout);
  assert.deepEqual(errors, []);

  // Test invalid loophole width
  const badLoophole: GenLayout = JSON.parse(JSON.stringify(validLayout));
  badLoophole.buildings[0].openings![2].width = 100; // Too wide for a loophole (max 60)
  const badLpErrors = validateGenLayout(badLoophole);
  assert.ok(badLpErrors.some((err) => err.includes('loophole width 100 cm outside narrow slit range')));

  // Test invalid mantle edge height (< 50 cm or > 150 cm)
  const badMantle: GenLayout = JSON.parse(JSON.stringify(validLayout));
  badMantle.buildings[0].mantle_edges![0].height_cm = 200;
  const badMantleErrors = validateGenLayout(badMantle);
  assert.ok(badMantleErrors.some((err) => err.includes('must be 50..150 cm')));
});
