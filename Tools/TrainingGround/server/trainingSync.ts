import fs from 'node:fs';
import path from 'node:path';

export const TRAINING_SYNC_FILES = ['L_Gen_Depot_1_X6.json', 'L_Gen_Depot_1_X6.sectors.json', 'L_Gen_Depot_1_X6.buildings_index.json'];

/** One direction, exact allowlist. Read all inputs before changing a destination. */
export function syncTrainingLevels(mainRoot: string, buildRoot: string): string {
  if (path.resolve(mainRoot) === path.resolve(buildRoot)) return '';
  const directory = 'Content/Data/LevelJson';
  const changes = TRAINING_SYNC_FILES.flatMap(name => {
    const source = path.join(mainRoot, directory, name), destination = path.join(buildRoot, directory, name);
    // Missing optional companions in old projects do not delete destination data.
    if (!fs.existsSync(source)) return [];
    const data = fs.readFileSync(source), old = fs.existsSync(destination) ? fs.readFileSync(destination) : undefined;
    return old?.equals(data) ? [] : [{ name, destination, data, old }];
  });
  if (!changes.length) return '';
  fs.mkdirSync(path.join(buildRoot, 'Saved/Backups'), { recursive: true });
  const backup = fs.mkdtempSync(path.join(buildRoot, 'Saved/Backups', `tg-sync-${new Date().toISOString().replace(/[:.]/g, '-')}-`));
  const details: string[] = [];
  for (const change of changes) {
    if (change.old) fs.writeFileSync(path.join(backup, change.name), change.old, { flag: 'wx' });
  }
  for (const { name, destination, data, old } of changes) {
    fs.mkdirSync(path.dirname(destination), { recursive: true });
    fs.writeFileSync(destination, data);
    let detail = name;
    if (name === TRAINING_SYNC_FILES[0]) {
      try {
        const before = old ? JSON.parse(old.toString('utf8').replace(/^\uFEFF/, '')).environment?.start_hour : undefined;
        const after = JSON.parse(data.toString('utf8').replace(/^\uFEFF/, '')).environment?.start_hour;
        if (before !== after) detail = `start_hour ${before ?? '—'} -> ${after ?? '—'}`;
      } catch { /* byte copy works for any authored content; runtime validates JSON */ }
    }
    details.push(detail);
    console.info(`[TG sync] ${name}: main -> ${buildRoot}; backup ${backup}; ${detail}`);
  }
  return `синхронизировано: ${details.join(', ')}`;
}
