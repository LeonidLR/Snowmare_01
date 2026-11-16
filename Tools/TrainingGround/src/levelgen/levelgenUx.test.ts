// Training Ground / level generator v2 - designer UX round (task 072): the plan's ZOOM + PAN math (planView.ts) and the MANUAL
// START + GOALS edits (objectives.ts moveObjective / addGoal / removeGoal + the per-point errors and warnings). `npm test`.
// Inputs: the Depot reference profile + generateLayout(seed 1) (pure, nothing written).
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import type { Vec2 } from '../movement/geometry.ts';
import { pointInPoly } from '../movement/geometry.ts';
import { frameOf, lotsOf, rectCentre, rectFromUE, streetRectsM, V2 } from './blockTemplates.ts';
import { checkLayoutRoutes } from './evaluate.ts';
import { FULL, streetSides, subdivideBlock, touchingSides, unionRect } from './fullReroll.ts';
import { runObstacleHeightGate } from './props/obstacleHeightGate.ts';
import { addLock, layoutGroups, polygonsOverlap, regenerateRegion, selectRegion, shapePolygon } from './regionRegen.ts';
import { generateLayout } from './generate.ts';
import { addGoal, checkObjectives, groundNav, moveObjective, placeObjectives, removeGoal, START_ID } from './objectives.ts';
import { FIT_VIEW, PLAN_VIEW, clampZoom, panBy, planTransform, toScreen, toWorld, viewOfRect, viewTransform, wheelFactor, zoomAt } from './planView.ts';
import type { Profile } from './profile.ts';
import type { GenLayout } from './schema.ts';
import { rect, validateGenLayout } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
const base = generateLayout({ profile, seed: 1 });
const baseErrors = validateGenLayout(base);
const newErrors = (l: GenLayout) => validateGenLayout(l).filter((e) => !baseErrors.includes(e));
const near = (a: readonly number[], b: readonly number[], eps = 1e-6) => a.every((v, i) => Math.abs(v - b[i]) < eps);

// ---- 1. zoom + pan ------------------------------------------------------------------------------------------------------------

test('ux plan view: fit = the stage-1 transform; zoom around the cursor keeps the world point under it; pan; limits', () => {
  const b = planTransform(base.bounds, 900, 520);
  assert.deepEqual(viewTransform(b, FIT_VIEW), b, 'zoom 1 / pan 0 is the fitted plan');
  const cursor: [number, number] = [612, 233];
  const w0 = toWorld(viewTransform(b, FIT_VIEW), cursor);
  let v = FIT_VIEW;
  for (const f of [1.2, 1.2, 1.2, 3, 0.5, 1.7]) {
    v = zoomAt(v, cursor, f);
    assert.ok(near(toWorld(viewTransform(b, v), cursor), w0, 1e-6), `zoom ${v.zoom}: the point under the cursor stays`);
  }
  // round trip at any zoom / pan: the tools pick where the canvas draws
  const t = viewTransform(b, panBy(zoomAt(FIT_VIEW, [100, 100], 7.3), -340, 125));
  for (const p of [base.spawn.pos, base.end, base.bounds.min, base.bounds.max] as Vec2[]) assert.ok(near(toWorld(t, toScreen(t, p)), p, 1e-6));
  // pan moves the screen position by exactly the drag
  const q0 = toScreen(viewTransform(b, v), base.spawn.pos), q1 = toScreen(viewTransform(b, panBy(v, 40, -25)), base.spawn.pos);
  assert.ok(near([q1[0] - q0[0], q1[1] - q0[1]], [40, -25]));
  // limits + the wheel
  assert.equal(clampZoom(1e6), PLAN_VIEW.zoom_max);
  assert.equal(clampZoom(1e-6), PLAN_VIEW.zoom_min);
  let z = FIT_VIEW;
  for (let i = 0; i < 200; i++) z = zoomAt(z, cursor, wheelFactor(-100));
  assert.equal(z.zoom, PLAN_VIEW.zoom_max, 'the wheel stops at zoom_max');
  assert.ok(wheelFactor(-100) > 1 && wheelFactor(100) < 1 && wheelFactor(0) === 1, 'wheel up = in, down = out');
  assert.ok(Math.abs(wheelFactor(-100) * wheelFactor(100) - 1) < 1e-12, 'one notch in + one out = back');
  // zoom to a rectangle: its centre lands in the box centre
  const a: Vec2 = [base.spawn.pos[0] - 1500, base.spawn.pos[1] - 1500], c: Vec2 = [base.spawn.pos[0] + 1500, base.spawn.pos[1] + 1500];
  const vr = viewOfRect(b, a, c, 900, 520);
  assert.ok(vr.zoom > 1);
  assert.ok(near(toScreen(viewTransform(b, vr), [(a[0] + c[0]) / 2, (a[1] + c[1]) / 2]), [450, 260], 1e-6));
});

