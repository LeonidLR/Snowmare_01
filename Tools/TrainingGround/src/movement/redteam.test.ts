// Training Ground / movement: the red team (FORMATION_CONTRACT.md 11) with the mock and fake clients - offline, deterministic.
// The regression layouts in Content/Data/FormationLab/redteam are reported (t.diagnostic), never gating: their known failures are
// expected_fail until the rules are fixed.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import type { ModelClient } from './ai.ts';
import { mockClient } from './ai.ts';
import type { RunMetrics } from './checks.ts';
import { claudeClient } from './clients.ts';
import type { Env } from './clients.ts';
import { readConfigFile, readLayout, REDTEAM_DIR } from './cli/paths.ts';
import { configValues } from './config.ts';
import type { RedteamOptions } from './redteam.ts';
import {
  ATTACK_FAMILIES, failureKey, keptFile, parseRedteam, redteam, redteamPrompt, refreshKept, regressLayout, renderRedteamReport, reproduces,
  runFailures, validateCandidate,
} from './redteam.ts';
import { mockRedteamText } from './redteamMock.ts';

const config = configValues(readConfigFile());
// Small and fast: one member count, one seed, auto + the hedgehog (the mock's zigzag breaks check 1 with it).
const base: RedteamOptions = {
  client: null, config, rounds: 1, budgetUsd: 5, perRound: 3, patience: 0, members: [4], seeds: 1, seedBase: 1,
  choices: ['auto', 'hedgehog'], roundBase: 0, dryRun: false,
};
const lines = (s: string) => s.trimEnd().split('\n').length;
const reportHeader = { client: 'mock', budgetUsd: 5, date: 'd', members: [4], seeds: 1, dir: 'x' };

test('dry run: builds the first request (families, schema, strict JSON), calls nobody, spends nothing', async () => {
  let called = false;
  const spy: ModelClient = { name: 'spy', async complete() { called = true; throw new Error('must not be called'); } };
  const res = await redteam({ ...base, client: spy, dryRun: true });
  assert.equal(called, false);
  assert.equal(res.spendUsd, 0);
  const req = res.dryRequest!;
  assert.equal(req.model, 'claude-opus-5-5');
  for (const f of ATTACK_FAMILIES) assert.ok(req.prompt.includes(f.id), f.id);
  assert.match(req.prompt, /strict JSON only/);
  assert.match(req.system, /CHECKS/);
  assert.ok(lines(renderRedteamReport(res, reportHeader)) <= 30);
});

test('mock loop: an invalid layout gets one retry, valid layouts are walked, failures are distinct and reproducible', async () => {
  const a = await redteam({ ...base, client: mockClient(1) });
  assert.deepEqual(a.calls.map((c) => c.attempt), ['design', 'retry'], 'round 1 #2 is broken on purpose -> one retry');
  const r = a.rounds[0];
  assert.equal(r.tried, 3);
  assert.equal(r.fixed, 1);
  assert.equal(r.valid, 3);
  // The mock's oblique-wall layout carries a synthetic, rules-independent failure (its zone expects the hedgehog): kept under any rules.
  assert.ok(a.kept.some((k) => k.failures.some((f) => f.check === 4 && f.choice === 'auto')), 'the synthetic check-4 failure is found');
  assert.equal(new Set(a.distinct.map((d) => d.key)).size, a.distinct.length);
  for (const k of a.kept) for (const f of k.failures) assert.ok(reproduces(k.layout, f, config), `${k.id} ${failureKey(f)} reproduces in one run`);
  const b = await redteam({ ...base, client: mockClient(1) });
  assert.deepEqual(b.distinct.map((d) => d.key), a.distinct.map((d) => d.key), 'deterministic');
  const report = renderRedteamReport(a, reportHeader);
  assert.ok(lines(report) <= 30);
  assert.match(report, /tried 3 \| valid 3 \(1 after the retry\)/);
});

