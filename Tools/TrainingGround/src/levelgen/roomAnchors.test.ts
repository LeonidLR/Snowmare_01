// Training Ground / level generator v2 - ROOM ANCHORS (roomAnchors.ts, GenRoom.anchor): the walkable representative point of a room in
// the building frame. The B_hall_n-like hollow stair strip (gap + flights), a room with a flight starting on its floor (B_x2_23-like),
// a free room (anchor = centre), a covered one (none), a rotated building (anchor in the building frame), determinism, the generator's
// own anchors, and the CLI (scripts/add-room-anchors.ts) idempotent. `npm test`. Inputs: the Depot reference profile (nothing written but a temp file).
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import type { Vec2 } from '../movement/geometry.ts';
import { generateLayout, STAIR_FLIGHT_W } from './generate.ts';
import type { Profile } from './profile.ts';
import { ANCHOR_CLEARANCE_CM, ANCHOR_FLIGHT_HALF_W_CM, addRoomAnchors, anchorLayoutText, roomAnchor, withRoomAnchors } from './roomAnchors.ts';
import { rotateContent, toBuildingFrame } from './rotation.ts';
import type { GenBuilding, GenLayout, GenRoom } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const TG = path.join(ROOT, 'Tools/TrainingGround');

const building = (o: Partial<GenBuilding> & Pick<GenBuilding, 'footprint' | 'floors'>): GenBuilding => ({
  id: 'B', height: o.floors * 300, enterable: true, roof_walkable: true, doors: [], stairs: [], roof_holes: [], floor_holes: [], set_piece: 'warehouse', ...o,
});
const room = (id: string, floor: number, min: Vec2, max: Vec2, kind: GenRoom['kind'] = 'room'): GenRoom => ({ id, floor, bounds: { min, max }, kind });
const poly = (x0: number, y0: number, x1: number, y1: number): Vec2[] => [[x0, y0], [x1, y0], [x1, y1], [x0, y1]];
/** Distance from p to the axis-aligned box. */
const distBox = (p: Vec2, x0: number, y0: number, x1: number, y1: number) => Math.hypot(Math.max(x0 - p[0], 0, p[0] - x1), Math.max(y0 - p[1], 0, p[1] - y1));

/** B_hall_n like: a hollow 3-storey hall, its stair strip on floor 2 = a gap + two flights (1 -> 2 and 2 -> 3), landings at both ends. */
function hallStrip(): { b: GenBuilding; core: GenRoom } {
  const core = room('B_hall_n_r_f2_core', 2, [3215, -3235], [3535, -2506], 'stairwell');
  const b = building({
    id: 'B_hall_n', footprint: [[3000, -4000], [6000, -2000]], floors: 3, hollow: true,
    floors_detail: [{ floor: 2, height_z: 600, gaps: [{ id: 'B_hall_n_gap_stair_f2', poly: poly(3375, -3085, 3535, -2656), kind: 'stairwell' }], rooms: [core] }],
    stairs: [
      { id: 'B_hall_n_stair_1', pos: [3455, -2656], top: [3455, -3085], from: 1, to: 2, kind: 'stairwell' },
      { id: 'B_hall_n_stair_2', pos: [3295, -3085], top: [3295, -2656], from: 2, to: 3, kind: 'stairwell' },
    ],
    rooms: [core],
  });
  return { b, core };
}

/** B_x2_23 like: a 2-storey block whose floor-1 room has the flight 1 -> 2 starting across its middle. */
function blockWithRamp(): { b: GenBuilding; r: GenRoom } {
  const r = room('B_x2_23_t8_block_r_f1_0', 1, [1265, -21414], [1846, -20414]);
  const b = building({
    id: 'B_x2_23_t8_block', footprint: [[1265, -21414], [2400, -20414]], floors: 2,
    floors_detail: [{ floor: 1, height_z: 300, gaps: [{ poly: poly(1671, -21249, 1831, -20820), kind: 'stairwell' }], rooms: [r] }],
    stairs: [
      { id: 'st0', pos: [1751, -20820], top: [1751, -21249], from: 0, to: 1, kind: 'stairwell' },
      { id: 'st1', pos: [1591, -20820], top: [1591, -21249], from: 1, to: 2, kind: 'stairwell' },
    ],
    rooms: [r],
  });
  return { b, r };
}

