// Training Ground / movement: simulator + bot checks (node --test). Determinism, the sample layout's expectations and the
// report contract (<= 30 lines).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { configHash, runSeries } from './bot.ts';
import { renderReport } from './checks.ts';
import { configValues, withValues } from './config.ts';
import { readConfigFile, readLayout } from './cli/paths.ts';
import { simulate } from './sim.ts';
import type { Frame } from './sim.ts';

const layout = readLayout('sample_small');
const file = readConfigFile();
const config = configValues(file);

test('the simulator is deterministic (same seed -> identical runs)', () => {
  const a = runSeries({ layout, config, members: [3, 4], seeds: 2, seedBase: 5 });
  const b = runSeries({ layout, config, members: [3, 4], seeds: 2, seedBase: 5 });
  assert.deepEqual(a.runs, b.runs);
  assert.equal(configHash(config), configHash({ ...config }));
});

test('sample layout: the terrain picks the expected formations, slots stay walkable, no flicker', () => {
  const { summary } = runSeries({ layout, config, members: [2, 3, 4], seeds: 2, seedBase: 1 });
  const byId = Object.fromEntries(summary.checks.map((c) => [c.id, c]));
  assert.equal(byId[4].fail, 0, `expected formation per zone: ${byId[4].worst}`);
  assert.equal(byId[5].fail, 0, `slots: ${byId[5].worst}`);
  assert.equal(byId[3].fail, 0, `flicker: ${byId[3].worst}`);
  assert.ok(summary.score >= 0 && summary.score <= 1);
});

test('frames: run = everyone faces the travel direction; patrol = the rear man walks backwards', () => {
  const frames: Frame[] = [];
  const walk = layout.routes.find((r) => r.pace === 'walk')!;
  simulate({ layout, route: walk, memberCount: 4, config, seed: 1, onFrame: (f) => frames.push(f) });
  const patrol = frames.filter((f) => f.mode === 'walk' && f.formation === 'diamond' && f.t > 2);
  assert.ok(patrol.length > 30);
  assert.ok(patrol.filter((f) => f.members.find((m) => m.place === 'Z')!.backwards).length / patrol.length > 0.8, 'the man at the rear place walks backwards in the diamond');
  const runFrames: Frame[] = [];
  const run = layout.routes.find((r) => r.pace === 'run')!;
  simulate({ layout, route: run, memberCount: 4, config, seed: 1, onFrame: (f) => runFrames.push(f) });
  const moving = runFrames.filter((f) => f.mode === 'run' && f.t > 2);
  assert.ok(moving.every((f) => f.members.every((m) => !m.backwards)), 'nobody runs backwards');
});

test('the tuner levers move the checks (the wall stare follows look.wall_dist)', () => {
  // v2.2: the walking sectors of 3-4 come from the coverage allocator; ClearLook's reach is a lever the checks feel - below the
  // stare check's 200 cm the side men look into the corridor walls, the shipped 260 keeps them out.
  const old = runSeries({ layout, config: configValues(withValues(file, { 'look.wall_dist': 100 }).file), members: [4], seeds: 1, seedBase: 1 }).summary;
  const now = runSeries({ layout, config, members: [4], seeds: 1, seedBase: 1 }).summary;
  const stare = (s: typeof now) => s.checks.find((c) => c.id === 1)!;
  assert.equal(config['look.wall_dist'], 260);
  assert.ok(stare(now).pass > stare(old).pass);
});
test('report: <= 30 lines, header, one line per check, advice section', () => {
  const { summary } = runSeries({ layout, config, members: [4], seeds: 1, seedBase: 1 });
  const header = { layout: 'sample_small', routes: 2, members: [4], seeds: 1, seed: 1, configHash: 'x', date: 'd' };
  const plain = renderReport(header, summary, null);
  assert.ok(plain.trimEnd().split('\n').length <= 30);
  assert.match(plain, /no model advice/);
  const advised = renderReport(header, summary, Array.from({ length: 15 }, (_, i) => `line ${i}`));
  assert.ok(advised.trimEnd().split('\n').length <= 30);
  assert.match(advised, /model advice:/);
});
