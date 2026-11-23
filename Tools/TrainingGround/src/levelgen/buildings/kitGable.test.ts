// Training Ground / level generator v3 - the khrushchevka GABLE DOOR and BALCONY SLOTS (designer decision 2026-11-22; template block_khrushchevka
// gable_door / balcony_slots in building_plans.json; kitPlan.ts gableVestibule, planDoors.ts kitEntrances, planWindows.ts markKitSlots):
// (1) the plan - a gable door per the template's ends (low / high / both / none, both from both_min_sections), its SERVICE ROOM (designer 2026-11-22: the
// ground storey of the end flat is a non-residential service bay - the cell at the gable's court corner is the main room, entered by the gable door, label
// by the plan seed), the reason in plan notes; balcony columns per the rate; (2) the generated block - the gable door is an exterior ground door (role entry)
// into a service room whose stair door leads to the tambour / floor landing, no flat loses its kitchen, the upper storeys keep three flats per landing, slots on
// the street living rooms from storey 1; (3) the stories - F4 holds for story 3 on a khrushchevka (the gable door = the quiet entry), story 9 locks the gable
// door itself (building_stories.json gable_door_state) so 3 + 9 hold with no manual override, story 4 places its balcony + fire escape. `npm test`.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { validateBuilding } from '../buildingTraversal.ts';
import { computeBuildingRoutes, routeCostHash } from '../buildingRoutes.ts';
import { withRoles } from '../buildingRoles.ts';
import type { StoryId } from '../buildingStories.ts';
import { applyStory, storySubSeed } from '../buildingStories.ts';
import type { BuildingSpec, Draft } from '../generate.ts';
import { addBuilding, finalizeDraftToLayout } from '../generate.ts';
import type { GenBuilding, GenLayout, Side } from '../schema.ts';
import { DEFAULT_PARAMS, LEVEL_HEIGHT, LEVELGEN_SCHEMA_V2 } from '../schema.ts';
import { KIT, planKitBlock, validateKitPlan } from './kitPlan.ts';

// ---- (1) the plan ----------------------------------------------------------------------------------------------------------------------

test('CodexTactics.LevelGen.KitGable.Plan: the template ends give one gable door at u = 0; high / both / none; the service bay is the whole end flat, the door in its kitchen cell at the court corner', () => {
  assert.equal(KIT.gable_door.ends, 'low', 'template default');
  const labels = new Set(KIT.gable_door.service_room.types.map((t) => t.label));
  const seen = new Set<string>();
  for (const [L, D] of [[2000, 1200], [1800, 1000], [3700, 1200]] as const) {
    for (const seed of [1, 4, 7, 9, 12]) {
      const p = planKitBlock({ L, D, seed });
      assert.equal(p.kind, 'bands', `${L} x ${D}`);
      assert.deepEqual(validateKitPlan(p), []);
      const g = p.kit!.gables!;
      assert.equal(g.length, 1, `${L} x ${D} seed ${seed}: one gable door`);
      assert.equal(g[0].wall, 'u0');
      assert.equal(g[0].section, 0);
      const byId = new Map(p.cells.map((c) => [c.id, c]));
      assert.ok(g[0].cells.every((id) => byId.get(id)!.unit === g[0].unit), 'one flat');
      assert.deepEqual([...g[0].cells].sort(), p.cells.filter((c) => c.unit === g[0].unit).map((c) => c.id).sort(), 'the whole end flat (no partial flat is left)');
      const door = byId.get(g[0].door)!;
      assert.equal(door.use, 'kitchen');
      assert.equal(door.u0, 0, 'on the gable');
      assert.equal(door.v0, 0, 'at the court corner');
      assert.ok(g[0].c - 75 >= door.v0 && g[0].c + 75 <= door.v1 && g[0].c % 50 === 0, `door centre ${g[0].c} in ${door.v0}..${door.v1}`);
      assert.ok(labels.has(g[0].labels[g[0].door]), 'the main room is one of the template service types');
      seen.add(g[0].type);
      for (const id of g[0].cells) {
        const u = byId.get(id)!.use;
        assert.equal(id in g[0].labels, u !== 'hall' && u !== 'corridor', `${id} (${u}): a label unless it is a passage`);
      }
      assert.ok(p.kit!.notes.some((n) => n.startsWith('gable door (u0)') && n.includes(g[0].unit) && n.includes(g[0].labels[g[0].door])), 'the service bay is recorded');
    }
  }
  assert.ok(seen.size >= 2, `the seed picks the service type (${[...seen].join(', ')})`);
  const hi = planKitBlock({ L: 2000, D: 1200, seed: 4, gable: 'high' });
  assert.deepEqual(hi.kit!.gables!.map((g) => g.wall), ['uL']);
  assert.equal(hi.cells.find((c) => c.id === hi.kit!.gables![0].door)!.u1, 2000);
  const both = planKitBlock({ L: 3700, D: 1200, seed: 4, gable: 'both' });
  assert.deepEqual(both.kit!.gables!.map((g) => [g.wall, g.section]), [['u0', 0], ['uL', 1]]);
  assert.deepEqual(validateKitPlan(both), []);
  assert.deepEqual(planKitBlock({ L: 2000, D: 1200, seed: 4, gable: 'none' }).kit!.gables, []);
  // the same plan otherwise: the gable door adds no draw to the plan's stream
  const a = planKitBlock({ L: 2000, D: 1200, seed: 4, gable: 'none' }), b = planKitBlock({ L: 2000, D: 1200, seed: 4 });
  assert.deepEqual(a.cells, b.cells);
  assert.deepEqual(a.kit!.sections, b.kit!.sections);
  assert.ok(KIT.gable_door.both_min_sections >= 2);
});

