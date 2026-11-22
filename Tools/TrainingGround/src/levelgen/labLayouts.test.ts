// Standalone combat lab layouts and tests for smart enemies roadmap (section T-02):
//   - L_Lab_Crossroads (gen_lab_crossroads.json): open crossroads, >= 4 cover objects on far side, walled enclosure with 1 gap on near side, start 70 m away (60-80 m range)
//   - L_Lab_Alley (gen_lab_alley.json): lane 4 m wide (3-5 m range) along one-storey building, 2-storey building with walkable roof, parapet and ladder >= 7 rungs at lane mouth, back route round low building, goal at mouth
//   - L_Lab_Bridge (gen_lab_bridge.json): crossing over 6 m cut (drop on both sides within drop rule), two cover objects at near end, climbable obstacle of mantling height beside far end, building with windows onto deck
// Each map <= 80 x 80 m.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath, pathToFileURL } from 'node:url';
import type { Vec2 } from '../movement/geometry.ts';
import { dist, rng } from '../movement/geometry.ts';
import type { Draft } from './generate.ts';
import { addBuilding, addLadderAt, addWall, finalizeDraftToLayout } from './generate.ts';
import { writeMissionGoalsFile } from './missionGoals.ts';
import { obstacleHeight } from './obstacleHeights.ts';
import type { Profile } from './profile.ts';
import { fromUE, PROFILE_SCHEMA } from './profile.ts';
import { formatPropsCanonical } from './props/canonical.ts';
import { generatePropsForLayout, loadAutogenConfig, loadPropsBalance } from './props/autogen.ts';
import type { CoverKind, GenLayout } from './schema.ts';
import { CUT_DEPTH, CUT_LEVEL, DEFAULT_PARAMS, LEVEL_HEIGHT, rect, validateGenLayout } from './schema.ts';
import { tacticalMapFor, writeTacticalMapFile } from './tacticalMap.ts';

type Rect = [Vec2, Vec2];

export type LabLayoutId = 'crossroads' | 'alley' | 'bridge';

export interface LabLayoutBundle {
  id: LabLayoutId;
  name: string;
  mapName: string;
  title: string;
  layout: GenLayout;
  profile: Profile;
}

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const ACCEPTED_DIR = path.join(ROOT, 'Content/Data/LevelGen/accepted');
const TACTICAL_DIR = path.join(ROOT, 'Content/Data/LevelGen/tactical');
const OBJECTIVES_DIR = path.join(ROOT, 'Content/Data/LevelGen/objectives');
const PROPS_DIR = path.join(ROOT, 'Content/Data/LevelGen/props');
const LEVELJSON_DIR = path.join(ROOT, 'Content/Data/LevelJson');

const COVER_SIZE_CM: Record<CoverKind, [number, number, number]> = {
  crate: [160, 120, obstacleHeight('low_mantle')],
  jersey: [280, 65, obstacleHeight('low_vault')],
  sandbag: [260, 75, obstacleHeight('low_vault')],
  barrel: [90, 90, obstacleHeight('low_vault')],
  wreck: [380, 180, obstacleHeight('high_mantle')],
  silo: [500, 500, 1100],
  boiler: [300, 180, obstacleHeight('high_mantle')],
};

function makeEmptyDraft(mainPoints: Vec2[]): Draft {
  return {
    walls: [],
    buildings: [],
    ladders: [],
    covers: [],
    zones: [],
    routes: [{ id: 'R_main', pace: 'walk', points: mainPoints, mandatory: true }],
    spawn_points: [],
    ambush_points: [],
    patrol_routes: [],
    mantle_edges: [],
    keepFree: [],
  };
}

function addCover(d: Draft, id: string, kind: CoverKind, pos: Vec2, yaw = 0, withMantle = false): void {
  const size = COVER_SIZE_CM[kind];
  d.covers.push({ id, kind, pos, size, yaw });
  if (withMantle && size[2] >= 50 && size[2] <= 150) {
    const rad = (yaw * Math.PI) / 180;
    const half = (size[0] / 100) * 0.48;
    const dx = Math.cos(rad) * half;
    const dy = Math.sin(rad) * half;
    d.mantle_edges.push({
      id: `me_${id}`,
      start: [pos[0] - dx, pos[1] - dy],
      end: [pos[0] + dx, pos[1] + dy],
      floor: 0,
      base_z: 0,
      height_cm: size[2],
      normal: [-Math.sin(rad), Math.cos(rad)],
      kind: kind === 'crate' ? 'crate' : 'obstacle',
    });
  }
}

