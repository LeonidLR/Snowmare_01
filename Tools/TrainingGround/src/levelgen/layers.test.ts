// Training Ground / level generator - the LAYER system (layers.ts + Content/Data/LevelGen/layers.json): every entity in exactly one
// layer on every shipped map (old files load), the obstacle size-class rule, visibility / solo, edits scoped to the active layer
// (layout + props overrides), the re-roll limited to obstacle layers. Nothing is written into the repo: the props edit is saved to
// a temp folder and the designer's files are compared byte for byte before / after. `npm test`.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import type { Vec2 } from '../movement/geometry.ts';
import { pointInPoly } from '../movement/geometry.ts';
import { geometryHash } from './chokepoints.ts';
import { generateLayout } from './generate.ts';
import { expandLayout } from './expand.ts';
import type { AddTemplate, LayerAssignment, LayersConfig } from './layers.ts';
import { analysisLayerCounts, assignLayers, assignMap, coverFootprintM2, defaultVisibility, editLayout, editProps, hiddenKeys, isSolo, layerById, layerCounts, LAYERS, layerTints, layoutEntities, matchingLayers,
  mergeLayerReroll, pickEntity, propEntities, propSizeCm, resolveProps, sizeClassOf, sizeClassRuleText, soloVisibility, validateLayersConfig, wallFootprintM2 } from './layers.ts';
import { placeObjectives } from './objectives.ts';
import type { Profile } from './profile.ts';
import { formatPropsCanonical } from './props/canonical.ts';
import type { LevelPropsFile } from './props/schema.ts';
import { validateProps } from './props/schema.ts';
import { regenerateRegion } from './regionRegen.ts';
import type { GenLayout } from './schema.ts';
import { validateGenLayout } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const LG = path.join(ROOT, 'Content/Data/LevelGen');
const readJson = <T>(f: string): T => JSON.parse(fs.readFileSync(f, 'utf8')) as T;
const sha = (f: string) => crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
const profile = readJson<Profile>(path.join(LG, 'refs/ref_01_02_depot.json'));
const base = generateLayout({ profile, seed: 1 });
const ACCEPTED = fs.readdirSync(path.join(LG, 'accepted')).filter((f) => f.endsWith('.json') && !f.endsWith('.meta.json'));
const DEPOT_PROPS = path.join(LG, 'props/L_Gen_Depot_1.props.json');
const ZONE_NW: Vec2[] = [[500, -9000], [6200, -9000], [6200, 0], [500, 0]];
const tpl = (layer: string, label: string): AddTemplate => layerById(layer)!.add!.find((t) => t.label === label)!;
const FREE_WALL_KINDS = ['wall', 'fence', 'curb'];

test('layers config: valid; the ten layers of the designer + the fire line + the chokepoints + the room plans (S2.7), in his order', () => {
  assert.deepEqual(validateLayersConfig(LAYERS), []);
  const main = LAYERS.layers.filter((l) => l.group === 'main').map((l) => l.name);
  assert.deepEqual(main, ['Дороги', 'Здания / структуры', 'Мосты', 'Преграды крупные', 'Преграды средние', 'Преграды мелкие', 'Мины', 'Лут', 'Особые предметы', 'Цели']);
  assert.deepEqual(LAYERS.layers.filter((l) => l.group === 'analysis').map((l) => l.id), ['fire_lines', 'chokepoints', 'plans']);
  assert.deepEqual(LAYERS.layers.filter((l) => l.group === 'threats').map((l) => l.id), ['threat_drop', 'threat_windows', 'threat_overwatch', 'threat_hiding']);
  // a broken config is reported, not swallowed
  const bad = JSON.parse(JSON.stringify(LAYERS)) as LayersConfig;
  const blocks = bad.layers.find((l) => l.id === 'blocks')!;
  bad.layers[1].id = bad.layers[0].id; bad.layers[2].color = 'red'; bad.layers.push({ ...blocks, id: 'blocks2' });
  const errs = validateLayersConfig(bad);
  assert.ok(errs.some((e) => e.includes('duplicate')) && errs.some((e) => e.includes('#rrggbb')) && errs.some((e) => e.includes('two fallback')), errs.join('; '));
});

