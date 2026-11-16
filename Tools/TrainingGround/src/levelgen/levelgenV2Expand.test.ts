// Training Ground / level generator v2, stage 2 (board 066): MAP EXPANSION x2/x4/x6 by area (expand.ts) and START + 2-4 GOALS
// (objectives.ts). `npm test`. Inputs: the Depot reference profile + generateLayout(seed 1) (pure, nothing written).
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import type { Vec2 } from '../movement/geometry.ts';
import { frameOf, lotsOf, rectCentre, rectFromUE, V2 } from './blockTemplates.ts';
import { checkLayoutRoutes } from './evaluate.ts';
import type { ExpandResult } from './expand.ts';
import { checkSeams, expandLayout, expansionSize } from './expand.ts';
import { generateLayout } from './generate.ts';
import { checkObjectives, placeObjectives } from './objectives.ts';
import type { Profile } from './profile.ts';
import { addLock, regenerateRegion, shapePolygon } from './regionRegen.ts';
import type { GenLayout } from './schema.ts';
import { validateExpansionState, validateGenLayout, validateObjectives } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
const base = generateLayout({ profile, seed: 1 });
const baseErrors = validateGenLayout(base);
const newErrors = (l: GenLayout) => validateGenLayout(l).filter((e) => !baseErrors.includes(e));
const hash = (l: unknown) => crypto.createHash('sha256').update(JSON.stringify(l)).digest('hex');
const rectZone = (a: Vec2, b: Vec2) => shapePolygon({ kind: 'rect', a, b });
const expanded = new Map<number, ExpandResult>();
const expand = (f: number) => { if (!expanded.has(f)) expanded.set(f, expandLayout({ layout: base, factor: f, seed: 7, profile })); return expanded.get(f)!; };
const rectM = (poly: Vec2[], frame: Vec2): [Vec2, Vec2] => rectFromUE([[Math.min(...poly.map((p) => p[0])), Math.min(...poly.map((p) => p[1]))], [Math.max(...poly.map((p) => p[0])), Math.max(...poly.map((p) => p[1]))]], frame);

// ---- C. map expansion ---------------------------------------------------------------------------------------------------------

test('v2 expansion: x2 / x4 / x6 by area = 250 x 175, 354 x 248, 434 x 304 m; the old map stays centred; bad factors refused', () => {
  assert.deepEqual(expansionSize([177, 124], 2), [250, 175]);
  assert.deepEqual(expansionSize([177, 124], 4), [354, 248]);
  assert.deepEqual(expansionSize([177, 124], 6), [434, 304]);
  for (const f of [2, 4, 6]) {
    const r = expand(f);
    assert.equal(r.ok, true, r.message);
    assert.deepEqual(frameOf(r.layout), expansionSize([177, 124], f));
    assert.deepEqual(r.layout.expansion?.seams.at(-1), [base.bounds.min, base.bounds.max], 'the seam = the old bounds');
    assert.deepEqual(r.layout.params, base.params, 'the generator params (base size) are kept');
    assert.equal(r.layout.name, `${base.name}_x${f}`);
    assert.deepEqual(newErrors(r.layout), [], `x${f}: no new schema errors`);
    assert.deepEqual(validateExpansionState(r.layout.expansion), []);
    const [W, H] = frameOf(r.layout);
    assert.ok(Math.abs((W * H) / (177 * 124) - f) < 0.02 * f, `x${f}: area ratio ${(W * H) / (177 * 124)}`);
  }
  assert.equal(expandLayout({ layout: base, factor: 3, seed: 1, profile }).ok, false);
  assert.equal(expandLayout({ layout: expand(4).layout, factor: 2, seed: 1, profile }).ok, false, 'never shrinks');
  const twice = expandLayout({ layout: expand(2).layout, factor: 6, seed: 9, profile });
  assert.equal(twice.ok, true, twice.message);
  assert.deepEqual(frameOf(twice.layout), [434, 304]);
  assert.equal(twice.layout.expansion?.steps.length, 2);
  assert.equal(twice.layout.name, `${base.name}_x6`);
  assert.ok(validateExpansionState({ version: 9, base_size_m: [1, 1], steps: [], seams: [] }).length >= 2);
});

