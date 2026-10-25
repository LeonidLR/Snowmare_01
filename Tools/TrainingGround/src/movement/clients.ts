// Training Ground / movement: the real model clients (FORMATION_CONTRACT.md 6) - the Anthropic Messages API and TypeSafe Jev over
// plain fetch (the repo has no SDK dependency). Keys: process.env, then the Windows HKCU user environment; never logged, printed
// or written to a file. Everything that touches the outside world (fetch, env, registry, sleep) is injectable, so the tests
// run offline with a fake fetch. Conventions follow Scripts/Tools/visual_qa.py and Scripts/Tools/jev_client.py.

import { execFileSync } from 'node:child_process';
import type { ModelClient, ModelReply, ModelRequest } from './ai.ts';
import { registerRealClient } from './ai.ts';

/** USD per 1M tokens. Source: the claude-api skill model table (cached 2026-10-06; Anthropic first-party rates). One place. */
export const PRICES_PER_MTOK: Record<string, { input: number; output: number }> = {
  'claude-opus-5-5': { input: 4, output: 20 },
  'claude-sonnet-5-5': { input: 2, output: 10 },
  'claude-haiku-5-5': { input: 0.1, output: 0.5 },
};

export function costOf(model: string, inputTokens: number, outputTokens: number): number {
  const p = PRICES_PER_MTOK[model];
  if (!p) throw new Error(`no price for model "${model}" - add it to PRICES_PER_MTOK`);
  return Math.round(((inputTokens * p.input + outputTokens * p.output) / 1e6) * 1e6) / 1e6;
}

export interface Env {
  fetch: typeof fetch;
  getenv: (name: string) => string | undefined;
  /** Reads a value from the Windows HKCU\Environment; undefined elsewhere / when absent. */
  readUserEnv: (name: string) => string | undefined;
  sleep: (ms: number) => Promise<void>;
}

export function readHkcuEnv(name: string): string | undefined {
  if (process.platform !== 'win32') return undefined;
  try {
    const out = execFileSync('reg', ['query', 'HKCU\\Environment', '/v', name], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] });
    const m = out.match(new RegExp(`^\\s*${name}\\s+REG_\\w+\\s+(.+?)\\s*$`, 'm'));
    return m ? m[1] : undefined;
  } catch { return undefined; }
}

export const realEnv: Env = {
  fetch: (...a) => fetch(...a),
  getenv: (n) => process.env[n],
  readUserEnv: readHkcuEnv,
  sleep: (ms) => new Promise((r) => setTimeout(r, ms)),
};

export function apiKey(env: Env, name: string): string | null {
  const k = (env.getenv(name) ?? env.readUserEnv(name) ?? '').trim();
  return k || null;
}

/** Removes anything key-shaped from a message before it can reach a log. */
function scrub(text: string, key: string): string {
  return text.split(key).join('***').replace(/sk-ant-[A-Za-z0-9_-]+/g, '***').slice(0, 300);
}

export interface ClientOptions { maxRetries?: number; baseDelayMs?: number; timeoutMs?: number }

/** Anthropic Messages API client. Returns null without a key. */
export function claudeClient(env: Env = realEnv, opt: ClientOptions = {}): ModelClient | null {
  const key = apiKey(env, 'ANTHROPIC_API_KEY');
  if (!key) return null;
  const maxRetries = opt.maxRetries ?? 4, baseDelay = opt.baseDelayMs ?? 1000, timeoutMs = opt.timeoutMs ?? 120_000;
  return {
    name: 'claude',
    async complete(req: ModelRequest): Promise<ModelReply> {
      // Opus 5.5 always thinks (thinking cannot be disabled; thinking tokens are billed and count against max_tokens), so
      // give headroom and keep the effort low: the answer is a small JSON object.
      const body = JSON.stringify({
        model: req.model,
        max_tokens: req.maxTokens + 4000,
        system: req.system,
        messages: [{ role: 'user', content: req.prompt }],
        output_config: { effort: req.effort ?? 'low' },
      });
      let lastErr = 'unknown error';
      for (let attempt = 0; attempt <= maxRetries; attempt++) {
        let res: Response;
        try {
          res = await env.fetch('https://api.anthropic.com/v1/messages', {
            method: 'POST',
            headers: { 'x-api-key': key, 'anthropic-version': '2023-06-01', 'content-type': 'application/json' },
            body, signal: AbortSignal.timeout(req.timeoutMs ?? timeoutMs),
          });
        } catch (e) {
          lastErr = `network: ${scrub(e instanceof Error ? e.message : String(e), key)}`;
          if (attempt < maxRetries) { await env.sleep(baseDelay * 2 ** attempt); continue; }
          break;
        }
        if (res.status === 401) throw new Error('Anthropic API: 401 unauthorized - ANTHROPIC_API_KEY is invalid or revoked');
        if (res.status === 429 || res.status >= 500) {
          lastErr = `HTTP ${res.status}`;
          if (attempt < maxRetries) {
            const ra = Number(res.headers.get('retry-after'));
            await env.sleep(Number.isFinite(ra) && ra > 0 ? Math.min(ra * 1000, 60_000) : baseDelay * 2 ** attempt);
            continue;
          }
          break;
        }
        const text = await res.text();
        if (!res.ok) throw new Error(`Anthropic API: HTTP ${res.status} ${scrub(text, key)}`);
        const data = JSON.parse(text) as { content?: { type: string; text?: string }[]; usage?: { input_tokens?: number; output_tokens?: number }; stop_reason?: string };
        const usage = { input_tokens: data.usage?.input_tokens ?? 0, output_tokens: data.usage?.output_tokens ?? 0 };
        if (data.stop_reason === 'refusal') throw new Error('Anthropic API: the model refused the request');
        const out = (data.content ?? []).filter((b) => b.type === 'text').map((b) => b.text ?? '').join('');
        return { text: out, usage, costUsd: costOf(req.model, usage.input_tokens, usage.output_tokens) };
      }
      throw new Error(`Anthropic API: ${lastErr} after ${maxRetries + 1} attempts`);
    },
  };
}

