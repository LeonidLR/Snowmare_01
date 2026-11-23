// S2.3 engine check: the bgen test layout (gen_bgen_test_x6) regenerated with the building plans ON.
// Same lineage as the unplanned file: base seed 1 (Depot profile) -> x4 seed 7 -> x6 seed 8; then the room anchors (add-room-anchors).
//   node scripts/gen-bgen-planned-x6.ts <out.json> [--no-plans]
// Writes nothing but <out.json>; prints the plan census (planned offices / blocks, 36 m blocks, offices with a 900-1200 core bay).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { expandLayout } from '../src/levelgen/expand.ts';
import { generateLayout } from '../src/levelgen/generate.ts';
import type { Profile } from '../src/levelgen/profile.ts';
import { anchorLayoutText } from '../src/levelgen/roomAnchors.ts';
import { validateGenLayout } from '../src/levelgen/schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
const out = process.argv[2];
if (!out) { console.error('usage: node scripts/gen-bgen-planned-x6.ts <out.json> [--no-plans]'); process.exit(2); }
const plans = !process.argv.includes('--no-plans');
const params = plans ? { building_plans: true } : {};
let l = generateLayout({ profile, seed: 1, params });
for (const [factor, seed] of [[4, 7], [6, 8]] as const) {
  const r = expandLayout({ layout: l, factor, seed, profile });
  if (!r.ok) { console.error(`expand x${factor}: ${r.message}`); process.exit(1); }
  l = r.layout;
}
l = { ...l, name: 'gen_bgen_test_x6' };
const errs = validateGenLayout(l);
if (errs.length) { console.error('schema errors:', errs.slice(0, 10)); process.exit(1); }
const text = anchorLayoutText(JSON.stringify(l, null, 1) + '\n', true);
fs.writeFileSync(out, text);
const o = JSON.parse(text);
console.log(JSON.stringify({ out, schema: o.schema, params: o.params, buildings: o.buildings.length, enterable: o.buildings.filter((b: any) => b.enterable).length,
  planned: o.buildings.filter((b: any) => b.plan).length }));
