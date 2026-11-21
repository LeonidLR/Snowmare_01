// Training Ground / level generator v3 - the BLOCK archetype (S2_ROOMS_PLAN.md S2.3: bandPlan.ts planBlock, planDoors.ts blockDoors, planWindows.ts slots,
// planUnits.ts, generate.ts addPlannedBlock, buildingStories.ts story 4 by flat column). Layers: (1) the plan alone - fixtures 10 x 18, 14 x 20 and
// 12 x 36 (two sections), ASCII goldens, a property test over footprints / depths / seeds 1-200 and the fallback ranges; (2) the generated building -
// loops per section storey, dead-end flats, lobbies, slots, closed flat entries, sections without a door between them, reachability of every room /
// storey / roof, the per-section loop rule of the validator; (3) a property test through the generator over the block footprint range (seeds 1-200:
// 0 validator errors, the fallback share printed); (4) story 4 by column: the key route's modes equal the fixture's sequence, rollback is byte-equal.
// `npm test`. The toggle-off golden is src/levelgen/plansToggle.test.ts (scripts/plans-golden.ts).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { buildTemplate, templateContext } from '../blockTemplates.ts';
import { applyStoriesToLayout, applyStory, escapeColumns, STORY_PICK_KNOBS, storyEligible, storyRecipe } from '../buildingStories.ts';
import { routeSummary } from '../buildingRoutes.ts';
import { traversalOk, validateBuilding } from '../buildingTraversal.ts';
import type { BuildingTraversal, TraversalIssue } from '../buildingTraversal.ts';
import type { Draft, Side } from '../generate.ts';
import { addBuilding, finalizeDraftToLayout, generateLayout } from '../generate.ts';
import type { Profile } from '../profile.ts';
import { effectiveOpeningState } from '../openingStates.ts';
import type { GenBuilding, GenLayout, GenRoom, WallOpening } from '../schema.ts';
import { DEFAULT_PARAMS, LEVEL_HEIGHT } from '../schema.ts';
import { BUILDING_PLANS, planBlock, planToAscii, snapFootprint, validatePlanTiling } from './bandPlan.ts';
import { planDoors } from './planDoors.ts';
import { planUnits } from './planUnits.ts';
import { planWindows } from './planWindows.ts';

const BLOCK = BUILDING_PLANS.recipes.block;
const COREBAY = BUILDING_PLANS.constants.core.bay_length_cm;
/** Does some section count cover a length: one `core flat` (core + one flat bay), or n x `flat core flat`? */
const sectionable = (L: number): boolean => (L >= COREBAY + BLOCK.flat_bay_cm.min && L <= COREBAY + BLOCK.flat_bay_cm.max) || Array.from({ length: 8 }, (_, i) => i + 1).some((n) => n * BLOCK.section_cm.min <= L && L <= n * BLOCK.section_cm.max);

// ---- helpers -------------------------------------------------------------------------------------------------------------------------

interface BlockOpts { w: number; h: number; floors?: number; sides?: Side[]; seed?: number; roof?: boolean; bid?: string; before?: (d: Draft) => void }

/** One planned block (a seeded stand-in for the generator's `random`) in an otherwise empty draft, finalized to a layout. */
function block(o: BlockOpts): { l: GenLayout; b: GenBuilding } {
  const d: Draft = { walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [], building_plans: true };
  o.before?.(d);
  let n = (o.seed ?? 1) * 7919;
  const random = (): number => { n = (n * 1103515245 + 12345) & 0x7fffffff; return n / 0x7fffffff; };
  const floors = o.floors ?? 2, bid = o.bid ?? 'B_t';
  addBuilding(d, bid, [[20.37, 15.21], [20.37 + o.w, 15.21 + o.h]], {
    height: floors * LEVEL_HEIGHT, floors, enterable: true, roof_walkable: o.roof ?? true, stairwell: true, set_piece: 'facade', archetype: 'block',
    doorSides: o.sides ?? ['south'], windowSides: ['south', 'north'], partitionRooms: true, plan_template: 'block_sections', // the S2.3 planner (the kit has kitPlan.test.ts)
  }, random);
  const l = finalizeDraftToLayout(d, [90, 90], { name: 't', profile: 't', seed: 1, params: { ...DEFAULT_PARAMS, size_m: [90, 90], building_plans: true }, start: [4, 2], end: [36, 58] });
  return { l, b: l.buildings.find((x) => x.id === bid)! };
}

const errorsOf = (issues: readonly TraversalIssue[]): string => issues.filter((i) => i.severity === 'error').map((i) => `${i.code}: ${i.message}`).join('; ');
const allOps = (b: GenBuilding): { op: WallOpening; interior: boolean }[] =>
  [...(b.openings ?? []).map((op) => ({ op, interior: false })), ...(b.partitions ?? []).flatMap((p) => (p.openings ?? []).map((op) => ({ op, interior: true })))];

