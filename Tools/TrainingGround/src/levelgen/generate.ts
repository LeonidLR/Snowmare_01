// Training Ground / level generator: profile + seed + params -> an ORIGINAL greybox layout (schema codex.levelgen_layout/1).
// A tactical-pattern GRAMMAR (docs/design/levelgen/LEVEL_GENERATOR.md section 3): the map is a street skeleton (the west
// street, the T-junction, the north street along the cut, the east yard, the south street, the inner street) laid out to the
// profile's proportions, the blocks between the streets are filled with set-pieces (facade, one-entry compound, industrial
// hall, warehouse, junkyard field, tank farm, ruined two-level factory, open square, dead end, towers), the mandatory route
// follows the skeleton, the variants thread the set-pieces (compound gates, alleys, the cut, ladders, roofs, the ruin's holes).
// Deterministic: the same (profile, seed, params) give the same layout. Works in the profile frame (metres, x east, y south)
// and converts to UE cm at the end (profile.ts toUE). Never a copy of the reference: proportions and densities come from it,
// every position is jittered by the seed and every block is a greybox set-piece of ours.

import { obstacleHeight, standardObstacleHeight } from './obstacleHeights.ts';
import { runObstacleHeightGate } from './props/obstacleHeightGate.ts';
import type { Vec2 } from '../movement/geometry.ts';
import { dist, rng } from '../movement/geometry.ts';
import type { ZoneKind } from '../movement/layout.ts';
import type { Profile } from './profile.ts';
import { mapBuildingCoords } from './buildingCoordFields.ts';
import { addRoomAnchors } from './roomAnchors.ts';
import { toUE } from './profile.ts';
import type {
  Archetype,
  BuildingFloor,
  CoverKind,
  Door,
  FloorGap,
  GenBuilding,
  GenCover,
  GenCut,
  GenLadder,
  GenLayout,
  GenParams,
  GenPartition,
  GenRoom,
  GenRoute,
  GenWall,
  MantleEdge,
  PatrolRoute,
  RoofHatch,
  RoofParapet,
  SetPiece,
  SpawnPoint,
  Stair,
  WallOpening,
} from './schema.ts';
import {
  CHIMNEY_HEIGHT_CM,
  CUT_BUILDING_MARGIN_CM,
  CUT_LEVEL,
  DEFAULT_PARAMS,
  HALL_ROOF_LEVEL,
  HOLE_HALF_CM,
  LEVELGEN_SCHEMA,
  LEVELGEN_SCHEMA_V2,
  LEVEL_HEIGHT,
  OPENING_DEFAULTS,
  STAIR_APPROACH_CM,
  STAIR_RUN_PER_RISE,
  TEMPLATE_ARCHETYPE,
  ladderForLevels,
  levelZ,
  rect,
} from './schema.ts';
import { WINDOW_JAMB_CM, windowOntoCore, windowPartitionClash } from './windowClearance.ts';
import type { BandPlan } from './buildings/bandPlan.ts';
import { BUILDING_PLANS, planBands, planBlock, snapFootprint } from './buildings/bandPlan.ts';
import type { PlanFrame } from './buildings/planFrame.ts';
import { OPPOSITE, SIDE_NORMAL, SIDE_YAW, cellRect, planDir, sharedSegs, toDraft, toPlan, vFlipped, wallKeyOf, wallSides, yawOfDir } from './buildings/planFrame.ts';
import type { PlanDoorsOpts, PlanOpening, WallKey } from './buildings/planDoors.ts';
import { WALL_ORDER, planDoors, wallSpan } from './buildings/planDoors.ts';
import { planWindows } from './buildings/planWindows.ts';
import { planUnits } from './buildings/planUnits.ts';
import { fnv1a } from './props/rng.ts';

type Rect = [Vec2, Vec2];

interface DraftBuilding extends Omit<GenBuilding, 'footprint' | 'doors' | 'stairs' | 'roof_holes' | 'floor_holes' | 'openings' | 'partitions' | 'floors_detail' | 'hatches' | 'parapets' | 'rooms' | 'mantle_edges'> {
  footprint: Rect;
  doors: Door[];
  openings: WallOpening[];
  partitions: GenPartition[];
  floors_detail: BuildingFloor[];
  hatches: RoofHatch[];
  parapets: RoofParapet[];
  rooms: GenRoom[];
  mantle_edges: MantleEdge[];
  stairs: Stair[];
  roof_holes: Vec2[];
  floor_holes: { pos: Vec2; floor: number }[];
}

/** Everything the grammar emits, in metres; converted once at the end. */
interface Draft {
  walls: { id: string; a: Vec2; b: Vec2; height: number; thickness: number; floor?: number; kind?: GenWall['kind']; building?: string }[];
  buildings: DraftBuilding[];
  cut?: GenCut;
  ladders: GenLadder[];
  covers: GenCover[];
  zones: { id: string; kind: ZoneKind; set_piece: SetPiece; poly: Vec2[]; density: number; coverKinds: CoverKind[]; intent?: 'kill_zone' }[];
  routes: GenRoute[];
  spawn_points: SpawnPoint[];
  ambush_points: { id: string; pos: Vec2; layer?: string }[];
  patrol_routes: PatrolRoute[];
  mantle_edges: MantleEdge[];
  /** Rectangles nothing may be placed in (streets, the route corridor). */
  keepFree: Rect[];
  /**
   * The `building_plans` toggle (S2.2): enterable offices get a band plan (addPlannedOffice) and the layout is schema /2. Set from the
   * layout params by every code that builds a Draft; never emitted. Absent = off = the legacy stages, byte for byte.
   */
  building_plans?: boolean;
}

const WALL_H = obstacleHeight('wall_no_traverse');        // cm, a greybox wall (the lab's)
const FENCE_H = obstacleHeight('high_fence_over');
const FENCE_T = 10;
const BUILDING_T = 30;
const DOOR_W = 1.8;        // m
const GATE_W = 3.0;        // m
const STREET_W = 9;        // m, curb to curb (scale B of the reference)
const ALLEY_W = 5;         // m
const CUT_W = 6;           // m, the trench
const CUT_WALL_T = 1.5;    // m
const CUT_WALL_H = obstacleHeight('high_mantle');    // cm, the retaining wall's parapet + fence above the ground (the ground-layer obstacle)

export interface GenerateOptions {
  profile: Profile;
  seed: number;
  params?: Partial<GenParams>;
  name?: string;
  /**
   * Region re-roll (levelgen v2 stage 1, regionRegen.ts): the seed of the SKELETON (sections 1-2: the streets, the cut, its
   * crossings and ladders). When it differs from `seed`, the skeleton is drawn from `skeletonSeed` and the blocks / routes / covers
   * (sections 3-5) from a fresh `seed` stream, so a re-rolled region keeps the map's roads and canal. Omitted (or equal to `seed`) =
   * the classic single stream: every existing seed's layout is unchanged.
   */
  skeletonSeed?: number;
}

/** A block of the classic grammar (section 3): its region key and its fitted rectangle (profile metres, x east / y south). */
export interface GenLot { key: string; rect_m: Rect }
export interface GenerateWithLots { layout: GenLayout; lots: GenLot[]; /** The street rectangles + the cut band (metres): nothing may be built in them. */ keepFree_m: Rect[]; size_m: Vec2 }

export function generateLayout(o: GenerateOptions): GenLayout {
  return generateLayoutWithLots(o).layout;
}

/**
 * generateLayout plus the blocks it filled (levelgen v2 stage 2, blockTemplates.ts): the same layout, byte for byte, and the lots a
 * region re-roll may give another block type. The east yard (fenced along the street) is not a lot.
 */
