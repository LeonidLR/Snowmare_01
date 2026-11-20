// Training Ground / level generator v3 - the plan overlay data and the plan summary (S2_ROOMS_PLAN.md S2.7: buildings/planOverlay.ts, planSummary.ts, the CLI `--plans` line,
// the layers.json «Планировки» layer). Pure data: (1) the floor selector -> the shown storey; (2) the loop finder (graphLoops) on small graphs; (3) the overlay of a planned
// office / block on the Depot x6: every room of the storey with a world polygon inside the (rotated) footprint, fills by unit / zone, the glyphs of planAscii.ts, doors by role
// with their states (flat door closed, portal open), dead-end flats, loops, groups for a later goal-zone layer, issues placed by ref; (4) planMapSummary counts equal a direct
// count, and `npm run levelgen -- --plans` prints the same line the page's «Здания» panel is built from. `npm test`.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { validateBuildings } from '../buildingTraversal.ts';
import type { TraversalIssue } from '../buildingTraversal.ts';
import { LAYERS, layerById } from '../layers.ts';
import { footprintPoly } from '../rotation.ts';
import { buildingToAscii } from './planAscii.ts';
import { DOOR_COLOUR, FILL_COLOUR, graphLoops, overlayFloor, PLAN_LEGEND, planOverlayFor, planOverlays } from './planOverlay.ts';
import { byId, draftBuilding, fixtures, planned, ROOT } from './planFixtures.ts';
import { replanBuildingStory } from '../storyPass.ts';
import { hasPlans, planKindOf, planMapSummary, planSummaryLine } from './planSummary.ts';

const TG = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

/** Is `p` inside the convex polygon `poly` (any winding), allowing `tol` cm outside an edge? */
const inside = (poly: readonly (readonly number[])[], p: readonly number[], tol: number): boolean => {
  let sign = 0;
  for (let i = 0; i < poly.length; i++) {
    const a = poly[i], b = poly[(i + 1) % poly.length];
    const cross = (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]), len = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1;
    if (Math.abs(cross) / len <= tol) continue;
    const s = Math.sign(cross);
    if (sign === 0) sign = s; else if (s !== sign) return false;
  }
  return true;
};

test('the floor selector: all / ground = the ground storey, 1 / 2 = that storey when the building has it, roof = none; a layer filter narrows it', () => {
  const b = { id: 'B_a', floors: 2 };
  assert.deepEqual((['all', 'ground', '1', '2', 'roof'] as const).map((m) => overlayFloor(b, m)), [0, 0, 1, null, null]);
  assert.equal(overlayFloor({ id: 'B_a', floors: 3 }, '2'), 2);
  assert.equal(overlayFloor(b, 'all', 'floor:B_a:1'), 1);
  assert.equal(overlayFloor(b, 'all', 'floor:B_other:1'), null, "another building's storey layer");
  assert.equal(overlayFloor(b, 'all', 'floor:B_a:2'), null, 'no such storey');
  assert.equal(overlayFloor(b, '1', 'ground'), 0, 'a layer filter wins over the mode');
  assert.equal(overlayFloor(b, 'all', 'roof:B_a'), null);
});

test('graphLoops: a triangle is a loop, a path / a star is not, a square with a tail marks only the square, parallel edges are one cycle', () => {
  assert.deepEqual(graphLoops(['a', 'b', 'c'], [['a', 'b'], ['b', 'c'], ['c', 'a']]), { inLoop: [true, true, true], cycles: 1 });
  assert.deepEqual(graphLoops(['a', 'b', 'c'], [['a', 'b'], ['b', 'c']]), { inLoop: [false, false], cycles: 0 });
  assert.deepEqual(graphLoops(['h', 'a', 'b', 'c'], [['h', 'a'], ['h', 'b'], ['h', 'c']]).inLoop, [false, false, false]);
  const sq = graphLoops(['a', 'b', 'c', 'd', 't'], [['a', 'b'], ['b', 'c'], ['c', 'd'], ['d', 'a'], ['d', 't']]);
  assert.deepEqual(sq.inLoop, [true, true, true, true, false]);
  assert.equal(sq.cycles, 1);
  assert.equal(graphLoops(['a', 'b'], [['a', 'b'], ['a', 'b']]).cycles, 1, 'the graph itself counts parallel edges (the overlay merges them first)');
  assert.equal(graphLoops(['a', 'b', 'c', 'd'], [['a', 'b'], ['b', 'a'], ['c', 'd'], ['d', 'c']]).cycles, 2);
});

