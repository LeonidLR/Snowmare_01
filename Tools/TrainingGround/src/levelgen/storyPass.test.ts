// Building generator S1 step 4 (storyPass.ts): the story pass as the last generation step - the page toggle / CLI --stories,
// off = byte-identical /1; region re-roll + locks; «другая история»; determinism on the Depot (seed 1) and x4 (7) -> x6 (8).
// `npm test`. Inputs: the Depot reference profile (pure, nothing written except the CLI run's temp dir).
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { routeCostHash, ROUTE_COSTS } from './buildingRoutes.ts';
import { STORY_PICK_KNOBS, storySubSeed } from './buildingStories.ts';
import { expandLayout } from './expand.ts';
import { generateLayout } from './generate.ts';
import type { Profile } from './profile.ts';
import { addLock, layoutGroups, regenerateRegion, shapePolygon } from './regionRegen.ts';
import type { GenBuilding, GenLayout } from './schema.ts';
import { LEVELGEN_SCHEMA, LEVELGEN_SCHEMA_V2, validateGenLayout } from './schema.ts';
import type { StoryPassResult } from './storyPass.ts';
import { generateCandidate, lockedBuildingIds, rerollBuildingStory, storyMapSummary, withBuildingStories } from './storyPass.ts';

const TG = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const ROOT = path.resolve(TG, '../..');
const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
const json = (l: unknown) => JSON.stringify(l, null, 1);
const storied = (seed = 1) => generateCandidate({ profile, seed, stories: true }) as StoryPassResult;
const byId = (l: GenLayout, id: string) => l.buildings.find((b) => b.id === id)!;
const withoutRole = ({ role: _r, ...rest }: GenBuilding) => rest;
const subSeeds = (seed: number, id: string) => Array.from({ length: STORY_PICK_KNOBS.t_max }, (_, t) => storySubSeed(seed, id, t));

/** x1 seed 1 -> x4 seed 7 -> x6 seed 8, the story pass after each step (as the page with the toggle on). */
function chain(): StoryPassResult[] {
  const s1 = storied(1);
  const r4 = expandLayout({ layout: s1.layout, factor: 4, seed: 7, profile });
  assert.equal(r4.ok, true, r4.message);
  const s4 = withBuildingStories(r4.layout, r4.seed, s1);
  const r6 = expandLayout({ layout: s4.layout, factor: 6, seed: 8, profile });
  assert.equal(r6.ok, true, r6.message);
  return [s1, s4, withBuildingStories(r6.layout, r6.seed, s4)];
}

test('story pass off: the candidate is the generator output byte for byte (/1, no cost_hash, no stories)', () => {
  const plain = generateLayout({ profile, seed: 1 });
  const off = generateCandidate({ profile, seed: 1, stories: false });
  assert.equal(json(off.layout), json(plain));
  assert.equal(off.layout.schema, LEVELGEN_SCHEMA);
  assert.equal('cost_hash' in off.layout, false);
  assert.equal(off.layout.buildings.some((b) => b.stories || b.role || b.key_route), false);
});

test('story pass on: schema /2 + cost_hash of ROUTE_COSTS, one story per enterable building, no new validator error', () => {
  const plainErrors = validateGenLayout(generateLayout({ profile, seed: 1 }));
  const { layout, base } = storied(1);
  assert.equal(layout.schema, LEVELGEN_SCHEMA_V2);
  assert.equal(layout.cost_hash, routeCostHash());
  assert.match(layout.cost_hash!, /^[0-9a-f]{8}$/);
  assert.notEqual(routeCostHash({ ...ROUTE_COSTS, n_squad: ROUTE_COSTS.n_squad + 1 }), layout.cost_hash, 'the hash follows the cost table');
  const enterable = layout.buildings.filter((b) => b.enterable && b.role);
  assert.ok(enterable.length > 0);
  for (const b of enterable) {
    assert.equal(b.stories?.length, 1, b.id);
    assert.ok(base[b.id], `${b.id}: pristine copy kept`);
    assert.equal(base[b.id].building.stories, undefined);
  }
  assert.deepEqual(validateGenLayout(layout).filter((e) => !plainErrors.includes(e)), []);
});