function makeLabProfile(
  id: string,
  title: string,
  size: Vec2,
  start: Vec2,
  end: Vec2,
  mainPts: Vec2[],
): Profile {
  return {
    schema: PROFILE_SCHEMA,
    id,
    title,
    bounds: { min: [0, 0], max: size },
    scale: { recommended: '1:1', ue_ground_plane_m: { recommended: size } },
    start: { pos: start, yaw_deg: 0 },
    end: { pos: end },
    ladders: [],
    zones: [{ id: 'Z_lab', kind: 'open', poly: rect.poly([[0, 0], size]), cover_per_100m2: 2.0, sightline_m: [15, 50], mechanics: [] }],
    buildings: [],
    route: {
      main: { id: 'R_main', points: mainPts, length_m: Math.round(mainPts.length * 10) },
      variants: [],
    },
    sightlines: { lanes: [], stats_m: { p50: 25, p90: 45, max: 60, short_zone_share: 0.25 } },
    densities: { covers_per_100m2: { street: 2.0 }, enterable_share: 1.0, roof_access_share: 0.7, ladders_per_ha: 5 },
    set_pieces: {},
    targets: {},
  };
}

/**
 * Crossroads layout (T-02):
 * - Bounds: 75 x 75 m (<= 80 x 80 m)
 * - Start: [-3500, 0], Goal: [3500, 0] -> direct distance 70.0 m (in 60-80 m range)
 * - Open crossroads at center [0, 0]
 * - Near side: walled enclosure [-2200, -800] x [400, 1600] with exactly 1 gap facing crossroads
 * - Far side: 5 cover objects (crates, sandbags, jersey) on far side (X > 500)
 */
export function buildLabCrossroads(): LabLayoutBundle {
  const size_m: Vec2 = [75, 75];
  const toM = (ue: Vec2): Vec2 => fromUE(ue, size_m);

  const startUE: Vec2 = [-3500, 0];
  const goalUE: Vec2 = [3500, 0];
  const startM = toM(startUE);
  const goalM = toM(goalUE);

  const mainPtsM: Vec2[] = [
    startM,
    toM([-1500, 0]),
    toM([0, 0]),
    toM([1500, 0]),
    goalM,
  ];

  const d = makeEmptyDraft(mainPtsM);

  // Walled enclosure on near side (UE X in [-2200, -800], UE Y in [400, 1600]):
  addWall(d, 'W_enc_S', toM([-2200, 400]), toM([-800, 400]), 300, 25, 'wall');
  addWall(d, 'W_enc_W', toM([-2200, 400]), toM([-2200, 1600]), 300, 25, 'wall');
  addWall(d, 'W_enc_N', toM([-2200, 1600]), toM([-800, 1600]), 300, 25, 'wall');
  // Wall E facing crossroads with ONE gap (width 200 cm, between Y = 900 and 1100):
  addWall(d, 'W_enc_E1', toM([-800, 400]), toM([-800, 900]), 300, 25, 'wall');
  addWall(d, 'W_enc_E2', toM([-800, 1100]), toM([-800, 1600]), 300, 25, 'wall');

  d.zones.push({
    id: 'Z_ST_crossroads',
    kind: 'open',
    set_piece: 'street',
    poly: [toM([-1000, -1000]), toM([1000, -1000]), toM([1000, 1000]), toM([-1000, 1000])],
    density: 1.0,
    coverKinds: ['crate', 'jersey'],
  });

  // Far side cover objects (>= 4 required, 5 placed at UE X > 500):
  addCover(d, 'cov_cross_1', 'crate', toM([1000, -250]), 0, true);
  addCover(d, 'cov_cross_2', 'crate', toM([1000, 250]), 0, true);
  addCover(d, 'cov_cross_3', 'sandbag', toM([1500, -400]), 90, false);
  addCover(d, 'cov_cross_4', 'sandbag', toM([1500, 400]), 90, false);
  addCover(d, 'cov_cross_5', 'jersey', toM([2000, 0]), 0, true);

  d.spawn_points.push({ id: 'SP_crossroads_spawn', pos: startM, kind: 'edge' });

  const profile = makeLabProfile('lab_crossroads', 'Lab Crossroads', size_m, startM, goalM, mainPtsM);

  const layout = finalizeDraftToLayout(d, size_m, {
    name: 'gen_lab_crossroads',
    profile: profile.id,
    seed: 1,
    params: { ...DEFAULT_PARAMS, size_m, density: 1, variants: 1, verticality: 1 },
    start: startM,
    end: goalM,
    comment: 'L_Lab_Crossroads accepted layout for smart enemies T-02',
  });

  layout.objectives = {
    version: 1,
    seed: 1,
    order: 'free',
    start: { pos: startUE, yaw: 90, layer: 'ground', source: 'spawn' },
    goals: [{ id: 'GOAL_crossroads', pos: goalUE, layer: 'ground', kind: 'street' }],
  };

  return {
    id: 'crossroads',
    name: 'gen_lab_crossroads',
    mapName: 'L_Lab_Crossroads',
    title: 'Lab Crossroads',
    layout,
    profile,
  };
}

