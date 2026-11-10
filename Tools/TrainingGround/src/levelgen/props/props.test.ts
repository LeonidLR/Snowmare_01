// Training Ground / level generator: tests for codex.levelgen_props/1 schema, validation, migrate stub, and read/write.
// Docs: docs/design/levelgen/PROPS_LAYERS.md sections 1.2, 1.4, 5 (task T1).
// Covers all 6 review points from docs/port/gemini/reviews/t1-props-schema-review.md.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  PROPS_SCHEMA,
  type LevelPropsFile,
  formatPropsJson,
  parsePropsJson,
  readPropsFile,
  validateProps,
  writePropsFile,
  writePropsJson,
} from './schema.ts';
import { migrateProps, needsMigration } from './migrate.ts';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const FIXTURES_DIR = path.resolve(__dirname, '../../../../../Content/Data/LevelGen/props/fixtures');
const FIXTURE_PROPS_PATH = path.join(FIXTURES_DIR, 'fixture_small.props.json');
const FIXTURE_LAYOUT_PATH = path.join(FIXTURES_DIR, 'fixture_small.layout.json');

test('props T1: shared fixture fixture_small.props.json exists and is valid codex.levelgen_props/1', () => {
  assert.ok(fs.existsSync(FIXTURE_PROPS_PATH), `fixture props exists at ${FIXTURE_PROPS_PATH}`);
  assert.ok(fs.existsSync(FIXTURE_LAYOUT_PATH), `fixture layout exists at ${FIXTURE_LAYOUT_PATH}`);

  const rawText = fs.readFileSync(FIXTURE_PROPS_PATH, 'utf8');
  const data = JSON.parse(rawText);

  // Validate the shared fixture
  const errors = validateProps(data);
  assert.deepEqual(errors, [], 'shared fixture must validate with zero errors');

  // Verify top-level fields
  const props = parsePropsJson(rawText);
  assert.equal(props.schema, PROPS_SCHEMA);
  assert.equal(props.map, 'L_Fixture_Small');
  assert.equal(props.layout, 'fixture_small');
  assert.equal(props.revision, 14);
  assert.equal(props.frozen, false);

  // Verify unknown root key kept
  assert.equal(props.x_note, 'unknown root key: must survive a round-trip');

  // Verify layers
  assert.equal(props.layers.length, 3);
  const [crates, barrels, mines] = props.layers;

  assert.equal(crates.id, 'crates');
  assert.equal(crates.kind, 'crates');
  assert.equal(crates.seed, 7);
  assert.equal(crates.locked, false);
  assert.equal(crates.preset, 'crates_default');
  assert.equal(crates.rules_hash, 'a91e');
  assert.equal(crates.objects.length, 2);
  assert.equal(crates.objects[0].id, 'crates_00000007_0');
  assert.equal(crates.objects[0].locked, true);
  assert.equal(crates.objects[0].x_tag, 'keep me', 'unknown object key must be preserved');
  assert.deepEqual(crates.report, { gate: 'pass', placed: 2, requested: 2 });

  assert.equal(barrels.id, 'barrels');
  assert.equal(barrels.kind, 'barrels');
  assert.equal(barrels.seed, 3);
  assert.equal(barrels.objects.length, 2);

  assert.equal(mines.id, 'mines');
  assert.equal(mines.kind, 'mines');
  assert.equal(mines.seed, 11);
  assert.equal(mines.objects.length, 2);

  // Verify overrides
  assert.ok(props.overrides);
  assert.equal(props.overrides.length, 4);
  assert.equal(props.overrides[0].op, 'move');
  assert.equal(props.overrides[1].op, 'delete');
  assert.equal(props.overrides[2].op, 'add');
  assert.equal(props.overrides[3].op, 'set');
});