test('CLI --stories = the page pass; without it the written candidate is the /1 generator output', () => {
  const run = (extra: string[]) => {
    const out = fs.mkdtempSync(path.join(os.tmpdir(), 'levelgen-stories-'));
    const r = spawnSync(process.execPath, ['src/levelgen/cli/levelgen.ts', '--seed', '1', '--no-sim', '--out', out, ...extra], { cwd: TG, encoding: 'utf8' });
    assert.equal(r.status, 0, r.stderr);
    const name = fs.readdirSync(out).find((f) => f.endsWith('.json') && !f.endsWith('.eval.json'))!;
    const text = fs.readFileSync(path.join(out, name), 'utf8');
    const report = fs.readFileSync(path.join(out, 'report.md'), 'utf8');
    fs.rmSync(out, { recursive: true, force: true });
    return { text, report };
  };
  const on = run(['--stories']);
  assert.equal(on.text, json(storied(1).layout) + '\n');
  assert.match(on.report, /^stories: \d+ buildings/m);
  const off = run([]);
  assert.equal(off.text, json(generateLayout({ profile, seed: 1 })) + '\n');
  assert.doesNotMatch(off.report, /^stories:/m);
});

test('region re-roll + lock: the locked building keeps its story; the regenerated ones get stories from the re-roll seed', () => {
  const s = storied(1);
  const groups = layoutGroups(s.layout).filter((g) => g.category === 'building' && byId(s.layout, g.id).stories?.length);
  const keep = groups[0];
  const [x, y] = keep.centre;
  const locked = addLock(s.layout, shapePolygon({ kind: 'rect', a: [x - 100, y - 100], b: [x + 100, y + 100] }), 'keep');
  assert.ok(lockedBuildingIds(locked).has(keep.id));
  const [w, h] = locked.params.size_m;
  const zone = shapePolygon({ kind: 'rect', a: [locked.bounds.min[0], locked.bounds.min[1]], b: [locked.bounds.min[0] + w * 100, locked.bounds.min[1] + h * 100] });
  const r = regenerateRegion({ layout: locked, zone, seed: 5, profile });
  assert.equal(r.ok, true, r.message);
  const sp = withBuildingStories(r.layout, r.seed, s);
  // the locked building: byte-equal apart from its role (roles are re-assigned on the new map)
  assert.equal(json(withoutRole(byId(sp.layout, keep.id))), json(withoutRole(byId(s.layout, keep.id))));
  assert.ok(sp.base[keep.id], 'its pristine copy carries over');
  // every regenerated enterable building (no story in the re-roll output) now has one, drawn from the re-roll seed's sub-seeds
  const fresh = r.layout.buildings.filter((b) => b.enterable && !b.stories?.length).map((b) => b.id);
  assert.ok(fresh.length > 0, 'something was regenerated');
  for (const id of fresh) {
    const b = byId(sp.layout, id);
    if (!b.role) continue;
    assert.equal(b.stories?.length, 1, id);
    assert.ok(subSeeds(r.seed, id).includes(b.stories![0].seed), `${id}: sub-seed of ${r.seed}`);
  }
  // «другая история» respects the lock
  assert.equal(rerollBuildingStory(sp.layout, keep.id, sp).ok, false);
});

test('«другая история»: one building re-rolled with the next sub-seed, routes recomputed, the rest untouched, deterministic', () => {
  const s = storied(1);
  const target = s.layout.buildings.find((b) => b.stories?.length)!;
  const a = rerollBuildingStory(s.layout, target.id, s), b = rerollBuildingStory(s.layout, target.id, s);
  assert.equal(a.ok, true, a.message);
  assert.equal(json(a.layout), json(b.layout));
  const after = byId(a.layout!, target.id);
  assert.equal(after.stories?.length, 1);
  assert.notEqual(after.stories![0].seed, target.stories![0].seed);
  assert.ok(after.key_route, 'key route recomputed');
  for (const o of s.layout.buildings.filter((x) => x.id !== target.id)) assert.equal(json(withoutRole(byId(a.layout!, o.id))), json(withoutRole(o)), o.id);
  assert.equal(rerollBuildingStory(s.layout, target.id, { base: {}, log: [] }).ok, false, 'no pristine copy -> refused');
});

test('determinism: Depot seed 1 and its x4 (7) -> x6 (8) expansion with stories are byte-equal run to run', () => {
  assert.equal(json(storied(1).layout), json(storied(1).layout));
  const a = chain(), b = chain();
  for (let i = 0; i < a.length; i++) assert.equal(json(a[i].layout), json(b[i].layout), `step ${i}`);
  const x6 = a[2];
  assert.deepEqual(validateGenLayout(x6.layout), []);
  // the old map's buildings keep their stories through both expansions
  for (const old of a[0].layout.buildings.filter((x) => x.stories?.length)) assert.deepEqual(byId(x6.layout, old.id).stories, old.stories, old.id);
  const sum = storyMapSummary(x6.layout, x6.log);
  assert.equal(Object.values(sum.stories).reduce((n, v) => n + (v ?? 0), 0), sum.buildings);
  assert.equal(sum.rollbacks, x6.log.length);
});
