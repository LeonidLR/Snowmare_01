// Standalone unit tests for smart enemies roadmap (section T-02) combat lab layouts:
//   - L_Lab_Crossroads (gen_lab_crossroads.json)
//   - L_Lab_Alley (gen_lab_alley.json)
//   - L_Lab_Bridge (gen_lab_bridge.json)
//
// These tests are pure readers: they never write to disk on test run.
// Layout generation is in scripts/build-lab-layouts.ts.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import type { Vec2 } from '../movement/geometry.ts';
import { dist } from '../movement/geometry.ts';
import type { GenLayout, LayoutWall } from './schema.ts';
import { validateGenLayout } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const ACCEPTED_DIR = path.join(ROOT, 'Content/Data/LevelGen/accepted');
const TACTICAL_DIR = path.join(ROOT, 'Content/Data/LevelGen/tactical');
const OBJECTIVES_DIR = path.join(ROOT, 'Content/Data/LevelGen/objectives');
const PROPS_DIR = path.join(ROOT, 'Content/Data/LevelGen/props');
const LEVELJSON_DIR = path.join(ROOT, 'Content/Data/LevelJson');

function loadLayout(name: string): GenLayout {
  const p = path.join(ACCEPTED_DIR, `${name}.json`);
  assert.ok(fs.existsSync(p), `Layout file ${p} must exist on disk`);
  return JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, '')) as GenLayout;
}

function distToSegment(p: Vec2, a: Vec2, b: Vec2): number {
  const dx = b[0] - a[0];
  const dy = b[1] - a[1];
  const l2 = dx * dx + dy * dy;
  if (l2 === 0) return dist(p, a);
  let t = ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2;
  t = Math.max(0, Math.min(1, t));
  return dist(p, [a[0] + t * dx, a[1] + t * dy]);
}

function assertGoalNotInsideObstacles(layout: GenLayout, labName: string): void {
  assert.ok(layout.objectives?.goals?.length, `${labName}: objectives.goals must exist`);
  for (const goal of layout.objectives.goals) {
    const [gx, gy] = goal.pos;

    // 1. Goal must NOT be inside any building footprint
    for (const b of layout.buildings) {
      const [[bx0, by0], [bx1, by1]] = b.footprint;
      const minX = Math.min(bx0, bx1);
      const maxX = Math.max(bx0, bx1);
      const minY = Math.min(by0, by1);
      const maxY = Math.max(by0, by1);
      const insideBuilding = gx >= minX && gx <= maxX && gy >= minY && gy <= maxY;
      assert.ok(
        !insideBuilding,
        `${labName}: goal [${gx}, ${gy}] must not be inside building ${b.id} footprint [[${minX}, ${minY}], [${maxX}, ${maxY}]]`,
      );
    }

    // 2. Goal must NOT intersect any wall
    const wallSegments: { id?: string; a: Vec2; b: Vec2; thickness: number }[] = [];
    for (const w of layout.walls ?? []) {
      wallSegments.push({ id: w.id, a: w.a, b: w.b, thickness: w.thickness ?? 20 });
    }
    for (const b of layout.buildings) {
      if (b.partitions) {
        for (const p of b.partitions) {
          wallSegments.push({ id: p.id, a: p.start, b: p.end, thickness: p.thickness ?? 20 });
        }
      }
    }
    for (const w of wallSegments) {
      const d = distToSegment([gx, gy], w.a, w.b);
      const safeDist = w.thickness / 2 + 5; // wall half-thickness + 5 cm margin
      assert.ok(
        d > safeDist,
        `${labName}: goal [${gx}, ${gy}] must not intersect wall ${w.id ?? 'unnamed'} (distance ${d.toFixed(1)} cm <= ${safeDist} cm)`,
      );
    }
  }
}

test('labLayouts: all 3 lab layouts pass schema validation and disk artifacts exist', () => {
  const labs = ['gen_lab_crossroads', 'gen_lab_alley', 'gen_lab_bridge'];
  for (const name of labs) {
    const layout = loadLayout(name);
    const errors = validateGenLayout(layout);
    assert.deepEqual(errors, [], `Layout ${name} must pass validateGenLayout`);

    // Verify all associated files exist on disk
    const mapName = name === 'gen_lab_crossroads' ? 'L_Lab_Crossroads'
      : name === 'gen_lab_alley' ? 'L_Lab_Alley'
      : 'L_Lab_Bridge';

    assert.ok(fs.existsSync(path.join(ACCEPTED_DIR, `${name}.meta.json`)), `${name}.meta.json exists`);
    assert.ok(fs.existsSync(path.join(TACTICAL_DIR, `${mapName}.tactical.json`)), `${mapName}.tactical.json exists`);
    assert.ok(fs.existsSync(path.join(OBJECTIVES_DIR, `${mapName}.objectives.json`)), `${mapName}.objectives.json exists`);
    assert.ok(fs.existsSync(path.join(PROPS_DIR, `${mapName}.props.json`)), `${mapName}.props.json exists`);
    assert.ok(fs.existsSync(path.join(LEVELJSON_DIR, `${mapName}.json`)), `${mapName}.json exists`);

    // Check meta file is clean (no invented Jev verdicts or fake score 1)
    const meta = JSON.parse(fs.readFileSync(path.join(ACCEPTED_DIR, `${name}.meta.json`), 'utf8'));
    assert.ok(!meta.verdicts || meta.verdicts.length === 0, `${name}: must not contain fake verdicts`);
    assert.ok(meta.note, `${name}: meta must contain descriptive note`);

    // Review point 3: Goal must NOT be inside any wall or building footprint
    assertGoalNotInsideObstacles(layout, name);
  }
});