test('CodexTactics.LevelGen.KitGable.BalconyColumns: the template rate picks the flats whose street rooms carry slots (rate 1 = every flat)', () => {
  const p = planKitBlock({ L: 2000, D: 1200, seed: 4 });
  const flats = [...new Set(p.cells.map((c) => c.unit).filter((u): u is string => !!u))].sort();
  assert.equal(KIT.balcony_slots.rate, 1);
  assert.deepEqual(p.kit!.balconyColumns, flats);
});

// ---- (2) the generated block -----------------------------------------------------------------------------------------------------------

interface Built { l: GenLayout; b: GenBuilding }

/** A khrushchevka in a layout with a mandatory street at y 55 (top-row building y 30..42); `street` = its street wall (the court is opposite). */
function block(o: { w?: number; floors?: number; street?: Side; gable?: BuildingSpec['gable_door']; state?: BuildingSpec['gable_door_state']; seed?: number } = {}): Built {
  const d: Draft = { walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [], building_plans: true };
  let n = (o.seed ?? 11) * 7919;
  const random = (): number => { n = (n * 1103515245 + 12345) & 0x7fffffff; return n / 0x7fffffff; };
  const floors = o.floors ?? 2, street = o.street ?? 'north';
  addBuilding(d, 'B', [[46, 30], [46 + (o.w ?? 20.2), 42.2]], {
    height: floors * LEVEL_HEIGHT, floors, enterable: true, roof_walkable: true, stairwell: true, set_piece: 'facade', archetype: 'block',
    doorSides: [street], windowSides: [street], partitionRooms: true, plan_template: 'block_khrushchevka',
    ...(o.gable ? { gable_door: o.gable } : {}), ...(o.state ? { gable_door_state: o.state } : {}),
  }, random);
  d.routes.push({ id: 'R', pace: 'walk', points: [[4, 55], [146, 55]], mandatory: true, layers: ['ground', 'ground'], links: [], set_pieces: ['street'] });
  const base = finalizeDraftToLayout(d, [150, 110], { name: 't', profile: 'ref_01_02_depot', seed: 1, params: { ...DEFAULT_PARAMS, size_m: [150, 110], building_plans: true }, start: [4, 55], end: [146, 55], yaw: 0 });
  const l = withRoles({ ...base, schema: LEVELGEN_SCHEMA_V2, cost_hash: routeCostHash() });
  return { l, b: l.buildings[0] };
}

const groundDoors = (b: GenBuilding) => (b.openings ?? []).filter((o) => o.kind === 'door' && (o.floor ?? 0) === 0 && o.role === 'entry');
const errors = (l: GenLayout, b: GenBuilding): string[] => validateBuilding(l, b).issues.filter((i) => i.severity === 'error').map((i) => `${i.code}: ${i.message}`);