export function generateLayoutWithLots(o: GenerateOptions): GenerateWithLots {
  const params: GenParams = { ...DEFAULT_PARAMS, size_m: [o.profile.bounds.max[0], o.profile.bounds.max[1]], ...o.params };
  const [w, h] = params.size_m;
  let random = rng((o.skeletonSeed ?? o.seed) * 104729 + 7);
  const jit = (amp: number) => (random() * 2 - 1) * amp;
  const pick = <T,>(xs: readonly T[]): T => xs[Math.floor(random() * xs.length)];
  const d: Draft = { walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [], ...(params.building_plans ? { building_plans: true } : {}) };
  let n = 0;
  const id = (p: string) => `${p}_${++n}`;
  const V = params.verticality;

  // ---- 1. the skeleton (metres) ------------------------------------------------------------------------------------------
  const yCut = h * (0.53 + jit(0.03));
  const cutN = yCut - CUT_W / 2, cutS = yCut + CUT_W / 2;            // the trench edges
  const yNorthSt = cutN - CUT_WALL_T - STREET_W / 2;                   // the north street centre
  const yInnerSt = cutS + CUT_WALL_T + STREET_W / 2;                   // the inner south street centre
  const yWestSt = h * (0.31 + jit(0.03));
  const xCross = w * (0.33 + jit(0.03));
  const ySouthSt = h * (0.94 + jit(0.01));
  const xEastYard = w * (0.94 + jit(0.01));
  const xCentreX = w * (0.34 + jit(0.04));                             // the central crossing
  const xNeX = Math.min(w - 4, xEastYard + 1);                        // the NE crossing in the east yard

  const streetRect = (a: Vec2, b: Vec2, width: number): Rect => [[Math.min(a[0], b[0]) - (a[0] === b[0] ? width / 2 : 0), Math.min(a[1], b[1]) - (a[1] === b[1] ? width / 2 : 0)],
    [Math.max(a[0], b[0]) + (a[0] === b[0] ? width / 2 : 0), Math.max(a[1], b[1]) + (a[1] === b[1] ? width / 2 : 0)]];
  const streets: { id: string; a: Vec2; b: Vec2; width: number; piece: SetPiece }[] = [
    { id: 'ST_west', a: [0, yWestSt], b: [xCross, yWestSt], width: STREET_W, piece: 'street' },
    { id: 'ST_cross', a: [xCross, yWestSt], b: [xCross, yNorthSt], width: STREET_W, piece: 't_junction' },
    { id: 'ST_north', a: [xCross - STREET_W / 2, yNorthSt], b: [w, yNorthSt], width: STREET_W, piece: 'marksman_lane' },
    { id: 'ST_east', a: [xEastYard, yNorthSt], b: [xEastYard, ySouthSt], width: STREET_W, piece: 'street' },
    { id: 'ST_south', a: [xCross - STREET_W / 2, ySouthSt], b: [xEastYard, ySouthSt], width: STREET_W, piece: 'marksman_lane' },
    { id: 'ST_inner', a: [xCentreX - STREET_W / 2, yInnerSt], b: [w * 0.73, yInnerSt], width: STREET_W, piece: 'street' },
  ];
  for (const s of streets) d.keepFree.push(streetRect(s.a, s.b, s.width));

  // ---- 2. the cut ------------------------------------------------------------------------------------------------------
  if (o.profile.cut) {
    const crossings: GenCut['crossings'] = [
      { id: 'X_centre', poly: rect.poly([[xCentreX - 2.5, cutN - CUT_WALL_T], [xCentreX + 2.5, cutS + CUT_WALL_T]]), kind: 'ramp' },
      { id: 'X_ne', poly: rect.poly([[xNeX - 2, cutN - CUT_WALL_T], [xNeX + 2, cutS + CUT_WALL_T]]), kind: 'bridge' },
    ];
    // no building on the cut walls (2026-11-03): the retaining walls + CUT_BUILDING_MARGIN_CM (clampRect's 1 m margin) stay free
    d.keepFree.push([[0, cutN - CUT_WALL_T], [w, cutS + CUT_WALL_T]]);
    d.cut = { poly: rect.poly([[0, cutN], [w, cutS]]), level: CUT_LEVEL, floor_z: levelZ(CUT_LEVEL), crossings };
    // the retaining walls (ground obstacles: a 2.2 m parapet + fence), split round the crossings
    const gaps = crossings.map((c) => [c.poly[0][0], c.poly[1][0]] as [number, number]);
    splitWall(d, 'CUTWALL_N', [0, cutN - CUT_WALL_T / 2], [w, cutN - CUT_WALL_T / 2], gaps, CUT_WALL_H, CUT_WALL_T * 100, 'cut_wall');
    splitWall(d, 'CUTWALL_S', [0, cutS + CUT_WALL_T / 2], [w, cutS + CUT_WALL_T / 2], gaps, CUT_WALL_H, CUT_WALL_T * 100, 'cut_wall');
    // the cut floor is a deliberate killing ground (game designer 2026-11-02): seen from both rims, few ways out
    d.zones.push({ id: 'Z_cut', kind: 'corridor', set_piece: 'cut_crossing', poly: rect.poly([[0, cutN], [w, cutS]]), density: 0.5, coverKinds: ['crate'], intent: 'kill_zone' });
    // ladders on the south wall (the reference: 3; verticality >= 1): level -1 -> 0 = 3 m = 8 rungs + an 8.8 cm plinth
    if (V >= 1) {
      const lad = ladderForLevels(1);
      for (const fx of [0.03, 0.24, 0.64]) {
        const x = Math.min(w - 2, Math.max(2, w * fx + jit(3)));
        if (Math.abs(x - xCentreX) < 5 || Math.abs(x - xNeX) < 5) continue;
        // on the south wall's trench face; the climber stands in the trench north of it (profile yaw 270 = towards -y)
        d.ladders.push({ id: id('LAD_cut'), pos: [x, cutS - 0.1], yaw: 270, rungs: lad.rungs, plinth_cm: lad.plinth_cm, from: 'cut', to: 'ground', level_from: CUT_LEVEL, level_to: 0, foot: [x, cutS - 1.1], top: [x, cutS + CUT_WALL_T + 1.0] });
      }
    }
  }

  // ---- 3. the blocks: set-pieces in the regions between the streets ----------------------------------------------------
  if (o.skeletonSeed !== undefined && o.skeletonSeed !== o.seed) random = rng(o.seed * 104729 + 7);
  const sx = w / 177, sy = h / 124; // the reference's proportions
  const R = (x0: number, y0: number, x1: number, y1: number): Rect => [[x0 * sx, y0 * sy], [x1 * sx, y1 * sy]];
  const region = {
    offices: R(0, 0, 60, 10), pipeYard: R(2, 12, 30, 29), compound: R(3, 48, 50, 61), sawtooth: R(56, 5, 94, 30), boilerYard: R(40, 14, 55, 30),
    office: R(70, 36, 85, 47), warehouse: R(97, 12, 121, 44), warehouseLot: R(97, 46, 121, 52), junkyard: R(124, 2, 175, 48),
    tanks: R(46, 80, 84, 118), swOpen: R(2, 78, 42, 120), ruin: R(100, 82, 119, 100), ruinAnnex: R(88.5, 92, 97.5, 110),
    steel: R(131, 76, 161, 107), steelAnnex: R(123, 96, 130, 111), eastYard: R(166, 60, 175, 116),
  };
  const fit = (r: Rect): Rect => clampRect(r, w, h, d.keepFree, 1.0); // out of the streets
  let lotGateX = 0; // the warehouse lot's south gate (shared by its fence and variant V2)
  let compGateX = 0, compN = 0; // the west compound's one gate (north fence) - the mandatory route's compound leg
  let shedDoor: Vec2 = [0, 0];

  // facades along the NW edge (closed tall blocks)
  addBuilding(d, 'B_office_w', fit(region.offices), { height: 900, floors: 3, hollow: true, enterable: false, roof_walkable: false, set_piece: 'facade', archetype: TEMPLATE_ARCHETYPE.facade }, random);
  // the pipe yard: a ladder overwatch platform (B2) + racks as covers
  {
    const r = fit(region.pipeYard);
    const plat: Rect = [[r[0][0] + 2, r[0][1] + 2], [Math.min(r[1][0], r[0][0] + 14), Math.min(r[1][1], r[0][1] + 10)]];
    const b = addBuilding(d, 'B_gallery', plat, { height: LEVEL_HEIGHT, floors: 1, enterable: false, roof_walkable: V >= 1, set_piece: 'ladder_overwatch', archetype: TEMPLATE_ARCHETYPE.ladder_overwatch }, random);
    if (V >= 1) addLadderAt(d, b, 'south', random);
    d.zones.push(zone('Z_pipeyard', 'open', 'ladder_overwatch', r, 3.5, ['crate', 'barrel', 'boiler']));
  }
  // the west compound: fence with one gate on the street side, a shed inside
  {
    const r = fit(region.compound);
    compGateX = r[0][0] + (r[1][0] - r[0][0]) * (0.5 + jit(0.15));
    compN = r[0][1];
    fenceRect(d, 'F_compound', r, [{ side: 'north', at: compGateX, width: GATE_W }]);
    const shed: Rect = [[r[0][0] + 1.5, r[0][1] + 2], [r[0][0] + 13, r[1][1] - 1.5]];
    const shedB = addBuilding(d, 'B_shed', shed, { height: LEVEL_HEIGHT, floors: 1, enterable: true, roof_walkable: false, set_piece: 'one_entry_compound', archetype: TEMPLATE_ARCHETYPE.one_entry_compound, doorSides: ['east'], windowSides: ['east', 'north'], partitionRooms: true }, random);
    shedDoor = shedB.doors[0]?.pos ?? [shed[1][0], (shed[0][1] + shed[1][1]) / 2];
    d.zones.push(zone('Z_compound', 'open', 'one_entry_compound', r, 3.5, ['crate', 'barrel', 'wreck']));
    d.ambush_points.push({ id: id('AMB_shed'), pos: [shed[1][0] + 1, shed[1][1] - 1] });
  }
  // the sawtooth hall (enterable, a walkable roof on level +3 = 9 m, one stairwell flight ground -> roof when V >= 2) + its boiler
  // yard (a small one-entry compound). The tall halls' roofs sit on HALL_ROOF_LEVEL (game designer 2026-11-02: the view from the roofs).
  {
    const r = fit(region.sawtooth);
    addBuilding(d, 'B_hall_n', r, { height: HALL_ROOF_LEVEL * LEVEL_HEIGHT, floors: HALL_ROOF_LEVEL, hollow: true, enterable: true, roof_walkable: V >= 1, stairwell: true, set_piece: 'industrial_hall', archetype: TEMPLATE_ARCHETYPE.industrial_hall, doorSides: ['south', 'south'], windowSides: ['south', 'north'], roofHatch: V >= 2 }, random);
    const y = fit(region.boilerYard);
    fenceRect(d, 'F_boilers', y, [{ side: 'south', at: (y[0][0] + y[1][0]) / 2, width: GATE_W }]);
    d.zones.push(zone('Z_boilers', 'open', 'one_entry_compound', y, 3.0, ['boiler', 'barrel']));
  }
  // the small office (low, a door on the south face, partitioned interior) and the alley between it and the warehouse lot
  addBuilding(d, 'B_office_s', fit(region.office), { height: LEVEL_HEIGHT, floors: 1, enterable: true, roof_walkable: false, set_piece: 'facade', archetype: TEMPLATE_ARCHETYPE.small_office, doorSides: ['south'], windowSides: ['south', 'north'], partitionRooms: true }, random);
  // the warehouse (enterable, roof, stairwell) + its fenced lot (one gate on the street)
  {
    const r = fit(region.warehouse);
    addBuilding(d, 'B_warehouse', r, { height: HALL_ROOF_LEVEL * LEVEL_HEIGHT, floors: HALL_ROOF_LEVEL, hollow: true, enterable: true, roof_walkable: V >= 1, stairwell: true, set_piece: 'warehouse', archetype: TEMPLATE_ARCHETYPE.warehouse, doorSides: ['west', 'south'], windowSides: ['west', 'south'], roofHatch: V >= 2 }, random);
    const lot = fit(region.warehouseLot);
    lotGateX = (lot[0][0] + lot[1][0]) / 2 + jit(4);
    fenceRect(d, 'F_lot', lot, [{ side: 'south', at: lotGateX, width: GATE_W }, { side: 'north', at: (lot[0][0] + lot[1][0]) / 2, width: GATE_W }]);
    d.zones.push(zone('Z_lot', 'open', 'alley', lot, 2.0, ['crate', 'barrel']));
  }
  // the junkyard field: fence with two gaps, very dense wrecks
  {
    const r = fit(region.junkyard);
    fenceRect(d, 'F_junkyard', r, [{ side: 'south', at: r[0][0] + 3 + jit(1), width: GATE_W }, { side: 'south', at: r[1][0] - 6 + jit(1), width: GATE_W }]);
    d.zones.push(zone('Z_junkyard', 'open', 'junkyard_field', r, 9.0, ['wreck', 'crate', 'barrel']));
  }
  // the tank farm: two silos (round covers), a pump house, fenced lots with gates, pipe racks
  {
    const r = fit(region.tanks);
    const silos: Vec2[] = [[r[0][0] + 7 + jit(1), r[0][1] + 7 + jit(1)], [r[0][0] + 7 + jit(1), r[0][1] + 17 + jit(1)]];
    for (const s of silos) d.covers.push({ id: id('COV_silo'), pos: s, yaw: 0, size: [800, 800, 1200], kind: 'silo' });
    // the silo lot (fenced, three gates) in the north part, the pump house south of it with a 4 m lane between (the route's tail)
    const lotS = Math.min(r[0][1] + 20, r[1][1] - 14);
    const pumpE = r[1][0] - 3 + jit(1);
    const pump: Rect = [[pumpE - 12, lotS + 4], [pumpE, Math.min(lotS + 14, r[1][1] - 1.5)]];
    addBuilding(d, 'B_pump', pump, { height: LEVEL_HEIGHT, floors: 1, enterable: true, roof_walkable: false, set_piece: 'tank_farm', archetype: TEMPLATE_ARCHETYPE.tank_farm, doorSides: ['west'], windowSides: ['west', 'south'] }, random);
    fenceRect(d, 'F_tanks', [[r[0][0], r[0][1]], [r[1][0], lotS]], [{ side: 'north', at: xCross, width: GATE_W + 2 }, { side: 'south', at: xCross, width: GATE_W + 2 }, { side: 'east', at: (r[0][1] + lotS) / 2, width: GATE_W }]);
    d.zones.push(zone('Z_tanks', 'open', 'tank_farm', r, 3.5, ['barrel', 'crate', 'boiler']));
  }
  // the SW open square + a dead end alley at the map edge
  {
    const r = fit(region.swOpen);
    d.zones.push(zone('Z_square', 'open', 'open_square', r, 1.0, ['crate', 'wreck']));
    // a dead-end alley closed at its NORTH end, open towards the south street the route comes along
    const ax = r[0][0] + 6, ay0 = r[1][1] - 15, ay1 = r[1][1] - 2;
    addWall(d, 'W_deadend_w', [ax, ay0], [ax, ay1], WALL_H, 40, 'wall');
    addWall(d, 'W_deadend_e', [ax + ALLEY_W, ay0], [ax + ALLEY_W, ay1], WALL_H, 40, 'wall');
    addWall(d, 'W_deadend_n', [ax, ay0], [ax + ALLEY_W, ay0], WALL_H, 40, 'wall');
    d.zones.push(zone('Z_deadend', 'wall_end', 'dead_end', [[ax, ay0], [ax + ALLEY_W, ay1]], 0.5, ['barrel']));
  }
  // the ruined two-level factory: 2 floors, roof holes (V >= 3), floor holes, an external stair on the south face, an annex with wall holes
  {
    const r = fit(region.ruin);
    const b = addBuilding(d, 'B_ruin', r, { height: 2 * LEVEL_HEIGHT, floors: 2, enterable: true, roof_walkable: V >= 1, stairwell: false, set_piece: 'ruined_two_level', archetype: TEMPLATE_ARCHETYPE.ruined_two_level, doorSides: ['west', 'south'], windowSides: ['west', 'south'] }, random);
    if (V >= 1) {
      // the external stair climbs along the south face (a 35 deg ramp sized to the level difference: the roof on level `floors` =
      // 2 -> 6 m rise = 8.6 m run) to `mid`, then a flat landing (mid -> top) crosses the wall's top onto the roof
      const run = b.floors * STAIR_RUN_PER_RISE * (LEVEL_HEIGHT / 100);
      const sx1 = Math.min(r[1][0] - 1.5, Math.max(r[0][0] + (r[1][0] - r[0][0]) * 0.7, r[0][0] + run + 0.5));
      b.stairs.push({ id: id('STAIR_ext'), pos: [sx1 - run, r[1][1] + 1.2], mid: [sx1, r[1][1] + 1.2], from: 0, to: b.floors, kind: 'external', top: [sx1, r[1][1] - 1.6] });
    }
    if (V >= 3) {
      b.roof_holes.push([r[0][0] + (r[1][0] - r[0][0]) * 0.3, r[0][1] + (r[1][1] - r[0][1]) * 0.4], [r[0][0] + (r[1][0] - r[0][0]) * 0.75, r[0][1] + (r[1][1] - r[0][1]) * 0.6]);
      b.floor_holes.push({ pos: [r[0][0] + (r[1][0] - r[0][0]) * 0.35, r[0][1] + (r[1][1] - r[0][1]) * 0.7], floor: 1 });
    }
    if (V >= 2) b.stairs.push({ id: id('STAIR_in'), pos: [r[0][0] + 2, r[0][1] + 2], from: 0, to: 1, kind: 'stairwell', top: [r[0][0] + 2, r[0][1] + 2 + STAIR_RUN_PER_RISE * (LEVEL_HEIGHT / 100)] });
    // interior: one partition per floor with a doorway (replaces default flat room record)
    b.rooms = [];
    for (let f = 0; f < b.floors; f++) {
      // the partition at 0.5-0.6 of the width: clear of the roof holes (0.3 / 0.75) and the floor hole (0.35) the route drops through
      const px = r[0][0] + (r[1][0] - r[0][0]) * (0.55 + jit(0.05));
      const gapY = r[0][1] + (r[1][1] - r[0][1]) * (0.3 + 0.4 * random());
      addWall(d, `${b.id}_part${f}_a`, [px, r[0][1]], [px, gapY - DOOR_W / 2], 300, BUILDING_T, 'interior', f, b.id);
      addWall(d, `${b.id}_part${f}_b`, [px, gapY + DOOR_W / 2], [px, r[1][1]], 300, BUILDING_T, 'interior', f, b.id);
      const pDoorId = `${b.id}_partdoor_f${f}_0`;
      b.partitions.push({
        id: `${b.id}_part_f${f}_0`,
        start: [px, r[0][1]],
        end: [px, r[1][1]],
        floor: f,
        thickness: BUILDING_T,
        openings: [{ id: pDoorId, kind: 'door', pos: [px, gapY], floor: f, width: DOOR_W * 100, sill_height: 0, top_height: 210 }],
      });
      const extDoorsLeft = f === 0 ? b.openings.filter((op) => op.kind === 'door' && op.pos[0] <= px + 0.1).map((op) => op.id!).filter(Boolean) : [];
      const extDoorsRight = f === 0 ? b.openings.filter((op) => op.kind === 'door' && op.pos[0] >= px - 0.1).map((op) => op.id!).filter(Boolean) : [];
      b.rooms.push(
        { id: `${b.id}_r_f${f}_0`, floor: f, bounds: { min: [r[0][0], r[0][1]], max: [px, r[1][1]] }, kind: 'room', door_ids: [...extDoorsLeft, pDoorId] },
        { id: `${b.id}_r_f${f}_1`, floor: f, bounds: { min: [px, r[0][1]], max: [r[1][0], r[1][1]] }, kind: 'room', door_ids: [...extDoorsRight, pDoorId] },
      );
    }
    if (b.floors > 1) {
      const f1Gaps: FloorGap[] = b.floor_holes.map((fh, idx) => ({
        id: `${b.id}_floor_hole_${idx}`,
        poly: rect.poly([[fh.pos[0] - 0.75, fh.pos[1] - 0.75], [fh.pos[0] + 0.75, fh.pos[1] + 0.75]]),
        kind: 'landing',
      }));
      const st1 = b.stairs.find((x) => x.to >= 1 && x.from < 1 && x.kind === 'stairwell');
      if (st1) {
        // exactly the flight's footprint: ends at its top (cause A, 2026-11-18), as wide as the flight
        const alongX = Math.abs(st1.top[0] - st1.pos[0]) >= Math.abs(st1.top[1] - st1.pos[1]);
        const hw = STAIR_FLIGHT_W / 2 + 0.05;
        const gx0 = Math.min(st1.pos[0], st1.top[0]), gx1 = Math.max(st1.pos[0], st1.top[0]);
        const gy0 = Math.min(st1.pos[1], st1.top[1]), gy1 = Math.max(st1.pos[1], st1.top[1]);
        f1Gaps.push({
          id: `${b.id}_gap_stair_f1`,
          poly: rect.poly(alongX ? [[gx0, st1.pos[1] - hw], [gx1, st1.pos[1] + hw]] : [[st1.pos[0] - hw, gy0], [st1.pos[0] + hw, gy1]]),
          kind: 'stairwell',
        });
      }
      b.floors_detail = [{ floor: 1, height_z: levelZ(1), gaps: f1Gaps, rooms: b.rooms.filter((rm) => rm.floor === 1) }];
    }
    const a = fit(region.ruinAnnex);
    addBuilding(d, 'B_ruin_annex', a, { height: LEVEL_HEIGHT, floors: 1, enterable: true, roof_walkable: false, set_piece: 'ruined_two_level', archetype: TEMPLATE_ARCHETYPE.ruined_two_level, doorSides: ['west', 'south', 'north'], windowSides: ['west', 'east'] }, random);
    d.zones.push(zone('Z_ruin', 'open', 'ruined_two_level', [[a[0][0] - 2, r[0][1] - 2], [r[1][0] + 2, a[1][1] + 2]], 2.5, ['crate', 'barrel']));
  }
  // the Steel Works hall (enterable, roof on +3, stairwell), two factory chimneys (tall obstacles, never climbable), the annex racks
  {
    const r = fit(region.steel);
    addBuilding(d, 'B_steelworks', r, { height: HALL_ROOF_LEVEL * LEVEL_HEIGHT, floors: HALL_ROOF_LEVEL, hollow: true, enterable: true, roof_walkable: V >= 1, stairwell: true, set_piece: 'industrial_hall', archetype: TEMPLATE_ARCHETYPE.industrial_hall, doorSides: ['south', 'west'], windowSides: ['south', 'west'] }, random);
    // the two factory chimneys: one at the hall's NE corner (over the north street's end), one at its SW (over the south street).
    // Game designer 2026-11-02: chimneys are NOT ladder towers - a tall closed obstacle that blocks sight, no ladder, no walkable
    // top, no height level of its own (validateGenLayout refuses a climbable chimney).
    for (const [k, c] of ([[0, [r[1][0] + 2.5 + jit(0.5), r[0][1] - 2.5 + jit(0.5)]], [1, [r[0][0] - 6 + jit(1), r[1][1] - 2 + jit(1)]]] as [number, Vec2][])) {
      let t: Rect = [[c[0] - 1.5, c[1] - 1.5], [c[0] + 1.5, c[1] + 1.5]];
      if (d.cut) { // no building on the cut walls (2026-11-03): a chimney reaching the wall band moves out of it, towards the nearer side
        const bandN = cutN - CUT_WALL_T - CUT_BUILDING_MARGIN_CM / 100, bandS = cutS + CUT_WALL_T + CUT_BUILDING_MARGIN_CM / 100;
        if (t[1][1] > bandN && t[0][1] < bandS) {
          const dy = c[1] < yCut ? bandN - t[1][1] : bandS - t[0][1];
          t = [[t[0][0], t[0][1] + dy], [t[1][0], t[1][1] + dy]];
        }
      }
      addBuilding(d, `B_chimney_${k}`, t, { height: CHIMNEY_HEIGHT_CM, floors: 1, hollow: true, enterable: false, roof_walkable: false, set_piece: 'chimney' }, random);
      if (V >= 1) random(); // the draw the former tower ladder took: keeps the rest of every seed's layout unchanged
    }
    const an = fit(region.steelAnnex);
    d.zones.push(zone('Z_steel_annex', 'corridor', 'alley', an, 2.5, ['crate', 'barrel']));
    d.zones.push(zone('Z_steelworks', 'wall_run', 'facade', [[r[0][0] - 2, r[0][1] - 4], [r[1][0] + 3, r[1][1] + 4]], 1.5, ['crate']));
  }
  // the east yard fence (gaps at both street ends) and the south fence
  fenceRect(d, 'F_east', [[xEastYard + STREET_W / 2 + 0.5, yNorthSt - STREET_W / 2], [w - 0.5, ySouthSt + STREET_W / 2]], [{ side: 'west', at: yNorthSt, width: STREET_W }, { side: 'west', at: ySouthSt, width: STREET_W }]);
  d.zones.push(zone('Z_eastyard', 'corridor', 'alley', [[xEastYard - STREET_W / 2, yNorthSt - STREET_W / 2], [w, ySouthSt + STREET_W / 2]], 2.5, ['crate', 'wreck']));
  addWall(d, 'F_south', [xCross - STREET_W, h - 1.5], [xEastYard + STREET_W / 2, h - 1.5], FENCE_H, FENCE_T, 'fence');
  // street zones (the classifier sees the facades on one side -> wall_run; the lanes are marksman lanes)
  for (const s of streets) {
    const r = streetRect(s.a, s.b, s.width);
    d.zones.push(zone(`Z_${s.id}`, s.piece === 'alley' ? 'corridor' : s.piece === 't_junction' ? 'corner' : 'wall_run', s.piece, r, s.piece === 'marksman_lane' ? 2.0 : 2.0, ['jersey', 'sandbag', 'wreck', 'barrel']));
  }

  // ---- 4. the routes ---------------------------------------------------------------------------------------------------
  // The squad hugs one side of each street (HUG m off the facade / fence / cut wall: the classifier sees the wall and picks the
  // echelon or the column) instead of walking the open middle; the tail dips into the tank farm past the pump house like the reference.
  const HUG = 2.6;
  const start: Vec2 = [1.5, yWestSt + STREET_W / 2 - HUG];
  const pumpB = d.buildings.find((b) => b.id === 'B_pump')!;
  const [[pumpW, pumpN], [pumpE, pumpS]] = pumpB.footprint;
  const hugS = ySouthSt + STREET_W / 2 - HUG;
  const hugN = yNorthSt + STREET_W / 2 - HUG;
  // The designer's loop: the west street -> the west compound (its one gate, to the shed's door and back out) -> the T-junction ->
  // the north street along the cut, full length (the marksman lane) -> the NE crossing -> the east yard round the Steel Works ->
  // the south street -> the ruined factory (from ABOVE with verticality 3: the external stair, a roof hole, a floor hole; else
  // through its doors) -> the tank farm past the pump house -> the dead end in the SW square (the wall layout at the end).
  const boil = fit(region.boilerYard), lot = fit(region.warehouseLot), off = fit(region.office);
  const ruinB = d.buildings.find((b) => b.id === 'B_ruin')!;
  const [[ruinW, ruinN], [ruinE, ruinS]] = ruinB.footprint;
  const extStair = ruinB.stairs.find((s) => s.kind === 'external');
  const hugW = yWestSt + STREET_W / 2 - HUG;
  const main: Vec2[] = [start, [compGateX, hugW], [compGateX, compN + 2], [shedDoor[0] + 1.8, shedDoor[1]], [compGateX, compN + 2], [compGateX, hugW],
    [xCross - STREET_W / 2 + HUG, hugW], [xCross - STREET_W / 2 + HUG, hugN], [xNeX, hugN], [xNeX, hugS], [ruinE + 6, hugS]];
  const mainLayers: string[] = main.map(() => 'ground');
  const mainLinks: { at: number; via: string }[] = [];
  const pieces: SetPiece[] = ['street', 'one_entry_compound', 'street', 't_junction', 'marksman_lane', 'cut_crossing', 'street', 'marksman_lane'];
  if (extStair && V >= 3 && ruinB.roof_holes.length && ruinB.floor_holes.length) {
    const rh = ruinB.roof_holes[0], fh = ruinB.floor_holes[0];
    // a leg that ends at a hole stops on its RIM (the hole is open: nobody stands over it); the drop leg lands 1 m past its centre
    const below: Vec2 = [rh[0], rh[1] + 1];
    main.push(extStair.pos, extStair.top, holeRim(rh, extStair.top)); mainLayers.push('ground', 'roof:B_ruin', 'roof:B_ruin'); mainLinks.push({ at: main.length - 2, via: extStair.id });
    main.push(below, holeRim(fh.pos, below)); mainLayers.push('floor:B_ruin:1', 'floor:B_ruin:1'); mainLinks.push({ at: main.length - 2, via: 'hole_roof_0' });
    main.push([fh.pos[0], fh.pos[1] + 1], [ruinW - 1.25, (ruinN + ruinS) / 2]); mainLayers.push('ground', 'ground'); mainLinks.push({ at: main.length - 2, via: 'hole_floor_0' });
    pieces.push('ruined_two_level');
  } else {
    // through the doors: in by the south door, out by the west door
    const south = ruinB.doors.find((x) => Math.abs(x.pos[1] - ruinS) < 0.01), west = ruinB.doors.find((x) => Math.abs(x.pos[0] - ruinW) < 0.01);
    if (south && west) { main.push([south.pos[0], ruinS + 1.5], [south.pos[0], ruinS - 2], [west.pos[0] + 2, west.pos[1]], [ruinW - 1.25, west.pos[1]]); mainLayers.push('ground', 'ground', 'ground', 'ground'); pieces.push('ruined_two_level'); }
  }
  // the tank farm past the pump house, then the dead end in the SW square
  const dead = d.zones.find((z) => z.set_piece === 'dead_end')!;
  const [[dx0, dy0], [dx1, dy1]] = bbox(dead.poly);
  main.push([pumpE + 1.8, hugS], [pumpE + 1.8, pumpN - 2], [pumpW - 1.8, pumpN - 2], [pumpW - 1.8, (pumpN + pumpS) / 2], [pumpW - 1.8, hugS], [(dx0 + dx1) / 2, dy1 + 1.2], [(dx0 + dx1) / 2, dy0 + 1.6]);
  mainLayers.push(...Array(7).fill('ground'));
  pieces.push('tank_farm', 'open_square', 'dead_end');
  const end: Vec2 = main[main.length - 1];
  d.routes.push({ id: 'R_main', pace: 'walk', points: main, mandatory: true, layers: mainLayers, links: mainLinks, set_pieces: pieces,
    expect: [{ zone: 'Z_ST_cross', formation: 'column' }, { zone: 'Z_deadend', formation: 'wall' }] });
  const variants: GenRoute[] = [];
  // V1 - the gallery overwatch (ladder + roof)
  const gallery = d.buildings.find((b) => b.id === 'B_gallery');
  const galleryLadder = d.ladders.find((l) => l.to === 'roof:B_gallery');
  if (gallery && galleryLadder && V >= 1) {
    variants.push({ id: 'V1_gallery', pace: 'walk', points: [start, galleryLadder.foot, galleryLadder.top, rect.centre(gallery.footprint)],
      layers: ['ground', 'ground', 'roof:B_gallery', 'roof:B_gallery'], links: [{ at: 2, via: galleryLadder.id }], set_pieces: ['ladder_overwatch'] });
  }
  // V2 - the north blocks: the boiler yard's gate (in and out), the alley between the small office and the warehouse lot, the lot's
  // gate, into the warehouse by its south door and up the stairwell onto the roof (V >= 2; else the hall only)
  {
    const wh = d.buildings.find((b) => b.id === 'B_warehouse')!;
    const alleyX = off[1][0] + ALLEY_W / 2 + 1;
    const southDoor = wh.doors.find((x) => Math.abs(x.pos[1] - wh.footprint[1][1]) < 0.01);
    const chain = stairChainUp(wh);
    const pts: Vec2[] = [[xCross - STREET_W / 2 + HUG, boil[1][1] + 3], [(boil[0][0] + boil[1][0]) / 2, boil[1][1] + 1], [(boil[0][0] + boil[1][0]) / 2, (boil[0][1] + boil[1][1]) / 2],
      [(boil[0][0] + boil[1][0]) / 2, boil[1][1] + 3], [alleyX, off[0][1] - 1], [alleyX, lot[1][1] + 2], [lotGateX, hugN], [lotGateX, lot[1][1] - 1.5], [(lot[0][0] + lot[1][0]) / 2, lot[0][1] + 1.5]];
    const layers = pts.map(() => 'ground');
    const links: { at: number; via: string }[] = [];
    if (southDoor) { pts.push([southDoor.pos[0], wh.footprint[1][1] - 2]); layers.push('ground'); }
    if (southDoor && chain) {
      const base = pts.length;
      pts.push(...chain.points, rect.centre(wh.footprint));
      layers.push(...chain.layers, `roof:${wh.id}`);
      links.push(...chain.links.map((l) => ({ at: base + l.at, via: l.via })));
    }
    variants.push({ id: 'V2_north_blocks', pace: 'walk', points: pts, layers, links, set_pieces: ['one_entry_compound', 'alley', 'warehouse'] });
  }
  // V3 - through the cut: in at the centre ramp, along the trench, out by the last ladder
  const cutLadders = d.ladders.filter((l) => l.from === 'cut').sort((a, b) => a.pos[0] - b.pos[0]);
  if (d.cut && cutLadders.length && V >= 1) {
    const out = cutLadders[cutLadders.length - 1];
    // down the centre ramp (the link leg runs from the ramp's north end to the trench floor beside it), along the trench, up the last ladder
    variants.push({ id: 'V3_cut', pace: 'walk',
      points: [[xCentreX, yNorthSt + STREET_W / 2 - HUG], [xCentreX, cutN - 0.6], [xCentreX + 3.5, yCut], [out.foot[0] - 2, yCut], out.foot, out.top, [out.top[0], yInnerSt]],
      layers: ['ground', 'ground', 'cut', 'cut', 'cut', 'ground', 'ground'], links: [{ at: 2, via: 'X_centre' }, { at: 5, via: out.id }], set_pieces: ['cut_crossing'],
      intent: 'kill_zone', intent_note: 'game designer 2026-11-02: the canal-floor variant is a deliberate kill zone - the trench floor is seen from both rims, the only ways out are the ramp and the ladders; a fast, risky shortcut, not a defect' });
  }
  // V4 - the inner street along the cut's south wall from the central crossing to the Steel Works' NW tower, then the ruin from BELOW
  // (its south door in, its west door out - the other way into the ruin than the mandatory route's)
  {
    const south = ruinB.doors.find((x) => Math.abs(x.pos[1] - ruinS) < 0.01), west = ruinB.doors.find((x) => Math.abs(x.pos[0] - ruinW) < 0.01);
    const pts: Vec2[] = [[xCentreX, yInnerSt + STREET_W / 2 - HUG], [ruinE + 6, yInnerSt + STREET_W / 2 - HUG], [ruinE + 6, ruinS + 3]];
    if (south && west) pts.push([south.pos[0], ruinS + 1.5], [south.pos[0], ruinS - 2], [west.pos[0] + 2, west.pos[1]], [ruinW - 1.25, west.pos[1]]);
    pts.push([ruinW - 1.25, hugS]);
    variants.push({ id: 'V4_inner_ruin_below', pace: 'walk', points: pts, set_pieces: ['street', 'ruined_two_level'] });
  }
  d.routes.push(...variants.slice(0, Math.max(0, params.variants)));

  // ---- 5. covers by zone density, then ambush points / patrols / spawns -------------------------------------------------
  const routeLine = d.routes.flatMap((r) => r.points.map((p, i) => [p, r.points[i + 1]] as [Vec2, Vec2 | undefined]).filter((s) => s[1])) as [Vec2, Vec2][];
  const nearRoute = (p: Vec2, dmin: number) => routeLine.some(([a, b]) => segDist(p, a, b) < dmin);
  const blocked = (p: Vec2, margin = 1.0) => d.buildings.some((b) => rect.contains(b.footprint, p, margin)) || d.covers.some((c) => dist(c.pos, p) < Math.max(c.size[0], c.size[1]) / 200 + 1.2)
    || (d.cut ? p[1] > cutN - CUT_WALL_T - 1 && p[1] < cutS + CUT_WALL_T + 1 : false) || d.ladders.some((l) => dist(l.top, p) < 2 || dist(l.pos, p) < 2);
  for (const z of d.zones) {
    if (z.set_piece === 'cut_crossing' || z.set_piece === 'facade' && z.kind === 'wall_run' && z.id !== 'Z_steelworks') continue;
    const [a, b] = bbox(z.poly);
    const area = (b[0] - a[0]) * (b[1] - a[1]);
    const want = Math.round((area / 100) * z.density * params.density * (z.set_piece === 'junkyard_field' ? 0.55 : 0.6)); // greybox: one cover object ~ 1.7 reference covers
    let placed = 0, tries = 0;
    while (placed < want && tries++ < want * 30) {
      const p: Vec2 = [a[0] + 1 + random() * (b[0] - a[0] - 2), a[1] + 1 + random() * (b[1] - a[1] - 2)];
      if (blocked(p) || nearRoute(p, 2.6) || d.keepFree.some((k) => rect.contains(k, p, -2.0) && z.set_piece !== 'street' && z.set_piece !== 'marksman_lane')) continue;
      const kind = pick(z.coverKinds);
      const size = COVER_SIZE[kind];
      d.covers.push({ id: id('COV'), pos: p, yaw: Math.round(random() * 4) * 90 + (kind === 'wreck' ? Math.round(jit(30)) : 0), size, kind });
      placed++;
    }
  }
  // ambush points (the AmbushSpotRules of COMBAT_LAB.md 2.1): per FREE wall end (no other wall end within 0.3 m; walls >= 1.5 m) one
  // spot on each face, 1 m inside the end and 2.6 m off the face; kept when 14-30 m from a route and not inside anything
  const groundWalls = d.walls.filter((wl) => (wl.floor ?? 0) === 0 && wl.kind !== 'cut_wall' && dist(wl.a, wl.b) >= 1.5);
  const ends = groundWalls.flatMap((wl) => [wl.a, wl.b]);
  for (const wl of groundWalls) {
    const u = unit(wl.b, wl.a), nrm: Vec2 = [-u[1], u[0]];
    for (const end of [wl.a, wl.b]) {
      if (ends.some((e) => e !== end && dist(e, end) < 0.3)) continue; // joined: a corner, not an end
      const inward = end === wl.a ? 1 : -1;
      for (const side of [1, -1]) {
        const spot: Vec2 = [end[0] + u[0] * inward * 1.0 + nrm[0] * side * 2.6, end[1] + u[1] * inward * 1.0 + nrm[1] * side * 2.6];
        const near = nearestRouteDist(spot, routeLine);
        if (near < 14 || near > 30 || blocked(spot, 0.5) || spot[0] < 1 || spot[1] < 1 || spot[0] > w - 1 || spot[1] > h - 1) continue;
        if (d.ambush_points.some((q) => dist(q.pos, spot) < 4)) continue;
        d.ambush_points.push({ id: id('AMB'), pos: spot });
      }
    }
  }
  // ... the outside corners of every building (the hiding spot of a man flattened round a corner), 1.6 m out diagonally ...
  for (const b of d.buildings) {
    const [[x0, y0], [x1, y1]] = b.footprint;
    for (const c of [[x0 - 1.6, y0 - 1.6], [x1 + 1.6, y0 - 1.6], [x0 - 1.6, y1 + 1.6], [x1 + 1.6, y1 + 1.6]] as Vec2[]) {
      const near = nearestRouteDist(c, routeLine);
      if (near < 14 || near > 30 || blocked(c, 0.3) || c[0] < 1 || c[1] < 1 || c[0] > w - 1 || c[1] > h - 1) continue;
      if (d.ambush_points.some((q) => dist(q.pos, c) < 4)) continue;
      d.ambush_points.push({ id: id('AMB'), pos: c });
    }
  }
  // ... and behind every tall cover (wreck / silo / boiler): the far side from the route, 14-30 m off it
  for (const c of d.covers) {
    if (c.size[2] < 140) continue;
    const near = nearestRouteDist(c.pos, routeLine);
    if (near < 13 || near > 29) continue;
    const away = routeLine.reduce<{ d: number; p: Vec2 }>((m, [a, b]) => { const q = closestOn(c.pos, a, b); const dd = dist(q, c.pos); return dd < m.d ? { d: dd, p: q } : m; }, { d: Infinity, p: c.pos });
    const u = unit(c.pos, away.p);
    const spot: Vec2 = [c.pos[0] + u[0] * (Math.max(c.size[0], c.size[1]) / 200 + 1.2), c.pos[1] + u[1] * (Math.max(c.size[0], c.size[1]) / 200 + 1.2)];
    if (blocked(spot, 0.3) || d.ambush_points.some((q) => dist(q.pos, spot) < 4)) continue;
    d.ambush_points.push({ id: id('AMB'), pos: spot });
  }
  // patrol loops in the open zones away from the start (three tries per zone)
  for (const z of d.zones) {
    if (!['open_square', 'junkyard_field', 'tank_farm', 'one_entry_compound', 'alley'].includes(z.set_piece)) continue;
    const [a, b] = bbox(z.poly);
    for (let t = 0; t < 3; t++) {
      const c: Vec2 = [a[0] + (b[0] - a[0]) * (0.3 + 0.4 * random()), a[1] + (b[1] - a[1]) * (0.3 + 0.4 * random())];
      if (dist(c, start) < 30) break;
      const r = Math.max(4, Math.min(12, Math.min(b[0] - a[0], b[1] - a[1]) / 3));
      const pts: Vec2[] = [0, 1, 2, 3].map((k) => [c[0] + Math.cos((k / 4) * Math.PI * 2 + jit(0.3)) * r, c[1] + Math.sin((k / 4) * Math.PI * 2 + jit(0.3)) * r]);
      if (pts.some((p) => blocked(p, 0.5))) continue;
      d.patrol_routes.push({ id: id('PAT'), points: pts });
      break;
    }
  }
  // spawn points at the edges, >= 40 m from the start, out of the cut
  for (const e of [[w - 3, yNorthSt - 8], [w - 3, ySouthSt - 6], [w * 0.6, 2], [w * 0.85, 2], [xCross + 12, h - 4], [w * 0.75, h - 4], [3, h - 6]] as Vec2[]) {
    if (dist(e, start) < 40 || blocked(e, 0.5)) continue;
    d.spawn_points.push({ id: id('SPAWN'), pos: e, kind: 'edge' });
  }

  // ---- 6. to UE cm ------------------------------------------------------------------------------------------------------
  const lots: GenLot[] = (Object.keys(region) as (keyof typeof region)[]).filter((k) => k !== 'eastYard').map((k) => ({ key: k, rect_m: fit(region[k]) }));
  const layout = finalizeDraftToLayout(d, [w, h], {
    name: o.name ?? `gen_${o.profile.id}_${o.seed}`,
    profile: o.profile.id,
    seed: o.seed,
    params,
    comment: `Generated by Tools/TrainingGround/src/levelgen/generate.ts from ${o.profile.id} (seed ${o.seed}); an original greybox, not a copy of the reference.`,
    start,
    end,
    yaw: 90,
  });
  return { layout, lots, keepFree_m: d.keepFree.map((r) => [[r[0][0], r[0][1]], [r[1][0], r[1][1]]] as Rect), size_m: [w, h] };
}

