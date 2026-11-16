// Training Ground / level generator v2, stage 2 (board 066): block types in a region re-roll, fence rings whole, saved locks,
// map expansion x2/x4/x6, start + goals. `npm test`. Inputs: the Depot reference profile + generateLayout(seed 1) (pure); the
// accepted maps are READ only; file writes go to an os.tmpdir() folder.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import type { Vec2 } from '../movement/geometry.ts';
import type { TemplateId } from './blockTemplates.ts';
import { buildTemplate, frameOf, lotsOf, pickTemplates, rectCentre, TEMPLATE_IDS, templateContext, templateFits, V2 } from './blockTemplates.ts';
import { checkLayoutRoutes } from './evaluate.ts';
import { saveLocks } from './node/locksFile.ts';
import { generateLayout } from './generate.ts';
import type { Profile } from './profile.ts';
import { runObstacleHeightGate } from './props/obstacleHeightGate.ts';
import { addLock, layoutGroups, polygonsOverlap, regenerateRegion, regenStateOf, selectRegion, shapePolygon, wallFamily, withLocks } from './regionRegen.ts';
import type { GenLayout } from './schema.ts';
import { validateGenLayout } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
const base = generateLayout({ profile, seed: 1 });
const hash = (l: unknown) => crypto.createHash('sha256').update(JSON.stringify(l)).digest('hex');
const rectZone = (a: Vec2, b: Vec2) => shapePolygon({ kind: 'rect', a, b });
const WHOLE = rectZone(base.bounds.min, base.bounds.max);
/** The north-west quarter: the facades, the pipe yard, the compound, the sawtooth hall, the boiler yard, the small office. */
const ZONE_NW = rectZone([500, -9000], [6200, 0]);
const newErrors = (l: GenLayout) => validateGenLayout(l).filter((e) => !validateGenLayout(base).includes(e));
const setPieceAt = (l: GenLayout, lotRect: [Vec2, Vec2]) => l.buildings.filter((b) => { const c = rectCentre(b.footprint); return c[0] >= lotRect[0][0] && c[0] <= lotRect[1][0] && c[1] >= lotRect[0][1] && c[1] <= lotRect[1][1]; }).map((b) => `${b.set_piece}:${b.floors}`).sort().join(',');

// ---- A. block types ---------------------------------------------------------------------------------------------------------

test('v2 lots: the classic grammar gives 15 blocks with their types; stored blocks win over the classic ones', () => {
  const lots = lotsOf(base, profile);
  assert.equal(lots.length, 15);
  assert.equal(lots.find((k) => k.id === 'LOT_compound')?.template, 'one_entry_compound');
  assert.equal(lots.find((k) => k.id === 'LOT_sawtooth')?.template, 'industrial_hall');
  for (const k of lots) assert.ok(k.rect[0][0] < k.rect[1][0] && k.rect[0][1] < k.rect[1][1], `${k.id}: [min, max]`);
  const stored: GenLayout = { ...base, regen: { version: 2, locks: [], rerolls: [], blocks: [{ id: 'LOT_x', rect: [[0, 0], [100, 100]], template: 'facade' }] } };
  assert.deepEqual(lotsOf(stored, profile).map((k) => k.id), ['LOT_x']);
});

test('v2 pickTemplates: only fitting catalog types, never the current one, seeded order', () => {
  const lots = lotsOf(base, profile);
  const frame = frameOf(base);
  for (const lot of lots) {
    const a = pickTemplates(lot, lot.template, 42, frame);
    assert.deepEqual(pickTemplates(lot, lot.template, 42, frame), a, 'deterministic');
    assert.ok(!a.includes(lot.template as TemplateId), `${lot.id}: never its current type`);
    assert.equal(new Set(a).size, a.length);
    assert.ok(a.length <= V2.templates.candidates_per_lot);
  }
  const big = lots.find((k) => k.id === 'LOT_junkyard')!;
  const orders = new Set([1, 2, 3, 4, 5, 6].map((s) => pickTemplates(big, big.template, s, frame)[0]));
  assert.ok(orders.size >= 3, `the first pick varies with the seed: ${[...orders].join(',')}`);
  assert.equal(templateFits('tank_farm', [10, 10]), false);
  assert.equal(templateFits('open_square', [10, 10]), true);
});

