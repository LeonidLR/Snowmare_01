import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { test } from 'node:test';
import { createServer } from 'node:http';
import {
  handleOpenEditor,
  handlePlaySector,
  readTrainingData,
  readTrainingBuilds,
  readSectorFightStats,
  setupTrainingApi,
  readTrainingSectors,
  UE_EDITOR_EXE,
  writeTrainingSelection,
  X6_MAP_PATH,
} from '../../server/trainingApi.ts';
import { X6 } from './trainingTypes.ts';

test('only kept maps and real sectors can be saved; level files are left intact', (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'tg-training-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const put = (relative: string, text: string) => {
    const file = path.join(root, relative);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, text);
    return file;
  };
  const levelText = '{"level_id":"L_Gen_Depot_1_X6","cold_rate_mult":0}';
  const level = put('Content/Data/LevelJson/' + X6 + '.json', levelText);
  assert.equal(readTrainingData(root).maps.length, 5);
  assert.deepEqual(readTrainingData(root).active, { map: X6, sector: 0 });
  assert.equal(writeTrainingSelection(root, { map: 'L_Gen_Bgen_Test_X6', sector: 0 }), null);
  assert.equal(writeTrainingSelection(root, { map: X6, sector: 1 }), null);
  assert.equal(fs.existsSync(path.join(root, 'Saved/Training')), false);

  const positions = [{ pos: [100, 200, 0], yaw: 45 }, { pos: [300, 400, 0], yaw: 65 }];
  const sector = {
    id: 5, name: 'Sector 5', box_cm: { min: [0, 0], max: [6100, 7200] }, size_m: [61, 72],
    starts: positions, pmc_posts: positions.map((point, i) => ({ ...point, id: 'p' + i, kind: 'street' })),
    pmc_squad: { count: 3, posture: 'patrol', weights: 'pmc_commander.json' },
    contents: { enterable_buildings: 2, covers: { wall: 4 }, fences_m: 7, ladders: 1 },
  };
  const sectorText = JSON.stringify({ map: X6, sectors: [sector, { ...sector, id: 7 }] });
  const staticFile = put('Content/Data/LevelJson/' + X6 + '.sectors.json', sectorText);
  assert.deepEqual(readTrainingSectors(root).map(({ id }) => id), [5]);
  assert.equal(writeTrainingSelection(root, { map: 'L_MainMenu', sector: 5 }), null);
  const active = { map: X6, sector: 5 };
  assert.deepEqual(writeTrainingSelection(root, { ...active, cold_rate_mult: 3 }), active);
  assert.deepEqual(readTrainingData(root).active, active);
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(root, 'Saved/Training/active_sector.json'), 'utf8')), active);
  assert.deepEqual(writeTrainingSelection(root, { map: 'L_MainMenu', sector: 0 }), { map: 'L_MainMenu', sector: 0 });
  assert.equal(fs.readFileSync(level, 'utf8'), levelText);
  assert.equal(fs.readFileSync(staticFile, 'utf8'), sectorText);

  const demo = { type: 'header', v: 1, map: 'UEDPIE_0_' + X6, sector_id: 5, started_utc: '2026-11-24T15:00:00Z' };
  put('Saved/Demos/demo.jsonl', JSON.stringify(demo) + '\n' + 'large frame data ignored');
  put('Saved/Demos/bad.jsonl', '{ corrupt\n');
  put('Saved/Arena/arena.json', JSON.stringify({ schema: 'codex.pmc_arena/1', map: '/Game/Maps/' + X6, started_utc: '2026-11-24T16:00:00Z' }));
  assert.deepEqual(readTrainingData(root).runs.map(({ source, map, sector }) => [source, map, sector]),
    [['arena', X6, null], ['demo', X6, 5]]);
});

