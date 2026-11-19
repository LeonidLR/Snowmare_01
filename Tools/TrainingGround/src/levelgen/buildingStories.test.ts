// Building stories engine (S1 step 3a, building-stories.md 3 / 5 / 6 / F1) + the validator's opening states and window entries.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import {
  applyDoorState,
  applyRecipe,
  applyStoriesToLayout,
  applyStory,
  applyWindowState,
  frontDoor,
  pickStory,
  STORY_PICK_KNOBS,
  storyEligible,
} from './buildingStories.ts';
import { computeBuildingRoutes } from './buildingRoutes.ts';
import { validateBuilding } from './buildingTraversal.ts';
import type { GenBuilding, GenLayout, WallOpening } from './schema.ts';
import { LEVELGEN_SCHEMA, LEVELGEN_SCHEMA_V2 } from './schema.ts';

/** A 10 x 8 m one-storey shed: doors south (route side) and north, a ground window west, an upper-free roof. */
function shed(id: string, openings: WallOpening[], x = 0): GenBuilding {
  return {
    id,
    set_piece: 'block',
    footprint: [[x, 0], [x + 1000, 800]],
    floors: 1,
    height: 300,
    enterable: true,
    roof_walkable: false,
    openings,
  } as unknown as GenBuilding;
}

const door = (id: string, pos: [number, number], state?: WallOpening['state']): WallOpening => ({ id, kind: 'door', pos, floor: 0, ...(state ? { state } : {}) });
const win = (id: string, pos: [number, number], state?: WallOpening['state'], floor = 0): WallOpening => ({ id, kind: 'window', pos, floor, ...(state ? { state } : {}) });

function layoutOf(buildings: GenBuilding[], schema = LEVELGEN_SCHEMA_V2): GenLayout {
  return {
    schema,
    walls: [],
    ladders: [],
    buildings,
    // the mandatory route runs south of the buildings (y = 1500)
    routes: [{ id: 'R_main', mandatory: true, points: [[-500, 1500], [3000, 1500]] }],
  } as unknown as GenLayout;
}

const twoDoors = () => shed('B1', [door('D_n', [500, 0]), door('D_s', [500, 800]), win('W_w', [0, 400])]);

test('door_state / window_state bricks set the states on the building', () => {
  const b = twoDoors();
  assert.equal(applyDoorState(b, 'D_s', 'locked'), true);
  assert.equal(b.openings!.find((o) => o.id === 'D_s')!.state, 'locked');
  assert.equal(applyDoorState(b, 'W_w', 'locked'), false, 'a window is not a door');
  assert.equal(applyDoorState(b, 'nope', 'locked'), false);
  assert.equal(applyWindowState({ walls: [] }, b, 'west', 'barred'), 1);
  assert.equal(b.openings!.find((o) => o.id === 'W_w')!.state, 'barred');
  assert.equal(applyWindowState({ walls: [] }, b, 'east', 'barred'), 0, 'no window on the east facade');
  assert.equal(applyWindowState({ walls: [] }, b, 0, 'broken'), 1, 'by storey');
});

test('a failed recipe rolls back: the original building is returned byte-equal, story_rolled_back is a warning', () => {
  const b = twoDoors();
  const l = layoutOf([b]);
  const json = JSON.stringify(b);
  // seal every entry: the shed becomes unreachable -> new errors -> rollback
  const res = applyRecipe(l, b, 'locked_door', [
    { kind: 'door_state', door: 'D_n', state: 'sealed' },
    { kind: 'door_state', door: 'D_s', state: 'sealed' },
    { kind: 'window_state', target: 'west', state: 'barred' },
  ], 7);
  assert.equal(res.ok, false);
  assert.equal(res.building, b, 'the original object');
  assert.equal(JSON.stringify(res.building), json, 'byte-equal JSON');
  assert.equal(JSON.stringify(b), json, 'the input was not mutated');
  assert.equal(res.issues.length, 1);
  assert.equal(res.issues[0].code, 'story_rolled_back');
  assert.equal(res.issues[0].severity, 'warning');
  // an ineligible story rolls back the same way
  const one = shed('B2', [door('D_s', [500, 800])]);
  const json1 = JSON.stringify(one);
  const r1 = applyStory(layoutOf([one]), one, 'locked_door', 3);
  assert.equal(r1.ok, false);
  assert.equal(JSON.stringify(r1.building), json1);
});

