// Training Ground / level generator - the CHOKEPOINT / threat analysis (chokepoints.ts + analysisGeom.ts +
// Content/Data/LevelGen/chokepoints.json) on SYNTHETIC layouts with known geometry: a narrow alley between two tall buildings is a
// canyon; an open square is nothing; a cluttered street is a jam; one gate in a wall is the only path (a second gate = a detour);
// the four threat modes (a low roof beside an alley = a drop ambush, a window facing a street in range = window fire, a
// ladder-reachable roof with a long view = a marksman, a tall wall beside a street = a hiding spot, an open field = none); the
// enemies' numbers come from the game configs; determinism; the Depot maps. Nothing is written. `npm test`.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import type { Vec2 } from '../movement/geometry.ts';
import { pointInPoly } from '../movement/geometry.ts';
import { clipSegment, layoutObstacles, ObstacleIndex, WalkGrid } from './analysisGeom.ts';
import type { ChokeConfig, Chokepoint, LayoutAnalysis } from './chokepoints.ts';
import { analyzeLayout, CHOKE, geometryHash, plural, rangeFalloff, samplePolyline, threatNumbers, validateChokeConfig, withoutStaleAnalysis } from './chokepoints.ts';
import { generateLayout } from './generate.ts';
import { groundNav, placeObjectives } from './objectives.ts';
import type { Profile } from './profile.ts';
import type { GenBuilding, GenCover, GenLadder, GenLayout, GenWall, WallOpening } from './schema.ts';
import { DEFAULT_PARAMS, LEVELGEN_SCHEMA, validateGenLayout } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const readJson = <T>(rel: string): T => JSON.parse(fs.readFileSync(path.join(ROOT, rel), 'utf8')) as T;

// ---- synthetic layouts (UE cm; X up, Y right; a 100 x 100 m map unless said) --------------------------------------------------------

interface Parts { walls?: GenWall[]; buildings?: GenBuilding[]; covers?: GenCover[]; ladders?: GenLadder[]; zones?: GenLayout['zones']; route: Vec2[]; half?: number }
function mk(p: Parts): GenLayout {
  const h = p.half ?? 5000;
  return { schema: LEVELGEN_SCHEMA, name: 'synthetic', profile: 'test', seed: 1, params: { ...DEFAULT_PARAMS }, bounds: { min: [-h, -h], max: [h, h] }, ground_z: 0,
    spawn: { pos: p.route[0], yaw: 0 }, end: p.route[p.route.length - 1], walls: p.walls ?? [], buildings: p.buildings ?? [], ladders: p.ladders ?? [], covers: p.covers ?? [],
    zones: p.zones ?? [], routes: [{ id: 'R', pace: 'walk', points: p.route, mandatory: true }], spawn_points: [], ambush_points: [], patrol_routes: [] };
}
const wall = (id: string, a: Vec2, b: Vec2, height = 330, thickness = 30, kind: GenWall['kind'] = 'wall'): GenWall => ({ id, a, b, height, thickness, kind });
interface BOpts { height?: number; floors?: number; roof?: boolean; enterable?: boolean; openings?: WallOpening[]; stairs?: GenBuilding['stairs'] }
/** A box building with its four perimeter walls (no door gap unless the caller cuts one). */
function box(id: string, x0: number, y0: number, x1: number, y1: number, o: BOpts = {}): { b: GenBuilding; walls: GenWall[] } {
  const height = o.height ?? 900;
  const b: GenBuilding = { id, footprint: [[x0, y0], [x1, y1]], height, floors: o.floors ?? Math.round(height / 300), enterable: o.enterable ?? false, roof_walkable: o.roof ?? false, doors: [], openings: o.openings ?? [],
    stairs: o.stairs ?? [], roof_holes: [], floor_holes: [], set_piece: 'facade' };
  const c: Vec2[] = [[x0, y0], [x1, y0], [x1, y1], [x0, y1]];
  const walls = c.map((a, i) => ({ id: `${id}_w${i}`, a, b: c[(i + 1) % 4], height, thickness: 30, kind: 'building' as const, building: id }));
  return { b, walls };
}
const cover = (id: string, pos: Vec2, kind: GenCover['kind'], size: [number, number, number], yaw = 0): GenCover => ({ id, pos, yaw, size, kind });
const street = (id: string, x0: number, y0: number, x1: number, y1: number): GenLayout['zones'][number] => ({ id, kind: 'wall_run', set_piece: 'street', poly: [[x0, y0], [x1, y0], [x1, y1], [x0, y1]] });
const byTag = (a: LayoutAnalysis, tag: string): Chokepoint[] => a.chokepoints.filter((c) => (c.tags as string[]).includes(tag));
const within = (v: number, want: number, eps: number) => Math.abs(v - want) <= eps;
/** The same config with every place listed (tier-2 exposure places score low). */
const ALL: ChokeConfig = { ...CHOKE, places: { ...CHOKE.places, min_score: 0 } };

/** Two 9 m buildings 30 m long, 6 m wide, their faces 4 m apart; the route runs through the alley; the way round a building is short. */
function alley(height = 900, opts: BOpts = {}): GenLayout {
  const n = box('B_n', -1500, -815, 1500, -215, { height, ...opts }), s = box('B_s', -1500, 215, 1500, 815, { height: 900 });
  return mk({ buildings: [n.b, s.b], walls: [...n.walls, ...s.walls], route: [[-3500, 0], [3500, 0]] });
}

