// S2.2 golden: hashes of generateLayout (x1) and expandLayout factor 6 (x6) for seeds 1-3 of the Depot profile, plans toggle OFF.
// `node scripts/plans-golden.ts [--write <file>] [--check <file>]`: the S2.2 proof that the toggle off keeps the output byte for byte.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { expandLayout } from '../src/levelgen/expand.ts';
import { generateLayout } from '../src/levelgen/generate.ts';
import type { Profile } from '../src/levelgen/profile.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
const hash = (l: unknown) => crypto.createHash('sha256').update(JSON.stringify(l)).digest('hex');
const out: Record<string, string> = {};
for (const seed of [1, 2, 3]) {
  const base = generateLayout({ profile, seed });
  out[`x1_s${seed}`] = hash(base);
  const x6 = expandLayout({ layout: base, factor: 6, seed: seed + 6, profile });
  out[`x6_s${seed}`] = hash(x6.ok ? x6.layout : x6);
}
const [mode, file] = process.argv.slice(2);
if (mode === '--write') fs.writeFileSync(file, JSON.stringify(out, null, 2) + '\n');
else if (mode === '--check') {
  const want = JSON.parse(fs.readFileSync(file, 'utf8')) as Record<string, string>;
  const bad = Object.keys(want).filter((k) => want[k] !== out[k]);
  console.log(bad.length ? `GOLDEN DIFF: ${bad.join(', ')}` : `golden ok (${Object.keys(want).length} layouts)`);
  process.exit(bad.length ? 1 : 0);
} else console.log(JSON.stringify(out, null, 2));
