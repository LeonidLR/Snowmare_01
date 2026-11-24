import { BUILDING_PLANS, type BandPlan, type PlanCell } from './bandPlan.ts';
import type { BayPlan } from './bayPlan.ts';
import { BAY_SECTIONS, type GableChoice, type KitGable } from './kitPlan.ts';
import type { PlanDoorsResult, PlanOpening } from './planDoors.ts';
import type { PlanWindowsOpts } from './planWindows.ts';
import type { DoorState } from '../schema.ts';

const isGable = (end: 'low' | 'high', gable: GableChoice) => gable === end || gable === 'both';

/** Adapt fixed rooms to the existing emitter and door JSON contract. */
export function bayBuildingPlan(
  bay: BayPlan,
  storeys: number,
  gable: GableChoice = 'low',
  gableState?: DoorState,
): { plan: BandPlan; doors: PlanDoorsResult } {
  const cells = bay.cells.flatMap((cell): PlanCell[] => {
    if (cell.room === 'stair') return [];
    const core = cell.room === 'landing';
    const hall = cell.room === 'hall';
    const storage = cell.room === 'storage';
    return [{
      id: cell.id, u0: cell.u0, u1: cell.u1, v0: core ? bay.stair.v0 : cell.v0, v1: cell.v1,
      kind: core ? 'core' : hall ? 'corridor' : storage ? 'service' : 'room',
      band: 'full',
      ...(cell.flat ? { unit: `s0F${cell.flat}` } : {}),
      use: cell.room === 'storage' ? 'service' : cell.room,
    }];
  });
  // The court-side half-landing sits over the stair bay. Both ground entrances
  // reach the floor landing through the neighbouring service bay instead.
  const mainFlat = gable === 'high' ? 2 : 1;
  const entryCell = bay.cells.find((cell) => cell.flat === mainFlat && cell.room === 'kitchen')!;
  const service = new Set(cells.filter((cell) => cell.unit === `s0F${mainFlat}`).map((cell) => cell.id));
  const gables: KitGable[] = [];
  for (const [end, wall, flat] of [['low', 'u0', 1], ['high', 'uL', 2]] as const) {
    if (!isGable(end, gable)) continue;
    const room = bay.cells.find((cell) => cell.flat === flat &&
      (cell.room === 'bedroom' || cell.room === 'living') &&
      (wall === 'u0' ? cell.u0 === 0 : cell.u1 === bay.length))!;
    const unit = `s0F${flat}`;
    const rooms = cells.filter((cell) => cell.unit === unit).map((cell) => cell.id);
    rooms.forEach((id) => service.add(id));
    gables.push({
      wall, section: 0, unit, cells: rooms, door: room.id, c: (room.v0 + room.v1) / 2,
      type: 'service', labels: { [room.id]: 'Дворницкая' }, ...(gableState ? { state: gableState } : {}),
    });
  }

  const openings: PlanOpening[] = [];
  for (let storey = 0; storey < storeys; storey++) {
    bay.doors.forEach((door, index) => {
      const width = door.role === 'service' ? BAY_SECTIONS.doors_cm.bath : BAY_SECTIONS.doors_cm[door.role];
      openings.push({
        id: `f${storey}_d${index}`, storey, kind: 'door', role: door.role === 'flat' ? 'unit_entry' : 'room',
        module: 'wall_door_200', axis: door.axis, at: door.at, c: door.centre, width, links: [door.a, door.b],
      });
    });
  }
  openings.push({
    id: 'f0_d_court', storey: 0, kind: 'door', role: 'entry', module: 'wall_door_300', axis: 'u', wall: 'v0', at: 0,
    c: (entryCell.u0 + entryCell.u1) / 2, width: BAY_SECTIONS.doors_cm.entry, links: [entryCell.id, 'street'],
  });
  for (const g of gables) {
    openings.push({
      id: `f0_d_gable_${g.wall}`, storey: 0, kind: 'door', role: 'entry', module: 'wall_door_300', axis: 'v',
      wall: g.wall, at: g.wall === 'u0' ? 0 : bay.length, c: g.c, width: BAY_SECTIONS.doors_cm.entry,
      links: [g.door, 'street'], ...(g.state ? { state: g.state } : {}),
    });
  }

  return {
    plan: {
      kind: 'bands', pattern: 'kit', archetype: 'block', L: bay.length, D: bay.depth, bays: [], cells,
      kit: { sections: [], doors: [], notes: [], template: bay.template, gables, balconyColumns: ['s0F3', 's0F4'] },
    },
    doors: { openings, lobbies: [entryCell.id, ...gables.map((g) => g.door)], service: [...service], warnings: [] },
  };
}

// A balcony bay reserves 60 cm between the door and its neighbouring window,
// matching the existing fire-escape planner's opening clearance.
export function bayWindows(plan: BandPlan, opts: PlanWindowsOpts): PlanOpening[] {
  const out: PlanOpening[] = [];
  const widths = BAY_SECTIONS.windows_cm;
  const jamb = BUILDING_PLANS.constants.openings.window_jamb_cm;
  for (let storey = 0; storey < opts.storeys; storey++) {
    let index = 0;
    for (const cell of plan.cells) {
      if (cell.use !== 'living' && cell.use !== 'bedroom' && cell.use !== 'kitchen') continue;
      const wall = cell.v0 === 0 ? 'v0' : cell.v1 === plan.D ? 'vD' : undefined;
      if (!wall) continue;
      const at = wall === 'v0' ? 0 : plan.D;
      const centre = (cell.u0 + cell.u1) / 2;
      const fits = (c: number, width: number): boolean => {
        const half = width / 2;
        return !(opts.blocked?.[wall] ?? []).some(([a, b]) => c + half > a && c - half < b)
          && !opts.doors.some((d) => d.storey === storey && d.wall === wall && Math.abs(d.c - c) < d.width / 2 + half + jamb);
      };
      const emit = (c: number, width: number, balcony = false): void => {
        out.push({
          id: `f${storey}_w${index++}`, storey, kind: 'window', role: 'window', module: 'wall_window_200',
          axis: 'u', at, c, width, links: [cell.id, 'street'], wall, ...(balcony ? { slot: 'balcony' } : {}),
        });
      };
      const balcony = storey > 0 && wall === 'vD' && cell.use === 'living' && !!cell.unit
        && plan.kit?.balconyColumns?.includes(cell.unit);
      if (balcony) {
        const wing = (widths.balcony_door + jamb + widths.balcony_window) / 2;
        const towardInterior = centre < plan.L / 2 ? 1 : -1;
        const doorCentre = centre + towardInterior * (wing - widths.balcony_door / 2);
        const windowCentre = centre - towardInterior * (wing - widths.balcony_window / 2);
        if (fits(doorCentre, widths.balcony_door) && fits(windowCentre, widths.balcony_window)) {
          emit(doorCentre, widths.balcony_door, true);
          emit(windowCentre, widths.balcony_window);
          continue;
        }
      }
      if (fits(centre, widths.standard)) emit(centre, widths.standard);
    }
  }
  return out;
}
