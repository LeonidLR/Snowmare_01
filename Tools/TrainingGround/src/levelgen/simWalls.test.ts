// Training Ground / level generator v2 - designer bug 2026-11-16 (evening): after a FULL region re-roll («Полностью (форма и
// расстановка)») «Прогнать отряд (симулятор)» showed the squad walking THROUGH obstacles. Root cause: the simulator's walkability
// (the ground NavGrid) was built from the layout's walls only - the covers (wrecks, jersey barriers, sandbags, crates, barrels,
// silos, boilers) were not obstacles to it, so the A* paths, the followers' steps and the separation ran straight through them.
// The full re-roll adds a lot of template covers on and next to the streets, which is where the designer saw it.
// These tests walk a re-rolled Depot and check every sampled position of the playback AND every straight piece between two
// consecutive samples (what the page draws) against the walls, the covers and the solid buildings. `npm test`.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import type { Segment, Vec2 } from '../movement/geometry.ts';
import { dirOf, segSegDist } from '../movement/geometry.ts';
import { configValues } from '../movement/config.ts';
import { wallSegments } from '../movement/layout.ts';
import { evaluateLayout } from './evaluate.ts';
import { generateLayout } from './generate.ts';
import { createTrackRecorder, positionsAt, trailAt } from './playback.ts';
import type { Profile } from './profile.ts';
import { regenerateRegion } from './regionRegen.ts';
import type { GenCover, GenLayout } from './schema.ts';
import { rect, toFormationLayout } from './schema.ts';
import { AGENT_RADIUS } from '../movement/nav.ts';
import { buildSimNav, coverObstacles, prepareSimLeg, SIM_NAV, snapToWalkable } from './simNav.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
const config = configValues(JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/FormationLab/formation_config.json'), 'utf8')));
const base = generateLayout({ profile, seed: 1 });
/** The north-west quarter of the Depot (6 blocks; the zone the UX round's tests re-roll). */
const ZONE_NW: Vec2[] = [[500, -9000], [6200, -9000], [6200, 0], [500, 0]];

/** The cover's footprint corners (its yawed rectangle: size[0] along the yaw, size[1] across). */
function coverCorners(c: GenCover, shrink = 0): Vec2[] {
  const hx = Math.max(0, c.size[0] / 2 - shrink), hy = Math.max(0, c.size[1] / 2 - shrink), d = dirOf(c.yaw), r: Vec2 = [-d[1], d[0]];
  const k = (sx: number, sy: number): Vec2 => [c.pos[0] + d[0] * hx * sx + r[0] * hy * sy, c.pos[1] + d[1] * hx * sx + r[1] * hy * sy];
  return [k(1, 1), k(-1, 1), k(-1, -1), k(1, -1)];
}
function inCover(c: GenCover, p: Vec2, shrink: number): boolean {
  const d = dirOf(c.yaw), v: Vec2 = [p[0] - c.pos[0], p[1] - c.pos[1]];
  return Math.abs(v[0] * d[0] + v[1] * d[1]) < c.size[0] / 2 - shrink && Math.abs(-v[0] * d[1] + v[1] * d[0]) < c.size[1] / 2 - shrink;
}

interface Breach { t: number; member: number; what: string }

/**
 * Every playback sample (each member) and every straight piece between two consecutive samples of the same leg against: the ground
 * walls (their thickness), the covers (their rectangle, `shrink` cm inside its edges: grazing a corner is not walking through it) and
 * the footprints of the buildings nobody can enter.
 */
function breaches(l: GenLayout, shrink = 10): Breach[] {
  const ev = evaluateLayout({ layout: l, profile, config, track: true });
  assert.ok(ev.route.completable, 'the mandatory route is completable');
  const tr = ev.track!;
  assert.ok(tr && tr.frames.length > 100, 'a track');
  const walls = wallSegments(toFormationLayout(l, 'ground'));
  const covers = l.covers.map((c) => ({ c, edges: coverCorners(c, shrink).map((a, i, p): Segment => ({ a, b: p[(i + 1) % 4], thickness: 0 })) }));
  const solid = l.buildings.filter((b) => !b.enterable);
  const out: Breach[] = [];
  for (let i = 0; i < tr.frames.length; i++) {
    const f = tr.frames[i], prev = tr.frames[i - 1];
    f.pos.forEach((p, m) => {
      for (const { c } of covers) if (inCover(c, p, shrink)) out.push({ t: f.t, member: m, what: `sample in cover ${c.id} (${c.kind})` });
      for (const b of solid) if (rect.contains(b.footprint, p, -shrink)) out.push({ t: f.t, member: m, what: `sample in building ${b.id}` });
      if (!prev || prev.leg !== f.leg) return;
      const a = prev.pos[m];
      for (const w of walls) if (segSegDist(a, p, w.a, w.b) < (w.thickness ?? 0) / 2) out.push({ t: f.t, member: m, what: `step through wall ${(w as { id?: string }).id ?? '?'}` });
      for (const { c, edges } of covers) if (edges.some((e) => segSegDist(a, p, e.a, e.b) === 0)) out.push({ t: f.t, member: m, what: `step through cover ${c.id} (${c.kind})` });
    });
  }
  return out;
}

test('sim walls: after a FULL re-roll the squad playback goes round every wall, cover and solid building (no sample, no step through)', () => {
  const r = regenerateRegion({ layout: base, zone: ZONE_NW, seed: 303, profile, blocks: 'full', chaos: 1 });
  assert.equal(r.ok, true, r.message);
  assert.ok(r.layout.covers.length > 50, 'the re-rolled map has covers to walk round');
  const bad = breaches(r.layout);
  assert.deepEqual(bad.slice(0, 8), [], `${bad.length} breaches, e.g. ${bad.slice(0, 3).map((b) => `t=${b.t} m${b.member} ${b.what}`).join('; ')}`);
  // the whole map re-planned (every block, chaos 0): the same
  const b = base.bounds;
  const all = regenerateRegion({ layout: base, zone: [[b.min[0], b.min[1]], [b.max[0], b.min[1]], [b.max[0], b.max[1]], [b.min[0], b.max[1]]], seed: 7, profile, blocks: 'full', chaos: 0 });
  assert.equal(all.ok, true, all.message);
  const bad2 = breaches(all.layout);
  assert.deepEqual(bad2.slice(0, 8), [], `whole map: ${bad2.length} breaches`);
});

test('sim walls: a cover is solid for the simulator (edges + spine hold its whole rectangle); snapping a point out of a cover', () => {
  // every point of the rectangle (a grid incl. the corners) lies within the agent radius of the cover's obstacle segments
  const solidAll = (c: GenCover) => {
    const segs = coverObstacles(c), d = dirOf(c.yaw), r: Vec2 = [-d[1], d[0]];
    for (let u = -1; u <= 1.0001; u += 0.1) for (let v = -1; v <= 1.0001; v += 0.1) {
      const p: Vec2 = [c.pos[0] + d[0] * u * c.size[0] / 2 + r[0] * v * c.size[1] / 2, c.pos[1] + d[1] * u * c.size[0] / 2 + r[1] * v * c.size[1] / 2];
      const reach = Math.min(...segs.map((w) => segSegDist(p, p, w.a, w.b) - (w.thickness ?? 0) / 2));
      assert.ok(reach < AGENT_RADIUS, `${c.kind} ${c.size.join('x')}: (${u.toFixed(1)}, ${v.toFixed(1)}) is ${reach.toFixed(1)} cm off the obstacle`);
    }
  };
  const wreck: GenCover = { id: 'cv', pos: [1000, 2000], yaw: 30, size: [420, 180, 140], kind: 'wreck' };
  solidAll(wreck);
  solidAll({ id: 'cr', pos: [0, 0], yaw: 0, size: [120, 120, 85], kind: 'crate' });
  solidAll({ id: 'jy', pos: [0, 0], yaw: 90, size: [60, 300, 95], kind: 'jersey' });
  // the sim nav blocks the cover; a route point inside it is moved to the nearest walkable spot, within snap_max_m
  const inside: Vec2 = [base.spawn.pos[0] + 1500, base.spawn.pos[1]];
  const l: GenLayout = { ...base, covers: [...base.covers, { ...wreck, id: 'cv_test', pos: inside }] };
  const nav = buildSimNav(l);
  assert.equal(nav.walkable(inside), false, 'the cover is an obstacle');
  const s = snapToWalkable(nav, inside)!;
  assert.ok(s && nav.walkable(s), 'snapped onto the walkable area');
  assert.ok(Math.hypot(s[0] - inside[0], s[1] - inside[1]) <= SIM_NAV.snap_max_m * 100, 'within snap_max_m');
  assert.deepEqual(snapToWalkable(nav, base.spawn.pos), base.spawn.pos, 'a walkable point stays');
  const leg = prepareSimLeg(nav, [base.spawn.pos, inside]);
  assert.equal(leg.snapped, 1);
  assert.ok(leg.points.every((p) => nav.walkable(p)), 'every leg point walkable');
  assert.ok(nav.path(leg.points[0], leg.points[1]), 'and the A* joins them');
});

test('sim walls: the playback trail of every member (the followers faint trails on the plan), within the leg', () => {
  const rec = createTrackRecorder(5);
  rec.beginLeg('R_main');
  for (let k = 0; k <= 30; k++) rec.frame({ t: k / 10, formation: 'column', members: [{ pos: [k * 10, 0] }, { pos: [k * 10, 100 + k] }] });
  rec.endLeg(3);
  const tr = rec.track();
  const lead = trailAt(tr, 2, 1), follower = trailAt(tr, 2, 1, 1);
  assert.equal(lead.length, follower.length);
  assert.ok(lead.every((p) => p[1] === 0), 'member 0 = the leader');
  assert.ok(follower.every((p) => p[1] >= 100), 'member 1 = his own positions');
  assert.deepEqual(follower.at(-1), positionsAt(tr, 2)!.members[1]);
  assert.deepEqual(trailAt(tr, 2, 1, 7), [], 'no such member: no trail');
});