/** The door graph of one storey over the rooms but the stairwell: nodes = room ids, edges = interior doors (portals included: they join the landing to the core only). */
function storeyGraph(b: GenBuilding, f: number): { nodes: string[]; edges: [string, string][] } {
  const rooms = (b.rooms ?? []).filter((r) => r.floor === f && r.kind !== 'stairwell').map((r) => r.id);
  const ids = new Set(rooms);
  const edges: [string, string][] = [];
  for (const { op, interior } of allOps(b)) if (interior && op.kind === 'door' && op.floor === f && ids.has(op.links![0]) && ids.has(op.links![1])) edges.push([op.links![0], op.links![1]]);
  return { nodes: rooms, edges };
}

/** The connected components (sorted) and the cyclomatic number E - V + C of each. */
function componentsOf(g: { nodes: string[]; edges: [string, string][] }): { nodes: string[]; cycles: number }[] {
  const seen = new Set<string>(), out: { nodes: string[]; cycles: number }[] = [];
  for (const start of g.nodes) {
    if (seen.has(start)) continue;
    const comp: string[] = [start];
    seen.add(start);
    for (let i = 0; i < comp.length; ++i) for (const [a, c] of g.edges) {
      const next = a === comp[i] ? c : c === comp[i] ? a : null;
      if (next && !seen.has(next)) { seen.add(next); comp.push(next); }
    }
    const e = g.edges.filter(([a, c]) => comp.includes(a) && comp.includes(c)).length;
    out.push({ nodes: comp.sort(), cycles: e - comp.length + 1 });
  }
  return out;
}

// ---- 1. the plan alone --------------------------------------------------------------------------------------------------------------

test('CodexTactics.LevelGen.BlockPlan.Fixtures: 10 x 18 and 14 x 20 are one `flat core flat` section, 12 x 36 two; every plan validates', () => {
  for (const [L, D, depth, sections] of [[1800, 1000, 350, 1], [2000, 1400, 350, 1], [2000, 1400, 200, 1], [3600, 1200, 350, 2]] as const) {
    const plan = planBlock({ L, D, depth, seed: 5 });
    assert.equal(plan.kind, 'bands', `${L}x${D}: ${plan.fallback}`);
    assert.deepEqual(validatePlanTiling(plan), []);
    assert.equal(plan.sections!.length, sections);
    for (const sec of plan.sections!) {
      assert.equal(sec.flats.length, 2, 'flat core flat');
      assert.ok(sec.u1 - sec.u0 >= BLOCK.section_cm.min && sec.u1 - sec.u0 <= BLOCK.section_cm.max);
      const cell = (part: string, flat?: string) => plan.cells.filter((c) => c.section === sec.index && c.part === part && (flat === undefined || c.flat === flat));
      assert.equal(cell('landing').length, 1);
      assert.equal(cell('A').length, 2); assert.equal(cell('B').length, 2); assert.equal(cell('C').length, 2);
      assert.equal(plan.cells.filter((c) => c.kind === 'core' && c.section === sec.index).length, 1);
      const landing = cell('landing')[0];
      assert.equal(landing.u1 - landing.u0, COREBAY, 'the landing runs the whole core bay');
      assert.ok(landing.v1 - landing.v0 >= 200 && landing.v1 - landing.v0 <= 400, 'a 200-400 landing');
      for (const c of [...cell('A'), ...cell('B'), ...cell('C')]) assert.ok(c.u1 - c.u0 >= 400 && c.v1 - c.v0 >= 400, `${c.id} is 4 x 4 m or more`);
    }
    // one A / B line for the whole building: the landing's far edge
    assert.ok(plan.cells.filter((c) => c.part === 'A').every((c) => c.v1 === plan.landingTop));
  }
});

test('CodexTactics.LevelGen.BlockPlan.Ascii: goldens of two plans (cells only, 100 cm per character)', () => {
  assert.equal(planToAscii(planBlock({ L: 1800, D: 1000, depth: 350, seed: 3 })), [
    '|RRRR|RRR|RRRR|RRR',
    '|RRRR|RRR|RRRR|RRR',
    '|RRRR|RRR|RRRR|RRR',
    '+----+---+----+---',
    '|RRRR|CCCCCCCC|RRR',
    '|RRRR+--------|RRR',
    '|RRRR|KKKKKKKK|RRR',
    '|RRRR|KKKKKKKK|RRR',
    '|RRRR|KKKKKKKK|RRR',
    '+----+--------+---',
  ].join('\n'));
  assert.equal(planToAscii(planBlock({ L: 1400, D: 1000, depth: 350, seed: 3 })), [
    '|RRRR|RRR|RRRR',
    '|RRRR|RRR|RRRR',
    '|RRRR|RRR|RRRR',
    '+----+---+----',
    '|RRRR|CCCCCCCC',
    '|RRRR+--------',
    '|RRRR|KKKKKKKK',
    '|RRRR|KKKKKKKK',
    '|RRRR|KKKKKKKK',
    '+----+--------',
  ].join('\n'));
});

