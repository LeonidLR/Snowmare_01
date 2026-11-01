// Training Ground department «Боевая лаборатория» (game designer 2026-11-01, docs/design/COMBAT_LAB.md section 7): dev-server endpoints
// (vite.config.ts). Only L_FormationLabCombat: its level JSON blocks "pressure" / "cheats" (part A), the named presets, the bot batches
// in Unreal (part C: Scripts/bot_run.ps1 -ReportDir, the bot's route mode) and their reports.
//   GET  /api/combat-lab/settings                -> { file, level_id, settings, route, presetsFile }
//   POST /api/combat-lab/settings { settings, base? } -> validated (src/combatLab/pressureSettings.ts), a backup in
//                                                   Saved/TrainingGround/combat_lab/backups, only the changed values rewritten -> { diff, backup }
//   GET  /api/combat-lab/presets                 -> Content/Data/CombatLab/presets.json
//   POST /api/combat-lab/presets { preset }      -> added / replaced by id
//   GET  /api/combat-lab/layout                  -> { bounds, walls, towers } for the map preview (formation_lab_01 + the combat overlay)
//   POST /api/combat-lab/run { runs, minutes, parallel, settings, preset? } -> a batch in Saved/Telemetry/combat_lab/<id> (level.json with the
//                                                   page's settings, batch.json), bot_run.ps1 started detached -> { batch }
//   GET  /api/combat-lab/status                  -> bot_status.json (+ the live snapshots of the running games)
//   GET  /api/combat-lab/batches                 -> { batches: [...] } newest first
//   GET  /api/combat-lab/report?batch=<id>       -> { runs, skipped, jev, markdown, batch, log }
import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { parseJsonText, patchJsonText } from '../src/utils/jsonTextPatch.ts';
import {
  applySettings, diffSettings, isJsonObject, pickSettings, upsertPreset, validatePreset, validatePresetFile, validateSettings, PRESETS_SCHEMA,
} from '../src/combatLab/pressureSettings.ts';
import type { CombatLabPreset, CombatLabPresetFile, CombatLabSettings } from '../src/combatLab/pressureSettings.ts';
import { batchId, parseRunsJsonl, realTimeoutSec, validateRunRequest } from '../src/combatLab/report.ts';
import type { LiveRun } from '../src/combatLab/report.ts';

const TOOL_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const ROOT = path.resolve(TOOL_DIR, '../..');
export const COMBAT_LAB_LEVEL = 'L_FormationLabCombat';
const MAP = '/Game/Maps/L_FormationLabCombat';
const LEVEL_FILE = path.join(ROOT, 'Content/Data/LevelJson', `${COMBAT_LAB_LEVEL}.json`);
const PRESETS_FILE = path.join(ROOT, 'Content/Data/CombatLab/presets.json');
const LAYOUT_FILE = path.join(ROOT, 'Content/Data/FormationLab/Combat/formation_lab_combat_01.json');
const BACKUP_DIR = path.join(ROOT, 'Saved/TrainingGround/combat_lab/backups');
const TELEMETRY_DIR = path.join(ROOT, 'Saved/Telemetry');
const BATCHES_DIR = path.join(TELEMETRY_DIR, 'combat_lab');
const STATUS_FILE = path.join(TELEMETRY_DIR, 'bot_status.json');
const BOT_RUN = path.join(ROOT, 'Scripts/bot_run.ps1');
const BATCH_ID = /^\d{8}_\d{6}$/;

type Req = { method?: string; url?: string; on: (ev: string, cb: (chunk?: unknown) => void) => void };
type Res = { statusCode: number; setHeader: (k: string, v: string) => void; end: (body?: string) => void };
type Use = (route: string, handler: (req: Req, res: Res, next?: () => void) => void) => void;

const send = (res: Res, status: number, data: unknown) => {
  res.statusCode = status;
  res.setHeader('Content-Type', 'application/json; charset=utf-8');
  res.end(JSON.stringify(data));
};

