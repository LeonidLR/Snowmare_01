// Training Ground / level generator: the generated layout format (schema codex.levelgen_layout/1) - an EXTENSION of the lab's
// codex.formation_layout/1 (docs/design/levelgen/LEVEL_GENERATOR.md section 2). Everything the lab format has keeps its meaning
// (bounds, ground_z, spawn, walls, zones, routes, props); the extension adds floors on walls, buildings with floors / stairs /
// roof holes, the dividing cut with its crossings, ladders, covers, the mandatory route + variants, the end, enemy spawn /
// ambush points and patrol routes. `toFormationLayout(layout, floor)` flattens one floor LAYER into a plain lab layout, so the
// simulator, the audit, the bot and Scripts/Editor/build_formation_lab.py all read a generated layout unchanged.
// Units: UE cm, X forward (north = +X, drawn up), Y right (east = +Y). A reference profile's metres (x east, y south) map to
// UE as X = -y * 100, Y = x * 100 (profile.ts).

import v2cfg from '../../../../Content/Data/LevelGen/levelgen_v2.json' with { type: 'json' };
import { obstacleHeight } from './obstacleHeights.ts';
import { footprintBBox, footprintPoly } from './rotation.ts';
import type { LayoutAnalysis } from './chokepoints.ts';
import type { PmcPlan } from './pmcPosts.ts';
import type { Vec2 } from '../movement/geometry.ts';
import { pointInPoly, segmentsIntersect } from '../movement/geometry.ts';
import type { Layout, LayoutRoute, LayoutWall, LayoutZone, ZoneKind } from '../movement/layout.ts';
import type { OpeningRole } from './buildings/planDoors.ts';
import { LAYOUT_SCHEMA, validateLayout, ZONE_KINDS } from '../movement/layout.ts';

/** levelgen_v2.json objectives: the goal-count limits validateObjectives checks (manual_count_min..count_max). */
const OBJ_CFG = v2cfg.objectives;

export const LEVELGEN_SCHEMA = 'codex.levelgen_layout/1';
/**
 * Layout schema /2 (ADR-0002, BUILDINGS_TECH_SPEC.md section 2): the building generator's fields (opening states, balconies,
 * fire escapes, stories, routes). The defaults change meaning - a missing door / window `state` is open / broken in /1 and the
 * opening-states default table in /2 (openingStates.ts) - so readers branch on the schema string. /1 files load unchanged.
 */
export const LEVELGEN_SCHEMA_V2 = 'codex.levelgen_layout/2';
/** Every layout schema string the readers accept. */
export const LEVELGEN_SCHEMAS = [LEVELGEN_SCHEMA, LEVELGEN_SCHEMA_V2] as const;
export type LevelgenSchema = (typeof LEVELGEN_SCHEMAS)[number];
/** True for 'codex.levelgen_layout/1' and '/2'. */
export function isLevelgenSchema(s: unknown): s is LevelgenSchema {
  return (LEVELGEN_SCHEMAS as readonly unknown[]).includes(s);
}
/**
 * HEIGHT LEVELS (game designer decision 2026-11-01): every map is split into discrete levels 3 m apart - level -1 = -3 m (the rail
 * cut / canal, pits, trenches, basements), 0 = the ground, +1 = +3 m (first floors, low roofs, platforms, raised rims), +2 = +6 m
 * (second floors, hall roofs, tower tops), upward in 3 m steps when needed. A layer (a walkable surface: 'ground', 'cut',
 * 'floor:<b>:<n>', 'roof:<b>') lives on exactly one level; a building's floor n sits on level n and its roof on level `floors`;
 * links (ladders, stairs, holes, ramps) join surfaces on different levels. The UE builder puts the slabs / cut floor at
 * level * LEVEL_HEIGHT and sizes the ladders to the level difference.
 */
export const LEVEL_HEIGHT = 300;
export const levelZ = (level: number): number => level * LEVEL_HEIGHT;
/** The ladder clip contract (docs/design/LADDERS.md): rung pitch and the shortest climbable ladder. */
export const RUNG_PITCH = 36.4;
export const MIN_RUNGS = 7;
/** The cut's floor = level -1. */
export const CUT_LEVEL = -1;
export const CUT_DEPTH = LEVEL_HEIGHT;
/** The cut wall's thickness when the layout has no `cut_wall` wall to read it from (the generator uses 1.5 m), cm. */
export const CUT_WALL_DEFAULT_THICKNESS_CM = 150;
/**
 * Hard rule 2026-11-03 (game designer): no building footprint touches the cut's walls. The forbidden band is the cut polygon's
 * bounding box grown by the wall thickness (the walls stand OUTSIDE the trench edge) plus this margin, cm.
 */
export const CUT_BUILDING_MARGIN_CM = 100;
/**
 * The level of the tall halls' walkable roofs (the sawtooth hall, the warehouse, the Steel Works): +3 = 9 m (game designer
 * 2026-11-02, "for the view from the roofs"; the reference draws them 8-12 m tall). The ruin keeps its two storeys (+2).
 */
export const HALL_ROOF_LEVEL = 3;
/** Half the side of a square roof / floor hole, cm (the builder cuts the slab 2 x this wide). */
export const HOLE_HALF_CM = 120;
/** A factory chimney's height, cm: a tall obstacle that blocks sight, never a height level (designer 2026-11-02: not a ladder tower). */
export const CHIMNEY_HEIGHT_CM = 2000;
/** Route intents a designer can mark: 'kill_zone' = a deliberately exposed passage (the cut floor), intended, never a defect. */
export const ROUTE_INTENTS = ['kill_zone'] as const;
export type RouteIntent = (typeof ROUTE_INTENTS)[number];

/**
 * A ladder over `levels` height levels: rungs = the whole rungs that fit (8 for 3 m, 16 for 6 m), the rest (8.8 / 17.6 cm) is a
 * plinth under its foot, so the top lands exactly on the upper level (LADDERS.md 3.1: TopZ = rungs * pitch +- 8 cm).
 */
export function ladderForLevels(levels: number): { rungs: number; plinth_cm: number } {
  const h = levels * LEVEL_HEIGHT;
  const rungs = Math.floor(h / RUNG_PITCH);
  return { rungs, plinth_cm: Math.round((h - rungs * RUNG_PITCH) * 10) / 10 };
}

export const WALL_KINDS = ['wall', 'fence', 'curb', 'cut_wall', 'building', 'parapet', 'interior', 'partition'] as const;
export type WallKind = (typeof WALL_KINDS)[number];

export const OPENING_KINDS = ['door', 'window', 'loophole'] as const;
export type OpeningKind = (typeof OPENING_KINDS)[number];

/**
 * Opening dimensions (cm):
 * - door: full opening from ground/floor, walk-through
 * - window: standard window with sill ~90 cm, view and fire port
 * - loophole: narrow fortified firing slit (width 20-40 cm, sill 110-140 cm, height 140-160 cm)
 */
export const OPENING_DEFAULTS: Record<OpeningKind, { width: number; sill: number; top: number }> = {
  door: { width: 120, sill: 0, top: 210 },
  window: { width: 120, sill: obstacleHeight('low_vault'), top: 210 },
  loophole: { width: 30, sill: 120, top: 150 },
};

export interface WallOpening {
  id?: string;
  kind: OpeningKind;
  pos: Vec2;
  floor: number;
  /** Wall identifier the opening is cut into. */
  wall?: string;
  /** Opening width along wall face, cm (default per kind). */
  width?: number;
  /** Sill height above floor slab, cm (door: 0, window: 90, loophole: 120). */
  sill_height?: number;
  /** Top height above floor slab, cm (door: 210, window: 210, loophole: 150). */
  top_height?: number;
  /** Facing normal or sight yaw, degrees. */
  yaw?: number;
  /**
   * Schema /2: the opening's state (opening-states.md 1 / 2). Absent = the default of the layout's schema - read it through
   * openingStates.ts effectiveOpeningState, never directly.
   */
  state?: DoorState | WindowState;
  /** Schema /2, planned buildings (S2_ROOMS_PLAN.md 4.1): the two room ids the opening joins, or [roomId, 'street'] for an exterior opening. */
  links?: [string, string];
  /** Schema /2, planned buildings: what the opening is for (the default state table reads it: 'portal' = open). */
  role?: OpeningRole;
  /** Schema /2, planned buildings: the kit module name ('wall_door_200', 'portal_200', 'wall_window_200'). */
  module?: string;
  /** Schema /2: a window marked as the place of a balcony / fire-escape door (blocks, S2.3: one per flat and storey from 1; story 4 turns it into a door). */
  slot?: 'balcony' | 'escape';
  /** Schema /2 (structureClasses.ts, structure_classes.json): the host wall's structural class and material (future destruction; nothing reads it yet). */
  structure?: 'load_bearing' | 'partition' | 'annex' | 'facade';
  material?: 'concrete_panel' | 'brick' | 'plaster' | 'wood' | 'sheet_metal';
  /** Schema /2 windows (structureClasses.ts annotateWindowVault): can a body vault / climb through it (sill height class, width, a landing outside). */
  vaultable?: boolean;
  /** Schema /2 windows: the sill height and the opening width the vault rule read, cm. */
  sill_cm?: number;
  width_cm?: number;
  /** Schema /2 windows: what a body climbing out lands on. */
  vault_onto?: 'ground' | 'balcony' | 'roof' | 'none';
}
export type { OpeningRole };

