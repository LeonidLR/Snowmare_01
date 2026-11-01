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
import { evaluateLayout } from './evaluate.ts';
import { generateLayout } from './generate.ts';
import type { Profile } from './profile.ts';
import { fromUE, targetScore, toUE, validateProfile } from './profile.ts';
import { ladderForLevels, layerLevel, layersOf, LEVEL_HEIGHT, levelsOf, mandatoryRoute, toFormationLayout, validateGenLayout } from './schema.ts';

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

test('height levels: 3 m steps, ladders sized to the level difference, every layer on a level, the route visits -1 / 0 / +1 / +2 in the ruin and the cut variant', () => {
  assert.equal(LEVEL_HEIGHT, 300);
  assert.deepEqual(ladderForLevels(1), { rungs: 8, plinth_cm: 8.8 });
  assert.deepEqual(ladderForLevels(2), { rungs: 16, plinth_cm: 17.6 });
  const l = generateLayout({ profile, seed: 4 });
  assert.deepEqual(levelsOf(l), [-1, 0, 1, 2]);
  assert.equal(layerLevel(l, 'cut'), -1);
  assert.equal(layerLevel(l, 'floor:B_ruin:1'), 1);
  assert.equal(layerLevel(l, 'roof:B_ruin'), 2);
  assert.equal(layerLevel(l, 'roof:B_gallery'), 1);
  for (const ld of l.ladders) assert.ok(Math.abs(ld.rungs * 36.4 + ld.plinth_cm - (ld.level_to - ld.level_from) * LEVEL_HEIGHT) < 1, ld.id);
  assert.ok(l.buildings.filter((b) => b.roof_walkable).every((b) => b.height === b.floors * LEVEL_HEIGHT), 'walkable roofs sit on their level');
  assert.ok(l.buildings.some((b) => b.hollow && b.stairs.some((s) => s.from === 0 && s.to === 2)), 'a hollow hall climbs ground -> roof in one flight');
  assert.equal(l.cut?.floor_z, -300);
  const ev = evaluateLayout({ layout: l, profile, config, noSim: true });
  assert.deepEqual(ev.mechanics.levels, [0, 1, 2]);
  assert.equal(ev.counts.height_levels, 4);
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
