// T-020: the weather block write of «Управление картой» - only environment + cold_rate_mult change, every other key and the file's
// spelling stay, nothing is written when nothing changes. Temp dirs only (never the designer's Content/Data files).
import { describe, it, beforeEach, afterEach } from 'node:test';
import * as assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { applyEnvironmentEdit, levelJsonPath, readLevelEnvironment, validateEnvironmentEdit, writeLevelEnvironment } from './levelEnvironment.ts';
import type { EnvironmentEdit } from './levelEnvironment.ts';

const LEVEL = `{
  "$schema": "../Schemas/level_config.schema.json",
  "level_id": "L_T",
  "levelgen_layout": "Content/Data/LevelGen/accepted/lay_t.json",
  "environment": {
    "_comment": "designer note: keep",
    "locked": true,
    "start_hour": 12,
    "weather": "Clear",
    "seed": 1
  },
  "cold_rate_mult": 0.8,
  "waves": [
    { "wave_index": 1, "spawns": [ { "enemy_type": "HOUND", "count": 3 } ] }
  ]
}
`;

describe('applyEnvironmentEdit (T-020)', () => {
  const env = JSON.parse(LEVEL).environment;
  const same: EnvironmentEdit = { environment: env, cold_rate_mult: 0.8 };

  it('an edit that changes nothing returns the original text byte for byte (no write)', () => {
    const r = applyEnvironmentEdit(LEVEL, same);
    assert.equal(r.changed, false);
    assert.equal(r.text, LEVEL);
  });

  it('changing the weather keeps every other key, the _comment, the waves and the layout of the file', () => {
    const r = applyEnvironmentEdit(LEVEL, { environment: { ...env, weather: 'Blizzard', weather_strength: 1.5, start_hour: 22.5 }, cold_rate_mult: 0.8 });
    assert.equal(r.changed, true);
    const data = JSON.parse(r.text);
    assert.deepEqual(data.environment, { _comment: 'designer note: keep', locked: true, start_hour: 22.5, weather: 'Blizzard', seed: 1, weather_strength: 1.5 });
    assert.equal(data.cold_rate_mult, 0.8);
    assert.equal(data.level_id, 'L_T');
    assert.equal(data.levelgen_layout, 'Content/Data/LevelGen/accepted/lay_t.json');
    assert.deepEqual(data.waves, JSON.parse(LEVEL).waves);
    assert.ok(r.text.includes('"waves": [\n    { "wave_index": 1, "spawns": [ { "enemy_type": "HOUND", "count": 3 } ] }\n  ]'), 'the hand-made wave lines are not reformatted');
  });

  it('cold_rate_mult: set, remove with null, and 1 (the default) is an absent key', () => {
    assert.equal(JSON.parse(applyEnvironmentEdit(LEVEL, { environment: env, cold_rate_mult: 1.4 }).text).cold_rate_mult, 1.4);
    assert.equal('cold_rate_mult' in JSON.parse(applyEnvironmentEdit(LEVEL, { environment: env, cold_rate_mult: null }).text), false);
    assert.equal('cold_rate_mult' in JSON.parse(applyEnvironmentEdit(LEVEL, { environment: env, cold_rate_mult: 1 }).text), false);
  });

  it('adds the blocks to a level that has none; an empty or null environment removes the block', () => {
    const bare = '{\n  "level_id": "L_B",\n  "waves": []\n}\n';
    const added = JSON.parse(applyEnvironmentEdit(bare, { environment: { weather: 'Snowfall', locked: true }, cold_rate_mult: 0.5 }).text);
    assert.deepEqual(added.environment, { weather: 'Snowfall', locked: true });
    assert.equal(added.cold_rate_mult, 0.5);
    assert.equal(applyEnvironmentEdit(bare, { environment: null, cold_rate_mult: null }).changed, false, 'nothing to remove');
    assert.equal(applyEnvironmentEdit(bare, { environment: {}, cold_rate_mult: null }).text, bare);
    assert.equal('environment' in JSON.parse(applyEnvironmentEdit(LEVEL, { environment: {}, cold_rate_mult: 0.8 }).text), false);
    assert.equal('environment' in JSON.parse(applyEnvironmentEdit(LEVEL, { environment: null, cold_rate_mult: 0.8 }).text), false);
  });

  it('keeps a BOM and CRLF line endings', () => {
    const crlf = `﻿${LEVEL.replace(/\n/g, '\r\n')}`;
    const r = applyEnvironmentEdit(crlf, { environment: { ...env, weather: 'Overcast' }, cold_rate_mult: 0.8 });
    assert.equal(r.changed, true);
    assert.ok(r.text.startsWith('﻿'));
    assert.ok(r.text.includes('\r\n') && !/[^\r]\n/.test(r.text));
    assert.equal(applyEnvironmentEdit(crlf, same).text, crlf);
  });
});

