// `npm run levelgen:critic -- --layout gen_ref_01_02_depot_1 [--client claude|jev|mock] [--budget-usd 2] [--model claude-opus-5-5]
//  [--image <png>] [--dry-run]` - asks a critic about a candidate written by `npm run levelgen` (Saved/TrainingGround/levelgen/<name>.json
// + .eval.json). The Claude critic needs PNGs: the candidate is rendered by Scripts/Tools/levelgen_render.py (python) unless --image is
// given; the reference image is Saved/Screenshots/LevelGen/ref_critic.png (made once from the un-skewed map, 1200 px wide). Spend is
// kept in Saved/TrainingGround/levelgen/critic_ledger.json across calls (the UI and this CLI share it); the verdict is appended to
// <name>.verdicts.json and printed as one line. --dry-run prints the prompt and the estimate, no call.
import fs from 'node:fs';
import path from 'node:path';
import { parseArgs, ROOT } from '../../movement/cli/paths.ts';
import { claudeCritic, criticPrompt, CRITIC_MODELS, emptyLedger, estimateCostUsd, jevCritic, mockCritic, renderVerdict } from '../critic.ts';
import type { CriticClient, CriticLedger, CriticVerdict } from '../critic.ts';
import type { Evaluation } from '../evaluate.ts';
import { ensureCriticImages, LEVELGEN_OUT, readLedger, readProfileById, renderCandidatePng, writeLedger } from '../node/files.ts';

const args = parseArgs(process.argv.slice(2));
const name = typeof args.layout === 'string' ? args.layout.replace(/\.json$/, '') : '';
if (!name) { console.error('--layout <name> is required (a candidate written by npm run levelgen)'); process.exit(2); }
const client = (['claude', 'jev', 'mock'].includes(String(args.client)) ? String(args.client) : 'mock') as CriticClient;
const budgetUsd = Math.max(0, Number(args['budget-usd'] ?? 2) || 2);
const model = typeof args.model === 'string' ? args.model : CRITIC_MODELS.default;
const layoutFile = path.join(LEVELGEN_OUT, `${name}.json`);
const evalFile = path.join(LEVELGEN_OUT, `${name}.eval.json`);
if (!fs.existsSync(layoutFile) || !fs.existsSync(evalFile)) { console.error(`${layoutFile} / .eval.json not found - run npm run levelgen first`); process.exit(2); }
const evaluation = JSON.parse(fs.readFileSync(evalFile, 'utf8')) as Evaluation;
const profile = readProfileById(evaluation.profile);
const ledger: CriticLedger = readLedger() ?? emptyLedger();

let verdict: CriticVerdict;
if (client === 'claude') {
  const images = ensureCriticImages(name, typeof args.image === 'string' ? args.image : undefined);
  if (args['dry-run']) {
    const prompt = criticPrompt({ layoutName: name, evaluation, profile });
    console.log(prompt);
    console.log(`\n[dry run] model ${model}, images ${images.reference ? 2 : 1}, estimate ~$${estimateCostUsd(model, prompt.length + 1200, images.reference ? 2 : 1, 2700).toFixed(3)}, ledger $${ledger.spentUsd.toFixed(4)} of $${budgetUsd}`);
    process.exit(0);
  }
  verdict = await claudeCritic({ layoutName: name, evaluation, profile, candidatePng: images.candidate, referencePng: images.reference, budgetUsd, ledger, model });
} else if (client === 'jev') {
  verdict = await jevCritic({ evaluation, profile });
} else {
  verdict = mockCritic(evaluation);
}
if (verdict.costUsd > 0) {
  ledger.spentUsd = Math.round((ledger.spentUsd + verdict.costUsd) * 1e6) / 1e6;
  ledger.calls.push({ at: verdict.at, layout: name, client, model: verdict.model, costUsd: verdict.costUsd, fits: verdict.fits });
  writeLedger(ledger);
}
const verdictsFile = path.join(LEVELGEN_OUT, `${name}.verdicts.json`);
const all: CriticVerdict[] = fs.existsSync(verdictsFile) ? JSON.parse(fs.readFileSync(verdictsFile, 'utf8')) : [];
all.push(verdict);
fs.writeFileSync(verdictsFile, JSON.stringify(all, null, 1) + '\n');
console.log(renderVerdict(verdict));
console.error(`[critic] ledger $${ledger.spentUsd.toFixed(4)} of $${budgetUsd} -> ${path.relative(ROOT, verdictsFile)}`);
// a stub so the renderer import is not tree-shaken away by a future bundler config
void renderCandidatePng;
