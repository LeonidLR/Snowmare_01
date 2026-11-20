#!/usr/bin/env node
// Writes Content/Data/LevelGen/tactical/<Map>.tactical.json (the in-game tactical map, src/levelgen/tacticalMap.ts,
// docs/design/TACTICAL_MAP.md P1) for the generated maps that were built before «Принять» wrote it:
//   node scripts/export-tactical-map.ts                          # the default targets below
//   node scripts/export-tactical-map.ts <layout.json> <Map> ...  # explicit pairs (a layout path may lie outside this checkout)
// The layout's own `analysis` is used when it is current; else the analysis is run here the way «Принять» does (chokepoints.ts
// analyzeLayout, the config's default conditions). A layout without an `objectives` block first gets the generator's automatic
// placement (objectives.ts placeObjectives, the layout's own seed) - the same goals scripts/export-mission-goals.ts wrote for the map,
// so the analysis follows the same start -> goals path the game has. The layout file is never written.

import * as fs from 'node:fs';
import * as path from 'node:path';
import type { GenLayout } from '../src/levelgen/schema.ts';
import { findProjectRoot } from '../src/levelgen/props/autogen.ts';
import { placeObjectives } from '../src/levelgen/objectives.ts';
import { tacticalMapFor, writeTacticalMapFile } from '../src/levelgen/tacticalMap.ts';

const root = findProjectRoot();
const accepted = (n: string) => path.join(root, 'Content/Data/LevelGen/accepted', `${n}.json`);
const DEFAULT_TARGETS: [string, string][] = [
  [accepted('gen_ref_01_02_depot_1_x4'), 'L_Gen_Depot_X4'],
  [accepted('gen_ref_01_02_depot_1_x6'), 'L_Gen_Depot_1_X6'],
  [accepted('gen_ref_01_02_depot_1_x2'), 'L_Gen_Test_X2'],
];

const args = process.argv.slice(2);
const targets: [string, string][] = args.length >= 2 ? Array.from({ length: Math.floor(args.length / 2) }, (_, i) => [args[2 * i], args[2 * i + 1]]) : DEFAULT_TARGETS;
const outDir = path.join(root, 'Content/Data/LevelGen/tactical');
let failed = 0;
for (const [layoutPath, map] of targets) {
  if (!fs.existsSync(layoutPath)) {
    console.error(`[tactical-map] ${map}: layout not found: ${layoutPath}`);
    failed++;
    continue;
  }
  let layout = JSON.parse(fs.readFileSync(layoutPath, 'utf8').replace(/^﻿/, '')) as GenLayout;
  let goals = 'the layout objectives';
  if (!layout.objectives) {
    const placed = placeObjectives({ layout, seed: layout.seed });
    if (placed.ok) {
      layout = placed.layout;
      goals = `auto placement (seed ${layout.seed})`;
    } else {
      goals = `none (automatic placement failed: ${placed.message})`;
    }
  }
  const t0 = Date.now();
  const layoutName = path.basename(layoutPath, '.json');
  const f = tacticalMapFor(layout, map, layoutName);
  const file = writeTacticalMapFile(layout, outDir, map, layoutName, f);
  console.log(`[tactical-map] ${map}: analysis ${f.analysis_source} (goals: ${goals}), ${Date.now() - t0} ms -> ${path.relative(root, file)}`);
  console.log(`  chokepoints ${f.chokepoints.length}, drop spots ${f.drop_spots.length}, windows ${f.windows.length}, overwatch roofs ${f.overwatch_roofs.length}, `
    + `hiding spots ${f.hiding_spots.length}, ladder-only roofs ${f.ladder_only_roofs.length}, walkable roofs ${f.walkable_roofs?.length ?? 0}, `
    + `open crossings ${f.open_crossings?.length ?? 0}, goal sites ${f.goal_sites?.length ?? 0}; hash ${f.geometry_hash}`);
  for (const c of f.chokepoints.slice(0, 3)) console.log(`  ${c.id} ${c.kind} ${c.score} @ ${c.pos.join(', ')} (${c.alternative}${c.detour_m !== null ? ` ${c.detour_m} m` : ''})`);
}
process.exit(failed ? 1 : 0);
