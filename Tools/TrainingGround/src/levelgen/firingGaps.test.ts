// Parapet FIRING GAPS (firingGaps.ts, Content/Data/LevelGen/firing_gaps.json, docs/design/levelgen/FIRING_GAPS_HANDOVER.md): the config's
// keys, planFiringGaps / withFiringGaps on synthetic roofs (spot gaps first, spacing, corner clear, caps, street-facing sides, ladder
// clear, roles), the layout schema, the tactical file (gap ids on the spots, the top-level list, byte-identical without gaps).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import type { Vec2 } from '../movement/geometry.ts';
import type { LayoutAnalysis } from './chokepoints.ts';
import { CHOKE, geometryHash } from './chokepoints.ts';
import type { FiringGapsConfig } from './firingGaps.ts';
import { FIRING_GAPS, FIRING_GAPS_KEYS, FIRING_GAPS_SCHEMA, gapForSpot, planFiringGaps, validateFiringGapsConfig, withFiringGaps } from './firingGaps.ts';
import type { FiringGap, GenBuilding, GenLadder, GenLayout, RoofParapet } from './schema.ts';
import { validateGenLayout } from './schema.ts';
import { formatTacticalMap, tacticalMapFor } from './tacticalMap.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const FILE = path.join(ROOT, 'Content/Data/LevelGen/firing_gaps.json');

// ---- the config -------------------------------------------------------------------------------------------------------------------

test('firing gaps config: the code reads exactly the keys of firing_gaps.json, each commented; the values the designer set; consistent with the rim spots', () => {
  const raw = JSON.parse(fs.readFileSync(FILE, 'utf8').replace(/^﻿/, '')) as Record<string, unknown>;
  const keys = Object.keys(raw).filter((k) => k !== 'schema' && k !== 'comment' && !k.endsWith('_comment'));
  assert.deepEqual(keys, [...FIRING_GAPS_KEYS], 'the JSON has no key the code ignores, the code reads no key the JSON lacks');
  for (const k of FIRING_GAPS_KEYS) assert.equal(typeof raw[`${k}_comment`], 'string', `${k} has a ${k}_comment`);
  assert.equal(raw.schema, FIRING_GAPS_SCHEMA);
  assert.deepEqual(validateFiringGapsConfig(FIRING_GAPS), []);
  // designer 2026-11-19: sill <= ~70 cm (60), 80-100 cm wide (90); every number in the JSON
  assert.deepEqual([FIRING_GAPS.enabled, FIRING_GAPS.sill_cm, FIRING_GAPS.width_cm, FIRING_GAPS.spacing_m, FIRING_GAPS.corner_clear_m, FIRING_GAPS.max_per_side, FIRING_GAPS.max_per_roof,
    FIRING_GAPS.stand_inset_cm, FIRING_GAPS.street_probe_m, FIRING_GAPS.ladder_clear_cm, FIRING_GAPS.roles], [true, 60, 90, 4, 1.2, 3, 8, 60, 8, 60, ['overwatch']]);
  assert.ok(FIRING_GAPS.sill_cm <= 70 && FIRING_GAPS.width_cm >= 80 && FIRING_GAPS.width_cm <= 100);
  // the analysis' rim spots stand where the gaps' shooters stand and keep the corner clear a gap needs
  assert.equal(CHOKE.threats.overwatch.rim_stand_inset_m * 100, FIRING_GAPS.stand_inset_cm);
  assert.ok(CHOKE.threats.overwatch.rim_corner_clear_m * 100 >= FIRING_GAPS.corner_clear_m * 100 + FIRING_GAPS.width_cm / 2, 'a gap fits behind every candidate spot');
  // the config's own checks
  assert.ok(validateFiringGapsConfig({ ...FIRING_GAPS, sill_cm: 0 }).length > 0);
  assert.ok(validateFiringGapsConfig({ ...FIRING_GAPS, max_per_roof: 2 }).some((e) => e.includes('max_per_roof')));
  assert.ok(validateFiringGapsConfig({ ...FIRING_GAPS, schema: 'x' }).length > 0);
});

// ---- synthetic roofs --------------------------------------------------------------------------------------------------------------