test('layers assignment: every entity of every shipped map is in exactly one layer; old files load unchanged', () => {
  const maps: [string, GenLayout][] = ACCEPTED.map((f) => [f, readJson<GenLayout>(path.join(LG, 'accepted', f))]);
  maps.push(['fixture L_Gen_Test_X2', readJson<GenLayout>(path.join(LG, 'TestFixtures/L_Gen_Test_X2.layout.json'))]);
  maps.push(['generated depot seed 1', base]);
  const rr = regenerateRegion({ layout: base, zone: ZONE_NW, seed: 303, profile, blocks: 'full', chaos: 1, maxRotationDeg: 30 });
  assert.ok(rr.ok);
  maps.push(['depot full re-roll (rotated)', rr.layout]);
  const ex = expandLayout({ layout: base, factor: 2, seed: 7, profile });
  assert.ok(ex.ok);
  maps.push(['depot x2 + goals', placeObjectives({ layout: ex.layout, seed: 1 }).layout]);
  assert.ok(maps.length >= 9);
  for (const [name, l] of maps) {
    // an old file has no analysis block; the refreshed accepted x4 of the firing-gaps branch carries a current one (add-firing-gaps.ts)
    if (name === 'gen_ref_01_02_depot_1_x4.json' && l.analysis) assert.equal(l.analysis.geometry_hash, geometryHash(l), `${name}: its analysis is current`);
    else assert.equal(l.analysis, undefined, `${name}: an old file has no analysis block`);
    const entities = layoutEntities(l);
    assert.equal(new Set(entities.map((e) => e.key)).size, entities.length, `${name}: entity keys are unique`);
    for (const e of entities) assert.equal(matchingLayers(e).length, 1, `${name}: ${e.key} matches ${matchingLayers(e).join(' + ') || 'nothing'}`);
    const a = assignLayers(entities);
    assert.deepEqual(a.problems, [], name);
    assert.equal(Object.values(a.byLayer).reduce((s, x) => s + x.length, 0), entities.length, `${name}: nothing lost, nothing twice`);
    assert.equal(Object.keys(a.layerOf).length, entities.length);
    // the entity census: every array of the file is covered
    const mantleIds = new Set([...(l.mantle_edges ?? []), ...l.buildings.flatMap((b) => b.mantle_edges ?? [])].map((m) => m.id));
    const n = l.zones.length + l.buildings.length + l.walls.length + l.covers.length + (l.cut ? 1 + l.cut.crossings.length : 0) + l.ladders.length + l.routes.length + 2 + l.spawn_points.length
      + l.ambush_points.length + l.patrol_routes.length + mantleIds.size + (l.objectives ? 1 + l.objectives.goals.length : 0);
    assert.equal(entities.length, n, `${name}: every entity of the file is enumerated`);
    // what the designer's layers hold
    assert.ok(a.byLayer.roads.every((e) => e.type === 'zone' && (e.id.startsWith('Z_ST_') || e.set_piece === 'street')), `${name}: roads = street zones only`);
    // gen_bgen_plans_check = the S2.3 engine fixture, gen_story_showcase = the story showcase (houses on open ground, a mandatory route but no
    // street zones): no streets by design
    if (!name.startsWith('gen_bgen_plans_check') && !name.startsWith('gen_story_showcase')) assert.ok(a.byLayer.roads.length >= 1, `${name}: has roads`);
    assert.equal(a.byLayer.buildings.filter((e) => e.type === 'building').length, l.buildings.length);
    assert.equal(a.byLayer.bridges.length, l.cut?.crossings.length ?? 0);
    assert.equal(a.byLayer.objectives.length, l.objectives ? 1 + l.objectives.goals.length : 0);
    const obstacles = [...a.byLayer.obstacles_large, ...a.byLayer.obstacles_medium, ...a.byLayer.obstacles_small];
    const freeWalls = l.walls.filter((w) => FREE_WALL_KINDS.includes(w.kind ?? 'wall')).length;
    assert.equal(obstacles.length, l.covers.length + freeWalls, `${name}: every cover / fence / free wall has a size class`);
    for (const id of ['mines', 'loot', 'special']) assert.equal(a.byLayer[id].length, 0, `${name}: no props file given -> the props layers are empty`);
  }
  // an accepted file still validates (the schema only gained an optional block)
  const depot = readJson<GenLayout>(path.join(LG, 'accepted/gen_ref_01_02_depot_1.json'));
  assert.deepEqual(validateGenLayout(depot), []);
});

