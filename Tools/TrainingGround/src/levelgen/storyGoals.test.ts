// Building stories after the mission goals (designer decision 2026-11-19): (a) restoryForGoals - the story pass re-run when
// `layout.objectives` changes (role-changed buildings re-picked from the pass seed's sub-seeds, locked districts and unchanged roles
// keep their stories); (b) story 9 'broken_back_windows' - window_state(back:0, broken), F4 quiet by a back window / fast by the
// front facade (door or a shattered glazed window), else rolled back byte for byte. `npm test`. Inputs: the Depot reference profile (pure, nothing written).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { computeBuildingRoutes } from './buildingRoutes.ts';
import {
  applyDoorState, applyStory, backWindows, frontDoorAnyState, openingRef, openingSide, passableExteriorDoors, pickSecondStory, STORY_COMPATIBLE,
  STORY_PICK_KNOBS, storyEligible, storyRecipe, storySubSeed,
} from './buildingStories.ts';
import { generateLayout } from './generate.ts';
import { placeObjectives } from './objectives.ts';
import { effectiveOpeningState } from './openingStates.ts';
import type { Profile } from './profile.ts';
import { addLock, shapePolygon } from './regionRegen.ts';
import type { GenBuilding, GenLayout } from './schema.ts';
import { LEVELGEN_SCHEMA_V2, unifyOpenings } from './schema.ts';
import type { StoryPassResult } from './storyPass.ts';
import { generateCandidate, hasStories, lockedBuildingIds, restoryForGoals } from './storyPass.ts';

