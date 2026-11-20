// Tactical map runtime file (docs/design/TACTICAL_MAP.md P1, designer 2026-11-17): the layout's danger analysis ->
// Content/Data/LevelGen/tactical/<Map>.tactical.json (codex.tactical_map/1, world space, layer per item, geometry_hash).
import { describe, it } from 'node:test';
import * as assert from 'node:assert/strict';
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import type { GenLayout } from './schema.ts';
import type { LayoutAnalysis } from './chokepoints.ts';
import { geometryHash } from './chokepoints.ts';
import { findProjectRoot } from './props/autogen.ts';
import { accessKind, currentAnalysis, formatTacticalMap, TACTICAL_MAP_SCHEMA, tacticalMapFor, tacticalMapPath, WINDOW_STAND_INSET_CM, writeTacticalMapFile } from './tacticalMap.ts';
import { runtimeDirsFor, writeRuntimeFiles } from './node/runtimeFiles.ts';

const root = findProjectRoot();
const readLayout = (n: string) => JSON.parse(fs.readFileSync(path.join(root, 'Content/Data/LevelGen/accepted', `${n}.json`), 'utf8').replace(/^﻿/, '')) as GenLayout;

const tinyBase = (): GenLayout => ({
  name: 'tiny', bounds: { min: [-5000, -5000], max: [5000, 5000] }, ground_z: 40, spawn: { pos: [0, 0], yaw: 0 }, end: [100, 100],
  walls: [{ id: 'W_canal', a: [-3000, -2000], b: [-3000, 2000], height: 200, thickness: 80, kind: 'cut_wall' }], zones: [],
  buildings: [
    { id: 'B_1', footprint: [[1000, 1000], [3000, 2000]], floors: 2, height: 600, enterable: true, roof_walkable: true,
      stairs: [{ id: 'B_1_st', from: 0, to: 2, top: [1500, 1500], pos: [1200, 1200] }], rooms: [] },
    { id: 'B_2', footprint: [[-2000, 1000], [-1000, 2000]], floors: 1, height: 300, enterable: false, roof_walkable: true, stairs: [] },
    { id: 'B_3', footprint: [[-2000, -2000], [-1000, -1000]], floors: 1, height: 300, enterable: false, roof_walkable: true, stairs: [] },
  ],
  ladders: [{ id: 'LAD_B_2', pos: [-2050, 1500], yaw: 180, rungs: 8, from: 'ground', to: 'roof:B_2', level_from: 0, level_to: 1, plinth_cm: 8.8, foot: [-2150, 1500], top: [-1950, 1500] }],
  covers: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [],
  objectives: { version: 1, seed: 3, order: 'free', start: { pos: [0, 0], yaw: 0, layer: 'ground', source: 'spawn' }, goals: [{ id: 'GOAL_1', pos: [-2000, 0], layer: 'ground', kind: 'street' }] },
} as unknown as GenLayout);

