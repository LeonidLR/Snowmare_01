// Training Ground / level generator v2, stage 1 (task 070): region re-roll + locked districts + undo history. `npm test`.
// Inputs: the Depot reference profile and generateLayout(seed 1) (pure); the accepted Depot map is READ only (old-file loading).
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import type { Vec2 } from '../movement/geometry.ts';
import type { RouteReport } from './evaluate.ts';
import { checkLayoutRoutes } from './evaluate.ts';
import { generateLayout } from './generate.ts';
import { classifyObstacleHeight } from './obstacleHeights.ts';
import { planTransform, toScreen, toWorld } from './planView.ts';
import type { Profile } from './profile.ts';
import { runObstacleHeightGate } from './props/obstacleHeightGate.ts';
import { canRedo, canUndo, HISTORY_MIN_STEPS, historyInit, historyPush, historyRedo, historyUndo } from './regenHistory.ts';
import {
  addLock, isInfrastructureZone, isUsableZone, layoutGroups, pointInZone, polygonArea, polygonsOverlap, regenerateRegion, regenStateOf,
  removeLock, RETRY_SEED_STEP, selectByCentre, selectZone, shapePolygon, simplifyLasso, spliceRoute,
} from './regionRegen.ts';
import type { GenLayout, GenRoute } from './schema.ts';
import { REGEN_STATE_VERSION, validateGenLayout, validateRegenState } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
const base = generateLayout({ profile, seed: 1 });
const hash = (l: GenLayout) => crypto.createHash('sha256').update(JSON.stringify(l)).digest('hex');
const rectZone = (a: Vec2, b: Vec2) => shapePolygon({ kind: 'rect', a, b });
/** South-east block: the ruin, the steelworks, a chimney; 3 points of the mandatory route. */
const ZONE_SE = rectZone([-6000, 2000], [-2000, 9000]);
type Arr = 'buildings' | 'walls' | 'ladders' | 'covers' | 'zones' | 'spawn_points' | 'ambush_points' | 'patrol_routes';
const ARRS: Arr[] = ['buildings', 'walls', 'ladders', 'covers', 'zones', 'spawn_points', 'ambush_points', 'patrol_routes'];

test('regen shapes: rectangle normalised, area, usable zone, lasso thinning, concave point-in-polygon', () => {
  assert.deepEqual(shapePolygon({ kind: 'rect', a: [100, 300], b: [0, 0] }), [[0, 0], [100, 0], [100, 300], [0, 300]]);
  assert.equal(polygonArea(rectZone([0, 0], [200, 300])), 60000);
  assert.equal(isUsableZone(rectZone([0, 0], [50, 50])), false); // 0.25 m^2
  assert.equal(isUsableZone(rectZone([0, 0], [100, 100])), true);
  assert.equal(isUsableZone([[0, 0], [1000, 0]]), false);
  assert.deepEqual(simplifyLasso([[0, 0], [10, 0], [60, 0], [61, 0], [200, 0]], 50), [[0, 0], [60, 0], [200, 0]]);
  // an L-shaped lasso: the notch is outside
  const L: Vec2[] = [[0, 0], [1000, 0], [1000, 400], [400, 400], [400, 1000], [0, 1000]];
  assert.equal(pointInZone([200, 200], L), true);
  assert.equal(pointInZone([800, 200], L), true);
  assert.equal(pointInZone([800, 800], L), false);
});

test('regen overlap: disjoint, contained, crossing, touching', () => {
  const a = rectZone([0, 0], [100, 100]);
  assert.equal(polygonsOverlap(a, rectZone([200, 200], [300, 300])), false);
  assert.equal(polygonsOverlap(a, rectZone([20, 20], [40, 40])), true);   // contained
  assert.equal(polygonsOverlap(a, [[-50, 50], [150, 40], [150, 60], [-50, 60]]), true); // a bar crossing, no vertex inside
  assert.equal(polygonsOverlap(a, rectZone([100.5, 0], [200, 100])), false); // a neighbour 5 mm away
});

