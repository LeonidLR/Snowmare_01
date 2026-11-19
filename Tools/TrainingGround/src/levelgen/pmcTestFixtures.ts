// Test fixtures of the PMC posts / charges tests (task 087): the accepted X4 layout + its committed tactical map, and a hand map.
import * as fs from 'node:fs';
import * as path from 'node:path';
import type { GenLayout } from './schema.ts';
import type { TacticalMapFile } from './tacticalMap.ts';
import { findProjectRoot } from './props/autogen.ts';

const readJson = <T>(rel: string): T => JSON.parse(fs.readFileSync(path.join(findProjectRoot(), rel), 'utf8').replace(/^﻿/, '')) as T;

/** The accepted X4 layout and Content/Data/LevelGen/tactical/L_Gen_Depot_X4.tactical.json (the same geometry_hash). */
export const loadX4 = (): { layout: GenLayout; tactical: TacticalMapFile } => ({
  layout: readJson<GenLayout>('Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x4.json'),
  tactical: readJson<TacticalMapFile>('Content/Data/LevelGen/tactical/L_Gen_Depot_X4.tactical.json'),
});

/** A 100 x 100 m map: the mandatory route runs west -> east along y = 0 from the start at x -45 m. */
export const tinyLayout = (): GenLayout => ({
  schema: 'codex.levelgen_layout/1', name: 'tiny_pmc', profile: 'test', seed: 1, params: { size_m: [100, 100], density: 1, variants: 1, verticality: 0 },
  bounds: { min: [-5000, -5000], max: [5000, 5000] }, ground_z: 0, spawn: { pos: [-4500, 0], yaw: 0 }, end: [4500, 0],
  walls: [], buildings: [], ladders: [], covers: [], zones: [], spawn_points: [], ambush_points: [], patrol_routes: [],
  routes: [{ id: 'R_main', mandatory: true, points: [[-4500, 0], [4500, 0]] }],
} as unknown as GenLayout);

const hs = (id: string, pos: [number, number], place?: string) => ({
  id, layer: 'ground', pos: [pos[0], pos[1], 0], facing_yaw: -90, reveal_normal_m: 5, reveal_stealth_m: 3, stealth_only: false, blind: false, rush_m: 5,
  cover: 'cover:C', cover_height_cm: 120, threatens: { line: 'route:R_main', route: 'R_main', pos: [pos[0], 0, 0] }, ...(place ? { place } : {}),
});

/** A chokepoint outline round x 40..50 m and one hiding spot per rule (inside / at the start / on the route / far / three good ones). */
export const tinyTactical = (): TacticalMapFile => ({
  schema: 'codex.tactical_map/1', map: 'L_Gen_Tiny', layout: 'tiny_pmc', geometry_hash: 'h', analysis_version: 1, analysis_source: 'computed',
  conditions: { weather: 'clear', time_of_day: 'day' }, ground_z: 0, thresholds: {} as TacticalMapFile['thresholds'],
  chokepoints: [{ id: 'CP_01', kind: 'narrow', kind_name: 'n', tags: ['narrow'], score: 0.8, layer: 'ground', pos: [4500, 0, 0], line: [], poly: [[4000, -600], [5000, -600], [5000, 600], [4000, 600]],
    length_m: 10, width_m: 4, lane_m: 4, route: 'mandatory', sources: [], alternative: 'none', detour_m: null, drop_spots: [], windows: [], overwatch: [], hiding_spots: ['HS_in'], reveal_m: 5, reveal_stealth_m: 3 }],
  drop_spots: [], windows: [], overwatch_roofs: [], ladder_only_roofs: [],
  hiding_spots: [
    hs('HS_in', [4500, 300], 'CP_01'), // inside the chokepoint outline
    hs('HS_start', [-4200, 800]), // 8.5 m from the start
    hs('HS_route', [0, 150]), // 1.5 m off the route
    hs('HS_far', [0, 4900]), // 49 m off the route (far with a 40 m limit)
    hs('HS_ok1', [1000, 1200], 'CP_01'), hs('HS_ok2', [2500, -1500]), hs('HS_ok3', [-500, 1500]),
  ] as TacticalMapFile['hiding_spots'],
});