test('overlay of an office storey: every room has a world polygon inside the footprint, fills / glyphs follow planAscii, doors carry roles, states and loop flags', () => {
  const { s6 } = fixtures();
  for (const b of planned(s6.layout, 'office')) {
    for (let f = 0; f < b.floors; f++) {
      const o = planOverlayFor(s6.layout, b, f)!;
      const rooms = (b.rooms ?? []).filter((r) => r.floor === f);
      assert.equal(o.rooms.length, rooms.length, `${b.id} f${f}`);
      assert.deepEqual(o.rooms.map((r) => r.id), rooms.map((r) => r.id).sort(), 'sorted by id');
      const fp = footprintPoly(b);
      for (const r of o.rooms) {
        assert.equal(r.poly.length, 4);
        for (const p of r.poly) assert.ok(inside(fp, p, 1.5), `${b.id} ${r.id}: corner inside the (rotated) footprint`);
        assert.ok(inside(r.poly, r.centre, 0.5));
        assert.ok(r.area_m2 > 0 && r.size_cm[0] > 0);
      }
      for (const r of o.rooms) {
        const src = rooms.find((x) => x.id === r.id)!;
        const want = src.kind === 'stairwell' ? 'core' : src.kind === 'corridor' ? 'corridor' : src.kind === 'lobby' ? 'lobby' : src.kind === 'service' ? 'service' : src.unit === 'suite' ? 'suite' : 'office';
        assert.equal(r.fill, want);
        assert.equal(r.colour, FILL_COLOUR[want]);
        assert.equal(r.glyph, { core: 'K', corridor: 'L', lobby: 'O', service: 's', suite: 'e', office: 'r' }[want]);
      }
      const ops = [...(b.openings ?? []), ...(b.partitions ?? []).flatMap((p) => (p.floor === f ? p.openings ?? [] : []))].filter((x) => x.floor === f && x.kind !== 'loophole');
      assert.equal(o.doors.length, ops.length);
      for (const d of o.doors) {
        const src = ops.find((x) => x.id === d.id)!;
        assert.equal(d.role, src.kind === 'window' ? 'window' : src.role ?? 'room');
        assert.equal(Math.round(Math.hypot(d.b[0] - d.a[0], d.b[1] - d.a[1])), src.width ?? 150, 'the segment is the opening width');
        assert.ok(Math.abs((d.a[0] + d.b[0]) / 2 - src.pos[0]) < 0.02 && Math.abs((d.a[1] + d.b[1]) / 2 - src.pos[1]) < 0.02, 'centred on the opening');
        if (d.role === 'portal') assert.equal(d.state, 'open', 'a pad portal has no leaf');
        if (d.kind === 'window') assert.ok(['glazed', 'broken', 'fixed', 'barred', 'boarded'].includes(d.state));
        assert.equal(d.exterior, (b.openings ?? []).includes(src));
        assert.equal(d.colour, d.slot ? DOOR_COLOUR.slot : DOOR_COLOUR[d.role] ?? DOOR_COLOUR.room);
        if (d.loop) assert.equal(d.exterior, false, `${d.id}: only interior doors loop`);
      }
      assert.ok(o.rooms.filter((r) => r.loop).every((r) => o.doors.some((d) => d.loop && d.links?.includes(r.id))), 'a room on a loop has a loop door');
    }
  }
});

