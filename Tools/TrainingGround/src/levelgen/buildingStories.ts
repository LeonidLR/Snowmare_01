// Training Ground / level generator - BUILDING STORIES engine (building generator S1 step 3a; docs/design/levelgen/gdd/building-stories.md
// sections 3 bricks, 5 application, 6 catalogue, F1 two-step pick, F3 role budget, F5 rollback budget).
// A story is a recipe of bricks applied to a COPY of a building; the copy must pass the traversal validator (buildingTraversal.ts,
// opening states honoured) with no error the untouched building did not already have, else every brick is undone (the original
// building object is returned) and a 'story_rolled_back' warning is logged. Catalogue for now: 1 'open' (no bricks) and
// 3 'locked_door' (door_state(front, locked)). Pure and deterministic per (seed, building id); NOT wired into the generator
// pipeline or the page yet (a later step does that).

import type { Vec2 } from '../movement/geometry.ts';
import { closestOnSegment, dist, rng } from '../movement/geometry.ts';
import { fnv1a } from './props/rng.ts';
import { toBuildingFrame } from './rotation.ts';
import { withRoles } from './buildingRoles.ts';
import type { TraversalIssue } from './buildingTraversal.ts';
import { TRAVERSAL_SEVERITY, validateBuilding } from './buildingTraversal.ts';
import { classifyOpening, effectiveOpeningState, isPassableState } from './openingStates.ts';
import type { BuildingRole, DoorState, GenBuilding, GenLayout, Side, StoryInstance, WallOpening, WindowState } from './schema.ts';
import { DOOR_STATES, WINDOW_STATES, unifyOpenings } from './schema.ts';
import { openingOutwardDir } from './tacticalEvaluate.ts';

// ---- catalogue ----------------------------------------------------------------------------------------------------------------------

export type StoryAuthor = StoryInstance['author'];
export type StoryId = 'open' | 'locked_door';

/** The catalogue rows implemented so far (building-stories.md 6): number, allowed authors, type. */
export const STORY_CATALOGUE: Record<StoryId, { num: number; authors: readonly StoryAuthor[]; type: 'base' | 'choice' }> = {
  /** 1 Open: no bricks, author traces only (any author). */
  open: { num: 1, authors: ['survivors', 'pmc', 'cold', 'decay'], type: 'base' },
  /** 3 Locked door: door_state(front, locked) - kick the front or go round to the back door. */
  locked_door: { num: 3, authors: ['pmc', 'decay', 'survivors'], type: 'choice' },
};

/**
 * The pick knobs (building-stories.md F1 / F3 / F5; later building_stories.json): `budget` = the share of buildings of a role that
 * get a story other than open (F3, *proposal*), `weights[role][story]` = F1 step-2 weights (archetype dimension not used yet;
 * detour stories are not in this step), `t_max` = tries per building before it keeps 'open' (F5).
 */
export const STORY_PICK_KNOBS = {
  budget: { objective: 1.0, garrison: 0.8, overwatch: 0.7, transit: 0.5, background: 0.1 } as Record<BuildingRole, number>,
  weights: {
    objective: { locked_door: 1 },
    garrison: { locked_door: 1 },
    overwatch: { locked_door: 1 },
    transit: { locked_door: 1 },
    background: { locked_door: 1 },
  } as Record<BuildingRole, Partial<Record<Exclude<StoryId, 'open'>, number>>>,
  t_max: 4,
};

export type StoryPickKnobs = typeof STORY_PICK_KNOBS;

// ---- bricks -------------------------------------------------------------------------------------------------------------------------

/** One brick of a recipe (building-stories.md 3); only the two opening-state bricks exist so far. */
export type Brick =
  | { kind: 'door_state'; door: string; state: DoorState }
  | { kind: 'window_state'; target: Side | number; state: WindowState };

/** The brick as stored in StoryInstance.bricks, e.g. "door_state(D_B1_0,locked)". */
export function brickToString(b: Brick): string {
  return b.kind === 'door_state' ? `door_state(${b.door},${b.state})` : `window_state(${b.target},${b.state})`;
}

