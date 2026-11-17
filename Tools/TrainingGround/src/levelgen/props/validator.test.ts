// Training Ground / level generator: the static props validator (port of the C++ FPropsValidator, G003b 2026-11-17).
//
// Validates:
//   1. variantHalfSize matches FPropsValidator::GetVariantHalfSize on the real balance file.
//   2. The committed autogen props files of the generated maps (X2 / X4 / X6) pass the static validator: the game drops nothing
//      at load (before G003b the game dropped 7 per expanded map: Depot crates over Depot covers, mines in a stair / door zone).
//   3. The checks themselves: a crate in a doorway corridor, a mine at a ladder / stair end and a crate inside a cover are dropped,
//      a doorway's own mine is not, an object attached to a dropped crate goes with it.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import type { LevelPropsFile, PropObject } from './schema.ts';
import { PropsLayoutView } from './layoutView.ts';
import { applyOverrides, type PropsResolvedObject } from './placement.ts';
import { validateStatic, variantHalfSize } from './validator.ts';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const ROOT = path.resolve(__dirname, '../../../../..');
const ACCEPTED_DIR = path.join(ROOT, 'Content/Data/LevelGen/accepted');
const PROPS_DIR = path.join(ROOT, 'Content/Data/LevelGen/props');
const BALANCE = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/Balance/props_balance.json'), 'utf-8')) as Record<string, unknown>;

function viewFor(layoutName: string): PropsLayoutView {
  const layout = JSON.parse(fs.readFileSync(path.join(ACCEPTED_DIR, `${layoutName}.json`), 'utf-8')) as Record<string, unknown>;
  return PropsLayoutView.build(layout, PropsLayoutView.readConfig(BALANCE));
}

function resolved(layerId: string, object: PropObject): PropsResolvedObject {
  return { layerId, object, bHand: false, bPinned: false };
}

test('props validator: half sizes match the C++ (kinds radius / size, prefix defaults)', () => {
  assert.equal(variantHalfSize('crate_s', BALANCE), 60);
  assert.equal(variantHalfSize('crate_l', BALANCE), 80);
  assert.equal(variantHalfSize('barrel_explosive', BALANCE), 30);
  assert.equal(variantHalfSize('turret_mg', BALANCE), 50);
  assert.equal(variantHalfSize('loot_pile', BALANCE), 30);
  assert.equal(variantHalfSize('mine_tripwire', BALANCE), 0);
  assert.equal(variantHalfSize('crate_l'), 80, 'no balance: the prefix default');
});

for (const map of ['L_Gen_Test_X2', 'L_Gen_Depot_X4', 'L_Gen_Depot_1_X6']) {
  test(`props validator: the committed autogen file of ${map} passes (the game drops nothing at load)`, () => {
    const props = JSON.parse(fs.readFileSync(path.join(PROPS_DIR, `${map}.props.json`), 'utf-8')) as LevelPropsFile;
    const view = viewFor(props.layout);
    const res = applyOverrides(props);
    const result = validateStatic(view, res.objects, res.keepOuts, BALANCE);
    assert.deepEqual(result.issues, [], `${map}: ${result.issues.map((i) => `${i.objectId}: ${i.reason}`).join('; ')}`);
    assert.equal(result.valid.length, res.objects.length);
  });
}

test('props validator: doorway corridor, ladder / stair disk, cover overlap, own doorway mine, attach chain', () => {
  const view = viewFor('gen_ref_01_02_depot_1_x4');
  const door = view.doorways.find((d) => d.walkLayer === 'ground');
  assert.ok(door, 'a ground doorway');
  const stair = view.buildExclusions({ ladderCm: 100, stairCm: 100 }).find((e) => e.kind === 'stair' || e.kind === 'ladder_foot');
  assert.ok(stair, 'a stair / ladder end');
  const cover = view.getCovers()[0];
  assert.ok(cover, 'a cover');

  const objects: PropsResolvedObject[] = [
    resolved('crates', { id: 'c_door', variant: 'crate_s', pos: [door!.pos[0], door!.pos[1]], walk_layer: 'ground' }),
    resolved('mines', { id: 'm_door', variant: 'mine_tripwire', pos: [door!.pos[0], door!.pos[1]], walk_layer: 'ground', door: door!.id, trigger: 'tripwire' }),
    resolved('mines', { id: 'm_stair', variant: 'mine_proximity', pos: [stair!.points[0][0], stair!.points[0][1]], walk_layer: stair!.walkLayer || 'ground', trigger: 'proximity' }),
    resolved('crates', { id: 'c_cover', variant: 'crate_s', pos: [cover.pos[0], cover.pos[1]], walk_layer: 'ground' }),
    resolved('mines', { id: 'm_on_cover_crate', variant: 'mine_crate', pos: [cover.pos[0], cover.pos[1]], walk_layer: 'ground', attach: 'c_cover', trigger: 'crate_open' }),
  ];
  const result = validateStatic(view, objects, [], BALANCE);
  const reasons = new Map(result.issues.map((i) => [i.objectId, i.reason]));
  assert.match(reasons.get('c_door') ?? '', /blocked by layout geometry|clear corridor of doorway/, 'a crate in a doorway is dropped');
  assert.equal(reasons.has('m_door') && /clear corridor/.test(reasons.get('m_door')!), false, 'the doorway\'s own mine is not refused for its corridor');
  assert.match(reasons.get('m_stair') ?? '', /keep-out disk of ladder\/stair|blocked by layout geometry|not on its walk layer/, 'a mine at a stair end is dropped');
  assert.match(reasons.get('c_cover') ?? '', /blocked by layout geometry/, 'a crate inside a cover is dropped');
  assert.match(reasons.get('m_on_cover_crate') ?? '', /attach target 'c_cover' not found or dropped/, 'its crate-mine goes with it');
});
