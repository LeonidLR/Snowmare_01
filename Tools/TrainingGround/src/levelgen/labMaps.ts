// Three small hand-authored LAB maps for the smart-enemies scenario clips (docs/design/smart-enemies, task T-02). Each one is the
// smallest map that holds ONE designer clip's geometry, placed by hand like the training arenas (trainingArenas.ts):
//   - L_Lab_Crossroads (DS-00A, hammer & anvil): an open crossroads, cover on the far side (the hammer), a walled "graveyard" with
//     one gap on the near side (the anvil), the attackers come 63 m up the N-S street;
//   - L_Lab_Alley (DS-00B, height + corner + cut-off): a 4 m lane between a low building and a wall / a tall building with a walkable
//     roof, the lane mouth at its east end, the way round the back behind the low building;
//   - L_Lab_Bridge (DS-00C, two ends + cut-off from an obstacle top): a trench across the map with ONE bridge, cover at the north
//     end, a climbable obstacle at the south end, a two-storey building with windows onto the deck, two ladders out of the trench.
// Each layout carries an explicit `objectives` block (the start + ONE goal), a mandatory R_main from the start to the goal, the
// designed ambush points and one spawn point at the start. Coordinates in the draft are metres (x east, y SOUTH), converted once to
// UE cm by finalizeDraftToLayout. `node src/levelgen/labMaps.ts` writes the accepted layouts AND the runtime files (the mission
// goals + the tactical map) of the three maps; the layouts are deterministic (seeds 401 / 402 / 403).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import type { Vec2 } from '../movement/geometry.ts';
import { rng } from '../movement/geometry.ts';
import type { Draft, DraftBuilding, Rect } from './generate.ts';
import { addBuilding, addLadderAt, addWall, COVER_SIZE, finalizeDraftToLayout, splitWall } from './generate.ts';
import { obstacleHeight } from './obstacleHeights.ts';
import type { RuntimeFilesResult } from './node/runtimeFiles.ts';
import { runtimeDirsFor, writeRuntimeFiles } from './node/runtimeFiles.ts';
import type { Profile } from './profile.ts';
import { PROFILE_SCHEMA, toUE } from './profile.ts';
import type { CoverKind, GenLayout, GenRoute, SetPiece } from './schema.ts';
import { CUT_LEVEL, DEFAULT_PARAMS, LEVEL_HEIGHT, OBJECTIVES_VERSION, ladderForLevels, levelZ, rect, validateGenLayout } from './schema.ts';

export type LabMapId = 'crossroads' | 'alley' | 'bridge';
export const LAB_MAP_IDS: readonly LabMapId[] = ['crossroads', 'alley', 'bridge'];

export interface LabMapBundle {
  id: LabMapId;
  /** The accepted layout's name (Content/Data/LevelGen/accepted/<name>.json). */
  name: string;
  /** The level / runtime-file name (Content/Data/LevelJson/<mapName>.json, tactical/ + objectives/ files). */
  mapName: string;
  title: string;
  layout: GenLayout;
  profile: Profile;
  /** The designed start and goal, profile metres (x east, y south). */
  start: Vec2;
  goal: Vec2;
}

/** A standard full-height wall (not mantle-able, a man behind it is hidden): obstacle_heights.json wall_no_traverse. */
const WALL_H = obstacleHeight('wall_no_traverse');
/** The tallest mantle-able standard height: obstacle_heights.json high_mantle. */
const MANTLE_H = obstacleHeight('high_mantle');
const WALL_T = 30;

function makeEmptyDraft(): Draft {
  return {
    walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [],
  };
}

function makeZone(id: string, kind: Draft['zones'][number]['kind'], setPiece: SetPiece, poly: Vec2[], intent?: 'kill_zone'): Draft['zones'][number] {
  return { id, kind, set_piece: setPiece, poly, density: 1.0, coverKinds: ['crate', 'jersey'], ...(intent ? { intent } : {}) };
}

