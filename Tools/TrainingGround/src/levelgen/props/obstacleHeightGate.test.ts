import assert from 'node:assert/strict';
import test from 'node:test';
import { OBSTACLE_HEIGHTS, classifyObstacleHeight, matchesObstacleHeight, obstacleHeight } from '../obstacleHeights.ts';
import { runObstacleHeightGate } from './obstacleHeightGate.ts';
import type { LevelPropsFile } from './schema.ts';
import { buildTrainingArena } from '../trainingArenas.ts';

for (const [key, c] of Object.entries(OBSTACLE_HEIGHTS)) {
  test(`obstacle heights: ${key} target and tolerance boundaries`, () => {
    assert.equal(classifyObstacleHeight(c.nominal_cm), key);
    assert.equal(matchesObstacleHeight(c.nominal_cm - c.tolerance_cm, key as keyof typeof OBSTACLE_HEIGHTS), true);
    assert.equal(matchesObstacleHeight(c.nominal_cm + c.tolerance_cm, key as keyof typeof OBSTACLE_HEIGHTS), true);
    assert.equal(c.measured, key !== 'step' && key !== 'wall_no_traverse');
  });
}

test('obstacle heights: gaps and malformed heights are rejected; taller walls remain walls', () => {
  for (const h of [60, 110, 175, 270, 0, -10, NaN, Infinity]) assert.equal(classifyObstacleHeight(h), undefined);
  assert.equal(classifyObstacleHeight(310), 'very_tall');
  assert.equal(classifyObstacleHeight(320), 'very_tall');
  assert.equal(classifyObstacleHeight(321), 'wall_no_traverse');
  assert.equal(classifyObstacleHeight(1200), 'wall_no_traverse');
});

test('obstacle height gate: generated arenas pass, including all nested obstacles', () => {
  for (const id of [1, 2, 3] as const) {
    const result = runObstacleHeightGate(buildTrainingArena(id).layout);
    assert.deepEqual(result, { gate: 'pass', errors: [] });
  }
});

test('obstacle height gate: errors retain the offending cover, wall and nested IDs', () => {
  const layout = buildTrainingArena(1).layout;
  layout.covers[0].size[2] = 110;
  layout.walls[0].height = 175;
  const b = layout.buildings.find((x) => x.parapets?.length)!;
  b.parapets![0].height_cm = 60;
  const result = runObstacleHeightGate(layout);
  assert.equal(result.gate, 'fail');
  assert.ok(result.errors.some((e) => e.id === layout.covers[0].id));
  assert.ok(result.errors.some((e) => e.id === layout.walls[0].id));
  assert.ok(result.errors.some((e) => e.id === b.parapets![0].wall_id));
});

test('obstacle height gate: enabled layer crate/barrel heights use physical balance dimensions', () => {
  const layout = buildTrainingArena(1).layout;
  const props: LevelPropsFile = { schema: 'codex.levelgen_props/1', map: 'test', layout: 'test', layout_hash: '', revision: 0, frozen: false, layers: [{ id: 'crates', kind: 'crates', enabled: true, seed: 1, locked: false, rules: {}, objects: [{ id: 'bad_crate', variant: 'crate_l', pos: [0, 0], walk_layer: 'ground' }] }] };
  const result = runObstacleHeightGate(layout, props, { crate_l: { size_cm: [160, 160, 110] } });
  assert.equal(result.gate, 'fail');
  assert.equal(result.errors[0].id, 'bad_crate');
  assert.equal(runObstacleHeightGate(layout, props, { crate_l: { size_cm: [160, 160, obstacleHeight('low_mantle')] } }).gate, 'pass');
});

// Same height can support different actions; explicit class matching must preserve both possibilities.
test('measured height bands: overlaps do not erase explicit classes', () => {
  assert.equal(matchesObstacleHeight(95, 'low_mantle'), true);
  assert.equal(matchesObstacleHeight(95, 'low_vault'), true);
  assert.equal(classifyObstacleHeight(95, 'low_mantle'), 'low_mantle');
  assert.equal(classifyObstacleHeight(135, 'high_fence_over'), 'high_fence_over');
  assert.equal(classifyObstacleHeight(135, 'high_mantle'), 'high_mantle');
});
