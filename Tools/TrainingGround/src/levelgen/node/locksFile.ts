// Training Ground / level generator v2, stage 2 (board 066): the «Сохранить замки» button's file side. The page's locked districts
// are written as polygons into the map JSON: into the accepted map (Content/Data/LevelGen/accepted/<name>.json) when it exists -
// only its `regen.locks` changes, the map itself stays as accepted - else the whole page layout is saved as a draft
// (Saved/TrainingGround/levelgen/<name>.json) so the locks are not lost before «Принять». The directories are parameters (tests
// write to a temp folder).
import fs from 'node:fs';
import path from 'node:path';
import { regenStateOf, withLocks } from '../regionRegen.ts';
import type { GenLayout } from '../schema.ts';
import { validateGenLayout } from '../schema.ts';
import { NAME_RE } from './files.ts';

export interface SaveLocksResult { target: 'accepted' | 'draft'; file: string; locks: number }

export function saveLocks(layout: GenLayout, dirs: { accepted: string; drafts: string }): SaveLocksResult {
  if (!NAME_RE.test(layout.name)) throw new Error('bad layout name');
  const locks = regenStateOf(layout).locks;
  const accepted = path.join(dirs.accepted, `${layout.name}.json`);
  if (fs.existsSync(accepted)) {
    const saved = JSON.parse(fs.readFileSync(accepted, 'utf8')) as GenLayout;
    const next = withLocks(saved, locks);
    const errors = validateGenLayout(next).filter((e) => !validateGenLayout(saved).includes(e));
    if (errors.length) throw new Error(errors.join('; '));
    fs.writeFileSync(accepted, JSON.stringify(next, null, 1) + '\n');
    return { target: 'accepted', file: accepted, locks: locks.length };
  }
  fs.mkdirSync(dirs.drafts, { recursive: true });
  const draft = path.join(dirs.drafts, `${layout.name}.json`);
  fs.writeFileSync(draft, JSON.stringify(layout, null, 1) + '\n');
  return { target: 'draft', file: draft, locks: locks.length };
}
