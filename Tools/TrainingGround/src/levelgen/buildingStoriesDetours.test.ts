// Building stories S1 step 3b: bricks block_flight / add_balcony_escape / roof_access, detour stories 4 (rubble_escape) and
// 7 (roof_outside), and the validator's blocked flights + fire escapes (building-stories.md 3 / 6 rows 4, 7 / Edge Cases;
// BUILDINGS_TECH_SPEC.md 2.1).
import assert from 'node:assert/strict';
import { test } from 'node:test';
import {
  applyBalconyEscape,
  applyBlockFlight,
  applyRoofAccess,
  applyStoriesToLayout,
  applyStory,
  pickStory,
  STORY_PICK_KNOBS,
  storyEligible,
} from './buildingStories.ts';
import { validateBuilding } from './buildingTraversal.ts';
import { checkBuildingReachability } from './tacticalEvaluate.ts';
import type { GenBuilding, GenLadder, GenLayout, GenWall } from './schema.ts';
import { LEVELGEN_SCHEMA_V2, ladderForLevels } from './schema.ts';

/**
 * A 10 x 8 m two-storey block (set_piece 'facade' = the generator's block / office): doors south (route side) and north, one room
 * per storey, a core flight 0 -> 1 and a flight 1 -> roof, walkable roof.
 */
function block(id: string, x = 0, extra: Partial<GenBuilding> = {}): GenBuilding {
  return {
    id,
    set_piece: 'facade',
    footprint: [[x, 0], [x + 1000, 800]],
    floors: 2,
    height: 600,
    enterable: true,
    roof_walkable: true,
    doors: [],
    openings: [
      { id: `${id}_D_s`, kind: 'door', pos: [x + 500, 800], floor: 0 },
      { id: `${id}_D_n`, kind: 'door', pos: [x + 500, 0], floor: 0 },
    ],
    rooms: [
      { id: `${id}_r_f0_0`, floor: 0, bounds: { min: [x, 0], max: [x + 1000, 800] }, kind: 'room', door_ids: [`${id}_D_s`, `${id}_D_n`] },
      { id: `${id}_r_f1_0`, floor: 1, bounds: { min: [x, 0], max: [x + 1000, 800] }, kind: 'room' },
    ],
    stairs: [
      { id: `${id}_stair_0`, pos: [x + 200, 160], from: 0, to: 1, kind: 'stairwell', top: [x + 200, 590] },
      { id: `${id}_stair_1`, pos: [x + 360, 590], from: 1, to: 2, kind: 'stairwell', top: [x + 360, 160] },
    ],
    roof_holes: [],
    floor_holes: [],
    ...extra,
  } as GenBuilding;
}

function layoutOf(buildings: GenBuilding[], walls: GenWall[] = [], ladders: GenLadder[] = []): GenLayout {
  return {
    schema: LEVELGEN_SCHEMA_V2,
    walls,
    ladders,
    buildings,
    routes: [{ id: 'R_main', mandatory: true, points: [[-500, 1500], [9000, 1500]] }],
  } as unknown as GenLayout;
}

/** A fence ring `gap` cm around the footprint: no outside spot on any facade. */
const fenceRing = (x0: number, y0: number, x1: number, y1: number, gap: number): GenWall[] => {
  const a: [number, number][] = [[x0 - gap, y0 - gap], [x1 + gap, y0 - gap], [x1 + gap, y1 + gap], [x0 - gap, y1 + gap]];
  return a.map((p, i) => ({ id: `F_${i}`, a: p, b: a[(i + 1) % 4], height: 200, kind: 'fence' }) as unknown as GenWall);
};

test('the base block is valid', () => {
  const b = block('B');
  const v = validateBuilding(layoutOf([b]), b);
  assert.equal(v.ok, true, JSON.stringify(v.issues));
  assert.equal(v.roof, 'reachable');
});