test('CodexTactics.LevelGen.BlockPlan.Property: planBlock over footprints, both core depths and seeds 1-200 validates and is deterministic; the fallback ranges are named', () => {
  let plans = 0, bands = 0;
  const fallbacks: Record<string, number> = {};
  const widths = new Set<number>(), sectionCounts: Record<number, number> = {};
  for (let L = 1000; L <= 4800; L += 100) {
    for (let D = 900; D <= 1700; D += 100) {
      if (D > L) continue;
      for (const depth of [350, 200] as const) {
        for (let seed = 1; seed <= 200; ++seed) {
          const plan = planBlock({ L, D, depth, seed });
          ++plans;
          if (plan.kind === 'fallback') {
            const key = plan.fallback!.replace(/\d+/g, '#');
            fallbacks[key] = (fallbacks[key] ?? 0) + 1;
            // only the documented ranges fall back: too small, a length no section count covers, a depth no landing + 2 rooms cover
            const why = plan.fallback!;
            if (L < BLOCK.min_length_cm || D < BLOCK.min_depth_cm) assert.match(why, /footprint_too_small/);
            else if (/block_no_section_for/.test(why)) assert.ok(!sectionable(L), `${L}x${D}: ${why}`);
            else assert.ok(/block_no_landing_depth/.test(why) && D > depth + BUILDING_PLANS.constants.core.corridor_max_cm + BUILDING_PLANS.constants.room_cm.max, `${L}x${D} ${depth} seed ${seed}: ${why}`);
            continue;
          }
          ++bands;
          assert.deepEqual(validatePlanTiling(plan), [], `${L}x${D} ${depth} seed ${seed}`);
          widths.add(L);
          sectionCounts[plan.sections!.length] = (sectionCounts[plan.sections!.length] ?? 0) + 1;
          if (seed <= 3) assert.deepEqual(planBlock({ L, D, depth, seed }), plan, 'deterministic');
        }
      }
    }
  }
  console.log(`planBlock property (L 1000-4800 x D 900-1700, wider than the block range): ${bands} planned of ${plans}; fallbacks ${JSON.stringify(fallbacks)}; sections ${JSON.stringify(sectionCounts)}`);
  // every block footprint 13-23 m x 10-16 m plans: no fallback inside the covered lengths
  for (const L of [1300, 1600, 1700, 2000, 2300, 3400, 3600, 4600]) for (let D = 1000; D <= Math.min(1400, L); D += 100) {
    for (const depth of [350, 200] as const) for (let seed = 1; seed <= 200; ++seed) assert.equal(planBlock({ L, D, depth, seed }).kind, 'bands', `${L}x${D}`);
  }
  // the gap 2400-3300 and the too small footprints are named fallbacks
  for (const L of [2400, 2800, 3300]) assert.match(planBlock({ L, D: 1200, depth: 350, seed: 1 }).fallback!, /block_no_section_for_/);
  assert.equal(planBlock({ L: 1200, D: 1000, depth: 350, seed: 1 }).fallback, 'footprint_too_small');
  assert.equal(planBlock({ L: 1800, D: 900, depth: 350, seed: 1 }).fallback, 'footprint_too_small');
});

test('CodexTactics.LevelGen.BlockPlan.Doors: every section has a loop, one dead-end flat, two pad portals, a lobby per exterior door and no door between sections', () => {
  for (const [L, D, depth] of [[1800, 1000, 350], [2000, 1400, 200], [3600, 1200, 350], [4000, 1000, 350]] as const) {
    for (let seed = 1; seed <= 25; ++seed) {
      const plan = planBlock({ L, D, depth, seed });
      assert.equal(plan.kind, 'bands');
      const cores = plan.sections!.map((sec) => ({ u0: sec.core.u0, depth }));
      const res = planDoors(plan, { storeys: 2, seed, core: null, cores, doorWalls: [{ wall: 'vD', t: 0.5 }, { wall: 'v0', t: 0.3 }] });
      assert.deepEqual(res.warnings, [], `${L}x${D} seed ${seed}`);
      const windows = planWindows(plan, { storeys: 2, doors: res.openings });
      for (const sec of plan.sections!) {
        const inSec = new Set(plan.cells.filter((c) => c.section === sec.index).map((c) => c.id));
        const doors = res.openings.filter((o) => o.storey === 1 && o.kind === 'door' && o.links.some((x) => inSec.has(x)));
        assert.equal(doors.filter((o) => o.role === 'portal').length, 2, 'two pad portals');
        assert.ok(doors.every((o) => o.links.every((x) => inSec.has(x))), 'no door leaves the section');
        // flat entries: one per flat from the landing, plus the loop flat's second one through C
        assert.equal(doors.filter((o) => o.role === 'unit_entry').length, 3, '2 flat entries + the loop flat\'s second');
        const ex = res.openings.filter((o) => o.role === 'entry' && inSec.has(o.links[0]));
        assert.deepEqual(ex.map((o) => o.wall).sort(), ['v0', 'vD'], 'one street and one court door per section');
        assert.ok(ex.every((o) => res.lobbies.includes(o.links[0])));
      }
      // slots: one per flat on every storey from slot_min_storey, in the B room, on the street wall
      const slots = windows.filter((w) => w.slot);
      assert.equal(slots.length, plan.sections!.length * 2 * (2 - BLOCK.slot_min_storey));
      assert.ok(slots.every((w) => w.storey >= BLOCK.slot_min_storey && w.wall === 'vD' && plan.cells.find((c) => c.id === w.links[0])!.part === 'B'));
      const units = planUnits(plan, [...res.openings, ...windows], res.lobbies, 2);
      assert.equal(units.filter((u) => u.storey === 1).length, plan.sections!.length * 2);
      assert.equal(units.filter((u) => u.storey === 1 && u.dead_end).length, plan.sections!.length, 'one dead-end flat per section');
    }
  }
});

