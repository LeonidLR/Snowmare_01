// node scripts/generate_arena_props.ts [--write|--check] [L_Gen_Train_1 ...]
// Defaults to a dry run of all three arenas. Never rewrites level JSON, maps or balance.
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { pathToFileURL } from 'node:url';
import { ROOT, CONFIG_PATH, readConfigFile } from '../src/movement/cli/paths.ts';
import { configValues } from '../src/movement/config.ts';
import { evaluateLayout } from '../src/levelgen/evaluate.ts';
import { mockCritic } from '../src/levelgen/critic.ts';
import type { GenLayout } from '../src/levelgen/schema.ts';
import { validateGenLayout } from '../src/levelgen/schema.ts';
import { buildTrainingArena } from '../src/levelgen/trainingArenas.ts';
import { formatPropsCanonical } from '../src/levelgen/props/canonical.ts';
import { PropsLayoutView } from '../src/levelgen/props/layoutView.ts';
import { buildRules, rerollLayer } from '../src/levelgen/props/placement.ts';
import { runFastGate } from '../src/levelgen/props/fastGate.ts';
import type { LevelPropsFile, PropLayerKind } from '../src/levelgen/props/schema.ts';
import { PROPS_SCHEMA, validateProps } from '../src/levelgen/props/schema.ts';

const ARENAS = ['L_Gen_Train_1', 'L_Gen_Train_2', 'L_Gen_Train_3'];
const ORDER: PropLayerKind[] = ['crates', 'barrels', 'turrets', 'mines', 'loot'];
const PROPS_DIR = path.join(ROOT, 'Content/Data/LevelGen/props');

export function requireArenaName(name: string): void {
  if (!ARENAS.includes(name)) throw new Error(`Only ${ARENAS.join(', ')} are supported: ${name}`);
}

function sorted(value: unknown): unknown {
  if (Array.isArray(value)) return value.map(sorted);
  if (value && typeof value === 'object') return Object.fromEntries(
    Object.entries(value).sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0).map(([k, v]) => [k, sorted(v)]));
  return value;
}

export function layoutHash(layout: GenLayout): string {
  const keys = ['walls', 'buildings', 'cut', 'ladders', 'covers', 'routes', 'spawn', 'end'] as const;
  return createHash('sha1').update(JSON.stringify(sorted(Object.fromEntries(keys.map(k => [k, layout[k]]))))).digest('hex');
}

/** Additional checks on the stored result; placement itself owns spacing/exclusion rules. */
export function checkArenaProps(layout: GenLayout, file: LevelPropsFile, balance: Record<string, unknown>) {
  const errors = [...validateProps(file)];
  const view = PropsLayoutView.build(JSON.stringify(layout), PropsLayoutView.readConfig(balance));
  const ids = new Set<string>();
  const objects = file.layers.filter(l => l.enabled).flatMap(l => l.objects);
  const byId = new Map(objects.map(o => [o.id, o]));
  for (const object of objects) {
    if (ids.has(object.id)) errors.push(`duplicate id ${object.id}`);
    ids.add(object.id);
    if (!view.isWalkable(object.pos, object.walk_layer)) errors.push(`off walk layer: ${object.id}`);
    if (object.attach && !byId.has(object.attach)) errors.push(`missing attachment: ${object.id}`);
  }
  const mines = file.layers.find(l => l.kind === 'mines');
  if (mines?.enabled) {
    const rules = buildRules(balance, mines);
    for (const mine of mines.objects) {
      if (mine.door) {
        const door = view.findDoorway(mine.door);
        if (!door || door.bOnMandatoryRoute) errors.push(`mandatory/missing doorway: ${mine.id}`);
      } else {
        const crate = mine.attach ? byId.get(mine.attach) : undefined;
        const radius = mine.attach ? buildRules(balance, file.layers.find(l => l.kind === 'crates')!).exclude : rules.mineField.common.exclude;
        const route = radius.route_cm as Record<string, unknown> | undefined;
        const distance = typeof route?.mandatory === 'number' ? route.mandatory : 260;
        if (view.isNearMandatoryRoute(crate?.pos ?? mine.pos, mine.walk_layer, distance)) errors.push(`mandatory route corridor: ${mine.id}`);
      }
    }
  }
  if (file.map !== layout.name || file.layout !== layout.name) errors.push('map/layout name mismatch');
  if (file.layout_hash !== layoutHash(layout)) errors.push('stale layout_hash');
  const fastGate = runFastGate(layout, file, balance);
  return { errors, fastGate };
}