test('v2 expansion: every old object and every lock stays byte-identical (only the listed seam-gate walls are opened)', () => {
  const locked = addLock(base, rectZone([500, -9000], [3000, -4000]), 'Двор');
  const r = expandLayout({ layout: locked, factor: 4, seed: 7, profile });
  assert.equal(r.ok, true, r.message);
  const gated = new Set(r.seamGates.map((g) => g.wall));
  assert.ok(r.seamGates.some((g) => g.wall.startsWith('F_east_E')), `the east yard fence across the north street is opened: ${JSON.stringify(r.seamGates)}`);
  for (const k of ['buildings', 'ladders', 'covers', 'zones', 'spawn_points', 'ambush_points', 'patrol_routes', 'routes', 'mantle_edges'] as const) {
    const now = new Set(((r.layout[k] ?? []) as object[]).map((x) => JSON.stringify(x)));
    for (const x of (locked[k] ?? []) as object[]) assert.ok(now.has(JSON.stringify(x)), `${k}: old object kept`);
  }
  const wallsNow = new Map(r.layout.walls.map((w) => [w.id, w]));
  for (const w of locked.walls) if (!gated.has(w.id)) assert.equal(JSON.stringify(wallsNow.get(w.id)), JSON.stringify(w), `wall ${w.id} identical`);
  for (const g of r.seamGates) {
    const w = locked.walls.find((x) => x.id === g.wall)!;
    const lo = Math.min(w.a[0], w.b[0]), hi = Math.max(w.a[0], w.b[0]);
    for (const id of g.pieces) {
      const p = wallsNow.get(id)!;
      for (const q of [p.a, p.b]) assert.ok(q[1] === w.a[1] && q[0] >= lo - 1 && q[0] <= hi + 1, `${id} lies on the old wall ${w.id}`);
    }
  }
  for (const k of ['spawn', 'end', 'seed', 'profile', 'pressure'] as const) assert.deepEqual(r.layout[k], locked[k], k);
  assert.deepEqual(r.layout.regen?.locks, locked.regen?.locks, 'the locks stay in place');
  assert.deepEqual(r.layout.cut!.crossings.slice(0, locked.cut!.crossings.length), locked.cut!.crossings, 'the old crossings');
  const blocks = r.layout.regen!.blocks!;
  assert.deepEqual(blocks.slice(0, 15).map((k) => k.id), lotsOf(base, profile).map((k) => k.id), 'the classic blocks first');
  assert.equal(blocks.length, 15 + r.lots.length);
});

