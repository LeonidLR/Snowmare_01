// Training Ground «Управление картой» (T-022): moving map elements on the plan - buildings with everything that belongs to them (also
// rotated ones), fence / wall segments and rings, covers, ladders (building constraint), bridges (cut constraint), the wall end points,
// the arrow-key nudge steps, the grid snap, the undo / redo steps (nudge runs merge into one), and the checks after a move (an overlap is
// flagged as a NEW problem). Fixtures are generated in memory from the depot reference (read only); nothing is written, no Content/Data
// file is touched. `npm test`.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import type { Vec2 } from '../movement/geometry.ts';
import { fullWindowAction, isKeyConsumer, planHeightFor } from '../utils/fullWindow.ts';
import { generateLayout } from './generate.ts';
import { assignMap, editLayout, layerById, layerMovable, LAYERS, layoutEntities, movableTypes, pickMoveEntity, validateLayersConfig } from './layers.ts';
import { describeProblems, mapIssues, newProblems, validateMapEdit } from './mapChecks.ts';
import { DEFAULT_SNAP_CM, fenceFamily, ladderAttachProblems, LADDER_REACH_CM, MIN_WALL_LEN_CM, moveBuilding, moveCover, moveCrossing, moveLadder, moveWallEnd, moveWalls, NUDGE_CM, nudgeDelta, snapMoveDelta, snapReference, SNAP_STEPS_CM, translateBuilding } from './mapEdit.ts';
import type { Profile } from './profile.ts';
import { canRedo, canUndo, historyInit, historyPush, historyPushCoalesced, historyRedo, historyUndo } from './regenHistory.ts';
import { regenerateRegion, wallFamily } from './regionRegen.ts';
import { footprintPoly, rotationOf } from './rotation.ts';
import type { GenBuilding, GenLayout } from './schema.ts';
import { validateGenLayout } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const LG = path.join(ROOT, 'Content/Data/LevelGen');
const readJson = <T>(f: string): T => JSON.parse(fs.readFileSync(f, 'utf8')) as T;
const profile = readJson<Profile>(path.join(LG, 'refs/ref_01_02_depot.json'));
const base = generateLayout({ profile, seed: 1 });
const snapshot = (l: GenLayout): string => JSON.stringify(l);
const D: Vec2 = [300, -200];

/** The [x, y] leaves of a value, by path ('size' / 'normal' are not positions). */
function points(v: unknown, at = '', out: Map<string, Vec2> = new Map()): Map<string, Vec2> {
  if (Array.isArray(v)) {
    if (v.length === 2 && typeof v[0] === 'number' && typeof v[1] === 'number') out.set(at, [v[0], v[1]]);
    else v.forEach((x, i) => points(x, `${at}[${i}]`, out));
  } else if (v && typeof v === 'object') {
    for (const [k, x] of Object.entries(v)) if (k !== 'size' && k !== 'normal') points(x, `${at}.${k}`, out);
  }
  return out;
}
const byId = <T extends { id: string }>(xs: T[], id: string): T => xs.find((x) => x.id === id)!;

