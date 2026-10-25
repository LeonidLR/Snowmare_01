// Training Ground / movement + level design: dev-server endpoints (vite.config.ts). They read the layout / config files and
// run the Node tools (bot, tuner) as child processes; the browser simulates with the same pure modules.
//   GET  /api/movement/layouts            -> { layouts: string[] }      (Content/Data/FormationLab/*.json, the config excluded)
//   GET  /api/movement/layout?name=<n>    -> the layout JSON
//   GET  /api/movement/config             -> formation_config.json
//   GET  /api/movement/report?kind=bot|train -> { text } (the last report.md / train_report.md)
//   POST /api/movement/run-bot  { layout, members, seeds }              -> { text, ok } (report.md)
//   POST /api/movement/train    { layout, client, budgetUsd, rounds, dryRun } -> { text, ok } (train_report.md)
import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

// fileURLToPath, not URL.pathname: the pathname keeps %20 for the space in "Unreal Projects" (ENOENT on Windows).
const TOOL_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const ROOT = path.resolve(TOOL_DIR, '../..');
const LAB_DIR = path.join(ROOT, 'Content/Data/FormationLab');
const OUT_DIR = path.join(ROOT, 'Saved/TrainingGround/movement');
const CONFIG = path.join(LAB_DIR, 'formation_config.json');
const NAME = /^[A-Za-z0-9_-]+$/;

type Req = { method?: string; url?: string; on: (ev: string, cb: (chunk?: unknown) => void) => void };
type Res = { statusCode: number; setHeader: (k: string, v: string) => void; end: (body?: string) => void };
type Use = (route: string, handler: (req: Req, res: Res, next?: () => void) => void) => void;

const send = (res: Res, status: number, data: unknown) => {
  res.statusCode = status;
  res.setHeader('Content-Type', 'application/json; charset=utf-8');
  res.end(JSON.stringify(data));
};

const readBody = (req: Req): Promise<Record<string, unknown>> => new Promise((resolve) => {
  let body = '';
  req.on('data', (c) => { body += String(c); });
  req.on('end', () => { try { resolve(body ? JSON.parse(body) : {}); } catch { resolve({}); } });
});

/** Runs a movement CLI with node (type stripping), returns its exit code and output. */
function runTool(script: string, args: string[]): Promise<{ code: number; out: string; err: string }> {
  return new Promise((resolve) => {
    const child = spawn(process.execPath, [path.join(TOOL_DIR, 'src/movement/cli', script), ...args], { cwd: TOOL_DIR });
    let out = '', err = '';
    child.stdout.on('data', (c) => { out += String(c); });
    child.stderr.on('data', (c) => { err += String(c); });
    const timer = setTimeout(() => child.kill(), 10 * 60 * 1000);
    child.on('close', (code) => { clearTimeout(timer); resolve({ code: code ?? 1, out, err }); });
  });
}

const layoutNames = () => fs.existsSync(LAB_DIR)
  ? fs.readdirSync(LAB_DIR).filter((f) => f.endsWith('.json') && f !== 'formation_config.json').map((f) => f.slice(0, -5)).sort()
  : [];

export function setupMovementApi(use: Use): void {
  use('/api/movement/layouts', (_req, res) => send(res, 200, { layouts: layoutNames() }));

  use('/api/movement/layout', (req, res) => {
    const name = new URL(req.url ?? '', 'http://x').searchParams.get('name') ?? '';
    if (!NAME.test(name) || !layoutNames().includes(name)) return send(res, 404, { error: `no layout "${name}"` });
    send(res, 200, JSON.parse(fs.readFileSync(path.join(LAB_DIR, `${name}.json`), 'utf8')));
  });

  use('/api/movement/config', (_req, res) => send(res, 200, JSON.parse(fs.readFileSync(CONFIG, 'utf8'))));

  use('/api/movement/report', (req, res) => {
    const kind = new URL(req.url ?? '', 'http://x').searchParams.get('kind') === 'train' ? 'train_report.md' : 'report.md';
    const file = path.join(OUT_DIR, kind);
    send(res, 200, { text: fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : '' });
  });

  use('/api/movement/run-bot', async (req, res, next) => {
    if (req.method !== 'POST') return next ? next() : send(res, 405, { error: 'POST only' });
    const b = await readBody(req);
    const layout = String(b.layout ?? 'sample_small');
    if (!NAME.test(layout)) return send(res, 400, { error: 'bad layout name' });
    const members = String(b.members ?? '2,3,4').replace(/[^0-9,]/g, '');
    const seeds = String(Math.max(1, Math.min(50, Number(b.seeds) || 4)));
    const r = await runTool('bot.ts', ['--layout', layout, '--members', members, '--seeds', seeds]);
    send(res, r.code === 0 ? 200 : 500, { ok: r.code === 0, text: r.out, log: r.err.slice(-2000) });
  });

  use('/api/movement/train', async (req, res, next) => {
    if (req.method !== 'POST') return next ? next() : send(res, 405, { error: 'POST only' });
    const b = await readBody(req);
    const layout = String(b.layout ?? 'sample_small');
    if (!NAME.test(layout)) return send(res, 400, { error: 'bad layout name' });
    const client = ['claude', 'jev', 'mock'].includes(String(b.client)) ? String(b.client) : 'mock';
    const args = ['--layout', layout, '--client', client,
      '--budget-usd', String(Math.max(0, Math.min(50, Number(b.budgetUsd) || 5))),
      '--rounds', String(Math.max(1, Math.min(50, Number(b.rounds) || 10)))];
    if (b.dryRun) args.push('--dry-run');
    const r = await runTool('train.ts', args);
    send(res, r.code === 0 ? 200 : 500, { ok: r.code === 0, text: r.out, log: r.err.slice(-2000) });
  });
}