/** Door / gate / shutter states (opening-states.md 1). */
export const DOOR_STATES = ['open', 'closed', 'locked', 'barricaded', 'sealed'] as const;
export type DoorState = (typeof DOOR_STATES)[number];
/** Window / loophole states (opening-states.md 2); loopholes are always 'fixed'. */
export const WINDOW_STATES = ['broken', 'glazed', 'barred', 'boarded', 'fixed'] as const;
export type WindowState = (typeof WINDOW_STATES)[number];
/** The facade side of a building (fire escapes). */
export type Side = 'north' | 'south' | 'east' | 'west';

/** Schema /2: a balcony on an upper storey (building local frame, cm); depth >= 150 at fire-escape stops. */
export interface Balcony { id: string; floor: number; rect: [Vec2, Vec2]; door_id: string; rail_cm: number }
/** Schema /2: an external fire escape (stair or ladder) with its stops (building local frame, cm). */
export interface FireEscape {
  id: string;
  kind: 'stair' | 'ladder';
  side: Side;
  /** Foot position on the facade. */
  at: Vec2;
  /** Approach cell of the ground stop. */
  ground: Vec2;
  stops: { floor: number; balcony_id: string }[];
  to_roof: boolean;
  down_only?: boolean;
  /** kind 'ladder': its entries in layout.ladders. */
  ladder_ids?: string[];
}
/** Schema /2: a snow drift up to an opening (story 10); slope <= 35 deg. */
export interface SnowRamp { id: string; from: Vec2; to_opening: string; width_cm: number }
/** Schema /2: a breachable wall panel; `wide_with` = the adjacent panel id of a wide breach. */
export interface BreachPanel {
  id: string;
  wall: string;
  floor: number;
  at: Vec2;
  width_cm: 200;
  wide_with?: string;
  hole: [number, number];
  links: [string, string];
  score: number;
}
/** Schema /2: a link between two buildings. */
export interface BuildingLink { id: string; kind: 'shared_wall_hole' | 'plank' | 'roof_bridge'; a: string; b: string; at: Vec2[] }
/** Schema /2: one applied building story (building-stories GDD). */
export interface StoryInstance { id: string; bricks: string[]; seed: number; author: 'survivors' | 'pmc' | 'cold' | 'decay'; outcome?: 'held' | 'fell' }
/** Schema /2: a stored route through a building; recomputed on load when its cost_hash differs from the layout's. */
export interface Route { legs: { from: string; to: string; mode: string }[]; cost: number; detour: number; cost_hash: string }
/** Schema /2: building role (building-stories GDD). */
export type BuildingRole = 'objective' | 'garrison' | 'overwatch' | 'transit' | 'background';
/** Schema /2: district theme. */
export type DistrictTheme = 'pmc' | 'survivors' | 'cold' | 'decay' | 'mixed';
export interface District { id: string; lots: string[]; theme: DistrictTheme }

export interface GenPartition {
  id: string;
  start: Vec2;
  end: Vec2;
  floor: number;
  /** Wall height, cm. Default LEVEL_HEIGHT (300). */
  height?: number;
  /** Wall thickness, cm. Default 20 cm. */
  thickness?: number;
  /** Door or window openings along this partition wall. */
  openings?: WallOpening[];
  /** Schema /2 (structureClasses.ts): structural class and material of the wall. */
  structure?: 'load_bearing' | 'partition' | 'annex' | 'facade';
  material?: 'concrete_panel' | 'brick' | 'plaster' | 'wood' | 'sheet_metal';
}

export interface FloorGap {
  id?: string;
  poly: Vec2[];
  kind?: 'stairwell' | 'landing' | 'atrium' | 'balcony' | 'catwalk';
}

export interface BuildingFloor {
  floor: number;
  /** Elevation Z in cm = levelZ(floor). */
  height_z?: number;
  /** Openings / voids in this floor slab (open landings, catwalks, stairwells). */
  gaps?: FloorGap[];
  /** Sub-rooms on this floor. */
  rooms?: GenRoom[];
}

export interface RoofHatch {
  id: string;
  pos: Vec2;
  /** Floor level the hatch is accessed from (typically floors - 1) or roof level. */
  floor: number;
  /** Dimensions of hatch opening, cm (e.g. [100, 100]). */
  size?: [number, number];
  /** Ladder id linked to this hatch. */
  ladder_id?: string;
}

export interface RoofParapet {
  wall_id?: string;
  start: Vec2;
  end: Vec2;
  /** Height above roof slab, cm (e.g. 100 cm chest-high cover). */
  height_cm: number;
  /** Thickness, cm (default 20 cm). */
  thickness_cm?: number;
}

/**
 * A FIRING GAP in a roof parapet (FIRING_GAPS_HANDOVER.md, firingGaps.ts planFiringGaps): a span cut out of the parapet and refilled with a
 * low sill a kneeling muzzle clears. World coordinates (cm). `stand` = the shooter's point behind the gap, `outward` = unit vector
 * over the parapet away from the roof, `facing_yaw` = yawOf(outward).
 */
export interface FiringGap {
  id: string;
  building: string;
  /** The wall_id of the parapet the gap is cut from. */
  parapet: string;
  center: Vec2;
  width_cm: number;
  sill_cm: number;
  outward: Vec2;
  stand: Vec2;
  facing_yaw: number;
}

export const MANTLE_KINDS = ['window', 'fence', 'parapet', 'obstacle', 'crate', 'partition', 'ledge'] as const;
export type MantleKind = (typeof MANTLE_KINDS)[number];

export const MIN_MANTLE_HEIGHT_CM = 50;
export const MAX_MANTLE_HEIGHT_CM = 150;
export function isMantleable(height_cm: number): boolean {
  return height_cm >= MIN_MANTLE_HEIGHT_CM && height_cm <= MAX_MANTLE_HEIGHT_CM;
}

/** A mantle edge by id: the layout's own list first, then every building's (null-safe on partial data). */
export function findMantleEdge(l: Partial<Pick<GenLayout, 'mantle_edges' | 'buildings'>>, id: string): MantleEdge | undefined {
  return (l.mantle_edges ?? []).find((m) => m.id === id) ?? (l.buildings ?? []).flatMap((b) => b.mantle_edges ?? []).find((m) => m.id === id);
}

/**
 * Why the leg a -> b (on layers la -> lb) cannot be a crossing of mantle edge `edge` (null: it can): both ends on ONE layer, the
 * edge's height in the mantle band (50..150 cm), and the segment a-b crossing the edge's span (segmentsIntersect).
 */
export function mantleLegIssue(a: Vec2, b: Vec2, la: string, lb: string, edge: MantleEdge): string | null {
  if (la !== lb) return `mantle edge ${edge.id}: a mantle leg stays on one layer (${la} -> ${lb})`;
  if (!isMantleable(edge.height_cm)) return `mantle edge ${edge.id}: height ${edge.height_cm} cm is outside the mantle band ${MIN_MANTLE_HEIGHT_CM}..${MAX_MANTLE_HEIGHT_CM} cm`;
  if (!segmentsIntersect(a, b, edge.start, edge.end)) return `the leg does not cross mantle edge ${edge.id}`;
  return null;
}

/** True when `via` names something a route link can take: a ladder, a building stair, a cut crossing, a hole (hole_*) or a mantle edge. */
export function isKnownLinkId(l: Partial<Pick<GenLayout, 'ladders' | 'buildings' | 'cut' | 'mantle_edges'>>, via: string): boolean {
  return via.startsWith('hole') || !!findMantleEdge(l, via) || (l.ladders ?? []).some((x) => x.id === via)
    || (l.buildings ?? []).some((b) => (b.stairs ?? []).some((x) => x.id === via)) || !!l.cut?.crossings?.some((c) => c.id === via);
}

export interface MantleEdge {
  id: string;
  start: Vec2;
  end: Vec2;
  floor: number;
  /** Base Z coordinate in UE cm. */
  base_z: number;
  /** Height of edge above foot level, cm (must be 50..150 cm for mantling). */
  height_cm: number;
  /** Outward normal direction of vault/climb. */
  normal: Vec2;
  kind?: MantleKind;
  building?: string;
  wall?: string;
}