test('building move: the WHOLE building shifts (footprint, storeys, openings, partitions, parapets, stairs, rooms, mantle edges) with its walls, ladders and carried points', () => {
  const before = snapshot(base);
  for (const id of ['B_hall_n', 'B_shed', 'B_ruin', 'B_gallery']) {
    const b = byId(base.buildings, id);
    const r = moveBuilding(base, id, D);
    assert.ok(r.ok, r.message);
    // every coordinate pair of the building moved by exactly D; nothing else of it changed
    const p0 = points(b), p1 = points(byId(r.layout.buildings, id));
    assert.ok(p0.size > 8, `${id}: has points`);
    assert.deepEqual([...p1.keys()], [...p0.keys()]);
    for (const [k, p] of p0) assert.deepEqual(p1.get(k), [p[0] + D[0], p[1] + D[1]], `${id}${k}`);
    const nb = byId(r.layout.buildings, id);
    assert.equal(nb.rotation, b.rotation, 'rotation kept');
    assert.equal(nb.floors, b.floors); assert.equal(nb.height, b.height); assert.equal(nb.roof_walkable, b.roof_walkable);
    // its walls moved, the other buildings' walls did not
    for (const w of base.walls) {
      const w1 = byId(r.layout.walls, w.id);
      if (w.building === id) assert.deepEqual([w1.a, w1.b], [[w.a[0] + D[0], w.a[1] + D[1]], [w.b[0] + D[0], w.b[1] + D[1]]], w.id);
      else assert.deepEqual(w1, w, `${w.id} untouched`);
    }
    // its ladders moved, the others did not
    for (const k of base.ladders) {
      const k1 = byId(r.layout.ladders, k.id);
      const mine = k.to === `roof:${id}` || k.to.startsWith(`floor:${id}:`) || k.from === `roof:${id}` || k.from.startsWith(`floor:${id}:`);
      assert.deepEqual([k1.pos, k1.foot, k1.top], mine ? [k.pos, k.foot, k.top].map((q) => [q[0] + D[0], q[1] + D[1]]) : [k.pos, k.foot, k.top], k.id);
    }
    // its top-level mantle edges moved
    for (const m of base.mantle_edges ?? []) {
      const m1 = byId(r.layout.mantle_edges!, m.id);
      assert.deepEqual(m1.start, m.building === id ? [m.start[0] + D[0], m.start[1] + D[1]] : m.start, m.id);
    }
    // the layout is still valid and (a pure translation) nothing else changed shape
    assert.deepEqual(validateGenLayout(r.layout).filter((e) => !validateGenLayout(base).includes(e)), [], `${id}: no new schema errors`);
    assert.equal(r.layout.buildings.length, base.buildings.length);
    // and the move is its own inverse
    assert.deepEqual(moveBuilding(r.layout, id, [-D[0], -D[1]]).layout, base, `${id}: moved back = the original`);
  }
  assert.equal(snapshot(base), before, 'the input layout is never changed');
});

test('building move: the ladder of a building moves with it, a ladder joining two buildings stays; ambush points / route points / goals follow, covers stay', () => {
  const l: GenLayout = JSON.parse(snapshot(base));
  const b = byId(l.buildings, 'B_hall_n');
  const inside: Vec2 = [(b.footprint[0][0] + b.footprint[1][0]) / 2, (b.footprint[0][1] + b.footprint[1][1]) / 2];
  l.covers.push({ id: 'COV_in_hall', pos: inside, yaw: 0, size: [60, 60, 100], kind: 'crate' });
  l.ambush_points.push({ id: 'AMB_in_hall', pos: inside });
  l.routes[0].layers = l.routes[0].points.map((_, i) => (i === 1 ? 'roof:B_hall_n' : 'ground'));
  const p1 = l.routes[0].points[1];
  l.objectives = { version: 1, seed: 1, order: 'free', start: { pos: [0, 0], yaw: 0, layer: 'ground', source: 'manual' }, goals: [{ id: 'GOAL_1', pos: inside, layer: 'floor:B_hall_n:1', kind: 'room', building: 'B_hall_n' }] };
  l.ladders.push({ ...l.ladders[0], id: 'LAD_bridge', from: 'roof:B_hall_n', to: 'roof:B_warehouse' });
  const r = moveBuilding(l, 'B_hall_n', D);
  assert.ok(r.ok, r.message);
  assert.deepEqual(byId(r.layout.covers, 'COV_in_hall').pos, inside, 'a cover is not carried (a crate field the building is dragged over would follow it)');
  assert.match(r.message, /укрытий внутри остаётся на месте: 1/);
  assert.deepEqual(byId(r.layout.ambush_points, 'AMB_in_hall').pos, [inside[0] + D[0], inside[1] + D[1]]);
  assert.deepEqual(r.layout.routes[0].points[1], [p1[0] + D[0], p1[1] + D[1]], 'the route point on the roof moves');
  assert.deepEqual(r.layout.routes[0].points[0], l.routes[0].points[0], 'ground route points stay');
  assert.deepEqual(r.layout.objectives!.goals[0].pos, [inside[0] + D[0], inside[1] + D[1]]);
  assert.deepEqual(byId(r.layout.ladders, 'LAD_bridge'), byId(l.ladders, 'LAD_bridge'), 'a ladder between two buildings stays');
  assert.match(r.message, /LAD_bridge/);
  // the hall's own walls (6 on main's fixture, more with the bgen parapet / firing-gap walls): every one is carried
  const hallWalls = l.walls.filter((w) => w.building === 'B_hall_n').length;
  assert.ok(r.carried && r.carried['лестниц'] === 1 && r.carried['стен'] === hallWalls && hallWalls >= 6, JSON.stringify(r.carried));
  // refused: no such building, zero delta, out of the map
  assert.equal(moveBuilding(base, 'B_nope', D).ok, false);
  assert.equal(moveBuilding(base, 'B_hall_n', [0, 0]).ok, false);
  assert.match(moveBuilding(base, 'B_hall_n', [1e6, 0]).message, /границы/);
});