test("overlay of a block: flats by column, dead-end flats marked, flat doors closed, units and the common zone as groups, the ASCII plan has the same letters", () => {
  const { s6 } = fixtures();
  const blocks = planned(s6.layout, 'block');
  assert.ok(blocks.length >= 4);
  let sawDead = false, sawLoop = false;
  for (const b of blocks) {
    for (let f = 0; f < b.floors; f++) {
      const o = planOverlayFor(s6.layout, b, f)!;
      const units = (b.units ?? []).filter((u) => u.floor === f);
      assert.deepEqual(o.groups.filter((g) => g.kind === 'unit').map((g) => g.id).sort(), units.map((u) => u.id).sort());
      for (const g of o.groups.filter((x) => x.kind === 'unit')) {
        const u = units.find((x) => x.id === g.id)!;
        assert.deepEqual([...g.rooms].sort(), [...u.rooms].sort());
        assert.equal(g.polys.length, g.rooms.length);
        assert.equal(!!g.dead_end, !!u.dead_end);
        assert.equal(g.run_cm, u.run_cm);
      }
      const common = o.groups.find((g) => g.id === 'common');
      assert.ok(common && common.kind === 'zone' && common.rooms.length >= 2, 'the stair core + landing zone');
      const flats = o.rooms.filter((r) => r.fill === 'flat');
      assert.equal(flats.length, units.reduce((n, u) => n + u.rooms.length, 0));
      for (const r of flats) {
        assert.equal(r.dead_end, !!units.find((u) => u.id === r.unit)!.dead_end);
        assert.equal(r.glyph === r.glyph.toUpperCase(), r.dead_end, 'upper case = dead end');
        sawDead ||= r.dead_end;
      }
      for (const col of new Set(flats.map((r) => r.column))) assert.equal(new Set(flats.filter((r) => r.column === col).map((r) => r.colour)).size, 1, 'one colour per flat');
      for (const d of o.doors.filter((x) => x.role === 'unit_entry')) assert.equal(d.state, 'closed', `${d.id}: a flat door is closed by default`);
      sawLoop ||= o.loops > 0;
    }
  }
  assert.ok(sawDead, 'some dead-end flat');
  assert.ok(sawLoop, 'some block storey has a loop');
});

test('the overlay and the ASCII plan (S2_3_PLANS.md) agree on an axis-aligned block 14 x 20 m and a 12 x 36 m block: same flat letters, dead-end flats upper case', () => {
  for (const [w, h, floors] of [[20, 14, 3], [36, 12, 2]] as const) {
    const { l, b } = draftBuilding('block', w, h, floors);
    for (let f = 0; f < floors; f++) {
      const o = planOverlayFor(l, b, f)!, ascii = buildingToAscii(b, f);
      const flats = o.rooms.filter((r) => r.fill === 'flat');
      assert.ok(flats.length >= 4, `${w} x ${h} f${f}: flats`);
      for (const r of flats) assert.ok(ascii.includes(r.glyph), `${w} x ${h} f${f}: letter ${r.glyph} is in the ASCII plan`);
      for (const g of ['K', 'L']) assert.ok(ascii.includes(g) && o.rooms.some((r) => r.glyph === g), `${g} in both`);
      // every door glyph of the overlay appears in the ASCII (E, U, d, e, P, w, S) except those a wall hides
      for (const g of new Set(o.doors.map((d) => d.glyph))) if (['E', 'U', 'P', 'w'].includes(g)) assert.ok(ascii.includes(g), `${w} x ${h} f${f}: door glyph ${g}`);
    }
  }
});

test('issues of the validator are placed by ref on the storey (a room id or an opening id); the others stay without a position', () => {
  const { s6 } = fixtures();
  const b = planned(s6.layout, 'office')[0], o0 = planOverlayFor(s6.layout, b, 0)!;
  const room = o0.rooms[0], door = o0.doors[0];
  const issues: TraversalIssue[] = [
    { severity: 'error', code: 'room_no_door', ref: room.id, message: 'x' },
    { severity: 'warning', code: 'window_rule', ref: door.id, message: 'y' },
    { severity: 'error', code: 'storey_no_loop', ref: `${b.id} f0`, message: 'z' },
    { severity: 'warning', code: 'corridor_narrow', message: 'no ref' },
  ];
  const o = planOverlayFor(s6.layout, b, 0, issues)!;
  assert.equal(o.issues.length, 4);
  assert.deepEqual(o.issues[0].pos, room.centre);
  assert.deepEqual(o.issues[1].pos, door.pos);
  assert.equal(o.issues[2].pos, undefined);
  assert.equal(o.issues[3].pos, undefined);
  assert.deepEqual(o.issues.map((i) => i.severity), ['error', 'warning', 'error', 'warning']);
});

