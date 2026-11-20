// Training Ground / level generator v3 - «другой план» (S2_ROOMS_PLAN.md S2.7: buildings/replan.ts, storyPass.ts replanBuildingStory, generate.ts planSeedOf).
// Proofs on the Depot x1 (one planned office) and its x6 expansion (planned offices and blocks, rotated, with stories): (1) the plan seed carries the counter - the same
// (building, counter) gives the same plan, another counter another one, and counter 0 is the generator's own seed; (2) one re-plan changes ONLY that building (its
// walls, mantle edges, story ladders), every other building / wall / ladder / cover / story / rollback is byte-identical, the footprint and rotation stay, the new building
// passes the validator and is a band plan; (3) a building inside a locked district, a never-planned one and a storied one without its pristine copy are refused, the layout
// is not touched; (4) re-run determinism of the whole step, schema validity, undo through the page's history. `npm test`.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { applyStoriesToLayout, STORY_PICK_KNOBS } from '../buildingStories.ts';
import { validateBuilding } from '../buildingTraversal.ts';
import type { Draft } from '../generate.ts';
import { addBuilding, finalizeDraftToLayout } from '../generate.ts';
import { historyInit, historyPush, historyUndo } from '../regenHistory.ts';
import { addLock, shapePolygon } from '../regionRegen.ts';
import type { GenBuilding, GenLayout } from '../schema.ts';
import { DEFAULT_PARAMS, LEVEL_HEIGHT, validateGenLayout } from '../schema.ts';
import { isBuildingLocked, replanBuildingStory } from '../storyPass.ts';
import { footprintBBox, rotationOf } from '../rotation.ts';
import { isReplannable, replanBuilding, replanOnce, spliceBuilding } from './replan.ts';
import { byId, fixtures, planned } from './planFixtures.ts';
import { planSignature, replanCounter } from './planSummary.ts';

const json = (x: unknown): string => JSON.stringify(x);

/** Everything of the layout that is NOT building `id`'s: the other buildings, the other walls / mantle edges / ladders, covers, zones, routes, goals. */
function withoutBuilding(l: GenLayout, id: string, storyLadders: ReadonlySet<string> = new Set()): unknown {
  return {
    ...l,
    buildings: l.buildings.filter((b) => b.id !== id),
    walls: l.walls.filter((w) => w.building !== id),
    mantle_edges: (l.mantle_edges ?? []).filter((m) => m.building !== id),
    ladders: l.ladders.filter((k) => !storyLadders.has(k.id)),
  };
}

test('plan seed: the same (building, counter) gives the same plan, other counters other plans, and no counter is the generated plan', () => {
  const { s6 } = fixtures();
  const b = planned(s6.layout, 'block')[0], pristine = s6.base[b.id]?.building ?? b;
  const a1 = replanOnce(s6.layout, pristine, 3), a2 = replanOnce(s6.layout, pristine, 3);
  assert.equal(json(a1), json(a2), 'deterministic');
  assert.equal(replanCounter(a1.building), 3, 'the counter is recoverable from the seed');
  assert.notEqual(a1.building.plan?.seed, pristine.plan?.seed, 'the counter is in the seed');
  const sigs = new Set([1, 2, 3, 4, 5, 6].map((c) => planSignature(replanOnce(s6.layout, pristine, c).building)));
  assert.ok(sigs.size >= 2, `six counters give at least two distinct plans (got ${sigs.size})`);
  assert.equal(replanCounter(pristine), 0, 'the generated plan has no counter');
  // counter 0 = the generator's own seed: replanOnce(0) reproduces the SEED of the generated plan
  assert.equal(replanOnce(s6.layout, pristine, 0).building.plan?.seed, pristine.plan?.seed);
});