// ---- 2. the generated building ----------------------------------------------------------------------------------------------------------

/** The shared proofs of one generated planned block (a layout with this building only). */
function checkBlock(l: GenLayout, b: GenBuilding, label: string): BuildingTraversal {
  assert.equal(b.plan?.kind, 'bands', `${label}: ${b.plan?.fallback}`);
  assert.equal(b.plan?.variant, 'block');
  const v = validateBuilding(l, b);
  assert.ok(traversalOk(v.issues), `${label}: ${errorsOf(v.issues)}`);
  assert.equal(v.rooms.reachable, v.rooms.total, `${label}: every room reachable`);
  assert.deepEqual(v.levels.reachable, v.levels.walkable, `${label}: every storey reachable`);
  if (b.roof_walkable) assert.equal(v.roof, 'reachable', `${label}: the roof is reachable`);
  const sections = b.plan!.sections!;
  // >= 1 loop per section storey: every connected part of a storey's door graph (one per section) has a cycle
  for (let f = 0; f < b.floors; ++f) {
    const comps = componentsOf(storeyGraph(b, f)).filter((c) => c.nodes.length >= 3);
    assert.equal(comps.length, sections, `${label} f${f}: one part per section`);
    for (const c of comps) assert.ok(c.cycles >= 1, `${label} f${f}: the part with ${c.nodes[0]} has no loop`);
  }
  // flats: units per storey, entries are doors, dead ends have one entry and a run
  const units = b.units ?? [];
  assert.equal(units.length, sections * 2 * b.floors);
  const ids = new Set(allOps(b).map((x) => x.op.id));
  for (const u of units) {
    assert.ok(u.rooms.length >= 1 && u.entries.length >= 1, `${u.id}: rooms and entries`);
    assert.ok(u.entries.every((e) => ids.has(e)));
    assert.equal(u.dead_end === true, u.entries.length === 1, `${u.id}: dead_end = a single entry`);
    assert.equal(u.run_cm !== undefined, u.dead_end === true);
    for (const r of u.rooms) assert.equal((b.rooms ?? []).find((x) => x.id === r)!.unit, u.id);
  }
  assert.equal(new Set(units.map((u) => u.column)).size, sections * 2, 'one column per flat');
  // zones: the core, landing and lobbies are common; flats' rooms carry their unit
  for (const r of b.rooms ?? []) assert.ok((r.unit !== undefined) !== (r.zone === 'common'), `${r.id}: a unit or the common zone`);
  // flat entries are closed by default, pad portals open
  for (const { op } of allOps(b)) {
    if (op.role === 'unit_entry') assert.equal(effectiveOpeningState(l, b, op), 'closed', op.id);
    if (op.role === 'portal') assert.equal(effectiveOpeningState(l, b, op), 'open', op.id);
  }
  return v;
}

test('CodexTactics.LevelGen.BlockPlan.Generated: fixtures 10 x 18, 14 x 20 and 12 x 36 - every room / storey / roof reachable, a loop in every section storey, flats, lobbies, slots', () => {
  for (const [w, h, floors, sections] of [[18.4, 10.3, 2, 1], [20.2, 14.1, 3, 1], [36.5, 12.2, 2, 2], [37.1, 12.6, 3, 2]] as const) {
    for (const seed of [1, 2, 3]) {
      const { l, b } = block({ w, h, floors, seed });
      const label = `${w}x${h} f${floors} s${seed}`;
      checkBlock(l, b, label);
      assert.equal(b.plan!.sections, sections);
      assert.equal(b.stairs.filter((s) => s.kind === 'stairwell' && s.from === 0).length, sections, `${label}: a core per section`);
      // exterior doors: a street and a court door per section on the ground, in lobbies; none above
      const entries = (b.openings ?? []).filter((o) => o.role === 'entry');
      assert.equal(entries.length, 2 * sections, label);
      assert.ok(entries.every((o) => o.floor === 0 && (b.rooms ?? []).find((r) => r.id === o.links![0])!.kind === 'lobby'));
      assert.equal(entries.filter((o) => o.module === 'wall_door_300').length, 1, 'only the first door is the 300 module');
      // slots: one per flat on each storey from 1, all on one wall, the same spot on every storey
      const slots = (b.openings ?? []).filter((o) => o.slot === 'balcony');
      assert.equal(slots.length, sections * 2 * (floors - 1), label);
      assert.ok(slots.every((o) => o.kind === 'window' && o.floor >= 1));
      for (const col of new Set((b.units ?? []).map((u) => u.column))) {
        const mine = slots.filter((o) => (b.units ?? []).find((u) => u.id === (b.rooms ?? []).find((r) => r.id === o.links![0])!.unit)!.column === col);
        assert.equal(mine.length, floors - 1, `${col}: a slot on every storey from 1`);
        assert.ok(mine.every((o) => Math.abs(o.pos[0] - mine[0].pos[0]) < 1 && Math.abs(o.pos[1] - mine[0].pos[1]) < 1), `${col}: the same spot`);
      }
      // base balconies: none (designer 2026-11-19: only where a fire escape is)
      assert.equal(b.balconies, undefined);
      assert.equal(b.fire_escapes, undefined);
    }
  }
});

