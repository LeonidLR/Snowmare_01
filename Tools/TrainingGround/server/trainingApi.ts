import { execFileSync, spawn } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import fs from 'node:fs';
import type { IncomingMessage, ServerResponse } from 'node:http';
import path from 'node:path';
import { TRAINING_MAPS, X6 } from '../src/aiTraining/trainingTypes.ts';
import type { TrainingData, TrainingRun, TrainingSector, TrainingSelection } from '../src/aiTraining/trainingTypes.ts';

export const X6_MAP_PATH = '/Game/Maps/Generated/L_Gen_Depot_1_X6';
export const UE_EDITOR_EXE = 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor.exe';

export interface TrainingLaunchDeps {
  isLanesWorktree?: (root: string) => boolean;
  readAgentLock?: (root: string) => { agent?: string; purpose?: string } | null;
  isMainEditorRunning?: (uprojectPath: string) => boolean;
  ensureZenReady?: () => Promise<void>;
  spawnDetached?: (command: string, args: string[], cwd: string) => number | undefined;
}

type ObjectData = Record<string, unknown>;
const mapIds = new Set<string>(TRAINING_MAPS.map(({ id }) => id));
const fallback: TrainingSelection = { map: X6, sector: 0 };
const record = (value: unknown): ObjectData | null => value && typeof value === 'object' && !Array.isArray(value) ? value as ObjectData : null;
const finite = (value: unknown): value is number => typeof value === 'number' && Number.isFinite(value);
const tuple = (value: unknown, length: number): boolean => Array.isArray(value) && value.length === length && value.every(finite);
const sectorId = (value: unknown): value is number => finite(value) && Number.isInteger(value) && value >= 0 && value <= 6;
const parse = (text: string) => record(JSON.parse(text.replace(/^\uFEFF/, '')));
const read = (file: string) => { try { return parse(fs.readFileSync(file, 'utf8')); } catch { return null; } };

export function readTrainingSectors(root: string): TrainingSector[] {
  const data = read(path.join(root, 'Content/Data/LevelJson', X6 + '.sectors.json'));
  if (data?.map !== X6 || !Array.isArray(data.sectors)) return [];
  const valid: TrainingSector[] = [];
  for (const entry of data.sectors) {
    const row = record(entry);
    const box = record(row?.box_cm);
    const squad = record(row?.pmc_squad);
    const contents = record(row?.contents);
    const covers = record(contents?.covers);
    const positions = (list: unknown, posts = false) => Array.isArray(list) && list.length >= 2 && list.length <= 3
      && list.every((entry) => {
        const point = record(entry);
        return tuple(point?.pos, 3) && finite(point?.yaw)
          && (!posts || (typeof point?.id === 'string' && typeof point?.kind === 'string'));
      });
    if (!row || !sectorId(row.id) || row.id === 0 || valid.some((sector) => sector.id === row.id)
      || !tuple(box?.min, 2) || !tuple(box?.max, 2) || typeof row.name !== 'string' || !tuple(row.size_m, 2)
      || !positions(row.starts) || !positions(row.pmc_posts, true)
      || !finite(squad?.count) || typeof squad.posture !== 'string' || typeof squad.weights !== 'string'
      || !finite(contents?.enterable_buildings) || !finite(contents?.fences_m) || !finite(contents?.ladders)
      || !covers || !Object.values(covers).every(finite)) continue;
    valid.push(row as unknown as TrainingSector);
  }
  return valid.sort((a, b) => a.id - b.id);
}

export function validTrainingSelection(data: unknown, sectors: readonly TrainingSector[]): TrainingSelection | null {
  const value = record(data);
  if (!value || typeof value.map !== 'string' || !mapIds.has(value.map) || !sectorId(value.sector)) return null;
  if (value.sector !== 0 && (value.map !== X6 || !sectors.some((sector) => sector.id === value.sector))) return null;
  return { map: value.map as TrainingSelection['map'], sector: value.sector };
}

const selectionFile = (root: string) => path.join(root, 'Saved/Training/active_sector.json');