test('planOverlays follows the floor selector over the whole map; non-planned buildings and a storey the building lacks are skipped; a BSP fallback is drawn as kind bsp', () => {
  const { s6 } = fixtures();
  const l = s6.layout, all = planned(l);
  const ground = planOverlays(l, 'ground'), f1 = planOverlays(l, '1'), f2 = planOverlays(l, '2'), roof = planOverlays(l, 'roof');
  const withPlan = l.buildings.filter((b) => b.plan);
  assert.equal(ground.length, withPlan.length);
  assert.equal(f1.length, withPlan.filter((b) => b.floors > 1).length);
  assert.equal(f2.length, withPlan.filter((b) => b.floors > 2).length);
  assert.equal(roof.length, 0);
  assert.equal(planOverlays(l, 'all').length, ground.length);
  assert.ok(ground.every((o) => o.floor === 0) && f1.every((o) => o.floor === 1));
  const one = all.find((b) => b.floors > 1)!;
  assert.deepEqual(planOverlays(l, 'all', `floor:${one.id}:1`).map((o) => `${o.building}:${o.floor}`), [`${one.id}:1`]);
  assert.equal(ground.filter((o) => o.kind === 'bsp').length, withPlan.length - all.length);
  assert.ok(ground.every((o) => !!byId(l, o.building).plan));
  const rep = validateBuildings(l);
  const withIssues = planOverlays(l, 'ground', 'all', (id) => rep.buildings.find((r) => r.id === id)?.issues ?? []);
  assert.deepEqual(withIssues.map((o) => o.issues.length), ground.map((o) => (rep.buildings.find((r) => r.id === o.building)?.issues ?? []).length));
});

test('the overlay is data-driven and pure: the same input gives the same output, the layout is not touched, groups keep the room polygons a goal-zone layer can outline', () => {
  const { s6 } = fixtures();
  const before = JSON.stringify(s6.layout), b = planned(s6.layout, 'block')[0];
  const a = planOverlayFor(s6.layout, b, 1)!, c = planOverlayFor(s6.layout, b, 1)!;
  assert.equal(JSON.stringify(a), JSON.stringify(c));
  assert.equal(JSON.stringify(s6.layout), before);
  for (const g of a.groups) for (const [i, id] of g.rooms.entries()) assert.deepEqual(g.polys[i], a.rooms.find((r) => r.id === id)!.poly);
  assert.equal(planOverlayFor(s6.layout, b, b.floors), null, 'no such storey');
  assert.equal(planOverlayFor(s6.layout, s6.layout.buildings.find((x) => !x.plan)!, 0), null, 'no plan');
});

test('the legend lists every glyph of S2_3_PLANS.md, and the layers config has the «Планировки» layer in the analysis group', () => {
  const doc = fs.readFileSync(path.join(ROOT, 'docs/design/levelgen/S2_3_PLANS.md'), 'utf8');
  const legend = doc.slice(doc.indexOf('Legend:'), doc.indexOf('Legend:') + 1400);
  for (const g of ['E', 'w', 'S', 'U', 'd', 'e', 'P', 'b', 'K', 'L', 'O', 'r', 's']) {
    assert.ok(PLAN_LEGEND.some((x) => x.glyph === g), `legend has ${g}`);
    assert.ok(legend.includes(`\`${g}\``), `S2_3_PLANS.md names ${g}`);
  }
  const layer = layerById('plans')!;
  assert.equal(layer.group, 'analysis');
  assert.deepEqual(layer.match, [{ type: 'analysis', kinds: ['plan'] }]);
  assert.ok(LAYERS.layers.some((l) => l.id === 'plans'));
});

