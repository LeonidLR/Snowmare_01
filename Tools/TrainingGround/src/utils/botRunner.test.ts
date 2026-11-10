import { test } from 'node:test';
import assert from 'node:assert/strict';
import { sanitizeBotRunArgs, FORBIDDEN_SHELL_PATTERN } from './botRunner.ts';

test('botRunner: valid payload sanitizes properly with defaults', () => {
  const result = sanitizeBotRunArgs({});
  assert.equal(result.ok, true);
  if (result.ok) {
    assert.equal(result.args.runs, undefined);
    assert.equal(result.args.profile, 'NORMAL');
    assert.equal(result.args.speed, '5.0');
    assert.equal(result.args.loadout, 'COLLECT');
    assert.equal(result.args.mapPath, '/Game/Maps/L_MovementTest');
    assert.equal(result.args.levelJsonPath, '');
  }
});

test('botRunner: valid payload with custom values', () => {
  const result = sanitizeBotRunArgs({
    runs: 25,
    profile: 'VETERAN',
    speed: 8.5,
    loadout: 'UNIQUE',
    mapPath: '/Game/Maps/L_Gen_Depot_1',
    levelJsonPath: 'C:\\CodexTactics\\Content\\Data\\LevelJson\\L_Gen_Depot_1.json',
  });
  assert.equal(result.ok, true);
  if (result.ok) {
    assert.equal(result.args.runs, '25');
    assert.equal(result.args.profile, 'VETERAN');
    assert.equal(result.args.speed, '8.5');
    assert.equal(result.args.loadout, 'UNIQUE');
    assert.equal(result.args.mapPath, '/Game/Maps/L_Gen_Depot_1');
    assert.equal(result.args.levelJsonPath, 'C:\\CodexTactics\\Content\\Data\\LevelJson\\L_Gen_Depot_1.json');
  }
});

test('botRunner: command injection vectors are rejected', () => {
  // Ampersand injection (X&calc&)
  assert.equal(sanitizeBotRunArgs({ profile: 'X&calc&' }).ok, false);
  assert.equal(sanitizeBotRunArgs({ loadout: 'COLLECT&calc' }).ok, false);

  // Semicolon injection
  assert.equal(sanitizeBotRunArgs({ profile: 'NORMAL;calc' }).ok, false);

  // Pipe injection
  assert.equal(sanitizeBotRunArgs({ runs: '1 | calc' as any }).ok, false);
  assert.equal(sanitizeBotRunArgs({ profile: 'NORMAL | dir' }).ok, false);

  // Caret / quotes / percent / exclamation marks / backticks
  for (const char of ['^', '%', '!', '"', "'", '`', '$', '\r', '\n', '<', '>']) {
    assert.equal(FORBIDDEN_SHELL_PATTERN.test(char), true, `Must match forbidden char: ${char}`);
    assert.equal(sanitizeBotRunArgs({ profile: `NORM${char}AL` }).ok, false);
  }
});

test('botRunner: invalid numeric ranges are rejected', () => {
  // runs out of range
  assert.equal(sanitizeBotRunArgs({ runs: 0 }).ok, false);
  assert.equal(sanitizeBotRunArgs({ runs: -5 }).ok, false);
  assert.equal(sanitizeBotRunArgs({ runs: 1001 }).ok, false);
  assert.equal(sanitizeBotRunArgs({ runs: 'abc' as any }).ok, false);

  // speed out of range
  assert.equal(sanitizeBotRunArgs({ speed: 0 }).ok, false);
  assert.equal(sanitizeBotRunArgs({ speed: -1.0 }).ok, false);
  assert.equal(sanitizeBotRunArgs({ speed: 101.0 }).ok, false);
  assert.equal(sanitizeBotRunArgs({ speed: 'fast' as any }).ok, false);
});

test('botRunner: path traversal and invalid Unreal paths are rejected', () => {
  // Directory traversal in mapPath
  assert.equal(sanitizeBotRunArgs({ mapPath: '/Game/Maps/../SecretMap' }).ok, false);
  // Non-/Game/ mapPath
  assert.equal(sanitizeBotRunArgs({ mapPath: 'C:\\Windows\\System32\\cmd.exe' }).ok, false);

  // Directory traversal in levelJsonPath
  assert.equal(sanitizeBotRunArgs({ levelJsonPath: 'C:\\Levels\\..\\system.json' }).ok, false);
  // Non-.json extension
  assert.equal(sanitizeBotRunArgs({ levelJsonPath: 'C:\\Levels\\exploit.bat' }).ok, false);
});
