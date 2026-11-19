// T-020: the re-roll of every props kind (rerollKinds.ts) with the 088 route clearance (routeClearance.ts). Read-only on the committed
// L_Gen_Depot_1 layout (what autogen.test.ts uses); every file the tests make lives in memory.
import { describe, it } from 'node:test';
import * as assert from 'node:assert/strict';
import * as fs from 'node:fs';
import * as path from 'node:path';
import type { GenLayout } from '../schema.ts';
import type { LevelPropsFile, PropLayer } from './schema.ts';
import { PropsLayoutView } from './layoutView.ts';
import { mandatorySegments, routeDistOnLayer } from '../pmcPosts.ts';
import { clearanceCm, DEFAULT_AUTOGEN_CONFIG } from './autogenConfig.ts';
import { findProjectRoot, generatePropsForLayout, loadAutogenConfig, loadPropsBalance } from './autogen.ts';
import { applyRouteClearance } from './routeClearance.ts';
import { PMC_CHARGES_ID, rerollPropsLayers, rerollSupport } from './rerollKinds.ts';
import { formatPropsCanonical } from './canonical.ts';

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
const ids = (l: PropLayer) => l.objects.map((o) => o.id).sort();

describe('rerollSupport (T-020)', () => {
  it('every placed kind and the power layer can be re-rolled; pmc_charges, locked layers, frozen files and unknown kinds say why not', () => {
    for (const kind of ['crates', 'barrels', 'turrets', 'mines', 'loot', 'power']) {
      assert.deepEqual(rerollSupport({ id: kind, kind: kind as PropLayer['kind'] }), { ok: true }, kind);
    }
    const pmc = rerollSupport({ id: PMC_CHARGES_ID, kind: 'mines', locked: true });
    assert.equal(pmc.ok, false);
    const locked = rerollSupport({ id: 'crates', kind: 'crates', locked: true });
    assert.equal(locked.ok, false);
    const unknown = rerollSupport({ id: 'x', kind: 'fountain' as PropLayer['kind'] });
    assert.equal(unknown.ok, false);
    assert.match((unknown as { reason: string }).reason, /fountain/);
    assert.equal(rerollSupport({ id: 'crates', kind: 'crates' }, true).ok, false, 'a frozen file refuses');
  });
});

