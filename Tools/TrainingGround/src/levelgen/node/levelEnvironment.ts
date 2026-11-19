// Training Ground / level generator (task T-020): the weather section of «Управление картой» reads and writes the CURRENT map's level JSON
// (Content/Data/LevelJson/<Map>.json): the `environment` block (FEnvironmentLevelSettings) and the top-level `cold_rate_mult`.
// Only those two keys are touched; every other key, the `_comment`s and the file's spelling (BOM / CRLF / indentation) stay, and
// nothing is written when the edit changes nothing (patchJsonText returns the original text). Never imported by the browser.
import fs from 'node:fs';
import path from 'node:path';
import type { EnvironmentConfig } from '../../types.ts';
import { jsonEqual, parseJsonText, patchJsonText } from '../../utils/jsonTextPatch.ts';
import { COLD_RATE_DEFAULT, validateColdRate, validateEnvironment } from '../../utils/levelSettings.ts';
import { MAP_DIRS } from './currentMapFiles.ts';

export const MAP_NAME_RE = /^[A-Za-z0-9_.-]+$/;

/** What the section sends: the whole desired `environment` block (null / {} = no block) and `cold_rate_mult` (null = key absent). */
export interface EnvironmentEdit {
  environment: EnvironmentConfig | null;
  cold_rate_mult: number | null;
}

export interface LevelEnvironmentState {
  file: string;
  environment: EnvironmentConfig | null;
  cold_rate_mult: number | null;
  layout: string;
}

export function levelJsonPath(map: string, dir: string = MAP_DIRS.levelJson): string | null {
  if (!MAP_NAME_RE.test(map) || map.includes('..')) return null;
  return path.join(dir, `${map}.json`);
}

const isObject = (v: unknown): v is Record<string, unknown> => typeof v === 'object' && v !== null && !Array.isArray(v);

/** Validation errors of an edit (empty = writable). The environment rules are the Wave Editor's own (levelSettings.ts). */
export function validateEnvironmentEdit(edit: EnvironmentEdit): string[] {
  const errors: string[] = [];
  if (edit.environment !== null && !isObject(edit.environment)) return ['environment: объект или null'];
  if (edit.environment) errors.push(...validateEnvironment(edit.environment));
  if (edit.cold_rate_mult !== null) {
    if (typeof edit.cold_rate_mult !== 'number') errors.push('cold_rate_mult: число');
    else errors.push(...validateColdRate(edit.cold_rate_mult));
  }
  return errors;
}

/**
 * Pure: the level JSON text with the edit applied. `changed` is false (and `text` is the original, byte for byte) when the edit
 * leaves the two keys as they are. An empty environment block removes the key; cold_rate_mult 1 / null removes the key.
 */
export function applyEnvironmentEdit(original: string, edit: EnvironmentEdit): { text: string; changed: boolean } {
  const data = parseJsonText(original);
  if (!isObject(data)) throw new Error('level JSON: an object expected');
  const next: Record<string, unknown> = { ...data };
  const hasEnv = edit.environment && Object.keys(edit.environment).length > 0;
  if (hasEnv) next.environment = edit.environment;
  else delete next.environment;
  if (edit.cold_rate_mult === null || edit.cold_rate_mult === COLD_RATE_DEFAULT) delete next.cold_rate_mult;
  else next.cold_rate_mult = edit.cold_rate_mult;
  if (jsonEqual(data, next)) return { text: original, changed: false };
  const text = patchJsonText(original, next);
  return { text, changed: text !== original };
}

export function readLevelEnvironment(map: string, dir: string = MAP_DIRS.levelJson): LevelEnvironmentState {
  const file = levelJsonPath(map, dir);
  if (!file || !fs.existsSync(file)) throw new Error(`level JSON of ${map} not found`);
  const data = parseJsonText(fs.readFileSync(file, 'utf8'));
  if (!isObject(data)) throw new Error('level JSON: an object expected');
  const lg = typeof data.levelgen_layout === 'string' ? path.basename(data.levelgen_layout.replace(/\\/g, '/')).replace(/\.json$/, '') : '';
  return {
    file: path.basename(file),
    environment: isObject(data.environment) ? (data.environment as EnvironmentConfig) : null,
    cold_rate_mult: typeof data.cold_rate_mult === 'number' ? data.cold_rate_mult : null,
    layout: lg,
  };
}

function writeAtomic(target: string, content: string): void {
  const tmp = `${target}.tmp_${process.pid}_${Date.now()}`;
  try {
    fs.writeFileSync(tmp, content, 'utf8');
    fs.renameSync(tmp, target);
  } catch (e) {
    try { fs.unlinkSync(tmp); } catch { /* nothing to clean */ }
    throw e;
  }
}

/** Writes the edit into the map's level JSON (atomic; no write when unchanged). Throws on a bad edit or a missing file. */
export function writeLevelEnvironment(map: string, edit: EnvironmentEdit, dir: string = MAP_DIRS.levelJson): { changed: boolean; file: string } {
  const file = levelJsonPath(map, dir);
  if (!file || !fs.existsSync(file)) throw new Error(`level JSON of ${map} not found`);
  const errors = validateEnvironmentEdit(edit);
  if (errors.length) throw new Error(errors.join('; '));
  const r = applyEnvironmentEdit(fs.readFileSync(file, 'utf8'), edit);
  if (r.changed) writeAtomic(file, r.text);
  return { changed: r.changed, file: path.basename(file) };
}