test('open-editor and play guards refuse unsafe launches and build expected commands when clear', async (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'tg-launch-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const put = (relative: string, text: string) => {
    const file = path.join(root, relative);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, text);
    return file;
  };
  put('docs/port/FIGHTS_HOWTO_RU.md', '# Как запустить\n1. Выберите сектор.\n');
  put('Scripts/Tools/open_editor_nozen.cmd', '@echo off\n');
  const positions = [{ pos: [100, 200, 0], yaw: 45 }, { pos: [300, 400, 0], yaw: 65 }];
  const sector = {
    id: 3, name: 'Sector 3', box_cm: { min: [0, 0], max: [6100, 7200] }, size_m: [61, 72],
    starts: positions, pmc_posts: positions.map((point, i) => ({ ...point, id: 'p' + i, kind: 'street' })),
    pmc_squad: { count: 3, posture: 'patrol', weights: 'pmc_commander.json' },
    contents: { enterable_buildings: 2, covers: { wall: 4 }, fences_m: 7, ladders: 1 },
  };
  put('Content/Data/LevelJson/' + X6 + '.sectors.json', JSON.stringify({ map: X6, sectors: [sector] }));

  const data = readTrainingData(root);
  assert.match(data.howto ?? '', /Как запустить/);
  assert.ok((data.editorCmdPath ?? '').endsWith('open_editor_nozen.cmd'));

  // Guard 1: Lanes worktree refused
  const lanesRes = await handlePlaySector(root, { sector: 3 }, { isLanesWorktree: () => true });
  assert.equal(lanesRes.status, 403);
  assert.match(String(lanesRes.body.error), /Lanes/);

  // Guard 2: agent.lock refused
  const lockRes = await handleOpenEditor(root, {
    isLanesWorktree: () => false,
    readAgentLock: () => ({ agent: 'claude', purpose: 'build' }),
  });
  assert.equal(lockRes.status, 409);
  assert.match(String(lockRes.body.error), /agent\.lock/);

  // Guard 3: running main editor/game refused
  const runningRes = await handlePlaySector(root, { sector: 3 }, {
    isLanesWorktree: () => false,
    readAgentLock: () => null,
    isMainEditorRunning: () => true,
  });
  assert.equal(runningRes.status, 409);
  assert.match(String(runningRes.body.error), /уже запущены/);

  // OK: open-editor
  const spawned: Array<{ command: string; args: string[] }> = [];
  const okEditor = await handleOpenEditor(root, {
    ensureZenReady: async () => {},
    isLanesWorktree: () => false,
    readAgentLock: () => null,
    isMainEditorRunning: () => false,
    spawnDetached: (command, args) => {
      spawned.push({ command, args });
      return 4242;
    },
  });
  assert.equal(okEditor.status, 200);
  assert.equal(okEditor.body.pid, 4242);
  assert.equal(spawned[0].command, UE_EDITOR_EXE);
  assert.deepEqual(spawned[0].args, [path.join(root, 'CodexTactics.uproject'), X6_MAP_PATH, '-NoZenAutoLaunch']);

  // OK: play sector 3 (writes active_sector.json + ensures Zen + spawns UnrealEditor -game)
  let zenCalled = false;
  const okPlay = await handlePlaySector(root, { sector: 3 }, {
    isLanesWorktree: () => false,
    readAgentLock: () => null,
    isMainEditorRunning: () => false,
    ensureZenReady: async () => { zenCalled = true; },
    spawnDetached: (command, args) => {
      spawned.push({ command, args });
      return 5150;
    },
  });
  assert.equal(zenCalled, true);
  assert.equal(okPlay.status, 200);
  assert.equal(okPlay.body.pid, 5150);
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(root, 'Saved/Training/active_sector.json'), 'utf8')), { map: X6, sector: 3 });
  assert.equal(spawned[1].command, UE_EDITOR_EXE);
  assert.deepEqual(spawned[1].args, [
    path.join(root, 'CodexTactics.uproject'),
    X6_MAP_PATH,
    '-game',
    '-Sector=3',
    '-windowed',
    '-ResX=1600',
    '-ResY=900',
    '-NoZenAutoLaunch',
  ]);
});


function buildFixture(t: { after: (cleanup: () => void) => void }) {
  const sandbox = fs.mkdtempSync(path.join(os.tmpdir(), 'tg-build-'));
  const root = path.join(sandbox, 'CodexTactics');
  const play = path.join(sandbox, 'CodexTactics-play');
  const put = (base: string, relative: string, text = '') => {
    const file = path.join(base, relative);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, text);
    return file;
  };
  t.after(() => {
    assert.ok(path.resolve(sandbox).startsWith(path.join(path.resolve(os.tmpdir()), 'tg-build-')));
    fs.rmSync(sandbox, { recursive: true, force: true });
  });
  put(root, 'CodexTactics.uproject', '{}');
  put(play, 'CodexTactics.uproject', '{}');
  const positions = [{ pos: [100, 200, 0], yaw: 0 }, { pos: [300, 400, 0], yaw: 0 }];
  put(root, 'Content/Data/LevelJson/' + X6 + '.sectors.json', JSON.stringify({ map: X6, sectors: [{
    id: 3, name: 'Sector 3', box_cm: { min: [0, 0], max: [1000, 1000] }, size_m: [10, 10], starts: positions,
    pmc_posts: positions.map((point, i) => ({ ...point, id: 'p' + i, kind: 'street' })),
    pmc_squad: { count: 3, posture: 'patrol', weights: 'pmc_commander.json' },
    contents: { enterable_buildings: 1, covers: { wall: 1 }, fences_m: 0, ladders: 0 },
  }] }));
  return { root, play, put };
}