// ---- 2 + 3. start drag, goals add / move / delete ------------------------------------------------------------------------------

const placed = placeObjectives({ layout: base, seed: 3 });
const nav = groundNav(base);
/** A walkable street point at least `far` cm from `from` (the street zones' centres). */
const streetSpot = (l: GenLayout, from: Vec2, far: number): Vec2 => {
  const g = groundNav(l);
  const c = l.zones.filter((z) => z.id.startsWith('Z_ST_')).map((z) => [z.poly.reduce((s, p) => s + p[0], 0) / z.poly.length, z.poly.reduce((s, p) => s + p[1], 0) / z.poly.length] as Vec2)
    .filter((p) => g.walkable(p) && Math.hypot(p[0] - from[0], p[1] - from[1]) >= far);
  assert.ok(c.length, 'a street spot');
  return c[0];
};

test('ux start drag: the start moves where dropped, the routes to every goal and the shortest order are re-walked', () => {
  assert.equal(placed.ok, true, placed.message);
  const l = placed.layout;
  const before = checkObjectives(l, nav);
  assert.ok(before.ok);
  assert.equal(before.bestPath[0].join(), l.objectives!.start.pos.join(), 'the order line starts at the start');
  const to = streetSpot(l, l.objectives!.start.pos, 4000);
  const r = moveObjective(l, START_ID, to, nav);
  assert.equal(r.ok, true, r.message);
  const ob = r.layout.objectives!;
  assert.deepEqual(ob.start.pos, [Math.round(to[0]), Math.round(to[1])]);
  assert.equal(ob.start.source, 'manual');
  assert.deepEqual(ob.goals, l.objectives!.goals, 'the goals stay');
  assert.ok(r.check!.goals.some((g, i) => g.length_m !== before.goals[i].length_m), 'the path lengths are re-walked');
  assert.deepEqual(r.check!.bestPath[0], ob.start.pos, 'the order line starts at the new start');
  assert.deepEqual(r.check, checkObjectives(r.layout), 'the edit reports the same check as a fresh walk');
  assert.deepEqual(newErrors(r.layout), [], 'still a valid layout');
  assert.equal(l.objectives!.start.pos.join(), placed.layout.objectives!.start.pos.join(), 'the input is not mutated');
  // into a building without an entrance: red with the reason, NOT moved elsewhere
  const closed = base.buildings.find((b) => !b.enterable);
  assert.ok(closed, 'Depot has a closed building');
  const inside = rect.centre(closed.footprint);
  const bad = moveObjective(l, START_ID, inside, nav);
  assert.equal(bad.ok, true, 'the move is applied');
  assert.deepEqual(bad.layout.objectives!.start.pos, [Math.round(inside[0]), Math.round(inside[1])], 'kept where dropped');
  assert.match(bad.check!.start.error ?? '', /без входа/);
  assert.equal(bad.check!.ok, false);
  assert.ok(bad.check!.goals.every((g) => g.error), 'every goal reports the bad start');
});

