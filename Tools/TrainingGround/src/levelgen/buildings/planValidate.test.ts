// Training Ground / level generator v3 - the PLAN VALIDATOR (S2_ROOMS_PLAN.md 4.2; step S2.2 piece C, planValidate.ts): one failing fixture per code
// (a planned office from the generator path - Draft + addBuilding with building_plans + finalizeDraftToLayout - with exactly one thing broken),
// the unbroken building without any of the new codes, and a property test over the S2.1 footprint range: every planned office passes
// validateBuilding with no error. `npm test`.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { BuildingSpec, Draft, Side } from '../generate.ts';
import { addBuilding, finalizeDraftToLayout } from '../generate.ts';
import type { TraversalCode, TraversalIssue } from '../buildingTraversal.ts';
import { TRAVERSAL_SEVERITY, traversalOk, validateBuilding } from '../buildingTraversal.ts';
import type { GenBuilding, GenLayout, GenPartition, GenRoom, WallOpening } from '../schema.ts';
import { DEFAULT_PARAMS, LEVEL_HEIGHT } from '../schema.ts';
import { snapFootprint } from './bandPlan.ts';
import { PLAN_SEVERITY } from './planValidate.ts';

const NEW_CODES = Object.keys(PLAN_SEVERITY) as TraversalCode[];

// ---- helpers -------------------------------------------------------------------------------------------------------------------------

interface OfficeOpts { w: number; h: number; floors?: number; sides?: Side[]; cores?: BuildingSpec['cores']; seed?: number; bid?: string; x0?: number; y0?: number }
interface Built { l: GenLayout; b: GenBuilding }

/** One planned office (a seeded stand-in for the generator's `random`) in an otherwise empty draft, finalized to a layout. */
function office(o: OfficeOpts): Built {
  const d: Draft = { walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [], building_plans: true };
  let n = (o.seed ?? 1) * 7919;
  const random = (): number => { n = (n * 1103515245 + 12345) & 0x7fffffff; return n / 0x7fffffff; };
  const floors = o.floors ?? 1, x0 = o.x0 ?? 20.37, y0 = o.y0 ?? 15.21, bid = o.bid ?? 'B_t';
  addBuilding(d, bid, [[x0, y0], [x0 + o.w, y0 + o.h]], {
    height: floors * LEVEL_HEIGHT, floors, enterable: true, roof_walkable: false, stairwell: floors > 1, set_piece: 'facade', archetype: 'office',
    doorSides: o.sides ?? ['south'], windowSides: ['south', 'north'], partitionRooms: true, ...(o.cores ? { cores: o.cores } : {}),
  }, random);
  const l = finalizeDraftToLayout(d, [60, 60], { name: 't', profile: 't', seed: 1, params: { ...DEFAULT_PARAMS, size_m: [60, 60], building_plans: true }, start: [4, 2], end: [36, 58] });
  return { l, b: l.buildings.find((x) => x.id === bid)! };
}

const issuesOf = (l: GenLayout, b: GenBuilding): TraversalIssue[] => validateBuilding(l, b).issues;
const newIssues = (issues: TraversalIssue[]): TraversalIssue[] => issues.filter((i) => NEW_CODES.includes(i.code));
const codes = (issues: TraversalIssue[]): string[] => [...new Set(issues.map((i) => i.code))].sort();
const errors = (issues: TraversalIssue[]): string => issues.filter((i) => i.severity === 'error').map((i) => `${i.code}: ${i.message}`).join('; ');

/** A deep copy of the layout with the building edited in place; returns the edited pair. */
function edit(base: Built, fn: (b: GenBuilding, l: GenLayout) => void): Built {
  const l = structuredClone(base.l);
  const b = l.buildings.find((x) => x.id === base.b.id)!;
  fn(b, l);
  return { l, b };
}

const allOps = (b: GenBuilding): { op: WallOpening; part?: GenPartition }[] =>
  [...(b.openings ?? []).map((op) => ({ op })), ...(b.partitions ?? []).flatMap((part) => (part.openings ?? []).map((op) => ({ op, part })))];
const room = (b: GenBuilding, pred: (r: GenRoom) => boolean): GenRoom => b.rooms!.find(pred)!;
/** Removes an opening (exterior or on a partition) and its id from every room's door_ids. */
function removeOpening(b: GenBuilding, id: string): void {
  b.openings = (b.openings ?? []).filter((o) => o.id !== id);
  for (const p of b.partitions ?? []) p.openings = (p.openings ?? []).filter((o) => o.id !== id);
  for (const r of b.rooms ?? []) r.door_ids = (r.door_ids ?? []).filter((x) => x !== id);
}

