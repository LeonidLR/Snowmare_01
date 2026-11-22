import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { dist } from '../movement/geometry.ts';
import type { GenLayout } from './schema.ts';
import { validateGenLayout } from './schema.ts';
import { buildAllLabLayouts, buildLabCrossroads, buildLabAlley, buildLabBridge } from './labLayouts.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const ACCEPTED_DIR = path.join(ROOT, 'Content/Data/LevelGen/accepted');
const TACTICAL_DIR = path.join(ROOT, 'Content/Data/LevelGen/tactical');
const OBJECTIVES_DIR = path.join(ROOT, 'Content/Data/LevelGen/objectives');
const PROPS_DIR = path.join(ROOT, 'Content/Data/LevelGen/props');
const LEVELJSON_DIR = path.join(ROOT, 'Content/Data/LevelJson');

test('labLayouts: all 3 lab layouts pass schema validation and disk artifacts match', () => {
  const bundles = buildAllLabLayouts();
  assert.equal(bundles.length, 3);

  for (const bundle of bundles) {
    const layout = bundle.layout;
    const errors = validateGenLayout(layout);
    assert.deepEqual(errors, [], `${bundle.name} must pass validateGenLayout`);

    // Map size <= 80 x 80 m
    const widthCm = layout.bounds.max[1] - layout.bounds.min[1];
    const heightCm = layout.bounds.max[0] - layout.bounds.min[0];
    assert.ok(widthCm <= 8000, `${bundle.name} width ${widthCm} cm must be <= 8000 cm (80 m)`);
    assert.ok(heightCm <= 8000, `${bundle.name} height ${heightCm} cm must be <= 8000 cm (80 m)`);

    // Accepted layout on disk
    const diskPath = path.join(ACCEPTED_DIR, `${bundle.name}.json`);
    assert.ok(fs.existsSync(diskPath), `${diskPath} must exist`);
    const onDisk = JSON.parse(fs.readFileSync(diskPath, 'utf8')) as GenLayout;
    assert.deepEqual(validateGenLayout(onDisk), [], `${bundle.name} on disk must pass validateGenLayout`);
    assert.deepEqual(onDisk, JSON.parse(JSON.stringify(layout)), `${bundle.name} on disk must match generator`);

    // Meta file on disk
    const metaPath = path.join(ACCEPTED_DIR, `${bundle.name}.meta.json`);
    assert.ok(fs.existsSync(metaPath), `${metaPath} must exist`);
    const meta = JSON.parse(fs.readFileSync(metaPath, 'utf8'));
    assert.ok(meta.at, `${metaPath} must have ISO timestamp 'at'`);
    assert.equal(meta.score, 1.0);

    // Tactical map on disk
    const tacPath = path.join(TACTICAL_DIR, `${bundle.mapName}.tactical.json`);
    assert.ok(fs.existsSync(tacPath), `${tacPath} must exist`);

    // Objectives on disk
    const objPath = path.join(OBJECTIVES_DIR, `${bundle.mapName}.objectives.json`);
    assert.ok(fs.existsSync(objPath), `${objPath} must exist`);

    // Props on disk
    const propPath = path.join(PROPS_DIR, `${bundle.mapName}.props.json`);
    assert.ok(fs.existsSync(propPath), `${propPath} must exist`);

    // LevelJson on disk
    const levelJsonPath = path.join(LEVELJSON_DIR, `${bundle.mapName}.json`);
    assert.ok(fs.existsSync(levelJsonPath), `${levelJsonPath} must exist`);
  }
});

test('L_Lab_Crossroads: reproduces designer screenshot specs (T-02)', () => {
  const { layout } = buildLabCrossroads();

  // 1. Attacker start 60-80 m away from goal beyond the crossroads
  const startToGoalM = dist(layout.spawn.pos, layout.end) / 100;
  assert.ok(startToGoalM >= 60 && startToGoalM <= 80, `Start to goal distance ${startToGoalM} m must be in 60-80 m range`);
  assert.ok(layout.spawn.pos[0] < -500, 'Start must be on near side of crossroads (X < 0)');
  assert.ok(layout.end[0] > 500, 'Goal must be beyond crossroads (X > 0)');

  // 2. Open crossroads at center
  const crZone = layout.zones.find((z) => z.set_piece === 'open_square');
  assert.ok(crZone, 'Crossroads must have open_square zone at intersection');

  // 3. Walled enclosure on near side with exactly one gap
  const encWalls = layout.walls.filter((w) => w.id.startsWith('W_enc_'));
  assert.equal(encWalls.length, 5, 'Enclosure must have 5 wall segments (3 continuous sides + 1 side with gap split in 2)');
  for (const w of encWalls) {
    assert.equal(w.height, 300, 'Enclosure walls must be 300 cm tall');
    assert.ok(w.a[0] <= 0 && w.b[0] <= 0, 'Enclosure walls must be on the near side (X <= 0)');
  }
  // The east-facing wall has a gap between Y = 900 and Y = 1100
  const e1 = encWalls.find((w) => w.id === 'W_enc_E1')!;
  const e2 = encWalls.find((w) => w.id === 'W_enc_E2')!;
  assert.ok(e1 && e2, 'East wall must be split into two segments forming the gap');
  assert.equal(Math.abs(e2.a[1] - e1.b[1]), 200, 'Gap width must be 200 cm (single door/breach opening)');

  // 4. Far side cover objects >= 4
  const farCovers = layout.covers.filter((c) => c.pos[0] > 500);
  assert.ok(farCovers.length >= 4, `Far side must have >= 4 cover objects, got ${farCovers.length}`);
  assert.ok(farCovers.some((c) => c.kind === 'crate'), 'Must include crates');
});

