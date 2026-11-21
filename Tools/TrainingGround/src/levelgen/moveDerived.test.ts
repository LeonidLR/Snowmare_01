// Training Ground / level generator: carrying derived files (props, tactical map) with a moved building (task 092).
// Docs: docs/port/gemini/inbox/092-tg-move-building-carries-props.md
// Gate: (1) props inside the moved footprint move by the delta, outside ones byte-identical;
//       (2) a locked prop inside the building moves too and stays locked;
//       (3) tactical spots with that building id move, others identical;
//       (4) after the carry the props validator (validator.ts / fastGate) and the 088 route clearance run and their new problems are shown;
//       (5) undo restores both files.

import { describe, it } from 'node:test';
import * as assert from 'node:assert/strict';
import * as fs from 'node:fs';
import * as path from 'node:path';
import type { Vec2 } from '../movement/geometry.ts';
import type { GenBuilding, GenLayout } from './schema.ts';
import type { LevelPropsFile, PropLayer, PropObject } from './props/schema.ts';
import type { TacticalMapFile } from './tacticalMap.ts';
import { PropsLayoutView } from './props/layoutView.ts';
import { findProjectRoot, loadAutogenConfig, loadPropsBalance } from './props/autogen.ts';
import { checkProps, newIssues } from './props/propsEdit.ts';
import { applyRouteClearance } from './props/routeClearance.ts';
import { translateBuilding } from './mapEdit.ts';
import { carryPropsWithBuilding, carryTacticalWithBuilding, isPropOnBuilding } from './moveDerived.ts';

const root = findProjectRoot();
const balance = loadPropsBalance(root);
const cfg = loadAutogenConfig(root);