test('block_flight: the flight stays but gives no edge; unknown / already blocked flights do not apply', () => {
  const b = block('B');
  assert.equal(applyBlockFlight(b, 'B_stair_0', 'rubble'), true);
  assert.equal(b.stairs[0].blocked, 'rubble');
  assert.equal(b.stairs.length, 2, 'geometry kept');
  assert.equal(applyBlockFlight(b, 'B_stair_0', 'burnt'), false, 'already blocked');
  assert.equal(applyBlockFlight(b, 'nope', 'rubble'), false);
  const v = validateBuilding(layoutOf([b]), b);
  assert.ok(v.issues.some((i) => i.code === 'unreachable_room' && i.ref === 'B_r_f1_0'));
  assert.ok(v.issues.some((i) => i.code === 'unreachable_level'));
  assert.ok(v.issues.some((i) => i.code === 'unreachable_roof'));
});

test('add_balcony_escape: balcony door + 150-deep balcony + ladder escape on a facade without doors; storey 1 reachable', () => {
  const b = block('B');
  const l = layoutOf([b]);
  applyBlockFlight(b, 'B_stair_0', 'rubble');
  assert.equal(applyBalconyEscape(l, b, 'B_r_f1_0', [1]), true);
  const esc = b.fire_escapes![0];
  assert.equal(esc.kind, 'ladder');
  assert.equal(esc.side, 'east', 'no door on east / west; east first');
  assert.equal(esc.to_roof, false);
  const bal = b.balconies![0];
  assert.equal(bal.floor, 1);
  assert.equal(bal.rect[1][0] - bal.rect[0][0], 150, 'balcony depth 150 at an escape stop');
  assert.equal(bal.rail_cm, 95);
  assert.deepEqual(esc.stops, [{ floor: 1, balcony_id: bal.id }]);
  const door = b.openings!.find((o) => o.id === bal.door_id)!;
  assert.equal(door.kind, 'door');
  assert.equal(door.floor, 1);
  assert.ok(b.rooms![1].door_ids!.includes(bal.door_id));
  const lad = l.ladders!.find((x) => x.id === esc.ladder_ids![0])!;
  assert.equal(lad.rungs, ladderForLevels(1).rungs);
  assert.equal(lad.plinth_cm, ladderForLevels(1).plinth_cm);
  assert.deepEqual([lad.from, lad.to, lad.level_from, lad.level_to], ['ground', 'floor:B:1', 0, 1]);
  assert.ok(esc.ground[0] > 1000, 'ground stop outside');
  const v = validateBuilding(l, b);
  assert.equal(v.ok, true, JSON.stringify(v.issues));
  assert.deepEqual(v.levels.reachable, [0, 1]);
  // a sealed balcony door cuts the room off again; a missing escape ladder removes the escape
  const sealed = structuredClone(b);
  sealed.openings!.find((o) => o.id === bal.door_id)!.state = 'sealed';
  assert.ok(checkBuildingReachability(l, sealed, { openingStates: true }).unreachableRooms.includes('B_r_f1_0'));
  const noLadder = { ...l, ladders: [] };
  assert.equal(validateBuilding(noLadder, b).ok, false);
});

test('a down_only escape only goes down: no reachability up', () => {
  const b = block('B');
  const l = layoutOf([b]);
  applyBlockFlight(b, 'B_stair_0', 'rubble');
  applyBalconyEscape(l, b, 'B_r_f1_0', [1]);
  b.fire_escapes![0].down_only = true;
  const v = validateBuilding(l, b);
  assert.ok(v.issues.some((i) => i.code === 'unreachable_room' && i.ref === 'B_r_f1_0'));
  assert.ok(v.issues.some((i) => i.code === 'unreachable_level'));
});

test('add_balcony_escape fails (nothing changed) when no facade spot is free', () => {
  const b = block('B');
  const l = layoutOf([b], fenceRing(0, 0, 1000, 800, 100));
  const json = JSON.stringify(b);
  assert.equal(applyBalconyEscape(l, b, 'B_r_f1_0', [1]), false);
  assert.equal(JSON.stringify(b), json);
  assert.equal(l.ladders!.length, 0);
  assert.equal(applyBalconyEscape(layoutOf([b]), b, 'B_r_f0_0', [1]), false, 'the room must be on the lowest stop storey');
  assert.equal(applyBalconyEscape(layoutOf([b]), b, 'B_r_f1_0', [2]), false, 'no escape stop on the roof level');
});

