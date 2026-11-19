// Training Ground / level generator v3 - the `building_plans` toggle (S2_ROOMS_PLAN.md S2.2 piece B: generate.ts addPlannedOffice, schema.ts,
// the page / CLI toggle). Proofs: (1) toggle OFF = the output byte for byte (scripts/plans-golden.ts, hashes recorded before any S2.2
// change); (2) toggle ON on the depot (x1 seed 1): B_office_s is a 'bands' plan on schema /2, every opening has id / links / role /
// module, two runs are equal, and every other building is untouched - which also proves the shared `random` stream is drawn exactly once
// per exterior door side; (3) the emitted building is consistent (rooms tile each storey, openings sit on their walls and link rooms
// that touch them, the pad portals meet the core's approach boxes for a core at either end, windows keep clear of a neighbour outside,
// no validator error on a sweep of footprints / orientations / storeys / door sides); (4) a plan that does not fit falls back to the
// legacy stages byte for byte; (5) the templates / expansion carry the toggle. `npm test`.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { buildTemplate, templateContext } from './blockTemplates.ts';
import { traversalOk, validateBuilding } from './buildingTraversal.ts';
import { expandLayout } from './expand.ts';
import type { Draft, Side } from './generate.ts';
import { addBuilding, finalizeDraftToLayout, generateLayout } from './generate.ts';
import { effectiveOpeningState } from './openingStates.ts';
import type { Profile } from './profile.ts';
import type { BuildingSpec } from './generate.ts';
import type { GenBuilding, GenLayout, WallOpening } from './schema.ts';
import { DEFAULT_PARAMS, LEVEL_HEIGHT, LEVELGEN_SCHEMA, LEVELGEN_SCHEMA_V2, validateGenLayout } from './schema.ts';

const TG = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const ROOT = path.resolve(TG, '../..');
const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;

// ---- helpers ----------------------------------------------------------------------------------------------------------------------

interface OfficeOpts { x0?: number; y0?: number; w: number; h: number; floors?: number; sides?: Side[]; plans: boolean; cores?: BuildingSpec['cores']; seed?: number; before?: (d: Draft) => void }

/** One office in an otherwise empty draft (a seeded stand-in for the generator's `random`, counted), finalized to a layout. */
function office(o: OfficeOpts): { d: Draft; l: GenLayout; b: GenBuilding; draws: number } {
  const d: Draft = { walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [], ...(o.plans ? { building_plans: true } : {}) };
  o.before?.(d);
  let n = (o.seed ?? 1) * 7919, draws = 0;
  const random = () => { draws++; n = (n * 1103515245 + 12345) & 0x7fffffff; return n / 0x7fffffff; };
  const floors = o.floors ?? 1, x0 = o.x0 ?? 20.37, y0 = o.y0 ?? 15.21;
  addBuilding(d, 'B_t', [[x0, y0], [x0 + o.w, y0 + o.h]], {
    height: floors * LEVEL_HEIGHT, floors, enterable: true, roof_walkable: false, stairwell: floors > 1, set_piece: 'facade', archetype: 'office',
    doorSides: o.sides ?? ['south'], windowSides: ['south', 'north'], partitionRooms: true, ...(o.cores ? { cores: o.cores } : {}),
  }, random);
  const l = finalizeDraftToLayout(d, [60, 60], { name: 't', profile: 't', seed: 1, params: { ...DEFAULT_PARAMS, size_m: [60, 60], ...(o.plans ? { building_plans: true } : {}) }, start: [4, 2], end: [36, 58] });
  return { d, l, b: l.buildings.find((x) => x.id === 'B_t')!, draws };
}

type Vec = readonly [number, number];
const near = (a: number, b: number, tol = 1): boolean => Math.abs(a - b) <= tol;
/** Distance from a point to the axis-aligned rectangle [min, max] (0 inside). */
const distToRect = (p: Vec, min: Vec, max: Vec): number => Math.hypot(Math.max(min[0] - p[0], 0, p[0] - max[0]), Math.max(min[1] - p[1], 0, p[1] - max[1]));
const allOpenings = (b: GenBuilding): { op: WallOpening; part?: NonNullable<GenBuilding['partitions']>[number] }[] =>
  [...(b.openings ?? []).map((op) => ({ op })), ...(b.partitions ?? []).flatMap((part) => (part.openings ?? []).map((op) => ({ op, part })))];

