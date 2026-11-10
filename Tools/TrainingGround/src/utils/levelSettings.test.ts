// Wave Editor level settings (blast door + demo): pure-logic tests (node --test; `npm test`).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  GATE_DEFAULTS, getDemo, getGate, isGateFieldInvalid, setDemoEnabled, setDemoKey, setGateKey, validateDemo, validateGate,
  validateLevelSettings, getColdRateMult, setColdRateMult, validateColdRate,
  HORDE_AFTER_FIREFIGHTS_DEFAULT, HORDE_AFTER_FIREFIGHTS_MIN, getHordeAfterFirefights, getHordeEnabled, setHordeAfterFirefights, setHordeEnabled, validateHordeAfterFirefights,
  HORDE_SPAWN_DISTANCE_DEFAULT, activeWaves, describeHordeWaves, getHordeSpawnDistance, getHordeWaveFeed, hasOwnHordeSpawnDistance, setHordeSpawnDistance,
  validateHordeSpawnDistance, waveForHorde,
  getEnvironment, setEnvironmentKey, validateEnvironment,
  getLoadouts, setOperativeLoadout, validateLoadouts,
  getKit, setOperativeKit, getKitOverrideCarry, setKitOverrideCarry, validateKit,
} from './levelSettings.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const OUTPOST = path.join(ROOT, 'Content/Data/LevelJson/level_01_outpost.json');
const FROZEN_LAKE = path.join(ROOT, 'Content/Data/LevelJson/L_Demo01_A_FrozenLake.json');

test('cold_rate_mult: absent = 1, set / remove keeps the other keys, 1 is written as an absent key', () => {
  assert.equal(getColdRateMult({}), 1);
  const level = { level_id: 'x', demo: { tutorial: true }, waves: [] };
  const less = setColdRateMult(level, 0.8);
  assert.equal(less.cold_rate_mult, 0.8);
  assert.deepEqual(less.demo, level.demo);
  assert.equal('cold_rate_mult' in level, false, 'input not mutated');
  assert.equal('cold_rate_mult' in setColdRateMult(less, 1), false);
  assert.equal('cold_rate_mult' in setColdRateMult(less, undefined), false);
  assert.deepEqual(validateColdRate(0.8), []);
  assert.deepEqual(validateColdRate(0), []);
  assert.equal(validateColdRate(-0.1).length, 1);
  assert.equal(validateColdRate(NaN).length, 1);
  assert.equal(validateColdRate(3.5).length, 1);
  assert.equal(validateLevelSettings({ waves: [], cold_rate_mult: -1 }).length, 1);
});

test('L_Demo01_A_FrozenLake.json: less cold (cold_rate_mult 0.4, user requests 2026-10-13 / 2026-10-14 rev 5g)', () => {
  const level = JSON.parse(fs.readFileSync(FROZEN_LAKE, 'utf-8'));
  assert.equal(getColdRateMult(level), 0.4);
  assert.deepEqual(validateLevelSettings(level), []);
});

test('gate: absent keys read as the game defaults', () => {
  assert.deepEqual(getGate({}), GATE_DEFAULTS);
  assert.deepEqual(getGate({ gate_open_mode: 'instant', gate_progress_per_second: 0.01 }).gate_open_mode, 'instant');
});

test('gate: validation (progress 0..1, per second > 0, final wave within the level)', () => {
  const ok = getGate({});
  assert.deepEqual(validateGate(ok, 3), []);
  assert.equal(validateGate({ ...ok, gate_progress_per_second: 0 }, 3).length, 1);
  assert.equal(validateGate({ ...ok, gate_progress_per_second: -1 }, 3).length, 1);
  assert.equal(validateGate({ ...ok, gate_max_progress_before_final: 1.2 }, 3).length, 1);
  assert.equal(validateGate({ ...ok, gate_max_progress_before_final: -0.1 }, 3).length, 1);
  assert.equal(validateGate({ ...ok, gate_full_open_after_wave: 4 }, 3).length, 1);
  assert.equal(validateGate({ ...ok, gate_full_open_after_wave: 1.5 }, 3).length, 1);
  assert.equal(validateGate({ ...ok, gate_progress_per_second: NaN }, 3).length, 1);
  assert.deepEqual(validateGate({ ...ok, gate_open_mode: 'instant', gate_progress_per_second: 0 }, 3), [], 'instant ignores the pacing fields');
  assert.equal(isGateFieldInvalid('gate_progress_per_second', { ...ok, gate_progress_per_second: 0 }, 3), true);
  assert.equal(isGateFieldInvalid('gate_progress_per_second', ok, 3), false);
});

