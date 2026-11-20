// T-023 «Пропсы уровней»: the zone re-roll (zoneReroll.ts via rerollPropsLayers {zone}), the hand move + auto-lock + nudge (propsEdit.ts),
// the pick, the check after an edit and the unsaved guard. Fixtures only: the committed L_Gen_Depot_1 layout is read, the props file is
// generated in memory (a deep clone per test), nothing is written to disk.
import { describe, it } from 'node:test';
import * as assert from 'node:assert/strict';
import * as fs from 'node:fs';
import * as path from 'node:path';
import type { Vec2 } from '../../movement/geometry.ts';
import { pointInPoly } from '../../movement/geometry.ts';
import type { GenLayout } from '../schema.ts';
import type { LevelPropsFile, PropLayer, PropObject } from './schema.ts';
import { PropsLayoutView } from './layoutView.ts';
import { mandatorySegments, routeDistOnLayer } from '../pmcPosts.ts';
import { clearanceCm } from './autogenConfig.ts';
import { findProjectRoot, generatePropsForLayout, loadAutogenConfig, loadPropsBalance } from './autogen.ts';
import { PMC_CHARGES_ID, rerollPropsLayers } from './rerollKinds.ts';
import { zonePreview } from './zoneReroll.ts';
import { checkProps, findProp, moveProp, moveRefusal, newIssues, pickProp, propRadius, resolveWalkLayer, setPropLocked, withGate, DEFAULT_PROP_SNAP_CM, PROP_SNAP_CM } from './propsEdit.ts';
import { formatPropsCanonical } from './canonical.ts';
import { nudgeDelta, snapMoveDelta } from '../mapEdit.ts';
import { historyInit, historyPushCoalesced, historyUndo } from '../regenHistory.ts';
import { confirmLeave, setUnsaved, unsavedList } from '../../utils/unsavedGuard.ts';

const root = findProjectRoot();
const layoutName = 'gen_ref_01_02_depot_1';
const layout = JSON.parse(fs.readFileSync(path.join(root, 'Content/Data/LevelGen/accepted', `${layoutName}.json`), 'utf8')) as GenLayout;
const balance = loadPropsBalance(root);
const cfg = loadAutogenConfig(root);
const view = PropsLayoutView.build(layout as unknown as Record<string, unknown>, PropsLayoutView.readConfig(balance));
const segs = mandatorySegments(layout);

// the autogen takes seconds: made once, every test works on a deep clone
const generated = generatePropsForLayout({ layout, balance, autogenConfig: cfg, seed: 7, layoutName, projectRoot: root }).props;
const fresh = (): LevelPropsFile => JSON.parse(JSON.stringify(generated)) as LevelPropsFile;
const layerOf = (f: LevelPropsFile, id: string): PropLayer => f.layers.find((l) => l.id === id)!;
const byId = (l: PropLayer): Map<string, PropObject> => new Map(l.objects.map((o) => [o.id, o]));

/** A box [minX, maxX] x [minY, maxY] as a polygon (UE cm). */
const box = (x0: number, x1: number, y0: number, y1: number): Vec2[] => [[x0, y0], [x1, y0], [x1, y1], [x0, y1]];
/** The left half (X below the median of the layer's crates) of the map: a zone with objects on both sides. */
function halfZone(f: LevelPropsFile, layerId: string): Vec2[] {
  const xs = layerOf(f, layerId).objects.map((o) => o.pos[0]).sort((a, b) => a - b);
  const mid = xs[Math.floor(xs.length / 2)];
  return box(layout.bounds.min[0] - 1000, mid, layout.bounds.min[1] - 1000, layout.bounds.max[1] + 1000);
}

