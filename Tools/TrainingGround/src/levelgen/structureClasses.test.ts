// Training Ground / level generator v3 - structure classes + window vault data (structureClasses.ts, lead addendum 2026-11-21): the kit block's
// outer walls, stair core and walls between flats are load_bearing concrete_panel, its flat partitions partition plaster; an office by its own rule;
// every opening carries its host wall's class; every window of a /2 layout has sill_cm / width_cm / vault_onto / vaultable (ground storey yes,
// an upper window over nothing no, onto a balcony yes, too narrow / too high a sill no); a /1 layout carries none of it. `npm test`.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Draft } from './generate.ts';
import { addBuilding, finalizeDraftToLayout } from './generate.ts';
import type { GenBuilding, GenLayout, WallOpening } from './schema.ts';
import { DEFAULT_PARAMS, LEVEL_HEIGHT } from './schema.ts';
import { STRUCTURE_CLASSES, annotateWindowVault, interiorRole, sillVaultable, structureOf } from './structureClasses.ts';

function one(archetype: 'block' | 'office', w: number, h: number, floors: number, plans = true): { l: GenLayout; b: GenBuilding } {
  const d: Draft = { walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [], building_plans: plans };
  let n = 7 * 7919;
  const random = (): number => { n = (n * 1103515245 + 12345) & 0x7fffffff; return n / 0x7fffffff; };
  addBuilding(d, 'B_s', [[20.37, 15.21], [20.37 + w, 15.21 + h]], {
    height: floors * LEVEL_HEIGHT, floors, enterable: true, roof_walkable: true, stairwell: true, set_piece: 'facade', archetype,
    doorSides: ['south'], windowSides: ['south', 'north'], partitionRooms: true,
  }, random);
  const l = finalizeDraftToLayout(d, [90, 90], { name: 't', profile: 't', seed: 1, params: { ...DEFAULT_PARAMS, size_m: [90, 90], building_plans: plans }, start: [4, 2], end: [36, 58] });
  return { l, b: l.buildings.find((x) => x.id === 'B_s')! };
}

test('CodexTactics.LevelGen.Structure.KitBlock: outer walls, the stair core and the walls between flats are load_bearing concrete_panel; flat partitions partition plaster', () => {
  const { l, b } = one('block', 20.2, 14.1, 2);
  assert.equal(b.plan?.pattern, 'kit');
  const walls = l.walls.filter((w) => w.building === b.id);
  assert.ok(walls.length > 0 && walls.every((w) => w.structure && w.material));
  for (const w of walls.filter((x) => x.kind === 'building')) assert.deepEqual([w.structure, w.material], ['load_bearing', 'concrete_panel'], w.id);
  for (const w of walls.filter((x) => /_lanewall_f\d+$/.test(x.id))) assert.deepEqual([w.structure, w.material], ['load_bearing', 'concrete_panel'], w.id);
  const roles = new Map<string, number>();
  for (const pt of b.partitions ?? []) {
    const role = interiorRole(b, pt.id, pt.start, pt.end, pt.floor);
    roles.set(role, (roles.get(role) ?? 0) + 1);
    assert.deepEqual([pt.structure, pt.material], role === 'inner' ? ['partition', 'plaster'] : ['load_bearing', 'concrete_panel'], `${pt.id} (${role})`);
    for (const op of pt.openings ?? []) assert.deepEqual([op.structure, op.material], [pt.structure, pt.material], `${op.id} takes its partition's class`);
  }
  assert.ok((roles.get('core') ?? 0) > 0 && (roles.get('cross') ?? 0) > 0 && (roles.get('inner') ?? 0) > 0, JSON.stringify([...roles]));
  // the flat door (landing / tambour -> hall) sits in a cross / core wall: load bearing
  const entry = (b.partitions ?? []).flatMap((pt) => (pt.openings ?? []).map((op) => ({ op, pt }))).find((x) => x.op.role === 'unit_entry')!;
  assert.equal(entry.op.structure, 'load_bearing');
  for (const op of b.openings ?? []) assert.deepEqual([op.structure, op.material], ['load_bearing', 'concrete_panel'], `${op.id}: an outer opening`);
});

