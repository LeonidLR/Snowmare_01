// Training Ground: tests for codex.hud_labels/1 schema, groupings, group toggles and non-destructive text patching.
// Docs: Step 3 of docs/port/gemini/inbox/010-night-queue.md.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  HUD_LABELS_SCHEMA,
  HUD_GROUPS,
  type HudLabelsFile,
  validateHudLabels,
  setGroupShow,
  setAllShow,
  getGroupCounts,
  getTotalCounts,
  patchHudLabelsText,
} from './hudLabels.ts';
import { writeAtomic } from '../../server/hudLabelsApi.ts';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const REPO_HUD_LABELS_PATH = path.resolve(__dirname, '../../../../Content/Data/UI/hud_labels.json');

test('hud_labels: repo file exists, validates, and has all 26 switches covered by the 4 groups', () => {
  assert.ok(fs.existsSync(REPO_HUD_LABELS_PATH), `hud_labels.json must exist at ${REPO_HUD_LABELS_PATH}`);
  const rawText = fs.readFileSync(REPO_HUD_LABELS_PATH, 'utf8');
  const data: HudLabelsFile = JSON.parse(rawText);

  const errors = validateHudLabels(data);
  assert.deepEqual(errors, [], 'repo file must validate cleanly');
  assert.equal(data.schema, HUD_LABELS_SCHEMA);

  const labelKeys = Object.keys(data.labels);
  assert.equal(labelKeys.length, 26, 'must contain exactly 26 HUD label switches');

  // Verify all 26 keys are partitioned into HUD_GROUPS without overlap or omission
  const groupedKeys = HUD_GROUPS.flatMap((g) => g.keys);
  assert.equal(groupedKeys.length, 26, 'grouped keys must total 26');

  const uniqueGrouped = new Set(groupedKeys);
  assert.equal(uniqueGrouped.size, 26, 'no duplicate keys across groups');

  for (const k of labelKeys) {
    assert.ok(uniqueGrouped.has(k), `label switch "${k}" must be categorized in a group`);
  }

  // Check counts per group
  assert.equal(getGroupCounts(data, 'operatives').total, 11);
  assert.equal(getGroupCounts(data, 'enemies').total, 5);
  assert.equal(getGroupCounts(data, 'world').total, 6);
  assert.equal(getGroupCounts(data, 'hud').total, 4);

  const total = getTotalCounts(data);
  assert.equal(total.total, 26);
  assert.equal(total.on, 26);
  assert.equal(total.off, 0);
});

test('hud_labels: round-trip on unchanged data produces byte-identical text', () => {
  const rawText = fs.readFileSync(REPO_HUD_LABELS_PATH, 'utf8');
  const data: HudLabelsFile = JSON.parse(rawText);

  const patched = patchHudLabelsText(rawText, data);
  assert.equal(patched, rawText, 'patching with unchanged data must be 100% byte-identical');
});

test('hud_labels: unknown root keys and unknown label fields are kept during patching', () => {
  const rawText = fs.readFileSync(REPO_HUD_LABELS_PATH, 'utf8');
  const data: HudLabelsFile = JSON.parse(rawText);

  // Inject unknown root key and unknown property inside a label
  data['x_custom_note'] = 'custom root metadata that must be preserved';
  data.labels['operative_name']['x_extra_tag'] = 'extra_123';

  // Also toggle a known switch
  data.labels['operative_name'].show = false;

  const patched = patchHudLabelsText(rawText, data);
  const parsedBack: HudLabelsFile = JSON.parse(patched);

  assert.equal(parsedBack['x_custom_note'], 'custom root metadata that must be preserved');
  assert.equal(parsedBack.labels['operative_name']['x_extra_tag'], 'extra_123');
  assert.equal(parsedBack.labels['operative_name'].show, false);

  // Verify other labels remain untouched
  assert.equal(parsedBack.labels['operative_stance'].show, true);
  assert.equal(parsedBack.comment, data.comment, 'comment field must be preserved');
});