test('building move keeps the rotation: a rotated building moves rigidly (its openings stay where they were relative to it)', () => {
  const zone: Vec2[] = [[500, -9000], [6200, -9000], [6200, 0], [500, 0]];
  const rr = regenerateRegion({ layout: base, zone, seed: 303, profile, blocks: 'full', chaos: 1, maxRotationDeg: 30 });
  assert.ok(rr.ok);
  const rotated = rr.layout.buildings.filter((b) => rotationOf(b) !== 0 && (b.openings?.length ?? 0) > 0);
  assert.ok(rotated.length > 0, 'the re-roll made rotated buildings');
  for (const b of rotated.slice(0, 3)) {
    const r = moveBuilding(rr.layout, b.id, D);
    if (!r.ok) continue; // a building near the edge may be refused: bounds
    const nb = byId(r.layout.buildings, b.id);
    assert.equal(rotationOf(nb), rotationOf(b));
    const c0 = footprintPoly(b).reduce((s, p) => [s[0] + p[0], s[1] + p[1]], [0, 0]), c1 = footprintPoly(nb).reduce((s, p) => [s[0] + p[0], s[1] + p[1]], [0, 0]);
    assert.deepEqual([c1[0] - c0[0], c1[1] - c0[1]], [4 * D[0], 4 * D[1]]);
    b.openings!.forEach((o, i) => assert.deepEqual([nb.openings![i].pos[0] - o.pos[0], nb.openings![i].pos[1] - o.pos[1]], D));
    assert.deepEqual(nb.openings!.map((o) => o.yaw), b.openings!.map((o) => o.yaw), 'yaws are directions: unchanged');
  }
  // translateBuilding is pure
  const b0 = rotated[0], s0 = JSON.stringify(b0);
  translateBuilding(b0, D);
  assert.equal(JSON.stringify(b0), s0);
});

test('fence / wall segment move: one segment, or the whole ring; its mantle edges follow; a building wall belongs to its building', () => {
  const fences = base.walls.filter((w) => w.kind === 'fence');
  const f = fences.find((w) => fenceFamily(w) === 'F_compound')!;
  const ring = base.walls.filter((w) => fenceFamily(w) === 'F_compound');
  assert.ok(ring.length >= 4);
  // single segment
  const one = moveWalls(base, f.id, D);
  assert.ok(one.ok, one.message);
  for (const w of base.walls) assert.deepEqual(byId(one.layout.walls, w.id).a, w.id === f.id ? [w.a[0] + D[0], w.a[1] + D[1]] : w.a, w.id);
  // the whole ring
  const all = moveWalls(base, f.id, D, true);
  assert.ok(all.ok);
  for (const w of base.walls) assert.deepEqual(byId(all.layout.walls, w.id).b, ring.some((x) => x.id === w.id) ? [w.b[0] + D[0], w.b[1] + D[1]] : w.b, w.id);
  assert.match(all.message, /забор F_compound/);
  // a mantle edge that names the wall follows it
  const m = (base.mantle_edges ?? []).find((x) => x.wall && fences.some((w) => w.id === x.wall));
  if (m) {
    const mv = moveWalls(base, m.wall!, D);
    assert.deepEqual(byId(mv.layout.mantle_edges!, m.id).start, [m.start[0] + D[0], m.start[1] + D[1]]);
  }
  // refused: a building's wall (the whole building moves), nothing there, zero, out of the map
  const bw = base.walls.find((w) => w.building)!;
  assert.match(moveWalls(base, bw.id, D).message, /принадлежит зданию/);
  assert.equal(moveWalls(base, 'W_nope', D).ok, false);
  assert.equal(moveWalls(base, f.id, [0, 0]).ok, false);
  assert.equal(moveWalls(base, f.id, [0, 1e6]).ok, false);
  // the family regex is the one of regionRegen.wallFamily
  for (const w of [...base.walls, ...readJson<GenLayout>(path.join(LG, 'accepted/gen_ref_01_02_depot_1_x4.json')).walls]) assert.equal(fenceFamily(w), wallFamily(w), w.id);
});