test('ux goals: add by click (up to count_max), drag, delete (down to manual_count_min); invalid spots red with the reason', () => {
  let l = placed.layout;
  const n0 = l.objectives!.goals.length;
  // add up to count_max; ids unique; the kind follows the spot
  const spots = [streetSpot(l, l.objectives!.start.pos, 2000)];
  const room = base.buildings.find((b) => b.enterable && (b.rooms ?? []).some((r) => r.floor === 0));
  assert.ok(room);
  const rr = room.rooms!.find((r) => r.floor === 0)!;
  spots.push([(rr.bounds.min[0] + rr.bounds.max[0]) / 2, (rr.bounds.min[1] + rr.bounds.max[1]) / 2]);
  for (let i = n0; i < V2.objectives.count_max; i++) {
    const r = addGoal(l, spots[(i - n0) % spots.length], nav);
    assert.equal(r.ok, true, r.message);
    l = r.layout;
  }
  const ids = l.objectives!.goals.map((g) => g.id);
  assert.equal(ids.length, V2.objectives.count_max);
  assert.equal(new Set(ids).size, ids.length, 'unique ids');
  assert.ok(l.objectives!.goals.some((g) => g.kind === 'street'));
  assert.ok(l.objectives!.goals.some((g) => g.kind === 'room' && g.building === room.id));
  const full = addGoal(l, spots[0], nav);
  assert.equal(full.ok, false, 'no more than count_max');
  assert.equal(full.layout, l, 'refused = unchanged');
  // close goals are a WARNING, not an error
  const chk = checkObjectives(l, nav);
  assert.equal(chk.ok, true, chk.errors.join('; '));
  assert.ok(chk.goals.some((g) => g.warnings.some((w) => w.includes('ближе'))), 'close goals warned');
  // drag a goal outside the map / into the canal / into a closed building: the reason, the goal kept where dropped
  const id = ids[0];
  const out = moveObjective(l, id, [base.bounds.max[0] + 500, base.bounds.min[1] - 500], nav);
  assert.equal(out.check!.goals.find((g) => g.id === id)!.error, 'за краем карты');
  assert.ok(out.check!.errors.some((e) => e.includes('за краем карты')));
  if (base.cut) {
    const xs = base.cut.poly.map((p) => p[0]), ys = base.cut.poly.map((p) => p[1]);
    let canal: Vec2 | null = null;
    for (let k = 1; k < 40 && !canal; k++) for (let j = 1; j < 40 && !canal; j++) {
      const p: Vec2 = [Math.min(...xs) + (Math.max(...xs) - Math.min(...xs)) * k / 40, Math.min(...ys) + (Math.max(...ys) - Math.min(...ys)) * j / 40];
      if (pointInPoly(p, base.cut.poly) && !base.cut.crossings.some((c) => pointInPoly(p, c.poly))) canal = p;
    }
    assert.ok(canal);
    assert.equal(moveObjective(l, id, canal, nav).check!.goals.find((g) => g.id === id)!.error, 'в канале');
  }
  const closed = base.buildings.find((b) => !b.enterable)!;
  const inClosed = moveObjective(l, id, rect.centre(closed.footprint), nav);
  assert.match(inClosed.check!.goals.find((g) => g.id === id)!.error ?? '', /без входа/);
  assert.deepEqual(inClosed.layout.objectives!.goals[0].pos, rect.centre(closed.footprint).map(Math.round));
  // walled in -> unreachable from the start
  const g0 = l.objectives!.goals[0].pos;
  const box: GenLayout['walls'] = ([[[-300, -300], [300, -300]], [[300, -300], [300, 300]], [[300, 300], [-300, 300]], [[-300, 300], [-300, -300]]] as [Vec2, Vec2][])
    .map(([a, c], i) => ({ id: `W_box_${i}`, a: [g0[0] + a[0], g0[1] + a[1]] as Vec2, b: [g0[0] + c[0], g0[1] + c[1]] as Vec2, height: 400, thickness: 30 }));
  const sealed = checkObjectives({ ...l, walls: [...l.walls, ...box] });
  assert.equal(sealed.goals[0].error, 'недостижима со старта');
  // a valid drag clears the error again; the order re-walked
  const back = moveObjective(out.layout, id, streetSpot(l, l.objectives!.start.pos, 6000), nav);
  assert.equal(back.check!.goals.find((g) => g.id === id)!.error, undefined);
  assert.equal(back.check!.bestOrder.order.length, V2.objectives.count_max);
  // delete down to manual_count_min, the last one refused
  for (const gid of ids.slice(0, ids.length - V2.objectives.manual_count_min)) {
    const r = removeGoal(l, gid, nav);
    assert.equal(r.ok, true, r.message);
    l = r.layout;
  }
  assert.equal(l.objectives!.goals.length, V2.objectives.manual_count_min);
  const last = removeGoal(l, l.objectives!.goals[0].id, nav);
  assert.equal(last.ok, false, 'never below manual_count_min');
  assert.deepEqual(newErrors(l), [], 'one goal is a valid map');
  assert.equal(checkObjectives(l, nav).ok, true);
  // a map without objectives: the first added goal creates them (the start = the spawn)
  const fresh = addGoal(base, streetSpot(base, base.spawn.pos, 5000), nav);
  assert.equal(fresh.ok, true, fresh.message);
  assert.equal(fresh.layout.objectives!.start.source, 'spawn');
  assert.deepEqual(fresh.layout.objectives!.start.pos, base.spawn.pos);
  assert.equal(fresh.layout.objectives!.goals.length, 1);
  assert.deepEqual(newErrors(fresh.layout), []);
  assert.deepEqual(JSON.parse(JSON.stringify(fresh.layout)).objectives, fresh.layout.objectives, 'saved in the JSON as before');
});