/** The reference of an opening: its id, else its index in unifyOpenings ("openings[3]"). */
export function openingRef(b: GenBuilding, index: number): string {
  return unifyOpenings(b)[index]?.id ?? `openings[${index}]`;
}

/** Make b.openings the editable list (legacy door-only buildings get unifyOpenings' copies; nothing else changes). */
function editableOpenings(b: GenBuilding): WallOpening[] {
  if (!b.openings?.length) b.openings = unifyOpenings(b).map((o) => ({ ...o }));
  return b.openings;
}

/** The facade side an opening sits on (building frame: north = min y, south = max y, west = min x, east = max x). */
export function openingSide(b: GenBuilding, op: WallOpening): Side {
  const [[x0, y0], [x1, y1]] = b.footprint;
  const q = toBuildingFrame(b, op.pos);
  const d: [Side, number][] = [['west', Math.abs(q[0] - x0)], ['east', Math.abs(q[0] - x1)], ['north', Math.abs(q[1] - y0)], ['south', Math.abs(q[1] - y1)]];
  return d.reduce((a, c) => (c[1] < a[1] ? c : a))[0];
}

/** door_state(door, state): set one door's state (b.openings id or "openings[i]" ref, partition doors by id). Mutates `b`. */
export function applyDoorState(b: GenBuilding, door: string, state: DoorState): boolean {
  if (!DOOR_STATES.includes(state)) return false;
  const m = /^openings\[(\d+)\]$/.exec(door);
  const ops = editableOpenings(b);
  const hit = m ? ops[Number(m[1])] : ops.find((o) => o.id === door) ?? (b.partitions ?? []).flatMap((pt) => pt.openings ?? []).find((o) => o.id === door);
  if (!hit || hit.kind !== 'door') return false;
  hit.state = state;
  return true;
}

/**
 * window_state(side or storey, state): set the state of every exterior window on a facade side (all storeys) or on a storey (all
 * sides). Loopholes stay 'fixed'. Returns the number of windows changed. Mutates `b`.
 */
export function applyWindowState(l: Pick<GenLayout, 'walls'>, b: GenBuilding, target: Side | number, state: WindowState): number {
  if (!WINDOW_STATES.includes(state)) return 0;
  let n = 0;
  for (const op of editableOpenings(b)) {
    if (op.kind !== 'window' || !classifyOpening(l, b, op).exterior) continue;
    if (typeof target === 'number' ? (op.floor ?? 0) !== target : openingSide(b, op) !== target) continue;
    op.state = state;
    n++;
  }
  return n;
}

function applyBrick(l: GenLayout, b: GenBuilding, brick: Brick): boolean {
  return brick.kind === 'door_state' ? applyDoorState(b, brick.door, brick.state) : applyWindowState(l, b, brick.target, brick.state) > 0;
}

// ---- recipes ------------------------------------------------------------------------------------------------------------------------

/** The exterior ground-floor doors of a building that are passable in their effective state, with their refs. */
export function passableExteriorDoors(l: GenLayout, b: GenBuilding): { ref: string; opening: WallOpening }[] {
  return unifyOpenings(b)
    .map((opening, i) => ({ ref: openingRef(b, i), opening }))
    .filter(({ opening: o }) => o.kind === 'door' && (o.floor ?? 0) === 0 && classifyOpening(l, b, o).exterior && isPassableState(effectiveOpeningState(l, b, o)));
}

function routeDistance(route: readonly Vec2[], p: Vec2): { d: number; point: Vec2 } {
  if (route.length === 1) return { d: dist(p, route[0]), point: route[0] };
  let best = { d: Infinity, point: p };
  for (let i = 0; i + 1 < route.length; i++) {
    const c = closestOnSegment(p, route[i], route[i + 1]).point;
    const d = dist(p, c);
    if (d < best.d) best = { d, point: c };
  }
  return best;
}

/**
 * The front door of a building: among its passable exterior ground-floor doors, the one facing the mandatory route (its outward
 * normal points at the nearest route point) and nearest to it; without a route (or none facing it) the nearest / first one.
 * Null without a passable exterior door.
 */
