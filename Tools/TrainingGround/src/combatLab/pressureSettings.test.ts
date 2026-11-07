// Combat lab part A (level settings + presets): pure-logic tests (node --test; `npm test`).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  PRESSURE_FIELDS, FIELDS_GOLDEN_FILE, applySettings, buildFieldsGolden, diffSettings, eventShares, fieldError, fieldValue, fieldsGoldenText,
  getAt, matchingPreset, pickSettings, presetSettings, setAt, typeShares, typeWeights, upsertPreset, validatePreset, validatePresetFile,
  validatePressure, validateSettings, fieldByPath,
} from './pressureSettings.ts';
import type { CombatLabPresetFile, JsonObject } from './pressureSettings.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const LEVEL = path.join(ROOT, 'Content/Data/LevelJson/L_FormationLabCombat.json');
const PRESETS = path.join(ROOT, 'Content/Data/CombatLab/presets.json');
const GOLDEN = path.join(ROOT, 'Tools/TrainingGround/golden', FIELDS_GOLDEN_FILE);

test('the field table: unique paths, defaults inside their own range, minFrom points at a field', () => {
  const paths = new Set<string>();
  for (const f of PRESSURE_FIELDS) {
    assert.equal(paths.has(f.path), false, `duplicate ${f.path}`);
    paths.add(f.path);
    if (f.kind === 'bool') { assert.equal(typeof f.def, 'boolean', f.path); continue; }
    assert.equal(typeof f.def, 'number', f.path);
    assert.equal(fieldError({}, f), '', `${f.path}: absent key is fine`);
    assert.equal(fieldError(setAt({}, f.path, f.def), f), '', `${f.path}: its default is valid`);
    if (f.minFrom) assert.ok(fieldByPath(f.minFrom.path), `${f.path}: minFrom ${f.minFrom.path}`);
    else assert.notEqual(f.min, undefined, `${f.path}: a min`);
    assert.notEqual(f.max, undefined, `${f.path}: a max (the page's slider)`);
  }
  // The designer's list (2026-11-01) is all there.
  for (const p of ['interval_sec', 'first_event_sec', 'group_min', 'group_max', 'max_alive', 'ambush.coordinated.max_alive', 'patrol_share',
    'ambush.coordinated.share', 'ambush.watch_half_angle_deg', 'ambush.spots.wall_standoff_cm', 'ambush.relocate.after_sec', 'ambush.relocate.far_cm',
    'ambush.coordinated.assault_min_sec', 'ambush.coordinated.assault_max_sec', 'seed']) assert.ok(paths.has(p), p);
});

test('validation rejects what the C++ parser would clamp (min / max / integer / cross-field)', () => {
  assert.deepEqual(validatePressure({}), []);
  assert.equal(validatePressure({ interval_sec: 4 }).length, 1, 'interval < 5 (C++ Max(5))');
  assert.equal(validatePressure({ interval_sec: 5 }).length, 0);
  assert.equal(validatePressure({ group_min: 1.5 }).length, 1, 'integer');
  assert.equal(validatePressure({ group_min: 3, group_max: 2 }).length, 1, 'group_max >= group_min');
  assert.equal(validatePressure({ group_min: 3, group_max: 3 }).length, 0);
  assert.equal(validatePressure({ patrol_share: 1.2 }).length, 1);
  assert.equal(validatePressure({ ambush: { coordinated: { bodies_min: 3 } } }).length, 1, 'bodies >= 4');
  assert.equal(validatePressure({ ambush: { coordinated: { assault_min_sec: 3, assault_max_sec: 2 } } }).length, 1);
  assert.equal(validatePressure({ ambush: { min_squad_distance_cm: 2000, max_squad_distance_cm: 2050 } }).length, 1, 'max >= min + 100');
  assert.equal(validatePressure({ ambush: { spots: { wall_standoff_cm: 40 } } }).length, 1);
  assert.equal(validatePressure({ ambush: { watch_half_angle_deg: 200 } }).length, 1);
  assert.equal(validatePressure({ ambush: 'x' }).length, 1, 'a sub-block must be an object');
  assert.equal(validatePressure({ enabled: 'yes' }).length, 1);
  assert.equal(validatePressure({ seed: -1 }).length, 1);
  assert.equal(validatePressure([]).length, 1);
});