/** Parapets the way the generator writes them (the wall ids end _N _S _W _E; N = the +X side). */
function parapets(id: string, [[x0, y0], [x1, y1]]: [Vec2, Vec2], height = 95): RoofParapet[] {
  const mk = (n: string, start: Vec2, end: Vec2): RoofParapet => ({ wall_id: `${id}_parapet_${n}`, start, end, height_cm: height, thickness_cm: 20 });
  return [mk('N', [x1, y0], [x1, y1]), mk('S', [x0, y0], [x0, y1]), mk('W', [x1, y0], [x0, y0]), mk('E', [x1, y1], [x0, y1])];
}
function roof(id: string, fp: [Vec2, Vec2], o: Partial<GenBuilding> = {}): GenBuilding {
  return { id, footprint: fp, height: 600, floors: 2, enterable: true, roof_walkable: true, doors: [], stairs: [], roof_holes: [], floor_holes: [], set_piece: 'facade', parapets: parapets(id, fp), ...o } as GenBuilding;
}
function layout(buildings: GenBuilding[], ladders: GenLadder[] = []): GenLayout {
  return { schema: 'codex.levelgen/1', name: 'gaps', profile: 'test', seed: 1, params: {}, bounds: { min: [-8000, -8000], max: [8000, 8000] }, ground_z: 0, spawn: { pos: [0, 0], yaw: 0 }, end: [10, 10],
    walls: [], buildings, ladders, covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [] } as unknown as GenLayout;
}
/** An analysis stub: only the overwatch roofs are read. */
const analysisOf = (roofs: { building: string; spots: Vec2[] }[]): LayoutAnalysis => ({ overwatchRoofs: roofs.map((r) => ({ building: r.building, level: 2, height_cm: 600, range_m: 110, access: ['ladder:L'],
  spots: r.spots.map((pos) => ({ pos, facing_yaw: 0, covered_m: 100, longest_los_m: 50 })), places: [] })) }) as unknown as LayoutAnalysis;
const FP: [Vec2, Vec2] = [[0, 0], [3200, 2400]];

test('planFiringGaps: a spot gets a gap on its nearest parapet at its projection (stand 60 cm behind it, facing outward); ids, width, sill from the config', () => {
  const l = layout([roof('B_a', FP)]);
  // the spot stands 60 cm inside the W side (y = 0), 1.4 m from... nothing: mid-edge
  const g = planFiringGaps(l, analysisOf([{ building: 'B_a', spots: [[1700, 60]] }])).filter((x) => x.parapet === 'B_a_parapet_W');
  const spotGap = g[0];
  assert.equal(spotGap.id, 'FG_B_a_1', 'spot gaps come first');
  assert.deepEqual([spotGap.building, spotGap.parapet, spotGap.width_cm, spotGap.sill_cm], ['B_a', 'B_a_parapet_W', 90, 60]);
  assert.deepEqual(spotGap.center, [1700, 0]);
  assert.deepEqual(spotGap.outward, [0, -1]);
  assert.deepEqual(spotGap.stand, [1700, 60], 'center - outward x stand_inset_cm = the spot');
  assert.equal(spotGap.facing_yaw, -90);
  assert.equal(gapForSpot([spotGap], 'B_a', [1700, 60])?.id, 'FG_B_a_1');
  assert.equal(gapForSpot([spotGap], 'B_a', [1700 + 200, 60]), null, 'a spot 2 m along the parapet is another gap\'s');
  assert.equal(gapForSpot([spotGap], 'B_a', [1700, 2340]), null, 'the far side of the roof is not behind it');
});

