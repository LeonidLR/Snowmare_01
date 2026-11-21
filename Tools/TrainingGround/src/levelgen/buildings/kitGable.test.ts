// Training Ground / level generator v3 - the khrushchevka GABLE DOOR and BALCONY SLOTS (designer decision 2026-11-22; template block_khrushchevka
// gable_door / balcony_slots in building_plans.json; kitPlan.ts gableVestibule, planDoors.ts kitEntrances, planWindows.ts markKitSlots):
// (1) the plan - a gable door per the template's ends (low / high / both / none, both from both_min_sections), its vestibule = the end flat's hall +
// corridor + kitchen with the door in the kitchen's gable span, the reason in plan notes; balcony columns per the rate; (2) the generated block - the
// gable door is an exterior ground door (role entry) on the end wall, the vestibule is a common ground lobby, the upper storeys keep three flats
// per landing, every room reachable, slots on the street living rooms from storey 1; (3) the stories - F4 holds for story 3 on a khrushchevka (the
// gable door = the quiet entry), story 3 + 9 hold with a locked (utility) gable door and roll back with a closed one (the gable door is then nearer
// than any back window), story 4 places its balcony + fire escape on a khrushchevka's slot column. `npm test`.
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

test('CodexTactics.LevelGen.KitGable.Plan: the template ends give one gable door at u = 0; high / both / none; the vestibule is the end flat\'s hall + corridor + kitchen, the door in the kitchen\'s gable span', () => {
  assert.equal(KIT.gable_door.ends, 'low', 'template default');
  for (const [L, D] of [[2000, 1200], [1800, 1000], [3700, 1200]] as const) {
    for (const seed of [1, 4, 7]) {
      const p = planKitBlock({ L, D, seed });
      assert.equal(p.kind, 'bands', `${L} x ${D}`);
      assert.deepEqual(validateKitPlan(p), []);
      const g = p.kit!.gables!;
      assert.equal(g.length, 1, `${L} x ${D} seed ${seed}: one gable door`);
      assert.equal(g[0].wall, 'u0');
      assert.equal(g[0].section, 0);
      const byId = new Map(p.cells.map((c) => [c.id, c]));
      const uses = g[0].cells.map((id) => byId.get(id)!.use).sort();
      assert.deepEqual(uses, ['corridor', 'hall', 'kitchen'], 'the end flat\'s court row + its corridor');
      assert.ok(g[0].cells.every((id) => byId.get(id)!.unit === g[0].unit), 'one flat');
      const door = byId.get(g[0].door)!;
      assert.equal(door.use, 'kitchen');
      assert.equal(door.u0, 0, 'on the gable');
      assert.equal(door.v0, 0, 'at the court corner');
      assert.ok(g[0].c - 75 >= door.v0 && g[0].c + 75 <= door.v1 && g[0].c % 50 === 0, `door centre ${g[0].c} in ${door.v0}..${door.v1}`);
      assert.ok(p.kit!.notes.some((n) => n.startsWith('gable door (u0)') && n.includes(g[0].unit)), 'the lost kitchen is recorded');
    }
  }
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

test('CodexTactics.LevelGen.KitGable.Building: the gable door is an exterior ground entry on the end wall into a common lobby; upper storeys keep three flats per landing; 0 validator errors', () => {
  for (const gable of ['west', 'east'] as const) {
    const { l, b } = block({ gable, floors: 3 });
    assert.equal(b.plan?.template, 'block_khrushchevka');
    const doors = groundDoors(b);
    assert.equal(doors.length, 2, 'the court door + the gable door');
    const gd = doors.find((o) => o.wall === `B_${gable[0].toUpperCase()}`);
    assert.ok(gd, `a door on the ${gable} gable`);
    assert.equal(gd!.links?.[1], 'street');
    assert.equal(gd!.module, 'wall_door_200');
    const lobby = (b.rooms ?? []).find((r) => r.id === gd!.links![0])!;
    assert.equal(lobby.kind, 'lobby');
    assert.equal(lobby.zone, 'common');
    assert.equal(lobby.unit, undefined);
    // the vestibule rooms are flat rooms again upstairs; three flats per landing on every upper storey
    const cell = lobby.id.split('_').pop()!;
    const up = (b.rooms ?? []).find((r) => r.id === `B_r_f1_${cell}`)!;
    assert.ok(up.unit, 'the same cell upstairs belongs to its flat');
    for (const f of [1, 2]) assert.equal((b.units ?? []).filter((u) => u.floor === f).length, 3, `storey ${f}: three flats`);
    assert.deepEqual(errors(l, b), []);
    assert.ok(b.plan?.notes?.some((x) => x.startsWith('gable door')));
  }
  const none = block({ gable: 'none' });
  assert.equal(groundDoors(none.b).length, 1, "gable_door 'none': the court door only");
  assert.equal(groundDoors(block({ state: 'locked' }).b).find((o) => o.module === 'wall_door_200')?.state, 'locked', 'the per-building state');
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

test('CodexTactics.LevelGen.KitGable.Story9: story 3 + 9 hold on a khrushchevka with a locked (utility) gable door - quiet by a broken back window; a closed gable door is nearer and F4 rolls 9 back', () => {
  const x = block({ gable: 'east', street: 'north', state: 'locked' });
  const res = storied(x, ['locked_door', 'broken_back_windows']);
  assert.deepEqual(res.ok, [true, true], res.why);
  const r = computeBuildingRoutes(res.l, res.b);
  const quiet = (res.b.openings ?? []).find((o) => o.id === r.quiet_entry)!;
  assert.equal(quiet.kind, 'window');
  assert.equal(quiet.state, 'broken');
  assert.equal(quiet.wall, 'B_N', 'the back (street) facade');
  assert.equal((res.b.openings ?? []).find((o) => o.id === r.fast_entry)?.wall, 'B_S', 'fast on the front (court) facade');
  assert.deepEqual(errors(res.l, res.b), []);
  const closed = storied(block({ gable: 'east', street: 'north' }), ['locked_door', 'broken_back_windows']);
  assert.deepEqual(closed.ok, [true, false]);
  assert.match(closed.why, /F4/);
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
