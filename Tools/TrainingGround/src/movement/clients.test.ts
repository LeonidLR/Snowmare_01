// Real model clients with a fake fetch: no network, no key needed.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { advicePrompt, proposePrompt } from './ai.ts';
import type { AdvicePayload, ProposePayload } from './ai.ts';
import { apiKey, claudeClient, costOf, jevClient, type Env } from './clients.ts';

const KEY = 'sk-ant-test-SECRET123';
function envWith(responses: Response[], opts: { key?: string | null; reg?: string } = {}) {
  const calls: { url: string; init: RequestInit }[] = [];
  const sleeps: number[] = [];
  const key = opts.key === undefined ? KEY : opts.key;
  const env: Env = {
    fetch: (async (url: string, init: RequestInit) => { calls.push({ url, init }); const r = responses.shift(); if (!r) throw new Error('no more responses'); return r; }) as unknown as typeof fetch,
    getenv: (n) => ((n === 'ANTHROPIC_API_KEY' || n === 'TYPESAFE_API_KEY') && key ? key : undefined),
    readUserEnv: () => opts.reg,
    sleep: async (ms) => { sleeps.push(ms); },
  };
  return { env, calls, sleeps };
}
const json = (o: unknown, status = 200, headers: Record<string, string> = {}) => new Response(JSON.stringify(o), { status, headers });
const ok = { content: [{ type: 'thinking', thinking: '' }, { type: 'text', text: '{"params": {}}' }], usage: { input_tokens: 1000, output_tokens: 500 }, stop_reason: 'end_turn' };
const propose = proposePrompt({ kind: 'propose', layout: 'l', round: 1, best: { score: 0, summary: { checks: [], score: 0, allGreen: false }, params: {} }, history: [], tunable: {} } as unknown as ProposePayload);

test('prices: opus 5.5 $4/$20, sonnet 5.5 $2/$10 per MTok; unknown model throws', () => {
  assert.equal(costOf('claude-opus-5-5', 1_000_000, 1_000_000), 24);
  assert.equal(costOf('claude-sonnet-5-5', 1000, 500), 0.007);
  assert.throws(() => costOf('nope', 1, 1));
});

test('claude: request shape, usage and cost from the reply, key only in the header', async () => {
  const { env, calls } = envWith([json(ok)]);
  const reply = await claudeClient(env)!.complete(propose);
  assert.equal(reply.text, '{"params": {}}');
  assert.equal(reply.usage.input_tokens, 1000);
  assert.equal(reply.costUsd, costOf('claude-opus-5-5', 1000, 500));
  const body = JSON.parse(calls[0].init.body as string);
  assert.equal(body.model, 'claude-opus-5-5');
  assert.equal((calls[0].init.headers as Record<string, string>)['x-api-key'], KEY);
  assert.ok(!String(calls[0].init.body).includes(KEY));
  assert.equal(body.temperature, undefined);
});

test('claude: retries 429 / 5xx with backoff, then succeeds', async () => {
  const { env, calls, sleeps } = envWith([json({}, 429, { 'retry-after': '2' }), json({}, 529), json(ok)]);
  const reply = await claudeClient(env)!.complete(propose);
  assert.equal(calls.length, 3);
  assert.deepEqual(sleeps, [2000, 1000 * 2]);
  assert.ok(reply.costUsd > 0);
});

test('claude: gives up after the retries; 401 is a clear error without the key', async () => {
  const { env } = envWith(Array.from({ length: 3 }, () => json({}, 500)));
  await assert.rejects(claudeClient(env, { maxRetries: 2 })!.complete(propose), /HTTP 500 after 3 attempts/);
  const e2 = envWith([json({ error: { message: 'bad' } }, 401)]);
  await assert.rejects(claudeClient(e2.env)!.complete(propose), (e: Error) => /401 unauthorized/.test(e.message) && !e.message.includes(KEY));
  const e3 = envWith([json({ error: { message: `echo ${KEY}` } }, 400)]);
  await assert.rejects(claudeClient(e3.env)!.complete(propose), (e: Error) => !e.message.includes(KEY));
});

test('keys: environment, then HKCU; none -> factory returns null', () => {
  assert.equal(apiKey(envWith([], { key: null, reg: ' from-reg ' }).env, 'ANTHROPIC_API_KEY'), 'from-reg');
  assert.equal(claudeClient(envWith([], { key: null }).env), null);
  assert.equal(jevClient(envWith([], { key: null }).env), null);
});

test('jev: advice only, one typed question; propose is refused; network failure surfaces as an error', async () => {
  const summary = { checks: [{ id: 3, fail: 2, info: false }], score: 0.5, allGreen: false };
  const adv = advicePrompt({ kind: 'advice', layout: 'l', summary, params: {} } as unknown as AdvicePayload);
  const { env, calls } = envWith([json({ answers: { fix_first: { choice: 'check_3', confidence: 0.9 } }, usage: { input_tokens: 10, output_tokens: 2 } })]);
  const reply = await jevClient(env)!.complete(adv);
  assert.match(reply.text, /fix check 3 first/);
  assert.equal(calls[0].url, 'https://api.typesafe.ai/v1/systemone');
  assert.equal(reply.costUsd, 0);
  await assert.rejects(jevClient(env)!.complete(propose), /not parameter proposals/);
  const down = envWith([]);
  await assert.rejects(jevClient(down.env, { maxRetries: 0 })!.complete(adv), /network/);
});