test('plan transform: world -> screen -> world round trip (mirrors LayoutView: X up, Y right)', () => {
  const t = planTransform(base.bounds, 800, 520);
  for (const p of [[0, 0], [5000, -3000], [-6100, 8800]] as Vec2[]) {
    const q = toWorld(t, toScreen(t, p));
    assert.ok(Math.abs(q[0] - p[0]) < 1e-6 && Math.abs(q[1] - p[1]) < 1e-6);
  }
  assert.ok(toScreen(t, [6000, 0])[1] < toScreen(t, [-6000, 0])[1], 'north (+X) is drawn up');
  assert.ok(toScreen(t, [0, -8000])[0] < toScreen(t, [0, 8000])[0], 'east (+Y) is drawn right');
});

test('regen groups: every object in exactly one group; buildings carry their walls + roof ladder; roads and the canal are fixed', () => {
  const groups = layoutGroups(base);
  for (const k of ARRS) {
    const seen = groups.flatMap((g) => (g.members[k] ?? []) as object[]);
    assert.equal(seen.length, base[k].length, k);
    assert.equal(new Set(seen).size, base[k].length, `${k}: no object in two groups`);
  }
  const gallery = groups.find((g) => g.key === 'building:B_gallery')!;
  assert.ok(gallery.members.ladders!.some((x) => x.to === 'roof:B_gallery'), 'the gallery ladder moves with the gallery');
  assert.ok(gallery.members.walls!.length > 0 && gallery.members.walls!.every((w) => w.building === 'B_gallery'));
  assert.ok(groups.filter((g) => g.category === 'wall' && g.members.walls![0].kind === 'cut_wall').every((g) => g.fixed));
  assert.ok(groups.filter((g) => g.category === 'ladder' && g.members.ladders![0].from === 'cut').every((g) => g.fixed));
  const zones = groups.filter((g) => g.category === 'zone');
  assert.ok(zones.some((g) => g.id === 'Z_cut' && g.fixed) && zones.some((g) => g.id.startsWith('Z_ST_') && g.fixed));
  assert.ok(zones.some((g) => !g.fixed));
});

test('regen select-by-centre: a building is taken whole only when its centre is inside; locks and roads are kept', () => {
  const ruin = base.buildings.find((b) => b.id === 'B_ruin')!;
  const [a, b] = ruin.footprint;
  const cx = (a[0] + b[0]) / 2;
  // the zone covers the ruin's northern half only (centre outside) -> not selected; past the centre -> selected
  const half = rectZone([cx + 50, a[1] - 100], [b[0] + 100, b[1] + 100]);
  const most = rectZone([cx - 50, a[1] - 100], [b[0] + 100, b[1] + 100]);
  const groups = layoutGroups(base);
  assert.ok(!selectByCentre(groups, half, []).inside.some((g) => g.id === 'B_ruin'));
  assert.ok(selectByCentre(groups, most, []).inside.some((g) => g.id === 'B_ruin'));
  const lock = rectZone([a[0] - 100, a[1] - 100], [b[0] + 100, b[1] + 100]);
  const sel = selectByCentre(groups, most, [{ id: 'LOCK_1', name: 'ruin', poly: lock }]);
  assert.ok(sel.locked.some((g) => g.id === 'B_ruin') && !sel.inside.some((g) => g.id === 'B_ruin'));
  const all = selectZone(base, rectZone(base.bounds.min, base.bounds.max));
  assert.ok(all.fixed.some((g) => g.id === 'Z_cut') && !all.inside.some((g) => g.fixed));
});

test('regen generateLayout: no skeletonSeed = the classic layout; a skeletonSeed keeps the streets + the canal, re-draws the blocks', () => {
  assert.equal(hash(generateLayout({ profile, seed: 1, skeletonSeed: 1 })), hash(base));
  const other = generateLayout({ profile, seed: 5, skeletonSeed: 1 });
  assert.deepEqual(other.cut, base.cut);
  assert.deepEqual(other.zones.filter(isInfrastructureZone), base.zones.filter(isInfrastructureZone));
  assert.notDeepEqual(other.buildings.map((b) => b.footprint), base.buildings.map((b) => b.footprint));
});

