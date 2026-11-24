// Training Ground / level generator: the power layer (docs/design/TURRET_POWER.md phase 1, designer decisions 2026-11-17).
//
// Validates:
//   1. The start-state quotas (largest remainder, ~75 % dark) and the network size draw.
//   2. The power layer of the committed generated maps (X2 / X4 / X6): every cable within reach, every turret linked, a running
//      hostile network never near the spawn, jerrycans for ~40 % of the generators, no warning - and a fresh build is identical
//      (determinism).
//   3. validatePowerLinks: the same cases and reasons as the C++ FPropsValidator::ValidatePowerLinks
//      (CodexTactics.LevelGen.Props.Power.LinkValidator).

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import type { LevelPropsFile, PropObject } from './schema.ts';
import { PropsLayoutView } from './layoutView.ts';
import { applyOverrides, type PropsResolvedObject } from './placement.ts';
import { PropsRng } from './rng.ts';
import { buildPowerLayer, drawNetworkSize, readPowerConfig, startQuotas, validatePowerLinks, POWER_LAYER_ID } from './power.ts';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const ROOT = path.resolve(__dirname, '../../../../..');
const PROPS_DIR = path.join(ROOT, 'Content/Data/LevelGen/props');
const ACCEPTED_DIR = path.join(ROOT, 'Content/Data/LevelGen/accepted');
const BALANCE = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/Balance/props_balance.json'), 'utf-8')) as Record<string, unknown>;
const MAPS: [string, string][] = [
  ['L_Gen_Depot_X4', 'gen_ref_01_02_depot_1_x4'],
  ['L_Gen_Depot_1_X6', 'gen_ref_01_02_depot_1_x6'],
  ['L_Gen_Test_X2', 'gen_ref_01_02_depot_1_x2'],
];

function load(map: string): LevelPropsFile {
  const file = map === 'L_Gen_Depot_1_X6'
    ? path.join(PROPS_DIR, `${map}.props.json`)
    : new URL(`../fixtures/archivedMaps/${map}.props.json`, import.meta.url);
  return JSON.parse(fs.readFileSync(file, 'utf-8')) as LevelPropsFile;
}

function viewFor(layoutName: string): PropsLayoutView {
  const layout = JSON.parse(fs.readFileSync(path.join(ACCEPTED_DIR, `${layoutName}.json`), 'utf-8')) as Record<string, unknown>;
  return PropsLayoutView.build(layout, PropsLayoutView.readConfig(BALANCE));
}

function r(layerId: string, object: PropObject): PropsResolvedObject {
  return { layerId, object, bHand: false, bPinned: false };
}

const KINDS = new Map([
  ['turrets', 'turrets'],
  ['power', 'power'],
  ['loot', 'loot'],
]);

test('power: config reads presets.power_default', () => {
  const cfg = readPowerConfig(BALANCE);
  assert.equal(cfg.variant, 'generator_diesel');
  assert.equal(cfg.cableMaxCm, 4000);
  assert.equal(cfg.cableMinCm, 800);
  assert.deepEqual(cfg.sizeWeights, [
    [1, 3],
    [2, 4],
    [3, 2],
    [4, 1],
  ]);
  assert.equal(cfg.fuelTable, 'fuel_rare');
  assert.equal(cfg.startShares.running_hostile + cfg.startShares.running_neutral, 0.25);
});

test('power: start quotas are largest-remainder and leave ~75 % dark', () => {
  const cfg = readPowerConfig(BALANCE);
  const q20 = startQuotas(cfg, 20);
  assert.deepEqual(q20, { running_hostile: 3, running_neutral: 2, fuelled_off: 3, dry: 8, broken: 4 });
  for (const n of [1, 2, 3, 4, 7, 9, 10]) {
    const q = startQuotas(cfg, n);
    const sum = Object.values(q).reduce((a, b) => a + b, 0);
    assert.equal(sum, n, `quotas sum to ${n}`);
    assert.ok((q.running_hostile + q.running_neutral) * 2 <= n, `at most half running for ${n}`);
  }
  assert.deepEqual(startQuotas(cfg, 0), { running_hostile: 0, running_neutral: 0, fuelled_off: 0, dry: 0, broken: 0 });
});

test('power: network sizes follow the weights 3 / 4 / 2 / 1 (mean ~2.1)', () => {
  const cfg = readPowerConfig(BALANCE);
  const rng = new PropsRng(7);
  const counts = [0, 0, 0, 0, 0];
  for (let i = 0; i < 10000; i++) counts[drawNetworkSize(cfg, rng)]++;
  const mean = (counts[1] + 2 * counts[2] + 3 * counts[3] + 4 * counts[4]) / 10000;
  assert.ok(Math.abs(mean - 2.1) < 0.05, `mean ${mean}`);
  assert.equal(counts[0], 0);
});