/** The first planned office of a footprint / seed scan that satisfies `ok` (deterministic; the scan is the fixture's contract). */
function find(label: string, ok: (x: Built) => boolean, opts: (seed: number) => OfficeOpts): Built {
  for (let seed = 1; seed <= 400; seed++) {
    const x = office({ ...opts(seed), seed, bid: `B_f${seed}` });
    if (x.b.plan?.kind === 'bands' && ok(x)) return x;
  }
  throw new Error(`no planned office for the ${label} fixture`);
}

const clean = (x: Built): boolean => newIssues(issuesOf(x.l, x.b)).length === 0;
/** A two-storey office with a core and no issue at all (neither an error nor a new-code warning). */
const base2 = (): Built => find('two-storey', (x) => clean(x) && traversalOk(issuesOf(x.l, x.b)), (seed) => ({ w: 15.3 + (seed % 3), h: 11.6 + (seed % 2), floors: 2, sides: ['south'] }));

// ---- the unbroken building ------------------------------------------------------------------------------------------------------------

test('CodexTactics.LevelGen.PlanValidate.Clean: an unbroken planned office raises none of the new codes; the codes are in TRAVERSAL_SEVERITY', () => {
  for (const code of NEW_CODES) assert.equal(TRAVERSAL_SEVERITY[code], PLAN_SEVERITY[code], code);
  const x = base2();
  assert.equal(x.b.plan?.kind, 'bands');
  assert.deepEqual(newIssues(issuesOf(x.l, x.b)), []);
  // a legacy (not planned) office never gets the plan codes, whatever it looks like
  const d: Draft = { walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [] };
  addBuilding(d, 'B_o', [[20, 15], [35.4, 26.2]], { height: LEVEL_HEIGHT, floors: 1, enterable: true, roof_walkable: false, set_piece: 'facade', archetype: 'office', doorSides: ['south'], windowSides: ['south', 'north'], partitionRooms: true }, () => 0.5);
  const l = finalizeDraftToLayout(d, [60, 60], { name: 'legacy', profile: 't', seed: 1, params: { ...DEFAULT_PARAMS, size_m: [60, 60] }, start: [4, 2], end: [36, 58] });
  assert.equal(l.buildings[0].plan, undefined);
  assert.deepEqual(newIssues(issuesOf(l, l.buildings[0])), []);
});

// ---- one failing fixture per code -----------------------------------------------------------------------------------------------------

/** Asserts that `x` raises `code` (and `severity` for it) while the unbroken building did not. */
function raises(x: Built, code: TraversalCode, severity: 'error' | 'warning' = PLAN_SEVERITY[code as keyof typeof PLAN_SEVERITY]): TraversalIssue[] {
  const hit = issuesOf(x.l, x.b).filter((i) => i.code === code);
  assert.ok(hit.length > 0, `${code} not raised`);
  assert.ok(hit.every((i) => i.severity === severity), `${code}: ${hit.map((i) => i.severity).join(',')} instead of ${severity}`);
  return hit;
}

test('CodexTactics.LevelGen.PlanValidate.room_tiling: a room grown over its neighbour, a room dropped', () => {
  const base = base2();
  const grown = edit(base, (b) => { const r = room(b, (q) => q.kind === 'room' && q.floor === 0); r.bounds.max = [r.bounds.max[0] + 100, r.bounds.max[1] + 100]; });
  raises(grown, 'room_tiling');
  const dropped = edit(base, (b) => { b.rooms = b.rooms!.filter((r) => !(r.kind === 'room' && r.floor === 1)).slice(0, -1); });
  raises(dropped, 'room_tiling');
});

test('CodexTactics.LevelGen.PlanValidate.off_grid: a door centre 20 cm off, a room edge 30 cm off', () => {
  const base = base2();
  const door = edit(base, (b) => { const d = (b.partitions ?? []).flatMap((p) => p.openings ?? []).find((o) => o.role === 'room')!; d.pos = d.pos[0] === (b.partitions!.find((p) => p.openings?.includes(d))!.start[0]) ? [d.pos[0], d.pos[1] + 20] : [d.pos[0] + 20, d.pos[1]]; });
  raises(door, 'off_grid');
  const edge = edit(base, (b) => { const r = room(b, (q) => q.kind === 'room' && q.floor === 0); r.bounds.min = [r.bounds.min[0] + 30, r.bounds.min[1]]; });
  raises(edge, 'off_grid');
});