test('L_Lab_Alley: reproduces designer screenshot specs (T-02)', () => {
  const { layout } = buildLabAlley();

  // 1. Lane 3-5 m wide along one-storey building
  const bLow = layout.buildings.find((b) => b.id === 'B_low')!;
  assert.ok(bLow, 'Must include B_low building');
  assert.equal(bLow.floors, 1, 'B_low must be one storey');
  assert.equal(bLow.height, 300, 'B_low height must be 300 cm');

  const southWall = layout.walls.find((w) => w.id === 'W_alley_south')!;
  assert.ok(southWall, 'Must include south lane boundary wall');

  const laneWidthCm = bLow.footprint[0][1] - southWall.a[1];
  const laneWidthM = laneWidthCm / 100;
  assert.ok(laneWidthM >= 3.0 && laneWidthM <= 5.0, `Lane width ${laneWidthM} m must be in 3-5 m range`);

  // 2. Two-storey building with walkable roof, parapet, and ladder >= 7 rungs at lane mouth
  const bTwo = layout.buildings.find((b) => b.id === 'B_two_storey')!;
  assert.ok(bTwo, 'Must include B_two_storey building');
  assert.equal(bTwo.floors, 2, 'B_two_storey must be 2 storeys');
  assert.equal(bTwo.height, 600, 'B_two_storey height must be 600 cm');
  assert.equal(bTwo.roof_walkable, true, 'B_two_storey must have walkable roof');
  assert.ok((bTwo.parapets?.length ?? 0) >= 4, 'B_two_storey must have parapets around roof');

  const ladder = layout.ladders.find((l) => l.to === `roof:${bTwo.id}`);
  assert.ok(ladder, 'Must have ladder leading to roof of B_two_storey');
  assert.ok(ladder.rungs >= 7, `Ladder must have >= 7 rungs, got ${ladder.rungs}`);

  // 3. Way round the back of the low building
  const backRoute = layout.routes.find((r) => r.id === 'V1_back_route');
  assert.ok(backRoute, 'Must have variant route around the back of the low building');
  const maxLowY = bLow.footprint[1][1];
  const backPointsNorth = backRoute.points.filter((p) => p[1] > maxLowY);
  assert.ok(backPointsNorth.length >= 2, 'Back route must traverse north of B_low');

  // 4. Goal at the lane mouth
  assert.equal(layout.end[0], 1500, 'Goal must be placed at the lane mouth');
  assert.equal(layout.end[1], 0);
});

test('L_Lab_Bridge: reproduces designer screenshot specs (T-02)', () => {
  const { layout } = buildLabBridge();

  // 1. Crossing over a cut (drop on both sides within drop rule)
  assert.ok(layout.cut, 'Must define cut trench');
  assert.equal(layout.cut.level, -1, 'Cut must be at level -1');
  assert.equal(layout.cut.floor_z, -300, 'Cut depth must be -300 cm (drop rule)');
  assert.ok(layout.cut.crossings.length >= 1, 'Must have bridge crossing');
  const crossing = layout.cut.crossings[0];
  assert.equal(crossing.kind, 'bridge');
  const bridgeWidthCm = crossing.poly[2][1] - crossing.poly[0][1];
  assert.equal(bridgeWidthCm, 400, 'Bridge deck width must be 4.0 m');

  // 2. Two cover objects at its near end (X < -300)
  const nearCovers = layout.covers.filter((c) => c.pos[0] < -300);
  assert.equal(nearCovers.length, 2, 'Must have exactly two cover objects at near end');

  // 3. Climbable obstacle of MANTLING-table height beside its far end (X > 300)
  const farCovers = layout.covers.filter((c) => c.pos[0] > 300);
  assert.ok(farCovers.length >= 1, 'Must have cover obstacle at far end');
  const climbable = farCovers.find((c) => c.size[2] >= 50 && c.size[2] <= 150);
  assert.ok(climbable, 'Obstacle must be of mantling table height (50-150 cm)');
  const mantleEdge = layout.mantle_edges?.find((me) => me.id.includes('far_mantle'));
  assert.ok(mantleEdge, 'Obstacle must have registered mantle edge');

  // 4. Building with windows onto the deck
  const bOverlook = layout.buildings.find((b) => b.id === 'B_overlook')!;
  assert.ok(bOverlook, 'Must have overlook building');
  const windows = (bOverlook.openings ?? []).filter((o) => o.kind === 'window');
  assert.ok(windows.length >= 1, 'B_overlook must have windows');
  // Windows face west towards bridge deck (yaw 180)
  assert.ok(windows.some((w) => w.yaw === 180), 'Windows must face west towards the bridge deck');
});