/** The structural proofs of one planned building (UE cm, 1 cm tolerance). */
function checkStructure(b: GenBuilding, label: string): void {
  const rooms = b.rooms ?? [], [fmin, fmax] = b.footprint;
  const roomIds = new Set(rooms.map((r) => r.id));
  assert.equal(roomIds.size, rooms.length, `${label}: room ids are unique`);
  assert.ok((b.floors_detail ?? []).every((fd) => (fd.rooms ?? []).length === 0), `${label}: floors_detail[].rooms stays empty`);
  // rooms tile every storey: no overlap, the area adds up to the footprint
  for (let f = 0; f < b.floors; f++) {
    const rs = rooms.filter((r) => r.floor === f);
    let area = 0;
    for (const r of rs) area += (r.bounds.max[0] - r.bounds.min[0]) * (r.bounds.max[1] - r.bounds.min[1]);
    const fp = (fmax[0] - fmin[0]) * (fmax[1] - fmin[1]);
    assert.ok(Math.abs(area - fp) <= fp * 0.002, `${label} f${f}: the rooms cover ${area} of ${fp} cm2`);
    for (let i = 0; i < rs.length; i++) for (let j = i + 1; j < rs.length; j++) {
      const a = rs[i].bounds, c = rs[j].bounds;
      const ov = Math.min(a.max[0], c.max[0]) - Math.max(a.min[0], c.min[0]), ow = Math.min(a.max[1], c.max[1]) - Math.max(a.min[1], c.min[1]);
      assert.ok(ov <= 1 || ow <= 1, `${label} f${f}: ${rs[i].id} overlaps ${rs[j].id}`);
    }
  }
  const ops = allOpenings(b), seen = new Set<string>();
  for (const { op, part } of ops) {
    const tag = `${label} ${op.id}`;
    assert.ok(op.id && op.links && op.role && op.module, `${tag}: id / links / role / module`);
    assert.ok(!seen.has(op.id!), `${tag}: unique id`);
    seen.add(op.id!);
    const [a, c] = op.links!;
    assert.ok(roomIds.has(a), `${tag}: link ${a} is a room`);
    assert.ok(c === 'street' || roomIds.has(c), `${tag}: link ${c} is a room or the street`);
    for (const id of op.links!) {
      if (id === 'street') continue;
      const room = rooms.find((r) => r.id === id)!;
      assert.equal(room.floor, op.floor, `${tag}: links a room of its own storey`);
      assert.ok(distToRect(op.pos, room.bounds.min, room.bounds.max) <= 1, `${tag}: touches ${id}`);
      assert.ok(room.door_ids?.includes(op.id!), `${tag}: listed in ${id}.door_ids`);
    }
    // the centre sits on the 50 grid along its wall (measured from the footprint's low corner), on the wall line
    let axis: 0 | 1;
    if (part) {
      axis = near(part.start[0], part.end[0]) ? 1 : 0;
      const line = axis === 1 ? part.start[0] : part.start[1];
      assert.ok(near(op.pos[1 - axis], line), `${tag}: on its partition line`);
      const lo = Math.min(part.start[axis], part.end[axis]), hi = Math.max(part.start[axis], part.end[axis]);
      assert.ok(op.pos[axis] - (op.width ?? 0) / 2 >= lo - 1 && op.pos[axis] + (op.width ?? 0) / 2 <= hi + 1, `${tag}: inside its partition`);
      assert.notEqual(a, c, `${tag}: joins two rooms`);
      assert.ok(op.kind === 'door' && op.wall === undefined, `${tag}: interior = a door record without a wall id`);
    } else {
      const onX = near(op.pos[0], fmin[0]) || near(op.pos[0], fmax[0]), onY = near(op.pos[1], fmin[1]) || near(op.pos[1], fmax[1]);
      assert.ok(onX !== onY, `${tag}: on exactly one footprint edge`);
      axis = onX ? 1 : 0;
      assert.match(op.wall ?? '', new RegExp(`^${b.id}_[NSWE]$`), `${tag}: wall id`);
      assert.equal(c, 'street', `${tag}: an exterior opening links the street`);
    }
    const rel = op.pos[axis] - fmin[axis];
    assert.ok(Math.abs(rel / 50 - Math.round(rel / 50)) * 50 <= 1.01, `${tag}: centre on the 50 grid (${rel})`);
  }
  // every window carries its mantle edge
  for (const { op } of ops.filter((x) => x.op.kind === 'window')) assert.ok(b.mantle_edges?.some((m) => m.id === `me_${op.id}`), `${label} ${op.id}: mantle edge`);
  // a door gap in the perimeter wall per exterior door is the caller's job (buildPerimeterWalls); the plan carries its record
  assert.equal(b.plan?.kind, 'bands', `${label}: plan record`);
  assert.equal(b.doors.length, (b.openings ?? []).filter((x) => x.kind === 'door').length, `${label}: doors mirror the exterior door openings`);
}