test('CodexTactics.LevelGen.BlockPlan.Sections: a 12 x 36 block has a wall without a door between its sections; each core reaches the roof; the two entrances are independent', () => {
  const { l, b } = block({ w: 36.5, h: 12.2, floors: 2, seed: 4 });
  checkBlock(l, b, '12x36');
  const sections = b.plan!.sections!;
  assert.equal(sections, 2);
  // the stairwell rooms of storey 1, one per section; no opening links rooms whose cores differ
  const cores = (b.rooms ?? []).filter((r) => r.kind === 'stairwell' && r.floor === 1);
  assert.equal(cores.length, 2);
  const along = (b.footprint[1][0] - b.footprint[0][0]) >= (b.footprint[1][1] - b.footprint[0][1]) ? 0 : 1;
  const mid = (cores[0].bounds.min[along] + cores[1].bounds.max[along]) / 2; // between the two cores: the section wall lies near it
  const side = (r: GenRoom): number => ((r.bounds.min[along] + r.bounds.max[along]) / 2 < mid ? 0 : 1);
  const rooms = new Map((b.rooms ?? []).map((r) => [r.id, r]));
  for (const { op, interior } of allOps(b)) {
    if (!interior || op.kind !== 'door') continue;
    const [a, c] = op.links!.map((x) => rooms.get(x)!);
    assert.equal(side(a), side(c), `${op.id}: a door across the middle of the block`);
  }
  // both sections have their own street door
  const entries = (b.openings ?? []).filter((o) => o.role === 'entry');
  assert.equal(new Set(entries.map((o) => side(rooms.get(o.links![0])!))).size, 2);
  // the roof: two flights arrive on it
  assert.equal(b.stairs.filter((s) => s.kind === 'stairwell' && s.to === b.floors).length, 2);
});

test('CodexTactics.LevelGen.BlockPlan.Fallbacks: a footprint that does not plan keeps the legacy output (plan.kind bsp, a named reason); buildings on the cores sides move or drop the plan', () => {
  for (const [w, h, why] of [[9.4, 9.4, /footprint_too_small/], [26.2, 11.3, /block_no_section_for_2600/]] as [number, number, RegExp][]) {
    const { b } = block({ w, h, floors: 2, seed: 2 });
    assert.equal(b.plan?.kind, 'bsp');
    assert.match(b.plan!.fallback!, why);
    assert.equal(b.units, undefined);
  }
  // buildings standing inside the footprint where a core box would go (the draft's north / south strip of the block): one side blocked -> the cores take
  // the other side and the plan stands; both blocked -> 'block_core_blocked', the legacy stages run
  const strip = (name: string, y0: number, y1: number) => (d: Draft) => {
    addBuilding(d, name, [[20.37 + 1, y0], [20.37 + 18.4 - 1, y1]], { height: LEVEL_HEIGHT, floors: 1, hollow: true, enterable: false, roof_walkable: false, set_piece: 'chimney' }, () => 0.5);
  };
  const north = block({ w: 18.4, h: 10.3, floors: 2, seed: 2, before: strip('B_n', 15.21 + 0.2, 15.21 + 3.4) });
  assert.equal(north.b.plan?.kind, 'bands');
  assert.equal(north.b.plan?.core_wall, 'B_t_S', 'the north strip is taken: the core wall moves to the south');
  const both = block({ w: 18.4, h: 10.3, floors: 2, seed: 2, before: (d) => { strip('B_n', 15.21 + 0.2, 15.21 + 3.4)(d); strip('B_s', 15.21 + 10.3 - 3.7, 15.21 + 10.1)(d); } });
  assert.equal(both.b.plan?.kind, 'bsp');
  assert.equal(both.b.plan?.fallback, 'block_core_blocked');
});