test('planFiringGaps: corner clear, spacing, per-side / per-roof caps, deterministic order (spot gaps, then sides N S W E), every gap id unique', () => {
  const l = layout([roof('B_a', FP)]);
  const an = analysisOf([{ building: 'B_a', spots: [[1700, 60]] }]);
  const gaps = planFiringGaps(l, an);
  assert.deepEqual(gaps, planFiringGaps(JSON.parse(JSON.stringify(l)) as GenLayout, JSON.parse(JSON.stringify(an)) as LayoutAnalysis), 'deterministic');
  assert.ok(gaps.length > 1 && gaps.length <= FIRING_GAPS.max_per_roof);
  assert.deepEqual(gaps.map((g) => g.id), gaps.map((_, i) => `FG_B_a_${i + 1}`));
  const perSide = new Map<string, number>();
  for (const g of gaps) perSide.set(g.parapet, (perSide.get(g.parapet) ?? 0) + 1);
  for (const n of perSide.values()) assert.ok(n <= FIRING_GAPS.max_per_side);
  // corner clear: a gap's edge keeps 1.2 m from either end of its parapet
  const lenOf = (g: FiringGap) => { const p = l.buildings[0].parapets!.find((x) => x.wall_id === g.parapet)!; return Math.hypot(p.end[0] - p.start[0], p.end[1] - p.start[1]); };
  const tOf = (g: FiringGap) => { const p = l.buildings[0].parapets!.find((x) => x.wall_id === g.parapet)!; return Math.hypot(g.center[0] - p.start[0], g.center[1] - p.start[1]); };
  for (const g of gaps) assert.ok(tOf(g) - g.width_cm / 2 >= FIRING_GAPS.corner_clear_m * 100 - 0.5 && lenOf(g) - tOf(g) - g.width_cm / 2 >= FIRING_GAPS.corner_clear_m * 100 - 0.5, `${g.id} keeps the corner clear`);
  // spacing: no two centres closer than spacing_m
  for (let i = 0; i < gaps.length; i++) for (let j = i + 1; j < gaps.length; j++) assert.ok(Math.hypot(gaps[i].center[0] - gaps[j].center[0], gaps[i].center[1] - gaps[j].center[1]) >= FIRING_GAPS.spacing_m * 100 - 1, `${gaps[i].id} / ${gaps[j].id} spacing`);
  // the first gap is the spot's, then the extra gaps run through the sides in the order N, S, W, E (the W side has the spot gap, so its extras come after the S ones)
  const order = gaps.slice(1).map((g) => g.parapet.slice(-1));
  const rank = (c: string) => 'NSWE'.indexOf(c);
  for (let i = 1; i < order.length; i++) assert.ok(rank(order[i]) >= rank(order[i - 1]), `sides in N S W E order: ${order.join('')}`);
  // a tighter config: caps bite (2 per side, 3 per roof)
  const tight: FiringGapsConfig = { ...FIRING_GAPS, max_per_side: 2, max_per_roof: 3 };
  const t = planFiringGaps(l, an, tight);
  assert.equal(t.length, 3);
  assert.equal(t[0].parapet, 'B_a_parapet_W', 'the spot gap survives the cap');
  // a parapet shorter than two corner clears + a width has no gap
  assert.deepEqual(planFiringGaps(layout([roof('B_s', [[0, 0], [250, 250]])]), analysisOf([{ building: 'B_s', spots: [[125, 60]] }])), []);
});

test('planFiringGaps: extra gaps only on STREET-FACING sides (the probe point is in the map and outside every other footprint)', () => {
  // B_b stands 3 m east of B_a: B_a's N side (x = 3200) faces B_b, not a street; the other sides probe 8 m out over open ground
  const a = roof('B_a', FP), b = roof('B_b', [[3500, 0], [6700, 2400]]);
  const gaps = planFiringGaps(layout([a, b]), analysisOf([{ building: 'B_a', spots: [[1700, 60]] }]));
  assert.ok(gaps.length > 0 && gaps.every((g) => g.building === 'B_a'), 'B_b is no overwatch roof and has no role');
  assert.ok(gaps.every((g) => g.parapet !== 'B_a_parapet_N'), 'the side that looks at the neighbour wall has no extra gap');
  assert.ok(gaps.some((g) => g.parapet === 'B_a_parapet_S'));
  // at the map edge the probe leaves the map: no gap on that side
  const edge = layout([roof('B_e', [[-7900, 0], [-4700, 2400]])]);
  const eg = planFiringGaps(edge, analysisOf([{ building: 'B_e', spots: [[-6200, 60]] }]));
  assert.ok(eg.every((g) => g.parapet !== 'B_e_parapet_S' || g.id === 'FG_B_e_1'), 'the S side (x = -7900, 8 m out = -8700) leaves the map');
});

test('planFiringGaps: which roofs - overwatch roofs of the analysis or a role from the config; walkable with parapets only; the config can switch it off', () => {
  const plain = roof('B_p', FP), noParapet = roof('B_n', [[4000, 0], [7200, 2400]], { parapets: undefined }), flat = roof('B_f', [[-4000, 0], [-800, 2400]], { roof_walkable: false });
  const l = layout([plain, noParapet, flat]);
  assert.deepEqual(planFiringGaps(l, analysisOf([])), [], 'no analysis roof, no role: no gaps');
  assert.deepEqual(planFiringGaps(l, null), []);
  assert.ok(planFiringGaps(l, analysisOf([{ building: 'B_p', spots: [] }])).length > 0, 'an analysis roof without spots still gets the street-side gaps');
  assert.deepEqual(planFiringGaps(l, analysisOf([{ building: 'B_n', spots: [[5500, 60]] }, { building: 'B_f', spots: [[-2400, 60]] }])), [], 'no parapets / not walkable: none');
  // the role route (no analysis)
  const role = layout([{ ...plain, role: 'overwatch' }, { ...roof('B_g', [[4000, 0], [7200, 2400]]), role: 'garrison' }]);
  assert.ok(planFiringGaps(role, null).every((g) => g.building === 'B_p'));
  assert.ok(planFiringGaps(role, null).length > 0);
  assert.deepEqual(planFiringGaps(role, null, { ...FIRING_GAPS, roles: [] }), []);
  assert.deepEqual(planFiringGaps(role, null, { ...FIRING_GAPS, enabled: false }), [], 'enabled false: nothing planned');
});

