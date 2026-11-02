// Training Ground / level generator: profile + seed + params -> an ORIGINAL greybox layout (schema codex.levelgen_layout/1).
// A tactical-pattern GRAMMAR (docs/design/levelgen/LEVEL_GENERATOR.md section 3): the map is a street skeleton (the west
// street, the T-junction, the north street along the cut, the east yard, the south street, the inner street) laid out to the
// profile's proportions, the blocks between the streets are filled with set-pieces (facade, one-entry compound, industrial
// hall, warehouse, junkyard field, tank farm, ruined two-level factory, open square, dead end, towers), the mandatory route
// follows the skeleton, the variants thread the set-pieces (compound gates, alleys, the cut, ladders, roofs, the ruin's holes).
// Deterministic: the same (profile, seed, params) give the same layout. Works in the profile frame (metres, x east, y south)
// and converts to UE cm at the end (profile.ts toUE). Never a copy of the reference: proportions and densities come from it,
// every position is jittered by the seed and every block is a greybox set-piece of ours.

import type { Vec2 } from '../movement/geometry.ts';
import { dist, rng } from '../movement/geometry.ts';
import type { ZoneKind } from '../movement/layout.ts';
import type { Profile } from './profile.ts';
import { toUE } from './profile.ts';
import type { CoverKind, Door, GenBuilding, GenCover, GenCut, GenLadder, GenLayout, GenParams, GenRoute, GenWall, PatrolRoute, SetPiece, SpawnPoint, Stair } from './schema.ts';
import { CHIMNEY_HEIGHT_CM, CUT_LEVEL, DEFAULT_PARAMS, HALL_ROOF_LEVEL, HOLE_HALF_CM, LEVELGEN_SCHEMA, LEVEL_HEIGHT, STAIR_RUN_PER_RISE, ladderForLevels, levelZ, rect } from './schema.ts';

type Rect = [Vec2, Vec2];

/** Everything the grammar emits, in metres; converted once at the end. */
interface Draft {
  walls: { id: string; a: Vec2; b: Vec2; height: number; thickness: number; floor?: number; kind?: GenWall['kind']; building?: string }[];
  buildings: (Omit<GenBuilding, 'footprint' | 'doors' | 'stairs' | 'roof_holes' | 'floor_holes'> & { footprint: Rect; doors: Door[]; stairs: Stair[]; roof_holes: Vec2[]; floor_holes: { pos: Vec2; floor: number }[] })[];
  cut?: GenCut;
  ladders: GenLadder[];
  covers: GenCover[];
  zones: { id: string; kind: ZoneKind; set_piece: SetPiece; poly: Vec2[]; density: number; coverKinds: CoverKind[]; intent?: 'kill_zone' }[];
  routes: GenRoute[];
  spawn_points: SpawnPoint[];
  ambush_points: { id: string; pos: Vec2 }[];
  patrol_routes: PatrolRoute[];
  /** Rectangles nothing may be placed in (streets, the route corridor). */
  keepFree: Rect[];
}

const WALL_H = 300;        // cm, a greybox wall (the lab's)
const FENCE_H = 250;
const FENCE_T = 10;
const BUILDING_T = 30;
const DOOR_W = 1.8;        // m
const GATE_W = 3.0;        // m
const STREET_W = 9;        // m, curb to curb (scale B of the reference)
const ALLEY_W = 5;         // m
const CUT_W = 6;           // m, the trench
const CUT_WALL_T = 1.5;    // m
const CUT_WALL_H = 220;    // cm, the retaining wall's parapet + fence above the ground (the ground-layer obstacle)

export interface GenerateOptions { profile: Profile; seed: number; params?: Partial<GenParams>; name?: string }

