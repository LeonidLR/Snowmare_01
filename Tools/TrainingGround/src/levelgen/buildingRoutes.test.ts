// Building route model (S1 step 3c, building-route-model.md rules 1-10, F1-F4) + story 10 snow_drift (building-stories.md row 10).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { applyStoriesToLayout, applyStory, STORY_PICK_KNOBS, storyRecipe } from './buildingStories.ts';
import { buildRouteGraph, computeBuildingRoutes, edgeCost, outsideLegCm, ROUTE_COSTS, routeCostHash, withRouteSummaries } from './buildingRoutes.ts';
import { validateBuilding, validateBuildings } from './buildingTraversal.ts';
import { generateLayout } from './generate.ts';
import type { Profile } from './profile.ts';
import type { GenBuilding, GenLayout, WallOpening } from './schema.ts';
import { LEVELGEN_SCHEMA_V2 } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;

function layoutOf(buildings: GenBuilding[]): GenLayout {
  return {
    schema: LEVELGEN_SCHEMA_V2,
    bounds: { min: [-3000, -3000], max: [12000, 4000] },
    walls: [],
    ladders: [],
    buildings,
    routes: [{ id: 'R_main', mandatory: true, points: [[-500, 1500], [9000, 1500]] }],
  } as unknown as GenLayout;
}

/** The F1 fixture: a 10 x 4 m shed, locked front door (south, route side), broken back window (north) 20 m round the ring. */
function f1Shed(): GenBuilding {
  return {
    id: 'B_f1', set_piece: 'block', footprint: [[0, 0], [1000, 400]], floors: 1, height: 300, enterable: true, roof_walkable: false,
    doors: [], stairs: [], roof_holes: [], floor_holes: [],
    openings: [
      { id: 'D_s', kind: 'door', pos: [500, 400], floor: 0, state: 'locked' },
      { id: 'W_n', kind: 'window', pos: [500, 0], floor: 0, state: 'broken' },
    ],
  } as unknown as GenBuilding;
}

/** A 10 x 8 m two-storey block with a core to the roof; `openings` replace the default doors. */
function block(id: string, x = 0, openings?: WallOpening[]): GenBuilding {
  return {
    id, set_piece: 'facade', footprint: [[x, 0], [x + 1000, 800]], floors: 2, height: 600, enterable: true, roof_walkable: true, doors: [],
    openings: openings ?? [
      { id: `${id}_D_s`, kind: 'door', pos: [x + 500, 800], floor: 0 },
      { id: `${id}_D_n`, kind: 'door', pos: [x + 500, 0], floor: 0 },
      { id: `${id}_W1`, kind: 'window', pos: [x + 500, 800], floor: 1 },
    ],
    rooms: [
      { id: `${id}_r0`, floor: 0, bounds: { min: [x, 0], max: [x + 1000, 800] }, kind: 'room', door_ids: [`${id}_D_s`, `${id}_D_n`] },
      { id: `${id}_r1`, floor: 1, bounds: { min: [x, 0], max: [x + 1000, 800] }, kind: 'room' },
    ],
    stairs: [
      // along the 10 m side: 200 approach + 430 run + 200 approach (BUILDING_GENERATOR.md 4.2) fit, the 8 m side would not
      { id: `${id}_st0`, pos: [x + 200, 200], from: 0, to: 1, kind: 'stairwell', top: [x + 630, 200] },
      { id: `${id}_st1`, pos: [x + 630, 360], from: 1, to: 2, kind: 'stairwell', top: [x + 200, 360] },
    ],
    roof_holes: [], floor_holes: [],
  } as unknown as GenBuilding;
}

test('cost table: F1 defaults and a stable hash', () => {
  assert.equal(ROUTE_COSTS.v_ref, 3);
  assert.equal(ROUTE_COSTS.t_rung, 0.9);
  assert.equal(ROUTE_COSTS.n_squad, 1);
  assert.equal(ROUTE_COSTS.t_follow, 0.5);
  assert.deepEqual(ROUTE_COSTS.actions.kick, { t: 3, n: 15, x: 0 });
  assert.deepEqual(ROUTE_COSTS.profiles.quiet, { a_n: 3, a_x: 120, a_e: 1 });
  assert.deepEqual(ROUTE_COSTS.exposure, { above_ground: 1, ground: 0.5 });
  assert.match(routeCostHash(), /^[0-9a-f]{8}$/);
  assert.equal(routeCostHash(), routeCostHash(structuredClone(ROUTE_COSTS)));
  // F1 worked example: the kick alone costs 10.5 balanced, 48 quiet, 4.5 fast
  const kick = { id: 'e', from: 'a', to: 'b', mode: 'kick' as const, len_m: 0, rungs: 0, single_file: false, exposure: 0, outside: false };
  assert.equal(edgeCost(kick, 'balanced'), 10.5);
  assert.equal(edgeCost(kick, 'quiet'), 48);
  assert.equal(edgeCost(kick, 'fast'), 4.5);
});

