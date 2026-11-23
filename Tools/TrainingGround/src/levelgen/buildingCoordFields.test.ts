// Training Ground / level generator - BUILDING_COORD_FIELDS coverage (S2_ROOMS_PLAN.md R4, risk 8: "a new coordinate field forgotten
// in one of the transform lists"). ALL = buildingCoordFields.fixture.ts, a building with every GenBuilding field set (a new field fails
// `npm run typecheck` until it is added there); the tests check that every number pair on it is in building_coord_fields.json and
// that rotateContent / finalizeDraftToLayout move every table field. ALL equals
// Content/Data/LevelGen/TestFixtures/building_all_coord_fields.json, the fixture the Python builder test reads. `npm test`. Pure.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import fixtureJson from '../../../../Content/Data/LevelGen/TestFixtures/building_all_coord_fields.json' with { type: 'json' };
import type { Vec2 } from '../movement/geometry.ts';
import type { CoordField } from './buildingCoordFields.ts';
import { BUILDING_COORD_FIELDS, BUILDING_NON_COORD_PATHS, mapBuildingCoords } from './buildingCoordFields.ts';
import { ALL_FIELDS_BUILDING as ALL } from './buildingCoordFields.fixture.ts';
import type { Draft, DraftBuilding } from './generate.ts';
import { finalizeDraftToLayout } from './generate.ts';
import { toUE } from './profile.ts';
import { normDeg, rotateContent, rotPoint, rotVec, toBuildingFrame } from './rotation.ts';
import type { GenBuilding } from './schema.ts';
import { DEFAULT_PARAMS, rect } from './schema.ts';

type Obj = Record<string, unknown>;
const isPair = (v: unknown): v is Vec2 => Array.isArray(v) && v.length === 2 && v.every((x) => typeof x === 'number');

/** The values at a table path ("partitions[].openings[].pos"), in document order. */
function valuesAt(node: unknown, path: string): unknown[] {
  let cur: unknown[] = [node];
  for (const head of path.split('.')) {
    const many = head.endsWith('[]'), key = many ? head.slice(0, -2) : head;
    cur = cur.flatMap((n) => { const v = (n as Obj | undefined)?.[key]; return v === undefined || v === null ? [] : many ? (v as unknown[]) : [v]; });
  }
  return cur;
}

/** Every point a value of `kind` holds (rect / bounds corners, list members); directions and yaws have none. */
function pointsOf(kind: CoordField['kind'], v: unknown): Vec2[] {
  switch (kind) {
    case 'point': return [v as Vec2];
    case 'point_list': return v as Vec2[];
    case 'rect': return v as Vec2[];
    case 'bounds': return [(v as { min: Vec2 }).min, (v as { max: Vec2 }).max];
    default: return [];
  }
}

const near = (a: readonly number[], b: readonly number[], tol = 0.02) => Math.abs(a[0] - b[0]) <= tol && Math.abs(a[1] - b[1]) <= tol;

test('building coord table: valid rows, no duplicates, every row resolves on the full building to a value of its kind', () => {
  const paths = BUILDING_COORD_FIELDS.map((f) => f.path);
  assert.equal(new Set(paths).size, paths.length, 'no duplicate path');
  for (const f of BUILDING_COORD_FIELDS) {
    const vs = valuesAt(ALL, f.path);
    assert.equal(vs.length, 1, `${f.path}: one value on ALL`);
    const v = vs[0];
    const ok = f.kind === 'yaw' ? typeof v === 'number'
      : f.kind === 'point' || f.kind === 'direction' ? isPair(v)
        : f.kind === 'point_list' ? Array.isArray(v) && v.length > 0 && v.every(isPair)
          : f.kind === 'rect' ? Array.isArray(v) && v.length === 2 && v.every(isPair)
            : isPair((v as Obj).min) && isPair((v as Obj).max);
    assert.ok(ok, `${f.path}: a ${f.kind}`);
  }
  for (const p of BUILDING_NON_COORD_PATHS) assert.ok(valuesAt(ALL, p).length === 1 && valuesAt(ALL, p).every(isPair), `${p}: a number pair`);
});

test('building coord table: every number pair (Vec2 / Vec2[]) on the schema-typed full building is in the table or declared a size', () => {
  const covered = new Set([...BUILDING_COORD_FIELDS.map((f) => f.path), ...BUILDING_NON_COORD_PATHS]);
  const missing: string[] = [], empty: string[] = [];
  const walk = (v: unknown, path: string): void => {
    if (path && covered.has(path)) return;
    if (isPair(v) || (Array.isArray(v) && v.length > 0 && v.every(isPair))) { missing.push(path); return; }
    if (Array.isArray(v)) { if (!v.length) empty.push(path); for (const el of v) walk(el, `${path}[]`); return; }
    if (v && typeof v === 'object') for (const [k, x] of Object.entries(v)) walk(x, path ? `${path}.${k}` : k);
  };
  walk(ALL, '');
  assert.deepEqual(missing, [], 'coordinate-looking fields missing from building_coord_fields.json');
  assert.deepEqual(empty, [], 'ALL keeps one entry per array (an empty list hides its fields from the walk)');
  assert.deepEqual(fixtureJson, ALL, 'building_all_coord_fields.json (the Python fixture) = ALL');
});