test('v2 templates: every catalog type builds in a 40 x 30 m block - heights in the measured classes, inside the block, ids tagged', () => {
  const ctx = templateContext(base, 77);
  const lot = { id: 'LOT_probe', rect: [[-1500, -2000], [1500, 2000]] as [Vec2, Vec2], template: 'none' };
  for (const t of TEMPLATE_IDS) {
    const b = buildTemplate(t, lot, ctx);
    const c = b.content;
    assert.ok(c.zones.length === 1, `${t}: one zone`);
    const fake: GenLayout = { ...base, walls: c.walls, buildings: c.buildings, covers: c.covers, ladders: c.ladders, mantle_edges: c.mantle_edges };
    assert.deepEqual(runObstacleHeightGate(fake).errors, [], `${t}: heights`);
    const inLot = (p: Vec2) => p[0] >= lot.rect[0][0] - 1 && p[0] <= lot.rect[1][0] + 1 && p[1] >= lot.rect[0][1] - 1 && p[1] <= lot.rect[1][1] + 1;
    for (const x of c.buildings) assert.ok(inLot(x.footprint[0]) && inLot(x.footprint[1]), `${t}: building ${x.id} inside the block`);
    for (const w of c.walls) assert.ok(inLot(w.a) && inLot(w.b), `${t}: wall ${w.id} inside the block`);
    for (const x of [...c.buildings, ...c.walls, ...c.covers]) assert.ok(x.id.includes('probe_t77'), `${t}: ${x.id} tagged`);
    if (b.visit) {
      assert.equal(b.visit.points.length, b.visit.layers.length);
      for (const p of b.visit.points) assert.ok(inLot(p), `${t}: visit inside the block`);
    }
    assert.equal(hash(buildTemplate(t, lot, ctx)), hash(b), `${t}: deterministic`);
  }
});

test('v2 re-roll A: the blocks in the zone change TYPE over 8 seeds (not only covers); the rest of the map is byte-identical', () => {
  const lots = lotsOf(base, profile);
  const typesSeen = new Map<string, Set<string>>();
  const { plan } = selectRegion(base, ZONE_NW, profile);
  assert.ok(plan.selLots.length >= 5, `blocks in the NW zone: ${plan.selLots.map((k) => k.id).join(',')}`);
  for (let seed = 1; seed <= 8; seed++) {
    const r = regenerateRegion({ layout: base, zone: ZONE_NW, seed: seed * 101, profile });
    assert.equal(r.ok, true, r.message);
    assert.ok(r.retemplated.length >= plan.selLots.length - 1, `seed ${seed}: re-typed ${r.retemplated.length} of ${plan.selLots.length}`);
    for (const x of r.retemplated) {
      assert.notEqual(x.from, x.to, `${x.id}: another type`);
      if (!typesSeen.has(x.id)) typesSeen.set(x.id, new Set());
      typesSeen.get(x.id)!.add(x.to);
      const lot = lots.find((k) => k.id === x.id)!;
      if (setPieceAt(base, lot.rect) || setPieceAt(r.layout, lot.rect)) assert.notEqual(setPieceAt(r.layout, lot.rect), setPieceAt(base, lot.rect), `${x.id}: the buildings changed`);
    }
    // the stored block types follow the re-roll
    const blocks = r.layout.regen!.blocks!;
    for (const x of r.retemplated) assert.equal(blocks.find((k) => k.id === x.id)?.template, x.to);
    // outside the plan nothing moved: every old object whose centre is out of scope is still there, the same JSON
    const sel = new Set(selectRegion(base, ZONE_NW, profile).selection.inside.map((g) => g.key));
    const out = layoutGroups(base).filter((g) => !sel.has(g.key));
    const json = new Set([...r.layout.walls, ...r.layout.buildings, ...r.layout.covers, ...r.layout.ladders, ...r.layout.zones].map((x) => JSON.stringify(x)));
    for (const g of out) for (const x of [...(g.members.walls ?? []), ...(g.members.buildings ?? []), ...(g.members.covers ?? []), ...(g.members.zones ?? [])]) assert.ok(json.has(JSON.stringify(x)), `${g.key} kept`);
    assert.deepEqual(r.layout.cut, base.cut);
    assert.ok(checkLayoutRoutes(r.layout).find((x) => x.mandatory)!.completable, `seed ${seed}: mandatory route`);
    assert.deepEqual(newErrors(r.layout), [], `seed ${seed}: no new schema errors`);
  }
  for (const [id, set] of typesSeen) assert.ok(set.size >= 2, `${id}: several types over 8 seeds (${[...set].join(',')})`);
});

