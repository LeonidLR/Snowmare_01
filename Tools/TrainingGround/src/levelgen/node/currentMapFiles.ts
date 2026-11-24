// Training Ground / level generator (task T-020): the Node side of the «current map» - lists the generated maps from the level JSONs,
// props files and accepted metas, and persists the designer's choice in Saved/TrainingGround/current_map.json (so it survives a
// browser change). Directories are parameters (tests use temp dirs); never imported by the browser.
import fs from 'node:fs';
import path from 'node:path';
import { ROOT } from '../../movement/cli/paths.ts';
import type { MapEntry } from '../currentMap.ts';

export interface MapDirs { levelJson: string; props: string; accepted: string; mapAssets?: string }
export const MAP_DIRS: MapDirs = {
  levelJson: path.join(ROOT, 'Content/Data/LevelJson'),
  props: path.join(ROOT, 'Content/Data/LevelGen/props'),
  accepted: path.join(ROOT, 'Content/Data/LevelGen/accepted'),
  mapAssets: path.join(ROOT, 'Content/Maps'),
};
export const CURRENT_MAP_FILE = path.join(ROOT, 'Saved/TrainingGround/current_map.json');

const readJson = (file: string): Record<string, unknown> | null => {
  try {
    const v = JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, '')) as unknown;
    return v && typeof v === 'object' && !Array.isArray(v) ? (v as Record<string, unknown>) : null;
  } catch { return null; }
};

/** `Content/Data/LevelGen/accepted/<layout>.json` (or a bare name) -> `<layout>`. */
const layoutLeaf = (p: unknown): string => (typeof p === 'string' && p ? path.basename(p.replace(/\\/g, '/')).replace(/\.json$/, '') : '');

/** Generated maps with level data or a real map asset. Orphan props cannot create a map. */
export function listMapEntries(dirs: MapDirs = MAP_DIRS): MapEntry[] {
  const byMap = new Map<string, MapEntry>();
  const entry = (map: string): MapEntry => {
    let e = byMap.get(map);
    if (!e) { e = { map, layout: '', levelJson: false, propsId: '', acceptedAt: '' }; byMap.set(map, e); }
    return e;
  };
  if (fs.existsSync(dirs.levelJson)) {
    for (const f of fs.readdirSync(dirs.levelJson)) {
      if (!f.endsWith('.json')) continue;
      const level = readJson(path.join(dirs.levelJson, f));
      const layout = layoutLeaf(level?.levelgen_layout);
      if (!level || !layout) continue;
      const e = entry(f.slice(0, -5));
      e.levelJson = true;
      e.layout = layout;
    }
  }
  const assetNames = dirs.mapAssets && fs.existsSync(dirs.mapAssets)
    ? new Set(fs.readdirSync(dirs.mapAssets, { recursive: true, withFileTypes: true })
      .filter((asset) => asset.isFile() && asset.name.endsWith('.umap')).map(({ name }) => name.slice(0, -5)))
    : new Set<string>();
  if (fs.existsSync(dirs.props)) {
    for (const f of fs.readdirSync(dirs.props)) {
      if (!f.endsWith('.props.json')) continue;
      const props = readJson(path.join(dirs.props, f));
      if (!props || typeof props.schema !== 'string' || !props.schema.startsWith('codex.levelgen_props/')) continue;
      const id = f.slice(0, -'.props.json'.length);
      const map = typeof props.map === 'string' && props.map ? props.map : id;
      if (!byMap.has(map) && !assetNames.has(map)) continue;
      const e = entry(map);
      e.propsId = id;
      if (!e.layout) e.layout = layoutLeaf(props.layout);
    }
  }
  for (const e of byMap.values()) {
    if (!e.layout) continue;
    const meta = readJson(path.join(dirs.accepted, `${e.layout}.meta.json`));
    if (meta && typeof meta.at === 'string') e.acceptedAt = meta.at;
  }
  return [...byMap.values()].sort((a, b) => a.map.localeCompare(b.map));
}

/** The saved choice (null: never picked / unreadable). */
export function readCurrentMap(file: string = CURRENT_MAP_FILE): string | null {
  const v = readJson(file);
  return v && typeof v.map === 'string' && v.map ? v.map : null;
}

/** Saves the choice (atomic: temp file + rename); false = already the saved value, nothing written. */
export function writeCurrentMap(map: string, file: string = CURRENT_MAP_FILE): boolean {
  if (readCurrentMap(file) === map) return false;
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const tmp = `${file}.tmp_${process.pid}_${Date.now()}`;
  fs.writeFileSync(tmp, `${JSON.stringify({ map, at: new Date().toISOString() }, null, 1)}\n`, 'utf8');
  fs.renameSync(tmp, file);
  return true;
}