// ---- 1. toggle off = byte for byte ------------------------------------------------------------------------------------------------

test('toggle OFF: the golden hashes of x1 / x6, seeds 1-3, are unchanged (scripts/plans-golden.ts)', () => {
  const out = execFileSync(process.execPath, ['scripts/plans-golden.ts', '--check', 'src/levelgen/fixtures/plans_golden_off.json'], { cwd: TG, encoding: 'utf8' });
  assert.match(out, /^golden ok/);
});

test('toggle OFF: an office draft is the /1 output - schema /1, no plan, no link / role fields', () => {
  const { l, b } = office({ w: 15.4, h: 11.2, plans: false });
  assert.equal(l.schema, LEVELGEN_SCHEMA);
  assert.equal(b.plan, undefined);
  assert.ok(allOpenings(b).every(({ op }) => op.links === undefined && op.role === undefined && op.module === undefined));
  assert.equal(l.params.building_plans, undefined);
});

// ---- 2. the depot with the toggle on --------------------------------------------------------------------------------------------

test('toggle ON (depot x1 seed 1): B_office_s is a bands plan on schema /2 with linked, roled openings; deterministic; every other building untouched', () => {
  const on = generateLayout({ profile, seed: 1, params: { building_plans: true } });
  const off = generateLayout({ profile, seed: 1 });
  assert.equal(on.schema, LEVELGEN_SCHEMA_V2);
  assert.equal(off.schema, LEVELGEN_SCHEMA);
  assert.equal(on.params.building_plans, true);
  assert.equal(off.params.building_plans, undefined);
  const b = on.buildings.find((x) => x.id === 'B_office_s')!;
  assert.equal(b.plan?.kind, 'bands');
  assert.equal(b.plan?.v, 1);
  assert.match(b.plan!.core_wall, /^B_office_s_[NSWE]$/);
  assert.match(b.plan!.street_wall, /^B_office_s_[NSWE]$/);
  checkStructure(b, 'B_office_s');
  const issues = validateBuilding(on, b).issues;
  assert.ok(traversalOk(issues), issues.filter((i) => i.severity === 'error').map((i) => i.message).join('; '));
  assert.deepEqual(validateGenLayout(on), validateGenLayout(off), 'the planned layout is as valid as the /1 one');
  // determinism
  assert.deepEqual(generateLayout({ profile, seed: 1, params: { building_plans: true } }), on);
  // one random() per exterior door side keeps the shared stream aligned: the buildings placed after the office are identical
  assert.deepEqual(on.buildings.filter((x) => x.id !== 'B_office_s'), off.buildings.filter((x) => x.id !== 'B_office_s'));
  // the opening-state defaults of schema /2: the exterior door closed, interior doors open, windows glazed
  for (const { op, part } of allOpenings(b)) {
    const st = effectiveOpeningState(on, b, op);
    assert.equal(st, op.kind === 'window' ? 'glazed' : part ? 'open' : 'closed', op.id);
  }
});

// ---- 3. the emitted building --------------------------------------------------------------------------------------------------------