const readBody = (req: Req): Promise<unknown> => new Promise((resolve) => {
  let body = '';
  req.on('data', (c) => { body += String(c); });
  req.on('end', () => { try { resolve(body ? JSON.parse(body) : {}); } catch { resolve(null); } });
});

const readText = (file: string) => (fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : null);
const readJson = (file: string): unknown => {
  const text = readText(file);
  return text === null ? null : parseJsonText(text);
};

/** The time stamp of a backup name (UTC, sortable). */
const stamp = () => new Date().toISOString().replace(/[-:]/g, '').replace(/\.\d+Z$/, 'Z');

/** The bot route of the level as the page draws it. */
function routeView(level: unknown) {
  const route = isJsonObject(level) && isJsonObject(level.bot_route) ? level.bot_route : null;
  return route ? { name: route.name ?? '', loop: route.loop !== false, points: Array.isArray(route.points) ? route.points : [] } : null;
}

/** Is a batch runner alive (bot_status.json isRunning with a live pid)? */
function runningStatus(): Record<string, unknown> | null {
  const status = readJson(STATUS_FILE);
  if (!isJsonObject(status) || status.isRunning !== true) return null;
  try {
    process.kill(Number(status.pid), 0);
    return status;
  } catch {
    return null;
  }
}

function liveRuns(dir: string): LiveRun[] {
  if (!fs.existsSync(dir)) return [];
  const out: LiveRun[] = [];
  for (const f of fs.readdirSync(dir).filter((n) => /^run_\d+\.live\.json$/.test(n))) {
    try {
      const live = JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8')) as LiveRun;
      const finished = fs.existsSync(path.join(dir, f.replace('.live.json', '.json')));
      if (!finished) out.push(live);
    } catch { /* being written: next poll */ }
  }
  return out.sort((a, b) => a.run - b.run);
}