test('wall end points: the length changes, a stub shorter than the minimum is refused', () => {
  const w = base.walls.find((x) => x.kind === 'fence')!;
  const r = moveWallEnd(base, w.id, 'b', [w.b[0] + 200, w.b[1]]);
  assert.ok(r.ok, r.message);
  const w1 = byId(r.layout.walls, w.id);
  assert.deepEqual(w1.a, w.a); assert.deepEqual(w1.b, [w.b[0] + 200, w.b[1]]);
  assert.equal(moveWallEnd(base, w.id, 'b', [w.a[0] + 10, w.a[1]]).ok, false, `shorter than ${MIN_WALL_LEN_CM} cm`);
  assert.equal(moveWallEnd(base, w.id, 'a', [w.a[0], w.a[1]]).ok, false, 'no change');
  assert.equal(moveWallEnd(base, base.walls.find((x) => x.building)!.id, 'a', [0, 0]).ok, false, 'a building wall has no free end');
  assert.equal(moveWallEnd(base, w.id, 'a', [1e7, 0]).ok, false, 'out of the map');
});

test('cover move; every other entity type is refused by the cover mover', () => {
  const c = base.covers[0];
  const r = moveCover(base, c.id, D);
  assert.deepEqual(byId(r.layout.covers, c.id).pos, [c.pos[0] + D[0], c.pos[1] + D[1]]);
  assert.equal(byId(r.layout.covers, c.id).yaw, c.yaw);
  assert.equal(moveCover(base, 'COV_nope', D).ok, false);
});

test('ladder move keeps its building constraint: along the wall yes, away from the building / off the roof no', () => {
  const k = byId(base.ladders, 'LAD_B_gallery');
  assert.deepEqual(ladderAttachProblems(base, k), []);
  const b = byId(base.buildings, 'B_gallery');
  const along = moveLadder(base, k.id, [0, 50]);
  assert.ok(along.ok || /лестница/.test(along.message));
  // far away from the building: refused, the layout is returned unchanged
  const away = moveLadder(base, k.id, [LADDER_REACH_CM + 500, 0]);
  assert.equal(away.ok, false);
  assert.match(away.message, /дальше|вне крыши/);
  assert.equal(away.layout, base);
  // a canal ladder is not a building ladder
  assert.equal(moveLadder(base, 'LAD_cut_1', D).ok, false);
  void b;
  // moving the building first and then checking: the ladder went with it, so it is attached
  const mv = moveBuilding(base, 'B_gallery', D);
  assert.deepEqual(ladderAttachProblems(mv.layout, byId(mv.layout.ladders, k.id)), []);
});

test('bridge move: it must still cross the canal', () => {
  const x = base.cut!.crossings[0];
  const ok = moveCrossing(base, x.id, [0, 100]);
  assert.ok(ok.ok, ok.message);
  const poly = byId(ok.layout.cut!.crossings, x.id).poly;
  assert.deepEqual(poly, x.poly.map((p) => [p[0], p[1] + 100]));
  // far along X: off the canal -> refused
  const off = moveCrossing(base, x.id, [4000, 0]);
  assert.equal(off.ok, false);
  assert.match(off.message, /канал/);
  assert.equal(moveCrossing(base, 'X_nope', D).ok, false);
});