export interface GenRoom {
  id: string;
  floor: number;
  bounds: { min: Vec2; max: Vec2 };
  kind?: 'room' | 'corridor' | 'stairwell' | 'hall' | 'balcony' | 'lobby' | 'service';
  /** Kit-planned blocks: what the room is for (kitPlan.ts RoomUse): hall, corridor, bath, kitchen, living, tambour, landing (the stair core). */
  use?: 'hall' | 'corridor' | 'bath' | 'kitchen' | 'living' | 'tambour' | 'landing';
  /** Ids of every opening whose `links` name the room (planned buildings: windows too; seam ids between two corridor cells). */
  door_ids?: string[];
  /** Planned buildings: the unit the room belongs to ('suite' = an end suite; a block's flat id, GenBuilding.units). */
  unit?: string;
  /** Planned buildings: the zone ('common' = a block's stair core, landing and ground lobbies; wings later). */
  zone?: string;
  /** Planned buildings: the exterior wall ids (`<building>_N|S|W|E`) the room touches. */
  walls?: string[];
  /**
   * A walkable representative point of the room on its own floor, in the BUILDING frame (the frame of `bounds`: unrotated, absolute cm);
   * absent = no walkable point found. The bounds centre can lie on a floor gap or a flight (roomAnchors.ts); the engine smoke probes this.
   */
  anchor?: Vec2;
}

/** The set-piece vocabulary of the grammar (docs/design/levelgen/LEVEL_GENERATOR.md section 3). */
export const SET_PIECES = ['alley', 't_junction', 'facade', 'one_entry_compound', 'open_square', 'cut_crossing', 'ladder_overwatch',
  'tower', 'chimney', 'ruined_two_level', 'dead_end', 'junkyard_field', 'marksman_lane', 'industrial_hall', 'warehouse', 'tank_farm', 'street',
  'checkpoint', 'bunker'] as const;
/**
 * Set-pieces that are obstacles only: never enterable, never a walkable roof, no ladder / stair onto them (a factory chimney -
 * game designer 2026-11-02: "chimneys are NOT ladder towers"). validateGenLayout enforces it.
 */
export const NEVER_CLIMBABLE: readonly SetPiece[] = ['chimney'];
export type SetPiece = (typeof SET_PIECES)[number];

/**
 * The building archetypes of docs/design/levelgen/gdd/building-archetypes.md section 1 (S2_ROOMS_PLAN.md section 7 item 2).
 * Informational until S2: stories, rooms and doors do not key off it yet (they still key off `set_piece`).
 */
export const ARCHETYPES = ['hall', 'warehouse', 'office', 'block', 'hospital', 'shop_flats', 'shed', 'ruin', 'gallery', 'facade'] as const;
export type Archetype = (typeof ARCHETYPES)[number];
/**
 * Lot template (levelgen_v2.json `templates.catalog`, blockTemplates.ts) / classic-grammar piece (generate.ts) -> the archetype
 * of the building it places. Templates without a building (junkyard_field, fenced_lot, boiler_yard) and the chimney (an
 * obstacle, not an archetype) have no entry.
 */
export const TEMPLATE_ARCHETYPE: Readonly<Record<string, Archetype>> = {
  industrial_hall: 'hall',
  warehouse: 'warehouse',
  small_office: 'office',
  two_storey: 'block',
  one_entry_compound: 'shed',
  tank_farm: 'shed',
  ladder_overwatch: 'gallery',
  facade: 'facade',
  ruined_two_level: 'ruin',
};

export const COVER_KINDS = ['jersey', 'sandbag', 'crate', 'wreck', 'barrel', 'silo', 'boiler'] as const;
export type CoverKind = (typeof COVER_KINDS)[number];

export interface GenWall extends LayoutWall {
  /** The height level the wall stands on: 0 = ground (default); n = a building's floor n (its slab at levelZ(n)). */
  floor?: number;
  kind?: WallKind;
  /** The building this wall belongs to (perimeter / interior / parapet). */
  building?: string;
  /** Schema /2 (structureClasses.ts, structure_classes.json): structural class and material of the segment (future destruction; nothing reads it yet). */
  structure?: 'load_bearing' | 'partition' | 'annex' | 'facade';
  material?: 'concrete_panel' | 'brick' | 'plaster' | 'wood' | 'sheet_metal';
}

export interface Door { pos: Vec2; floor: number; /** The wall id the door is cut into. */ wall?: string }
/**
 * A stair = a walkable ramp at ~35 deg (run = 1.43 x rise) from `pos` (on `from`) to `top` (on `to`); an external one climbs along the
 * face outside the building to `mid` (the ramp's top end) and crosses onto the roof by a flat landing mid -> top. The builder cuts
 * the slab on `to` open above the ramp's last 3 m (the stairwell opening).
 */
export interface Stair {
  id: string; pos: Vec2; from: number; to: number; kind: 'stairwell' | 'external'; /** The top landing point (on `to`). */ top: Vec2;
  /**
   * External stair: the ramp's end before its landing. A HALF-LANDING SWITCHBACK stairwell (kit block, kitPlan.ts; `turn` set): the foot of the
   * second flight on the mid landing - flight a runs pos -> turn (from -> half level), flight b mid -> top (half level -> to).
   */
  mid?: Vec2;
  /** Switchback stairwell only: the top of the first flight (on the mid landing, half a storey above `from`). */
  turn?: Vec2;
  /** Switchback stairwell only: the mid landing slab, [min, max] in the BUILDING frame (like `footprint`, building_coord_fields.json: a world rect cannot turn), its top half a storey above `from`. */
  mid_rect?: [Vec2, Vec2];
  /** Schema /2: the flight is impassable (stories 4, 13); the validator removes its edge (S1 later). */
  blocked?: 'rubble' | 'burnt' | 'barricade';
  /** Schema /2: one-way, top -> foot only. */
  down_only?: boolean;
}
/** The stair's horizontal run per metre of rise (35 deg; the characters walk up to 44 deg). */
export const STAIR_RUN_PER_RISE = 1.43;

/** One walkable flight of a stair: from `a` (on level `z0`, storeys, may be a half level) to `b` (level `z1`). */
export interface StairFlight { a: Vec2; b: Vec2; z0: number; z1: number }
/**
 * The flights of a stair: one (pos -> top) for a straight flight or an external stair's ramp (pos -> mid), two for a half-landing switchback
 * (pos -> turn, mid -> top). Every consumer that needs the ramp bodies (validators, anchors, the builder's mirror) goes through this.
 */
export function stairFlights(st: Stair): StairFlight[] {
  if (st.kind === 'stairwell' && st.turn && st.mid) {
    const h = (st.from + st.to) / 2;
    return [{ a: st.pos, b: st.turn, z0: st.from, z1: h }, { a: st.mid, b: st.top, z0: h, z1: st.to }];
  }
  return [{ a: st.pos, b: st.kind === 'external' && st.mid ? st.mid : st.top, z0: st.from, z1: st.to }];
}
/** Is this a half-landing switchback stairwell (two flights + a mid landing)? */
export const isSwitchback = (st: Stair): boolean => st.kind === 'stairwell' && !!st.turn && !!st.mid;
/**
 * The free approach box (cm) in front of every stair-core flight's foot and after its top (BUILDING_GENERATOR.md 4.2: 200; the squad
 * bunches here). One source for the generator (generate.ts STAIR_APPROACH, metres) and the validator (TRAVERSAL.approachDepth).
 */
export const STAIR_APPROACH_CM = 200;

