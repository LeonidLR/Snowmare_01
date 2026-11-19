// Training Ground / level generator v3 - DOORS and WINDOWS of a band plan, tests (S2_ROOMS_PLAN.md S2.2 piece A, planDoors.ts / planWindows.ts):
// a property test over the bandPlan.test.ts footprints / core placements / seeds (every 9th seed of 1-200, all 200 for two footprints),
// storeys 1 and 2 (2 only with a core), one and two exterior doors: grid, links, connectivity, loops, jamb distances, core openings only
// the two portals, window clearances, determinism - plus two hand fixtures with exact openings. `npm test`. Pure.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { BandPlan, BandPlanInput, PlanCell } from './bandPlan.ts';
import { BUILDING_PLANS, planBands, planToAscii, snapFootprint } from './bandPlan.ts';
import type { PlanDoorsOpts, PlanOpening, WallKey } from './planDoors.ts';
import { planDoors } from './planDoors.ts';
import { planWindows } from './planWindows.ts';

type Core = BandPlanInput['core'];
const COREBAY = BUILDING_PLANS.constants.core.bay_length_cm;
const MIN_ROOM = BUILDING_PLANS.constants.room_cm.min;
const O = BUILDING_PLANS.constants.openings;
const stretchOk = (x: number): boolean => x === 0 || x >= MIN_ROOM;

/** The footprints of the property test, once each (copy of bandPlan.test.ts). */
function footprints(): { L: number; D: number }[] {
  const seen = new Map<string, { L: number; D: number }>();
  for (let w = 800; w <= 1800; w += 100) {
    for (let d = 900; d <= 1400; d += 100) {
      const a = snapFootprint(w, d);
      seen.set(`${a.L}x${a.D}`, { L: a.L, D: a.D });
    }
  }
  return [...seen.values()];
}