for (const [map, layout] of MAPS) {
  test(`power: ${map} - cables in reach, every turret linked, fair, jerrycans, no warning`, () => {
    const file = load(map);
    const view = viewFor(layout);
    const kindOf = new Map(file.layers.map((l) => [l.id, l.kind as string]));
    const objects = applyOverrides(file).objects;
    assert.deepEqual(validatePowerLinks(objects, kindOf, BALANCE, view.spawnPos), [], 'no power warning');
    const power = file.layers.find((l) => l.kind === POWER_LAYER_ID);
    assert.ok(power && power.objects.length > 0, 'a power layer with generators');
    const turrets = objects.filter((o) => kindOf.get(o.layerId) === 'turrets');
    const live = turrets.filter((t) => t.object.powered === true);
    assert.ok(live.length * 2 <= turrets.length, `${live.length} of ${turrets.length} turrets live at start (~75 % dark)`);
    for (const g of power.objects) {
      assert.ok(Array.isArray(g.links) && g.links.length >= 1 && g.links.length <= 4, `${g.id} feeds 1-4 turrets`);
      for (const id of g.links ?? []) {
        const t = turrets.find((x) => x.object.id === id);
        assert.ok(t, `${g.id} -> ${id} exists`);
        const d = Math.hypot(t.object.pos[0] - g.pos[0], t.object.pos[1] - g.pos[1]);
        assert.ok(d >= 800 - 1 && d <= 4000, `${g.id} -> ${id} cable ${Math.round(d)} cm in [8, 40] m`);
      }
      assert.ok(view.isFree(g.pos, 'ground', 70), `${g.id} stands on free ground`);
    }
    const fuel = objects.filter((o) => kindOf.get(o.layerId) === 'loot' && o.object.loot_table === 'fuel_rare');
    // The designer-authored X6 props from c2d54ede omit the generated jerrycans.
    const expectedFuel = map === 'L_Gen_Depot_1_X6' ? 0 : Math.max(1, Math.round(0.4 * power.objects.length));
    assert.equal(fuel.length, expectedFuel, `${map}: designer or generator fuel count`);
  });
}

test('power: the build is deterministic (rebuilding X4 gives the committed layer)', () => {
  // The canonical writer adds "locked": false; key order does not matter.
  const norm = (o: PropObject) => {
    const copy: Record<string, unknown> = { ...o };
    if (copy.locked === false) delete copy.locked;
    return copy;
  };
  const committed = load('L_Gen_Depot_X4').layers.find((l) => l.kind === POWER_LAYER_ID)!.objects.map(norm);
  const rebuilt = load('L_Gen_Depot_X4');
  rebuilt.layers = rebuilt.layers.filter((l) => l.kind !== POWER_LAYER_ID);
  const result = buildPowerLayer(rebuilt, viewFor('gen_ref_01_02_depot_1_x4'), BALANCE, 1);
  assert.deepEqual(result.layer.objects.map(norm), committed);
  assert.equal(result.unlinked.length, 0);
});

test('power: validatePowerLinks - the same cases and reasons as the C++ validator', () => {
  const g1 = r('power', { id: 'g1', variant: 'generator_diesel', pos: [0, 0], walk_layer: 'ground', start: 'dry', links: ['t1', 't2'] });
  const t1 = r('turrets', { id: 't1', variant: 'turret_mg', pos: [1000, 0], walk_layer: 'ground', generator: 'g1' });
  const t2 = r('turrets', { id: 't2', variant: 'turret_mg', pos: [0, 3999], walk_layer: 'ground', generator: 'g1' });
  assert.deepEqual(validatePowerLinks([g1, t1, t2], KINDS, BALANCE), []);
  assert.deepEqual(validatePowerLinks([t1], KINDS, BALANCE), [], 'no generator on the map: no power checks');

  const g2 = r('power', {
    id: 'g2', variant: 'generator_diesel', pos: [0, 0], walk_layer: 'ground', start: 'humming', links: ['t1', 'far', 'crate', 't3', 't4'],
  });
  const far = r('turrets', { id: 'far', variant: 'turret_mg', pos: [4500, 0], walk_layer: 'ground' });
  const crate = r('loot', { id: 'crate', variant: 'loot_pile', pos: [100, 0], walk_layer: 'ground' });
  const t3 = r('turrets', { id: 't3', variant: 'turret_mg', pos: [0, 500], walk_layer: 'ground' });
  const t4 = r('turrets', { id: 't4', variant: 'turret_mg', pos: [0, -500], walk_layer: 'ground' });
  const t5 = r('turrets', { id: 't5', variant: 'turret_mg', pos: [9000, 9000], walk_layer: 'ground' });
  const t6 = r('turrets', { id: 't6', variant: 'turret_mg', pos: [9000, 0], walk_layer: 'ground', generator: 'g1' });
  const issues = validatePowerLinks([g1, t1, t2, g2, far, crate, t3, t4, t5, t6], KINDS, BALANCE);
  const has = (id: string, part: string) => issues.some((i) => i.objectId === id && i.reason.includes(part));
  assert.ok(has('g2', "unknown generator start 'humming'"));
  assert.ok(has('g2', 'feeds 5 turrets (max 4)'));
  assert.ok(has('g2', "cable to 'far' is 4500 cm (max 4000)"));
  assert.ok(has('g2', "link 'crate' is not a turret of the map"));
  assert.ok(has('t1', "turret linked by 'g1' and 'g2'"));
  assert.ok(has('t6', "generator 'g1' does not list this turret"));
  assert.ok(has('t5', 'no power line'));
  assert.ok(!issues.some((i) => i.objectId === 't3'), 'a linked turret without its own field is fine');

  const hostile = r('power', { id: 'g9', variant: 'generator_diesel', pos: [5000, 0], walk_layer: 'ground', start: 'running_hostile' });
  assert.ok(
    validatePowerLinks([hostile], KINDS, BALANCE, [0, 0]).some((i) => i.objectId === 'g9' && i.reason.includes('running hostile network within 6000 cm')),
  );
  assert.deepEqual(validatePowerLinks([hostile], KINDS, BALANCE, [20000, 20000]), []);
});