test('layers: the move permission - the buildings layer moves buildings + ladders, bridges moves crossings, obstacles move walls / covers; layers.json is untouched', () => {
  assert.deepEqual(validateLayersConfig(LAYERS), []);
  // the designer's data file does NOT list move for these layers: the permission is the code's MAP_MOVE
  assert.ok(!layerById('buildings')!.edit.includes('move') && !layerById('bridges')!.edit.includes('move'));
  assert.ok(layerMovable(layerById('buildings')!) && layerMovable(layerById('bridges')!) && layerMovable(layerById('obstacles_small')!));
  assert.ok(!layerMovable(layerById('roads')!) && !layerMovable(layerById('canal')!) && !layerMovable(layerById('routes')!));
  assert.deepEqual([...movableTypes(layerById('buildings')!)], ['building', 'ladder']);
  const a = assignMap(base);
  const layerOf = (key: string) => a.layerOf[key];
  // buildings layer: a building, a building's WALL (redirected to the whole building), a ladder
  const bw = base.walls.find((w) => w.building === 'B_shed')!;
  const viaWall = editLayout(base, 'buildings', { op: 'move', key: `wall:${bw.id}`, delta: D });
  assert.ok(viaWall.ok, viaWall.message);
  assert.equal(viaWall.key, 'building:B_shed');
  assert.deepEqual(byId(viaWall.layout.buildings, 'B_shed').footprint[0], [byId(base.buildings, 'B_shed').footprint[0][0] + D[0], byId(base.buildings, 'B_shed').footprint[0][1] + D[1]]);
  assert.ok(editLayout(base, 'buildings', { op: 'move', key: 'building:B_hall_n', delta: D }).ok);
  assert.equal(layerOf('ladder:LAD_B_gallery'), 'buildings');
  assert.ok(editLayout(base, 'buildings', { op: 'move', key: 'ladder:LAD_B_gallery', delta: [0, 40] }).ok);
  // bridges layer
  const cx = base.cut!.crossings[0];
  assert.ok(editLayout(base, 'bridges', { op: 'move', key: `crossing:${cx.id}`, delta: [0, 100] }).ok);
  // the layer scope still holds: a building through the roads layer / a cover through the buildings layer
  assert.equal(editLayout(base, 'roads', { op: 'move', key: 'building:B_hall_n', delta: D }).ok, false);
  assert.equal(editLayout(base, 'buildings', { op: 'move', key: `cover:${base.covers[0].id}`, delta: D }).ok, false);
  // rotate stays for covers / walls of the move layers only
  assert.equal(editLayout(base, 'buildings', { op: 'rotate', key: 'building:B_hall_n', deg: 15 }).ok, false);
  // obstacles: a fence segment, the ring flag, an end point, a cover
  const f = base.walls.find((w) => fenceFamily(w) === 'F_compound')!;
  const lf = layerOf(`wall:${f.id}`);
  assert.ok(editLayout(base, lf, { op: 'move', key: `wall:${f.id}`, delta: D }).ok);
  const fam = editLayout(base, lf, { op: 'move', key: `wall:${f.id}`, delta: D, family: true });
  assert.ok(fam.ok && /забор F_compound/.test(fam.message));
  assert.ok(editLayout(base, lf, { op: 'move_end', key: `wall:${f.id}`, end: 'b', pos: [f.b[0] + 100, f.b[1]] }).ok);
  assert.equal(editLayout(base, 'buildings', { op: 'move_end', key: 'building:B_hall_n', end: 'a', pos: [0, 0] }).ok, false);
  const cv = base.covers[0];
  assert.ok(editLayout(base, layerOf(`cover:${cv.id}`), { op: 'move', key: `cover:${cv.id}`, delta: D }).ok);
  // every entity of the plan is still in exactly one layer after the moves
  const moved = moveBuilding(base, 'B_hall_n', D).layout;
  assert.deepEqual(assignMap(moved).problems, []);
  assert.equal(layoutEntities(moved).length, layoutEntities(base).length);
});

test('picking for the move tool: a click on a building wall picks the building; on a ladder the ladder', () => {
  const a = assignMap(base);
  const b = byId(base.buildings, 'B_hall_n'), w = base.walls.find((x) => x.building === b.id)!;
  const onWall: Vec2 = [(w.a[0] + w.b[0]) / 2, (w.a[1] + w.b[1]) / 2];
  const hit = pickMoveEntity(a, base, 'buildings', onWall, 30);
  assert.ok(hit);
  assert.ok(hit!.key === `building:${b.id}` || hit!.type === 'ladder', hit!.key);
  const k = byId(base.ladders, 'LAD_B_gallery');
  assert.equal(pickMoveEntity(a, base, 'buildings', k.pos, 10)?.key, 'ladder:LAD_B_gallery');
  assert.equal(pickMoveEntity(a, base, 'buildings', [0, 0], 5)?.type === 'building', false);
});

