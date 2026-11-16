// Training Ground / level generator v2 - designer UX round (task 072, item 6): «Собрать в Unreal». Node side only (the dev server,
// server/levelgenApi.ts): builds the ACCEPTED layout into /Game/Maps/Generated/L_Gen_<Name> in the background, then checks its
// route, through the existing pipeline scripts of THIS checkout (the server's ROOT - the builder writes the map and the level JSON
// into the checkout it runs from, and the route smoke reads accepted/<layout>.json from the same one):
//   powershell -File Scripts/levelgen_build_map.ps1 -Layout <name> -Target <map> -StatusFile Saved/LevelGen/build_<leaf>.json
//   powershell -File Scripts/levelgen_route_smoke.ps1 -Map <map> -StatusFile Saved/LevelGen/route_<leaf>.json
// powershell.exe is spawned DIRECTLY (never through Git Bash: it mangles the /Game/... paths and the builder then fails silently),
// HIDDEN (bug 2026-11-16 evening: the designer saw two PowerShell windows): windowsHide + piped stdio (libuv then starts it with
// CREATE_NO_WINDOW - no console window at all, and never the TG server's own console, which -WindowStyle Hidden would hide if it were
// shared), -NoProfile -NonInteractive -WindowStyle Hidden; the scripts start Unreal through Scripts/hidden_process.ps1 (no window
// either). Their output is streamed into the page's log panel (BuildStatus.log) and Saved/LevelGen/<build|route>_<leaf>.log.
// Rules: the target is always derived from the accepted name (mapNameFor, levelgen_v2.json build) - the page cannot name another
// map; an existing map is replaced only when the level JSON says it was built from this same layout AND the designer confirmed;
// one build at a time (this server's lock; the scripts also take the shared agent build lock and report waiting_lock).
// The status merges the scripts' own status files (codex.job_status/1, Scripts/job_status.ps1). The process runner is injectable
// (the tests mock it: no Unreal run in the tests).
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import readline from 'node:readline';
import cfg from '../../../../../Content/Data/LevelGen/levelgen_v2.json' with { type: 'json' };

export const BUILD_CFG = cfg.build;
const NAME_RE = /^[A-Za-z0-9_-]{1,80}$/;
const LEAF_RE = /^L_Gen_[A-Za-z0-9_]{1,80}$/;

/** The map name rule: L_Gen_<Name>, Name = the layout name without strip_prefix, each '_' word capitalised (L_Gen_* kept). */
export function mapNameFor(layoutName: string): string {
  if (layoutName.startsWith('L_Gen_')) return layoutName.replace(/[^A-Za-z0-9_]/g, '_');
  const rest = layoutName.replace(new RegExp(BUILD_CFG.strip_prefix), '').replace(/[^A-Za-z0-9_]/g, '_');
  const words = rest.split('_').filter(Boolean).map((w) => w[0].toUpperCase() + w.slice(1));
  return `L_Gen_${words.join('_') || 'Map'}`;
}

/** A started process: its pid and its exit code. */
export interface RunHandle { pid?: number; done: Promise<number> }
/** A started process's options: `onLine` gets every line it prints (stdout and stderr). */
export interface RunOptions { cwd: string; env: NodeJS.ProcessEnv; onLine?: (line: string) => void }
export type ProcessRunner = (exe: string, args: string[], opts: RunOptions) => RunHandle;

/** powershell.exe's arguments for a script, hidden and non-interactive (no profile, no prompt, no window). */
export const hiddenPowershellArgs = (script: string, args: readonly string[]): string[] =>
  ['-NoProfile', '-NonInteractive', '-WindowStyle', 'Hidden', '-ExecutionPolicy', 'Bypass', '-File', script, ...args];

/** spawn()'s options for a hidden child: no shell, no `start`, no new console; stdio piped (=> CREATE_NO_WINDOW on Windows). */
export const HIDDEN_SPAWN = { windowsHide: true, shell: false, detached: false, stdio: ['ignore', 'pipe', 'pipe'] } as const;