export interface FinalizeMeta {
  name: string;
  profile: string;
  seed: number;
  params: GenParams;
  comment?: string;
  start: Vec2;
  end: Vec2;
  yaw?: number;
}

/** Convert a Draft constructed in profile metres (x east, y south) to a GenLayout in UE cm. */
export function finalizeDraftToLayout(d: Draft, size: Vec2, meta: FinalizeMeta): GenLayout {
  // Normalize new draft geometry before mapping coordinates; mirrors stay at the same target.
  for (const wall of d.walls) wall.height = standardObstacleHeight(wall.height);
  for (const cover of d.covers) cover.size = [cover.size[0], cover.size[1], standardObstacleHeight(cover.size[2])];
  for (const edge of d.mantle_edges) edge.height_cm = standardObstacleHeight(edge.height_cm);
  for (const building of d.buildings) {
    for (const p of building.partitions) if (p.height !== undefined) p.height = standardObstacleHeight(p.height);
    for (const p of building.parapets) p.height_cm = standardObstacleHeight(p.height_cm);
    for (const edge of building.mantle_edges) edge.height_cm = standardObstacleHeight(edge.height_cm);
  }
  const [w, h] = size;
  const U = (p: Vec2): Vec2 => toUE(p, size);
  const UR = (r: Rect): [Vec2, Vec2] => {
    const a = U(r[0]), b = U(r[1]);
    return [[Math.min(a[0], b[0]), Math.min(a[1], b[1])], [Math.max(a[0], b[0]), Math.max(a[1], b[1])]];
  };
  const normalToUE = (n: Vec2): Vec2 => [-n[1], n[0]];
  // Every building field of BUILDING_COORD_FIELDS: the draft has no building rotation, so both frames map the same way.
  const mapBuilding = (b: DraftBuilding): GenBuilding => mapBuildingCoords(b, {
    point: (p) => U(p),
    rect: (r) => UR(r),
    direction: (v) => normalToUE(v),
    yaw: (y) => yawToUE(y),
  });
  const mapMantleEdge = (me: MantleEdge): MantleEdge => ({
    ...me,
    start: U(me.start),
    end: U(me.end),
    normal: normalToUE(me.normal),
  });

  const allMantles = [
    ...d.mantle_edges,
    ...d.buildings.flatMap((b) => b.mantle_edges ?? []),
  ].map(mapMantleEdge);

  const layout: GenLayout = {
    schema: d.building_plans ? LEVELGEN_SCHEMA_V2 : LEVELGEN_SCHEMA,
    name: meta.name,
    profile: meta.profile,
    seed: meta.seed,
    params: meta.params,
    comment: meta.comment ?? `Generated layout ${meta.name}`,
    bounds: { min: [-(h / 2) * 100, -(w / 2) * 100], max: [(h / 2) * 100, (w / 2) * 100] },
    ground_z: 0,
    spawn: { pos: U(meta.start), yaw: meta.yaw ?? 90 },
    end: U(meta.end),
    walls: d.walls.map((wl) => ({ id: wl.id, a: U(wl.a), b: U(wl.b), height: wl.height, thickness: wl.thickness, ...(wl.floor ? { floor: wl.floor } : {}), ...(wl.kind ? { kind: wl.kind } : {}), ...(wl.building ? { building: wl.building } : {}) })),
    buildings: d.buildings.map(mapBuilding),
    cut: d.cut ? { poly: d.cut.poly.map(U), level: d.cut.level, floor_z: d.cut.floor_z, crossings: d.cut.crossings.map((c) => ({ ...c, poly: c.poly.map(U) })) } : undefined,
    ladders: d.ladders.map((l) => ({ ...l, pos: U(l.pos), foot: U(l.foot), top: U(l.top), yaw: yawToUE(l.yaw) })),
    covers: d.covers.map((c) => ({ ...c, pos: U(c.pos), yaw: yawToUE(c.yaw) })),
    zones: d.zones.map((z) => ({ id: z.id, kind: z.kind, set_piece: z.set_piece, poly: z.poly.map(U), ...(z.intent ? { intent: z.intent } : {}) })),
    routes: d.routes.map((r) => ({ ...r, points: r.points.map(U) })),
    spawn_points: d.spawn_points.map((s) => ({ ...s, pos: U(s.pos) })),
    ambush_points: d.ambush_points.map((a) => ({ ...a, pos: U(a.pos) })),
    patrol_routes: d.patrol_routes.map((p) => ({ ...p, points: p.points.map(U) })),
    ...(allMantles.length ? { mantle_edges: allMantles } : {}),
    // The pressure template of a generated map (COMBAT_SIM.md section 5, 2026-11-02): ambush-heavy - 30 % patrols, half of the ambushes
    // coordinated plans, a group every 90 s, caps of 8 so a plan of 4-6 finds room beside a living patrol (the combat lab's own level
    // keeps the designer's 60 % / 35 % / 120 s / 4). The level JSON's block is the master once the map is built (the builder keeps it).
    pressure: {
      first_event_sec: 30,
      interval_sec: 90,
      group_min: 2,
      group_max: 4,
      max_alive: 8,
      patrol_share: 0.3,
      seed: meta.seed,
      ambush: { points: d.ambush_points.map((a) => U(a.pos)), coordinated: { share: 0.5, max_alive: 8 } },
    },
  };
  const heightGate = runObstacleHeightGate(layout);
  if (heightGate.gate === 'fail') throw new Error(heightGate.errors.map((e) => e.reason).join('; '));
  // every room of an enterable building gets its walkable anchor (building frame; rotateContent moves it by the coord table)
  return addRoomAnchors(layout);
}

