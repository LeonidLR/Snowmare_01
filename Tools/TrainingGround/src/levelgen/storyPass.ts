// Training Ground / level generator, building generator S1 step 4: the STORY PASS as the last generation step (BUILDINGS_TECH_SPEC.md
// section 1 steps 5-7 + 11, section 5 page; ADR-0002 decisions 1-4). One shared entry for the page («Истории зданий (S1)» toggle)
// and the CLI (`--stories`):
//   - withBuildingStories(layout, seed): the layout as schema /2 + `cost_hash` (routeCostHash of ROUTE_COSTS), then
//     applyStoriesToLayout - roles, one story per enterable building, key routes. A building that already has stories (kept by a
//     region re-roll / a lock, or the old map of an expansion) keeps them; the fresh ones get stories from `seed`'s sub-seeds.
//   - restoryForGoals: the pass re-run after the mission goals change (designer 2026-11-19): role-changed buildings re-picked.
//   - rerollBuildingStory: «другая история» - one building's story re-rolled with the next sub-seed (refused inside a lock).
//   - storyMapSummary: the per-map line of the «Здания» panel (role shares, storied share, rollbacks, choice share).
// Toggle off = none of this runs: the generator's /1 output is unchanged byte for byte. Pure, deterministic, no DOM.
import { applyStoriesToLayout, storySubSeed, type StoryId, type StoryLogEntry } from './buildingStories.ts';
import { assignRoles } from './buildingRoles.ts';
import { routeCostHash } from './buildingRoutes.ts';
import { generateLayout } from './generate.ts';
import type { Profile } from './profile.ts';
import { isLockedAt, layoutGroups, regenStateOf } from './regionRegen.ts';
import type { BuildingRole, GenBuilding, GenLadder, GenLayout, GenParams } from './schema.ts';
import { LEVELGEN_SCHEMA_V2 } from './schema.ts';

/** A building as it was before its story (the re-roll restores it) and the layout ladders its story added. */
export interface StoryPristine { building: GenBuilding; ladders: string[] }

/** What the page keeps next to a storied candidate (in memory only - not part of the layout JSON). */
export interface StoryState {
  /** Pre-story building per storied building id (kept buildings carry theirs over from the previous pass). */
  base: Record<string, StoryPristine>;
  /** Rollbacks of the stories that are on the map now (kept buildings' entries + this pass). */
  log: StoryLogEntry[];
}

export interface StoryPassResult extends StoryState { layout: GenLayout }

/** The ladder's building, from its layer names ('roof:<id>', 'floor:<id>:<n>'), as regionRegen groups them. */
const layerOwner = (layer: string): string | undefined =>
  layer.startsWith('roof:') ? layer.slice(5) : layer.startsWith('floor:') ? layer.split(':')[1] : undefined;
const ladderOwner = (k: GenLadder): string | undefined => layerOwner(k.to) ?? layerOwner(k.from);

/**
 * The story pass (the last generation step): schema /2 + cost_hash, then applyStoriesToLayout(layout, seed). `prev` = the state of
 * the layout this one was derived from (region re-roll / expansion / re-roll): kept buildings keep their pristine copy and log.
 */
export function withBuildingStories(layout: GenLayout, seed: number, prev?: StoryState): StoryPassResult {
  const input: GenLayout = { ...layout, schema: LEVELGEN_SCHEMA_V2, cost_hash: routeCostHash() };
  const { layout: out, log } = applyStoriesToLayout(input, seed);
  const inBuildings = new Map(input.buildings.map((b) => [b.id, b] as const));
  const inLadders = new Set((input.ladders ?? []).map((k) => k.id));
  const added = new Map<string, string[]>();
  for (const k of out.ladders ?? []) {
    const owner = inLadders.has(k.id) ? undefined : ladderOwner(k);
    if (owner) added.set(owner, [...(added.get(owner) ?? []), k.id]);
  }
  const base: Record<string, StoryPristine> = {};
  const kept = new Set<string>();
  for (const b of out.buildings) {
    if (!b.stories?.length) continue;
    const before = inBuildings.get(b.id);
    if (before?.stories?.length) {
      kept.add(b.id);
      const p = prev?.base[b.id];
      if (p) base[b.id] = p;
    } else if (before) base[b.id] = { building: before, ladders: added.get(b.id) ?? [] };
  }
  return { layout: out, base, log: [...(prev?.log ?? []).filter((e) => kept.has(e.building)), ...log] };
}