test('anchor constants: the flight half width is the generator lane (STAIR_FLIGHT_W / 2 + 5 cm of wall)', () => {
  assert.equal(ANCHOR_FLIGHT_HALF_W_CM, (STAIR_FLIGHT_W * 100) / 2 + 5);
});

test('roomAnchor: the hollow stair strip: the anchor is on the ARRIVAL (south) landing, not on the isolated north pocket nearer the centre', () => {
  const { b, core } = hallStrip();
  const a = roomAnchor(b, core)!;
  assert.ok(a, 'an anchor');
  // stair_1 (1 -> 2) tops out at y -3085: the man stands on the south landing y -3235..-3085; the north landing (y -2656..-2506) is cut off
  // from it by the gap + the flights, although its cells (y -2600) lie as near the bounds centre (-2870) as the south ones
  assert.deepEqual(a, [3375, -3150]);
  assert.ok(a[1] >= -3235 && a[1] <= -3085 - ANCHOR_CLEARANCE_CM, 'on the south landing');
  assert.ok(distBox(a, 3375, -3085, 3535, -2656) >= ANCHOR_CLEARANCE_CM, 'clear of the gap');
  assert.ok(distBox(a, 3215, -3085, 3375, -2656) >= ANCHOR_CLEARANCE_CM, 'clear of the 2 -> 3 flight');
  assert.ok(a[0] >= 3215 + 50 && a[0] <= 3535 - 50 && a[1] >= -3235 + 50 && a[1] <= -2506 - 50, 'inside the bounds inset');
  // with no flight arriving (and no door) the room is not restricted: the old rule picks the nearer north landing
  const noArrival = { ...b, stairs: b.stairs.filter((s) => s.to !== 2) };
  assert.deepEqual(roomAnchor(noArrival, core), [3375, -2600]);
});

test('roomAnchor: with no arriving flight the first door of the room (by id) seeds the entry; a partition is open at its doors', () => {
  const r: GenRoom = { ...room('R', 0, [0, 0], [600, 400]), door_ids: ['d_b', 'd_a'] };
  const wall = { id: 'p', start: [300, 0] as Vec2, end: [300, 400] as Vec2, floor: 0 };
  const base = building({ footprint: [[0, 0], [600, 400]], floors: 1, rooms: [r] });
  const eastDoor = { id: 'd_a', kind: 'door' as const, pos: [600, 100] as Vec2, floor: 0, width: 100 };
  const westDoor = { id: 'd_b', kind: 'door' as const, pos: [0, 100] as Vec2, floor: 0, width: 100 };
  const closed = { ...base, partitions: [wall], openings: [westDoor, eastDoor] };
  assert.ok(roomAnchor(closed, r)![0] > 300, 'd_a (first by id) is on the east side of the closed partition');
  const withDoor = { ...base, partitions: [{ ...wall, openings: [{ id: 'pd', kind: 'door' as const, pos: [300, 200] as Vec2, floor: 0, width: 100 }] }], openings: [westDoor, eastDoor] };
  assert.ok(roomAnchor(withDoor, r)![0] < 300, 'a door in the partition joins both sides: the tie goes to the smaller x');
});

test('roomAnchor: a room with a flight starting on its floor gets an anchor off the ramp (and not at the centre)', () => {
  const { b, r } = blockWithRamp();
  const centre: Vec2 = [1555.5, -20914];
  assert.ok(distBox(centre, 1511, -21249, 1671, -20820) === 0, 'the centre is on the ramp 1 -> 2');
  const a = roomAnchor(b, r)!;
  assert.ok(a);
  assert.notDeepEqual(a, centre);
  assert.ok(distBox(a, 1511, -21249, 1671, -20820) >= ANCHOR_CLEARANCE_CM, 'clear of the ramp');
  assert.ok(distBox(a, 1671, -21249, 1831, -20820) >= ANCHOR_CLEARANCE_CM, 'clear of the stairwell gap');
});