// ---- helpers (metres) -----------------------------------------------------------------------------------------------------

export const COVER_SIZE: Record<CoverKind, [number, number, number]> = {
  jersey: [300, 60, obstacleHeight('low_vault')], sandbag: [250, 80, obstacleHeight('low_vault')], crate: [120, 120, obstacleHeight('low_mantle')], wreck: [420, 180, obstacleHeight('high_mantle')], barrel: [60, 60, obstacleHeight('low_vault')], silo: [800, 800, 1200], boiler: [200, 120, obstacleHeight('high_mantle')],
};

/**
 * Profile yaw (degrees from +x east towards +y south) to UE yaw (from +X north towards +Y east): UE = profile + 90 (both turn clockwise
 * on the map: profile east 0 -> UE 90, profile south 90 -> UE 180). Until 2026-11-02 this was 90 - profile, a MIRROR: every ladder faced
 * its wall in UE (the game designer's "ladders sunk into the walls"; build_levelgen_map.py / levelgen_ladders.py now take the ladder's
 * yaw from pos -> foot, so the layouts accepted before the fix build right too).
 */
const yawToUE = (deg: number) => ((deg + 90) % 360 + 360) % 360;

function unit(a: Vec2, b: Vec2): Vec2 { const L = dist(a, b) || 1; return [(a[0] - b[0]) / L, (a[1] - b[1]) / L]; }
/** Where a leg towards an open hole stops: on the solid slab 0.8 m past the hole's edge (HOLE_HALF_CM), on the side it comes from. */
export const HOLE_RIM_M = HOLE_HALF_CM / 100 + 0.8;
function holeRim(hole: Vec2, from: Vec2): Vec2 { const u = unit(from, hole); return [hole[0] + u[0] * HOLE_RIM_M, hole[1] + u[1] * HOLE_RIM_M]; }
function segDist(p: Vec2, a: Vec2, b: Vec2): number {
  const ab: Vec2 = [b[0] - a[0], b[1] - a[1]]; const l2 = ab[0] * ab[0] + ab[1] * ab[1];
  const t = l2 === 0 ? 0 : Math.max(0, Math.min(1, ((p[0] - a[0]) * ab[0] + (p[1] - a[1]) * ab[1]) / l2));
  return dist(p, [a[0] + ab[0] * t, a[1] + ab[1] * t]);
}
function closestOn(p: Vec2, a: Vec2, b: Vec2): Vec2 {
  const ab: Vec2 = [b[0] - a[0], b[1] - a[1]]; const l2 = ab[0] * ab[0] + ab[1] * ab[1];
  const t = l2 === 0 ? 0 : Math.max(0, Math.min(1, ((p[0] - a[0]) * ab[0] + (p[1] - a[1]) * ab[1]) / l2));
  return [a[0] + ab[0] * t, a[1] + ab[1] * t];
}
const nearestRouteDist = (p: Vec2, line: [Vec2, Vec2][]) => line.reduce((m, [a, b]) => Math.min(m, segDist(p, a, b)), Infinity);
const bbox = (poly: Vec2[]): Rect => [[Math.min(...poly.map((p) => p[0])), Math.min(...poly.map((p) => p[1]))], [Math.max(...poly.map((p) => p[0])), Math.max(...poly.map((p) => p[1]))]];

function zone(id: string, kind: ZoneKind, set_piece: SetPiece, r: Rect, density: number, coverKinds: CoverKind[]) {
  return { id, kind, set_piece, poly: rect.poly(r), density, coverKinds };
}

/** Keeps a rectangle inside the map and out of the street rectangles (shrinks the overlapping side). */
function clampRect(r: Rect, w: number, h: number, free: Rect[], margin: number): Rect {
  let [[x0, y0], [x1, y1]] = r;
  x0 = Math.max(1, x0); y0 = Math.max(1, y0); x1 = Math.min(w - 1, x1); y1 = Math.min(h - 1, y1);
  for (const f of free) {
    const [[fx0, fy0], [fx1, fy1]] = [[f[0][0] - margin, f[0][1] - margin], [f[1][0] + margin, f[1][1] + margin]];
    if (x1 <= fx0 || x0 >= fx1 || y1 <= fy0 || y0 >= fy1) continue;
    // shrink along the axis that loses least
    const cutL = fx1 - x0, cutR = x1 - fx0, cutT = fy1 - y0, cutB = y1 - fy0;
    const m = Math.min(cutL, cutR, cutT, cutB);
    if (m === cutL) x0 = fx1; else if (m === cutR) x1 = fx0; else if (m === cutT) y0 = fy1; else y1 = fy0;
  }
  if (x1 - x0 < 3) x1 = x0 + 3;
  if (y1 - y0 < 3) y1 = y0 + 3;
  return [[x0, y0], [x1, y1]];
}

function addWall(d: Draft, id: string, a: Vec2, b: Vec2, height: number, thickness: number, kind: GenWall['kind'], floor = 0, building?: string): void {
  if (dist(a, b) < 0.3) return;
  d.walls.push({ id, a, b, height, thickness, kind, ...(floor ? { floor } : {}), ...(building ? { building } : {}) });
}

/** A wall from a to b (axis-aligned) with gaps [x0, x1] (along its axis) left open. */
function splitWall(d: Draft, id: string, a: Vec2, b: Vec2, gaps: [number, number][], height: number, thickness: number, kind: GenWall['kind'], floor = 0, building?: string): void {
  const horizontal = a[1] === b[1];
  const lo = horizontal ? Math.min(a[0], b[0]) : Math.min(a[1], b[1]);
  const hi = horizontal ? Math.max(a[0], b[0]) : Math.max(a[1], b[1]);
  const cuts = gaps.map(([g0, g1]) => [Math.max(lo, Math.min(g0, g1)), Math.min(hi, Math.max(g0, g1))] as [number, number]).filter(([g0, g1]) => g1 > lo && g0 < hi).sort((x, y) => x[0] - y[0]);
  let cur = lo, k = 0;
  const seg = (s: number, e: number) => {
    if (e - s < 0.3) return;
    const pa: Vec2 = horizontal ? [s, a[1]] : [a[0], s], pb: Vec2 = horizontal ? [e, a[1]] : [a[0], e];
    addWall(d, `${id}_${k++}`, pa, pb, height, thickness, kind, floor, building);
  };
  for (const [g0, g1] of cuts) { seg(cur, g0); cur = Math.max(cur, g1); }
  seg(cur, hi);
}

