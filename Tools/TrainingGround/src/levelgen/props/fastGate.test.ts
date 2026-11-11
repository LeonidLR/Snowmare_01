// Training Ground / level generator: fast nav gate tests (PROPS_LAYERS.md section 2.4 and section 5 T3).
//
// Validates:
//   1. isPropNavAffecting correctly filters nav-affecting vs non-nav-affecting props.
//   2. fixture_small with default layers passes the gate.
//   3. fixture_small with a crate placed in the gate at [800, 0] fails with the failing leg named and blocker id.
//   4. Depot layout (gen_ref_01_02_depot_1) with default layers passes the fast gate.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import type { GenLayout } from '../schema.ts';
import type { LevelPropsFile, PropObject } from './schema.ts';
import { isPropNavAffecting, propToObstacleSegments, runFastGate } from './fastGate.ts';
import { PropsLayoutView } from './layoutView.ts';
import { rerollLayer } from './placement.ts';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const ROOT = path.resolve(__dirname, '../../../../..');

const FIXTURES_DIR = path.join(ROOT, 'Content/Data/LevelGen/props/fixtures');
const ACCEPTED_DIR = path.join(ROOT, 'Content/Data/LevelGen/accepted');
const PROPS_DIR = path.join(ROOT, 'Content/Data/LevelGen/props');
const BALANCE_PATH = path.join(ROOT, 'Content/Data/Balance/props_balance.json');

test('fast gate: isPropNavAffecting classifies props correctly', () => {
  const balance = JSON.parse(fs.readFileSync(BALANCE_PATH, 'utf-8'));
  const kinds = balance.kinds;

  assert.equal(isPropNavAffecting({ id: 'c1', variant: 'crate_s', pos: [0, 0], walk_layer: 'ground' }, kinds), true);
  assert.equal(isPropNavAffecting({ id: 'c2', variant: 'crate_l', pos: [0, 0], walk_layer: 'ground' }, kinds), true);
  assert.equal(isPropNavAffecting({ id: 'b1', variant: 'barrel', pos: [0, 0], walk_layer: 'ground' }, kinds), true);
  assert.equal(isPropNavAffecting({ id: 'b2', variant: 'barrel_explosive', pos: [0, 0], walk_layer: 'ground' }, kinds), true);
  assert.equal(isPropNavAffecting({ id: 't1', variant: 'turret_mg', pos: [0, 0], walk_layer: 'ground' }, kinds), true);
  assert.equal(isPropNavAffecting({ id: 'm1', variant: 'mine', pos: [0, 0], walk_layer: 'ground' }, false), false);
  assert.equal(isPropNavAffecting({ id: 'l1', variant: 'loot_pile', pos: [0, 0], walk_layer: 'ground' }, false), false);
});

test('fast gate: propToObstacleSegments produces valid obstacle segments', () => {
  const balance = JSON.parse(fs.readFileSync(BALANCE_PATH, 'utf-8'));
  const kinds = balance.kinds;

  const barrelSegs = propToObstacleSegments({ id: 'b1', variant: 'barrel', pos: [100, 200], walk_layer: 'ground' }, kinds);
  assert.equal(barrelSegs.length, 1);
  assert.equal(barrelSegs[0].thickness, 60);

  const crateSegs = propToObstacleSegments({ id: 'c1', variant: 'crate_s', pos: [100, 200], walk_layer: 'ground', yaw: 0 }, kinds);
  assert.equal(crateSegs.length, 6);
  assert.equal(crateSegs[0].propId, 'c1');
});

test('fast gate: fixture_small with default layers passes', () => {
  const layout: GenLayout = JSON.parse(fs.readFileSync(path.join(FIXTURES_DIR, 'fixture_small.layout.json'), 'utf-8'));
  const props: LevelPropsFile = JSON.parse(fs.readFileSync(path.join(FIXTURES_DIR, 'fixture_small.props.json'), 'utf-8'));
  const balance = JSON.parse(fs.readFileSync(BALANCE_PATH, 'utf-8'));

  const result = runFastGate(layout, props, balance);
  assert.equal(result.gate, 'pass', `expected pass but got ${result.gate} (${result.reason})`);
});

