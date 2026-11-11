// `node src/movement/cli/shapesGolden.ts` - writes Tools/TrainingGround/golden/formation_SavedShapes.json from the saved-shape fixtures
// (Content/Data/FormationLab/fixtures/formation_shapes_*.json; FORMATION_CONTRACT.md 19). `--check` only compares and exits 1 when the
// file is missing or stale. C++ parity: CodexTactics.Formation.Parity.SavedShapes.
import fs from 'node:fs';
import path from 'node:path';
import { sameGoldenText } from '../golden.ts';
import { SHAPES_GOLDEN_FILE, buildShapesGolden, readShapesFixtures, shapesGoldenText } from '../shapesGolden.ts';
import { GOLDEN_DIR, LAB_DIR, parseArgs } from './paths.ts';

const args = parseArgs(process.argv.slice(2));
const text = shapesGoldenText(buildShapesGolden(readShapesFixtures(LAB_DIR)));
const file = path.join(GOLDEN_DIR, SHAPES_GOLDEN_FILE);
const same = fs.existsSync(file) && sameGoldenText(fs.readFileSync(file, 'utf8'), text);
if (args.check) {
  console.log(same ? `${SHAPES_GOLDEN_FILE} up to date` : `STALE ${SHAPES_GOLDEN_FILE} - run node src/movement/cli/shapesGolden.ts`);
  process.exit(same ? 0 : 1);
}
fs.mkdirSync(GOLDEN_DIR, { recursive: true });
if (!same) fs.writeFileSync(file, text);
console.log(`${same ? 'same ' : 'wrote'} ${SHAPES_GOLDEN_FILE}: ${JSON.parse(text).cases.length} cases`);