type Side = 'north' | 'south' | 'east' | 'west';

/** A fence round a rectangle with gates { side, at (the coordinate along that side), width }. */
function fenceRect(d: Draft, id: string, r: Rect, gates: { side: Side; at: number; width: number }[]): void {
  const [[x0, y0], [x1, y1]] = r;
  const g = (side: Side): [number, number][] => gates.filter((x) => x.side === side).map((x) => [x.at - x.width / 2, x.at + x.width / 2]);
  splitWall(d, `${id}_N`, [x0, y0], [x1, y0], g('north'), FENCE_H, FENCE_T, 'fence');
  splitWall(d, `${id}_S`, [x0, y1], [x1, y1], g('south'), FENCE_H, FENCE_T, 'fence');
  splitWall(d, `${id}_W`, [x0, y0], [x0, y1], g('west'), FENCE_H, FENCE_T, 'fence');
  splitWall(d, `${id}_E`, [x1, y0], [x1, y1], g('east'), FENCE_H, FENCE_T, 'fence');
}

export interface BuildingSpec {
  height: number;
  floors: number;
  hollow?: boolean;
  enterable: boolean;
  roof_walkable: boolean;
  stairwell?: boolean;
  set_piece: SetPiece;
  doorSides?: Side[];
  windowSides?: Side[];
  loopholeSides?: Side[];
  partitionRooms?: boolean;
  roofHatch?: boolean;
  /**
   * Stair cores of a building that gets one (S2_ROOMS_PLAN.md section 3: two for the hospital and long residential blocks,
   * designer 2026-11-19): a count spread from the low to the high end of the long axis, or explicit positions. Default 1 = ['low']
   * (S1). A core that does not fit (or would overlap a neighbour / an earlier core on both sides) is skipped.
   */
  cores?: number | CoreAt[];
  /** building-archetypes.md section 1, from the lot template (TEMPLATE_ARCHETYPE); informational until S2. */
  archetype?: Archetype;
}

export { addWall, fenceRect, addBuilding, addLadderAt, splitWall };
export type { Draft, DraftBuilding, Side, Rect };

/** Stair core geometry (metres). BUILDING_GENERATOR.md 4.2: flights >= 150 wide, 35 deg, a free approach box at every end. */
export const STAIR_FLIGHT_W = 1.5;
export const STAIR_LANE_PITCH = 1.6;   // centre to centre of the two switchback lanes (10 cm between the flights)
export const STAIR_APPROACH = STAIR_APPROACH_CM / 100; // free box in front of a foot / after a top (the squad bunches here), 4.2: 200 cm
/** The wall between the two lanes of a switchback core (BUILDING_GENERATOR.md 4.2): fills the 10 cm lane gap, no door. */
export const STAIR_LANE_WALL_T = 10;
const WALL_INNER = BUILDING_T / 200;   // the perimeter wall's half thickness: its inner face is this far inside the footprint

/** Where a stair core sits along the long axis: the low / high end, or metres from the low inner wall face to the core box. */
export type CoreAt = 'low' | 'high' | number;
export interface StairCoreOptions {
  at?: CoreAt;
  /** The core's number in its building: 0 (default) keeps the S1 ids, k > 0 prefixes the stair ids with `c<k>_`. */
  index?: number;
}
/** A core box's length along the long axis (metres): both approach boxes + one flight's run. */
export const stairCoreLength = (): number => 2 * STAIR_APPROACH + STAIR_RUN_PER_RISE * (LEVEL_HEIGHT / 100);

export interface StairCore {
  stairs: Stair[];
  /** The whole core: both lanes + the approach boxes (a keep-out for partitions, props, covers). */
  box: Rect;
  /** A flight's exact footprint (lane width x run, no overshoot): the slab gap above it. */
  flightRect: (st: Stair) => Rect;
  /** A hall's upper-level slab keeps only the core; these rectangles are the rest (the 'atrium' gaps). */
  voidRects: Rect[];
  /** The hall's mezzanine at +1 (designer fork 4, 2026-11-18): the whole strip along the core's side wall. */
  mezzanine: Rect;
  /** The 'atrium' gaps of the mezzanine level (everything but the mezzanine strip). */
  mezzanineVoid: Rect[];
  /**
   * A 2-lane core's lane walls (BUILDING_GENERATOR.md 4.2): one per storey the flights span, on the 10 cm gap between the lanes,
   * from the flights' low end to their high end only (the approach boxes at both ends stay free), no door - a path cannot hug
   * the ramp edge. Empty for a 1-lane core.
   */
  laneWalls: { a: Vec2; b: Vec2; floor: number }[];
  side: Side;
}

/**
 * Switchback core along a long side wall of `r` reaching level `upTo` (one 300-rise flight per storey, lanes alternating).
 * Positions are NOT snapped to the 50 cm grid on purpose: they are measured from the wall's inner face (footprint edge +
 * WALL_INNER) so the approach boxes and the 10 cm lane gap are exact; the kit's stair module is placed by these points.
 * Returns null when the footprint is too short / narrow for a 35-degree flight with both approach boxes (inside length >=
 * 2 x STAIR_APPROACH + run: 4.0 + 4.29 = 8.29 m plus the wall faces).
 * `opts.at` places the core along the long axis (S2_ROOMS_PLAN.md section 7 item 2): 'low' (default, the low-end corner as in
 * S1), 'high' (flush with the high-end inner face) or metres from the low inner face to the core box; outside the wall faces -> null.
 * `opts.index` > 0 (a building's second, third... core) prefixes the stair ids with `c<index>_`; 0 keeps the S1 ids.
 */
export function planStairCore(bid: string, r: Rect, upTo: number, avoidSides: Side[] = [], others: readonly Rect[] = [],
  opts: StairCoreOptions = {}): StairCore | null {
  if (upTo <= 0) return null;
  const [[x0, y0], [x1, y1]] = r;
  const alongY = y1 - y0 >= x1 - x0;
  const run = STAIR_RUN_PER_RISE * (LEVEL_HEIGHT / 100);
  const lo = (alongY ? y0 : x0) + WALL_INNER, hi = (alongY ? y1 : x1) - WALL_INNER;
  const at = opts.at ?? 'low';
  const start = at === 'low' ? lo : at === 'high' ? hi - stairCoreLength() : lo + at; // the core box's low end
  const a0 = start + STAIR_APPROACH, a1 = a0 + run;
  if (start < lo - 1e-6 || a1 + STAIR_APPROACH > hi + 1e-6) return null;
  const lanes = upTo > 1 ? 2 : 1;
  const coreW = 0.05 + STAIR_FLIGHT_W + (lanes - 1) * STAIR_LANE_PITCH + 0.05;
  const crossLo = (alongY ? x0 : y0) + WALL_INNER, crossHi = (alongY ? x1 : y1) - WALL_INNER;
  if (coreW + 1.2 > crossHi - crossLo) return null; // keep >= 1.2 m of floor beside the core
  const sides: Side[] = alongY ? ['west', 'east'] : ['north', 'south'];
  // a side is blocked when its core box (lanes x flights + approaches) intersects another building's footprint (2026-11-18 engine
  // test: B_hall_n's core stood inside the B_office_w facade block, no navmesh). Blocked beats the door sides; both blocked -> no
  // core (the validator then reports the storeys unreachable and «Принять» stays closed) - never a core inside a neighbour.
  const boxFor = (sd: Side): Rect => {
    const low = sd === 'west' || sd === 'north';
    const bMin = low ? crossLo : crossHi - coreW, bMax = low ? crossLo + coreW : crossHi;
    return alongY ? [[bMin, start], [bMax, a1 + STAIR_APPROACH]] : [[start, bMin], [a1 + STAIR_APPROACH, bMax]];
  };
  const clear = (sd: Side) => !others.some((o) => rect.overlaps(boxFor(sd), o, -0.01));
  const side = sides.find((sd) => !avoidSides.includes(sd) && clear(sd)) ?? sides.find(clear);
  if (!side) return null;
  const nearLow = side === 'west' || side === 'north';
  const laneC = (k: number) => nearLow ? crossLo + 0.05 + STAIR_FLIGHT_W / 2 + k * STAIR_LANE_PITCH
    : crossHi - 0.05 - STAIR_FLIGHT_W / 2 - k * STAIR_LANE_PITCH;
  const P = (along: number, cross: number): Vec2 => (alongY ? [cross, along] : [along, cross]);
  const stairs: Stair[] = [];
  const idBase = opts.index ? `${bid}_c${opts.index}` : bid;
  for (let n = 0; n < upTo; n++) {
    const k = n % 2, up = n % 2 === 0;
    stairs.push({ id: `${idBase}_stair_${n}`, pos: P(up ? a0 : a1, laneC(k)), from: n, to: n + 1, kind: 'stairwell', top: P(up ? a1 : a0, laneC(k)) });
  }
  const cMin = nearLow ? crossLo : crossHi - coreW, cMax = nearLow ? crossLo + coreW : crossHi;
  const R = (aMin: number, aMax: number, bMin: number, bMax: number): Rect => (alongY ? [[bMin, aMin], [bMax, aMax]] : [[aMin, bMin], [aMax, bMax]]);
  const box = R(start, a1 + STAIR_APPROACH, cMin, cMax);
  const flightRect = (st: Stair): Rect => {
    const along = (p: Vec2) => (alongY ? p[1] : p[0]), cross = (p: Vec2) => (alongY ? p[0] : p[1]);
    const c = cross(st.pos);
    return R(Math.min(along(st.pos), along(st.top)), Math.max(along(st.pos), along(st.top)), c - STAIR_FLIGHT_W / 2 - 0.05, c + STAIR_FLIGHT_W / 2 + 0.05);
  };
  const fLo = alongY ? y0 : x0, fHi = alongY ? y1 : x1, gLo = alongY ? x0 : y0, gHi = alongY ? x1 : y1;
  const voidRects: Rect[] = [
    nearLow ? R(fLo, fHi, cMax, gHi) : R(fLo, fHi, gLo, cMin),
    R(a1 + STAIR_APPROACH, fHi, cMin, cMax),
    ...(start > lo + 1e-6 ? [R(fLo, start, cMin, cMax)] : []), // a core off the low end: the strip before it
  ].filter((v) => rect.size(v)[0] > 0.05 && rect.size(v)[1] > 0.05);
  const mezzanine = R(lo, hi, cMin, cMax);
  const mezzanineVoid = [voidRects[0]];
  const mid = (laneC(0) + laneC(1)) / 2;
  const laneWalls = lanes > 1 ? Array.from({ length: upTo }, (_, f) => ({ a: P(a0, mid), b: P(a1, mid), floor: f })) : [];
  return { stairs, box, flightRect, voidRects, mezzanine, mezzanineVoid, laneWalls, side };
}

/** The route layer name of a building level: ground, its storeys, its roof (the ladders' `from` / `to` convention). */
export function levelLayer(b: { id: string; floors: number }, level: number): string {
  return level <= 0 ? 'ground' : level >= b.floors ? `roof:${b.id}` : `floor:${b.id}:${level}`;
}

/**
 * Route points up a building's stair core from the ground to `toLevel` (default: its roof): every flight's foot then top,
 * layers per level, a link at every arrival. Null when the flights do not chain 0 -> toLevel.
 */
export function stairChainUp(b: { id: string; floors: number; stairs: Stair[] }, toLevel = b.floors):
  { points: Vec2[]; layers: string[]; links: { at: number; via: string }[]; flights: Stair[] } | null {
  const flights: Stair[] = [];
  for (let lv = 0; lv < toLevel; lv++) {
    const st = b.stairs.find((x) => x.kind === 'stairwell' && x.from === lv && x.to === lv + 1);
    if (!st) return null;
    flights.push(st);
  }
  if (!flights.length) return null;
  const points: Vec2[] = [], layers: string[] = [], links: { at: number; via: string }[] = [];
  for (const st of flights) {
    points.push(st.pos, st.top);
    layers.push(levelLayer(b, st.from), levelLayer(b, st.to));
    links.push({ at: points.length - 1, via: st.id });
  }
  return { points, layers, links, flights };
}

/**
 * The working state of one addBuilding call (metres, draft frame), threaded through its stages. The stages mutate it in order;
 * `emitBuilding` turns it into the DraftBuilding.
 */
interface BuildingPlan {
  d: Draft;
  bid: string;
  r: Rect;
  s: BuildingSpec;
  random: () => number;
  x0: number; y0: number; x1: number; y1: number;
  /** The exterior door sides (the requested ones + the opposite side above 150 m2). */
  doorSides: Side[];
  /** Storeys with a walkable floor of their own (a hall: only its ground). */
  walkableFloors: number;
  /** The stair cores, index 0 = the S1 core (its ids are unprefixed). Empty when the building has none. */
  cores: StairCore[];
  /** The exterior door gaps per wall (along-wall intervals), filled by placeExteriorDoors. */
  perSide: Record<Side, [number, number][]>;
  doors: Door[];
  openings: WallOpening[];
  partitions: GenPartition[];
  floors_detail: BuildingFloor[];
  hatches: RoofHatch[];
  parapets: RoofParapet[];
  rooms: GenRoom[];
  mantle_edges: MantleEdge[];
  stairs: Stair[];
}

const OPPOSITE_SIDE: Record<Side, Side> = { north: 'south', south: 'north', east: 'west', west: 'east' };

/**
 * A rectangular building: perimeter walls, openings (doors, windows, loopholes), partitions, rooms, roof parapets and hatches.
 * Split into named stages (S2_ROOMS_PLAN.md section 7 item 2). The S2 order is shell -> cores -> rooms -> doors -> windows ->
 * roof -> slabs (3.2); the stages below still run in the S1 order (openings before rooms) so the output - and the shared
 * `random` stream, drawn only by placeExteriorDoors - is unchanged. Window and parapet mantle edges are emitted with them.
 * With the draft's `building_plans` toggle an enterable office / block takes the planned path instead (addPlannedOffice S2.2, addPlannedBlock S2.3).
 */
function addBuilding(d: Draft, bid: string, r: Rect, s: BuildingSpec, random: () => number): DraftBuilding {
  if (d.building_plans && s.enterable && !s.hollow) {
    if (s.archetype === 'office') return addPlannedOffice(d, bid, r, s, random);
    if (s.archetype === 'block') return addPlannedBlock(d, bid, r, s, random);
  }
  return legacyStages(planShell(d, bid, r, s, random));
}

/** The S1 stages 2-14 of addBuilding on a fresh shell (the legacy path, and the fallback of a plan that did not fit). */
function legacyStages(p: BuildingPlan): DraftBuilding {
  planCores(p);
  placeExteriorDoors(p);
  placeWindows(p);
  placeLoopholes(p);
  buildPerimeterWalls(p);
  placeStairs(p);
  planRooms(p);
  addHallLandings(p);
  fitWindows(p.r, p.openings, p.mantle_edges, p.partitions, p.cores, p.perSide);
  addRoofParapets(p);
  addRoofHatch(p);
  buildSlabs(p);
  return emitBuilding(p);
}

/**
 * Step S2.2 piece B: an enterable office from its BAND PLAN (S2_ROOMS_PLAN.md 3.2; buildings/bandPlan.ts + planDoors.ts + planWindows.ts).
 * The footprint is snapped to the 100 cm grid (keeping its min corner, rounded to a whole cm), the stair core is placed by planCores as
 * always and mapped to the plan (core at the low / high end of the long wall it stands on), the plan is cut, its doors / portals /
 * windows planned, and everything is emitted: rooms per cell and storey, partitions per shared edge, the same walls as `interior` walls,
 * exterior doors and windows with `links` / `role` / `module`. A plan that does not fit (or a core the plan cannot map) runs the legacy
 * stages on the ORIGINAL rectangle and says why in `plan.fallback`. The shared `random` stream is drawn once per exterior door side,
 * in placeExteriorDoors' order, and nowhere else (nothing is drawn before a fallback).
 */