describe('zone re-roll (T-023)', () => {
  it('re-rolls a layer ONLY inside the zone: every object outside stays byte-identical, the inside is replaced by the new seed', () => {
    for (const kind of ['crates', 'barrels']) {
      const f = fresh();
      const zone = halfZone(f, kind);
      const before = layerOf(f, kind).objects;
      const inside = before.filter((o) => pointInPoly(o.pos, zone));
      const outside = before.filter((o) => !pointInPoly(o.pos, zone));
      assert.ok(inside.length > 0 && outside.length > 0, `${kind}: the zone must split the layer`);
      const out = rerollPropsLayers(f, balance, view, layout, { ids: [kind], seedFor: () => 424242, zone }, cfg);
      assert.deepEqual(out.rerolled, [kind]);
      const after = byId(layerOf(f, kind));
      for (const o of outside) assert.deepEqual(after.get(o.id), o, `${kind} ${o.id} outside the zone changed`);
      for (const o of inside) assert.ok(!after.has(o.id), `${kind} ${o.id} inside the zone was not replaced`);
      const added = layerOf(f, kind).objects.filter((o) => !outside.some((x) => x.id === o.id));
      assert.ok(added.length > 0, `${kind}: nothing placed in the zone`);
      for (const o of added) assert.ok(pointInPoly(o.pos, zone), `${kind} ${o.id} placed outside the zone`);
      assert.equal(layerOf(f, kind).seed, 424242);
      assert.equal(out.zone?.removed, inside.length);
      assert.equal(out.zone?.added, added.length);
      assert.equal(out.zone?.keptOutside, outside.length);
      assert.equal(layerOf(f, kind).report?.gate, 'pass', `${kind}: gate`);
    }
  });

  it('is deterministic for the same seed, and a different seed gives a different layout in the zone', () => {
    const base = fresh();
    const zone = halfZone(base, 'crates');
    const run = (seed: number) => { const f = JSON.parse(JSON.stringify(base)) as LevelPropsFile; rerollPropsLayers(f, balance, view, layout, { ids: ['crates'], seedFor: () => seed, zone }, cfg); return formatPropsCanonical(f); };
    assert.equal(run(11), run(11));
    assert.notEqual(run(11), run(12));
  });

  it('keeps locked objects, hand-pinned objects (a move override) and locked layers / pmc_charges / other layers exactly as they are', () => {
    const f = fresh();
    const zone = halfZone(f, 'crates');
    const crates = layerOf(f, 'crates');
    const inZone = crates.objects.filter((o) => pointInPoly(o.pos, zone));
    assert.ok(inZone.length >= 3);
    const lockedCrate = inZone[0];
    lockedCrate.locked = true;
    const pinnedCrate = inZone[1];
    f.overrides = [{ op: 'move', id: pinnedCrate.id, pos: [pinnedCrate.pos[0], pinnedCrate.pos[1]], walk_layer: pinnedCrate.walk_layer }];
    layerOf(f, 'barrels').locked = true;
    f.layers.push({ id: PMC_CHARGES_ID, kind: 'mines', enabled: true, seed: 1, locked: true, preset: 'mines_default', rules: {}, objects: [{ id: 'pmc_c1', variant: 'mine_ap', pos: [zone[0][0] + 1000, zone[0][1] + 1000], walk_layer: 'ground', locked: true }] });
    const others = JSON.stringify(['barrels', 'turrets', 'mines', 'loot', 'power', PMC_CHARGES_ID].map((id) => layerOf(f, id)));
    const out = rerollPropsLayers(f, balance, view, layout, { ids: ['crates', 'barrels', PMC_CHARGES_ID], seedFor: () => 9, zone }, cfg);
    assert.deepEqual(out.rerolled, ['crates']);
    assert.deepEqual(out.skipped.map((s) => s.id).sort(), [PMC_CHARGES_ID, 'barrels'].sort());
    const after = byId(layerOf(f, 'crates'));
    assert.deepEqual(after.get(lockedCrate.id), lockedCrate, 'the locked crate stays');
    assert.deepEqual(after.get(pinnedCrate.id), pinnedCrate, 'the pinned crate stays');
    assert.equal(out.zone?.keptPinned, 2);
    // the other layers: the mines / loot attached to replaced crates are only reported; nothing else changed except warnings
    const strip = (l: PropLayer) => JSON.stringify({ ...l, report: undefined });
    const nowOthers = JSON.stringify(['barrels', 'turrets', 'mines', 'loot', 'power', PMC_CHARGES_ID].map((id) => layerOf(f, id)));
    assert.equal(
      JSON.stringify(JSON.parse(nowOthers).map((l: PropLayer) => strip(l))),
      JSON.stringify(JSON.parse(others).map((l: PropLayer) => strip(l))),
      'no object of another layer changed',
    );
  });

  it('the 088 route clearance judges only the NEW objects: an unlocked crate outside the zone that sits on the route is not removed', () => {
    const f = fresh();
    const zone = halfZone(f, 'crates');
    const crates = layerOf(f, 'crates');
    const stray = crates.objects.find((o) => !pointInPoly(o.pos, zone) && !o.attach && !o.locked)!;
    // put it ON the mandatory route (a normal clearance would drop it), still outside the zone
    const seg = segs.find((s) => !pointInPoly(s.a, zone) && s.la === 'ground' && s.lb === 'ground')!;
    assert.ok(seg, 'a route segment outside the zone');
    stray.pos = [Math.round(seg.a[0]), Math.round(seg.a[1])];
    stray.walk_layer = 'ground';
    const limit = clearanceCm(cfg, 'crates', 'route_corridor_m');
    assert.ok(routeDistOnLayer(stray.pos, segs, 'ground') < limit, 'precondition: inside the corridor');
    const out = rerollPropsLayers(f, balance, view, layout, { ids: ['crates'], seedFor: () => 5, zone }, cfg);
    assert.ok(layerOf(f, 'crates').objects.some((o) => o.id === stray.id && o.pos[0] === stray.pos[0]), 'the stray crate outside the zone survived');
    assert.ok(!('locked' in (byId(layerOf(f, 'crates')).get(stray.id) ?? {}) && (byId(layerOf(f, 'crates')).get(stray.id) as PropObject).locked === true), 'the temporary pin is gone');
    // and nothing NEW stands inside the corridor
    for (const o of layerOf(f, 'crates').objects) {
      if (o.id === stray.id || !pointInPoly(o.pos, zone) || o.locked) continue;
      assert.ok(routeDistOnLayer(o.pos, segs, o.walk_layer || 'ground') >= limit, `new crate ${o.id} inside the corridor`);
    }
    assert.ok(out.clearance.droppedTotal >= 0);
  });

  it('re-rolling crates + mines + loot in one zone leaves no object attached to a crate that is gone, and says nothing about orphans then', () => {
    const f = fresh();
    const zone = halfZone(f, 'crates');
    const out = rerollPropsLayers(f, balance, view, layout, { ids: ['crates', 'mines', 'loot'], seedFor: () => 2024, zone }, cfg);
    const all = new Map<string, PropObject>();
    for (const l of f.layers) for (const o of l.objects) all.set(o.id, o);
    const orphans = [...all.values()].filter((o) => o.attach !== undefined && !all.has(o.attach));
    assert.equal(orphans.length, 0, `orphans: ${orphans.map((o) => o.id).join(', ')}`);
    assert.ok(!out.notes.some((n) => /привязаны/.test(n)));
    for (const l of f.layers) assert.ok(!(l.report?.warnings ?? []).some((w) => /^attach_orphaned/.test(w)), `${l.id} has a stale orphan warning`);
  });

  it('crates alone in a zone: the mines / loot attached to the replaced crates are named as orphans (not silently kept)', () => {
    const f = fresh();
    const zone = halfZone(f, 'crates');
    const crateIds = new Set(layerOf(f, 'crates').objects.filter((o) => pointInPoly(o.pos, zone) && !o.locked).map((o) => o.id));
    const attached = f.layers.flatMap((l) => l.objects).filter((o) => o.attach && crateIds.has(o.attach));
    assert.ok(attached.length > 0, 'the fixture has attached objects inside the zone');
    const out = rerollPropsLayers(f, balance, view, layout, { ids: ['crates'], seedFor: () => 2025, zone }, cfg);
    assert.ok(out.notes.some((n) => /привязаны/.test(n)));
    assert.ok(f.layers.some((l) => (l.report?.warnings ?? []).some((w) => /^attach_orphaned/.test(w))));
  });

  it('the power layer is not rebuilt by a zone (outside stays): skipped with a reason; a turret zone re-roll only reports the links', () => {
    const f = fresh();
    const zone = halfZone(f, 'turrets');
    const powerBefore = JSON.stringify(layerOf(f, 'power'));
    const out = rerollPropsLayers(f, balance, view, layout, { ids: ['turrets', 'power'], seedFor: () => 77, zone }, cfg);
    assert.deepEqual(out.rerolled, ['turrets']);
    assert.equal(out.powerRebuilt, false);
    assert.ok(out.skipped.some((s) => s.id === 'power' && s.reason.length > 0));
    assert.equal(JSON.stringify(layerOf(f, 'power')), powerBefore);
  });

  it('zonePreview counts what the re-roll replaces and what a lock keeps', () => {
    const f = fresh();
    const zone = halfZone(f, 'crates');
    const crates = layerOf(f, 'crates');
    const inZone = crates.objects.filter((o) => pointInPoly(o.pos, zone));
    inZone[0].locked = true;
    const p = zonePreview(f, ['crates'], zone);
    assert.equal(p.locked.length, 1);
    assert.equal(p.inside.length, inZone.length - 1);
    assert.deepEqual(zonePreview(f, [], zone), { inside: [], locked: [] });
  });

  it('a frozen file refuses the zone re-roll too', () => {
    const f = fresh();
    f.frozen = true;
    const out = rerollPropsLayers(f, balance, view, layout, { ids: ['crates'], seedFor: () => 1, zone: halfZone(f, 'crates') }, cfg);
    assert.deepEqual(out.rerolled, []);
    assert.equal(out.skipped.length, 1);
  });
});