test('regen re-roll: everything outside the zone stays byte-identical, the inside changes, routes still completable', () => {
  const r = regenerateRegion({ layout: base, zone: ZONE_SE, seed: 101, profile });
  assert.equal(r.ok, true, r.message);
  assert.ok(r.replaced.includes('B_ruin') && r.added.length > 0);
  assert.equal(r.skeletonMatch, true);
  const removed = new Set<object>(selectZone(base, ZONE_SE).inside.flatMap((g) => ARRS.flatMap((k) => (g.members[k] ?? []) as object[])));
  for (const k of ARRS) {
    const kept = (base[k] as object[]).filter((x) => !removed.has(x));
    assert.equal(JSON.stringify((r.layout[k] as object[]).slice(0, kept.length)), JSON.stringify(kept), `${k}: kept objects identical, in order`);
  }
  for (const k of ['schema', 'name', 'profile', 'seed', 'params', 'bounds', 'ground_z', 'spawn', 'end', 'cut', 'pressure'] as const) assert.deepEqual(r.layout[k], base[k], k);
  assert.notEqual(JSON.stringify(r.layout.covers), JSON.stringify(base.covers), 'the covers inside the zone are re-drawn');
  // the routes that do not enter the zone are the same objects; the mandatory one is spliced and completable
  assert.equal(r.layout.routes.find((x) => x.id === 'V1_gallery'), base.routes.find((x) => x.id === 'V1_gallery'));
  assert.ok(checkLayoutRoutes(r.layout).find((x) => x.mandatory)!.completable);
  assert.deepEqual(validateGenLayout(r.layout), validateGenLayout(base), 'no new schema errors');
  assert.equal(r.layout.regen?.rerolls.length, 1);
  assert.equal(r.layout.regen?.rerolls[0].seed, 101);
  // ids stay unique
  for (const k of ARRS) assert.equal(new Set((r.layout[k] as { id: string }[]).map((x) => x.id)).size, r.layout[k].length, `${k} ids unique`);
});

test('regen determinism: same map + zone + seed -> the same JSON hash; another seed -> another result', () => {
  const a = regenerateRegion({ layout: base, zone: ZONE_SE, seed: 101, profile });
  const b = regenerateRegion({ layout: base, zone: ZONE_SE, seed: 101, profile });
  const c = regenerateRegion({ layout: base, zone: ZONE_SE, seed: 202, profile });
  assert.equal(hash(a.layout), hash(b.layout));
  assert.notEqual(hash(a.layout), hash(c.layout));
  console.log(`# determinism: seed 101 sha256 ${hash(a.layout)}; seed 202 sha256 ${hash(c.layout)}`);
});

test('regen locks: a locked district survives a re-roll whose zone overlaps it; nothing new intrudes; stored + reloaded', () => {
  const ruin = base.buildings.find((b) => b.id === 'B_ruin')!;
  const [a, b] = ruin.footprint;
  const lockPoly = rectZone([a[0] - 200, a[1] - 200], [b[0] + 200, b[1] + 200]);
  const locked = addLock(base, lockPoly, 'Руина');
  assert.equal(regenStateOf(locked).locks[0].id, 'LOCK_1');
  assert.equal(regenStateOf(locked).locks[0].name, 'Руина');
  assert.deepEqual(validateGenLayout(locked), validateGenLayout(base));
  // JSON round trip (what the accept endpoint writes / the page reloads)
  const reloaded = JSON.parse(JSON.stringify(locked)) as GenLayout;
  assert.equal(reloaded.regen?.version, REGEN_STATE_VERSION);
  for (const seed of [101, 202, 303]) {
    const r = regenerateRegion({ layout: reloaded, zone: ZONE_SE, seed, profile });
    assert.equal(r.ok, true, r.message);
    assert.ok(r.locked.includes('B_ruin'));
    assert.equal(JSON.stringify(r.layout.buildings.find((x) => x.id === 'B_ruin')), JSON.stringify(ruin), 'the locked building is untouched');
    for (const w of base.walls.filter((w) => w.building === 'B_ruin')) assert.ok(r.layout.walls.some((x) => JSON.stringify(x) === JSON.stringify(w)));
    const addedB = r.layout.buildings.filter((x) => r.added.includes(x.id));
    assert.ok(addedB.every((x) => !polygonsOverlap(rectZone(x.footprint[0], x.footprint[1]), lockPoly)), 'no new building inside the lock');
    assert.deepEqual(r.layout.regen?.locks, reloaded.regen?.locks, 'the lock is kept in the JSON');
  }
  assert.equal(regenStateOf(removeLock(locked, 'LOCK_1')).locks.length, 0);
  assert.equal(regenStateOf(addLock(locked, lockPoly)).locks[1].id, 'LOCK_2');
  assert.throws(() => addLock(base, [[0, 0], [10, 0], [10, 10]]));
});