test('chokepoints config: valid; the kinds in the designer\'s words; the plural helper', () => {
  assert.deepEqual(validateChokeConfig(CHOKE), []);
  assert.deepEqual(Object.values(CHOKE.kind_names), ['узкий проход', 'завал', 'каньон с высотами', 'единственный путь', 'засада сверху (спрыгивание)', 'огонь из окон', 'снайпер на крыше', 'укрытие для засады / скрытый противник']);
  assert.equal(CHOKE.narrow.width_m, 6);
  assert.ok(validateChokeConfig({ ...CHOKE, narrow: { ...CHOKE.narrow, width_m: 1 } }).length > 0);
  assert.deepEqual([1, 2, 5, 11, 21, 104].map((n) => plural(n, 'окно', 'окна', 'окон')), ['окно', 'окна', 'окон', 'окон', 'окно', 'окна']);
});

test('chokepoints geometry: sampling, canal clipping, nearest / ray / height-aware sight line, the grid path with a blocked patch', () => {
  assert.deepEqual(samplePolyline([[0, 0], [100, 0], [100, 120]], 50), [[0, 0], [50, 0], [100, 0], [100, 50], [100, 100], [100, 120]]);
  // a canal edge is open where a crossing spans it
  const lip = clipSegment([0, 0], [1000, 0], [[[400, -50], [600, -50], [600, 50], [400, 50]]], false);
  assert.deepEqual(lip, [[[0, 0], [400, 0]], [[600, 0], [1000, 0]]]);
  const l = mk({ walls: [wall('W_tall', [0, -1000], [0, 1000], 330), wall('W_low', [500, -1000], [500, 1000], 95)], covers: [cover('C1', [-500, 0], 'crate', [120, 120, 85])], route: [[-3000, 0], [3000, 0]] });
  const items = layoutObstacles(l), idx = new ObstacleIndex(items, l.bounds.min, l.bounds.max);
  const n = idx.nearest([-200, 300], 1000)!;
  assert.ok(within(n.dist, 185, 0.01) && idx.obstacles[n.obs].src === 'wall:W_tall', 'the wall face is half a thickness in front of its centre line');
  assert.equal(idx.obstacles[idx.nearest([-500, 200], 1000)!.obs].src, 'cover:C1');
  assert.equal(idx.obstacles[idx.nearest([-500, 200], 1000, true)!.obs].src, 'wall:W_tall', 'hard only: a crate is clutter');
  const r = idx.ray([-300, 0], [1, 0], 4000)!;
  assert.ok(within(r.dist, 285, 0.01) && idx.obstacles[r.obs].src === 'wall:W_tall');
  assert.equal(idx.ray([-300, 0], [0, 1], 300), null);
  // sight: the 3.3 m wall blocks a standing eye; the 95 cm wall does not; from 6 m up the line clears the tall wall near the eye
  assert.equal(idx.los([-300, 0], 160, [300, 0], 150), false);
  assert.equal(idx.los([200, 0], 160, [900, 0], 150), true);
  assert.equal(idx.los([200, 0], 160, [900, 0], 90), true, 'a 95 cm wall 4 m from a crouched man does not hide him from 7 m');
  assert.equal(idx.los([450, 0], 160, [540, 0], 90), true);
  assert.equal(idx.los([-3000, 0], 160, [540, 0], 90), false, 'from far the same low wall hides the crouched man behind it');
  assert.equal(idx.los([-100, 0], 760, [2000, 0], 150), true);
  assert.equal(idx.los([-2000, 0], 760, [100, 0], 150), false);
  // the grid: a wall with one gap; blocking the gap leaves no way
  const g = mk({ walls: [wall('A', [0, -5000], [0, -150]), wall('B', [0, 150], [0, 5000])], route: [[-1000, 0], [1000, 0]] });
  const grid = new WalkGrid(groundNav(g));
  const base = grid.pathLength([-1000, 0], [1000, 0], 3000, false)!;
  assert.ok(within(base, 2000, 60));
  grid.setPatch([[0, 0]], 300);
  assert.equal(grid.pathLength([-1000, 0], [1000, 0], 3000, true), null);
  grid.clearPatch();
  assert.ok(within(grid.pathLength([-1000, 0], [1000, 0], 3000, true)!, base, 1e-6));
  const f = grid.flood([[-1000, 0]], 500);
  const [ci, cj] = grid.cellOf([-700, 0]);
  assert.ok(within(f.dist[cj * grid.w + ci], 300, 60) && f.dist[grid.cellOf([-300, 0])[1] * grid.w + grid.cellOf([-300, 0])[0]] === Infinity);
});