/** A cover (crate / jersey ...) with a mantle edge on each long face; yaw in the profile frame (0 = long axis along x). */
function addCover(d: Draft, id: string, kind: CoverKind, pos: Vec2, yaw = 0): void {
  const size = COVER_SIZE[kind];
  d.covers.push({ id, kind, pos, size: [size[0], size[1], size[2]], yaw });
  const rad = (yaw * Math.PI) / 180;
  const ux = Math.cos(rad), uy = Math.sin(rad);
  const nx = -uy, ny = ux;
  const halfLong = (size[0] / 100) * 0.48, halfDepth = size[1] / 200;
  for (const [suffix, s] of [['a', 1], ['b', -1]] as const) {
    const c: Vec2 = [pos[0] + nx * halfDepth * s, pos[1] + ny * halfDepth * s];
    d.mantle_edges.push({
      id: `me_${id}_${suffix}`,
      start: [c[0] - ux * halfLong, c[1] - uy * halfLong],
      end: [c[0] + ux * halfLong, c[1] + uy * halfLong],
      floor: 0,
      base_z: 0,
      height_cm: size[2],
      normal: [nx * s, ny * s],
      kind: kind === 'crate' ? 'crate' : 'obstacle',
    });
  }
}

/** A door's outside / inside approach points (1.8 m each side) - a copy of the training arenas' helper. */
function doorApproach(b: DraftBuilding, side: 'north' | 'south' | 'east' | 'west'): { door: DraftBuilding['doors'][number]; outside: Vec2; inside: Vec2 } {
  const [[x0, y0], [x1, y1]] = b.footprint;
  const door = b.doors.find((x) => {
    if (side === 'north') return Math.abs(x.pos[1] - y0) < 0.2;
    if (side === 'south') return Math.abs(x.pos[1] - y1) < 0.2;
    if (side === 'west') return Math.abs(x.pos[0] - x0) < 0.2;
    return Math.abs(x.pos[0] - x1) < 0.2;
  });
  if (!door) throw new Error(`${b.id}: no door on the ${side} side`);
  const [dx, dy] = door.pos;
  if (side === 'north') return { door, outside: [dx, y0 - 1.8], inside: [dx, y0 + 1.8] };
  if (side === 'south') return { door, outside: [dx, y1 + 1.8], inside: [dx, y1 - 1.8] };
  if (side === 'west') return { door, outside: [x0 - 1.8, dy], inside: [x0 + 1.8, dy] };
  return { door, outside: [x1 + 1.8, dy], inside: [x1 - 1.8, dy] };
}

function makeLabProfile(id: string, title: string, size: Vec2, start: Vec2, end: Vec2, mainPts: Vec2[]): Profile {
  return {
    schema: PROFILE_SCHEMA,
    id,
    title,
    bounds: { min: [0, 0], max: size },
    scale: { recommended: '1:1', ue_ground_plane_m: { recommended: size } },
    start: { pos: start, yaw_deg: 0 },
    end: { pos: end },
    ladders: [],
    zones: [{ id: 'Z_lab', kind: 'street', poly: rect.poly([[0, 0], size]), cover_per_100m2: 1.0, sightline_m: [10, 60], mechanics: [] }],
    buildings: [],
    route: { main: { id: 'R_main', points: mainPts, length_m: Math.round(mainPts.reduce((n, p, i) => (i ? n + Math.hypot(p[0] - mainPts[i - 1][0], p[1] - mainPts[i - 1][1]) : 0), 0)) }, variants: [] },
    sightlines: { lanes: [], stats_m: { p50: 20, p90: 40, max: 60, short_zone_share: 0.3 } },
    densities: { covers_per_100m2: {}, enterable_share: 0, roof_access_share: 0, ladders_per_ha: 0 },
    set_pieces: {},
    // a hand-placed lab map is not scored against a reference: no generator targets
    targets: {},
  };
}

