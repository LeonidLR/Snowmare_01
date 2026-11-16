// Training Ground / level generator v2 - UX round (task 072, item 6): «Собрать в Unreal» = src/levelgen/node/buildPipeline.ts with a
// MOCKED process runner (no Unreal, no powershell). Everything is written to an os.tmpdir() project root; the real accepted maps
// and Content/Data/LevelJson/** are never touched. `npm test`.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { test } from 'node:test';
import type { ProcessRunner } from './buildPipeline.ts';
import { createBuildPipeline, HIDDEN_SPAWN, hiddenPowershellArgs, mapNameFor, navTilesOf, powershellRunner } from './buildPipeline.ts';

interface Call { exe: string; args: string[]; cwd: string; agent?: string; finish: (code: number) => void; say: (line: string) => void }

function fixture() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'tg-build-'));
  const w = (rel: string, data: unknown) => { const f = path.join(root, rel); fs.mkdirSync(path.dirname(f), { recursive: true }); fs.writeFileSync(f, typeof data === 'string' ? data : JSON.stringify(data)); return f; };
  w('Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x4.json', { name: 'gen_ref_01_02_depot_1_x4' });
  w('Content/Data/LevelGen/accepted/gen_ref_01_02_depot_2.json', { name: 'gen_ref_01_02_depot_2' });
  // a map this layout built before (confirm needed) and a map from another layout (refused)
  w('Content/Maps/Generated/L_Gen_Depot_2.umap', 'umap');
  w('Content/Data/LevelJson/L_Gen_Depot_2.json', { levelgen_layout: 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_2.json' });
  w('Content/Data/LevelGen/accepted/gen_ref_01_02_depot_9.json', { name: 'gen_ref_01_02_depot_9' });
  w('Content/Maps/Generated/L_Gen_Depot_9.umap', 'umap');
  w('Content/Data/LevelJson/L_Gen_Depot_9.json', { levelgen_layout: 'Content/Data/LevelGen/accepted/something_else.json' });
  w('Content/Data/LevelGen/accepted/L_Gen_Train_7.json', { name: 'L_Gen_Train_7' });
  w('Content/Maps/Generated/L_Gen_Train_7.umap', 'umap');
  const calls: Call[] = [];
  const runner: ProcessRunner = (exe, args, opts) => {
    let finish!: (code: number) => void;
    const done = new Promise<number>((r) => { finish = r; });
    calls.push({ exe, args, cwd: opts.cwd, agent: opts.env.CODEX_AGENT, finish, say: (line) => opts.onLine?.(line) });
    return { pid: 4242, done };
  };
  let clock = Date.parse('2026-11-16T10:00:00Z');
  const p = createBuildPipeline({ root, runner, now: () => clock, agent: 'user' });
  return { root, w, calls, p, tick: (s: number) => { clock += s * 1000; } };
}
const flush = () => new Promise((r) => setTimeout(r, 5));
const arg = (c: Call, k: string) => c.args[c.args.indexOf(k) + 1];

test('ux build: the map name rule (shown in the UI) and the NavBake tiles line', () => {
  assert.equal(mapNameFor('gen_ref_01_02_depot_1'), 'L_Gen_Depot_1', 'the existing Depot map keeps its name');
  assert.equal(mapNameFor('gen_ref_01_02_depot_1_x4'), 'L_Gen_Depot_1_X4');
  assert.equal(mapNameFor('L_Gen_Train_1'), 'L_Gen_Train_1');
  assert.equal(mapNameFor('my-yard_2'), 'L_Gen_My_Yard_2');
  assert.equal(navTilesOf('x\n[2026] LogCodexBuildNav: Display: [NavBake] /Game/Maps/Generated/L_Gen_Test_X2: built=1 releasedAsyncLoadLock=1 removedMisaligned=0 tiles: Default=751 Vehicle=617 (0.4 s)\n[NavBake] PASS: saved x'), 'Default=751 Vehicle=617');
  assert.equal(navTilesOf('nothing'), '');
});

test('ux build: target guard - only L_Gen_* from the accepted name; a foreign map refused; a map of this layout needs a confirm', () => {
  const { p, calls } = fixture();
  const fresh = p.target('gen_ref_01_02_depot_1_x4');
  assert.equal(fresh.map, '/Game/Maps/Generated/L_Gen_Depot_1_X4');
  assert.equal(fresh.file, 'Content/Maps/Generated/L_Gen_Depot_1_X4.umap');
  assert.deepEqual([fresh.exists, fresh.canBuild, fresh.needsConfirm], [false, true, false]);
  const mine = p.target('gen_ref_01_02_depot_2');
  assert.deepEqual([mine.exists, mine.canBuild, mine.needsConfirm, mine.builtFrom], [true, true, true, 'gen_ref_01_02_depot_2']);
  const foreign = p.target('gen_ref_01_02_depot_9');
  assert.deepEqual([foreign.exists, foreign.canBuild], [true, false]);
  assert.match(foreign.reason, /другой раскладки/);
  const noJson = p.target('L_Gen_Train_7');
  assert.equal(noJson.canBuild, false, 'a map without a generator level JSON is never replaced');
  assert.equal(p.target('not_accepted').canBuild, false);
  assert.equal(p.target('../evil').canBuild, false);
  // the HTTP answers
  assert.equal(p.start('gen_ref_01_02_depot_9', true).status, 409, 'foreign: refused even with a confirm');
  assert.equal(p.start('L_Gen_Train_7', true).status, 409);
  assert.equal(p.start('not_accepted', false).status, 404);
  const ask = p.start('gen_ref_01_02_depot_2', false);
  assert.equal(ask.status, 409);
  assert.equal(ask.body.needsConfirm, true);
  assert.equal(calls.length, 0, 'nothing ran');
  assert.equal(p.status().stage, 'idle');
});

test('ux build: build -> route smoke through powershell.exe directly, progress + result from the status files; one at a time', async () => {
  const { p, calls, root, w, tick } = fixture();
  const r = p.start('gen_ref_01_02_depot_2', true);
  assert.equal(r.status, 200, JSON.stringify(r.body));
  assert.equal(calls.length, 1);
  const b = calls[0];
  assert.equal(b.exe, 'powershell.exe', 'never Git Bash');
  assert.deepEqual(b.args.slice(0, 8), ['-NoProfile', '-NonInteractive', '-WindowStyle', 'Hidden', '-ExecutionPolicy', 'Bypass', '-File', path.join(root, 'Scripts', 'levelgen_build_map.ps1')]);
  assert.equal(arg(b, '-Layout'), 'gen_ref_01_02_depot_2');
  assert.equal(arg(b, '-Target'), '/Game/Maps/Generated/L_Gen_Depot_2');
  assert.equal(arg(b, '-StatusFile'), path.join(root, 'Saved/LevelGen/build_L_Gen_Depot_2.json'));
  assert.equal(b.cwd, root, 'runs in the server checkout');
  assert.equal(b.agent, 'user');
  // the lock: a second build is refused while this one runs
  const busy = p.start('gen_ref_01_02_depot_1_x4', false);
  assert.equal(busy.status, 409);
  assert.equal(busy.body.busy, true);
  // progress from the script's status file
  w('Saved/LevelGen/build_L_Gen_Depot_2.json', { schema: 'codex.job_status/1', state: 'waiting_lock', message: 'waiting for the build lock' });
  tick(42);
  let s = p.status();
  assert.equal(s.stage, 'build');
  assert.equal(s.elapsed_s, 42);
  assert.match(s.message, /замок/);
  const navLog = w('Saved/Logs/NavBake-L_Gen_Depot_2.log', '[NavBake] /Game/Maps/Generated/L_Gen_Depot_2: built=1 tiles: Default=812 Vehicle=640 (0.5 s)\n');
  w('Saved/LevelGen/build_L_Gen_Depot_2.json', { schema: 'codex.job_status/1', state: 'done', message: '/Game/Maps/Generated/L_Gen_Depot_2 ready', result: { actors: 1258, map: '/Game/Maps/Generated/L_Gen_Depot_2', navbake_log: navLog } });
  b.finish(0);
  await flush();
  assert.equal(calls.length, 2, 'the route smoke starts after a good build');
  const sm = calls[1];
  assert.equal(sm.exe, 'powershell.exe');
  assert.equal(arg(sm, '-File'), path.join(root, 'Scripts', 'levelgen_route_smoke.ps1'));
  assert.equal(arg(sm, '-Map'), '/Game/Maps/Generated/L_Gen_Depot_2');
  assert.equal(arg(sm, '-StatusFile'), path.join(root, 'Saved/LevelGen/route_L_Gen_Depot_2.json'));
  tick(10);
  s = p.status();
  assert.equal(s.stage, 'smoke');
  assert.equal(s.stage_elapsed_s, 10);
  assert.deepEqual([s.result.built, s.result.actors, s.result.nav_tiles], [true, 1258, 'Default=812 Vehicle=640']);
  w('Saved/LevelGen/route_L_Gen_Depot_2.json', { schema: 'codex.job_status/1', state: 'done', message: 'PASS: ...', result: { result: 'PASS', summary: 'Smoke: LevelGenRouteSmoke ... 25 / 25 waypoints, 316 s' } });
  sm.finish(0);
  await flush();
  s = p.status();
  assert.equal(s.stage, 'done');
  assert.equal(s.result.route, 'PASS');
  assert.match(s.result.route_summary ?? '', /25 \/ 25/);
  assert.equal(s.file, 'Content/Maps/Generated/L_Gen_Depot_2.umap', 'where to open it');
  // done: the lock is free again
  assert.equal(p.start('gen_ref_01_02_depot_1_x4', false).status, 200);
});

test('ux build: a refused / failed build stops before the smoke with the reason; a failed route smoke is FAIL with its summary', async () => {
  const { p, calls, w } = fixture();
  p.start('gen_ref_01_02_depot_1_x4', false);
  w('Saved/LevelGen/build_L_Gen_Depot_1_X4.json', { state: 'refused', reason: 'editor_open', message: 'Unreal Editor (CodexTactics) is open on this checkout: close it first' });
  calls[0].finish(2);
  await flush();
  let s = p.status();
  assert.equal(s.stage, 'failed');
  assert.equal(calls.length, 1, 'no smoke after a failed build');
  assert.match(s.result.reason ?? '', /editor_open/);
  assert.match(s.message, /close it first/);
  // again (the old status files are cleared on start), now the smoke fails
  assert.equal(p.start('gen_ref_01_02_depot_1_x4', false).status, 200);
  assert.equal(p.status().build, null, 'a new run starts with fresh status files');
  w('Saved/LevelGen/build_L_Gen_Depot_1_X4.json', { state: 'done', result: { actors: 900 } });
  calls[1].finish(0);
  await flush();
  w('Saved/LevelGen/route_L_Gen_Depot_1_X4.json', { state: 'failed', reason: 'smoke_failed', message: 'FAIL: stuck at #7', result: { result: 'FAIL', summary: 'stuck at waypoint 7' } });
  calls[2].finish(1);
  await flush();
  s = p.status();
  assert.equal(s.stage, 'failed');
  assert.equal(s.result.route, 'FAIL');
  assert.equal(s.result.built, true);
  assert.match(s.result.reason ?? '', /smoke_failed/);
});

test('build hidden (bug 2026-11-16 evening): no window - hidden powershell args, no shell / console inherited; the scripts output streams into the page log', async () => {
  // the spawn options: windowsHide, no shell (`start` / cmd), no detached console, NO inherited stdio (libuv then adds CREATE_NO_WINDOW)
  assert.equal(HIDDEN_SPAWN.windowsHide, true);
  assert.equal(HIDDEN_SPAWN.shell, false);
  assert.equal(HIDDEN_SPAWN.detached, false);
  assert.deepEqual([...HIDDEN_SPAWN.stdio], ['ignore', 'pipe', 'pipe']);
  assert.deepEqual(hiddenPowershellArgs('C:/x.ps1', ['-A', '1']), ['-NoProfile', '-NonInteractive', '-WindowStyle', 'Hidden', '-ExecutionPolicy', 'Bypass', '-File', 'C:/x.ps1', '-A', '1']);
  const { p, calls, root, w } = fixture();
  p.start('gen_ref_01_02_depot_1_x4', false);
  calls[0].say('levelgen: building the map from x4 ...');
  calls[0].say('placed 3872 actors');
  let s = p.status();
  assert.deepEqual(s.log, ['[1/2] levelgen: building the map from x4 ...', '[1/2] placed 3872 actors']);
  assert.match(fs.readFileSync(path.join(root, 'Saved/LevelGen/build_L_Gen_Depot_1_X4.log'), 'utf8'), /placed 3872 actors/);
  w('Saved/LevelGen/build_L_Gen_Depot_1_X4.json', { state: 'done', result: { actors: 3872 } });
  calls[0].finish(0);
  await flush();
  calls[1].say('Smoke RESULT: PASS');
  s = p.status();
  assert.equal(s.log.at(-1), '[2/2] Smoke RESULT: PASS');
  assert.ok(fs.existsSync(path.join(root, 'Saved/LevelGen/route_L_Gen_Depot_1_X4.log')));
  // the page gets the tail only; the server keeps log_lines
  for (let i = 0; i < 1000; i++) calls[1].say(`line ${i}`);
  s = p.status();
  assert.ok(s.log.length > 0 && s.log.length <= 400, `tail ${s.log.length}`);
  assert.equal(s.log.at(-1), '[2/2] line 999');
  calls[1].finish(0);
  await flush();
  // a new run starts with an empty log (and fresh log files)
  p.start('gen_ref_01_02_depot_1_x4', false);
  assert.deepEqual(p.status().log, []);
  assert.equal(fs.existsSync(path.join(root, 'Saved/LevelGen/route_L_Gen_Depot_1_X4.log')), false);
});

test('build hidden: the real runner streams a child stdout + stderr lines and its exit code (node itself, no powershell)', async () => {
  const lines: string[] = [];
  const h = powershellRunner(process.execPath, ['-e', 'console.log("one"); console.error("two"); console.log("three  "); process.exit(3)'], { cwd: process.cwd(), env: process.env, onLine: (l) => lines.push(l) });
  assert.equal(await h.done, 3);
  await flush();
  assert.deepEqual([...lines].sort(), ['one', 'three', 'two']);
});