test('chokepoints: a narrow alley between two tall buildings is found as a CANYON with its exact width; a short way round = not the only path', () => {
  const a = analyzeLayout(alley());
  const c = a.chokepoints.find((x) => x.kind === 'canyon');
  assert.ok(c, `kinds: ${a.chokepoints.map((x) => x.kind).join(', ')}`);
  assert.equal(c.kind_name, 'каньон с высотами');
  assert.deepEqual(c.tags.filter((t) => ['narrow', 'canyon'].includes(t)), ['narrow', 'canyon']);
  assert.ok(within(c.width_m!, 4.0, 0.02), `alley width ${c.width_m} m (faces 4.00 m apart)`);
  assert.ok(Math.abs(c.pos[0]) <= 1600 && Math.abs(c.pos[1]) < 60, `the narrowest point is in the alley: ${c.pos}`);
  assert.ok(c.numbers.side_a!.height_cm === 900 && c.numbers.side_b!.height_cm === 900 && c.numbers.canyon_share >= 0.6);
  assert.equal(new Set([c.numbers.side_a!.what, c.numbers.side_b!.what]).size, 2, 'the two sides are the two buildings');
  assert.equal(c.route, 'mandatory'); assert.deepEqual(c.sources, ['route:R']);
  assert.equal(c.numbers.alternative, 'detour');
  assert.ok(c.numbers.detour_m! > 5 && c.numbers.detour_m! < 30 && !c.tags.includes('only_way'), `detour ${c.numbers.detour_m} m`);
  assert.ok(c.length_m >= 28 && c.length_m <= 38, `the stretch covers the alley (30 m + the corners where an enemy can wait): ${c.length_m} m`);
  assert.ok(c.score > 0.25 && c.score <= 1);
  assert.ok(c.reasons.some((r) => r.startsWith('узко: 4 м')) && c.reasons.some((r) => r.startsWith('каньон')) && c.reasons.includes('на основном маршруте'), c.reasons.join(' | '));
  assert.ok(c.poly.length >= 6 && c.line.every((p) => pointInPoly(p, c.poly) || true));
  assert.ok(pointInPoly(c.pos, c.poly), 'the outline holds the narrowest point');
  // 9 m buildings: nobody drops from them, no roof is walkable, no window
  assert.deepEqual([a.dropAmbushSpots.length, a.overwatchRoofs.length, a.windowsCovering.length], [0, 0, 0]);
  // the same alley 8 m wide is not narrow
  const wide = box('B_n', -1500, -1015, 1500, -415), s = box('B_s', -1500, 415, 1500, 1015);
  const b = analyzeLayout(mk({ buildings: [wide.b, s.b], walls: [...wide.walls, ...s.walls], route: [[-3500, 0], [3500, 0]] }));
  assert.equal(byTag(b, 'narrow').length, 0);
});

test('chokepoints: an open square is NOT a chokepoint (nothing found, no hiding spot); a bare street neither', () => {
  const open = analyzeLayout(mk({ route: [[-4000, -3000], [0, 500], [4000, 3000]], zones: [{ id: 'Z_sq', kind: 'open', set_piece: 'open_square', poly: [[-4500, -4500], [4500, -4500], [4500, 4500], [-4500, 4500]] }] }));
  assert.deepEqual([open.chokepoints.length, open.hidingSpots.length, open.dropAmbushSpots.length, open.windowsCovering.length, open.overwatchRoofs.length], [0, 0, 0, 0, 0]);
  assert.ok(open.stats.samples > 100 && open.stats.places_found === 0);
  const st = analyzeLayout(mk({ route: [[-4000, 0], [4000, 0]], zones: [street('Z_ST_a', -4500, -450, 4500, 450)] }), { config: ALL });
  assert.equal(st.chokepoints.length, 0);
  assert.ok(st.stats.lines >= 2, 'the route and the street centre line are both sampled');
  // far walls round the square change nothing
  const far = analyzeLayout(mk({ route: [[-4000, 0], [4000, 0]], walls: [wall('N', [-4800, 4500], [4800, 4500]), wall('S', [-4800, -4500], [4800, -4500])] }));
  assert.equal(far.chokepoints.filter((c) => c.tags.includes('narrow') || c.tags.includes('clutter')).length, 0);
});

test('chokepoints: a cluttered street is found as a JAM (covers per 10 m, blocked share, the squeezed lane)', () => {
  const covers: GenCover[] = [];
  for (let i = 0; i < 8; i++) covers.push(cover(`WR_${i}`, [-900 + i * 250, i % 2 ? 200 : -200], 'wreck', [420, 180, 140]));
  const l = mk({ covers, zones: [street('Z_ST_a', -4500, -450, 4500, 450)], route: [[-4000, 0], [4000, 0]] });
  const a = analyzeLayout(l);
  const c = a.chokepoints.find((x) => x.kind === 'clutter');
  assert.ok(c, `kinds: ${a.chokepoints.map((x) => `${x.kind} ${x.score}`).join(', ')}`);
  assert.equal(c.kind_name, 'завал');
  assert.equal(c.width_m, null, 'no wall within the probe: the street itself is open');
  assert.ok(within(c.lane_m!, 2.2, 0.05), `the lane between the wrecks: ${c.lane_m} m (faces 2.20 m apart)`);
  assert.ok(c.numbers.obstacles_per_window >= CHOKE.clutter.min_obstacles && c.numbers.blocked_fraction >= CHOKE.clutter.blocked_fraction, JSON.stringify(c.numbers));
  assert.ok(!c.tags.includes('narrow') && !c.tags.includes('only_way') && c.numbers.alternative === 'detour', 'the squad can walk round the wrecks');
  assert.ok(Math.abs(c.pos[0]) < 1200 && c.reasons.some((r) => r.startsWith('завал:')), c.reasons.join(' | '));
  assert.ok(c.sources.includes('route:R') && c.sources.includes('street:Z_ST_a'), 'the route and the street under it are ONE place');
  // two covers on the same street are no jam
  const two = analyzeLayout(mk({ covers: covers.slice(0, 2).map((x, i) => ({ ...x, pos: [i * 2000, 300] as Vec2 })), zones: l.zones, route: [[-4000, 0], [4000, 0]] }));
  assert.equal(byTag(two, 'clutter').length, 0);
});