test('v2 expansion: roads and the canal continue across the seams (no dead ends), no building cuts the old map / a street / the canal', () => {
  for (const f of [2, 4, 6]) {
    const r = expand(f);
    const frame = frameOf(r.layout);
    const seams = checkSeams(r.layout, r.roads);
    assert.ok(seams.filter((s) => s.kind === 'road').length >= 2 && seams.filter((s) => s.kind === 'canal').length === 2, `x${f}: ${JSON.stringify(seams)}`);
    for (const s of seams) assert.ok(s.connected && s.ratio <= 1.25, `x${f}: ${s.kind} ${s.id} ${s.side} continues (ratio ${s.ratio})`);
    // every new street: both ends at the map edge or on another street
    const all = r.layout.zones.filter((z) => z.id.startsWith('Z_ST_')).map((z) => ({ id: z.id, r: rectM(z.poly, frame) }));
    const touches = (a: [Vec2, Vec2], b: [Vec2, Vec2]) => a[0][0] <= b[1][0] + 0.6 && b[0][0] <= a[1][0] + 0.6 && a[0][1] <= b[1][1] + 0.6 && b[0][1] <= a[1][1] + 0.6;
    for (const s of all.filter((x) => x.id.startsWith('Z_ST_x'))) {
      const horiz = s.r[1][0] - s.r[0][0] >= s.r[1][1] - s.r[0][1];
      const ends: [Vec2, Vec2][] = horiz ? [[[s.r[0][0], s.r[0][1]], [s.r[0][0] + 0.5, s.r[1][1]]], [[s.r[1][0] - 0.5, s.r[0][1]], [s.r[1][0], s.r[1][1]]]] : [[[s.r[0][0], s.r[0][1]], [s.r[1][0], s.r[0][1] + 0.5]], [[s.r[0][0], s.r[1][1] - 0.5], [s.r[1][0], s.r[1][1]]]];
      for (const e of ends) {
        const atEdge = e[0][0] <= 0.6 || e[1][0] >= frame[0] - 0.6 || e[0][1] <= 0.6 || e[1][1] >= frame[1] - 0.6;
        assert.ok(atEdge || all.some((o) => o.id !== s.id && touches(e, o.r)), `x${f}: ${s.id} has no dead end`);
      }
    }
    // the canal spans the new width; its north retaining wall is continuous except one gap per crossing
    const cutR = rectM(r.layout.cut!.poly, frame);
    assert.ok(cutR[0][0] <= 0.01 && cutR[1][0] >= frame[0] - 0.01, `x${f}: the canal reaches both new edges`);
    const north = r.layout.walls.filter((w) => w.kind === 'cut_wall' && w.id.startsWith('CUTWALL_N')).map((w) => [Math.min(w.a[1], w.b[1]), Math.max(w.a[1], w.b[1])] as [number, number]).sort((a, b) => a[0] - b[0]);
    let gaps = 0;
    for (let i = 1; i < north.length; i++) if (north[i][0] - north[i - 1][1] > 10) gaps++;
    assert.equal(gaps, r.layout.cut!.crossings.length, `x${f}: one gap per crossing`);
    assert.ok(north[0][0] <= r.layout.bounds.min[1] + 1 && north.at(-1)![1] >= r.layout.bounds.max[1] - 1, `x${f}: the wall reaches both new edges`);
    // no new building on the old map, a street, the canal band
    const O = rectFromUE(r.layout.expansion!.seams.at(-1)!, frame);
    const band: [Vec2, Vec2] = [[cutR[0][0], cutR[0][1] - 2.5], [cutR[1][0], cutR[1][1] + 2.5]];
    const ov = (a: [Vec2, Vec2], b: [Vec2, Vec2]) => a[0][0] < b[1][0] - 0.05 && b[0][0] < a[1][0] - 0.05 && a[0][1] < b[1][1] - 0.05 && b[0][1] < a[1][1] - 0.05;
    const oldIds = new Set(base.buildings.map((b) => b.id));
    for (const b of r.layout.buildings.filter((x) => !oldIds.has(x.id))) {
      const fr = rectFromUE(b.footprint, frame);
      assert.ok(!ov(fr, O), `x${f}: ${b.id} outside the old map`);
      assert.ok(!all.some((s) => ov(fr, s.r)), `x${f}: ${b.id} on no street`);
      assert.ok(!ov(fr, band), `x${f}: ${b.id} off the canal`);
    }
    assert.ok(r.lots.length >= 15 && new Set(r.lots.map((k) => k.template)).size >= 5, `x${f}: ${r.lots.length} new blocks of several types`);
  }
});

test('v2 expansion: the routes are re-validated (all still completable); deterministic per seed; the street plan follows the skeleton seed', () => {
  const r = expand(4);
  for (const x of r.routes) assert.ok(x.completable, `${x.id}: ${x.fail}`);
  assert.deepEqual(checkLayoutRoutes(r.layout).map((x) => x.completable), checkLayoutRoutes(base).map((x) => x.completable));
  assert.equal(hash(expandLayout({ layout: base, factor: 4, seed: 7, profile }).layout), hash(r.layout), 'same map + factor + seed -> the same JSON');
  const other = expandLayout({ layout: base, factor: 4, seed: 8, skeletonSeed: 7, profile });
  assert.deepEqual(other.streets, r.streets, 'the same skeleton seed -> the same streets');
  assert.deepEqual(other.layout.zones.filter((z) => z.id.startsWith('Z_ST_')), r.layout.zones.filter((z) => z.id.startsWith('Z_ST_')));
  assert.notDeepEqual(other.lots.map((k) => k.template), r.lots.map((k) => k.template), 'another block seed -> other block types');
  console.log(`# v2 expansion x4 seed 7 sha256 ${hash(r.layout)}`);
});

