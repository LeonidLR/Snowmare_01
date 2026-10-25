// `npm run train -- [--budget-usd 5] [--rounds 10] [--patience 3] [--client claude|jev|mock] [--dry-run] [--layout sample_small]
//  [--members 3,4] [--seeds 3] [--seed 1]` - the tuner loop (FORMATION_CONTRACT.md 6). Writes Saved/TrainingGround/movement/
// train_report.md (<= 30 lines), train_best_config.json, train_calls.jsonl (spend per call) and, with --dry-run,
// train_dry_request.txt (the first request, nothing sent). The real clients are registered by the local wiring; the key is
// read from the environment and never printed.
import fs from 'node:fs';
import path from 'node:path';
import { resolveClient } from '../ai.ts';
import { registerClients } from '../clients.ts';
import { renderTrainReport, train } from '../train.ts';
import { OUT_DIR, parseArgs, readConfigFile, readLayout } from './paths.ts';

registerClients();
const args = parseArgs(process.argv.slice(2));
const str = (k: string, d: string) => (typeof args[k] === 'string' ? (args[k] as string) : d);
const num = (k: string, d: number) => (Number.isFinite(Number(args[k])) && typeof args[k] === 'string' ? Number(args[k]) : d);
const layout = readLayout(str('layout', 'sample_small'));
const configFile = readConfigFile(typeof args.config === 'string' ? args.config : undefined);
const dryRun = args['dry-run'] === true;
const clientName = str('client', 'claude');
const seed = num('seed', 1);
const { client, note } = dryRun ? { client: null, note: 'dry run' } : resolveClient(clientName, seed);
if (!dryRun && !client) console.error(`[train] ${note}`);
const budgetUsd = num('budget-usd', 5);

const res = await train({
  layout, configFile, client, dryRun,
  rounds: num('rounds', 10), budgetUsd, patience: num('patience', 3),
  members: str('members', '3,4').split(',').map(Number).filter((v) => v >= 1 && v <= 4),
  seeds: num('seeds', 3), seedBase: seed,
  log: (line) => console.error(line),
});

fs.mkdirSync(OUT_DIR, { recursive: true });
const report = renderTrainReport(res, {
  layout: layout.name, client: dryRun ? 'none (dry run)' : client ? client.name : `none (${note})`, budgetUsd,
  date: new Date().toISOString().slice(0, 16).replace('T', ' '),
});
fs.writeFileSync(path.join(OUT_DIR, 'train_report.md'), report);
fs.writeFileSync(path.join(OUT_DIR, 'train_best_config.json'), JSON.stringify(res.best.file, null, 2) + '\n');
fs.writeFileSync(path.join(OUT_DIR, 'train_calls.jsonl'), res.calls.map((c) => JSON.stringify(c)).join('\n') + (res.calls.length ? '\n' : ''));
if (res.dryRequest) {
  fs.writeFileSync(path.join(OUT_DIR, 'train_dry_request.txt'),
    `model: ${res.dryRequest.model}\nestimated input tokens: ${res.dryRequest.estInputTokens}\n\n--- system ---\n${res.dryRequest.system}\n\n--- prompt ---\n${res.dryRequest.prompt}\n`);
}
process.stdout.write(report);