export interface GenBuilding {
  id: string;
  /**
   * Rectangle corners [min, max] (cm) of the UNROTATED footprint; with `rotation` the building stands rotated about its centre
   * (rotation.ts: footprintPoly / inFootprint). Its other points (doors, openings, stairs, holes, ...) are world coordinates.
   */
  footprint: [Vec2, Vec2];
  /** Degrees about the footprint centre (UE yaw sense); absent = 0 = axis-aligned (designer 2026-11-16, item 4). */
  rotation?: number;
  /** Total height, cm = floors * LEVEL_HEIGHT (the roof sits on level `floors`). */
  height: number;
  /** Interior floors counting the ground floor: floor n (1 <= n < floors) sits on level n; the roof on level `floors`. */
  floors: number;
  /** No intermediate floor slabs (a hall, a tower): the stairwell / ladder climbs straight from the ground to the roof. */
  hollow?: boolean;
  enterable: boolean;
  roof_walkable: boolean;
  doors: Door[];
  /** Unified wall openings (doors, windows, loopholes). Backward-compatible with `doors`. */
  openings?: WallOpening[];
  /** Interior partition walls (with door/window openings). */
  partitions?: GenPartition[];
  /** Storey floor slabs with gap areas (open landings, catwalks, stairwells). */
  floors_detail?: BuildingFloor[];
  /** Hatches providing roof access from upper storey. */
  hatches?: RoofHatch[];
  /** Parapet segments around walkable roof perimeter. */
  parapets?: RoofParapet[];
  /** Sub-rooms partitioned within this building. */
  rooms?: GenRoom[];
  /** Mantle edge markers on windows, parapets, and low obstacles (50-150 cm). */
  mantle_edges?: MantleEdge[];
  stairs: Stair[];
  /** Holes in the roof (down onto the top floor) - the ruined factory. */
  roof_holes: Vec2[];
  /** Holes in an upper floor's slab (down one floor): { pos, floor }. */
  floor_holes: { pos: Vec2; floor: number }[];
  set_piece: SetPiece;
  // --- schema /2 (BUILDINGS_TECH_SPEC.md section 2), all optional ---
  /** The archetype (building-archetypes.md section 1), from the lot template (TEMPLATE_ARCHETYPE); none on a chimney. */
  archetype?: Archetype;
  role?: BuildingRole;
  balconies?: Balcony[];
  fire_escapes?: FireEscape[];
  snow_ramps?: SnowRamp[];
  breach_panels?: BreachPanel[];
  /** 0-2 applied stories (F2 second story). */
  stories?: StoryInstance[];
  key_route?: Route;
  breach_route?: Route;
  /** Opening id of the fastest entry. */
  fast_entry?: string;
  /** Opening id of the quietest entry. */
  quiet_entry?: string;
  /**
   * Schema /2, planned buildings (S2_ROOMS_PLAN.md 4.1; the `building_plans` toggle): how the interior was made. 'bands' = the band
   * plan (buildings/bandPlan.ts) with doors / windows from planDoors / planWindows; 'bsp' = the plan fell back (`fallback` = why) and the
   * legacy partitioning ran. `core_wall` / `street_wall` are wall ids (`<id>_N|S|W|E`), never Side labels of another frame.
   */
  plan?: { v: 1; kind: 'bands' | 'bsp' | 'single'; seed: number; core_wall: string; street_wall: string; variant?: string; fallback?: string; /** Blocks: the number of sections (one stair core and entrance each). */ sections?: number;
    /** Kit blocks: 'kit' (kitPlan.ts); notes = why a section has fewer flats / rooms than the kit, or why the kit pattern fell back to the S2.3 planner. */
    pattern?: 'kit'; notes?: string[];
    /** Planned blocks: the plan template (building_plans.json recipes.block.templates, e.g. block_khrushchevka / block_sections). */
    template?: string };
  /** Residential blocks: the building series of its plan template (e.g. '1-464', the khrushchevka); absent = no series (a generic plan). */
  series?: string;
  /** Residential blocks: the era / house type of its plan template (khrushchev_1960s, generic; later stalin, barracks, dormitory ...). */
  era?: string;
  /** Schema /2, planned blocks (S2_ROOMS_PLAN.md 4.1): the flats of every storey (ids only: rooms, doors, columns). */
  units?: BuildingUnit[];
}

/** A flat of a planned block on one storey: its rooms, its entry doors, the column it shares with the flats above / below, the dead-end tag (story 17). */
export interface BuildingUnit {
  id: string;
  kind: 'flat';
  floor: number;
  /** Room ids. */
  rooms: string[];
  /** Opening ids of the doors joining the flat to a room outside it. */
  entries: string[];
  /** The stack of the same flat on every storey (balcony slots, a fire escape). */
  column: string;
  /** A single entry: nothing else leads in. */
  dead_end?: boolean;
  /** Dead ends: cm walked from the nearest stair pad to the deepest room. */
  run_cm?: number;
}

export interface GenCut {
  /** The trench polygon (walkable at floor_z). */
  poly: Vec2[];
  /** The trench floor's height level (CUT_LEVEL = -1) and its z relative to ground_z, cm (levelZ(level)). */
  level: number;
  floor_z: number;
  /** Walkable crossings at ground level (bridges / ramps): polygons across the trench. */
  crossings: { id: string; poly: Vec2[]; kind: 'bridge' | 'ramp' }[];
}

export interface GenLadder {
  id: string;
  /** The ladder actor's origin (the bottom centre of the rung plane, on the LOWER floor). */
  pos: Vec2;
  /** Actor yaw: +X points away from the wall, towards the climber (LADDERS.md 3.1). */
  yaw: number;
  rungs: number;
  /** The lower and upper LAYERS it links: 'ground' | 'cut' | 'roof:<building>' | 'floor:<building>:<n>'. */
  from: string;
  to: string;
  /** Their height levels (level_to - level_from >= 1; the ladder's height = that difference x 3 m). */
  level_from: number;
  level_to: number;
  /** cm of step under the ladder's foot so rungs x pitch + plinth = the level difference exactly (ladderForLevels). */
  plinth_cm: number;
  /** The bottom link point (on the lower level, ~1 m out from the rungs: where a route arrives; LADDERS.md 3.1). */
  foot: Vec2;
  /** The top link point (on the upper level). */
  top: Vec2;
}

export interface GenCover { id: string; pos: Vec2; yaw: number; size: [number, number, number]; kind: CoverKind }

export interface GenRoute extends LayoutRoute {
  /** The one route the squad must complete (exactly one per layout). */
  mandatory?: boolean;
  /** The floor / level each point stands on (same length as points; default all 0). 'cut' = the trench floor; 'roof:<b>' / 'floor:<b>:<n>'. */
  layers?: string[];
  /**
   * Links taken between consecutive points: { at: index of the point AFTER the link, via: ladder / stair / hole / cut crossing id }.
   * A MANTLE link ({ at, via: '<mantle edge id>' }, T-02 gap 4) joins two points on the SAME layer: the leg points[at - 1] -> points[at]
   * crosses that edge (an obstacle the squad climbs over instead of walking round it, mantleLegIssue); the walls-only nav check lets the
   * leg through and the evaluation adds the vault time (building_route_costs.json actions.vault.t) per mantle.
   */
  links?: { at: number; via: string }[];
  /** Which set-pieces the route passes, in order (for the report). */
  set_pieces?: SetPiece[];
  /**
   * The designer's intent for the route: 'kill_zone' = a deliberately exposed passage (V3 along the cut floor: seen from both
   * rims, few exits). The evaluation reports its exposure as INTENDED and the critics are told not to count it as a defect.
   */
  intent?: RouteIntent;
  /** Why (shown with the intent in the report and the critic prompt). */
  intent_note?: string;
}

export interface SpawnPoint { id: string; pos: Vec2; kind: 'edge' | 'patrol' | 'ambush' }
export interface PatrolRoute { id: string; points: Vec2[] }

export interface GenLayout {
  /** LEVELGEN_SCHEMA ('/1') or LEVELGEN_SCHEMA_V2 ('/2'); kept `string` so JSON-loaded layouts type-check unchanged. */
  schema: string;
  /** Schema /2: sha1 of building_route_costs.json; stored routes with another hash are recomputed. */
  cost_hash?: string;
  /**
   * Export the designer-marked killing grounds (zones / routes with an `intent`) to the runtime files: the tactical file's
   * `kill_zones` and tagged zone actors (T-02 gap 1). Opt-in so the accepted maps (Depot X2 / X4 / X6) stay byte-identical until
   * they are re-exported on purpose; the lab maps set it.
   */
  export_intents?: boolean;
  /** Schema /2: districts of lots with a theme. */
  districts?: District[];
  /** Schema /2: links between buildings. */
  building_links?: BuildingLink[];
  name: string;
  comment?: string;
  /** The reference profile id and the generation inputs (reproducible). */
  profile: string;
  seed: number;
  params: GenParams;
  bounds: { min: Vec2; max: Vec2 };
  ground_z: number;
  spawn: { pos: Vec2; yaw: number };
  end: Vec2;
  walls: GenWall[];
  buildings: GenBuilding[];
  cut?: GenCut;
  ladders: GenLadder[];
  covers: GenCover[];
  zones: (LayoutZone & { set_piece?: SetPiece; /** 'kill_zone': the zone is a deliberate killing ground (the cut). */ intent?: RouteIntent })[];
  routes: GenRoute[];
  spawn_points: SpawnPoint[];
  /** layer: 'ground' (absent) or a route layer ('roof:<bid>', 'floor:<bid>:<n>') - the level the ambusher waits on (T-02 gap 3). */
  ambush_points: { id: string; pos: Vec2; layer?: string }[];
  patrol_routes: PatrolRoute[];
  /** Global mantle edge markers for vaulting/climbing (50-150 cm). */
  mantle_edges?: MantleEdge[];
  /** The combat-lab style pressure block for the level JSON (COMBAT_LAB.md section 2). */
  pressure?: Record<string, unknown>;
  /** Region re-roll state (levelgen v2 stage 1, regionRegen.ts): locked districts + the re-roll log. Absent in older files. */
  regen?: RegenState;
  /** Map expansion (levelgen v2 stage 2, expand.ts): the canvas grew by area round the old map, which kept its coordinates. */
  expansion?: ExpansionState;
  /** Start + goals visited in any order (levelgen v2 stage 2, objectives.ts). Absent = the classic spawn -> end only. */
  objectives?: ObjectiveSet;
  /**
   * The chokepoint / threat analysis (chokepoints.ts analyzeLayout), written by «Принять» for the game / the PMC AI: chokepoints,
   * dropAmbushSpots, windowsCovering, overwatchRoofs, hidingSpots. Absent in older files. Stale when its geometry_hash differs
   * from geometryHash(layout) (the map was edited after it).
   */
  analysis?: LayoutAnalysis;
  /**
   * Firing gaps in the parapets of overwatch roofs (firingGaps.ts withFiringGaps, written by «Принять» / scripts/add-firing-gaps.ts); the
   * builder cuts each from its parapet and spawns a sill. TOP-LEVEL on purpose: geometryHash hashes `buildings`, and a derived field there
   * would make the analysis stale. Not part of geometryHash. Absent = no gaps (the config is off, or no roof qualifies).
   */
  firing_gaps?: FiringGap[];
  /**
   * PMC posts + the defend posts' charges (pmcPosts.ts / pmcCharges.ts, PMC_DEFENCE.md D2, task 087): the builder carries the posts into
   * the level JSON `pmc_squads`, the props autogen writes the charges as the locked props layer `pmc_charges`. Absent = no generated PMC.
   * Not part of geometryHash (it never changes the geometry); stale when its geometry_hash differs from the tactical map's.
   */
  pmc?: PmcPlan;
}

