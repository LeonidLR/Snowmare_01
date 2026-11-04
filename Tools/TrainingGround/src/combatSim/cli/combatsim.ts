// `npm run combatsim -- --layout <name|path> [--seeds 200] [--seed-base 1] [--minutes 10] [--members 3] [--pressure <json|file>]
//   [--level <level json>] [--out <dir>] [--no-cache] [--constants <json>] [--immortal 0|1] [--infinite-ammo 0|1]`
// The combat simulator over a layout (docs/design/levelgen/COMBAT_SIM.md): the world and the squad track once, then every seed; writes
// <out>/combatsim.json (schema codex.combat_sim/1) and <out>/report.md (<= 40 lines) and prints the report. The pressure block comes
// from --pressure (a JSON text or a file), else the map's level JSON (found by the layout), else the layout's own `pressure`, else the
// game's defaults; --level names a level JSON explicitly (its pressure + bot_route).
import fs from 'node:fs';
import path from 'node:path';
import { configValues } from '../../movement/config.ts';
import { parseArgs, readConfigFile, ROOT } from '../../movement/cli/paths.ts';
import { runBatch, seedRange } from '../batch.ts';
import type { SimConstants } from '../config.ts';
import { validateCombatSimBatch } from '../schema.ts';
import { loadProjectCombatRules, outDir, readCachedTrack, readLevelJson, resolveLayout, writeCachedTrack } from '../node/files.ts';

const args = parseArgs(process.argv.slice(2));
const name = typeof args.layout === 'string' ? args.layout : '';
if (!name) {
  console.error('usage: npm run combatsim -- --layout <name|path> [--seeds 200] [--seed-base 1] [--minutes 10] [--members 3] [--pressure <json|file>] [--level <level json>] [--out <dir>] [--no-cache]');
  process.exit(2);
}
const resolved = resolveLayout(name);
const level = typeof args.level === 'string' ? { file: path.resolve(args.level), data: readLevelJson(args.level) } : resolved.level;
let pressure: unknown = level?.data.pressure ?? resolved.layout.pressure ?? {};
if (typeof args.pressure === 'string') {
  const text = fs.existsSync(args.pressure) ? fs.readFileSync(args.pressure, 'utf8') : args.pressure;
  const parsed = JSON.parse(text) as Record<string, unknown>;
  // a file / text holding a whole level JSON or a settings object: take its pressure block; a bare block: merged over the level's
  pressure = parsed.pressure && typeof parsed.pressure === 'object' ? parsed.pressure : { ...(pressure as Record<string, unknown>), ...parsed };
}
let constants: Partial<SimConstants> | undefined = typeof args.constants === 'string' ? (JSON.parse(fs.existsSync(args.constants) ? fs.readFileSync(args.constants, 'utf8') : args.constants) as Partial<SimConstants>) : undefined;
const useSightConditions = args['sight-conditions'] !== undefined && args['sight-conditions'] !== '0';
const useEngagementBands = args['engagement-bands'] !== undefined && args['engagement-bands'] !== '0';
if (useSightConditions || useEngagementBands || args.weather || args['sun-elevation']) {
  const projectRules = loadProjectCombatRules(ROOT);
  constants = {
    ...constants,
    ...(useSightConditions ? { useSightConditions: true, sightConditions: projectRules.sightConditions } : {}),
    ...(useEngagementBands ? { useEngagementBands: true, engagementBands: projectRules.pmcBandConfig, weaponReachesCm: projectRules.weaponReachesCm } : {}),
    ...(typeof args.weather === 'string' ? { weather: args.weather as any } : {}),
    ...(args['sun-elevation'] !== undefined ? { sunElevationDeg: Number(args['sun-elevation']) } : {}),
  };
}
const seeds = seedRange(Number(args.seeds ?? 200), Number(args['seed-base'] ?? 1));
const minutes = Number(args.minutes ?? 10);
const members = Number(args.members ?? 3);
const formationConfig = configValues(readConfigFile());
const cached = args['no-cache'] ? null : readCachedTrack(resolved.layout, formationConfig, members);
const t0 = Date.now();
const { batch, track } = runBatch({
  layout: resolved.layout, layoutFile: path.relative(ROOT, resolved.file).replace(/\\/g, '/'), pressure, botRoute: level?.data.bot_route, formationConfig, seeds, durationSec: minutes * 60, members, constants,
  cheats: { immortal: args.immortal === undefined ? true : args.immortal !== '0', infiniteAmmo: args['infinite-ammo'] === undefined ? true : args['infinite-ammo'] !== '0' },
  track: cached ?? undefined,
  onProgress: (done, total) => { if (done % 25 === 0 || done === total) console.error(`[combatsim] ${done} / ${total} seeds, ${((Date.now() - t0) / 1000).toFixed(1)} s`); },
});
if (!cached) console.error(`[combatsim] track cached: ${path.relative(ROOT, writeCachedTrack(resolved.layout, formationConfig, track))} (${track.frames.length} frames, lap ${track.lapSeconds} s)`);
const problems = validateCombatSimBatch(batch);
if (problems.length) { console.error(`[combatsim] output invalid: ${problems.join('; ')}`); process.exit(1); }
const dir = outDir(resolved.layout.name, typeof args.out === 'string' ? args.out : undefined);
fs.writeFileSync(path.join(dir, 'combatsim.json'), JSON.stringify(batch, null, 1) + '\n');
fs.writeFileSync(path.join(dir, 'report.md'), batch.report.join('\n') + '\n');
console.log(batch.report.join('\n'));
console.log(`\nwritten: ${path.relative(ROOT, path.join(dir, 'combatsim.json'))}, ${path.relative(ROOT, path.join(dir, 'report.md'))}${level ? ` (pressure from ${path.relative(ROOT, level.file)})` : ''}`);