// ---- 4. full re-roll (lot subdivision + footprints + placement) ---------------------------------------------------------------

const hash = (l: unknown) => crypto.createHash('sha256').update(JSON.stringify(l)).digest('hex');
const rectZone = (a: Vec2, b: Vec2) => shapePolygon({ kind: 'rect', a, b });
/** The north-west quarter (stage 2's test zone): the facades, the pipe yard, the compound, the sawtooth hall, the boiler yard, the office. */
const ZONE_NW = rectZone([500, -9000], [6200, 0]);
const overlapM = (a: [Vec2, Vec2], b: [Vec2, Vec2]) => Math.min(a[1][0], b[1][0]) - Math.max(a[0][0], b[0][0]) > 0.01 && Math.min(a[1][1], b[1][1]) - Math.max(a[0][1], b[0][1]) > 0.01;

test('ux full re-roll: the subdivision - parcels tile the block, never overlap, every parcel faces a street; chaos 0 tidy, 1 irregular', () => {
  const frame = frameOf(base), streets = streetRectsM(base);
  const lots = lotsOf(base, profile);
  let tidyInnerSame = 0, chaosInnerMoved = 0, parcelsTotal = 0, cuts = 0;
  for (const lot of lots) {
    const blockM = rectFromUE(lot.rect, frame);
    const sides = streetSides(blockM, streets);
    for (const chaos of [0, 1]) for (const seed of [1, 2, 3]) {
      const ps = subdivideBlock(blockM, streets, seed, lot.id, chaos);
      assert.deepEqual(subdivideBlock(blockM, streets, seed, lot.id, chaos), ps, 'deterministic');
      parcelsTotal += ps.length; if (ps.length > 1) cuts++;
      const u = unionRect(ps.map((p) => p.rect));
      assert.ok(near(u.flat(), blockM.flat(), 1e-6), `${lot.id}: the parcels span the block`);
      ps.forEach((p, i) => {
        assert.ok(touchingSides(p.rect, blockM, sides).length > 0 || ps.length === 1, `${lot.id} #${i}: on a street side`);
        assert.ok(sides.includes(p.side), `${lot.id} #${i}: faces a street side (${p.side})`);
        assert.ok(p.inner[0][0] >= p.rect[0][0] - 1e-9 && p.inner[0][1] >= p.rect[0][1] - 1e-9 && p.inner[1][0] <= p.rect[1][0] + 1e-9 && p.inner[1][1] <= p.rect[1][1] + 1e-9, 'inner inside the parcel');
        for (const q of ps.slice(i + 1)) assert.ok(!overlapM(p.rect, q.rect), `${lot.id}: parcels never overlap`);
        if (chaos === 0 && near(p.inner.flat(), p.rect.flat())) tidyInnerSame++;
        if (chaos === 1 && !near(p.inner.flat(), p.rect.flat(), 0.05)) chaosInnerMoved++;
      });
      if (chaos === 0 && ps.length === 2) {
        // a tidy single cut halves the block (minus the gap)
        const a = ps[0].rect, b = ps[1].rect;
        const da = Math.abs((a[1][0] - a[0][0]) - (b[1][0] - b[0][0])), db = Math.abs((a[1][1] - a[0][1]) - (b[1][1] - b[0][1]));
        assert.ok(Math.min(da, db) < 1e-6, `${lot.id}: an even cut`);
      }
    }
  }
  assert.ok(cuts > 20, `big blocks are cut (${cuts})`);
  assert.ok(tidyInnerSame > 0 && chaosInnerMoved > parcelsTotal / 8, `chaos 0 = flush parcels (${tidyInnerSame}), chaos 1 = shifted / shrunk (${chaosInnerMoved})`);
});