test('types: the five only (FROST_HOUND alias, MILITARY_ZOMBIE), never SPITTER / BRUTE / CRYO_DRONE, weights 0..100', () => {
  assert.deepEqual(validatePressure({ types: { HOUND: 1, frostbitten: 2, military_zombie: 0, CUTTER: 1, MARKSMAN: 0.5 } }), []);
  assert.match(validatePressure({ types: { SPITTER: 1 } })[0], /SPITTER/, 'task #34: the Military Zombie replaced the Spitter');
  assert.match(validatePressure({ types: { BRUTE: 1 } })[0], /BRUTE/);
  assert.match(validatePressure({ types: { CRYO_DRONE: 1 } })[0], /CRYO_DRONE/);
  assert.match(validatePressure({ types: { DRAGON: 1 } })[0], /неизвестный/);
  assert.equal(validatePressure({ types: { HOUND: 1, FROST_HOUND: 1 } }).length, 1, 'the same type twice');
  assert.equal(validatePressure({ types: { HOUND: -1 } }).length, 1);
  assert.equal(validatePressure({ types: { HOUND: 'a' } }).length, 1);
  assert.deepEqual(typeWeights({}), { HOUND: 1, FROSTBITTEN: 1, MILITARY_ZOMBIE: 1, CUTTER: 1, MARKSMAN: 1 }, 'absent = equal');
  assert.deepEqual(typeWeights({ types: { HOUND: 0, CUTTER: 0 } }), { HOUND: 1, FROSTBITTEN: 1, MILITARY_ZOMBIE: 1, CUTTER: 1, MARKSMAN: 1 }, 'all zero = equal');
  const shares = typeShares({ types: { HOUND: 3, FROST_HOUND: 0, CUTTER: 1 } });
  assert.equal(shares.HOUND, 0.75);
  assert.equal(shares.CUTTER, 0.25);
  assert.equal(shares.MILITARY_ZOMBIE, 0);
});

test('ambush points: [[x, y], ...]', () => {
  assert.deepEqual(validatePressure({ ambush: { points: [[1, 2], [-3.5, 4]] } }), []);
  assert.equal(validatePressure({ ambush: { points: [[1]] } }).length, 1);
  assert.equal(validatePressure({ ambush: { points: [[1, 'a']] } }).length, 1);
  assert.equal(validatePressure({ ambush: { points: {} } }).length, 1);
});

test('event shares: patrol, single ambush, coordinated plan', () => {
  const s = eventShares({ patrol_share: 0.6, ambush: { coordinated: { share: 0.35 } } });
  assert.equal(s.patrol, 0.6);
  assert.ok(Math.abs(s.ambush - 0.26) < 1e-9);
  assert.ok(Math.abs(s.coordinated - 0.14) < 1e-9);
  assert.ok(Math.abs(s.patrol + s.ambush + s.coordinated - 1) < 1e-9);
  assert.deepEqual(eventShares({ patrol_share: 0, ambush: { coordinated: { share: 1 } } }), { patrol: 0, ambush: 0, coordinated: 1 });
});