export function frontDoor(l: GenLayout, b: GenBuilding): { ref: string; opening: WallOpening } | null {
  const doors = passableExteriorDoors(l, b);
  if (!doors.length) return null;
  const route = l.routes.find((r) => r.mandatory)?.points ?? [];
  if (!route.length) return doors[0];
  const scored = doors.map((d, i) => {
    const { d: dd, point } = routeDistance(route, d.opening.pos);
    const n = openingOutwardDir(b, d.opening);
    const facing = (point[0] - d.opening.pos[0]) * n[0] + (point[1] - d.opening.pos[1]) * n[1] > 0;
    return { d, i, dd, facing };
  });
  scored.sort((a, c) => Number(c.facing) - Number(a.facing) || a.dd - c.dd || a.i - c.i);
  return scored[0].d;
}

/** The bricks of a story for this building, or null when the story is not eligible (F1 `eligible(s, b)`). */
export function storyRecipe(l: GenLayout, b: GenBuilding, story: StoryId): Brick[] | null {
  if (!b.enterable) return story === 'open' ? [] : null;
  switch (story) {
    case 'open':
      return [];
    case 'locked_door': {
      // the back door must stay: >= 2 passable exterior doors (building-stories.md 6 row 3 "round to the back door")
      if (passableExteriorDoors(l, b).length < 2) return null;
      const front = frontDoor(l, b);
      return front ? [{ kind: 'door_state', door: front.ref, state: 'locked' }] : null;
    }
  }
}

/** Is the story eligible for the building (F1)? */
export function storyEligible(l: GenLayout, b: GenBuilding, story: StoryId): boolean {
  return storyRecipe(l, b, story) !== null;
}

// ---- pick (F1) ----------------------------------------------------------------------------------------------------------------------

export interface PickStoryOptions {
  /** The layout the building belongs to (eligibility reads its schema, walls and mandatory route); absent = a bare /1 layout. */
  layout?: GenLayout;
  /** Stories already tried and rolled back for this building: excluded, and step 1 is skipped (the building is storied). */
  exclude?: readonly StoryId[];
}

/**
 * F1 two-step pick: step 1 - storied with probability budget[role] (F3), else 'open'; step 2 - among the eligible stories other
 * than 'open', P(s) = w[role][s] / sum of the eligible weights; nothing eligible -> 'open'. Deterministic per seed.
 */
export function pickStory(
  building: GenBuilding,
  role: BuildingRole | undefined,
  seed: number,
  knobs: StoryPickKnobs = STORY_PICK_KNOBS,
  opts: PickStoryOptions = {},
): StoryId {
  if (!role || !building.enterable) return 'open';
  const r = rng(seed);
  const exclude = opts.exclude ?? [];
  if (!exclude.length && r() >= (knobs.budget[role] ?? 0)) return 'open';
  const l: GenLayout = opts.layout ?? ({ schema: 'codex.levelgen_layout/1', walls: [], routes: [], buildings: [building] } as unknown as GenLayout);
  const weights = knobs.weights[role] ?? {};
  const cands = (Object.keys(weights) as Exclude<StoryId, 'open'>[])
    .filter((s) => (weights[s] ?? 0) > 0 && !exclude.includes(s) && storyEligible(l, building, s))
    .sort();
  const total = cands.reduce((a, s) => a + (weights[s] ?? 0), 0);
  if (!cands.length || total <= 0) return 'open';
  let x = r() * total;
  for (const s of cands) {
    x -= weights[s] ?? 0;
    if (x < 0) return s;
  }
  return cands[cands.length - 1];
}

// ---- apply (section 5 steps 2-4) ----------------------------------------------------------------------------------------------------

export interface ApplyStoryResult {
  /** The story held: every brick applied and no new validator error. */
  ok: boolean;
  /** The storied copy (with `stories` appended) when ok, else the ORIGINAL building object, untouched. */
  building: GenBuilding;
  /** The story instance (applied when ok, attempted when not). */
  instance: StoryInstance;
  /** 'story_rolled_back' (warning) when not ok. */
  issues: TraversalIssue[];
}

const errorKeys = (issues: readonly TraversalIssue[]) => new Set(issues.filter((i) => i.severity === 'error').map((i) => `${i.code}|${i.ref ?? ''}`));