/** The version of the `analysis` block; a file with a newer one is refused, an absent block = not analysed (old files load). */
export const ANALYSIS_VERSION = 2;
// 2 (firing gaps): the overwatch spots stand at the roof rim (threats.overwatch.rim_stand_inset_m) and count only targets OUTWARD of
// their own roof edge (outward_only); a version-1 analysis still loads but its spots lie 1 m in and see across their own slab.

/**
 * The version of the `regen` block; a file with a newer version is refused (validateGenLayout), an absent block = no locks.
 * 2 (stage 2): + `blocks` (the block type of every lot, so a re-roll can give a block another type). A version-1 block still loads.
 */
export const REGEN_STATE_VERSION = 2;
/** A LOCKED district: a polygon (UE cm); every object whose centre is inside it is never touched by a region re-roll. */
export interface LockDistrict { id: string; name: string; poly: Vec2[] }
/** One applied re-roll (reproducible: same map + zone + seed -> same result). */
export interface RerollRecord { seed: number; zone: Vec2[]; replaced: number; added: number; retries: number; /** 'jitter' | 'types' | 'full' (absent = before task 072). */ mode?: string; /** The 'full' mode's chaos 0..1. */ chaos?: number }
/**
 * A block (lot) of the map and its current type (blockTemplates.ts): `rect` is [min, max] UE cm. `parent`: the block a 'full'
 * re-roll cut this parcel from (fullReroll.ts) - its parcels re-roll together when all of them are in the zone.
 */
export interface BlockRecord { id: string; rect: [Vec2, Vec2]; template: string; parent?: string }
export interface RegenState { version: number; locks: LockDistrict[]; rerolls: RerollRecord[]; blocks?: BlockRecord[] }

/** One expansion step: the factor by area relative to the base map, the seed of its street plan, the canvas after it (m). */
export interface ExpansionStep { factor: number; seed: number; size_m: [number, number] }
export const EXPANSION_STATE_VERSION = 1;
export interface ExpansionState {
  version: number;
  /** The generator's map size (layout.params.size_m) - the frame of the core the re-roll regenerates. */
  base_size_m: [number, number];
  steps: ExpansionStep[];
  /** The old map's rectangle of every step (UE cm [min, max]): drawn as the seam on the plan. */
  seams: [Vec2, Vec2][];
}

export const OBJECTIVES_VERSION = 1;
export interface ObjectiveGoal { id: string; pos: Vec2; layer: string; kind: 'room' | 'yard' | 'street'; building?: string; zone?: string }
export interface ObjectiveSet {
  version: number;
  seed: number;
  /** Goals may be visited in any order. */
  order: 'free';
  start: { pos: Vec2; yaw: number; layer: string; source: 'spawn' | 'edge' | 'manual' };
  goals: ObjectiveGoal[];
}

export interface GenParams {
  /** Map size, m (default: the profile's). */
  size_m: [number, number];
  /** Cover density multiplier (1 = the profile's). */
  density: number;
  /** Route variants wanted. */
  variants: number;
  /** 0 = flat; 1 = ladders + roofs; 2 = + stairwells / floors; 3 = + the ruin's holes. */
  verticality: 0 | 1 | 2 | 3;
  /** «Макс. угол поворота зданий», degrees (absent = levelgen_v2.json rotation.max_deg_default = 0): re-rolls / expansions rotate templates within +-it. */
  max_rotation_deg?: number;
  /**
   * «Планировки зданий (S2)»: enterable offices get a band plan with doors, windows and links (generate.ts addPlannedOffice; schema /2).
   * Absent = off, and the output is byte for byte the /1 generator's - never put in DEFAULT_PARAMS.
   */
  building_plans?: boolean;
}

export const DEFAULT_PARAMS: GenParams = { size_m: [177, 124], density: 1, variants: 3, verticality: 3 };

const isNum = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);
const isVec = (v: unknown): v is Vec2 => Array.isArray(v) && v.length === 2 && isNum(v[0]) && isNum(v[1]);

