// Training Ground / level generator: placement rules and golden tests for codex.levelgen_props/1.
// Docs: docs/design/levelgen/PROPS_LAYERS.md sections 0, 2.2-2.5, 5 (T2), and 7.
// C++ reference: Source/CodexTacticsTests/Private/LevelGen/PropsPlacementRulesTest.cpp

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { PropsRng, fnv1a, streamSeed } from './rng.ts';
import { PropsLayoutView } from './layoutView.ts';
import {
  makeInput,
  rerollLayer,
  place,
  type LevelPropsFile,
  type PropObject,
} from './placement.ts';
import { formatPropsCanonical } from './canonical.ts';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const FIXTURES_DIR = path.resolve(__dirname, '../../../../../Content/Data/LevelGen/props/fixtures');

// Normalize CRLF to LF for portable golden comparisons
function normalizeLf(s: string): string {
  return s.replace(/\r\n/g, '\n');
}

function findFirstDiff(expected: string, actual: string) {
  const minLen = Math.min(expected.length, actual.length);
  for (let i = 0; i < minLen; i++) {
    if (expected[i] !== actual[i]) {
      const expSnippet = expected.slice(Math.max(0, i - 25), Math.min(expected.length, i + 25));
      const actSnippet = actual.slice(Math.max(0, i - 25), Math.min(actual.length, i + 25));
      const line = expected.slice(0, i).split('\n').length;
      const col = i - expected.slice(0, i).lastIndexOf('\n');
      return `Diff at byte ${i} (line ${line}, col ${col}):\n  Expected: ${JSON.stringify(expSnippet)}\n  Actual:   ${JSON.stringify(actSnippet)}`;
    }
  }
  if (expected.length !== actual.length) {
    return `Length mismatch: expected ${expected.length}, got ${actual.length}`;
  }
  return null;
}

function findDifferingObject(expectedFile: LevelPropsFile, actualFile: LevelPropsFile, layerId: string) {
  const expLayer = expectedFile.layers.find((l) => l.id === layerId);
  const actLayer = actualFile.layers.find((l) => l.id === layerId);
  if (!expLayer || !actLayer) return `Layer ${layerId} missing`;
  const maxLen = Math.max(expLayer.objects.length, actLayer.objects.length);
  for (let i = 0; i < maxLen; i++) {
    const o1 = expLayer.objects[i];
    const o2 = actLayer.objects[i];
    if (JSON.stringify(o1) !== JSON.stringify(o2)) {
      return `Object #${i} mismatch:\n  Expected: ${JSON.stringify(o1)}\n  Actual:   ${JSON.stringify(o2)}`;
    }
  }
  return null;
}

// ---------------- 1. RNG & FNV-1a Reference Vectors ----------------

test('props T2: PropsRng mulberry32 reference vectors match C++ vectors', () => {
  const vectors: Array<{ seed: number; out: number[] }> = [
    { seed: 0, out: [0x4434b462, 0x00159c37, 0x39285b08, 0x256d8104, 0x77a2cbd4] },
    { seed: 1, out: [0xa087eaf3, 0x00b349c9, 0x8706c4eb, 0xfb2627fd, 0xf7e79d2b] },
    { seed: 7, out: [0x02ff152c, 0x0fdc7f12, 0xfa169e5e, 0xb2f38b96, 0x857d6fe7] },
    { seed: 0xffffffff, out: [0xe57bf3d3, 0x3081a5a4, 0xb7350390, 0xf1ade904, 0xd8616a2f] },
  ];

  for (const v of vectors) {
    const rng = new PropsRng(v.seed);
    for (let i = 0; i < 5; i++) {
      assert.equal(
        rng.nextU32(),
        v.out[i] >>> 0,
        `mulberry32(0x${(v.seed >>> 0).toString(16)}) #${i} mismatch`
      );
    }
  }

  // Layer stream of crates / seed 7 = mulberry32(7 ^ fnv1a("crates"))
  assert.equal(streamSeed(7, 'crates'), 0xfcecbe4e >>> 0);
  const stream = new PropsRng(streamSeed(7, 'crates'));
  assert.equal(stream.nextU32(), 0x9bb9e7c5 >>> 0);
  assert.equal(stream.nextU32(), 0x694c7fa9 >>> 0);
  assert.equal(stream.nextU32(), 0x82a1553f >>> 0);
});