/** The core placements that fit a long side: none, at 0, at the far end, nearest the middle; both depths (copy of bandPlan.test.ts). */
function cores(L: number): { label: string; core: Core }[] {
  const out: { label: string; core: Core }[] = [{ label: 'none', core: null }];
  const valid: number[] = [];
  for (let u0 = 0; u0 + COREBAY <= L; u0 += 100) if (stretchOk(u0) && stretchOk(L - u0 - COREBAY)) valid.push(u0);
  if (!valid.length) return out;
  const middle = (L - COREBAY) / 2;
  const mid = valid.reduce((best, u0) => (Math.abs(u0 - middle) < Math.abs(best - middle) ? u0 : best), valid[0]);
  for (const depth of [350, 200] as const) {
    const seenU0 = new Set<number>();
    for (const [name, u0] of [['start', 0], ['end', L - COREBAY], ['mid', mid]] as const) {
      if (seenU0.has(u0) || !valid.includes(u0)) continue;
      seenU0.add(u0);
      out.push({ label: `${depth}@${name}`, core: { u0, depth } });
    }
  }
  return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Independent checkers (they do not share code with planDoors.ts)

const cellOf = (plan: BandPlan, id: string): PlanCell => plan.cells.find((c) => c.id === id)!;
const isRoomy = (c: PlanCell): boolean => c.kind === 'room' || c.kind === 'end';

/** Two corridor cells that touch along an edge: an open boundary, no wall. */
function seamPairs(plan: BandPlan): [string, string][] {
  const out: [string, string][] = [];
  const cs = plan.cells.filter((c) => c.kind === 'corridor');
  for (let i = 0; i < cs.length; ++i) {
    for (let j = i + 1; j < cs.length; ++j) {
      const a = cs[i], b = cs[j];
      const ou = Math.min(a.u1, b.u1) - Math.max(a.u0, b.u0), ov = Math.min(a.v1, b.v1) - Math.max(a.v0, b.v0);
      if ((ou === 0 && ov > 0) || (ov === 0 && ou > 0)) out.push([a.id, b.id]);
    }
  }
  return out;
}

/** The crossing wall axes along a door's wall line: cell edges on either side minus corridor seams, plus the corners. */
function crossingAxes(plan: BandPlan, facts: Facts, axis: 'u' | 'v', at: number): number[] {
  const hit = facts.cross.get(`${axis}${at}`);
  if (hit) return hit;
  const seam = new Set(facts.seams.flat());
  const xs = new Set<number>(axis === 'u' ? [0, plan.L] : [0, plan.D]);
  for (const c of plan.cells) {
    const touches = axis === 'u' ? c.v0 === at || c.v1 === at : c.u0 === at || c.u1 === at;
    if (!touches) continue;
    for (const e of axis === 'u' ? [c.u0, c.u1] : [c.v0, c.v1]) {
      // A corridor edge shared with another corridor cell is no wall.
      const open = seam.has(c.id) && plan.cells.some((o) => o !== c && o.kind === 'corridor' && c.kind === 'corridor'
        && (axis === 'u' ? (o.u0 === e && c.u1 === e) || (o.u1 === e && c.u0 === e) : (o.v0 === e && c.v1 === e) || (o.v1 === e && c.v0 === e)));
      if (!open) xs.add(e);
    }
  }
  const list = [...xs];
  facts.cross.set(`${axis}${at}`, list);
  return list;
}

/** What the checks of one plan reuse. */
interface Facts { seams: [string, string][]; cross: Map<string, number[]> }

/** Cycle in the multigraph after pruning degree <= 1 nodes repeatedly. */
function hasCycle(nodes: string[], edges: [string, string][]): boolean {
  const alive = new Set(nodes);
  const deg = (n: string): number => edges.filter(([a, b]) => alive.has(a) && alive.has(b) && (a === n || b === n)).length;
  for (let changed = true; changed;) {
    changed = false;
    for (const n of [...alive]) if (deg(n) <= 1) { alive.delete(n); changed = true; }
  }
  return alive.size > 0;
}

function components(nodes: string[], edges: [string, string][]): number {
  const parent = new Map(nodes.map((n) => [n, n]));
  const find = (x: string): string => (parent.get(x) === x ? x : (parent.set(x, find(parent.get(x)!)), parent.get(x)!));
  for (const [a, b] of edges) parent.set(find(a), find(b));
  return new Set(nodes.map(find)).size;
}

const stats = { loopPlans: 0, loopForced: 0 };
const tally = { noLoopPossible: 0, plans: 0, runs: 0, doors: 0, windows: 0, exteriorFallbacks: 0, exteriorNone: 0, relaxed: 0, noCycleCorridorless: 0, noCoreFreeCycle: 0, warnOther: 0 };

function checkRun(plan: BandPlan, facts: Facts, label: string, core: Core, seed: number, storeys: number, doorWalls: PlanDoorsOpts['doorWalls']): void {
  const opts: PlanDoorsOpts = { storeys, seed, core: core ? { u0: core.u0, depth: core.depth } : null, doorWalls, stats };
  const res = planDoors(plan, opts);
  const again = planDoors(plan, { ...opts, stats: undefined });
  assert.deepEqual(again, res, `doors determinism ${label}`);
  const wins = planWindows(plan, { storeys, doors: res.openings });
  assert.deepEqual(planWindows(plan, { storeys, doors: res.openings }), wins, `windows determinism ${label}`);
  ++tally.runs; tally.doors += res.openings.length; tally.windows += wins.length;
  for (const w of res.warnings) {
    if (w.startsWith('exterior_door_fallback')) ++tally.exteriorFallbacks;
    else if (w.startsWith('exterior_door_none')) ++tally.exteriorNone;
    else if (w.startsWith('door_jamb_relaxed')) ++tally.relaxed;
    else { ++tally.warnOther; assert.fail(`${label}: warning ${w}\n${planToAscii(plan)}`); }
  }
  const check = (ok: boolean, what: string): void => { if (!ok) assert.fail(`${label} ${what}
${planToAscii(plan)}
${JSON.stringify(res)}`); };
  const relaxed = new Set(res.warnings.filter((w) => w.startsWith('door_jamb_relaxed:')).map((w) => w.split(':')[1]));
  const ids = new Set<string>();
  for (const o of [...res.openings, ...wins]) {
    check(!ids.has(o.id), `duplicate id ${o.id}`); ids.add(o.id);
    check(o.c % 50 === 0, `${o.id} centre ${o.c} off the 50 grid`);
  }
  const core0 = plan.cells.find((c) => c.kind === 'core');
  const rooms = plan.cells.filter(isRoomy).length;
  const corridorless = plan.variant === 'corridorless';

  for (let storey = 0; storey < storeys; ++storey) {
    const doors = res.openings.filter((o) => o.storey === storey);
    // Every non-core, non-corridor cell has a door; the core cell has exactly the two portals and nothing else.
    for (const c of plan.cells) {
      const mine = doors.filter((d) => d.links[0] === c.id || d.links[1] === c.id);
      if (c.kind === 'core') check(mine.length === 2 && mine.every((d) => d.role === 'portal'), `core ${c.id} openings`);
      else if (c.kind !== 'corridor') check(mine.length >= 1, `${c.id} has no door (storey ${storey})`);
    }
    const wallsOfCore = new Set<string>(core0 ? [core0.id] : []);
    for (const o of [...doors, ...wins.filter((w) => w.storey === storey)]) {
      if (o.role !== 'portal') check(!o.links.some((l) => wallsOfCore.has(l)), `${o.id} on a core cell`);
    }
    // Connectivity: cells (+ street) joined by door links and corridor seams are one component.
    const edges: [string, string][] = [...doors.map((d): [string, string] => [d.links[0], d.links[1]]), ...facts.seams];
    const nodes = plan.cells.map((c) => c.id);
    if (doors.some((d) => d.links[1] === 'street')) nodes.push('street');
    check(components(nodes, edges) === 1, `storey ${storey} not connected`);
    // Loops: >= 1 cycle per storey with >= 3 rooms (street node only for corridor-less plans) - where the rule can place one: two
    // neighbouring rooms of one band (never an end suite, never service) with a wall between them.
    if (rooms >= 3) {
      const g = corridorless ? edges : edges.filter(([a, b]) => a !== 'street' && b !== 'street');
      const pairs = plan.cells.some((a) => a.kind === 'room' && (a.band === 'back' || a.band === 'front') && plan.cells.some((b) => b !== a && b.kind === 'room' && b.band === a.band && b.u0 === a.u1 && b.v0 < a.v1 && a.v0 < b.v1));
      const single = corridorless && plan.cells.every((c) => c.band === 'full');
      if (single) { if (!hasCycle(nodes, g)) { if (single) ++tally.noCycleCorridorless; else ++tally.noLoopPossible; } } // no same-band pair / a row of rooms is a path
      else {
        check(hasCycle(nodes, g), `storey ${storey} has no cycle`);
        if (!corridorless) {
          const noCore = g.filter(([a, b]) => cellOf(plan, a)?.kind !== 'core' && cellOf(plan, b)?.kind !== 'core');
          if (!hasCycle(nodes.filter((n) => cellOf(plan, n)?.kind !== 'core'), noCore)) ++tally.noCoreFreeCycle;
        }
      }
    }
    // Interior doors: jamb distance from the crossing axes (not end / corridor end / portal / a warned relaxed service door).
    for (const d of doors) {
      if (d.wall || d.role === 'portal') continue;
      const [a, b] = d.links.map((l) => cellOf(plan, l));
      check(d.width === O.door_cm && d.module === 'wall_door_200', `${d.id} size / module`);
      if (a.kind === 'end' || b.kind === 'end' || relaxed.has(a.id)) continue;
      const xs = crossingAxes(plan, facts, d.axis, d.at);
      check(xs.every((x) => Math.abs(d.c - x) >= O.jamb_cm), `${d.id} centre ${d.c} within ${O.jamb_cm} of a crossing axis (${xs.join(',')})`);
    }
    // Exterior doors: storey 0 only, on a cell touching their wall.
    const ext = doors.filter((d) => d.wall);
    if (storey > 0) check(ext.length === 0, 'exterior door above storey 0');
    else {
      check(ext.length + res.warnings.filter((w) => w.startsWith('exterior_door_none')).length === doorWalls.length, 'exterior door count');
      ext.forEach((d, i) => {
        assert.equal(d.role, 'entry');
        assert.equal(d.module, i === 0 ? 'wall_door_300' : 'wall_door_200');
        const cell = cellOf(plan, d.links[0]);
        const touches = d.wall === 'v0' ? cell.v0 === 0 : d.wall === 'vD' ? cell.v1 === plan.D : d.wall === 'u0' ? cell.u0 === 0 : cell.u1 === plan.L;
        check(touches && cell.kind !== 'core' && (cell.kind === 'corridor' ? !res.lobbies.includes(cell.id) : res.lobbies.includes(cell.id)), `${d.id} slot`);
      });
    }
    // Windows: clearances and which cells carry them.
    const ws = wins.filter((w) => w.storey === storey);
    const extDoors = ext;
    for (const w of ws) {
      const cell = cellOf(plan, w.links[0]);
      check(cell.kind === 'room' || cell.kind === 'end' || cell.kind === 'corridor', `${w.id} in a ${cell.kind}`);
      const along = w.axis === 'u' ? [cell.u0, cell.u1] : [cell.v0, cell.v1];
      const exempt = cell.kind === 'corridor';
      const edgeGap = exempt ? 0 : O.window_clear_cm;
      check(w.c - O.window_cm / 2 >= along[0] + edgeGap && w.c + O.window_cm / 2 <= along[1] - edgeGap, `${w.id} centre ${w.c} too near its partition`);
      for (const d of extDoors.filter((x) => x.wall === w.wall)) check(Math.abs(w.c - d.c) >= O.window_cm / 2 + d.width / 2 + edgeGap + (exempt ? O.window_jamb_cm : 0), `${w.id} too near door ${d.id}`);
      for (const o of ws) if (o !== w && o.wall === w.wall) check(Math.abs(w.c - o.c) >= O.window_cm + O.window_jamb_cm, `${w.id} too near ${o.id}`);
    }
    // 3.6: every corridor end on an exterior wall has its window unless an exterior door (exit) is in that corridor end.
    for (const c of plan.cells.filter((x) => x.kind === 'corridor')) {
      for (const wall of ['u0', 'uL'] as WallKey[]) {
        if ((wall === 'u0' ? c.u0 : c.u1) !== (wall === 'u0' ? 0 : plan.L)) continue;
        const has = ws.some((w) => w.links[0] === c.id && w.wall === wall);
        check(has === !extDoors.some((d) => d.wall === wall && d.links.includes(c.id)), `corridor ${c.id} end window on ${wall}`);
      }
    }
  }
}

const tOf = (seed: number, k: number): number => 0.25 + 0.5 * (((seed * 37 + k * 53) % 100) / 100);

test('CodexTactics.LevelGen.PlanDoors.Property: doors and windows of every sampled plan hold the rules', () => {
  const started = Date.now();
  const FULL = new Set(['1800x1200', '1200x900']);
  for (const { L, D } of footprints()) {
    for (const { label, core } of cores(L)) {
      for (let seed = 1; seed <= 200; seed += FULL.has(`${L}x${D}`) ? 1 : 9) {
        const plan = planBands({ L, D, archetype: 'office', core, seed });
        if (plan.kind !== 'bands') continue;
        ++tally.plans;
        const facts: Facts = { seams: seamPairs(plan), cross: new Map() };
        const name = `${L}x${D} ${label} seed ${seed}`;
        checkRun(plan, facts, `${name} [vD]`, core, seed, core ? 2 : 1, [{ wall: 'vD', t: tOf(seed, 0) }]);
        checkRun(plan, facts, `${name} [vD,v0]`, core, seed, core ? 2 : 1, [{ wall: 'vD', t: tOf(seed, 0) }, { wall: 'v0', t: tOf(seed, 1) }]);
        if (core) {
          // One storey is the first storey of two.
          const one = planDoors(plan, { storeys: 1, seed, core, doorWalls: [{ wall: 'vD', t: tOf(seed, 0) }] });
          const two = planDoors(plan, { storeys: 2, seed, core, doorWalls: [{ wall: 'vD', t: tOf(seed, 0) }] });
          assert.deepEqual(two.openings.filter((o) => o.storey === 0), one.openings, `${name} storey 0 is storey-count independent`);
        }
      }
    }
  }
  const ms = Date.now() - started;
  const share = stats.loopPlans ? Math.round((100 * stats.loopForced) / stats.loopPlans) : 0;
  console.log(`plan doors: ${tally.plans} plans, ${tally.runs} runs, ${tally.doors} doors/portals, ${tally.windows} windows, ${ms} ms; `
    + `enfilade forced ${stats.loopForced}/${stats.loopPlans} storey-plans (${share}%); exterior-door fallback warnings ${tally.exteriorFallbacks}, no door ${tally.exteriorNone}; `
    + `relaxed service jambs ${tally.relaxed}; corridor-less single-row plans without a loop ${tally.noCycleCorridorless}; storeys with >= 3 rooms but no same-band pair ${tally.noLoopPossible}; plans whose loop needs the core portals ${tally.noCoreFreeCycle}`);
  assert.ok(tally.plans > 1000);
  assert.equal(tally.noCoreFreeCycle, 0);
  assert.equal(tally.noLoopPossible, 0, 'every corridor storey with >= 3 rooms has a loop (an end suite joins it when no same-band pair exists)');
});

// ---------------------------------------------------------------------------------------------------------------------------------
// Hand fixtures. Plans are written out cell by cell (ids as bandPlan.finish numbers them: by u0, v0, u1, v1).

const cell = (id: string, kind: PlanCell['kind'], band: PlanCell['band'], u0: number, u1: number, v0: number, v1: number): PlanCell => ({ id, kind, band, u0, u1, v0, v1 });
/** A compact view of an opening: `id role c@axis=at links wall`. */
const show = (o: PlanOpening): string => `${o.id} ${o.role} ${o.module} c${o.c} ${o.axis}=${o.at} ${o.links.join('>')}${o.wall ? ` ${o.wall}` : ''}`;

test('CodexTactics.LevelGen.PlanDoors.Fixture: double-loaded 1300 x 1000 with a two-lane core at 0, two storeys', () => {
  // v 600-1000 front | 350-600 corridor over the core bay (0-900) / 400-600 corridor run (900-1300) | 0-400 back.
  const plan: BandPlan = {
    kind: 'bands', L: 1300, D: 1000, variant: 'double',
    bays: [{ kind: 'core', u0: 0, u1: 900 }, { kind: 'room', u0: 900, u1: 1300 }],
    cells: [
      cell('c0', 'core', 'back', 0, 900, 0, 350), cell('c1', 'corridor', 'corridor', 0, 900, 350, 600),
      cell('c2', 'room', 'front', 0, 400, 600, 1000), cell('c3', 'room', 'front', 400, 900, 600, 1000),
      cell('c4', 'room', 'back', 900, 1300, 0, 400), cell('c5', 'corridor', 'corridor', 900, 1300, 400, 600),
      cell('c6', 'room', 'front', 900, 1300, 600, 1000),
    ],
  };
  const res = planDoors(plan, { storeys: 2, seed: 7, core: { u0: 0, depth: 350 }, doorWalls: [{ wall: 'vD', t: 0.3 }] });
  assert.deepEqual(res.warnings, []);
  assert.deepEqual(res.lobbies, ['c2']);
  assert.deepEqual(res.openings.map(show), EXPECTED_DOUBLE);
  const wins = planWindows(plan, { storeys: 2, doors: res.openings });
  assert.deepEqual(wins.map(show), EXPECTED_DOUBLE_WINDOWS);
});

test('CodexTactics.LevelGen.PlanDoors.Fixture: single-loaded 1000 x 800 without a core, one storey, a blocked wall stretch', () => {
  // v 400-800 front | 200-400 corridor | 0-200 service (300 wide rooms cannot keep the 200 jamb: relaxed, warned).
  const plan: BandPlan = {
    kind: 'bands', L: 1000, D: 800, variant: 'single',
    bays: [{ kind: 'room', u0: 0, u1: 400 }, { kind: 'room', u0: 400, u1: 1000 }],
    cells: [
      cell('c0', 'service', 'back', 0, 400, 0, 200), cell('c1', 'corridor', 'corridor', 0, 1000, 200, 400), cell('c2', 'room', 'front', 0, 400, 400, 800),
      cell('c3', 'service', 'back', 400, 700, 0, 200), cell('c4', 'room', 'front', 400, 1000, 400, 800), cell('c5', 'service', 'back', 700, 1000, 0, 200),
    ],
  };
  const res = planDoors(plan, { storeys: 1, seed: 3, core: null, doorWalls: [{ wall: 'vD', t: 0.5 }, { wall: 'vD', t: 0.5 }] });
  assert.deepEqual(res.warnings, ['door_jamb_relaxed:c3', 'door_jamb_relaxed:c5']);
  assert.deepEqual(res.lobbies, ['c2', 'c4']);
  assert.deepEqual(res.openings.map(show), EXPECTED_SINGLE);
  const wins = planWindows(plan, { storeys: 1, doors: res.openings, blocked: { u0: [[500, 700]] } });
  assert.deepEqual(wins.map(show), EXPECTED_SINGLE_WINDOWS);
  // The same call without the blocked stretch keeps the u0 front-room window.
  assert.ok(planWindows(plan, { storeys: 1, doors: res.openings }).length > wins.length);
});

test('CodexTactics.LevelGen.PlanDoors.Fallback: a wall without a slot moves the door to the next wall and warns', () => {
  // The core wall (v = 0) holds only 300 wide service rooms: no slot there, so the door goes to vD with a warning.
  const plan: BandPlan = {
    kind: 'bands', L: 900, D: 800, variant: 'single',
    bays: [],
    cells: [
      cell('c0', 'service', 'back', 0, 300, 0, 200), cell('c1', 'corridor', 'corridor', 0, 900, 200, 400), cell('c2', 'room', 'front', 0, 400, 400, 800),
      cell('c3', 'service', 'back', 300, 600, 0, 200), cell('c4', 'room', 'front', 400, 900, 400, 800), cell('c5', 'service', 'back', 600, 900, 0, 200),
    ],
  };
  const res = planDoors(plan, { storeys: 1, seed: 1, core: null, doorWalls: [{ wall: 'v0', t: 0.5 }] });
  assert.deepEqual(res.warnings, ['door_jamb_relaxed:c0', 'door_jamb_relaxed:c3', 'door_jamb_relaxed:c5', 'exterior_door_fallback:v0->vD']);
  assert.deepEqual(res.openings.filter((o) => o.wall).map(show), ['f0_d5 entry wall_door_300 c600 u=800 c4>street vD']);
  assert.deepEqual(res.lobbies, ['c4']);
  // No slot anywhere (a plan whose every exterior cell is a core): the door is dropped with a warning.
  const bare: BandPlan = { kind: 'bands', L: 900, D: 400, variant: 'single', bays: [], cells: [cell('c0', 'core', 'back', 0, 900, 0, 350), cell('c1', 'corridor', 'corridor', 0, 900, 350, 400)] };
  const none = planDoors(bare, { storeys: 1, seed: 1, core: null, doorWalls: [{ wall: 'v0', t: 0.5 }] });
  assert.ok(none.warnings.includes('exterior_door_none:v0'));
  const fb = planDoors({ ...plan, kind: 'fallback', cells: [] }, { storeys: 1, seed: 1, core: null, doorWalls: [] });
  assert.deepEqual(fb, { openings: [], lobbies: [], warnings: ['plan_fallback'] });
});

// Expected openings, checked by hand against the rules (crossing axes, 200 jamb, 50 grid, facing, legacy t); fixed by seed 7 / seed 3.
const EXPECTED_DOUBLE: string[] = [
  'f0_d0 room wall_door_200 c200 u=600 c2>c1',
  'f0_d1 room wall_door_200 c600 u=600 c3>c1',
  'f0_d2 room wall_door_200 c1100 u=400 c4>c5',
  'f0_d3 room wall_door_200 c1100 u=600 c6>c5',
  'f0_d4 enfilade wall_door_200 c800 v=400 c2>c3',
  'f0_d5 portal portal_200 c100 u=350 c0>c1',
  'f0_d6 portal portal_200 c750 u=350 c0>c1',
  'f0_d7 entry wall_door_300 c200 u=1000 c2>street vD',
  'f1_d0 room wall_door_200 c200 u=600 c2>c1',
  'f1_d1 room wall_door_200 c600 u=600 c3>c1',
  'f1_d2 room wall_door_200 c1100 u=400 c4>c5',
  'f1_d3 room wall_door_200 c1100 u=600 c6>c5',
  'f1_d4 enfilade wall_door_200 c800 v=400 c2>c3',
  'f1_d5 portal portal_200 c100 u=350 c0>c1',
  'f1_d6 portal portal_200 c750 u=350 c0>c1',
];
const EXPECTED_DOUBLE_WINDOWS: string[] = [
  'f0_w0 window wall_window_200 c500 v=0 c1>street u0',
  'f0_w1 window wall_window_200 c800 v=0 c2>street u0',
  'f0_w2 window wall_window_200 c650 u=1000 c3>street vD',
  'f0_w3 window wall_window_200 c1100 u=0 c4>street v0',
  'f0_w4 window wall_window_200 c200 v=1300 c4>street uL',
  'f0_w5 window wall_window_200 c500 v=1300 c5>street uL',
  'f0_w6 window wall_window_200 c1100 u=1000 c6>street vD',
  'f0_w7 window wall_window_200 c800 v=1300 c6>street uL',
  'f1_w0 window wall_window_200 c500 v=0 c1>street u0',
  'f1_w1 window wall_window_200 c200 u=1000 c2>street vD',
  'f1_w2 window wall_window_200 c800 v=0 c2>street u0',
  'f1_w3 window wall_window_200 c650 u=1000 c3>street vD',
  'f1_w4 window wall_window_200 c1100 u=0 c4>street v0',
  'f1_w5 window wall_window_200 c200 v=1300 c4>street uL',
  'f1_w6 window wall_window_200 c500 v=1300 c5>street uL',
  'f1_w7 window wall_window_200 c1100 u=1000 c6>street vD',
  'f1_w8 window wall_window_200 c800 v=1300 c6>street uL',
];
const EXPECTED_SINGLE: string[] = [
  'f0_d0 room wall_door_200 c200 u=200 c0>c1',
  'f0_d1 room wall_door_200 c200 u=400 c2>c1',
  'f0_d2 room wall_door_200 c550 u=200 c3>c1',
  'f0_d3 room wall_door_200 c650 u=400 c4>c1',
  'f0_d4 room wall_door_200 c850 u=200 c5>c1',
  'f0_d5 entry wall_door_300 c600 u=800 c4>street vD',
  'f0_d6 entry wall_door_200 c200 u=800 c2>street vD',
];
const EXPECTED_SINGLE_WINDOWS: string[] = [
  'f0_w0 window wall_window_200 c300 v=0 c1>street u0',
  'f0_w1 window wall_window_200 c300 v=1000 c1>street uL',
  'f0_w2 window wall_window_200 c600 v=1000 c4>street uL',
];
