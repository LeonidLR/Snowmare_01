// Training Ground / level generator v3 - the KIT pattern of the residential block (kitPlan.ts; docs/design/levelgen/BGEN_KIT_PLANS_2026-11-21.md):
// (1) the plan alone - the four blocks of the test map L_Gen_BgenPlans (18 x 10, 20 x 14, 36 x 12, 37 x 12 m): flats per section, the room set of
// every flat, areas inside the kit's tolerance, the core on the court wall, the tambour, every door on a shared wall; a property over footprints /
// depths / seeds (a plan or a named fallback, never an invalid plan); (2) the generated building - flat doors `unit_entry` and nothing else, the
// hall windowless, every room reachable from the court door, the court entrance through the core, the half-landing switchback (two half flights per
// storey, the mid landing, the slab gap beyond the floor landing, a lane wall per storey), 0 validator errors over a footprint sweep; (3) the S2.9
// root cause: the S2.3 ground floors have no role mismatch any more (every unit entry is `unit_entry`, every `unit_entry` is a unit entry). `npm test`.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { validateBuilding } from '../buildingTraversal.ts';
import type { Draft } from '../generate.ts';
import { addBuilding, finalizeDraftToLayout } from '../generate.ts';
import type { GenBuilding, GenLayout, WallOpening } from '../schema.ts';
import { DEFAULT_PARAMS, LEVEL_HEIGHT, STAIR_RUN_PER_RISE, isSwitchback, stairFlights } from '../schema.ts';
import { toBuildingFrame } from '../rotation.ts';
import { DEFAULT_BLOCK_TEMPLATE, KIT, blockTemplate, kitPlanToAscii, kitStairGeom, planKitBlock, templateForSeries, validateKitPlan } from './kitPlan.ts';
import type { BandPlan, PlanCell } from './bandPlan.ts';

const area = (c: PlanCell): number => ((c.u1 - c.u0) * (c.v1 - c.v0)) / 1e4;
const FIXTURES: [number, number][] = [[1800, 1000], [2000, 1400], [3600, 1200], [3700, 1200]];

function kitBlock(w: number, h: number, floors = 2, seed = 4, opts: { pattern?: 'kit' | 'sections'; roof?: boolean } = {}): { l: GenLayout; b: GenBuilding } {
  const d: Draft = { walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [], building_plans: true };
  let n = seed * 7919;
  const random = (): number => { n = (n * 1103515245 + 12345) & 0x7fffffff; return n / 0x7fffffff; };
  addBuilding(d, 'B_k', [[20.37, 15.21], [20.37 + w, 15.21 + h]], {
    height: floors * LEVEL_HEIGHT, floors, enterable: true, roof_walkable: opts.roof ?? true, stairwell: true, set_piece: 'facade', archetype: 'block',
    doorSides: ['south'], windowSides: ['south', 'north'], partitionRooms: true, plan_template: opts.pattern === 'sections' ? 'block_sections' : 'block_khrushchevka',
  }, random);
  const l = finalizeDraftToLayout(d, [90, 90], { name: 't', profile: 't', seed: 1, params: { ...DEFAULT_PARAMS, size_m: [90, 90], building_plans: true }, start: [4, 2], end: [36, 58] });
  return { l, b: l.buildings.find((x) => x.id === 'B_k')! };
}

const allDoors = (b: GenBuilding): WallOpening[] => [...(b.openings ?? []), ...(b.partitions ?? []).flatMap((p) => p.openings ?? [])].filter((o) => o.kind === 'door');
const errorsOf = (b: GenBuilding, l: GenLayout): string[] => validateBuilding(l, b).issues.filter((i) => i.severity === 'error').map((i) => `${i.code}: ${i.message}`);

// ---- (1) the plan ----------------------------------------------------------------------------------------------------------------------

test('CodexTactics.LevelGen.KitPlan.Sections: the test map blocks cut into kit sections - three flats per landing (two when the street band is too shallow), the core and the tambour on the court wall', () => {
  for (const [L, D] of FIXTURES) {
    const p = planKitBlock({ L, D, seed: 7 });
    assert.equal(p.kind, 'bands', `${L} x ${D}: ${p.fallback}`);
    assert.deepEqual(validateKitPlan(p), []);
    const three = D >= KIT.three_flat_min_depth_cm;
    for (const sec of p.kit!.sections) {
      assert.deepEqual(sec.flats, three ? ['A', 'B', 'C'] : ['A', 'C'], `${L} x ${D} s${sec.index}`);
      const core = p.cells.filter((c) => c.kind === 'core' && c.section === sec.index);
      assert.equal(core.length, 1);
      assert.equal(core[0].v0, 0, 'the core stands on the court wall (v = 0)');
      assert.deepEqual([core[0].u1 - core[0].u0, core[0].v1], [KIT.core.width_cm, KIT.core.depth_cm]);
      const tam = p.cells.filter((c) => c.use === 'tambour' && c.section === sec.index);
      assert.equal(tam.length, three ? 1 : 0);
      if (three) { assert.equal(tam[0].v0, 0); assert.ok(tam[0].u1 === core[0].u0 || tam[0].u0 === core[0].u1, 'the tambour beside the core'); }
    }
    if (!three) assert.ok(p.kit!.notes.some((n) => n.includes('too shallow')), 'the two-flat reason is recorded');
  }
  assert.equal(planKitBlock({ L: 3600, D: 1200, seed: 7 }).kit!.sections.length, 2, 'a 36 m block = two entrances');
});