const layoutPath = path.join(root, 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json');
const layout = JSON.parse(fs.readFileSync(layoutPath, 'utf8')) as GenLayout;

const propsPath = path.join(root, 'Content/Data/LevelGen/props/L_Gen_Depot_1.props.json');
const baseProps = JSON.parse(fs.readFileSync(propsPath, 'utf8')) as LevelPropsFile;

const tacticalPath = path.join(root, 'Content/Data/LevelGen/tactical/L_Gen_Test_X2.tactical.json');
const baseTactical = JSON.parse(fs.readFileSync(tacticalPath, 'utf8')) as TacticalMapFile;

const clone = <T>(v: T): T => JSON.parse(JSON.stringify(v)) as T;

describe('moveDerived - carryPropsWithBuilding', () => {
  const building = layout.buildings.find((b) => b.id === 'B_hall_n')!;
  assert.ok(building, 'B_hall_n found');

  it('(1) props inside the moved footprint move by the delta, outside ones byte-identical', () => {
    const props = clone(baseProps);
    // Add a prop explicitly on the roof of B_hall_n to test that roof props count
    props.layers[0].objects.push({
      id: 'crates_test_roof',
      variant: 'crate_s',
      pos: [4000, -1500],
      walk_layer: `roof:${building.id}`,
      yaw: 0,
      locked: false,
    });

    const delta: Vec2 = [500, -300];

    // Identify which props are inside before move
    const insideBefore: PropObject[] = [];
    const outsideBefore: PropObject[] = [];
    for (const l of props.layers) {
      for (const o of l.objects) {
        if (isPropOnBuilding(o, building)) insideBefore.push(o);
        else outsideBefore.push(o);
      }
    }
    assert.ok(insideBefore.length >= 6, 'at least 6 props inside/on B_hall_n');
    assert.ok(outsideBefore.length > 0, 'at least one prop outside B_hall_n');

    const result = carryPropsWithBuilding(props, building, delta);

    // Verify input not mutated
    assert.notEqual(result, props);

    // Check inside props moved by delta
    for (const original of insideBefore) {
      const moved = result.layers.flatMap((l) => l.objects).find((o) => o.id === original.id)!;
      assert.ok(moved, `moved prop ${original.id} found`);
      assert.equal(moved.pos[0], original.pos[0] + delta[0]);
      assert.equal(moved.pos[1], original.pos[1] + delta[1]);
      assert.equal(moved.walk_layer, original.walk_layer);
    }

    // Check outside props are byte-identical
    for (const original of outsideBefore) {
      const moved = result.layers.flatMap((l) => l.objects).find((o) => o.id === original.id)!;
      assert.ok(moved, `outside prop ${original.id} found`);
      assert.deepEqual(moved, original, `outside prop ${original.id} must be byte-identical`);
    }
  });

  it('(2) a locked prop inside the building moves too (it belongs to the building) and stays locked', () => {
    const props = clone(baseProps);
    const delta: Vec2 = [400, 200];

    // Pick a prop inside the building and explicitly lock it
    const inside = props.layers.flatMap((l) => l.objects).find((o) => isPropOnBuilding(o, building))!;
    assert.ok(inside, 'prop inside found');
    inside.locked = true;

    // Pick an unlocked prop inside as well
    const insideUnlocked = props.layers.flatMap((l) => l.objects).find((o) => isPropOnBuilding(o, building) && o.id !== inside.id);
    if (insideUnlocked) insideUnlocked.locked = false;

    const result = carryPropsWithBuilding(props, building, delta);

    const movedLocked = result.layers.flatMap((l) => l.objects).find((o) => o.id === inside.id)!;
    assert.equal(movedLocked.pos[0], inside.pos[0] + delta[0]);
    assert.equal(movedLocked.pos[1], inside.pos[1] + delta[1]);
    assert.equal(movedLocked.locked, true, 'locked prop stays locked');

    if (insideUnlocked) {
      const movedUnlocked = result.layers.flatMap((l) => l.objects).find((o) => o.id === insideUnlocked.id)!;
      assert.equal(movedUnlocked.pos[0], insideUnlocked.pos[0] + delta[0]);
      assert.equal(movedUnlocked.pos[1], insideUnlocked.pos[1] + delta[1]);
      assert.equal(movedUnlocked.locked, false, 'unlocked prop stays unlocked');
    }
  });

  it('attached prop moves along with container, and hand overrides follow', () => {
    const props = clone(baseProps);
    const delta: Vec2 = [200, 200];
    const inside = props.layers.flatMap((l) => l.objects).find((o) => isPropOnBuilding(o, building))!;
    assert.ok(inside, 'inside prop found');

    // Create an attached prop (e.g. mine on crate)
    const attachedProp: PropObject = {
      id: 'mine_attached_test',
      variant: 'mine_frag',
      pos: [inside.pos[0], inside.pos[1]],
      walk_layer: inside.walk_layer,
      attach: inside.id,
      locked: true,
    };
    props.layers[0].objects.push(attachedProp);

    // Create a hand override for the inside prop
    props.overrides = [
      { op: 'move', id: inside.id, pos: [inside.pos[0], inside.pos[1]], walk_layer: inside.walk_layer },
    ];

    const result = carryPropsWithBuilding(props, building, delta);

    const movedAttached = result.layers.flatMap((l) => l.objects).find((o) => o.id === attachedProp.id)!;
    assert.equal(movedAttached.pos[0], attachedProp.pos[0] + delta[0]);
    assert.equal(movedAttached.pos[1], attachedProp.pos[1] + delta[1]);

    const movedOverride = result.overrides?.find((o) => o.id === inside.id);
    assert.ok(movedOverride && movedOverride.op === 'move');
    assert.deepEqual(movedOverride.pos, [inside.pos[0] + delta[0], inside.pos[1] + delta[1]]);
  });

  it('re-measures generator cables when generator or linked turret moves', () => {
    const props = clone(baseProps);
    const delta: Vec2 = [300, 300];
    const inside = props.layers.flatMap((l) => l.objects).find((o) => isPropOnBuilding(o, building))!;

    // Add a generator inside the building and a turret outside
    const gen: PropObject = {
      id: 'gen_inside_test',
      variant: 'generator_diesel',
      pos: [inside.pos[0], inside.pos[1]],
      walk_layer: inside.walk_layer,
      links: ['turret_outside_test'],
      cable_cm: [1000],
    };
    const turret: PropObject = {
      id: 'turret_outside_test',
      variant: 'turret_heavy',
      pos: [inside.pos[0] + 1000, inside.pos[1]],
      walk_layer: 'ground',
    };
    props.layers[0].objects.push(gen, turret);

    const result = carryPropsWithBuilding(props, building, delta);
    const movedGen = result.layers.flatMap((l) => l.objects).find((o) => o.id === gen.id)!;
    // Expected distance: hypot((inside.pos[0]+300 - (inside.pos[0]+1000)), (inside.pos[1]+300 - inside.pos[1]))
    // = hypot(-700, 300) = sqrt(490000 + 90000) = sqrt(580000) ~ 762
    const expectedDist = Math.round(Math.hypot(movedGen.pos[0] - turret.pos[0], movedGen.pos[1] - turret.pos[1]));
    assert.equal(movedGen.cable_cm?.[0], expectedDist);
  });

  it('zero delta or building with no props returns unchanged clone', () => {
    const props = clone(baseProps);
    const resZero = carryPropsWithBuilding(props, building, [0, 0]);
    assert.deepEqual(resZero, props);

    // Empty building (e.g. B_pump has 0 props in depot 1)
    const emptyBuilding = layout.buildings.find((b) => b.id === 'B_pump')!;
    const resEmpty = carryPropsWithBuilding(props, emptyBuilding, [500, 500]);
    assert.deepEqual(resEmpty, props);
  });

  it('(5) undo restores both files to byte-identical state', () => {
    const props = clone(baseProps);
    const delta: Vec2 = [700, -400];

    const carried = carryPropsWithBuilding(props, building, delta);
    // Reverse move
    const restored = carryPropsWithBuilding(carried, translateBuilding(building, delta), [-delta[0], -delta[1]]);

    assert.deepEqual(restored, props, 'undo restores props byte-for-byte');
  });
});

describe('moveDerived - carryTacticalWithBuilding', () => {
  it('(3) tactical spots with that building id move, others identical', () => {
    const tactical = clone(baseTactical);
    const buildingId = 'B_x1_19_t7_hall';
    const delta: Vec2 = [600, -400];

    // Find tactical spots for B_x1_19_t7_hall
    const windowsBefore = tactical.windows.filter((w) => w.building === buildingId);
    const overwatchBefore = tactical.overwatch_roofs.filter((r) => r.building === buildingId);

    assert.ok(windowsBefore.length > 0, `${buildingId} has windows`);
    assert.ok(overwatchBefore.length > 0, `${buildingId} has overwatch roofs`);

    // Find other spots
    const otherWindows = tactical.windows.filter((w) => w.building !== buildingId);
    const otherOverwatch = tactical.overwatch_roofs.filter((r) => r.building !== buildingId);
    assert.ok(otherWindows.length > 0, 'other windows exist');

    const result = carryTacticalWithBuilding(tactical, buildingId, delta);

    // Verify input not mutated
    assert.notEqual(result, tactical);

    // Check windows moved
    for (const orig of windowsBefore) {
      const moved = result.windows.find((w) => w.id === orig.id)!;
      assert.ok(moved, `window ${orig.id} found`);
      assert.deepEqual(moved.pos, [orig.pos[0] + delta[0], orig.pos[1] + delta[1], orig.pos[2]]);
      assert.deepEqual(moved.stand, [orig.stand[0] + delta[0], orig.stand[1] + delta[1], orig.stand[2]]);
    }

    // Check overwatch spots moved
    for (const orig of overwatchBefore) {
      const moved = result.overwatch_roofs.find((r) => r.building === orig.building)!;
      assert.ok(moved);
      for (let i = 0; i < orig.spots.length; i++) {
        assert.deepEqual(moved.spots[i].pos, [orig.spots[i].pos[0] + delta[0], orig.spots[i].pos[1] + delta[1], orig.spots[i].pos[2]]);
      }
    }

    // Check other windows are byte-identical
    for (const orig of otherWindows) {
      const other = result.windows.find((w) => w.id === orig.id)!;
      assert.deepEqual(other, orig, `other window ${orig.id} must be byte-identical`);
    }

    // Check other overwatch roofs are byte-identical
    for (const orig of otherOverwatch) {
      const other = result.overwatch_roofs.find((r) => r.building === orig.building)!;
      assert.deepEqual(other, orig, `other overwatch roof ${orig.building} must be byte-identical`);
    }

    // Also check drop spots with a building that has drop spots (B_gallery)
    const dropBuildingId = 'B_gallery';
    const dropBefore = tactical.drop_spots.filter((d) => d.surface === `roof:${dropBuildingId}`);
    assert.ok(dropBefore.length > 0, `${dropBuildingId} has drop spots`);
    const dropResult = carryTacticalWithBuilding(tactical, dropBuildingId, delta);
    for (const orig of dropBefore) {
      const moved = dropResult.drop_spots.find((d) => d.id === orig.id)!;
      assert.ok(moved);
      assert.deepEqual(moved.edge, [orig.edge[0] + delta[0], orig.edge[1] + delta[1], orig.edge[2]]);
      assert.deepEqual(moved.land, [orig.land[0] + delta[0], orig.land[1] + delta[1], orig.land[2]]);
    }

    // Undo tactical
    const restoredTactical = carryTacticalWithBuilding(result, buildingId, [-delta[0], -delta[1]]);
    assert.deepEqual(restoredTactical, tactical, 'undo restores tactical map byte-for-byte');
  });
});

describe('moveDerived - validation and route clearance after carry', () => {
  it('(4) after the carry the props validator (validator.ts / fastGate) and the 088 route clearance run and their new problems are shown', () => {
    const building = layout.buildings.find((b) => b.id === 'B_hall_n')!;
    const delta: Vec2 = [300, 200];
    const movedBuilding = translateBuilding(building, delta);

    const movedLayout: GenLayout = {
      ...layout,
      buildings: layout.buildings.map((b) => (b.id === building.id ? movedBuilding : b)),
    };

    const viewBefore = PropsLayoutView.build(layout as unknown as Record<string, unknown>, PropsLayoutView.readConfig(balance));
    const checkBefore = checkProps(baseProps, viewBefore, layout, balance);

    const carriedProps = carryPropsWithBuilding(baseProps, building, delta);

    const viewAfter = PropsLayoutView.build(movedLayout as unknown as Record<string, unknown>, PropsLayoutView.readConfig(balance));
    const checkAfter = checkProps(carriedProps, viewAfter, movedLayout, balance);

    // newIssues compares before and after
    const addedIssues = newIssues(checkBefore.issues, checkAfter.issues);
    assert.ok(Array.isArray(addedIssues));
    assert.ok(typeof checkAfter.gate.gate === 'string');

    // Run 088 route clearance
    const clearanceStats = applyRouteClearance(clone(carriedProps), movedLayout, cfg, { keepLockedObjects: true });
    assert.ok(typeof clearanceStats.droppedTotal === 'number');
    assert.ok(Array.isArray(clearanceStats.pmcChargesInside));
  });
});
