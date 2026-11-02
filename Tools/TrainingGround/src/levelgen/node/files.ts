// Training Ground / level generator: the Node-side file helpers shared by the CLI (src/levelgen/cli) and the dev server
// (server/levelgenApi.ts) - never imported by the browser. Paths, the reference profiles, the critic ledger, the PNG renders
// made by Scripts/Tools/levelgen_render.py (python + PIL; Node has no image library and the repo wants none).
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { ROOT } from '../../movement/cli/paths.ts';
import type { CriticLedger } from '../critic.ts';
import type { Profile } from '../profile.ts';
import { validateProfile } from '../profile.ts';

export const REFS_DIR = path.join(ROOT, 'Content/Data/LevelGen/refs');
export const ACCEPTED_DIR = path.join(ROOT, 'Content/Data/LevelGen/accepted');
export const LEVELGEN_OUT = path.join(ROOT, 'Saved/TrainingGround/levelgen');
export const SHOTS_DIR = path.join(ROOT, 'Saved/Screenshots/LevelGen');
export const MAPREFS_DIR = path.join(ROOT, 'Saved/MapRefs');
export const RENDER_PY = path.join(ROOT, 'Scripts/Tools/levelgen_render.py');
const LEDGER = path.join(LEVELGEN_OUT, 'critic_ledger.json');
export const NAME_RE = /^[A-Za-z0-9_-]+$/;

export function listProfiles(): { id: string; title: string; size_m: [number, number] }[] {
  if (!fs.existsSync(REFS_DIR)) return [];
  return fs.readdirSync(REFS_DIR).filter((f) => f.endsWith('.json')).map((f) => {
    const p = JSON.parse(fs.readFileSync(path.join(REFS_DIR, f), 'utf8')) as Profile;
    return { id: f.slice(0, -5), title: p.title ?? f, size_m: [p.bounds?.max?.[0] ?? 0, p.bounds?.max?.[1] ?? 0] };
  });
}

export function readProfileById(id: string): Profile {
  if (!NAME_RE.test(id)) throw new Error(`bad profile id "${id}"`);
  const file = path.join(REFS_DIR, `${id}.json`);
  const data = JSON.parse(fs.readFileSync(file, 'utf8'));
  const errors = validateProfile(data);
  if (errors.length) throw new Error(`${file}: ${errors.join('; ')}`);
  return data as Profile;
}

export function readLedger(): CriticLedger | null {
  return fs.existsSync(LEDGER) ? (JSON.parse(fs.readFileSync(LEDGER, 'utf8')) as CriticLedger) : null;
}

export function writeLedger(ledger: CriticLedger): void {
  fs.mkdirSync(LEVELGEN_OUT, { recursive: true });
  fs.writeFileSync(LEDGER, JSON.stringify(ledger, null, 1) + '\n');
}

/** Runs the python renderer (python -I, PIL); returns false when python / PIL is missing (the caller reports it). */
function runRender(args: string[]): boolean {
  const r = spawnSync('python', ['-I', RENDER_PY, ...args], { cwd: ROOT, encoding: 'utf8', timeout: 120_000 });
  if (r.status !== 0) { console.error(`[levelgen] render failed: ${(r.stderr || r.stdout || '').slice(-400)}`); return false; }
  return true;
}

/** Renders a candidate layout JSON (on disk) to PNG; returns the PNG path or null. */
export function renderCandidatePng(layoutFile: string, outPng: string, ppm = 6): string | null {
  return runRender(['layout', layoutFile, outPng, '--ppm', String(ppm)]) ? outPng : null;
}

/**
 * The reference image for the critic: the un-skewed annotated map shrunk to 1200 px as a JPEG (made once into
 * Saved/Screenshots/LevelGen/ref_critic.jpg from Saved/MapRefs; ~250 KB against a 2 MB PNG). Null when the map references are not
 * on this machine.
 */
export function ensureReferencePng(): string | null {
  const out = path.join(SHOTS_DIR, 'ref_critic.jpg');
  if (fs.existsSync(out)) return out;
  const clean = path.join(MAPREFS_DIR, 'FOT_Map_Great_Bend.webp'), annotated = path.join(MAPREFS_DIR, 'Map_Ref_01_02_Depot.jpg');
  if (!fs.existsSync(clean) || !fs.existsSync(annotated)) return null;
  fs.mkdirSync(SHOTS_DIR, { recursive: true });
  if (!fs.existsSync(path.join(SHOTS_DIR, 'ref_topdown_annotated.png')) && !runRender(['unskew', clean, annotated, SHOTS_DIR])) return null;
  return runRender(['shrink', path.join(SHOTS_DIR, 'ref_topdown_annotated.png'), out, '1200']) ? out : null;
}

/** Base64 PNGs for the Claude critic: the candidate (rendered unless given) and the reference (null when unavailable). */
export function ensureCriticImages(name: string, candidatePng?: string): { candidate: string; reference: string | null } {
  const png = candidatePng ?? renderCandidatePng(path.join(LEVELGEN_OUT, `${name}.json`), path.join(LEVELGEN_OUT, `${name}.png`));
  if (!png || !fs.existsSync(png)) throw new Error('no candidate PNG (python + PIL needed for Scripts/Tools/levelgen_render.py, or pass --image)');
  const ref = ensureReferencePng();
  return { candidate: fs.readFileSync(png).toString('base64'), reference: ref ? fs.readFileSync(ref).toString('base64') : null };
}