test('F1 fixture: balanced -> kick, quiet -> window, fast -> kick, F4 = 1', () => {
  const b = f1Shed(), l = layoutOf([b]);
  // the window's outside leg: 9.5 m to the ring, 20 m round it (rule 2)
  assert.equal(Math.round(outsideLegCm(b, [500, 1500], [500, -150])), 950 + 2000);
  const r = computeBuildingRoutes(l, b);
  assert.equal(r.routes.balanced!.entry, 'D_s');
  assert.ok(r.key_route!.legs.some((x) => x.mode === 'kick'));
  assert.equal(r.quiet_entry, 'W_n');
  assert.ok(r.routes.quiet!.edges.some((e) => e.mode === 'vault'));
  assert.equal(r.fast_entry, 'D_s');
  assert.equal(r.choice, true);
  // balanced: 9.5 m street leg (ground, x1.5) + 3.5 m to the room centre + kick 10.5
  assert.ok(Math.abs(r.key_route!.cost - (9.5 / 3 * 1.5 + 3.5 / 3 + 10.5)) < 0.01, `${r.key_route!.cost}`);
  assert.equal(r.key_route!.detour, 1);
  assert.equal(r.key_route!.cost_hash, routeCostHash());
});

test('D = 1 for every building with the open story; stored routes carry entries', () => {
  const l = layoutOf([block('B0'), block('B1', 2000), f1Shed()]);
  const none = { ...STORY_PICK_KNOBS, budget: { objective: 0, garrison: 0, overwatch: 0, transit: 0, background: 0 } };
  const out = applyStoriesToLayout(l, 3, none).layout;
  for (const q of out.buildings) {
    assert.equal(q.stories![0].id, 'open');
    assert.equal(q.key_route!.detour, 1, q.id);
    assert.ok(q.fast_entry && q.quiet_entry, q.id);
  }
  // the page summary reads the stored route
  const rep = withRouteSummaries(out, validateBuildings(out));
  assert.ok(rep.buildings.every((x) => x.route?.stored && x.stories?.[0] === 'open'));
});

test('story 10 snow_drift: front sealed + a ramp to the storey-1 window; reachable, fast up the drift, quiet by the back door', () => {
  const b = block('B'), l = layoutOf([b]);
  assert.deepEqual(storyRecipe(l, b, 'snow_drift')!.map((x) => x.kind), ['door_state', 'snow_ramp']);
  const res = applyStory(l, b, 'snow_drift', 11);
  assert.equal(res.ok, true, JSON.stringify(res.issues));
  assert.equal(res.instance.author, 'cold');
  const q = res.building;
  assert.equal(q.openings!.find((o) => o.id === 'B_D_s')!.state, 'sealed');
  assert.equal(q.snow_ramps!.length, 1);
  const ramp = q.snow_ramps![0];
  assert.equal(ramp.to_opening, 'B_W1');
  // run 429 cm for a 300 cm rise: <= 35 deg
  assert.ok(Math.atan2(300, ramp.from[1] - 800) * 180 / Math.PI <= 35, `${ramp.from}`);
  const lq = layoutOf([q]);
  assert.equal(validateBuilding(lq, q).ok, true);
  const r = computeBuildingRoutes(lq, q, { baseline: { layout: l, building: b } });
  assert.equal(r.fast_entry, 'B_W1');
  assert.equal(r.quiet_entry, 'B_D_n');
  assert.equal(r.choice, true);
  assert.ok(r.key_route!.detour > 0);
});

test('story 10 rolls back when fast and quiet would enter by the same opening (F4) or the ramp has no room', () => {
  // only the front door: sealed, the ramp window is the one way in for every profile
  const one = block('B', 0, [
    { id: 'B_D_s', kind: 'door', pos: [500, 800], floor: 0 },
    { id: 'B_W1', kind: 'window', pos: [500, 800], floor: 1 },
  ]);
  const json = JSON.stringify(one);
  const res = applyStory(layoutOf([one]), one, 'snow_drift', 5);
  assert.equal(res.ok, false);
  assert.match(res.issues[0].message, /F4/);
  assert.equal(JSON.stringify(res.building), json);
  // a neighbour on the front: the 429 cm run does not fit -> not eligible
  const b = block('B'), wall = block('N', 0);
  wall.footprint = [[0, 1000], [1000, 1300]];
  assert.equal(storyRecipe(layoutOf([b, wall]), b, 'snow_drift'), null);
  // one storey: not eligible
  assert.equal(storyRecipe(layoutOf([f1Shed()]), f1Shed(), 'snow_drift'), null);
});