test('roof_access: removes the core flight onto the roof and the hatch, adds a facade ladder ground -> roof', () => {
  const hatchLadder: GenLadder = { id: 'LAD_B_hatch', pos: [800, 700], yaw: 90, rungs: 8, plinth_cm: 8.8, from: 'floor:B:1', to: 'roof:B', level_from: 1, level_to: 2, foot: [800, 710], top: [800, 600] };
  const b = block('B', 0, { hatches: [{ id: 'B_roof_hatch', pos: [800, 620], floor: 1, size: [100, 100], ladder_id: 'LAD_B_hatch' }] });
  const l = layoutOf([b], [], [hatchLadder]);
  assert.equal(applyRoofAccess(l, b, 'ladder'), true);
  assert.equal(b.stairs.length, 1);
  assert.ok(!b.stairs.some((s) => s.to >= b.floors), 'no core-to-roof flight');
  assert.deepEqual(b.hatches, []);
  assert.deepEqual(l.ladders!.map((x) => x.id), ['LAD_B_roof']);
  const lad = l.ladders![0];
  assert.deepEqual([lad.from, lad.to, lad.rungs, lad.plinth_cm], ['ground', 'roof:B', ladderForLevels(2).rungs, ladderForLevels(2).plinth_cm]);
  assert.ok(lad.foot[0] > 1000 && lad.yaw === 0, 'outside the east facade, facing out');
  assert.equal(validateBuilding(l, b).ok, true);
  assert.equal(applyRoofAccess(layoutOf([b]), b, 'ladder'), false, 'no inside access left');
});

test('story 7 roof_outside: roof reachable via the facade ladder, no core-to-roof edge; rollback when no ladder spot', () => {
  const b = block('B');
  const l = layoutOf([b]);
  assert.equal(storyEligible(l, b, 'roof_outside'), true);
  const res = applyStory(l, b, 'roof_outside', 5);
  assert.equal(res.ok, true, JSON.stringify(res.issues));
  assert.deepEqual(res.instance.bricks, ['roof_access(ladder)']);
  assert.ok(['decay', 'cold'].includes(res.instance.author));
  const l2 = { ...l, buildings: [res.building], ladders: res.ladders! };
  const v = validateBuilding(l2, res.building);
  assert.equal(v.ok, true, JSON.stringify(v.issues));
  assert.equal(v.roof, 'reachable');
  // without the facade ladder the roof is unreachable: no inside edge is left
  const v0 = validateBuilding({ ...l2, ladders: [] }, res.building);
  assert.ok(v0.issues.some((i) => i.code === 'unreachable_roof'));
  // a fence ring 100 cm out: the ladder's foot zone does not fit -> rollback, byte-equal
  const fenced = layoutOf([b], fenceRing(0, 0, 1000, 800, 100));
  const json = JSON.stringify(b);
  const rb = applyStory(fenced, b, 'roof_outside', 5);
  assert.equal(rb.ok, false);
  assert.equal(rb.building, b);
  assert.equal(JSON.stringify(rb.building), json);
  assert.equal(rb.ladders, undefined);
  // not eligible: no walkable roof / no inside access
  const flat = block('F', 0, { roof_walkable: false });
  assert.equal(storyEligible(layoutOf([flat]), flat, 'roof_outside'), false);
});