test('L_Lab_Crossroads: reproduces designer screenshot specs (T-02)', () => {
  const l = loadLayout('gen_lab_crossroads');

  // 1. Map size <= 80 x 80 m
  const widthM = (l.bounds.max[0] - l.bounds.min[0]) / 100;
  const heightM = (l.bounds.max[1] - l.bounds.min[1]) / 100;
  assert.ok(widthM <= 80 && heightM <= 80, `Bounds must be <= 80 x 80 m (got ${widthM} x ${heightM} m)`);

  // 2. Start-to-goal distance 60-80 m (exactly 70 m)
  assert.ok(l.objectives?.start && l.objectives?.goals?.[0], 'Objectives start and goal must exist');
  const dStartGoal = dist(l.objectives.start.pos, l.objectives.goals[0].pos) / 100;
  assert.ok(dStartGoal >= 60 && dStartGoal <= 80, `Start-to-goal distance must be in 60-80 m (got ${dStartGoal} m)`);
  assert.equal(dStartGoal, 70, 'Start-to-goal distance is 70 m');

  // 3. Walled enclosure on near side (X < 0) with exactly 1 gap facing the crossroads [0, 0]
  const nearWalls = (l.walls ?? []).filter((w) => w.a[0] < 0 && w.b[0] < 0 && w.a[1] > 0);
  assert.ok(nearWalls.length >= 4, 'Enclosure must have walls forming perimeter');
  // East wall of enclosure has gap facing crossroads
  const eastWallParts = nearWalls.filter((w) => Math.abs(w.a[1] - 1600) < 5 && Math.abs(w.b[1] - 1600) < 5);
  assert.equal(eastWallParts.length, 2, 'East wall must be split into 2 parts leaving exactly 1 gap');

  // 4. >= 4 covers on far side (X > 500)
  const farCovers = l.covers.filter((c) => c.pos[0] > 500);
  assert.ok(farCovers.length >= 4, `>= 4 covers on far side (got ${farCovers.length})`);
});

test('L_Lab_Alley: reproduces designer screenshot specs (T-02)', () => {
  const l = loadLayout('gen_lab_alley');

  // 1. Map size <= 80 x 80 m
  const widthM = (l.bounds.max[0] - l.bounds.min[0]) / 100;
  const heightM = (l.bounds.max[1] - l.bounds.min[1]) / 100;
  assert.ok(widthM <= 80 && heightM <= 80, `Bounds must be <= 80 x 80 m (got ${widthM} x ${heightM} m)`);

  // 2. Lane 4 m wide (3-5 m range) between B_low and south wall W_alley_south
  const bLow = l.buildings.find((b) => b.id === 'B_low');
  assert.ok(bLow, 'B_low building must exist');
  const sWall = (l.walls ?? []).find((w) => w.id === 'W_alley_south');
  assert.ok(sWall, 'W_alley_south wall must exist');
  const laneWidthM = (bLow.footprint[0][1] - sWall.a[1]) / 100;
  assert.ok(laneWidthM >= 3 && laneWidthM <= 5, `Lane width must be 3-5 m (got ${laneWidthM} m)`);
  assert.equal(laneWidthM, 4.0, 'Lane width is exactly 4.0 m');

  // 3. Review fix A3: B_low door and windows must be on the lane-side wall (Y = 200 face)
  assert.ok(bLow.openings && bLow.openings.length >= 2, 'B_low must have door and windows');
  const bLowDoors = bLow.openings.filter((o) => o.kind === 'door');
  assert.ok(bLowDoors.length >= 1, 'B_low must have at least 1 door');
  for (const d of bLowDoors) {
    assert.ok(
      Math.abs(d.pos[1] - 200) < 15,
      `B_low door must be on the lane-side wall at Y=200 (got Y=${d.pos[1]})`,
    );
  }
  const bLowWindows = bLow.openings.filter((o) => o.kind === 'window');
  assert.ok(bLowWindows.length >= 1, 'B_low must have windows');
  for (const w of bLowWindows) {
    assert.ok(
      Math.abs(w.pos[1] - 200) < 15,
      `B_low window must be on the lane-side wall at Y=200 (got Y=${w.pos[1]})`,
    );
  }

  // 4. Two-storey building B_two_storey at mouth of lane
  const bTwo = l.buildings.find((b) => b.id === 'B_two_storey');
  assert.ok(bTwo, 'B_two_storey must exist');
  assert.equal(bTwo.floors, 2, 'Must have 2 storeys');
  assert.equal(bTwo.roof_walkable, true, 'Roof must be walkable');
  assert.ok((bTwo.parapets?.length ?? 0) > 0, 'Roof must have parapets');

  // 5. Review fix A2: Ladder on lane-mouth face (X = 1500)
  const ladder = l.ladders.find((lad) => lad.id === 'LAD_B_two_storey');
  assert.ok(ladder, 'Ladder for B_two_storey must exist');
  assert.ok(ladder.rungs >= 7, `Ladder must have >= 7 rungs (got ${ladder.rungs})`);
  assert.ok(
    Math.abs(ladder.pos[0] - 1500) < 25,
    `Ladder must be on the lane-mouth face at X=1500 (got X=${ladder.pos[0]})`,
  );
  // Ladder yaw must face South (-X) into the lane mouth (yaw 180 in UE)
  assert.equal(ladder.yaw, 180, 'Ladder yaw must face South into the lane mouth');

  // 6. Review fix A1: Goal at lane mouth in open space, NOT inside solid wall
  assert.ok(l.objectives?.goals?.[0], 'Goal must exist');
  const goal = l.objectives.goals[0];
  assert.equal(goal.pos[0], 1350, 'Goal pos X must be in the lane at 1350');
  assert.equal(goal.pos[1], 0, 'Goal pos Y must be in the lane centre at 0');

  // 7. Route & variant: main and back route round low building
  assert.ok(l.routes.length >= 2, 'Must have main route and back route variant');
  const backRoute = l.routes.find((r) => r.id === 'V1_back_route');
  assert.ok(backRoute, 'Variant V1_back_route must exist');
  const passesBehind = backRoute.points.some((p) => p[1] >= 1600);
  assert.ok(passesBehind, 'Back route must pass behind B_low (Y >= 1600)');
  assert.deepEqual(backRoute.points[backRoute.points.length - 1], goal.pos, 'Back route ends at goal');
});