export function generateLayout(o: GenerateOptions): GenLayout {
  const params: GenParams = { ...DEFAULT_PARAMS, size_m: [o.profile.bounds.max[0], o.profile.bounds.max[1]], ...o.params };
  const [w, h] = params.size_m;
  const random = rng(o.seed * 104729 + 7);
  const jit = (amp: number) => (random() * 2 - 1) * amp;
  const pick = <T,>(xs: readonly T[]): T => xs[Math.floor(random() * xs.length)];
  const d: Draft = { walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], keepFree: [] };
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
  addBuilding(d, 'B_office_w', fit(region.offices), { height: 900, floors: 3, hollow: true, enterable: false, roof_walkable: false, set_piece: 'facade' }, random);
  // the pipe yard: a ladder overwatch platform (B2) + racks as covers
  {
    const r = fit(region.pipeYard);
    const plat: Rect = [[r[0][0] + 2, r[0][1] + 2], [Math.min(r[1][0], r[0][0] + 14), Math.min(r[1][1], r[0][1] + 10)]];
    const b = addBuilding(d, 'B_gallery', plat, { height: LEVEL_HEIGHT, floors: 1, enterable: false, roof_walkable: V >= 1, set_piece: 'ladder_overwatch' }, random);
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
    const shedB = addBuilding(d, 'B_shed', shed, { height: LEVEL_HEIGHT, floors: 1, enterable: true, roof_walkable: false, set_piece: 'one_entry_compound', doorSides: ['east'] }, random);
    shedDoor = shedB.doors[0]?.pos ?? [shed[1][0], (shed[0][1] + shed[1][1]) / 2];
    d.zones.push(zone('Z_compound', 'open', 'one_entry_compound', r, 3.5, ['crate', 'barrel', 'wreck']));
    d.ambush_points.push({ id: id('AMB_shed'), pos: [shed[1][0] + 1, shed[1][1] - 1] });
  }
  // the sawtooth hall (enterable, a walkable roof on level +3 = 9 m, one stairwell flight ground -> roof when V >= 2) + its boiler
  // yard (a small one-entry compound). The tall halls' roofs sit on HALL_ROOF_LEVEL (game designer 2026-11-02: the view from the roofs).
  {
    const r = fit(region.sawtooth);
    addBuilding(d, 'B_hall_n', r, { height: HALL_ROOF_LEVEL * LEVEL_HEIGHT, floors: HALL_ROOF_LEVEL, hollow: true, enterable: true, roof_walkable: V >= 1, stairwell: V >= 2, set_piece: 'industrial_hall', doorSides: ['south', 'south'] }, random);
    const y = fit(region.boilerYard);
    fenceRect(d, 'F_boilers', y, [{ side: 'south', at: (y[0][0] + y[1][0]) / 2, width: GATE_W }]);
    d.zones.push(zone('Z_boilers', 'open', 'one_entry_compound', y, 3.0, ['boiler', 'barrel']));
  }
  // the small office (low, a door on the south face) and the alley between it and the warehouse lot
  addBuilding(d, 'B_office_s', fit(region.office), { height: LEVEL_HEIGHT, floors: 1, enterable: true, roof_walkable: false, set_piece: 'facade', doorSides: ['south'] }, random);
  // the warehouse (enterable, roof, stairwell) + its fenced lot (one gate on the street)
  {
    const r = fit(region.warehouse);
    addBuilding(d, 'B_warehouse', r, { height: HALL_ROOF_LEVEL * LEVEL_HEIGHT, floors: HALL_ROOF_LEVEL, hollow: true, enterable: true, roof_walkable: V >= 1, stairwell: V >= 2, set_piece: 'warehouse', doorSides: ['west', 'south'] }, random);
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
    addBuilding(d, 'B_pump', pump, { height: LEVEL_HEIGHT, floors: 1, enterable: true, roof_walkable: false, set_piece: 'tank_farm', doorSides: ['west'] }, random);
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
    const b = addBuilding(d, 'B_ruin', r, { height: 2 * LEVEL_HEIGHT, floors: 2, enterable: true, roof_walkable: V >= 1, stairwell: false, set_piece: 'ruined_two_level', doorSides: ['west', 'south'] }, random);
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
    // interior: one partition per floor with a doorway
    for (let f = 0; f < b.floors; f++) {
      // the partition at 0.5-0.6 of the width: clear of the roof holes (0.3 / 0.75) and the floor hole (0.35) the route drops through
      const px = r[0][0] + (r[1][0] - r[0][0]) * (0.55 + jit(0.05));
      const gapY = r[0][1] + (r[1][1] - r[0][1]) * (0.3 + 0.4 * random());
      addWall(d, `${b.id}_part${f}_a`, [px, r[0][1]], [px, gapY - DOOR_W / 2], 300, BUILDING_T, 'interior', f, b.id);
      addWall(d, `${b.id}_part${f}_b`, [px, gapY + DOOR_W / 2], [px, r[1][1]], 300, BUILDING_T, 'interior', f, b.id);
    }
    const a = fit(region.ruinAnnex);
    addBuilding(d, 'B_ruin_annex', a, { height: LEVEL_HEIGHT, floors: 1, enterable: true, roof_walkable: false, set_piece: 'ruined_two_level', doorSides: ['west', 'south', 'north'] }, random);
    d.zones.push(zone('Z_ruin', 'open', 'ruined_two_level', [[a[0][0] - 2, r[0][1] - 2], [r[1][0] + 2, a[1][1] + 2]], 2.5, ['crate', 'barrel']));
  }
  // the Steel Works hall (enterable, roof on +3, stairwell), two factory chimneys (tall obstacles, never climbable), the annex racks
  {
    const r = fit(region.steel);
    addBuilding(d, 'B_steelworks', r, { height: HALL_ROOF_LEVEL * LEVEL_HEIGHT, floors: HALL_ROOF_LEVEL, hollow: true, enterable: true, roof_walkable: V >= 1, stairwell: V >= 2, set_piece: 'industrial_hall', doorSides: ['south', 'west'] }, random);
    // the two factory chimneys: one at the hall's NE corner (over the north street's end), one at its SW (over the south street).
    // Game designer 2026-11-02: chimneys are NOT ladder towers - a tall closed obstacle that blocks sight, no ladder, no walkable
    // top, no height level of its own (validateGenLayout refuses a climbable chimney).
    for (const [k, c] of ([[0, [r[1][0] + 2.5 + jit(0.5), r[0][1] - 2.5 + jit(0.5)]], [1, [r[0][0] - 6 + jit(1), r[1][1] - 2 + jit(1)]]] as [number, Vec2][])) {
      const t: Rect = [[c[0] - 1.5, c[1] - 1.5], [c[0] + 1.5, c[1] + 1.5]];
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
    const stair = wh.stairs.find((s) => s.kind === 'stairwell' && s.to === wh.floors);
    const pts: Vec2[] = [[xCross - STREET_W / 2 + HUG, boil[1][1] + 3], [(boil[0][0] + boil[1][0]) / 2, boil[1][1] + 1], [(boil[0][0] + boil[1][0]) / 2, (boil[0][1] + boil[1][1]) / 2],
      [(boil[0][0] + boil[1][0]) / 2, boil[1][1] + 3], [alleyX, off[0][1] - 1], [alleyX, lot[1][1] + 2], [lotGateX, hugN], [lotGateX, lot[1][1] - 1.5], [(lot[0][0] + lot[1][0]) / 2, lot[0][1] + 1.5]];
    const layers = pts.map(() => 'ground');
    const links: { at: number; via: string }[] = [];
    if (southDoor) { pts.push([southDoor.pos[0], wh.footprint[1][1] - 2]); layers.push('ground'); }
    if (southDoor && stair) { pts.push(stair.pos, stair.top, rect.centre(wh.footprint)); layers.push('ground', `roof:${wh.id}`, `roof:${wh.id}`); links.push({ at: pts.length - 2, via: stair.id }); }
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
  const size: Vec2 = [w, h];
  const U = (p: Vec2): Vec2 => toUE(p, size);
  const UR = (r: Rect): [Vec2, Vec2] => { const a = U(r[0]), b = U(r[1]); return [[Math.min(a[0], b[0]), Math.min(a[1], b[1])], [Math.max(a[0], b[0]), Math.max(a[1], b[1])]]; };
  const layout: GenLayout = {
    schema: LEVELGEN_SCHEMA, name: o.name ?? `gen_${o.profile.id}_${o.seed}`, profile: o.profile.id, seed: o.seed, params,
    comment: `Generated by Tools/TrainingGround/src/levelgen/generate.ts from ${o.profile.id} (seed ${o.seed}); an original greybox, not a copy of the reference.`,
    bounds: { min: [-(h / 2) * 100, -(w / 2) * 100], max: [(h / 2) * 100, (w / 2) * 100] }, ground_z: 0,
    spawn: { pos: U(start), yaw: 90 }, end: U(end),
    walls: d.walls.map((wl) => ({ id: wl.id, a: U(wl.a), b: U(wl.b), height: wl.height, thickness: wl.thickness, ...(wl.floor ? { floor: wl.floor } : {}), ...(wl.kind ? { kind: wl.kind } : {}), ...(wl.building ? { building: wl.building } : {}) })),
    buildings: d.buildings.map((b) => ({ ...b, footprint: UR(b.footprint), doors: b.doors.map((x) => ({ ...x, pos: U(x.pos) })), stairs: b.stairs.map((s) => ({ ...s, pos: U(s.pos), top: U(s.top), ...(s.mid ? { mid: U(s.mid) } : {}) })),
      roof_holes: b.roof_holes.map(U), floor_holes: b.floor_holes.map((x) => ({ pos: U(x.pos), floor: x.floor })) })),
    cut: d.cut ? { poly: d.cut.poly.map(U), level: d.cut.level, floor_z: d.cut.floor_z, crossings: d.cut.crossings.map((c) => ({ ...c, poly: c.poly.map(U) })) } : undefined,
    ladders: d.ladders.map((l) => ({ ...l, pos: U(l.pos), foot: U(l.foot), top: U(l.top), yaw: yawToUE(l.yaw) })),
    covers: d.covers.map((c) => ({ ...c, pos: U(c.pos), yaw: yawToUE(c.yaw) })),
    zones: d.zones.map((z) => ({ id: z.id, kind: z.kind, set_piece: z.set_piece, poly: z.poly.map(U), ...(z.intent ? { intent: z.intent } : {}) })),
    routes: d.routes.map((r) => ({ ...r, points: r.points.map(U) })),
    spawn_points: d.spawn_points.map((s) => ({ ...s, pos: U(s.pos) })),
    ambush_points: d.ambush_points.map((a) => ({ ...a, pos: U(a.pos) })),
    patrol_routes: d.patrol_routes.map((p) => ({ ...p, points: p.points.map(U) })),
    // The pressure template of a generated map (COMBAT_SIM.md section 5, 2026-11-02): ambush-heavy - 30 % patrols, half of the ambushes
    // coordinated plans, a group every 90 s, caps of 8 so a plan of 4-6 finds room beside a living patrol (the combat lab's own level
    // keeps the designer's 60 % / 35 % / 120 s / 4). The level JSON's block is the master once the map is built (the builder keeps it).
    pressure: { first_event_sec: 30, interval_sec: 90, group_min: 2, group_max: 4, max_alive: 8, patrol_share: 0.3, seed: o.seed,
      ambush: { points: d.ambush_points.map((a) => U(a.pos)), coordinated: { share: 0.5, max_alive: 8 } } },
  };
  return layout;
}

// ---- helpers (metres) -----------------------------------------------------------------------------------------------------

const COVER_SIZE: Record<CoverKind, [number, number, number]> = {
  jersey: [300, 60, 90], sandbag: [250, 80, 100], crate: [120, 120, 110], wreck: [420, 180, 150], barrel: [60, 60, 90], silo: [800, 800, 1200], boiler: [200, 120, 160],
};

/** Profile yaw (degrees from +x east towards +y south) to UE yaw (from +X north towards +Y east): UE = 90 - profile. */
const yawToUE = (deg: number) => ((90 - deg) % 360 + 360) % 360;

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

interface BuildingSpec { height: number; floors: number; hollow?: boolean; enterable: boolean; roof_walkable: boolean; stairwell?: boolean; set_piece: SetPiece; doorSides?: Side[] }

/** A rectangular building: perimeter walls on the ground floor with its doors cut out, a stairwell when asked (floors + roof). */
function addBuilding(d: Draft, bid: string, r: Rect, s: BuildingSpec, random: () => number): Draft['buildings'][number] {
  const [[x0, y0], [x1, y1]] = r;
  const doors: Door[] = [];
  const perSide: Record<Side, [number, number][]> = { north: [], south: [], east: [], west: [] };
  for (const side of s.enterable ? s.doorSides ?? ['south'] : []) {
    const horizontal = side === 'north' || side === 'south';
    const lo = horizontal ? x0 : y0, hi = horizontal ? x1 : y1;
    const at = lo + (hi - lo) * (0.25 + 0.5 * random());
    perSide[side].push([at - DOOR_W / 2, at + DOOR_W / 2]);
    doors.push({ pos: horizontal ? [at, side === 'north' ? y0 : y1] : [side === 'west' ? x0 : x1, at], floor: 0 });
  }
  splitWall(d, `${bid}_N`, [x0, y0], [x1, y0], perSide.north, s.height, BUILDING_T, 'building', 0, bid);
  splitWall(d, `${bid}_S`, [x0, y1], [x1, y1], perSide.south, s.height, BUILDING_T, 'building', 0, bid);
  splitWall(d, `${bid}_W`, [x0, y0], [x0, y1], perSide.west, s.height, BUILDING_T, 'building', 0, bid);
  splitWall(d, `${bid}_E`, [x1, y0], [x1, y1], perSide.east, s.height, BUILDING_T, 'building', 0, bid);
  const stairs: Stair[] = [];
  if (s.stairwell && s.enterable) {
    // one stairwell in a corner, climbing every floor up to the roof when walkable
    const top = s.roof_walkable ? s.floors : s.floors - 1;
    // a 35 deg ramp along the longer inner side (run = 1.43 x rise); a hollow building (a hall, a tower) has no intermediate slabs:
    // one flight ground -> roof
    const flight = (f: number, t: number, n: number): Stair => {
      const run = (t - f) * STAIR_RUN_PER_RISE * (LEVEL_HEIGHT / 100);
      const alongY = y1 - y0 >= x1 - x0;
      // flights side by side (2 m apart) so the upper ramp never passes over the opening the lower one comes up through
      const pos: Vec2 = alongY ? [x0 + 1.5 + n * 2, y0 + 1.2] : [x0 + 1.2, y0 + 1.5 + n * 2];
      const topPt: Vec2 = alongY ? [x0 + 1.5 + n * 2, Math.min(y1 - 1.2, y0 + 1.2 + run)] : [Math.min(x1 - 1.2, x0 + 1.2 + run), y0 + 1.5 + n * 2];
      return { id: `${bid}_stair_${n}`, pos, from: f, to: t, kind: 'stairwell', top: topPt };
    };
    if (s.hollow) { if (top > 0) stairs.push(flight(0, top, 0)); }
    else for (let f = 0; f < top; f++) stairs.push(flight(f, f + 1, f));
  }
  const b = { id: bid, footprint: r, height: s.height, floors: s.floors, ...(s.hollow ? { hollow: true } : {}), enterable: s.enterable, roof_walkable: s.roof_walkable, doors, stairs, roof_holes: [] as Vec2[], floor_holes: [] as { pos: Vec2; floor: number }[], set_piece: s.set_piece };
  d.buildings.push(b);
  return b;
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
