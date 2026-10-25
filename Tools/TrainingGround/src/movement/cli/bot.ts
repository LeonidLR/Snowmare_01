// `npm run bot:movement -- [--layout sample_small] [--members 2,3,4] [--seeds 4] [--seed 1] [--client none|mock|claude|jev]
//  [--config <file>] [--manual auto|<menu item>|all] [--strict]` - runs the movement series, writes
// Saved/TrainingGround/movement/runs.jsonl (raw, not for agents) and report.md (<= 30 lines, FORMATION_CONTRACT.md 4).
// --manual <item>: every run with that hold-G menu choice; --manual all: auto + every menu item, report = the matrix
// (manual_matrix.md too). --strict exits 1 when a check fails (with all: auto any check, a manual item checks 5 / 7).
import fs from 'node:fs';
import path from 'node:path';
import { advicePrompt, resolveClient } from '../ai.ts';
import { configHash, runSeries } from '../bot.ts';
import { renderManualMatrix, renderReport } from '../checks.ts';
import { configValues } from '../config.ts';
import type { MenuChoice } from '../menu.ts';
import { MENU_ITEMS } from '../menu.ts';
import { CONFIG_PATH, OUT_DIR, parseArgs, readConfigFile, readLayout } from './paths.ts';

const args = parseArgs(process.argv.slice(2));
const layoutName = typeof args.layout === 'string' ? args.layout : 'sample_small';
const members = (typeof args.members === 'string' ? args.members : '2,3,4').split(',').map(Number).filter((v) => v >= 1 && v <= 4);
const seeds = Number(args.seeds ?? 4) || 4;
const seed = Number(args.seed ?? 1) || 1;
const layout = readLayout(layoutName);
const configFile = readConfigFile(typeof args.config === 'string' ? args.config : CONFIG_PATH);
const config = configValues(configFile);

const started = Date.now();
const manualArg = typeof args.manual === 'string' ? args.manual : 'auto';
const header = () => ({
  layout: layout.name, routes: layout.routes.length, members, seeds, seed, configHash: configHash(config),
  date: new Date().toISOString().slice(0, 16).replace('T', ' '),
});
if (manualArg === 'all') {
  const choices: MenuChoice[] = ['auto', ...MENU_ITEMS.map((i) => i.id)];
  const rows = choices.map((choice) => ({ choice, ...runSeries({ layout, config, members, seeds, seedBase: seed, manual: choice }) }));
  const text = renderManualMatrix(header(), rows.map((r) => ({ choice: r.choice, summary: r.summary })));
  fs.mkdirSync(OUT_DIR, { recursive: true });
  fs.writeFileSync(path.join(OUT_DIR, 'manual_matrix.md'), text);
  fs.writeFileSync(path.join(OUT_DIR, 'report.md'), text);
  process.stdout.write(text);
  console.error(`[bot] ${rows.reduce((a, r) => a + r.runs.length, 0)} runs in ${((Date.now() - started) / 1000).toFixed(1)} s -> ${path.relative(process.cwd(), OUT_DIR)}`);
  const gate = (r: (typeof rows)[number]) => r.choice === 'auto' ? r.summary.allGreen : r.summary.checks.filter((ch) => [5, 7].includes(ch.id)).every((ch) => ch.fail === 0);
  process.exit(args.strict && !rows.every(gate) ? 1 : 0);
}
if (manualArg !== 'auto' && !MENU_ITEMS.some((i) => i.id === manualArg)) { console.error(`--manual: unknown item "${manualArg}"`); process.exit(2); }
const { runs, summary } = runSeries({ layout, config, members, seeds, seedBase: seed, manual: manualArg as MenuChoice });
fs.mkdirSync(OUT_DIR, { recursive: true });
fs.writeFileSync(path.join(OUT_DIR, 'runs.jsonl'), runs.map((r) => JSON.stringify({ layout: layout.name, config: configHash(config), ...r })).join('\n') + '\n');

let advice: string[] | null = null;
const { client, note } = resolveClient(typeof args.client === 'string' ? args.client : 'none', seed);
if (client) {
  const reply = await client.complete(advicePrompt({ kind: 'advice', layout: layout.name, summary, params: config }));
  advice = reply.text.split('\n').map((l) => l.trim()).filter(Boolean).slice(0, 10);
  console.error(`[advice] ${client.name}: ${reply.usage.input_tokens} in / ${reply.usage.output_tokens} out, $${reply.costUsd.toFixed(4)}`);
} else if (args.client && args.client !== 'none') console.error(`[advice] ${note}`);

const report = renderReport({ ...header(), manual: manualArg === 'auto' ? undefined : manualArg }, summary, advice);
fs.writeFileSync(path.join(OUT_DIR, 'report.md'), report);
process.stdout.write(report);
console.error(`[bot] ${runs.length} runs in ${((Date.now() - started) / 1000).toFixed(1)} s -> ${path.relative(process.cwd(), OUT_DIR)}`);
if (args.strict && !summary.allGreen) process.exit(1);