test('v2 expansion + re-roll: re-rolling the new west area changes its block types and keeps the old core byte-identical', () => {
  const r = expand(4);
  const west = rectZone([r.layout.bounds.min[0], r.layout.bounds.min[1]], [r.layout.bounds.max[0], base.bounds.min[1] - 100]);
  const rr = regenerateRegion({ layout: r.layout, zone: west, seed: 55, profile });
  assert.equal(rr.ok, true, rr.message);
  assert.ok(rr.retemplated.length >= 5, `re-typed ${rr.retemplated.length}`);
  assert.ok(rr.retemplated.every((x) => x.id.startsWith('LOT_x1_') && x.from !== x.to));
  const now = new Set([...rr.layout.walls, ...rr.layout.buildings, ...rr.layout.covers].map((x) => JSON.stringify(x)));
  const inCore = (c: Vec2) => c[1] > base.bounds.min[1] && c[1] < base.bounds.max[1] && c[0] > base.bounds.min[0] && c[0] < base.bounds.max[0];
  for (const b of r.layout.buildings) if (inCore(rectCentre(b.footprint))) assert.ok(now.has(JSON.stringify(b)), `${b.id} kept`);
  for (const w of r.layout.walls) if (inCore([(w.a[0] + w.b[0]) / 2, (w.a[1] + w.b[1]) / 2])) assert.ok(now.has(JSON.stringify(w)), `${w.id} kept`);
  assert.deepEqual(rr.layout.expansion, r.layout.expansion);
  assert.ok(checkLayoutRoutes(rr.layout).find((x) => x.mandatory)!.completable);
  assert.deepEqual(newErrors(rr.layout), []);
});

test('v2 expansion: a locked wall at the seam stays closed and is reported', () => {
  const fence = base.walls.find((w) => w.id === 'F_east_E_0')!;
  const lock = rectZone([Math.min(fence.a[0], fence.b[0]) - 300, fence.a[1] - 300], [Math.max(fence.a[0], fence.b[0]) + 300, fence.a[1] + 300]);
  const r = expandLayout({ layout: addLock(base, lock, 'Забор'), factor: 2, seed: 7, profile });
  assert.equal(r.ok, true, r.message);
  assert.ok(r.blockedSeams.some((x) => x.includes('F_east_E_0')), JSON.stringify(r.blockedSeams));
  assert.equal(JSON.stringify(r.layout.walls.find((w) => w.id === 'F_east_E_0')), JSON.stringify(fence));
});

// ---- D. start + goals -------------------------------------------------------------------------------------------------------

