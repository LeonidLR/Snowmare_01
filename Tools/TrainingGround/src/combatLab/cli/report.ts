// `node src/combatLab/cli/report.ts --batch <Saved/Telemetry/combat_lab/<id>>` - the combat lab batch report (Scripts/bot_run.ps1 -ReportDir
// runs it after the last game): reads runs.jsonl (+ batch.json, jev.txt), writes report.md (~40 lines, the coordinator reads it) and
// aggregate.json (the page reads the records itself). Prints the report.
import fs from 'node:fs';
import path from 'node:path';
import { aggregate, parseRunsJsonl, reportMarkdown } from '../report.ts';

const argv = process.argv.slice(2);
const at = argv.indexOf('--batch');
const dir = at >= 0 ? argv[at + 1] : undefined;
if (!dir || !fs.existsSync(dir)) {
  console.error('usage: node src/combatLab/cli/report.ts --batch <batch folder with runs.jsonl>');
  process.exit(2);
}
const read = (name: string) => (fs.existsSync(path.join(dir, name)) ? fs.readFileSync(path.join(dir, name), 'utf8').replace(/^﻿/, '') : '');
const { runs, skipped } = parseRunsJsonl(read('runs.jsonl'));
let preset: string | undefined;
try {
  const batch = JSON.parse(read('batch.json') || '{}');
  preset = typeof batch.preset === 'string' && batch.preset ? batch.preset : undefined;
} catch { /* no batch.json: fine */ }
const markdown = reportMarkdown(runs, { id: path.basename(path.resolve(dir)), preset, jev: read('jev.txt') || undefined });
fs.writeFileSync(path.join(dir, 'report.md'), markdown);
fs.writeFileSync(path.join(dir, 'aggregate.json'), `${JSON.stringify({ skipped_lines: skipped, ...aggregate(runs) }, null, 2)}\n`);
console.log(markdown);
if (runs.length === 0) process.exit(1);