test('a sweep of planned offices (both orientations, 1-2 storeys, door sides, seeds) is consistent and has no validator error; most are planned, not fallbacks', () => {
  let planned = 0, total = 0;
  const fallbacks: Record<string, number> = {};
  for (const [w, h] of [[15.3, 11.6], [11.4, 15.8], [18.1, 9.7], [9.2, 18.6], [12.5, 10.5], [17.9, 13.2]]) {
    for (const floors of [1, 2]) {
      for (const sides of [['south'], ['east'], ['north', 'south'], ['west']] as Side[][]) {
        for (const seed of [1, 2, 3]) {
          const { b, l } = office({ w, h, floors, sides, plans: true, seed });
          total++;
          if (b.plan?.kind !== 'bands') { fallbacks[b.plan?.fallback ?? '?'] = (fallbacks[b.plan?.fallback ?? '?'] ?? 0) + 1; continue; }
          planned++;
          const label = `${w}x${h} f${floors} ${sides} s${seed}`;
          checkStructure(b, label);
          const issues = validateBuilding(l, b).issues;
          assert.ok(traversalOk(issues), `${label}: ${issues.filter((i) => i.severity === 'error').map((i) => i.message).join('; ')}`);
        }
      }
    }
  }
  console.log(`plansToggle sweep: ${planned} planned of ${total}; fallbacks ${JSON.stringify(fallbacks)}`);
  assert.ok(planned >= total * 0.7, `planned ${planned} of ${total}`);
});

test('the first exterior door is a 300 module on the street wall; the lobby / room kinds, units and exterior walls of the rooms', () => {
  const { b } = office({ w: 15.3, h: 11.6, plans: true, sides: ['south'], seed: 5 });
  assert.equal(b.plan?.kind, 'bands');
  const entries = (b.openings ?? []).filter((o) => o.role === 'entry');
  assert.ok(entries.length >= 1);
  assert.equal(entries[0].module, 'wall_door_300');
  assert.ok(entries.slice(1).every((o) => o.module === 'wall_door_200'));
  assert.equal(entries[0].wall, b.plan!.street_wall, 'plan.street_wall = the wall of the first entry');
  const lobbies = (b.rooms ?? []).filter((r) => r.kind === 'lobby');
  assert.ok(lobbies.length >= 1 && lobbies.every((r) => r.floor === 0), 'lobbies exist on storey 0 only');
  for (const o of entries) assert.ok(lobbies.some((r) => r.id === o.links![0]) || (b.rooms ?? []).find((r) => r.id === o.links![0])!.kind === 'corridor');
  for (const r of b.rooms ?? []) assert.ok(Array.isArray(r.walls) && r.walls.every((w) => /^B_t_[NSWE]$/.test(w)));
  assert.ok((b.rooms ?? []).some((r) => r.kind === 'corridor'), 'a corridor');
});

test('two storeys with a core: the stair-core cell is a stairwell room per storey, both pad portals open onto the pads - for a core at the low end and at the high end', () => {
  for (const cores of [undefined, ['high'] as BuildingSpec['cores']]) {
    for (const [w, h, sides] of [[15.3, 11.6, ['south']], [11.4, 15.8, ['east']], [14.2, 12.4, ['north']]] as [number, number, Side[]][]) {
      const label = `${w}x${h} ${sides} cores ${JSON.stringify(cores)}`;
      const { b } = office({ w, h, floors: 2, sides, plans: true, cores, seed: 2 });
      assert.equal(b.plan?.kind, 'bands', `${label}: ${b.plan?.fallback}`);
      assert.equal((b.rooms ?? []).filter((r) => r.kind === 'stairwell').length, 2, `${label}: a stairwell room per storey`);
      const portals = (b.partitions ?? []).flatMap((p) => p.openings ?? []).filter((o) => o.role === 'portal');
      assert.equal(portals.length, 4, `${label}: two portals per storey`);
      assert.ok(portals.every((o) => o.width === 200 && o.module === 'portal_200' && o.sill_height === 0), `${label}: portal size`);
      // the pads: 200 deep before the first foot and after its top, along the core's axis; each pad meets a portal over >= 150 cm
      const st = b.stairs[0], axis = near(st.pos[0], st.top[0]) ? 1 : 0;
      const lo = Math.min(st.pos[axis], st.top[axis]), hi = Math.max(st.pos[axis], st.top[axis]);
      for (const f of [0, 1]) {
        const cs = portals.filter((o) => o.floor === f).map((o) => o.pos[axis]);
        for (const pad of [lo - 100, hi + 100]) {
          assert.ok(cs.some((c) => Math.abs(c - pad) <= 50), `${label} f${f}: a portal within 50 cm of the pad centre ${pad} (portals ${cs})`);
        }
      }
      // nothing else opens into the stairwell cell
      const stair = (b.rooms ?? []).filter((r) => r.kind === 'stairwell').map((r) => r.id);
      for (const { op } of allOpenings(b)) if (op.role !== 'portal') assert.ok(!op.links!.some((x) => stair.includes(x)), `${label}: ${op.id} opens into the core`);
    }
  }
});