test('CodexTactics.LevelGen.BlockPlan.Validator: a section without its loop is storey_no_loop although the other section keeps one', () => {
  const { l, b } = block({ w: 36.5, h: 12.2, floors: 2, seed: 4 });
  assert.ok(traversalOk(validateBuilding(l, b).issues));
  const along = (b.footprint[1][0] - b.footprint[0][0]) >= (b.footprint[1][1] - b.footprint[0][1]) ? 0 : 1;
  // the first section's landing on storey 1, and its second flat entry: the unit_entry on the landing's long wall (the A entries are on its end walls)
  const landing = (b.rooms ?? []).filter((r) => r.floor === 1 && r.kind === 'corridor').sort((x, y) => x.bounds.min[along] - y.bounds.min[along])[0];
  const cut = structuredClone(b);
  let removed = 0;
  for (const p of cut.partitions ?? []) {
    p.openings = (p.openings ?? []).filter((op) => {
      if (op.floor !== 1 || op.role !== 'unit_entry' || !op.links!.includes(landing.id)) return true;
      const atEnd = Math.abs(op.pos[along] - landing.bounds.min[along]) < 2 || Math.abs(op.pos[along] - landing.bounds.max[along]) < 2;
      if (atEnd) return true;
      removed++;
      return false;
    });
  }
  assert.equal(removed, 1, "the loop flat's second entry");
  const issues = validateBuilding({ ...l, buildings: l.buildings.map((x) => (x.id === b.id ? cut : x)) }, cut).issues;
  const bad = issues.filter((i) => i.code === 'storey_no_loop');
  assert.equal(bad.length, 1, `exactly the first section of storey 1: ${bad.map((i) => i.message)}`);
  assert.match(bad[0].ref!, /f1/);
  // the other section's loop still stands: the whole-building graph would have passed
  assert.ok(componentsOf(storeyGraph(cut, 1)).some((c) => c.nodes.length >= 3 && c.cycles >= 1));
});

// ---- 3. the property test through the generator ---------------------------------------------------------------------------------------

test('CodexTactics.LevelGen.BlockPlan.GeneratorProperty: every planned block over the block footprint range passes validateBuilding with no error (seeds 1-200)', () => {
  const started = Date.now();
  const failures: string[] = [], fallbacks: Record<string, number> = {}, tally: Record<string, number> = {}, seeds = new Set<number>();
  let total = 0, planned = 0, run = 0;
  const lengths = [1300, 1400, 1500, 1600, 1700, 1800, 1900, 2000, 2100, 2200, 2300, 3400, 3600, 3800, 4000, 4600];
  for (const L of lengths) {
    for (const D of [1000, 1200, 1400]) {
      for (const alongY of [false, true]) {
        const long: Side[] = alongY ? ['west', 'east'] : ['north', 'south'], short: Side[] = alongY ? ['north', 'south'] : ['west', 'east'];
        for (const cfg of [{ floors: 2, roof: true }, { floors: 3, roof: true }, { floors: 2, roof: false }]) {
          for (const sides of [[long[run % 2]], [short[run % 2]], [long[0], long[1]]] as Side[][]) {
            run++;
            const seed = 1 + ((run * 13) % 200);
            seeds.add(seed);
            const w = (alongY ? D : L) / 100 + 0.37, h = (alongY ? L : D) / 100 + 0.61;
            const { l, b } = block({ w, h, floors: cfg.floors, roof: cfg.roof, sides, seed, bid: `B_p${seed}` });
            total++;
            if (b.plan?.kind !== 'bands') { const k = (b.plan?.fallback ?? 'none').replace(/\d+/g, '#'); fallbacks[k] = (fallbacks[k] ?? 0) + 1; continue; }
            planned++;
            const v = validateBuilding(l, b);
            for (const i of v.issues) tally[`${i.severity}:${i.code}`] = (tally[`${i.severity}:${i.code}`] ?? 0) + 1;
            const label = `${L}x${D} ${alongY ? 'y' : 'x'}-long f${cfg.floors}${cfg.roof ? 'R' : ''} ${sides} seed ${seed}`;
            if (!traversalOk(v.issues)) { failures.push(`${label}: ${errorsOf(v.issues)}`); continue; }
            if (v.rooms.reachable !== v.rooms.total || v.levels.reachable.length !== v.levels.walkable.length || (b.roof_walkable && v.roof !== 'reachable')) failures.push(`${label}: not everything reachable`);
            for (let f = 0; f < b.floors; ++f) for (const c of componentsOf(storeyGraph(b, f)).filter((x) => x.nodes.length >= 3)) if (c.cycles < 1) failures.push(`${label} f${f}: no loop in ${c.nodes[0]}`);
          }
        }
      }
    }
  }
  console.log(`block generator property: ${planned} planned of ${total} blocks (fallback share ${Math.round((100 * (total - planned)) / total)}%; fallbacks ${JSON.stringify(fallbacks)}), ${seeds.size} distinct seeds, ${Date.now() - started} ms; issues ${JSON.stringify(tally)}`);
  assert.equal(failures.length, 0, `${failures.length} planned blocks fail:\n${failures.slice(0, 12).join('\n')}`);
  assert.ok(planned >= total * 0.8, `planned ${planned} of ${total}`);
  assert.equal(seeds.size, 200, 'every seed 1-200 is used');
});