test('regen schema: old files (no regen block) still load; a newer regen version is refused', () => {
  const accepted = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json'), 'utf8')) as GenLayout;
  assert.equal(accepted.regen, undefined);
  assert.deepEqual(regenStateOf(accepted), { version: REGEN_STATE_VERSION, locks: [], rerolls: [] });
  const r = regenerateRegion({ layout: accepted, zone: ZONE_SE, seed: 7, profile });
  assert.equal(r.ok, true, r.message);
  assert.deepEqual(validateRegenState({ version: 1, locks: [], rerolls: [] }), []);
  assert.ok(validateRegenState({ version: 99, locks: [], rerolls: [] }).some((e) => e.includes('newer')));
  assert.ok(validateRegenState({ version: 1, locks: [{ id: 'A', name: 'A', poly: [[0, 0], [1, 1]] }] }).length > 0);
  assert.ok(validateGenLayout({ ...base, regen: { version: 99, locks: [], rerolls: [] } }).some((e) => e.includes('newer')));
  assert.throws(() => regenStateOf({ ...base, regen: { version: 99, locks: [], rerolls: [] } }));
});

test('regen route gate: a broken route is rejected and retried with the next seed; all broken -> the map is unchanged', () => {
  let calls = 0;
  // the first call is the "before" check; the next two attempts break the mandatory route
  const flaky = (l: GenLayout): RouteReport[] => {
    const real = checkLayoutRoutes(l);
    const n = calls++;
    return n >= 1 && n <= 2 ? real.map((x) => (x.mandatory ? { ...x, completable: false, fail: 'stub break' } : x)) : real;
  };
  const r = regenerateRegion({ layout: base, zone: ZONE_SE, seed: 101, profile, checkRoutes: flaky });
  assert.equal(r.ok, true);
  assert.equal(r.retries, 2);
  assert.equal(r.seed, 101 + 2 * RETRY_SEED_STEP);
  assert.deepEqual(r.attempts.map((x) => x.ok), [false, false, true]);
  let first = true;
  const broken = (l: GenLayout): RouteReport[] => {
    const real = checkLayoutRoutes(l);
    if (first) { first = false; return real; }
    return real.map((x) => (x.mandatory ? { ...x, completable: false, fail: 'stub break' } : x));
  };
  const f = regenerateRegion({ layout: base, zone: ZONE_SE, seed: 101, profile, checkRoutes: broken, maxRetries: 3 });
  assert.equal(f.ok, false);
  assert.equal(f.layout, base, 'the input layout is returned unchanged');
  assert.equal(f.retries, 4);
});