test('CodexTactics.LevelGen.KitGable.Building: the gable door is an exterior ground entry on the end wall into a service room with a stair door; no flat loses its kitchen; upper storeys keep three flats per landing; 0 validator errors', () => {
  for (const gable of ['west', 'east'] as const) {
    const { l, b } = block({ gable, floors: 3 });
    assert.equal(b.plan?.template, 'block_khrushchevka');
    const doors = groundDoors(b);
    assert.equal(doors.length, 2, 'the court door + the gable door');
    const gd = doors.find((o) => o.wall === `B_${gable[0].toUpperCase()}`);
    assert.ok(gd, `a door on the ${gable} gable`);
    assert.equal(gd!.links?.[1], 'street');
    assert.equal(gd!.module, 'wall_door_200');
    const rooms = new Map((b.rooms ?? []).map((r) => [r.id, r]));
    const svc = rooms.get(gd!.links![0])!;
    assert.equal(svc.kind, 'service', 'the gable door enters a service room, not a lobby / a flat');
    assert.equal(svc.use, 'service');
    assert.equal(svc.zone, 'service');
    assert.equal(svc.unit, undefined, 'no flat');
    assert.ok(KIT.gable_door.service_room.types.some((t) => t.label === svc.label), `label ${svc.label}`);
    // the service room -> stair door: the bay's passage joins the section's tambour / floor landing by one door (the end flat's entry, now a plain room door)
    const bay = (b.rooms ?? []).filter((r) => r.floor === 0 && r.zone === 'service');
    assert.ok(bay.length >= 4, 'the whole end flat is the service bay');
    const bayIds = new Set(bay.map((r) => r.id));
    const interior = (b.partitions ?? []).flatMap((p) => p.openings ?? []);
    const stairDoor = interior.filter((o) => o.kind === 'door' && o.floor === 0 && o.links?.some((x) => bayIds.has(x)) && o.links.some((x) => !bayIds.has(x)));
    assert.equal(stairDoor.length, 1, 'exactly one interior door from the bay to the rest');
    const outside = rooms.get(stairDoor[0].links!.find((x) => !bayIds.has(x))!)!;
    assert.ok(outside.use === 'tambour' || outside.use === 'landing', `the stair door leads to the tambour / floor landing (${outside.use})`);
    assert.equal(stairDoor[0].role, 'room', 'not a flat entry any more');
    // storey 0: no flat without a kitchen; the end flat is no flat there and is a flat again upstairs (three per landing)
    for (const u of (b.units ?? []).filter((x) => x.floor === 0)) assert.ok(u.rooms.some((r) => rooms.get(r)?.use === 'kitchen'), `${u.id} keeps its kitchen`);
    for (const f of [1, 2]) {
      const us = (b.units ?? []).filter((u) => u.floor === f);
      assert.equal(us.length, 3 * (b.plan?.sections ?? 0), `storey ${f}: three flats per landing`);
      for (const u of us) assert.ok(u.rooms.some((r) => rooms.get(r)?.use === 'kitchen'), `${u.id} keeps its kitchen`);
    }
    assert.equal((b.units ?? []).filter((u) => u.floor === 0).length, (b.units ?? []).filter((u) => u.floor === 1).length - 1, 'storey 0: one flat fewer (the service bay)');
    const up = rooms.get(`B_r_f1_${svc.id.split('_').pop()!}`)!;
    assert.ok(up.unit && up.use === 'kitchen', "the same cell upstairs is its flat's kitchen");
    assert.deepEqual(errors(l, b), []);
    assert.ok(b.plan?.notes?.some((x) => x.startsWith('gable door') && x.includes('service bay')));
  }
  const none = block({ gable: 'none' });
  assert.equal(groundDoors(none.b).length, 1, "gable_door 'none': the court door only");
  assert.equal((none.b.rooms ?? []).filter((r) => r.zone === 'service').length, 0, 'and no service bay');
  assert.equal(groundDoors(block({ state: 'locked' }).b).find((o) => o.module === 'wall_door_200')?.state, 'locked', 'the per-building state override still works');
});

test('CodexTactics.LevelGen.KitGable.Slots: street-wall windows of the living rooms carry balcony slots from storey 1, the same columns on every storey', () => {
  const { b } = block({ floors: 3, street: 'south' });
  const slots = (b.openings ?? []).filter((o) => o.slot === 'balcony');
  assert.ok(slots.length > 0);
  assert.ok(slots.every((o) => (o.floor ?? 0) >= KIT.balcony_slots.min_storey), 'none on the ground storey');
  assert.ok(slots.every((o) => o.kind === 'window' && o.wall === 'B_S'), 'street wall only');
  const rooms = new Map((b.rooms ?? []).map((r) => [r.id, r]));
  assert.ok(slots.every((o) => KIT.balcony_slots.uses.includes(rooms.get(o.links![0])!.use as string)), 'living rooms only');
  const at = (f: number) => slots.filter((o) => o.floor === f).map((o) => Math.round(o.pos[0] * 100)).sort((x, y) => x - y);
  assert.deepEqual(at(1), at(2), 'stacked');
});

// ---- (3) the stories -------------------------------------------------------------------------------------------------------------------

function storied(x: Built, stories: StoryId[]): { ok: boolean[]; b: GenBuilding; l: GenLayout; why: string } {
  let { l, b } = x;
  const ok: boolean[] = [];
  let why = '';
  for (const [i, s] of stories.entries()) {
    const r = applyStory(l, b, s, storySubSeed(5, i ? 'B#2' : 'B', 0));
    ok.push(r.ok);
    if (!r.ok) { why = r.issues.map((q) => q.message).join('; '); break; }
    b = r.building;
    l = { ...l, buildings: [b], ...(r.ladders ? { ladders: r.ladders } : {}) };
  }
  return { ok, b, l, why };
}