test('plan summary: counts equal a direct count; the line carries them; a plans-off layout has no plans', () => {
  const { s1, s6 } = fixtures();
  for (const l of [s1.layout, s6.layout]) {
    const s = planMapSummary(l), enterable = l.buildings.filter((b) => b.enterable && !b.hollow);
    assert.equal(s.enterable, enterable.length);
    assert.equal(s.planned, enterable.filter((b) => b.plan?.kind === 'bands').length);
    assert.equal(s.fallback, enterable.filter((b) => b.plan?.kind === 'bsp').length);
    assert.equal(s.legacy, enterable.filter((b) => !b.plan).length);
    assert.equal(s.planned + s.fallback + s.legacy, s.enterable);
    assert.equal(s.archetypes.reduce((n, a) => n + a.buildings, 0), s.enterable);
    assert.equal(s.rooms, enterable.filter((b) => b.plan?.kind === 'bands').reduce((n, b) => n + (b.rooms ?? []).length, 0));
    assert.equal(s.dead_end_units, enterable.filter((b) => b.plan?.kind === 'bands').reduce((n, b) => n + (b.units ?? []).filter((u) => u.dead_end).length, 0));
    assert.equal(Object.values(s.stories).reduce((n, c) => n + c, 0), enterable.filter((b) => b.plan?.kind === 'bands' && b.stories?.length).length);
    assert.deepEqual(planMapSummary(l, validateBuildings(l).buildings), s, 'the report given in = the report computed here');
    const line = planSummaryLine(s);
    assert.ok(line.startsWith(`plans: ${s.enterable} enterable, ${s.planned} planned, ${s.fallback} BSP fallback`), line);
    assert.ok(line.includes(`rooms ${s.rooms}`) && line.includes(`dead-end flats ${s.dead_end_units}`));
  }
  assert.ok(hasPlans(s6.layout));
  const plain = { ...s1.layout, buildings: s1.layout.buildings.map(({ plan: _p, ...b }) => b) };
  assert.equal(hasPlans(plain), false);
  assert.equal(planMapSummary(plain).planned, 0);
  assert.equal(planKindOf(plain.buildings[0]), 'legacy');
  assert.equal(planMapSummary(s6.layout).replanned, 0);
  // a re-planned building is counted
  const b = planned(s6.layout)[0], r = replanBuildingStory(s6.layout, b.id, s6);
  assert.equal(r.ok, true, r.message);
  assert.equal(planMapSummary(r.layout!).replanned, 1);
  assert.match(planSummaryLine(planMapSummary(r.layout!)), /re-planned 1;/);
});

test("CLI `--plans` prints the page's plan summary line (same function, same numbers): `--seed 1 --no-sim --stories`", () => {
  const out = fs.mkdtempSync(path.join(os.tmpdir(), 'levelgen-plans-'));
  try {
    const cli = path.join(TG, 'src/levelgen/cli/levelgen.ts');
    const run = spawnSync(process.execPath, [cli, '--seed', '1', '--no-sim', '--plans', '--stories', '--out', out], { cwd: TG, encoding: 'utf8' });
    assert.equal(run.status, 0, run.stderr);
    const line = run.stdout.split('\n').find((x) => x.startsWith('plans: '));
    assert.ok(line, `a plans: line in the report\n${run.stdout.slice(-600)}`);
    const name = fs.readdirSync(out).find((f) => f.endsWith('.json') && !f.endsWith('.eval.json'))!;
    const layout = JSON.parse(fs.readFileSync(path.join(out, name), 'utf8'));
    const ev = JSON.parse(fs.readFileSync(path.join(out, name.replace(/\.json$/, '.eval.json')), 'utf8'));
    assert.equal(line, planSummaryLine(planMapSummary(layout, ev.buildings?.buildings)));
    const plain = spawnSync(process.execPath, [cli, '--seed', '1', '--no-sim', '--out', out], { cwd: TG, encoding: 'utf8' });
    assert.equal(plain.status, 0, plain.stderr);
    assert.ok(!plain.stdout.split('\n').some((x) => x.startsWith('plans: ')), 'no plans line without --plans');
  } finally { fs.rmSync(out, { recursive: true, force: true }); }
});
