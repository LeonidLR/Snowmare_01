// Building tactical roles (buildingRoles.ts, building-stories.md section 1): precedence, each role, knobs, determinism.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import type { Vec2 } from '../movement/geometry.ts';
import { assignRoles, BUILDING_ROLE_KNOBS, countPlacesByBuilding, withRoles } from './buildingRoles.ts';
import { generateLayout } from './generate.ts';
import type { Profile } from './profile.ts';
import type { GenBuilding, GenLayout, GenParams } from './schema.ts';
import { LEVEL_HEIGHT, LEVELGEN_SCHEMA_V2 } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');

function bld(id: string, min: Vec2, max: Vec2, floors: number, o: Partial<GenBuilding> = {}): GenBuilding {
  return {
    id, footprint: [min, max], height: floors * LEVEL_HEIGHT, floors, enterable: true, roof_walkable: false, set_piece: 'facade',
    doors: [], stairs: [], roof_holes: [], floor_holes: [], ...o,
  };
}

/**
 * The mandatory route runs along y = -2000 (x -5000..30000). One goal in B_goal; 3 PMC places (2 in B_place, 1 in the closed shed).
 * Distances (m): B_near 10 from B_goal; B_place on the route, 90 from the goal; B_tall1-3 30 from the route; B_tall4 5 and B_transit
 * 2 from the route; B_lowroof 35 from the route / 40 from the goal (1 storey); B_noroof 40 from the route; B_far 190+ from all.
 */
function fixture(): GenLayout {
  const buildings: GenBuilding[] = [
    bld('B_goal', [0, 0], [1000, 1000], 1),
    bld('B_near', [2000, 0], [3000, 1000], 3, { roof_walkable: true }),
    bld('B_closed', [1200, 0], [1800, 600], 1, { enterable: false }),
    bld('B_place', [10000, -2500], [11000, -1500], 1),
    bld('B_tall1', [14000, 1000], [15000, 2000], 3, { roof_walkable: true }),
    bld('B_tall3', [20000, 1000], [21000, 2000], 2, { roof_walkable: true }),
    bld('B_tall2', [17000, 1000], [18000, 2000], 2, { roof_walkable: true }),
    bld('B_tall4', [23000, -1500], [24000, -500], 2, { roof_walkable: true }),
    bld('B_transit', [26000, -1800], [27000, -1000], 1),
    bld('B_lowroof', [5000, 1500], [6000, 2500], 1, { roof_walkable: true }),
    bld('B_noroof', [29000, 2000], [30000, 3000], 2),
    bld('B_far', [0, 20000], [1000, 21000], 1),
  ];
  return {
    schema: LEVELGEN_SCHEMA_V2, name: 'roles_fixture', profile: 'test', seed: 0, params: {} as GenParams,
    bounds: { min: [-6000, -6000], max: [32000, 22000] }, ground_z: 0, spawn: { pos: [-5000, -2000], yaw: 0 }, end: [30000, -2000],
    walls: [], buildings, ladders: [], covers: [], zones: [],
    routes: [
      { id: 'R_side', pace: 'walk', points: [[0, 20500], [1000, 20500]] },
      { id: 'R_main', pace: 'walk', mandatory: true, points: [[-5000, -2000], [12000, -2000], [30000, -2000]] },
    ],
    spawn_points: [], ambush_points: [], patrol_routes: [],
    objectives: {
      version: 1, seed: 0, order: 'free', start: { pos: [-5000, -2000], yaw: 0, layer: 'ground', source: 'spawn' },
      goals: [{ id: 'GOAL_1', pos: [500, 500], layer: 'ground', kind: 'room', building: 'B_goal' }],
    },
  };
}

const PLACES = countPlacesByBuilding([{ building: 'B_place' }, { building: 'B_place' }, { building: 'B_closed' }]);

test('countPlacesByBuilding counts places per building id', () => {
  assert.deepEqual(PLACES, { B_place: 2, B_closed: 1 });
  assert.deepEqual(countPlacesByBuilding([{}, { building: 'X' }]), { X: 1 });
});

test('assignRoles: every role on the fixture; non-enterable buildings get none', () => {
  assert.deepEqual(assignRoles(fixture(), { placesByBuilding: PLACES }), {
    B_goal: 'objective',
    B_near: 'garrison', // near the objective (also tall + near the route: garrison wins over overwatch)
    B_place: 'garrison', // PMC places (also on the route: garrison wins over transit)
    B_tall1: 'overwatch',
    B_tall3: 'overwatch',
    B_tall2: 'overwatch',
    B_tall4: 'transit', // 4th tallest candidate over N_watch = 3 (ties by id) -> falls to transit
    B_transit: 'transit',
    B_lowroof: 'background', // 1 storey: no overwatch; 35 m from the route: no transit
    B_noroof: 'background', // no walkable roof
    B_far: 'background',
  });
});