test('gate: setGateKey keeps the other keys and the demo object, undefined removes the key', () => {
  const level = { level_id: 'x', demo: { tutorial: true, future_key: 7 }, gate_progress_per_second: 0.004, waves: [] };
  const next = setGateKey(level, 'gate_progress_per_second', 0.01);
  assert.equal(next.gate_progress_per_second, 0.01);
  assert.deepEqual(next.demo, level.demo);
  assert.equal(level.gate_progress_per_second, 0.004, 'input not mutated');
  assert.equal('gate_progress_per_second' in setGateKey(level, 'gate_progress_per_second', undefined), false);
});

test('demo: no object = every flag off; a bare object reads the game defaults (intro briefing off)', () => {
  const none = getDemo({});
  assert.equal(none.enabled, false);
  assert.equal(none.intro_dialogue, true);
  const bare = getDemo({ demo: {} });
  assert.deepEqual(bare, {
    enabled: true, panic: true, vision_cones: 'default', tutorial: false, tutorial_steps: 'demo_01_steps.json', hud: 'default', intro_dialogue: false,
  });
  const full = getDemo({ demo: { panic: false, vision_cones: false, tutorial: true, tutorial_steps: 'a.json', hud: 'demo', intro_dialogue: true } });
  assert.deepEqual(full, { enabled: true, panic: false, vision_cones: 'off', tutorial: true, tutorial_steps: 'a.json', hud: 'demo', intro_dialogue: true });
});

test('demo: enable / disable, set keys, unknown keys survive, no object = no-op', () => {
  const level = { level_id: 'x', waves: [] };
  assert.equal(setDemoKey(level, 'panic', false), level);
  const on = setDemoEnabled(level, true);
  assert.deepEqual(on.demo, { panic: true, tutorial: false, intro_dialogue: false });
  const edited = setDemoKey({ ...on, demo: { ...on.demo, future_key: 'keep' } }, 'vision_cones', true);
  assert.equal(edited.demo.vision_cones, true);
  assert.equal(edited.demo.future_key, 'keep');
  assert.equal('vision_cones' in setDemoKey(edited, 'vision_cones', undefined).demo, false);
  assert.equal('demo' in setDemoEnabled(edited, false), false);
  // Re-enabling an existing object does not replace it.
  assert.equal(setDemoEnabled(edited, true).demo, edited.demo);
});

test('demo: tutorial_steps must be a plain *.json file name', () => {
  const base = getDemo({ demo: {} });
  assert.deepEqual(validateDemo(base), []);
  for (const bad of ['steps.txt', '../x.json', 'a/b.json', 'a' + String.fromCharCode(92) + 'b.json']) {
    assert.equal(validateDemo({ ...base, tutorial_steps: bad }).length, 1, bad);
  }
  assert.deepEqual(validateDemo({ ...getDemo({}), tutorial_steps: 'bad' }), [], 'disabled demo is not validated');
});

test('level_01_outpost.json: gate values read, validate, and an edit round-trips without touching other keys', () => {
  const text = fs.readFileSync(OUTPOST, 'utf-8');
  const level = JSON.parse(text);
  const gate = getGate(level);
  assert.deepEqual(validateLevelSettings(level), []);
  // Unchanged settings: re-saving through the editor keeps every key and value (the file has no demo object).
  const same = setGateKey(level, 'gate_open_mode', gate.gate_open_mode);
  assert.deepEqual(same, level);
  assert.equal('demo' in same, false);
  const edited = setGateKey(level, 'gate_max_progress_before_final', 0.8);
  assert.equal(JSON.parse(JSON.stringify(edited)).gate_max_progress_before_final, 0.8);
  assert.deepEqual(Object.keys(edited), Object.keys(level));
  // Demo object added then removed leaves the level as loaded.
  assert.deepEqual(setDemoEnabled(setDemoEnabled(level, true), false), level);
});


