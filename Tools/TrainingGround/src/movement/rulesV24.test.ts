// Training Ground / movement: v2.4 tests - the wall END (FORMATION_CONTRACT.md 14). User bug report 2026-10-26 (the editor,
// Codex.Formation.Debug): three operatives crouched at the end face of a thin free-standing wall left large uncovered zones on
// both sides of the end, along the wall's two faces - "this must be stabilised so they cover the large zones".
import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { Segment, Vec2 } from './geometry.ts';
import { angleDiff, closestOnSegment, dist, distToWall, normYaw, sub, yawOf } from './geometry.ts';
import { configValues } from './config.ts';
import { readConfigFile, readLayout } from './cli/paths.ts';
import { runSeries } from './bot.ts';
import { LIMITS } from './checks.ts';
import type { WallSlot } from './rules.ts';
import { FormationWallRules, openArcCoverage, wallRoom } from './rules.ts';
import { simulate } from './sim.ts';

const C = configValues(readConfigFile());
const FOV = C['sight.fov'], PROBE = C['wall_end.probe_dist'];

/** A thin free-standing wall (15 m, 40 cm) along -X ending at the origin; the leader's wall slot 45 cm off its end face. */
const thin: Segment = { a: [-1500, 0], b: [0, 0], thickness: 40 };
const layoutAt = (wall: Segment, click: Vec2, n: number): WallSlot[] => {
  const room = wallRoom(wall, click);
  return FormationWallRules.Layout(wall, click, n, room.roomLeft, room.roomRight, C);
};
const cover = (walls: Segment[], slots: WallSlot[]) => openArcCoverage(walls, slots.map((s) => ({ pos: s.pos, yaw: s.yaw })), FOV, PROBE).pct;
/** Check 1 (contract 4): a wall surface within 200 cm whose closest point lies within 45 deg of his look. */
const stares = (walls: Segment[], s: WallSlot) => walls.some((w) => distToWall(s.pos, w) <= LIMITS.stareDist
  && angleDiff(s.yaw, yawOf(sub(closestOnSegment(s.pos, w.a, w.b).point, s.pos))) <= LIMITS.stareAngle);

test('v2.4: the bug - at the end face of a thin wall the face looks (8 / 6 / 4) hold only ~70 % of the open arc round the end', () => {
  for (const n of [3, 4]) {
    const slots = layoutAt(thin, [65, 0], n);
    assert.equal(slots[0].yaw, 0, 'K looks along the wall axis, away from the end (the outward normal of the end face)');
    const pct = cover([thin], slots);
    assert.ok(pct < 75, `n${n}: ${pct} %`);
  }
});

test('v2.4: EndLooks spreads the looks over the whole open arc round the end - both flanks along the faces, K kept, nobody into the wall', () => {
  for (const wall of [thin, { ...thin, thickness: 20 }, { a: [-300, 0], b: [0, 0], thickness: 40 }] as Segment[]) {
    for (const n of [3, 4]) {
      const click: Vec2 = [(wall.thickness ?? 0) / 2 + C['wall.face_offset'], 0];
      const slots = layoutAt(wall, click, n);
      const r = FormationWallRules.EndLooks([wall], slots, C);
      assert.ok(r.spread, `n${n} spread`);
      assert.ok(r.basePct < C['wall_end.cover_pct'] && r.pct >= LIMITS.wallArcPct, `n${n}: ${r.basePct} -> ${r.pct}`);
      assert.equal(r.slots[0].yaw, slots[0].yaw, 'K keeps the user\'s 6 o\'clock');
      r.slots.forEach((s, i) => assert.deepEqual(s.pos, slots[i].pos, 'positions unchanged'));
      for (const s of r.slots) assert.ok(!stares([wall], s), `${s.role} ${s.yaw} stares into the wall`);
      // Both flanks: somebody holds the directions back along each face (1.5 m off it, 5 m behind the end).
      for (const side of [1, -1]) {
        const target: Vec2 = [-500, side * 150];
        assert.ok(r.slots.some((s) => angleDiff(s.yaw, yawOf(sub(target, s.pos))) <= FOV / 2 && dist(s.pos, target) > 0
          && Math.sign(s.pos[1]) === side), `n${n}: the ${side > 0 ? 'right' : 'left'} flank along the face`);
      }
    }
  }
});

test('v2.4: a wall FACE keeps the user\'s layout looks (L 8 / K 6 / P 4, the 4th along the wall) - EndLooks changes nothing there', () => {
  const face: Segment = { a: [0, -1500], b: [0, 1500], thickness: 40 };
  const corner: Segment[] = [face, { a: [0, -150], b: [1500, -150], thickness: 40 }];
  for (const n of [2, 3, 4]) {
    for (const walls of [[face], corner]) {
      const slots = layoutAt(face, [65, 0], n);
      const r = FormationWallRules.EndLooks(walls, slots, C);
      assert.equal(r.spread, false);
      assert.deepEqual(r.slots, slots);
    }
  }
  // Lab lanes A and F (the dead ends): unchanged.
  const lab = readLayout('formation_lab_01');
  for (const id of ['W_A_1', 'W_F_15']) {
    const w = lab.walls.find((x) => x.id === id)!;
    const seg: Segment = { a: w.a, b: w.b, thickness: w.thickness };
    const click: Vec2 = id === 'W_A_1' ? [4435, -4500] : [4435, 4500];
    for (const n of [3, 4]) assert.equal(FormationWallRules.EndLooks([seg], layoutAt(seg, click, n), C).spread, false, `${id} n${n}`);
  }
});