test('layers size classes: the rule (height + footprint from layers.json), every obstacle of the Depot maps, the text shown in the panel', () => {
  const r = LAYERS.size_classes;
  assert.deepEqual([r.small_max_height_cm, r.large_min_height_cm, r.large_min_footprint_m2], [105, 230, 20]);
  // heights: the mantle classes 85 / 95 (small), 135 / 140 (medium), 230 / 310 / 330 (large)
  for (const [h, want] of [[30, 'small'], [85, 'small'], [95, 'small'], [105, 'small'], [106, 'medium'], [135, 'medium'], [140, 'medium'], [229, 'medium'], [230, 'large'], [310, 'large'], [330, 'large'], [1200, 'large']] as const)
    assert.equal(sizeClassOf(h, 1), want, `height ${h}`);
  // footprint: big and taller than small = large; big but low stays small (a low platform is vaulted)
  assert.equal(sizeClassOf(140, 19.9), 'medium');
  assert.equal(sizeClassOf(140, 20), 'large');
  assert.equal(sizeClassOf(95, 64), 'small');
  // a kind override wins
  assert.equal(sizeClassOf(95, 1, 'jersey', { ...r, kind_class: { jersey: 'large' } }), 'large');
  // the layout's real objects
  const x4 = readJson<GenLayout>(path.join(LG, 'accepted/gen_ref_01_02_depot_1_x4.json'));
  const a = assignMap(x4);
  const kindsOf = (layer: string) => [...new Set(a.byLayer[layer].map((e) => `${e.type}:${e.kind}:${e.height_cm}`))].sort();
  assert.deepEqual(kindsOf('obstacles_small'), ['cover:barrel:95', 'cover:crate:85', 'cover:jersey:95', 'cover:sandbag:95']);
  assert.deepEqual(kindsOf('obstacles_medium'), ['cover:boiler:140', 'cover:wreck:140', 'wall:fence:135']);
  assert.deepEqual(kindsOf('obstacles_large'), ['cover:silo:1200', 'wall:wall:330']);
  for (const id of ['obstacles_large', 'obstacles_medium', 'obstacles_small'] as const) {
    const c = layerById(id)!.size_class!;
    for (const e of a.byLayer[id]) assert.equal(sizeClassOf(e.height_cm!, e.footprint_m2!, e.kind), c);
  }
  assert.equal(wallFootprintM2({ a: [0, 0], b: [1000, 0], thickness: 20 }), 2);
  assert.equal(coverFootprintM2({ size: [420, 180, 140] }), 7.56);
  // the panel text carries the config's numbers
  assert.ok(sizeClassRuleText('small').includes('105') && sizeClassRuleText('large').includes('230') && sizeClassRuleText('large').includes('20') && sizeClassRuleText('medium').includes('106–229'));
  // prop variants take their size from props_balance.json
  assert.deepEqual(propSizeCm('crate_l'), [160, 160, 140]);
  assert.deepEqual(propSizeCm('barrel'), [60, 60, 90]);
  assert.equal(propSizeCm('mine'), null);
});