const clearLaunch = {
  isLanesWorktree: () => false,
  readAgentLock: () => null,
  isMainEditorRunning: () => false,
  ensureZenReady: async () => {},
  spawnDetached: () => 123,
};

test('Main/Play launch uses only canonical project and cwd; selection stays on TG root', async (t) => {
  const { root, play, put } = buildFixture(t);
  const levelName = 'Content/Data/LevelJson/' + X6 + '.json';
  const mainText = '{"environment":{"start_hour":12}}';
  put(root, levelName, mainText);
  put(play, levelName, '{"environment":{"start_hour":2}}');
  const calls: Array<{ args: string[]; cwd: string }> = [];
  const deps = { ...clearLaunch, spawnDetached: (_command: string, args: string[], cwd: string) => {
    assert.equal(fs.readFileSync(path.join(cwd, levelName), 'utf8'), mainText, 'settings arrive before spawning');
    calls.push({ args, cwd }); return 123;
  } };
  const selected = await handlePlaySector(root, { build: 'play', sector: 0, root: 'C:/untrusted' }, deps);
  assert.equal(selected.status, 200);
  assert.equal(selected.body.build, 'play');
  assert.match(String(selected.body.syncMessage), /start_hour 2 -> 12/);
  assert.equal(calls[0].args[0], path.join(play, 'CodexTactics.uproject'));
  assert.equal(calls[0].cwd, play);
  assert.ok(calls[0].args.includes('-Sector=0'));
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(root, 'Saved/Training/active_sector.json'), 'utf8')), { map: X6, sector: 0 });
  assert.equal(fs.existsSync(path.join(play, 'Saved/Training')), false, 'selection is written on server root, not build root');
  assert.equal((await handleOpenEditor(root, deps, { build: 'play' })).status, 200);
  assert.deepEqual(calls[1], { args: [path.join(play, 'CodexTactics.uproject'), X6_MAP_PATH, '-NoZenAutoLaunch'], cwd: play });
  assert.equal((await handlePlaySector(root, { build: 'main', sector: 0 }, deps)).status, 200);
  assert.equal(calls[2].cwd, root);
  assert.equal(calls[2].args[0], path.join(root, 'CodexTactics.uproject'));
  // Guard callbacks must inspect the selected build. A main lock must not block Play.
  const lockDeps = { ...deps, readAgentLock: (checked: string) => checked === root ? { agent: 'builder' } : null };
  assert.equal((await handlePlaySector(root, { build: 'play', sector: 0 }, lockDeps)).status, 200);
  assert.equal((await handlePlaySector(root, { build: 'main', sector: 0 }, lockDeps)).status, 409);
  const playLock = { ...deps, readAgentLock: (checked: string) => checked === play ? { agent: 'builder' } : null };
  assert.equal((await handleOpenEditor(root, playLock, { build: 'play' })).status, 409);
  const running = { ...deps, isMainEditorRunning: (project: string) => project === path.join(play, 'CodexTactics.uproject') };
  assert.equal((await handlePlaySector(root, { build: 'play', sector: 0 }, running)).status, 409);
  assert.equal((await handleOpenEditor(root, running, { build: 'play' })).status, 409);
  assert.equal((await handlePlaySector(root, { build: 'play', sector: 0 }, { ...deps, isLanesWorktree: () => true })).status, 403);
  for (const build of ['../other', 'worker', '', null, 1]) {
    assert.equal((await handlePlaySector(root, { build, sector: 0 }, deps)).status, 400);
    assert.equal((await handleOpenEditor(root, deps, { build })).status, 400);
  }
  fs.unlinkSync(path.join(play, 'CodexTactics.uproject'));
  const missing = await handlePlaySector(root, { build: 'play', sector: 0 }, deps);
  assert.equal(missing.status, 404);
  assert.match(String(missing.body.error), /Play отсутствует/);
  assert.equal((await handleOpenEditor(root, deps, { build: 'play' })).status, 404);
});