test('story 3 locked_door needs >= 2 passable exterior doors', () => {
  const one = shed('B1', [door('D_s', [500, 800]), win('W_w', [0, 400])]);
  const l1 = layoutOf([one]);
  assert.equal(storyEligible(l1, one, 'locked_door'), false);
  assert.equal(pickStory(one, 'objective', 1, STORY_PICK_KNOBS, { layout: l1 }), 'open', 'nothing eligible -> open');
  const sealedBack = shed('B1', [door('D_n', [500, 0], 'sealed'), door('D_s', [500, 800])]);
  assert.equal(storyEligible(layoutOf([sealedBack]), sealedBack, 'locked_door'), false, 'a sealed back door is no alternative');
  const two = twoDoors();
  assert.equal(storyEligible(layoutOf([two]), two, 'locked_door'), true);
});

test('story 3 boards the front facade ground windows (F4 rule A, designer 2026-11-19): fast = the kick, not a front window', () => {
  // south (route side) door + two south ground windows + a south storey-1-free shed; north back door; a west side window
  const b = shed('B1', [door('D_n', [500, 0]), door('D_s', [500, 800]), win('W_s1', [200, 800]), win('W_s2', [800, 800]), win('W_w', [0, 400])]);
  const l = layoutOf([b]);
  const res = applyStory(l, b, 'locked_door', 11);
  assert.equal(res.ok, true, JSON.stringify(res.issues));
  assert.deepEqual(res.building.stories!.map((s) => s.bricks), [['door_state(D_s,locked)', 'window_state(south:0,boarded)']]);
  const st = (id: string) => res.building.openings!.find((o) => o.id === id)!.state;
  assert.equal(st('W_s1'), 'boarded');
  assert.equal(st('W_s2'), 'boarded');
  assert.equal(st('W_w'), undefined, 'a side window keeps its default');
  const r = computeBuildingRoutes({ ...l, buildings: [res.building] }, res.building);
  assert.ok(r.fast_entry !== undefined && !['W_s1', 'W_s2'].includes(r.fast_entry), `fast by ${r.fast_entry}`);
  assert.equal(r.choice, true, `fast ${r.fast_entry} / quiet ${r.quiet_entry}`);
  // no window on the front facade -> the door brick alone (twoDoors: the window is west)
  const plain = applyStory(layoutOf([twoDoors()]), twoDoors(), 'locked_door', 11);
  assert.deepEqual(plain.building.stories!.map((s) => s.bricks), [['door_state(D_s,locked)']]);
});

test('the front door faces the mandatory route; a locked front keeps the building reachable', () => {
  const b = twoDoors();
  const l = layoutOf([b]);
  assert.equal(frontDoor(l, b)!.ref, 'D_s', 'the south door faces the route at y = 1500');
  assert.equal(frontDoor({ ...l, routes: [] }, b)!.ref, 'D_n', 'no route -> the first exterior door');
  const res = applyStory(l, b, 'locked_door', 11);
  assert.equal(res.ok, true);
  assert.equal(res.building.openings!.find((o) => o.id === 'D_s')!.state, 'locked');
  assert.deepEqual(res.building.stories!.map((s) => [s.id, s.bricks]), [['locked_door', ['door_state(D_s,locked)']]]);
  assert.ok(['pmc', 'decay', 'survivors'].includes(res.building.stories![0].author));
  assert.equal(res.building.stories![0].outcome, undefined);
  const v = validateBuilding({ ...l, buildings: [res.building] }, res.building);
  assert.equal(v.ok, true, JSON.stringify(v.issues));
  // even a single locked door is an entry (a kick is not an explosive)
  const lone = shed('B3', [door('D_s', [500, 800], 'locked')]);
  assert.equal(validateBuilding(layoutOf([lone]), lone).ok, true);
});