/** The lab validation of the ground layer + the extension's own checks (empty = valid). */
export function validateGenLayout(data: unknown): string[] {
  if (!data || typeof data !== 'object') return ['expected a JSON object'];
  const l = data as Partial<GenLayout>;
  const e: string[] = [];
  if (!isLevelgenSchema(l.schema)) e.push(`schema must be "${LEVELGEN_SCHEMA}" or "${LEVELGEN_SCHEMA_V2}"`);
  if (!isNum(l.seed)) e.push('seed: expected a number');
  if (typeof l.profile !== 'string') e.push('profile: expected the reference profile id');
  if (!isVec(l.end)) e.push('end: expected [x, y]');
  const ground = (() => { try { return toFormationLayout({ ...(l as GenLayout), schema: LEVELGEN_SCHEMA }, 'ground'); } catch (err) { e.push(`ground layer: ${err instanceof Error ? err.message : String(err)}`); return null; } })();
  if (ground) e.push(...validateLayout(ground).map((m) => `ground layer: ${m}`));
  const ids = new Set<string>();
  for (const b of l.buildings ?? []) {
    if (ids.has(b.id)) e.push(`buildings: duplicate id "${b.id}"`); ids.add(b.id);
    if (!Array.isArray(b.footprint) || b.footprint.length !== 2 || !isVec(b.footprint[0]) || !isVec(b.footprint[1])) e.push(`building ${b.id}: footprint must be [min, max]`);
    else if (b.footprint[0][0] >= b.footprint[1][0] || b.footprint[0][1] >= b.footprint[1][1]) e.push(`building ${b.id}: footprint min must be below max`);
    if (b.rotation !== undefined && (!isNum(b.rotation) || Math.abs(b.rotation) > 180)) e.push(`building ${b.id}: rotation must be a number of degrees in -180..180`);
    if (!isNum(b.floors) || b.floors < 1) e.push(`building ${b.id}: floors >= 1`);
    if (b.roof_walkable && isNum(b.floors) && b.height !== b.floors * LEVEL_HEIGHT) e.push(`building ${b.id}: a walkable roof sits on level ${b.floors} = ${b.floors * LEVEL_HEIGHT} cm, height is ${b.height}`);
    for (const s of b.stairs ?? []) if (s.to > b.floors) e.push(`building ${b.id}: stair ${s.id} leads to floor ${s.to} of ${b.floors}`);
    for (const op of b.openings ?? []) {
      if (!OPENING_KINDS.includes(op.kind)) e.push(`building ${b.id}: opening kind must be door/window/loophole (got "${op.kind}")`);
      if (!isVec(op.pos)) e.push(`building ${b.id}: opening pos must be [x, y]`);
      if (!isNum(op.floor) || op.floor < 0 || op.floor > b.floors) e.push(`building ${b.id}: opening floor out of range (${op.floor})`);
      if (op.width !== undefined && (!isNum(op.width) || op.width <= 0)) e.push(`building ${b.id}: opening width must be > 0`);
      if (op.kind === 'loophole' && op.width !== undefined && (op.width < 15 || op.width > 60)) {
        e.push(`building ${b.id}: loophole width ${op.width} cm outside narrow slit range (20-40 cm)`);
      }
      if (op.sill_height !== undefined && (!isNum(op.sill_height) || op.sill_height < 0)) e.push(`building ${b.id}: opening sill_height must be >= 0`);
      if (op.top_height !== undefined && (!isNum(op.top_height) || op.top_height <= (op.sill_height ?? 0))) e.push(`building ${b.id}: opening top_height must be above sill_height`);
    }
    for (const pt of b.partitions ?? []) {
      if (!isVec(pt.start) || !isVec(pt.end)) e.push(`building ${b.id}: partition ${pt.id} start/end must be [x, y]`);
      if (!isNum(pt.floor) || pt.floor < 0 || pt.floor >= b.floors) e.push(`building ${b.id}: partition ${pt.id} floor out of range`);
      if (pt.thickness !== undefined && (!isNum(pt.thickness) || pt.thickness <= 0)) e.push(`building ${b.id}: partition ${pt.id} thickness must be > 0`);
    }
    for (const fd of b.floors_detail ?? []) {
      if (!isNum(fd.floor) || fd.floor < 0 || fd.floor >= b.floors) e.push(`building ${b.id}: floor_detail floor out of range (${fd.floor})`);
      for (const gap of fd.gaps ?? []) {
        if (!Array.isArray(gap.poly) || gap.poly.length < 3 || !gap.poly.every(isVec)) {
          e.push(`building ${b.id}: floor ${fd.floor} gap poly must be at least 3 points`);
        }
      }
    }
    for (const ht of b.hatches ?? []) {
      if (!isVec(ht.pos)) e.push(`building ${b.id}: hatch pos must be [x, y]`);
      if (!isNum(ht.floor) || ht.floor < 0) e.push(`building ${b.id}: hatch floor must be >= 0`);
    }
    for (const pr of b.parapets ?? []) {
      if (!isVec(pr.start) || !isVec(pr.end)) e.push(`building ${b.id}: parapet start/end must be [x, y]`);
      if (!isNum(pr.height_cm) || pr.height_cm <= 0) e.push(`building ${b.id}: parapet height_cm must be > 0`);
    }
    for (const me of b.mantle_edges ?? []) {
      if (!isVec(me.start) || !isVec(me.end) || !isVec(me.normal)) e.push(`building ${b.id}: mantle_edge ${me.id} start/end/normal must be [x, y]`);
      if (!isNum(me.height_cm) || !isMantleable(me.height_cm)) e.push(`building ${b.id}: mantle_edge ${me.id} height ${me.height_cm} cm must be 50..150 cm`);
    }
    if (NEVER_CLIMBABLE.includes(b.set_piece)) {
      if (b.roof_walkable || b.enterable || (b.stairs ?? []).length) e.push(`building ${b.id}: a ${b.set_piece} is an obstacle - no walkable roof, no entry, no stairs`);
      if ((l.ladders ?? []).some((ld) => ld.to === `roof:${b.id}` || ld.from === `roof:${b.id}` || ld.to.startsWith(`floor:${b.id}:`))) e.push(`building ${b.id}: a ${b.set_piece} never gets a ladder`);
    }
  }
  for (const ld of l.ladders ?? []) {
    if (!isNum(ld.rungs) || ld.rungs < MIN_RUNGS) e.push(`ladder ${ld.id}: >= ${MIN_RUNGS} rungs (a shorter ladder is decoration, LADDERS.md)`);
    if (!isVec(ld.pos) || !isVec(ld.top) || !isVec(ld.foot)) e.push(`ladder ${ld.id}: pos / foot / top must be [x, y]`);
    if (!isNum(ld.level_from) || !isNum(ld.level_to) || ld.level_to - ld.level_from < 1) e.push(`ladder ${ld.id}: level_to must be above level_from`);
    else if (isNum(ld.rungs) && Math.abs(ld.rungs * RUNG_PITCH + (ld.plinth_cm ?? 0) - levelZ(ld.level_to - ld.level_from)) > 1) e.push(`ladder ${ld.id}: rungs x pitch + plinth must equal ${levelZ(ld.level_to - ld.level_from)} cm`);
  }
  if (l.cut && l.cut.level !== CUT_LEVEL) e.push(`cut: level must be ${CUT_LEVEL}`);
  if (l.cut && Array.isArray(l.cut.poly) && l.cut.poly.length >= 3 && l.cut.poly.every(isVec)) {
    // building vs cut wall (2026-11-03): the wall stands outside the trench edge, `t` thick; keep CUT_BUILDING_MARGIN_CM clear of it
    const wallTs = (l.walls ?? []).filter((w) => w.kind === 'cut_wall').map((w) => w.thickness);
    const t = wallTs.length ? Math.max(...wallTs) : CUT_WALL_DEFAULT_THICKNESS_CM;
    const reach = t + CUT_BUILDING_MARGIN_CM;
    const xs = l.cut.poly.map((p) => p[0]), ys = l.cut.poly.map((p) => p[1]);
    const x0 = Math.min(...xs) - reach, x1 = Math.max(...xs) + reach, y0 = Math.min(...ys) - reach, y1 = Math.max(...ys) + reach;
    for (const b of l.buildings ?? []) {
      if (!Array.isArray(b.footprint) || b.footprint.length !== 2 || !isVec(b.footprint[0]) || !isVec(b.footprint[1])) continue;
      const f = footprintBBox(b);
      if (f[0][0] < x1 && f[1][0] > x0 && f[0][1] < y1 && f[1][1] > y0) e.push(`building ${b.id}: footprint overlaps the cut wall (wall ${t} cm + margin ${CUT_BUILDING_MARGIN_CM} cm around the cut; no building on the cut walls)`);
    }
  }
  const mandatory = (l.routes ?? []).filter((r) => r.mandatory);
  if (mandatory.length !== 1) e.push(`routes: exactly one mandatory route (found ${mandatory.length})`);
  for (const r of l.routes ?? []) {
    if (r.layers && r.layers.length !== r.points.length) e.push(`route ${r.id}: layers must match points (${r.layers.length} vs ${r.points.length})`);
    for (const k of r.links ?? []) {
      if (k.at < 1 || k.at >= r.points.length) { e.push(`route ${r.id}: link at ${k.at} out of range`); continue; }
      const edge = findMantleEdge(l, k.via);
      if (edge) {
        // a mantle link (T-02 gap 4): the leg must cross the edge, on one layer, within the mantle band
        const lay = r.layers ?? r.points.map(() => 'ground');
        const issue = isVec(r.points[k.at - 1]) && isVec(r.points[k.at]) ? mantleLegIssue(r.points[k.at - 1], r.points[k.at], lay[k.at - 1], lay[k.at], edge) : null;
        if (issue) e.push(`route ${r.id}: link at ${k.at}: ${issue}`);
      } else if (!isKnownLinkId(l, k.via)) e.push(`route ${r.id}: link at ${k.at}: unknown link "${k.via}" (a ladder, stair, hole, cut crossing or mantle edge id)`);
    }
    if (r.intent !== undefined && !(ROUTE_INTENTS as readonly string[]).includes(r.intent)) e.push(`route ${r.id}: unknown intent "${r.intent}"`);
    if (r.intent === 'kill_zone' && r.mandatory) e.push(`route ${r.id}: the mandatory route is never a kill zone (a variant the player chooses)`);
  }
  for (const me of l.mantle_edges ?? []) {
    if (!isVec(me.start) || !isVec(me.end) || !isVec(me.normal)) e.push(`mantle_edge ${me.id}: start/end/normal must be [x, y]`);
    if (!isNum(me.height_cm) || !isMantleable(me.height_cm)) e.push(`mantle_edge ${me.id}: height ${me.height_cm} cm must be 50..150 cm`);
  }
  if (l.regen !== undefined) e.push(...validateRegenState(l.regen));
  if (l.expansion !== undefined) e.push(...validateExpansionState(l.expansion));
  if (l.objectives !== undefined) e.push(...validateObjectives(l.objectives, l.bounds));
  if (l.analysis !== undefined) e.push(...validateAnalysis(l.analysis));
  if (l.firing_gaps !== undefined) e.push(...validateFiringGaps(l.firing_gaps, l.buildings ?? []));
  return e;
}