test('«другой план» on the x6 map: every planned building re-plans validly; only that building changes; footprint, rotation and entrance sides stay', () => {
  const { s6 } = fixtures();
  const all = planned(s6.layout);
  assert.ok(all.length >= 15 && all.some((b) => b.archetype === 'block') && all.some((b) => rotationOf(b) !== 0), 'a planned, rotated, mixed map');
  for (const b of all) {
    const r = replanBuildingStory(s6.layout, b.id, s6);
    assert.equal(r.ok, true, r.message);
    const out = r.layout!, nb = byId(out, b.id), pristine = s6.base[b.id]?.building ?? b;
    assert.equal(nb.plan?.kind, 'bands', `${b.id}: still a band plan`);
    assert.ok(replanCounter(nb) >= 1 && replanCounter(nb) === r.counter, `${b.id}: counter recorded in the seed`);
    assert.notEqual(planSignature(nb), planSignature(pristine), `${b.id}: a different plan`);
    assert.equal(validateBuilding(out, nb).issues.filter((i) => i.severity === 'error').length, 0, `${b.id}: no validator error`);
    // everything else byte-identical (the story ladders of the old story are the only other thing allowed to go)
    const gone = new Set(s6.base[b.id]?.ladders ?? []);
    assert.equal(json(withoutBuilding(out, b.id)), json(withoutBuilding(s6.layout, b.id, gone)), `${b.id}: the rest of the map is untouched`);
    // geometry anchors: footprint (<= 1 cm), rotation, storeys, the sides that carry ground doors
    const [[ax, ay], [bx, by]] = footprintBBox(b), [[cx, cy], [dx, dy]] = footprintBBox(nb);
    assert.ok(Math.max(Math.abs(ax - cx), Math.abs(ay - cy), Math.abs(bx - dx), Math.abs(by - dy)) <= 1, `${b.id}: footprint within 1 cm`);
    assert.equal(rotationOf(nb), rotationOf(b));
    for (const e of s6.log.filter((x) => x.building !== b.id)) assert.ok(r.log!.some((y) => json(y) === json(e)), `${b.id}: the others keep their rollback entries`);
    assert.equal(nb.floors, b.floors);
    const sides = (x: GenBuilding): string => [...new Set((x.doors ?? []).filter((d) => d.floor === 0).map((d) => d.wall))].sort().join(',');
    assert.equal(sides(nb), sides(pristine), `${b.id}: the same walls carry the ground doors`);
  }
});

test('a storied block: the old story (a fire escape + its ladder) is dropped with the old plan, the new pristine copy is the re-planned building', () => {
  // one planned block + a mandatory route, story 4 (rubble escape) forced on it; the state is what the page keeps next to the layout
  const d: Draft = { walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [], building_plans: true };
  let n = 4 * 7919;
  const random = (): number => { n = (n * 1103515245 + 12345) & 0x7fffffff; return n / 0x7fffffff; };
  addBuilding(d, 'B_t', [[20.37, 15.21], [38.77, 25.51]], { height: 2 * LEVEL_HEIGHT, floors: 2, enterable: true, roof_walkable: true, stairwell: true, set_piece: 'facade', archetype: 'block', doorSides: ['south'], windowSides: ['south', 'north'], partitionRooms: true }, random);
  const l0 = finalizeDraftToLayout(d, [90, 90], { name: 't', profile: 't', seed: 1, params: { ...DEFAULT_PARAMS, size_m: [90, 90], building_plans: true }, start: [4, 2], end: [36, 58] });
  const b0 = byId(l0, 'B_t'), c = [(b0.footprint[0][0] + b0.footprint[1][0]) / 2, (b0.footprint[0][1] + b0.footprint[1][1]) / 2];
  const layout: GenLayout = { ...l0, routes: [{ id: 'R', mandatory: true, points: [[c[0] - 3000, c[1] + 1500], [c[0] + 3000, c[1] + 1500]] } as never] };
  const roles = ['objective', 'garrison', 'overwatch', 'transit', 'background'] as const;
  const knobs = { ...STORY_PICK_KNOBS, budget: Object.fromEntries(roles.map((r) => [r, 1])) as never, weights: Object.fromEntries(roles.map((r) => [r, { rubble_escape: 1 }])) as never, t_max: 2 };
  const storied = applyStoriesToLayout(layout, 7, knobs).layout;
  const sb = byId(storied, 'B_t');
  assert.deepEqual(sb.stories?.map((x) => x.id), ['rubble_escape']);
  const before = new Set(layout.ladders.map((k) => k.id)), added = storied.ladders.filter((k) => !before.has(k.id)).map((k) => k.id);
  assert.ok(added.length > 0, 'the story added a ladder');
  const state = { base: { B_t: { building: byId(layout, 'B_t'), ladders: added } }, log: [] as never[] };
  const r = replanBuildingStory(storied, 'B_t', state);
  assert.equal(r.ok, true, r.message);
  const nb = byId(r.layout!, 'B_t');
  assert.equal(replanCounter(nb), r.counter);
  const ids = r.layout!.ladders.map((k) => k.id);
  assert.equal(new Set(ids).size, ids.length, 'no duplicate ladder ids');
  const fresh = r.base!.B_t;
  assert.ok(fresh, 'the new story pass keeps a pristine copy for the building');
  assert.equal(fresh.building.stories, undefined, 'the pristine copy has no story');
  assert.equal(planSignature(fresh.building), planSignature(nb), 'it is the re-planned building');
  assert.notEqual(planSignature(nb), planSignature(byId(layout, 'B_t')), 'and not the old plan');
  for (const k of r.layout!.ladders) if (added.includes(k.id)) assert.ok(fresh.ladders.includes(k.id), `${k.id}: an old story ladder stays only when the new story added it again`);
  assert.ok((nb.fire_escapes ?? []).length === 0 || (nb.stories ?? []).some((x) => x.id === 'rubble_escape'), 'a fire escape only with its story');
});

