// Combat lab map selector: the map list, id validation, the generated layout preview, presets per map (node --test; `npm test`).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { LAB_MAP_ID, LAB_MAP_PATH, acceptedLayoutPath, buildMapList, layoutToPreview, presetForSave, presetSettingsForMap, resolveMap, withoutAmbushPoints } from './maps.ts';
import { getAt, matchingPreset } from './pressureSettings.ts';
import type { CombatLabPreset, CombatLabPresetFile, CombatLabSettings } from './pressureSettings.ts';

const route = (name: string, n: number) => ({ bot_route: { name, loop: true, points: Array.from({ length: n }, (_, i) => ({ at: [i, i] })) } });

test('buildMapList: the lab first, generated maps only with a bot_route object', () => {
  const maps = buildMapList(route('lab loop', 21), [
    { leaf: 'L_Gen_Zeta', level: route('zeta', 3) },
    { leaf: 'L_Gen_Depot_1', level: route('depot', 9) },
    { leaf: 'L_Gen_NoRoute', level: { level_id: 'x' } },
    { leaf: 'L_Gen_NoJson', level: null },
    { leaf: 'L_Gen_Bad', level: { bot_route: [] } },
    { leaf: '../evil', level: route('evil', 1) },
    { leaf: LAB_MAP_ID, level: route('dup', 1) },
  ]);
  assert.deepEqual(maps.map((m) => m.id), [LAB_MAP_ID, 'L_Gen_Depot_1', 'L_Gen_Zeta']);
  assert.deepEqual(maps[0], { id: LAB_MAP_ID, map: LAB_MAP_PATH, levelFile: 'Content/Data/LevelJson/L_FormationLabCombat.json', kind: 'lab', hasRoute: true, routeName: 'lab loop', routePoints: 21 });
  assert.deepEqual(maps[1], { id: 'L_Gen_Depot_1', map: '/Game/Maps/Generated/L_Gen_Depot_1', levelFile: 'Content/Data/LevelJson/L_Gen_Depot_1.json', kind: 'generated', hasRoute: true, routeName: 'depot', routePoints: 9 });
});

test('buildMapList: the lab is listed even when its level is unreadable', () => {
  const maps = buildMapList(null, []);
  assert.equal(maps.length, 1);
  assert.equal(maps[0].hasRoute, false);
});

test('resolveMap: missing = the lab, unknown / malformed ids are errors', () => {
  const maps = buildMapList(route('lab', 2), [{ leaf: 'L_Gen_Depot_1', level: route('d', 2) }]);
  assert.equal(resolveMap(maps, undefined).map?.id, LAB_MAP_ID);
  assert.equal(resolveMap(maps, '').map?.id, LAB_MAP_ID);
  assert.equal(resolveMap(maps, 'L_Gen_Depot_1').map?.kind, 'generated');
  for (const bad of ['L_Gen_Other', '../L_Gen_Depot_1', 'L_Gen_Depot_1/..', 'a b', 5, {}, 'x'.repeat(200)]) assert.ok(resolveMap(maps, bad).error, String(bad));
});