/** The real runner: powershell.exe directly, hidden, its output streamed line by line to `onLine`. */
export const powershellRunner: ProcessRunner = (exe, args, opts) => {
  const child = spawn(exe, args, { cwd: opts.cwd, env: opts.env, ...HIDDEN_SPAWN, stdio: [...HIDDEN_SPAWN.stdio] });
  for (const stream of [child.stdout, child.stderr]) {
    if (!stream) continue;
    stream.setEncoding('utf8');
    readline.createInterface({ input: stream, crlfDelay: Infinity }).on('line', (line) => { if (line.trim()) opts.onLine?.(line.replace(/\s+$/, '')); });
  }
  const done = new Promise<number>((resolve) => {
    child.on('error', (e) => { opts.onLine?.(`[TG] не удалось запустить ${exe}: ${e.message}`); resolve(-1); });
    child.on('exit', (code) => resolve(code ?? -1));
  });
  return { pid: child.pid, done };
};

export interface TargetInfo {
  name: string; leaf: string; map: string; /** The .umap, relative to the root. */ file: string;
  exists: boolean; /** The accepted layout the existing map's level JSON names (empty: none / unknown). */ builtFrom: string;
  /** May build now (with confirm when needsConfirm). */ canBuild: boolean; needsConfirm: boolean; reason: string;
}

/** codex.job_status/1 (Scripts/job_status.ps1). */
export interface JobStatus { state?: string; reason?: string; message?: string; exit_code?: number | null; started_utc?: string; updated_utc?: string; result?: Record<string, unknown>; lock_holder?: unknown }

export type BuildStage = 'idle' | 'build' | 'smoke' | 'done' | 'failed';
export interface BuildStatus {
  stage: BuildStage;
  name: string; map: string; file: string;
  started_utc: string; elapsed_s: number; stage_elapsed_s: number;
  /** The scripts' own status (state running / waiting_lock / done / failed / refused, message). */
  build: JobStatus | null; smoke: JobStatus | null;
  /** The final result: built (actors, drop links), the nav tiles line, the route PASS / FAIL with its summary. */
  result: { built: boolean; actors?: number; nav_tiles?: string; route?: 'PASS' | 'FAIL' | ''; route_summary?: string; reason?: string };
  /** One line for the page. */
  message: string;
  /** The scripts' latest output lines (build.log_lines kept), each with its stage ("1/2" build, "2/2" route check). */
  log: string[];
}

const rel = (root: string, f: string) => path.relative(root, f).split(path.sep).join('/');
const readJson = (f: string): unknown => { try { return JSON.parse(fs.readFileSync(f, 'utf8').replace(/^﻿/, '')); } catch { return null; } };