test('nudge steps: 10 cm / Shift 1 m / Ctrl 10 m, up = +X, right = +Y; other keys are not nudges', () => {
  assert.deepEqual(NUDGE_CM, { fine: 10, coarse: 100, huge: 1000 });
  assert.deepEqual(nudgeDelta('ArrowUp'), [10, 0]);
  assert.deepEqual(nudgeDelta('ArrowDown'), [-10, 0]);
  assert.deepEqual(nudgeDelta('ArrowRight'), [0, 10]);
  assert.deepEqual(nudgeDelta('ArrowLeft'), [0, -10]);
  assert.deepEqual(nudgeDelta('ArrowUp', { shift: true }), [100, 0]);
  assert.deepEqual(nudgeDelta('ArrowLeft', { ctrl: true }), [0, -1000]);
  assert.deepEqual(nudgeDelta('ArrowLeft', { shift: true, ctrl: true }), [0, -1000], 'Ctrl wins');
  assert.equal(nudgeDelta('a'), null);
  assert.equal(nudgeDelta('Delete'), null);
  // a nudge moves the building by exactly that step
  const r = moveBuilding(base, 'B_shed', nudgeDelta('ArrowRight', { shift: true })!);
  assert.deepEqual(byId(r.layout.buildings, 'B_shed').footprint[0], [byId(base.buildings, 'B_shed').footprint[0][0], byId(base.buildings, 'B_shed').footprint[0][1] + 100]);
});

test('grid snap: the reference point lands on the grid; no snap = whole cm; references per entity type', () => {
  assert.deepEqual(snapMoveDelta([1234, 567], [30, 41], 100), [66, 33]); // (1264, 608) -> (1300, 600)
  assert.deepEqual(snapMoveDelta([0, 0], [49, 51], 100), [0, 100]);
  assert.deepEqual(snapMoveDelta([1234, 567], [30.4, 40.6], 0), [30, 41]);
  assert.deepEqual(snapMoveDelta([100, 100], [1000, 1000], 1000), [900, 900]);
  assert.ok(SNAP_STEPS_CM.includes(DEFAULT_SNAP_CM) && SNAP_STEPS_CM.includes(1000));
  const b = byId(base.buildings, 'B_hall_n');
  const ref = snapReference(base, 'building:B_hall_n')!;
  assert.deepEqual(ref, [(b.footprint[0][0] + b.footprint[1][0]) / 2, (b.footprint[0][1] + b.footprint[1][1]) / 2]);
  const d = snapMoveDelta(ref, [-333, 777], 500);
  const mv = moveBuilding(base, 'B_hall_n', d);
  assert.ok(mv.ok, mv.message);
  const moved = mv.layout;
  const ref2 = snapReference(moved, 'building:B_hall_n')!;
  assert.deepEqual(ref2, [4000, -500], `the centre ${ref2} is on the 5 m grid`);
  assert.equal(snapReference(base, 'zone:Z'), null);
});

test('undo / redo through the page history: a move is one step, an arrow-key run of one entity is ONE step, any other edit breaks the run', () => {
  let h = historyInit(base, 'исходная');
  const m1 = moveBuilding(base, 'B_shed', D).layout;
  h = historyPush(h, m1, 'move');
  assert.equal(h.past.length, 1);
  // ten nudges of the same building: one more step in total
  let cur = m1;
  for (let i = 0; i < 10; i++) { cur = moveBuilding(cur, 'B_hall_n', [10, 0]).layout; h = historyPushCoalesced(h, cur, 'nudge', 'nudge:building:B_hall_n'); }
  assert.equal(h.past.length, 2, 'the run is one step');
  assert.deepEqual(byId(h.present.state.buildings, 'B_hall_n').footprint[0][0], byId(base.buildings, 'B_hall_n').footprint[0][0] + 100);
  // another entity = a new step
  cur = moveBuilding(cur, 'B_shed', [10, 0]).layout; h = historyPushCoalesced(h, cur, 'nudge', 'nudge:building:B_shed');
  assert.equal(h.past.length, 3);
  // undo steps back through them, redo forward again, the originals are untouched
  h = historyUndo(h); assert.equal(byId(h.present.state.buildings, 'B_shed').footprint[0][1], byId(m1.buildings, 'B_shed').footprint[0][1]);
  h = historyUndo(h); assert.deepEqual(h.present.state.buildings, m1.buildings, 'the whole run undone at once');
  h = historyUndo(h); assert.deepEqual(h.present.state, base);
  assert.ok(!canUndo(h) && canRedo(h));
  h = historyRedo(h); h = historyRedo(h); h = historyRedo(h);
  assert.deepEqual(h.present.state, cur);
  assert.ok(!canRedo(h));
  // after an undo a new nudge starts a NEW step (it must not merge into the step before)
  h = historyUndo(h);
  const after = moveBuilding(h.present.state, 'B_hall_n', [10, 0]).layout;
  const h2 = historyPushCoalesced(h, after, 'nudge', 'nudge:building:B_hall_n');
  assert.equal(h2.past.length, h.past.length + 1);
});