/**
 * Apply a story to a copy of `building` (a building of `layout`), validate the copy in the layout, and keep it only when it adds
 * no validator error the original did not have. The author is drawn from the story's allowed authors by `seed`; no outcome.
 */
export function applyStory(layout: GenLayout, building: GenBuilding, storyId: StoryId, seed: number): ApplyStoryResult {
  return applyRecipe(layout, building, storyId, storyRecipe(layout, building, storyId), seed);
}

/** applyStory with an explicit recipe (null = not eligible): the shared apply / validate / rollback step. */
export function applyRecipe(layout: GenLayout, building: GenBuilding, storyId: StoryId, recipe: readonly Brick[] | null, seed: number): ApplyStoryResult {
  const r = rng(seed);
  const authors = STORY_CATALOGUE[storyId].authors;
  const author = authors[Math.floor(r() * authors.length) % authors.length];
  const instance: StoryInstance = { id: storyId, bricks: (recipe ?? []).map(brickToString), seed, author };
  const rollback = (why: string): ApplyStoryResult => ({
    ok: false,
    building,
    instance,
    issues: [{ severity: TRAVERSAL_SEVERITY.story_rolled_back, code: 'story_rolled_back', ref: storyId, message: `${building.id}: story ${storyId} rolled back (${why})` }],
  });
  if (!recipe) return rollback('not eligible');
  const copy: GenBuilding = structuredClone(building);
  for (const brick of recipe) if (!applyBrick(layout, copy, brick)) return rollback(`brick ${brickToString(brick)} did not apply`);
  if (recipe.length) {
    const swap = (b: GenBuilding): GenLayout => ({ ...layout, buildings: layout.buildings.map((x) => (x.id === building.id ? b : x)) });
    const before = errorKeys(validateBuilding(swap(building), building).issues);
    const after = validateBuilding(swap(copy), copy);
    const added = after.issues.filter((i) => i.severity === 'error' && !before.has(`${i.code}|${i.ref ?? ''}`));
    if (added.length) return rollback(added.map((i) => i.code).join(', '));
  }
  copy.stories = [...(copy.stories ?? []), instance];
  return { ok: true, building: copy, instance, issues: [] };
}

// ---- per map (section 5) ------------------------------------------------------------------------------------------------------------

export interface StoryLogEntry {
  building: string;
  story: StoryId;
  try: number;
  issue: TraversalIssue;
}

/** The deterministic sub-seed of building `id`, try `t` (section 5 step 4 "deterministic sub-seed"). */
export function storySubSeed(seed: number, id: string, t: number): number {
  return fnv1a(`${seed}:${id}:${t}`);
}

/**
 * Roles (withRoles) + per enterable building: pick (F1) and apply, up to t_max tries (a rolled-back story is excluded from the next
 * try), then 'open'. Every enterable building ends with one StoryInstance; a building that already has `stories` (locked on the
 * page) is kept as it is. Deterministic per (seed, building id). The input layout is not mutated.
 */
export function applyStoriesToLayout(
  layout: GenLayout,
  seed: number,
  knobs: StoryPickKnobs = STORY_PICK_KNOBS,
): { layout: GenLayout; log: StoryLogEntry[] } {
  let cur = withRoles(layout);
  const log: StoryLogEntry[] = [];
  for (const id of cur.buildings.map((b) => b.id)) {
    const b = cur.buildings.find((x) => x.id === id)!;
    if (!b.enterable || !b.role || b.stories?.length) continue;
    const failed: StoryId[] = [];
    let done: GenBuilding | null = null;
    for (let t = 0; t < knobs.t_max && !done; t++) {
      const sub = storySubSeed(seed, id, t);
      const story = pickStory(b, b.role, sub, knobs, { layout: cur, exclude: failed });
      if (story === 'open') break;
      const res = applyStory(cur, b, story, sub);
      if (res.ok) done = res.building;
      else {
        failed.push(story);
        for (const issue of res.issues) log.push({ building: id, story, try: t, issue });
      }
    }
    if (!done) done = applyStory(cur, b, 'open', storySubSeed(seed, id, 0)).building;
    const next = done;
    cur = { ...cur, buildings: cur.buildings.map((x) => (x.id === id ? next : x)) };
  }
  return { layout: cur, log };
}