const TG = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const profile = JSON.parse(fs.readFileSync(path.join(TG, '../../Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
const json = (l: unknown) => JSON.stringify(l, null, 1);
const byId = (l: GenLayout, id: string) => l.buildings.find((b) => b.id === id)!;
const withoutRole = ({ role: _r, ...rest }: GenBuilding) => rest;
const subSeeds = (seed: number, id: string) => Array.from({ length: STORY_PICK_KNOBS.t_max }, (_, t) => storySubSeed(seed, id, t));

/** Depot seed 1 with stories, then the page's «Расставить старт и цели» (goal seed 1, auto count, start at the spawn). */
function withGoals() {
  const s = generateCandidate({ profile, seed: 1, stories: true }) as StoryPassResult;
  const g = placeObjectives({ layout: s.layout, seed: 1 });
  assert.equal(g.ok, true, g.message);
  return { s, goals: g.layout };
}

test('goals change: role-changed buildings get their story re-picked from the pass seed; the rest keep theirs byte for byte', () => {
  const { s, goals } = withGoals();
  const r = restoryForGoals(goals, goals.seed, s);
  assert.ok(r.repicked.length > 0, 'the goals give some buildings a new role (objective / garrison)');
  assert.deepEqual(r.keptChanged, []);
  for (const b of goals.buildings.filter((x) => x.enterable && x.stories?.length)) {
    const after = byId(r.layout, b.id);
    if (r.repicked.includes(b.id)) {
      assert.notEqual(after.role, b.role, `${b.id}: re-picked only when its role changed`);
      assert.equal(after.stories?.length, 1, b.id);
      assert.ok(subSeeds(goals.seed, b.id).includes(after.stories![0].seed), `${b.id}: sub-seed of the pass seed`);
      assert.ok(r.base[b.id], `${b.id}: a pristine copy for «другая история»`);
    } else {
      assert.equal(after.role, b.role, `${b.id}: an unchanged role`);
      assert.equal(json(after), json(b), `${b.id}: kept byte for byte`);
    }
  }
  assert.equal(json(r.layout.objectives), json(goals.objectives), 'the goals themselves are untouched');
  // the same goals again: nothing changes role -> the input layout itself (no new undo noise, nav reused)
  const again = restoryForGoals(r.layout, r.layout.seed, r);
  assert.equal(again.layout, r.layout);
  assert.deepEqual(again.repicked, []);
  // clearing the goals turns the objective / garrison buildings back and re-picks them again
  const { objectives: _o, ...cleared } = r.layout;
  const back = restoryForGoals(cleared as GenLayout, r.layout.seed, r);
  assert.ok(back.repicked.length > 0);
  assert.equal(back.layout.buildings.some((b) => b.role === 'objective'), false);
});

test('goals change: a building inside a locked district keeps its story even when its role changes', () => {
  const { s, goals } = withGoals();
  const id = restoryForGoals(goals, goals.seed, s).repicked[0];
  const b = byId(goals, id);
  const [[x0, y0], [x1, y1]] = b.footprint;
  const [cx, cy] = [(x0 + x1) / 2, (y0 + y1) / 2];
  const locked = addLock(goals, shapePolygon({ kind: 'rect', a: [cx - 100, cy - 100], b: [cx + 100, cy + 100] }), 'keep');
  assert.ok(lockedBuildingIds(locked).has(id));
  const r = restoryForGoals(locked, locked.seed, s);
  assert.ok(r.keptChanged.includes(id));
  assert.equal(r.repicked.includes(id), false);
  assert.deepEqual(byId(r.layout, id).stories, b.stories, 'the locked building keeps its story');
  assert.notEqual(byId(r.layout, id).role, b.role, 'its role still follows the goals');
});

test('goals re-run: deterministic run to run; a toggle-off map has no stories (the page leaves it /1)', () => {
  const a = withGoals(), b = withGoals();
  assert.equal(json(restoryForGoals(a.goals, a.goals.seed, a.s).layout), json(restoryForGoals(b.goals, b.goals.seed, b.s).layout));
  assert.equal(hasStories(generateCandidate({ profile, seed: 1, stories: false }).layout), false);
  assert.equal(hasStories(a.s.layout), true);
});

// ---- story 9 ------------------------------------------------------------------------------------------------------------------------

/** The Depot seed 1 as schema /2 (the opening-state defaults: doors closed, windows glazed), no stories. */
const depotV2 = (): GenLayout => ({ ...generateLayout({ profile, seed: 1 }), schema: LEVELGEN_SCHEMA_V2 });

/** `b` with its front door (frontDoorAnyState) set to `state`, in a copy of `l`. */
function withFront(l: GenLayout, b: GenBuilding, state: 'locked' | 'sealed' | 'barricaded') {
  const c: GenBuilding = structuredClone(b);
  assert.ok(applyDoorState(c, frontDoorAnyState(l, b)!.ref, state), b.id);
  return { l: { ...l, buildings: l.buildings.map((x) => (x.id === b.id ? c : x)) } as GenLayout, b: c };
}

test('story 9 eligibility (designer 2026-11-19): only with a loud or absent front; the facade opposite the front, its ground windows only', () => {
  const l = depotV2();
  const cands = l.buildings.filter((b) => b.enterable && backWindows(l, b)?.refs.length);
  assert.ok(cands.length > 0);
  for (const b of cands) {
    // the default front door ('closed' in /2) is quiet -> not eligible
    assert.equal(storyRecipe(l, b, 'broken_back_windows'), null, `${b.id}: closed front`);
    for (const state of ['locked', 'sealed', 'barricaded'] as const) {
      const w = withFront(l, b, state);
      const back = backWindows(w.l, w.b)!;
      assert.equal(back.front.ref, frontDoorAnyState(l, b)!.ref, `${b.id} ${state}: the front stays the front`);
      assert.notEqual(back.side, openingSide(w.b, back.front.opening), `${b.id}: back != front side`);
      assert.deepEqual(storyRecipe(w.l, w.b, 'broken_back_windows'), [{ kind: 'window_state', target: back.side, state: 'broken', floor: 0 }], `${b.id} ${state}`);
    }
  }
  // no exterior door at all -> no front / back facade -> not eligible
  const doorless = l.buildings.find((b) => b.enterable && !frontDoorAnyState(l, b));
  if (doorless) assert.equal(storyRecipe(l, doorless, 'broken_back_windows'), null);
});

test('story 9 rollback: F4 fails on a locked-front Depot building -> the original object, layout unchanged', () => {
  const l0 = depotV2();
  const hit = l0.buildings.filter((x) => x.enterable && backWindows(l0, x)?.refs.length).map((x) => withFront(l0, x, 'locked'))
    .find((w) => !applyStory(w.l, w.b, 'broken_back_windows', 7).ok);
  assert.ok(hit, 'some locked-front building where F4 fails');
  const { l, b } = hit;
  const before = json(l);
  const res = applyStory(l, b, 'broken_back_windows', 7);
  assert.equal(res.ok, false);
  assert.equal(res.building, b, 'the original building object');
  assert.equal(res.ladders, undefined);
  assert.match(res.issues[0].message, /F4/);
  assert.equal(json(l), before, 'nothing mutated');
});

test('story 9 apply + F4: with a noisy (locked) front door, quiet enters by a back window, fast by the front; deterministic', () => {
  const l0 = depotV2();
  // the front door locked (a kick: 15 m of noise) and the other exterior doors sealed, so the quiet profile has a reason to go round;
  // the first eligible building where the story then holds
  const prep = (cand: GenBuilding) => {
    const locked: GenBuilding = structuredClone(cand);
    const front = backWindows(l0, cand)!.front;
    for (const d of passableExteriorDoors(l0, cand)) assert.ok(applyDoorState(locked, d.ref, d.ref === front.ref ? 'locked' : 'sealed'));
    const l: GenLayout = { ...l0, buildings: l0.buildings.map((x) => (x.id === cand.id ? locked : x)) };
    return { l, locked, res: applyStory(l, locked, 'broken_back_windows', 7) };
  };
  const hit = l0.buildings.filter((x) => x.enterable && backWindows(l0, x)?.refs.length).map(prep).find((p) => p.res.ok);
  assert.ok(hit, 'story 9 holds on some building with a noisy front door');
  const { l, locked, res } = hit;
  const back = backWindows(l, locked)!;
  assert.equal(json(applyStory(l, locked, 'broken_back_windows', 7)), json(res), 'deterministic');
  const b = res.building;
  assert.deepEqual(b.stories!.at(-1)!.bricks, [`window_state(${back.side}:0,broken)`]);
  assert.ok(['survivors', 'decay'].includes(b.stories!.at(-1)!.author));
  const ops = unifyOpenings(b), opsBefore = unifyOpenings(locked);
  ops.forEach((o, i) => {
    const ref = openingRef(b, i), was = effectiveOpeningState(l, locked, opsBefore[i]);
    const now = effectiveOpeningState({ ...l, buildings: [b] }, b, o);
    if (back.refs.includes(ref)) assert.equal(now, 'broken', `${ref}: back ground window broken`);
    else assert.equal(now, was, `${ref}: unchanged (front glazed, upper storeys, doors)`);
  });
  const r = computeBuildingRoutes({ ...l, buildings: l.buildings.map((x) => (x.id === b.id ? b : x)) }, b);
  const fastOp = unifyOpenings(b).find((_, i) => openingRef(b, i) === r.fast_entry)!;
  assert.equal(openingSide(b, fastOp), openingSide(b, back.front.opening), `fast by the front facade (${r.fast_entry})`);
  assert.ok(back.refs.includes(r.quiet_entry!), `quiet by a back window (${r.quiet_entry})`);
});

test('F2 3 -> 9: after story 3 (front locked, back door kept) story 9 is the compatible second story, applies and passes F4', () => {
  assert.deepEqual(STORY_COMPATIBLE.locked_door, ['broken_back_windows']);
  const l0 = depotV2();
  // the state story 3 leaves (its recipe door_state(front, locked); the back door stays passable) on the 3-eligible buildings
  const after3 = (b: GenBuilding) => {
    const rec = storyRecipe(l0, b, 'locked_door');
    if (!rec || rec[0].kind !== 'door_state') return null;
    const c: GenBuilding = structuredClone(b);
    assert.ok(applyDoorState(c, rec[0].door, 'locked'));
    c.stories = [{ id: 'locked_door', bricks: [`door_state(${rec[0].door},locked)`], seed: 1, author: 'pmc' }];
    return { before: b, b: c, l: { ...l0, buildings: l0.buildings.map((x) => (x.id === b.id ? c : x)) } as GenLayout };
  };
  const cases = l0.buildings.map(after3).filter((x): x is NonNullable<typeof x> => x !== null && !!backWindows(x.l, x.b)?.refs.length);
  assert.ok(cases.length > 0, 'a 3-eligible building with back ground windows');
  const knobs = { ...STORY_PICK_KNOBS, p_two: 1 };
  let held = 0;
  for (const { l, b, before } of cases) {
    assert.ok(passableExteriorDoors(l, b).length >= 1, `${b.id}: the back door stays`);
    assert.equal(storyEligible(l0, before, 'broken_back_windows', 'objective'), false, `${b.id}: 9 not eligible before 3 (closed front)`);
    assert.equal(storyEligible(l, b, 'broken_back_windows', 'objective'), true, `${b.id}: 9 eligible after 3`);
    assert.equal(pickSecondStory(b, 'locked_door', 'objective', 1, knobs, { layout: l }), 'broken_back_windows', `${b.id}: F2 picks 9 after 3`);
    assert.equal(pickSecondStory(b, 'locked_door', 'objective', 1, { ...knobs, p_two: 0 }, { layout: l }), null, 'p_two 0 -> no second story');
    assert.equal(pickSecondStory(b, 'snow_drift', 'objective', 1, knobs, { layout: l }), null, 'not compatible -> none');
    const res = applyStory(l, b, 'broken_back_windows', 7);
    if (!res.ok) continue;
    held++;
    assert.equal(json(applyStory(l, b, 'broken_back_windows', 7)), json(res), 'deterministic');
    assert.deepEqual(res.building.stories!.map((st) => st.id), ['locked_door', 'broken_back_windows'], 'both stories stored');
    const back = backWindows(l, b)!;
    const r = computeBuildingRoutes({ ...l, buildings: l.buildings.map((x) => (x.id === b.id ? res.building : x)) }, res.building);
    const fastOp = unifyOpenings(res.building).find((_, i) => openingRef(res.building, i) === r.fast_entry)!;
    assert.equal(openingSide(res.building, fastOp), openingSide(b, back.front.opening), `${b.id}: fast by the front facade - kick the front or its glass (${r.fast_entry})`);
    assert.ok(back.refs.includes(r.quiet_entry!), `${b.id}: quiet by a back window (${r.quiet_entry})`);
  }
  assert.ok(held > 0, 'story 9 holds after story 3 on some Depot building');
});