export function writeTrainingSelection(root: string, data: unknown): TrainingSelection | null {
  const value = validTrainingSelection(data, readTrainingSectors(root));
  if (!value) return null;
  const file = selectionFile(root);
  const existing = read(file);
  if (existing?.map === value.map && existing.sector === value.sector) return value;
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const temp = file + '.' + randomUUID() + '.tmp';
  try {
    fs.writeFileSync(temp, JSON.stringify(value, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
    fs.renameSync(temp, file);
  } finally {
    if (fs.existsSync(temp)) fs.unlinkSync(temp);
  }
  return value;
}

function demoHeader(file: string): ObjectData | null {
  const fd = fs.openSync(file, 'r');
  try {
    const bytes = Buffer.alloc(32 * 1024);
    const count = fs.readSync(fd, bytes, 0, bytes.length, 0);
    const newline = bytes.subarray(0, count).indexOf(10);
    return newline < 0 ? null : parse(bytes.toString('utf8', 0, newline));
  } finally {
    fs.closeSync(fd);
  }
}

function recentRuns(root: string): TrainingRun[] {
  const result: TrainingRun[] = [];
  for (const source of ['demo', 'arena'] as const) {
    const dir = path.join(root, 'Saved', source === 'demo' ? 'Demos' : 'Arena');
    if (!fs.existsSync(dir)) continue;
    const ext = source === 'demo' ? '.jsonl' : '.json';
    const files = fs.readdirSync(dir, { withFileTypes: true }).filter((entry) => entry.isFile() && entry.name.endsWith(ext))
      .map(({ name }) => name).sort().reverse().slice(0, 12);
    for (const file of files) {
      try {
        const full = path.join(dir, file);
        const stat = fs.statSync(full);
        const data = source === 'demo' ? demoHeader(full) : stat.size <= 4 * 1024 * 1024 ? read(full) : null;
        if (!data || typeof data.map !== 'string'
          || (source === 'demo' ? data.type !== 'header' || data.v !== 1 : data.schema !== 'codex.pmc_arena/1')) continue;
        const map = data.map.split('/').at(-1)?.replace(/^UEDPIE_\d+_/, '') ?? data.map;
        const time = typeof data.started_utc === 'string' && Number.isFinite(Date.parse(data.started_utc))
          ? data.started_utc : stat.mtime.toISOString();
        result.push({ file, source, time, map, sector: sectorId(data.sector_id) ? data.sector_id : null });
      } catch { /* The recorder can rotate a file while it is being scanned. */ }
    }
  }
  return result.sort((a, b) => Date.parse(b.time) - Date.parse(a.time)).slice(0, 12);
}

export function readTrainingData(root: string): TrainingData {
  const sectors = readTrainingSectors(root);
  const layout = read(path.join(root, 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x6.json'));
  const validLayout = layout?.bounds && Array.isArray(layout.buildings) && Array.isArray(layout.walls)
    ? layout as unknown as TrainingData['layout'] : null;
  let howto = '';
  try {
    howto = fs.readFileSync(path.join(root, 'docs/port/FIGHTS_HOWTO_RU.md'), 'utf8').replace(/^\uFEFF/, '');
  } catch { /* optional if missing in test fixtures */ }
  const editorCmdPath = path.join(root, 'Scripts/Tools/open_editor_nozen.cmd');
  return {
    maps: TRAINING_MAPS,
    active: validTrainingSelection(read(selectionFile(root)), sectors) ?? fallback,
    sectors,
    layout: validLayout,
    runs: recentRuns(root),
    howto,
    editorCmdPath,
  };
}

function defaultIsLanesWorktree(root: string): boolean {
  return /[\\/]Lanes[\\/]/i.test(path.resolve(root));
}

function defaultReadAgentLock(root: string): { agent?: string; purpose?: string } | null {
  const lockFile = path.join(root, 'Saved/agent.lock');
  if (!fs.existsSync(lockFile)) return null;
  try {
    const lock = parse(fs.readFileSync(lockFile, 'utf8'));
    if (!lock) return null;
    if (lock.manual) return { agent: String(lock.agent ?? ''), purpose: String(lock.purpose ?? '') };
    const pid = Number(lock.pid);
    if (Number.isInteger(pid) && pid > 0) {
      try {
        process.kill(pid, 0);
        return { agent: String(lock.agent ?? ''), purpose: String(lock.purpose ?? '') };
      } catch {
        return null;
      }
    }
  } catch {
    return null;
  }
  return null;
}

function defaultIsMainEditorRunning(uprojectPath: string): boolean {
  const normUproj = path.resolve(uprojectPath).replace(/\\/g, '/').toLowerCase();
  try {
    const ps = "[Console]::OutputEncoding=[Text.Encoding]::UTF8; Get-CimInstance Win32_Process | Where-Object { $_.Name -like 'UnrealEditor*' } | Select-Object ProcessId,CommandLine | ConvertTo-Json -Compress";
    const out = execFileSync('powershell', ['-NoProfile', '-NonInteractive', '-Command', ps], {
      encoding: 'utf8',
      timeout: 15000,
      windowsHide: true,
    }).trim();
    if (!out) return false;
    const raw = JSON.parse(out);
    const list = Array.isArray(raw) ? raw : [raw];
    return list.some((entry) => {
      const cmd = typeof entry?.CommandLine === 'string' ? entry.CommandLine.replace(/\\/g, '/').toLowerCase() : '';
      return cmd.includes(normUproj);
    });
  } catch {
    return false;
  }
}

async function defaultEnsureZenReady(): Promise<void> {
  const healthUrl = 'http://127.0.0.1:8558/health/ready';
  const check = async (): Promise<boolean> => {
    try {
      const res = await fetch(healthUrl, { signal: AbortSignal.timeout(1500) });
      return res.ok;
    } catch {
      return false;
    }
  };
  if (await check()) return;

  const localAppData = process.env.LOCALAPPDATA || '';
  const zenDir = path.join(localAppData, 'UnrealEngine', 'Common', 'Zen');
  const zenServer = path.join(zenDir, 'Install', 'zenserver.exe');
  const zenData = path.join(zenDir, 'Data');
  if (fs.existsSync(zenServer)) {
    const child = spawn(zenServer, ['--port', '8558', '--data-dir', zenData, '--http', 'asio', '--quiet'], {
      detached: true,
      stdio: 'ignore',
      windowsHide: true,
    });
    child.unref();
  }
  const deadline = Date.now() + 15000;
  while (Date.now() < deadline) {
    if (await check()) return;
    await new Promise((resolve) => setTimeout(resolve, 500));
  }
  throw new Error('ZenServer не ответил на http://127.0.0.1:8558/health/ready');
}

function defaultSpawnDetached(command: string, args: string[], cwd: string): number | undefined {
  const child = spawn(command, args, { cwd, detached: true, stdio: 'ignore', windowsHide: true });
  child.unref();
  return child.pid;
}

function checkLaunchGuards(root: string, deps: TrainingLaunchDeps): { status: number; error: string } | null {
  const isLanes = deps.isLanesWorktree ?? defaultIsLanesWorktree;
  if (isLanes(root)) {
    return { status: 403, error: 'Запуск разрешён только из основного каталога (main), не из worktree Lanes.' };
  }
  const readLock = deps.readAgentLock ?? defaultReadAgentLock;
  const lock = readLock(root);
  if (lock) {
    const detail = [lock.agent, lock.purpose].filter(Boolean).join(', ');
    return {
      status: 409,
      error: `Сейчас идёт сборка или работа агента (Saved/agent.lock${detail ? `: ${detail}` : ''}) — дождитесь завершения.`,
    };
  }
  const uprojectPath = path.join(root, 'CodexTactics.uproject');
  const isRunning = deps.isMainEditorRunning ?? defaultIsMainEditorRunning;
  if (isRunning(uprojectPath)) {
    return { status: 409, error: 'Редактор или игра CodexTactics уже запущены в main — закройте их перед запуском.' };
  }
  return null;
}

export function handleOpenEditor(root: string, deps: TrainingLaunchDeps = {}): { status: number; body: ObjectData } {
  const guard = checkLaunchGuards(root, deps);
  if (guard) return { status: guard.status, body: { error: guard.error } };
  const cmdPath = path.join(root, 'Scripts', 'Tools', 'open_editor_nozen.cmd');
  if (!fs.existsSync(cmdPath)) {
    return { status: 404, body: { error: 'Не найден скрипт запуска Scripts/Tools/open_editor_nozen.cmd' } };
  }
  const runDetached = deps.spawnDetached ?? defaultSpawnDetached;
  const args = ['/c', cmdPath, X6_MAP_PATH];
  const pid = runDetached('cmd.exe', args, root);
  return { status: 200, body: { pid: pid ?? null, cmdPath, command: 'cmd.exe', args } };
}

export async function handlePlaySector(
  root: string,
  payload: unknown,
  deps: TrainingLaunchDeps = {},
): Promise<{ status: number; body: ObjectData }> {
  const guard = checkLaunchGuards(root, deps);
  if (guard) return { status: guard.status, body: { error: guard.error } };
  const raw = record(payload);
  const sector = raw?.sector;
  if (!sectorId(sector)) {
    return { status: 400, body: { error: 'Укажите корректный номер сектора (0–6)' } };
  }
  const active = writeTrainingSelection(root, { map: X6, sector });
  if (!active) {
    return { status: 400, body: { error: 'Сектор недоступен для карты X6' } };
  }
  const ensureZen = deps.ensureZenReady ?? defaultEnsureZenReady;
  await ensureZen();
  const uprojectPath = path.join(root, 'CodexTactics.uproject');
  const args = [
    uprojectPath,
    X6_MAP_PATH,
    '-game',
    `-Sector=${active.sector}`,
    '-windowed',
    '-ResX=1600',
    '-ResY=900',
    '-NoZenAutoLaunch',
  ];
  const runDetached = deps.spawnDetached ?? defaultSpawnDetached;
  const pid = runDetached(UE_EDITOR_EXE, args, root);
  return { status: 200, body: { pid: pid ?? null, active, command: UE_EDITOR_EXE, args } };
}

type Use = (route: string, handler: (req: IncomingMessage, res: ServerResponse) => void | Promise<void>) => void;

async function readJsonBody(req: IncomingMessage): Promise<{ ok: true; value: ObjectData | null } | { ok: false; status: number; error: string }> {
  if (!/^application\/json(?:\s*;|$)/i.test(req.headers['content-type'] ?? '')) {
    return { ok: false, status: 415, error: 'JSON required' };
  }
  const chunks: Buffer[] = [];
  let size = 0;
  for await (const chunk of req) {
    size += chunk.length;
    if (size <= 4096) chunks.push(chunk);
  }
  if (size > 4096) return { ok: false, status: 413, error: 'Selection is too large' };
  try {
    return { ok: true, value: parse(Buffer.concat(chunks).toString('utf8')) };
  } catch {
    return { ok: false, status: 400, error: 'Invalid JSON' };
  }
}

export function setupTrainingApi(use: Use, root: string, deps: TrainingLaunchDeps = {}) {
  const send = (res: ServerResponse, status: number, value: unknown) => {
    res.statusCode = status;
    res.setHeader('Content-Type', 'application/json; charset=utf-8');
    res.end(JSON.stringify(value));
  };

  use('/api/ai-training', async (req, res) => {
    try {
      if (req.method === 'GET') return send(res, 200, readTrainingData(root));
      if (req.method !== 'POST') return send(res, 405, { error: 'GET or POST required' });
      const parsed = await readJsonBody(req);
      if (!parsed.ok) return send(res, parsed.status, { error: parsed.error });
      const active = writeTrainingSelection(root, parsed.value);
      return active ? send(res, 200, { active }) : send(res, 400, { error: 'Сектор недоступен для выбранной карты' });
    } catch (error) {
      return send(res, 500, { error: error instanceof Error ? error.message : String(error) });
    }
  });

  use('/api/training/open-editor', async (req, res) => {
    try {
      if (req.method !== 'POST') return send(res, 405, { error: 'POST required' });
      const result = handleOpenEditor(root, deps);
      return send(res, result.status, result.body);
    } catch (error) {
      return send(res, 500, { error: error instanceof Error ? error.message : String(error) });
    }
  });

  use('/api/training/play', async (req, res) => {
    try {
      if (req.method !== 'POST') return send(res, 405, { error: 'POST required' });
      const parsed = await readJsonBody(req);
      if (!parsed.ok) return send(res, parsed.status, { error: parsed.error });
      const result = await handlePlaySector(root, parsed.value, deps);
      return send(res, result.status, result.body);
    } catch (error) {
      return send(res, 500, { error: error instanceof Error ? error.message : String(error) });
    }
  });
}
