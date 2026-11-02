// Training Ground department «Боевая лаборатория» (game designer 2026-11-01, docs/design/COMBAT_LAB.md section 7): dev-server endpoints
// (vite.config.ts). NOT lab-only any more: it works per MAP - the lab L_FormationLabCombat (the default when no map is given) and every
// generated map (Content/Maps/Generated/<leaf>.umap) whose level JSON has a "bot_route" (src/combatLab/maps.ts). Per map: its level JSON blocks
// "pressure" / "cheats" (part A), the bot batches in Unreal (part C: Scripts/bot_run.ps1 -ReportDir, the bot's route mode) and their reports.
// The named presets are ONE shared file (presets saved from a generated map carry no ambush.points).
// Every endpoint takes the map: GET ?map=<id>, POST body { map }. Missing = the lab; an id outside GET /maps -> 400.
//   GET  /api/combat-lab/maps                    -> { default, maps: [{ id, map, levelFile, kind, hasRoute, routeName, routePoints }] }
//   GET  /api/combat-lab/settings                -> { map, file, level_id, settings, route }
//   POST /api/combat-lab/settings { map?, settings, base? } -> validated (src/combatLab/pressureSettings.ts), a backup in
//                                                   Saved/TrainingGround/combat_lab/backups (named by the map id), only the changed values rewritten -> { diff, backup }
//   GET  /api/combat-lab/presets                 -> Content/Data/CombatLab/presets.json
//   POST /api/combat-lab/presets { map?, preset } -> added / replaced by id (from a generated map: stored without ambush.points)
//   GET  /api/combat-lab/layout                  -> { map, bounds, walls, towers, spawn, buildings, ladders } for the map preview (the lab: formation_lab_01 +
//                                                   the combat overlay; a generated map: its accepted levelgen layout)
//   POST /api/combat-lab/run { map?, runs, minutes, parallel, settings, preset? } -> a batch in Saved/Telemetry/combat_lab/<id> (level.json with the
//                                                   page's settings, batch.json with map + level), bot_run.ps1 started detached -> { batch }
//   GET  /api/combat-lab/status                  -> bot_status.json (+ the live snapshots of the running games, the running batch's level)
//   GET  /api/combat-lab/batches?map=            -> { map, batches: [...] } newest first, only the batches of that map (no "level" = the lab)
//   GET  /api/combat-lab/report?batch=<id>&map=  -> { runs, skipped, jev, markdown, batch, log }
import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { parseJsonText, patchJsonText } from '../src/utils/jsonTextPatch.ts';
import {
  applySettings, diffSettings, isJsonObject, pickSettings, upsertPreset, validatePreset, validatePresetFile, validateSettings, PRESETS_SCHEMA,
} from '../src/combatLab/pressureSettings.ts';
import type { CombatLabPreset, CombatLabPresetFile, CombatLabSettings } from '../src/combatLab/pressureSettings.ts';
import { LAB_MAP_ID, acceptedLayoutPath, buildMapList, layoutToPreview, presetForSave, resolveMap } from '../src/combatLab/maps.ts';
import type { CombatLabMapInfo, GeneratedMapInput } from '../src/combatLab/maps.ts';
import { batchId, parseRunsJsonl, realTimeoutSec, validateRunRequest } from '../src/combatLab/report.ts';
import type { LiveRun } from '../src/combatLab/report.ts';

const TOOL_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const ROOT = path.resolve(TOOL_DIR, '../..');
export const COMBAT_LAB_LEVEL = LAB_MAP_ID;
const LEVEL_JSON_DIR = path.join(ROOT, 'Content/Data/LevelJson');
const GENERATED_MAPS_DIR = path.join(ROOT, 'Content/Maps/Generated');
const PRESETS_FILE = path.join(ROOT, 'Content/Data/CombatLab/presets.json');
const LAB_LAYOUT_FILE = path.join(ROOT, 'Content/Data/FormationLab/Combat/formation_lab_combat_01.json');
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
const rel = (file: string) => path.relative(ROOT, file).replace(/\\/g, '/');