test('CodexTactics.LevelGen.KitGable.Story3: F4 holds for story 3 on a khrushchevka - fast kicks the locked court door, quiet goes round to the gable door', () => {
  for (const gable of ['west', 'east'] as const) {
    const x = block({ gable, street: 'north' }); // the court (and its door) faces the street at y 55
    const gd = groundDoors(x.b).find((o) => o.module === 'wall_door_200')!;
    const res = storied(x, ['locked_door']);
    assert.deepEqual(res.ok, [true], res.why);
    const r = computeBuildingRoutes(res.l, res.b);
    assert.ok(r.choice);
    assert.equal(r.quiet_entry, gd.id, 'the gable door is the quiet entry');
    assert.equal((res.b.openings ?? []).find((o) => o.id === r.fast_entry)?.state, 'locked', 'fast = the kick');
  }
});

test('CodexTactics.LevelGen.KitGable.Story9: story 9 locks the gable door itself - 3 + 9 hold on a khrushchevka with the plain closed gable door, quiet by a broken back window; a manual locked override still works', () => {
  for (const gable of ['east', 'west'] as const) {
    const x = block({ gable, street: 'north' }); // no gable_door_state override: the gable door is the template default
    const gd = groundDoors(x.b).find((o) => o.module === 'wall_door_200')!;
    assert.notEqual(gd.state, 'locked', 'the template default stays closed / openable');
    const res = storied(x, ['locked_door', 'broken_back_windows']);
    assert.deepEqual(res.ok, [true, true], res.why);
    const door = (res.b.openings ?? []).find((o) => o.id === gd.id)!;
    assert.equal(door.state, 'locked', 'story 9 locked the gable door');
    assert.ok(res.b.stories!.at(-1)!.bricks.includes(`door_state(${gd.id},locked)`), 'as a brick of the story');
    const r = computeBuildingRoutes(res.l, res.b);
    const quiet = (res.b.openings ?? []).find((o) => o.id === r.quiet_entry)!;
    assert.equal(quiet.kind, 'window');
    assert.equal(quiet.state, 'broken');
    assert.equal(quiet.wall, 'B_N', 'the back (street) facade');
    assert.equal((res.b.openings ?? []).find((o) => o.id === r.fast_entry)?.wall, 'B_S', 'fast on the front (court) facade');
    assert.deepEqual(errors(res.l, res.b), []);
  }
  // the manual override gives the same result (no second brick for a door that is locked already)
  const manual = storied(block({ gable: 'east', street: 'north', state: 'locked' }), ['locked_door', 'broken_back_windows']);
  assert.deepEqual(manual.ok, [true, true], manual.why);
  assert.ok(manual.b.stories!.at(-1)!.bricks.every((k) => !k.startsWith('door_state')), 'already locked: nothing to lock');
  // a house with no gable door is not touched by the lock
  const plain = storied(block({ gable: 'none', street: 'north' }), ['locked_door', 'broken_back_windows']);
  if (plain.ok[1]) assert.ok(plain.b.stories!.at(-1)!.bricks.every((k) => !k.startsWith('door_state')));
});

test('CodexTactics.LevelGen.KitGable.Story4: story 4 places its balcony + fire escape on a khrushchevka slot column (rubble on the stair, a balcony door per upper storey)', () => {
  for (const street of ['south', 'north'] as const) {
    const x = block({ floors: 3, street });
    const slotAt = new Set((x.b.openings ?? []).filter((o) => o.slot === 'balcony').map((o) => `${o.floor}|${o.pos.map((v) => Math.round(v * 100)).join(',')}`));
    const res = storied(x, ['rubble_escape']);
    assert.deepEqual(res.ok, [true], res.why);
    const b = res.b;
    assert.equal((b.stairs ?? []).find((s) => s.from === 0 && s.to === 1)?.blocked, 'rubble');
    assert.equal(b.fire_escapes?.length, 1);
    assert.deepEqual(b.balconies!.map((x) => x.floor), [1, 2]);
    const doors = (b.openings ?? []).filter((o) => o.role === 'balcony');
    assert.equal(doors.length, 2);
    assert.ok(doors.every((o) => slotAt.has(`${o.floor}|${o.pos.map((v) => Math.round(v * 100)).join(',')}`)), 'each balcony door replaces a slot window (street wall)');
    const rooms = new Map((b.rooms ?? []).map((r) => [r.id, r]));
    assert.ok(doors.every((o) => rooms.get(o.links![0])?.use === 'living'));
    assert.deepEqual(errors(res.l, b), []);
  }
});
