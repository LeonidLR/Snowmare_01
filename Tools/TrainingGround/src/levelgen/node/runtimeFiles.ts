// Training Ground / level generator - the per-map RUNTIME files the game reads, written by «Принять» (POST /api/levelgen/accept) and
// refreshed before «Собрать в Unreal» (POST /api/levelgen/build-map) in server/levelgenApi.ts:
//   Content/Data/LevelGen/objectives/<Map>.objectives.json - the mission goals (missionGoals.ts, F-4 «а»); none without `objectives`;
//   Content/Data/LevelGen/tactical/<Map>.tactical.json     - the tactical map (tacticalMap.ts, TACTICAL_MAP.md P1); always written
//                                                             (the layout's current analysis, else computed as «Принять» does).
// Each file is independent: a failure of one is reported and does not stop the other (nor the accept / build).
import path from 'node:path';
import { writeMissionGoalsFile } from '../missionGoals.ts';
import type { GenLayout } from '../schema.ts';
import { writeTacticalMapFile } from '../tacticalMap.ts';

export interface RuntimeDirs { objectives: string; tactical: string }

/** The runtime dirs next to Content/Data/LevelGen/accepted. */
export const runtimeDirsFor = (acceptedDir: string): RuntimeDirs => ({ objectives: path.join(acceptedDir, '..', 'objectives'), tactical: path.join(acceptedDir, '..', 'tactical') });

export interface RuntimeFilesResult { objectives: string | null; tactical: string | null; errors: string[] }

/** Writes the map's runtime files; never throws. */
export function writeRuntimeFiles(l: GenLayout, dirs: RuntimeDirs, mapName: string, layoutName = l.name): RuntimeFilesResult {
  const r: RuntimeFilesResult = { objectives: null, tactical: null, errors: [] };
  try { r.objectives = writeMissionGoalsFile(l, dirs.objectives, mapName, layoutName); } catch (e) { r.errors.push(`objectives: ${e instanceof Error ? e.message : String(e)}`); }
  try { r.tactical = writeTacticalMapFile(l, dirs.tactical, mapName, layoutName); } catch (e) { r.errors.push(`tactical: ${e instanceof Error ? e.message : String(e)}`); }
  return r;
}