test('planFiringGaps: no gap near a ladder top / an external stair top arriving on that parapet', () => {
  const ladder: GenLadder = { id: 'LAD_1', pos: [1990, 3], yaw: 180, rungs: 8, from: 'ground', to: 'roof:B_a', level_from: 0, level_to: 2, plinth_cm: 8.8, foot: [2000, 0], top: [1700, 110] };
  const l = layout([roof('B_a', FP)], [ladder]);
  const an = analysisOf([{ building: 'B_a', spots: [[1700, 60]] }]);
  // the spot stands right over the ladder top: its gap is skipped; extras keep 60 cm + half a gap clear of the top's projection (1700 on the W side)
  const gaps = planFiringGaps(l, an);
  const w = gaps.filter((g) => g.parapet === 'B_a_parapet_W');
  assert.ok(w.every((g) => Math.abs(g.center[0] - 1700) - g.width_cm / 2 >= FIRING_GAPS.ladder_clear_cm), JSON.stringify(w.map((g) => g.center)));
  assert.ok(!gaps.some((g) => g.stand[0] === 1700 && g.stand[1] === 60));
  // without the ladder the spot gap is there
  assert.ok(planFiringGaps(layout([roof('B_a', FP)]), an).some((g) => g.stand[0] === 1700 && g.stand[1] === 60));
  // an external stair arriving on the roof counts like a ladder
  const stair = { id: 'ST_1', pos: [0, 0], from: 0, to: 2, kind: 'external', mid: [100, 0], top: [1700, 110] } as unknown as GenBuilding['stairs'][number];
  const ws = planFiringGaps(layout([roof('B_a', FP, { stairs: [stair] })]), an);
  assert.ok(!ws.some((g) => g.stand[0] === 1700 && g.stand[1] === 60));
});

test('planFiringGaps on a ROTATED roof: gaps follow the turned parapets (centres on the wall, outward away from the roof, stand 60 cm inside)', () => {
  const deg = 30, c: Vec2 = [1600, 1200], r = (deg * Math.PI) / 180;
  const rot = (p: Vec2): Vec2 => [c[0] + (p[0] - c[0]) * Math.cos(r) - (p[1] - c[1]) * Math.sin(r), c[1] + (p[0] - c[0]) * Math.sin(r) + (p[1] - c[1]) * Math.cos(r)];
  const b = roof('B_r', FP, { rotation: deg });
  b.parapets = parapets('B_r', FP).map((p) => ({ ...p, start: rot(p.start), end: rot(p.end) }));
  const gaps = planFiringGaps(layout([b]), analysisOf([{ building: 'B_r', spots: [] }]));
  assert.ok(gaps.length >= 4);
  for (const g of gaps) {
    const p = b.parapets.find((x) => x.wall_id === g.parapet)!;
    const dx = p.end[0] - p.start[0], dy = p.end[1] - p.start[1], len = Math.hypot(dx, dy);
    const off = ((g.center[0] - p.start[0]) * -dy + (g.center[1] - p.start[1]) * dx) / len;
    assert.ok(Math.abs(off) < 0.2, `${g.id} lies on its parapet line (off ${off})`);
    assert.ok(Math.hypot(g.outward[0], g.outward[1]) > 0.999 && Math.abs(g.outward[0] * dx + g.outward[1] * dy) / len < 0.001, 'outward is the unit normal');
    assert.ok((g.center[0] - c[0]) * g.outward[0] + (g.center[1] - c[1]) * g.outward[1] > 0, 'outward points away from the roof centre');
    assert.ok(Math.abs(Math.hypot(g.center[0] - g.stand[0], g.center[1] - g.stand[1]) - FIRING_GAPS.stand_inset_cm) < 0.2);
  }
});

// ---- the layout, the hash, the tactical file ---------------------------------------------------------------------------------------