test('horde_after_firefights: default 10, min 1, set / validate / roundtrip', () => {
  assert.equal(HORDE_AFTER_FIREFIGHTS_DEFAULT, 10, 'game designer 2026-11-09');
  assert.equal(getHordeAfterFirefights({}), HORDE_AFTER_FIREFIGHTS_DEFAULT);
  assert.equal(getHordeAfterFirefights({ horde_after_firefights: 10 }), 10);
  assert.equal(getHordeEnabled({}), true);
  assert.equal(getHordeEnabled({ horde_enabled: false }), false);

  assert.deepEqual(validateHordeAfterFirefights(6), []);
  assert.deepEqual(validateHordeAfterFirefights(1), []);
  assert.equal(validateHordeAfterFirefights(0).length, 1);
  assert.equal(validateHordeAfterFirefights(-2).length, 1);
  assert.equal(validateHordeAfterFirefights(2.5).length, 1);
  assert.equal(validateHordeAfterFirefights(NaN).length, 1);

  const level = { level_id: 'x', waves: [] };
  const updated = setHordeAfterFirefights(level, 8);
  assert.equal(updated.horde_after_firefights, 8);
  assert.equal('horde_after_firefights' in level, false, 'input not mutated');

  const removed = setHordeAfterFirefights(updated, undefined);
  assert.equal('horde_after_firefights' in removed, false);

  assert.equal(validateLevelSettings({ waves: [], horde_after_firefights: -1 }).length, 1);
  assert.equal(validateLevelSettings({ waves: [], horde_after_firefights: 8 }).length, 0);
});

test('horde from waves (game designer 2026-11-09): which levels feed it, horde N = wave N then the last, SPITTER -> MILITARY_ZOMBIE', () => {
  const depot = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelJson/L_Gen_Depot_1.json'), 'utf-8'));
  assert.equal(getHordeWaveFeed(depot), 'waves', 'the Depot (combat_start ambush) feeds the horde from its waves');
  const lake = JSON.parse(fs.readFileSync(FROZEN_LAKE, 'utf-8'));
  assert.equal(getHordeWaveFeed(lake), 'scripted', 'the Lake (button) plays its waves as scripted fights');
  assert.equal(getHordeWaveFeed({ waves: [{ spawns: [] }] }), 'map', 'auto: depends on the map');
  assert.equal(getHordeWaveFeed({ combat_start: 'ambush', waves: [] }), 'none');
  assert.equal(getHordeWaveFeed({ combat_start: 'ambush', waves: [{ is_active: false }] }), 'none', 'inactive waves do not count');
  assert.equal(getHordeWaveFeed({ combat_start: 'ambush', waves: [{}], horde: { waves_feed_horde: false } }), 'off');
  // The game's HordeRules::WaveForHorde.
  assert.deepEqual([1, 2, 3, 4, 7].map((n) => waveForHorde(n, 3)), [0, 1, 2, 2, 2]);
  assert.equal(waveForHorde(1, 0), -1);
  // The designer's Training Ground waves (2026-11-09).
  const level = {
    combat_start: 'ambush',
    waves: [
      { wave_index: 1, max_simultaneous_enemies: 2, spawns: [{ enemy_type: 'FROSTBITTEN', count: 4 }, { enemy_type: 'HOUND', count: 30 }] },
      { wave_index: 2, is_active: false, spawns: [{ enemy_type: 'BRUTE', count: 9 }] },
      { wave_index: 3, max_simultaneous_enemies: 12, spawns: [{ enemy_type: 'HOUND', count: 55 }, { enemy_type: 'SPITTER', count: 2 }] },
    ],
  };
  assert.equal(activeWaves(level).length, 2);
  const lines = describeHordeWaves(level);
  assert.equal(lines.length, 2);
  assert.match(lines[0], /^Орда 1 ← волна 1: 4 FROSTBITTEN \+ 30 HOUND, до 2 одновременно$/);
  assert.match(lines[1], /^Орда 2 \(и все следующие орды\) ← волна 3: 55 HOUND \+ 2 MILITARY_ZOMBIE, до 12 одновременно; SPITTER → MILITARY_ZOMBIE$/);
});