/** generateLayout + (when `stories`) the story pass with the layout's seed - the page's «Генерировать» and the CLI share it. */
export function generateCandidate(o: { profile: Profile; seed: number; params?: Partial<GenParams>; stories: boolean }): StoryPassResult | { layout: GenLayout } {
  const layout = generateLayout({ profile: o.profile, seed: o.seed, params: o.params });
  return o.stories ? withBuildingStories(layout, o.seed) : { layout };
}

/** Does the layout carry the story pass (any building with stories)? A toggle-off map has none: goal edits leave it /1. */
export const hasStories = (l: GenLayout): boolean => l.buildings.some((b) => b.stories?.length);

/** A building without its story and the route fields the pass writes (the 'open' story of a loaded map has no pristine copy). */
function unstoried(b: GenBuilding): GenBuilding {
  const { stories: _s, key_route: _k, breach_route: _b, fast_entry: _f, quiet_entry: _q, ...rest } = b;
  return rest;
}

export interface GoalStoryResult extends StoryPassResult {
  /** Buildings whose story was re-picked (their role changed with the goals). */
  repicked: string[];
  /** Buildings whose role changed but whose story stayed: inside a locked district, or storied without a pristine copy (loaded map). */
  keptChanged: string[];
}

/**
 * Designer decision 2026-11-19 (a): the story pass after the mission goals, re-run whenever `layout.objectives` changes. Roles are
 * re-assigned from the new goals (buildingRoles.ts); a storied building whose role changed goes back to its pristine copy (its story
 * ladders dropped) and gets a story again from `seed`'s sub-seeds (storySubSeed(seed, id, t), the generation pass's chain); a
 * building inside a locked district, or whose role did not change, keeps its story. Nothing changed role -> `layout` itself.
 * The caller runs it only on a storied map (hasStories) with the toggle on.
 */
export function restoryForGoals(layout: GenLayout, seed: number, state?: StoryState): GoalStoryResult {
  const roles = assignRoles(layout);
  const locked = lockedBuildingIds(layout);
  const repicked: string[] = [], keptChanged: string[] = [], drop = new Set<string>();
  const buildings = layout.buildings.map((b) => {
    if (!b.stories?.length || roles[b.id] === b.role) return b;
    const p = state?.base[b.id];
    if (!locked.has(b.id) && (p || b.stories.at(-1)!.id === 'open')) {
      repicked.push(b.id);
      for (const k of p?.ladders ?? []) drop.add(k);
      return p ? p.building : unstoried(b);
    }
    keptChanged.push(b.id);
    return b;
  });
  const base = state?.base ?? {}, log = state?.log ?? [];
  if (!repicked.length && !keptChanged.length) return { layout, base, log, repicked, keptChanged };
  const gone = new Set(repicked);
  const rest: StoryState = { base: Object.fromEntries(Object.entries(base).filter(([id]) => !gone.has(id))), log: log.filter((e) => !gone.has(e.building)) };
  const bare: GenLayout = { ...layout, buildings, ...(layout.ladders ? { ladders: layout.ladders.filter((k) => !drop.has(k.id)) } : {}) };
  return { ...withBuildingStories(bare, seed, rest), repicked, keptChanged };
}

/** The ids of the buildings inside a locked district (by centre, as the region re-roll selects them). */
export function lockedBuildingIds(l: GenLayout): Set<string> {
  const locks = regenStateOf(l).locks;
  if (!locks.length) return new Set();
  return new Set(layoutGroups(l).filter((g) => g.category === 'building' && isLockedAt(g.centre, locks)).map((g) => g.id));
}

/** Is the building inside a locked district? */
export const isBuildingLocked = (l: GenLayout, id: string): boolean => lockedBuildingIds(l).has(id);

/** «другая история»: sub-seeds tried until the story id differs from the current one (a tool cap, not a design number). */
export const REROLL_MAX_STEPS = 8;