test('re-roll determinism: the same layout, building and state give the same result; the second «другой план» takes the next counter', () => {
  const { s6 } = fixtures();
  const b = planned(s6.layout, 'office')[0];
  const a = replanBuildingStory(s6.layout, b.id, s6), c = replanBuildingStory(s6.layout, b.id, s6);
  assert.equal(json(a.layout), json(c.layout));
  assert.equal(json(a.base), json(c.base));
  const again = replanBuildingStory(a.layout!, b.id, { base: a.base!, log: a.log! });
  assert.equal(again.ok, true, again.message);
  assert.ok(again.counter! > a.counter!, 'the counter only grows');
  assert.notEqual(planSignature(byId(again.layout!, b.id)), planSignature(byId(a.layout!, b.id)));
  assert.deepEqual(validateGenLayout(again.layout!), []);
});

test('the input layout is never mutated and undo returns it: the page history holds the old object', () => {
  const { s6 } = fixtures();
  const before = json(s6.layout), b = planned(s6.layout)[0];
  const r = replanBuildingStory(s6.layout, b.id, s6);
  assert.equal(r.ok, true);
  assert.equal(json(s6.layout), before, 'untouched');
  let h = historyInit(s6.layout, 'исходная');
  h = historyPush(h, r.layout!, `другой план ${b.id}`);
  assert.equal(h.present.state, r.layout);
  h = historyUndo(h);
  assert.equal(h.present.state, s6.layout, 'undo = the very same layout object');
});

test('locked buildings keep plan and stories: refused, nothing changes (locked district by centre)', () => {
  const { s6 } = fixtures();
  const b = planned(s6.layout)[0], [[x0, y0], [x1, y1]] = footprintBBox(b);
  const cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
  const locked = addLock(s6.layout, shapePolygon({ kind: 'rect', a: [cx - 300, cy - 300], b: [cx + 300, cy + 300] }), 'keep');
  assert.equal(isBuildingLocked(locked, b.id), true);
  const r = replanBuildingStory(locked, b.id, s6);
  assert.equal(r.ok, false);
  assert.match(r.message, /закреплённом/);
  assert.equal(r.layout, undefined);
  // another planned building outside the lock still re-plans, and the locked one is byte-identical in the result
  const other = planned(s6.layout).find((x) => !isBuildingLocked(locked, x.id) && x.id !== b.id)!;
  const ok = replanBuildingStory(locked, other.id, s6);
  assert.equal(ok.ok, true, ok.message);
  assert.equal(json(byId(ok.layout!, b.id)), json(byId(locked, b.id)), 'the locked building keeps plan and stories');
});