test('horde distance (game designer 2026-11-09): default 300 m from horde.json, a level override in "horde", removed when emptied', () => {
  assert.equal(getHordeSpawnDistance({}), HORDE_SPAWN_DISTANCE_DEFAULT);
  assert.equal(HORDE_SPAWN_DISTANCE_DEFAULT, 300);
  const level = { level_id: 'x', waves: [], horde: { count: 12 } };
  const far = setHordeSpawnDistance(level, 400);
  assert.equal(getHordeSpawnDistance(far), 400);
  assert.ok(hasOwnHordeSpawnDistance(far));
  assert.deepEqual(far.horde, { count: 12, firefight_spawn_distance_m: 400 }, 'other horde keys kept');
  assert.equal('firefight_spawn_distance_m' in level.horde, false, 'input not mutated');
  const back = setHordeSpawnDistance(far, undefined);
  assert.deepEqual(back.horde, { count: 12 });
  const plain = setHordeSpawnDistance(setHordeSpawnDistance({ waves: [] }, 250), undefined);
  assert.equal('horde' in plain, false, 'an emptied horde object is removed');
  assert.deepEqual(validateHordeSpawnDistance(300), []);
  assert.equal(validateHordeSpawnDistance(10).length, 1);
  assert.equal(validateHordeSpawnDistance(NaN).length, 1);
  assert.equal(validateLevelSettings({ waves: [], horde: { firefight_spawn_distance_m: 5 } }).length, 1);
  assert.equal(validateLevelSettings({ waves: [], horde: { firefight_spawn_distance_m: 300 } }).length, 0);
});

test('environment: get, set, override_carry, climate/weather validation, remove when empty', () => {
  assert.deepEqual(getEnvironment({}), {});
  const level = { level_id: 'test_lvl', custom_unknown_field: 'preserved_data' };

  // Set locked and override_carry
  let next = setEnvironmentKey(level, 'locked', true);
  assert.equal(next.environment.locked, true);
  assert.equal(next.custom_unknown_field, 'preserved_data');

  next = setEnvironmentKey(next, 'override_carry', true);
  assert.equal(next.environment.override_carry, true);

  next = setEnvironmentKey(next, 'start_hour', 22.5);
  next = setEnvironmentKey(next, 'weather', 'Blizzard');
  assert.equal(next.environment.start_hour, 22.5);
  assert.equal(next.environment.weather, 'Blizzard');

  // Validation
  assert.deepEqual(validateEnvironment(getEnvironment(next)), []);
  assert.equal(validateEnvironment({ start_hour: 25 }).length, 1);
  assert.equal(validateEnvironment({ start_hour: -1 }).length, 1);
  assert.equal(validateEnvironment({ climate: 'arctic', weather: 'Rain' as any }).length, 1, 'Rain requires temperate climate');
  assert.deepEqual(validateEnvironment({ climate: 'temperate', weather: 'Rain' }), []);
  assert.equal(validateEnvironment({ snow_cover: 1.5 }).length, 1);
  assert.equal(validateEnvironment({ exposure_bias: 5 }).length, 1);

  // False override_carry is omitted from JSON
  next = setEnvironmentKey(next, 'override_carry', false);
  assert.equal('override_carry' in next.environment, false);

  // Clearing all keys removes environment object
  next = setEnvironmentKey(next, 'locked', false);
  next = setEnvironmentKey(next, 'start_hour', undefined);
  next = setEnvironmentKey(next, 'weather', undefined);
  assert.equal('environment' in next, false, 'emptied environment object is removed');
  assert.equal(next.custom_unknown_field, 'preserved_data');
});

test('loadouts: get, set, reorder, empty list removes key, validate, unknown keys preserved', () => {
  assert.deepEqual(getLoadouts({}), {});
  const level = { level_id: 'test_lvl', preserve_me: 42 };

  // Set loadout for commander
  let next = setOperativeLoadout(level, 'commander', ['m16', 'pistol']);
  assert.deepEqual(getLoadouts(next), { commander: ['m16', 'pistol'] });
  assert.equal(next.preserve_me, 42);

  // Add engineer
  next = setOperativeLoadout(next, 'engineer', ['shotgun', 'pistol']);
  assert.deepEqual(getLoadouts(next).engineer, ['shotgun', 'pistol']);

  // Case normalization
  next = setOperativeLoadout(next, 'Commander', ['M16', 'PISTOL', 'GRENADE']);
  assert.deepEqual(getLoadouts(next).commander, ['m16', 'pistol', 'grenade']);

  // Empty list removes operative key
  next = setOperativeLoadout(next, 'commander', []);
  assert.equal('commander' in getLoadouts(next), false);
  assert.deepEqual(getLoadouts(next).engineer, ['shotgun', 'pistol']);

  // Removing last operative removes loadouts object
  next = setOperativeLoadout(next, 'engineer', undefined);
  assert.equal('loadouts' in next, false);
  assert.equal(next.preserve_me, 42);

  // Validation
  assert.deepEqual(validateLoadouts(undefined), []);
  assert.deepEqual(validateLoadouts({ commander: ['m16', 'pistol'] }), []);
  assert.equal(validateLoadouts({ commander: 'm16' as any }).length, 1);
});