test('get / set / pick / apply keep every other key and never mutate', () => {
  const level = { level_id: 'x', waves: [1], cheats: { immortal: true, extra: 7 }, pressure: { seed: 5, ambush: { points: [[1, 2]] } } };
  const settings = pickSettings(level);
  assert.deepEqual(settings.cheats, { immortal: true, infinite_ammo: false });
  const edited = setAt(settings.pressure, 'ambush.coordinated.share', 0.5);
  assert.equal(getAt(edited, 'ambush.coordinated.share'), 0.5);
  assert.deepEqual(getAt(edited, 'ambush.points'), [[1, 2]]);
  assert.equal(getAt(settings.pressure, 'ambush.coordinated'), undefined, 'input untouched');
  assert.equal(fieldValue(edited, fieldByPath('interval_sec')!), 120, 'absent = the C++ default');
  const applied = applySettings(level, { pressure: edited, cheats: { immortal: false, infinite_ammo: true } }) as typeof level & { cheats: JsonObject };
  assert.deepEqual(applied.waves, [1]);
  assert.equal(applied.cheats.extra, 7, 'unknown cheat keys kept');
  assert.equal(applied.cheats.immortal, false);
  assert.equal(level.cheats.immortal, true, 'input untouched');
  const diff = diffSettings(settings, { pressure: edited, cheats: { immortal: false, infinite_ammo: true } });
  assert.deepEqual(diff.map((d) => d.path), ['pressure.ambush.coordinated.share', 'cheats.immortal', 'cheats.infinite_ammo']);
  assert.equal(diff[0].from, undefined);
  assert.equal(diff[0].to, 0.5);
  assert.deepEqual(setAt({ a: 1 }, 'a', undefined), {}, 'undefined removes');
  assert.deepEqual(validateSettings({ pressure: {}, cheats: { immortal: 'y' } }).length, 1);
});

test('L_FormationLabCombat.json: its blocks are valid', () => {
  const level = JSON.parse(fs.readFileSync(LEVEL, 'utf8'));
  const settings = pickSettings(level);
  assert.deepEqual(validateSettings(settings), []);
  assert.equal(settings.cheats.immortal, true);
  assert.equal(fieldValue(settings.pressure, fieldByPath('first_event_sec')!), 30);
});

test('presets.json: valid, the designer\'s four, "Default" = the level as shipped', () => {
  const file = JSON.parse(fs.readFileSync(PRESETS, 'utf8')) as CombatLabPresetFile;
  assert.deepEqual(validatePresetFile(file), []);
  const ids = file.presets.map((p) => p.id);
  for (const id of ['default', 'patrols_only', 'ambush_heavy', 'coordinated_only']) assert.ok(ids.includes(id), id);
  const level = JSON.parse(fs.readFileSync(LEVEL, 'utf8'));
  assert.equal(matchingPreset(file, pickSettings(level)), 'default');
  const patrols = presetSettings(file.presets.find((p) => p.id === 'patrols_only')!);
  assert.equal(eventShares(patrols.pressure).patrol, 1);
  const coordinated = presetSettings(file.presets.find((p) => p.id === 'coordinated_only')!);
  assert.equal(eventShares(coordinated.pressure).coordinated, 1);
  // Copies: editing the applied settings leaves the preset alone.
  const copy = presetSettings(file.presets[0]);
  (copy.pressure as JsonObject).seed = 9;
  assert.notEqual(file.presets[0].pressure.seed, 9);
  // Upsert replaces in place, appends new ones.
  const replaced = upsertPreset(file, { ...file.presets[1], name: 'Renamed' });
  assert.equal(replaced.presets[1].name, 'Renamed');
  assert.equal(replaced.presets.length, file.presets.length);
  assert.equal(upsertPreset(file, { ...file.presets[0], id: 'mine', name: 'Mine' }).presets.length, file.presets.length + 1);
  assert.ok(validatePreset({ ...file.presets[0], id: 'Bad Id' }).length > 0);
  assert.ok(validatePresetFile({ ...file, presets: [file.presets[0], file.presets[0]] }).some((e) => /повторяется/.test(e)));
});

test('the C++ golden (npm run golden) is current', () => {
  assert.equal(fs.readFileSync(GOLDEN, 'utf8').replace(/\r\n/g, '\n'), fieldsGoldenText());
  const golden = buildFieldsGolden() as { fields: { path: string; cpp_clamp: string }[] };
  assert.equal(golden.fields.length, PRESSURE_FIELDS.length);
});