export interface RerollResult extends Partial<StoryPassResult> {
  ok: boolean;
  /** The story before / after (after = before when no other story came up within REROLL_MAX_STEPS). */
  from?: string;
  to?: string;
  /** The pass seed the new story came from. */
  seed?: number;
  message: string;
}

/** The next sub-seed of a building's story chain: derived from its current story instance's seed (no extra state). */
export const nextStorySeed = (instanceSeed: number, id: string, step: number): number => storySubSeed(instanceSeed, `${id}#reroll`, step);

/**
 * Re-roll one building's story with the next sub-seed (section 5 «другая история»): the pristine building and ladders come back,
 * the story pass runs for it alone (pick + apply + rollback + key routes), its rollback log entries are replaced. Refused for a
 * building inside a locked district, without a pristine copy (a loaded map) or without a story.
 */
export function rerollBuildingStory(layout: GenLayout, id: string, state: StoryState): RerollResult {
  const b = layout.buildings.find((x) => x.id === id);
  const cur = b?.stories?.[b.stories.length - 1];
  const pristine = state.base[id];
  if (!b || !cur) return { ok: false, message: `${id}: у здания нет истории` };
  if (!pristine) return { ok: false, message: `${id}: нет исходного здания (карта загружена из файла) - перегенерируйте` };
  if (isBuildingLocked(layout, id)) return { ok: false, message: `${id}: здание в закреплённом районе` };
  const drop = new Set(pristine.ladders);
  const bare: GenLayout = {
    ...layout,
    buildings: layout.buildings.map((x) => (x.id === id ? pristine.building : x)),
    ladders: (layout.ladders ?? []).filter((k) => !drop.has(k.id)),
  };
  const rest: StoryState = { base: state.base, log: state.log.filter((e) => e.building !== id) };
  let res: StoryPassResult | null = null, seed = 0;
  for (let step = 0; step < REROLL_MAX_STEPS; step++) {
    seed = nextStorySeed(cur.seed, id, step);
    res = withBuildingStories(bare, seed, rest);
    const now = res.layout.buildings.find((x) => x.id === id)?.stories?.at(-1)?.id;
    if (now !== cur.id) break;
  }
  const to = res!.layout.buildings.find((x) => x.id === id)?.stories?.at(-1)?.id ?? 'open';
  return {
    ok: true, ...res!, from: cur.id, to, seed,
    message: to === cur.id ? `${id}: другая история не выпала за ${REROLL_MAX_STEPS} попыток (${cur.id})` : `${id}: ${cur.id} -> ${to}`,
  };
}

// ---- per-map summary (section 5 "Map summary"; computed only, no new numbers) ------------------------------------------------------

export interface StoryMapSummary {
  /** Enterable buildings with a role. */
  buildings: number;
  /** Share of the role among them. */
  roles: Partial<Record<BuildingRole, number>>;
  /** Buildings per story id. */
  stories: Partial<Record<StoryId | string, number>>;
  /** Share of buildings whose story is not 'open'. */
  storied: number;
  /** Rolled-back story tries (from the page's log; undefined = unknown, e.g. a loaded map). */
  rollbacks?: number;
  /** Share of buildings with a choice: fast entry != quiet entry (F4). */
  choice: number;
}

export function storyMapSummary(l: GenLayout, log?: readonly StoryLogEntry[]): StoryMapSummary {
  const bs = l.buildings.filter((b) => b.enterable && b.role);
  const n = bs.length || 1;
  const roles: Partial<Record<BuildingRole, number>> = {};
  const stories: Partial<Record<string, number>> = {};
  let storied = 0, choice = 0;
  for (const b of bs) {
    roles[b.role!] = (roles[b.role!] ?? 0) + 1 / n;
    const s = b.stories?.at(-1)?.id ?? 'open';
    stories[s] = (stories[s] ?? 0) + 1;
    if (s !== 'open') storied++;
    if (b.fast_entry !== undefined && b.quiet_entry !== undefined && b.fast_entry !== b.quiet_entry) choice++;
  }
  return { buildings: bs.length, roles, stories, storied: storied / n, ...(log ? { rollbacks: log.length } : {}), choice: choice / n };
}
