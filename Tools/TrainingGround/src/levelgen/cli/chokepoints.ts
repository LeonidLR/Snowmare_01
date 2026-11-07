// CLI tool for Chokepoint & Kill Zone Analysis:
// Usage: node src/levelgen/cli/chokepoints.ts [path/to/layout.json] [--out <path>]

import fs from 'node:fs';
import path from 'node:path';
import { parseArgs, ROOT } from '../../movement/cli/paths.ts';
import { analyzeChokepoints } from '../chokepoints.ts';
import type { GenLayout } from '../schema.ts';

const DEFAULT_LAYOUT = path.join(ROOT, 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json');

const args = parseArgs(process.argv.slice(2));
const positional = process.argv.slice(2).filter((a) => !a.startsWith('--'));
const rawInput = typeof args.layout === 'string'
  ? args.layout
  : (positional.length > 0 ? positional[0] : null);

const layoutPath = rawInput
  ? (fs.existsSync(path.resolve(rawInput)) ? path.resolve(rawInput) : path.resolve(ROOT, rawInput))
  : DEFAULT_LAYOUT;

if (!fs.existsSync(layoutPath)) {
  console.error(`Layout file not found: ${layoutPath}`);
  process.exit(1);
}

const raw = fs.readFileSync(layoutPath, 'utf8');
const layout = JSON.parse(raw) as GenLayout;

const started = Date.now();
const result = analyzeChokepoints(layout);
const elapsedMs = Date.now() - started;

console.log(`\n=== CHOKEPOINT & KILL ZONE ANALYSIS: ${result.layoutName} ===`);
console.log(`Analyzed in ${elapsedMs} ms`);
console.log(`Corridors detected (1-3 m wide between blocks >= 3 m): ${result.summary.totalCorridors}`);
console.log(`- Narrow corridors (<= 2 m): ${result.summary.narrowCount}`);
console.log(`- Crossfire kill zones (rating >= 75): ${result.summary.crossfireCount}`);
console.log(`- Peak Kill Zone rating: ${result.summary.highestKillZoneRating} / 100`);
console.log(`- Average corridor width: ${result.summary.meanWidthM} m | Average length: ${result.summary.meanLengthM} m\n`);

result.corridors.forEach((c, idx) => {
  console.log(`[#${idx + 1}] Corridor: ${c.id}`);
  console.log(`    Class: ${c.category.toUpperCase()} | Kill Zone Rating: ${c.killZoneRating}/100`);
  console.log(`    Dimensions: width ${c.widthM} m (${c.widthCm} cm), length ${c.lengthM} m (${c.lengthCm} cm)`);
  console.log(`    Flanking Blocks:`);
  console.log(`      * Left:  ${c.leftBlockId} (height: ${c.leftHeightCm / 100} m) -> best perch score: ${c.leftBestScore}/100`);
  console.log(`      * Right: ${c.rightBlockId} (height: ${c.rightHeightCm / 100} m) -> best perch score: ${c.rightBestScore}/100`);
  console.log(`    Ambush Perches (${c.ambushPositions.length}):`);
  c.ambushPositions.forEach((p) => {
    console.log(`      - [${p.side.toUpperCase()}] ${p.id} @ [${p.pos[0]}, ${p.pos[1]}] +${p.heightCm / 100}m: score ${p.totalScore} (h:${p.heightScore}, cov:${p.coverScore}, los:${p.losScore})`);
  });
  console.log('');
});

console.log(`Heatmap generated: ${result.heatmap.cells.length} active cells (${result.heatmap.cellSizeCm} cm grid)`);

if (typeof args.out === 'string') {
  const outPath = path.isAbsolute(args.out) ? args.out : path.resolve(ROOT, args.out);
  fs.mkdirSync(path.dirname(outPath), { recursive: true });
  fs.writeFileSync(outPath, JSON.stringify(result, null, 2) + '\n');
  console.log(`Output written to: ${outPath}`);
}