test('regen seams + heights: roads and the canal are untouched by a whole-map re-roll; new obstacles use the height classes', () => {
  const whole = rectZone(base.bounds.min, base.bounds.max);
  for (const seed of [11, 22]) {
    const r = regenerateRegion({ layout: base, zone: whole, seed, profile });
    assert.equal(r.ok, true, r.message);
    assert.deepEqual(r.layout.cut, base.cut, 'the canal and its crossings are kept');
    assert.deepEqual(r.layout.zones.filter(isInfrastructureZone), base.zones.filter(isInfrastructureZone), 'the streets are kept');
    assert.deepEqual(r.layout.walls.filter((w) => w.kind === 'cut_wall'), base.walls.filter((w) => w.kind === 'cut_wall'));
    const roads = [...base.zones.filter(isInfrastructureZone).map((z) => z.poly), base.cut!.poly];
    const shrunk = (f: [Vec2, Vec2]) => rectZone([f[0][0] + 20, f[0][1] + 20], [f[1][0] - 20, f[1][1] - 20]);
    for (const b of r.layout.buildings.filter((x) => r.added.includes(x.id))) assert.ok(roads.every((p) => !polygonsOverlap(shrunk(b.footprint), p)), `${b.id} blocks no road`);
    const added = new Set(r.added);
    const fresh: GenLayout = { ...r.layout, walls: r.layout.walls.filter((w) => added.has(w.id) || (w.building !== undefined && added.has(w.building))), covers: r.layout.covers.filter((c) => added.has(c.id)), buildings: r.layout.buildings.filter((b) => added.has(b.id)), mantle_edges: [] };
    assert.ok(fresh.walls.length > 0 && fresh.covers.length > 0);
    assert.deepEqual(runObstacleHeightGate(fresh).errors, [], 'every new obstacle height is a measured class');
    assert.ok(fresh.walls.every((w) => classifyObstacleHeight(w.height) !== undefined));
  }
});

test('regen splice: old points outside + fresh points inside, links re-indexed, border legs without links', () => {
  const inZone = (p: Vec2) => p[0] >= 10 && p[0] <= 20;
  const old: GenRoute = { id: 'R', mandatory: true, pace: 'walk', points: [[0, 0], [5, 0], [12, 0], [15, 0], [30, 0], [40, 0]], layers: ['ground', 'ground', 'ground', 'roof:X', 'ground', 'cut'], links: [{ at: 1, via: 'A' }, { at: 3, via: 'B' }, { at: 5, via: 'C' }] } as GenRoute;
  const fresh: GenRoute = { id: 'R', pace: 'walk', points: [[0, 0], [11, 0], [13, 0], [19, 0], [40, 0]], layers: ['ground', 'ground', 'roof:Y', 'roof:Y', 'ground'], links: [{ at: 2, via: 'L1' }, { at: 4, via: 'Z' }] } as GenRoute;
  const s = spliceRoute(old, fresh, inZone, new Map([['L1', 'L1_r7']]));
  assert.deepEqual(s.points, [[0, 0], [5, 0], [11, 0], [13, 0], [19, 0], [30, 0], [40, 0]]);
  assert.deepEqual(s.layers, ['ground', 'ground', 'ground', 'roof:Y', 'roof:Y', 'ground', 'cut']);
  assert.deepEqual(s.links, [{ at: 1, via: 'A' }, { at: 3, via: 'L1_r7' }, { at: 6, via: 'C' }]);
  assert.equal(spliceRoute(old, fresh, () => false), old, 'a route outside the zone is the same object');
  assert.deepEqual(spliceRoute(old, undefined, inZone).points, [[0, 0], [5, 0], [30, 0], [40, 0]]);
});

test('regen history: >= 20 undo steps, redo, a new step clears redo, the limit trims the oldest', () => {
  let h = historyInit(0, 'start', 5); // a limit below the floor is raised to HISTORY_MIN_STEPS
  assert.equal(h.limit, HISTORY_MIN_STEPS);
  assert.equal(canUndo(h), false);
  for (let i = 1; i <= 25; i++) h = historyPush(h, i, `reroll ${i}`);
  assert.equal(h.past.length, HISTORY_MIN_STEPS);
  for (let i = 0; i < HISTORY_MIN_STEPS; i++) h = historyUndo(h);
  assert.equal(h.present.state, 5);
  assert.equal(canUndo(h), false);
  assert.equal(historyUndo(h), h);
  h = historyRedo(historyRedo(h));
  assert.equal(h.present.state, 7);
  assert.equal(canRedo(h), true);
  h = historyPush(h, 100, 'lock');
  assert.equal(canRedo(h), false);
  assert.equal(historyUndo(h).present.state, 7);
});
