// Wave Editor «Скрытность и бой»: pure-logic tests (node --test, Node >= 22.18 strips the types; `npm test`).
// Also round-trips the real repo files: an unchanged save must be byte-identical and parse to the same data.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { jsonEqual, parseJsonText, patchJsonText } from './jsonTextPatch.ts';
import {
  CVARS, HORDE_ARCHETYPES, HORDE_BOOL_FIELDS, HORDE_BUILTIN, HORDE_NUMBER_FIELDS,
  applyLevelEncounterPatch, buildLevelPatch, effectiveHorde, mergeAiTuningCvars, pickLevelEncounter, validateCVarEdits,
  validateHorde, validateLevelPatch, validatePerception,
} from './stealthCombat.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const AI_DIR = path.join(ROOT, 'Content/Data/AI');
const LEVELS_DIR = path.join(ROOT, 'Content/Data/LevelJson');

const repoJsonFiles = (): string[] => [
  ...['enemy_perception.json', 'horde.json', 'ai_tuning.json', 'squad_roe.json'].map((f) => path.join(AI_DIR, f)),
  ...fs.readdirSync(LEVELS_DIR).filter((f) => f.endsWith('.json')).map((f) => path.join(LEVELS_DIR, f)),
].filter((f) => fs.existsSync(f));

test('patchJsonText: unchanged data is byte-identical for every repo data file', () => {
  for (const file of repoJsonFiles()) {
    const text = fs.readFileSync(file, 'utf-8');
    assert.equal(patchJsonText(text, parseJsonText(text)), text, path.basename(file));
  }
});

test('patchJsonText: a changed number keeps the rest of the text (spelling, one-line objects)', () => {
  const text = '{\n  "a": 45.0,\n  "o": { "x": 1, "y": 2.50 },\n  "list": [1, 2]\n}';
  const out = patchJsonText(text, { a: 45, o: { x: 3, y: 2.5 }, list: [1, 2] });
  assert.equal(out, '{\n  "a": 45.0,\n  "o": { "x": 3, "y": 2.50 },\n  "list": [1, 2]\n}');
});

test('patchJsonText: add / remove keys, keep unknown keys, CRLF and BOM', () => {
  const text = '﻿{\r\n  "keep": "x",\r\n  "gone": 1,\r\n  "unknown_key": [true]\r\n}\r\n';
  const out = patchJsonText(text, { keep: 'x', unknown_key: [true], added: { k: 2 } });
  assert.ok(out.startsWith('﻿'));
  assert.ok(!/[^\r]\n/.test(out), 'only CRLF line ends');
  assert.ok(out.endsWith('}\r\n'));
  assert.ok(jsonEqual(parseJsonText(out), { keep: 'x', unknown_key: [true], added: { k: 2 } }));
  // Remove the last member and every member.
  assert.deepEqual(JSON.parse(patchJsonText('{\n  "a": 1,\n  "b": 2\n}', { a: 1 })), { a: 1 });
  assert.equal(patchJsonText('{\n  "a": 1,\n  "b": 2\n}', { a: 1 }), '{\n  "a": 1\n}');
  assert.deepEqual(JSON.parse(patchJsonText('{ "a": 1, "b": 2 }', {})), {});
  assert.deepEqual(JSON.parse(patchJsonText('{\n  "a": 1,\n  "b": 2,\n  "c": 3\n}', { a: 1, c: 3, d: 4 })), { a: 1, c: 3, d: 4 });
});

test('patchJsonText: level patch keeps every other key and the file style', () => {
  const file = path.join(LEVELS_DIR, 'stage_02.json');
  const text = fs.readFileSync(file, 'utf-8');
  const level = parseJsonText(text) as Record<string, unknown>;
  const patched = applyLevelEncounterPatch(level, { combat_start: 'ambush', patrol_search_seconds: 45, horde: { count: 10 } });
  const out = patchJsonText(text, patched);
  const back = parseJsonText(out) as Record<string, unknown>;
  assert.equal(back.combat_start, 'ambush');
  assert.equal(back.patrol_search_seconds, 45);
  assert.deepEqual(back.horde, { count: 10 });
  for (const key of Object.keys(level)) assert.ok(jsonEqual(back[key], level[key]), key);
  assert.ok(out.includes('"prep_phase_duration": 45.0'), 'untouched values keep their spelling');
  // Removing the keys again restores the original text.
  const restored = applyLevelEncounterPatch(back, { combat_start: null, patrol_search_seconds: null, horde: null });
  assert.equal(patchJsonText(out, restored), text);
});