test('layers props: the Depot props file feeds the layers (crates, barrels); mines / loot / turrets fixtures; hand overrides are applied', () => {
  const before = sha(DEPOT_PROPS);
  const file = readJson<LevelPropsFile>(DEPOT_PROPS);
  const a = assignLayers(propEntities(file));
  assert.deepEqual(a.problems, []);
  assert.equal(a.byLayer.special.length, 8, 'the 8 explosive barrels are special items');
  assert.equal(a.byLayer.obstacles_medium.length, 53, 'crate_s (110 cm) and crate_l (140 cm) are medium obstacles');
  assert.equal(a.byLayer.mines.length + a.byLayer.loot.length, 0);
  for (const [fixture, layer] of [['golden_small_mines', 'mines'], ['golden_depot_loot_s1', 'loot'], ['golden_small_turrets', 'special']] as const) {
    const f = readJson<LevelPropsFile>(path.join(LG, 'props/fixtures', `${fixture}.json`));
    const fa = assignLayers(propEntities(f));
    assert.deepEqual(fa.problems, [], fixture);
    assert.ok(fa.byLayer[layer].length >= 1, `${fixture}: ${layer} has ${fa.byLayer[layer].length}`);
  }
  // overrides: delete hides, move moves, add adds
  const id0 = file.layers[0].objects[0].id, id1 = file.layers[0].objects[1].id;
  const edited: LevelPropsFile = { ...file, overrides: [{ op: 'delete', id: id0, pos: file.layers[0].objects[0].pos }, { op: 'move', id: id1, pos: [111, 222], walk_layer: 'ground' },
    { op: 'add', object: { id: 'hand_barrels_0', origin: 'hand', variant: 'barrel_explosive', pos: [5, 6], walk_layer: 'ground' } }] };
  const res = resolveProps(edited);
  assert.ok(!res.some((x) => x.object.id === id0));
  assert.deepEqual(res.find((x) => x.object.id === id1)!.object.pos, [111, 222]);
  assert.equal(res.find((x) => x.object.id === 'hand_barrels_0')!.kind, 'barrels');
  assert.equal(res.length, resolveProps(file).length);
  assert.equal(sha(DEPOT_PROPS), before, 'the props file of the designer was only read');
});

test('layers visibility: hidden layers give exactly their keys; solo; counts with the analysis overlays; tints for the size classes', () => {
  const a = assignMap(base);
  const all = defaultVisibility();
  assert.equal(hiddenKeys(a, all).size, 0);
  assert.equal(defaultVisibility(LAYERS, ['threat_hiding']).threat_hiding, false);
  const v = { ...all, obstacles_small: false, roads: false };
  const hidden = hiddenKeys(a, v);
  assert.equal(hidden.size, a.byLayer.obstacles_small.length + a.byLayer.roads.length);
  for (const e of [...a.byLayer.obstacles_small, ...a.byLayer.roads]) assert.ok(hidden.has(e.key));
  for (const e of a.byLayer.obstacles_medium) assert.ok(!hidden.has(e.key));
  const solo = soloVisibility('buildings');
  assert.ok(isSolo(solo, 'buildings') && !isSolo(all, 'buildings'));
  assert.equal(hiddenKeys(a, solo).size, layoutEntities(base).length - a.byLayer.buildings.length);
  const counts = layerCounts(a, { fire_line: 7, chokepoint: 3, hiding: 5 });
  assert.deepEqual([counts.fire_lines, counts.chokepoints, counts.threat_hiding, counts.threat_drop], [7, 3, 5, 0]);
  assert.equal(counts.obstacles_small, a.byLayer.obstacles_small.length);
  assert.deepEqual(analysisLayerCounts({ window: 2, plan: 4 }), { fire_lines: 0, chokepoints: 0, plans: 4, threat_drop: 0, threat_windows: 2, threat_overwatch: 0, threat_hiding: 0 });
  const tints = layerTints(a);
  assert.equal(tints.get(a.byLayer.obstacles_large[0].key), layerById('obstacles_large')!.color);
  assert.equal(tints.size, a.byLayer.obstacles_large.length + a.byLayer.obstacles_medium.length + a.byLayer.obstacles_small.length);
});

