// Story showcase (designer request 2026-11-21): a small test layout `gen_story_showcase` with ONE generated building per implemented building
// story (buildingStories.ts STORY_CATALOGUE), so the designer can walk each one in game (map /Game/Maps/Generated/L_Gen_StoryShowcase).
// 2 x 3 grid of enterable buildings on flat ground (150 x 110 m), a mandatory street between the rows, the squad start at its west end. Each building gets exactly its story through the generator's own story API (applyStory: forced id,
// the same apply / validate / rollback step and F4 check as the random pass) - no random pick. Story 9 is eligible only with a loud front
// (building_stories.json loud_front_states), so S09 carries the generator's F2 pair 3 + 9 (its last story is 9); every other building one story.
// After the stories: key route / fast / quiet entries per building (computeBuildingRoutes, as applyStoriesToLayout), schema /2 + cost_hash
// (as withBuildingStories), mantle-edge sync, room anchors (as gen-bgen-plans-check.ts). Fails (exit 1) when a story rolls back.
//   node scripts/gen-story-showcase.ts <out.json> [--plans] [--ascii]
// The out file is the accepted layout (Content/Data/LevelGen/accepted/gen_story_showcase.json); the builder writes the level JSON.
import fs from 'node:fs';
import { buildingToAscii } from '../src/levelgen/buildings/planAscii.ts';
import { routeCostHash, computeBuildingRoutes } from '../src/levelgen/buildingRoutes.ts';
import { withRoles } from '../src/levelgen/buildingRoles.ts';
import type { StoryId } from '../src/levelgen/buildingStories.ts';
import { applyStory, storySubSeed } from '../src/levelgen/buildingStories.ts';
import { validateBuilding } from '../src/levelgen/buildingTraversal.ts';
import type { Draft, Side } from '../src/levelgen/generate.ts';
import { addBuilding, finalizeDraftToLayout } from '../src/levelgen/generate.ts';
import { anchorLayoutText } from '../src/levelgen/roomAnchors.ts';
import type { GenLayout } from '../src/levelgen/schema.ts';
import { DEFAULT_PARAMS, LEVEL_HEIGHT, LEVELGEN_SCHEMA_V2, validateGenLayout } from '../src/levelgen/schema.ts';

const out = process.argv[2];
if (!out || out.startsWith('--')) { console.error('usage: node scripts/gen-story-showcase.ts <out.json> [--plans] [--ascii]'); process.exit(2); }
const plans = process.argv.includes('--plans');

const SIZE: [number, number] = [150, 110];
/** The street between the rows (metres, profile frame: x east, y south). */
const STREET_Y = 55;
const START: [number, number] = [4, STREET_Y];
const END: [number, number] = [146, STREET_Y];
const PASS_SEED = 2111;

interface Spec {
  id: string;
  /** The stories in order (the last one is the showcased story; S09: 3 then 9, the generator's F2 pair). */
  stories: StoryId[];
  archetype: 'office' | 'block';
  /** A planned block's template (building_plans.json recipes.block.templates); absent = templates.default (block_khrushchevka). */
  plan_template?: string;
  x: number; y: number; w: number; h: number;
  floors: number;
  seed: number;
  doorSides: Side[];
  windowSides: Side[];
  roof_walkable: boolean;
}
// 2026-11-21 night (kit block plans, docs/design/levelgen/STORY_SHOWCASE_2026-11-22.md): khrushchevka blocks (template block_khrushchevka:
// court-side switchback core, ONE door per section, on the court wall) wherever the story holds on them, else the closest working house:
//  - S01 open, S07 roof ladder: khrushchevka, one section; S07 three storeys = the tallest -> overwatch (story 7 is a detour: needs
//    objective / garrison / overwatch). S10 snow drift: khrushchevka, two sections (37 m) at right angles to the street (the sealed front
//    door is the one nearer the street, the second section's door and the ramp window are the two ways in).
//  - S03 locked door: the S2.3 template (block_sections). On a kit block every door is on the court wall, so with the near door locked the
//    fast and the quiet entry are the same other door (parallel to the street) or a gable window (at right angles) - F4 rolls story 3 back.
//  - S04 rubble + fire escape: the S2.3 template - the kit plans have no balcony slots yet (BGEN_KIT_PLANS_2026-11-21.md section 7).
//  - S09 locked door + broken back windows: an office (the old S09) - story 9's quiet entry must be a broken back window, and both block
//    planners put a second door nearer than those windows (kit: the court door; S2.3: its gable door).
// Top row (y 30..42): street wall south; bottom row (y 68..80): street wall north; the mandatory street at y 55.
const N: Side = 'north', S: Side = 'south';
const specs: Spec[] = [
  { id: 'S01_open', stories: ['open'], archetype: 'block', x: 8, y: 30, w: 20.0, h: 12.0, floors: 2, seed: 11, doorSides: [S], windowSides: [S, N], roof_walkable: true },
  { id: 'S03_locked_door', stories: ['locked_door'], archetype: 'block', plan_template: 'block_sections', x: 46, y: 30, w: 20.2, h: 12.2, floors: 2, seed: 13, doorSides: [S, N], windowSides: [S, N], roof_walkable: true },
  { id: 'S04_rubble_escape', stories: ['rubble_escape'], archetype: 'block', plan_template: 'block_sections', x: 101, y: 30, w: 20.2, h: 12.2, floors: 3, seed: 2, doorSides: [S], windowSides: [S, N], roof_walkable: true },
  { id: 'S07_roof_outside', stories: ['roof_outside'], archetype: 'block', x: 8, y: 68, w: 20.0, h: 12.0, floors: 3, seed: 1, doorSides: [N], windowSides: [N, S], roof_walkable: true },
  { id: 'S09_broken_back_windows', stories: ['locked_door', 'broken_back_windows'], archetype: 'office', x: 46, y: 68, w: 18.4, h: 10.3, floors: 2, seed: 2, doorSides: [N, S], windowSides: [N, S], roof_walkable: true },
  { id: 'S10_snow_drift', stories: ['snow_drift'], archetype: 'block', x: 101, y: 65, w: 12.0, h: 37.0, floors: 2, seed: 1, doorSides: ['west'], windowSides: ['west', 'east'], roof_walkable: true },
];

