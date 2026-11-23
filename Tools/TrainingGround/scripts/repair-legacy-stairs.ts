#!/usr/bin/env node
// Repairs the straight stairwell flights of layouts accepted before the 2026-11-18 / 11-19 stair rules, in place (src/levelgen/stairRepair.ts;
// the 2026-11-23 X4 traversal smoke, docs/port/gemini/outbox/100-x4-stairs-diagnosis.report.md section 4): stairwell slab gaps past a flight's
// top become the flight's exact footprint; a flight inside another building slides out of it. Every other key keeps its place and the file
// its style (BOM / EOL / indent). Explicit paths only, no defaults:
//   node scripts/repair-legacy-stairs.ts [--dry] [--full] [--force] <layout.json> [<layout.json> ...]
//   --dry    print what would change, write nothing
//   --full   also slide flights to the 200 cm approach boxes (every legacy flight moves)
//   --force  also for a file not in JSON.stringify style (rewritten normalised)
// Idempotent. The derived files are NOT written here: when a flight moved, re-export the tactical map afterwards
// (`node scripts/export-tactical-map.ts <layout.json> <Map>`), then rebuild the map (Scripts/levelgen_build_map.ps1). Style: scripts/add-firing-gaps.ts.

import * as fs from 'node:fs';
import { textStyle } from '../src/levelgen/roomAnchors.ts';
import type { GenLayout } from '../src/levelgen/schema.ts';
import { validateGenLayout } from '../src/levelgen/schema.ts';
import { repairLegacyStairs } from '../src/levelgen/stairRepair.ts';

const args = process.argv.slice(2);
const dry = args.includes('--dry'), full = args.includes('--full'), force = args.includes('--force');
const files = args.filter((a) => !a.startsWith('--'));
if (!files.length) {
  console.error('usage: node scripts/repair-legacy-stairs.ts [--dry] [--full] [--force] <layout.json> [<layout.json> ...]');
  process.exit(2);
}

let failed = 0;
for (const file of files) {
  const text = fs.readFileSync(file, 'utf8');
  const st = textStyle(text);
  const render = (v: unknown): string => st.bom + JSON.stringify(v, null, st.indent).replace(/\n/g, st.eol) + st.tail;
  const layout = JSON.parse(text.slice(st.bom.length)) as GenLayout;
  if (!force && render(layout) !== text) {
    console.error(`[repair-stairs] ${file}: the file is not in JSON.stringify style: a rewrite would change more than the stairs (use --force)`);
    failed++;
    continue;
  }
  const { layout: next, changes } = repairLegacyStairs(layout, { full });
  const errors = validateGenLayout(next);
  if (errors.length) { console.error(`[repair-stairs] ${file}: invalid after the change: ${errors.slice(0, 3).join('; ')}`); failed++; continue; }
  const out = render(next);
  console.log(`[repair-stairs] ${file}${dry ? ' (dry run)' : ''}: ${changes.length} change(s)${out === text ? ' (unchanged)' : ''}`);
  for (const c of changes) console.log(`  ${c.kind}: ${c.message}`);
  if (changes.some((c) => c.kind === 'flights_moved')) console.log('  flights moved: re-export the tactical map (scripts/export-tactical-map.ts) before rebuilding the map');
  if (changes.some((c) => c.kind === 'unresolved')) failed++;
  if (!dry && out !== text) fs.writeFileSync(file, out);
}
process.exit(failed ? 1 : 0);