/** The time stamp of a backup name (UTC, sortable). */
const stamp = () => new Date().toISOString().replace(/[-:]/g, '').replace(/\.\d+Z$/, 'Z');

const levelPath = (id: string) => path.join(LEVEL_JSON_DIR, `${id}.json`);

/** The maps the department offers right now (the directory listings are read here, the rules are in src/combatLab/maps.ts). */
function listMaps(): CombatLabMapInfo[] {
  const generated: GeneratedMapInput[] = [];
  if (fs.existsSync(GENERATED_MAPS_DIR)) {
    for (const f of fs.readdirSync(GENERATED_MAPS_DIR).filter((n) => n.toLowerCase().endsWith('.umap'))) {
      const leaf = f.slice(0, -'.umap'.length);
      let level: unknown = null;
      try { level = readJson(levelPath(leaf)); } catch { /* unreadable: not listed */ }
      generated.push({ leaf, level });
    }
  }
  let lab: unknown = null;
  try { lab = readJson(levelPath(LAB_MAP_ID)); } catch { /* the lab is listed anyway */ }
  return buildMapList(lab, generated);
}

/** The map of a request (query `map` / body `map`); sends a 400 and returns null for an unknown id. */
function mapOf(res: Res, requested: unknown): CombatLabMapInfo | null {
  const r = resolveMap(listMaps(), requested);
  if (r.error !== undefined) {
    send(res, 400, { error: r.error });
    return null;
  }
  return r.map;
}

const queryOf = (req: Req) => new URL(req.url ?? '', 'http://x').searchParams;

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

/** The map id a batch belongs to (batch.json "level"; batches from before the map selector have none = the lab). */
function batchLevel(meta: unknown): string {
  return isJsonObject(meta) && typeof meta.level === 'string' && meta.level ? meta.level : LAB_MAP_ID;
}