describe('validateEnvironmentEdit (T-020)', () => {
  it('weather_strength 0..2, cold_rate_mult 0..3, the Wave Editor\'s own environment rules', () => {
    assert.deepEqual(validateEnvironmentEdit({ environment: { weather: 'Clear', weather_strength: 0 }, cold_rate_mult: 3 }), []);
    assert.deepEqual(validateEnvironmentEdit({ environment: { weather_strength: 2 }, cold_rate_mult: 0 }), []);
    assert.equal(validateEnvironmentEdit({ environment: { weather_strength: 2.1 }, cold_rate_mult: null }).length, 1);
    assert.equal(validateEnvironmentEdit({ environment: { weather_strength: -0.1 }, cold_rate_mult: null }).length, 1);
    assert.equal(validateEnvironmentEdit({ environment: null, cold_rate_mult: 3.1 }).length, 1);
    assert.equal(validateEnvironmentEdit({ environment: { start_hour: 25 }, cold_rate_mult: null }).length, 1);
    assert.equal(validateEnvironmentEdit({ environment: { climate: 'arctic', weather: 'Rain' }, cold_rate_mult: null }).length, 1);
  });
});

describe('writeLevelEnvironment / readLevelEnvironment on a temp dir (T-020)', () => {
  let dir = '';
  beforeEach(() => {
    dir = fs.mkdtempSync(path.join(os.tmpdir(), 'tg-env-'));
    fs.writeFileSync(path.join(dir, 'L_T.json'), LEVEL);
    fs.writeFileSync(path.join(dir, 'L_Bare.json'), '{ "level_id": "L_Bare", "waves": [] }\n');
  });
  afterEach(() => { fs.rmSync(dir, { recursive: true, force: true }); });

  it('reads the block, the cold multiplier and the layout name; a bare level reads as nulls', () => {
    const s = readLevelEnvironment('L_T', dir);
    assert.equal(s.file, 'L_T.json');
    assert.equal(s.environment?.weather, 'Clear');
    assert.equal(s.environment?.start_hour, 12);
    assert.equal(s.cold_rate_mult, 0.8);
    assert.equal(s.layout, 'lay_t');
    const b = readLevelEnvironment('L_Bare', dir);
    assert.deepEqual([b.environment, b.cold_rate_mult, b.layout], [null, null, '']);
    assert.throws(() => readLevelEnvironment('L_Missing', dir), /not found/);
  });

  it('writes the change atomically; an unchanged edit does not touch the file', () => {
    const file = path.join(dir, 'L_T.json');
    const before = fs.readFileSync(file, 'utf8');
    const mtime = fs.statSync(file).mtimeMs;
    const env = readLevelEnvironment('L_T', dir).environment;
    assert.deepEqual(writeLevelEnvironment('L_T', { environment: env, cold_rate_mult: 0.8 }, dir), { changed: false, file: 'L_T.json' });
    assert.equal(fs.readFileSync(file, 'utf8'), before);
    assert.equal(fs.statSync(file).mtimeMs, mtime);

    assert.deepEqual(writeLevelEnvironment('L_T', { environment: { ...env, weather: 'Snowfall' }, cold_rate_mult: 1.2 }, dir), { changed: true, file: 'L_T.json' });
    const after = JSON.parse(fs.readFileSync(file, 'utf8'));
    assert.equal(after.environment.weather, 'Snowfall');
    assert.equal(after.environment._comment, 'designer note: keep');
    assert.equal(after.cold_rate_mult, 1.2);
    assert.deepEqual(fs.readdirSync(dir).sort(), ['L_Bare.json', 'L_T.json'], 'no temp file left');
  });

  it('refuses a bad edit, a missing map and a path-like map name without touching anything', () => {
    const before = fs.readFileSync(path.join(dir, 'L_T.json'), 'utf8');
    assert.throws(() => writeLevelEnvironment('L_T', { environment: { weather_strength: 5 }, cold_rate_mult: null }, dir), /Сила погоды/);
    assert.throws(() => writeLevelEnvironment('L_Missing', { environment: null, cold_rate_mult: null }, dir), /not found/);
    assert.throws(() => writeLevelEnvironment('../L_T', { environment: null, cold_rate_mult: null }, dir), /not found/);
    assert.equal(levelJsonPath('a/b', dir), null);
    assert.equal(levelJsonPath('..', dir), null);
    assert.equal(fs.readFileSync(path.join(dir, 'L_T.json'), 'utf8'), before);
  });
});
