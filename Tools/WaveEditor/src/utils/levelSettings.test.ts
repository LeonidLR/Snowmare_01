// Wave Editor level settings (blast door + demo): pure-logic tests (node --test; `npm test`).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  GATE_DEFAULTS, getDemo, getGate, isGateFieldInvalid, setDemoEnabled, setDemoKey, setGateKey, validateDemo, validateGate,
  validateLevelSettings, getColdRateMult, setColdRateMult, validateColdRate,
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