interface LabSpec {
  id: LabMapId;
  name: string;
  mapName: string;
  title: string;
  seed: number;
  size: Vec2;
  start: Vec2;
  /** The UE yaw of the spawn / the start (0 = north / up on the plan, 90 = east). */
  startYaw: number;
  goal: Vec2;
}

export const LAB_SPECS: Record<LabMapId, LabSpec> = {
  crossroads: { id: 'crossroads', name: 'gen_lab_crossroads', mapName: 'L_Lab_Crossroads', title: 'Lab 1: Crossroads (hammer & anvil, DS-00A)', seed: 401, size: [60, 80], start: [30, 77], startYaw: 0, goal: [30, 1.5] },
  alley: { id: 'alley', name: 'gen_lab_alley', mapName: 'L_Lab_Alley', title: 'Lab 2: Alley (height, corner, cut-off, DS-00B)', seed: 402, size: [64, 48], start: [4, 26], startYaw: 90, goal: [51, 26] },
  bridge: { id: 'bridge', name: 'gen_lab_bridge', mapName: 'L_Lab_Bridge', title: 'Lab 3: Bridge (two ends, cut-off from an obstacle top, DS-00C)', seed: 403, size: [72, 60], start: [36, 57], startYaw: 0, goal: [36, 4] },
};

/** Draft -> layout (UE cm) with the explicit objectives block: the start + exactly one goal. */
function finish(spec: LabSpec, d: Draft, variants: number, profile: Profile, comment: string): LabMapBundle {
  const layout = finalizeDraftToLayout(d, spec.size, {
    name: spec.name,
    profile: profile.id,
    seed: spec.seed,
    params: { ...DEFAULT_PARAMS, size_m: spec.size, density: 1, variants, verticality: 3 },
    comment,
    start: spec.start,
    end: spec.goal,
    yaw: spec.startYaw,
  });
  const withGoal: GenLayout = {
    ...layout,
    objectives: {
      version: OBJECTIVES_VERSION,
      seed: spec.seed,
      order: 'free',
      start: { pos: toUE(spec.start, spec.size), yaw: spec.startYaw, layer: 'ground', source: 'manual' },
      goals: [{ id: 'GOAL_1', pos: toUE(spec.goal, spec.size), layer: 'ground', kind: 'street' }],
    },
  };
  return { id: spec.id, name: spec.name, mapName: spec.mapName, title: spec.title, layout: withGoal, profile, start: spec.start, goal: spec.goal };
}

const groundLayers = (n: number): string[] => Array(n).fill('ground');