test('validation after a move: a building dragged onto another is flagged as a NEW problem (never silently accepted); an old defect is not blamed on the move', () => {
  const v0 = validateMapEdit(base);
  assert.deepEqual(newProblems(v0, v0), { issues: [], schema: [] }, 'nothing moved, nothing new');
  const a = byId(base.buildings, 'B_hall_n'), b = byId(base.buildings, 'B_warehouse');
  const ca: Vec2 = [(a.footprint[0][0] + a.footprint[1][0]) / 2, (a.footprint[0][1] + a.footprint[1][1]) / 2];
  const cb: Vec2 = [(b.footprint[0][0] + b.footprint[1][0]) / 2, (b.footprint[0][1] + b.footprint[1][1]) / 2];
  // the target must stay inside the map: move the warehouse onto the hall
  const r = moveBuilding(base, 'B_warehouse', [ca[0] - cb[0], ca[1] - cb[1]]);
  assert.ok(r.ok, r.message);
  const fresh = newProblems(v0, validateMapEdit(r.layout));
  assert.ok(fresh.issues.some((i) => i.kind === 'building_overlap' && i.id === 'building_overlap:B_hall_n:B_warehouse' && i.severity === 'error'), JSON.stringify(fresh.issues.map((i) => i.id)));
  assert.match(describeProblems(fresh), /B_hall_n и B_warehouse пересекаются|B_warehouse и B_hall_n пересекаются/);
  // the same pair after moving back: the problem is gone
  const back = moveBuilding(r.layout, 'B_warehouse', [cb[0] - ca[0], cb[1] - ca[1]]);
  assert.deepEqual(newProblems(v0, validateMapEdit(back.layout)).issues.filter((i) => i.kind === 'building_overlap'), []);
  // a building on the cut walls / a cover inside a building / on a wall
  const cut = base.cut!;
  const onCut = moveBuilding(base, 'B_shed', [cut.poly[0][0] - byId(base.buildings, 'B_shed').footprint[0][0], cut.poly[0][1] - byId(base.buildings, 'B_shed').footprint[0][1]]);
  if (onCut.ok) assert.ok(newProblems(v0, validateMapEdit(onCut.layout)).issues.some((i) => i.kind === 'building_cut'));
  const w = base.walls.find((x) => x.kind === 'fence')!;
  const coverOnWall: GenLayout = { ...base, covers: [...base.covers, { id: 'COV_on_wall', pos: [(w.a[0] + w.b[0]) / 2, (w.a[1] + w.b[1]) / 2], yaw: 0, size: [100, 100, 100], kind: 'crate' }] };
  assert.ok(newProblems(v0, validateMapEdit(coverOnWall)).issues.some((i) => i.id.startsWith('cover_on_wall:COV_on_wall')));
  const inB: GenLayout = { ...base, covers: [...base.covers, { id: 'COV_in_b', pos: ca, yaw: 0, size: [100, 100, 100], kind: 'crate' }] };
  assert.ok(newProblems(v0, validateMapEdit(inB)).issues.some((i) => i.id === 'cover_in_building:COV_in_b:B_hall_n'));
  // a torn-off ladder is reported even if it came from outside the tool
  const torn: GenLayout = { ...base, ladders: base.ladders.map((k) => (k.id === 'LAD_B_gallery' ? { ...k, pos: [k.pos[0] + 5000, k.pos[1]], foot: [k.foot[0] + 5000, k.foot[1]], top: [k.top[0] + 5000, k.top[1]] } : k)) };
  assert.ok(mapIssues(torn).some((i) => i.id === 'ladder_detached:LAD_B_gallery'));
});