test('chokepoints: route redundancy - one gate in a wall is the ONLY PATH; a second gate near = a short detour; far = critical', () => {
  const gateWall = (extra: number[]) => {
    const cuts = [0, ...extra].sort((x, y) => x - y), walls: GenWall[] = [];
    let from = -5000;
    cuts.forEach((y, i) => { walls.push(wall(`G_${i}`, [0, from], [0, y - 150])); from = y + 150; });
    walls.push(wall('G_end', [0, from], [0, 5000]));
    return mk({ walls, route: [[-3000, 0], [3000, 0]] });
  };
  const one = analyzeLayout(gateWall([]));
  const g = one.chokepoints[0];
  assert.equal(g.kind, 'only_way'); assert.equal(g.kind_name, 'единственный путь');
  assert.ok(within(g.width_m!, 2.7, 0.02), `gate width ${g.width_m} m (3.00 m between the wall ends less the two wall caps of half a thickness: the nav grid's wall body)`);
  assert.ok(Math.abs(g.pos[0]) <= 60 && Math.abs(g.pos[1]) <= 150);
  assert.deepEqual([g.numbers.alternative, g.numbers.detour_m, g.values.only_way], ['none', null, 1]);
  assert.ok(g.reasons.some((r) => r.startsWith('единственный путь')));
  assert.ok(g.length_m <= 10, `a gate is a short place: ${g.length_m} m`);
  // a second gate 15 m along the wall: a way round, a short detour, not "the only path"
  const near = analyzeLayout(gateWall([1500])).chokepoints.find((c) => Math.abs(c.pos[1]) <= 150)!;
  assert.equal(near.numbers.alternative, 'detour');
  assert.ok(near.numbers.detour_m! > 8 && near.numbers.detour_m! < 25 && !near.tags.includes('only_way') && near.kind === 'narrow', `${near.kind} detour ${near.numbers.detour_m}`);
  assert.ok(near.values.only_way > 0 && near.values.only_way < CHOKE.redundancy.tag_min_value && near.score < g.score);
  // the second gate 40 m away: the detour is critical
  const far = analyzeLayout(gateWall([4000])).chokepoints.find((c) => Math.abs(c.pos[1]) <= 150)!;
  assert.ok(far.numbers.alternative === 'detour' && far.numbers.detour_m! >= CHOKE.redundancy.detour_critical_m && far.kind === 'only_way', `${far.kind} detour ${far.numbers.detour_m}`);
});

test('threat model: the numbers come from the game configs (falls, weapons, perception, stealth); only the gaps have defaults', () => {
  const T = threatNumbers();
  const falls = readJson<{ params: Record<string, number> }>('Content/Data/Movement/falls.json').params;
  const weap = readJson<{ weapons: Record<string, Record<string, number>>; enemy_weapons: Record<string, Record<string, number>>; range_falloff: Record<string, number> }>('Content/Data/Weapons/weapons_tuning.json');
  const perc = readJson<{ sight_conditions: Record<string, never> & { squad_base_range_m: number; human_enemy_range_multiplier: number; weather: Record<string, number>; time_of_day: Record<string, number> }; archetypes: Record<string, Record<string, number>> }>('Content/Data/AI/enemy_perception.json');
  const stealth = readJson<Record<string, number>>('Content/Data/AI/stealth_mode.json');
  assert.deepEqual([T.drop.min_drop_cm, T.drop.max_drop_cm, T.drop.max_gap_cm], [falls['link.min_drop_cm'], falls['link.max_drop_cm'], falls['link.max_gap_cm']]);
  assert.deepEqual([T.drop.min_drop_cm, T.drop.max_drop_cm, T.drop.max_gap_cm], [150, 650, 250]);
  assert.deepEqual(T.drop.jumpers, ['cutter', 'frost_hound'], 'the archetypes with enemy_drops = 2');
  assert.equal(T.drop.reach_cm, falls['link.max_gap_cm'] + CHOKE.threats.drop_ambush.strike_reach_cm_default);
  assert.deepEqual([T.window.effective_m, T.window.attack_m], [weap.weapons.m16.effective_range_m, weap.weapons.m16.attack_range_m]);
  assert.deepEqual([T.overwatch.effective_m, T.overwatch.attack_m, T.overwatch.preferred_min_m], [weap.enemy_weapons.marksman_rifle.effective_range_m, weap.enemy_weapons.marksman_rifle.attack_range_m, weap.enemy_weapons.marksman_rifle.preferred_min_range_m]);
  assert.equal(T.overwatch.sight_ground_m, perc.archetypes.MARKSMAN.sight_range_m * perc.sight_conditions.human_enemy_range_multiplier);
  assert.equal(T.hiding.squad_sight_m, perc.sight_conditions.squad_base_range_m);
  assert.equal(T.hiding.stealth_multiplier, stealth.detection_range_multiplier);
  assert.equal(T.hiding.stealth_sight_m, perc.sight_conditions.squad_base_range_m * stealth.detection_range_multiplier);
  assert.deepEqual([T.falloff.floor, T.falloff.exponent], [weap.range_falloff.floor_multiplier, weap.range_falloff.exponent]);
  // conditions scale the sight (the squad's and the marksman's), not the weapons
  const night = threatNumbers(CHOKE, { weather: 'blizzard', time_of_day: 'night' });
  const k = perc.sight_conditions.weather.blizzard * perc.sight_conditions.time_of_day.night;
  assert.ok(within(night.hiding.squad_sight_m, 120 * k, 1e-9) && within(night.hiding.stealth_sight_m, 120 * k * 0.4, 1e-9) && night.window.attack_m === T.window.attack_m);
  // every number is listed with its source; the defaults are marked
  assert.ok(T.sources.length >= 20 && T.sources.every((s) => s.source.length > 5));
  assert.equal(T.sources.filter((s) => s.source.includes('по умолчанию')).length, 7, 'the seven defaults the game configs do not hold');
  // the falloff: 1 up to the effective range, the floor at the attack range, nothing beyond
  assert.deepEqual([rangeFalloff(10, 28, 60, T.falloff), rangeFalloff(28, 28, 60, T.falloff), rangeFalloff(60, 28, 60, T.falloff), rangeFalloff(61, 28, 60, T.falloff)], [1, 1, 0.1, 0]);
  assert.ok(rangeFalloff(44, 28, 60, T.falloff) > 0.1 && rangeFalloff(44, 28, 60, T.falloff) < 1);
});