test('CodexTactics.LevelGen.PlanValidate.room_size: a room shrunk under 400 cm, a service room widened, a stair core cut', () => {
  const base = base2();
  raises(edit(base, (b) => { const r = room(b, (q) => q.kind === 'room' && q.floor === 0); r.bounds.max = [r.bounds.min[0] + 350, r.bounds.max[1]]; }), 'room_size');
  raises(edit(base, (b) => { const r = room(b, (q) => q.kind === 'stairwell' && q.floor === 0); r.bounds.max = [r.bounds.max[0] - 100, r.bounds.max[1] - 100]; }), 'room_size');
});

test('CodexTactics.LevelGen.PlanValidate.room_no_door: every door of a room removed', () => {
  const base = base2();
  const x = edit(base, (b) => {
    const r = room(b, (q) => q.kind === 'room' && q.floor === 1);
    for (const { op } of allOps(b)) if (op.kind === 'door' && op.links?.includes(r.id)) removeOpening(b, op.id!);
  });
  raises(x, 'room_no_door');
});

test('CodexTactics.LevelGen.PlanValidate.opening_no_links: a door without links, a window without id, a link to no room', () => {
  const base = base2();
  raises(edit(base, (b) => { delete allOps(b).find((o) => o.part && o.op.role === 'room')!.op.links; }), 'opening_no_links');
  raises(edit(base, (b) => { delete (b.openings ?? []).find((o) => o.kind === 'window')!.id; }), 'opening_no_links');
  raises(edit(base, (b) => { (b.openings ?? []).find((o) => o.kind === 'window')!.links = ['B_t_r_f0_nowhere', 'street']; }), 'opening_no_links');
});

test('CodexTactics.LevelGen.PlanValidate.storey_no_loop: loops removed (error); a story downgrades it to a warning', () => {
  // a storey with >= 3 rooms whose doors form a tree once the enfilade doors and every second door of a pair are gone
  const base = find('loop', (x) => clean(x) && x.b.rooms!.filter((r) => r.floor === 0 && (r.kind === 'room' || r.kind === 'lobby')).length >= 3, (seed) => ({ w: 14.2 + (seed % 4), h: 10.1 + (seed % 2), floors: 1, sides: ['south'] }));
  const tree = (b: GenBuilding): void => {
    const seen = new Set<string>();
    for (const { op } of allOps(b)) {
      if (op.kind !== 'door' || !op.links || op.links[1] === 'street') continue;
      const key = [...op.links].sort().join('|');
      if (op.role === 'enfilade' || seen.has(key)) removeOpening(b, op.id!); else seen.add(key);
    }
  };
  const x = edit(base, tree);
  assert.ok(allOps(x.b).every((o) => o.op.role !== 'enfilade'));
  const hit = raises(x, 'storey_no_loop', 'error');
  assert.ok(hit.length >= 1);
  const staged = edit(x, (b) => { b.stories = [{ id: 'dead_end_unit', bricks: [], seed: 1, author: 'designer' } as never]; });
  raises(staged, 'storey_no_loop', 'warning');
  assert.ok(!errors(issuesOf(staged.l, staged.b)).includes('storey_no_loop'));
});

test('CodexTactics.LevelGen.PlanValidate.corridor_narrow: 110 cm clear is an error, 170 cm a warning, 180 passes', () => {
  const base = base2();
  const narrow = (short: number) => edit(base, (b) => {
    const r = room(b, (q) => q.kind === 'corridor' && q.floor === 0);
    const wide = r.bounds.max[0] - r.bounds.min[0] <= r.bounds.max[1] - r.bounds.min[1] ? 0 : 1; // the short axis
    r.bounds.max[wide] = r.bounds.min[wide] + short;
  });
  raises(narrow(130), 'corridor_narrow', 'error');
  raises(narrow(190), 'corridor_narrow', 'warning');
  assert.ok(!issuesOf(base.l, base.b).some((i) => i.code === 'corridor_narrow'));
});

