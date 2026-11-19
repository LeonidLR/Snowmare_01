import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { configValues } from '../movement/config.ts';
import type { Vec2 } from '../movement/geometry.ts';
import { dist } from '../movement/geometry.ts';
import { evaluateLayout } from './evaluate.ts';
import { buildAllLabMaps, buildLabMap, LAB_MAP_IDS, LAB_SPECS } from './labMaps.ts';
import type { LabMapId } from './labMaps.ts';
import { checkObjectives } from './objectives.ts';
import { matchesObstacleHeight } from './obstacleHeights.ts';
import { fromUE, toUE } from './profile.ts';
import type { GenLayout } from './schema.ts';
import { validateGenLayout } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const LEVELGEN_DIR = path.join(ROOT, 'Content/Data/LevelGen');
const ACCEPTED_DIR = path.join(LEVELGEN_DIR, 'accepted');
const config = configValues(JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/FormationLab/formation_config.json'), 'utf8')));

const readJson = <T>(file: string): T => JSON.parse(fs.readFileSync(file, 'utf8')) as T;
/** A layout point (UE cm) in the profile frame (metres, x east, y south) of its lab map. */
const metres = (id: LabMapId) => (p: Vec2): Vec2 => fromUE(p, LAB_SPECS[id].size);
/** The distance (m) of a point to the segment a-b. */
function segDist(p: Vec2, a: Vec2, b: Vec2): number {
  const abx = b[0] - a[0], aby = b[1] - a[1], l2 = abx * abx + aby * aby;
  const t = l2 === 0 ? 0 : Math.max(0, Math.min(1, ((p[0] - a[0]) * abx + (p[1] - a[1]) * aby) / l2));
  return dist(p, [a[0] + abx * t, a[1] + aby * t]);
}

const AMBUSH_IDS: Record<LabMapId, string[]> = {
  crossroads: ['AMB_hammer_w', 'AMB_hammer_e', 'AMB_anvil'],
  alley: ['AMB_roof', 'AMB_corner', 'AMB_cutoff'],
  bridge: ['AMB_end_w', 'AMB_end_e', 'AMB_obstacle'],
};

test('lab maps: three layouts, deterministic, valid, completable, <= 80 x 80 m, with the designed start + one goal', () => {
  const labs = buildAllLabMaps();
  assert.deepEqual(labs.map((l) => l.name), ['gen_lab_crossroads', 'gen_lab_alley', 'gen_lab_bridge']);
  assert.deepEqual(labs.map((l) => l.mapName), ['L_Lab_Crossroads', 'L_Lab_Alley', 'L_Lab_Bridge']);
  assert.deepEqual(labs.map((l) => l.layout.seed), [401, 402, 403]);

  for (const lab of labs) {
    const { layout } = lab;
    const spec = LAB_SPECS[lab.id];
    assert.deepEqual(validateGenLayout(layout), [], `${lab.name} must pass validateGenLayout`);
    assert.deepEqual(JSON.parse(JSON.stringify(buildLabMap(lab.id).layout)), JSON.parse(JSON.stringify(layout)), `${lab.name} must be deterministic`);

    // size: the bounds are the map's size in UE cm (<= 80 x 80 m)
    const widthM = (layout.bounds.max[1] - layout.bounds.min[1]) / 100, heightM = (layout.bounds.max[0] - layout.bounds.min[0]) / 100;
    assert.deepEqual([widthM, heightM], spec.size, `${lab.name} bounds`);
    assert.ok(widthM <= 80 && heightM <= 80, `${lab.name} is ${widthM} x ${heightM} m, the limit is 80 x 80`);

    // the evaluator: the mandatory route and every variant complete
    const ev = evaluateLayout({ layout, profile: lab.profile, config });
    assert.equal(ev.route.completable, true, `${lab.name} mandatory route failed: ${ev.route.fail}`);
    assert.ok(ev.variants.length >= 1, `${lab.name} wants at least one variant route`);
    for (const v of ev.variants) assert.equal(v.completable, true, `${lab.name} variant ${v.id} failed: ${v.fail}`);
    assert.equal(ev.tactical.mantle.invalid, 0, `${lab.name}: no invalid mantle edges`);
    assert.equal(ev.tactical.unreachableRooms.length, 0, `${lab.name} unreachable rooms: ${ev.tactical.unreachableRooms.join(', ')}`);
    assert.equal(ev.tactical.blockedWindows.length, 0, `${lab.name} blocked windows: ${JSON.stringify(ev.tactical.blockedWindows)}`);

    // the start + exactly one goal, at the designed positions; the mandatory route runs start -> goal
    assert.equal(layout.objectives?.goals.length, 1, `${lab.name}: exactly one goal`);
    assert.deepEqual(layout.objectives?.goals[0].pos, toUE(spec.goal, spec.size), `${lab.name}: the goal is the designed one`);
    assert.deepEqual(layout.objectives?.start.pos, toUE(spec.start, spec.size), `${lab.name}: the start is the designed one`);
    assert.deepEqual(layout.spawn.pos, toUE(spec.start, spec.size));
    assert.deepEqual(layout.end, toUE(spec.goal, spec.size));
    const check = checkObjectives(layout);
    assert.deepEqual(check.errors, [], `${lab.name} objectives`);
    assert.equal(check.ok, true);
    const main = layout.routes.find((r) => r.mandatory)!;
    assert.equal(layout.routes.filter((r) => r.mandatory).length, 1);
    assert.equal(main.id, 'R_main');
    assert.deepEqual(main.points[0], layout.spawn.pos);
    assert.deepEqual(main.points[main.points.length - 1], layout.end);

    // one spawn point at the start; the designed ambush points
    assert.deepEqual(layout.spawn_points.map((s) => s.pos), [layout.spawn.pos]);
    assert.deepEqual(layout.ambush_points.map((a) => a.id), AMBUSH_IDS[lab.id]);

    // accepted artifact on disk = the builder's output
    const diskPath = path.join(ACCEPTED_DIR, `${lab.name}.json`);
    assert.equal(fs.existsSync(diskPath), true, `${diskPath} must exist (node src/levelgen/labMaps.ts)`);
    const onDisk = readJson<GenLayout>(diskPath);
    assert.deepEqual(validateGenLayout(onDisk), []);
    assert.deepEqual(onDisk, JSON.parse(JSON.stringify(layout)), `${lab.name}.json on disk must match the labMaps builder`);
  }
});

test('lab maps: the runtime files (mission goals + tactical map) exist and carry the designed goal', () => {
  for (const id of LAB_MAP_IDS) {
    const lab = buildLabMap(id);
    const goals = readJson<{ schema: string; map: string; layout: string; order: string; goals: { id: string; pos: [number, number, number]; layer: string }[] }>(
      path.join(LEVELGEN_DIR, 'objectives', `${lab.mapName}.objectives.json`));
    assert.equal(goals.schema, 'codex.mission_goals/1');
    assert.equal(goals.map, lab.mapName);
    assert.equal(goals.layout, lab.name);
    assert.equal(goals.goals.length, 1);
    const want = toUE(lab.goal, LAB_SPECS[id].size);
    assert.deepEqual(goals.goals[0].pos, [want[0], want[1], 0], `${lab.mapName}: the exported goal is the designed one`);
    assert.equal(goals.goals[0].layer, 'ground');

    const tactical = readJson<{ schema: string; map: string; layout: string }>(path.join(LEVELGEN_DIR, 'tactical', `${lab.mapName}.tactical.json`));
    assert.equal(tactical.schema, 'codex.tactical_map/1');
    assert.equal(tactical.map, lab.mapName);
    assert.equal(tactical.layout, lab.name);

    // the level JSON (Scripts/Editor/build_levelgen_map.py plan) is there too
    assert.equal(fs.existsSync(path.join(ROOT, 'Content/Data/LevelJson', `${lab.mapName}.json`)), true, `${lab.mapName} level JSON`);
  }
});

test('L_Lab_Crossroads (DS-00A): >= 4 covers on the far side, a walled enclosure with ONE gap, the attackers 60-80 m away', () => {
  const { layout } = buildLabMap('crossroads');
  const m = metres('crossroads');
  const zone = layout.zones.find((z) => z.id === 'Z_killzone')!;
  assert.ok(zone, 'Z_killzone');
  assert.equal(zone.intent, 'kill_zone');
  const polyM = zone.poly.map(m);
  const xs = polyM.map((p) => p[0]), ys = polyM.map((p) => p[1]);
  assert.deepEqual([Math.min(...xs), Math.max(...xs), Math.min(...ys), Math.max(...ys)], [25.5, 34.5, 9.5, 18.5], 'the crossing square');

  // the hammer: covers north of the crossing, two each side of the N-S axis
  const north = layout.covers.map((c) => m(c.pos)).filter((p) => p[1] < 9.5);
  assert.ok(north.length >= 4, `expected >= 4 covers north of the crossing, got ${north.length}`);
  assert.ok(north.filter((p) => p[0] < 30).length >= 2 && north.filter((p) => p[0] > 30).length >= 2, 'two covers each side of the axis');

  // the anvil: the enclosure's walls are full height (not mantle-able), exactly one gap, in the west side, 2.5 m wide, centred at y 28
  const grave = layout.walls.filter((w) => w.id.startsWith('W_grave_'));
  assert.ok(grave.length > 0 && grave.every((w) => w.height >= 200 && matchesObstacleHeight(w.height, 'wall_no_traverse')), 'enclosure walls are full-height standard walls');
  const west = grave.filter((w) => w.id.startsWith('W_grave_W_'));
  assert.equal(west.length, 2, 'the west side is split into two pieces = one gap');
  assert.equal(grave.length, 5, 'north, east, south whole + the west side in two pieces: exactly one gap in the whole enclosure');
  const westY = west.map((w) => [w.a, w.b].map((p) => m(p)[1])).sort((a, b) => Math.min(...a) - Math.min(...b));
  const gap = Math.min(...westY[1]) - Math.max(...westY[0]);
  assert.ok(Math.abs(gap - 2.5) < 0.01, `the gap is 2.5 m, got ${gap}`);
  assert.ok(Math.abs((Math.min(...westY[1]) + Math.max(...westY[0])) / 2 - 28) < 0.01, 'the gap is centred at y 28');
  assert.ok(west.every((w) => Math.abs(m(w.a)[0] - 37) < 0.01), 'the gap faces the street (west side x 37)');
  // no walkable roof anywhere on this map
  assert.ok(layout.buildings.every((b) => !b.roof_walkable));

  // the attackers: the start is 60-80 m from the crossing
  const centre: Vec2 = [(Math.min(...xs) + Math.max(...xs)) / 2, (Math.min(...ys) + Math.max(...ys)) / 2];
  const d = dist(m(layout.spawn.pos), centre);
  assert.ok(d >= 60 && d <= 80, `start -> crossing ${d} m`);

  // the enclosure route goes through the gap and back
  const v1 = layout.routes.find((r) => r.id === 'V1_enclosure')!;
  assert.ok(v1.points.some((p) => m(p)[0] > 37.5 && m(p)[1] > 22 && m(p)[1] < 34), 'V1_enclosure enters the enclosure');
});

test('L_Lab_Alley (DS-00B): a 3-5 m lane, a low and a tall building with a roof ladder, the way round the back', () => {
  const { layout } = buildLabMap('alley');
  const m = metres('alley');
  const low = layout.buildings.find((b) => b.id === 'B_low')!;
  const tall = layout.buildings.find((b) => b.id === 'B_tall')!;
  const footM = (b: typeof low): [Vec2, Vec2] => {
    const a = m(b.footprint[0]), c = m(b.footprint[1]);
    return [[Math.min(a[0], c[0]), Math.min(a[1], c[1])], [Math.max(a[0], c[0]), Math.max(a[1], c[1])]];
  };
  const [[lx0, ly0], [lx1, ly1]] = footM(low);
  const [[tx0, ty0], [tx1, ty1]] = footM(tall);
  assert.deepEqual([lx0, ly0, lx1, ly1], [12, 14, 42, 24]);
  assert.deepEqual([tx0, ty0, tx1, ty1], [30, 28, 46, 40]);

  // the lane: between B_low's south facade and the wall / B_tall's north facade
  const laneWidth = ty0 - ly1;
  assert.ok(laneWidth >= 3 && laneWidth <= 5, `lane width ${laneWidth} m`);
  const wall = layout.walls.find((w) => w.id === 'W_lane_s')!;
  assert.ok(Math.abs(m(wall.a)[1] - ty0) < 0.01 && wall.height >= 200, 'a full-height wall closes the lane south of x 12..30');
  const lane = layout.zones.find((z) => z.id === 'Z_lane')!;
  assert.equal(lane.intent, 'kill_zone');

  // B_low: one storey, the roof is not walkable; B_tall: two storeys, a walkable roof with parapets, windows onto the lane, a stairwell
  assert.equal(low.floors, 1);
  assert.equal(low.roof_walkable, false);
  assert.equal(tall.floors, 2);
  assert.equal(tall.roof_walkable, true);
  assert.ok((tall.parapets?.length ?? 0) >= 1, 'B_tall parapets');
  assert.ok(tall.stairs.some((s) => s.kind === 'stairwell' && s.to === 2), 'B_tall has a stairwell up to its roof');
  const northWindows = (tall.openings ?? []).filter((o) => o.kind === 'window' && Math.abs(m(o.pos)[1] - ty0) < 0.01);
  assert.ok(northWindows.length >= 2, `B_tall windows onto the lane: ${northWindows.length}`);

  // the ladder to B_tall's roof stands on its EAST side
  const ladder = layout.ladders.find((l) => l.to === 'roof:B_tall')!;
  assert.ok(ladder, 'a ladder to B_tall roof');
  assert.ok(ladder.rungs >= 7, `ladder rungs ${ladder.rungs}`);
  assert.ok(m(ladder.foot)[0] > tx1 && m(ladder.top)[0] < tx1, 'the ladder is on the east face');

  // the mouth at the east end, the goal beyond it; the start at the lane's west entrance
  assert.ok(m(layout.end)[0] > tx1, 'the goal is beyond the lane mouth');
  assert.ok(m(layout.spawn.pos)[0] < lx0, 'the start is west of the lane');

  // the way round the back: points beside B_low stay north of its north facade (y 14)
  const back = layout.routes.find((r) => r.id === 'V1_back')!;
  assert.ok(back, 'V1_back');
  const beside = back.points.map(m).filter((p) => p[0] >= lx0 && p[0] <= tx1);
  assert.ok(beside.length >= 3 && beside.every((p) => p[1] < ly0), `the back route passes north of y ${ly0}`);
  assert.ok(layout.routes.some((r) => r.id === 'V2_roof' && (r.layers ?? []).includes('roof:B_tall')), 'V2_roof climbs onto B_tall');
  assert.ok(layout.covers.length >= 1 && layout.covers.every((c) => m(c.pos)[1] < ly0), 'the back route has cover');
});

test('L_Lab_Bridge (DS-00C): a trench with ONE bridge, cover at the north end, a mantle obstacle at the south end, windows onto the deck', () => {
  const { layout } = buildLabMap('bridge');
  const m = metres('bridge');
  assert.ok(layout.cut, 'the cut');
  assert.deepEqual(layout.cut!.crossings.map((c) => [c.id, c.kind]), [['X_bridge', 'bridge']]);
  const xp = layout.cut!.crossings[0].poly.map(m);
  const bx: [number, number] = [Math.min(...xp.map((p) => p[0])), Math.max(...xp.map((p) => p[0]))];
  const by: [number, number] = [Math.min(...xp.map((p) => p[1])), Math.max(...xp.map((p) => p[1]))];
  assert.deepEqual([...bx, ...by], [34, 38, 25.5, 34.5], 'the bridge spans rim to rim incl. the retaining walls');
  const deck = layout.zones.find((z) => z.id === 'Z_deck')!;
  assert.ok(deck && deck.set_piece === 'cut_crossing' && deck.intent === 'kill_zone');
  // the retaining walls are split round the bridge only
  assert.equal(layout.walls.filter((w) => w.kind === 'cut_wall').length, 4, 'two retaining walls, each split once at the bridge');
  // two ladders out of the trench, one per wall, tall enough
  const outs = layout.ladders.filter((l) => l.from === 'cut' && l.to === 'ground');
  assert.equal(outs.length, 2);
  assert.ok(outs.every((l) => l.rungs >= 7));
  const lx = outs.map((l) => Math.round(m(l.pos)[0])).sort((a, b) => a - b);
  assert.deepEqual(lx, [10, 62]);

  // the near (north) end: >= 2 covers within 4 m of the deck's north edge, one each side
  const north = layout.covers.map((c) => m(c.pos)).filter((p) => segDist(p, [bx[0], by[0]], [bx[1], by[0]]) <= 4);
  assert.ok(north.length >= 2, `covers near the north end: ${north.length}`);
  assert.ok(north.some((p) => p[0] < bx[0]) && north.some((p) => p[0] > bx[1]), 'one each side of the deck');

  // the far (south) end: a mantle edge of a mantling-table height within 6 m of the deck's south edge, on a wall of that height
  const south = (layout.mantle_edges ?? []).filter((e) => e.kind === 'obstacle' && matchesObstacleHeight(e.height_cm, 'high_mantle')
    && [e.start, e.end].some((p) => segDist(m(p), [bx[0], by[1]], [bx[1], by[1]]) <= 6));
  assert.ok(south.length >= 2, `mantle edges at the south end: ${south.length} (north and south faces)`);
  const ob = layout.walls.find((w) => w.id === 'W_obstacle')!;
  assert.ok(ob && matchesObstacleHeight(ob.height, 'high_mantle'), 'the obstacle has a standard mantling height');
  const [oa, ob2] = [m(ob.a), m(ob.b)];
  assert.ok(Math.abs(dist(oa, ob2) - 6) < 0.01 && Math.abs(ob.thickness - 120) < 0.01, 'about 6 m x 1.2 m');
  assert.ok(oa[0] > bx[1] && ob2[0] > bx[1], 'the obstacle stands east of the deck');

  // B_windows: two storeys, windows on its EAST side
  const bw = layout.buildings.find((b) => b.id === 'B_windows')!;
  assert.equal(bw.floors, 2);
  const [bx0, bx1] = [m(bw.footprint[0])[0], m(bw.footprint[1])[0]].sort((a, b) => a - b);
  assert.deepEqual([bx0, bx1], [16, 29]);
  const east = (bw.openings ?? []).filter((o) => o.kind === 'window' && Math.abs(m(o.pos)[0] - bx1) < 0.01);
  assert.ok(east.length >= 1, `windows on the east side: ${east.length}`);
  assert.ok((bw.openings ?? []).some((o) => o.kind === 'window' && Math.abs(m(o.pos)[1] - 21) < 0.01), 'windows on the south side');
  assert.ok(bw.stairs.some((s) => s.kind === 'stairwell'), 'B_windows has a stairwell');
});