test('withFiringGaps: sets the TOP-LEVEL list (buildings and the geometry hash untouched), leaves the key out when there is none, validates', () => {
  const l = layout([roof('B_a', FP)]);
  const an = analysisOf([{ building: 'B_a', spots: [[1700, 60]] }]);
  const w = withFiringGaps(l, an);
  assert.ok(w.firing_gaps && w.firing_gaps.length > 1);
  assert.equal(w.buildings, l.buildings, 'the buildings are the same objects: the gaps live outside them');
  assert.equal(geometryHash(w), geometryHash(l), 'a derived list never makes the analysis stale');
  assert.equal(l.firing_gaps, undefined, 'the input is not changed');
  assert.deepEqual(validateGenLayout(w).filter((e) => e.includes('firing_gap')), []);
  // disabled / none planned: the key is absent (the layout stays byte-identical)
  assert.ok(!('firing_gaps' in withFiringGaps(l, an, { ...FIRING_GAPS, enabled: false })));
  assert.ok(!('firing_gaps' in withFiringGaps(l, null)));
  assert.ok(!('firing_gaps' in withFiringGaps(w, null)), 'a stale list is dropped when nothing is planned now');
  // idempotent
  assert.deepEqual(withFiringGaps(w, an), w);
  // the schema validator
  const bad = (g: unknown) => validateGenLayout({ ...l, firing_gaps: g as FiringGap[] }).filter((e) => e.includes('firing_gap'));
  assert.ok(bad({}).length > 0);
  assert.ok(bad([{ ...w.firing_gaps![0], building: 'B_zz' }]).some((e) => e.includes('unknown building')));
  assert.ok(bad([{ ...w.firing_gaps![0], parapet: 'nope' }]).some((e) => e.includes('no parapet')));
  assert.ok(bad([w.firing_gaps![0], w.firing_gaps![0]]).some((e) => e.includes('duplicate')));
  assert.ok(bad([{ ...w.firing_gaps![0], sill_cm: 0, center: 'x' }]).length >= 2);
});

test('tactical map: spots get their gap id, the top-level firing_gaps list is written only for a layout with gaps (no gaps = no key)', () => {
  const l = layout([roof('B_a', FP)], [{ id: 'LAD_1', pos: [10, 3000], yaw: 180, rungs: 8, from: 'ground', to: 'roof:B_a', level_from: 0, level_to: 2, plinth_cm: 8.8, foot: [0, 3000], top: [100, 3000] }]);
  const spot: Vec2 = [1700, 60];
  const an = { ...analysisOf([{ building: 'B_a', spots: [spot] }]), version: 2, geometry_hash: geometryHash(l), conditions: { weather: 'clear', time_of_day: 'day' },
    thresholds: {}, chokepoints: [], dropAmbushSpots: [], windowsCovering: [], hidingSpots: [], stats: { lines: 0, length_m: 0, samples: 0, places_found: 0 } } as unknown as LayoutAnalysis;
  const lg = { ...withFiringGaps({ ...l, analysis: an }, an), analysis: an };
  const f = tacticalMapFor(lg, 'M', 'g');
  const gap = lg.firing_gaps![0];
  assert.equal(f.overwatch_roofs[0].spots[0].gap, gap.id);
  assert.equal(f.firing_gaps!.length, lg.firing_gaps!.length);
  const tg = f.firing_gaps![0];
  assert.deepEqual([tg.id, tg.building, tg.layer, tg.parapet, tg.width_cm, tg.sill_cm, tg.yaw], [gap.id, 'B_a', 'roof:B_a', 'B_a_parapet_W', 90, 60, -90]);
  assert.deepEqual([tg.pos[2], tg.stand[2]], [f.overwatch_roofs[0].z, f.overwatch_roofs[0].z], 'on the roof floor');
  assert.deepEqual([tg.pos[0], tg.pos[1], tg.stand[0], tg.stand[1]], [1700, 0, 1700, 60]);
  assert.ok(formatTacticalMap(f).includes('"firing_gaps": ['));
  assert.deepEqual(JSON.parse(formatTacticalMap(f)).firing_gaps, JSON.parse(JSON.stringify(f.firing_gaps)));
  // a layout without gaps: no key, no `gap` on the spot - the file is what it was before this feature
  const plain = tacticalMapFor({ ...l, analysis: an }, 'M', 'g');
  assert.ok(!('firing_gaps' in plain) && !formatTacticalMap(plain).includes('firing_gaps'));
  assert.ok(!('gap' in plain.overwatch_roofs[0].spots[0]));
});
