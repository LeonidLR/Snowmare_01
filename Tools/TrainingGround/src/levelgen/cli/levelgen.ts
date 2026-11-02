// `npm run levelgen -- [--profile ref_01_02_depot] [--seed 1] [--seeds 1] [--size 177x124] [--density 1] [--variants 3]
//  [--verticality 3] [--out Saved/TrainingGround/levelgen] [--no-sim] [--accept <name>]` - generates candidate layouts from a
// reference profile, evaluates each (route completability, mechanics, sightlines, ambush quality, profile similarity) and writes
// <out>/<name>.json (schema codex.levelgen_layout/1), <name>.eval.json and report.md (<= 30 lines per candidate; the only thing
// an agent reads). --accept <name> copies a written candidate into Content/Data/LevelGen/accepted/ (what the UE builder reads).
import fs from 'node:fs';
import path from 'node:path';
import { configValues } from '../../movement/config.ts';
import { CONFIG_PATH, parseArgs, readConfigFile, ROOT } from '../../movement/cli/paths.ts';
import { evaluateLayout } from '../evaluate.ts';
import { generateLayout } from '../generate.ts';
import { ACCEPTED_DIR, LEVELGEN_OUT, readProfileById } from '../node/files.ts';
import type { GenParams } from '../schema.ts';
import { validateGenLayout } from '../schema.ts';

const args = parseArgs(process.argv.slice(2));
const profile = readProfileById(typeof args.profile === 'string' ? args.profile : 'ref_01_02_depot');
const seed0 = Number(args.seed ?? 1) || 1;
const seeds = Number(args.seeds ?? 1) || 1;
const out = typeof args.out === 'string' ? path.resolve(args.out) : LEVELGEN_OUT;
const params: Partial<GenParams> = {};
if (typeof args.size === 'string') { const [w, h] = args.size.split('x').map(Number); if (w > 0 && h > 0) params.size_m = [w, h]; }
if (args.density !== undefined) params.density = Number(args.density) || 1;
if (args.variants !== undefined) params.variants = Number(args.variants) || 0;
if (args.verticality !== undefined) params.verticality = Math.max(0, Math.min(3, Number(args.verticality) || 0)) as GenParams['verticality'];
const config = configValues(readConfigFile(CONFIG_PATH));

fs.mkdirSync(out, { recursive: true });
if (typeof args.accept === 'string') {
  const src = path.join(out, `${args.accept}.json`);
  if (!fs.existsSync(src)) { console.error(`--accept: ${src} not found (generate it first)`); process.exit(2); }
  fs.mkdirSync(ACCEPTED_DIR, { recursive: true });
  fs.copyFileSync(src, path.join(ACCEPTED_DIR, `${args.accept}.json`));
  console.log(`accepted -> ${path.relative(ROOT, path.join(ACCEPTED_DIR, `${args.accept}.json`))}`);
  process.exit(0);
}
const started = Date.now();
const report: string[] = [];
for (let s = 0; s < seeds; s++) {
  const seed = seed0 + s;
  const layout = generateLayout({ profile, seed, params });
  const schema = validateGenLayout(layout);
  if (schema.length) { report.push(`# ${layout.name}: INVALID - ${schema.slice(0, 5).join('; ')}`); continue; }
  const ev = evaluateLayout({ layout, profile, config, noSim: !!args['no-sim'] });
  fs.writeFileSync(path.join(out, `${layout.name}.json`), JSON.stringify(layout, null, 1) + '\n');
  fs.writeFileSync(path.join(out, `${layout.name}.eval.json`), JSON.stringify(ev, null, 1) + '\n');
  report.push(...ev.summary, '');
}
const text = report.join('\n') + '\n';
fs.writeFileSync(path.join(out, 'report.md'), text);
process.stdout.write(text);
console.error(`[levelgen] ${seeds} candidate(s) in ${((Date.now() - started) / 1000).toFixed(1)} s -> ${path.relative(process.cwd(), out)}`);
