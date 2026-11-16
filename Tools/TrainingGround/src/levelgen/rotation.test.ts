// Training Ground / level generator v2 - ROTATED BUILDINGS (designer 2026-11-16, item 4): rotation.ts helpers, rotated templates in
// the region re-roll (full / types) and the expansion at «Макс. угол поворота зданий» 45 / 30, the old files loading as 0, locks
// byte-identical, doors facing their street, routes valid, the simulator walking round the rotated walls. `npm test`.
// Inputs: the Depot reference profile + generateLayout(seed 1) and the accepted Depot (read only; nothing written).
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import type { Vec2 } from '../movement/geometry.ts';
import { pointInPoly, rng, segSegDist } from '../movement/geometry.ts';
import { configValues } from '../movement/config.ts';
import { wallSegments } from '../movement/layout.ts';
import { checkLayoutRoutes, evaluateLayout } from './evaluate.ts';
import { expandLayout } from './expand.ts';
import { generateLayout } from './generate.ts';
import type { Profile } from './profile.ts';
import { addLock, polygonsOverlap, regenerateRegion } from './regionRegen.ts';
import { drawAngle, fitScale, footprintBBox, footprintPoly, inFootprint, normDeg, ROTATION, rotateContent, rotationOf, rotPoint, toBuildingFrame } from './rotation.ts';
import type { GenLayout } from './schema.ts';
import { rect, toFormationLayout, validateGenLayout } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
const config = configValues(JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/FormationLab/formation_config.json'), 'utf8')));
const base = generateLayout({ profile, seed: 1 });
const baseErrors = validateGenLayout(base);
const newErrors = (l: GenLayout) => validateGenLayout(l).filter((e) => !baseErrors.includes(e));
const hash = (v: unknown) => crypto.createHash('sha256').update(JSON.stringify(v)).digest('hex').slice(0, 16);
const zoneOf = (l: GenLayout): Vec2[] => { const b = l.bounds; return [[b.min[0], b.min[1]], [b.max[0], b.min[1]], [b.max[0], b.max[1]], [b.min[0], b.max[1]]]; };
const shrinkPoly = (p: Vec2[], cm: number): Vec2[] => {
  const c: Vec2 = [p.reduce((s, q) => s + q[0], 0) / p.length, p.reduce((s, q) => s + q[1], 0) / p.length];
  return p.map((q) => { const d = Math.hypot(q[0] - c[0], q[1] - c[1]); const k = d > 2 * cm ? (d - cm) / d : 0.5; return [c[0] + (q[0] - c[0]) * k, c[1] + (q[1] - c[1]) * k] as Vec2; });
};

/** Every overlap a rotated map must not have: building / building, a foreign wall through a building, a cover inside a building. */
function overlaps(l: GenLayout): string[] {
  const out: string[] = [];
  const polys = l.buildings.map((b) => ({ b, poly: shrinkPoly(footprintPoly(b), 15) }));
  for (let i = 0; i < polys.length; i++) for (let j = i + 1; j < polys.length; j++) {
    if (polygonsOverlap(polys[i].poly, polys[j].poly)) out.push(`${polys[i].b.id} x ${polys[j].b.id}`);
  }
  for (const w of l.walls) {
    if ((w.floor ?? 0) !== 0 || w.kind === 'cut_wall') continue;
    for (const { b, poly } of polys) {
      if (w.building === b.id) continue;
      const crosses = poly.some((p, k) => segSegDist(w.a, w.b, p, poly[(k + 1) % poly.length]) === 0) || pointInPoly(w.a, poly) || pointInPoly(w.b, poly);
      if (crosses) out.push(`wall ${w.id} x ${b.id}`);
    }
  }
  for (const c of l.covers) for (const { b, poly } of polys) if (pointInPoly(c.pos, poly)) out.push(`cover ${c.id} in ${b.id}`);
  return out;
}

/**
 * The door opens towards a street: its outward normal (its side's normal in the building's frame, turned with the building) points
 * towards the nearest point of the nearest street (positive dot) - a turn of up to 45 deg keeps a street-facing door street-facing.
 */
function doorSeesStreet(b: GenLayout['buildings'][number], pos: Vec2, streets: GenLayout['zones']): boolean {
  const [[x0, y0], [x1, y1]] = b.footprint, q = toBuildingFrame(b, pos);
  const dW = Math.abs(q[0] - x0), dE = Math.abs(q[0] - x1), dN = Math.abs(q[1] - y0), dS = Math.abs(q[1] - y1), m = Math.min(dW, dE, dN, dS);
  const n = rotPoint(m === dW ? [-1, 0] : m === dE ? [1, 0] : m === dN ? [0, -1] : [0, 1], [0, 0], rotationOf(b));
  let best: Vec2 = pos, bd = Infinity;
  for (const z of streets) for (let k = 0; k < z.poly.length; k++) {
    const a = z.poly[k], e = z.poly[(k + 1) % z.poly.length], L2 = (e[0] - a[0]) ** 2 + (e[1] - a[1]) ** 2;
    const t = L2 ? Math.max(0, Math.min(1, ((pos[0] - a[0]) * (e[0] - a[0]) + (pos[1] - a[1]) * (e[1] - a[1])) / L2)) : 0;
    const p: Vec2 = [a[0] + (e[0] - a[0]) * t, a[1] + (e[1] - a[1]) * t], d = Math.hypot(p[0] - pos[0], p[1] - pos[1]);
    if (d < bd) { bd = d; best = p; }
  }
  if (streets.some((z) => pointInPoly(pos, z.poly))) return true;
  return n[0] * (best[0] - pos[0]) + n[1] * (best[1] - pos[1]) > 0;
}

/** The overlaps a map adds to the ones its source already had (the reference Depot has a facade touching a hall). */
const baseOverlaps = new Set(overlaps(base));
const newOverlaps = (l: GenLayout) => overlaps(l).filter((x) => !baseOverlaps.has(x));

test('rotation: helpers - a turn and back, fitScale fits, footprintPoly / inFootprint agree, no draw at max 0', () => {
  const p: Vec2 = [1234, -567], c: Vec2 = [100, 200];
  const q = rotPoint(rotPoint(p, c, 37), c, -37);
  assert.ok(Math.hypot(q[0] - p[0], q[1] - p[1]) < 1e-9);
  assert.deepEqual(rotPoint(p, c, 0), p);
  assert.ok(Math.abs(rotPoint([1, 0], [0, 0], 90)[1] - 1) < 1e-12, 'UE yaw sense: +X turns towards +Y');
  assert.equal(normDeg(190), -170); assert.equal(normDeg(-180), 180);
  for (const [w, h] of [[2000, 1000], [1000, 3000], [800, 800]]) for (const deg of [5, 17, 30, 45, 60, 90]) {
    const k = fitScale(w, h, deg);
    const corners: Vec2[] = [[-w * k / 2, -h * k / 2], [w * k / 2, -h * k / 2], [w * k / 2, h * k / 2], [-w * k / 2, h * k / 2]].map((v) => rotPoint(v, [0, 0], deg));
    for (const v of corners) assert.ok(Math.abs(v[0]) <= w / 2 + 1e-6 && Math.abs(v[1]) <= h / 2 + 1e-6, `${w}x${h} at ${deg}: fits`);
  }
  const b = { footprint: [[0, 0], [2000, 1000]] as [Vec2, Vec2], rotation: 30 };
  const poly = footprintPoly(b);
  for (let k = 0; k < 200; k++) {
    const r = rng(k + 1), pt: Vec2 = [-500 + r() * 3000, -800 + r() * 2600];
    assert.equal(inFootprint(b, pt), pointInPoly(pt, poly), `point ${pt}`);
  }
  const bb = footprintBBox(b);
  for (const v of poly) assert.ok(rect.contains(bb, v, 1e-6));
  assert.deepEqual(toBuildingFrame(b, rotPoint([2000, 1000], [1000, 500], 30)).map((v) => Math.round(v)), [2000, 1000]);
  let calls = 0;
  assert.equal(drawAngle(() => { calls++; return 0.9; }, 0), 0);
  assert.equal(calls, 0, 'an axis-aligned run draws no random number');
  const a = drawAngle(() => 1, 45, 0.5);
  assert.ok(Math.abs(a - 22.5) < 1e-9, `${a}`);
  assert.equal(drawAngle(() => 0.5, 45), 0, 'below min_deg -> 0');
});

test('rotation: rotateContent is rigid - distances kept, yaws turned, footprint moved with its centre (rotation grows)', () => {
  const content = { walls: base.walls, buildings: base.buildings, ladders: base.ladders, covers: base.covers, zones: base.zones, ambush_points: base.ambush_points, mantle_edges: base.mantle_edges ?? [] };
  const pivot: Vec2 = [1000, -2000];
  const r = rotateContent(content, pivot, 30);
  assert.equal(rotateContent(content, pivot, 0), content, '0 = the same objects');
  for (let i = 0; i < base.walls.length; i++) {
    const a = base.walls[i], b = r.walls[i];
    assert.ok(Math.abs(Math.hypot(a.b[0] - a.a[0], a.b[1] - a.a[1]) - Math.hypot(b.b[0] - b.a[0], b.b[1] - b.a[1])) < 0.05, 'wall length kept');
  }
  for (let i = 0; i < base.buildings.length; i++) {
    const a = base.buildings[i], b = r.buildings[i];
    assert.equal(rotationOf(b), 30);
    const want = rotPoint(rect.centre(a.footprint), pivot, 30), got = rect.centre(b.footprint);
    assert.ok(Math.hypot(want[0] - got[0], want[1] - got[1]) < 0.05, `${a.id}: centre turned`);
    assert.deepEqual(rect.size(b.footprint).map((v) => Math.round(v)), rect.size(a.footprint).map((v) => Math.round(v)), 'size kept');
    // its world corners = the old corners turned about the pivot
    const oldCorners = footprintPoly(a).map((p) => rotPoint(p, pivot, 30)), newCorners = footprintPoly(b);
    oldCorners.forEach((p, k) => assert.ok(Math.hypot(p[0] - newCorners[k][0], p[1] - newCorners[k][1]) < 0.1));
    a.doors.forEach((d, k) => { const w = rotPoint(d.pos, pivot, 30); assert.ok(Math.hypot(w[0] - b.doors[k].pos[0], w[1] - b.doors[k].pos[1]) < 0.05); });
  }
  r.ladders.forEach((x, k) => assert.ok(Math.abs(normDeg(x.yaw - base.ladders[k].yaw - 30)) < 0.01, 'ladder yaw + 30'));
  r.covers.forEach((x, k) => assert.ok(Math.abs(normDeg(x.yaw - base.covers[k].yaw - 30)) < 0.01, 'cover yaw + 30'));
});

test('rotation: old files load as 0; max 0 re-roll / expansion byte-identical to before (no rotation fields)', () => {
  const accepted = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json'), 'utf8')) as GenLayout;
  assert.deepEqual(validateGenLayout(accepted).filter((e) => /rotation/.test(e)), []);
  assert.ok(accepted.buildings.every((b) => rotationOf(b) === 0 && b.rotation === undefined));
  assert.deepEqual(footprintPoly(accepted.buildings[0]), rect.poly(accepted.buildings[0].footprint));
  const zone = zoneOf(base);
  const plain = regenerateRegion({ layout: base, zone, seed: 5, profile, blocks: 'full', chaos: 1 });
  const zero = regenerateRegion({ layout: base, zone, seed: 5, profile, blocks: 'full', chaos: 1, maxRotationDeg: 0 });
  assert.equal(hash(zero.layout), hash(plain.layout), 'max 0 = the old re-roll');
  assert.ok(zero.layout.buildings.every((b) => b.rotation === undefined) && zero.layout.params.max_rotation_deg === undefined);
  assert.equal(ROTATION.max_deg_default, 0, 'the default keeps the industrial maps square');
  assert.deepEqual(validateGenLayout({ ...base, buildings: [{ ...base.buildings[0], rotation: 'x' }, ...base.buildings.slice(1)] }).filter((e) => /rotation/.test(e)).length, 1);
});

test('rotation: full / types re-roll of the whole map at max 45 over 4 seeds - rotated, deterministic, no overlaps, routes valid, doors face a street', () => {
  const zone = zoneOf(base);
  const streets = base.zones.filter((z) => z.id.startsWith('Z_ST_'));
  let rotated = 0, doorsChecked = 0;
  for (const seed of [1, 2, 3, 4]) for (const blocks of ['full', 'types'] as const) {
    const r = regenerateRegion({ layout: base, zone, seed, profile, blocks, chaos: 1, maxRotationDeg: 45 });
    assert.equal(r.ok, true, r.message);
    assert.equal(r.layout.params.max_rotation_deg, 45, 'the setting is kept in the map');
    const again = regenerateRegion({ layout: base, zone, seed, profile, blocks, chaos: 1, maxRotationDeg: 45 });
    assert.equal(hash(again.layout), hash(r.layout), 'deterministic');
    const turned = r.layout.buildings.filter((b) => rotationOf(b) !== 0);
    rotated += turned.length;
    for (const b of turned) assert.ok(Math.abs(rotationOf(b)) <= 45 + 1e-9, `${b.id}: within +-45`);
    assert.deepEqual(newOverlaps(r.layout), [], `${blocks} seed ${seed}`);
    assert.deepEqual(newErrors(r.layout), [], `${blocks} seed ${seed}: valid`);
    assert.ok(checkLayoutRoutes(r.layout).find((x) => x.mandatory)!.completable, 'the mandatory route');
    // a rotated building with doors still has one opening towards its nearest street (doorSeesStreet)
    // (compared with the same seed at max 0: a template whose door faced a yard, not a street, is not asked to face one turned)
    const flat = regenerateRegion({ layout: base, zone, seed, profile, blocks, chaos: 1, maxRotationDeg: 0 }).layout;
    const facedFlat = new Set(flat.buildings.filter((x) => x.doors.some((d) => doorSeesStreet(x, d.pos, streets))).map((x) => x.id));
    for (const b of turned.filter((x) => x.doors.length && facedFlat.has(x.id))) {
      doorsChecked++;
      assert.ok(b.doors.some((d) => doorSeesStreet(b, d.pos, streets)), `${blocks} seed ${seed}: ${b.id} (${rotationOf(b)} deg) has a door facing a street`);
    }
  }
  assert.ok(rotated >= 20, `rotated buildings over the runs: ${rotated}`);
  assert.ok(doorsChecked >= 5, `doors checked: ${doorsChecked}`);
});

test('rotation: a locked district stays byte-identical at max 45; the expansion at 30 rotates its new blocks without overlaps', () => {
  // lock the block round the first building: it, its walls and everything in the lock survive the 45 deg re-roll byte for byte
  const b0 = base.buildings[0];
  const [[x0, y0], [x1, y1]] = b0.footprint;
  const lockPoly: Vec2[] = [[x0 - 300, y0 - 300], [x1 + 300, y0 - 300], [x1 + 300, y1 + 300], [x0 - 300, y1 + 300]];
  const locked = addLock(base, lockPoly, 'keep');
  const r = regenerateRegion({ layout: locked, zone: zoneOf(base), seed: 3, profile, blocks: 'full', chaos: 1, maxRotationDeg: 45 });
  assert.equal(r.ok, true, r.message);
  assert.equal(JSON.stringify(r.layout.buildings.find((b) => b.id === b0.id)), JSON.stringify(b0), 'the locked building');
  for (const w of base.walls.filter((x) => x.building === b0.id)) assert.equal(JSON.stringify(r.layout.walls.find((x) => x.id === w.id)), JSON.stringify(w));
  assert.ok(r.layout.buildings.some((b) => rotationOf(b) !== 0), 'the rest turned');
  // expansion x4 with the map's setting 30
  const src: GenLayout = { ...base, params: { ...base.params, max_rotation_deg: 30 } };
  const e = expandLayout({ layout: src, factor: 4, seed: 7, profile });
  assert.equal(e.ok, true, e.message);
  const turned = e.layout.buildings.filter((b) => rotationOf(b) !== 0);
  assert.ok(turned.length >= 5, `expansion rotated ${turned.length}`);
  assert.ok(turned.every((b) => Math.abs(rotationOf(b)) <= 30 + 1e-9));
  assert.deepEqual(newOverlaps(e.layout), []);
  assert.equal(hash(expandLayout({ layout: src, factor: 4, seed: 7, profile }).layout), hash(e.layout), 'deterministic');
  assert.ok(base.buildings.every((b) => JSON.stringify(e.layout.buildings.find((x) => x.id === b.id)) === JSON.stringify(b)), 'the old core untouched');
});

test('rotation: the simulator and the roof layers see the rotated walls (no sample in a solid rotated building, no step through a wall)', () => {
  const r = regenerateRegion({ layout: base, zone: zoneOf(base), seed: 3, profile, blocks: 'types', chaos: 1, maxRotationDeg: 45 });
  assert.equal(r.ok, true);
  const l = r.layout;
  // the roof layer of a rotated walkable roof follows the rotated outline
  const roof = l.buildings.find((b) => rotationOf(b) !== 0 && b.roof_walkable && !(b.parapets && b.parapets.length));
  if (roof) {
    const segs = wallSegments(toFormationLayout(l, `roof:${roof.id}`));
    const poly = footprintPoly(roof);
    for (const s of segs) assert.ok(poly.some((p) => Math.hypot(p[0] - s.a[0], p[1] - s.a[1]) < 1), 'the parapet runs on the rotated corners');
  }
  const ev = evaluateLayout({ layout: l, profile, config, track: true });
  assert.ok(ev.route.completable);
  const walls = wallSegments(toFormationLayout(l, 'ground'));
  const solid = l.buildings.filter((b) => !b.enterable && rotationOf(b) !== 0);
  let steps = 0;
  const fr = ev.track!.frames;
  for (let i = 1; i < fr.length; i++) {
    if (fr[i].leg !== fr[i - 1].leg) continue;
    fr[i].pos.forEach((p, m) => {
      for (const b of solid) assert.ok(!inFootprint(b, p, -15), `t=${fr[i].t} m${m} inside ${b.id}`);
      const a = fr[i - 1].pos[m];
      for (const w of walls) if (segSegDist(a, p, w.a, w.b) < (w.thickness ?? 0) / 2) assert.fail(`t=${fr[i].t} m${m} steps through ${(w as { id?: string }).id}`);
      steps++;
    });
  }
  assert.ok(steps > 1000);
});
