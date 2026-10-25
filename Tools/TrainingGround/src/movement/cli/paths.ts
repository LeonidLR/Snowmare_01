// Training Ground / movement: project paths for the Node command-line tools (bot, golden, train).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import type { FormationConfigFile } from '../config.ts';
import { validateConfigFile } from '../config.ts';
import type { Layout } from '../layout.ts';
import { validateLayout } from '../layout.ts';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../../..');
export const CONFIG_REL = 'Content/Data/FormationLab/formation_config.json';
export const CONFIG_PATH = path.join(ROOT, CONFIG_REL);
export const LAB_DIR = path.join(ROOT, 'Content/Data/FormationLab');
export const GOLDEN_DIR = path.join(ROOT, 'Tools/TrainingGround/golden');
export const OUT_DIR = path.join(ROOT, 'Saved/TrainingGround/movement');

export function readConfigFile(file = CONFIG_PATH): FormationConfigFile {
  const data = JSON.parse(fs.readFileSync(file, 'utf8'));
  const errors = validateConfigFile(data);
  if (errors.length) throw new Error(`${file}: ${errors.join('; ')}`);
  return data as FormationConfigFile;
}

export function readLayout(nameOrPath: string): Layout {
  const file = nameOrPath.endsWith('.json') ? path.resolve(nameOrPath) : path.join(LAB_DIR, `${nameOrPath}.json`);
  const data = JSON.parse(fs.readFileSync(file, 'utf8'));
  const errors = validateLayout(data);
  if (errors.length) throw new Error(`${file}:\n  ${errors.join('\n  ')}`);
  return data as Layout;
}

/** Simple --key value / --flag argument parser. */
export function parseArgs(argv: string[]): Record<string, string | true> {
  const out: Record<string, string | true> = {};
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (!a.startsWith('--')) continue;
    const key = a.slice(2);
    const next = argv[i + 1];
    if (next !== undefined && !next.startsWith('--')) { out[key] = next; i++; } else out[key] = true;
  }
  return out;
}