test('v2 objectives: 2-4 goals by map size, reachable from the start and from each other, spread, saved in the JSON, deterministic', () => {
  const maps: [number, GenLayout][] = [[1, base], [2, expand(2).layout], [4, expand(4).layout], [6, expand(6).layout]];
  for (const [f, l] of maps) {
    const want = (V2.objectives.default_count as Record<string, number>)[String(f)];
    const r = placeObjectives({ layout: l, seed: 3 });
    assert.equal(r.ok, true, `x${f}: ${r.message}`);
    const ob = r.layout.objectives!;
    assert.equal(ob.goals.length, want, `x${f}: ${want} goals`);
    assert.equal(ob.order, 'free');
    const check = checkObjectives(r.layout);
    assert.ok(check.ok && check.pairwise, `x${f}: every goal reachable, any order`);
    assert.equal(check.bestOrder.order.length, ob.goals.length);
    assert.ok(check.minSpacing_m >= V2.objectives.min_spacing_m, `x${f}: spacing ${check.minSpacing_m}`);
    for (const g of ob.goals) assert.ok(Math.hypot(g.pos[0] - ob.start.pos[0], g.pos[1] - ob.start.pos[1]) >= V2.objectives.min_from_start_m * 100, `x${f}: ${g.id} far from the start`);
    assert.deepEqual(newErrors(r.layout), [], `x${f}: valid`);
    assert.deepEqual(JSON.parse(JSON.stringify(r.layout)).objectives, ob, 'survives the JSON round trip');
    assert.equal(hash(placeObjectives({ layout: l, seed: 3 }).layout), hash(r.layout), `x${f}: deterministic`);
  }
  assert.equal(placeObjectives({ layout: expand(4).layout, seed: 11, count: 4 }).objectives?.goals.length, 4);
  const many = placeObjectives({ layout: base, seed: 11, count: 9 });
  assert.ok(many.ok && many.objectives!.goals.length <= 4, 'the count is clamped to 2-4');
  assert.notDeepEqual(placeObjectives({ layout: expand(6).layout, seed: 4 }).objectives?.goals, placeObjectives({ layout: expand(6).layout, seed: 3 }).objectives?.goals, 'another seed -> other goals');
});

test('v2 objectives: never inside a locked building unless allowed; an edge start; an unreachable goal is caught', () => {
  const l = expand(4).layout;
  const lockAll = addLock(l, rectZone(l.bounds.min, l.bounds.max), 'всё');
  const r = placeObjectives({ layout: lockAll, seed: 3 });
  assert.equal(r.ok, true, r.message);
  assert.ok(r.objectives!.goals.every((g) => g.kind !== 'room'), 'no goal in a locked building');
  const allowed = placeObjectives({ layout: lockAll, seed: 3, allowLocked: true });
  assert.ok(allowed.candidates > r.candidates, 'allowed: the locked rooms are candidates again');
  const e = placeObjectives({ layout: l, seed: 5, start: 'edge' });
  assert.equal(e.objectives?.start.source, 'edge');
  const p = e.objectives!.start.pos, b = l.bounds;
  assert.ok(Math.min(p[0] - b.min[0], b.max[0] - p[0], p[1] - b.min[1], b.max[1] - p[1]) <= V2.objectives.start_edge_inset_m * 100 + 1, 'the start is at the map edge');
  // a goal walled in -> the check fails
  const g = e.objectives!.goals[0].pos;
  const sides: [Vec2, Vec2][] = [[[-200, -200], [200, -200]], [[200, -200], [200, 200]], [[200, 200], [-200, 200]], [[-200, 200], [-200, -200]]];
  const box: GenLayout['walls'] = sides.map(([a, c], i) => ({ id: `W_box_${i}`, a: [g[0] + a[0], g[1] + a[1]] as Vec2, b: [g[0] + c[0], g[1] + c[1]] as Vec2, height: 400, thickness: 30 }));
  const sealed = checkObjectives({ ...e.layout, walls: [...e.layout.walls, ...box] });
  assert.equal(sealed.ok, false);
  assert.equal(sealed.goals[0].reachable, false);
  // UX round (task 072): manual_count_min (1) .. count_max (4) goals are valid; none or more than count_max are refused
  const st = { pos: [0, 0] as Vec2, yaw: 0, layer: 'ground', source: 'spawn' as const };
  const goal = (id: string) => ({ id, pos: [0, 0] as Vec2, layer: 'ground', kind: 'yard' as const });
  assert.deepEqual(validateObjectives({ version: 1, seed: 1, order: 'free', start: st, goals: [goal('A')] }), [], 'one goal is valid');
  assert.ok(validateObjectives({ version: 1, seed: 1, order: 'free', start: st, goals: [] }).length > 0, 'no goal is refused');
  assert.ok(validateObjectives({ version: 1, seed: 1, order: 'free', start: st, goals: ['A', 'B', 'C', 'D', 'E'].map(goal) }).length > 0, 'five goals are refused');
});