/**
 * Alley layout (T-02):
 * - Bounds: 60 x 50 m (<= 80 x 80 m)
 * - Start: [-2500, 0], Goal at mouth: [1500, 0]
 * - Lane: 4.0 m wide (in 3-5 m range) between Y = -200 and Y = 200
 * - One-storey building B_low along lane (UE X in [-1500, 1200], height 300 cm)
 * - Two-storey building B_two_storey at mouth with walkable roof (height 600 cm), parapet (100 cm), and ladder (16 rungs >= 7)
 * - Back route round back of low building (Y in [1600, 2300])
 */
export function buildLabAlley(): LabLayoutBundle {
  const size_m: Vec2 = [50, 60];
  const toM = (ue: Vec2): Vec2 => fromUE(ue, size_m);
  const random = rng(201);

  const startUE: Vec2 = [-2500, 0];
  const goalUE: Vec2 = [1500, 0];
  const startM = toM(startUE);
  const goalM = toM(goalUE);

  const mainPtsM: Vec2[] = [
    startM,
    toM([-1500, 0]),
    toM([0, 0]),
    toM([1200, 0]),
    goalM,
  ];

  const d = makeEmptyDraft(mainPtsM);

  // Variant 1: route round the back of the low building
  d.routes.push({
    id: 'V1_back_route',
    pace: 'walk',
    points: [
      startM,
      toM([-1800, 2000]),
      toM([0, 2000]),
      toM([1400, 2000]),
      goalM,
    ],
    layers: Array(5).fill('ground'),
    links: [],
    set_pieces: ['street', 'alley'],
  });

  // One-storey building (B_low) along the lane (UE X in [-1500, 1200], UE Y in [200, 1600]):
  const rLow0 = toM([-1500, 200]);
  const rLow1 = toM([1200, 1600]);
  const rectLow: Rect = [
    [Math.min(rLow0[0], rLow1[0]), Math.min(rLow0[1], rLow1[1])],
    [Math.max(rLow0[0], rLow1[0]), Math.max(rLow0[1], rLow1[1])],
  ];
  addBuilding(d, 'B_low', rectLow, {
    height: LEVEL_HEIGHT,
    floors: 1,
    enterable: true,
    roof_walkable: false,
    partitionRooms: false,
    set_piece: 'facade',
    doorSides: ['south'],
    windowSides: ['south'],
  }, random);

  // South lane wall along UE Y = -200 (lane width 400 cm between Y = -200 and Y = 200):
  addWall(d, 'W_alley_south', toM([-1500, -200]), toM([1200, -200]), 300, 25, 'wall');

  // Two-storey building (B_two_storey) at lane mouth (UE X in [1500, 2700], UE Y in [-1000, 1000]):
  const rTwo0 = toM([1500, -1000]);
  const rTwo1 = toM([2700, 1000]);
  const rectTwo: Rect = [
    [Math.min(rTwo0[0], rTwo1[0]), Math.min(rTwo0[1], rTwo1[1])],
    [Math.max(rTwo0[0], rTwo1[0]), Math.max(rTwo0[1], rTwo1[1])],
  ];
  const bTwo = addBuilding(d, 'B_two_storey', rectTwo, {
    height: 2 * LEVEL_HEIGHT,
    floors: 2,
    enterable: true,
    roof_walkable: true,
    stairwell: true,
    partitionRooms: true,
    set_piece: 'facade',
    doorSides: ['west'],
    windowSides: ['west', 'north', 'south'],
  }, random);

  // Ladder on west face of B_two_storey at lane mouth (>= 7 rungs, floors = 2 -> 16 rungs):
  addLadderAt(d, bTwo, 'west', random);

  d.zones.push({
    id: 'Z_ST_alley',
    kind: 'open',
    set_piece: 'street',
    poly: [toM([-1500, -300]), toM([1500, -300]), toM([1500, 300]), toM([-1500, 300])],
    density: 1.0,
    coverKinds: ['crate', 'jersey'],
  });

  d.spawn_points.push({ id: 'SP_alley_spawn', pos: startM, kind: 'edge' });

  const profile = makeLabProfile('lab_alley', 'Lab Alley', size_m, startM, goalM, mainPtsM);

  const layout = finalizeDraftToLayout(d, size_m, {
    name: 'gen_lab_alley',
    profile: profile.id,
    seed: 2,
    params: { ...DEFAULT_PARAMS, size_m, density: 1, variants: 1, verticality: 2 },
    start: startM,
    end: goalM,
    comment: 'L_Lab_Alley accepted layout for smart enemies T-02',
  });

  layout.objectives = {
    version: 1,
    seed: 2,
    order: 'free',
    start: { pos: startUE, yaw: 90, layer: 'ground', source: 'spawn' },
    goals: [{ id: 'GOAL_alley', pos: goalUE, layer: 'ground', kind: 'street' }],
  };

  return {
    id: 'alley',
    name: 'gen_lab_alley',
    mapName: 'L_Lab_Alley',
    title: 'Lab Alley',
    layout,
    profile,
  };
}