test('layers editing: the tools act on the ACTIVE layer only (pick, move, rotate, delete, add); a building on a route is kept', () => {
  const a = assignMap(base);
  const small = a.byLayer.obstacles_small.find((e) => e.type === 'cover')!, medium = a.byLayer.obstacles_medium.find((e) => e.type === 'cover')!;
  // pick: only the active layer answers
  assert.equal(pickEntity(a, 'obstacles_small', small.pos, 30)?.key, small.key);
  assert.equal(pickEntity(a, 'obstacles_medium', small.pos, 5), null, 'a small cover is not picked on the medium layer');
  assert.equal(pickEntity(a, 'roads', a.byLayer.roads[0].pos, 30)?.key, a.byLayer.roads[0].key);
  // move on its own layer
  const mv = editLayout(base, 'obstacles_small', { op: 'move', key: small.key, delta: [100, -50] });
  assert.ok(mv.ok, mv.message);
  const was = base.covers.find((c) => c.id === small.id)!, now = mv.layout.covers.find((c) => c.id === small.id)!;
  assert.deepEqual(now.pos, [Math.round(was.pos[0] + 100), Math.round(was.pos[1] - 50)]);
  assert.equal(mv.layout.covers.length, base.covers.length);
  assert.deepEqual(mv.layout.walls, base.walls);
  assert.notEqual(mv.layout, base);
  // ... refused from another layer, and nothing changes
  for (const edit of [{ op: 'move', key: small.key, delta: [100, 0] }, { op: 'delete', key: small.key }, { op: 'rotate', key: small.key, deg: 90 }] as const) {
    const no = editLayout(base, 'obstacles_medium', edit);
    assert.ok(!no.ok && no.layout === base && no.message.includes('Преграды мелкие'), no.message);
  }
  assert.ok(!editLayout(base, 'roads', { op: 'move', key: a.byLayer.roads[0].key, delta: [1, 1] }).ok, 'roads have no move tool');
  assert.ok(!editLayout(base, 'fire_lines', { op: 'delete', key: small.key }).ok);
  // rotate + delete
  const rot = editLayout(base, 'obstacles_medium', { op: 'rotate', key: medium.key, deg: 90 });
  assert.equal(rot.layout.covers.find((c) => c.id === medium.id)!.yaw, (base.covers.find((c) => c.id === medium.id)!.yaw + 90) % 360);
  const del = editLayout(base, 'obstacles_medium', { op: 'delete', key: medium.key });
  assert.ok(del.ok && del.layout.covers.length === base.covers.length - 1 && !del.layout.covers.some((c) => c.id === medium.id));
  // a free-standing fence moves as a whole
  const fence = [...a.byLayer.obstacles_medium, ...a.byLayer.obstacles_large].find((e) => e.type === 'wall')!;
  const fm = editLayout(base, a.layerOf[fence.key], { op: 'move', key: fence.key, delta: [30, 40] });
  const w0 = base.walls.find((w) => w.id === fence.id)!, w1 = fm.layout.walls.find((w) => w.id === fence.id)!;
  assert.deepEqual([w1.a, w1.b], [[Math.round(w0.a[0] + 30), Math.round(w0.a[1] + 40)], [Math.round(w0.b[0] + 30), Math.round(w0.b[1] + 40)]]);
  // add: a template of the layer; a template of another class is refused
  const add = editLayout(base, 'obstacles_small', { op: 'add', template: tpl('obstacles_small', 'бочка'), pos: [1234.4, -567.6] });
  assert.ok(add.ok && add.key === 'cover:COV_user_1', add.message);
  assert.deepEqual(add.layout.covers.at(-1), { id: 'COV_user_1', pos: [1234, -568], yaw: 0, size: [60, 60, 95], kind: 'barrel' });
  assert.equal(assignMap(add.layout).layerOf['cover:COV_user_1'], 'obstacles_small');
  assert.ok(!editLayout(base, 'obstacles_small', { op: 'add', template: tpl('obstacles_medium', 'остов машины'), pos: [0, 0] }).ok);
  const addWall = editLayout(base, 'obstacles_large', { op: 'add', template: tpl('obstacles_large', 'стена 3,3 м (4 м)'), pos: [2500, -6000] });
  assert.ok(addWall.ok && assignMap(addWall.layout).layerOf[addWall.key!] === 'obstacles_large');
  assert.deepEqual(validateGenLayout(add.layout), validateGenLayout(base));
  // roads: select + delete; buildings: a building on the route is kept, a free one goes with its walls and ladders
  const road = a.byLayer.roads[0];
  const rd = editLayout(base, 'roads', { op: 'delete', key: road.key });
  assert.ok(rd.ok && rd.layout.zones.length === base.zones.length - 1 && assignMap(rd.layout).byLayer.roads.length === a.byLayer.roads.length - 1);
  const onRoute = editLayout(base, 'buildings', { op: 'delete', key: 'building:B_ruin' });
  assert.ok(!onRoute.ok && onRoute.message.includes('маршруте'), onRoute.message);
  const used = new Set(base.routes.flatMap((r) => r.layers ?? []).map((x) => x.split(':')[1]).filter(Boolean));
  const free = base.buildings.find((b) => !used.has(b.id) && base.ladders.some((k) => k.to === `roof:${b.id}`)) ?? base.buildings.find((b) => !used.has(b.id))!;
  const bd = editLayout(base, 'buildings', { op: 'delete', key: `building:${free.id}` });
  assert.ok(bd.ok, bd.message);
  assert.ok(!bd.layout.buildings.some((b) => b.id === free.id) && !bd.layout.walls.some((w) => w.building === free.id) && !bd.layout.ladders.some((k) => k.to === `roof:${free.id}`));
  assert.deepEqual(assignMap(bd.layout).problems, []);
  assert.ok(!editLayout(base, 'obstacles_small', { op: 'delete', key: `building:${free.id}` }).ok, 'a building is not deleted from an obstacle layer');
});