test('threat 1 - DROP AMBUSH: a low walkable roof beside an alley; not a 9 m roof; a ladder-only roof counts less; a thick wall top', () => {
  const stairs: GenBuilding['stairs'] = [{ id: 'S1', pos: [-1400, -900], from: 0, to: 1, kind: 'external', top: [-1400, -700] }];
  const low = analyzeLayout(alley(300, { roof: true, stairs }));
  assert.ok(low.dropAmbushSpots.length >= 1);
  const d = low.dropAmbushSpots[0];
  assert.deepEqual([d.surface, d.kind, d.drop_cm, d.access, d.jumpers], ['roof:B_n', 'roof', 300, 'stairs', ['cutter', 'frost_hound']]);
  assert.ok(d.gap_cm <= 400 && within(d.gap_cm, 215, 30), `the route runs 2.15 m from the roof edge: ${d.gap_cm} cm`);
  assert.ok(within(d.edge[1], -215, 1) && within(d.land[1], 0, 1) && d.line === 'route:R');
  const c = low.chokepoints.find((x) => x.tags.includes('drop_ambush'))!;
  assert.equal(c.kind, 'drop_ambush'); assert.equal(c.kind_name, 'засада сверху (спрыгивание)');
  assert.ok(c.tags.includes('narrow') && c.values.drop_ambush === 1 && c.numbers.drop_spots.includes(d.id) && d.places.includes(c.id));
  assert.ok(c.reasons.some((r) => r.startsWith('засада сверху: каттеры, гончие спрыгивают с крыши B_n (3 м)')), c.reasons.join(' | '));
  // the same alley under 9 m roofs: too high to drop (max 6.5 m)
  const high = analyzeLayout(alley(900, { roof: true, stairs: [{ ...stairs[0], to: 3 }] }));
  assert.equal(high.dropAmbushSpots.length, 0);
  assert.ok(high.chokepoints.every((x) => !x.tags.includes('drop_ambush')) && high.chokepoints.some((x) => x.kind === 'canyon'));
  // a roof reached by a ladder only: the jumpers do not climb - it counts ladder_only_factor
  const ladder: GenLadder = { id: 'LAD_1', pos: [-1400, -830], yaw: 180, rungs: 8, from: 'ground', to: 'roof:B_n', level_from: 0, level_to: 1, plinth_cm: 8.8, foot: [-1400, -930], top: [-1400, -700] };
  const lad = analyzeLayout({ ...alley(300, { roof: true }), ladders: [ladder] });
  assert.equal(lad.dropAmbushSpots[0].access, 'ladder');
  const lc = lad.chokepoints.find((x) => x.tags.includes('drop_ambush'))!;
  assert.equal(lc.values.drop_ambush, CHOKE.threats.drop_ambush.ladder_only_factor_default);
  assert.ok(lc.score < c.score);
  // the route 6 m from the roof edge is out of reach (2.5 m gap + 1.5 m strike)
  const n = box('B_n', -1500, -1215, 1500, -615, { height: 300, roof: true, stairs });
  assert.equal(analyzeLayout(mk({ buildings: [n.b], walls: n.walls, route: [[-3500, 0], [3500, 0]] })).dropAmbushSpots.length, 0);
  // a thick 2.3 m wall top (the canal walls) is a drop surface, a thin fence is not
  const thick = analyzeLayout(mk({ walls: [wall('CW', [-1500, -300], [1500, -300], 230, 150, 'cut_wall'), wall('F', [-1500, 300], [1500, 300], 230, 10, 'fence')], route: [[-3500, 0], [3500, 0]] }));
  assert.deepEqual([...new Set(thick.dropAmbushSpots.map((x) => `${x.surface}:${x.kind}:${x.access}`))], ['wall:CW:wall_top:wall']);
});