function addPlannedOffice(d: Draft, bid: string, r: Rect, s: BuildingSpec, random: () => number): DraftBuilding {
  const COREBAY = BUILDING_PLANS.constants.core.bay_length_cm;
  const sideId = (side: Side): string => `${bid}_${side[0].toUpperCase()}`;
  // 1. snap: whole-cm min corner, sizes floored to the 100 grid, the long axis = the plan's u
  const sn = snapFootprint((r[1][0] - r[0][0]) * 100 + 1e-6, (r[1][1] - r[0][1]) * 100 + 1e-6);
  const wx = sn.swapped ? sn.D : sn.L, wy = sn.swapped ? sn.L : sn.D;
  const cx0 = Math.round(r[0][0] * 100), cy0 = Math.round(r[0][1] * 100), cx1 = cx0 + wx, cy1 = cy0 + wy;
  const rp: Rect = [[cx0 / 100, cy0 / 100], [cx1 / 100, cy1 / 100]];
  // a square footprint (a tie) takes the axis planStairCore takes: the same expression on the same rectangle, so the two always agree
  const alongY = rp[1][1] - rp[0][1] >= rp[1][0] - rp[0][0];
  // 2. shell + the core, mapped to the plan
  const p = planShell(d, bid, rp, s, random);
  planCores(p);
  const first = p.doorSides[0];
  const longSides: Side[] = alongY ? ['west', 'east'] : ['north', 'south'];
  let coreSide: Side = longSides.includes(first) ? OPPOSITE[first] : alongY ? 'west' : 'north';
  let reason: string | null = null;
  let core: { u0: number; depth: 350 | 200; portalU0: number } | null = null;
  if (p.cores.length > 1) reason = 'multi_core';
  else if (p.cores.length === 1) {
    const c = p.cores[0], [[bx0, by0], [bx1, by1]] = c.box;
    coreSide = c.side;
    const lowRel = Math.round((alongY ? by0 : bx0) * 100 - (alongY ? cy0 : cx0)); // the box's low end from the footprint's low corner
    const inner = Math.round(BUILDING_T / 2); // the perimeter wall's half thickness: a core at the low end starts here
    const highRel = sn.L - inner - Math.round(stairCoreLength() * 100); // ...and one flush with the high end here
    const lenOk = Math.abs((alongY ? by1 - by0 : bx1 - bx0) * 100 - stairCoreLength() * 100) <= 1;
    const u0 = Math.abs(lowRel - inner) <= 1 ? 0 : Math.abs(lowRel - highRel) <= 1 ? sn.L - COREBAY : null;
    if (u0 === null || !lenOk) reason = 'core_unmapped';
    // portalU0 = where planDoors measures its two pad portals from: the pads sit `inner` after the cell's start for the low core,
    // so the high core (its box starts later in its cell) gets the same portals shifted by the box's offset
    else core = { u0, depth: c.laneWalls.length ? 350 : 200, portalU0: lowRel - inner };
  }
  // 3. the plan
  const planSeed = fnv1a(`${bid}|${sn.L}x${sn.D}|${s.floors}|office`);
  const plan = reason ? null : planBands({ L: sn.L, D: sn.D, archetype: 'office', core: core ? { u0: core.u0, depth: core.depth } : null, seed: planSeed });
  if (!plan || plan.kind === 'fallback') {
    const p2 = planShell(d, bid, r, s, random);
    const b = legacyStages(p2);
    b.plan = { v: 1, kind: 'bsp', seed: planSeed, core_wall: sideId(p2.cores[0]?.side ?? coreSide), street_wall: sideId(first), fallback: reason ?? plan?.fallback ?? 'plan_fallback' };
    return b;
  }
  const f: PlanFrame = { alongY, coreSide, cx0, cy0, cx1, cy1 };
  return emitPlanned({ d, bid, s, random, p, plan, f, rp, planSeed, coreSide, first, doors: { core: core ? { u0: core.portalU0, depth: core.depth } : null } });
}

/**
 * Step S2.3: an enterable residential BLOCK from its band plan (S2_ROOMS_PLAN.md 3.3 / 3.4; bandPlan.ts planBlock). Unlike the office the plan comes
 * first: it cuts the long side into sections (`flat core flat`), and each section's stair core is then placed at the plan's position
 * (`cores: [metres]` of planCores, one `c<k>_` prefixed core per section after the first); the core wall is the long wall away from the street
 * (the first door side). Shell, doors, windows, partitions, rooms are the shared tail (emitPlanned), plus the flats (`units`, `dead_end`, the balcony
 * columns) and the `slot` windows. A plan that does not fit - or a core the plan cannot map, or cores that landed on different walls - runs the legacy
 * stages on the ORIGINAL rectangle with the original spec and says why in `plan.fallback`.
 */
function addPlannedBlock(d: Draft, bid: string, r: Rect, s: BuildingSpec, random: () => number): DraftBuilding {
  const COREBAY = BUILDING_PLANS.constants.core.bay_length_cm;
  const sideId = (side: Side): string => `${bid}_${side[0].toUpperCase()}`;
  const sn = snapFootprint((r[1][0] - r[0][0]) * 100 + 1e-6, (r[1][1] - r[0][1]) * 100 + 1e-6);
  const wx = sn.swapped ? sn.D : sn.L, wy = sn.swapped ? sn.L : sn.D;
  const cx0 = Math.round(r[0][0] * 100), cy0 = Math.round(r[0][1] * 100), cx1 = cx0 + wx, cy1 = cy0 + wy;
  const rp: Rect = [[cx0 / 100, cy0 / 100], [cx1 / 100, cy1 / 100]];
  const alongY = rp[1][1] - rp[0][1] >= rp[1][0] - rp[0][0];
  const doorSides = planShell(d, bid, rp, s, random).doorSides, first = doorSides[0];
  const longSides: Side[] = alongY ? ['west', 'east'] : ['north', 'south'];
  // the core's flights reach the roof of a walkable-roof block, else the top storey: >= 2 flights = two lanes (350 deep), one = a single lane (200)
  const coreTop = s.roof_walkable ? s.floors : s.floors - 1;
  const depth: 350 | 200 = coreTop > 1 ? 350 : 200;
  const planSeed = fnv1a(`${bid}|${sn.L}x${sn.D}|${s.floors}|block`);
  const plan = planBlock({ L: sn.L, D: sn.D, depth, seed: planSeed });
  let coreSide: Side = longSides.includes(first) ? OPPOSITE[first] : alongY ? 'west' : 'north';
  const fallback = (why: string): DraftBuilding => {
    const p2 = planShell(d, bid, r, s, random);
    const b = legacyStages(p2);
    b.plan = { v: 1, kind: 'bsp', seed: planSeed, core_wall: sideId(p2.cores[0]?.side ?? coreSide), street_wall: sideId(first), fallback: why };
    return b;
  };
  if (plan.kind === 'fallback' || !plan.sections) return fallback(plan.fallback ?? 'plan_fallback');
  // the shell with one core per section, at the plan's positions; only the street side is kept clear of them (the court side is the core wall)
  const p = planShell(d, bid, rp, { ...s, cores: plan.sections.map((sec) => sec.core.u0 / 100) }, random);
  const allSides = p.doorSides;
  p.doorSides = [first];
  planCores(p);
  p.doorSides = allSides;
  if (p.cores.length !== plan.sections.length) return fallback('block_core_blocked');
  coreSide = p.cores[0].side;
  const inner = Math.round(BUILDING_T / 2);
  for (const [k, c] of p.cores.entries()) {
    const [[bx0, by0], [bx1, by1]] = c.box;
    const lowRel = Math.round((alongY ? by0 : bx0) * 100 - (alongY ? cy0 : cx0));
    const lenOk = Math.abs((alongY ? by1 - by0 : bx1 - bx0) * 100 - stairCoreLength() * 100) <= 1;
    const sec = plan.sections[k];
    if (c.side !== coreSide) return fallback('block_core_sides_differ');
    if (!lenOk || Math.abs(lowRel - inner - sec.core.u0) > 1 || sec.core.u0 + COREBAY > sn.L || (c.laneWalls.length ? 350 : 200) !== depth) return fallback('block_core_unmapped');
  }
  const f: PlanFrame = { alongY, coreSide, cx0, cy0, cx1, cy1 };
  return emitPlanned({ d, bid, s, random, p, plan, f, rp, planSeed, coreSide, first, doors: { core: null, cores: plan.sections.map((sec) => ({ u0: sec.core.u0, depth })) } });
}

/** What the shared tail of a planned building needs (addPlannedOffice / addPlannedBlock): the shell with its cores placed, the plan, its frame. */
interface PlannedArgs {
  d: Draft; bid: string; s: BuildingSpec; random: () => number; p: BuildingPlan; plan: BandPlan; f: PlanFrame; rp: Rect; planSeed: number;
  coreSide: Side; first: Side;
  /** The core(s) for planDoors: the pad-portal origin and depth (an office `core`, a block `cores` per section). */
  doors: Pick<PlanDoorsOpts, 'core' | 'cores'>;
}

/**
 * The shared tail of a planned building (S2_ROOMS_PLAN.md 3.2 steps 5-9): the exterior door draws (ONE per door side, in placeExteriorDoors' order),
 * doors / portals / windows (and a block's units and slots), the draft openings, partitions, rooms, then the rest of the pipeline.
 */
function emitPlanned(a: PlannedArgs): DraftBuilding {
  const { d, bid, s, random, p, plan, f, rp, planSeed, coreSide, first } = a;
  const O = BUILDING_PLANS.constants.openings;
  const sideId = (side: Side): string => `${bid}_${side[0].toUpperCase()}`;
  const sides = wallSides(f);
  // 5. exterior doors: ONE draw per door side, as placeExteriorDoors does (the plan's v axis may run against the draft's: mirror t)
  const doorWalls = p.doorSides.map((side) => {
    const wall = wallKeyOf(f, side), t = 0.25 + 0.5 * random();
    return { wall, t: (wall === 'u0' || wall === 'uL') && vFlipped(f) ? 1 - t : t };
  });
  const res = planDoors(plan, { storeys: s.floors, seed: planSeed, ...a.doors, doorWalls });
  // windows keep clear of a neighbour standing within 1 m outside the wall
  const blocked: Partial<Record<WallKey, [number, number][]>> = {};
  for (const key of WALL_ORDER) {
    const side = sides[key], horizontal = side === 'north' || side === 'south';
    const strip: Rect = side === 'north' ? [[rp[0][0], rp[0][1] - 1], [rp[1][0], rp[0][1]]] : side === 'south' ? [[rp[0][0], rp[1][1]], [rp[1][0], rp[1][1] + 1]]
      : side === 'west' ? [[rp[0][0] - 1, rp[0][1]], [rp[0][0], rp[1][1]]] : [[rp[1][0], rp[0][1]], [rp[1][0] + 1, rp[1][1]]];
    for (const o of d.buildings) {
      const [[fx0, fy0], [fx1, fy1]] = o.footprint;
      if (Math.min(strip[1][0], fx1) - Math.max(strip[0][0], fx0) <= 1e-9 || Math.min(strip[1][1], fy1) - Math.max(strip[0][1], fy0) <= 1e-9) continue;
      const lo = horizontal ? Math.max(rp[0][0], fx0) : Math.max(rp[0][1], fy0), hi = horizontal ? Math.min(rp[1][0], fx1) : Math.min(rp[1][1], fy1);
      const a = horizontal ? toPlan(f, lo, rp[0][1]) : toPlan(f, rp[0][0], lo), b = horizontal ? toPlan(f, hi, rp[0][1]) : toPlan(f, rp[0][0], hi);
      const ax = key === 'v0' || key === 'vD' ? 0 : 1; // the plan axis along the wall: u for the long walls, v for the ends
      (blocked[key] ??= []).push([Math.min(a[ax], b[ax]), Math.max(a[ax], b[ax])]);
    }
  }
  const windows = planWindows(plan, { storeys: s.floors, doors: res.openings, blocked });
  const block = plan.archetype === 'block';
  const units = block ? planUnits(plan, res.openings, res.lobbies, s.floors) : [];
  const unitOf = new Map<string, string>(); // `${storey}|${cell id}` -> unit id
  for (const u of units) for (const c of u.cells) unitOf.set(`${u.storey}|${c}`, `${bid}_u_f${u.storey}_${u.key}`);
  // 6. the openings in the draft (metres)
  const roomId = (storey: number, cell: string): string => `${bid}_r_f${storey}_${cell}`;
  const mkOpening = (o: PlanOpening): WallOpening => {
    const q = o.axis === 'u' ? toDraft(f, o.c, o.at) : toDraft(f, o.at, o.c);
    const window = o.kind === 'window';
    return {
      id: `${bid}_${o.id}`,
      kind: o.kind,
      pos: q,
      floor: o.storey,
      ...(o.wall ? { wall: sideId(sides[o.wall]) } : {}),
      width: o.width,
      sill_height: window ? O.window_sill_cm : 0,
      top_height: window ? O.window_head_cm : O.door_top_cm,
      yaw: o.wall ? SIDE_YAW[sides[o.wall]] : yawOfDir(planDir(f, o.axis === 'u' ? 'v' : 'u')),
      links: [roomId(o.storey, o.links[0]), o.links[1] === 'street' ? 'street' : roomId(o.storey, o.links[1])],
      role: o.role,
      module: o.module,
      ...(o.slot ? { slot: o.slot } : {}),
    };
  };
  const along = (side: Side, q: Vec2): number => (side === 'north' || side === 'south' ? q[0] : q[1]);
  for (const o of [...res.openings, ...windows].filter((x) => x.wall)) {
    const side = sides[o.wall!], op = mkOpening(o), wallId = sideId(side);
    p.openings.push(op);
    if (o.kind === 'door') {
      p.doors.push({ pos: op.pos, floor: o.storey, wall: wallId });
      p.perSide[side].push([along(side, op.pos) - o.width / 200, along(side, op.pos) + o.width / 200]);
    } else {
      const horizontal = side === 'north' || side === 'south', half = o.width / 200;
      const dx = horizontal ? half : 0, dy = horizontal ? 0 : half;
      p.mantle_edges.push({
        id: `me_${op.id}`,
        start: [op.pos[0] - dx, op.pos[1] - dy],
        end: [op.pos[0] + dx, op.pos[1] + dy],
        floor: o.storey,
        base_z: levelZ(o.storey),
        height_cm: O.window_sill_cm,
        normal: [SIDE_NORMAL[side][0], SIDE_NORMAL[side][1]],
        kind: 'window',
        building: bid,
        wall: wallId,
      });
    }
  }
  // 8. partitions (one per shared cell edge that is not a corridor seam), the same wall as an `interior` wall split at its openings
  const interior = res.openings.filter((x) => !x.wall);
  const segs = sharedSegs(plan);
  const seamIds = new Map<string, string[]>();
  for (let storey = 0; storey < s.floors; storey++) {
    let k = 0, seamK = 0;
    for (const seg of segs) {
      if (seg.seam) {
        const seamId = `${bid}_f${storey}_seam_${seamK++}`;
        for (const c of [seg.a, seg.b]) seamIds.set(roomId(storey, c.id), [...(seamIds.get(roomId(storey, c.id)) ?? []), seamId]);
        continue;
      }
      const ops = interior.filter((o) => o.storey === storey && o.axis === seg.axis && o.at === seg.at && o.c >= seg.lo && o.c <= seg.hi
        && o.links.includes(seg.a.id) && o.links.includes(seg.b.id)).map(mkOpening);
      const a = seg.axis === 'u' ? toDraft(f, seg.lo, seg.at) : toDraft(f, seg.at, seg.lo), b = seg.axis === 'u' ? toDraft(f, seg.hi, seg.at) : toDraft(f, seg.at, seg.hi);
      const partId = `${bid}_part_f${storey}_${k++}`;
      p.partitions.push({ id: partId, start: a, end: b, floor: storey, thickness: 20, openings: ops });
      const horizontal = a[1] === b[1];
      const gaps = ops.map((op): [number, number] => { const c = horizontal ? op.pos[0] : op.pos[1], h = (op.width ?? 0) / 200; return [c - h, c + h]; });
      splitWall(d, partId, a, b, gaps, LEVEL_HEIGHT, 20, 'interior', storey, bid);
    }
  }
  // 7. rooms: one per cell and storey
  const lobbies = new Set(res.lobbies);
  const doorIds = new Map<string, string[]>();
  for (const o of [...res.openings, ...windows]) {
    for (const link of o.links) if (link !== 'street') doorIds.set(roomId(o.storey, link), [...(doorIds.get(roomId(o.storey, link)) ?? []), `${bid}_${o.id}`]);
  }
  for (let storey = 0; storey < s.floors; storey++) {
    for (const cell of plan.cells) {
      const id = roomId(storey, cell.id), [min, max] = cellRect(f, cell);
      const kind: GenRoom['kind'] = storey === 0 && lobbies.has(cell.id) && cell.kind !== 'service' ? 'lobby' : cell.kind === 'core' ? 'stairwell' : cell.kind === 'corridor' ? 'corridor' : cell.kind === 'service' ? 'service' : 'room';
      const ids = [...(doorIds.get(id) ?? []), ...(seamIds.get(id) ?? [])];
      p.rooms.push({
        id, floor: storey, bounds: { min, max }, kind,
        ...(cell.kind === 'end' ? { unit: 'suite' } : {}),
        ...(unitOf.has(`${storey}|${cell.id}`) ? { unit: unitOf.get(`${storey}|${cell.id}`) } : {}),
        ...(block && !unitOf.has(`${storey}|${cell.id}`) ? { zone: 'common' } : {}),
        walls: WALL_ORDER.filter((key) => wallSpan(plan, cell, key)).map((key) => sideId(sides[key])),
        ...(ids.length ? { door_ids: ids } : {}),
      });
    }
  }
  // 9. the rest of the pipeline (no fitWindows / planRooms / placeWindows / placeExteriorDoors on this path)
  placeLoopholes(p);
  buildPerimeterWalls(p);
  placeStairs(p);
  addRoofParapets(p);
  addRoofHatch(p);
  buildSlabs(p);
  for (const fd of p.floors_detail) fd.rooms = []; // the rooms live in `rooms` only (nothing reads the second copy)
  const b = emitBuilding(p, { v: 1, kind: 'bands', seed: planSeed, core_wall: sideId(coreSide), street_wall: p.doors[0]?.wall ?? sideId(first),
    ...(block ? { variant: 'block', sections: plan.sections?.length ?? 1 } : plan.variant ? { variant: plan.variant } : {}) });
  if (units.length) {
    b.units = units.map((u) => ({
      id: `${bid}_u_f${u.storey}_${u.key}`, kind: 'flat', floor: u.storey, rooms: u.cells.map((c) => roomId(u.storey, c)), entries: u.entries.map((e) => `${bid}_${e}`),
      column: `${bid}_col_${u.key}`, ...(u.dead_end ? { dead_end: true, run_cm: u.run_cm } : {}),
    }));
  }
  return b;
}