test('roomAnchor: a free room anchors at its centre; partitions, hatches and floor holes of the floor block, those of another floor do not', () => {
  const r = room('R', 0, [0, 0], [600, 400]);
  const b = building({ footprint: [[0, 0], [600, 400]], floors: 2, rooms: [r] });
  assert.deepEqual(roomAnchor(b, r), [300, 200]);
  const split = { ...b, partitions: [{ id: 'p', start: [300, 0], end: [300, 400], floor: 0, thickness: 20 }] as GenBuilding['partitions'] };
  const a = roomAnchor(split, r)!;
  assert.ok(Math.abs(a[0] - 300) >= 10 + ANCHOR_CLEARANCE_CM, 'off the partition (half thickness + clearance)');
  const upstairs = { ...b, partitions: [{ id: 'p', start: [300, 0], end: [300, 400], floor: 1 }] as GenBuilding['partitions'], hatches: [{ id: 'h', pos: [300, 200] as Vec2, floor: 1 }], floor_holes: [{ pos: [300, 200] as Vec2, floor: 1 }] };
  assert.deepEqual(roomAnchor(upstairs, r), [300, 200], 'another floor blocks nothing');
  const hatch = { ...b, hatches: [{ id: 'h', pos: [300, 200] as Vec2, floor: 0, size: [100, 100] as [number, number] }] };
  const h = roomAnchor(hatch, r)!;
  assert.ok(distBox(h, 250, 150, 350, 250) >= ANCHOR_CLEARANCE_CM);
  const hole = { ...b, floor_holes: [{ pos: [300, 200] as Vec2, floor: 0 }] };
  assert.ok(distBox(roomAnchor(hole, r)!, 180, 80, 420, 320) >= ANCHOR_CLEARANCE_CM);
});

test('roomAnchor: a room fully covered by a gap (or too small for the clearance) has no anchor', () => {
  const r = room('R', 1, [0, 0], [300, 300]);
  const covered = building({ footprint: [[0, 0], [300, 300]], floors: 2, rooms: [r], floors_detail: [{ floor: 1, gaps: [{ poly: poly(-10, -10, 310, 310), kind: 'atrium' }], rooms: [r] }] });
  assert.equal(roomAnchor(covered, r), undefined);
  const tiny = room('T', 0, [0, 0], [90, 400]);
  assert.equal(roomAnchor(building({ footprint: [[0, 0], [90, 400]], floors: 1, rooms: [tiny] }), tiny), undefined);
  const out = withRoomAnchors(covered);
  assert.ok(!('anchor' in out.rooms![0]), 'no key for a room without a point');
});

test('roomAnchor: a rotated building anchors in the building frame (not the world frame), the same point as unrotated', () => {
  const { b } = hallStrip();
  const c0: Vec2 = [(b.footprint[0][0] + b.footprint[1][0]) / 2, (b.footprint[0][1] + b.footprint[1][1]) / 2];
  const content = { walls: [], buildings: [b], ladders: [], covers: [], zones: [], ambush_points: [], mantle_edges: [] };
  const turned = rotateContent(content, c0, 30).buildings[0];
  assert.equal(turned.rotation, 30);
  assert.deepEqual(turned.rooms![0].bounds, b.rooms![0].bounds, 'room bounds stay in the building frame');
  const a = roomAnchor(turned, turned.rooms![0])!;
  assert.deepEqual(a, roomAnchor(b, b.rooms![0]));
  // the world gap polygon is turned: read as if it were in the building frame it would block elsewhere
  const gapPoly = turned.floors_detail![0].gaps![0].poly;
  assert.ok(gapPoly.some((p) => Math.abs(p[0] - 3375) > 1 && Math.abs(p[0] - 3535) > 1), 'the gap really is turned');
  const q = toBuildingFrame(turned, gapPoly[0]);
  assert.ok(Math.abs(q[0] - 3375) < 0.1 || Math.abs(q[0] - 3535) < 0.1, 'and maps back to the building frame');
  const anchored = withRoomAnchors(turned);
  assert.deepEqual(anchored.rooms![0].anchor, a);
});