test('story 3 F4: fast kicks the locked front, quiet takes another entry; rolls back when both take the same door', () => {
  const b = block('B'), l = layoutOf([b]);
  const res = applyStory(l, b, 'locked_door', 2);
  assert.equal(res.ok, true, JSON.stringify(res.issues));
  const r = computeBuildingRoutes(layoutOf([res.building]), res.building);
  assert.equal(r.fast_entry, 'B_D_s');
  assert.ok(r.routes.fast!.edges.some((e) => e.mode === 'kick'));
  assert.equal(r.quiet_entry, 'B_D_n');
  // a side door right next to the front: fast walks round to it instead of kicking -> no choice -> rollback
  const side = block('B', 0, [
    { id: 'B_D_s', kind: 'door', pos: [500, 800], floor: 0 },
    { id: 'B_D_e', kind: 'door', pos: [1000, 700], floor: 0 },
  ]);
  side.rooms![0].door_ids = ['B_D_s', 'B_D_e'];
  const back = applyStory(layoutOf([side]), side, 'locked_door', 2);
  assert.equal(back.ok, false);
  assert.match(back.issues[0].message, /F4/);
});

test('key routes never use explosive or drop edges; a cheaper breach route is stored separately', () => {
  // front barricaded (explosive only), the back door far round, a floor hole (drop) from storey 1
  const b = block('B', 0, [
    { id: 'B_D_s', kind: 'door', pos: [500, 800], floor: 0, state: 'barricaded' },
    { id: 'B_D_n', kind: 'door', pos: [500, 0], floor: 0 },
  ]);
  b.floor_holes = [{ pos: [800, 400], floor: 1 }];
  const l = layoutOf([b]);
  const g = buildRouteGraph(l, b);
  assert.ok(g.edges.some((e) => e.mode === 'explosive') && g.edges.some((e) => e.mode === 'drop'));
  const r = computeBuildingRoutes(l, b);
  for (const p of ['balanced', 'quiet', 'fast'] as const) {
    assert.ok(r.routes[p]!.edges.every((e) => e.mode !== 'explosive' && e.mode !== 'drop'), p);
  }
  assert.equal(r.quiet_entry, 'B_D_n');
  // with a cheap charge the breach route undercuts the walk round: stored, never the key route
  const cheap = { ...ROUTE_COSTS, actions: { ...ROUTE_COSTS.actions, explosive: { t: 1, n: 0, x: 1 } }, profiles: { ...ROUTE_COSTS.profiles, balanced: { a_n: 0.5, a_x: 0, a_e: 1 } } };
  const rb = computeBuildingRoutes(l, b, { costs: cheap });
  assert.ok(rb.breach_route && rb.breach_route.legs.some((x) => x.mode === 'explosive'));
  assert.ok(rb.key_route!.legs.every((x) => x.mode !== 'explosive' && x.mode !== 'drop'));
  assert.ok(rb.breach_route.cost < rb.key_route!.cost);
});

test('generated depot: stories + routes deterministic, every key route walkable, every choice story passes F4', () => {
  const base = generateLayout({ profile, seed: 1 });
  const a = applyStoriesToLayout(base, 7), c = applyStoriesToLayout(base, 7);
  assert.equal(JSON.stringify(a), JSON.stringify(c));
  let routed = 0;
  for (const q of a.layout.buildings.filter((x) => x.enterable)) {
    assert.equal(validateBuilding(a.layout, q).ok, true, q.id);
    assert.ok(q.key_route, `${q.id}: no key route`);
    routed++;
    assert.ok(q.key_route.legs.every((x) => x.mode !== 'explosive' && x.mode !== 'drop'), q.id);
    const st = q.stories![0].id;
    if (st === 'locked_door' || st === 'snow_drift') assert.notEqual(q.fast_entry, q.quiet_entry, `${q.id} ${st}`);
    if (st === 'open') assert.equal(q.key_route.detour, 1, q.id);
  }
  assert.ok(routed > 0);
});