test('CodexTactics.LevelGen.KitPlan.Rooms: every flat = a windowless hall + bath + kitchen + 1-3 living rooms, areas inside the kit tolerance, living rooms >= one 3 m bay', () => {
  for (const [L, D] of FIXTURES) {
    for (const seed of [1, 2, 3, 7, 11]) {
      const p = planKitBlock({ L, D, seed });
      assert.equal(p.kind, 'bands');
      const flats = new Set(p.cells.map((c) => c.unit).filter((u): u is string => !!u));
      for (const f of flats) {
        const cs = p.cells.filter((c) => c.unit === f);
        const n = (use: string): number => cs.filter((c) => c.use === use).length;
        assert.equal(n('hall'), 1, `${f}: one hall`);
        assert.equal(n('bath'), 1, `${f}: one bath`);
        assert.equal(n('kitchen'), 1, `${f}: one kitchen`);
        const want = (KIT.living_rooms as Record<string, number>)[f.slice(-1)];
        assert.ok(n('living') >= 1 && n('living') <= 3, `${f}: ${n('living')} living rooms`);
        if (f.endsWith('B')) assert.equal(n('living'), want);
        for (const c of cs) {
          const band = (KIT.room_area_m2 as Record<string, number[]>)[c.use!];
          assert.ok(area(c) >= band[0] && area(c) <= band[1], `${f} ${c.use}: ${area(c)} m2 outside ${band}`);
          if (c.use === 'living') assert.ok(Math.min(c.u1 - c.u0, c.v1 - c.v0) >= KIT.full_flat.living_min_cm);
          if (c.use === 'hall' || c.use === 'bath') assert.notEqual(c.kind, 'room', 'hall / bath are no window rooms');
        }
      }
    }
  }
  // the 3-room flat C of a deep wide section gets its three living rooms; 1-room B its one
  const p = planKitBlock({ L: 2000, D: 1400, seed: 7 });
  assert.equal(p.cells.filter((c) => c.unit === 's0C' && c.use === 'living').length, 3);
  assert.match(kitPlanToAscii(p), /S/);
});

test('CodexTactics.LevelGen.KitPlan.Property: footprints 14-46 m x 10-15 m, seeds 1-40 - a valid plan or a named fallback; sections inside the kit range', () => {
  let planned = 0, fallback = 0;
  for (let L = 1400; L <= 4600; L += 100) {
    for (let D = 1000; D <= Math.min(1500, L); D += 100) {
      for (let seed = 1; seed <= 40; seed += 3) {
        const p = planKitBlock({ L, D, seed });
        if (p.kind === 'fallback') { assert.ok(p.fallback && p.fallback.length > 3); ++fallback; continue; }
        ++planned;
        assert.deepEqual(validateKitPlan(p), [], `${L} x ${D} seed ${seed}`);
        assert.deepEqual(planKitBlock({ L, D, seed }), p, 'deterministic');
      }
    }
  }
  assert.ok(planned > fallback, `planned ${planned}, fallback ${fallback}`);
  assert.equal(planKitBlock({ L: 2500, D: 1200, seed: 1 }).kind, 'fallback', '25 m: no kit section count (the S2.3 planner takes it)');
});

test('CodexTactics.LevelGen.KitPlan.StairGeom: the switchback fits the core - 35 deg half flights, a 200 cm floor landing, the mid landing >= its minimum', () => {
  const g = kitStairGeom(0)!;
  assert.ok(g.run >= (300 / 2) * STAIR_RUN_PER_RISE);
  assert.equal(g.vLanding - KIT.core.wall_inner_cm, KIT.core.landing_cm);
  assert.ok(g.vFar - g.vMid >= KIT.core.mid_landing_cm);
  assert.ok(g.lane[1] - g.lane[0] >= KIT.core.flight_w_cm + KIT.core.lane_gap_cm);
  assert.ok(Math.atan2(150, g.run) * 180 / Math.PI <= 44, 'walkable');
});

// ---- (2) the generated building --------------------------------------------------------------------------------------------------------

