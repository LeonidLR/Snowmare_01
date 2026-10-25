// `npm run redteam -- [--budget-usd 5] [--rounds 4] [--per-round 4] [--patience 3] [--client claude|mock] [--dry-run]
//  [--members 2,3,4] [--seeds 2] [--seed 1] [--config <file>] [--layouts-dir <dir>] [--effort low|medium|high]` - the red team (FORMATION_CONTRACT.md 11): the model designs
// adversarial layouts, the bot walks them (auto + every manual formation), failing layouts are kept as regression layouts in
// Content/Data/FormationLab/redteam/<round>_<n>.json (rounds continue after the files already there, nothing is overwritten;
// the mock client keeps its layouts in Saved/TrainingGround/movement/redteam_mock instead - they are not regression layouts).
// Writes (after every round, so an interrupted run keeps them) Saved/TrainingGround/movement/redteam_report.md (<= 30 lines),
// redteam_calls.jsonl (spend per call; never prompts or the key) and, with --dry-run, redteam_dry_request.txt (the first request, nothing sent).
import fs from 'node:fs';
import path from 'node:path';
import { resolveClient } from '../ai.ts';
import { configHash } from '../bot.ts';
import { registerClients } from '../clients.ts';
import { configValues } from '../config.ts';
import type { RedteamResult } from '../redteam.ts';
import { keptFile, redteam, renderRedteamReport } from '../redteam.ts';
import { CONFIG_PATH, OUT_DIR, REDTEAM_DIR, ROOT, parseArgs, readConfigFile } from './paths.ts';

registerClients();
const args = parseArgs(process.argv.slice(2));
const str = (k: string, d: string) => (typeof args[k] === 'string' ? (args[k] as string) : d);
const num = (k: string, d: number) => (typeof args[k] === 'string' && Number.isFinite(Number(args[k])) ? Number(args[k]) : d);
const config = configValues(readConfigFile(typeof args.config === 'string' ? args.config : CONFIG_PATH));
const dryRun = args['dry-run'] === true;
const clientName = str('client', 'claude');
const seed = num('seed', 1);
const { client, note } = dryRun ? { client: null, note: 'dry run' } : resolveClient(clientName, seed);
if (!dryRun && !client) { console.error(`[redteam] ${note}`); process.exit(2); }
const budgetUsd = num('budget-usd', 5);
const members = str('members', '2,3,4').split(',').map(Number).filter((v) => v >= 1 && v <= 4);
const seeds = num('seeds', 2);

const layoutsDir = typeof args['layouts-dir'] === 'string' ? path.resolve(args['layouts-dir'])
  : client?.name === 'mock' ? path.join(OUT_DIR, 'redteam_mock') : REDTEAM_DIR;
// Continue the numbering after the rounds already saved (never overwrite a regression layout).
fs.mkdirSync(layoutsDir, { recursive: true });
const roundBase = fs.readdirSync(layoutsDir).map((f) => /^(\d+)_\d+\.json$/.exec(f)).reduce((m, x) => (x ? Math.max(m, Number(x[1])) : m), 0);

const started = Date.now();
const date = new Date().toISOString().slice(0, 16).replace('T', ' ');
const meta = { client: client?.name ?? 'none', configHash: configHash(config), members, seeds, seed, date };
fs.mkdirSync(OUT_DIR, { recursive: true });
const NL = '\n';
// Saved after every round and at the end: an interrupted run keeps its layouts, its call log and a report so far.
const save = (res: RedteamResult) => {
  for (const k of res.kept) fs.writeFileSync(path.join(layoutsDir, `${k.id}.json`), JSON.stringify(keptFile(k, meta), null, 2) + NL);
  fs.writeFileSync(path.join(OUT_DIR, 'redteam_calls.jsonl'), res.calls.map((c) => JSON.stringify(c)).join(NL) + (res.calls.length ? NL : ''));
  const text = renderRedteamReport(res.stopReason ? res : { ...res, stopReason: `in progress (round ${res.rounds.length})` }, {
    client: dryRun ? 'none (dry run)' : client ? client.name : `none (${note})`, budgetUsd, date, members, seeds,
    dir: path.relative(ROOT, layoutsDir).split(path.sep).join('/'),
  });
  fs.writeFileSync(path.join(OUT_DIR, 'redteam_report.md'), text);
  return text;
};
const res = await redteam({
  client, config, dryRun, budgetUsd, members, seeds, seedBase: seed, roundBase,
  rounds: num('rounds', 4), perRound: num('per-round', 4), patience: num('patience', 3),
  effort: (['low', 'medium', 'high'] as const).find((e) => e === args.effort) ?? 'medium',
  log: (line) => console.error(line), onRound: save,
});
const report = save(res);
if (res.dryRequest) {
  const d = res.dryRequest;
  fs.writeFileSync(path.join(OUT_DIR, 'redteam_dry_request.txt'),
    [`model: ${d.model}`, `estimated input tokens: ${d.estInputTokens}`, '', '--- system ---', d.system, '', '--- prompt ---', d.prompt, ''].join(NL));
}
process.stdout.write(report);
console.error(`[redteam] ${((Date.now() - started) / 1000).toFixed(1)} s, ${res.kept.length} layouts kept -> ${path.relative(process.cwd(), layoutsDir)}`);