test('refusals: a never-planned building, a storied building without its pristine copy (a loaded map), a footprint no plan fits', () => {
  const { s1, s6 } = fixtures();
  const legacy = s1.layout.buildings.find((b) => !b.plan && b.enterable)!;
  assert.equal(isReplannable(legacy), false);
  assert.equal(replanBuildingStory(s1.layout, legacy.id, s1).ok, false);
  assert.equal(replanBuildingStory(s1.layout, 'B_nope', s1).ok, false);
  const storied = planned(s6.layout).find((b) => b.stories?.length)!;
  const noBase = replanBuildingStory(s6.layout, storied.id, { base: {}, log: [] });
  assert.equal(noBase.ok, false);
  assert.match(noBase.message, /нет исходного/);
  // a map without stories (the toggle was off): no state needed, the building is re-planned alone
  const bare = { ...s6.layout, buildings: s6.layout.buildings.map((b) => { const { stories: _s, key_route: _k, breach_route: _b, fast_entry: _f, quiet_entry: _q, ...rest } = b; return rest as GenBuilding; }) };
  const t = planned(bare, 'block')[0];
  const r = replanBuildingStory(bare, t.id);
  assert.equal(r.ok, true, r.message);
  assert.equal(r.layout!.buildings.some((b) => b.stories?.length), false, 'no stories invented');
  assert.equal(json(withoutBuilding(r.layout!, t.id)), json(withoutBuilding(bare, t.id)));
});

test('spliceBuilding swaps exactly the building, its walls and its mantle edges (same list position), keeps the order of the rest', () => {
  const { s6 } = fixtures();
  const b = planned(s6.layout, 'block')[0], pristine = s6.base[b.id]?.building ?? b;
  const piece = replanOnce(s6.layout, pristine, 2);
  const out = spliceBuilding(s6.layout, b.id, piece);
  assert.deepEqual(out.buildings.map((x) => x.id), s6.layout.buildings.map((x) => x.id), 'same order');
  assert.ok(out.walls.every((w) => w.building !== b.id || piece.walls.some((p) => p.id === w.id)));
  assert.equal(out.walls.filter((w) => w.building === b.id).length, piece.walls.length);
  assert.equal(json(withoutBuilding(out, b.id)), json(withoutBuilding(s6.layout, b.id)));
});

test('a drafted office / block pair on its own (no templates): re-plan works on a layout built straight from addBuilding', () => {
  const d: Draft = { walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [], building_plans: true };
  let n = 7919;
  const random = (): number => { n = (n * 1103515245 + 12345) & 0x7fffffff; return n / 0x7fffffff; };
  addBuilding(d, 'B_o', [[10.37, 10.21], [25.37, 21.21]], { height: 2 * LEVEL_HEIGHT, floors: 2, enterable: true, roof_walkable: false, stairwell: true, set_piece: 'facade', archetype: 'office', doorSides: ['south'], windowSides: ['south', 'north'], partitionRooms: true }, random);
  addBuilding(d, 'B_b', [[40.37, 10.21], [60.37, 24.21]], { height: 2 * LEVEL_HEIGHT, floors: 2, enterable: true, roof_walkable: true, stairwell: true, set_piece: 'facade', archetype: 'block', doorSides: ['south'], windowSides: ['south', 'north'], partitionRooms: true }, random);
  const l = finalizeDraftToLayout(d, [90, 90], { name: 't', profile: 't', seed: 1, params: { ...DEFAULT_PARAMS, size_m: [90, 90], building_plans: true }, start: [4, 2], end: [36, 58] });
  for (const id of ['B_o', 'B_b']) {
    const r = replanBuilding(l, byId(l, id));
    assert.equal(r.ok, true, r.message);
    const out = spliceBuilding(l, id, r.piece!);
    assert.equal(json(withoutBuilding(out, id)), json(withoutBuilding(l, id)));
    assert.equal(validateBuilding(out, byId(out, id)).issues.filter((i) => i.severity === 'error').length, 0);
  }
});