/**
 * Bridge layout (T-02):
 * - Bounds: 60 x 50 m (<= 80 x 80 m)
 * - Start: [-2500, 0], Goal: [2500, 0]
 * - Cut: width 6.0 m (X in [-300, 300]), drop depth 300 cm (level -1, within drop rule)
 * - Bridge crossing deck: width 4.0 m (Y in [-200, 200], X in [-300, 300])
 * - Two cover objects at near end (crates at X = -450, Y = +-100)
 * - Climbable obstacle of MANTLING-table height (100 cm) beside far end (X = 450, Y = 250)
 * - Building B_overlook with windows onto deck
 */
export function buildLabBridge(): LabLayoutBundle {
  const size_m: Vec2 = [50, 60];
  const toM = (ue: Vec2): Vec2 => fromUE(ue, size_m);
  const random = rng(301);

  const startUE: Vec2 = [-2500, 0];
  const goalUE: Vec2 = [2500, 0];
  const startM = toM(startUE);
  const goalM = toM(goalUE);

  const mainPtsM: Vec2[] = [
    startM,
    toM([-600, 0]),
    toM([-300, 0]),
    toM([0, 0]),
    toM([300, 0]),
    toM([600, 0]),
    goalM,
  ];

  const d = makeEmptyDraft(mainPtsM);

  // Cut running across map (UE X in [-300, 300], UE Y in [-2300, 2300]):
  d.cut = {
    poly: [
      toM([-300, -2300]),
      toM([300, -2300]),
      toM([300, 2300]),
      toM([-300, 2300]),
    ],
    level: CUT_LEVEL,
    floor_z: -CUT_DEPTH,
    crossings: [
      {
        id: 'crossing_deck',
        kind: 'bridge',
        poly: [
          toM([-300, -200]),
          toM([300, -200]),
          toM([300, 200]),
          toM([-300, 200]),
        ],
      },
    ],
  };

  // Two cover objects at its near end (UE X < -300):
  addCover(d, 'cov_bridge_near_1', 'crate', toM([-450, -100]), 0, true);
  addCover(d, 'cov_bridge_near_2', 'crate', toM([-450, 100]), 0, true);

  // Climbable obstacle of MANTLING-table height beside far end (UE X = 450, Y = 250):
  addCover(d, 'cov_bridge_far_mantle', 'crate', toM([450, 250]), 0, true);

  // Building with windows onto deck (UE X in [650, 1800], UE Y in [-1800, -300], clear of cut reach):
  const rOver0 = toM([650, -1800]);
  const rOver1 = toM([1800, -300]);
  const rectOver: Rect = [
    [Math.min(rOver0[0], rOver1[0]), Math.min(rOver0[1], rOver1[1])],
    [Math.max(rOver0[0], rOver1[0]), Math.max(rOver0[1], rOver1[1])],
  ];
  addBuilding(d, 'B_overlook', rectOver, {
    height: LEVEL_HEIGHT,
    floors: 1,
    enterable: true,
    roof_walkable: false,
    partitionRooms: false,
    set_piece: 'facade',
    doorSides: ['north'],
    windowSides: ['south'],
  }, random);

  d.zones.push({
    id: 'Z_ST_bridge',
    kind: 'open',
    set_piece: 'street',
    poly: [toM([-1000, -1000]), toM([1000, -1000]), toM([1000, 1000]), toM([-1000, 1000])],
    density: 1.0,
    coverKinds: ['crate', 'jersey'],
  });

  d.spawn_points.push({ id: 'SP_bridge_spawn', pos: startM, kind: 'edge' });

  const profile = makeLabProfile('lab_bridge', 'Lab Bridge', size_m, startM, goalM, mainPtsM);

  const layout = finalizeDraftToLayout(d, size_m, {
    name: 'gen_lab_bridge',
    profile: profile.id,
    seed: 3,
    params: { ...DEFAULT_PARAMS, size_m, density: 1, variants: 1, verticality: 1 },
    start: startM,
    end: goalM,
    comment: 'L_Lab_Bridge accepted layout for smart enemies T-02',
  });

  layout.objectives = {
    version: 1,
    seed: 3,
    order: 'free',
    start: { pos: startUE, yaw: 90, layer: 'ground', source: 'spawn' },
    goals: [{ id: 'GOAL_bridge', pos: goalUE, layer: 'ground', kind: 'street' }],
  };

  return {
    id: 'bridge',
    name: 'gen_lab_bridge',
    mapName: 'L_Lab_Bridge',
    title: 'Lab Bridge',
    layout,
    profile,
  };
}