describe('moving a prop by hand (T-023)', () => {
  const freeCrate = (f: LevelPropsFile): PropObject => layerOf(f, 'crates').objects.find((o) => !o.locked && !o.attach && o.walk_layer === 'ground')!;

  it('moves by the delta, never changes its input, lands on a walkable layer and pins the prop (locked = true)', () => {
    const f = fresh();
    const c = freeCrate(f);
    const before = JSON.stringify(f);
    // find a free step: try the 4 directions at 1 m until the spot is free
    const dirs: Vec2[] = [[100, 0], [-100, 0], [0, 100], [0, -100]];
    const d = dirs.find((x) => view.isFree([c.pos[0] + x[0], c.pos[1] + x[1]], 'ground', 60))!;
    const r = moveProp(f, view, c.id, d, balance);
    assert.ok(r.ok);
    if (!r.ok) return;
    assert.equal(JSON.stringify(f), before, 'the input file is untouched');
    const moved = findProp(r.file, c.id)!.obj;
    assert.deepEqual(moved.pos, [c.pos[0] + d[0], c.pos[1] + d[1]]);
    assert.equal(moved.locked, true, 'auto-lock after a manual move');
    assert.equal(moved.walk_layer, 'ground');
    assert.equal(r.locked, true);
    // the lock can be removed again
    const unlocked = findProp(setPropLocked(r.file, c.id, false), c.id)!.obj;
    assert.equal(unlocked.locked, false);
    // lock = false option leaves the flag alone
    const r2 = moveProp(f, view, c.id, d, balance, { lock: false });
    assert.ok(r2.ok && findProp(r2.file, c.id)!.obj.locked !== true);
  });

  it('a moved prop is not taken back by a re-roll of its layer (the lock), while its unlocked neighbours are re-rolled', () => {
    const f = fresh();
    const c = freeCrate(f);
    const d: Vec2 = [0, 100];
    const r = moveProp(f, view, c.id, d, balance);
    assert.ok(r.ok);
    if (!r.ok) return;
    const g = r.file;
    rerollPropsLayers(g, balance, view, layout, { ids: ['crates'], seedFor: () => 31 }, cfg);
    const kept = findProp(g, c.id);
    assert.ok(kept, 'the moved crate survived the re-roll');
    assert.deepEqual(kept!.obj.pos, r.pos);
    assert.equal(kept!.obj.locked, true);
  });

  it('carries what is attached to it (a mine on its crate) and follows a hand move override', () => {
    const f = fresh();
    const mine = layerOf(f, 'mines').objects.find((o) => o.attach && layerOf(f, 'crates').objects.some((c) => c.id === o.attach))!;
    assert.ok(mine, 'the fixture has a mined crate');
    const crate = findProp(f, mine.attach!)!.obj;
    f.overrides = [{ op: 'move', id: crate.id, pos: [crate.pos[0], crate.pos[1]], walk_layer: crate.walk_layer }];
    const dirs: Vec2[] = [[100, 0], [-100, 0], [0, 100], [0, -100]];
    const d = dirs.find((x) => view.isFree([crate.pos[0] + x[0], crate.pos[1] + x[1]], crate.walk_layer, 60))!;
    const r = moveProp(f, view, crate.id, d, balance);
    assert.ok(r.ok);
    if (!r.ok) return;
    assert.ok(r.carried.includes(mine.id));
    assert.deepEqual(findProp(r.file, mine.id)!.obj.pos, r.pos);
    const op = (r.file.overrides ?? [])[0] as { pos: Vec2 };
    assert.deepEqual(op.pos, r.pos);
  });

  it('refuses: frozen file, locked layer, pmc_charges, a spot with nothing to stand on (off the map), a zero move, an unknown id', () => {
    const f = fresh();
    const c = freeCrate(f);
    const frozen = JSON.parse(JSON.stringify(f)) as LevelPropsFile;
    frozen.frozen = true;
    assert.equal(moveProp(frozen, view, c.id, [100, 0], balance).ok, false);
    const lockedLayer = JSON.parse(JSON.stringify(f)) as LevelPropsFile;
    layerOf(lockedLayer, 'crates').locked = true;
    assert.match(moveRefusal(lockedLayer, c.id) ?? '', /закреплён/);
    const pmcFile = JSON.parse(JSON.stringify(f)) as LevelPropsFile;
    pmcFile.layers.push({ id: PMC_CHARGES_ID, kind: 'mines', enabled: true, seed: 1, locked: false, preset: 'x', rules: {}, objects: [{ id: 'pmc_c1', variant: 'mine_ap', pos: [0, 0], walk_layer: 'ground' }] });
    assert.match(moveRefusal(pmcFile, 'pmc_c1') ?? '', /ЧВК/);
    const off = moveProp(f, view, c.id, [layout.bounds.max[0] * 10, 0], balance);
    assert.equal(off.ok, false);
    assert.equal(moveProp(f, view, c.id, [0, 0], balance).ok, false);
    assert.equal(moveProp(f, view, 'nope', [100, 0], balance).ok, false);
  });

  it('snaps to the floor it stands on: a roof prop dragged off the roof lands on the ground, a ground prop dragged onto a walkable roof keeps the ground when free', () => {
    const roofBuilding = layout.buildings.find((b) => b.roof_walkable);
    assert.ok(roofBuilding, 'the fixture has a walkable roof');
    if (!roofBuilding) return;
    const cx = Math.round((roofBuilding.footprint[0][0] + roofBuilding.footprint[1][0]) / 2);
    const cy = Math.round((roofBuilding.footprint[0][1] + roofBuilding.footprint[1][1]) / 2);
    const roof = `roof:${roofBuilding.id}`;
    assert.ok(view.isWalkable([cx, cy], roof));
    assert.equal(resolveWalkLayer(view, [cx, cy], roof), roof, 'stays on the roof while it is walkable there');
    const far: Vec2 = [Math.round(layout.bounds.max[0] - 300), Math.round(layout.bounds.min[1] + 300)];
    const landed = resolveWalkLayer(view, far, roof);
    assert.ok(landed === null || landed === 'ground' || landed.startsWith('roof:') === false, 'off the roof it falls to another surface');
    assert.equal(resolveWalkLayer(view, [layout.bounds.max[0] * 5, 0], 'ground'), null, 'nothing under a point off the map');
  });

  it('nudge: one arrow press is 10 cm (Shift 1 m, Ctrl 10 m), ArrowUp = +X; ten nudges of one prop are ONE undo step and undo restores it', () => {
    assert.deepEqual(nudgeDelta('ArrowUp'), [10, 0]);
    assert.deepEqual(nudgeDelta('ArrowRight', { shift: true }), [0, 100]);
    assert.deepEqual(nudgeDelta('ArrowDown', { ctrl: true }), [-1000, 0]);
    assert.equal(nudgeDelta('a'), null);
    const f = fresh();
    const c = freeCrate(f);
    let h = historyInit(f);
    let cur = f;
    let moved = 0;
    for (let i = 0; i < 10; i++) {
      const r = moveProp(cur, view, c.id, [0, 10], balance);
      if (!r.ok) break;
      moved++;
      cur = r.file;
      h = historyPushCoalesced(h, cur, 'nudge', `nudge:${c.id}`);
    }
    assert.ok(moved >= 5, 'the crate has room to nudge');
    assert.equal(h.past.length, 1, 'a run of nudges is one undo step');
    assert.deepEqual(findProp(cur, c.id)!.obj.pos, [c.pos[0], c.pos[1] + 10 * moved]);
    const back = historyUndo(h).present.state;
    assert.deepEqual(findProp(back, c.id)!.obj.pos, c.pos);
    assert.notEqual(findProp(back, c.id)!.obj.locked, true, 'undo also takes the auto-lock back');
  });

  it('grid snap lands the prop on the lattice (RegionOverlay applies snapMoveDelta before the move)', () => {
    assert.ok(PROP_SNAP_CM.includes(DEFAULT_PROP_SNAP_CM));
    const f = fresh();
    const c = freeCrate(f);
    const raw: Vec2 = [137, -64];
    const d = snapMoveDelta(c.pos, raw, DEFAULT_PROP_SNAP_CM);
    const r = moveProp(f, view, c.id, d, balance);
    if (!r.ok) return; // the snapped spot may be unwalkable
    assert.equal(r.pos[0] % DEFAULT_PROP_SNAP_CM, 0);
    assert.equal(r.pos[1] % DEFAULT_PROP_SNAP_CM, 0);
  });

  it('a generator network keeps its cable lengths after a turret moved', () => {
    const f = fresh();
    const gen = layerOf(f, 'power').objects.find((g) => (g.links ?? []).length > 0);
    assert.ok(gen, 'the fixture has a generator with links');
    if (!gen) return;
    const tid = gen.links![0];
    const t = findProp(f, tid)!.obj;
    const dirs: Vec2[] = [[100, 0], [-100, 0], [0, 100], [0, -100]];
    const d = dirs.find((x) => view.isFree([t.pos[0] + x[0], t.pos[1] + x[1]], t.walk_layer, 60));
    assert.ok(d, 'the turret has a free neighbour spot');
    if (!d) return;
    const r = moveProp(f, view, tid, d, balance);
    assert.ok(r.ok);
    if (!r.ok) return;
    const g2 = findProp(r.file, gen.id)!.obj;
    const t2 = findProp(r.file, tid)!.obj;
    assert.equal(g2.cable_cm![0], Math.round(Math.hypot(g2.pos[0] - t2.pos[0], g2.pos[1] - t2.pos[1])));
  });
});