test('assignRoles: precedence objective > garrison > overwatch > transit', () => {
  const l = fixture();
  // the objective building also holds places and stands on the route: still objective
  l.buildings[0] = { ...l.buildings[0], footprint: [[0, -2500], [1000, 1000]], floors: 3, height: 900, roof_walkable: true };
  const r = assignRoles(l, { placesByBuilding: { ...PLACES, B_goal: 1 } });
  assert.equal(r.B_goal, 'objective');
  // without the places B_place is on the route -> transit; B_near keeps garrison by distance alone
  const noPlaces = assignRoles(fixture());
  assert.equal(noPlaces.B_place, 'transit');
  assert.equal(noPlaces.B_near, 'garrison');
});

test('assignRoles: no objectives and no mandatory route -> every enterable building is background', () => {
  const l = fixture();
  delete l.objectives;
  l.routes = l.routes.map((r) => ({ ...r, mandatory: false }));
  const r = assignRoles(l);
  assert.ok(Object.values(r).every((x) => x === 'background'), JSON.stringify(r));
});

test('assignRoles: knobs - N_watch and the radii', () => {
  const one = assignRoles(fixture(), { placesByBuilding: PLACES, knobs: { watch_max: 1 } });
  assert.deepEqual([one.B_tall1, one.B_tall2, one.B_tall3, one.B_tall4], ['overwatch', 'background', 'background', 'transit']);
  const near = assignRoles(fixture(), { knobs: { garrison_radius_m: 5, watch_radius_m: 9 } });
  assert.equal(near.B_near, 'background', '10 m from the goal > 5 m; 10 m from the goal + 20 m from the route > 9 m watch radius');
  assert.equal(near.B_tall1, 'background');
  assert.equal(BUILDING_ROLE_KNOBS.garrison_radius_m, 30);
  assert.equal(BUILDING_ROLE_KNOBS.watch_radius_m, 60);
  assert.equal(BUILDING_ROLE_KNOBS.watch_max, 3);
});

test('assignRoles: rotated footprint distance (a 45 deg building corner reaches the route)', () => {
  const l = fixture();
  // a 10 x 10 m square 16 m north of the route; turned 45 deg about its centre its corner comes to ~13.9 m -> within 15 m
  const sq = bld('B_rot', [8000, -400], [9000, 600], 1);
  const straight = assignRoles({ ...l, buildings: [sq] });
  const turned = assignRoles({ ...l, buildings: [{ ...sq, rotation: 45 }] });
  assert.equal(straight.B_rot, 'background', 'axis-aligned: 16 m from the route');
  assert.equal(turned.B_rot, 'transit', 'rotated: the corner is ~13.9 m from the route');
});

test('withRoles writes role onto enterable buildings only and changes nothing else', () => {
  const l = fixture();
  l.buildings[2] = { ...l.buildings[2], role: 'garrison' }; // a stale role on the closed shed is dropped
  const before = JSON.stringify(l);
  const out = withRoles(l, { placesByBuilding: PLACES });
  assert.equal(JSON.stringify(l), before, 'the input is not mutated');
  const roles = assignRoles(l, { placesByBuilding: PLACES });
  for (const b of out.buildings) assert.equal(b.role, roles[b.id]);
  assert.equal(out.buildings[2].role, undefined);
  const strip = (x: GenLayout) => JSON.stringify({ ...x, buildings: x.buildings.map(({ role: _r, ...b }) => b) });
  assert.equal(strip(out), strip(l));
});

test('assignRoles is deterministic on a generated layout and independent of the building order', () => {
  const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
  const a = generateLayout({ profile, seed: 1 });
  const b = generateLayout({ profile, seed: 1 });
  const ra = assignRoles(a), rb = assignRoles(b);
  assert.deepEqual(ra, rb);
  assert.deepEqual(assignRoles({ ...a, buildings: [...a.buildings].reverse() }), ra, 'ties by id, not by order');
  const enterable = a.buildings.filter((x) => x.enterable).map((x) => x.id);
  assert.deepEqual(Object.keys(ra).sort(), [...enterable].sort(), 'exactly the enterable buildings have a role');
  assert.ok(Object.values(ra).filter((x) => x === 'overwatch').length <= BUILDING_ROLE_KNOBS.watch_max);
  assert.ok(Object.values(ra).some((x) => x !== 'background'), JSON.stringify(ra));
});