test('props T2: fnv1a string hash reference vectors match C++ vectors', () => {
  assert.equal(fnv1a(''), 0x811c9dc5 >>> 0);
  assert.equal(fnv1a('a'), 0xe40c292c >>> 0);
  assert.equal(fnv1a('foobar'), 0xbf9cf968 >>> 0);
  assert.equal(fnv1a('crates'), 0xfcecbe49 >>> 0);
  assert.equal(fnv1a('barrels'), 0x575f8e06 >>> 0);
  // UTF-8 Cyrillic "ящик" (U+044F, U+0449, U+0438, U+043A)
  assert.equal(fnv1a('\u044F\u0449\u0438\u043A'), 0xdb9bb9b9 >>> 0);
});

test('props T2: PropsRng bounded NextBelow, shuffle and modulo match C++ vectors', () => {
  {
    const rng = new PropsRng(1);
    const expected = [6, 0, 5, 9, 9, 2, 6, 7];
    for (let i = 0; i < 8; i++) {
      assert.equal(rng.nextBelow(10), expected[i], `nextBelow(10) #${i}`);
    }
  }
  {
    const rng = new PropsRng(42);
    const items = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
    rng.shuffle(items);
    assert.deepEqual(items, [0, 7, 3, 5, 2, 1, 8, 9, 4, 6], 'shuffle seed 42');
  }
  {
    const rng = new PropsRng(5);
    const expected = [0, 3, 2, 1, 3, 0];
    for (let i = 0; i < 6; i++) {
      assert.equal(rng.nextU32() % 4, expected[i], `nextU32() % 4 #${i}`);
    }
  }
  {
    // nextBelow(0) returns 0 and draws nothing; 1-item shuffle draws nothing
    const a = new PropsRng(9);
    const b = new PropsRng(9);
    assert.equal(a.nextBelow(0), 0);
    const one = [3];
    a.shuffle(one);
    assert.equal(a.nextU32(), b.nextU32(), 'streams still aligned');
  }
});

// ---------------- 2. PropsLayoutView Geometry Checks ----------------

test('props T2: PropsLayoutView builds and correctly extracts Depot geometry', () => {
  const balancePath = path.resolve(FIXTURES_DIR, '../../../Balance/props_balance.json');
  const balance = JSON.parse(fs.readFileSync(balancePath, 'utf8'));
  const config = PropsLayoutView.readConfig(balance);

  const depotLayoutPath = path.resolve(FIXTURES_DIR, '../../accepted/gen_ref_01_02_depot_1.json');
  const depotRaw = fs.readFileSync(depotLayoutPath, 'utf8');
  const view = PropsLayoutView.build(depotRaw, config);

  assert.equal(view.name, 'gen_ref_01_02_depot_1');
  assert.equal(view.spawnPos[0], 2356);
  assert.equal(view.spawnPos[1], -8700);

  // Check gate at (1400, -6084) is found and on mandatory route
  const gate = view.doorways.find((d) => d.pos[0] === 1400 && d.pos[1] === -6084);
  assert.ok(gate, 'gate at (1400, -6084) must be found');
  assert.equal(gate.bOnMandatoryRoute, true, 'gate (1400, -6084) must be on mandatory route');

  // Check B_hall_n has 2 doors
  const hallDoors = view.doorways.filter((d) => d.building === 'B_hall_n');
  assert.equal(hallDoors.length, 2, 'B_hall_n must have 2 doors');

  // Check B_pump#0 is not on mandatory route
  const pump0 = view.doorways.find((d) => d.building === 'B_pump' && d.floor === 0);
  assert.ok(pump0, 'B_pump#0 door must exist');
  assert.equal(pump0.bOnMandatoryRoute, false, 'B_pump#0 must not be on mandatory route');
});

// ---------------- 3. Golden Placement Fixture Tests ----------------

interface GoldenCase {
  case: string;
  layout: string;
  input: string;
  layer: string;
  seed: number;
  output: string;
  derived_from?: string;
  /** "C4": turrets / mines / loot cases (PROPS_LAYERS.md 7.1), skipped until the TS port of C4 lands. */
  step?: string;
  lock?: number[];
  delete?: number[];
  move?: number[];
  move_by_cm?: [number, number];
  hand?: number[];
}

interface GoldenManifest {
  schema: string;
  balance: string;
  cases: GoldenCase[];
}