export function setupCombatLabApi(use: Use): void {
  use('/api/combat-lab/settings', async (req, res) => {
    try {
      const text = readText(LEVEL_FILE);
      if (text === null) return send(res, 404, { error: `${COMBAT_LAB_LEVEL}.json not found` });
      const level = parseJsonText(text);
      if (req.method === 'GET') {
        return send(res, 200, { file: path.relative(ROOT, LEVEL_FILE).replace(/\\/g, '/'), level_id: isJsonObject(level) ? level.level_id : COMBAT_LAB_LEVEL,
          settings: pickSettings(level), route: routeView(level) });
      }
      if (req.method !== 'POST') return send(res, 405, { error: 'GET / POST only' });
      const body = await readBody(req);
      if (!isJsonObject(body)) return send(res, 400, { error: 'ожидается { settings }' });
      const errors = validateSettings(body.settings);
      if (errors.length) return send(res, 400, { error: errors.join('; '), errors });
      const current = pickSettings(level);
      // Someone changed the file since the page loaded it: refuse, show what changed.
      if (body.base !== undefined && diffSettings(body.base as unknown as CombatLabSettings, current).length > 0) {
        return send(res, 409, { error: 'файл уровня изменился после загрузки страницы — перезагрузите настройки', diff: diffSettings(body.base as unknown as CombatLabSettings, current) });
      }
      const next = body.settings as unknown as CombatLabSettings;
      const diff = diffSettings(current, next);
      if (diff.length === 0) return send(res, 200, { success: true, diff, backup: null });
      fs.mkdirSync(BACKUP_DIR, { recursive: true });
      const backup = path.join(BACKUP_DIR, `${COMBAT_LAB_LEVEL}_${stamp()}.json`);
      fs.writeFileSync(backup, text);
      fs.writeFileSync(LEVEL_FILE, patchJsonText(text, applySettings(level as object, next)));
      send(res, 200, { success: true, diff, backup: path.relative(ROOT, backup).replace(/\\/g, '/'), settings: pickSettings(parseJsonText(fs.readFileSync(LEVEL_FILE, 'utf8'))) });
    } catch (e) {
      send(res, 500, { error: (e as Error).message });
    }
  });

  use('/api/combat-lab/presets', async (req, res) => {
    try {
      const text = readText(PRESETS_FILE);
      const file = (text === null ? { schema: PRESETS_SCHEMA, presets: [] } : parseJsonText(text)) as CombatLabPresetFile;
      if (req.method === 'GET') return send(res, 200, file);
      if (req.method !== 'POST') return send(res, 405, { error: 'GET / POST only' });
      const body = await readBody(req);
      const preset = isJsonObject(body) ? body.preset : undefined;
      const errors = validatePreset(preset);
      if (errors.length) return send(res, 400, { error: errors.join('; '), errors });
      const next = upsertPreset(file, preset as unknown as CombatLabPreset);
      const fileErrors = validatePresetFile(next);
      if (fileErrors.length) return send(res, 400, { error: fileErrors.join('; ') });
      fs.mkdirSync(path.dirname(PRESETS_FILE), { recursive: true });
      fs.writeFileSync(PRESETS_FILE, text === null ? `${JSON.stringify(next, null, 2)}\n` : patchJsonText(text, next));
      send(res, 200, next);
    } catch (e) {
      send(res, 500, { error: (e as Error).message });
    }
  });

  use('/api/combat-lab/layout', (_req, res) => {
    try {
      const overlay = readJson(LAYOUT_FILE) as Record<string, unknown> | null;
      if (!overlay) return send(res, 404, { error: 'formation_lab_combat_01.json not found' });
      const base = typeof overlay.extends === 'string' ? readJson(path.resolve(path.dirname(LAYOUT_FILE), overlay.extends)) as Record<string, unknown> | null : null;
      send(res, 200, { bounds: base?.bounds ?? null, walls: base?.walls ?? [], spawn: base?.spawn ?? null, towers: overlay.towers ?? [] });
    } catch (e) {
      send(res, 500, { error: (e as Error).message });
    }
  });

  use('/api/combat-lab/run', async (req, res, next) => {
    if (req.method !== 'POST') return next ? next() : send(res, 405, { error: 'POST only' });
    try {
      const body = await readBody(req);
      if (!isJsonObject(body)) return send(res, 400, { error: 'ожидается { runs, minutes, parallel, settings }' });
      const errors = [...validateRunRequest(body), ...validateSettings(body.settings)];
      if (errors.length) return send(res, 400, { error: errors.join('; '), errors });
      if (runningStatus()) return send(res, 409, { error: 'боты уже идут — дождитесь конца серии' });
      if (!fs.existsSync(BOT_RUN)) return send(res, 501, { error: 'Scripts/bot_run.ps1 не найден' });
      const runs = Number(body.runs), minutes = Number(body.minutes), parallel = Number(body.parallel);
      const settings = body.settings as unknown as CombatLabSettings;
      const id = batchId(new Date());
      const dir = path.join(BATCHES_DIR, id);
      fs.mkdirSync(dir, { recursive: true });
      const level = parseJsonText(fs.readFileSync(LEVEL_FILE, 'utf8')) as object;
      const levelJson = path.join(dir, 'level.json');
      fs.writeFileSync(levelJson, `${JSON.stringify(applySettings(level, settings), null, 2)}\n`);
      const seed = typeof settings.pressure.seed === 'number' ? settings.pressure.seed : 1;
      const batch = { id, map: MAP, level: COMBAT_LAB_LEVEL, runs, minutes, parallel, preset: typeof body.preset === 'string' ? body.preset : '',
        seed_base: seed, started_utc: new Date().toISOString(), settings };
      fs.writeFileSync(path.join(dir, 'batch.json'), `${JSON.stringify(batch, null, 2)}\n`);
      // Its own minimized console window (cmd "start"; a detached powershell without a console exits at once): the batch outlives the
      // dev server, and bot_run.ps1 keeps a transcript in the batch folder (bot_run.log).
      const q = (s: string) => `"${s}"`;
      const command = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', q(BOT_RUN), '-Runs', runs, '-Parallel', parallel, '-Map', MAP,
        '-DurationSec', Math.round(minutes * 60), '-ReportDir', q(dir), '-SeedBase', seed === 0 ? 1 : seed, '-TimeoutSeconds', realTimeoutSec(minutes),
        '-LevelJson', q(levelJson), '-LogFile', q(path.join(dir, 'bot_run.log'))].join(' ');
      const child = spawn('cmd.exe', ['/d', '/s', '/c', `"start "CodexTactics combat lab bots" /min ${command} "`], {
        cwd: ROOT, stdio: 'ignore', windowsHide: true, windowsVerbatimArguments: true, detached: true,
        env: { ...process.env, CODEX_AGENT: process.env.CODEX_AGENT || 'user' } });
      child.unref();
      send(res, 200, { success: true, batch: id, dir: path.relative(ROOT, dir).replace(/\\/g, '/') });
    } catch (e) {
      send(res, 500, { error: (e as Error).message });
    }
  });

  use('/api/combat-lab/status', (_req, res) => {
    const status = readJson(STATUS_FILE);
    const base = isJsonObject(status) ? status : {};
    const running = runningStatus();
    const reportDir = typeof base.reportDir === 'string' ? base.reportDir : '';
    const isLab = base.mode === 'combat_lab' && reportDir;
    send(res, 200, {
      ...base,
      isRunning: Boolean(running),
      stale: base.isRunning === true && !running,
      batch: isLab ? path.basename(reportDir) : '',
      live: isLab && running ? liveRuns(reportDir) : [],
      elapsedSec: running && typeof base.startedAtUtc === 'string' ? Math.round((Date.now() - Date.parse(base.startedAtUtc)) / 1000) : base.elapsedSec ?? 0,
    });
  });

  use('/api/combat-lab/batches', (_req, res) => {
    if (!fs.existsSync(BATCHES_DIR)) return send(res, 200, { batches: [] });
    const batches = fs.readdirSync(BATCHES_DIR).filter((n) => BATCH_ID.test(n)).sort().reverse().map((id) => {
      const dir = path.join(BATCHES_DIR, id);
      const meta = readJson(path.join(dir, 'batch.json'));
      const runsText = readText(path.join(dir, 'runs.jsonl')) ?? '';
      return { id, ...(isJsonObject(meta) ? { runs: meta.runs, minutes: meta.minutes, preset: meta.preset, started_utc: meta.started_utc } : {}),
        finished_runs: parseRunsJsonl(runsText).runs.length, has_report: fs.existsSync(path.join(dir, 'report.md')) };
    });
    send(res, 200, { batches });
  });

  use('/api/combat-lab/report', (req, res) => {
    const id = new URL(req.url ?? '', 'http://x').searchParams.get('batch') ?? '';
    if (!BATCH_ID.test(id)) return send(res, 400, { error: 'bad batch id' });
    const dir = path.join(BATCHES_DIR, id);
    if (!fs.existsSync(dir)) return send(res, 404, { error: `no batch ${id}` });
    const { runs, skipped } = parseRunsJsonl(readText(path.join(dir, 'runs.jsonl')) ?? '');
    const log = (readText(path.join(dir, 'bot_run.log')) ?? '').split(/\r?\n/).slice(-30).join('\n');
    send(res, 200, { id, dir: path.relative(ROOT, dir).replace(/\\/g, '/'), batch: readJson(path.join(dir, 'batch.json')), runs, skipped,
      jev: readText(path.join(dir, 'jev.txt')) ?? '', markdown: readText(path.join(dir, 'report.md')) ?? '', log });
  });
}