test('story 4 rubble_escape: flight 0->1 blocked, storey 1 reachable via the escape; rollback byte-equal without a spot', () => {
  const b = block('B');
  const l = layoutOf([b]);
  assert.equal(storyEligible(l, b, 'rubble_escape'), true);
  const res = applyStory(l, b, 'rubble_escape', 9);
  assert.equal(res.ok, true, JSON.stringify(res.issues));
  assert.deepEqual(res.instance.bricks, ['block_flight(B_stair_0,rubble)', 'add_balcony_escape(B_r_f1_0,1)']);
  assert.ok(['decay', 'pmc'].includes(res.instance.author));
  assert.equal(res.building.stairs.find((s) => s.id === 'B_stair_0')!.blocked, 'rubble');
  const l2 = { ...l, buildings: [res.building], ladders: res.ladders! };
  const v = validateBuilding(l2, res.building);
  assert.equal(v.ok, true, JSON.stringify(v.issues));
  assert.deepEqual(v.levels.reachable, [0, 1]);
  assert.equal(v.roof, 'reachable', 'the roof via flight 1 -> 2 from storey 1');
  // the escape cannot be placed -> rollback (storey 1 would be unreachable), the original object
  const fenced = layoutOf([b], fenceRing(0, 0, 1000, 800, 100));
  const json = JSON.stringify(b);
  const rb = applyStory(fenced, b, 'rubble_escape', 9);
  assert.equal(rb.ok, false);
  assert.equal(rb.building, b);
  assert.equal(JSON.stringify(rb.building), json);
  assert.equal(rb.issues[0].code, 'story_rolled_back');
  // archetype / storeys
  const one = block('S', 0, { floors: 1, height: 300, roof_walkable: false, stairs: [] });
  assert.equal(storyEligible(layoutOf([one]), one, 'rubble_escape'), false, '1 storey');
  const hall = block('H', 0, { set_piece: 'warehouse' });
  assert.equal(storyEligible(layoutOf([hall]), hall, 'rubble_escape'), false, 'not a block / office');
});

test('detour stories are never picked for transit / background, but are for objective / garrison / overwatch', () => {
  const b = block('B');
  const l = layoutOf([b]);
  const knobs = {
    ...STORY_PICK_KNOBS,
    budget: { objective: 1, garrison: 1, overwatch: 1, transit: 1, background: 1 },
    weights: { ...STORY_PICK_KNOBS.weights, transit: { rubble_escape: 1, roof_outside: 1 }, background: { rubble_escape: 1, roof_outside: 1 } },
  };
  const seen = new Set<string>();
  for (let s = 0; s < 100; s++) {
    assert.equal(pickStory(b, 'transit', s, knobs, { layout: l }), 'open');
    assert.equal(pickStory(b, 'background', s, knobs, { layout: l }), 'open');
    seen.add(pickStory(b, 'objective', s, knobs, { layout: l }));
  }
  assert.ok(seen.has('rubble_escape') && seen.has('roof_outside') && seen.has('locked_door'), [...seen].join());
  assert.equal(storyEligible(l, b, 'roof_outside', 'transit'), false);
  assert.equal(storyEligible(l, b, 'roof_outside', 'overwatch'), true);
});

test('applyStoriesToLayout with detours: deterministic, every building valid, ladders carried into the layout', () => {
  const bs = [0, 1, 2, 3].map((k) => block(`B${k}`, k * 2000));
  const l = layoutOf(bs);
  const json = JSON.stringify(l);
  let detours = 0;
  for (const seed of [1, 2, 3, 4, 5, 6, 7, 8]) {
    const a = applyStoriesToLayout(l, seed), c = applyStoriesToLayout(l, seed);
    assert.equal(JSON.stringify(a), JSON.stringify(c));
    for (const q of a.layout.buildings) {
      const v = validateBuilding(a.layout, q);
      assert.equal(v.ok, true, `${seed} ${q.id} ${q.stories?.[0].id}: ${JSON.stringify(v.issues)}`);
      const st = q.stories![0].id;
      if (st === 'rubble_escape' || st === 'roof_outside') {
        detours++;
        assert.ok(['objective', 'garrison', 'overwatch'].includes(q.role!), `${q.id} ${q.role}`);
      }
      for (const e of q.fire_escapes ?? []) for (const id of e.ladder_ids ?? []) assert.ok(a.layout.ladders!.some((x) => x.id === id));
    }
  }
  assert.ok(detours > 0, 'some detour stories were applied');
  assert.equal(JSON.stringify(l), json, 'input untouched');
});