// ---- 4. story 4 by flat column ------------------------------------------------------------------------------------------------------------

test('CodexTactics.LevelGen.BlockPlan.Story4: the escape takes a flat column\'s slot windows; the key route modes equal the fixture\'s sequence; the story rolls back byte-equal without a spot', () => {
  for (const [floors, storeys] of [[2, [1]], [3, [1, 2]]] as [number, number[]][]) {
    const { l, b } = block({ w: 18.4, h: 10.3, floors, seed: 4 });
    assert.ok(storyEligible(l, b, 'rubble_escape'));
    const columns = escapeColumns(l, b, storeys);
    assert.ok(columns.length >= 2, 'a column per flat can take the escape');
    const recipe = storyRecipe(l, b, 'rubble_escape')!;
    assert.equal(recipe[0].kind, 'block_flight');
    assert.deepEqual(recipe[1], { kind: 'add_balcony_escape', room: columns[0].id, storeys });
    const res = applyStory(l, b, 'rubble_escape', 9);
    assert.ok(res.ok, res.issues.map((i) => i.message).join('; '));
    const nb = res.building;
    // the slot windows of that column became balcony doors at the same points; the other columns keep theirs
    const unitOf = (id: string): string => (nb.rooms ?? []).find((r) => r.id === id)!.unit!;
    const column = (b.units ?? []).find((u) => u.id === unitOf(columns[0].id))!.column;
    const gone = (b.openings ?? []).filter((o) => o.slot === 'balcony' && (b.units ?? []).find((u) => u.id === (b.rooms ?? []).find((r) => r.id === o.links![0])!.unit)!.column === column);
    assert.equal(gone.length, storeys.length);
    for (const g of gone) assert.ok(!(nb.openings ?? []).some((o) => o.id === g.id), `${g.id}: the slot window is gone`);
    assert.equal((nb.openings ?? []).filter((o) => o.slot === 'balcony').length, (b.openings ?? []).filter((o) => o.slot === 'balcony').length - storeys.length);
    const doors = (nb.openings ?? []).filter((o) => o.role === 'balcony');
    assert.equal(doors.length, storeys.length);
    for (const [i, d] of doors.entries()) {
      assert.ok(Math.abs(d.pos[0] - gone[i].pos[0]) < 1 && Math.abs(d.pos[1] - gone[i].pos[1]) < 1, 'at the slot\'s point');
      assert.match(d.links![1], /^balcony:/);
      assert.equal(d.width, 150);
    }
    assert.equal(nb.balconies!.length, storeys.length);
    assert.equal(nb.fire_escapes!.length, 1);
    assert.ok(!(nb.mantle_edges ?? []).some((m) => gone.some((g) => m.id === `me_${g.id}`)), 'the window mantle edges went with the windows');
    const l2: GenLayout = { ...l, ladders: res.ladders ?? l.ladders, buildings: l.buildings.map((x) => (x.id === b.id ? nb : x)) };
    const after = validateBuilding(l2, nb);
    assert.ok(traversalOk(after.issues), errorsOf(after.issues));
    assert.equal(after.rooms.reachable, after.rooms.total);
    assert.deepEqual(after.levels.reachable, after.levels.walkable, 'storey 1 (and 2) reachable by the escape');
    // the key route: street -> ladder -> balcony door -> flat (a closed flat door) -> landing -> stairs up
    const modes = routeSummary(l2, nb)!.modes;
    assert.deepEqual(modes, floors === 2 ? ['walk', 'climb', 'open', 'walk', 'open', 'walk'] : ['walk', 'climb', 'open', 'walk', 'open', 'walk'], `modes ${modes}`);
  }
  // no spot: a building standing right outside the slots' wall -> rolled back, the original object returned
  const { l, b } = block({ w: 18.4, h: 10.3, floors: 2, seed: 4 });
  const [[x0, y0], [x1, y1]] = b.footprint;
  const wall = [{ id: 'W_all', a: [x0 - 500, y0 - 300] as [number, number], b: [x1 + 500, y0 - 300] as [number, number], height: 300, thickness: 20 }];
  void wall;
  const ring: GenBuilding = { ...structuredClone(b), id: 'B_ring', footprint: [[x0 - 600, y0 - 700], [x1 + 600, y1 + 700]], rooms: [], openings: [], partitions: [], stairs: [], doors: [], units: undefined, enterable: false, plan: undefined };
  const blocked: GenLayout = { ...l, buildings: [...l.buildings.map((x) => ({ ...x })), { ...ring, footprint: [[x0 - 1, y0 - 400], [x1 + 1, y0 - 1]] }, { ...ring, id: 'B_ring2', footprint: [[x0 - 1, y1 + 1], [x1 + 1, y1 + 400]] }, { ...ring, id: 'B_ring3', footprint: [[x0 - 400, y0 - 1], [x0 - 1, y1 + 1]] }, { ...ring, id: 'B_ring4', footprint: [[x1 + 1, y0 - 1], [x1 + 400, y1 + 1]] }] };
  const before = JSON.stringify(blocked.buildings.find((x) => x.id === b.id));
  const rb = applyStory(blocked, blocked.buildings.find((x) => x.id === b.id)!, 'rubble_escape', 9);
  assert.equal(rb.ok, false);
  assert.equal(JSON.stringify(blocked.buildings.find((x) => x.id === b.id)), before, 'the original is untouched');
  assert.equal(rb.building, blocked.buildings.find((x) => x.id === b.id), 'the original object comes back');
  assert.deepEqual(escapeColumns(blocked, blocked.buildings.find((x) => x.id === b.id)!, [1]), []);
});

