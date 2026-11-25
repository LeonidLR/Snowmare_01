import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { test } from 'node:test';
import {
  handleOpenEditor,
  handlePlaySector,
  readTrainingData,
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