const d: Draft = { walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [], ...(plans ? { building_plans: true } : {}) };
for (const s of specs) {
  let n = s.seed * 7919;
  const random = (): number => { n = (n * 1103515245 + 12345) & 0x7fffffff; return n / 0x7fffffff; };
  addBuilding(d, s.id, [[s.x, s.y], [s.x + s.w, s.y + s.h]], {
    height: s.floors * LEVEL_HEIGHT, floors: s.floors, enterable: true, roof_walkable: s.roof_walkable, stairwell: true, set_piece: 'facade', archetype: s.archetype,
    doorSides: s.doorSides, windowSides: s.windowSides, partitionRooms: true, ...(s.plan_template ? { plan_template: s.plan_template } : {}),
  }, random);
}
d.routes.push({ id: 'R_main', pace: 'walk', points: [START, END], mandatory: true, layers: ['ground', 'ground'], links: [], set_pieces: ['street'] });
const base = finalizeDraftToLayout(d, SIZE, {
  name: 'gen_story_showcase', profile: 'ref_01_02_depot', seed: PASS_SEED, params: { ...DEFAULT_PARAMS, size_m: SIZE, ...(plans ? { building_plans: true } : {}) },
  start: START, end: END, yaw: 0, comment: 'Story showcase: one building per implemented building story (scripts/gen-story-showcase.ts)',
});
const errs0 = validateGenLayout(base);
if (errs0.length) { console.error('schema errors:', errs0.slice(0, 10)); process.exit(1); }

// the story pass with forced ids (withBuildingStories / applyStoriesToLayout, minus the random pick)
let cur: GenLayout = withRoles({ ...base, schema: LEVELGEN_SCHEMA_V2, cost_hash: routeCostHash() });
let failed = false;
for (const s of specs) {
  const before = cur;
  const pristine = cur.buildings.find((b) => b.id === s.id)!;
  let b = pristine;
  for (const [i, story] of s.stories.entries()) {
    const res = applyStory(cur, b, story, storySubSeed(PASS_SEED, i ? `${s.id}#2` : s.id, 0));
    if (!res.ok) { failed = true; console.error(`${s.id}: ${res.issues.map((x) => x.message).join('; ')}`); break; }
    b = res.building;
    const held = res;
    cur = { ...cur, buildings: cur.buildings.map((x) => (x.id === s.id ? held.building : x)), ...(held.ladders ? { ladders: held.ladders } : {}) };
  }
  const routes = computeBuildingRoutes(cur, b, { baseline: { layout: before, building: pristine } });
  if (routes.key_route) b.key_route = routes.key_route;
  if (routes.breach_route) b.breach_route = routes.breach_route;
  if (routes.fast_entry !== undefined) b.fast_entry = routes.fast_entry;
  if (routes.quiet_entry !== undefined) b.quiet_entry = routes.quiet_entry;
}
// syncMantleEdges (buildingStories.ts, private): a window turned into a balcony door drops its layout-level mantle edge
if (cur.mantle_edges?.length) {
  const own = new Map(cur.buildings.filter((b) => b.mantle_edges).map((b) => [b.id, new Set(b.mantle_edges!.map((m) => m.id))]));
  cur = { ...cur, mantle_edges: cur.mantle_edges.filter((m) => m.kind !== 'window' || !m.building || !own.has(m.building) || own.get(m.building)!.has(m.id)) };
}
const errs = validateGenLayout(cur);
if (errs.length) { console.error('schema errors after stories:', errs.slice(0, 10)); process.exit(1); }

const text = anchorLayoutText(JSON.stringify(cur, null, 1) + '\n', true);
fs.writeFileSync(out, text);
const o = JSON.parse(text) as GenLayout;
for (const s of specs) {
  const b = o.buildings.find((x) => x.id === s.id)!;
  const v = validateBuilding(o, b);
  const got = (b.stories ?? []).map((x) => x.id).join('+') || '-';
  const want = s.stories.join('+');
  const errors = v.issues.filter((i) => i.severity === 'error');
  console.log(`${s.id}: story ${got} ${got === want ? 'KEPT' : `EXPECTED ${want}`} role=${b.role ?? '-'} plan=${b.plan?.kind ?? '-'}/${b.plan?.template ?? '-'}${b.plan?.fallback ? ` fallback=${b.plan.fallback}` : ''} floors=${b.floors}`
    + ` fast=${b.fast_entry ?? '-'} quiet=${b.quiet_entry ?? '-'} bricks=[${(b.stories ?? []).flatMap((x) => x.bricks).join(' ')}] errors=${errors.length}${errors.length ? ` (${errors.map((i) => i.code).join(',')})` : ''}`);
  if (got !== want) failed = true;
}
if (process.argv.includes('--ascii')) for (const b of o.buildings) { console.log(`\n== ${b.id}`); for (let f = 0; f < b.floors; ++f) console.log(buildingToAscii(b, f)); }
if (failed) { console.error('story showcase: a story did not hold - change the building choice (size / doors / floors), not the validator'); process.exit(1); }