test('props T1: round-trip preserves all data and unknown keys with stable ordering', () => {
  const rawText = fs.readFileSync(FIXTURE_PROPS_PATH, 'utf8');
  const original = parsePropsJson(rawText);

  // Format with CRLF
  const formattedCrlf = writePropsJson(original, { crlf: true });
  const parsedAgain = parsePropsJson(formattedCrlf);

  // Deep comparison of parsed objects
  assert.deepEqual(parsedAgain, original, 'round-trip parsed data must be deepEqual to original');
  assert.equal(parsedAgain.x_note, 'unknown root key: must survive a round-trip');
  assert.equal(parsedAgain.layers[0].objects[0].x_tag, 'keep me');

  // Second serialization must produce byte-identical string (deterministic stability)
  const formattedTwice = formatPropsJson(parsedAgain, { crlf: true });
  assert.equal(formattedTwice, formattedCrlf, 'subsequent serialization must be byte-identical');
});

test('props T1 (defect 1): nested key insertion order is preserved (variants order drives the roll)', () => {
  const rawText = fs.readFileSync(FIXTURE_PROPS_PATH, 'utf8');
  const doc = JSON.parse(rawText) as LevelPropsFile;

  // Explicitly set insertion order of variants: crate_s, then crate_l
  doc.layers[0].rules = {
    variants: { crate_s: 3, crate_l: 1 },
    count: { mode: 'count', value: 2 },
  };

  const formatted = formatPropsJson(doc);
  const variantsMatch = formatted.match(/"variants":\s*\{([^}]+)\}/);
  assert.ok(variantsMatch, 'variants must be present in formatted output');
  const content = variantsMatch[1];
  const idxS = content.indexOf('"crate_s"');
  const idxL = content.indexOf('"crate_l"');
  assert.ok(idxS !== -1 && idxL !== -1 && idxS < idxL, 'crate_s must precede crate_l in insertion order');
});

test('props T1 (defect 2): undefined values are skipped and special characters in keys are escaped', () => {
  const rawText = fs.readFileSync(FIXTURE_PROPS_PATH, 'utf8');
  const doc = JSON.parse(rawText) as LevelPropsFile;

  // Add an undefined property and a key with special characters
  (doc as any)['undef_key'] = undefined;
  (doc as any)['key_with_"quotes"_and_\\backslash'] = 'safe_val';

  const formatted = formatPropsJson(doc);
  assert.ok(!formatted.includes('undefined'), 'output must never contain literal undefined text');
  assert.ok(formatted.includes('"key_with_\\"quotes\\"_and_\\\\backslash"'), 'special characters in key must be escaped');

  const parsed = JSON.parse(formatted);
  assert.equal(parsed['key_with_"quotes"_and_\\backslash'], 'safe_val');
  assert.ok(!('undef_key' in parsed));
});

test('props T1 (defect 3): no trailing commas when objects or report is missing', () => {
  const rawText = fs.readFileSync(FIXTURE_PROPS_PATH, 'utf8');
  const doc = JSON.parse(rawText) as LevelPropsFile;

  // Create a minimal layer with neither objects nor report
  const minimalDoc: LevelPropsFile = {
    ...doc,
    layers: [
      {
        id: 'bare_layer',
        kind: 'crates',
        enabled: true,
        seed: 123,
        locked: false,
        rules: {},
        objects: [],
      },
    ],
  };

  const formatted = formatPropsJson(minimalDoc);
  // Must parse cleanly as standard JSON with zero trailing comma errors
  const parsed = JSON.parse(formatted);
  assert.equal(parsed.layers[0].id, 'bare_layer');
  assert.deepEqual(parsed.layers[0].objects, []);
});

test('props T1 (defect 4): overrides: [] is preserved on write', () => {
  const rawText = fs.readFileSync(FIXTURE_PROPS_PATH, 'utf8');
  const doc = JSON.parse(rawText) as LevelPropsFile;

  // Empty overrides array
  doc.overrides = [];
  const formatted = formatPropsJson(doc);
  assert.ok(formatted.includes('"overrides": []'), 'overrides: [] must be preserved in formatted JSON');

  const parsed = JSON.parse(formatted);
  assert.ok(Array.isArray(parsed.overrides));
  assert.equal(parsed.overrides.length, 0);
});