test('fresh Play is the default; source changes and missing binaries give an honest freshness hint', (t) => {
  const { root, play, put } = buildFixture(t);
  const source = put(play, 'Source/Example.cpp', '// source');
  const dll = put(play, 'Binaries/Win64/UnrealEditor-CodexTactics.dll', 'test artifact');
  put(play, 'Saved/Logs/RemoteBuilds.jsonl', JSON.stringify({ exit: 0, head: 'abcd1234' }) + '\n');
  fs.utimesSync(source, 100, 100); fs.utimesSync(dll, 200, 200);
  let status = readTrainingBuilds(root);
  assert.equal(status.defaultBuild, 'play');
  assert.equal(status.play.fresh, true);
  assert.equal(status.play.builtHead, 'abcd1234');
  assert.equal(status.play.label, 'Play: собрано, abcd1234');
  fs.utimesSync(source, 300, 300);
  status = readTrainingBuilds(root);
  assert.equal(status.defaultBuild, 'main');
  assert.equal(status.play.label, 'Play: устарело');
  fs.unlinkSync(dll);
  assert.equal(readTrainingBuilds(root).play.label, 'Play: не собрана');
  fs.unlinkSync(path.join(play, 'CodexTactics.uproject'));
  assert.equal(readTrainingBuilds(root).play.label, 'Play: отсутствует');
});

test('Play launch recordings through shared Demos are counted in the TG sector fight table', async (t) => {
  const { root, play, put } = buildFixture(t);
  const shared = path.join(root, 'Saved/Demos');
  fs.mkdirSync(shared, { recursive: true });
  fs.mkdirSync(path.join(play, 'Saved'), { recursive: true });
  const junction = path.join(play, 'Saved/Demos');
  fs.symlinkSync(shared, junction, 'junction');
  assert.equal(fs.realpathSync(junction), fs.realpathSync(shared));
  const result = await handlePlaySector(root, { build: 'play', sector: 3 }, {
    ...clearLaunch,
    spawnDetached: (_command, args, cwd) => {
      assert.equal(cwd, play);
      assert.equal(args[0], path.join(play, 'CodexTactics.uproject'));
      assert.ok(args.includes('-Sector=3'));
      // Recorder output uses the actual shared filesystem path, not a mocked stats result.
      put(cwd, 'Saved/Demos/tg-play.jsonl', [
        { type: 'header', v: 1, map: X6, sector_id: 3, started_utc: '2026-11-26T12:00:00Z' },
        { type: 'round_end', sector_id: 3, result: 'victory', duration_s: 60 },
        { type: 'footer', rounds: 1, score_victory: 1, duration_s: 60 },
      ].map((row) => JSON.stringify(row)).join('\n') + '\n');
      return 123;
    },
  });
  assert.equal(result.status, 200);
  assert.equal(fs.existsSync(path.join(shared, 'tg-play.jsonl')), true);
  const stats = readSectorFightStats(root).sectors.find((row) => row.sector === 3)!;
  assert.equal(stats.sessions, 1); assert.equal(stats.rounds, 1); assert.equal(stats.wins, 1);
  assert.equal(readTrainingData(root).runs[0].file, 'tg-play.jsonl');
  // Remove the fixture junction before recursively cleaning up the fixture.
  if (process.platform === 'win32') fs.rmdirSync(junction); else fs.unlinkSync(junction);
  assert.equal(fs.existsSync(path.join(shared, 'tg-play.jsonl')), true);
});

test('open-editor HTTP endpoint accepts and validates the build JSON payload', async (t) => {
  const { root, play } = buildFixture(t);
  const routes = new Map<string, Parameters<typeof setupTrainingApi>[0] extends (route: string, handler: infer H) => void ? H : never>();
  let project = '';
  setupTrainingApi((route, handler) => routes.set(route, handler), root, {
    ...clearLaunch, spawnDetached: (_command, args) => { project = args[0]; return 123; },
  });
  const server = createServer((req, res) => { const handler = routes.get(req.url ?? ''); if (handler) void handler(req, res); else { res.statusCode = 404; res.end(); } });
  await new Promise<void>((resolve) => server.listen(0, '127.0.0.1', resolve));
  t.after(() => { server.close(); });
  const address = server.address(); assert.ok(address && typeof address !== 'string');
  const url = `http://127.0.0.1:${address.port}/api/training/open-editor`;
  const response = await fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ build: 'play' }) });
  assert.equal(response.status, 200); assert.equal(project, path.join(play, 'CodexTactics.uproject'));
  const invalid = await fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ build: '../elsewhere' }) });
  assert.equal(invalid.status, 400);
});