/** The footprint edge the stair core stands on and the middle of its first flight's run (building frame, rotation 0 in these fixtures). */
function coreWall(b: GenBuilding): { along: 0 | 1; wall: number; mid: number; flightFrom: number } {
  const st = b.stairs!.find((s) => s.kind === 'stairwell')!;
  const along: 0 | 1 = Math.abs(st.top[0] - st.pos[0]) >= Math.abs(st.top[1] - st.pos[1]) ? 0 : 1, across = 1 - along;
  const lo = b.footprint[0][across], hi = b.footprint[1][across];
  return { along, wall: Math.abs(st.pos[across] - lo) < Math.abs(st.pos[across] - hi) ? lo : hi, mid: (st.pos[along] + st.top[along]) / 2, flightFrom: st.from };
}

test('CodexTactics.LevelGen.PlanValidate.opening_into_core: a window beside a flight, a door opening into the core', () => {
  const base = base2();
  const win = edit(base, (b) => {
    const c = coreWall(b), w = (b.openings ?? []).find((o) => o.kind === 'window' && o.floor === c.flightFrom)!;
    const pos: [number, number] = [0, 0]; pos[c.along] = c.mid; pos[1 - c.along] = c.wall;
    w.pos = pos;
  });
  raises(win, 'opening_into_core');
  const door = edit(base, (b) => { allOps(b).find((o) => o.op.role === 'portal' && o.op.floor === 0)!.op.role = 'room'; });
  raises(door, 'opening_into_core');
});

test('CodexTactics.LevelGen.PlanValidate.stair_pad_unlinked: a portal removed, a portal narrowed to 150', () => {
  const base = base2();
  const portals = (b: GenBuilding, floor: number) => allOps(b).filter((o) => o.op.role === 'portal' && o.op.floor === floor);
  raises(edit(base, (b) => removeOpening(b, portals(b, 0)[0].op.id!)), 'stair_pad_unlinked');
  raises(edit(base, (b) => { portals(b, 1)[1].op.width = 150; }), 'stair_pad_unlinked');
});

test('CodexTactics.LevelGen.PlanValidate.window_rule (warning): a room without its window, a window 100 cm from a partition, two windows 40 cm apart', () => {
  const base = base2();
  const windowsOf = (b: GenBuilding, r: GenRoom) => (b.openings ?? []).filter((o) => o.kind === 'window' && o.links?.[0] === r.id);
  const bare = edit(base, (b) => {
    const r = b.rooms!.find((q) => q.kind === 'room' && q.floor === 1 && windowsOf(b, q).length > 0)!;
    for (const w of windowsOf(b, r)) removeOpening(b, w.id!);
  });
  raises(bare, 'window_rule', 'warning');
  // a window moved along its wall until its edge is 100 cm from the room's partition
  const moved = edit(base, (b) => {
    const r = b.rooms!.find((q) => q.kind === 'room' && q.floor === 0 && windowsOf(b, q).length > 0 && q.walls!.length > 0)!;
    const w = windowsOf(b, r)[0];
    const onX = Math.abs(w.pos[1] - b.footprint[0][1]) < 1 || Math.abs(w.pos[1] - b.footprint[1][1]) < 1; // the wall runs along x
    const along = onX ? 0 : 1, lo = r.bounds.min[along];
    w.pos = onX ? [lo + 100 + 75, w.pos[1]] : [w.pos[0], lo + 100 + 75];
  });
  raises(moved, 'window_rule', 'warning');
  // a second window next to an existing one
  const twin = edit(base, (b) => {
    const w = (b.openings ?? []).find((o) => o.kind === 'window' && b.rooms!.find((r) => r.id === o.links![0])!.kind === 'room')!;
    const onX = Math.abs(w.pos[1] - b.footprint[0][1]) < 1 || Math.abs(w.pos[1] - b.footprint[1][1]) < 1;
    b.openings!.push({ ...w, id: `${w.id}_twin`, pos: onX ? [w.pos[0] + 190, w.pos[1]] : [w.pos[0], w.pos[1] + 190] });
  });
  raises(twin, 'window_rule', 'warning');
});