test('CodexTactics.LevelGen.KitPlan.Generated: flat doors are unit_entry (and nothing else is), the court door opens into the core, halls windowless, every room reachable, 0 validator errors', () => {
  for (const [w, h, floors] of [[18.4, 10.3, 2], [20.2, 14.1, 3], [36.5, 12.2, 2], [37.1, 12.6, 3]] as const) {
    const { l, b } = kitBlock(w, h, floors);
    assert.equal(b.plan?.kind, 'bands');
    assert.equal(b.plan?.pattern, 'kit');
    assert.deepEqual(errorsOf(b, l), [], `${w} x ${h}`);
    const v = validateBuilding(l, b);
    assert.equal(v.rooms.reachable, v.rooms.total, 'every room reachable');
    const rooms = new Map((b.rooms ?? []).map((r) => [r.id, r]));
    const unitOf = new Map<string, string>();
    for (const u of b.units ?? []) for (const r of u.rooms) unitOf.set(r, u.id);
    const doors = allDoors(b);
    for (const d of doors) {
      if (!d.links || d.links[1] === 'street') continue;
      const [a, c] = d.links.map((x) => unitOf.get(x));
      const entry = a !== c;
      assert.equal(d.role === 'unit_entry', entry, `${d.id}: role ${d.role}, joins ${d.links.join(' / ')}`);
      if (entry) {
        const outside = d.links.find((x) => !unitOf.has(x))!;
        assert.ok(['landing', 'tambour'].includes(rooms.get(outside)!.use ?? ''), `${d.id}: a flat is entered from the landing or the tambour`);
      }
    }
    for (const u of b.units ?? []) {
      assert.equal(u.entries.length, 1, `${u.id}: one flat door`);
      assert.ok(u.dead_end);
      const hall = u.rooms.map((r) => rooms.get(r)!).find((r) => r.use === 'hall')!;
      assert.ok(doors.some((d) => d.id === u.entries[0] && d.links!.includes(hall.id)), `${u.id}: the flat door opens into the hall`);
    }
    for (const r of b.rooms ?? []) {
      const windows = (b.openings ?? []).filter((o) => o.kind === 'window' && o.links?.[0] === r.id);
      if (r.use === 'hall' || r.use === 'bath' || r.use === 'corridor' || r.use === 'tambour') assert.equal(windows.length, 0, `${r.id} (${r.use}) is windowless`);
      if ((r.use === 'kitchen' || r.use === 'living') && r.walls?.length) assert.ok(windows.length > 0, `${r.id} (${r.use}) on an exterior wall has a window`);
    }
    const entries = (b.openings ?? []).filter((o) => o.kind === 'door' && o.links?.[1] === 'street');
    assert.equal(entries.length, b.plan!.sections, 'one court door per section');
    for (const e of entries) {
      assert.equal(rooms.get(e.links![0])!.use, 'landing', 'the entrance opens onto the floor landing in the core');
      assert.equal(e.wall, b.plan!.core_wall, 'the entrance is on the court (core) wall');
    }
  }
});

test('CodexTactics.LevelGen.KitPlan.Switchback: two half flights per storey round a mid landing, the slab gap beyond the floor landing, one lane wall per storey', () => {
  const { l, b } = kitBlock(20.2, 14.1, 3);
  const flights = (b.stairs ?? []).filter((s) => s.kind === 'stairwell');
  assert.equal(flights.length, 3, 'a walkable roof: ground -> 1 -> 2 -> roof');
  for (const st of flights) {
    assert.ok(isSwitchback(st), st.id);
    const [fa, fb] = stairFlights(st);
    assert.equal(fa.z1 - fa.z0, 0.5);
    assert.equal(fb.z1 - fb.z0, 0.5);
    const len = (p: number[], q: number[]): number => Math.hypot(q[0] - p[0], q[1] - p[1]);
    assert.ok(len(fa.a, fa.b) >= 150 * STAIR_RUN_PER_RISE - 1 && len(fb.a, fb.b) >= 150 * STAIR_RUN_PER_RISE - 1, 'each half flight keeps 35 deg');
    // flight a goes in, flight b comes back out: opposite directions, side by side
    const da = [fa.b[0] - fa.a[0], fa.b[1] - fa.a[1]], db = [fb.b[0] - fb.a[0], fb.b[1] - fb.a[1]];
    assert.ok(da[0] * db[0] + da[1] * db[1] < 0, 'a switchback');
    // the foot of flight a and the top of flight b both on the floor landing (same distance from the court wall)
    const P = toBuildingFrame(b, st.pos), T = toBuildingFrame(b, st.top);
    assert.ok(Math.abs(P[0] - T[0]) < 1 || Math.abs(P[1] - T[1]) < 1);
    const gap = b.floors_detail!.find((fd) => fd.floor === st.to)?.gaps?.find((g) => g.kind === 'stairwell');
    if (st.to < b.floors) assert.ok(gap, `the slab of level ${st.to} opens above the flights`);
  }
  const laneWalls = l.walls.filter((w) => w.building === b.id && /_lanewall_f\d+$/.test(w.id));
  assert.equal(laneWalls.length, 3);
});