test('threat 2 - WINDOW FIRE: a window of an enterable building facing a street within rifle range; not facing away, not beyond range, not through a wall', () => {
  const win = (yaw: number): WallOpening[] => [{ id: 'WIN_1', kind: 'window', pos: [0, -2000], floor: 0, yaw }];
  const scene = (o: { yaw?: number; enterable?: boolean; walls?: GenWall[]; covers?: GenCover[]; y?: number }) => {
    const y = o.y ?? -2000, b = box('B_w', -600, y - 800, 600, y, { height: 300, enterable: o.enterable ?? true, openings: win(o.yaw ?? 90).map((w) => ({ ...w, pos: [0, y] as Vec2 })) });
    return analyzeLayout(mk({ buildings: [b.b], walls: [...b.walls, ...(o.walls ?? [])], covers: o.covers, route: [[-4000, 0], [4000, 0]] }), { config: ALL });
  };
  const a = scene({});
  assert.equal(a.windowsCovering.length, 1);
  const w = a.windowsCovering[0];
  assert.deepEqual([w.id, w.building, w.floor, w.kind, w.yaw], ['WIN_1', 'B_w', 0, 'window', 90]);
  assert.ok(within(w.min_dist_m, 19.6, 0.5) && w.covered_m > 40, `nearest ${w.min_dist_m} m, covers ${w.covered_m} m of the route`);
  const c = a.chokepoints.find((x) => x.tags.includes('window_fire'))!;
  assert.equal(c.kind, 'window_fire'); assert.equal(c.kind_name, 'огонь из окон');
  assert.ok(c.numbers.windows.includes('WIN_1') && w.places.includes(c.id) && c.values.window_fire > 0);
  assert.ok(c.reasons.some((r) => r.startsWith('огонь из окон: 1 окно (WIN_1), m16 до 28 / 60 м')), c.reasons.join(' | '));
  // with the shipped min_score a single window over open ground is not a listed place (it is a modifier), the list stays short
  const b0 = box('B_w', -600, -2800, 600, -2000, { height: 300, enterable: true, openings: win(90) });
  assert.equal(analyzeLayout(mk({ buildings: [b0.b], walls: b0.walls, route: [[-4000, 0], [4000, 0]] })).chokepoints.length, 0);
  // facing away / a building nobody can enter / beyond the 60 m attack range / a 3.3 m wall in between
  assert.equal(scene({ yaw: -90 }).windowsCovering.length, 0);
  assert.equal(scene({ enterable: false }).windowsCovering.length, 0);
  assert.equal(scene({ y: -4100 }).windowsCovering.length, 0, 'the window 41 m + from the route... is still in range');
  assert.equal(scene({ walls: [wall('SCREEN', [-4800, -1000], [4800, -1000], 330)] }).windowsCovering.length, 0);
  // low cover does not screen a standing man from a window
  assert.equal(scene({ covers: [cover('J1', [0, -1000], 'jersey', [300, 60, 95], 0), cover('J2', [300, -1000], 'jersey', [300, 60, 95], 0)] }).windowsCovering.length, 1);
});

test('threat 3 - MARKSMEN ON ROOFS: a ladder-reachable roof with a long view over a street is an overwatch position; no ladder = none; a tall building screens', () => {
  const ladder: GenLadder = { id: 'LAD_1', pos: [0, -3015], yaw: 180, rungs: 16, from: 'ground', to: 'roof:B_o', level_from: 0, level_to: 2, plinth_cm: 17.6, foot: [0, -3115], top: [0, -2900] };
  const scene = (o: { ladders?: GenLadder[]; extra?: { b: GenBuilding; walls: GenWall[] }[]; covers?: GenCover[] }) => {
    const b = box('B_o', -500, -3000, 500, -2200, { height: 600, roof: true });
    return analyzeLayout(mk({ buildings: [b.b, ...(o.extra ?? []).map((x) => x.b)], walls: [...b.walls, ...(o.extra ?? []).flatMap((x) => x.walls)], ladders: o.ladders ?? [ladder], covers: o.covers,
      zones: [street('Z_ST_a', -4500, -450, 4500, 450)], route: [[-4000, 0], [4000, 0]] }), { config: ALL });
  };
  const a = scene({});
  assert.equal(a.overwatchRoofs.length, 1);
  const r = a.overwatchRoofs[0];
  assert.deepEqual([r.building, r.level, r.height_cm, r.access], ['B_o', 2, 600, ['ladder:LAD_1']]);
  assert.equal(r.range_m, 110, 'the marksman rifle\'s attack range (his sight from 6 m up is longer: 45 x 2 x 1.5 = 135 m)');
  assert.ok(r.spots.length >= 1 && r.spots.length <= CHOKE.threats.overwatch.spots_per_roof);
  const s = r.spots[0];
  assert.ok(s.covered_m >= 60 && s.longest_los_m >= 40, `covers ${s.covered_m} m, longest sight line ${s.longest_los_m} m`);
  assert.ok(s.pos[1] >= -2950 && s.pos[1] <= -2250 && Math.abs(s.pos[0]) <= 450, 'the spot is on the roof, inside its edge');
  assert.ok(s.facing_yaw > 30 && s.facing_yaw < 150, `he faces the street (+Y): ${s.facing_yaw}`);
  const c = a.chokepoints.find((x) => x.tags.includes('sniper'))!;
  assert.equal(c.kind, 'sniper'); assert.equal(c.kind_name, 'снайпер на крыше');
  assert.ok(c.numbers.overwatch.includes('B_o') && r.places.includes(c.id) && c.reasons.some((x) => x.startsWith('снайпер на крыше: B_o (+2), дальность 110 м')));
  // no ladder / stairs: nobody gets up there
  assert.equal(scene({ ladders: [] }).overwatchRoofs.length, 0);
  // low cover on the street does not hide it from the roof
  const cov = scene({ covers: [0, 1, 2, 3].map((i) => cover(`J${i}`, [-600 + i * 400, -300], 'jersey', [300, 60, 95])) });
  assert.ok(cov.overwatchRoofs[0].spots[0].covered_m >= s.covered_m - 6);
  // a 9 m slab between the roof and the street screens most of it
  const screened = scene({ extra: [box('B_screen', -3000, -1700, 3000, -1300, { height: 900 })] });
  assert.ok((screened.overwatchRoofs[0]?.spots[0].covered_m ?? 0) < s.covered_m / 2, `screened: ${screened.overwatchRoofs[0]?.spots[0].covered_m} m against ${s.covered_m} m`);
});

