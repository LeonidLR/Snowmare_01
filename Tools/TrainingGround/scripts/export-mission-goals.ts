#!/usr/bin/env node
// Writes Content/Data/LevelGen/objectives/<Map>.objectives.json (the in-game mission goals, src/levelgen/missionGoals.ts) for the
// generated maps that were built before «Принять» wrote it (designer 2026-11-17, TASK_BOARD F-4 «а»):
//   node scripts/export-mission-goals.ts                          # the default targets below
//   node scripts/export-mission-goals.ts <layout.json> <Map> ...  # explicit pairs (a layout path may lie outside this checkout)
// A layout without an `objectives` block gets the generator's automatic placement (objectives.ts placeObjectives, the layout's own
// seed, start = the map's spawn = its PlayerStart) - the same goals «Цели» would place on the plan. The layout file is never written.

import * as fs from 'node:fs';
import * as path from 'node:path';
import type { GenLayout } from '../src/levelgen/schema.ts';
import { findProjectRoot } from '../src/levelgen/props/autogen.ts';
import { placeObjectives } from '../src/levelgen/objectives.ts';
import { missionGoalsFor, writeMissionGoalsFile } from '../src/levelgen/missionGoals.ts';

const root = findProjectRoot();
const accepted = (n: string) => path.join(root, 'Content/Data/LevelGen/accepted', `${n}.json`);
const DEFAULT_TARGETS: [string, string][] = [
  [accepted('gen_ref_01_02_depot_1_x4'), 'L_Gen_Depot_X4'],
  [accepted('gen_ref_01_02_depot_1_x6'), 'L_Gen_Depot_1_X6'],
  [accepted('gen_ref_01_02_depot_1_x2'), 'L_Gen_Test_X2'],
];

const args = process.argv.slice(2);
const targets: [string, string][] = args.length >= 2 ? Array.from({ length: Math.floor(args.length / 2) }, (_, i) => [args[2 * i], args[2 * i + 1]]) : DEFAULT_TARGETS;
const outDir = path.join(root, 'Content/Data/LevelGen/objectives');
let failed = 0;
for (const [layoutPath, map] of targets) {
  if (!fs.existsSync(layoutPath)) {
    console.error(`[mission-goals] ${map}: layout not found: ${layoutPath}`);
    failed++;
    continue;
  }
  let layout = JSON.parse(fs.readFileSync(layoutPath, 'utf8').replace(/^﻿/, '')) as GenLayout;
  let source = 'the layout objectives';
  if (!layout.objectives) {
    const placed = placeObjectives({ layout, seed: layout.seed });
    if (!placed.ok) {
      console.error(`[mission-goals] ${map}: no objectives and the automatic placement failed: ${placed.message}`);
      failed++;
      continue;
    }
    layout = placed.layout;
    source = `auto placement (seed ${layout.seed}): ${placed.message}`;
  }
  const file = writeMissionGoalsFile(layout, outDir, map, path.basename(layoutPath, '.json'));
  const goals = missionGoalsFor(layout, map)?.goals ?? [];
  console.log(`[mission-goals] ${map}: ${goals.length} goals from ${source} -> ${file ? path.relative(root, file) : '(nothing written)'}`);
  for (const g of goals) console.log(`  ${g.id} ${g.kind} @ ${g.pos.join(', ')}${g.area ? ' (room area)' : ''}`);
}
process.exit(failed ? 1 : 0);