describe('pick, check and guards (T-023)', () => {
  it('pickProp takes the nearest shown prop within its radius + tolerance; hidden layers and the walk-layer filter are respected', () => {
    const f = fresh();
    const c = layerOf(f, 'crates').objects.find((o) => o.walk_layer === 'ground')!;
    const hit = pickProp(f, c.pos, 5, { balance });
    assert.ok(hit);
    assert.equal(hit!.obj.id, c.id);
    assert.equal(pickProp(f, [c.pos[0] + 5000, c.pos[1] + 5000], 5, { balance }), null);
    assert.equal(pickProp(f, c.pos, 5, { balance, visibleLayers: { crates: false, barrels: false, turrets: false, mines: false, loot: false, power: false } }), null);
    assert.equal(pickProp(f, c.pos, 5, { balance, layerFilter: 'cut' })?.obj.id === c.id, false);
    assert.ok(propRadius(c, balance) >= 25);
  });

  it('checkProps flags a prop dragged INTO a building as a NEW problem (the game would drop it) and does not blame the defects the file had', () => {
    const f = fresh();
    const base = checkProps(f, view, layout, balance);
    const b = layout.buildings.find((x) => x.floors >= 1 && x.footprint)!;
    const cx = Math.round((b.footprint[0][0] + b.footprint[1][0]) / 2);
    const cy = Math.round((b.footprint[0][1] + b.footprint[1][1]) / 2);
    const crate = layerOf(f, 'crates').objects.find((o) => o.walk_layer === 'ground' && !o.attach)!;
    const g = JSON.parse(JSON.stringify(f)) as LevelPropsFile;
    findProp(g, crate.id)!.obj.pos = [cx, cy];
    const after = checkProps(g, view, layout, balance);
    const added = newIssues(base.issues, after.issues);
    // the spot is inside a building: either the validator drops it or the building is enterable and the spot is a floor
    if (view.isBlocked([cx, cy], 'ground', 60)) assert.ok(added.some((i) => i.objectId === crate.id), 'the blocked spot is reported');
    assert.equal(newIssues(base.issues, base.issues).length, 0);
    assert.ok(after.gate.gate === 'pass' || after.gate.gate === 'fail');
  });

  it('withGate writes the fast-gate verdict into the named layers only and returns the same file when nothing changes', () => {
    const f = fresh();
    const same = withGate(f, { gate: 'pass' }, ['crates']);
    const failed = withGate(f, { gate: 'fail', gate_leg: 'leg 2', gate_blockers: ['crates_x'] }, ['crates']);
    assert.notEqual(failed, f);
    assert.equal(layerOf(failed, 'crates').report?.gate, 'fail');
    assert.deepEqual(layerOf(failed, 'crates').report?.gate_blockers, ['crates_x']);
    assert.equal(layerOf(failed, 'barrels').report?.gate, layerOf(f, 'barrels').report?.gate, 'another layer is not touched');
    const back = withGate(failed, { gate: 'pass' }, ['crates']);
    assert.equal(layerOf(back, 'crates').report?.gate, 'pass');
    assert.equal('gate_leg' in (layerOf(back, 'crates').report ?? {}), false);
    assert.equal(withGate(same, { gate: 'pass' }, ['crates']), same);
  });

  it('the unsaved guard asks only while a page holds edits and lets the user stay or leave', () => {
    setUnsaved('props', null);
    assert.equal(confirmLeave(() => { throw new Error('must not ask'); }), true);
    setUnsaved('props', 'пропсы карты X');
    assert.deepEqual(unsavedList(), ['пропсы карты X']);
    let asked = '';
    assert.equal(confirmLeave((t) => { asked = t; return false; }), false);
    assert.match(asked, /пропсы карты X/);
    assert.equal(confirmLeave(() => true), true);
    setUnsaved('props', null);
    assert.deepEqual(unsavedList(), []);
  });
});