test('CodexTactics.LevelGen.KitPlan.FallbackChain: a footprint the kit cannot cut (13 m: under a two-flat section) takes the S2.3 sections planner and names the kit reason', () => {
  const { l, b } = kitBlock(13.4, 10.3, 2);
  assert.equal(b.plan?.kind, 'bands');
  assert.notEqual(b.plan?.pattern, 'kit');
  assert.ok(b.plan?.notes?.some((n) => n.includes('kit_no_section_for_1300')), JSON.stringify(b.plan));
  assert.deepEqual(errorsOf(b, l), []);
});

test('CodexTactics.LevelGen.KitPlan.GeneratorSweep: kit blocks over 15-46 m x 10-15 m, 2 / 3 storeys, roof or not - 0 validator errors', () => {
  let kit = 0, other = 0;
  for (let w = 15.2; w <= 46; w += 1.5) {
    for (const h of [10.1, 11.3, 12.6, 14.2]) {
      for (const [floors, roof] of [[2, true], [3, false], [3, true]] as const) {
        const { l, b } = kitBlock(w, h, floors, Math.round(w * 10) % 17, { roof });
        if (b.plan?.pattern === 'kit') ++kit; else ++other;
        assert.deepEqual(errorsOf(b, l), [], `${w} x ${h} ${floors} storeys roof ${roof}`);
      }
    }
  }
  assert.ok(kit > other, `kit ${kit}, other ${other}`);
});

// ---- (3) the S2.9 root cause on the S2.3 planner ---------------------------------------------------------------------------------------

test('CodexTactics.LevelGen.KitPlan.RolesFollowUnits: S2.3 ground floors - every unit entry is unit_entry, every unit_entry door is some flat\'s entry (the 24 S2.9 mismatches)', () => {
  for (const [w, h, floors] of [[18.4, 10.3, 2], [20.2, 14.1, 3], [36.5, 12.2, 2]] as const) {
    const { b } = kitBlock(w, h, floors, 4, { pattern: 'sections' });
    assert.notEqual(b.plan?.pattern, 'kit');
    const doors = new Map(allDoors(b).map((d) => [d.id!, d]));
    const listed = new Set((b.units ?? []).flatMap((u) => u.entries));
    for (const id of listed) assert.equal(doors.get(id)?.role, 'unit_entry', `${id} is a flat entry`);
    for (const [id, d] of doors) if (d.role === 'unit_entry') assert.ok(listed.has(id), `${id}: a unit_entry no flat lists`);
  }
});

// keep the type import used (BandPlan) for readers of the plan shape
export type { BandPlan };

// ---- (4) the template catalog (lead addendum 2026-11-21: several house types, the khrushchevka is one) ----------------------------------

test('CodexTactics.LevelGen.KitPlan.Templates: the khrushchevka (series 1-464) is the default block template; a series or a template id picks; the building records template / series / era', () => {
  assert.equal(DEFAULT_BLOCK_TEMPLATE, 'block_khrushchevka');
  assert.deepEqual(blockTemplate(), { id: 'block_khrushchevka', planner: 'kit_sections', series: '1-464', era: 'khrushchev_1960s' });
  assert.equal(blockTemplate('no_such_template').id, 'block_khrushchevka', 'an unknown id falls back to the default');
  assert.equal(blockTemplate('block_sections').planner, 'sections');
  assert.equal(templateForSeries('1-464'), 'block_khrushchevka');
  assert.equal(templateForSeries('II-18'), undefined);
  const kit = kitBlock(20.2, 14.1, 2).b;
  assert.equal(kit.plan?.template, 'block_khrushchevka');
  assert.equal(kit.series, '1-464');
  assert.equal(kit.era, 'khrushchev_1960s');
  const s23 = kitBlock(20.2, 14.1, 2, 4, { pattern: 'sections' }).b;
  assert.equal(s23.plan?.template, 'block_sections');
  assert.notEqual(s23.plan?.pattern, 'kit');
  assert.equal(s23.series, undefined);
  assert.equal(s23.era, 'generic');
  // every template entry names a planner the generator knows and the fields the house-type config needs
  for (const id of ['block_khrushchevka', 'block_sections']) {
    const t = blockTemplate(id);
    assert.ok(['kit_sections', 'sections'].includes(t.planner), id);
  }
  assert.ok(KIT.flats_per_landing === 3 && KIT.storey_cm === 300 && KIT.entrance_side === 'court' && KIT.stair === 'half_landing');
});