export function generateArenaProps(layout: GenLayout, balance: Record<string, unknown>): LevelPropsFile {
  requireArenaName(layout.name);
  const errors = validateGenLayout(layout);
  if (errors.length) throw new Error(errors.join('; '));
  const file: LevelPropsFile = {
    schema: PROPS_SCHEMA, map: layout.name, layout: layout.name, layout_hash: layoutHash(layout),
    revision: 1, frozen: false,
    layers: ORDER.map((kind, index) => ({ id: kind, kind, enabled: true, seed: (layout.seed + index) >>> 0,
      locked: false, preset: `${kind}_default`, rules: {}, objects: [] })), overrides: [],
  };
  const view = PropsLayoutView.build(JSON.stringify(layout), PropsLayoutView.readConfig(balance));
  for (const layer of file.layers) rerollLayer(file, balance, view, layer.id, layer.seed);
  const validation = checkArenaProps(layout, file, balance);
  for (const layer of file.layers) {
    if (layer.report) {
      layer.report.gate = validation.fastGate.gate;
      if (validation.fastGate.gate_leg) layer.report.gate_leg = validation.fastGate.gate_leg;
      if (validation.fastGate.gate_blockers) layer.report.gate_blockers = validation.fastGate.gate_blockers;
    }
  }
  return file;
}

export function runCli(args: string[]): void {
  const write = args.includes('--write');
  const check = args.includes('--check');
  if (write && check) throw new Error('Choose --write or --check');
  const names = args.filter(a => !['--write', '--check'].includes(a));
  const maps = names.length ? names : ARENAS;
  for (const map of maps) requireArenaName(map);
  const balance = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/Balance/props_balance.json'), 'utf8'));
  const config = configValues(readConfigFile(CONFIG_PATH));
  const outputs: { target: string; text: string }[] = [];
  let failed = false;
  for (const map of maps) {
    const level = JSON.parse(fs.readFileSync(path.join(ROOT, `Content/Data/LevelJson/${map}.json`), 'utf8'));
    const layoutPath = path.join(ROOT, `Content/Data/LevelGen/accepted/${map}.json`);
    if (path.resolve(ROOT, level.levelgen_layout) !== layoutPath) throw new Error(`Unexpected layout reference: ${map}`);
    const layout: GenLayout = JSON.parse(fs.readFileSync(layoutPath, 'utf8'));
    const target = path.join(PROPS_DIR, `${map}.props.json`);
    const file = check ? JSON.parse(fs.readFileSync(target, 'utf8')) as LevelPropsFile : generateArenaProps(layout, balance);
    const { errors, fastGate } = checkArenaProps(layout, file, balance);
    const profile = buildTrainingArena(Number(map.slice(-1)) as 1 | 2 | 3).profile;
    const evaluation = evaluateLayout({ layout, profile, config }); // full simulation, not noSim
    const fullPass = validateGenLayout(layout).length === 0 && evaluation.route.completable &&
      evaluation.variants.every(r => r.completable) && mockCritic(evaluation).fits &&
      evaluation.tactical.unreachableRooms.length === 0 && evaluation.tactical.blockedWindows.length === 0 &&
      evaluation.tactical.buildings.every(b => b.roofReachable) && evaluation.tactical.mantle.invalid === 0;
    failed ||= errors.length > 0 || fastGate.gate !== 'pass' || !fullPass;
    console.log(`${map}: schema/mines=${errors.length ? 'FAIL' : 'PASS'} fastGate=${fastGate.gate.toUpperCase()} fullGenerator=${fullPass ? 'PASS' : 'FAIL'} ` +
      file.layers.map(l => `${l.id}=${l.objects.length}`).join(' '));
    for (const layer of file.layers) {
      const report = layer.report;
      if (report?.short_by || report?.warnings?.length) console.log(`  ${layer.id}: short_by=${report.short_by ?? 0} warnings=${(report.warnings ?? []).join(',') || 'none'}`);
    }
    for (const error of errors) console.log(`  ERROR ${error}`);
    if (fastGate.reason) console.log(`  ERROR ${fastGate.reason}`);
    outputs.push({ target, text: formatPropsCanonical(file) });
  }
  if (failed) throw new Error('Arena validation failed; no files written');
  if (write) {
    // Never overwrite editor overrides, locked layers or any other existing authored data.
    for (const output of outputs) if (fs.existsSync(output.target) && fs.readFileSync(output.target, 'utf8').replace(/\r\n/g, '\n') !== output.text.replace(/\r\n/g, '\n'))
      throw new Error(`Refusing to overwrite existing props: ${output.target}`);
    for (const output of outputs) if (!fs.existsSync(output.target)) fs.writeFileSync(output.target, output.text, { encoding: 'utf8', flag: 'wx' });
    console.log(`Wrote/verified ${outputs.length} props files in Content/Data/LevelGen/props`);
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  try { runCli(process.argv.slice(2)); }
  catch (error) { console.error(error instanceof Error ? error.message : String(error)); process.exitCode = 1; }
}