/** Lab 1: the crossroads. */
function buildCrossroads(): LabMapBundle {
  const spec = LAB_SPECS.crossroads;
  const random = rng(spec.seed);
  const { start, goal } = spec;
  const d = makeEmptyDraft();

  // The crossing square of the E-W street (9 m, y 9.5..18.5) and the N-S street (9 m, x 25.5..34.5), centre (30, 14): the kill zone.
  const crossing: Rect = [[25.5, 9.5], [34.5, 18.5]];
  d.zones.push(makeZone('Z_killzone', 'corridor', 'street', rect.poly(crossing), 'kill_zone'));

  // Near side, south-west: one framing block (one storey, shut, no walkable roof) - the rest of the map stays open.
  addBuilding(d, 'B_sw_block', [[4, 22], [22, 40]], { height: LEVEL_HEIGHT, floors: 1, enterable: false, roof_walkable: false, set_piece: 'facade' }, random);

  // The hammer: four covers on the far (north) side, two each side of the N-S axis, every line reaches into the crossing.
  addCover(d, 'C_hammer_w1', 'crate', [22, 6]);
  addCover(d, 'C_hammer_w2', 'crate', [26, 4.5]);
  addCover(d, 'C_hammer_e1', 'crate', [34, 4.5]);
  addCover(d, 'C_hammer_e2', 'crate', [38, 6]);

  // The anvil: the walled "graveyard" east of the N-S street, [37..49] x [22..34], full-height walls (a man inside is hidden from the
  // crossing, no mantle), ONE gap 2.5 m wide in the west side (facing the street) centred at y = 28.
  const gy = 28, gw = 2.5;
  splitWall(d, 'W_grave_W', [37, 22], [37, 34], [[gy - gw / 2, gy + gw / 2]], WALL_H, WALL_T, 'wall');
  addWall(d, 'W_grave_N', [37, 22], [49, 22], WALL_H, WALL_T, 'wall');
  addWall(d, 'W_grave_E', [49, 22], [49, 34], WALL_H, WALL_T, 'wall');
  addWall(d, 'W_grave_S', [37, 34], [49, 34], WALL_H, WALL_T, 'wall');

  // The attackers' last bounds on the south street.
  addCover(d, 'C_appr_j1', 'jersey', [27, 42]);
  addCover(d, 'C_appr_c1', 'crate', [33, 52]);
  addCover(d, 'C_appr_j2', 'jersey', [27, 62]);

  const mainPts: Vec2[] = [start, [30, 62], [30, 42], [30, 24], [30, 14], goal];
  const main: GenRoute = { id: 'R_main', pace: 'walk', points: mainPts, layers: groundLayers(mainPts.length), links: [], set_pieces: ['street', 'street', 'street'], mandatory: true };
  const enclosure: GenRoute = {
    id: 'V1_enclosure',
    pace: 'walk',
    points: [start, [30, 50], [32, 34], [35.5, gy], [39, gy], [44, gy], [39, gy], [35.5, gy], [31, 20], [30, 12], goal],
    layers: groundLayers(11),
    links: [],
    set_pieces: ['street'],
  };
  d.routes.push(main, enclosure);

  d.spawn_points.push({ id: 'SP_start', pos: start, kind: 'edge' });
  d.ambush_points.push(
    { id: 'AMB_hammer_w', pos: [24, 7] },
    { id: 'AMB_hammer_e', pos: [36, 7] },
    { id: 'AMB_anvil', pos: [45, 28] },
  );

  const profile = makeLabProfile('lab_01_crossroads', spec.title, spec.size, start, goal, mainPts);
  return finish(spec, d, 1, profile, 'Hand-placed lab map for the smart-enemies clip DS-00A (hammer & anvil): an open crossroads, cover on the far side, a walled enclosure with one gap on the near side.');
}