test('props T1 (defect 5): null / invalid object in add override is rejected by validator and does not crash', () => {
  const rawText = fs.readFileSync(FIXTURE_PROPS_PATH, 'utf8');
  const doc = JSON.parse(rawText) as LevelPropsFile;

  const corruptedAdd = {
    ...doc,
    overrides: [{ op: 'add' as const, object: null as any }],
  };

  const errors = validateProps(corruptedAdd);
  assert.ok(errors.some((e) => e.includes('object must be an object')), 'validator must reject null object in add override');

  // formatPropsJson without skipValidation must refuse to write invalid doc
  assert.throws(() => formatPropsJson(corruptedAdd), /Cannot format invalid props file/);

  // Even with skipValidation it must not throw an unhandled TypeError crash
  assert.doesNotThrow(() => formatPropsJson(corruptedAdd, { skipValidation: true }));
});

test('props T1 (defect 6): validator rejects duplicate object ids across layers and add overrides', () => {
  const rawText = fs.readFileSync(FIXTURE_PROPS_PATH, 'utf8');
  const doc = JSON.parse(rawText) as LevelPropsFile;

  // Duplicate object id inside layers
  const dupInLayers: LevelPropsFile = {
    ...doc,
    layers: [
      {
        ...doc.layers[0],
        objects: [
          doc.layers[0].objects[0],
          { ...doc.layers[0].objects[1], id: doc.layers[0].objects[0].id },
        ],
      },
      ...doc.layers.slice(1),
    ],
  };
  const errors1 = validateProps(dupInLayers);
  assert.ok(errors1.some((e) => e.includes('duplicate object id')), 'must reject duplicate object id within layers');

  // Duplicate object id across layer object and add override
  const dupInAdd: LevelPropsFile = {
    ...doc,
    overrides: [
      {
        op: 'add',
        object: {
          id: doc.layers[0].objects[0].id, // same as crates_00000007_0
          variant: 'crate_s',
          pos: [100, 200],
          walk_layer: 'ground',
        },
      },
    ],
  };
  const errors2 = validateProps(dupInAdd);
  assert.ok(errors2.some((e) => e.includes('duplicate object id')), 'must reject add override duplicating an existing object id');
});

test('props T1 (defect 6): validator enforces integer pos and yaw in objects and overrides', () => {
  const rawText = fs.readFileSync(FIXTURE_PROPS_PATH, 'utf8');
  const doc = JSON.parse(rawText) as LevelPropsFile;

  // Float pos in layer object
  const floatObjPos = {
    ...doc,
    layers: [
      {
        ...doc.layers[0],
        objects: [{ ...doc.layers[0].objects[0], pos: [100.5, 200] as any }],
      },
    ],
  };
  assert.ok(validateProps(floatObjPos).some((e) => e.includes('pos must be [x, y] in integer cm')));

  // Float yaw in layer object
  const floatObjYaw = {
    ...doc,
    layers: [
      {
        ...doc.layers[0],
        objects: [{ ...doc.layers[0].objects[0], yaw: 45.5 }],
      },
    ],
  };
  assert.ok(validateProps(floatObjYaw).some((e) => e.includes('yaw must be an integer degree')));

  // Float pos in move override
  const floatMovePos = {
    ...doc,
    overrides: [{ op: 'move' as const, id: 'x', pos: [10.2, 20] as any, walk_layer: 'ground' }],
  };
  assert.ok(validateProps(floatMovePos).some((e) => e.includes('pos must be [x, y] in integer cm')));
});

test('props T1 (defect 6): validator rejects set override attempting to modify id, pos or walk_layer', () => {
  const rawText = fs.readFileSync(FIXTURE_PROPS_PATH, 'utf8');
  const doc = JSON.parse(rawText) as LevelPropsFile;

  // set override with id in fields
  const setWithId = {
    ...doc,
    overrides: [{ op: 'set' as const, id: 'crates_00000007_0', fields: { id: 'new_id' } }],
  };
  assert.ok(validateProps(setWithId).some((e) => e.includes('fields must not modify id')));

  // set override with pos in fields
  const setWithPos = {
    ...doc,
    overrides: [{ op: 'set' as const, id: 'crates_00000007_0', fields: { pos: [0, 0] } }],
  };
  assert.ok(validateProps(setWithPos).some((e) => e.includes('fields must not modify pos')));
});

