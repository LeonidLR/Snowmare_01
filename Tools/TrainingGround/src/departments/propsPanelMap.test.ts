// Task 097: switching the props tab from a map that has a props file to one that does not
// drops the old canvas, shows the Russian hint, and saves into the new map (creating the file).
// A late response for the previous map must not restore its props. Temp map name only — no real level files.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';

import type { LevelPropsFile } from '../levelgen/props/schema.ts';
import { validateProps } from '../levelgen/props/schema.ts';
import { PROPS_DIR, readPropsData, savePropsData } from '../../server/propsApi.ts';
import {
  applyPropsLoad,
  dropPropsForMap,
  missingPropsHint,
  propsLoadId,
  propsSaveRequest,
  type PropsCanvasSnapshot,
} from './propsPanelMap.ts';

const file = (map: string, layers: LevelPropsFile['layers'] = []): LevelPropsFile => ({
  schema: 'codex.levelgen_props/1',
  map,
  layout: `${map}_layout`,
  layout_hash: 'abc',
  revision: 4,
  frozen: false,
  layers,
});

const blank = (): PropsCanvasSnapshot => ({ mapName: null, propsId: null, file: null, loadGen: 0, hint: null });

test('props panel 097: A (file) -> B (no file) drops the canvas and shows the hint; a late A response does not restore it', () => {
  const a = file('MapA', [{ id: 'crates', kind: 'crates', enabled: true, seed: 1, locked: false, rules: {}, objects: [{ id: 'c1', variant: 'crate_l', pos: [10, 20], walk_layer: 'ground' }] }]);
  let snap = dropPropsForMap(blank(), 'MapA', 'MapA');
  snap = applyPropsLoad(snap, snap.loadGen, a);
  assert.equal(snap.file?.map, 'MapA');
  assert.equal(snap.file?.layers[0].objects.length, 1);
  assert.equal(snap.hint, null);

  const genA = snap.loadGen;
  snap = dropPropsForMap(snap, 'MapB', null);
  assert.equal(snap.file, null, 'canvas props must be dropped');
  assert.equal(snap.hint, missingPropsHint('MapB'));
  assert.equal(snap.hint, 'У карты MapB нет файла пропсов - добавьте пропс, чтобы создать его');
  assert.equal(propsLoadId('MapB', null), 'MapB');
  assert.equal(propsLoadId(null, null), '');

  // The GET that was in flight for MapA completes after the switch. It must not come back.
  const stale = applyPropsLoad(snap, genA, a);
  assert.equal(stale.file, null);
  assert.equal(stale.hint, snap.hint);

  // A response that ignored the generation but still carries MapA's document is refused too.
  const wrongMap = applyPropsLoad(snap, snap.loadGen, a);
  assert.equal(wrongMap.file, null);

  const saveB = propsSaveRequest({ mapName: 'MapB', propsId: null, file: null, layoutId: 'lay_b' });
  assert.ok(!('error' in saveB));
  if ('error' in saveB) return;
  assert.equal(saveB.map, 'MapB');
  assert.notEqual(saveB.map, '');
  assert.equal(saveB.body.base_revision, 0);
  assert.deepEqual(saveB.body.layers, []);
  assert.equal(saveB.body.layout, 'lay_b');

  // Back to A: A's own file loads again.
  snap = dropPropsForMap(snap, 'MapA', 'MapA');
  assert.equal(snap.file, null);
  assert.equal(snap.hint, null);
  snap = applyPropsLoad(snap, snap.loadGen, a);
  assert.equal(snap.file?.map, 'MapA');
  assert.equal(snap.file?.layers[0].objects[0].id, 'c1');

  const saveA = propsSaveRequest({ mapName: 'MapA', propsId: 'MapA', file: snap.file, layoutId: 'lay_a' });
  assert.ok(!('error' in saveA));
  if ('error' in saveA) return;
  assert.equal(saveA.map, 'MapA');
  assert.equal(saveA.body.base_revision, 4);
  assert.equal(saveA.body.layers[0].objects[0].id, 'c1');
});

test('props panel 097: save refuses another map\'s document and never builds a request with an empty map', () => {
  const a = file('MapA');
  const refused = propsSaveRequest({ mapName: 'MapB', propsId: null, file: a, layoutId: null });
  assert.ok('error' in refused);
  const none = propsSaveRequest({ mapName: null, propsId: null, file: null, layoutId: null });
  assert.ok('error' in none);
  assert.equal(propsLoadId(null, ''), '');
});

test('props panel 097: saving a map with no props file creates Content/Data/LevelGen/props/<map>.props.json for that map', () => {
  const map = 'grok097_tmp_nostale';
  const fullPath = path.join(PROPS_DIR, `${map}.props.json`);
  try {
    if (fs.existsSync(fullPath)) fs.unlinkSync(fullPath);
    assert.throws(
      () => savePropsData(map, 5, []),
      (err: { status?: number }) => err.status === 404,
      'a non-zero revision must not create a missing file',
    );
    assert.equal(fs.existsSync(fullPath), false);

    const plan = propsSaveRequest({ mapName: map, propsId: null, file: null, layoutId: 'lay_tmp' });
    assert.ok(!('error' in plan));
    if ('error' in plan) return;
    const created = savePropsData(plan.map, plan.body.base_revision, plan.body.layers, plan.body.overrides, {
      layout: plan.body.layout,
      layout_hash: plan.body.layout_hash,
    });
    assert.equal(created.success, true);
    assert.equal(created.revision, 1);
    assert.equal(created.props.map, map);
    assert.equal(fs.existsSync(fullPath), true);
    assert.equal(path.basename(fullPath), `${map}.props.json`);

    const read = readPropsData(map);
    assert.equal(read.props.map, map);
    assert.equal(read.props.layout, 'lay_tmp');
    assert.equal(read.props.revision, 1);
    assert.deepEqual(read.props.layers, []);
    assert.deepEqual(validateProps(read.props), []);

    // A second save of this temp map updates it; it does not touch any other file.
    const again = savePropsData(map, 1, []);
    assert.equal(again.revision, 2);
    assert.equal(again.props.map, map);
  } finally {
    if (fs.existsSync(fullPath)) fs.unlinkSync(fullPath);
  }
});