test('v2.4: the pair keeps its wall looks (K 7:30 / partner 4:30, a user decision) - EndLooks needs 3+', () => {
  const slots = layoutAt(thin, [65, 0], 2);
  const r = FormationWallRules.EndLooks([thin], slots, C);
  assert.equal(r.spread, false);
  assert.deepEqual(r.slots, slots);
});

test('v2.4: EndLooks is pure and rotation invariant (the same looks relative to the end face)', () => {
  const rot = (p: Vec2, yaw: number, off: Vec2): Vec2 => {
    const c = Math.cos((yaw * Math.PI) / 180), s = Math.sin((yaw * Math.PI) / 180);
    return [off[0] + p[0] * c - p[1] * s, off[1] + p[0] * s + p[1] * c];
  };
  const ref = FormationWallRules.EndLooks([thin], layoutAt(thin, [65, 0], 3), C).slots.map((s) => s.yaw);
  for (const yaw of [37, -120, 180]) {
    const w: Segment = { a: rot(thin.a, yaw, [300, -200]), b: rot(thin.b, yaw, [300, -200]), thickness: 40 };
    const got = FormationWallRules.EndLooks([w], layoutAt(w, rot([65, 0], yaw, [300, -200]), 3), C).slots.map((s) => normYaw(s.yaw - yaw));
    got.forEach((g, i) => assert.ok(angleDiff(g, ref[i]) <= 1, `yaw ${yaw}: ${g} vs ${ref[i]}`));
  }
});

test('v2.4: openArcCoverage - an open field is all open; a cone covers its own points; behind a wall nothing is open', () => {
  const one = openArcCoverage([], [{ pos: [0, 0], yaw: 0 }], FOV, PROBE);
  assert.equal(one.open, 180);
  assert.ok(Math.abs(one.pct - (100 * 71) / 180) < 1e-9, `${one.pct}`); // -70..70 every 2 deg from -180: 71 points
  const all = openArcCoverage([], [0, 120, -120].map((yaw) => ({ pos: [0, 0] as Vec2, yaw })), FOV, PROBE);
  assert.equal(all.pct, 100);
  const boxed: Segment[] = [{ a: [-100, -100], b: [100, -100] }, { a: [100, -100], b: [100, 100] }, { a: [100, 100], b: [-100, 100] }, { a: [-100, 100], b: [-100, -100] }];
  assert.deepEqual(openArcCoverage(boxed, [{ pos: [0, 0], yaw: 0 }], FOV, PROBE), { open: 0, covered: 0, pct: 100 });
});

test('v2.4: the lab route R_G_end (W_G_20\'s free end face) - check 13 green for 3 and 4 men, the looks steady while they hold', () => {
  const layout = readLayout('formation_lab_01');
  const s = runSeries({ layout, config: C, members: [3, 4], seeds: 2, seedBase: 1, routes: ['R_G_end'] });
  const arc = s.summary.checks.find((c) => c.id === 13)!;
  assert.equal(arc.fail, 0, arc.worst);
  assert.equal(arc.pass, 4);
  for (const id of [1, 4, 5, 7]) assert.equal(s.summary.checks.find((c) => c.id === id)!.fail, 0, `check ${id}`);
  // Stable: standing in the layout, nobody's look moves by more than 2 deg once he has turned to it (no flicker / reshuffle).
  const route = layout.routes.find((r) => r.id === 'R_G_end')!;
  const seen = new Map<string, { place: string; yaw: number }>();
  let frames = 0;
  simulate({ layout, route, memberCount: 3, config: C, seed: 1, haltSeconds: 6, onFrame: (f) => {
    if (f.mode !== 'wall' || !f.members.every((m) => dist(m.pos, m.slot) <= 30)) return;
    frames++;
    for (const m of f.members) {
      const prev = seen.get(m.role);
      if (prev && frames > 30) {
        assert.equal(m.place, prev.place, `${m.role} changed place`);
        assert.ok(angleDiff(m.yaw, prev.yaw) <= 2, `${m.role} look ${prev.yaw} -> ${m.yaw} at t=${f.t}`);
      }
      if (!prev || frames <= 30) seen.set(m.role, { place: m.place, yaw: m.yaw });
    }
  } });
  assert.ok(frames > 60, `${frames} standing frames`);
});

test('v2.4: looks a higher rule fixed (the corner stack) stay; only the others are spread round them', () => {
  const slots = layoutAt(thin, [65, 0], 4);
  const r = FormationWallRules.EndLooks([thin], slots, C, [slots[1].role]);
  assert.ok(r.spread && r.pct > r.basePct);
  assert.equal(r.slots[0].yaw, slots[0].yaw);
  assert.equal(r.slots[1].yaw, slots[1].yaw);
  assert.equal(FormationWallRules.EndLooks([thin], slots, C, slots.slice(1).map((s) => s.role)).spread, false);
});