test('fast gate: fixture_small with a crate in the gate fails with leg named and blocker id', () => {
  const layout: GenLayout = JSON.parse(fs.readFileSync(path.join(FIXTURES_DIR, 'fixture_small.layout.json'), 'utf-8'));
  const props: LevelPropsFile = JSON.parse(fs.readFileSync(path.join(FIXTURES_DIR, 'fixture_small.props.json'), 'utf-8'));
  const balance = JSON.parse(fs.readFileSync(BALANCE_PATH, 'utf-8'));

  // Place a crate right inside the 300 cm compound gate at [800, 0] on the mandatory route
  const blockerCrate: PropObject = {
    id: 'crates_gate_blocker_0',
    variant: 'crate_s',
    pos: [800, 0],
    walk_layer: 'ground',
    yaw: 0,
    locked: false,
  };

  const modifiedProps: LevelPropsFile = {
    ...props,
    layers: props.layers.map((l) =>
      l.id === 'crates' ? { ...l, objects: [...l.objects, blockerCrate] } : l
    ),
  };

  const result = runFastGate(layout, modifiedProps, balance);
  assert.equal(result.gate, 'fail', 'expected fast gate to fail with crate in gate');
  assert.ok(result.gate_leg, 'expected gate_leg to be defined');
  // Route point 3 is [800, 0]; leg 3 is [900, 0] -> [800, 0], leg 4 is [800, 0] -> [300, 0]
  assert.match(result.gate_leg!, /leg [34]/, `expected failing leg to be leg 3 or leg 4, got: ${result.gate_leg}`);
  assert.ok(result.gate_blockers && result.gate_blockers.length > 0, 'expected gate_blockers to identify the blocker');
  assert.ok(result.gate_blockers!.includes('crates_gate_blocker_0'), `expected blockers to include crates_gate_blocker_0, got: ${result.gate_blockers}`);
});

test('fast gate: Depot layout with default layers passes', () => {
  const layout: GenLayout = JSON.parse(fs.readFileSync(path.join(ACCEPTED_DIR, 'gen_ref_01_02_depot_1.json'), 'utf-8'));
  const props: LevelPropsFile = JSON.parse(fs.readFileSync(path.join(PROPS_DIR, 'L_Gen_Depot_1.props.json'), 'utf-8'));
  const balance = JSON.parse(fs.readFileSync(BALANCE_PATH, 'utf-8'));

  const result = runFastGate(layout, props, balance);
  assert.equal(result.gate, 'pass', `expected Depot to pass gate, but got ${result.gate}: ${result.reason} at ${result.gate_leg}`);
});

test('fast gate: rerollLayer with layout sets report.gate to pass', () => {
  const layout: GenLayout = JSON.parse(fs.readFileSync(path.join(FIXTURES_DIR, 'fixture_small.layout.json'), 'utf-8'));
  const props: LevelPropsFile = JSON.parse(fs.readFileSync(path.join(FIXTURES_DIR, 'fixture_small.props.json'), 'utf-8'));
  const balance = JSON.parse(fs.readFileSync(BALANCE_PATH, 'utf-8'));
  const view = PropsLayoutView.build(JSON.stringify(layout), PropsLayoutView.readConfig(balance));

  // Reroll without layout -> gate remains 'n/a' (preserving golden fixture parity)
  const clonedProps1: LevelPropsFile = JSON.parse(JSON.stringify(props));
  rerollLayer(clonedProps1, balance, view, 'crates', 7);
  const cratesLayer1 = clonedProps1.layers.find((l) => l.id === 'crates');
  assert.equal(cratesLayer1?.report?.gate, 'n/a');

  // Reroll with layout -> runs fast gate and sets gate to 'pass'
  const clonedProps2: LevelPropsFile = JSON.parse(JSON.stringify(props));
  rerollLayer(clonedProps2, balance, view, 'crates', 7, layout);
  const cratesLayer2 = clonedProps2.layers.find((l) => l.id === 'crates');
  assert.equal(cratesLayer2?.report?.gate, 'pass');
});
