// Training Ground / level generator v3 - UNITS of a block plan (S2_ROOMS_PLAN.md 3.4 / 3.5 rule 4 / 4.1; step S2.3): which rooms make one flat, the
// doors that are its entries, the balcony column it shares with the flats above and below, and the dead-end tag (a flat with a single entry)
// with the walk `run_cm` from the nearest stair pad to its deepest room. PURE: integer cm, plan frame (see planDoors.ts), cell and opening ids
// of the plan - generate.ts maps them to room ids. Common rooms (stairwell, landing, the ground-storey lobbies) belong to no unit.
import type { BandPlan, PlanCell } from './bandPlan.ts';
import type { PlanOpening } from './planDoors.ts';

export interface PlanUnit {
  /** `s<section><side>`: the flat of one side of a section's core bay (the same on every storey). */
  key: string;
  kind: 'flat';
  storey: number;
  /** The plan cell ids of its rooms (lobbies of storey 0 left out). */
  cells: string[];
  /** The opening ids (plan ids, `f<storey>_d<k>`) of the doors joining the unit to a room outside it. */
  entries: string[];
  /** The stack of the same flat on every storey: balcony slots and a fire escape share it. */
  column: string;
  /** A single entry: nothing but one door leads in (story 17). */
  dead_end?: boolean;
  /** Dead ends only: cm walked from the nearest stair-pad portal through the entry to the middle of the deepest room. */
  run_cm?: number;
}

type Pt = [number, number];
const pointOf = (o: PlanOpening): Pt => (o.axis === 'u' ? [o.c, o.at] : [o.at, o.c]);
const centreOf = (c: PlanCell): Pt => [(c.u0 + c.u1) / 2, (c.v0 + c.v1) / 2];
const dist = (a: Pt, b: Pt): number => Math.hypot(a[0] - b[0], a[1] - b[1]);

/** The walk of a dead-end unit: pad portal -> entry door -> the doors of the breadth-first tree to the deepest room -> its middle. */
function runOf(cells: PlanCell[], doors: PlanOpening[], entry: PlanOpening, inside: string, portals: PlanOpening[]): number {
  const byId = new Map(cells.map((c) => [c.id, c]));
  const parent = new Map<string, { from: string; door: PlanOpening } | null>([[inside, null]]);
  const depth = new Map<string, number>([[inside, 0]]);
  const queue = [inside];
  for (let i = 0; i < queue.length; ++i) {
    const at = queue[i];
    for (const d of doors) {
      if (d === entry || !d.links.includes(at)) continue;
      const to = d.links[0] === at ? d.links[1] : d.links[0];
      if (!byId.has(to) || parent.has(to)) continue;
      parent.set(to, { from: at, door: d });
      depth.set(to, depth.get(at)! + 1);
      queue.push(to);
    }
  }
  // the deepest room: most doors from the entry, then the farthest from the entry door
  const e = pointOf(entry);
  const deepest = [...depth.keys()].sort((x, y) => depth.get(y)! - depth.get(x)! || dist(centreOf(byId.get(y)!), e) - dist(centreOf(byId.get(x)!), e) || (x < y ? -1 : 1))[0];
  const chain: Pt[] = [centreOf(byId.get(deepest)!)];
  for (let at = deepest, p = parent.get(at); p; at = p.from, p = parent.get(at)) chain.unshift(pointOf(p.door));
  const pad = portals.length ? pointOf(portals.reduce((best, q) => (dist(pointOf(q), e) < dist(pointOf(best), e) ? q : best))) : e;
  const route: Pt[] = [pad, e, ...chain];
  let run = 0;
  for (let i = 1; i < route.length; ++i) run += dist(route[i - 1], route[i]);
  return Math.round(run);
}

/**
 * The flats of a block plan on every storey. Doors with a `portal` role (the pad portals) are not entries; a unit is a dead end when exactly one
 * door leads in. Deterministic; units sorted by storey, then key.
 */
export function planUnits(plan: BandPlan, openings: PlanOpening[], lobbies: readonly string[], storeys: number): PlanUnit[] {
  if (plan.archetype !== 'block') return [];
  const out: PlanUnit[] = [];
  // a kit plan names the flat on every cell (`unit`); an S2.3 plan by section + side
  const keyOf = (c: PlanCell): string | null => (plan.pattern === 'kit' ? c.unit ?? null : c.flat !== undefined && c.section !== undefined ? `s${c.section}${c.flat}` : null);
  const keys = [...new Set(plan.cells.map(keyOf).filter((k): k is string => !!k))].sort();
  for (let storey = 0; storey < storeys; ++storey) {
    const doors = openings.filter((o) => o.storey === storey && o.kind === 'door');
    for (const key of keys) {
      const cells = plan.cells.filter((c) => keyOf(c) === key && !(storey === 0 && lobbies.includes(c.id)));
      if (!cells.length) continue;
      const ids = new Set(cells.map((c) => c.id));
      const entries = doors.filter((d) => d.role !== 'portal' && d.links[1] !== 'street' && d.links.filter((l) => ids.has(l)).length === 1);
      const unit: PlanUnit = { key, kind: 'flat', storey, cells: cells.map((c) => c.id), entries: entries.map((d) => d.id), column: key };
      if (entries.length === 1) {
        const inside = entries[0].links.find((l) => ids.has(l))!;
        const portals = doors.filter((d) => d.role === 'portal' && plan.cells.some((c) => c.kind === 'core' && c.section === Number(key.slice(1, -1)) && d.links.includes(c.id)));
        // a kit core has no pad portals: the walk starts at the core's court door / landing door nearest the entry (the floor landing)
        if (!portals.length && plan.pattern === 'kit') portals.push(...doors.filter((d) => plan.cells.some((c) => c.kind === 'core' && c.section === Number(key.slice(1, -1)) && d.links.includes(c.id))));
        unit.dead_end = true;
        unit.run_cm = runOf(cells, doors.filter((d) => d.links.every((l) => ids.has(l))), entries[0], inside, portals);
      }
      out.push(unit);
    }
  }
  return out;
}

/**
 * The role of every door follows the units (S2.9 smoke: 24 role mismatches on the S2.3 ground floors - a flat entry with the role `room`, a
 * `unit_entry` joining a lobby to the landing): a door joining exactly one room of a unit of its storey to a room outside the unit is a
 * `unit_entry`; a door that was planned as `unit_entry` but joins two common rooms (a ground lobby taken out of its flat) or two rooms of one flat
 * becomes a plain `room` door. Portals, exterior doors and windows keep their roles. Mutates `openings`; returns how many roles changed.
 */
export function fixEntryRoles(openings: PlanOpening[], units: readonly PlanUnit[]): number {
  const unitOf = new Map<string, string>();
  for (const u of units) for (const c of u.cells) unitOf.set(`${u.storey}|${c}`, u.key);
  let changed = 0;
  for (const o of openings) {
    if (o.kind !== 'door' || o.role === 'portal' || o.links[1] === 'street') continue;
    const a = unitOf.get(`${o.storey}|${o.links[0]}`), b = unitOf.get(`${o.storey}|${o.links[1]}`);
    const entry = (a === undefined) !== (b === undefined) || (a !== undefined && b !== undefined && a !== b);
    if (entry && o.role !== 'unit_entry') { o.role = 'unit_entry'; ++changed; }
    else if (!entry && o.role === 'unit_entry') { o.role = 'room'; ++changed; }
  }
  return changed;
}