test('rotateContent: every table field of a building follows the rigid turn (world fields turn, building-frame fields move with the centre)', () => {
  const pivot: Vec2 = [-500, 700], deg = 30;
  const before: GenBuilding = ALL;
  const content = { walls: [], buildings: [before], ladders: [], covers: [], zones: [], ambush_points: [], mantle_edges: [] };
  const after = rotateContent(content, pivot, deg).buildings[0];
  assert.equal(after.rotation, normDeg((before.rotation ?? 0) + deg));
  const c0 = rect.centre(before.footprint), c1 = rect.centre(after.footprint);
  assert.ok(near(c1, rotPoint(c0, pivot, deg)), 'the footprint centre turns about the pivot');
  /** A point relative to its building, in the building's frame (what a rigid move must keep). */
  const local = (b: GenBuilding, f: CoordField, p: Vec2): Vec2 => {
    const q = f.frame === 'world' ? toBuildingFrame(b, p) : p, c = rect.centre(b.footprint);
    return [q[0] - c[0], q[1] - c[1]];
  };
  for (const f of BUILDING_COORD_FIELDS) {
    const v0 = valuesAt(before, f.path)[0], v1 = valuesAt(after, f.path)[0];
    if (f.kind === 'yaw') { assert.equal(v1, normDeg((v0 as number) + deg), `${f.path}: yaw + ${deg}`); continue; }
    if (f.kind === 'direction') { assert.ok(near(v1 as Vec2, rotVec(v0 as Vec2, deg), 1e-9), `${f.path}: direction turned`); continue; }
    const p0 = pointsOf(f.kind, v0), p1 = pointsOf(f.kind, v1);
    assert.equal(p1.length, p0.length, f.path);
    p0.forEach((p, i) => {
      assert.ok(!near(p, p1[i], 1), `${f.path}: moved`);
      assert.ok(near(local(before, f, p), local(after, f, p1[i])), `${f.path}: same place on the building`);
      if (f.frame === 'world') assert.ok(near(p1[i], rotPoint(p, pivot, deg)), `${f.path}: turned about the pivot`);
      else assert.ok(near(p1[i], [p[0] + c1[0] - c0[0], p[1] + c1[1] - c0[1]]), `${f.path}: moved with the centre`);
    });
  }
});

test('finalizeDraftToLayout: every table field of a draft building is mapped to UE (points, normalised rects, normals, yaws)', () => {
  const size: Vec2 = [4000, 4000];
  const { rotation: _r, ...unrotated } = ALL;
  const draft: Draft = { walls: [], buildings: [unrotated as DraftBuilding], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [] };
  const out = finalizeDraftToLayout(draft, size, { name: 'coord_fields', profile: 'test', seed: 1, params: DEFAULT_PARAMS, start: [0, 0], end: [1, 1] }).buildings[0];
  const U = (p: Vec2) => toUE(p, size);
  for (const f of BUILDING_COORD_FIELDS) {
    const v0 = valuesAt(unrotated, f.path)[0], v1 = valuesAt(out, f.path)[0];
    if (f.kind === 'yaw') { assert.equal(v1, (((v0 as number) + 90) % 360 + 360) % 360, `${f.path}: UE yaw`); continue; }
    if (f.kind === 'direction') { const n = v0 as Vec2; assert.deepEqual(v1, [-n[1], n[0]], `${f.path}: UE normal`); continue; }
    const p0 = pointsOf(f.kind, v0), p1 = pointsOf(f.kind, v1);
    if (f.kind === 'rect' || f.kind === 'bounds') {
      const a = U(p0[0]), b = U(p0[1]);
      assert.deepEqual(p1, [[Math.min(a[0], b[0]), Math.min(a[1], b[1])], [Math.max(a[0], b[0]), Math.max(a[1], b[1])]], `${f.path}: UE rect [min, max]`);
    } else assert.deepEqual(p1, p0.map(U), `${f.path}: UE points`);
  }
  assert.deepEqual(mapBuildingCoords(ALL, { point: (p) => p, rect: (r) => r, direction: (v) => v, yaw: (y) => y }), ALL, 'identity maps = a copy');
});