const handAnalysis = (l: GenLayout): LayoutAnalysis => ({
  version: 1, geometry_hash: geometryHash(l), conditions: { weather: 'clear', time_of_day: 'day' },
  thresholds: { narrow_width_m: 6, clutter_min_obstacles: 4, clutter_blocked_fraction: 0.12, lane_width_m: 2.5, canyon_tall_cm: 230, canyon_side_max_m: 8, detour_critical_m: 40,
    drop_reach_cm: 400, drop_min_cm: 150, drop_max_cm: 650, window_range_m: 60, sniper_range_m: 110, reveal_close_m: 12, stealth_sight_m: 48, min_score: 0.25 },
  chokepoints: [{ id: 'CP_01', kind: 'only_way', kind_name: 'единственный путь', tags: ['narrow', 'only_way'], score: 0.8, pos: [0, 1500], line: [[0, 1200], [0, 1800]],
    poly: [[-100, 1200], [100, 1200], [100, 1800], [-100, 1800]], length_m: 6, width_m: 2, lane_m: 2, route: 'mandatory', sources: ['route:R_main'],
    values: { narrow: 1, clutter: 0, canyon: 0, only_way: 1, drop_ambush: 0, window_fire: 0, sniper: 0, hiding: 0, on_route: 1 },
    numbers: { obstacles_per_window: 0, blocked_fraction: 0, canyon_share: 0, side_a: null, side_b: null, alternative: 'detour', detour_m: 25.5,
      drop_spots: ['DA_01'], windows: ['W_1'], overwatch: ['B_1'], hiding_spots: ['HS_01'], reveal_m: 4, reveal_stealth_m: 3 }, reasons: [] }],
  dropAmbushSpots: [
    { id: 'DA_01', surface: 'roof:B_2', kind: 'roof', edge: [-1000, 1500], land: [-900, 1500], drop_cm: 300, gap_cm: 100, access: 'ladder', jumpers: ['cutter'], line: 'route:R_main', places: ['CP_01'] },
    { id: 'DA_02', surface: 'wall:W_canal', kind: 'wall_top', edge: [-3000, 0], land: [-2900, 0], drop_cm: 200, gap_cm: 100, access: 'wall', jumpers: ['cutter'], line: 'street:Z', places: [] },
  ],
  windowsCovering: [
    { id: 'W_1', building: 'B_1', floor: 1, kind: 'window', pos: [1000, 1500], yaw: 180, covered_m: 40, min_dist_m: 9, places: ['CP_01'] },
    { id: 'W_0', building: 'B_1', floor: 0, kind: 'loophole', pos: [2000, 1000], yaw: -90, covered_m: 12, min_dist_m: 15, places: ['CP_01'] },
  ],
  overwatchRoofs: [{ building: 'B_1', level: 2, height_cm: 600, range_m: 110, access: ['stair:B_1_st'], spots: [{ pos: [1100, 1100], facing_yaw: -135, covered_m: 300, longest_los_m: 80 }], places: ['CP_01'] }],
  hidingSpots: [{ id: 'HS_01', pos: [300, 1500], facing_yaw: 180, reveal_normal_m: 4, reveal_stealth_m: 3, stealth_only: false, blind: false, rush_m: 3, cover: 'cover:C_1', cover_height_cm: 120,
    threatens: { line: 'route:R_main', route: 'R_main', pos: [0, 1500] }, place: 'CP_01' }],
  stats: { lines: 1, length_m: 10, samples: 20, places_found: 1 },
});

const tiny = (): GenLayout => { const l = tinyBase(); return { ...l, analysis: handAnalysis(l) }; };