/** The `firing_gaps` list's own checks (empty = valid): shape, unique ids, a known building, the parapet one of its walls. */
export function validateFiringGaps(g: unknown, buildings: readonly GenBuilding[]): string[] {
  if (!Array.isArray(g)) return ['firing_gaps: expected an array'];
  const e: string[] = [], seen = new Set<string>();
  for (const x of g as Partial<FiringGap>[]) {
    const id = typeof x?.id === 'string' ? x.id : '?';
    if (typeof x?.id !== 'string' || !x.id) { e.push('firing_gaps: every gap needs an id'); continue; }
    if (seen.has(id)) e.push(`firing_gap ${id}: duplicate id`);
    seen.add(id);
    const b = buildings.find((bb) => bb.id === x.building);
    if (!b) e.push(`firing_gap ${id}: unknown building "${x.building}"`);
    else if (!(b.parapets ?? []).some((p) => p.wall_id === x.parapet)) e.push(`firing_gap ${id}: building ${b.id} has no parapet "${x.parapet}"`);
    for (const k of ['center', 'outward', 'stand'] as const) if (!isVec(x[k])) e.push(`firing_gap ${id}: ${k} must be [x, y]`);
    for (const k of ['width_cm', 'sill_cm'] as const) if (!isNum(x[k]) || (x[k] as number) <= 0) e.push(`firing_gap ${id}: ${k} must be > 0`);
    if (!isNum(x.facing_yaw)) e.push(`firing_gap ${id}: facing_yaw must be a number`);
  }
  return e;
}

/** The `analysis` block's own checks (empty = valid): a known version and the five lists. */
export function validateAnalysis(a: unknown): string[] {
  if (!a || typeof a !== 'object') return ['analysis: expected an object'];
  const x = a as Partial<LayoutAnalysis>;
  const e: string[] = [];
  if (!isNum(x.version) || x.version < 1) e.push('analysis.version: expected a positive number');
  else if (x.version > ANALYSIS_VERSION) e.push(`analysis.version ${x.version} is newer than this tool (${ANALYSIS_VERSION})`);
  for (const k of ['chokepoints', 'dropAmbushSpots', 'windowsCovering', 'overwatchRoofs', 'hidingSpots'] as const) if (!Array.isArray(x[k])) e.push(`analysis.${k}: expected an array`);
  for (const cp of Array.isArray(x.chokepoints) ? x.chokepoints : []) if (typeof cp?.id !== 'string' || !(cp.score >= 0 && cp.score <= 1) || !Array.isArray(cp.poly)) { e.push('analysis.chokepoints: { id, score 0..1, poly }'); break; }
  return e;
}

/** The `expansion` block's own checks (empty = valid). */
export function validateExpansionState(s: unknown): string[] {
  if (!s || typeof s !== 'object') return ['expansion: expected an object'];
  const x = s as Partial<ExpansionState>;
  const e: string[] = [];
  if (!isNum(x.version) || x.version < 1) e.push('expansion.version: expected a positive number');
  else if (x.version > EXPANSION_STATE_VERSION) e.push(`expansion.version ${x.version} is newer than this tool (${EXPANSION_STATE_VERSION})`);
  if (!isVec(x.base_size_m)) e.push('expansion.base_size_m: expected [w, h]');
  if (!Array.isArray(x.steps) || !x.steps.length) e.push('expansion.steps: expected at least one step');
  else for (const st of x.steps) if (!isNum(st?.factor) || st.factor <= 1 || !isNum(st.seed) || !isVec(st.size_m)) e.push('expansion.steps: { factor > 1, seed, size_m: [w, h] }');
  if (!Array.isArray(x.seams)) e.push('expansion.seams: expected an array');
  return e;
}

/** The `objectives` block's own checks (empty = valid): manual_count_min..count_max goals (levelgen_v2.json) with unique ids inside the bounds. */
export function validateObjectives(s: unknown, bounds?: { min: Vec2; max: Vec2 }): string[] {
  if (!s || typeof s !== 'object') return ['objectives: expected an object'];
  const o = s as Partial<ObjectiveSet>;
  const e: string[] = [];
  if (!isNum(o.version) || o.version < 1) e.push('objectives.version: expected a positive number');
  else if (o.version > OBJECTIVES_VERSION) e.push(`objectives.version ${o.version} is newer than this tool (${OBJECTIVES_VERSION})`);
  if (o.order !== 'free') e.push('objectives.order: expected "free"');
  if (!o.start || !isVec(o.start.pos)) e.push('objectives.start.pos: expected [x, y]');
  if (!Array.isArray(o.goals) || o.goals.length < OBJ_CFG.manual_count_min || o.goals.length > OBJ_CFG.count_max) e.push(`objectives.goals: expected ${OBJ_CFG.manual_count_min}-${OBJ_CFG.count_max} goals`);
  else {
    const ids = new Set<string>();
    for (const g of o.goals) {
      if (typeof g?.id !== 'string' || !g.id || ids.has(g.id)) e.push('objectives.goals: ids must be unique non-empty strings');
      else ids.add(g.id);
      if (!isVec(g?.pos)) e.push(`objectives goal ${g?.id}: pos must be [x, y]`);
      else if (bounds && (g.pos[0] < bounds.min[0] || g.pos[0] > bounds.max[0] || g.pos[1] < bounds.min[1] || g.pos[1] > bounds.max[1])) e.push(`objectives goal ${g.id}: outside the bounds`);
    }
  }
  return e;
}

/** The `regen` block's own checks (empty = valid): a known version, locks with >= 3 points and unique ids. */
export function validateRegenState(s: unknown): string[] {
  if (!s || typeof s !== 'object') return ['regen: expected an object'];
  const r = s as Partial<RegenState>;
  const e: string[] = [];
  if (!isNum(r.version) || r.version < 1) e.push('regen.version: expected a positive number');
  else if (r.version > REGEN_STATE_VERSION) e.push(`regen.version ${r.version} is newer than this tool (${REGEN_STATE_VERSION})`);
  if (!Array.isArray(r.locks)) e.push('regen.locks: expected an array');
  else {
    const ids = new Set<string>();
    for (const k of r.locks) {
      if (typeof k?.id !== 'string' || !k.id) { e.push('regen.locks: a lock without an id'); continue; }
      if (ids.has(k.id)) e.push(`regen.locks: duplicate id "${k.id}"`);
      ids.add(k.id);
      if (!Array.isArray(k.poly) || k.poly.length < 3 || !k.poly.every(isVec)) e.push(`regen lock ${k.id}: poly needs >= 3 [x, y] points`);
    }
  }
  if (r.rerolls !== undefined && !Array.isArray(r.rerolls)) e.push('regen.rerolls: expected an array');
  return e;
}

/**
 * The named layers (walkable surfaces) a layout has: 'ground', 'cut', 'roof:<b>', 'floor:<b>:<n>'. A hollow hall has a floor
 * layer only where its stair core leaves a landing slab (floors_detail entry, BUILDING_GENERATOR.md 2026-11-18 switchback core).
 */
export function layersOf(l: GenLayout): string[] {
  const out = ['ground'];
  if (l.cut) out.push('cut');
  for (const b of l.buildings) {
    for (let n = 1; n < b.floors; n++) {
      if (!b.hollow || (b.floors_detail ?? []).some((fd) => fd.floor === n)) out.push(`floor:${b.id}:${n}`);
    }
    if (b.roof_walkable) out.push(`roof:${b.id}`);
  }
  return out;
}

/** The height level of a layer: ground 0, the cut -1, floor n = n, a roof = the building's `floors`. */
export function layerLevel(l: GenLayout, layer: string): number {
  if (layer === 'ground') return 0;
  if (layer === 'cut') return l.cut?.level ?? CUT_LEVEL;
  const m = /^(roof|floor):([^:]+)(?::(\d+))?$/.exec(layer);
  if (!m) throw new Error(`unknown layer "${layer}"`);
  const b = l.buildings.find((x) => x.id === m[2]);
  if (!b) throw new Error(`layer "${layer}": no building "${m[2]}"`);
  return m[1] === 'roof' ? b.floors : Number(m[3] ?? 1);
}

/** Every height level the layout uses, lowest first. */
export const levelsOf = (l: GenLayout): number[] => [...new Set(layersOf(l).map((x) => layerLevel(l, x)))].sort((a, b) => a - b);

const rectPoly = ([a, b]: [Vec2, Vec2]): Vec2[] => [[a[0], a[1]], [b[0], a[1]], [b[0], b[1]], [a[0], b[1]]];