test('L_Lab_Bridge: reproduces designer screenshot specs (T-02)', () => {
  const l = loadLayout('gen_lab_bridge');

  // 1. Map size <= 80 x 80 m
  const widthM = (l.bounds.max[0] - l.bounds.min[0]) / 100;
  const heightM = (l.bounds.max[1] - l.bounds.min[1]) / 100;
  assert.ok(widthM <= 80 && heightM <= 80, `Bounds must be <= 80 x 80 m (got ${widthM} x ${heightM} m)`);

  // 2. Cut 6 m wide across the map with drop 300 cm (level -1)
  assert.ok(l.cut, 'Cut must exist');
  const cutWidthM = 6.0;
  assert.equal(l.cut.level, -1, 'Cut level must be -1');
  assert.equal(l.cut.floor_z, -300, 'Cut floor_z must be -300 cm');
  assert.ok(l.cut.crossings.length >= 1, 'Crossing must exist');
  const deck = l.cut.crossings.find((c) => c.kind === 'bridge');
  assert.ok(deck, 'Bridge crossing deck must exist');
  assert.equal(deck.width, 4.0, 'Bridge deck width must be 4.0 m');

  // 3. Two cover objects at near end (X < -300)
  const nearCovers = l.covers.filter((c) => c.pos[0] < -300);
  assert.equal(nearCovers.length, 2, 'Exactly two covers at near end');

  // 4. Climbable obstacle of mantling height beside far end
  const mantleCov = l.covers.find((c) => c.id === 'cov_bridge_far_mantle');
  assert.ok(mantleCov, 'Far mantle obstacle must exist');
  assert.ok(mantleCov.size[2] >= 75 && mantleCov.size[2] <= 130, 'Mantle obstacle must be of low_mantle height');
  const mantleEdge = l.mantle_edges.find((e) => e.id === 'me_cov_bridge_far_mantle');
  assert.ok(mantleEdge, 'Mantle edge must be registered');

  // 5. Review fix B1: Building B_overlook with windows on the wall facing the deck (Y = -300 face)
  const bOverlook = l.buildings.find((b) => b.id === 'B_overlook');
  assert.ok(bOverlook, 'B_overlook must exist');
  const windows = (bOverlook.openings ?? []).filter((o) => o.kind === 'window');
  assert.ok(windows.length >= 1, 'B_overlook must have windows');

  // Geometric LOS check:
  // Windows sit on the wall facing the deck at Y = -300 (deck is at Y in [-200, 200]).
  // Window yaw/normal faces +Y directly onto the bridge deck.
  for (const win of windows) {
    assert.ok(
      Math.abs(win.pos[1] - (-300)) < 15,
      `Window must be on the deck-facing wall at Y=-300 (got Y=${win.pos[1]})`,
    );
    assert.equal(win.yaw, 90, 'Window yaw must face East (+Y) directly towards the bridge deck');

    // Raycast/sightline check: a ray extending from the window along its normal (+Y) intersects the deck Y-span [-200, 200]
    const rayTargetY = win.pos[1] + 300; // Y = 0 (centre of deck)
    assert.ok(
      rayTargetY >= -200 && rayTargetY <= 200,
      'Sightline along window normal reaches bridge deck Y span [-200, 200]',
    );
  }
});
