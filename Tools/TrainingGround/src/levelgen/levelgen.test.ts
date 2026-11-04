// Training Ground / level generator tests (docs/design/levelgen/LEVEL_GENERATOR.md): the schema and the lab compatibility of a
// generated layout, determinism, the mandatory route's completability over seeds, the layer links, the evaluator's failure
// reporting, the profile mapping. `npm test` (node --test, no Unreal).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { configValues } from '../movement/config.ts';
import { validateLayout } from '../movement/layout.ts';
import { auditLayout } from '../movement/levelAudit.ts';
import { criticPrompt, mockCritic } from './critic.ts';
import { evaluateLayout } from './evaluate.ts';
import { extendLayout, generateLayout, HOLE_RIM_M } from './generate.ts';
import type { Profile } from './profile.ts';
import { fromUE, targetScore, toUE, validateProfile } from './profile.ts';
import type { GenLayout } from './schema.ts';
import { CUT_BUILDING_MARGIN_CM, HALL_ROOF_LEVEL, HOLE_HALF_CM, ladderForLevels, layerLevel, layersOf, LEVEL_HEIGHT, levelsOf, mandatoryRoute, STAIR_RUN_PER_RISE, toFormationLayout, validateGenLayout } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
const config = configValues(JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/FormationLab/formation_config.json'), 'utf8')));

test('the Depot reference profile is valid and carries the generator targets', () => {
  assert.deepEqual(validateProfile(profile), []);
  assert.equal(profile.id, 'ref_01_02_depot');
  assert.ok(profile.route.main.points.length >= 10);
  assert.ok(profile.route.variants.length >= 3);
  assert.equal(profile.ladders.length, 4);
  assert.ok(profile.targets.route_length_m[0] < profile.route.main.length_m && profile.route.main.length_m < profile.targets.route_length_m[1]);
});

test('profile metres <-> UE cm round trip and the target score', () => {
  const size: [number, number] = [177, 124];
  const p: [number, number] = [12, 21];
  const u = toUE(p, size);
  assert.deepEqual(u, [4100, -7650]); // north = +X (y south -> -X), east = +Y
  const back = fromUE(u, size);
  assert.ok(Math.abs(back[0] - p[0]) < 1e-9 && Math.abs(back[1] - p[1]) < 1e-9);
  assert.equal(targetScore(5, [3, 7]), 1);
  assert.equal(targetScore(9, [3, 7]), 0.5);
  assert.equal(targetScore(20, [3, 7]), 0);
  assert.equal(targetScore(1, undefined), 1);
});

test('bug 2026-11-02 (ladders sunk into the walls): every ladder yaw is UE +X away from its wall, from pos towards the foot (LADDERS.md 3.1)', () => {
  for (let seed = 1; seed <= 4; seed++) {
    const l = generateLayout({ profile, seed });
    assert.ok(l.ladders.length >= 1, `seed ${seed}: ladders`);
    for (const ld of l.ladders) {
      const want = ((Math.atan2(ld.foot[1] - ld.pos[1], ld.foot[0] - ld.pos[0]) * 180) / Math.PI + 360) % 360;
      const delta = Math.abs(((ld.yaw - want + 540) % 360) - 180);
      assert.ok(delta < 0.5, `seed ${seed} ${ld.id}: yaw ${ld.yaw}, pos -> foot ${want.toFixed(1)}`);
      // the top lies behind the rung plane (on the wall's side), the foot in front of it
      const out: [number, number] = [Math.cos((ld.yaw * Math.PI) / 180), Math.sin((ld.yaw * Math.PI) / 180)];
      assert.ok((ld.top[0] - ld.pos[0]) * out[0] + (ld.top[1] - ld.pos[1]) * out[1] < 0, `seed ${seed} ${ld.id}: the top behind the rungs`);
    }
  }
});

test('a generated layout is deterministic for a seed and differs between seeds', () => {
  const a = generateLayout({ profile, seed: 7 });
  const b = generateLayout({ profile, seed: 7 });
  const c = generateLayout({ profile, seed: 8 });
  assert.equal(JSON.stringify(a), JSON.stringify(b));
  assert.notEqual(JSON.stringify(a), JSON.stringify(c));
  assert.equal(a.seed, 7);
  assert.equal(a.profile, 'ref_01_02_depot');
});

test('every seed 1..8 gives a valid layout whose ground layer is a valid lab layout with a clean audit of its route points', () => {
  for (let seed = 1; seed <= 8; seed++) {
    const l = generateLayout({ profile, seed });
    assert.deepEqual(validateGenLayout(l), [], `seed ${seed}`);
    const ground = toFormationLayout(l, 'ground');
    assert.deepEqual(validateLayout(ground), [], `seed ${seed} ground layer`);
    const errors = auditLayout(ground, config).filter((i) => i.severity === 'error');
    assert.deepEqual(errors, [], `seed ${seed} audit`);
    assert.ok(l.ladders.every((x) => x.rungs >= 7), 'ladders climbable');
    assert.ok(l.buildings.some((b) => b.set_piece === 'ruined_two_level' && b.roof_holes.length === 2 && b.floor_holes.length === 1), 'the ruin has its holes');
    assert.ok(l.cut && l.cut.crossings.length === 2, 'the cut with two crossings');
    assert.equal(l.routes.filter((r) => r.mandatory).length, 1);
  }
});

test('the mandatory route and the variants are completable (structural check, no sim) for seeds 1..8', () => {
  for (let seed = 1; seed <= 8; seed++) {
    const l = generateLayout({ profile, seed });
    const ev = evaluateLayout({ layout: l, profile, config, noSim: true });
    assert.ok(ev.route.completable, `seed ${seed}: ${ev.route.fail}`);
    for (const v of ev.variants) assert.ok(v.completable, `seed ${seed} ${v.id}: ${v.fail}`);
    const [lo, hi] = profile.targets.route_length_m;
    assert.ok(ev.route.length_m >= lo && ev.route.length_m <= hi, `seed ${seed} route ${ev.route.length_m} m (target ${lo}..${hi})`);
    assert.ok(ev.similarity.score >= 0.7, `seed ${seed} similarity ${ev.similarity.score}`);
    assert.ok(ev.summary.length <= 30);
  }
});

test('the simulator walk reports the terrain classes and formations of the mandatory route', () => {
  const l = generateLayout({ profile, seed: 1 });
  const ev = evaluateLayout({ layout: l, profile, config });
  assert.ok(ev.route.completable);
  const terrain = Object.values(ev.mechanics.terrain).reduce((s, v) => s + (v ?? 0), 0);
  assert.ok(terrain > 0.99 && terrain < 1.01, 'terrain shares sum to 1');
  assert.ok((ev.mechanics.terrain.open ?? 0) < 0.6, 'the route hugs the facades: not mostly open');
  assert.ok(Object.keys(ev.mechanics.formation).length >= 2, 'more than one formation seen');
  assert.ok(ev.mechanics.simSeconds > 60);
  assert.ok(ev.sightlines.lanes_over_80m >= 2, 'the marksman lanes');
  assert.ok(ev.sightlines.max_m < 180, 'rays end at the map edge');
  assert.ok(ev.score > 0.7);
});

test('verticality: the layers, the links of the variants, and a route changing layers without a link fails the check', () => {
  const l = generateLayout({ profile, seed: 3 });
  const layers = layersOf(l);
  assert.ok(layers.includes('ground') && layers.includes('cut') && layers.includes('roof:B_gallery') && layers.includes('floor:B_ruin:1') && layers.includes('roof:B_ruin'));
  const v3 = l.routes.find((r) => r.id === 'V3_cut')!;
  assert.deepEqual(v3.links?.map((k) => k.via.startsWith('LAD_cut') || k.via === 'X_centre'), [true, true]);
  const roof = toFormationLayout(l, 'roof:B_gallery');
  assert.equal(roof.walls.length, 4, 'a roof layer is its parapet');
  assert.ok(roof.routes.some((r) => r.id.startsWith('V1_gallery')), 'the roof leg of V1 lives on the roof layer');
  // break the link
  const broken = structuredClone(l);
  const bv1 = broken.routes.find((r) => r.id === 'V1_gallery')!;
  bv1.links = [];
  const ev = evaluateLayout({ layout: broken, profile, config, noSim: true });
  const rep = ev.variants.find((v) => v.id === 'V1_gallery')!;
  assert.equal(rep.completable, false);
  assert.match(rep.fail ?? '', /no link ground -> roof:B_gallery/);
  // flat: no ladders, no roofs
  const flat = generateLayout({ profile, seed: 3, params: { verticality: 0 } });
  assert.equal(flat.ladders.length, 0);
  assert.ok(flat.buildings.every((b) => !b.roof_walkable));
  assert.ok(!flat.routes.some((r) => r.id === 'V1_gallery' || r.id === 'V3_cut'));
  assert.deepEqual(validateGenLayout(flat), []);
});

test('height levels: 3 m steps, ladders sized to the level difference, every layer on a level (the hall roofs on +3), the route visits 0 / +1 / +2 in the ruin', () => {
  assert.equal(LEVEL_HEIGHT, 300);
  assert.deepEqual(ladderForLevels(1), { rungs: 8, plinth_cm: 8.8 });
  assert.deepEqual(ladderForLevels(2), { rungs: 16, plinth_cm: 17.6 });
  const l = generateLayout({ profile, seed: 4 });
  assert.deepEqual(levelsOf(l), [-1, 0, 1, 2, 3]);
  assert.equal(layerLevel(l, 'cut'), -1);
  assert.equal(layerLevel(l, 'floor:B_ruin:1'), 1);
  assert.equal(layerLevel(l, 'roof:B_ruin'), 2);
  assert.equal(layerLevel(l, 'roof:B_gallery'), 1);
  for (const ld of l.ladders) assert.ok(Math.abs(ld.rungs * 36.4 + ld.plinth_cm - (ld.level_to - ld.level_from) * LEVEL_HEIGHT) < 1, ld.id);
  assert.ok(l.buildings.filter((b) => b.roof_walkable).every((b) => b.height === b.floors * LEVEL_HEIGHT), 'walkable roofs sit on their level');
  assert.ok(l.buildings.some((b) => b.hollow && b.stairs.some((s) => s.from === 0 && s.to === HALL_ROOF_LEVEL)), 'a hollow hall climbs ground -> roof in one flight');
  assert.equal(l.cut?.floor_z, -300);
  const ev = evaluateLayout({ layout: l, profile, config, noSim: true });
  assert.deepEqual(ev.mechanics.levels, [0, 1, 2]);
  assert.equal(ev.counts.height_levels, 5);
});

const HALLS = ['B_hall_n', 'B_warehouse', 'B_steelworks'];
const flatDist = (a: [number, number], b: [number, number]) => Math.hypot(a[0] - b[0], a[1] - b[1]);
const hallWalls = (l: GenLayout, building: string) => l.walls.filter((w) => w.building === building && (w.floor ?? 0) === 0 && w.kind === 'building');

test('game designer 2026-11-02 (6): the hall roofs sit on +3 = 9 m, their stairwells and the ruin external stair are sized to the level difference', () => {
  assert.equal(HALL_ROOF_LEVEL, 3);
  for (let seed = 1; seed <= 8; seed++) {
    const l = generateLayout({ profile, seed });
    for (const id of HALLS) {
      const b = l.buildings.find((x) => x.id === id)!;
      assert.equal(b.height, 900, `${seed} ${id} height`);
      assert.equal(layerLevel(l, `roof:${id}`), 3, `${seed} ${id} roof level`);
      const flight = b.stairs.find((s) => s.kind === 'stairwell')!;
      assert.equal(flight.to - flight.from, 3, `${seed} ${id}: one flight ground -> +3`);
      // 35 deg: run = 1.43 x 9 m
      assert.ok(Math.abs(flatDist(flight.pos, flight.top) - 3 * STAIR_RUN_PER_RISE * LEVEL_HEIGHT) < 2, `${seed} ${id} run ${flatDist(flight.pos, flight.top)}`);
      assert.ok(hallWalls(l, id).every((w) => w.height === 900), `${seed} ${id}: the walls reach the roof`);
    }
    const ruin = l.buildings.find((x) => x.id === 'B_ruin')!;
    assert.equal(ruin.floors, 2, 'the ruin keeps its two storeys (+2)');
    const ext = ruin.stairs.find((s) => s.kind === 'external')!;
    assert.equal(ext.to, ruin.floors);
    assert.ok(Math.abs(flatDist(ext.pos, ext.mid!) - ruin.floors * STAIR_RUN_PER_RISE * LEVEL_HEIGHT) < 2, 'the external ramp is sized to its rise');
    for (const ld of l.ladders) assert.ok(Math.abs(ld.rungs * 36.4 + ld.plinth_cm - (ld.level_to - ld.level_from) * LEVEL_HEIGHT) < 1, `${seed} ${ld.id}`);
  }
  assert.deepEqual(ladderForLevels(3), { rungs: 24, plinth_cm: 26.4 }, 'a 9 m ladder, should one ever be placed');
});

test('game designer 2026-11-02 (4): factory chimneys are never climbable towers - no ladder, no walkable top, no level - for every seed and verticality', () => {
  for (let seed = 1; seed <= 6; seed++) {
    for (const verticality of [0, 1, 2, 3] as const) {
      const l = generateLayout({ profile, seed, params: { verticality } });
      const chimneys = l.buildings.filter((b) => b.set_piece === 'chimney');
      assert.equal(chimneys.length, 2, `seed ${seed} V${verticality}: two chimneys`);
      for (const c of chimneys) {
        assert.ok(!c.roof_walkable && !c.enterable && c.stairs.length === 0, `${c.id} closed`);
        assert.ok(!l.ladders.some((ld) => ld.to.includes(c.id) || ld.from.includes(c.id)), `${c.id}: no ladder`);
        assert.ok(!layersOf(l).some((x) => x.includes(c.id)), `${c.id}: no layer`);
        assert.ok(c.height >= 1500, `${c.id}: a tall sight blocker`);
      }
      assert.ok(!l.buildings.some((b) => b.set_piece === 'tower'), 'no tower set-piece is generated');
      assert.deepEqual(validateGenLayout(l), [], `seed ${seed} V${verticality}`);
    }
  }
  // the validator refuses a climbable chimney
  const bad = structuredClone(generateLayout({ profile, seed: 1 }));
  const c = bad.buildings.find((b) => b.set_piece === 'chimney')!;
  c.roof_walkable = true;
  bad.ladders.push({ ...bad.ladders[0], id: 'LAD_bad', to: `roof:${c.id}`, level_to: 1 });
  const errors = validateGenLayout(bad);
  assert.ok(errors.some((e) => e.includes(`${c.id}: a chimney is an obstacle`)), errors.join('; '));
  assert.ok(errors.some((e) => e.includes(`${c.id}: a chimney never gets a ladder`)), errors.join('; '));
});

test('game designer 2026-11-02 (5): the canal-floor variant is a deliberate kill zone - marked in the layout, reported as intended, never a defect', () => {
  const l = generateLayout({ profile, seed: 1 });
  const v3 = l.routes.find((r) => r.id === 'V3_cut')!;
  assert.equal(v3.intent, 'kill_zone');
  assert.match(v3.intent_note ?? '', /deliberate kill zone/);
  assert.equal(l.zones.find((z) => z.id === 'Z_cut')?.intent, 'kill_zone');
  assert.ok(!mandatoryRoute(l).intent, 'the mandatory route is not a kill zone');
  const ev = evaluateLayout({ layout: l, profile, config, noSim: true });
  const k = ev.killZones.find((x) => x.route === 'V3_cut')!;
  assert.ok(k && k.intended && k.completable);
  assert.ok(k.share > 0.5 && k.in_zone_m > 30, `in the zone ${k.in_zone_m} m (${k.share})`);
  assert.ok(k.ways_out >= 2, 'the ramp + the ladders');
  assert.equal(ev.variants.find((v) => v.id === 'V3_cut')?.intent, 'kill_zone');
  assert.ok(ev.summary.some((line) => line.startsWith('kill zone (INTENDED, not a defect): V3_cut')));
  assert.ok(ev.summary.some((line) => line.includes('V3_cut ok') && line.includes('[kill zone by design]')));
  // the critics are told
  assert.match(criticPrompt({ layoutName: l.name, evaluation: ev, profile }), /"intent":"kill_zone"/);
  assert.ok(mockCritic(ev).reasons.some((r) => r.includes('intended kill zone')));
  // the mandatory route can never be one
  const bad = structuredClone(l);
  mandatoryRoute(bad).intent = 'kill_zone';
  assert.ok(validateGenLayout(bad).some((e) => e.includes('never a kill zone')));
});

test('elevated vision: a hall roof on +3 sees over the 6-9 m blocks - further than the same roof on +2 and than the street', () => {
  const l = generateLayout({ profile, seed: 1 });
  const ev = evaluateLayout({ layout: l, profile, config, noSim: true });
  const at3 = ev.roofViews.find((r) => r.id === 'B_hall_n')!;
  assert.equal(at3.level, 3);
  // the same hall with its roof on +2 (the greybox before 2026-11-02)
  const low = structuredClone(l);
  const b = low.buildings.find((x) => x.id === 'B_hall_n')!;
  b.floors = 2; b.height = 600; b.stairs = b.stairs.map((s) => ({ ...s, to: 2 }));
  for (const w of hallWalls(low, 'B_hall_n')) w.height = 600;
  const at2 = evaluateLayout({ layout: low, profile, config, noSim: true }).roofViews.find((r) => r.id === 'B_hall_n')!;
  assert.equal(at2.level, 2);
  assert.ok(at3.mean_m > at2.mean_m, `+3 mean ${at3.mean_m} m vs +2 ${at2.mean_m} m`);
  assert.ok(at3.open_share > at2.open_share, `+3 open ${at3.open_share} vs +2 ${at2.open_share}`);
  assert.ok(at3.mean_m > ev.sightlines.open_p50_m, 'the roof is more open than the street');
  for (const id of HALLS) assert.ok(ev.roofViews.some((r) => r.id === id && r.level === 3));
  assert.ok(!ev.roofViews.some((r) => r.id.startsWith('B_chimney')));
  assert.ok(ev.summary.some((line) => line.startsWith('roof views')));
});

test('a route leg towards an open roof / floor hole stops on its rim (the hole is open in the built map), the drop lands under the hole', () => {
  for (let seed = 1; seed <= 8; seed++) {
    const l = generateLayout({ profile, seed });
    const r = mandatoryRoute(l);
    const ruin = l.buildings.find((b) => b.id === 'B_ruin')!;
    const holeLinks = (r.links ?? []).filter((x) => x.via.startsWith('hole_'));
    assert.equal(holeLinks.length, 2, `seed ${seed}: the roof and the floor drop`);
    for (const k of holeLinks) {
      const m = /^hole_(roof|floor)_(\d+)$/.exec(k.via)!;
      const hole = m[1] === 'roof' ? ruin.roof_holes[Number(m[2])] : ruin.floor_holes[Number(m[2])].pos;
      const rim = r.points[k.at - 1];
      const d = flatDist(rim, hole);
      assert.ok(d >= HOLE_HALF_CM + 35 && Math.abs(d - HOLE_RIM_M * 100) < 2, `seed ${seed} ${k.via}: the rim ${d} cm from the hole centre`);
      assert.ok(flatDist(r.points[k.at], hole) < HOLE_HALF_CM, `seed ${seed} ${k.via}: the drop lands under the hole`);
    }
  }
});

test('a route point moved into a closed building is reported as the failing leg (an enterable one is reachable through its doors)', () => {
  const l = generateLayout({ profile, seed: 2 });
  const inside = (id: string): [number, number] => { const b = l.buildings.find((x) => x.id === id)!; return [(b.footprint[0][0] + b.footprint[1][0]) / 2, (b.footprint[0][1] + b.footprint[1][1]) / 2]; };
  const open = structuredClone(l);
  mandatoryRoute(open).points[3] = inside('B_warehouse');
  assert.equal(evaluateLayout({ layout: open, profile, config, noSim: true }).route.completable, true, 'the warehouse has doors');
  const broken = structuredClone(l);
  mandatoryRoute(broken).points[3] = inside('B_office_w');
  const ev = evaluateLayout({ layout: broken, profile, config, noSim: true });
  assert.equal(ev.route.completable, false);
  assert.match(ev.route.fail ?? '', /leg 3: (unreachable on ground|point in a wall)/);
  assert.ok(ev.score < 0.5);
  assert.ok(ev.summary.some((line) => line.includes('FAILS at')));
});

test('the size and density parameters scale the layout', () => {
  const small = generateLayout({ profile, seed: 1, params: { size_m: [120, 90], density: 0.5, variants: 1 } });
  assert.deepEqual(validateGenLayout(small), []);
  assert.deepEqual(small.bounds.max, [4500, 6000]);
  assert.equal(small.routes.length, 2);
  const big = generateLayout({ profile, seed: 1 });
  assert.ok(small.covers.length < big.covers.length / 2);
  const ev = evaluateLayout({ layout: small, profile, config, noSim: true });
  assert.ok(ev.route.completable, ev.route.fail);
});

test('game designer 2026-11-03: no building on the cut walls - the footprint keeps the wall thickness + margin clear of the cut', () => {
  const l = generateLayout({ profile, seed: 1 });
  assert.ok(l.cut, 'the reference profile has a cut');
  const wallT = Math.max(...l.walls.filter((w) => w.kind === 'cut_wall').map((w) => w.thickness));
  const ys = l.cut.poly.map((p) => p[1]), xs = l.cut.poly.map((p) => p[0]);
  const yMin = Math.min(...ys), yMax = Math.max(...ys), xMid = (Math.min(...xs) + Math.max(...xs)) / 2;
  const withShed = (y0: number, y1: number): GenLayout => ({
    ...l, buildings: l.buildings.map((b) => b.id === 'B_shed' ? { ...b, footprint: [[xMid - 500, y0], [xMid + 500, y1]] as [[number, number], [number, number]] } : b),
  });
  const reach = wallT + CUT_BUILDING_MARGIN_CM;
  // on the wall: the north wall spans yMin - wallT .. yMin
  const bad = validateGenLayout(withShed(yMin - wallT - 1000, yMin - wallT + 50)).filter((m) => m.includes('B_shed') && m.includes('cut wall'));
  assert.equal(bad.length, 1, 'a shed on the cut wall fails with the cut-wall message');
  // inside the margin: still fails
  assert.equal(validateGenLayout(withShed(yMin - reach - 1000, yMin - reach + 10)).filter((m) => m.includes('cut wall')).length, 1);
  // margin respected: passes the rule
  assert.deepEqual(validateGenLayout(withShed(yMin - reach - 1000, yMin - reach)).filter((m) => m.includes('cut wall')), []);
  assert.deepEqual(validateGenLayout(withShed(yMax + reach, yMax + reach + 1000)).filter((m) => m.includes('cut wall')), []);
});

test('game designer 2026-11-03: generated layouts keep every building off the cut walls (seeds 1..8)', () => {
  for (let seed = 1; seed <= 8; seed++) {
    const l = generateLayout({ profile, seed });
    assert.deepEqual(validateGenLayout(l).filter((m) => m.includes('cut wall')), [], `seed ${seed}`);
  }
});

test('T11: Depot x2 extend mode preserves the frozen layout byte-identically in coordinates (seeds 1..3)', () => {
  const baseLayout = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json'), 'utf8')) as GenLayout;
  for (let seed = 1; seed <= 3; seed++) {
    const ext = extendLayout({ baseLayout, seed, profile });
    for (const b of baseLayout.buildings) {
      const eb = ext.buildings.find((x) => x.id === b.id);
      assert.ok(eb, `seed ${seed}: building ${b.id} missing`);
      assert.deepEqual(eb.footprint, b.footprint, `seed ${seed}: building ${b.id} footprint changed`);
      assert.deepEqual(eb.doors, b.doors);
      assert.deepEqual(eb.stairs, b.stairs);
      assert.deepEqual(eb.roof_holes, b.roof_holes);
      assert.deepEqual(eb.floor_holes, b.floor_holes);
    }
    for (const w of baseLayout.walls) {
      const ew = ext.walls.find((x) => x.id === w.id);
      assert.ok(ew, `seed ${seed}: wall ${w.id} missing`);
      assert.deepEqual(ew.a, w.a);
      assert.deepEqual(ew.b, w.b);
    }
    for (const l of baseLayout.ladders) {
      const el = ext.ladders.find((x) => x.id === l.id);
      assert.ok(el, `seed ${seed}: ladder ${l.id} missing`);
      assert.deepEqual(el.pos, l.pos);
      assert.deepEqual(el.foot, l.foot);
      assert.deepEqual(el.top, l.top);
      assert.equal(el.yaw, l.yaw);
    }
    for (const c of baseLayout.covers) {
      const ec = ext.covers.find((x) => x.id === c.id);
      assert.ok(ec, `seed ${seed}: cover ${c.id} missing`);
      assert.deepEqual(ec.pos, c.pos);
    }
    for (const s of baseLayout.spawn_points) {
      const es = ext.spawn_points.find((x) => x.id === s.id);
      assert.ok(es, `seed ${seed}: spawn ${s.id} missing`);
      assert.deepEqual(es.pos, s.pos);
    }
  }
});

test('T11: Depot x2 extended layout passes all validators, audit, and keeps buildings off cut walls (seeds 1..3)', () => {
  const baseLayout = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json'), 'utf8')) as GenLayout;
  for (let seed = 1; seed <= 3; seed++) {
    const ext = extendLayout({ baseLayout, seed, profile });
    assert.deepEqual(validateGenLayout(ext), [], `seed ${seed} validateGenLayout`);
    const ground = toFormationLayout(ext, 'ground');
    assert.deepEqual(validateLayout(ground), [], `seed ${seed} validateLayout ground`);
    const auditErrors = auditLayout(ground, config).filter((i) => i.severity === 'error');
    assert.deepEqual(auditErrors, [], `seed ${seed} audit errors`);
  }
});

test('T11: Depot x2 mandatory bot route covers both frozen part and extended part, and is completable (seeds 1..3)', () => {
  const baseLayout = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json'), 'utf8')) as GenLayout;
  for (let seed = 1; seed <= 3; seed++) {
    const ext = extendLayout({ baseLayout, seed, profile });
    const r = mandatoryRoute(ext);
    const oldPart = r.points.filter((p) => p[1] <= 8850);
    const newPart = r.points.filter((p) => p[1] > 8850);
    assert.ok(oldPart.length >= 5, `seed ${seed}: route old points ${oldPart.length}`);
    assert.ok(newPart.length >= 5, `seed ${seed}: route new points ${newPart.length}`);
    const ev = evaluateLayout({ layout: ext, profile, config, noSim: true });
    assert.ok(ev.route.completable, `seed ${seed} route: ${ev.route.fail}`);
    for (const v of ev.variants) assert.ok(v.completable, `seed ${seed} variant ${v.id}: ${v.fail}`);
  }
});

test('T11: Depot x2 extendLayout is deterministic for a seed and differs between seeds', () => {
  const baseLayout = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json'), 'utf8')) as GenLayout;
  const a = extendLayout({ baseLayout, seed: 7 });
  const b = extendLayout({ baseLayout, seed: 7 });
  const c = extendLayout({ baseLayout, seed: 8 });
  assert.equal(JSON.stringify(a), JSON.stringify(b));
  assert.notEqual(JSON.stringify(a), JSON.stringify(c));
  assert.equal(a.seed, 7);
});
