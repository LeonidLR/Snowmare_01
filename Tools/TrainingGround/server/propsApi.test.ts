// Training Ground: tests for props server API (server/propsApi.ts).
// Docs: PROPS_LAYERS.md section 4.1; Step 4 of inbox/010-night-queue.md.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  listPropsFiles,
  readPropsData,
  resolveLayoutFile,
  savePropsData,
} from './propsApi.ts';
import type { LevelPropsFile } from '../src/levelgen/props/schema.ts';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const FIXTURES_DIR = path.resolve(__dirname, '../../../Content/Data/LevelGen/props/fixtures');

test('propsApi: listPropsFiles discovers props fixtures and repo files', () => {
  const files = listPropsFiles();
  assert.ok(files.length > 0, 'must find at least one props file');
  const small = files.find((f) => f.id === 'fixture_small' || f.name.includes('fixture_small'));
  assert.ok(small, 'fixture_small must be present');
  assert.equal(small.isFixture, true);

  const depot = files.find((f) => f.map === 'L_Gen_Depot_1' || f.id.includes('depot'));
  assert.ok(depot, 'Depot props file must be found');
});

test('propsApi: readPropsData loads props, associated layout and balance', () => {
  const data = readPropsData('golden_depot_crates_s1');
  assert.ok(data.props, 'props must be loaded');
  assert.equal(data.props.schema, 'codex.levelgen_props/1');
  assert.equal(data.props.map, 'L_Gen_Depot_1');
  assert.ok(data.layout, 'associated layout must be loaded');
  assert.equal(data.layout.name, 'gen_ref_01_02_depot_1');
  assert.ok(data.balance, 'props_balance must be loaded');
});

test('propsApi: resolveLayoutFile resolves layout from accepted and fixtures', () => {
  const depot = resolveLayoutFile('gen_ref_01_02_depot_1');
  assert.ok(depot, 'gen_ref_01_02_depot_1 must be found');
  assert.equal(depot.name, 'gen_ref_01_02_depot_1');

  const small = resolveLayoutFile('fixture_small');
  assert.ok(small, 'fixture_small layout must be found');
  assert.equal(small.name, 'fixture_small');
});

test('propsApi: savePropsData refuses when file is frozen with 403', () => {
  const tmpFile = path.resolve(FIXTURES_DIR, '.test_frozen_sample.props.json');
  try {
    const frozenSample: LevelPropsFile = {
      schema: 'codex.levelgen_props/1',
      map: 'test_map',
      layout: 'fixture_small',
      layout_hash: '0000000000000000000000000000000000000000',
      revision: 3,
      frozen: true,
      layers: [],
    };
    fs.writeFileSync(tmpFile, JSON.stringify(frozenSample, null, 2), 'utf8');

    assert.throws(
      () => {
        savePropsData(tmpFile, 3, []);
      },
      (err: any) => {
        return err.status === 403 && /frozen/i.test(err.message);
      },
      'must throw 403 when file is frozen'
    );
  } finally {
    if (fs.existsSync(tmpFile)) fs.unlinkSync(tmpFile);
  }
});

test('propsApi: savePropsData refuses stale revision with 409', () => {
  const tmpFile = path.resolve(FIXTURES_DIR, '.test_stale_sample.props.json');
  try {
    const sample: LevelPropsFile = {
      schema: 'codex.levelgen_props/1',
      map: 'test_map',
      layout: 'fixture_small',
      layout_hash: '0000000000000000000000000000000000000000',
      revision: 5,
      frozen: false,
      layers: [],
    };
    fs.writeFileSync(tmpFile, JSON.stringify(sample, null, 2), 'utf8');

    assert.throws(
      () => {
        // base_revision 4 !== file.revision 5
        savePropsData(tmpFile, 4, []);
      },
      (err: any) => {
        return err.status === 409 && err.current_revision === 5 && /stale/i.test(err.message);
      },
      'must throw 409 when base_revision does not match file.revision'
    );
  } finally {
    if (fs.existsSync(tmpFile)) fs.unlinkSync(tmpFile);
  }
});

test('propsApi: savePropsData increments revision, writes canonically, and updates layers atomically', () => {
  const tmpFile = path.resolve(FIXTURES_DIR, '.test_save_sample.props.json');
  try {
    const sample: LevelPropsFile = {
      schema: 'codex.levelgen_props/1',
      map: 'test_map',
      layout: 'fixture_small',
      layout_hash: '0000000000000000000000000000000000000000',
      revision: 2,
      frozen: false,
      layers: [
        {
          id: 'crates',
          kind: 'crates',
          enabled: true,
          seed: 42,
          locked: false,
          rules: {},
          objects: [
            {
              id: 'crates_0000002a_0',
              variant: 'crate_s',
              pos: [100, 200],
              walk_layer: 'ground',
              yaw: 0,
            },
          ],
        },
      ],
      overrides: [
        {
          op: 'move',
          id: 'crates_0000002a_0',
          pos: [150, 250],
          walk_layer: 'ground',
        },
      ],
    };
    fs.writeFileSync(tmpFile, JSON.stringify(sample, null, 2), 'utf8');

    const updatedLayers = [
      {
        ...sample.layers[0],
        objects: [
          {
            id: 'crates_0000002a_0',
            variant: 'crate_s',
            pos: [120, 220] as [number, number],
            walk_layer: 'ground',
            yaw: 90,
          },
        ],
      },
    ];

    const result = savePropsData(tmpFile, 2, updatedLayers);
    assert.equal(result.success, true);
    assert.equal(result.revision, 3, 'revision must be bumped from 2 to 3');

    // Re-read from disk to verify atomic persistence & canonical formatting
    const diskText = fs.readFileSync(tmpFile, 'utf8');
    assert.ok(diskText.includes('\t"schema": "codex.levelgen_props/1"'), 'must use tabs (canonical format)');
    const parsed: LevelPropsFile = JSON.parse(diskText);
    assert.equal(parsed.revision, 3);
    assert.equal(parsed.layers[0].objects[0].pos[0], 120);
    assert.equal(parsed.overrides?.length, 1, 'overrides must be preserved untouched');
  } finally {
    if (fs.existsSync(tmpFile)) fs.unlinkSync(tmpFile);
  }
});