/** Lab 2: the alley. */
function buildAlley(): LabMapBundle {
  const spec = LAB_SPECS.alley;
  const random = rng(spec.seed);
  const { start, goal } = spec;
  const d = makeEmptyDraft();

  // B_low: one storey, roof NOT walkable; the lane (y 24..28) runs along its south facade. A door on each long side (the north one
  // is the back route's), windows onto the lane.
  addBuilding(d, 'B_low', [[12, 14], [42, 24]], {
    height: LEVEL_HEIGHT, floors: 1, enterable: true, roof_walkable: false, set_piece: 'facade', doorSides: ['north', 'south'], windowSides: ['south'],
  }, random);

  // The lane's south side: a full-height wall from x 12 to 30, then B_tall. A second wall closes the ground south of it, so the lane
  // (and the back route) are the only ways east: nothing walks round the south.
  addWall(d, 'W_lane_s', [12, 28], [30, 28], WALL_H, WALL_T, 'wall');
  addWall(d, 'W_south_seal', [12, 28], [12, 48], WALL_H, WALL_T, 'wall');

  // B_tall: two storeys, a walkable roof (parapets from the builder), a stairwell, windows onto the lane (north), a ladder on the east.
  const tall = addBuilding(d, 'B_tall', [[30, 28], [46, 40]], {
    height: 2 * LEVEL_HEIGHT, floors: 2, enterable: true, roof_walkable: true, stairwell: true, set_piece: 'facade', doorSides: ['south', 'west'], windowSides: ['north'],
  }, random);
  addLadderAt(d, tall, 'east', random);
  const ladder = d.ladders[d.ladders.length - 1];

  // Open ground north of B_low (y 3..14): the way round the back, two crates on it.
  addCover(d, 'C_back_c1', 'crate', [24, 9.5]);
  addCover(d, 'C_back_c2', 'crate', [37, 10.5]);

  d.zones.push(makeZone('Z_lane', 'corridor', 'street', rect.poly([[12, 24], [44, 28]]), 'kill_zone'));

  const mainPts: Vec2[] = [start, [12, 26], [22, 26], [32, 26], [44, 26], goal];
  const main: GenRoute = { id: 'R_main', pace: 'walk', points: mainPts, layers: groundLayers(mainPts.length), links: [], set_pieces: ['street'], mandatory: true };
  const backPts: Vec2[] = [start, [8, 22], [8, 8], [20, 6.5], [32, 6.5], [46, 7], [50, 12], [51, 19], goal];
  const back: GenRoute = { id: 'V1_back', pace: 'walk', points: backPts, layers: groundLayers(backPts.length), links: [], set_pieces: ['street'] };
  // V2: the back way to the goal side, up the east ladder onto B_tall's roof, across it, and down the same ladder to the goal.
  const roofPts: Vec2[] = [...backPts.slice(0, 6), [50, 12], [50, ladder.foot[1]], ladder.foot, ladder.top, [38, ladder.top[1]], ladder.top, ladder.foot, goal];
  const roofLayers = [...groundLayers(9), 'roof:B_tall', 'roof:B_tall', 'roof:B_tall', 'ground', 'ground'];
  const roof: GenRoute = {
    id: 'V2_roof', pace: 'walk', points: roofPts, layers: roofLayers,
    links: [{ at: 9, via: ladder.id }, { at: 12, via: ladder.id }], set_pieces: ['ladder_overwatch'],
  };
  d.routes.push(main, back, roof);

  d.spawn_points.push({ id: 'SP_start', pos: start, kind: 'edge' });
  d.ambush_points.push(
    // ambush points are ground-only in the schema (no layer): the roof ambusher is placed at the ladder foot, the AI climbs from there
    { id: 'AMB_roof', pos: [ladder.foot[0], ladder.foot[1]] },
    { id: 'AMB_corner', pos: [44, 23] },
    { id: 'AMB_cutoff', pos: [10, 10] },
  );

  const profile = makeLabProfile('lab_02_alley', spec.title, spec.size, start, goal, mainPts);
  return finish(spec, d, 2, profile, 'Hand-placed lab map for the smart-enemies clip DS-00B (height + corner + cut-off): a 4 m lane between a low building and a wall / a tall building with a walkable roof, the way round the back.');
}