const JEV_URL = 'https://api.typesafe.ai/v1/systemone';

/**
 * TypeSafe Jev (System One) gives typed judgments, not free text, so this client serves the 'advice' purpose only: it asks which
 * check to fix first and answers with one line. 'propose' needs strict JSON parameters - use the claude client. Jev has no known
 * per-token price table, so costUsd is 0 (tokens are logged). Returns null without a key.
 */
export function jevClient(env: Env = realEnv, opt: ClientOptions = {}): ModelClient | null {
  const key = apiKey(env, 'TYPESAFE_API_KEY');
  if (!key) return null;
  const maxRetries = opt.maxRetries ?? 3, baseDelay = opt.baseDelayMs ?? 1000;
  return {
    name: 'jev',
    async complete(req: ModelRequest): Promise<ModelReply> {
      if (req.payload.kind !== 'advice') throw new Error('Jev gives typed judgments, not parameter proposals - use --client claude for the tuner');
      const checks = req.payload.summary.checks.filter((c) => !c.info && c.fail > 0);
      if (!checks.length) return { text: 'all checks green - no change suggested', usage: { input_tokens: 0, output_tokens: 0 }, costUsd: 0 };
      const criteria = Object.fromEntries(checks.map((c) => [`check_${c.id}`, `Check ${c.id} fails ${c.fail} times`]));
      const body = JSON.stringify({
        state: req.prompt, model: 'jev-latest',
        questions: { fix_first: { type: 'choice', instructions: 'Which failing check should the tuner fix first (the root cause of the others)?', criteria } },
      });
      let lastErr = 'unknown error';
      for (let attempt = 0; attempt <= maxRetries; attempt++) {
        let res: Response;
        try {
          res = await env.fetch(JEV_URL, { method: 'POST', headers: { Authorization: `Bearer ${key}`, 'Content-Type': 'application/json' }, body, signal: AbortSignal.timeout(opt.timeoutMs ?? 60_000) });
        } catch (e) {
          lastErr = `network: ${scrub(e instanceof Error ? e.message : String(e), key)}`;
          if (attempt < maxRetries) { await env.sleep(baseDelay * 2 ** attempt); continue; }
          break;
        }
        if (res.status === 401 || res.status === 403) throw new Error(`Jev API: ${res.status} - TYPESAFE_API_KEY rejected`);
        if (res.status === 429 || res.status === 529 || res.status >= 500) {
          lastErr = `HTTP ${res.status}`;
          if (attempt < maxRetries) { await env.sleep(baseDelay * 2 ** attempt); continue; }
          break;
        }
        const text = await res.text();
        if (!res.ok) throw new Error(`Jev API: HTTP ${res.status} ${scrub(text, key)}`);
        const data = JSON.parse(text) as { answers?: Record<string, { choice?: string; selected?: string; confidence?: number }>; usage?: { input_tokens?: number; output_tokens?: number } };
        const a = data.answers?.fix_first;
        const choice = a?.choice ?? a?.selected ?? 'unknown';
        return {
          text: `Jev: fix ${choice.replace('check_', 'check ')} first${a?.confidence !== undefined ? ` (confidence ${a.confidence})` : ''}`,
          usage: { input_tokens: data.usage?.input_tokens ?? 0, output_tokens: data.usage?.output_tokens ?? 0 }, costUsd: 0,
        };
      }
      throw new Error(`Jev API: ${lastErr} after ${maxRetries + 1} attempts`);
    },
  };
}

/** Registers 'claude' and 'jev' with resolveClient (called by the CLI). */
export function registerClients(env: Env = realEnv): void {
  registerRealClient('claude', () => claudeClient(env));
  registerRealClient('jev', () => jevClient(env));
}