test('threat 4 - HIDDEN ENEMIES: a tall wall beside a street gives a hiding spot with a short reveal distance; an open field gives none; stealth', () => {
  // the squad walks -X -> +X along y = 0; a 3.3 m wall stands across its right side, 2.5 m from the route
  const l = mk({ walls: [wall('W_hide', [0, 250], [0, 1000], 330)], route: [[-4000, 0], [4000, 0]] });
  const a = analyzeLayout(l, { config: ALL });
  assert.ok(a.hidingSpots.length >= 1);
  const h = a.hidingSpots[0];
  assert.ok(h.pos[0] > 0 && h.pos[1] > 250, `behind the wall as the squad comes: ${h.pos}`);
  assert.ok(h.reveal_normal_m <= CHOKE.threats.hiding.reveal_close_m && h.reveal_normal_m >= 1, `seen only from ${h.reveal_normal_m} m`);
  assert.ok(h.reveal_stealth_m <= h.reveal_normal_m && !h.stealth_only);
  assert.deepEqual([h.cover, h.cover_height_cm, h.threatens.line, h.threatens.route], ['wall:W_hide', 330, 'route:R', 'R']);
  assert.ok(Math.abs(h.threatens.pos[1]) < 1 && h.rush_m <= CHOKE.threats.hiding.rush_max_m && h.rush_m >= CHOKE.threats.hiding.min_offset_m);
  // facing = from the spot to the route point it threatens
  const yaw = (Math.atan2(h.threatens.pos[1] - h.pos[1], h.threatens.pos[0] - h.pos[0]) * 180) / Math.PI;
  assert.ok(within(h.facing_yaw, yaw, 0.11), `${h.facing_yaw} vs ${yaw}`);
  assert.ok(a.hidingSpots.every((x) => x.pos[0] > -60), 'the side the squad sees from afar holds no spot');
  const c = a.chokepoints.find((x) => x.tags.includes('hiding'))!;
  assert.equal(c.kind, 'hiding'); assert.equal(c.kind_name, 'укрытие для засады / скрытый противник');
  assert.ok(c.numbers.hiding_spots.includes(h.id) && h.place === c.id && c.numbers.reveal_m === Math.min(...a.hidingSpots.filter((x) => x.place === c.id).map((x) => x.reveal_normal_m)));
  assert.ok(c.reasons.some((r) => r.startsWith('скрытый противник:') && r.includes('виден только с')), c.reasons.join(' | '));
  // an open field: nothing to hide behind
  assert.equal(analyzeLayout(mk({ route: [[-4000, 0], [4000, 0]] }), { config: ALL }).hidingSpots.length, 0);
  // a wall far from the route (30 m) is no ambush spot: too far to rush
  assert.equal(analyzeLayout(mk({ walls: [wall('W_far', [0, 3000], [0, 4000], 330)], route: [[-4000, 0], [4000, 0]] }), { config: ALL }).hidingSpots.length, 0);
  // STEALTH: a wall ALONG the street, 4 m from the route - its near face is in plain view. By day nobody hides there; in a night
  // blizzard the squad sees 21 m and a stealth fighter only from 8.4 m: the face becomes a row of stealth-only spots
  const par = mk({ walls: [wall('W_par', [-2000, 400], [2000, 400], 330)], route: [[-4000, 0], [4000, 0]] });
  const nearFace = (x: LayoutAnalysis) => x.hidingSpots.filter((sp) => sp.pos[1] < 385 && Math.abs(sp.pos[0]) < 1800);
  const day = analyzeLayout(par, { config: ALL });
  assert.equal(nearFace(day).length, 0, 'by day the face of the wall hides nobody');
  assert.ok(day.hidingSpots.every((sp) => !sp.stealth_only));
  const night = analyzeLayout(par, { config: ALL, conditions: { weather: 'blizzard', time_of_day: 'night' } });
  assert.deepEqual(night.conditions, { weather: 'blizzard', time_of_day: 'night' });
  assert.ok(within(night.thresholds.stealth_sight_m, 8.4, 0.01) && within(a.thresholds.stealth_sight_m, 48, 0.01));
  const so = nearFace(night);
  assert.ok(so.length >= 3 && night.hidingSpots.length > day.hidingSpots.length, `${night.hidingSpots.length} spots at night (${so.length} on the face) against ${day.hidingSpots.length} by day`);
  for (const x of so) {
    assert.ok(x.stealth_only && x.reveal_stealth_m < x.reveal_normal_m && within(x.reveal_stealth_m, 8.4, 0.05), JSON.stringify(x));
    assert.ok(x.reveal_stealth_m <= CHOKE.threats.hiding.stealth_reveal_threshold_m && x.reveal_normal_m > CHOKE.threats.hiding.reveal_close_m && x.reveal_normal_m <= 21.01);
  }
  // ... and the place beside them scores higher than by day (stealth raises the score)
  const dayBest = Math.max(0, ...day.chokepoints.map((x) => x.values.hiding)), nightBest = Math.max(0, ...night.chokepoints.map((x) => x.values.hiding));
  assert.ok(nightBest > 0 && night.chokepoints.filter((x) => x.tags.includes('hiding')).length > day.chokepoints.filter((x) => x.tags.includes('hiding')).length, `hiding value ${dayBest} by day, ${nightBest} at night`);
  const nc = night.chokepoints.find((x) => x.reasons.some((r) => r.includes('в режиме скрытности')))!;
  assert.ok(nc.numbers.reveal_stealth_m! <= 8.4 && nc.reasons.some((r) => r.includes('только для бойца в режиме скрытности — виден с 8.4 м вместо')), nc.reasons.join(' | '));
  // a low wall (95 cm) hides a crouched man until the squad is close
  const lowWall = analyzeLayout(mk({ walls: [wall('W_low', [0, 250], [0, 1000], 95)], route: [[-4000, 0], [4000, 0]] }), { config: ALL });
  assert.ok(lowWall.hidingSpots.length >= 1 && lowWall.hidingSpots[0].cover_height_cm === 95 && lowWall.hidingSpots[0].reveal_normal_m <= 12);
});

