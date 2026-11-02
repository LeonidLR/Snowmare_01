// Training Ground / combat simulator: the Node-side file helpers of the CLI (never imported by the browser): finding a layout by name or
// path (an accepted levelgen layout, a candidate, a lab layout, a file), its level JSON (the pressure / bot_route blocks by the map name),
// the squad-track cache (Saved/TrainingGround/combatsim/tracks/<layout>_<members>_<hash>.json - the formation simulator walk takes ~10 s
// per layout, a run replays it), the output folder.
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { ACCEPTED_DIR, LEVELGEN_OUT } from '../../levelgen/node/files.ts';
import type { GenLayout } from '../../levelgen/schema.ts';
import { LEVELGEN_SCHEMA, validateGenLayout } from '../../levelgen/schema.ts';
import type { Layout } from '../../movement/layout.ts';
import { LAYOUT_SCHEMA, validateLayout } from '../../movement/layout.ts';
import { LAB_DIR, ROOT } from '../../movement/cli/paths.ts';
import type { SquadTrack } from '../squadTrack.ts';
import { TRACK_SCHEMA } from '../squadTrack.ts';
import { labToGenLayout } from '../world.ts';

export const COMBATSIM_OUT = path.join(ROOT, 'Saved/TrainingGround/combatsim');
export const TRACKS_DIR = path.join(COMBATSIM_OUT, 'tracks');
export const LEVEL_JSON_DIR = path.join(ROOT, 'Content/Data/LevelJson');
export const MAPS_DIR = path.join(ROOT, 'Content/Maps/Generated');

export interface ResolvedLayout { layout: GenLayout; file: string; /** The level JSON of the map built from it, when found. */ level?: { file: string; data: Record<string, unknown> } }

/**
 * `--layout` as a name or a path: an accepted levelgen layout (Content/Data/LevelGen/accepted/<name>.json), a candidate
 * (Saved/TrainingGround/levelgen/<name>.json), a map name (L_Gen_... -> its level JSON's `levelgen_layout`), a lab layout
 * (Content/Data/FormationLab/<name>.json, flattened), or any .json file of either schema.
 */
export function resolveLayout(nameOrPath: string): ResolvedLayout {
  const candidates: string[] = [];
  if (nameOrPath.endsWith('.json')) candidates.push(path.resolve(nameOrPath));
  else {
    const levelFile = path.join(LEVEL_JSON_DIR, `${nameOrPath}.json`);
    if (fs.existsSync(levelFile)) {
      const level = JSON.parse(fs.readFileSync(levelFile, 'utf8')) as Record<string, unknown>;
      if (typeof level.levelgen_layout === 'string') candidates.push(path.join(ROOT, level.levelgen_layout));
    }
    candidates.push(path.join(ACCEPTED_DIR, `${nameOrPath}.json`), path.join(LEVELGEN_OUT, `${nameOrPath}.json`), path.join(LAB_DIR, `${nameOrPath}.json`));
  }
  const file = candidates.find((f) => fs.existsSync(f));
  if (!file) throw new Error(`layout "${nameOrPath}" not found (tried ${candidates.map((c) => path.relative(ROOT, c)).join(', ')})`);
  const data = JSON.parse(fs.readFileSync(file, 'utf8')) as { schema?: string };
  let layout: GenLayout;
  if (data.schema === LEVELGEN_SCHEMA) {
    const errors = validateGenLayout(data);
    if (errors.length) throw new Error(`${file}: ${errors.join('; ')}`);
    layout = data as unknown as GenLayout;
  } else if (data.schema === LAYOUT_SCHEMA) {
    const errors = validateLayout(data);
    if (errors.length) throw new Error(`${file}: ${errors.join('; ')}`);
    layout = labToGenLayout(data as unknown as Layout);
  } else throw new Error(`${file}: schema "${data.schema}" is neither ${LEVELGEN_SCHEMA} nor ${LAYOUT_SCHEMA}`);
  return { layout, file, level: findLevelJson(layout, file) };
}

/** The level JSON whose `levelgen_layout` points at this layout file (the generated map), or the lab's by the layout name. */
export function findLevelJson(layout: GenLayout, layoutFile: string): ResolvedLayout['level'] {
  if (!fs.existsSync(LEVEL_JSON_DIR)) return undefined;
  const rel = path.relative(ROOT, layoutFile).replace(/\\/g, '/');
  for (const f of fs.readdirSync(LEVEL_JSON_DIR).filter((x) => x.endsWith('.json'))) {
    try {
      const data = JSON.parse(fs.readFileSync(path.join(LEVEL_JSON_DIR, f), 'utf8')) as Record<string, unknown>;
      if (typeof data.levelgen_layout === 'string' && data.levelgen_layout.replace(/\\/g, '/') === rel) return { file: path.join(LEVEL_JSON_DIR, f), data };
      if (layout.profile === 'lab' && f === `${layout.name}.json`) return { file: path.join(LEVEL_JSON_DIR, f), data };
    } catch { /* not a level JSON */ }
  }
  return undefined;
}

export function readLevelJson(file: string): Record<string, unknown> {
  return JSON.parse(fs.readFileSync(path.resolve(file), 'utf8')) as Record<string, unknown>;
}

/** The track cache key: the layout's geometry + route + the formation config + the member count. */
export function trackKey(layout: GenLayout, formationConfig: Record<string, number>, members: number): string {
  const h = createHash('sha1');
  h.update(JSON.stringify({ walls: layout.walls, buildings: layout.buildings, covers: layout.covers, ladders: layout.ladders, routes: layout.routes, bounds: layout.bounds, cut: layout.cut ?? null }));
  h.update(JSON.stringify(formationConfig));
  h.update(String(members));
  h.update(TRACK_SCHEMA);
  return h.digest('hex').slice(0, 12);
}

export function readCachedTrack(layout: GenLayout, formationConfig: Record<string, number>, members: number): SquadTrack | null {
  const file = path.join(TRACKS_DIR, `${safeName(layout.name)}_${members}_${trackKey(layout, formationConfig, members)}.json`);
  if (!fs.existsSync(file)) return null;
  try {
    const t = JSON.parse(fs.readFileSync(file, 'utf8')) as SquadTrack;
    return t.schema === TRACK_SCHEMA && t.members === members ? t : null;
  } catch { return null; }
}

export function writeCachedTrack(layout: GenLayout, formationConfig: Record<string, number>, track: SquadTrack): string {
  fs.mkdirSync(TRACKS_DIR, { recursive: true });
  const file = path.join(TRACKS_DIR, `${safeName(layout.name)}_${track.members}_${trackKey(layout, formationConfig, track.members)}.json`);
  fs.writeFileSync(file, JSON.stringify(track));
  return file;
}

export const safeName = (s: string) => s.replace(/[^A-Za-z0-9_.-]+/g, '_');

/** The output folder of a batch (default Saved/TrainingGround/combatsim/<layout>/). */
export function outDir(layoutName: string, requested?: string): string {
  const dir = requested ? path.resolve(requested) : path.join(COMBATSIM_OUT, safeName(layoutName));
  fs.mkdirSync(dir, { recursive: true });
  return dir;
}
