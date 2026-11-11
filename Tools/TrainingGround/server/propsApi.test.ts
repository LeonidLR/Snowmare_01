// Training Ground: tests for props server API (server/propsApi.ts).
// Docs: PROPS_LAYERS.md section 4.1; Step 4 of inbox/010-night-queue.md; review defect #82.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';

import {
  listPropsFiles,
  readPropsData,
  resolveLayoutFile,
  resolvePropsFile,
  savePropsData,
  PROPS_DIR,
} from './propsApi.ts';
import type { LevelPropsFile } from '../src/levelgen/props/schema.ts';



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

test('propsApi: resolvePropsFile rejects path traversal, slashes, and non-bare names', () => {
  assert.equal(resolvePropsFile('../something'), null);
  assert.equal(resolvePropsFile('..\\something'), null);
  assert.equal(resolvePropsFile('dir/name'), null);
  assert.equal(resolvePropsFile('dir\\name'), null);
  assert.equal(resolvePropsFile('/etc/passwd'), null);
  assert.equal(resolvePropsFile('C:\\windows\\system32'), null);
  assert.equal(resolvePropsFile('name with spaces'), null);
  assert.equal(resolvePropsFile('name;injection'), null);
});

test('propsApi: savePropsData refuses path traversal and slashes with 400', () => {
  const invalidNames = [
    '../something',
    '..\\something',
    'dir/file.props.json',
    'dir\\file.props.json',
    'C:\\path\\file.props.json',
    'foo/../bar.props.json',
    '/abs/path.props.json',
  ];

  for (const name of invalidNames) {
    assert.throws(
      () => {
        savePropsData(name, 1, []);
      },
      (err: any) => {
        return err.status === 400 && /(invalid|path separator|traversal|bare)/i.test(err.message);
      },
      `must refuse invalid path "${name}" with 400`
    );
  }
});

test('propsApi: savePropsData refuses fixtures and golden_* files with 400', () => {
  const fixtureNames = [
    'golden_depot_crates_s1',
    'golden_depot_barrels_s1',
    'golden_in_depot',
    'fixture_small',
    'golden_small_crates',
  ];

  for (const name of fixtureNames) {
    assert.throws(
      () => {
        savePropsData(name, 1, []);
      },
      (err: any) => {
        return err.status === 400 && /(fixture|golden)/i.test(err.message);
      },
      `must refuse fixture "${name}" with 400`
    );
  }
});

test('propsApi: savePropsData refuses files not ending with .props.json with 400', () => {
  const nonPropsNames = [
    'level_01.umap',
    'level_01.json',
    'config.ini',
    'script.py',
  ];

  for (const name of nonPropsNames) {
    assert.throws(
      () => {
        savePropsData(name, 1, []);
      },
      (err: any) => {
        return err.status === 400 && /(must end with \.props\.json|\.props\.json)/i.test(err.message);
      },
      `must refuse non-.props.json name "${name}" with 400`
    );
  }
});

test('propsApi: savePropsData refuses when file is frozen with 403', () => {
  const bareName = 'test_tmp_frozen.props.json';
  const fullPath = path.join(PROPS_DIR, bareName);
  try {
    const frozenSample: LevelPropsFile = {
      schema: 'codex.levelgen_props/1',
      map: 'test_tmp_frozen',
      layout: 'fixture_small',
      layout_hash: '0000000000000000000000000000000000000000',
      revision: 3,
      frozen: true,
      layers: [],
    };
    fs.writeFileSync(fullPath, JSON.stringify(frozenSample, null, 2), 'utf8');

    assert.throws(
      () => {
        savePropsData(bareName, 3, []);
      },
      (err: any) => {
        return err.status === 403 && /frozen/i.test(err.message);
      },
      'must throw 403 when file is frozen'
    );
  } finally {
    if (fs.existsSync(fullPath)) fs.unlinkSync(fullPath);
  }
});

test('propsApi: savePropsData refuses stale revision with 409', () => {
  const bareName = 'test_tmp_stale.props.json';
  const fullPath = path.join(PROPS_DIR, bareName);
  try {
    const sample: LevelPropsFile = {
      schema: 'codex.levelgen_props/1',
      map: 'test_tmp_stale',
      layout: 'fixture_small',
      layout_hash: '0000000000000000000000000000000000000000',
      revision: 5,
      frozen: false,
      layers: [],
    };
    fs.writeFileSync(fullPath, JSON.stringify(sample, null, 2), 'utf8');

    assert.throws(
      () => {
        // base_revision 4 !== file.revision 5
        savePropsData(bareName, 4, []);
      },
      (err: any) => {
        return err.status === 409 && err.current_revision === 5 && /stale/i.test(err.message);
      },
      'must throw 409 when base_revision does not match file.revision'
    );
  } finally {
    if (fs.existsSync(fullPath)) fs.unlinkSync(fullPath);
  }
});

test('propsApi: savePropsData increments revision, writes canonically, and updates layers atomically for valid bare names', () => {
  const bareName = 'test_tmp_save.props.json';
  const fullPath = path.join(PROPS_DIR, bareName);
  try {
    const sample: LevelPropsFile = {
      schema: 'codex.levelgen_props/1',
      map: 'test_tmp_save',
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
    fs.writeFileSync(fullPath, JSON.stringify(sample, null, 2), 'utf8');

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

    const result = savePropsData(bareName, 2, updatedLayers);
    assert.equal(result.success, true);
    assert.equal(result.revision, 3, 'revision must be bumped from 2 to 3');

    // Re-read from disk to verify atomic persistence & canonical formatting
    const diskText = fs.readFileSync(fullPath, 'utf8');
    assert.ok(diskText.includes('\t"schema": "codex.levelgen_props/1"'), 'must use tabs (canonical format)');
    const parsed: LevelPropsFile = JSON.parse(diskText);
    assert.equal(parsed.revision, 3);
    assert.equal(parsed.layers[0].objects[0].pos[0], 120);
    assert.equal(parsed.overrides?.length, 1, 'overrides must be preserved untouched');
  } finally {
    if (fs.existsSync(fullPath)) fs.unlinkSync(fullPath);
  }
});
