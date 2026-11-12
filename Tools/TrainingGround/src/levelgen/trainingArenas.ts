// Standalone combat-building training arenas (inbox 027 item 5):
//   - L_Gen_Train_1: two-storey partitioned house block, street, windows, stairwell, roof parapet
//   - L_Gen_Train_2: fortified checkpoint with narrow loopholes, sandbags, bunker, watch tower with hatch & ladder
//   - L_Gen_Train_3: warehouse yard with second-floor roof catwalk / gap landing, parapet mantle edges, courtyard obstacles

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import type { Vec2 } from '../movement/geometry.ts';
import { rng } from '../movement/geometry.ts';
import type { Draft, DraftBuilding } from './generate.ts';
import { addBuilding, addLadderAt, addWall, fenceRect, finalizeDraftToLayout } from './generate.ts';
import type { Profile } from './profile.ts';
import { PROFILE_SCHEMA } from './profile.ts';
import type { CoverKind, GenLayout, SetPiece } from './schema.ts';
import { DEFAULT_PARAMS, LEVEL_HEIGHT, rect, STAIR_RUN_PER_RISE } from './schema.ts';
import { validateGenLayout } from './schema.ts';

export type ArenaId = 1 | 2 | 3;

export interface TrainingArenaBundle {
  id: ArenaId;
  name: string;
  title: string;
  layout: GenLayout;
  profile: Profile;
}