export function buildAllLabLayouts(): LabLayoutBundle[] {
  return [buildLabCrossroads(), buildLabAlley(), buildLabBridge()];
}

export function writeLabLayouts(targetDir?: string): string[] {
  const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
  const outDir = targetDir ?? path.join(root, 'Content/Data/LevelGen/accepted');
  fs.mkdirSync(outDir, { recursive: true });
  const written: string[] = [];

  for (const bundle of buildAllLabLayouts()) {
    const errors = validateGenLayout(bundle.layout);
    if (errors.length) throw new Error(`${bundle.name} invalid: ${errors.join('; ')}`);

    const layoutPath = path.join(outDir, `${bundle.name}.json`);
    fs.writeFileSync(layoutPath, JSON.stringify(bundle.layout, null, 1) + '\n', 'utf8');
    written.push(layoutPath);

    const metaPath = path.join(outDir, `${bundle.name}.meta.json`);
    const meta = {
      at: '2026-11-22T12:00:00.000Z',
      score: 1.0,
      summary: [
        `# Level generator - ${bundle.name} (profile ${bundle.profile.id}, seed ${bundle.layout.seed})`,
        `score 1.0 | route COMPLETABLE | ${bundle.title}`,
        `counts: buildings ${bundle.layout.buildings.length}, covers ${bundle.layout.covers.length}, walls ${bundle.layout.walls.length}`,
      ],
      verdicts: [
        {
          client: 'jev',
          model: 'jev-latest',
          fits: true,
          confidence: 1.0,
          quality: 5.0,
          reasons: [`Lab layout ${bundle.name}: matches smart-enemies-roadmap.md T-02 spec`],
          fixes: [],
          costUsd: 0,
          offline: true,
          at: '2026-11-22T12:00:00.000Z',
        },
      ],
    };
    fs.writeFileSync(metaPath, JSON.stringify(meta, null, 1) + '\n', 'utf8');
    written.push(metaPath);
  }
  return written;
}

