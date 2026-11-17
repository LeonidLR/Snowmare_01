// Training Ground / level generator v2 - MISSION GOALS for the game (designer 2026-11-17, TASK_BOARD F-4 option «а»).
// The layout's `objectives` block (objectives.ts: start + 2-4 goals, layout units = UE cm on the ground plane) becomes the runtime
// file Content/Data/LevelGen/objectives/<Map>.objectives.json that the game reads (C++ UMissionGoalsSubsystem, MissionGoalsRules):
// every goal in WORLD space (x, y, z cm; z = ground_z + the layer's level x 300 cm, the builder's z_of / layer_level in
// Scripts/Editor/build_levelgen_map.py), its kind, layer, reach radius and - for a room goal - the room's floor polygon (the
// building's rotation applied), so "inside the room" counts. The start is not a goal (it is already the map's PlayerStart).
// Written next to the props file by «Принять» / «Собрать в Unreal» (server/levelgenApi.ts) and for the existing generated maps by
// scripts/export-mission-goals.ts. Pure: no file access here except writeMissionGoalsFile.

import fs from 'node:fs';
import path from 'node:path';
import type { Vec2 } from '../movement/geometry.ts';
import { buildingRectPoly, footprintPoly, toBuildingFrame } from './rotation.ts';
import type { GenLayout, ObjectiveGoal } from './schema.ts';

export const MISSION_GOALS_SCHEMA = 'codex.mission_goals/1';
/** A goal is reached within this 2D distance of its point, cm (designer decision pending; C++ MissionGoalsRules::DefaultRadiusCm). */
export const DEFAULT_GOAL_RADIUS_CM = 300;
/** The operative's feet must be within this of the goal's floor, cm (C++ MissionGoalsRules::DefaultFloorToleranceCm). */
export const DEFAULT_FLOOR_TOLERANCE_CM = 150;
/** cm per height level (build_levelgen_map.py LEVEL_HEIGHT). */
export const LEVEL_HEIGHT_CM = 300;

export interface MissionGoalEntry {
  id: string;
  kind: ObjectiveGoal['kind'];
  layer: string;
  /** World cm: x, y on the plan, z = the floor top of the goal's layer. */
  pos: [number, number, number];
  radius_cm: number;
  building?: string;
  zone?: string;
  /** Room goals: the room's floor polygon (world cm, 4 corners); the goal counts anywhere inside it. */
  area?: Vec2[];
}

export interface MissionGoalsFile {
  schema: typeof MISSION_GOALS_SCHEMA;
  map: string;
  layout: string;
  /** "free" = any order (the generator's only order today); "sequence" = only the next goal counts. */
  order: 'free' | 'sequence';
  default_radius_cm: number;
  floor_tolerance_cm: number;
  goals: MissionGoalEntry[];
}

/** The floor top of a walk layer, world cm (the cut: its own floor_z; else ground_z + level x LEVEL_HEIGHT_CM). */
export function layerZ(l: GenLayout, layer: string): number {
  const gz = Number(l.ground_z ?? 0);
  if (layer === 'cut' && l.cut && Number.isFinite(l.cut.floor_z)) return gz + l.cut.floor_z;
  return gz + layerLevel(l, layer) * LEVEL_HEIGHT_CM;
}

/** The level of a walk layer: "ground" 0, "cut" the cut's level, "roof:<B>" its floors, "floor:<B>:<n>" n (build_levelgen_map.py layer_level). */
export function layerLevel(l: GenLayout, layer: string): number {
  if (!layer || layer === 'ground') return 0;
  if (layer === 'cut') return Number(l.cut?.level ?? -1);
  const [kind, bid, n] = layer.split(':');
  const b = l.buildings.find((x) => x.id === bid);
  if (!b) return 0;
  return kind === 'roof' ? Number(b.floors) : Number(n || 1);
}

const round = (v: number) => Math.round(v * 10) / 10;

/** The polygon a room goal counts inside: the floor's room holding the goal point, else the whole footprint. */
export function roomArea(l: GenLayout, g: ObjectiveGoal, floor: number): Vec2[] | undefined {
  const b = l.buildings.find((x) => x.id === g.building);
  if (!b) return undefined;
  const local = toBuildingFrame(b, g.pos);
  const room = (b.rooms ?? []).find((r) => r.floor === floor
    && local[0] >= r.bounds.min[0] && local[0] <= r.bounds.max[0] && local[1] >= r.bounds.min[1] && local[1] <= r.bounds.max[1]);
  const poly = room ? buildingRectPoly(b, [room.bounds.min, room.bounds.max]) : footprintPoly(b);
  return poly.map((p) => [round(p[0]), round(p[1])] as Vec2);
}

/** The runtime goals file of a layout, or null when it has no `objectives` block (nothing to write: the game then has no goals). */
export function missionGoalsFor(l: GenLayout, mapName: string, layoutName = l.name, radiusCm = DEFAULT_GOAL_RADIUS_CM): MissionGoalsFile | null {
  const ob = l.objectives;
  if (!ob || !Array.isArray(ob.goals) || !ob.goals.length) return null;
  const goals = ob.goals.map((g): MissionGoalEntry => {
    const level = layerLevel(l, g.layer);
    const e: MissionGoalEntry = { id: g.id, kind: g.kind, layer: g.layer || 'ground', pos: [round(g.pos[0]), round(g.pos[1]), round(layerZ(l, g.layer || 'ground'))], radius_cm: radiusCm };
    if (g.building) e.building = g.building;
    if (g.zone) e.zone = g.zone;
    if (g.kind === 'room') {
      const area = roomArea(l, g, Math.max(0, level));
      if (area) e.area = area;
    }
    return e;
  });
  const order = (ob.order as string) === 'sequence' ? 'sequence' : 'free';
  return { schema: MISSION_GOALS_SCHEMA, map: mapName, layout: layoutName, order, default_radius_cm: radiusCm, floor_tolerance_cm: DEFAULT_FLOOR_TOLERANCE_CM, goals };
}

/** Stable text (2-space JSON + newline): the committed files diff cleanly. */
export const formatMissionGoals = (f: MissionGoalsFile): string => JSON.stringify(f, null, 2) + '\n';

/** <dir>/<Map>.objectives.json. */
export const missionGoalsPath = (dir: string, mapName: string): string => path.join(dir, `${mapName}.objectives.json`);

/**
 * Write (or refresh) the map's goals file into `dir` (Content/Data/LevelGen/objectives). A layout without objectives writes nothing and
 * leaves an existing file alone (returns null). Returns the written path.
 */
export function writeMissionGoalsFile(l: GenLayout, dir: string, mapName: string, layoutName = l.name): string | null {
  const f = missionGoalsFor(l, mapName, layoutName);
  if (!f) return null;
  fs.mkdirSync(dir, { recursive: true });
  const file = missionGoalsPath(dir, mapName);
  fs.writeFileSync(file, formatMissionGoals(f), 'utf8');
  return file;
}
