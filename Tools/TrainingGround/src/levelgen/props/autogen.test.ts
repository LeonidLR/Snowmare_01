import { describe, it } from 'node:test';
import * as assert from 'node:assert/strict';
import * as fs from 'node:fs';
import * as path from 'node:path';
import type { GenLayout } from '../schema.ts';
import type { LevelPropsFile } from './schema.ts';
import { validateProps } from './schema.ts';
import { formatPropsCanonical } from './canonical.ts';
import { applyOverrides } from './placement.ts';
import {
  generatePropsForLayout,
  canOverwritePropsFile,
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
});