test('a neighbour standing within 1 m outside a wall blocks the windows along it', () => {
  const nx = 20.9, base = office({ w: 15.3, h: 11.6, plans: true, seed: 4 });
  const south = (b: GenBuilding) => (b.openings ?? []).filter((o) => o.kind === 'window' && o.wall === 'B_t_S');
  assert.ok(south(base.b).length >= 3, 'the unblocked south wall has windows');
  const withNeighbour = office({
    w: 15.3, h: 11.6, plans: true, seed: 4,
    before: (d) => { addBuilding(d, 'B_n', [[nx, 15.21 + 11 + 0.5], [nx + 4, 15.21 + 11 + 4]], { height: LEVEL_HEIGHT, floors: 1, hollow: true, enterable: false, roof_walkable: false, set_piece: 'chimney' }, () => 0.5); },
  });
  const ws = south(withNeighbour.b);
  assert.ok(ws.length < south(base.b).length, 'a window or more went');
  // the neighbour (draft x in [nx, nx + 4]; the footprint snaps 11.6 m to 11, so it stands 0.5 m outside the wall) in the layout's own frame
  const nb = withNeighbour.l.buildings.find((x) => x.id === 'B_n')!;
  const iv: [number, number] = [Math.min(nb.footprint[0][1], nb.footprint[1][1]), Math.max(nb.footprint[0][1], nb.footprint[1][1])];
  for (const o of ws) assert.ok(o.pos[1] + 75 <= iv[0] + 1e-6 || o.pos[1] - 75 >= iv[1] - 1e-6, `${o.id} does not face the neighbour`);
});

// ---- 4. fallbacks run the legacy stages byte for byte -----------------------------------------------------------------------------------

test('a plan that does not fit keeps the legacy output byte for byte (plus the plan record) and draws the same random numbers', () => {
  for (const [w, h, floors, cores, why] of [[7.4, 6.3, 1, undefined, /footprint_too_small/], [12.3, 40.2, 3, 2, /^multi_core$/]] as [number, number, number, BuildingSpec['cores'], RegExp][]) {
    const on = office({ w, h, floors, plans: true, sides: ['south'], cores, seed: 3 });
    const off = office({ w, h, floors, plans: false, sides: ['south'], cores, seed: 3 });
    assert.equal(on.b.plan?.kind, 'bsp');
    assert.match(on.b.plan!.fallback!, why);
    assert.equal(on.draws, off.draws, 'the same number of draws (nothing is drawn before the fallback)');
    const { plan: _plan, ...rest } = on.b;
    assert.deepEqual(rest, off.b);
    assert.deepEqual(on.l.walls, off.l.walls);
  }
});

// ---- 5. the toggle reaches templates and expansions --------------------------------------------------------------------------------------

test('the template context and the expansion carry the toggle; a built small_office is planned', () => {
  const on = generateLayout({ profile, seed: 1, params: { building_plans: true } });
  const off = generateLayout({ profile, seed: 1 });
  assert.equal(templateContext(on, 5).building_plans, true);
  assert.equal(templateContext(off, 5).building_plans, undefined);
  // a 20 x 14 m lot in the middle of the map, UE cm
  const lot = { id: 'LOT_T', rect: [[-700, -1000], [700, 1000]] as [[number, number], [number, number]], template: '' };
  const built = buildTemplate('small_office', lot, templateContext(on, 5));
  const offices = built.content.buildings.filter((b) => b.archetype === 'office');
  assert.ok(offices.length >= 1 && offices.every((b) => b.plan !== undefined), 'the template office carries a plan record');
  const plain = buildTemplate('small_office', lot, templateContext(off, 5));
  assert.ok(plain.content.buildings.every((b) => b.plan === undefined));
  const x = expandLayout({ layout: on, factor: 4, seed: 7, profile });
  assert.ok(x.ok);
  assert.equal(x.layout.schema, LEVELGEN_SCHEMA_V2);
  assert.equal(x.layout.params.building_plans, true);
});