test('v2 re-roll A: new blocks never overlap roads / the canal; their obstacle heights are measured classes; ids stay unique', () => {
  for (const seed of [5, 17, 29]) {
    const r = regenerateRegion({ layout: base, zone: WHOLE, seed, profile });
    assert.equal(r.ok, true, r.message);
    assert.ok(r.retemplated.length >= 10, `whole map: ${r.retemplated.length} blocks re-typed`);
    const roads = [...base.zones.filter((z) => z.id.startsWith('Z_ST_')).map((z) => z.poly), base.cut!.poly];
    const added = new Set(r.added);
    const shrunk = (f: [Vec2, Vec2]) => rectZone([f[0][0] + 20, f[0][1] + 20], [f[1][0] - 20, f[1][1] - 20]);
    for (const b of r.layout.buildings.filter((x) => added.has(x.id))) assert.ok(roads.every((p) => !polygonsOverlap(shrunk(b.footprint), p)), `${b.id} blocks no road`);
    const fresh: GenLayout = { ...r.layout, walls: r.layout.walls.filter((w) => added.has(w.id) || (w.building !== undefined && added.has(w.building)) || [...added].some((a) => w.id.startsWith(`${a}_`))), covers: r.layout.covers.filter((c) => added.has(c.id)), buildings: r.layout.buildings.filter((b) => added.has(b.id)), mantle_edges: [] };
    assert.deepEqual(runObstacleHeightGate(fresh).errors, []);
    for (const k of ['buildings', 'walls', 'ladders', 'covers', 'zones'] as const) assert.equal(new Set((r.layout[k] as { id: string }[]).map((x) => x.id)).size, r.layout[k].length, `${k} ids unique`);
    assert.deepEqual(newErrors(r.layout), []);
  }
});

test('v2 re-roll A: a locked block is untouched byte-for-byte; the anchor block (the route end) keeps its type', () => {
  const lot = lotsOf(base, profile).find((k) => k.id === 'LOT_compound')!;
  const lockPoly = rectZone([lot.rect[0][0] - 150, lot.rect[0][1] - 150], [lot.rect[1][0] + 150, lot.rect[1][1] + 150]);
  const locked = addLock(base, lockPoly, 'Двор');
  const inLock = (p: Vec2) => p[0] >= lockPoly[0][0] && p[0] <= lockPoly[2][0] && p[1] >= lockPoly[0][1] && p[1] <= lockPoly[2][1];
  const keptBefore = JSON.stringify([...base.walls.filter((w) => inLock([(w.a[0] + w.b[0]) / 2, (w.a[1] + w.b[1]) / 2])), ...base.buildings.filter((b) => inLock(rectCentre(b.footprint)))]);
  for (const seed of [3, 4]) {
    const r = regenerateRegion({ layout: locked, zone: WHOLE, seed, profile });
    assert.equal(r.ok, true, r.message);
    assert.ok(!r.retemplated.some((x) => x.id === 'LOT_compound'), 'the locked block keeps its type');
    const after = JSON.stringify([...r.layout.walls.filter((w) => inLock([(w.a[0] + w.b[0]) / 2, (w.a[1] + w.b[1]) / 2])), ...r.layout.buildings.filter((b) => inLock(rectCentre(b.footprint)))]);
    assert.equal(after, keptBefore, 'the objects in the lock are byte-identical');
    assert.ok(r.anchored.includes('LOT_swOpen'), 'the dead-end square (the route end) is an anchor');
    assert.deepEqual(r.layout.regen?.locks, locked.regen?.locks);
  }
});

test('v2 re-roll A: deterministic (same map + zone + seed -> same hash); a second re-roll changes the types again', () => {
  const a = regenerateRegion({ layout: base, zone: ZONE_NW, seed: 909, profile });
  const b = regenerateRegion({ layout: base, zone: ZONE_NW, seed: 909, profile });
  assert.equal(hash(a.layout), hash(b.layout));
  const c = regenerateRegion({ layout: a.layout, zone: ZONE_NW, seed: 910, profile });
  assert.equal(c.ok, true, c.message);
  for (const x of c.retemplated) assert.equal(x.from, a.retemplated.find((y) => y.id === x.id)?.to ?? x.from, `${x.id}: from = the type the first re-roll gave`);
  assert.ok(c.retemplated.every((x) => x.from !== x.to));
  // reloaded from JSON (what «Принять» / «Сохранить замки» write) -> the same result
  const reloaded = JSON.parse(JSON.stringify(a.layout)) as GenLayout;
  assert.equal(hash(regenerateRegion({ layout: reloaded, zone: ZONE_NW, seed: 910, profile }).layout), hash(c.layout));
  console.log(`# v2 determinism: NW seed 909 sha256 ${hash(a.layout)}`);
});

test('v2 re-roll jitter mode = the stage-1 behaviour: no block changes type', () => {
  const r = regenerateRegion({ layout: base, zone: ZONE_NW, seed: 101, profile, blocks: 'jitter' });
  assert.equal(r.ok, true, r.message);
  assert.deepEqual(r.retemplated, []);
  assert.equal(r.layout.regen?.blocks, undefined);
});

// ---- B. fence rings whole ---------------------------------------------------------------------------------------------------