test('CodexTactics.LevelGen.BlockPlan.Snap: block footprints snap down to the 100 grid like the office', () => {
  assert.deepEqual(snapFootprint(1843.2, 1029.9), { L: 1800, D: 1000, swapped: false });
  assert.deepEqual(snapFootprint(1029.9, 1843.2), { L: 1800, D: 1000, swapped: true });
});

test('CodexTactics.LevelGen.BlockPlan.StoryPass: applyStoriesToLayout writes the escape and the layout-level window mantle edges lose the replaced windows', () => {
  const { l, b } = block({ w: 18.4, h: 10.3, floors: 2, seed: 4 });
  const c = [(b.footprint[0][0] + b.footprint[1][0]) / 2, (b.footprint[0][1] + b.footprint[1][1]) / 2];
  const layout: GenLayout = { ...l, routes: [{ id: 'R', mandatory: true, points: [[c[0] - 3000, c[1] + 1500], [c[0] + 3000, c[1] + 1500]] } as never] };
  const roles = ['objective', 'garrison', 'overwatch', 'transit', 'background'] as const;
  const knobs = { ...STORY_PICK_KNOBS, budget: Object.fromEntries(roles.map((r) => [r, 1])) as never, weights: Object.fromEntries(roles.map((r) => [r, { rubble_escape: 1 }])) as never, t_max: 2 };
  const out = applyStoriesToLayout(layout, 7, knobs).layout;
  const nb = out.buildings.find((x) => x.id === b.id)!;
  assert.deepEqual(nb.stories?.map((x) => x.id), ['rubble_escape']);
  assert.equal(out.mantle_edges!.length, l.mantle_edges!.length - 1, 'one window of the column went (storey 1)');
  const own = new Set(nb.mantle_edges!.map((m) => m.id));
  for (const m of out.mantle_edges!) if (m.kind === 'window' && m.building === b.id) assert.ok(own.has(m.id), `${m.id} belongs to the building`);
  assert.ok(nb.key_route && nb.key_route.legs.some((x) => x.mode === 'climb'), 'the key route climbs the escape');
});

// ---- 5. the lot template ---------------------------------------------------------------------------------------------------------------------

test('CodexTactics.LevelGen.BlockPlan.Template: two_storey with plans on is a planned block of 2 or 3 storeys, with plans off it is the S1 two-storey building', () => {
  const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../../..');
  const profile = JSON.parse(fs.readFileSync(path.join(root, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
  const on = generateLayout({ profile, seed: 1, params: { building_plans: true } });
  const off = generateLayout({ profile, seed: 1 });
  const lot = { id: 'LOT_T', rect: [[-1000, -800], [1000, 800]] as [[number, number], [number, number]], template: '' };
  const storeys: Record<number, number> = {}, kinds: Record<string, number> = {};
  for (let seed = 1; seed <= 40; ++seed) {
    const built = buildTemplate('two_storey', lot, templateContext(on, seed));
    const blocks = built.content.buildings.filter((x) => x.archetype === 'block');
    assert.equal(blocks.length, 1);
    storeys[blocks[0].floors] = (storeys[blocks[0].floors] ?? 0) + 1;
    kinds[blocks[0].plan?.kind ?? 'none'] = (kinds[blocks[0].plan?.kind ?? 'none'] ?? 0) + 1;
    assert.ok(blocks[0].floors === 2 || blocks[0].floors === 3);
    assert.equal(blocks[0].height, blocks[0].floors * LEVEL_HEIGHT);
    const plain = buildTemplate('two_storey', lot, templateContext(off, seed)).content.buildings.find((x) => x.archetype === 'block')!;
    assert.equal(plain.floors, 2, 'plans off: always two storeys');
    assert.equal(plain.plan, undefined);
  }
  console.log(`block template: storeys ${JSON.stringify(storeys)}, plans ${JSON.stringify(kinds)}`);
  assert.ok(storeys[2] > 0 && storeys[3] > 0, 'both storey counts occur');
  assert.ok((kinds.bands ?? 0) >= 30, 'most lots plan');
});