function deriveRecipe(source: LevelPropsFile, layerId: string, c: GoldenCase): LevelPropsFile {
  const derived: LevelPropsFile = JSON.parse(JSON.stringify(source));
  const layer = derived.layers.find((l) => l.id === layerId);
  if (!layer) return derived;
  const original = JSON.parse(JSON.stringify(layer.objects)) as PropObject[];

  if (c.lock) {
    for (const i of c.lock) {
      if (original[i]) layer.objects[i].locked = true;
    }
  }

  if (!derived.overrides) derived.overrides = [];

  if (c.move && c.move_by_cm) {
    for (const i of c.move) {
      if (original[i]) {
        derived.overrides.push({
          op: 'move',
          id: original[i].id,
          pos: [original[i].pos[0] + c.move_by_cm[0], original[i].pos[1] + c.move_by_cm[1]],
          walk_layer: original[i].walk_layer,
          yaw: original[i].yaw,
        });
      }
    }
  }

  if (c.delete) {
    for (const i of c.delete) {
      if (original[i]) {
        derived.overrides.push({
          op: 'delete',
          id: original[i].id,
          pos: original[i].pos,
        });
      }
    }
  }

  if (c.hand) {
    let handIndex = 0;
    const handIds = new Set<string>();
    for (const i of c.hand) {
      if (original[i]) {
        const handObj: PropObject = {
          ...original[i],
          id: `hand_${layerId}_${handIndex++}`,
          origin: 'hand',
          locked: false,
        };
        derived.overrides.push({
          op: 'add',
          object: handObj,
        });
        handIds.add(original[i].id);
      }
    }
    layer.objects = layer.objects.filter((o) => !handIds.has(o.id));
  }

  delete layer.report;
  return derived;
}

const manifestPath = path.join(FIXTURES_DIR, 'golden_cases.json');
const manifest: GoldenManifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
const balancePath = path.resolve(FIXTURES_DIR, manifest.balance);
const balance = JSON.parse(fs.readFileSync(balancePath, 'utf8'));
const outputsMap = new Map<string, LevelPropsFile>();

test('props T2: derived case input reproduces golden_in_depot_locked.props.json byte for byte', () => {
  const c1 = manifest.cases.find((c) => c.case === 'depot_crates_s1');
  const c2 = manifest.cases.find((c) => c.case === 'depot_locked_reroll');
  assert.ok(c1 && c2, 'c1 and c2 exist');

  const layoutRaw = fs.readFileSync(path.resolve(FIXTURES_DIR, c1.layout), 'utf8');
  const inputRaw = fs.readFileSync(path.resolve(FIXTURES_DIR, c1.input), 'utf8');
  const config = PropsLayoutView.readConfig(balance);
  const view = PropsLayoutView.build(layoutRaw, config);
  const file: LevelPropsFile = JSON.parse(inputRaw);

  rerollLayer(file, balance, view, c1.layer, c1.seed);

  const derived = deriveRecipe(file, c2.layer, c2);
  const formattedDerived = formatPropsCanonical(derived);
  const expectedDerived = normalizeLf(fs.readFileSync(path.resolve(FIXTURES_DIR, c2.input), 'utf8'));

  const diff = findFirstDiff(expectedDerived, formattedDerived);
  assert.equal(diff, null, `Derived input must match ${c2.input} byte-for-byte`);
});

for (const c of manifest.cases) {
  const skip = c.step === 'C4' ? 'C4 case: TS port pending (PROPS_LAYERS.md 7.1)' : false;
  test(`props T2 golden case: ${c.case} reproduces ${c.output} byte for byte`, { skip }, () => {
    const layoutPath = path.resolve(FIXTURES_DIR, c.layout);
    const inputPath = path.resolve(FIXTURES_DIR, c.input);
    const outputPath = path.resolve(FIXTURES_DIR, c.output);

    const layoutRaw = fs.readFileSync(layoutPath, 'utf8');
    const inputRaw = fs.readFileSync(inputPath, 'utf8');
    const expectedRaw = normalizeLf(fs.readFileSync(outputPath, 'utf8'));

    const config = PropsLayoutView.readConfig(balance);
    const view = PropsLayoutView.build(layoutRaw, config);
    const file: LevelPropsFile = JSON.parse(inputRaw);

    const result = rerollLayer(file, balance, view, c.layer, c.seed);
    outputsMap.set(c.case, JSON.parse(JSON.stringify(file)));

    assert.ok(result.report.placed > 0, `${c.case}: places something`);
    assert.equal(result.report.short_by, 0, `${c.case}: not short`);

    const formatted = formatPropsCanonical(file);
    const diff = findFirstDiff(expectedRaw, formatted);
    if (diff) {
      const expectedObj = JSON.parse(expectedRaw) as LevelPropsFile;
      const differingObj = findDifferingObject(expectedObj, file, c.layer);
      assert.fail(
        `${c.case} FAILED parity with ${c.output} (RNG calls: ${result.rngCalls ?? 'unknown'}):\n${diff}\n${differingObj ?? ''}`
      );
    }

    assert.equal(formatted, expectedRaw, `${c.case} must match ${c.output} byte for byte`);
  });
}
