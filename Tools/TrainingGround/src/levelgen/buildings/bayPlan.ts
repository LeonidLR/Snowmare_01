// Fixed 1-464 section in integer centimetres. u runs along the facade and v=0 faces the court.
import { BAY_SECTIONS, baySectionTemplate } from './kitPlan.ts';

type Room = keyof typeof BAY_SECTIONS.rooms | 'stair' | 'landing' | 'storage';
type FlatRoom = 'bath' | 'living' | 'kitchen' | 'bedroom' | 'storage';
type DoorRole = keyof typeof BAY_SECTIONS.doors_cm | 'service';
type Facade = 'court' | 'street';

interface Rect {
  u0: number;
  u1: number;
  v0: number;
  v1: number;
}

export interface BayCell extends Rect {
  id: string;
  room: Room;
  flat?: 1 | 2 | 3 | 4;
}

export interface BayDoor {
  a: string;
  b: string;
  role: DoorRole;
  axis: 'u' | 'v';
  at: number;
  centre: number;
}

export interface BayPlan {
  template: string;
  length: number;
  depth: number;
  spine: number;
  bays: { u0: number; u1: number }[];
  cells: BayCell[];
  doors: BayDoor[];
  landing: BayCell;
  stair: BayCell;
}

const CORE_BAY = 2;
const HALL_BAND_CM = 240;
const BATH_BAY_CM = 175;
const FACING_STAGGER_CM = 35;

function flatCells(
  flat: 1 | 2 | 3 | 4,
  side: 'low' | 'high',
  facade: Facade,
  bays: BayPlan['bays'],
  spine: number,
  depth: number,
  bedroom: boolean,
): { hall: BayCell; rooms: (BayCell & { room: FlatRoom })[] } {
  const [outer, inner] = side === 'low' ? [bays[0], bays[1]] : [bays[bays.length - 1], bays[CORE_BAY + 1]];
  const u0 = Math.min(outer.u0, inner.u0);
  const u1 = Math.max(outer.u1, inner.u1);
  const low = side === 'low';
  const court = facade === 'court';
  const hallEdge = spine + (court ? -HALL_BAND_CM : HALL_BAND_CM);
  const front = { v0: court ? 0 : hallEdge, v1: court ? hallEdge : depth };
  const back = { v0: Math.min(spine, hallEdge), v1: Math.max(spine, hallEdge) };
  const bathEdge = low ? u0 + BATH_BAY_CM : u1 - BATH_BAY_CM;
  const living = low ? outer : bays[CORE_BAY + 2];
  const cell = <R extends Room>(room: R, box: Rect): BayCell & { room: R } =>
    ({ id: `F${flat}-${room}`, room, flat, ...box });
  const hall = cell('hall', { u0: low ? bathEdge : u0, u1: low ? u1 : bathEdge, ...back });
  const rooms: (BayCell & { room: FlatRoom })[] = [
    cell('bath', { u0: low ? u0 : bathEdge, u1: low ? bathEdge : u1, ...back }),
    cell('living', { ...living, ...front }),
    cell('kitchen', { ...inner, ...front }),
  ];
  if (!low && living.u1 < outer.u1) {
    rooms.push(cell(bedroom ? 'bedroom' : 'storage', { ...outer, ...front }));
  }
  return { hall, rooms };
}

function doorBetween(a: BayCell, b: BayCell, role: DoorRole, preferred?: number): BayDoor {
  const vertical = a.u1 === b.u0 || b.u1 === a.u0;
  const start = vertical ? Math.max(a.v0, b.v0) : Math.max(a.u0, b.u0);
  const end = vertical ? Math.min(a.v1, b.v1) : Math.min(a.u1, b.u1);
  const centre = preferred ?? Math.floor((start + end) / 2);
  return {
    a: a.id, b: b.id, role, centre,
    axis: vertical ? 'v' : 'u',
    at: vertical ? Math.max(a.u0, b.u0) : Math.max(a.v0, b.v0),
  };
}

/** Interior of one section. The generator can rotate it into any facade frame. */
export function planBaySection(template: string): BayPlan | undefined {
  const config = baySectionTemplate(template);
  if (!config) return undefined;

  let length = 0;
  const bays = config.bays_cm.map((width) => {
    const u0 = length;
    length += width;
    return { u0, u1: length };
  });
  const depth = BAY_SECTIONS.depth_cm;
  const spine = depth / 2;
  const core = bays[CORE_BAY];
  const wall = BAY_SECTIONS.walls_cm.bearing;
  const landingEnd = spine + BAY_SECTIONS.core.landing_past_spine_cm + wall / 2;
  const landingStart = landingEnd - BAY_SECTIONS.core.floor_landing_clear_cm[1] - wall;
  const stair: BayCell = { id: 'stair', room: 'stair', ...core, v0: 0, v1: landingStart };
  const landing: BayCell = { id: 'landing', room: 'landing', ...core, v0: landingStart, v1: landingEnd };
  const service: BayCell = { id: 'common-service', room: 'storage', ...core, v0: landingEnd, v1: depth };
  const cells = [stair, landing, service];
  const doors = [doorBetween(landing, service, 'service')];
  const firstCentre = spine - Math.floor(BAY_SECTIONS.flat_door_spacing_cm / 2);

  for (const flat of [1, 2, 3, 4] as const) {
    const low = flat % 2 === 1;
    const court = flat <= 2;
    const { hall, rooms } = flatCells(
      flat, low ? 'low' : 'high', court ? 'court' : 'street', bays, spine, depth,
      flat === 2 && config.flat_two_living_rooms > 1,
    );
    cells.push(hall, ...rooms);
    const offset = (court ? 0 : BAY_SECTIONS.flat_door_spacing_cm) - (low ? 0 : FACING_STAGGER_CM);
    doors.push(doorBetween(landing, hall, 'flat', firstCentre + offset));
    for (const room of rooms) {
      doors.push(doorBetween(hall, room, room.room === 'storage' ? 'service' : room.room));
    }
  }

  return { template, length, depth, spine, bays, cells, doors, landing, stair };
}

/** 50 cm sampling for reviewing room assignments and facade orientation in the terminal. */
export function bayPlanAscii(plan: BayPlan): string {
  const glyphs: Record<Room, string> = {
    living: 'L', bedroom: 'B', kitchen: 'K', bath: 'b', hall: '.', tambour: 'T', service: 'S', storage: 's', stair: '^', landing: '=',
  };
  const rows = [`${plan.template} ${plan.length}x${plan.depth} cm (court at top)`];
  for (let v = 25; v < plan.depth; v += 50) {
    let row = '';
    for (let u = 25; u < plan.length; u += 50) {
      const cell = plan.cells.find((c) => c.u0 <= u && u < c.u1 && c.v0 <= v && v < c.v1);
      row += cell ? glyphs[cell.room] : ' ';
    }
    rows.push(row);
  }
  return rows.join('\n');
}