describe('tactical map runtime file (TACTICAL_MAP P1)', () => {
  it('uses the layout analysis when it is current and maps every item to world space with its layer', () => {
    const f = tacticalMapFor(tiny(), 'L_Gen_Tiny');
    assert.equal(f.schema, TACTICAL_MAP_SCHEMA);
    assert.equal(f.map, 'L_Gen_Tiny');
    assert.equal(f.layout, 'tiny');
    assert.equal(f.analysis_source, 'layout');
    assert.equal(f.geometry_hash, geometryHash(tinyBase()));
    assert.equal(f.ground_z, 40);
    assert.equal(f.thresholds.ladder_only_factor, 0.65, 'the designer factor from chokepoints.json');
    const cp = f.chokepoints[0];
    assert.deepEqual(cp.pos, [0, 1500, 40]);
    assert.deepEqual(cp.line, [[0, 1200, 40], [0, 1800, 40]]);
    assert.equal(cp.layer, 'ground');
    assert.equal(cp.alternative, 'detour');
    assert.equal(cp.detour_m, 25.5);
    assert.deepEqual(cp.hiding_spots, ['HS_01']);
  });

  it('drop spots: the edge on the roof level / the wall top, the land point on the ground; ladder-only flagged', () => {
    const [roof, wall] = tacticalMapFor(tiny(), 'M').drop_spots;
    assert.deepEqual(roof.edge, [-1000, 1500, 340]);
    assert.deepEqual(roof.land, [-900, 1500, 40]);
    assert.equal(roof.layer, 'roof:B_2');
    assert.equal(roof.ladder_only, true);
    assert.deepEqual(wall.edge, [-3000, 0, 240], 'a wall top = ground_z + the wall height');
    assert.equal(wall.ladder_only, false);
  });

  it('windows: floor layer and z, the stand point inside behind the window, the outward look yaw', () => {
    const [w1, w0] = tacticalMapFor(tiny(), 'M').windows;
    assert.equal(w1.layer, 'floor:B_1:1');
    assert.deepEqual(w1.pos, [1000, 1500, 340]);
    assert.deepEqual(w1.stand, [1000 + WINDOW_STAND_INSET_CM, 1500, 340], 'yaw 180 looks out to -X: the stand point is at +X');
    assert.equal(w0.layer, 'ground');
    assert.deepEqual(w0.stand, [2000, 1000 + WINDOW_STAND_INSET_CM, 40]);
  });

  it('overwatch roofs and ladder-only roofs', () => {
    const f = tacticalMapFor(tiny(), 'M');
    const ow = f.overwatch_roofs[0];
    assert.equal(ow.layer, 'roof:B_1');
    assert.equal(ow.z, 640);
    assert.deepEqual(ow.spots[0].pos, [1100, 1100, 640]);
    assert.equal(ow.access_kind, 'stairs');
    assert.equal(ow.ladder_only, false);
    // B_2 has a ladder only; B_1 a stair; B_3 nothing at all (no access = not a placement roof)
    assert.deepEqual(f.ladder_only_roofs.map((r) => r.building), ['B_2']);
    const lr = f.ladder_only_roofs[0];
    assert.deepEqual(lr.center, [-1500, 1500, 340]);
    assert.deepEqual(lr.ladders, ['LAD_B_2']);
    assert.deepEqual(lr.ladder_tops, [[-1950, 1500, 340]]);
    assert.deepEqual(lr.drop_spots, ['DA_01']);
    assert.equal(lr.overwatch, false);
    assert.equal(accessKind(['ladder:a', 'hatch:b']), 'ladder');
    assert.equal(accessKind(['hatch:b']), 'hatch');
    assert.equal(accessKind([]), 'none');
  });

  it('hiding spots keep the reveal distances, stealth flag, facing yaw and the threatened point in world space', () => {
    const h = tacticalMapFor(tiny(), 'M').hiding_spots[0];
    assert.deepEqual(h.pos, [300, 1500, 40]);
    assert.deepEqual(h.threatens.pos, [0, 1500, 40]);
    assert.equal(h.threatens.route, 'R_main');
    assert.equal(h.facing_yaw, 180);
    assert.equal(h.reveal_normal_m, 4);
    assert.equal(h.stealth_only, false);
    assert.equal(h.place, 'CP_01');
  });

  it('a stale analysis (the map changed after it) is not used', () => {
    const l = tiny();
    const moved = { ...l, buildings: l.buildings.map((b) => (b.id === 'B_3' ? { ...b, footprint: [[-2100, -2000], [-1000, -1000]] as GenLayout['buildings'][0]['footprint'] } : b)) };
    assert.ok(currentAnalysis(l), 'the hash matches: the layout analysis is used');
    assert.equal(currentAnalysis(moved), null, 'another geometry: the export computes a fresh one');
    assert.equal(currentAnalysis(tinyBase()), null, 'no analysis block');
  });

  it('the text form parses back to the same object and ends with a newline', () => {
    const f = tacticalMapFor(tiny(), 'M');
    const text = formatTacticalMap(f);
    assert.ok(text.endsWith('}\n'));
    assert.deepEqual(JSON.parse(text), JSON.parse(JSON.stringify(f)));
  });

  it('schema v2 exports building and floor on roof spots and inside chokepoints/hiding spots', () => {
    const l = tiny();
    const lWithInside = {
      ...l,
      analysis: {
        ...l.analysis!,
        chokepoints: [
          ...l.analysis!.chokepoints,
          { ...l.analysis!.chokepoints[0], id: 'CP_inside', pos: [1500, 1500] as [number, number] },
        ],
        hidingSpots: [
          ...l.analysis!.hidingSpots,
          { ...l.analysis!.hidingSpots[0], id: 'HS_inside', pos: [1600, 1600] as [number, number] },
        ],
      },
    };
    const f = tacticalMapFor(lWithInside, 'M');
    assert.equal(f.schema, 'codex.tactical_map/2');
    const owSpot = f.overwatch_roofs[0].spots[0];
    assert.equal(owSpot.building, 'B_1');
    assert.equal(owSpot.floor, 2);

    const cpOutside = f.chokepoints.find((c) => c.id === 'CP_01')!;
    assert.equal(cpOutside.building, undefined);
    assert.equal(cpOutside.floor, undefined);

    const cpInside = f.chokepoints.find((c) => c.id === 'CP_inside')!;
    assert.equal(cpInside.building, 'B_1');
    assert.equal(cpInside.floor, 0);

    const hsInside = f.hiding_spots.find((h) => h.id === 'HS_inside')!;
    assert.equal(hsInside.building, 'B_1');
    assert.equal(hsInside.floor, 0);
  });

  it('schema v2 exports roof access details on overwatch, ladder-only and walkable roofs', () => {
    const f = tacticalMapFor(tiny(), 'M');
    const ow = f.overwatch_roofs[0];
    assert.equal(ow.floor, 2);
    assert.equal(ow.access_points.length, 1);
    assert.deepEqual(ow.access_points[0], { id: 'B_1_st', kind: 'stairs', top: [1500, 1500, 640] });

    const lr = f.ladder_only_roofs[0];
    assert.equal(lr.access_points.length, 1);
    assert.deepEqual(lr.access_points[0], { id: 'LAD_B_2', kind: 'ladder', top: [-1950, 1500, 340] });

    assert.ok(Array.isArray(f.walkable_roofs));
    assert.deepEqual(f.walkable_roofs.map((r) => r.building), ['B_1', 'B_2', 'B_3']);
    const wrB1 = f.walkable_roofs.find((r) => r.building === 'B_1')!;
    assert.equal(wrB1.floor, 2);
    assert.equal(wrB1.ladder_only, false);
    assert.deepEqual(wrB1.access, [{ id: 'B_1_st', kind: 'stairs', top: [1500, 1500, 640] }]);
    const wrB2 = f.walkable_roofs.find((r) => r.building === 'B_2')!;
    assert.equal(wrB2.ladder_only, true);
    assert.deepEqual(wrB2.access, [{ id: 'LAD_B_2', kind: 'ladder', top: [-1950, 1500, 340] }]);
  });

  it('schema v2 exports open crossings along routes and goal sites round goals', () => {
    const l = tiny();
    const lWithRouteAndCover = {
      ...l,
      covers: [
        { id: 'COV_goal', pos: [-2050, 50] as [number, number], yaw: 0, size: [100, 50, 120] as [number, number, number], kind: 'half' as const },
        { id: 'COV_seeing', pos: [500, 500] as [number, number], yaw: 0, size: [100, 50, 120] as [number, number, number], kind: 'full' as const },
      ],
      routes: [
        { id: 'R_open', mandatory: true, points: [[0, 0] as [number, number], [0, 2500] as [number, number]] },
      ],
    };
    const f = tacticalMapFor(lWithRouteAndCover, 'M');
    assert.ok(f.open_crossings.length >= 1);
    const oc = f.open_crossings[0];
    assert.equal(oc.route, 'R_open');
    assert.ok(oc.length_m >= 10);
    assert.deepEqual(oc.seeing_covers, ['COV_goal', 'COV_seeing']);

    assert.ok(f.goal_sites.length >= 1);
    const gs = f.goal_sites[0];
    assert.equal(gs.goal_id, 'GOAL_1');
    assert.equal(gs.cover_id, 'COV_goal');
    assert.equal(gs.sector_arc_deg, 90);
    assert.equal(gs.cover_height_cm, 120);
    assert.ok(gs.dist_m < 25);
  });

  it('the committed x4 Depot file matches its accepted layout (the analysis computed as «Принять» does, deterministic)', () => {
    const x4 = readLayout('gen_ref_01_02_depot_1_x4');
    const f = tacticalMapFor(x4, 'L_Gen_Depot_X4', 'gen_ref_01_02_depot_1_x4');
    assert.equal(f.analysis_source, x4.analysis ? 'layout' : 'computed');
    const have = fs.readFileSync(path.join(root, 'Content/Data/LevelGen/tactical/L_Gen_Depot_X4.tactical.json'), 'utf8').split('\r\n').join('\n'); // autocrlf checkouts
    assert.equal(have, formatTacticalMap(f));
    // LEVELGEN_LAYERS_REPORT.md §7: Depot x4 = 46 places, 7 drop spots, 163 windows, 22 roofs, 150 hiding spots
    assert.deepEqual([f.chokepoints.length, f.drop_spots.length, f.windows.length, f.overwatch_roofs.length, f.hiding_spots.length], [46, 7, 163, 22, 150]);
    assert.ok(f.ladder_only_roofs.length > 0, 'the x4 Depot has ladder-only roofs');
    assert.ok(f.chokepoints.every((c, i) => i === 0 || c.score <= f.chokepoints[i - 1].score), 'sorted by score');
  });
});