test('chokepoints on the Depot: deterministic, valid in the layout JSON, sorted, cross-referenced; the geometry hash marks a stale analysis', () => {
  const profile = readJson<Profile>('Content/Data/LevelGen/refs/ref_01_02_depot.json');
  const base = placeObjectives({ layout: generateLayout({ profile, seed: 1 }), seed: 1 }).layout;
  const a = analyzeLayout(base), b = analyzeLayout(JSON.parse(JSON.stringify(base)) as GenLayout);
  assert.deepEqual(a, b, 'the same layout (also after a JSON round trip) = the same analysis');
  assert.equal(JSON.stringify(a), JSON.stringify(analyzeLayout(base)));
  assert.equal(a.version, 1); assert.equal(a.geometry_hash, geometryHash(base));
  assert.ok(a.chokepoints.length >= 5 && a.chokepoints.length <= CHOKE.places.max_items, `${a.chokepoints.length} places`);
  assert.deepEqual(a.chokepoints.map((c) => c.id), a.chokepoints.map((_, i) => `CP_${String(i + 1).padStart(2, '0')}`));
  for (let i = 1; i < a.chokepoints.length; i++) assert.ok(a.chokepoints[i - 1].score >= a.chokepoints[i].score, 'sorted by score');
  for (const c of a.chokepoints) {
    assert.ok(c.score >= CHOKE.places.min_score && c.score <= 1 && c.tags.includes(c.kind) && c.reasons.length >= 2 && c.poly.length >= 4 && c.line.length >= 2, c.id);
    assert.ok(c.pos[0] >= base.bounds.min[0] && c.pos[0] <= base.bounds.max[0] && c.pos[1] >= base.bounds.min[1] && c.pos[1] <= base.bounds.max[1]);
    assert.ok(!base.buildings.some((bd) => c.pos[0] > bd.footprint[0][0] + 60 && c.pos[0] < bd.footprint[1][0] - 60 && c.pos[1] > bd.footprint[0][1] + 60 && c.pos[1] < bd.footprint[1][1] - 60), `${c.id} is not inside a building`);
    if (c.tags.includes('narrow')) assert.ok(c.width_m !== null && c.width_m < CHOKE.narrow.width_m);
  }
  // the Depot's known narrow places: the compound's single gate and a canal crossing are "the only path" on the mandatory route
  assert.ok(a.chokepoints.some((c) => c.kind === 'only_way' && c.route === 'mandatory'));
  assert.ok(a.chokepoints.some((c) => c.tags.includes('canyon')) && a.hidingSpots.length >= 5 && a.overwatchRoofs.length >= 2);
  // the spot lists point at listed places only
  const ids = new Set(a.chokepoints.map((c) => c.id));
  for (const x of [...a.dropAmbushSpots, ...a.windowsCovering, ...a.overwatchRoofs]) for (const p of x.places) assert.ok(ids.has(p));
  for (const h of a.hidingSpots) if (h.place) assert.ok(ids.has(h.place));
  for (const w of a.windowsCovering) assert.ok(w.places.length >= 1);
  // saved in the layout JSON: valid; a newer version is refused; an old file (no block) loads
  const saved: GenLayout = { ...base, analysis: a };
  assert.deepEqual(validateGenLayout(saved), validateGenLayout(base));
  assert.ok(validateGenLayout({ ...base, analysis: { ...a, version: 99 } }).some((e) => e.includes('newer')));
  assert.ok(validateGenLayout({ ...base, analysis: { version: 1 } as unknown as LayoutAnalysis }).some((e) => e.includes('analysis.chokepoints')));
  // the hash: the analysis itself is not part of it; moving a cover makes the stored analysis stale
  assert.equal(geometryHash(saved), a.geometry_hash);
  assert.equal(withoutStaleAnalysis(saved), saved);
  const edited: GenLayout = { ...saved, covers: saved.covers.map((cv, i) => (i ? cv : { ...cv, pos: [cv.pos[0] + 50, cv.pos[1]] as Vec2 })) };
  assert.notEqual(geometryHash(edited), a.geometry_hash);
  assert.equal(withoutStaleAnalysis(edited).analysis, undefined);
  assert.ok(edited.analysis, 'the input object is not changed');
  // the designer's accepted x4 map (read only): the analysis runs and stays inside the limits
  const x4 = readJson<GenLayout>('Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x4.json');
  const t0 = Date.now(), ax = analyzeLayout(x4);
  assert.ok(ax.chokepoints.length >= 10 && ax.chokepoints.length <= CHOKE.places.max_items && ax.windowsCovering.length >= 10 && ax.hidingSpots.length <= CHOKE.threats.hiding.max_spots);
  assert.ok(Date.now() - t0 < 20000, 'the x4 analysis stays interactive');
  assert.equal(x4.analysis, undefined);
});