describe('rerollPropsLayers (T-020)', () => {
  it('the generated Depot file has every kind and a power layer; the shared route clearance finds nothing left to drop (idempotent)', () => {
    const f = fresh();
    for (const kind of ['crates', 'barrels', 'turrets', 'mines', 'loot', 'power']) assert.ok(f.layers.some((l) => l.id === kind), `layer ${kind}`);
    const before = formatPropsCanonical(f);
    const stats = applyRouteClearance(f, layout, cfg);
    assert.equal(stats.droppedTotal, 0, 'the autogen already applied it');
    assert.equal(formatPropsCanonical(f), before);
  });

  it('re-rolls a single layer of each kind: only that layer changes, deterministic for the same seed, the gate stays pass', () => {
    for (const kind of ['crates', 'barrels', 'turrets', 'mines', 'loot']) {
      const base = fresh();
      const a = JSON.parse(JSON.stringify(base)) as LevelPropsFile;
      const b = JSON.parse(JSON.stringify(base)) as LevelPropsFile;
      const req = { ids: [kind], seedFor: () => 4242 };
      const ra = rerollPropsLayers(a, balance, view, layout, req, cfg);
      const rb = rerollPropsLayers(b, balance, view, layout, req, cfg);
      assert.deepEqual(ra.rerolled, [kind]);
      assert.equal(formatPropsCanonical(a), formatPropsCanonical(b), `${kind}: deterministic`);
      assert.equal(layerOf(a, kind).seed, 4242);
      assert.equal(layerOf(a, kind).report?.gate, 'pass', `${kind}: gate`);
      assert.deepEqual(rb.skipped, []);
      // a layer nobody asked for is untouched (power is rebuilt only for turrets / loot, which also changes the turrets' power links)
      for (const other of ['crates', 'barrels']) {
        if (other !== kind) assert.deepEqual(ids(layerOf(a, other)), ids(layerOf(base, other)), `${kind} re-roll leaves ${other}`);
      }
    }
  });

  it('the route clearance (088) applies after a re-roll: a wide clearance leaves no mine / crate / barrel inside it, and pins stay', () => {
    const wideM = 25;
    const wide = {
      ...cfg,
      layers: {
        ...cfg.layers,
        crates: { ...cfg.layers!.crates!, route_corridor_m: wideM },
        barrels: { ...cfg.layers!.barrels!, route_corridor_m: wideM },
        mines: { ...cfg.layers!.mines!, route_clear_m: wideM },
      },
    };
    const f = fresh();
    const out = rerollPropsLayers(f, balance, view, layout, { ids: ['crates', 'barrels', 'mines'], seedFor: (l) => (l.seed ?? 1) + 1 }, wide);
    assert.ok(out.clearance.droppedTotal > 0, 'a 25 m clearance must drop something');
    for (const [id, limit] of [['crates', clearanceCm(wide, 'crates', 'route_corridor_m')], ['barrels', clearanceCm(wide, 'barrels', 'route_corridor_m')], ['mines', clearanceCm(wide, 'mines', 'route_clear_m')]] as const) {
      for (const o of layerOf(f, id).objects) {
        assert.ok(routeDistOnLayer(o.pos, segs, o.walk_layer || 'ground') >= limit, `${id} ${o.id} inside the clearance`);
      }
    }
    // a hand-locked object inside the clearance survives the clearance (keepLockedObjects)
    const g = fresh();
    const crate = layerOf(g, 'crates').objects[0];
    crate.locked = true;
    const strict = applyRouteClearance(g, layout, wide, { only: new Set(['crates']), keepLockedObjects: true });
    assert.ok(layerOf(g, 'crates').objects.some((o) => o.id === crate.id), 'the pinned crate stays');
    assert.ok(strict.droppedCrates > 0);
  });

  it('locked layers and pmc_charges are never touched; the unsupported ones come back as skipped with a reason, not an error', () => {
    const f = fresh();
    layerOf(f, 'barrels').locked = true;
    const pmc: PropLayer = { id: PMC_CHARGES_ID, kind: 'mines', enabled: true, seed: 1, locked: true, preset: 'mines_default', rules: {}, objects: [{ id: 'pmc_c1', variant: 'mine_ap', pos: [0, 0], walk_layer: 'ground', locked: true }] };
    f.layers.push(pmc);
    f.layers.push({ id: 'fountains', kind: 'fountain' as PropLayer['kind'], enabled: true, seed: 1, locked: false, preset: 'x', rules: {}, objects: [] });
    const barrelsBefore = ids(layerOf(f, 'barrels'));
    const pmcBefore = JSON.stringify(layerOf(f, PMC_CHARGES_ID));
    const out = rerollPropsLayers(f, balance, view, layout, { ids: ['barrels', PMC_CHARGES_ID, 'fountains', 'mines'], seedFor: () => 99 }, cfg);
    assert.deepEqual(out.rerolled, ['mines']);
    assert.deepEqual(out.skipped.map((s) => s.id).sort(), [PMC_CHARGES_ID, 'barrels', 'fountains'].sort());
    assert.ok(out.skipped.every((s) => s.reason.length > 0));
    assert.deepEqual(ids(layerOf(f, 'barrels')), barrelsBefore);
    assert.equal(JSON.stringify(layerOf(f, PMC_CHARGES_ID)), pmcBefore);
  });

  it('re-rolling the turrets rebuilds the power network: every powered turret points at a generator that exists', () => {
    const f = fresh();
    const out = rerollPropsLayers(f, balance, view, layout, { ids: ['turrets'], seedFor: () => 31337 }, cfg);
    assert.equal(out.powerRebuilt, true);
    const gens = new Set(layerOf(f, 'power').objects.map((o) => o.id));
    assert.ok(gens.size > 0);
    for (const t of layerOf(f, 'turrets').objects) {
      if (t.generator !== undefined) assert.ok(gens.has(t.generator), `turret ${t.id} -> ${t.generator}`);
    }
  });

  it('the power layer re-rolls on its own (same seed, same result) and a locked power layer is left with a note', () => {
    const a = fresh();
    const b = fresh();
    rerollPropsLayers(a, balance, view, layout, { ids: ['power'], seedFor: () => 5 }, cfg);
    rerollPropsLayers(b, balance, view, layout, { ids: ['power'], seedFor: () => 5 }, cfg);
    assert.equal(formatPropsCanonical(a), formatPropsCanonical(b));
    const f = fresh();
    layerOf(f, 'power').locked = true;
    const powerBefore = JSON.stringify(layerOf(f, 'power'));
    const out = rerollPropsLayers(f, balance, view, layout, { ids: ['turrets'], seedFor: () => 5 }, cfg);
    assert.equal(out.powerRebuilt, false);
    assert.equal(JSON.stringify(layerOf(f, 'power')), powerBefore);
    assert.ok(out.notes.some((n) => /power/.test(n)));
  });

  it('re-rolling crates together with the mines / loot attached to them leaves no stale attach_orphaned warning', () => {
    const f = fresh();
    rerollPropsLayers(f, balance, view, layout, { ids: ['crates', 'mines', 'loot'], seedFor: () => 777 }, cfg);
    const byId = new Map<string, PropLayer['objects'][number]>();
    for (const l of f.layers) for (const o of l.objects) byId.set(o.id, o);
    for (const l of f.layers) {
      for (const w of l.report?.warnings ?? []) {
        const m = /^attach_orphaned (.+)$/.exec(w);
        if (!m) continue;
        const o = byId.get(m[1]);
        assert.ok(o && o.attach !== undefined && !byId.has(o.attach), `stale warning ${w} on ${l.id}`);
      }
    }
  });

  it('a frozen file refuses every layer', () => {
    const f = fresh();
    f.frozen = true;
    const out = rerollPropsLayers(f, balance, view, layout, { ids: ['crates', 'mines'], seedFor: () => 1 }, cfg);
    assert.deepEqual(out.rerolled, []);
    assert.equal(out.skipped.length, 2);
  });

  it('DEFAULT_AUTOGEN_CONFIG is the shipped config of the route clearance (6 m mines, 2 m crates / barrels)', () => {
    assert.equal(clearanceCm(DEFAULT_AUTOGEN_CONFIG, 'mines', 'route_clear_m'), 600);
    assert.equal(clearanceCm(DEFAULT_AUTOGEN_CONFIG, 'crates', 'route_corridor_m'), 200);
    assert.equal(clearanceCm(cfg, 'barrels', 'route_corridor_m'), 200);
  });
});