test('roomAnchor / withRoomAnchors: deterministic, order independent, shared rooms stay one object, the input is not mutated, idempotent', () => {
  const { b } = hallStrip();
  assert.deepEqual(roomAnchor(b, b.rooms![0]), roomAnchor(b, b.rooms![0]));
  assert.deepEqual(roomAnchor({ ...b, stairs: [...b.stairs].reverse() }, b.rooms![0]), roomAnchor(b, b.rooms![0]));
  const before = JSON.stringify(b);
  const once = withRoomAnchors(b);
  assert.equal(JSON.stringify(b), before, 'input untouched');
  assert.equal(once.rooms![0], once.floors_detail![0].rooms![0], 'the room shared by rooms[] and floors_detail[] stays shared');
  assert.deepEqual(withRoomAnchors(once), once, 'idempotent');
  const second = room('S', 0, [0, 0], [400, 400]);
  const withTies = building({ footprint: [[0, 0], [400, 400]], floors: 1, rooms: [second], partitions: [{ id: 'p', start: [200, 0], end: [200, 400], floor: 0 }] });
  const t = roomAnchor(withTies, second)!;
  assert.ok(t[0] < 200, 'a tie between the two sides goes to the smaller x');
  const open = building({ footprint: [[0, 0], [400, 400]], floors: 1, enterable: false, rooms: [second] });
  assert.equal(withRoomAnchors(open), open, 'a building that is not enterable is returned as it is');
});

const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
const stripAnchors = (l: GenLayout): string => JSON.stringify(JSON.parse(JSON.stringify(l), (k, v) => (k === 'anchor' ? undefined : v)), null, 1);

test('the generator anchors every room of an enterable building: inside its bounds, equal to addRoomAnchors, and the CLI text reproduces them', () => {
  const l = generateLayout({ profile, seed: 1 });
  let rooms = 0, anchored = 0;
  for (const b of l.buildings) {
    if (!b.enterable) continue;
    for (const r of [...(b.rooms ?? []), ...(b.floors_detail ?? []).flatMap((fd) => fd.rooms ?? [])]) {
      rooms++;
      if (!r.anchor) continue;
      anchored++;
      assert.ok(r.anchor[0] >= r.bounds.min[0] && r.anchor[0] <= r.bounds.max[0] && r.anchor[1] >= r.bounds.min[1] && r.anchor[1] <= r.bounds.max[1], `${r.id}: anchor inside bounds`);
    }
  }
  assert.ok(rooms > 0 && anchored > 0, 'the depot has anchored rooms');
  assert.deepEqual(addRoomAnchors(l), l, 'already anchored');
  const text = JSON.stringify(l, null, 1);
  assert.equal(anchorLayoutText(text), text, 'idempotent, same bytes');
  const stripped = stripAnchors(l);
  assert.ok(!stripped.includes('"anchor"'));
  assert.equal(anchorLayoutText(stripped), text, 'a layout without anchors gets exactly the generator anchors');
});

test('CLI add-room-anchors: writes a copy with --out, in place otherwise, and a second run changes no byte', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'anchors-'));
  try {
    const l = generateLayout({ profile, seed: 1 });
    const stripped = stripAnchors(l);
    const file = path.join(dir, 'layout.json'), copy = path.join(dir, 'copy.json');
    fs.writeFileSync(file, stripped);
    const run = (...a: string[]) => spawnSync(process.execPath, [path.join(TG, 'scripts/add-room-anchors.ts'), ...a], { encoding: 'utf8', cwd: TG });
    const r0 = run(file, '--out', copy);
    assert.equal(r0.status, 0, r0.stderr);
    assert.equal(fs.readFileSync(file, 'utf8'), stripped, '--out leaves the original alone');
    const anchored = fs.readFileSync(copy, 'utf8');
    assert.notEqual(anchored, stripped);
    assert.equal(anchored, JSON.stringify(l, null, 1), 'the anchors are the generator anchors');
    assert.equal(run(file).status, 0);
    const first = fs.readFileSync(file);
    assert.equal(run(file).status, 0);
    assert.ok(first.equals(fs.readFileSync(file)), 'run twice = same bytes');
    assert.equal(first.toString('utf8'), anchored);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