test('ux full re-roll: footprints and positions change over N seeds, locks byte-identical, routes valid, deterministic, chaos matters', () => {
  const lots = lotsOf(base, profile);
  // lock the sawtooth hall block: it must survive every full re-roll byte for byte
  const saw = lots.find((k) => k.id === 'LOT_sawtooth')!;
  const lockPoly = rectZone([saw.rect[0][0] - 100, saw.rect[0][1] - 100], [saw.rect[1][0] + 100, saw.rect[1][1] + 100]);
  const locked = addLock(base, lockPoly, 'Цех');
  const inLock = (p: Vec2) => pointInPoly(p, lockPoly);
  const lockedJson = (l: GenLayout) => layoutGroups(l).filter((g) => inLock(g.centre)).map((g) => JSON.stringify(g.members)).sort();
  const lockedBefore = lockedJson(locked);
  assert.ok(lockedBefore.some((j) => j.includes('B_hall_n')), 'the lock holds the hall (with its walls, ladders, mantle edges)');
  const plan = selectRegion(locked, ZONE_NW, profile, 'full').plan;
  assert.ok(plan.groups.length >= 4, `blocks re-planned: ${plan.groups.map((g) => g.id).join(',')}`);
  assert.ok(!plan.groups.some((g) => g.id === 'LOT_sawtooth'), 'the locked block is not re-planned');
  const oldIn = (l: GenLayout) => l.buildings.filter((b) => plan.groups.some((g) => pointInPoly(rect.centre(b.footprint), rectZone(g.rect[0], g.rect[1]))));
  const oldFootprints = new Set(oldIn(locked).map((b) => JSON.stringify(b.footprint)));
  const seeds = [101, 202, 303, 404, 505, 606];
  const roads = [...base.zones.filter((z) => z.id.startsWith('Z_ST_')).map((z) => z.poly), base.cut!.poly];
  const shrunk = (f: [Vec2, Vec2]) => rectZone([f[0][0] + 20, f[0][1] + 20], [f[1][0] - 20, f[1][1] - 20]);
  const counts = new Set<number>();
  for (const seed of seeds) {
    for (const chaos of [0, 1]) {
      const r = regenerateRegion({ layout: locked, zone: ZONE_NW, seed, profile, blocks: 'full', chaos });
      assert.equal(r.ok, true, `seed ${seed} chaos ${chaos}: ${r.message}`);
      const now = oldIn(r.layout);
      counts.add(now.length);
      // the shapes / positions really change: no old footprint survives in a re-planned block
      for (const b of now) assert.ok(!oldFootprints.has(JSON.stringify(b.footprint)), `seed ${seed} chaos ${chaos}: ${b.id} is a new footprint`);
      // locks byte-identical
      assert.deepEqual(lockedJson(r.layout), lockedBefore, `seed ${seed} chaos ${chaos}: the lock is untouched`);
      assert.deepEqual(r.layout.regen!.locks, locked.regen!.locks);
      // the route through the region stays valid; roads / canal untouched; heights measured; ids unique
      assert.ok(checkLayoutRoutes(r.layout).find((x) => x.mandatory)!.completable, `seed ${seed} chaos ${chaos}: mandatory route`);
      assert.deepEqual(r.layout.cut, base.cut);
      assert.deepEqual(r.layout.zones.filter((z) => z.id.startsWith('Z_ST_')), base.zones.filter((z) => z.id.startsWith('Z_ST_')));
      const added = new Set(r.added);
      for (const b of r.layout.buildings.filter((x) => added.has(x.id))) assert.ok(roads.every((p) => !polygonsOverlap(shrunk(b.footprint), p)), `${b.id} blocks no road`);
      const fresh: GenLayout = { ...r.layout, walls: r.layout.walls.filter((w) => added.has(w.id) || (w.building !== undefined && added.has(w.building)) || [...added].some((a) => w.id.startsWith(`${a}_`))), covers: r.layout.covers.filter((c) => added.has(c.id)), buildings: r.layout.buildings.filter((b) => added.has(b.id)), mantle_edges: [] };
      assert.deepEqual(runObstacleHeightGate(fresh).errors, [], 'heights are measured classes');
      for (const k of ['buildings', 'walls', 'ladders', 'covers', 'zones'] as const) assert.equal(new Set((r.layout[k] as { id: string }[]).map((x) => x.id)).size, r.layout[k].length, `${k} ids unique`);
      assert.deepEqual(newErrors(r.layout), [], 'no new schema errors');
      // the parcels are stored with their parent; the record says full + chaos
      const blocks = r.layout.regen!.blocks!;
      for (const g of plan.groups) assert.ok(blocks.some((k) => k.parent === g.id), `${g.id}: parcels stored`);
      assert.ok(!blocks.some((k) => plan.groups.some((g) => g.id === k.id)), 'the old blocks are replaced by their parcels');
      assert.equal(r.layout.regen!.rerolls.at(-1)!.mode, 'full');
      assert.equal(r.layout.regen!.rerolls.at(-1)!.chaos, chaos);
      // deterministic per seed
      assert.equal(hash(regenerateRegion({ layout: locked, zone: ZONE_NW, seed, profile, blocks: 'full', chaos }).layout), hash(r.layout), 'deterministic');
    }
    assert.notEqual(hash(regenerateRegion({ layout: locked, zone: ZONE_NW, seed, profile, blocks: 'full', chaos: 0 }).layout), hash(regenerateRegion({ layout: locked, zone: ZONE_NW, seed, profile, blocks: 'full', chaos: 1 }).layout), 'chaos changes the result');
  }
  assert.ok(counts.size >= 2, `the building count varies with the seed (${[...counts].join(',')})`);
});