/** Stage 1, shell: the footprint, the empty element lists and the exterior door sides (>= 2 sides above 150 m2: BUILDING_GENERATOR.md 3 step 5). */
function planShell(d: Draft, bid: string, r: Rect, s: BuildingSpec, random: () => number): BuildingPlan {
  const [[x0, y0], [x1, y1]] = r;
  const doorSides = [...(s.doorSides ?? ['south'])];
  if (s.enterable && (x1 - x0) * (y1 - y0) > 150 && new Set(doorSides).size < 2) doorSides.push(OPPOSITE_SIDE[doorSides[0]]);
  return {
    d, bid, r, s, random, x0, y0, x1, y1, doorSides,
    walkableFloors: s.hollow ? 1 : s.floors,
    cores: [],
    perSide: { north: [], south: [], east: [], west: [] },
    doors: [], openings: [], partitions: [], floors_detail: [], hatches: [], parapets: [], rooms: [], mantle_edges: [], stairs: [],
  };
}

/** The core positions a `cores` request asks for: a count is spread evenly from 'low' to 'high' along the long axis. */
function coreSlots(r: Rect, req: BuildingSpec['cores']): CoreAt[] {
  if (Array.isArray(req)) return req;
  const n = Math.max(1, Math.floor(req ?? 1));
  if (n === 1) return ['low'];
  const [[x0, y0], [x1, y1]] = r;
  const free = Math.max(y1 - y0, x1 - x0) - 2 * WALL_INNER - stairCoreLength();
  return Array.from({ length: n }, (_, k): CoreAt => (k === 0 ? 'low' : k === n - 1 ? 'high' : (free * k) / (n - 1)));
}

/**
 * Stage 2, core(s) - first, as BUILDING_GENERATOR.md 3 wants (they constrain windows, rooms and slabs).
 * P1 (2026-11-18): every enterable multi-storey building gets a core; a hall's core reaches its roof, or only its mezzanine;
 * an explicit `stairwell: false` (hand-shaped ruins with external stairs / holes) is kept. Only buildings placed EARLIER are
 * known here; a later neighbour overlapping a core is caught by the validator (stair_in_other_building, order-independent) -
 * API review 4afb04f6. A later core also keeps out of the earlier cores' boxes.
 */
function planCores(p: BuildingPlan): void {
  const { s } = p;
  const wantCore = s.enterable && (s.stairwell ?? (!s.hollow && s.floors >= 2));
  if (!wantCore) return;
  const coreTop = s.hollow ? (s.roof_walkable ? s.floors : 1) : (s.roof_walkable ? s.floors : s.floors - 1);
  const neighbours = p.d.buildings.map((o) => o.footprint);
  for (const at of coreSlots(p.r, s.cores)) {
    const core = planStairCore(p.bid, p.r, coreTop, p.doorSides, [...neighbours, ...p.cores.map((c) => c.box)], { at, index: p.cores.length });
    if (core) p.cores.push(core);
  }
}

/** Stage 3, exterior doors on the door sides (the only stage that draws from `random`: one draw per door). */
function placeExteriorDoors(p: BuildingPlan): void {
  const { bid, s, x0, y0, x1, y1, doors, openings, perSide } = p;
  for (const side of s.enterable ? p.doorSides : []) {
    const horizontal = side === 'north' || side === 'south';
    const lo = horizontal ? x0 : y0, hi = horizontal ? x1 : y1;
    const at = lo + (hi - lo) * (0.25 + 0.5 * p.random());
    perSide[side].push([at - DOOR_W / 2, at + DOOR_W / 2]);
    const pos: Vec2 = horizontal ? [at, side === 'north' ? y0 : y1] : [side === 'west' ? x0 : x1, at];
    const doorYaw = side === 'north' ? 270 : side === 'south' ? 90 : side === 'west' ? 180 : 0;
    doors.push({ pos, floor: 0, wall: `${bid}_${side[0].toUpperCase()}` });
    openings.push({
      id: `${bid}_door_${side}_${doors.length}`,
      kind: 'door',
      pos,
      floor: 0,
      wall: `${bid}_${side[0].toUpperCase()}`,
      width: OPENING_DEFAULTS.door.width,
      sill_height: OPENING_DEFAULTS.door.sill,
      top_height: OPENING_DEFAULTS.door.top,
      yaw: doorYaw,
    });
  }
}

/** Stage 4, windows on facades (walkable storeys only so units can stand behind them) + their mantle edges. */
function placeWindows(p: BuildingPlan): void {
  const { bid, s, x0, y0, x1, y1, openings, mantle_edges, perSide, walkableFloors } = p;
  const core = p.cores[0] ?? null;
  for (const side of s.windowSides ?? []) {
    const horizontal = side === 'north' || side === 'south';
    const lo = horizontal ? x0 : y0, hi = horizontal ? x1 : y1;
    const len = hi - lo;
    const yaw = side === 'north' ? 270 : side === 'south' ? 90 : side === 'west' ? 180 : 0;
    const normal: Vec2 = side === 'north' ? [0, -1] : side === 'south' ? [0, 1] : side === 'west' ? [-1, 0] : [1, 0];
    const dx = horizontal ? 0.6 : 0, dy = horizontal ? 0 : 0.6;
    if (len >= 5) {
      const count = Math.max(1, Math.floor((len - 3) / 4));
      const step = (len - 3) / (count + 1);
      for (let k = 1; k <= count; k++) {
        const at = lo + 1.5 + k * step;
        const nearDoor = perSide[side].some(([d0, d1]) => at >= d0 - 1.2 && at <= d1 + 1.2);
        for (let f = 0; f < (s.hollow && core ? 2 : walkableFloors); f++) {
          if (f === 0 && nearDoor) continue;
          if (s.hollow && f === 1 && side !== core?.side) continue; // a hall's upper band only along its mezzanine
          const pos: Vec2 = horizontal ? [at, side === 'north' ? y0 : y1] : [side === 'west' ? x0 : x1, at];
          const wid = `${bid}_win_${side}_f${f}_${k}`;
          openings.push({
            id: wid,
            kind: 'window',
            pos,
            floor: f,
            wall: `${bid}_${side[0].toUpperCase()}`,
            width: OPENING_DEFAULTS.window.width,
            sill_height: OPENING_DEFAULTS.window.sill,
            top_height: OPENING_DEFAULTS.window.top,
            yaw,
          });
          mantle_edges.push({
            id: `me_${wid}`,
            start: [pos[0] - dx, pos[1] - dy],
            end: [pos[0] + dx, pos[1] + dy],
            floor: f,
            base_z: levelZ(f),
            height_cm: OPENING_DEFAULTS.window.sill,
            normal,
            kind: 'window',
            building: bid,
            wall: `${bid}_${side[0].toUpperCase()}`,
          });
        }
      }
    }
  }
}

/** Stage 5, fortified loopholes. */
function placeLoopholes(p: BuildingPlan): void {
  const { bid, s, x0, y0, x1, y1, openings, perSide, walkableFloors } = p;
  for (const side of s.loopholeSides ?? []) {
    const horizontal = side === 'north' || side === 'south';
    const lo = horizontal ? x0 : y0, hi = horizontal ? x1 : y1;
    const len = hi - lo;
    const yaw = side === 'north' ? 270 : side === 'south' ? 90 : side === 'west' ? 180 : 0;
    if (len >= 3) {
      const count = Math.max(1, Math.floor((len - 2) / 2.5));
      const step = (len - 2) / (count + 1);
      for (let k = 1; k <= count; k++) {
        const at = lo + 1.0 + k * step;
        const nearDoor = perSide[side].some(([d0, d1]) => at >= d0 - 0.8 && at <= d1 + 0.8);
        for (let f = 0; f < walkableFloors; f++) {
          if (f === 0 && nearDoor) continue;
          const pos: Vec2 = horizontal ? [at, side === 'north' ? y0 : y1] : [side === 'west' ? x0 : x1, at];
          openings.push({
            id: `${bid}_lp_${side}_f${f}_${k}`,
            kind: 'loophole',
            pos,
            floor: f,
            wall: `${bid}_${side[0].toUpperCase()}`,
            width: OPENING_DEFAULTS.loophole.width,
            sill_height: OPENING_DEFAULTS.loophole.sill,
            top_height: OPENING_DEFAULTS.loophole.top,
            yaw,
          });
        }
      }
    }
  }
}

/** Stage 6, the perimeter walls, split at the exterior door gaps. */
function buildPerimeterWalls(p: BuildingPlan): void {
  const { d, bid, s, x0, y0, x1, y1, perSide } = p;
  splitWall(d, `${bid}_N`, [x0, y0], [x1, y0], perSide.north, s.height, BUILDING_T, 'building', 0, bid);
  splitWall(d, `${bid}_S`, [x0, y1], [x1, y1], perSide.south, s.height, BUILDING_T, 'building', 0, bid);
  splitWall(d, `${bid}_W`, [x0, y0], [x0, y1], perSide.west, s.height, BUILDING_T, 'building', 0, bid);
  splitWall(d, `${bid}_E`, [x1, y0], [x1, y1], perSide.east, s.height, BUILDING_T, 'building', 0, bid);
}

/**
 * Stage 7, stairs - switchback cores (BUILDING_GENERATOR.md 4.2, causes A / B of the 2026-11-18 audit): one 300-rise flight per
 * storey, flights alternate between two lanes along the long side wall, a free approach box of STAIR_APPROACH in front of every
 * foot and after every top, never a corner pocket. Halls (hollow) get the same core: the landings of their upper levels are
 * slabs around the core only (floors_detail 'atrium' gaps = the rest of the hall), so the roof is reached by stairs. Each core's
 * lane walls (4.2): an interior wall one storey high between its two lanes on every storey the flights span.
 */
function placeStairs(p: BuildingPlan): void {
  const { d, bid } = p;
  p.cores.forEach((core, k) => {
    p.stairs.push(...core.stairs);
    const pre = k ? `${bid}_c${k}` : bid;
    for (const lw of core.laneWalls) addWall(d, `${pre}_lanewall_f${lw.floor}`, lw.a, lw.b, LEVEL_HEIGHT, STAIR_LANE_WALL_T, 'interior', lw.floor, bid);
  });
}