test('CodexTactics.LevelGen.PlanValidate.CorridorEnd: a corridor-end window raises neither window_rule nor window_partition_clash on a planned building', () => {
  const x = find('corridor end', (y) => (y.b.openings ?? []).some((o) => o.kind === 'window' && y.b.rooms!.find((r) => r.id === o.links?.[0])?.kind === 'corridor'), (seed) => ({ w: 15.3 + (seed % 3), h: 11.6, floors: 1, sides: ['south'] }));
  const ends = new Set((x.b.openings ?? []).filter((o) => o.kind === 'window' && x.b.rooms!.find((r) => r.id === o.links?.[0])?.kind === 'corridor').map((o) => o.id));
  assert.ok(ends.size > 0);
  const issues = issuesOf(x.l, x.b).filter((i) => (i.code === 'window_partition_clash' || i.code === 'window_rule') && ends.has(i.ref));
  assert.deepEqual(issues, [], 'no clash or rule warning on a corridor-end window');
  assert.ok(!issuesOf(x.l, x.b).some((i) => i.code === 'window_partition_clash'), 'and no window_partition_clash at all on this planned building');
});

// ---- the property test -----------------------------------------------------------------------------------------------------------------

/** The S2.1 footprints (800..1800 x 900..1400 in 100 steps, both orders through snapFootprint), once each. */
function footprints(): { L: number; D: number }[] {
  const seen = new Map<string, { L: number; D: number }>();
  for (let w = 800; w <= 1800; w += 100) for (let d = 900; d <= 1400; d += 100) { const a = snapFootprint(w, d); seen.set(`${a.L}x${a.D}`, { L: a.L, D: a.D }); }
  return [...seen.values()];
}

test('CodexTactics.LevelGen.PlanValidate.Property: every planned office over the S2.1 footprint range passes validateBuilding with no error', () => {
  const started = Date.now();
  const tally: Record<string, number> = {};
  const fallbacks: Record<string, number> = {};
  const variants: Record<string, number> = {};
  const seeds = new Set<number>();
  let total = 0, planned = 0, run = 0;
  const failures: string[] = [];
  for (const { L, D } of footprints()) {
    for (const alongY of [false, true, false, true]) { // each orientation twice (different seeds and door positions)
      // door sides: one long side, one short side, two (opposite long) sides
      const long: Side[] = alongY ? ['west', 'east'] : ['north', 'south'], short: Side[] = alongY ? ['north', 'south'] : ['west', 'east'];
      for (const cfg of [{ floors: 1, cores: undefined }, { floors: 2, cores: undefined }, { floors: 2, cores: ['high'] as BuildingSpec['cores'] }]) { // 1 storey without a core, 2 with a core at the low / the high end
        for (const sides of [[long[run % 2]], [short[run % 2]], [long[0], long[1]]] as Side[][]) {
          run++;
          const seed = 1 + ((run * 13) % 200);
          seeds.add(seed);
          const w = (alongY ? D : L) / 100 + 0.37, h = (alongY ? L : D) / 100 + 0.61; // off-grid sizes snap down to the 100 grid
          const { l, b } = office({ w, h, floors: cfg.floors, cores: cfg.cores, sides, seed, bid: `B_p${seed}`, x0: 12.37 + (seed % 7) * 0.01, y0: 9.21 + (seed % 5) * 0.01 });
          total++;
          if (b.plan?.kind !== 'bands') { const k = b.plan?.fallback ?? 'none'; fallbacks[k.split(' | ')[0].replace(/\d+/g, '#')] = (fallbacks[k.split(' | ')[0].replace(/\d+/g, '#')] ?? 0) + 1; continue; }
          planned++;
          variants[b.plan.variant ?? '?'] = (variants[b.plan.variant ?? '?'] ?? 0) + 1;
          const issues = issuesOf(l, b);
          for (const i of issues) tally[`${i.severity}:${i.code}`] = (tally[`${i.severity}:${i.code}`] ?? 0) + 1;
          if (!traversalOk(issues)) failures.push(`${L}x${D} ${alongY ? 'y' : 'x'}-long f${cfg.floors} ${sides} seed ${seed}: ${errors(issues)}`);
        }
      }
    }
  }
  console.log(`plan validator property: ${planned} planned of ${total} offices (fallback share ${Math.round((100 * (total - planned)) / total)}%; fallbacks ${JSON.stringify(fallbacks)}), `
    + `variants ${JSON.stringify(variants)}, ${seeds.size} distinct seeds, ${Date.now() - started} ms; issues ${JSON.stringify(tally)}`);
  assert.equal(failures.length, 0, `${failures.length} planned offices fail:\n${failures.slice(0, 12).join('\n')}`);
  assert.ok(planned >= total * 0.5, `planned ${planned} of ${total}`);
  assert.equal(seeds.size, 200, 'every seed 1-200 is used');
});