test('props T1: validator rejects bad layer kind', () => {
  const rawText = fs.readFileSync(FIXTURE_PROPS_PATH, 'utf8');
  const doc = JSON.parse(rawText) as LevelPropsFile;

  // Corrupt layer kind
  const corrupted = {
    ...doc,
    layers: [
      {
        ...doc.layers[0],
        kind: 'lasers_bad' as any,
      },
      ...doc.layers.slice(1),
    ],
  };

  const errors = validateProps(corrupted);
  assert.ok(errors.length > 0, 'must have validation errors for bad kind');
  assert.ok(
    errors.some((e) => e.includes('bad kind') && e.includes('lasers_bad')),
    `expected bad kind error, got: ${errors.join('; ')}`
  );

  // parsePropsJson must throw
  assert.throws(() => parsePropsJson(JSON.stringify(corrupted)), /bad kind/);
});

test('props T1: validator rejects missing or invalid seed', () => {
  const rawText = fs.readFileSync(FIXTURE_PROPS_PATH, 'utf8');
  const doc = JSON.parse(rawText) as LevelPropsFile;

  // Layer with missing seed
  const noSeed = {
    ...doc,
    layers: [
      {
        ...doc.layers[0],
        seed: undefined as any,
      },
    ],
  };

  const errorsNoSeed = validateProps(noSeed);
  assert.ok(errorsNoSeed.length > 0, 'must reject missing seed');
  assert.ok(
    errorsNoSeed.some((e) => e.includes('seed')),
    `expected missing seed error, got: ${errorsNoSeed.join('; ')}`
  );

  // Layer with negative seed
  const negativeSeed = {
    ...doc,
    layers: [
      {
        ...doc.layers[0],
        seed: -5,
      },
    ],
  };

  const errorsNegative = validateProps(negativeSeed);
  assert.ok(errorsNegative.some((e) => e.includes('seed')), 'must reject negative seed');
});

test('props T1: validator rejects bad top-level schema and corrupted fields', () => {
  const rawText = fs.readFileSync(FIXTURE_PROPS_PATH, 'utf8');
  const doc = JSON.parse(rawText) as LevelPropsFile;

  // Unknown schema
  assert.ok(
    validateProps({ ...doc, schema: 'codex.levelgen_props/99' }).some((e) => e.includes('schema')),
    'must reject schema version /99'
  );

  // Negative revision
  assert.ok(
    validateProps({ ...doc, revision: -1 }).some((e) => e.includes('revision')),
    'must reject negative revision'
  );

  // Non-boolean frozen
  assert.ok(
    validateProps({ ...doc, frozen: 'not_a_bool' as any }).some((e) => e.includes('frozen')),
    'must reject non-boolean frozen'
  );

  // Duplicate layer id
  assert.ok(
    validateProps({
      ...doc,
      layers: [doc.layers[0], { ...doc.layers[0], id: doc.layers[0].id, seed: 99 }],
    }).some((e) => e.includes('duplicate layer id')),
    'must reject duplicate layer id'
  );
});

test('props T1: migrate stub correctly passes /1 and rejects unknown versions', () => {
  const rawText = fs.readFileSync(FIXTURE_PROPS_PATH, 'utf8');
  const doc = JSON.parse(rawText);

  // Current schema /1 needs no migration
  assert.equal(needsMigration(doc), false);
  const migrated = migrateProps(doc);
  assert.deepEqual(migrated, doc);

  // Future schema version /2
  const docV2 = { ...doc, schema: 'codex.levelgen_props/2' };
  assert.equal(needsMigration(docV2), true);
  assert.throws(() => migrateProps(docV2), /unsupported schema version/);

  // Non-props schema
  assert.throws(() => migrateProps({ schema: 'other/1' }), /unknown props schema/);
});

test('props T1: file helpers readPropsFile and writePropsFile work end to end', () => {
  // readPropsFile
  const loaded = readPropsFile(FIXTURE_PROPS_PATH);
  assert.equal(loaded.map, 'L_Fixture_Small');

  // writePropsFile to temp location
  const tempPath = path.join(FIXTURES_DIR, '_temp_test_props.json');
  try {
    writePropsFile(tempPath, loaded, { crlf: true });
    assert.ok(fs.existsSync(tempPath));

    const reloaded = readPropsFile(tempPath);
    assert.deepEqual(reloaded, loaded);
  } finally {
    if (fs.existsSync(tempPath)) {
      fs.unlinkSync(tempPath);
    }
  }
});