export function setupCombatLabApi(use: Use): void {
  use('/api/combat-lab/maps', (_req, res) => {
    try {
      send(res, 200, { default: LAB_MAP_ID, maps: listMaps() });
    } catch (e) {
      send(res, 500, { error: (e as Error).message });
    }
  });

  use('/api/combat-lab/settings', async (req, res) => {
    try {
      const body = req.method === 'POST' ? await readBody(req) : undefined;
      if (req.method === 'POST' && !isJsonObject(body)) return send(res, 400, { error: 'ожидается { settings }' });
      const map = mapOf(res, req.method === 'POST' ? (body as Record<string, unknown>).map : queryOf(req).get('map') ?? undefined);
      if (!map) return;
      const levelFile = levelPath(map.id);
      const text = readText(levelFile);
      if (text === null) return send(res, 404, { error: `${map.id}.json not found` });
      const level = parseJsonText(text);
      if (req.method === 'GET') {
        return send(res, 200, { map: map.id, file: rel(levelFile), level_id: isJsonObject(level) ? level.level_id : map.id,
          settings: pickSettings(level), route: routeView(level) });
      }
      if (req.method !== 'POST') return send(res, 405, { error: 'GET / POST only' });
      const post = body as Record<string, unknown>;
      const errors = validateSettings(post.settings);
      if (errors.length) return send(res, 400, { error: errors.join('; '), errors });
      const current = pickSettings(level);
      // Someone changed the file since the page loaded it: refuse, show what changed.
      if (post.base !== undefined && diffSettings(post.base as unknown as CombatLabSettings, current).length > 0) {
        return send(res, 409, { error: 'файл уровня изменился после загрузки страницы — перезагрузите настройки', diff: diffSettings(post.base as unknown as CombatLabSettings, current) });
      }
      const next = post.settings as unknown as CombatLabSettings;
      const diff = diffSettings(current, next);
      if (diff.length === 0) return send(res, 200, { success: true, diff, backup: null });
      fs.mkdirSync(BACKUP_DIR, { recursive: true });
      const backup = path.join(BACKUP_DIR, `${map.id}_${stamp()}.json`);
      fs.writeFileSync(backup, text);
      fs.writeFileSync(levelFile, patchJsonText(text, applySettings(level as object, next)));
      send(res, 200, { success: true, diff, backup: rel(backup), settings: pickSettings(parseJsonText(fs.readFileSync(levelFile, 'utf8'))) });
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
      const map = mapOf(res, isJsonObject(body) ? body.map : undefined);
      if (!map) return;
      const preset = isJsonObject(body) ? body.preset : undefined;
      const errors = validatePreset(preset);
      if (errors.length) return send(res, 400, { error: errors.join('; '), errors });
      // From a generated map: no ambush.points in the shared file (they would overwrite the lab's own points on apply).
      const next = upsertPreset(file, presetForSave(preset as unknown as CombatLabPreset, map.id));
      const fileErrors = validatePresetFile(next);
      if (fileErrors.length) return send(res, 400, { error: fileErrors.join('; ') });
      fs.mkdirSync(path.dirname(PRESETS_FILE), { recursive: true });
      fs.writeFileSync(PRESETS_FILE, text === null ? `${JSON.stringify(next, null, 2)}\n` : patchJsonText(text, next));
      send(res, 200, next);
    } catch (e) {
      send(res, 500, { error: (e as Error).message });
    }
  });

  use('/api/combat-lab/layout', (req, res) => {
    try {
      const map = mapOf(res, queryOf(req).get('map') ?? undefined);
      if (!map) return;
      if (map.kind === 'generated') {
        const layoutRel = acceptedLayoutPath(readJson(levelPath(map.id)));
        if (!layoutRel) return send(res, 404, { error: `${map.id}: в уровне нет levelgen_layout (Content/Data/LevelGen/accepted/...)` });
        const layout = readJson(path.join(ROOT, layoutRel));
        if (!layout) return send(res, 404, { error: `${layoutRel} not found` });
        return send(res, 200, { map: map.id, ...layoutToPreview(layout) });
      }
      const overlay = readJson(LAB_LAYOUT_FILE) as Record<string, unknown> | null;
      if (!overlay) return send(res, 404, { error: 'formation_lab_combat_01.json not found' });
      const base = typeof overlay.extends === 'string' ? readJson(path.resolve(path.dirname(LAB_LAYOUT_FILE), overlay.extends)) as Record<string, unknown> | null : null;
      send(res, 200, { map: map.id, bounds: base?.bounds ?? null, walls: base?.walls ?? [], spawn: base?.spawn ?? null, towers: overlay.towers ?? [] });
    } catch (e) {
      send(res, 500, { error: (e as Error).message });
    }
  });

  use('/api/combat-lab/run', async (req, res, next) => {
    if (req.method !== 'POST') return next ? next() : send(res, 405, { error: 'POST only' });
    try {
      const body = await readBody(req);
      if (!isJsonObject(body)) return send(res, 400, { error: 'ожидается { runs, minutes, parallel, settings }' });
      const map = mapOf(res, body.map);
      if (!map) return;
      const errors = [...validateRunRequest(body), ...validateSettings(body.settings)];
      if (errors.length) return send(res, 400, { error: errors.join('; '), errors });
      if (!map.hasRoute) return send(res, 400, { error: `${map.id}: у уровня нет bot_route` });
      if (runningStatus()) return send(res, 409, { error: 'боты уже идут — дождитесь конца серии' });
      if (!fs.existsSync(BOT_RUN)) return send(res, 501, { error: 'Scripts/bot_run.ps1 не найден' });
      const runs = Number(body.runs), minutes = Number(body.minutes), parallel = Number(body.parallel);
      const settings = body.settings as unknown as CombatLabSettings;
      const id = batchId(new Date());
      const dir = path.join(BATCHES_DIR, id);
      fs.mkdirSync(dir, { recursive: true });
      const level = parseJsonText(fs.readFileSync(levelPath(map.id), 'utf8')) as object;
      const levelJson = path.join(dir, 'level.json');
      fs.writeFileSync(levelJson, `${JSON.stringify(applySettings(level, settings), null, 2)}\n`);
      const seed = typeof settings.pressure.seed === 'number' ? settings.pressure.seed : 1;
      const batch = { id, map: map.map, level: map.id, runs, minutes, parallel, preset: typeof body.preset === 'string' ? body.preset : '',
        seed_base: seed, started_utc: new Date().toISOString(), settings };
      fs.writeFileSync(path.join(dir, 'batch.json'), `${JSON.stringify(batch, null, 2)}\n`);
      // Its own minimized console window (cmd "start"; a detached powershell without a console exits at once): the batch outlives the
      // dev server, and bot_run.ps1 keeps a transcript in the batch folder (bot_run.log).
      const q = (s: string) => `"${s}"`;
      const command = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', q(BOT_RUN), '-Runs', runs, '-Parallel', parallel, '-Map', map.map,
        '-DurationSec', Math.round(minutes * 60), '-ReportDir', q(dir), '-SeedBase', seed === 0 ? 1 : seed, '-TimeoutSeconds', realTimeoutSec(minutes),
        '-LevelJson', q(levelJson), '-LogFile', q(path.join(dir, 'bot_run.log'))].join(' ');
      const child = spawn('cmd.exe', ['/d', '/s', '/c', `"start "CodexTactics combat lab bots" /min ${command} "`], {
        cwd: ROOT, stdio: 'ignore', windowsHide: true, windowsVerbatimArguments: true, detached: true,
        env: { ...process.env, CODEX_AGENT: process.env.CODEX_AGENT || 'user' } });
      child.unref();
      send(res, 200, { success: true, batch: id, map: map.id, dir: rel(dir) });
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
      level: isLab ? batchLevel(readJson(path.join(reportDir, 'batch.json'))) : '',
      live: isLab && running ? liveRuns(reportDir) : [],
      elapsedSec: running && typeof base.startedAtUtc === 'string' ? Math.round((Date.now() - Date.parse(base.startedAtUtc)) / 1000) : base.elapsedSec ?? 0,
    });
  });

  use('/api/combat-lab/batches', (req, res) => {
    const map = mapOf(res, queryOf(req).get('map') ?? undefined);
    if (!map) return;
    if (!fs.existsSync(BATCHES_DIR)) return send(res, 200, { map: map.id, batches: [] });
    const batches = fs.readdirSync(BATCHES_DIR).filter((n) => BATCH_ID.test(n)).sort().reverse().flatMap((id) => {
      const dir = path.join(BATCHES_DIR, id);
      const meta = readJson(path.join(dir, 'batch.json'));
      if (batchLevel(meta) !== map.id) return [];
      const runsText = readText(path.join(dir, 'runs.jsonl')) ?? '';
      return [{ id, level: map.id, ...(isJsonObject(meta) ? { runs: meta.runs, minutes: meta.minutes, preset: meta.preset, started_utc: meta.started_utc } : {}),
        finished_runs: parseRunsJsonl(runsText).runs.length, has_report: fs.existsSync(path.join(dir, 'report.md')) }];
    });
    send(res, 200, { map: map.id, batches });
  });

  use('/api/combat-lab/report', (req, res) => {
    const params = queryOf(req);
    const map = mapOf(res, params.get('map') ?? undefined);
    if (!map) return;
    const id = params.get('batch') ?? '';
    if (!BATCH_ID.test(id)) return send(res, 400, { error: 'bad batch id' });
    const dir = path.join(BATCHES_DIR, id);
    if (!fs.existsSync(dir)) return send(res, 404, { error: `no batch ${id}` });
    const batch = readJson(path.join(dir, 'batch.json'));
    if (batchLevel(batch) !== map.id) return send(res, 404, { error: `batch ${id} is not a ${map.id} batch` });
    const { runs, skipped } = parseRunsJsonl(readText(path.join(dir, 'runs.jsonl')) ?? '');
    const log = (readText(path.join(dir, 'bot_run.log')) ?? '').split(/\r?\n/).slice(-30).join('\n');
    send(res, 200, { id, map: map.id, dir: rel(dir), batch, runs, skipped,
      jev: readText(path.join(dir, 'jev.txt')) ?? '', markdown: readText(path.join(dir, 'report.md')) ?? '', log });
  });
}