test('acceptedLayoutPath: only files under the accepted folder', () => {
  assert.equal(acceptedLayoutPath({ levelgen_layout: 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json' }), 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json');
  assert.equal(acceptedLayoutPath({ levelgen_layout: 'Content/Data/LevelGen/accepted/../../../secret.json' }), null);
  assert.equal(acceptedLayoutPath({ levelgen_layout: 'C:/Windows/x.json' }), null);
  assert.equal(acceptedLayoutPath({ levelgen_layout: 'Content/Data/LevelGen/other/x.json' }), null);
  assert.equal(acceptedLayoutPath({}), null);
  assert.equal(acceptedLayoutPath(null), null);
});

test('layoutToPreview: walls, building footprints, ladders, spawn, bounds; broken entries dropped', () => {
  const p = layoutToPreview({
    schema: 'codex.levelgen_layout/1', bounds: { min: [-6200, -8850], max: [6200, 8850] },
    walls: [{ id: 'W1', a: [0, 0], b: [100, 0], thickness: 150, height: 220, kind: 'cut_wall' }, { id: 'W2', a: [0, 0] }, { a: [1, 1], b: [2, 2] }],
    buildings: [{ id: 'B1', footprint: [[300, 200], [100, -50]], floors: 3, set_piece: 'facade' }, { id: 'B2', footprint: [[0, 0]] }],
    ladders: [{ id: 'LAD', pos: [5, 6], yaw: 180 }, { id: 'bad' }],
    spawn: { pos: [10, 20], yaw: 90 },
  });
  assert.deepEqual(p.bounds, { min: [-6200, -8850], max: [6200, 8850] });
  assert.deepEqual(p.walls, [{ id: 'W1', a: [0, 0], b: [100, 0], thickness: 150 }, { id: 'wall_2', a: [1, 1], b: [2, 2] }]);
  assert.deepEqual(p.buildings, [{ id: 'B1', min: [100, -50], max: [300, 200], floors: 3 }]);
  assert.deepEqual(p.ladders, [{ id: 'LAD', pos: [5, 6] }]);
  assert.deepEqual(p.spawn, { pos: [10, 20] });
  assert.deepEqual(p.towers, []);
  assert.deepEqual(layoutToPreview(null), { bounds: null, walls: [], towers: [], spawn: null, buildings: [], ladders: [] });
});

const PRESET: CombatLabPreset = {
  id: 'ambush_heavy', name: 'Ambush heavy', pressure: { seed: 5, ambush: { share: 0.7, points: [[1, 1], [2, 2]] } }, cheats: { immortal: true, infinite_ammo: false },
};
const settingsWith = (points?: number[][]): CombatLabSettings => ({ pressure: { seed: 9, ambush: points ? { share: 0.1, points } : { share: 0.1 } }, cheats: { immortal: false, infinite_ammo: false } });

test('presetSettingsForMap: the lab takes the preset points, another map keeps its own', () => {
  const lab = presetSettingsForMap(PRESET, settingsWith([[9, 9]]), LAB_MAP_ID);
  assert.deepEqual(getAt(lab.pressure, 'ambush.points'), [[1, 1], [2, 2]]);
  const gen = presetSettingsForMap(PRESET, settingsWith([[9, 9]]), 'L_Gen_Depot_1');
  assert.deepEqual(getAt(gen.pressure, 'ambush.points'), [[9, 9]]);
  assert.equal(getAt(gen.pressure, 'ambush.share'), 0.7);
  assert.equal(getAt(gen.pressure, 'seed'), 5);
  assert.equal(gen.cheats.immortal, true);
  const none = presetSettingsForMap(PRESET, settingsWith(), 'L_Gen_Depot_1');
  assert.equal(getAt(none.pressure, 'ambush.points'), undefined);
  // The preset is not mutated / aliased.
  (getAt(gen.pressure, 'ambush.points') as number[][])[0][0] = 0;
  assert.deepEqual(PRESET.pressure, { seed: 5, ambush: { share: 0.7, points: [[1, 1], [2, 2]] } });
});

test('presetSettingsForMap: a preset without points never wipes the lab points', () => {
  const stripped: CombatLabPreset = presetForSave(PRESET, 'L_Gen_Depot_1');
  const lab = presetSettingsForMap(stripped, settingsWith([[9, 9]]), LAB_MAP_ID);
  assert.deepEqual(getAt(lab.pressure, 'ambush.points'), [[9, 9]]);
});

test('presetForSave: generated maps store no ambush.points, the lab keeps them', () => {
  assert.equal(presetForSave(PRESET, LAB_MAP_ID), PRESET);
  const gen = presetForSave(PRESET, 'L_Gen_Depot_1');
  assert.equal(getAt(gen.pressure, 'ambush.points'), undefined);
  assert.equal(getAt(gen.pressure, 'ambush.share'), 0.7);
  assert.deepEqual(getAt(PRESET.pressure, 'ambush.points'), [[1, 1], [2, 2]]);
  assert.deepEqual(withoutAmbushPoints({ seed: 1 }), { seed: 1 });
});

test('matchingPreset: ambush.points are ignored on non-lab maps, compared on the lab', () => {
  const file: CombatLabPresetFile = { schema: 'codex.combatlab_presets/1', presets: [PRESET] };
  const own = presetSettingsForMap(PRESET, settingsWith([[9, 9]]), 'L_Gen_Depot_1');
  assert.equal(matchingPreset(file, own), undefined);
  assert.equal(matchingPreset(file, own, 'L_Gen_Depot_1'), 'ambush_heavy');
  const labSame = presetSettingsForMap(PRESET, settingsWith([[9, 9]]), LAB_MAP_ID);
  assert.equal(matchingPreset(file, labSame), 'ambush_heavy');
  // A preset saved without points matches the lab whatever its points are.
  const savedFile: CombatLabPresetFile = { schema: file.schema, presets: [presetForSave(PRESET, 'L_Gen_Depot_1')] };
  assert.equal(matchingPreset(savedFile, own), 'ambush_heavy');
});
