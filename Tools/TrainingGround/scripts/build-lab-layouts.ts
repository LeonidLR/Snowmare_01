#!/usr/bin/env node
// Standalone combat lab layout generator for smart enemies roadmap (section T-02):
//   - L_Lab_Crossroads (gen_lab_crossroads.json)
//   - L_Lab_Alley (gen_lab_alley.json)
//   - L_Lab_Bridge (gen_lab_bridge.json)
//
// Usage:
//   node scripts/build-lab-layouts.ts

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import type { Vec2 } from '../src/movement/geometry.ts';
import { dist, rng } from '../src/movement/geometry.ts';
import type { Draft } from '../src/levelgen/generate.ts';
import { addBuilding, addWall, finalizeDraftToLayout } from '../src/levelgen/generate.ts';
import { writeMissionGoalsFile } from '../src/levelgen/missionGoals.ts';
import { obstacleHeight } from '../src/levelgen/obstacleHeights.ts';
import type { Profile } from '../src/levelgen/profile.ts';
import { fromUE, PROFILE_SCHEMA } from '../src/levelgen/profile.ts';
import { formatPropsCanonical } from '../src/levelgen/props/canonical.ts';
import { generatePropsForLayout, loadAutogenConfig, loadPropsBalance } from '../src/levelgen/props/autogen.ts';
import type { CoverKind, GenLayout } from '../src/levelgen/schema.ts';
import { CUT_DEPTH, CUT_LEVEL, DEFAULT_PARAMS, LEVEL_HEIGHT, rect, validateGenLayout } from '../src/levelgen/schema.ts';
import { tacticalMapFor, writeTacticalMapFile } from '../src/levelgen/tacticalMap.ts';

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

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
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
 * - Start to goal: exactly 70 m (in 60-80 m range)
 * - Crossroads in centre [0, 0]
 * - Walled enclosure on near side with 1 gap facing the crossroads
 * - >= 4 covers on far side (crates, sandbags, jersey)
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
    toM([-2000, 0]),
    toM([0, 0]),
    toM([2000, 0]),
    goalM,
  ];

  const d = makeEmptyDraft(mainPtsM);

  // Walled enclosure on near side (UE X in [-2200, -800], UE Y in [400, 1600]):
  addWall(d, 'W_cross_near_S', toM([-2200, 400]), toM([-2200, 1600]), 300, 20, 'wall');
  addWall(d, 'W_cross_near_W', toM([-2200, 400]), toM([-800, 400]), 300, 20, 'wall');
  addWall(d, 'W_cross_near_N', toM([-800, 400]), toM([-800, 1600]), 300, 20, 'wall');
  // East wall has 1 gap (gap between Y = 900 and Y = 1100, facing the crossroads):
  addWall(d, 'W_cross_near_E1', toM([-2200, 1600]), toM([-1600, 1600]), 300, 20, 'wall');
  addWall(d, 'W_cross_near_E2', toM([-1400, 1600]), toM([-800, 1600]), 300, 20, 'wall');

  d.zones.push({
    id: 'Z_ST_crossroads',
    kind: 'open',
    set_piece: 'street',
    poly: [toM([-1000, -1000]), toM([1000, -1000]), toM([1000, 1000]), toM([-1000, 1000])],
    density: 1.0,
    coverKinds: ['crate', 'jersey'],
  });

  // Covers on far side (UE X > 500, >= 4 required):
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
 * - Start: [-2500, 0], Goal at mouth: [1350, 0] (in lane, clear of B_two_storey wall at X=1500)
 * - Lane: 4.0 m wide (in 3-5 m range) between Y = -200 and Y = 200
 * - One-storey building B_low along lane (UE X in [-1500, 1200], UE Y in [200, 1600], height 300 cm)
 *   Door and windows face onto the lane (Y = 200 face, doorSides: ['west'], windowSides: ['west'])
 * - Two-storey building B_two_storey at mouth with walkable roof (height 600 cm), parapet, and ladder >= 7 rungs
 *   Ladder on lane-mouth face (X = 1500 face, facing south into lane mouth)
 * - Back route round back of low building (Y in [1600, 2300])
 */
