// T-020: the current map of the level generator department - the default order, the map list from the level JSONs / props / accepted
// metas, and the persisted choice. Temp dirs only (never the designer's Content/Data files).
import { describe, it, beforeEach, afterEach } from 'node:test';
import * as assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { mapLabel, pickCurrentMap, type MapEntry } from './currentMap.ts';
import { listMapEntries, readCurrentMap, writeCurrentMap, type MapDirs } from './node/currentMapFiles.ts';

const entry = (map: string, acceptedAt = '', layout = ''): MapEntry => ({ map, layout, levelJson: true, propsId: map, acceptedAt });

describe('pickCurrentMap: the default order (T-020)', () => {
  const entries = [
    entry('L_Gen_Depot_1', '2026-11-02T06:30:58Z'),
    entry('L_Gen_Depot_1_X6', '2026-11-16T14:45:24Z'),
    entry('L_Gen_Depot_X4', '2026-11-16T13:21:32Z'),
    entry('L_Gen_Train_1'),
  ];

  it('1. the saved choice wins, even over a newer accepted layout', () => {
    assert.equal(pickCurrentMap(entries, 'L_Gen_Depot_1'), 'L_Gen_Depot_1');
    assert.equal(pickCurrentMap(entries, 'L_Gen_Train_1'), 'L_Gen_Train_1');
  });

  it('2. no saved choice: the most recently accepted layout\'s map', () => {
    assert.equal(pickCurrentMap(entries, null), 'L_Gen_Depot_1_X6');
    assert.equal(pickCurrentMap(entries, undefined), 'L_Gen_Depot_1_X6');
  });

  it('2b. a saved map that no longer exists is ignored (falls to the most recent)', () => {
    assert.equal(pickCurrentMap(entries, 'L_Gone'), 'L_Gen_Depot_1_X6');
  });

  it('2c. equal accept times: the map name decides, deterministically', () => {
    const same = [entry('B', '2026-11-16T00:00:00Z'), entry('A', '2026-11-16T00:00:00Z')];
    assert.equal(pickCurrentMap(same, null), 'A');
  });

  it('3. nothing was ever accepted: the first map by name; no maps: null', () => {
    assert.equal(pickCurrentMap([entry('L_B'), entry('L_A')], null), 'L_A');
    assert.equal(pickCurrentMap([], 'anything'), null);
  });

  it('the selector label shows the map and its layout', () => {
    assert.equal(mapLabel(entry('L_Gen_Depot_1_X6', '', 'gen_ref_01_02_depot_1_x6')), 'L_Gen_Depot_1_X6  ←  gen_ref_01_02_depot_1_x6');
    assert.equal(mapLabel(entry('L_Lone')), 'L_Lone');
  });
});