const COVER_SIZE_CM: Record<CoverKind, [number, number, number]> = {
  crate: [160, 120, 120],
  jersey: [280, 65, 95],
  sandbag: [260, 75, 90],
  barrel: [90, 90, 110],
  wreck: [380, 180, 140],
  silo: [500, 500, 1100],
  boiler: [300, 180, 220],
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

function makeZone(id: string, kind: Draft['zones'][number]['kind'], setPiece: SetPiece, poly: Vec2[]): Draft['zones'][number] {
  return { id, kind, set_piece: setPiece, poly, density: 2.0, coverKinds: ['crate', 'jersey'] };
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

function addLowMantleWall(
  d: Draft,
  id: string,
  a: Vec2,
  b: Vec2,
  heightCm: number,
  normal: Vec2,
  kind: 'fence' | 'obstacle' = 'obstacle',
): void {
  addWall(d, id, a, b, heightCm, 25, kind === 'fence' ? 'fence' : 'wall');
  d.mantle_edges.push({
    id: `me_${id}`,
    start: a,
    end: b,
    floor: 0,
    base_z: 0,
    height_cm: heightCm,
    normal,
    kind,
  });
}

function doorApproachPoints(b: DraftBuilding, preferSide?: 'north' | 'south' | 'east' | 'west'): {
  door: DraftBuilding['doors'][number];
  outside: Vec2;
  inside: Vec2;
} {
  const [[x0, y0], [x1, y1]] = b.footprint;
  const door = (preferSide ? b.doors.find((x) => {
    if (preferSide === 'north') return Math.abs(x.pos[1] - y0) < 0.2;
    if (preferSide === 'south') return Math.abs(x.pos[1] - y1) < 0.2;
    if (preferSide === 'west') return Math.abs(x.pos[0] - x0) < 0.2;
    return Math.abs(x.pos[0] - x1) < 0.2;
  }) : undefined) ?? b.doors[0];

  const [dx, dy] = door.pos;
  if (Math.abs(dy - y0) < 0.2) return { door, outside: [dx, y0 - 1.8], inside: [dx, y0 + 1.8] };
  if (Math.abs(dy - y1) < 0.2) return { door, outside: [dx, y1 + 1.8], inside: [dx, y1 - 1.8] };
  if (Math.abs(dx - x0) < 0.2) return { door, outside: [x0 - 1.8, dy], inside: [x0 + 1.8, dy] };
  return { door, outside: [x1 + 1.8, dy], inside: [x1 - 1.8, dy] };
}

function makeArenaProfile(
  id: string,
  title: string,
  size: Vec2,
  start: Vec2,
  end: Vec2,
  targets: Record<string, [number, number]>,
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
    zones: [{ id: 'Z_arena', kind: 'street', poly: rect.poly([[0, 0], size]), cover_per_100m2: 2.0, sightline_m: [15, 50], mechanics: ['door', 'stair'] }],
    buildings: [],
    route: {
      main: { id: 'R_main', points: mainPts, length_m: Math.round(mainPts.length * 10) },
      variants: [],
    },
    sightlines: { lanes: [], stats_m: { p50: 25, p90: 45, max: 60, short_zone_share: 0.25 } },
    densities: { covers_per_100m2: { street: 2.0 }, enterable_share: 1.0, roof_access_share: 0.7, ladders_per_ha: 5 },
    set_pieces: {},
    targets,
  };
}

/** Arena 1: two-storey partitioned house block + street + south garage + stairwell + roof parapet. */
function buildArena1(): TrainingArenaBundle {
  const size: Vec2 = [60, 45];
  const random = rng(101);
  const start: Vec2 = [3, 22.5];
  const end: Vec2 = [57, 22.5];
  const d = makeEmptyDraft([start, end]);

  // North house: 2 storeys, partitioned ground & upper rooms, interior stairwell to roof + south/east doors & windows
  const house = addBuilding(d, 'B_house', [[16, 6], [42, 18]], {
    height: 2 * LEVEL_HEIGHT,
    floors: 2,
    enterable: true,
    roof_walkable: true,
    stairwell: true,
    partitionRooms: true,
    set_piece: 'facade',
    doorSides: ['south', 'east'],
    windowSides: ['south', 'west', 'north'],
  }, random);
  addLadderAt(d, house, 'east', random);
  const houseLad = d.ladders[d.ladders.length - 1];

  // South garage across the avenue: partitioned single-storey interior + yard fence
  const garage = addBuilding(d, 'B_garage', [[20, 29], [36, 39]], {
    height: LEVEL_HEIGHT,
    floors: 1,
    enterable: true,
    roof_walkable: false,
    partitionRooms: true,
    set_piece: 'one_entry_compound',
    doorSides: ['north'],
    windowSides: ['north', 'east'],
  }, random);
  const hEntry = doorApproachPoints(house, 'south');
  // Front garden fence along y = 20.5 with front gate aligned directly with B_house south door
  fenceRect(d, 'F_house_yard', [[13, 2], [45, 20.5]], [
    { side: 'south', at: hEntry.door.pos[0], width: 5.5 },
    { side: 'east', at: 12, width: 4.5 },
  ]);

  // Street tactical covers along road curbs + low mantleable garden wall
  addCover(d, 'C_st_j1', 'jersey', [11, 22.0], 0, true);
  addCover(d, 'C_st_w1', 'wreck', [22, 26.0], 8, false);
  addCover(d, 'C_st_c1', 'crate', [34, 21.8], 0, true);
  addCover(d, 'C_st_j2', 'jersey', [46, 25.5], 0, true);
  addCover(d, 'C_st_b1', 'barrel', [49, 21.2], 0, false);
  addCover(d, 'C_gar_c', 'crate', [40, 33], 90, true);
  addLowMantleWall(d, 'W_garden_low', [8, 12], [8, 18], 95, [1, 0], 'fence');

  d.zones.push(
    makeZone('Z_street', 'corridor', 'street', rect.poly([[0, 20.5], [60, 27]])),
    makeZone('Z_north_yard', 'open', 'facade', rect.poly([[13, 2], [45, 20.5]])),
    makeZone('Z_south_yard', 'open', 'one_entry_compound', rect.poly([[16, 27], [42, 42]])),
  );

  // Wire routes dynamically using actual generated doorway/stair/ladder links
  const st0 = house.stairs.find((s) => s.from === 0 && s.to === 1)!;
  const st1 = house.stairs.find((s) => s.from === 1 && s.to === 2)!;
  const houseCentre = rect.centre(house.footprint);

  const mainPts: Vec2[] = [
    start,
    [13, 23.5],
    [hEntry.outside[0], 23.5],
    [hEntry.door.pos[0], 19.2],
    hEntry.inside,
    st0.pos,
    st0.top,
    [st1.pos[0], st1.pos[1]],
    st1.top,
    houseCentre,
    houseLad.top,
    houseLad.foot,
    [houseLad.foot[0], 23.5],
    end,
  ];
  const mainLayers: string[] = [
    'ground', 'ground', 'ground', 'ground', 'ground', 'ground',
    'floor:B_house:1', 'floor:B_house:1',
    'roof:B_house', 'roof:B_house', 'roof:B_house',
    'ground', 'ground', 'ground',
  ];
  const mainLinks = [
    { at: 6, via: st0.id },
    { at: 8, via: st1.id },
    { at: 11, via: houseLad.id },
  ];
  d.routes = [{ id: 'R_main', pace: 'walk', points: mainPts, layers: mainLayers, links: mainLinks, mandatory: true }];

  // Variants: south garage clear + direct street flank
  const gEntry = doorApproachPoints(garage, 'north');
  d.routes.push(
    {
      id: 'V1_south_garage',
      pace: 'walk',
      points: [start, [14, 26], [gEntry.outside[0], 27], gEntry.outside, gEntry.inside, [gEntry.door.pos[0] + 2, gEntry.inside[1]], gEntry.outside, [44, 26], end],
      layers: Array(9).fill('ground'),
      links: [],
      set_pieces: ['street', 'one_entry_compound'],
    },
    {
      id: 'V2_street_push',
      pace: 'walk',
      points: [start, [16, 22.5], [29, 22.0], [42, 22.5], end],
      layers: ['ground', 'ground', 'ground', 'ground', 'ground'],
      links: [],
      set_pieces: ['street'],
    },
  );

  d.spawn_points.push({ id: 'SP_west', pos: start, kind: 'edge' });
  d.ambush_points.push(
    { id: 'AMB_house_e', pos: [44, 16] },
    { id: 'AMB_garage_e', pos: [38, 30] },
    { id: 'AMB_street_n', pos: [11, 21.5] },
  );
  d.patrol_routes.push({ id: 'PAT_street', points: [[15, 22], [44, 22], [44, 26], [15, 26]] });

  const profile = makeArenaProfile('train_01_house_block', 'Train 1: Partitioned Two-Storey House Block', size, start, end, {
    route_length_m: [60, 140],
    enterable_buildings: [2, 3],
    roofs_walkable: [1, 2],
    ladders: [1, 2],
    stairwells: [1, 2],
    variants: [2, 3],
  }, mainPts);

  const layout = finalizeDraftToLayout(d, size, {
    name: 'L_Gen_Train_1',
    profile: profile.id,
    seed: 101,
    params: { ...DEFAULT_PARAMS, size_m: size, density: 1, variants: 2, verticality: 3 },
    start,
    end,
  });
  return { id: 1, name: layout.name, title: profile.title, layout, profile };
}

/** Arena 2: fortified checkpoint with narrow loopholes, sandbags, bunker, and watchtower with hatch + ladder. */
function buildArena2(): TrainingArenaBundle {
  const size: Vec2 = [65, 48];
  const random = rng(202);
  const start: Vec2 = [3, 24];
  const end: Vec2 = [62, 24];
  const d = makeEmptyDraft([start, end]);

  // Main fortified checkpoint building (2 storeys + loopholes onto south street + east door)
  const cp = addBuilding(d, 'B_checkpoint', [[22, 6], [42, 18]], {
    height: 2 * LEVEL_HEIGHT,
    floors: 2,
    enterable: true,
    roof_walkable: true,
    stairwell: true,
    partitionRooms: true,
    set_piece: 'checkpoint',
    doorSides: ['east', 'north'],
    loopholeSides: ['south', 'west'],
    windowSides: ['east'],
  }, random);
  addLadderAt(d, cp, 'west', random);
  const cpLad = d.ladders[d.ladders.length - 1];

  // Southeast pillbox bunker with narrow loopholes onto central road
  const bunker = addBuilding(d, 'B_bunker', [[44, 29], [57, 39]], {
    height: LEVEL_HEIGHT,
    floors: 1,
    enterable: true,
    roof_walkable: false,
    partitionRooms: true,
    set_piece: 'bunker',
    doorSides: ['south'],
    loopholeSides: ['north', 'west'],
  }, random);

  // Southwest climbable watch tower: 2 storeys hollow + interior roof hatch + ladder
  const tower = addBuilding(d, 'B_tower', [[10, 30], [18, 38]], {
    height: 2 * LEVEL_HEIGHT,
    floors: 2,
    hollow: true,
    enterable: true,
    roof_walkable: true,
    roofHatch: true,
    set_piece: 'tower',
    doorSides: ['east'],
    loopholeSides: ['north', 'east'],
  }, random);
  const towerHatchLad = d.ladders.find((ld) => ld.to === `roof:${tower.id}`)!;

  // Sandbag chicane & mantleable low concrete barricades flanking road y = 21..27
  addCover(d, 'C_sb_w1', 'sandbag', [12, 21.2], 0, true);
  addCover(d, 'C_sb_w2', 'sandbag', [16, 26.8], 0, true);
  addCover(d, 'C_sb_c1', 'sandbag', [29, 21.5], 0, true);
  addCover(d, 'C_sb_c2', 'sandbag', [33, 26.8], 0, true);
  addCover(d, 'C_j_e1', 'jersey', [48, 21.0], 0, true);
  addCover(d, 'C_cr_e2', 'crate', [54, 26.5], 0, true);
  addLowMantleWall(d, 'W_sb_barricade_n', [24, 19.8], [28, 19.8], 95, [0, 1], 'obstacle');
  addLowMantleWall(d, 'W_sb_barricade_s', [37, 28.0], [42, 28.0], 95, [0, -1], 'obstacle');

  d.zones.push(
    makeZone('Z_road', 'corridor', 'street', rect.poly([[0, 20], [65, 28]])),
    makeZone('Z_checkpoint', 'open', 'checkpoint', rect.poly([[18, 4], [46, 20]])),
    makeZone('Z_tower_post', 'open', 'bunker', rect.poly([[8, 28], [60, 44]])),
  );

  // Route: West approach -> climb inside watchtower through hatch ladder -> down -> cross road -> enter B_checkpoint -> stair -> roof -> ladder -> east end
  const tEntry = doorApproachPoints(tower, 'east');
  const cpEntry = doorApproachPoints(cp, 'east');
  const st0 = cp.stairs.find((s) => s.from === 0 && s.to === 1)!;
  const st1 = cp.stairs.find((s) => s.from === 1 && s.to === 2)!;

  const mainPts: Vec2[] = [
    start,
    [15, 24],
    [tEntry.outside[0], 27],
    tEntry.outside,
    tEntry.inside,
    towerHatchLad.foot,
    towerHatchLad.top,
    [13, 33],
    towerHatchLad.top,
    towerHatchLad.foot,
    tEntry.inside,
    tEntry.outside,
    [22, 24],
    [45, 23.5],
    [cpEntry.outside[0], 18],
    cpEntry.outside,
    cpEntry.inside,
    st0.pos,
    st0.top,
    st1.pos,
    st1.top,
    rect.centre(cp.footprint),
    cpLad.top,
    cpLad.foot,
    [19, 23.5],
    [46, 24],
    end,
  ];
  const mainLayers: string[] = [
    'ground', 'ground', 'ground', 'ground', 'ground', 'ground',
    `roof:${tower.id}`, `roof:${tower.id}`, `roof:${tower.id}`,
    'ground', 'ground', 'ground', 'ground', 'ground', 'ground', 'ground', 'ground', 'ground',
    `floor:${cp.id}:1`, `floor:${cp.id}:1`,
    `roof:${cp.id}`, `roof:${cp.id}`, `roof:${cp.id}`,
    'ground', 'ground', 'ground', 'ground',
  ];
  const mainLinks = [
    { at: 6, via: towerHatchLad.id },
    { at: 9, via: towerHatchLad.id },
    { at: 18, via: st0.id },
    { at: 20, via: st1.id },
    { at: 23, via: cpLad.id },
  ];
  d.routes = [{ id: 'R_main', pace: 'walk', points: mainPts, layers: mainLayers, links: mainLinks, mandatory: true }];

  // Variant 1: south bunker breach; Variant 2: direct checkpoint road dash
  const bEntry = doorApproachPoints(bunker, 'south');
  d.routes.push(
    {
      id: 'V1_bunker_breach',
      pace: 'walk',
      points: [start, [20, 24], [41, 25], [41, 42], [bEntry.outside[0], 42], bEntry.outside, bEntry.inside, bEntry.outside, [60, 40], end],
      layers: Array(10).fill('ground'),
      links: [],
      set_pieces: ['bunker', 'street'],
    },
    {
      id: 'V2_road_push',
      pace: 'walk',
      points: [start, [21, 24], [38, 24], [52, 24], end],
      layers: ['ground', 'ground', 'ground', 'ground', 'ground'],
      links: [],
      set_pieces: ['checkpoint', 'street'],
    },
  );

  d.spawn_points.push({ id: 'SP_west', pos: start, kind: 'edge' });
  d.ambush_points.push(
    { id: 'AMB_cp_w', pos: [20, 14] },
    { id: 'AMB_bunker_nw', pos: [41, 29] },
    { id: 'AMB_tower_n', pos: [14, 27] },
  );
  d.patrol_routes.push({ id: 'PAT_checkpoint', points: [[20, 23], [46, 23], [46, 26], [20, 26]] });

  const profile = makeArenaProfile('train_02_checkpoint', 'Train 2: Fortified Checkpoint & Watchtower', size, start, end, {
    route_length_m: [110, 220],
    enterable_buildings: [3, 4],
    roofs_walkable: [1, 2],
    ladders: [2, 3],
    stairwells: [1, 2],
    variants: [2, 3],
  }, mainPts);

  const layout = finalizeDraftToLayout(d, size, {
    name: 'L_Gen_Train_2',
    profile: profile.id,
    seed: 202,
    params: { ...DEFAULT_PARAMS, size_m: size, density: 1, variants: 2, verticality: 3 },
    start,
    end,
  });
  return { id: 2, name: layout.name, title: profile.title, layout, profile };
}

/** Arena 3: warehouse yard with 2nd-floor roof catwalk/gap landing, parapet mantle edges, courtyard obstacles. */
function buildArena3(): TrainingArenaBundle {
  const size: Vec2 = [75, 55];
  const random = rng(303);
  const start: Vec2 = [3, 31];
  const end: Vec2 = [72, 31];
  const d = makeEmptyDraft([start, end]);

  // Main partitioned two-storey warehouse with stairwell + roof hatch + hatch ladder + external west ladder
  const wh = addBuilding(d, 'B_warehouse', [[14, 5], [44, 21]], {
    height: 2 * LEVEL_HEIGHT,
    floors: 2,
    enterable: true,
    roof_walkable: true,
    stairwell: true,
    partitionRooms: true,
    roofHatch: true,
    set_piece: 'warehouse',
    doorSides: ['south', 'west'],
    windowSides: ['south', 'east', 'north'],
  }, random);
  addLadderAt(d, wh, 'west', random);
  const whExtLad = d.ladders.find((ld) => ld.id === `LAD_${wh.id}`)!;
  const whHatchLad = d.ladders.find((ld) => ld.id === `LAD_${wh.id}_hatch`)!;

  // East two-level catwalk/landing annex: external stair to roof, roof hole -> 2nd floor landing gap -> floor hole -> ground
  const annex = addBuilding(d, 'B_catwalk', [[49, 7], [65, 22]], {
    height: 2 * LEVEL_HEIGHT,
    floors: 2,
    enterable: true,
    roof_walkable: true,
    stairwell: false,
    partitionRooms: true,
    set_piece: 'ruined_two_level',
    doorSides: ['south', 'west'],
    windowSides: ['south', 'west'],
  }, random);
  const extRun = annex.floors * STAIR_RUN_PER_RISE * (LEVEL_HEIGHT / 100);
  const [[ax0, ay0], [ax1, ay1]] = annex.footprint;
  const sx1 = ax1 - 1.5;
  const extStairId = 'STAIR_catwalk_ext';
  annex.stairs.push({ id: extStairId, pos: [sx1 - extRun, ay1 + 1.3], mid: [sx1, ay1 + 1.3], from: 0, to: annex.floors, kind: 'external', top: [sx1, ay1 - 1.5] });
  const roofHolePos: Vec2 = [ax0 + 3.2, ay0 + 4.5];
  const floorHolePos: Vec2 = [ax0 + 3.2, ay1 - 4.0];
  annex.roof_holes.push(roofHolePos);
  annex.floor_holes.push({ pos: floorHolePos, floor: 1 });
  // Append second-floor open catwalk/landing voids to floors_detail for B_catwalk
  const f1Detail = annex.floors_detail.find((fd) => fd.floor === 1);
  const holeGapPoly = rect.poly([[floorHolePos[0] - 1.2, floorHolePos[1] - 1.2], [floorHolePos[0] + 1.2, floorHolePos[1] + 1.2]]);
  const catwalkVoidPoly = rect.poly([[ax1 - 5.5, ay0 + 2.5], [ax1 - 2.0, ay0 + 6.0]]);
  if (f1Detail) {
    (f1Detail.gaps ??= []).push(
      { id: 'B_catwalk_gap_floor0', poly: holeGapPoly, kind: 'landing' },
      { id: 'B_catwalk_gap_catwalk', poly: catwalkVoidPoly, kind: 'catwalk' },
    );
  } else {
    annex.floors_detail.push({
      floor: 1,
      height_z: LEVEL_HEIGHT,
      gaps: [
        { id: 'B_catwalk_gap_floor0', poly: holeGapPoly, kind: 'landing' },
        { id: 'B_catwalk_gap_catwalk', poly: catwalkVoidPoly, kind: 'catwalk' },
      ],
    });
  }

  // South yard shed
  const shed = addBuilding(d, 'B_yard_shed', [[24, 39], [42, 49]], {
    height: LEVEL_HEIGHT,
    floors: 1,
    enterable: true,
    roof_walkable: false,
    partitionRooms: true,
    set_piece: 'one_entry_compound',
    doorSides: ['north'],
    windowSides: ['north', 'east'],
  }, random);

  // Courtyard obstacles + loading dock mantleable walls
  addCover(d, 'C_yard_cr1', 'crate', [12, 29.0], 0, true);
  addCover(d, 'C_yard_w1', 'wreck', [24, 33.0], 10, false);
  addCover(d, 'C_yard_cr2', 'crate', [37, 29.0], 90, true);
  addCover(d, 'C_yard_j1', 'jersey', [48, 33.0], 0, true);
  addCover(d, 'C_yard_b1', 'barrel', [62, 29.5], 0, false);
  addLowMantleWall(d, 'W_dock_parapet_1', [16, 25.5], [22, 25.5], 100, [0, 1], 'obstacle');
  addLowMantleWall(d, 'W_dock_parapet_2', [34, 25.5], [40, 25.5], 100, [0, 1], 'obstacle');

  d.zones.push(
    makeZone('Z_freight_ave', 'corridor', 'street', rect.poly([[0, 27], [75, 36]])),
    makeZone('Z_wh_yard', 'open', 'warehouse', rect.poly([[11, 2], [68, 26]])),
    makeZone('Z_south_shed', 'open', 'one_entry_compound', rect.poly([[20, 37], [46, 52]])),
  );

  // Main multi-level warehouse + roof hatch + external ladder -> catwalk annex external stair -> roof hole drop -> floor 1 -> floor hole drop -> ground -> end
  const whEntry = doorApproachPoints(wh, 'south');
  const whSt0 = wh.stairs.find((s) => s.from === 0 && s.to === 1)!;
  const annexEntry = doorApproachPoints(annex, 'south');
  const roofRim: Vec2 = [roofHolePos[0] - 1.8, roofHolePos[1]];
  const f1Land: Vec2 = [roofHolePos[0], roofHolePos[1] + 1.8];
  const f1DropRim: Vec2 = [floorHolePos[0] - 1.8, floorHolePos[1]];
  const gLand: Vec2 = [floorHolePos[0], floorHolePos[1] + 1.8];
  const extSt = annex.stairs[0];

  const mainPts: Vec2[] = [
    start,
    [11, 31],
    [whEntry.outside[0], 29],
    whEntry.outside,
    whEntry.inside,
    whSt0.pos,
    whSt0.top,
    whHatchLad.foot,
    whHatchLad.top,
    whExtLad.top,
    whExtLad.foot,
    [11, 25],
    [29, 27],
    [45, 27],
    extSt.pos,
    extSt.top,
    roofRim,
    f1Land,
    f1DropRim,
    gLand,
    annexEntry.inside,
    annexEntry.outside,
    [55, 31],
    end,
  ];
  const mainLayers: string[] = [
    'ground', 'ground', 'ground', 'ground', 'ground', 'ground',
    `floor:${wh.id}:1`, `floor:${wh.id}:1`,
    `roof:${wh.id}`, `roof:${wh.id}`,
    'ground', 'ground', 'ground', 'ground', 'ground',
    `roof:${annex.id}`, `roof:${annex.id}`,
    `floor:${annex.id}:1`, `floor:${annex.id}:1`,
    'ground', 'ground', 'ground', 'ground', 'ground',
  ];
  const mainLinks = [
    { at: 6, via: whSt0.id },
    { at: 8, via: whHatchLad.id },
    { at: 10, via: whExtLad.id },
    { at: 15, via: extStairId },
    { at: 17, via: 'hole_roof_0' },
    { at: 19, via: 'hole_floor_0' },
  ];
  d.routes = [{ id: 'R_main', pace: 'walk', points: mainPts, layers: mainLayers, links: mainLinks, mandatory: true }];

  // Variant 1: courtyard direct ground push; Variant 2: south shed flank
  const sEntry = doorApproachPoints(shed, 'north');
  d.routes.push(
    {
      id: 'V1_courtyard_direct',
      pace: 'walk',
      points: [start, [18, 31], [35, 31], [54, 31], end],
      layers: ['ground', 'ground', 'ground', 'ground', 'ground'],
      links: [],
      set_pieces: ['street', 'warehouse'],
    },
    {
      id: 'V2_shed_flank',
      pace: 'walk',
      points: [start, [18, 36], [sEntry.outside[0], 37], sEntry.outside, sEntry.inside, sEntry.outside, [48, 36], end],
      layers: Array(8).fill('ground'),
      links: [],
      set_pieces: ['one_entry_compound', 'street'],
    },
  );

  d.spawn_points.push({ id: 'SP_west', pos: start, kind: 'edge' });
  d.ambush_points.push(
    { id: 'AMB_wh_se', pos: [45, 22] },
    { id: 'AMB_catwalk_w', pos: [47, 15] },
    { id: 'AMB_shed_ne', pos: [44, 39] },
  );
  d.patrol_routes.push({ id: 'PAT_yard', points: [[15, 30], [58, 30], [58, 34], [15, 34]] });

  const profile = makeArenaProfile('train_03_warehouse_yard', 'Train 3: Warehouse Yard & Roof Catwalk', size, start, end, {
    route_length_m: [140, 260],
    enterable_buildings: [3, 4],
    roofs_walkable: [2, 3],
    ladders: [2, 3],
    stairwells: [1, 2],
    external_stairs: [1, 2],
    variants: [2, 3],
  }, mainPts);

  const layout = finalizeDraftToLayout(d, size, {
    name: 'L_Gen_Train_3',
    profile: profile.id,
    seed: 303,
    params: { ...DEFAULT_PARAMS, size_m: size, density: 1, variants: 2, verticality: 3 },
    start,
    end,
  });
  return { id: 3, name: layout.name, title: profile.title, layout, profile };
}

export function buildTrainingArena(id: ArenaId): TrainingArenaBundle {
  if (id === 1) return buildArena1();
  if (id === 2) return buildArena2();
  return buildArena3();
}

export function buildAllTrainingArenas(): TrainingArenaBundle[] {
  return [buildArena1(), buildArena2(), buildArena3()];
}

export function writeTrainingArenas(targetDir?: string): string[] {
  const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
  const outDir = targetDir ?? path.join(root, 'Content/Data/LevelGen/accepted');
  fs.mkdirSync(outDir, { recursive: true });
  const written: string[] = [];
  for (const arena of buildAllTrainingArenas()) {
    const errors = validateGenLayout(arena.layout);
    if (errors.length) throw new Error(`${arena.name} invalid: ${errors.join('; ')}`);
    const outPath = path.join(outDir, `${arena.name}.json`);
    fs.writeFileSync(outPath, JSON.stringify(arena.layout, null, 1) + '\n', 'utf8');
    written.push(outPath);
  }
  return written;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const files = writeTrainingArenas();
  for (const f of files) console.log(`wrote ${f}`);
}