export function buildLabAlley(): LabLayoutBundle {
  const size_m: Vec2 = [50, 60];
  const toM = (ue: Vec2): Vec2 => fromUE(ue, size_m);
  const random = rng(201);

  const startUE: Vec2 = [-2500, 0];
  // A1 fix: goal in the lane at X = 1350, clear of B_two_storey wall at X = 1500
  const goalUE: Vec2 = [1350, 0];
  const startM = toM(startUE);
  const goalM = toM(goalUE);

  const mainPtsM: Vec2[] = [
    startM,
    toM([-1500, 0]),
    toM([0, 0]),
    toM([1000, 0]),
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
  // A3 fix: door and windows face lane (Y = 200 face, which is 'west' in profile coordinates)
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
    doorSides: ['west'],
    windowSides: ['west'],
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
    doorSides: ['south'],
    windowSides: ['south', 'north', 'west'],
  }, random);

  // A2 fix: Ladder on lane-mouth face (UE X = 1500, facing South into the lane):
  // Profile side 'south' corresponds to UE X = 1500 face.
  // Place along x = 22.5 (UE Y = -250), right at the mouth of the 4m lane (Y in [-200, 200]).
  const ladderYProfile = rectTwo[1][1]; // y1 = 15 in profile, maps to UE X = 1500
  const ladderXProfile = toM([1500, -250])[0]; // 22.5
  const gap = 0.1, foot = 1.1;
  d.ladders.push({
    id: `LAD_${bTwo.id}`,
    pos: [ladderXProfile, ladderYProfile + gap],
    yaw: 90, // profile 90 -> UE 180 (facing South)
    rungs: 16, // floors = 2 -> 16 rungs >= 7
    plinth_cm: 17.6,
    from: 'ground',
    to: `roof:${bTwo.id}`,
    level_from: 0,
    level_to: 2,
    foot: [ladderXProfile, ladderYProfile + foot],
    top: [ladderXProfile, ladderYProfile - 1],
  });

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
 * - Climbable obstacle of MANTLING-table height beside far end (X = 450, Y = 250)
 * - Building B_overlook with windows onto deck (Y = -300 face, windowSides: ['east'])
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
        pos: toM([0, 0]),
        width: 4.0,
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

  // Building with windows onto deck (UE X in [650, 1800], UE Y in [-1800, -300]):
  // B1 fix: windows on wall facing the deck (UE Y = -300 face, which is 'east' in profile coordinates)
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
    windowSides: ['east'], // facing UE +Y towards the deck at Y in [-200, 200]
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

export function writeLabLayouts(): LabLayoutBundle[] {
  const bundles: LabLayoutBundle[] = [
    buildLabCrossroads(),
    buildLabAlley(),
    buildLabBridge(),
  ];

  for (const b of bundles) {
    const errs = validateGenLayout(b.layout);
    if (errs.length) {
      throw new Error(`Validation failed for ${b.name}: ${errs.join('; ')}`);
    }

    const layoutPath = path.join(ACCEPTED_DIR, `${b.name}.json`);
    const metaPath = path.join(ACCEPTED_DIR, `${b.name}.meta.json`);

    fs.mkdirSync(path.dirname(layoutPath), { recursive: true });
    fs.writeFileSync(layoutPath, JSON.stringify(b.layout, null, 1) + '\n', 'utf8');

    // Fix 4: Clean meta without invented Jev verdicts or fake score 1
    const metaDoc = {
      at: new Date().toISOString(),
      note: 'Standalone combat lab layout for smart enemies T-02 (layouts part); score and AI verdicts not computed',
      summary: [
        `# Level generator - ${b.name} (${b.title})`,
        `Combat lab layout for smart enemies roadmap (T-02). Map: ${b.mapName}`,
        `bounds: [${b.layout.bounds.min.join(', ')}] .. [${b.layout.bounds.max.join(', ')}] cm`,
        `buildings: ${b.layout.buildings.length}, covers: ${b.layout.covers.length}, routes: ${b.layout.routes.length}`,
      ],
    };
    fs.writeFileSync(metaPath, JSON.stringify(metaDoc, null, 1) + '\n', 'utf8');
    console.log(`wrote ${layoutPath}`);
    console.log(`wrote ${metaPath}`);
  }

  return bundles;
}

export function writeAllLabExports(): void {
  const bundles = writeLabLayouts();
  const cfg = loadAutogenConfig();
  const balance = loadPropsBalance();

  for (const b of bundles) {
    // 1. Tactical map export
    writeTacticalMapFile(b.layout, TACTICAL_DIR, b.mapName, b.name);
    console.log(`exported ${path.join(TACTICAL_DIR, `${b.mapName}.tactical.json`)}`);

    // 2. Objectives export
    if (b.layout.objectives) {
      const objPath = path.join(OBJECTIVES_DIR, `${b.mapName}.objectives.json`);
      writeMissionGoalsFile(b.layout.objectives, objPath);
      console.log(`exported ${objPath}`);
    }

    // 3. Props export
    const propsResult = generatePropsForLayout({
      layout: b.layout,
      profile: b.profile,
      autogenConfig: cfg,
      balance,
      seed: 1,
      mapName: b.mapName,
      layoutName: b.name,
      projectRoot: ROOT,
    });
    const propsPath = path.join(PROPS_DIR, `${b.mapName}.props.json`);
    fs.mkdirSync(path.dirname(propsPath), { recursive: true });
    fs.writeFileSync(propsPath, formatPropsCanonical(propsResult.props), 'utf8');
    console.log(`exported ${propsPath}`);

    // 4. LevelJson export
    const levelDoc = {
      levelgen_layout: `Content/Data/LevelGen/accepted/${b.name}.json`,
    };
    const levelJsonPath = path.join(LEVELJSON_DIR, `${b.mapName}.json`);
    fs.mkdirSync(path.dirname(levelJsonPath), { recursive: true });
    fs.writeFileSync(levelJsonPath, JSON.stringify(levelDoc, null, 2) + '\n', 'utf8');
    console.log(`exported ${levelJsonPath}`);
  }
}

// CLI execution:
writeAllLabExports();