test('a barricaded only entry fails with unreachable_without_explosive; a sealed one with no_exterior_door', () => {
  const bar = shed('B1', [door('D_s', [500, 800], 'barricaded'), win('W_w', [0, 400], 'barred')]);
  const vb = validateBuilding(layoutOf([bar]), bar);
  assert.equal(vb.ok, false);
  assert.ok(vb.issues.some((i) => i.code === 'unreachable_without_explosive' && i.severity === 'error'));
  assert.ok(!vb.issues.some((i) => i.code === 'no_exterior_door'));
  const sealed = shed('B2', [door('D_s', [500, 800], 'sealed')]);
  const vs = validateBuilding(layoutOf([sealed]), sealed);
  assert.ok(vs.issues.some((i) => i.code === 'no_exterior_door'));
  assert.ok(!vs.issues.some((i) => i.code === 'unreachable_without_explosive'));
});

test('a vaultable ground window is an entry (glazed / broken); upper-storey and barred windows are not', () => {
  const viaWindow = shed('B1', [door('D_s', [500, 800], 'sealed'), win('W_w', [0, 400])]);
  assert.equal(validateBuilding(layoutOf([viaWindow]), viaWindow).ok, true, '/2 default glazed: vault + shatter');
  const barred = shed('B2', [door('D_s', [500, 800], 'sealed'), win('W_w', [0, 400], 'barred')]);
  assert.equal(validateBuilding(layoutOf([barred]), barred).ok, false);
  const high = shed('B3', [door('D_s', [500, 800], 'sealed'), { ...win('W_w', [0, 400]), sill_height: 180 }]);
  assert.equal(validateBuilding(layoutOf([high]), high).ok, false, 'a 180 cm sill is not vaultable');
});

test('/1 layouts: defaults are passable (doors open, windows broken)', () => {
  const b = twoDoors();
  const v1 = validateBuilding(layoutOf([b], LEVELGEN_SCHEMA), b);
  assert.equal(v1.ok, true);
  assert.deepEqual(v1.levels.reachable, [0]);
});

test('applyStoriesToLayout: deterministic per (seed, building id), independent of the building order, input untouched', () => {
  const bs = [0, 1, 2, 3, 4, 5].map((k) => shed(`B${k}`, [door(`D${k}n`, [k * 1500 + 500, 0]), door(`D${k}s`, [k * 1500 + 500, 800])], k * 1500));
  const l = layoutOf(bs);
  const json = JSON.stringify(l);
  const a = applyStoriesToLayout(l, 42), b = applyStoriesToLayout(l, 42);
  assert.equal(JSON.stringify(a), JSON.stringify(b));
  assert.equal(JSON.stringify(l), json, 'the input layout is not mutated');
  const rev = applyStoriesToLayout({ ...l, buildings: [...bs].reverse() }, 42);
  const byId = (x: GenLayout) => Object.fromEntries(x.buildings.map((q) => [q.id, q.stories]));
  assert.deepEqual(byId(rev.layout), byId(a.layout));
  for (const q of a.layout.buildings) {
    assert.ok(q.role, `${q.id} has a role`);
    assert.equal(q.stories?.length, 1, `${q.id} has one story`);
    assert.equal(validateBuilding(a.layout, q).ok, true);
  }
  // all transit (on the route): budget 0.5 -> some locked, some open over the 6 buildings
  const ids = a.layout.buildings.map((q) => q.stories![0].id);
  assert.ok(ids.every((s) => s === 'open' || s === 'locked_door'));
  const other = applyStoriesToLayout(l, 43);
  assert.ok(other.layout.buildings.every((q) => q.stories?.length === 1));
});

test('pickStory follows the role budget (objective always storied when eligible, background rarely)', () => {
  const b = twoDoors();
  const l = layoutOf([b]);
  let obj = 0, bg = 0;
  for (let s = 0; s < 200; s++) {
    if (pickStory(b, 'objective', s, STORY_PICK_KNOBS, { layout: l }) !== 'open') obj++;
    if (pickStory(b, 'background', s, STORY_PICK_KNOBS, { layout: l }) !== 'open') bg++;
  }
  assert.equal(obj, 200);
  assert.ok(bg > 5 && bg < 45, `background storied ${bg} / 200 (budget 0.1)`);
  assert.equal(pickStory(b, 'objective', 1, STORY_PICK_KNOBS, { layout: l, exclude: ['locked_door'] }), 'open', 'excluded -> open');
});