test('kept file: comment + expected_fail with reasons; the regression of the same layout under the same rules: all known, nothing new', async () => {
  const res = await redteam({ ...base, client: mockClient(1) });
  const k = res.kept.find((x) => x.failures.some((f) => f.check === 4))!;
  const file = keptFile(k, { client: 'mock', configHash: 'h', members: [4], seeds: 1, seed: 1, date: 'd' });
  assert.match(String(file.comment), /^Red team 1_\d \(/);
  const ef = file.expected_fail as { reason: string }[];
  assert.equal(ef.length, k.failures.length);
  assert.ok(ef.every((e) => /known red-team failure/.test(e.reason)));
  const row = regressLayout('x.json', JSON.parse(JSON.stringify(file)), { config, members: [4], seeds: 1, seedBase: 1, choices: base.choices });
  assert.equal(row.known, k.failures.length);
  assert.deepEqual(row.fixed, []);
  assert.deepEqual(row.fresh, []);
});

test('budget: a paid client stops before the next call would exceed the cap; spend logged per call', async () => {
  const paid: ModelClient = {
    name: 'paid',
    async complete(req) {
      return { text: mockRedteamText(req.payload as never), usage: { input_tokens: 2000, output_tokens: 3000 }, costUsd: 0.4 };
    },
  };
  const res = await redteam({ ...base, client: paid, budgetUsd: 1, rounds: 5, perRound: 1, choices: ['auto'] });
  assert.ok(res.spendUsd <= 1 + 1e-9, `spent ${res.spendUsd}`);
  assert.equal(res.calls.length, 2);
  assert.match(res.stopReason, /budget/);
  assert.ok(res.calls.every((c) => c.costUsd === 0.4 && c.inputTokens === 2000 && !('prompt' in c)));
});

test('patience: rounds without a new distinct failure stop the loop', async () => {
  const res = await redteam({ ...base, client: mockClient(1), rounds: 8, perRound: 1, patience: 2, choices: ['auto'] });
  assert.match(res.stopReason, /no new distinct failure for 2 rounds/);
  assert.ok(res.rounds.length < 8);
});

test('a broken reply costs one retry with the error fed back, not the run', async () => {
  const prompts: string[] = [];
  const broken: ModelClient = { name: 'broken', async complete(req) { prompts.push(req.prompt); return { text: 'sorry, no JSON', usage: { input_tokens: 1, output_tokens: 1 }, costUsd: 0 }; } };
  const res = await redteam({ ...base, client: broken, rounds: 2 });
  assert.equal(res.calls.length, 4);
  assert.ok(res.calls.every((c) => c.error?.startsWith('parse')));
  assert.match(prompts[1], /could not be used: parse/);
  assert.ok(res.rounds.every((r) => r.error && r.tried === 0));
});

test('parseRedteam: wrapped / bare array / single layout / prose round it; garbage throws', () => {
  const lay = { walls: [], routes: [] };
  assert.equal(parseRedteam(`Here: {"layouts":[{"family":"u_turn","target_checks":[6],"intent":"x","layout":${JSON.stringify(lay)}}]} done`)[0].family, 'u_turn');
  assert.equal(parseRedteam(`[{"family":"a","layout":{}}, {"family":"b","layout":{}}]`).length, 2);
  assert.deepEqual(parseRedteam(JSON.stringify(lay))[0].layout, lay);
  assert.throws(() => parseRedteam('nothing here'));
});

test('validateCandidate: normalizes the name / schema; rejects a point in a wall, a start against a wall, an unreachable end', () => {
  const sample = readLayout('sample_small');
  const ok = validateCandidate({ ...sample, schema: 'whatever', name: 'model-name', comment: 'x', expected_fail: [] }, 'redteam_1_1', config);
  assert.deepEqual(ok.errors, []);
  assert.equal(ok.layout!.name, 'redteam_1_1');
  assert.ok(!('expected_fail' in ok.layout!));
  const inWall = structuredClone(sample);
  inWall.routes[0].points.splice(1, 0, [-500, -300]);
  assert.match(validateCandidate(inWall, 'n', config).errors.join('\n'), /route point inside a wall/);
  const start = structuredClone(sample);
  start.walls.push({ id: 'W_back', a: [-2700, -800], b: [-2700, 800], height: 300, thickness: 40 });
  start.routes[0].points[0] = [-2550, 0];
  assert.match(validateCandidate(start, 'n', config).errors.join('\n'), /starts round points\[0\]/);
  const boxed = structuredClone(sample);
  const B: [number, number][] = [[2300, 1300], [2700, 1300], [2700, 1700], [2300, 1700]];
  B.forEach((p, i) => boxed.walls.push({ id: `W_box${i}`, a: p, b: B[(i + 1) % 4], height: 300, thickness: 40 }));
  boxed.routes[0].points.push([2500, 1500]);
  assert.match(validateCandidate(boxed, 'n', config).errors.join('\n'), /unreachable/);
  assert.match(validateCandidate('x', 'n', config).errors[0], /JSON object/);
});

test('runFailures: auto is gated by every check, a manual formation only by 1 / 5 / 7', () => {
  const run = {
    route: 'R1', members: 4, seed: 1, duration: 10, distance: 1000,
    wallStare: { events: 1, worst: { t: 2, what: 'L at W1', value: 0.5 } }, rearCover: { walkTime: 10, pct: 50 },
    flicker: { changes: 0, unexpected: 0, per100m: 0, worst: null }, expect: [{ zone: 'Z1', formation: 'column', settled: 3, pct: 10 }],
    slots: { fallbackTicks: 0, invalidTicks: 0, spacingEvents: 0, worst: null }, pace: null, stuck: { events: 0, worst: null },
    split: { events: 1, worst: { t: 4, what: 'wall cuts Z', value: 0.4 } }, bypass: [], corner: { ends: 0, missed: 0, worst: null },
  } as RunMetrics;
  assert.deepEqual(runFailures(run, 'auto').map((f) => f.check), [1, 2, 4, 7]);
  assert.deepEqual(runFailures(run, 'echelon_left').map((f) => f.check), [1, 7]);
  assert.equal(failureKey(runFailures(run, 'auto')[3]), '#7 auto n4');
});

test('claude client: the red-team request asks for medium effort, a long timeout and room for the layouts', async () => {
  const bodies: string[] = [];
  const env: Env = {
    fetch: (async (_u: string, init: RequestInit) => { bodies.push(init.body as string); return new Response(JSON.stringify({ content: [{ type: 'text', text: '{}' }], usage: { input_tokens: 10, output_tokens: 10 }, stop_reason: 'end_turn' }), { status: 200 }); }) as unknown as typeof fetch,
    getenv: (n) => (n === 'ANTHROPIC_API_KEY' ? 'sk-ant-test' : undefined), readUserEnv: () => undefined, sleep: async () => {},
  };
  const req = redteamPrompt({ kind: 'redteam', round: 1, count: 4, families: [], found: [], previous: [] });
  await claudeClient(env)!.complete(req);
  const body = JSON.parse(bodies[0]);
  assert.equal(body.output_config.effort, 'medium');
  assert.ok(body.max_tokens >= 4 * 2200);
  assert.equal(req.timeoutMs, 600_000);
});

test('regression layouts (Content/Data/FormationLab/redteam): valid, expected_fail with reasons; known failures reported, not gating', (t) => {
  const files = fs.existsSync(REDTEAM_DIR) ? fs.readdirSync(REDTEAM_DIR).filter((f) => f.endsWith('.json')) : [];
  if (!files.length) { t.diagnostic('no red-team layouts yet'); return; }
  let known = 0, fixed = 0;
  for (const f of files) {
    const data = JSON.parse(fs.readFileSync(path.join(REDTEAM_DIR, f), 'utf8'));
    const v = validateCandidate(data, data.name, config);
    assert.deepEqual(v.errors, [], f);
    assert.ok(typeof data.comment === 'string' && data.comment.length > 0, `${f}: comment`);
    assert.ok(Array.isArray(data.expected_fail) && (data.expected_fail.length > 0 || typeof data.fixed_in === 'string'), `${f}: expected_fail or fixed_in`);
    for (const e of data.expected_fail) {
      assert.ok(typeof e.reason === 'string' && e.reason.length > 0, `${f}: reason`);
      // One run per entry (its minimal reproduction): still failing = known; passing = the rules fixed it (clear the entry).
      if (reproduces(v.layout!, e, config)) known++;
      else { fixed++; t.diagnostic(`${f} ${failureKey(e)} ${e.route} s${e.seed}: passes now - clear its expected_fail`); }
    }
  }
  t.diagnostic(`red-team regression: ${files.length} layouts, expected_fail still failing ${known}, fixed ${fixed}`);
});

test('UI view: picker groups (lab first, "Red team" in numeric order); comment + failures of a kept file; plain layouts -> null', async () => {
  const { groupLayoutNames, redteamView, seekFor } = await import('./redteamView.ts');
  const g = groupLayoutNames(['sample_small', 'redteam/10_1', 'formation_lab_01', 'redteam/2_3']);
  assert.deepEqual(g.map((x) => x.label), ['Лаборатория', 'Red team']);
  assert.deepEqual(g[1].names, ['redteam/2_3', 'redteam/10_1']);
  assert.deepEqual(groupLayoutNames(['sample_small']).map((x) => x.label), ['Лаборатория']);
  assert.equal(redteamView(readLayout('sample_small')), null);
  // A hand-made kept layout: the view must not depend on which failures the current rules still reproduce.
  const kept = { id: '1_1', family: 'u_turn', intent: 'x', targets: [7], layout: readLayout('sample_small'),
    failures: [{ check: 7, choice: 'auto' as const, members: 4, route: 'R1_walk', seed: 1, t: 12.5, what: 'wall cuts Z' }, { check: 1, choice: 'line' as const, members: 3, route: 'R2_run', seed: 2, t: null, what: 'L at W1' }] };
  const file = keptFile(kept, { client: 'mock', configHash: 'h', members: [4], seeds: 1, seed: 1, date: 'd' });
  const v = redteamView(JSON.parse(JSON.stringify(file)))!;
  assert.match(v.comment, /^Red team 1_1 [(]u_turn/);
  assert.equal(v.family, 'u_turn');
  assert.deepEqual(v.failures.map((x) => [x.check, x.route, x.choice]), [[7, 'R1_walk', 'auto'], [1, 'R2_run', 'line']]);
  assert.equal(redteamView({ ...file, expected_fail: [], fixed_in: 'v2.2' })!.failures.length, 0);
  assert.equal(seekFor({ t: 10 }), 8);
  assert.equal(seekFor({ t: 1 }), 0);
  assert.equal(seekFor({ t: null }), 0);
});

test('refreshKept: still-failing entries kept with new reasons, passing ones -> fixed_entries, nothing left -> fixed_in', () => {
  const f = (check: number, members: number) => ({ check, choice: 'auto' as const, members, route: 'R1', seed: 1, t: 3, what: 'x' });
  const data = { name: 'n', expected_fail: [{ ...f(7, 4), reason: 'old' }, { ...f(1, 3), reason: 'old' }] };
  const row = { file: 'n.json', known: 1, fixed: ['#1 auto n3'], fresh: [f(5, 2)], runs: 1, current: [f(7, 4), f(5, 2)], fixedChecks: [1], knownChecks: [7] };
  const out = refreshKept(data, row, 'v9') as { expected_fail: { check: number; reason: string; since?: string }[]; fixed_entries: { check: number; fixed_in: string }[]; fixed_in?: string };
  assert.deepEqual(out.expected_fail.map((e) => e.check), [7, 5]);
  assert.match(out.expected_fail[0].reason, /still failing under v9/);
  assert.equal(out.expected_fail[1].since, 'v9');
  assert.deepEqual(out.fixed_entries, [{ check: 1, choice: 'auto', members: 3, route: 'R1', seed: 1, fixed_in: 'v9' }]);
  assert.equal(out.fixed_in, undefined);
  const done = refreshKept(data, { ...row, known: 0, fixed: ['#7 auto n4', '#1 auto n3'], fresh: [], current: [] }, 'v9') as { expected_fail: unknown[]; fixed_in?: string };
  assert.deepEqual(done.expected_fail, []);
  assert.equal(done.fixed_in, 'v9');
});