test('layers props editing: a mine / loot / special item by click, move, delete - as overrides, on the active layer; saved to a temp folder only', () => {
  const before = sha(DEPOT_PROPS);
  const file = readJson<LevelPropsFile>(DEPOT_PROPS);
  const mine = editProps(file, 'mines', { op: 'add', template: tpl('mines', 'мина'), pos: [1500.6, -2000.2] });
  assert.ok(mine.ok && mine.id === 'hand_mines_0', mine.message);
  const n0 = file.overrides?.length ?? 0;
  assert.equal(readJson<LevelPropsFile>(DEPOT_PROPS).overrides?.length ?? 0, n0);
  assert.equal(mine.props.overrides!.length, n0 + 1, 'one add record; the input file object is not changed');
  assert.deepEqual(mine.props.overrides!.at(-1), { op: 'add', object: { id: 'hand_mines_0', origin: 'hand', variant: 'mine', pos: [1501, -2000], walk_layer: 'ground', yaw: 0, trigger: 'proximity' } });
  assert.ok(mine.props.layers.some((l) => l.kind === 'mines' && l.locked && l.objects.length === 0), 'the mines layer is created for the hand objects');
  let a: LayerAssignment = assignLayers(propEntities(mine.props));
  assert.equal(a.byLayer.mines.length, 1); assert.deepEqual(a.problems, []);
  // a second mine, loot, a turret
  const two = editProps(mine.props, 'mines', { op: 'add', template: tpl('mines', 'растяжка'), pos: [1700, -2000] });
  assert.equal(two.id, 'hand_mines_1');
  const loot = editProps(two.props, 'loot', { op: 'add', template: tpl('loot', 'лут (куча)'), pos: [900, 900] });
  const tur = editProps(loot.props, 'special', { op: 'add', template: tpl('special', 'турель'), pos: [800, 800] });
  assert.ok(loot.ok && tur.ok);
  a = assignLayers(propEntities(tur.props));
  assert.deepEqual([a.byLayer.mines.length, a.byLayer.loot.length, a.byLayer.special.length], [2, 1, 9]);
  // scoped: a mine template on the loot layer, a mine moved / deleted from the loot layer, a layout layer - all refused
  assert.ok(!editProps(file, 'loot', { op: 'add', template: tpl('mines', 'мина'), pos: [0, 0] }).ok);
  assert.ok(!editProps(tur.props, 'loot', { op: 'move', id: 'hand_mines_0', pos: [1, 1] }).ok);
  assert.ok(!editProps(tur.props, 'loot', { op: 'delete', id: 'hand_mines_0' }).ok);
  assert.ok(!editProps(tur.props, 'roads', { op: 'delete', id: 'hand_mines_0' }).ok);
  assert.ok(!editProps({ ...file, frozen: true }, 'mines', { op: 'add', template: tpl('mines', 'мина'), pos: [0, 0] }).ok, 'a frozen file is not edited');
  // move / delete a hand object = its own add record; a generated object = move / delete overrides
  const mv = editProps(tur.props, 'mines', { op: 'move', id: 'hand_mines_0', pos: [1600, -2100] });
  assert.deepEqual(resolveProps(mv.props).find((x) => x.object.id === 'hand_mines_0')!.object.pos, [1600, -2100]);
  assert.equal(mv.props.overrides!.length, tur.props.overrides!.length);
  const rm = editProps(mv.props, 'mines', { op: 'delete', id: 'hand_mines_0' });
  assert.ok(!resolveProps(rm.props).some((x) => x.object.id === 'hand_mines_0') && rm.props.overrides!.length === mv.props.overrides!.length - 1);
  const barrel = file.layers.find((l) => l.kind === 'barrels')!.objects[0];
  const bm = editProps(rm.props, 'special', { op: 'move', id: barrel.id, pos: [barrel.pos[0] + 100, barrel.pos[1]] });
  assert.ok(bm.ok && bm.props.overrides!.some((o) => o.op === 'move' && o.id === barrel.id));
  const bd = editProps(bm.props, 'special', { op: 'delete', id: barrel.id });
  assert.ok(bd.props.overrides!.some((o) => o.op === 'delete' && o.id === barrel.id) && !bd.props.overrides!.some((o) => o.op === 'move' && o.id === barrel.id));
  assert.equal(assignLayers(propEntities(bd.props)).byLayer.special.length, 8, '8 barrels - 1 deleted + 1 turret');
  // the result is a valid props file; it goes to a temp folder, the file of the designer stays byte for byte
  assert.deepEqual(validateProps(bd.props), []);
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'levelgen-layers-'));
  try {
    const out = path.join(tmp, 'L_Gen_Depot_1.props.json');
    fs.writeFileSync(out, formatPropsCanonical(bd.props));
    assert.deepEqual(validateProps(readJson(out)), []);
    assert.equal(assignLayers(propEntities(readJson<LevelPropsFile>(out))).byLayer.mines.length, 1);
  } finally { fs.rmSync(tmp, { recursive: true, force: true }); }
  assert.equal(sha(DEPOT_PROPS), before);
});