test('perception: repo file is valid; negatives / FOV > 180 / NaN are rejected', () => {
  const doc = parseJsonText(fs.readFileSync(path.join(AI_DIR, 'enemy_perception.json'), 'utf-8'));
  assert.deepEqual(validatePerception(doc), []);
  const bad = JSON.parse(JSON.stringify(doc));
  bad.archetypes.MARKSMAN.sight_range_m = -1;
  bad.archetypes.BRUTE.sight_half_angle_deg = 200;
  bad.search.duration_seconds = Number.NaN;
  assert.equal(validatePerception(bad).length, 3);
});

test('horde: repo file is valid; min > max distance, bad counts and an empty mix are rejected', () => {
  const doc = parseJsonText(fs.readFileSync(path.join(AI_DIR, 'horde.json'), 'utf-8')) as Record<string, any>;
  assert.deepEqual(validateHorde(effectiveHorde(doc)), []);
  assert.ok(validateHorde({ ...doc, min_distance_m: 50, max_distance_m: 40 }).some((e) => e.includes('мин.')));
  assert.ok(validateHorde({ ...doc, count: 0 }).length > 0);
  assert.ok(validateHorde({ ...doc, count: 2.5 }).length > 0);
  assert.ok(validateHorde({ ...doc, spawn_samples: 300 }).length > 0);
  assert.ok(validateHorde({ ...doc, composition: { FROST_HOUND: 0 } }).length > 0);
  assert.ok(validateHorde({ ...doc, composition: { FROST_HOUND: -1 } }).length > 0);
});

test('level patch: absent defaults stay absent, only changed horde keys are written, reset removes the override', () => {
  const none = pickLevelEncounter({ level_id: 'x', waves: [] });
  assert.deepEqual(buildLevelPatch(none, { combat_start: 'auto', patrol_search_seconds: null, horde_enabled: true, horde: {} }), {});
  assert.deepEqual(
    buildLevelPatch(none, { combat_start: 'ambush', patrol_search_seconds: 30, horde_enabled: false, horde: { count: 12 } }),
    { combat_start: 'ambush', patrol_search_seconds: 30, horde_enabled: false, horde: { count: 12 } },
  );
  const set = pickLevelEncounter({ combat_start: 'ambush', patrol_search_seconds: 30, horde_enabled: false, horde: { count: 12 } });
  assert.deepEqual(
    buildLevelPatch(set, { combat_start: 'auto', patrol_search_seconds: null, horde_enabled: true, horde: {} }),
    { combat_start: null, patrol_search_seconds: null, horde_enabled: null, horde: null },
  );
  assert.deepEqual(validateLevelPatch({ combat_start: 'later' }).length, 1);
  assert.deepEqual(validateLevelPatch({ patrol_search_seconds: -5 }).length, 1);
  assert.deepEqual(validateLevelPatch({ waves: [] }).length, 1, 'only the four keys may be patched');
  assert.deepEqual(validateLevelPatch({ horde: { max_distance_m: 20 } }, { min_distance_m: 10 }), []);
  assert.ok(validateLevelPatch({ horde: { max_distance_m: 20 } }, { min_distance_m: 25 }).length > 0);
});