/** Walls of one layer as lab walls (the parapet of a roof, the interior walls of a floor, the ground's everything). */
export function layerWalls(l: GenLayout, layer: string): LayoutWall[] {
  if (layer === 'ground') {
    return l.walls.filter((w) => (w.floor ?? 0) === 0).map(({ floor: _f, kind: _k, building: _b, ...w }) => w);
  }
  if (layer === 'cut') {
    if (!l.cut) return [];
    // the trench walls as the trench polygon's edges (the floor is bounded by them); crossings are above the climber's head
    const p = l.cut.poly;
    return p.map((a, i) => ({ id: `cut_edge_${i}`, a, b: p[(i + 1) % p.length], height: CUT_DEPTH, thickness: 20 }));
  }
  const m = /^(roof|floor):([^:]+)(?::(\d+))?$/.exec(layer);
  if (!m) throw new Error(`unknown layer "${layer}"`);
  const b = l.buildings.find((x) => x.id === m[2]);
  if (!b) throw new Error(`layer "${layer}": no building "${m[2]}"`);
  const walls: LayoutWall[] = [];
  const fp = footprintPoly(b);
  if (m[1] === 'roof') {
    if (b.parapets && b.parapets.length > 0) {
      b.parapets.forEach((p, i) => walls.push({ id: p.wall_id ?? `${b.id}_parapet_${i}`, a: p.start, b: p.end, height: p.height_cm, thickness: p.thickness_cm ?? 20 }));
    } else {
      // a parapet all round: nobody walks off the roof; the stair / ladder heads lie inside the footprint
      fp.forEach((a, i) => walls.push({ id: `${b.id}_parapet_${i}`, a, b: fp[(i + 1) % 4], height: 100, thickness: 20 }));
    }
    return walls;
  }
  const floor = Number(m[3] ?? 1);
  if (floor >= b.floors) throw new Error(`layer "${layer}": ${b.id} has no floor ${floor}`);
  if (b.hollow) {
    // a hall's upper level is only its stair-core landing: walk inside the core box, its edges toward the void are rails
    const core = (b.rooms ?? []).find((rm) => rm.floor === floor && (rm.kind === 'stairwell' || rm.kind === 'balcony'));
    if (!core) throw new Error(`layer "${layer}": ${b.id} has no floor ${floor}`);
    const cp = rectPoly([core.bounds.min, core.bounds.max]);
    cp.forEach((a, i) => walls.push({ id: `${b.id}_f${floor}_core_${i}`, a, b: cp[(i + 1) % 4], height: 100, thickness: 10 }));
    return walls;
  }
  fp.forEach((a, i) => walls.push({ id: `${b.id}_f${floor}_perimeter_${i}`, a, b: fp[(i + 1) % 4], height: LEVEL_HEIGHT, thickness: 30 }));
  const floorWalls = l.walls.filter((w) => w.building === b.id && (w.floor ?? 0) === floor);
  for (const w of floorWalls) walls.push({ id: w.id, a: w.a, b: w.b, height: w.height, thickness: w.thickness });
  if (!floorWalls.some((w) => w.kind === 'interior')) {
    for (const pt of b.partitions ?? []) {
      if (pt.floor !== floor) continue;
      const h = pt.height ?? LEVEL_HEIGHT;
      const th = pt.thickness ?? 20;
      const doors = (pt.openings ?? []).filter((op) => op.kind === 'door');
      if (doors.length === 0) {
        walls.push({ id: pt.id, a: pt.start, b: pt.end, height: h, thickness: th });
      } else {
        const dx = pt.end[0] - pt.start[0], dy = pt.end[1] - pt.start[1];
        const len = Math.hypot(dx, dy);
        if (len > 1) {
          const ux = dx / len, uy = dy / len;
          const spans = doors.map((d) => {
            const t = ((d.pos[0] - pt.start[0]) * ux + (d.pos[1] - pt.start[1]) * uy);
            const hw = (d.width ?? OPENING_DEFAULTS.door.width) / 2;
            return [Math.max(0, t - hw), Math.min(len, t + hw)] as [number, number];
          }).sort((a, b) => a[0] - b[0]);
          let cur = 0, idx = 0;
          for (const [s0, s1] of spans) {
            if (s0 - cur > 20) {
              walls.push({
                id: `${pt.id}_s${idx++}`,
                a: [pt.start[0] + ux * cur, pt.start[1] + uy * cur],
                b: [pt.start[0] + ux * s0, pt.start[1] + uy * s0],
                height: h,
                thickness: th,
              });
            }
            cur = Math.max(cur, s1);
          }
          if (len - cur > 20) {
            walls.push({
              id: `${pt.id}_s${idx++}`,
              a: [pt.start[0] + ux * cur, pt.start[1] + uy * cur],
              b: pt.end,
              height: h,
              thickness: th,
            });
          }
        }
      }
    }
  }
  return walls;
}

/** Unify legacy `doors` and new `openings` into a single list of WallOpening. */
export function unifyOpenings(b: GenBuilding): WallOpening[] {
  if (b.openings && b.openings.length > 0) return b.openings;
  return (b.doors ?? []).map((d, i) => ({
    id: `door_${b.id}_${i}`,
    kind: 'door' as const,
    pos: d.pos,
    floor: d.floor,
    wall: d.wall,
    width: OPENING_DEFAULTS.door.width,
    sill_height: OPENING_DEFAULTS.door.sill,
    top_height: OPENING_DEFAULTS.door.top,
  }));
}

/** The lab-compatible zone kind of a generated zone (the audit / the sim classify by the lab kinds only). */
export function labZoneKind(kind: string): ZoneKind {
  return (ZONE_KINDS as readonly string[]).includes(kind) ? (kind as ZoneKind) : 'open';
}

/**
 * One layer of a generated layout as a plain lab layout (schema codex.formation_layout/1): the simulator, the audit and the map
 * builder read it as they read formation_lab_01.json. Routes are cut to the legs on that layer (a route changing layers is
 * split: each on-layer run of points becomes "<route>_L<k>"); the spawn stays the layout's (ground) or the layer's first route point.
 */
export function toFormationLayout(l: GenLayout, layer = 'ground'): Layout {
  const walls = layerWalls(l, layer);
  const routes: LayoutRoute[] = [];
  for (const r of l.routes) {
    const layers = r.layers ?? r.points.map(() => 'ground');
    let run: Vec2[] = [];
    let k = 0;
    // a mantle crossing is no walk: the run is cut at the leg that crosses the edge (the simulator never paths it through its obstacle)
    const mantleAt = new Set((r.links ?? []).filter((x) => findMantleEdge(l, x.via)).map((x) => x.at));
    const flush = () => {
      if (run.length >= 2) {
        const id = layers.every((x) => x === layer) && !mantleAt.size ? r.id : `${r.id}_L${k++}`;
        // a split route keeps only the expectations of the zones THIS run enters (sampled every metre along its legs)
        const samples: Vec2[] = [];
        for (let i = 1; i < run.length; i++) {
          const a = run[i - 1], b = run[i], L = Math.hypot(b[0] - a[0], b[1] - a[1]);
          for (let s = 0; s <= L; s += 100) samples.push([a[0] + ((b[0] - a[0]) * s) / L, a[1] + ((b[1] - a[1]) * s) / L]);
        }
        samples.push(run[run.length - 1]);
        const expect = layer === 'ground' ? r.expect?.filter((x) => { const z = l.zones.find((zz) => zz.id === x.zone); return !!z && samples.some((p) => pointInPoly(p, z.poly)); }) : undefined;
        routes.push({ id, pace: r.pace, points: run, expect: expect && expect.length ? expect : undefined, threats: r.threats, slide: r.slide, expect_bypass: r.expect_bypass });
      }
      run = [];
    };
    r.points.forEach((p, i) => { if (layers[i] === layer) { if (mantleAt.has(i)) flush(); run.push(p); } else flush(); });
    flush();
  }
  const zones: LayoutZone[] = layer === 'ground' ? l.zones.map((z) => ({ id: z.id, kind: labZoneKind(z.kind), poly: z.poly })) : [];
  const first = routes[0]?.points[0];
  return {
    schema: LAYOUT_SCHEMA, name: layer === 'ground' ? l.name : `${l.name}@${layer}`, bounds: l.bounds, ground_z: l.ground_z,
    spawn: layer === 'ground' || !first ? l.spawn : { pos: first, yaw: 0 }, walls, zones, routes,
    props: layer === 'ground' ? l.covers.map((c) => ({ id: c.id, kind: 'heat_source' as const, pos: c.pos })).slice(0, 0) : undefined,
  };
}

/** The mandatory route (validateGenLayout guarantees exactly one). */
export const mandatoryRoute = (l: GenLayout): GenRoute => l.routes.find((r) => r.mandatory) ?? l.routes[0];

/** Axis-aligned rectangle helpers shared by the generator and the evaluator. */
export const rect = {
  poly: rectPoly,
  centre: ([a, b]: [Vec2, Vec2]): Vec2 => [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2],
  contains: ([a, b]: [Vec2, Vec2], p: Vec2, margin = 0): boolean => p[0] >= a[0] - margin && p[0] <= b[0] + margin && p[1] >= a[1] - margin && p[1] <= b[1] + margin,
  overlaps: (r: [Vec2, Vec2], s: [Vec2, Vec2], gap = 0): boolean => !(r[1][0] + gap <= s[0][0] || s[1][0] + gap <= r[0][0] || r[1][1] + gap <= s[0][1] || s[1][1] + gap <= r[0][1]),
  size: ([a, b]: [Vec2, Vec2]): Vec2 => [b[0] - a[0], b[1] - a[1]],
};
