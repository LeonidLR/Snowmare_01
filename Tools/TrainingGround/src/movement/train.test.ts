// Training Ground / movement: the tuner loop (FORMATION_CONTRACT.md 6) with the mock and fake clients - no network.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { ModelClient } from './ai.ts';
import { FIXED_PARAMS, mockClient, parsePropose, resolveClient } from './ai.ts';
import { readConfigFile, readLayout } from './cli/paths.ts';
import { withValues } from './config.ts';
import { renderTrainReport, train, tunableOf } from './train.ts';

const layout = readLayout('sample_small');
// The tuner needs something to tune: sample_small is all green with the shipped config, so the tests start from a short
// ClearLook reach (look.wall_dist 100 < the stare check's 200) with the allocator in its corridors (column.narrow_width 100:
// v2.3 keeps the 1A corridor looks, which stay off the walls whatever the reach) - check 1 fails; the lever bot.test.ts uses.
const configFile = withValues(readConfigFile(), { 'look.wall_dist': 100, 'column.narrow_width': 100 }).file;
const base = { layout, configFile, rounds: 6, budgetUsd: 5, patience: 3, members: [3, 4], seeds: 1, seedBase: 1 };

test('dry run: builds the first request, calls nobody, spends nothing', async () => {
  let called = false;
  const spy: ModelClient = { name: 'spy', async complete() { called = true; throw new Error('must not be called'); } };
  const res = await train({ ...base, client: spy, dryRun: true });
  assert.equal(called, false);
  assert.equal(res.spendUsd, 0);
  assert.ok(res.dryRequest && res.dryRequest.prompt.includes('strict JSON') && res.dryRequest.estInputTokens > 100);
  assert.equal(res.dryRequest!.model, 'claude-opus-5-5');
  const report = renderTrainReport(res, { layout: 'sample_small', client: 'none', budgetUsd: 5, date: 'd' });
  assert.ok(report.trimEnd().split('\n').length <= 30);
  assert.match(report, /dry run/);
});

test('mock client: the loop keeps the best set (never worse than the baseline) and reports the diff', async () => {
  const res = await train({ ...base, client: mockClient(1), dryRun: false });
  assert.ok(res.best.score >= res.baseline.score);
  assert.ok(res.rounds.length >= 1);
  assert.ok(res.stopReason.length > 0);
  const report = renderTrainReport(res, { layout: 'sample_small', client: 'mock', budgetUsd: 5, date: 'd' });
  assert.ok(report.trimEnd().split('\n').length <= 30);
  if (res.best.round > 0) assert.match(report, /winning parameters \(diff/);
});

test('budget: a paid client stops before the next call would exceed the cap; spend is logged per call', async () => {
  const paid: ModelClient = {
    name: 'paid',
    async complete() {
      // A change that moves no check (the hedgehog is not walked), so only the budget can stop the loop.
      return { text: '{"params": {"hedgehog.radius": 140}, "reason": "x"}', usage: { input_tokens: 1000, output_tokens: 50 }, costUsd: 0.4 };
    },
  };
  const res = await train({ ...base, client: paid, dryRun: false, budgetUsd: 1, rounds: 10, patience: 10 });
  assert.ok(res.spendUsd <= 1 + 1e-9, `spent ${res.spendUsd}`);
  assert.equal(res.calls.length, 2);
  assert.match(res.stopReason, /budget/);
  assert.ok(res.calls.every((c) => c.costUsd === 0.4 && c.inputTokens === 1000));
});

test('a broken model reply costs a round, not the run; fixed / unknown keys are dropped', async () => {
  const broken: ModelClient = { name: 'broken', async complete() { return { text: 'sorry, no JSON', usage: { input_tokens: 1, output_tokens: 1 }, costUsd: 0 }; } };
  const res = await train({ ...base, client: broken, dryRun: false, patience: 2 });
  assert.equal(res.rounds.length, 2);
  assert.ok(res.rounds.every((r) => r.error));
  assert.match(res.stopReason, /no improvement/);
  const tunable = tunableOf(configFile);
  for (const k of FIXED_PARAMS) assert.ok(!(k in tunable));
  const parsed = parsePropose('x {"params": {"pace.walk_speed": 300, "nope": 1, "pace.patrol": 0.7}, "reason": "r"} y', tunable);
  assert.deepEqual(parsed.params, { 'pace.patrol': 0.7 });
});

test('resolveClient: mock always works; real clients report "not wired" until registered locally', () => {
  assert.equal(resolveClient('mock').client?.name, 'mock');
  const claude = resolveClient('claude');
  assert.equal(claude.client, null);
  assert.match(claude.note, /not wired/);
});
