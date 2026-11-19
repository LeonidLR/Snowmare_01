#!/usr/bin/env node
// Adds (or refreshes) the parapet FIRING GAPS of existing layout files, in place (src/levelgen/firingGaps.ts, docs/design/levelgen/
// FIRING_GAPS_HANDOVER.md, BUILDING_GENERATOR.md "Firing gaps"): the chokepoint analysis is recomputed when the file has none, a stale
// one (another geometry hash) or one older than ANALYSIS_VERSION; then `analysis` and the top-level `firing_gaps` are written and every other
// key keeps its place and the file its style (BOM / EOL / indent). Explicit paths only, no defaults:
//   node scripts/add-firing-gaps.ts [--dry] [--force] <layout.json> [<layout.json> ...]
//   node scripts/add-firing-gaps.ts --out <file> <layout.json>      # one layout, written to a copy; the layout stays as it is
//   --dry    print what would change (counts per roof), write nothing
//   --force  also for a file not in JSON.stringify style (rewritten normalised)
// Idempotent. `npm run levelgen:firing-gaps -- [--dry] <layout.json>...`. The tactical file is NOT written here: run
// `node scripts/export-tactical-map.ts <layout.json> <Map>` afterwards (it reads the file's own analysis + firing_gaps).
// Style: scripts/add-room-anchors.ts.

import * as fs from 'node:fs';
import * as path from 'node:path';
import { analyzeLayout, geometryHash } from '../src/levelgen/chokepoints.ts';
import { planFiringGaps } from '../src/levelgen/firingGaps.ts';
import { textStyle } from '../src/levelgen/roomAnchors.ts';
import type { GenLayout } from '../src/levelgen/schema.ts';
import { ANALYSIS_VERSION, validateGenLayout } from '../src/levelgen/schema.ts';

const args = process.argv.slice(2);
const flag = (k: string): string | true | undefined => { const i = args.indexOf(`--${k}`); if (i < 0) return undefined; const n = args[i + 1]; return n !== undefined && !n.startsWith('--') ? n : true; };
const dry = args.includes('--dry'), force = args.includes('--force'), outArg = flag('out');
const files = args.filter((a, i) => !a.startsWith('--') && !(i > 0 && args[i - 1] === '--out'));
if (!files.length || outArg === true || (typeof outArg === 'string' && files.length !== 1)) {
  console.error('usage: node scripts/add-firing-gaps.ts [--dry] [--force] <layout.json> [<layout.json> ...]\n       node scripts/add-firing-gaps.ts [--dry] --out <file> <layout.json>');
  process.exit(2);
}

let failed = 0;
for (const file of files) {
  const text = fs.readFileSync(file, 'utf8');
  const st = textStyle(text);
  const render = (v: unknown): string => st.bom + JSON.stringify(v, null, st.indent).replace(/\n/g, st.eol) + st.tail;
  const layout = JSON.parse(text.slice(st.bom.length)) as GenLayout;
  if (!force && render(layout) !== text) {
    console.error(`[firing-gaps] ${file}: the file is not in JSON.stringify style: a rewrite would change more than the analysis and the gaps (use --force to rewrite it normalised)`);
    failed++;
    continue;
  }
  const hadAnalysis = !!layout.analysis;
  const stale = !layout.analysis || layout.analysis.geometry_hash !== geometryHash(layout) || layout.analysis.version < ANALYSIS_VERSION;
  const t0 = Date.now();
  const analysis = stale ? analyzeLayout(layout) : layout.analysis!;
  // the analysis key keeps its place when the file had one (JSON.stringify keeps insertion order); a new one goes last, firing_gaps after it
  const next: GenLayout = { ...layout, analysis };
  const gaps = planFiringGaps(next, analysis);
  if (gaps.length) next.firing_gaps = gaps; else delete next.firing_gaps;
  const errors = validateGenLayout(next);
  if (errors.length) { console.error(`[firing-gaps] ${file}: invalid after the change: ${errors.slice(0, 3).join('; ')}`); failed++; continue; }
  const out = render(next);
  const perRoof = new Map<string, number>();
  for (const g of gaps) perRoof.set(g.building, (perRoof.get(g.building) ?? 0) + 1);
  const spots = analysis.overwatchRoofs.reduce((n, r) => n + r.spots.length, 0);
  const target = typeof outArg === 'string' ? path.resolve(outArg) : path.resolve(file);
  console.log(`[firing-gaps] ${path.basename(file)}${dry ? ' (dry run)' : ` -> ${target}`}: analysis ${stale ? `${hadAnalysis ? 'recomputed' : 'computed'} in ${Date.now() - t0} ms` : 'current'} (v${analysis.version}, ${analysis.overwatchRoofs.length} overwatch roofs, ${spots} spots), `
    + `${gaps.length} gaps on ${perRoof.size} roofs${out === text ? ' (unchanged)' : ''}`);
  if (perRoof.size) console.log(`  per roof: ${[...perRoof.entries()].map(([b, n]) => `${b} ${n}`).join(', ')}`);
  if (!dry && (out !== text || target !== path.resolve(file))) fs.writeFileSync(target, out);
}
process.exit(failed ? 1 : 0);