test('the checks run on every shipped map without throwing and fast enough to run after every move', () => {
  for (const f of fs.readdirSync(path.join(LG, 'accepted')).filter((x) => x.endsWith('.json') && !x.endsWith('.meta.json'))) {
    const l = readJson<GenLayout>(path.join(LG, 'accepted', f));
    const t0 = performance.now();
    const v = validateMapEdit(l);
    const ms = performance.now() - t0;
    assert.ok(ms < 1500, `${f}: ${ms.toFixed(0)} ms`);
    assert.deepEqual(newProblems(v, v), { issues: [], schema: [] }, f);
    // the generator's old defects are not move-made problems: a no-op layout copy has none
    assert.deepEqual(newProblems(v, validateMapEdit(JSON.parse(JSON.stringify(l)))), { issues: [], schema: [] }, f);
  }
});

test('full window rules (shared by the map page and the props canvas): F11 toggles, Esc leaves, a text field / a modifier / an inactive page keep their keys', () => {
  const on = { enabled: true, full: false };
  assert.equal(fullWindowAction({ key: 'F11' }, on), 'toggle');
  assert.equal(fullWindowAction({ key: 'F11' }, { enabled: true, full: true }), 'toggle');
  assert.equal(fullWindowAction({ key: 'Escape' }, { enabled: true, full: true }), 'exit');
  assert.equal(fullWindowAction({ key: 'Escape' }, on), null, 'Esc does nothing in the normal mode');
  assert.equal(fullWindowAction({ key: 'Escape' }, { enabled: true, full: true, escExits: false }), null, 'the polygon tool keeps its Esc');
  assert.equal(fullWindowAction({ key: 'F11' }, { enabled: false, full: false }), null, 'another page / tab: the browser keeps F11');
  for (const target of [{ tagName: 'INPUT', type: 'text' }, { tagName: 'INPUT', type: 'number' }, { tagName: 'INPUT' }, { tagName: 'TEXTAREA' }]) assert.equal(fullWindowAction({ key: 'F11', target }, on), null, JSON.stringify(target));
  // a <select> / a checkbox / a button that kept the focus after a click do not swallow F11
  for (const target of [{ tagName: 'SELECT' }, { tagName: 'INPUT', type: 'checkbox' }, { tagName: 'INPUT', type: 'range' }, { tagName: 'BUTTON' }]) assert.equal(fullWindowAction({ key: 'F11', target }, on), 'toggle', JSON.stringify(target));
  // the page's own shortcuts (arrows / Delete / Ctrl+Z) stay out of typing fields, selects and sliders, but work after a checkbox click
  assert.equal(isKeyConsumer({ tagName: 'INPUT', type: 'number' }), true);
  assert.equal(isKeyConsumer({ tagName: 'SELECT' }), true);
  assert.equal(isKeyConsumer({ tagName: 'INPUT', type: 'range' }), true);
  assert.equal(isKeyConsumer({ tagName: 'INPUT', type: 'checkbox' }), false);
  assert.equal(isKeyConsumer({ tagName: 'BUTTON' }), false);
  assert.equal(isKeyConsumer(null), false);
  assert.equal(fullWindowAction({ key: 'F11', target: { tagName: 'DIV', isContentEditable: true } }, on), null);
  assert.equal(fullWindowAction({ key: 'F11', target: { tagName: 'CANVAS' } }, on), 'toggle');
  assert.equal(fullWindowAction({ key: 'F11', ctrlKey: true }, on), null);
  assert.equal(fullWindowAction({ key: 'F11', shiftKey: true }, on), null);
  assert.equal(fullWindowAction({ key: 'a' }, on), null);
});

test('plan height: the viewport minus the toolbars, never below the minimum', () => {
  assert.equal(planHeightFor(1080), 820);
  assert.equal(planHeightFor(700), 600);
  assert.equal(planHeightFor(300), 600);
  assert.equal(planHeightFor(1000, 170, 320), 830);
  assert.equal(planHeightFor(400, 170, 320), 320);
});