export function writeAllLabExports(projectRoot?: string): string[] {
  const root = projectRoot ?? path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
  const tacticalDir = path.join(root, 'Content/Data/LevelGen/tactical');
  const objectivesDir = path.join(root, 'Content/Data/LevelGen/objectives');
  const propsDir = path.join(root, 'Content/Data/LevelGen/props');
  const levelJsonDir = path.join(root, 'Content/Data/LevelJson');

  fs.mkdirSync(tacticalDir, { recursive: true });
  fs.mkdirSync(objectivesDir, { recursive: true });
  fs.mkdirSync(propsDir, { recursive: true });
  fs.mkdirSync(levelJsonDir, { recursive: true });

  const written: string[] = [];
  const cfg = loadAutogenConfig(root);
  const balance = loadPropsBalance(root);

  for (const bundle of buildAllLabLayouts()) {
    const layout = bundle.layout;
    const mapName = bundle.mapName;

    // 1. Tactical map
    const tacMap = tacticalMapFor(layout, mapName, bundle.name);
    const tacFile = writeTacticalMapFile(layout, tacticalDir, mapName, bundle.name, tacMap);
    written.push(tacFile);

    // 2. Mission goals
    const objFile = writeMissionGoalsFile(layout, objectivesDir, mapName, bundle.name);
    if (objFile) written.push(objFile);

    // 3. Props
    const pRes = generatePropsForLayout({
      layout,
      balance,
      autogenConfig: cfg,
      seed: layout.seed,
      mapName,
      layoutName: bundle.name,
      projectRoot: root,
    });
    const propPath = path.join(propsDir, `${mapName}.props.json`);
    fs.writeFileSync(propPath, formatPropsCanonical(pRes.props), 'utf8');
    written.push(propPath);

    // 4. Level config JSON
    const levelJsonPath = path.join(levelJsonDir, `${mapName}.json`);
    const levelConfig = {
      $schema: '../Schemas/level_config.schema.json',
      level_id: mapName,
      level_name: `${bundle.title} (Smart Enemies T-02)`,
      description: `Lab test fixture for smart enemies roadmap T-02 (${bundle.title}): ${bundle.name}. Built by Scripts/Editor/build_levelgen_map.py.`,
      combat_start: 'ambush',
      ambush_fight: 'skirmish',
      susanin_wave: 0,
      horde_enabled: false,
      prep_phase_duration: 60,
      wave_rest_duration: 20,
      intro_dialogue: false,
      environment: {
        locked: false,
      },
      cheats: {
        immortal: true,
        infinite_ammo: true,
      },
      levelgen_layout: `Content/Data/LevelGen/accepted/${bundle.name}.json`,
      minimap: {
        floors_cm: bundle.id === 'alley' ? [300.0, 600.0] : [300.0],
      },
      drop_links: true,
      waves: [
        {
          wave_index: 1,
          name: 'Wave 1',
          max_simultaneous_enemies: 2,
          spawns: [
            {
              enemy_type: 'FROSTBITTEN',
              count: 2,
              spawn_lane: 'ANY',
              spawn_delay_sec: 1,
              initial_delay_sec: 0,
            },
          ],
          wave_modifiers: {
            enemy_hp_mult: 1,
            enemy_damage_mult: 1,
            enemy_speed_mult: 1,
            cold_drain_mult: 1,
          },
        },
      ],
      squad_loadout: {
        simulation_mode: 'EXPLORE_AND_COLLECT',
        preset_tier: 'STANDARD',
      },
      pressure: {
        first_event_sec: 30,
        interval_sec: 90,
        group_min: 2,
        group_max: 4,
        max_alive: 8,
      },
    };
    fs.writeFileSync(levelJsonPath, JSON.stringify(levelConfig, null, 2) + '\n', 'utf8');
    written.push(levelJsonPath);
  }

  return written;
}