test('CodexTactics.LevelGen.Structure.Rules: the table covers every rule role; offices / halls / sheds by their own rules; free walls by kind; the hp table is a TBD placeholder', () => {
  assert.deepEqual(structureOf('office', 'perimeter'), { structure: 'load_bearing', material: 'brick' });
  assert.deepEqual(structureOf('office', 'inner'), { structure: 'partition', material: 'plaster' });
  assert.deepEqual(structureOf('hall', 'perimeter'), { structure: 'facade', material: 'sheet_metal' });
  assert.deepEqual(structureOf('shed', 'perimeter'), { structure: 'annex', material: 'wood' });
  assert.deepEqual(structureOf(undefined, 'perimeter'), structureOf('default', 'perimeter'));
  for (const [arch, rule] of Object.entries(STRUCTURE_CLASSES.rules)) {
    for (const role of ['perimeter', 'core', 'cross', 'inner']) {
      const [s, m] = (rule as Record<string, string[]>)[role];
      assert.ok(STRUCTURE_CLASSES.structures.includes(s) && STRUCTURE_CLASSES.materials.includes(m), `${arch}.${role}`);
      assert.ok(`${s}/${m}` in STRUCTURE_CLASSES.classes, `${arch}.${role}: ${s}/${m} has a placeholder row`);
    }
  }
  for (const row of Object.values(STRUCTURE_CLASSES.classes)) assert.equal(row.hp_class, 'TBD');
  const { l, b } = one('office', 12.0, 10.2, 2);
  for (const w of l.walls.filter((x) => x.building === b.id && x.kind === 'building')) assert.deepEqual([w.structure, w.material], ['load_bearing', 'brick']);
});

test('CodexTactics.LevelGen.WindowVault.Generated: every window of a /2 layout has sill_cm / width_cm / vault_onto / vaultable; ground windows vaultable, upper windows over nothing not', () => {
  const { b } = one('block', 20.2, 14.1, 2);
  const windows = (b.openings ?? []).filter((o) => o.kind === 'window');
  assert.ok(windows.length > 0);
  for (const w of windows) {
    assert.equal(w.sill_cm, w.sill_height);
    assert.equal(w.width_cm, w.width);
    assert.equal(w.vault_onto, w.floor === 0 ? 'ground' : 'none', w.id);
    assert.equal(w.vaultable, w.floor === 0, `${w.id}: ground windows yes (sill ${w.sill_cm}, width ${w.width_cm}), upper over nothing no`);
  }
});

test('CodexTactics.LevelGen.WindowVault.Rules: a balcony outside makes an upper window vaultable; a narrow window or a sill off our mantle heights is not; a /1 layout carries no vault data', () => {
  const { l, b } = one('block', 20.2, 14.1, 2);
  const up = (b.openings ?? []).find((o) => o.kind === 'window' && o.floor === 1)! as WallOpening;
  // a 150 deep balcony in front of the window (building frame), as story 4 would add it
  const [[x0, y0], [x1, y1]] = b.footprint, p = up.pos;
  const d = [Math.abs(p[0] - x0), Math.abs(p[0] - x1), Math.abs(p[1] - y0), Math.abs(p[1] - y1)], k = d.indexOf(Math.min(...d));
  const rect: [[number, number], [number, number]] = k === 0 ? [[x0 - 150, p[1] - 100], [x0, p[1] + 100]] : k === 1 ? [[x1, p[1] - 100], [x1 + 150, p[1] + 100]]
    : k === 2 ? [[p[0] - 100, y0 - 150], [p[0] + 100, y0]] : [[p[0] - 100, y1], [p[0] + 100, y1 + 150]];
  b.balconies = [{ id: 'bal', floor: 1, rect, door_id: 'x', rail_cm: 95 }];
  annotateWindowVault(l);
  assert.equal(up.vault_onto, 'balcony');
  assert.equal(up.vaultable, true);
  assert.equal(sillVaultable(95), true);
  assert.equal(sillVaultable(250), false, 'a sill above the high mantle');
  const ground = (b.openings ?? []).find((o) => o.kind === 'window' && o.floor === 0)!;
  ground.width = 60;
  annotateWindowVault(l);
  assert.equal(ground.vaultable, false, 'narrower than 80 cm');
  const off = one('block', 20.2, 14.1, 2, false);
  for (const o of off.b.openings ?? []) assert.equal(o.vaultable, undefined, 'plans off: the /1 output is unchanged');
  assert.ok(off.l.walls.every((w) => w.structure === undefined));
});
