// Training Ground / level generator v3 - «другой план» (S2_ROOMS_PLAN.md step S2.7): ONE planned building re-planned with a new seed, nothing else on the map touched.
// The plan seed of a building is fnv1a(id | footprint | storeys | archetype) (generate.ts planSeedOf); a re-plan adds a counter n (`|r<n>`; planSummary.ts replanCounter finds it again from the seed), so the same (building, n)
// always gives the same plan. The building is rebuilt the way a block template builds it: its own empty draft, the same addBuilding, finalizeDraftToLayout, then
// rotateContent about its own centre by the building's rotation. Kept from the old building: footprint, rotation, archetype, storeys, roof, which walls carry exterior doors
// (and where along them: the door draw `t` is reproduced, so the entrance stays about where the route points expect it). A counter is accepted when the plan is a band
// plan (as the old one was), differs from the current one, and its building passes validateBuilding without an error; up to REPLAN_MAX_TRIES counters are tried.
// The splice (`spliceBuilding`) replaces the building, its walls and its mantle edges in the layout; the story pass for it alone is storyPass.ts replanBuildingStory.
// Pure and deterministic; no DOM.
import type { Vec2 } from '../../movement/geometry.ts';
import { validateBuilding } from '../buildingTraversal.ts';
import type { BuildingSpec, Draft, Rect, Side } from '../generate.ts';
import { addBuilding, finalizeDraftToLayout } from '../generate.ts';
import { fromUE } from '../profile.ts';
import { rotateContent, rotationOf, toBuildingFrame } from '../rotation.ts';
import type { GenBuilding, GenLayout, GenWall, MantleEdge } from '../schema.ts';
import { planSignature, replanCounter } from './planSummary.ts';

/** Counters tried for one click (a tool cap, not a design number): the first valid plan that differs from the current one wins. */
export const REPLAN_MAX_TRIES = 8;

const SIDE_OF: Record<string, Side> = { N: 'north', S: 'south', E: 'east', W: 'west' };

/** Can the planner re-plan this building? An enterable, planned (band plan or its BSP fallback) office / block with interiors. */
export const isReplannable = (b: GenBuilding): boolean => !!b.plan && b.enterable && !b.hollow && (b.archetype === 'office' || b.archetype === 'block');

/** The profile-metre frame of a layout: width x height of its bounds (blockTemplates.ts frameOf). */
const frameOf = (l: Pick<GenLayout, 'bounds'>): Vec2 => [(l.bounds.max[1] - l.bounds.min[1]) / 100, (l.bounds.max[0] - l.bounds.min[0]) / 100];
const wallSide = (wall: string | undefined): Side | undefined => (wall ? SIDE_OF[wall.slice(wall.lastIndexOf('_') + 1)] : undefined);
const round2 = (v: number): number => Math.round(v * 100) / 100;

/** The draft rectangle (metres, x east / y south) of the building's UNROTATED footprint: a planned band plan is a multiple of 100 cm, a fallback keeps its cm. */
function draftRect(b: GenBuilding, frame: Vec2): Rect {
  const [[ax, ay], [bx, by]] = b.footprint;
  const p = fromUE([ax, ay], frame), q = fromUE([bx, by], frame);
  const x0 = Math.min(p[0], q[0]), y0 = Math.min(p[1], q[1]);
  const snap = (cm: number): number => (b.plan?.kind === 'bands' ? Math.round(cm / 100) * 100 : Math.round(cm));
  const wcm = snap(Math.abs(p[0] - q[0]) * 100), hcm = snap(Math.abs(p[1] - q[1]) * 100);
  return [[round2(x0), round2(y0)], [round2(x0) + wcm / 100, round2(y0) + hcm / 100]];
}

/** The exterior door sides of the old building (the street side first) and the along-wall fraction of its ground door on each, from the building frame. */
function doorSidesOf(b: GenBuilding, rect: Rect, frame: Vec2): { sides: Side[]; draws: number[] } {
  const street = wallSide(b.plan?.street_wall);
  const sides: Side[] = street ? [street] : [];
  const at = new Map<Side, number>();
  for (const door of b.doors ?? []) {
    if (door.floor !== 0) continue;
    const side = wallSide(door.wall);
    if (!side) continue;
    if (!sides.includes(side)) sides.push(side);
    if (at.has(side)) continue;
    const q = fromUE(toBuildingFrame(b, door.pos), frame);
    const horizontal = side === 'north' || side === 'south';
    const lo = horizontal ? rect[0][0] : rect[0][1], hi = horizontal ? rect[1][0] : rect[1][1];
    at.set(side, ((horizontal ? q[0] : q[1]) - lo) / Math.max(1e-6, hi - lo));
  }
  if (!sides.length) sides.push('south');
  // the draw behind a door at fraction t is (t - 0.25) / 0.5 (placeExteriorDoors / emitPlanned); clamped to the draw's range
  return { sides, draws: sides.map((s) => Math.min(0.999, Math.max(0, ((at.get(s) ?? 0.5) - 0.25) / 0.5))) };
}