/** Lab 3: the bridge. */
function buildBridge(): LabMapBundle {
  const spec = LAB_SPECS.bridge;
  const random = rng(spec.seed);
  const { start, goal, size } = spec;
  const d = makeEmptyDraft();
  const w = size[0];

  // The trench across the full width (y 27..33, 6 m) with 1.5 m retaining walls on both rims, ONE crossing: the bridge x 34..38.
  const CUT_W = 6, CUT_WALL_T = 1.5, yCut = 30;
  const cutN = yCut - CUT_W / 2, cutS = yCut + CUT_W / 2;
  const bx0 = 34, bx1 = 38;
  d.cut = {
    poly: rect.poly([[0, cutN], [w, cutS]]),
    level: CUT_LEVEL,
    floor_z: levelZ(CUT_LEVEL),
    crossings: [{ id: 'X_bridge', poly: rect.poly([[bx0, cutN - CUT_WALL_T], [bx1, cutS + CUT_WALL_T]]), kind: 'bridge' }],
  };
  splitWall(d, 'CUTWALL_N', [0, cutN - CUT_WALL_T / 2], [w, cutN - CUT_WALL_T / 2], [[bx0, bx1]], MANTLE_H, CUT_WALL_T * 100, 'cut_wall');
  splitWall(d, 'CUTWALL_S', [0, cutS + CUT_WALL_T / 2], [w, cutS + CUT_WALL_T / 2], [[bx0, bx1]], MANTLE_H, CUT_WALL_T * 100, 'cut_wall');
  // the approach road (the attackers' street, x 31..41): a "road" zone for the map's layers, the deck's zone lies inside it
  d.zones.push(makeZone('Z_road', 'corridor', 'street', rect.poly([[31, 0], [41, size[1]]])));
  d.zones.push(makeZone('Z_deck', 'corridor', 'cut_crossing', rect.poly([[bx0, cutN - CUT_WALL_T], [bx1, cutS + CUT_WALL_T]]), 'kill_zone'));

  // Two ladders out of the trench (3 m = 8 rungs + a plinth): the south wall's trench face at x 10, the north wall's at x 62.
  const lad = ladderForLevels(1);
  d.ladders.push(
    { id: 'LAD_cut_s', pos: [10, cutS - 0.1], yaw: 270, rungs: lad.rungs, plinth_cm: lad.plinth_cm, from: 'cut', to: 'ground', level_from: CUT_LEVEL, level_to: 0, foot: [10, cutS - 1.1], top: [10, cutS + CUT_WALL_T + 1.0] },
    { id: 'LAD_cut_n', pos: [62, cutN + 0.1], yaw: 90, rungs: lad.rungs, plinth_cm: lad.plinth_cm, from: 'cut', to: 'ground', level_from: CUT_LEVEL, level_to: 0, foot: [62, cutN + 1.1], top: [62, cutN - CUT_WALL_T - 1.0] },
  );
  const ladS = d.ladders[0], ladN = d.ladders[1];

  // The near (north) end: a cover each side of the deck, a line along it. B_windows overlooks the deck and its north end.
  addCover(d, 'C_end_w', 'crate', [31.5, 23]);
  addCover(d, 'C_end_e', 'crate', [40.5, 23]);
  const bw = addBuilding(d, 'B_windows', [[16, 9], [29, 21]], {
    height: 2 * LEVEL_HEIGHT, floors: 2, enterable: true, roof_walkable: false, stairwell: true, set_piece: 'facade', doorSides: ['west', 'north'], windowSides: ['east', 'south'],
  }, random);

  // The far (south) end: a climbable obstacle east of the deck, 6 m x 1.2 m, a standard mantling height, a mantle edge on each long face.
  const oy = 37.5, ot = 1.2;
  addWall(d, 'W_obstacle', [41, oy], [47, oy], MANTLE_H, ot * 100, 'wall');
  for (const [suffix, s] of [['n', -1], ['s', 1]] as const) {
    d.mantle_edges.push({ id: `me_W_obstacle_${suffix}`, start: [41, oy + (ot / 2) * s], end: [47, oy + (ot / 2) * s], floor: 0, base_z: 0, height_cm: MANTLE_H, normal: [0, s], kind: 'obstacle' });
  }

  // The attackers' approach covers south of the cut.
  addCover(d, 'C_appr_j1', 'jersey', [29, 46]);
  addCover(d, 'C_appr_c1', 'crate', [43, 50]);

  const mainPts: Vec2[] = [start, [36, 46], [36, 40], [36, 35.5], [36, 30], [36, 24], [36, 14], goal];
  const main: GenRoute = { id: 'R_main', pace: 'walk', points: mainPts, layers: groundLayers(mainPts.length), links: [], set_pieces: ['street', 'cut_crossing'], mandatory: true };
  // V1: down the south ladder into the trench, along its floor, up the north ladder (a kill zone by design, like the generator's V3).
  const trenchPts: Vec2[] = [start, [20, 46], [10, 40], ladS.top, ladS.foot, [36, yCut], ladN.foot, ladN.top, [50, 14], goal];
  const trench: GenRoute = {
    id: 'V1_trench', pace: 'walk', points: trenchPts, layers: ['ground', 'ground', 'ground', 'ground', 'cut', 'cut', 'cut', 'ground', 'ground', 'ground'],
    links: [{ at: 4, via: ladS.id }, { at: 7, via: ladN.id }], set_pieces: ['cut_crossing'],
    intent: 'kill_zone', intent_note: 'the trench floor is seen from both rims; the ladders are the only ways out (a way for a man who dropped, not a plan)',
  };
  // V2: across the bridge and into B_windows by its west door, out by the north door, to the goal.
  const west = doorApproach(bw, 'west'), north = doorApproach(bw, 'north');
  const windowsPts: Vec2[] = [start, [36, 46], [36, 36], [36, 28], [34, 22.5], [west.outside[0], 22.5], west.outside, west.inside, [west.inside[0] + 3, west.inside[1]], north.inside, north.outside, [36, 6], goal];
  const windows: GenRoute = { id: 'V2_windows', pace: 'walk', points: windowsPts, layers: groundLayers(windowsPts.length), links: [], set_pieces: ['cut_crossing', 'facade'] };
  d.routes.push(main, trench, windows);

  d.spawn_points.push({ id: 'SP_start', pos: start, kind: 'edge' });
  d.ambush_points.push(
    { id: 'AMB_end_w', pos: [31.5, 21.5] },
    { id: 'AMB_end_e', pos: [40.5, 21.5] },
    // the foot of the obstacle on its south side: the AI mantles from here
    { id: 'AMB_obstacle', pos: [44, 39] },
  );

  const profile = makeLabProfile('lab_03_bridge', spec.title, size, start, goal, mainPts);
  return finish(spec, d, 2, profile, 'Hand-placed lab map for the smart-enemies clip DS-00C (two ends + cut-off from an obstacle top): a trench with one bridge, cover at the north end, a climbable obstacle at the south end, a building overlooking the deck.');
}

