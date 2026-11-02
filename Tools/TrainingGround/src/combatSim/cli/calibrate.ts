// `npm run combatsim:calibrate -- --batch <unreal batch dir> [--layout <name|path>] [--seeds 200] [--out <dir>] [--constants <json>]`
// Compares the simulator with an Unreal combat-lab bot batch (Saved/Telemetry/combat_lab/<id>/: aggregate.json + batch.json): the sim runs
// the same layout with the batch's own pressure settings (batch.json "settings") for the same run length, then prints and writes the
// per-metric table sim / Unreal (<out>/calibration.md, calibration.json). The layout defaults to the batch's map (its level JSON).
import fs from 'node:fs';
import path from 'node:path';
import type { Aggregate } from '../../combatLab/report.ts';
import { configValues } from '../../movement/config.ts';
import { parseArgs, readConfigFile, ROOT } from '../../movement/cli/paths.ts';
import { runBatch, seedRange } from '../batch.ts';
import type { SimConstants } from '../config.ts';
import { calibrationMarkdown, calibrationTable } from '../report.ts';
import { outDir, readCachedTrack, resolveLayout, writeCachedTrack } from '../node/files.ts';

const args = parseArgs(process.argv.slice(2));
const batchDir = typeof args.batch === 'string' ? path.resolve(args.batch) : '';
if (!batchDir || !fs.existsSync(path.join(batchDir, 'aggregate.json'))) {
  console.error('usage: npm run combatsim:calibrate -- --batch <dir with aggregate.json + batch.json> [--layout <name|path>] [--seeds 200] [--out <dir>]');
  process.exit(2);
}
const unreal = JSON.parse(fs.readFileSync(path.join(batchDir, 'aggregate.json'), 'utf8')) as Aggregate;
const meta = fs.existsSync(path.join(batchDir, 'batch.json')) ? (JSON.parse(fs.readFileSync(path.join(batchDir, 'batch.json'), 'utf8')) as Record<string, unknown>) : {};
const settings = (meta.settings ?? {}) as { pressure?: unknown; cheats?: { immortal?: boolean; infinite_ammo?: boolean } };
const layoutName = typeof args.layout === 'string' ? args.layout : typeof meta.level === 'string' ? meta.level : '';
if (!layoutName) { console.error('the batch names no level; pass --layout'); process.exit(2); }
const resolved = resolveLayout(layoutName);
const minutes = typeof meta.minutes === 'number' ? meta.minutes : unreal.minutes / Math.max(1, unreal.runs);
const pressure = settings.pressure ?? resolved.level?.data.pressure ?? resolved.layout.pressure ?? {};
const constants = typeof args.constants === 'string' ? (JSON.parse(fs.existsSync(args.constants) ? fs.readFileSync(args.constants, 'utf8') : args.constants) as Partial<SimConstants>) : undefined;
const members = Number(args.members ?? 3);
const formationConfig = configValues(readConfigFile());
const cached = readCachedTrack(resolved.layout, formationConfig, members);
const seeds = seedRange(Number(args.seeds ?? 200), Number(args['seed-base'] ?? 1));
const { batch, track } = runBatch({
  layout: resolved.layout, layoutFile: path.relative(ROOT, resolved.file).replace(/\\/g, '/'), pressure, botRoute: resolved.level?.data.bot_route, formationConfig, seeds, durationSec: minutes * 60, members, constants,
  cheats: { immortal: settings.cheats?.immortal !== false, infiniteAmmo: settings.cheats?.infinite_ammo !== false }, track: cached ?? undefined,
});
if (!cached) writeCachedTrack(resolved.layout, formationConfig, track);
const rows = calibrationTable(batch.aggregate, unreal);
const title = `Calibration: sim (${seeds.length} seeds x ${minutes} min) vs Unreal batch ${path.basename(batchDir)} (${unreal.runs} x ${minutes} min, ${layoutName})`;
const md = calibrationMarkdown(rows, title);
const dir = outDir(resolved.layout.name, typeof args.out === 'string' ? args.out : undefined);
fs.writeFileSync(path.join(dir, 'calibration.md'), md);
fs.writeFileSync(path.join(dir, 'calibration.json'), JSON.stringify({ schema: 'codex.combat_sim_calibration/1', unreal_batch: batchDir, layout: resolved.layout.name, seeds: seeds.length, minutes, rows, sim: batch.aggregate, unreal }, null, 1) + '\n');
console.log(md);
console.log(`written: ${path.relative(ROOT, path.join(dir, 'calibration.md'))}`);
process.exit(rows.filter((r) => r.main).every((r) => r.ok) ? 0 : 1);
