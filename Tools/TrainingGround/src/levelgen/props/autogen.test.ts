import { describe, it } from 'node:test';
import * as assert from 'node:assert/strict';
import * as fs from 'node:fs';
import * as path from 'node:path';
import type { GenLayout } from '../schema.ts';
import type { LevelPropsFile } from './schema.ts';
import { validateProps } from './schema.ts';
import { formatPropsCanonical } from './canonical.ts';
import { applyOverrides } from './placement.ts';
import { mandatorySegments, routeDistOnLayer } from '../pmcPosts.ts';
import {
  generatePropsForLayout,
  canOverwritePropsFile,
  clearanceCm,
  targetMapNameForLayout,
  findProjectRoot,
  loadAutogenConfig,
  loadPropsBalance,
} from './autogen.ts';

describe('autogen props placement (task 076)', () => {
  const root = findProjectRoot();
  const depotLayoutPath = path.join(root, 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json');
  const depotPropsPath = path.join(root, 'Content/Data/LevelGen/props/L_Gen_Depot_1.props.json');
  const x2LayoutPath = path.join(root, 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x2.json');

  const depotLayout = JSON.parse(fs.readFileSync(depotLayoutPath, 'utf8')) as GenLayout;
  const depotProps = JSON.parse(fs.readFileSync(depotPropsPath, 'utf8')) as LevelPropsFile;
  const balance = loadPropsBalance(root);
  const autogenCfg = loadAutogenConfig(root);

  it('refuse-to-overwrite rule: authored files without source auto are protected', () => {
    // Authored files like L_Gen_Depot_1.props.json do not have source: "auto"
    assert.equal(depotProps.source, undefined);
    assert.equal(canOverwritePropsFile(depotProps), false, 'Must refuse to overwrite authored file');

    const autoFile: LevelPropsFile = {
      ...depotProps,
      source: 'auto',
    };
    assert.equal(canOverwritePropsFile(autoFile), true, 'Must allow overwriting auto-generated file');
  });

  it('map-name rule: resolves exact LevelJson target names, falls back to mapNameFor', () => {
    // Existing maps configured in Content/Data/LevelJson
    assert.equal(targetMapNameForLayout('gen_ref_01_02_depot_1_x4', root), 'L_Gen_Depot_X4');
    assert.equal(targetMapNameForLayout('gen_ref_01_02_depot_1_x2', root), 'L_Gen_Test_X2');
    assert.equal(targetMapNameForLayout('gen_ref_01_02_depot_1_x6', root), 'L_Gen_Depot_1_X6');

    // Arbitrary layout name falls back to mapNameFor
    assert.equal(targetMapNameForLayout('gen_ref_99_99_warehouse_a', root), 'L_Gen_Warehouse_A');
  });

  it('determinism: same layout and seed produces byte-identical canonical JSON', () => {
    const res1 = generatePropsForLayout({
      layout: depotLayout,
      balance,
      autogenConfig: autogenCfg,
      seed: 42,
      layoutName: 'gen_ref_01_02_depot_1',
      projectRoot: root,
    });

    const res2 = generatePropsForLayout({
      layout: depotLayout,
      balance,
      autogenConfig: autogenCfg,
      seed: 42,
      layoutName: 'gen_ref_01_02_depot_1',
      projectRoot: root,
    });

    const json1 = formatPropsCanonical(res1.props);
    const json2 = formatPropsCanonical(res2.props);
    assert.equal(json1, json2, 'Props outputs must be byte-identical for identical seed and layout');
  });

  it('validator and gates pass on N seeds (1..3)', () => {
    for (const seed of [1, 2, 3]) {
      const res = generatePropsForLayout({
        layout: depotLayout,
        balance,
        autogenConfig: autogenCfg,
        seed,
        layoutName: 'gen_ref_01_02_depot_1',
        projectRoot: root,
      });

      const errs = validateProps(res.props);
      assert.equal(errs.length, 0, `Schema validation failed on seed ${seed}: ${errs.join(', ')}`);
      assert.equal(res.fastGateResult.gate, 'pass', `Fast gate failed on seed ${seed}: leg=${res.fastGateResult.gate_leg}`);
      assert.equal(res.props.source, 'auto');

      // Check all 5 placement layers are present, then the power layer (TURRET_POWER.md phase 1)
      const layerKinds = res.props.layers.map((l) => l.kind);
      assert.deepEqual(layerKinds, ['crates', 'barrels', 'turrets', 'mines', 'loot', 'power']);
      assert.deepEqual(res.powerIssues, [], `power warnings on seed ${seed}`);
    }
  });

  it('expanded map keeps original props from sourceProps inside base area', () => {
    const x2Layout = JSON.parse(fs.readFileSync(x2LayoutPath, 'utf8')) as GenLayout;

    // Run autogen with sourceProps pointing to depotProps
    const res = generatePropsForLayout({
      layout: x2Layout,
      balance,
      autogenConfig: autogenCfg,
      seed: 1,
      sourceProps: depotProps,
      layoutName: 'gen_ref_01_02_depot_1_x2',
      projectRoot: root,
    });

    assert.equal(res.fastGateResult.gate, 'pass');

    // Check that original depot crates inside base area are preserved
    const baseMin = [-6200, -8850];
    const baseMax = [6200, 8850];
    const resolvedSource = applyOverrides(depotProps);
    const originalCrates = resolvedSource.objects.filter((r) => r.layerId === 'crates').map((r) => r.object);
    const originalInside = originalCrates.filter(
      (o) => o.pos[0] >= baseMin[0] && o.pos[0] <= baseMax[0] && o.pos[1] >= baseMin[1] && o.pos[1] <= baseMax[1]
    );

    const newCrates = res.props.layers.find((l) => l.kind === 'crates')?.objects ?? [];
    for (const orig of originalInside) {
      const match = newCrates.find((n) => n.id === orig.id);
      assert.ok(match, `Original crate ${orig.id} must be preserved in expanded map`);
      assert.deepEqual(match.pos, orig.pos, `Position of ${orig.id} must be identical`);
    }

    // Verify more crates were added for the expanded area
    assert.ok(newCrates.length >= originalInside.length, 'Expanded map should have at least as many crates as kept base crates');
  });

  it('088 route clearance: no mine/tripwire within route_clear_m and no crate/barrel within route_corridor_m on 3 seeds', () => {
    const routeClearCm = clearanceCm(autogenCfg, 'mines', 'route_clear_m');
    const crateCorridorCm = clearanceCm(autogenCfg, 'crates', 'route_corridor_m');
    const barrelCorridorCm = clearanceCm(autogenCfg, 'barrels', 'route_corridor_m');
    assert.ok(routeClearCm > 0 && crateCorridorCm > 0 && barrelCorridorCm > 0, 'the clearances come from props_autogen.json');

    for (const layoutName of ['gen_ref_01_02_depot_1_x2']) {
      const layout = JSON.parse(
        fs.readFileSync(path.join(root, 'Content/Data/LevelGen/accepted', `${layoutName}.json`), 'utf8')
      ) as GenLayout;
      const segs = mandatorySegments(layout);
      assert.ok(segs.length > 0, 'Mandatory route must have segments');

      for (const seed of [1, 2, 3]) {
        const res = generatePropsForLayout({ layout, balance, autogenConfig: autogenCfg, seed, layoutName, projectRoot: root });
        assert.equal(res.fastGateResult.gate, 'pass', `Fast gate must pass on ${layoutName} seed ${seed}`);
        if (seed === 1) {
          // determinism: the same seed gives the same drops and the same canonical JSON
          const again = generatePropsForLayout({ layout, balance, autogenConfig: autogenCfg, seed, layoutName, projectRoot: root });
          assert.deepEqual(again.droppedByRouteClearance, res.droppedByRouteClearance);
          assert.equal(formatPropsCanonical(again.props), formatPropsCanonical(res.props));
        }

        for (const l of res.props.layers) {
          const limit =
            l.kind === 'mines' && l.id !== 'pmc_charges' ? routeClearCm
            : l.kind === 'crates' ? crateCorridorCm
            : l.kind === 'barrels' ? barrelCorridorCm
            : 0;
          if (limit === 0) continue;
          for (const obj of l.objects) {
            const d = routeDistOnLayer(obj.pos, segs, obj.walk_layer || 'ground');
            assert.ok(
              d >= limit,
              `${l.kind} ${obj.id} (${obj.variant}) at [${obj.pos}] on ${obj.walk_layer} is ${d} cm from the route < ${limit} cm (${layoutName}, seed ${seed})`
            );
          }
        }
      }
    }
  });

  it('088 route clearance bites: a wide clearance drops objects, cascades to attachments, never touches pmc_charges', () => {
    const layoutName = 'gen_ref_01_02_depot_1_x2';
    const layout = JSON.parse(
      fs.readFileSync(path.join(root, 'Content/Data/LevelGen/accepted', `${layoutName}.json`), 'utf8')
    ) as GenLayout;
    const segs = mandatorySegments(layout);
    const run = (cfg: typeof autogenCfg) =>
      generatePropsForLayout({ layout, balance, autogenConfig: cfg, seed: 1, layoutName, projectRoot: root });
    const wideM = 25; // far wider than the shipped clearances
    const wide = run({
      ...autogenCfg,
      layers: {
        ...autogenCfg.layers,
        crates: { ...autogenCfg.layers!.crates, route_corridor_m: wideM },
        barrels: { ...autogenCfg.layers!.barrels, route_corridor_m: wideM },
        mines: { ...autogenCfg.layers!.mines, route_clear_m: wideM },
      },
    });
    const st = wide.droppedByRouteClearance;
    assert.ok(st.droppedCrates > 0 && st.droppedBarrels > 0 && st.droppedMines > 0, 'a 25 m clearance must drop crates, barrels and mines');
    assert.equal(st.droppedTotal, st.droppedCrates + st.droppedBarrels + st.droppedMines + st.droppedLoot);

    const ids = new Set(wide.props.layers.flatMap((l) => l.objects.map((o) => o.id)));
    for (const l of wide.props.layers) {
      for (const o of l.objects) {
        if (o.attach) assert.ok(ids.has(o.attach), `${o.id} is attached to the dropped ${o.attach}`);
        if (l.kind === 'crates') assert.ok(routeDistOnLayer(o.pos, segs, o.walk_layer || 'ground') >= wideM * 100);
      }
    }
    const pmcIds = (r: ReturnType<typeof run>) =>
      r.props.layers.filter((l) => l.id === 'pmc_charges').flatMap((l) => l.objects.map((o) => o.id));
    assert.deepEqual(pmcIds(wide), pmcIds(run(autogenCfg)), 'the locked pmc_charges layer is not changed by the clearance');
  });
});