test('kit: get, set, partial fields, ammo, kit_override_carry, validate', () => {
  assert.deepEqual(getKit({}), {});
  assert.equal(getKitOverrideCarry({}), false);
  const level = { level_id: 'test_lvl', keep_this: true };

  // Set kit for commander
  let next = setOperativeKit(level, 'commander', {
    grenades: 2,
    medkits: 1,
    heat_packs: 2,
    ammo: { m16: 90, pistol: 24 },
  });
  assert.deepEqual(getKit(next).commander, {
    grenades: 2,
    medkits: 1,
    heat_packs: 2,
    ammo: { m16: 90, pistol: 24 },
  });
  assert.equal(next.keep_this, true);

  // kit_override_carry toggle
  next = setKitOverrideCarry(next, true);
  assert.equal(getKitOverrideCarry(next), true);
  next = setKitOverrideCarry(next, false);
  assert.equal(getKitOverrideCarry(next), false);
  assert.equal('kit_override_carry' in next, false);

  // Partial fields: missing fields are omitted
  next = setOperativeKit(next, 'engineer', { grenades: 3 });
  assert.deepEqual(getKit(next).engineer, { grenades: 3 });
  assert.equal('medkits' in getKit(next).engineer, false);

  // Validation
  assert.deepEqual(validateKit(getKit(next)), []);
  assert.equal(validateKit({ commander: { grenades: -1 } }).length, 1);
  assert.equal(validateKit({ commander: { ammo: { m16: -5 } } }).length, 1);

  // Empty kit removes key
  next = setOperativeKit(next, 'commander', undefined);
  assert.equal('commander' in getKit(next), false);
  next = setOperativeKit(next, 'engineer', {});
  assert.equal('kit' in next, false);
});

test('level JSON round-trip: environment + loadouts + kit edit preserves all other keys', () => {
  // Read a real level as template
  const originalJson = fs.readFileSync(FROZEN_LAKE, 'utf-8');
  const level = JSON.parse(originalJson);

  // Add extra unknown custom keys to simulate external / designer properties
  level.designer_notes = 'Authored by lead level designer';
  level.custom_patrol_v2 = { route_id: 101, waypoints: [1, 2, 3] };

  // Apply environment, loadouts, kit
  let updated = setEnvironmentKey(level, 'override_carry', true);
  updated = setEnvironmentKey(updated, 'start_hour', 21.0);
  updated = setOperativeLoadout(updated, 'commander', ['m16', 'pistol', 'shotgun']);
  updated = setOperativeKit(updated, 'commander', { grenades: 4, ammo: { m16: 150 } });
  updated = setKitOverrideCarry(updated, true);

  // Validate entire level settings
  const errors = validateLevelSettings(updated);
  assert.deepEqual(errors, []);

  // Check new fields
  assert.equal(updated.environment.override_carry, true);
  assert.equal(updated.environment.start_hour, 21.0);
  assert.deepEqual(updated.loadouts.commander, ['m16', 'pistol', 'shotgun']);
  assert.deepEqual(updated.kit.commander, { grenades: 4, ammo: { m16: 150 } });
  assert.equal(updated.kit_override_carry, true);

  // Check preserved fields
  assert.equal(updated.level_id, 'L_Demo01_A_FrozenLake');
  assert.equal(updated.level_name, '0.0 Frozen Dam');
  assert.equal(updated.designer_notes, 'Authored by lead level designer');
  assert.deepEqual(updated.custom_patrol_v2, { route_id: 101, waypoints: [1, 2, 3] });
  assert.equal(updated.cold_rate_mult, 0.4);
  assert.deepEqual(updated.waves, level.waves);
});
