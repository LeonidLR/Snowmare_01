// `npm run golden` - writes Tools/TrainingGround/golden/formation_<Rule>.json from the current formation_config.json
// (FORMATION_CONTRACT.md 3). `--check` only compares and exits 1 when a file is missing or stale.
import fs from 'node:fs';
import path from 'node:path';
import { configValues } from '../config.ts';
import { GOLDEN_RULES, buildGolden, goldenFileName, goldenText } from '../golden.ts';
import { CONFIG_REL, GOLDEN_DIR, parseArgs, readConfigFile } from './paths.ts';

const args = parseArgs(process.argv.slice(2));
const files = buildGolden(configValues(readConfigFile()), CONFIG_REL);
fs.mkdirSync(GOLDEN_DIR, { recursive: true });
let stale = 0;
for (const rule of GOLDEN_RULES) {
  const file = path.join(GOLDEN_DIR, goldenFileName(rule));
  const text = goldenText(files[rule]);
  const same = fs.existsSync(file) && fs.readFileSync(file, 'utf8') === text;
  if (args.check) {
    if (!same) { stale++; console.log(`STALE ${path.relative(process.cwd(), file)}`); }
  } else {
    if (!same) fs.writeFileSync(file, text);
    console.log(`${same ? 'same ' : 'wrote'} ${goldenFileName(rule)}: ${files[rule].cases.length} cases`);
  }
}
if (args.check) {
  console.log(stale ? `${stale} golden file(s) stale - run npm run golden` : 'golden files up to date');
  process.exit(stale ? 1 : 0);
}