describe('runtime files on «Принять» / «Собрать в Unreal» (node/runtimeFiles.ts)', () => {
  it('writes the objectives and the tactical file next to each other', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'tactical-'));
    const dirs = runtimeDirsFor(path.join(dir, 'accepted'));
    const r = writeRuntimeFiles(tiny(), dirs, 'L_Gen_Tiny');
    assert.deepEqual(r.errors, []);
    assert.equal(r.objectives, path.join(dir, 'objectives', 'L_Gen_Tiny.objectives.json'));
    assert.equal(r.tactical, tacticalMapPath(path.join(dir, 'tactical'), 'L_Gen_Tiny'));
    assert.equal(fs.readFileSync(r.tactical!, 'utf8'), formatTacticalMap(tacticalMapFor(tiny(), 'L_Gen_Tiny')));
    // no objectives: no goals file, the tactical map is still written
    const r2 = writeRuntimeFiles({ ...tiny(), objectives: undefined, analysis: handAnalysis({ ...tinyBase(), objectives: undefined }) }, dirs, 'L_Gen_NoGoals');
    assert.equal(r2.objectives, null);
    assert.ok(r2.tactical && fs.existsSync(r2.tactical));
    fs.rmSync(dir, { recursive: true, force: true });
  });

  it('a failing file is reported, never thrown, and does not stop the other', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'tactical-'));
    const bad = { ...tiny(), buildings: null } as unknown as GenLayout;
    const r = writeRuntimeFiles(bad, runtimeDirsFor(path.join(dir, 'accepted')), 'L_Gen_Bad');
    assert.equal(r.tactical, null);
    assert.ok(r.errors.some((e) => e.startsWith('tactical:')));
    assert.ok(r.objectives && fs.existsSync(r.objectives), 'the goals file is still written');
    fs.rmSync(dir, { recursive: true, force: true });
  });

  it('writeTacticalMapFile writes <Map>.tactical.json into the dir', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'tactical-'));
    const file = writeTacticalMapFile(tiny(), dir, 'M');
    assert.equal(file, tacticalMapPath(dir, 'M'));
    assert.equal(JSON.parse(fs.readFileSync(file, 'utf8')).schema, TACTICAL_MAP_SCHEMA);
    fs.rmSync(dir, { recursive: true, force: true });
  });
});