test('ux full re-roll: a second full re-roll re-plans the parcels of a block together (by parent); the default chaos comes from the config', () => {
  const first = regenerateRegion({ layout: base, zone: ZONE_NW, seed: 11, profile, blocks: 'full' });
  assert.equal(first.ok, true, first.message);
  assert.equal(first.layout.regen!.rerolls.at(-1)!.chaos, FULL.default_chaos);
  const plan = selectRegion(first.layout, ZONE_NW, profile, 'full').plan;
  const parents = new Set(first.layout.regen!.blocks!.filter((k) => k.parent).map((k) => k.parent));
  assert.ok(plan.groups.some((g) => parents.has(g.id) && g.lots.length > 1), 'siblings grouped under their parent');
  for (const g of plan.groups.filter((x) => x.lots.length > 1)) {
    const parent = lotsOf(base, profile).find((k) => k.id === g.id)!;
    assert.ok(near(g.rect.flat(), parent.rect.flat(), 1e-6), `${g.id}: the parent block whole`);
  }
  const second = regenerateRegion({ layout: first.layout, zone: ZONE_NW, seed: 12, profile, blocks: 'full', chaos: 1 });
  assert.equal(second.ok, true, second.message);
  assert.ok(checkLayoutRoutes(second.layout).find((x) => x.mandatory)!.completable);
  assert.deepEqual(newErrors(second.layout), []);
  // a JSON round trip keeps the parcels (reload from the accepted file)
  const reloaded = JSON.parse(JSON.stringify(second.layout)) as GenLayout;
  assert.deepEqual(lotsOf(reloaded, profile), second.layout.regen!.blocks);
  assert.ok(rectCentre(second.layout.regen!.blocks![0].rect).every(Number.isFinite));
});