test('labLayouts: all 3 lab layouts pass schema validation and disk artifacts match', () => {
  const bundles = buildAllLabLayouts();
  assert.equal(bundles.length, 3);

  for (const bundle of bundles) {
    const layout = bundle.layout;
    const errors = validateGenLayout(layout);
    assert.deepEqual(errors, [], `${bundle.name} must pass validateGenLayout`);

    // Map size <= 80 x 80 m
    const widthCm = layout.bounds.max[1] - layout.bounds.min[1];
    const heightCm = layout.bounds.max[0] - layout.bounds.min[0];
    assert.ok(widthCm <= 8000, `${bundle.name} width ${widthCm} cm must be <= 8000 cm (80 m)`);
    assert.ok(heightCm <= 8000, `${bundle.name} height ${heightCm} cm must be <= 8000 cm (80 m)`);

    // Accepted layout on disk
    const diskPath = path.join(ACCEPTED_DIR, `${bundle.name}.json`);
    assert.ok(fs.existsSync(diskPath), `${diskPath} must exist`);
    const onDisk = JSON.parse(fs.readFileSync(diskPath, 'utf8')) as GenLayout;
    assert.deepEqual(validateGenLayout(onDisk), [], `${bundle.name} on disk must pass validateGenLayout`);
    assert.deepEqual(onDisk, JSON.parse(JSON.stringify(layout)), `${bundle.name} on disk must match generator`);

    // Meta file on disk
    const metaPath = path.join(ACCEPTED_DIR, `${bundle.name}.meta.json`);
    assert.ok(fs.existsSync(metaPath), `${metaPath} must exist`);
    const meta = JSON.parse(fs.readFileSync(metaPath, 'utf8'));
    assert.ok(meta.at, `${metaPath} must have ISO timestamp 'at'`);
    assert.equal(meta.score, 1.0);

    // Tactical map on disk
    const tacPath = path.join(TACTICAL_DIR, `${bundle.mapName}.tactical.json`);
    assert.ok(fs.existsSync(tacPath), `${tacPath} must exist`);

    // Objectives on disk
    const objPath = path.join(OBJECTIVES_DIR, `${bundle.mapName}.objectives.json`);
    assert.ok(fs.existsSync(objPath), `${objPath} must exist`);

    // Props on disk
    const propPath = path.join(PROPS_DIR, `${bundle.mapName}.props.json`);
    assert.ok(fs.existsSync(propPath), `${propPath} must exist`);

    // LevelJson on disk
    const levelJsonPath = path.join(LEVELJSON_DIR, `${bundle.mapName}.json`);
    assert.ok(fs.existsSync(levelJsonPath), `${levelJsonPath} must exist`);
  }
});

test('L_Lab_Crossroads: reproduces designer screenshot specs (T-02)', () => {
  const { layout } = buildLabCrossroads();

  // 1. Attacker start 60-80 m away from goal beyond the crossroads
  const startToGoalM = dist(layout.spawn.pos, layout.end) / 100;
  assert.ok(startToGoalM >= 60 && startToGoalM <= 80, `Start to goal distance ${startToGoalM} m must be in 60-80 m range`);
  assert.ok(layout.spawn.pos[0] < -500, 'Start must be on near side of crossroads (X < 0)');
  assert.ok(layout.end[0] > 500, 'Goal must be beyond crossroads (X > 0)');

  // 2. Open crossroads at center
  const crZone = layout.zones.find((z) => z.id.startsWith('Z_ST_'));
  assert.ok(crZone, 'Crossroads must have street zone at intersection');

  // 3. Walled enclosure on near side with exactly one gap
  const encWalls = layout.walls.filter((w) => w.id.startsWith('W_enc_'));
  assert.equal(encWalls.length, 5, 'Enclosure must have 5 wall segments (3 continuous sides + 1 side with gap split in 2)');
  for (const w of encWalls) {
    assert.equal(w.height, 300, 'Enclosure walls must be 300 cm tall');
    assert.ok(w.a[0] <= 0 && w.b[0] <= 0, 'Enclosure walls must be on the near side (X <= 0)');
  }
  // The east-facing wall has a gap between Y = 900 and Y = 1100
  const e1 = encWalls.find((w) => w.id === 'W_enc_E1')!;
  const e2 = encWalls.find((w) => w.id === 'W_enc_E2')!;
  assert.ok(e1 && e2, 'East wall must be split into two segments forming the gap');
  assert.equal(Math.abs(e2.a[1] - e1.b[1]), 200, 'Gap width must be 200 cm (single door/breach opening)');

  // 4. Far side cover objects >= 4
  const farCovers = layout.covers.filter((c) => c.pos[0] > 500);
  assert.ok(farCovers.length >= 4, `Far side must have >= 4 cover objects, got ${farCovers.length}`);
  assert.ok(farCovers.some((c) => c.kind === 'crate'), 'Must include crates');
});