/** The spec of a building as the template that made it gave it (only what the planned path and its BSP fallback read). */
function specOf(b: GenBuilding, sides: Side[]): BuildingSpec {
  const opposite: Record<Side, Side> = { north: 'south', south: 'north', east: 'west', west: 'east' };
  return {
    height: b.height, floors: b.floors, enterable: b.enterable, roof_walkable: b.roof_walkable, set_piece: b.set_piece,
    ...(b.stairs?.some((s) => s.kind === 'stairwell') ? { stairwell: true } : {}),
    doorSides: sides, windowSides: [sides[0], opposite[sides[0]]], partitionRooms: true,
    ...(b.hatches?.length ? { roofHatch: true } : {}),
    ...(b.archetype ? { archetype: b.archetype } : {}),
    // a block keeps its plan pattern (a kit block re-plans as a kit block, an S2.3 block as S2.3)
    ...(b.archetype === 'block' && b.plan?.kind === 'bands' ? { plan_pattern: b.plan.pattern === 'kit' ? 'kit' as const : 'sections' as const } : {}),
  };
}

export interface ReplanPiece {
  building: GenBuilding;
  walls: GenWall[];
  mantle_edges: MantleEdge[];
}

/**
 * The building re-planned with counter `counter` (plan seed |r<counter>), as UE content in the old building's place and rotation. Deterministic: the same building and counter
 * always give the same piece. Throws when the generator refuses the footprint.
 */
export function replanOnce(layout: GenLayout, b: GenBuilding, counter: number): ReplanPiece {
  const frame = frameOf(layout);
  const rect = draftRect(b, frame);
  const { sides, draws } = doorSidesOf(b, rect, frame);
  let k = 0;
  const random = (): number => draws[k++] ?? 0.5;
  const d: Draft = { walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [],
    building_plans: true, plan_reroll: { [b.id]: counter } };
  addBuilding(d, b.id, rect, specOf(b, sides), random);
  const l = finalizeDraftToLayout(d, frame, { name: 'replan', profile: layout.profile, seed: layout.seed, params: layout.params, start: [0, 0], end: [0, 0] });
  const nb = l.buildings.find((x) => x.id === b.id);
  if (!nb) throw new Error(`${b.id}: the re-plan produced no building`);
  const [[x0, y0], [x1, y1]] = nb.footprint;
  const content = { walls: l.walls, buildings: [nb], ladders: [], covers: [], zones: [], ambush_points: [], mantle_edges: l.mantle_edges ?? [] };
  const out = rotateContent(content, [(x0 + x1) / 2, (y0 + y1) / 2] as Vec2, rotationOf(b));
  const built = out.buildings[0];
  return { building: b.role ? { ...built, role: b.role } : built, walls: out.walls, mantle_edges: out.mantle_edges };
}

/** The layout with the building's piece swapped in: the old building (same list position), its walls and its mantle edges go; `dropLadders` (story ladders) go too. */
export function spliceBuilding(layout: GenLayout, id: string, piece: ReplanPiece, dropLadders: ReadonlySet<string> = new Set()): GenLayout {
  const drop = new Set(layout.walls.filter((w) => w.building === id).map((w) => w.id));
  const mantles = (layout.mantle_edges ?? []).filter((m) => m.building !== id).concat(piece.mantle_edges);
  return {
    ...layout,
    buildings: layout.buildings.map((x) => (x.id === id ? piece.building : x)),
    walls: [...layout.walls.filter((w) => !drop.has(w.id)), ...piece.walls],
    ...(layout.ladders ? { ladders: layout.ladders.filter((k) => !dropLadders.has(k.id)) } : {}),
    ...(mantles.length ? { mantle_edges: mantles } : {}),
  };
}

export interface ReplanResult {
  ok: boolean;
  message: string;
  piece?: ReplanPiece;
  /** The counter the accepted plan has (replanCounter of its building). */
  counter?: number;
  /** Counters tried (accepted one included). */
  tries?: number;
}

/**
 * «другой план» for one building: counters `current + 1 ...` are tried until a plan is valid, is a band plan (when the current one is) and differs from the current one.
 * `layout` supplies the frame and the neighbours for the validator; `pristine` is the building WITHOUT its story (the page's StoryState.base copy, or the building itself).
 */
export function replanBuilding(layout: GenLayout, pristine: GenBuilding, maxTries = REPLAN_MAX_TRIES): ReplanResult {
  if (!isReplannable(pristine)) return { ok: false, message: `${pristine.id}: здание не планировалось (включите «Планировки зданий (S2)» и перегенерируйте)` };
  const start = replanCounter(pristine);
  const was = planSignature(pristine);
  let lastWhy = 'нет подходящего плана';
  for (let t = 1; t <= maxTries; t++) {
    const counter = start + t;
    let piece: ReplanPiece;
    try { piece = replanOnce(layout, pristine, counter); } catch (e) { lastWhy = e instanceof Error ? e.message : String(e); continue; }
    if (pristine.plan?.kind === 'bands' && piece.building.plan?.kind !== 'bands') { lastWhy = `план ${counter} ушёл в BSP (${piece.building.plan?.fallback ?? '?'})`; continue; }
    if (planSignature(piece.building) === was) { lastWhy = `план ${counter} совпал с текущим`; continue; }
    const check = validateBuilding(spliceBuilding(layout, pristine.id, piece), piece.building);
    const errors = check.issues.filter((i) => i.severity === 'error');
    if (errors.length) { lastWhy = `план ${counter}: ${errors[0].code}${errors[0].ref ? ` (${errors[0].ref})` : ''}`; continue; }
    return { ok: true, piece, counter, tries: t, message: `${pristine.id}: другой план ${counter}${t > 1 ? ` (после ${t - 1} отброшенных)` : ''}` };
  }
  return { ok: false, tries: maxTries, message: `${pristine.id}: другой план не нашёлся за ${maxTries} попыток (${lastWhy})` };
}