describe('listMapEntries / the persisted choice (T-020)', () => {
  let tmp = '';
  let dirs: MapDirs;
  const write = (file: string, data: unknown) => {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, typeof data === 'string' ? data : JSON.stringify(data));
  };
  beforeEach(() => {
    tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'tg-currentmap-'));
    dirs = { levelJson: path.join(tmp, 'LevelJson'), props: path.join(tmp, 'props'), accepted: path.join(tmp, 'accepted') };
    const mapAssets = path.join(tmp, 'Maps');
    dirs.mapAssets = mapAssets;
    write(path.join(mapAssets, 'Generated/L_Orphan.umap'), 'asset marker');
    // X6: level JSON + props + accepted meta; Depot: level JSON + props, older meta; Hand: a level JSON without a layout (not a generated map);
    // Orphan: a props file only; a fixture and a non-props json in props/ are not maps
    write(path.join(dirs.levelJson, 'L_X6.json'), { level_id: 'L_X6', levelgen_layout: 'Content/Data/LevelGen/accepted/lay_x6.json' });
    write(path.join(dirs.levelJson, 'L_Depot.json'), { level_id: 'L_Depot', levelgen_layout: 'Content\\Data\\LevelGen\\accepted\\lay_depot.json' });
    write(path.join(dirs.levelJson, 'stage_01.json'), { level_id: 'stage_01' });
    write(path.join(dirs.levelJson, 'broken.json'), '{ not json');
    write(path.join(dirs.props, 'L_X6.props.json'), { schema: 'codex.levelgen_props/1', map: 'L_X6', layout: 'lay_x6', layers: [] });
    write(path.join(dirs.props, 'L_Depot.props.json'), { schema: 'codex.levelgen_props/1', map: 'L_Depot', layout: 'lay_depot', layers: [] });
    write(path.join(dirs.props, 'L_Orphan.props.json'), { schema: 'codex.levelgen_props/1', map: 'L_Orphan', layout: 'lay_orphan', layers: [] });
    write(path.join(dirs.props, 'L_Gen_Bgen_Test_X6.props.json'), { schema: 'codex.levelgen_props/1', map: 'L_Gen_Bgen_Test_X6', layout: 'stale' });
    write(path.join(dirs.props, 'notes.props.json'), { schema: 'something/else' });
    write(path.join(dirs.props, 'fixtures', 'golden_a.props.json'), { schema: 'codex.levelgen_props/1', map: 'Golden' });
    write(path.join(dirs.accepted, 'lay_x6.meta.json'), { at: '2026-11-16T14:45:24.205Z' });
    write(path.join(dirs.accepted, 'lay_depot.meta.json'), { at: '2026-11-02T06:30:58.260Z' });
  });
  afterEach(() => { fs.rmSync(tmp, { recursive: true, force: true }); });

  it('lists level JSON layouts and real props-only map assets; omits orphan props and fixtures', () => {
    const list = listMapEntries(dirs);
    assert.deepEqual(list.map((e) => e.map), ['L_Depot', 'L_Orphan', 'L_X6']);
    const x6 = list.find((e) => e.map === 'L_X6')!;
    assert.deepEqual(x6, { map: 'L_X6', layout: 'lay_x6', levelJson: true, propsId: 'L_X6', acceptedAt: '2026-11-16T14:45:24.205Z' });
    assert.equal(list.find((e) => e.map === 'L_Depot')!.layout, 'lay_depot', 'a Windows-style layout path');
    const orphan = list.find((e) => e.map === 'L_Orphan')!;
    assert.equal(orphan.levelJson, false);
    assert.equal(orphan.layout, 'lay_orphan', 'the layout comes from the props file');
    assert.equal(orphan.acceptedAt, '', 'never accepted');
  });

  it('end to end: nothing saved -> the most recently accepted; save a choice -> it survives; a removed map falls back', () => {
    const file = path.join(tmp, 'Saved', 'current_map.json');
    assert.equal(readCurrentMap(file), null);
    assert.equal(pickCurrentMap(listMapEntries(dirs), readCurrentMap(file)), 'L_X6');
    assert.equal(writeCurrentMap('L_Depot', file), true);
    assert.equal(readCurrentMap(file), 'L_Depot');
    assert.equal(pickCurrentMap(listMapEntries(dirs), readCurrentMap(file)), 'L_Depot');
    fs.rmSync(path.join(dirs.levelJson, 'L_Depot.json'));
    fs.rmSync(path.join(dirs.props, 'L_Depot.props.json'));
    assert.equal(pickCurrentMap(listMapEntries(dirs), readCurrentMap(file)), 'L_X6', 'the saved map is gone');
    assert.equal(pickCurrentMap(listMapEntries(dirs), 'L_Gen_Bgen_Test_X6'), 'L_X6', 'a props-only leftover cannot restore the stale choice');
  });

  it('writing the same choice again writes nothing; a damaged file reads as no choice', () => {
    const file = path.join(tmp, 'Saved', 'current_map.json');
    assert.equal(writeCurrentMap('L_X6', file), true);
    const mtime = fs.statSync(file).mtimeMs;
    assert.equal(writeCurrentMap('L_X6', file), false);
    assert.equal(fs.statSync(file).mtimeMs, mtime);
    assert.deepEqual(fs.readdirSync(path.dirname(file)), ['current_map.json'], 'no temp file left');
    fs.writeFileSync(file, '{ damaged');
    assert.equal(readCurrentMap(file), null);
  });

  it('a missing directory set is an empty list, not an error', () => {
    assert.deepEqual(listMapEntries({ levelJson: path.join(tmp, 'nope1'), props: path.join(tmp, 'nope2'), accepted: path.join(tmp, 'nope3') }), []);
  });
});