test('hud_labels: group toggles (all on / all off) modify only the target group', () => {
  const rawText = fs.readFileSync(REPO_HUD_LABELS_PATH, 'utf8');
  const data: HudLabelsFile = JSON.parse(rawText);

  // 1. Toggle 'enemies' group off
  const enemiesOff = setGroupShow(data, 'enemies', false);
  const enemiesCounts = getGroupCounts(enemiesOff, 'enemies');
  assert.equal(enemiesCounts.on, 0);
  assert.equal(enemiesCounts.off, 5);

  // Other groups must still be completely on
  assert.equal(getGroupCounts(enemiesOff, 'operatives').on, 11);
  assert.equal(getGroupCounts(enemiesOff, 'world').on, 6);
  assert.equal(getGroupCounts(enemiesOff, 'hud').on, 4);

  // 2. Toggle 'enemies' group back on
  const enemiesOn = setGroupShow(enemiesOff, 'enemies', true);
  assert.equal(getGroupCounts(enemiesOn, 'enemies').on, 5);
  assert.equal(getGroupCounts(enemiesOn, 'enemies').off, 0);

  // 3. Toggle 'world' group off
  const worldOff = setGroupShow(data, 'world', false);
  assert.equal(getGroupCounts(worldOff, 'world').on, 0);
  assert.equal(getGroupCounts(worldOff, 'world').off, 6);
  assert.equal(getGroupCounts(worldOff, 'operatives').on, 11);

  // 4. Toggle all off
  const allOff = setAllShow(data, false);
  const allOffCounts = getTotalCounts(allOff);
  assert.equal(allOffCounts.on, 0);
  assert.equal(allOffCounts.off, 26);

  // 5. Toggle all on
  const allOn = setAllShow(allOff, true);
  const allOnCounts = getTotalCounts(allOn);
  assert.equal(allOnCounts.on, 26);
  assert.equal(allOnCounts.off, 0);

  // Verify that patching allOff preserves comments and structure
  const patchedAllOff = patchHudLabelsText(rawText, allOff);
  const parsedAllOff = JSON.parse(patchedAllOff);
  assert.equal(parsedAllOff.comment, data.comment);
  for (const item of Object.values(parsedAllOff.labels as Record<string, { show: boolean }>)) {
    assert.equal(item.show, false);
  }
});

test('hud_labels: validator rejects malformed files', () => {
  assert.ok(validateHudLabels(null).length > 0);
  assert.ok(validateHudLabels({ schema: 'other/1', labels: {} }).some((e) => e.includes('schema')));
  assert.ok(validateHudLabels({ schema: HUD_LABELS_SCHEMA }).some((e) => e.includes('labels must be an object')));

  // Bad label entry
  const badEntry = {
    schema: HUD_LABELS_SCHEMA,
    labels: {
      bad: { show: 'not_bool', label: '', doc: 123 },
    },
  };
  const errors = validateHudLabels(badEntry);
  assert.ok(errors.some((e) => e.includes('show must be a boolean')));
  assert.ok(errors.some((e) => e.includes('label must be a non-empty string')));
  assert.ok(errors.some((e) => e.includes('doc must be a non-empty string')));
});

test('hud_labels: atomic write writes cleanly without leaving temporary files', () => {
  const tempTestPath = path.resolve(__dirname, `_temp_hud_labels_test_${process.pid}.json`);
  try {
    const payload = '{"test": true}\n';
    writeAtomic(tempTestPath, payload);
    assert.ok(fs.existsSync(tempTestPath), 'target file must exist');
    assert.equal(fs.readFileSync(tempTestPath, 'utf8'), payload);

    // Verify no lingering .tmp files in directory
    const dir = path.dirname(tempTestPath);
    const lingering = fs.readdirSync(dir).filter((f) => f.includes(`_temp_hud_labels_test_${process.pid}.json.tmp.`));
    assert.equal(lingering.length, 0, 'no temporary files must remain after atomic write');
  } finally {
    if (fs.existsSync(tempTestPath)) {
      fs.unlinkSync(tempTestPath);
    }
  }
});