test('ai_tuning cvars: whitelisted stealth knobs only, other keys kept, empty removes', () => {
  const doc = { comment: 'c', result: { runs: 8 }, cvars: { 'Codex.Bot.AssaultClearRadius': '1800', 'Codex.Perception.HearingScale': '1.2' } };
  const out = mergeAiTuningCvars(doc, {
    'Codex.Perception.HearingScale': '', 'Codex.Patrol.SearchSeconds': '45', 'Codex.Horde.Enabled': '0', 'Codex.Bot.AssaultClearRadius': '1',
  });
  assert.deepEqual(out.cvars, { 'Codex.Bot.AssaultClearRadius': '1800', 'Codex.Patrol.SearchSeconds': '45' });
  assert.deepEqual(out.result, { runs: 8 });
  assert.equal(validateCVarEdits({ 'Codex.Perception.SightRangeScale': '-1' }).length, 1);
  assert.equal(validateCVarEdits({ 'Codex.Perception.SightRangeScale': 'abc' }).length, 1);
  assert.equal(validateCVarEdits({ 'Codex.RealTimeOrders': '0' }).length, 1, 'informational cvars are not editable');
  assert.deepEqual(validateCVarEdits({ 'Codex.Patrol.SearchSeconds': '-1', 'Codex.Perception.FovScale': '' }), []);
});

test('horde #12: the horde tab edits the keys the game reads (horde.json firefight_*, Military Zombie, no Spitter)', () => {
  const shipped = JSON.parse(fs.readFileSync(path.join(AI_DIR, 'horde.json'), 'utf-8'));
  // Every horde.json key has a built-in value (the game's FHordeConfig) and every firefight key an editor field.
  for (const key of Object.keys(shipped).filter((k) => k !== 'comment')) {
    assert.ok(key in HORDE_BUILTIN, `${key} has a built-in value`);
  }
  const edited = new Set([...HORDE_NUMBER_FIELDS.map((f) => f.key), ...HORDE_BOOL_FIELDS.map((f) => f.key)]);
  for (const key of Object.keys(shipped).filter((k) => k.startsWith('firefight_'))) {
    assert.ok(edited.has(key), `${key} is editable`);
  }
  assert.deepEqual(validateHorde(shipped), [], 'the shipped horde.json is valid');
  const types = HORDE_ARCHETYPES.map((a) => a.key);
  assert.ok(types.includes('MILITARY_ZOMBIE'), 'the Military Zombie is offered');
  assert.ok(!types.includes('SPITTER'), 'the Spitter is not (the game refuses it in the horde)');
  assert.ok(!Object.keys(shipped.composition).includes('SPITTER'), 'no Spitter in the shipped composition');
  // A firefight value out of range is reported.
  assert.equal(validateHorde({ ...shipped, firefight_slip_exit_factor: 0.5 }).length, 1);
  // 2026-11-09 (game designer): no horde after a long real-time fight - the timer keys are gone from the file, the built-ins and the editor.
  for (const key of ['trigger_seconds', 'repeats', 'repeat_seconds', 'count_increase_per_repeat']) {
    assert.ok(!(key in shipped), `horde.json has no ${key}`);
    assert.ok(!(key in HORDE_BUILTIN), `no built-in ${key}`);
    assert.ok(!edited.has(key), `${key} is not edited`);
  }
  assert.ok(!CVARS.some((c) => c.name === 'Codex.Horde.TriggerSeconds' || c.name === 'Codex.Horde.RepeatSeconds'), 'the clock cvars are gone');
  assert.ok(CVARS.some((c) => c.name === 'Codex.Horde.AfterFirefights'), 'the threshold cvar is listed');
  // Game designer play-test 2026-11-09: the firefight horde 300 m away (+- 40), a new bearing (90 deg), the waves feed it - shipped and editable.
  assert.equal(shipped.firefight_spawn_distance_m, 300);
  assert.equal(shipped.firefight_spawn_spread_m, 40);
  assert.equal(shipped.firefight_bearing_change_deg, 90);
  assert.equal(shipped.waves_feed_horde, true);
  assert.ok(edited.has('waves_feed_horde'), 'waves_feed_horde is editable');
  assert.equal(validateHorde({ ...shipped, firefight_spawn_distance_m: 5 }).length, 1, 'a 5 m horde distance is refused');
});