test('layers re-roll: limited to chosen obstacle layers - only their covers inside the zone change; deterministic', () => {
  const run = (seed: number, ids: string[]) => {
    const fresh = regenerateRegion({ layout: base, zone: ZONE_NW, seed, profile, blocks: 'jitter' });
    assert.ok(fresh.ok);
    return mergeLayerReroll(base, fresh.layout, ZONE_NW, ids);
  };
  const r = run(777, ['obstacles_small']);
  assert.ok(r.ok && r.removed.length > 0 && r.added.length > 0, r.message);
  const a0 = assignMap(base), a1 = assignMap(r.layout);
  assert.deepEqual(a1.problems, []);
  // everything but the small covers of the zone is the same object for object
  assert.deepEqual(r.layout.walls, base.walls); assert.deepEqual(r.layout.buildings, base.buildings); assert.deepEqual(r.layout.zones, base.zones); assert.deepEqual(r.layout.routes, base.routes);
  const others = (l: GenLayout, a: LayerAssignment) => l.covers.filter((c) => a.layerOf[`cover:${c.id}`] !== 'obstacles_small' || !pointInPoly(c.pos, ZONE_NW));
  assert.deepEqual(others(r.layout, a1), others(base, a0), 'medium / large covers and every cover outside the zone are untouched');
  const inZoneSmall = (l: GenLayout, a: LayerAssignment) => l.covers.filter((c) => a.layerOf[`cover:${c.id}`] === 'obstacles_small' && pointInPoly(c.pos, ZONE_NW));
  assert.notDeepEqual(inZoneSmall(r.layout, a1), inZoneSmall(base, a0), 'the small covers in the zone were re-dealt');
  for (const id of r.added) assert.equal(a1.layerOf[`cover:${id}`], 'obstacles_small');
  assert.equal(new Set(r.layout.covers.map((c) => c.id)).size, r.layout.covers.length, 'ids stay unique');
  // deterministic; another seed differs; two layers at once
  assert.deepEqual(run(777, ['obstacles_small']).layout, r.layout);
  assert.notDeepEqual(run(778, ['obstacles_small']).layout.covers, r.layout.covers);
  const two = run(777, ['obstacles_small', 'obstacles_medium']);
  assert.ok(two.ok && two.removed.length > r.removed.length);
  assert.deepEqual(validateGenLayout(r.layout), validateGenLayout(base));
  // a layer with nothing in the zone
  assert.ok(!mergeLayerReroll(base, base, [[0, 0], [1, 0], [1, 1], [0, 1]], ['obstacles_small']).ok);
});