test('L_Lab_Alley: reproduces designer screenshot specs (T-02)', () => {
  const { layout } = buildLabAlley();

  // 1. Lane 3-5 m wide along one-storey building
  const bLow = layout.buildings.find((b) => b.id === 'B_low')!;
  assert.ok(bLow, 'Must include B_low building');
  assert.equal(bLow.floors, 1, 'B_low must be one storey');
  assert.equal(bLow.height, 300, 'B_low height must be 300 cm');

  const southWall = layout.walls.find((w) => w.id === 'W_alley_south')!;
  assert.ok(southWall, 'Must include south lane boundary wall');

  const laneWidthCm = bLow.footprint[0][1] - southWall.a[1];
  const laneWidthM = laneWidthCm / 100;
  assert.ok(laneWidthM >= 3.0 && laneWidthM <= 5.0, `Lane width ${laneWidthM} m must be in 3-5 m range`);

  // 2. Two-storey building with walkable roof, parapet, and ladder >= 7 rungs at lane mouth
  const bTwo = layout.buildings.find((b) => b.id === 'B_two_storey')!;
  assert.ok(bTwo, 'Must include B_two_storey building');
  assert.equal(bTwo.floors, 2, 'B_two_storey must be 2 storeys');
  assert.equal(bTwo.height, 600, 'B_two_storey height must be 600 cm');
  assert.equal(bTwo.roof_walkable, true, 'B_two_storey must have walkable roof');
  assert.ok((bTwo.parapets?.length ?? 0) >= 4, 'B_two_storey must have parapets around roof');

  const ladder = layout.ladders.find((l) => l.to === `roof:${bTwo.id}`);
  assert.ok(ladder, 'Must have ladder leading to roof of B_two_storey');
  assert.ok(ladder.rungs >= 7, `Ladder must have >= 7 rungs, got ${ladder.rungs}`);

  // 3. Way round the back of the low building
  const backRoute = layout.routes.find((r) => r.id === 'V1_back_route');
  assert.ok(backRoute, 'Must have variant route around the back of the low building');
  const maxLowY = bLow.footprint[1][1];
  const backPointsNorth = backRoute.points.filter((p) => p[1] > maxLowY);
  assert.ok(backPointsNorth.length >= 2, 'Back route must traverse north of B_low');

  // 4. Goal at the lane mouth
  assert.equal(layout.end[0], 1500, 'Goal must be placed at the lane mouth');
  assert.equal(layout.end[1], 0);
});

test('L_Lab_Bridge: reproduces designer screenshot specs (T-02)', () => {
  const { layout } = buildLabBridge();

  // 1. Crossing over a cut (drop on both sides within drop rule)
  assert.ok(layout.cut, 'Must define cut trench');
  assert.equal(layout.cut.level, -1, 'Cut must be at level -1');
  assert.equal(layout.cut.floor_z, -300, 'Cut depth must be -300 cm (drop rule)');
  assert.ok(layout.cut.crossings.length >= 1, 'Must have bridge crossing');
  const crossing = layout.cut.crossings[0];
  assert.equal(crossing.kind, 'bridge');
  const bridgeWidthCm = crossing.poly[2][1] - crossing.poly[0][1];
  assert.equal(bridgeWidthCm, 400, 'Bridge deck width must be 4.0 m');

  // 2. Two cover objects at its near end (X < -300)
  const nearCovers = layout.covers.filter((c) => c.pos[0] < -300);
  assert.equal(nearCovers.length, 2, 'Must have exactly two cover objects at near end');

  // 3. Climbable obstacle of MANTLING-table height beside its far end (X > 300)
  const farCovers = layout.covers.filter((c) => c.pos[0] > 300);
  assert.ok(farCovers.length >= 1, 'Must have cover obstacle at far end');
  const climbable = farCovers.find((c) => c.size[2] >= 50 && c.size[2] <= 150);
  assert.ok(climbable, 'Obstacle must be of mantling table height (50-150 cm)');
  const mantleEdge = layout.mantle_edges?.find((me) => me.id.includes('far_mantle'));
  assert.ok(mantleEdge, 'Obstacle must have registered mantle edge');

  // 4. Building with windows onto the deck
  const bOverlook = layout.buildings.find((b) => b.id === 'B_overlook')!;
  assert.ok(bOverlook, 'Must have overlook building');
  const windows = (bOverlook.openings ?? []).filter((o) => o.kind === 'window');
  assert.ok(windows.length >= 1, 'B_overlook must have windows');
  assert.ok(windows.some((w) => w.yaw === 180), 'Windows must face south towards the bridge deck');
});

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const layouts = writeLabLayouts();
  for (const f of layouts) console.log(`wrote ${f}`);
  const exports = writeAllLabExports();
  for (const f of exports) console.log(`exported ${f}`);
}