/** Stage 8, partitions and rooms (a BSP split for enterable partitioned buildings, else one room per storey). */
function planRooms(p: BuildingPlan): void {
  const { d, bid, r, s, x0, y0, x1, y1, openings, partitions, rooms, perSide, walkableFloors } = p;
  const bw = x1 - x0, bd = y1 - y0;
  if (!s.enterable) return;
  for (let f = 0; f < walkableFloors; f++) {
    if (!s.hollow && s.partitionRooms && (bw >= 8 || bd >= 6)) {
      // every core (both lanes + the approach boxes) is a keep-out for partitions on every storey it serves
      const stairBoxes: Rect[] = p.cores.map((c) => c.box);
      const extDoorXs = f === 0 ? [...perSide.north, ...perSide.south].map(([a, b]) => (a + b) / 2) : [];
      const extDoorYs = f === 0 ? [...perSide.west, ...perSide.east].map(([a, b]) => (a + b) / 2) : [];

      const blockedXs = [...extDoorXs];
      const blockedYs = [...extDoorYs];

      const leafBoxes: Rect[] = [];
      let partIdx = 0;
      const maxDepth = bw >= 12 && bd >= 9 ? 2 : 1;

      const pickSplit = (lo: number, hi: number, blockedCoords: number[], stairRanges: [number, number][]): number | null => {
        if (hi - lo < 6.5) return null;
        for (const t of [0.5, 0.42, 0.58, 0.35, 0.65]) {
          const c = lo + (hi - lo) * t;
          if (c - lo < 3.0 || hi - c < 3.0) continue;
          if (blockedCoords.some((bc) => Math.abs(c - bc) < 1.8)) continue;
          if (stairRanges.some(([s0, s1]) => c >= s0 && c <= s1)) continue;
          return c;
        }
        return null;
      };

      const bsp = (box: Rect, depth: number) => {
        const [[bx0, by0], [bx1, by1]] = box;
        const rw = bx1 - bx0, rd = by1 - by0;
        if (depth >= maxDepth || (rw < 6.5 && rd < 6.5)) {
          leafBoxes.push(box);
          return;
        }
        const tryX = (): number | null => pickSplit(
          bx0, bx1,
          blockedXs,
          stairBoxes.filter((sb) => sb[0][1] < by1 && sb[1][1] > by0).map((sb) => [sb[0][0], sb[1][0]]),
        );
        const tryY = (): number | null => pickSplit(
          by0, by1,
          blockedYs,
          stairBoxes.filter((sb) => sb[0][0] < bx1 && sb[1][0] > bx0).map((sb) => [sb[0][1], sb[1][1]]),
        );

        const preferX = rw >= rd;
        const splitX = preferX ? tryX() : null;
        const splitY = splitX === null ? tryY() : null;
        const fallbackX = splitX === null && splitY === null && !preferX ? tryX() : null;
        const px = splitX ?? fallbackX;

        if (px !== null) {
          let doorY = by0 + rd * (depth === 0 && maxDepth > 1 ? 0.28 : 0.5);
          if (stairBoxes.some((sb) => px >= sb[0][0] && px <= sb[1][0] && doorY >= sb[0][1] - 0.5 && doorY <= sb[1][1] + 0.5)) {
            doorY = Math.abs(doorY - by0) < Math.abs(by1 - doorY) ? by1 - 1.5 : by0 + 1.5;
          }
          blockedYs.push(doorY);
          const idx = partIdx++;
          const pDoorId = `${bid}_partdoor_f${f}_${idx}`;
          const pDoor: WallOpening = {
            id: pDoorId, kind: 'door', pos: [px, doorY], floor: f,
            width: OPENING_DEFAULTS.door.width, sill_height: 0, top_height: 210,
          };
          partitions.push({
            id: `${bid}_part_f${f}_${idx}`, start: [px, by0], end: [px, by1], floor: f, thickness: 20, openings: [pDoor],
          });
          splitWall(d, `${bid}_part_f${f}_${idx}`, [px, by0], [px, by1], [[doorY - DOOR_W / 2, doorY + DOOR_W / 2]], LEVEL_HEIGHT, 20, 'interior', f, bid);
          bsp([[bx0, by0], [px, by1]], depth + 1);
          bsp([[px, by0], [bx1, by1]], depth + 1);
        } else if (splitY !== null) {
          const py = splitY;
          let doorX = bx0 + rw * (depth === 0 && maxDepth > 1 ? 0.28 : 0.5);
          if (stairBoxes.some((sb) => py >= sb[0][1] && py <= sb[1][1] && doorX >= sb[0][0] - 0.5 && doorX <= sb[1][0] + 0.5)) {
            doorX = Math.abs(doorX - bx0) < Math.abs(bx1 - doorX) ? bx1 - 1.5 : bx0 + 1.5;
          }
          blockedXs.push(doorX);
          const idx = partIdx++;
          const pDoorId = `${bid}_partdoor_f${f}_${idx}`;
          const pDoor: WallOpening = {
            id: pDoorId, kind: 'door', pos: [doorX, py], floor: f,
            width: OPENING_DEFAULTS.door.width, sill_height: 0, top_height: 210,
          };
          partitions.push({
            id: `${bid}_part_f${f}_${idx}`, start: [bx0, py], end: [bx1, py], floor: f, thickness: 20, openings: [pDoor],
          });
          splitWall(d, `${bid}_part_f${f}_${idx}`, [bx0, py], [bx1, py], [[doorX - DOOR_W / 2, doorX + DOOR_W / 2]], LEVEL_HEIGHT, 20, 'interior', f, bid);
          bsp([[bx0, by0], [bx1, py]], depth + 1);
          bsp([[bx0, py], [bx1, by1]], depth + 1);
        } else {
          leafBoxes.push(box);
        }
      };

      bsp(r, 0);
      const allDoorsOnFloor = [
        ...openings.filter((op) => op.kind === 'door' && op.floor === f),
        ...partitions.filter((pt) => pt.floor === f).flatMap((pt) => pt.openings ?? []).filter((op) => op.kind === 'door'),
      ];
      leafBoxes.forEach((lb, rIdx) => {
        const [[rx0, ry0], [rx1, ry1]] = lb;
        const touchingDoors = allDoorsOnFloor
          .filter((op) => op.pos[0] >= rx0 - 0.25 && op.pos[0] <= rx1 + 0.25 && op.pos[1] >= ry0 - 0.25 && op.pos[1] <= ry1 + 0.25)
          .map((op) => op.id!)
          .filter(Boolean);
        rooms.push({
          id: `${bid}_r_f${f}_${rIdx}`,
          floor: f,
          bounds: { min: [rx0, ry0], max: [rx1, ry1] },
          kind: 'room',
          door_ids: touchingDoors,
        });
      });
    } else {
      const floorDoors = openings
        .filter((op) => op.kind === 'door' && op.floor === f)
        .map((op) => op.id!)
        .filter(Boolean);
      rooms.push({
        id: `${bid}_r_f${f}_0`,
        floor: f,
        bounds: { min: [x0, y0], max: [x1, y1] },
        kind: s.hollow ? 'hall' : 'room',
        ...(floorDoors.length ? { door_ids: floorDoors } : {}),
      });
    }
  }
}

/** Stage 9, a hall's stair core: its upper landings are walkable rooms (+1 = the mezzanine strip along the core's wall, higher = the core landing). */
function addHallLandings(p: BuildingPlan): void {
  const { bid, s, rooms } = p;
  const core = p.cores[0];
  if (!s.hollow || !core) return;
  for (let f = 1; f < s.floors; f++) {
    const bx = f === 1 ? core.mezzanine : core.box;
    rooms.push({ id: `${bid}_r_f${f}_${f === 1 ? 'mezz' : 'core'}`, floor: f, bounds: { min: bx[0], max: bx[1] }, kind: f === 1 ? 'balcony' : 'stairwell' });
  }
}

/** Stage 11, parapets on a walkable roof + their mantle edges. */
function addRoofParapets(p: BuildingPlan): void {
  const { bid, s, x0, y0, x1, y1, parapets, mantle_edges } = p;
  if (!s.roof_walkable) return;
  parapets.push(
    { wall_id: `${bid}_parapet_N`, start: [x0, y0], end: [x1, y0], height_cm: obstacleHeight('low_vault'), thickness_cm: 20 },
    { wall_id: `${bid}_parapet_S`, start: [x0, y1], end: [x1, y1], height_cm: obstacleHeight('low_vault'), thickness_cm: 20 },
    { wall_id: `${bid}_parapet_W`, start: [x0, y0], end: [x0, y1], height_cm: obstacleHeight('low_vault'), thickness_cm: 20 },
    { wall_id: `${bid}_parapet_E`, start: [x1, y0], end: [x1, y1], height_cm: obstacleHeight('low_vault'), thickness_cm: 20 },
  );
  mantle_edges.push(
    { id: `me_${bid}_para_N`, start: [x0, y0], end: [x1, y0], floor: s.floors, base_z: levelZ(s.floors), height_cm: obstacleHeight('low_vault'), normal: [0, -1], kind: 'parapet', building: bid },
    { id: `me_${bid}_para_S`, start: [x0, y1], end: [x1, y1], floor: s.floors, base_z: levelZ(s.floors), height_cm: obstacleHeight('low_vault'), normal: [0, 1], kind: 'parapet', building: bid },
    { id: `me_${bid}_para_W`, start: [x0, y0], end: [x0, y1], floor: s.floors, base_z: levelZ(s.floors), height_cm: obstacleHeight('low_vault'), normal: [-1, 0], kind: 'parapet', building: bid },
    { id: `me_${bid}_para_E`, start: [x1, y0], end: [x1, y1], floor: s.floors, base_z: levelZ(s.floors), height_cm: obstacleHeight('low_vault'), normal: [1, 0], kind: 'parapet', building: bid },
  );
}

/** Stage 12, the roof hatch + its access ladder. */
function addRoofHatch(p: BuildingPlan): void {
  const { d, bid, s, x1, y1, hatches } = p;
  if (!s.roof_walkable || !s.roofHatch) return;
  const hatchFloor = s.hollow ? 0 : s.floors - 1;
  const hx = x1 - 1.8, hy = y1 - 1.8;
  const ladderId = `LAD_${bid}_hatch`;
  const lad = ladderForLevels(s.floors - hatchFloor);
  d.ladders.push({
    id: ladderId,
    pos: [hx, hy + 0.1],
    yaw: 90,
    rungs: lad.rungs,
    plinth_cm: lad.plinth_cm,
    from: hatchFloor === 0 ? 'ground' : `floor:${bid}:${hatchFloor}`,
    to: `roof:${bid}`,
    level_from: hatchFloor,
    level_to: s.floors,
    foot: [hx, hy + 1.1],
    top: [hx, hy - 1.0],
  });
  hatches.push({ id: `${bid}_roof_hatch`, pos: [hx, hy], floor: hatchFloor, size: [100, 100], ladder_id: ladderId });
}

/**
 * Stage 13, floors detail (slabs with gaps). A stairwell gap is EXACTLY the footprint of the flight arriving on that slab (no
 * overshoot past its top: cause A), one per core; a hall's upper levels keep only its core's slab (the rest is an 'atrium' gap).
 */
function buildSlabs(p: BuildingPlan): void {
  const { bid, s, rooms, floors_detail } = p;
  const core0 = p.cores[0];
  if (s.hollow && !core0) return;
  for (let f = 1; f < s.floors; f++) {
    const gaps: FloorGap[] = [];
    p.cores.forEach((core, k) => {
      const st = core.stairs.find((x) => x.to === f && x.from === f - 1);
      if (st) gaps.push({ id: `${k ? `${bid}_c${k}` : bid}_gap_stair_f${f}`, poly: rect.poly(core.flightRect(st)), kind: 'stairwell' });
    });
    if (s.hollow && core0) (f === 1 ? core0.mezzanineVoid : core0.voidRects).forEach((vr, k) => gaps.push({ id: `${bid}_gap_void_f${f}_${k}`, poly: rect.poly(vr), kind: 'atrium' }));
    floors_detail.push({
      floor: f,
      height_z: levelZ(f),
      gaps,
      rooms: rooms.filter((rm) => rm.floor === f),
    });
  }
}

/** Stage 14, the DraftBuilding (pushed onto the draft). */
function emitBuilding(p: BuildingPlan, plan?: GenBuilding['plan']): DraftBuilding {
  const { s } = p;
  const b: DraftBuilding = {
    id: p.bid,
    footprint: p.r,
    height: s.height,
    floors: s.floors,
    ...(s.hollow ? { hollow: true } : {}),
    enterable: s.enterable,
    roof_walkable: s.roof_walkable,
    doors: p.doors,
    openings: p.openings,
    partitions: p.partitions,
    floors_detail: p.floors_detail,
    hatches: p.hatches,
    parapets: p.parapets,
    rooms: p.rooms,
    mantle_edges: p.mantle_edges,
    stairs: p.stairs,
    roof_holes: [] as Vec2[],
    floor_holes: [] as { pos: Vec2; floor: number }[],
    set_piece: s.set_piece,
    ...(s.archetype ? { archetype: s.archetype } : {}),
    ...(plan ? { plan } : {}),
  };
  p.d.buildings.push(b);
  return b;
}

/** Stage 10 of addBuilding, window clearances (metres; windowClearance.ts works in cm x `M`): slide or drop the windows that break a clearance. */
function fitWindows(r: Rect, openings: WallOpening[], mantleEdges: MantleEdge[], partitions: readonly GenPartition[], cores: readonly StairCore[],
  perSide: Record<Side, [number, number][]>): void {
  const M = 0.01; // 1 cm in metres
  const [[x0, y0], [x1, y1]] = r;
  const SIDE: Record<string, Side> = { N: 'north', S: 'south', W: 'west', E: 'east' };
  const flights = cores.flatMap((c) => c.stairs);
  const coreWalls = cores.flatMap((c) => c.laneWalls).map((lw) => ({ ...lw, thickness: STAIR_LANE_WALL_T * M }));
  const dropped = new Set<WallOpening>();
  for (const op of openings) {
    if (op.kind !== 'window' || !op.wall) continue;
    const side = SIDE[op.wall.slice(-1)];
    if (!side) continue;
    const horizontal = side === 'north' || side === 'south', ax = horizontal ? 0 : 1;
    const f = op.floor ?? 0, w = (op.width ?? OPENING_DEFAULTS.window.width) * M;
    const lo = horizontal ? x0 : y0, hi = horizontal ? x1 : y1;
    const parts = partitions.filter((pt) => pt.floor === f).map((pt) => ({ start: pt.start, end: pt.end, thickness: (pt.thickness ?? 20) * M }));
    const others = openings.filter((o) => o !== op && !dropped.has(o) && o.kind === 'window' && (o.floor ?? 0) === f && o.wall === op.wall);
    const posAt = (at: number): Vec2 => (horizontal ? [at, op.pos[1]] : [op.pos[0], at]);
    const clear = (at: number) => at >= lo + 1.5 - 1e-9 && at <= hi - 1.5 + 1e-9
      && !(f === 0 && perSide[side].some(([d0, d1]) => at >= d0 - 1.2 && at <= d1 + 1.2))
      && !windowOntoCore(r, posAt(at), w, f, flights, coreWalls, M)
      && windowPartitionClash(r, posAt(at), w, parts, M) < 0
      && others.every((o) => Math.abs(o.pos[ax] - at) >= (w + (o.width ?? OPENING_DEFAULTS.window.width) * M) / 2 + WINDOW_JAMB_CM * M - 1e-9);
    const at0 = op.pos[ax];
    if (clear(at0)) continue;
    const at = [0.25, -0.25, 0.5, -0.5, 0.75, -0.75, 1, -1].map((dt) => at0 + dt).find(clear);
    const me = mantleEdges.find((m) => m.id === `me_${op.id}`);
    if (at === undefined) {
      dropped.add(op);
      if (me) mantleEdges.splice(mantleEdges.indexOf(me), 1);
      continue;
    }
    op.pos = posAt(at);
    if (me) {
      const dt = at - at0;
      me.start = horizontal ? [me.start[0] + dt, me.start[1]] : [me.start[0], me.start[1] + dt];
      me.end = horizontal ? [me.end[0] + dt, me.end[1]] : [me.end[0], me.end[1] + dt];
    }
  }
  for (let i = openings.length - 1; i >= 0; i--) if (dropped.has(openings[i])) openings.splice(i, 1);
}

/** A ladder up a building's face onto its roof (10 cm off the face, the combat lab's towers). */
function addLadderAt(d: Draft, b: Draft['buildings'][number], side: Side, random: () => number): void {
  const [[x0, y0], [x1, y1]] = b.footprint;
  const along = side === 'north' || side === 'south' ? x0 + (x1 - x0) * (0.3 + 0.4 * random()) : y0 + (y1 - y0) * (0.3 + 0.4 * random());
  const gap = 0.1, foot = 1.1;
  // the climber stands outside the face; yaw (profile frame) points from the wall towards him
  const pos: Vec2 = side === 'north' ? [along, y0 - gap] : side === 'south' ? [along, y1 + gap] : side === 'west' ? [x0 - gap, along] : [x1 + gap, along];
  const footPt: Vec2 = side === 'north' ? [along, y0 - foot] : side === 'south' ? [along, y1 + foot] : side === 'west' ? [x0 - foot, along] : [x1 + foot, along];
  const yaw = side === 'north' ? 270 : side === 'south' ? 90 : side === 'west' ? 180 : 0;
  const top: Vec2 = side === 'north' ? [along, y0 + 1] : side === 'south' ? [along, y1 - 1] : side === 'west' ? [x0 + 1, along] : [x1 - 1, along];
  const lad = ladderForLevels(b.floors); // the roof sits on level `floors`
  d.ladders.push({ id: `LAD_${b.id}`, pos, yaw, rungs: lad.rungs, plinth_cm: lad.plinth_cm, from: 'ground', to: `roof:${b.id}`, level_from: 0, level_to: b.floors, foot: footPt, top });
}