test('v2 fences: every fence ring is one re-roll group; a zone border through a fence never leaves a gap', () => {
  const groups = layoutGroups(base);
  const fam = groups.find((g) => g.id === 'F_compound')!;
  assert.ok(fam && fam.category === 'wall' && fam.members.walls!.length >= 4, 'the compound fence is one group');
  assert.ok(base.walls.filter((w) => w.id.startsWith('F_compound_')).every((w) => wallFamily(w) === 'F_compound'));
  assert.equal(wallFamily(base.walls.find((w) => w.id === 'F_south')!), undefined);
  // zones cutting through the compound fence at several places (stage-1 jitter re-roll: the old open item)
  const lot = lotsOf(base, profile).find((k) => k.id === 'LOT_compound')!;
  const [mn, mx] = lot.rect;
  for (const fx of [0.3, 0.5, 0.7]) {
    const cut = mn[1] + (mx[1] - mn[1]) * fx;
    for (const seed of [11, 12]) {
      const r = regenerateRegion({ layout: base, zone: rectZone([mn[0] - 2000, mn[1] - 2000], [mx[0] + 2000, cut]), seed, profile, blocks: 'jitter' });
      assert.equal(r.ok, true, r.message);
      // the ring is whole: as many segments and the same fenced length (perimeter minus its one gate) as before - only the gate moved
      const len = (ws: GenLayout['walls']) => Math.round(ws.reduce((a, w) => a + Math.hypot(w.b[0] - w.a[0], w.b[1] - w.a[1]), 0));
      const now = r.layout.walls.filter((w) => w.id.startsWith('F_compound_'));
      const old = base.walls.filter((w) => wallFamily(w) === 'F_compound');
      assert.equal(now.length, old.length, `cut at ${fx}, seed ${seed}: segments`);
      assert.ok(Math.abs(len(now) - len(old)) <= 2, `cut at ${fx}, seed ${seed}: fenced length ${len(now)} vs ${len(old)} (no gap)`);
    }
  }
});

test('v2 schema: regen version 2 with blocks validates; a version-1 file still loads', () => {
  const r = regenerateRegion({ layout: base, zone: ZONE_NW, seed: 1, profile });
  assert.equal(r.layout.regen?.version, 2);
  assert.deepEqual(newErrors(r.layout), []);
  const v1: GenLayout = { ...base, regen: { version: 1, locks: [], rerolls: [] } };
  assert.deepEqual(newErrors(v1), []);
  assert.equal(regenStateOf(v1).version, 2);
});

test('v2 save locks: an accepted map gets only regen.locks (polygons); without one the page layout is saved as a draft', () => {
  const src = path.join(ROOT, 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json');
  const before = hash(fs.readFileSync(src, 'utf8'));
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'levelgen-v2-'));
  try {
    const acc = path.join(tmp, 'accepted'), drafts = path.join(tmp, 'drafts');
    fs.mkdirSync(acc);
    const original = JSON.parse(fs.readFileSync(src, 'utf8')) as GenLayout;
    fs.writeFileSync(path.join(acc, `${original.name}.json`), JSON.stringify(original, null, 1) + '\n');
    // the page: the accepted map re-rolled in the NW, then two districts locked
    const page0 = regenerateRegion({ layout: original, zone: ZONE_NW, seed: 5, profile }).layout;
    const page = addLock(addLock(page0, rectZone([-6000, 2000], [-2000, 9000]), 'Руина'), [[1000, -8000], [5000, -8000], [3000, -4000]], 'Треугольник');
    const r = saveLocks(page, { accepted: acc, drafts });
    assert.equal(r.target, 'accepted');
    assert.equal(r.locks, 2);
    const saved = JSON.parse(fs.readFileSync(r.file, 'utf8')) as GenLayout;
    assert.deepEqual(saved.regen?.locks, page.regen?.locks, 'the locks are stored as polygons');
    assert.deepEqual(saved.regen?.locks[1].poly, [[1000, -8000], [5000, -8000], [3000, -4000]]);
    const strip = (l: GenLayout) => JSON.stringify({ ...l, regen: undefined });
    assert.equal(strip(saved), strip(original), 'the accepted map itself is unchanged (the re-roll is saved only by «Принять»)');
    assert.deepEqual(withLocks(original, []).regen?.locks, []);
    // no accepted map of that name -> a draft with the whole layout
    const d = saveLocks({ ...page, name: 'draft_probe' }, { accepted: acc, drafts });
    assert.equal(d.target, 'draft');
    assert.equal(hash(JSON.parse(fs.readFileSync(d.file, 'utf8'))), hash({ ...page, name: 'draft_probe' }));
    assert.throws(() => saveLocks({ ...page, name: '../evil' }, { accepted: acc, drafts }));
  } finally { fs.rmSync(tmp, { recursive: true, force: true }); }
  assert.equal(hash(fs.readFileSync(src, 'utf8')), before, 'the real accepted map is never written');
});
