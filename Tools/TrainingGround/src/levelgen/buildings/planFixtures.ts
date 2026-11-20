// Shared fixtures of the S2.7 tests (replan.test.ts, planOverlay.test.ts): the Depot x1 with plans, stories and rotated buildings, and its x6 expansion - what the page holds
// with both toggles on. Built once per test process; nothing is written. Not a test file (node --test only picks *.test.ts).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { expandLayout } from '../expand.ts';
import type { Draft } from '../generate.ts';
import { addBuilding, finalizeDraftToLayout } from '../generate.ts';
import type { Profile } from '../profile.ts';
import type { GenBuilding, GenLayout } from '../schema.ts';
import { DEFAULT_PARAMS, LEVEL_HEIGHT } from '../schema.ts';
import type { StoryPassResult } from '../storyPass.ts';
import { generateCandidate, withBuildingStories } from '../storyPass.ts';

const TG = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
export const ROOT = path.resolve(TG, '../..');
export const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;

let cached: { s1: StoryPassResult; s6: StoryPassResult } | null = null;

/** Depot seed 1 (plans, stories, max rotation 18 deg) and its x6 expansion (seed 7) with the story pass: 18 planned buildings, offices and blocks, some rotated. */
export function fixtures(): { s1: StoryPassResult; s6: StoryPassResult } {
  if (cached) return cached;
  const s1 = generateCandidate({ profile, seed: 1, params: { building_plans: true, max_rotation_deg: 18 }, stories: true }) as StoryPassResult;
  const r6 = expandLayout({ layout: s1.layout, factor: 6, seed: 7, profile });
  if (!r6.ok) throw new Error(r6.message);
  cached = { s1, s6: withBuildingStories(r6.layout, r6.seed, s1) };
  return cached;
}

export const byId = (l: GenLayout, id: string): GenBuilding => l.buildings.find((b) => b.id === id)!;
export const planned = (l: GenLayout, archetype?: string): GenBuilding[] => l.buildings.filter((b) => b.plan?.kind === 'bands' && (!archetype || b.archetype === archetype));

/** One axis-aligned planned building (a block of `w` x `h` m or an office) straight from addBuilding in an empty draft, finalized to a layout: no template, no rotation. */
export function draftBuilding(archetype: 'office' | 'block', w: number, h: number, floors: number, seed = 4): { l: GenLayout; b: GenBuilding } {
  const d: Draft = { walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [], building_plans: true };
  let n = seed * 7919;
  const random = (): number => { n = (n * 1103515245 + 12345) & 0x7fffffff; return n / 0x7fffffff; };
  addBuilding(d, 'B_t', [[20.37, 15.21], [20.37 + w, 15.21 + h]], {
    height: floors * LEVEL_HEIGHT, floors, enterable: true, roof_walkable: archetype === 'block', stairwell: floors > 1, set_piece: 'facade', archetype,
    doorSides: ['south'], windowSides: ['south', 'north'], partitionRooms: true,
  }, random);
  const l = finalizeDraftToLayout(d, [90, 90], { name: 't', profile: 't', seed: 1, params: { ...DEFAULT_PARAMS, size_m: [90, 90], building_plans: true }, start: [4, 2], end: [36, 58] });
  return { l, b: byId(l, 'B_t') };
}