/** The "tiles: Default=751 Vehicle=617" part of a NavBake log ([NavBake] lines). */
export function navTilesOf(logText: string): string {
  const lines = logText.split(/\r?\n/).filter((l) => l.includes('[NavBake]') && l.includes('tiles:'));
  const m = /tiles:\s*([^()]+?)\s*\(/.exec(lines.at(-1) ?? '');
  return m ? m[1].trim() : '';
}

export interface BuildPipelineOptions { root: string; runner?: ProcessRunner; now?: () => number; agent?: string }

export function createBuildPipeline(o: BuildPipelineOptions) {
  const root = o.root;
  const runner = o.runner ?? powershellRunner;
  const now = o.now ?? (() => Date.now());
  const acceptedDir = path.join(root, 'Content/Data/LevelGen/accepted');
  const statusDir = path.join(root, 'Saved/LevelGen');
  let cur: { name: string; leaf: string; map: string; stage: BuildStage; t0: number; ts: number; reason: string } | null = null;
  let log: string[] = [];
  /** A line from a script: kept for the page (the last build.log_lines) and appended to its Saved/LevelGen/*.log. */
  const logLine = (tag: string, file: string) => (line: string) => {
    const text = `${tag} ${line}`;
    log.push(text);
    if (log.length > BUILD_CFG.log_lines) log = log.slice(-BUILD_CFG.log_lines);
    try { fs.appendFileSync(file, `${line}
`); } catch { /* the log file is a convenience */ }
  };

  const target = (name: string): TargetInfo => {
    const leaf = mapNameFor(name);
    const map = `${BUILD_CFG.map_dir}/${leaf}`;
    const umap = path.join(root, 'Content/Maps/Generated', `${leaf}.umap`);
    const info: TargetInfo = { name, leaf, map, file: rel(root, umap), exists: fs.existsSync(umap), builtFrom: '', canBuild: false, needsConfirm: false, reason: '' };
    if (!NAME_RE.test(name)) return { ...info, reason: 'плохое имя раскладки' };
    if (!LEAF_RE.test(leaf)) return { ...info, reason: `имя карты ${leaf} не по правилу L_Gen_*` };
    if (!fs.existsSync(path.join(acceptedDir, `${name}.json`))) return { ...info, reason: `раскладка не принята: нет Content/Data/LevelGen/accepted/${name}.json — сначала «Принять»` };
    if (!info.exists) return { ...info, canBuild: true };
    const lj = readJson(path.join(root, 'Content/Data/LevelJson', `${leaf}.json`)) as { levelgen_layout?: string } | null;
    info.builtFrom = typeof lj?.levelgen_layout === 'string' ? path.basename(lj.levelgen_layout, '.json') : '';
    if (info.builtFrom !== name) {
      return { ...info, reason: info.builtFrom ? `карта ${leaf} собрана из другой раскладки (${info.builtFrom}) — не перезаписываю` : `карта ${leaf} есть, но не из генератора (нет level JSON с levelgen_layout) — не перезаписываю` };
    }
    return { ...info, canBuild: true, needsConfirm: true, reason: `карта ${leaf} уже собрана из этой раскладки — заменить?` };
  };

  const statusFiles = (leaf: string) => ({ build: path.join(statusDir, `build_${leaf}.json`), smoke: path.join(statusDir, `route_${leaf}.json`),
    buildLog: path.join(statusDir, `build_${leaf}.log`), smokeLog: path.join(statusDir, `route_${leaf}.log`) });
  const env = () => ({ ...process.env, CODEX_AGENT: o.agent ?? (process.env.CODEX_AGENT || 'user') });
  const ps = (script: string, args: string[]) => hiddenPowershellArgs(path.join(root, 'Scripts', script), args);

  const run = async (name: string, leaf: string, map: string) => {
    const sf = statusFiles(leaf);
    const build = runner('powershell.exe', ps('levelgen_build_map.ps1', ['-Layout', name, '-Target', map, '-StatusFile', sf.build]), { cwd: root, env: env(), onLine: logLine('[1/2]', sf.buildLog) });
    const code = await build.done;
    if (!cur) return;
    if (code !== 0) { cur.stage = 'failed'; cur.reason = `сборка: код ${code}`; return; }
    cur.stage = 'smoke'; cur.ts = now();
    const smoke = runner('powershell.exe', ps('levelgen_route_smoke.ps1', ['-Map', map, '-StatusFile', sf.smoke]), { cwd: root, env: env(), onLine: logLine('[2/2]', sf.smokeLog) });
    const sc = await smoke.done;
    if (!cur) return;
    cur.stage = sc === 0 ? 'done' : 'failed';
    cur.reason = sc === 0 ? '' : `проверка маршрута: код ${sc}`;
  };

  /** Start a build ({ status, body } for the HTTP reply). */
  const start = (name: string, confirm: boolean): { status: number; body: Record<string, unknown> } => {
    if (cur && (cur.stage === 'build' || cur.stage === 'smoke')) return { status: 409, body: { error: `уже идёт сборка ${cur.leaf} — дождитесь конца (одна сборка за раз)`, busy: true } };
    const t = target(name);
    if (!t.canBuild) return { status: t.exists ? 409 : t.reason.startsWith('раскладка не принята') ? 404 : 400, body: { error: t.reason, target: t } };
    if (t.needsConfirm && !confirm) return { status: 409, body: { error: t.reason, needsConfirm: true, target: t } };
    fs.mkdirSync(statusDir, { recursive: true });
    const sf = statusFiles(t.leaf);
    for (const f of [sf.build, sf.smoke, sf.buildLog, sf.smokeLog]) fs.rmSync(f, { force: true });
    log = [];
    const t0 = now();
    cur = { name, leaf: t.leaf, map: t.map, stage: 'build', t0, ts: t0, reason: '' };
    run(name, t.leaf, t.map).catch((e) => { if (cur) { cur.stage = 'failed'; cur.reason = String(e); } });
    return { status: 200, body: { started: true, target: t } };
  };

  const status = (): BuildStatus => {
    if (!cur) return { stage: 'idle', name: '', map: '', file: '', started_utc: '', elapsed_s: 0, stage_elapsed_s: 0, build: null, smoke: null, result: { built: false }, message: 'сборок не было', log: [] };
    const sf = statusFiles(cur.leaf);
    const build = readJson(sf.build) as JobStatus | null, smoke = readJson(sf.smoke) as JobStatus | null;
    const br = (build?.result ?? {}) as Record<string, unknown>, sr = (smoke?.result ?? {}) as Record<string, unknown>;
    const navLog = typeof br.navbake_log === 'string' ? br.navbake_log : '';
    let navTiles = '';
    try { if (navLog && fs.existsSync(navLog)) navTiles = navTilesOf(fs.readFileSync(navLog, 'utf8')); } catch { /* the log is optional */ }
    const built = build?.state === 'done';
    const route = (sr.result === 'PASS' || sr.result === 'FAIL' ? sr.result : '') as 'PASS' | 'FAIL' | '';
    const ended = cur.stage === 'done' || cur.stage === 'failed';
    const t = now();
    const stageMsg = cur.stage === 'build'
      ? `сборка карты: ${build?.state === 'waiting_lock' ? 'ждёт общий замок сборки' : build?.message || 'запуск'}`
      : cur.stage === 'smoke' ? `проверка маршрута ботом: ${smoke?.state === 'waiting_lock' ? 'ждёт общий замок сборки' : smoke?.message || 'запуск'}`
        : cur.stage === 'done' ? `готово: ${cur.map}, маршрут ${route || '?'}` : `ошибка: ${(cur.stage === 'failed' && (smoke?.message || build?.message)) || cur.reason}`;
    return {
      stage: cur.stage, name: cur.name, map: cur.map, file: `Content/Maps/Generated/${cur.leaf}.umap`,
      started_utc: new Date(cur.t0).toISOString(), elapsed_s: Math.round((t - cur.t0) / 1000), stage_elapsed_s: ended ? 0 : Math.round((t - cur.ts) / 1000),
      build, smoke,
      result: { built, ...(typeof br.actors === 'number' ? { actors: br.actors } : {}), ...(navTiles ? { nav_tiles: navTiles } : {}), route, ...(typeof sr.summary === 'string' && sr.summary ? { route_summary: sr.summary } : {}),
        ...(cur.stage === 'failed' ? { reason: [build?.state !== 'done' ? build?.reason || build?.message : smoke?.reason || smoke?.message, cur.reason].filter(Boolean).join('; ') } : {}) },
      message: stageMsg,
      log: log.slice(-BUILD_CFG.log_tail),
    };
  };

  return { target, start, status, mapNameFor };
}
export type BuildPipeline = ReturnType<typeof createBuildPipeline>;
