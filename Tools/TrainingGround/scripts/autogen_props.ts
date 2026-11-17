#!/usr/bin/env node
// CLI script to generate props files for existing generated maps.
// Task: 076. Generates props for L_Gen_Depot_X4, L_Gen_Depot_1_X6, and L_Gen_Test_X2.

import * as fs from 'node:fs';
import * as path from 'node:path';
import type { GenLayout } from '../src/levelgen/schema.ts';
import type { LevelPropsFile } from '../src/levelgen/props/schema.ts';
import { formatPropsCanonical } from '../src/levelgen/props/canonical.ts';
import {
  generatePropsForLayout,
  canOverwritePropsFile,
  findProjectRoot,
  loadAutogenConfig,
  loadPropsBalance,
  targetMapNameForLayout,
} from '../src/levelgen/props/autogen.ts';

interface MapTarget {
  layoutName: string;
  mapName: string;
  sourcePropsMap?: string;
  seed?: number;
}

const DEFAULT_TARGETS: MapTarget[] = [
  {
    layoutName: 'gen_ref_01_02_depot_1_x4',
    mapName: 'L_Gen_Depot_X4',
    sourcePropsMap: 'L_Gen_Depot_1',
    seed: 1,
  },
  {
    layoutName: 'gen_ref_01_02_depot_1_x6',
    mapName: 'L_Gen_Depot_1_X6',
    sourcePropsMap: 'L_Gen_Depot_1',
    seed: 1,
  },
  {
    layoutName: 'gen_ref_01_02_depot_1_x2',
    mapName: 'L_Gen_Test_X2',
    sourcePropsMap: 'L_Gen_Depot_1',
    seed: 1,
  },
];

export function runAutogenProps(projectRoot?: string, targets = DEFAULT_TARGETS): void {
  const root = projectRoot ?? findProjectRoot();
  const cfg = loadAutogenConfig(root);
  const balance = loadPropsBalance(root);

  console.log(`[autogen_props] Project root: ${root}`);

  for (const t of targets) {
    const layoutPath = path.join(root, 'Content/Data/LevelGen/accepted', `${t.layoutName}.json`);
    if (!fs.existsSync(layoutPath)) {
      console.error(`[autogen_props] Layout file not found: ${layoutPath}`);
      continue;
    }

    const layout = JSON.parse(fs.readFileSync(layoutPath, 'utf8')) as GenLayout;
    let sourceProps: LevelPropsFile | null = null;

    if (t.sourcePropsMap) {
      const spPath = path.join(root, 'Content/Data/LevelGen/props', `${t.sourcePropsMap}.props.json`);
      if (fs.existsSync(spPath)) {
        sourceProps = JSON.parse(fs.readFileSync(spPath, 'utf8')) as LevelPropsFile;
        console.log(`[autogen_props] Using base source props: ${spPath}`);
      }
    }

    const resolvedMapName = t.mapName || targetMapNameForLayout(t.layoutName, root);
    const outPath = path.join(root, 'Content/Data/LevelGen/props', `${resolvedMapName}.props.json`);

    if (fs.existsSync(outPath)) {
      const existing = JSON.parse(fs.readFileSync(outPath, 'utf8')) as LevelPropsFile;
      if (!canOverwritePropsFile(existing)) {
        console.warn(`[autogen_props] Refusing to overwrite non-auto file: ${outPath}`);
        continue;
      }
    }

    console.log(`[autogen_props] Generating props for ${t.layoutName} -> ${resolvedMapName}...`);
    const res = generatePropsForLayout({
      layout,
      balance,
      autogenConfig: cfg,
      seed: t.seed ?? 1,
      sourceProps,
      mapName: resolvedMapName,
      layoutName: t.layoutName,
      projectRoot: root,
    });

    if (res.fastGateResult.gate !== 'pass') {
      console.warn(`[autogen_props] WARNING: Fast gate status: ${res.fastGateResult.gate} on leg ${res.fastGateResult.gate_leg}`);
    }

    const canonicalJson = formatPropsCanonical(res.props);
    fs.writeFileSync(outPath, canonicalJson, 'utf8');

    const totalProps = res.props.layers.reduce((sum, l) => sum + l.objects.length, 0);
    console.log(`[autogen_props] Saved ${outPath} (${totalProps} props across ${res.props.layers.length} layers, fast gate: ${res.fastGateResult.gate}, left out by the static validator: ${res.droppedByValidator.length})`);
    for (const issue of res.droppedByValidator) {
      console.log(`[autogen_props]   left out ${issue.objectId} (${issue.layerId}): ${issue.reason}`);
    }
  }
}

// Direct execution
runAutogenProps();