export function buildLabMap(id: LabMapId): LabMapBundle {
  if (id === 'crossroads') return buildCrossroads();
  if (id === 'alley') return buildAlley();
  return buildBridge();
}

export function buildAllLabMaps(): LabMapBundle[] {
  return LAB_MAP_IDS.map(buildLabMap);
}

export interface WrittenLabMap { name: string; accepted: string; runtime: RuntimeFilesResult }

/** Writes the accepted layouts (validated) + the runtime files (tactical + objectives) of the three lab maps. */
export function writeLabMaps(acceptedDir?: string): WrittenLabMap[] {
  const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
  const outDir = acceptedDir ?? path.join(root, 'Content/Data/LevelGen/accepted');
  fs.mkdirSync(outDir, { recursive: true });
  const written: WrittenLabMap[] = [];
  for (const lab of buildAllLabMaps()) {
    const errors = validateGenLayout(lab.layout);
    if (errors.length) throw new Error(`${lab.name} invalid: ${errors.join('; ')}`);
    const accepted = path.join(outDir, `${lab.name}.json`);
    fs.writeFileSync(accepted, JSON.stringify(lab.layout, null, 1) + '\n', 'utf8');
    const runtime = writeRuntimeFiles(lab.layout, runtimeDirsFor(outDir), lab.mapName);
    if (runtime.errors.length) throw new Error(`${lab.name} runtime files: ${runtime.errors.join('; ')}`);
    written.push({ name: lab.name, accepted, runtime });
  }
  return written;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  for (const w of writeLabMaps()) console.log(`wrote ${w.accepted}\n      ${w.runtime.objectives}\n      ${w.runtime.tactical}`);
}
