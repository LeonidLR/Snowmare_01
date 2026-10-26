// Training Ground / movement: the bot's checks (FORMATION_CONTRACT.md 4) over simulator frames, per run and per series.
// Thresholds not fixed by the contract are named constants here and printed in the report.

import type { FormationConfig } from './config.ts';
import { cfg } from './config.ts';
import type { Vec2 } from './geometry.ts';
import type { Segment } from './geometry.ts';
import { add, angleDiff, closestOnSegment, dirOf, dist, distToWall, round, scale, segmentBlocked, sub, yawOf } from './geometry.ts';
import type { Formation, Layout, LayoutRoute } from './layout.ts';
import { wallSegments, zoneAt } from './layout.ts';
import type { WallSide } from './rules.ts';
import { CORNER_GRAZE, cornerAhead, FormationGuardRules, openArcCoverage, wallSideOf } from './rules.ts';
import type { Frame } from './sim.ts';
import { SIM_DT } from './sim.ts';

export const LIMITS = {
  stareDist: 200, // cm (contract)
  stareAngle: 45, // deg (contract)
  stareMinSeconds: 0.3, // a glance while turning is not a stare (looking where one walks never counts: approaching a wall)
  rearCoverPct: 90, // % of patrol time, 3 and 4 members (contract; v2.2: the triad has a rear man too)
  rearMargin: 30, // the 5-7 o'clock sector (60 deg) must sit inside a cone: centre within fov / 2 - 30
  flickerPer100m: 1, // unexpected formation changes per 100 m (contract)
  flickerRevert: 2, // a change undone within 2 x classify.hold_distance of the leader's path is a flicker
  expectLookAhead: 500, // cm: a change into the formation expected just ahead counts as expected
  expectSettleCm: 100, // cm of path after entering a zone (plus classify.hold_distance for an open-ground expectation) before matching counts; standing: 0.5 s
  expectPct: 80, // % of settled time in the zone with the expected formation
  spacingMinSeconds: 0.5,
  paceBehind: 150, // cm (contract), checked on the 95th percentile
  stuckDist: 600,
  stuckSeconds: 3,
  splitSeconds: 0.3, // v2.1 check 7: a wall between the leader and a member's slot / position (or chain neighbours) this long
  cornerPeek: 200, // cm, check 9: the hidden area's point behind the far face at the wall end
  cornerMargin: 10, // deg, check 9: the corner / hidden point within fov / 2 - margin of a member's facing
  coverPct: 90, // % of patrol time the largest open uncovered arc stays within coverage.max_gap_deg (check 10, 3+ members)
  coverStep: 2, // deg, the arc sampling of check 10
  slideOrderTol: 20, // cm, check 11: two neighbours may be this much out of order along the wall
  slideOpenPct: 90, // %, check 11: the open half (the wall's outward normal +-90) covered by the cones
  wallArcPct: 95, // %, check 13 (v2.4): the open probe ring round the standing wall layout held by the cones (wall_end.probe_dist)
  wallArcSettleCm: 50, // cm, check 13: everyone this close to his wall slot - the layout stands
  wallArcTimePct: 90, // %, check 13: of the standing time with the ring held >= wallArcPct
  approachGrace: 0.5, // s, check 14 (v2.5): the first moments of the covered approach (the looks turn) are not judged
  approachFlankPct: 90, // %, check 14: of the approach time a flank (3 men) / each flank (4) inside a cone (+-rearMargin inside it)
} as const;

export interface Worst { t: number; what: string; value: number }

/** Diamond and wedge are one formation (the wedge is the diamond with 3); expectations compare them as equal. */
export const sameFormation = (a: Formation, b: Formation): boolean =>
  a === b || ((a === 'diamond' || a === 'wedge') && (b === 'diamond' || b === 'wedge'));

export interface RunMetrics {
  route: string;
  members: number;
  seed: number;
  duration: number;
  distance: number;
  wallStare: { events: number; worst: Worst | null };
  rearCover: { walkTime: number; pct: number } | null;
  flicker: { changes: number; unexpected: number; per100m: number; worst: Worst | null };
  expect: { zone: string; formation: Formation; settled: number; pct: number }[];
  slots: { fallbackTicks: number; invalidTicks: number; spacingEvents: number; worst: Worst | null };
  pace: { p95: number; max: number; worst: Worst | null } | null;
  stuck: { events: number; worst: Worst | null };
  /** v2.1 check 7: squad split by a wall (events = splits lasting >= LIMITS.splitSeconds). */
  split: { events: number; worst: Worst | null };
  /** v2.1 check 8: the side the leader really passed each expected wall on. */
  bypass: { wall: string; want: WallSide; got: WallSide }[];
  /** v2.1 check 9: free wall ends passed on patrol, and those nobody looked round in time. */
  corner: { ends: number; missed: number; worst: Worst | null };
  /** v2.2 check 10: patrol time with the largest open uncovered arc <= coverage.max_gap_deg (3+ members). */
  cover: { walkTime: number; okTime: number; pct: number; worst: Worst | null } | null;
  /** v2.2 check 11: wall slides - time, order broken (>= 0.3 s), time with the open half covered >= LIMITS.slideOpenPct. */
  slide: { time: number; orderBreaks: number; okTime: number; pct: number; worst: Worst | null };
  /**
   * v2.4 check 13 (user bug report 2026-10-26, a wall END): the standing wall layout (3+ members, everyone within
   * LIMITS.wallArcSettleCm of his slot) - time, time with the open probe ring held >= LIMITS.wallArcPct, the lowest share seen.
   */
  wallArc: { time: number; okTime: number; pct: number; worst: Worst | null } | null;
  /**
   * v2.5 check 14 (user rule 2026-10-26, the covered approach to the wall; 2+ members): the approach time after LIMITS.approachGrace, the
   * time nobody's cone held the rear (the wall's outward normal +-rearMargin), the time some / the left / the right flank (normal -/+ 90)
   * was held; flankPct / leftPct / rightPct of the approach time.
   */
  approach: { time: number; rearMiss: number; flankPct: number; leftPct: number; rightPct: number; worst: Worst | null } | null;
}

/** Collects one run's frames into RunMetrics. */
export class RunCollector {
  private readonly layout: Layout;
  private readonly route: LayoutRoute;
  private readonly n: number;
  private readonly c: FormationConfig;
  private readonly walls;
  private stareTimer = new Map<string, number>();
  private stareCounted = new Set<string>();
  private m: RunMetrics;
  private prevFormation: Formation | null = null;
  private lastChange: { t: number; d: number; from: Formation; what: string } | null = null;
  private haltStart: number | null = null;
  private zoneEnter = new Map<string, number>();
  private expectAcc = new Map<string, { settled: number; match: number }>();
  private spacingTimer = new Map<string, number>();
  private stuckTimer = new Map<string, number>();
  private behind: number[] = [];
  private rearWatched = 0;
  private readonly segs: Segment[];
  private splitTimer = 0;
  private splitCounted = false;
  private trail: Vec2[] = [];
  private cornerPrev = new Map<string, number>();
  private slideBreak = 0;
  private slideCounted = false;
  private approachStart: number | null = null;
  private approachFlank = { any: 0, left: 0, right: 0 };

  constructor(layout: Layout, route: LayoutRoute, memberCount: number, seed: number, config: FormationConfig) {
    this.layout = layout; this.route = route; this.n = memberCount; this.c = config;
    this.segs = wallSegments(layout);
    this.walls = layout.walls.map((w, i) => ({ id: w.id, seg: this.segs[i] }));
    this.m = {
      route: route.id, members: memberCount, seed, duration: 0, distance: 0,
      wallStare: { events: 0, worst: null },
      rearCover: memberCount >= 3 ? { walkTime: 0, pct: 100 } : null,
      flicker: { changes: 0, unexpected: 0, per100m: 0, worst: null },
      expect: [],
      slots: { fallbackTicks: 0, invalidTicks: 0, spacingEvents: 0, worst: null },
      pace: null,
      stuck: { events: 0, worst: null },
      split: { events: 0, worst: null },
      bypass: [],
      corner: { ends: 0, missed: 0, worst: null },
      cover: memberCount >= 3 ? { walkTime: 0, okTime: 0, pct: 100, worst: null } : null,
      slide: { time: 0, orderBreaks: 0, okTime: 0, pct: 100, worst: null },
      wallArc: memberCount >= 3 ? { time: 0, okTime: 0, pct: 100, worst: null } : null,
      approach: memberCount >= 2 ? { time: 0, rearMiss: 0, flankPct: 100, leftPct: 100, rightPct: 100, worst: null } : null,
    };
  }

  onFrame(f: Frame): void {
    this.m.duration = f.t;
    this.m.distance = f.leaderDistance;
    const leader = f.members[0];

    // 1. wall stare
    for (const mem of f.members) {
      let staring = '';
      // Looking where he walks (facing within stareAngle of his travel) is approaching, not staring (a dead end ahead).
      // v2.1: the corner man looks at the wall end on purpose (slicing the pie), that is not a stare either.
      const approaching = (mem.moveYaw !== null && angleDiff(mem.yaw, mem.moveYaw) <= LIMITS.stareAngle) || mem.cornerWatch;
      for (const w of approaching ? [] : this.walls) {
        if (distToWall(mem.pos, w.seg) > LIMITS.stareDist) continue;
        const towards = yawOf(sub(closestOnSegment(mem.pos, w.seg.a, w.seg.b).point, mem.pos));
        if (angleDiff(mem.yaw, towards) <= LIMITS.stareAngle) { staring = w.id; break; }
      }
      const key = mem.role;
      if (staring) {
        const s = (this.stareTimer.get(key) ?? 0) + SIM_DT;
        this.stareTimer.set(key, s);
        if (s >= LIMITS.stareMinSeconds && !this.stareCounted.has(key)) { this.stareCounted.add(key); this.m.wallStare.events++; }
        if (s >= LIMITS.stareMinSeconds && (!this.m.wallStare.worst || s > this.m.wallStare.worst.value)) {
          this.m.wallStare.worst = { t: round(f.t, 1), what: `${mem.role} at ${staring}`, value: round(s, 2) };
        }
      } else { this.stareTimer.set(key, 0); this.stareCounted.delete(key); }
    }

    // 2. rear cover (patrol only)
    if (this.m.rearCover && f.mode === 'walk') {
      this.m.rearCover.walkTime += SIM_DT;
      const rear = f.heading + 180, lim = cfg(this.c, 'sight.fov') / 2 - LIMITS.rearMargin;
      if (f.members.some((mem) => angleDiff(mem.yaw, rear) <= lim)) this.rearWatched += SIM_DT;
    }

    // 3. flicker + 4. expected formation per zone
    if (f.mode === 'halt' || f.mode === 'wall') this.haltStart ??= f.t; else this.haltStart = null;
    const zone = zoneAt(this.layout, leader.pos);
    // v2.1: 3 and 4 judge the REQUESTED formation (the terrain's / the menu's decision); the wall guard's fallbacks are geometry
    // (check 7 proves they keep the squad whole; their hysteresis is the rule's).
    // A change is unexpected when it contradicts the expectation of the zone the leader is in / about to enter, or when it
    // is reverted within LIMITS.flickerRevert x hold_distance of path (a real flicker); a lasting change on open ground is fine.
    if (this.prevFormation && f.requested !== this.prevFormation) {
      this.m.flicker.changes++;
      const ahead: Vec2 = [leader.pos[0] + dirOf(f.heading)[0] * LIMITS.expectLookAhead, leader.pos[1] + dirOf(f.heading)[1] * LIMITS.expectLookAhead];
      const zones = [zone, zoneAt(this.layout, ahead)].filter(Boolean).map((z) => z!.id);
      const wants = (this.route.expect ?? []).filter((e) => zones.includes(e.zone));
      const what = `${this.prevFormation} -> ${f.requested}${zone ? ` in ${zone.id}` : ''}`;
      const reverted = this.lastChange && f.leaderDistance - this.lastChange.d < LIMITS.flickerRevert * cfg(this.c, 'classify.hold_distance') &&
        sameFormation(this.lastChange.from, f.requested);
      if (reverted) this.unexpected(f.t, `${this.lastChange!.what}, back after ${round(f.t - this.lastChange!.t, 1)} s`);
      else if (wants.length && !wants.some((e) => sameFormation(e.formation, f.requested))) this.unexpected(f.t, `${what} (zone wants ${wants[0].formation})`);
      this.lastChange = { t: f.t, d: f.leaderDistance, from: this.prevFormation, what };
    }
    this.prevFormation = f.requested;
    for (const e of this.route.expect ?? []) {
      const key = e.zone;
      if (zone?.id === key) {
        if (!this.zoneEnter.has(key)) this.zoneEnter.set(key, f.leaderDistance);
        const walkedIn = f.leaderDistance - this.zoneEnter.get(key)!;
        const halted = this.haltStart !== null && f.t - this.haltStart >= 0.5;
        // Tighter classes (echelon, column, wall) take over at once (v2); only a looser expectation waits for hold_distance.
        const looser = sameFormation(e.formation, 'diamond') || e.formation === 'blizzard';
        const settleCm = LIMITS.expectSettleCm + (looser ? cfg(this.c, 'classify.hold_distance') : 0);
        if (walkedIn >= settleCm || halted) {
          const acc = this.expectAcc.get(key) ?? { settled: 0, match: 0 };
          acc.settled += SIM_DT;
          if (sameFormation(f.requested, e.formation)) acc.match += SIM_DT;
          this.expectAcc.set(key, acc);
        }
      } else this.zoneEnter.delete(key);
    }

    // 5. slots + spacing
    if (f.members.some((mem) => mem.slotFallback)) this.m.slots.fallbackTicks++;
    for (const mem of f.members) if (mem.slotInvalid) {
      this.m.slots.invalidTicks++;
      if (!this.m.slots.worst) this.m.slots.worst = { t: round(f.t, 1), what: `${mem.role} has no walkable slot`, value: 0 };
    }
    const minSp = cfg(this.c, 'slot.min_spacing');
    for (let i = 0; i < f.members.length; i++) for (let j = i + 1; j < f.members.length; j++) {
      const a = f.members[i], b = f.members[j], key = a.role + b.role, d = dist(a.pos, b.pos);
      if (d < minSp) {
        const s = (this.spacingTimer.get(key) ?? 0) + SIM_DT;
        this.spacingTimer.set(key, s);
        if (Math.abs(s - LIMITS.spacingMinSeconds) < SIM_DT / 2) {
          this.m.slots.spacingEvents++;
          this.m.slots.worst ??= { t: round(f.t, 1), what: `${a.role}-${b.role} ${Math.round(d)} cm apart`, value: round(d, 0) };
        }
      } else this.spacingTimer.set(key, 0);
    }

    // 6. pace: the rear man behind his slot (patrol, 4 members)
    // Not while the wall layout is in effect: its slots are destinations at the wall (the last arrive_dist of the leader's walk),
    // not a formation that follows him - the old "Z 580 cm behind" worst case at a wall end was that.
    if (this.n >= 3 && f.mode === 'walk' && f.formation !== 'wall') { // v2.2: the triad's Z too
      const z = f.members.find((mem) => mem.place === 'Z'); // the man at the rear place
      if (z) {
        const h = dirOf(f.heading), d = sub(z.slot, z.pos);
        const behind = d[0] * h[0] + d[1] * h[1];
        this.behind.push(Math.max(0, behind));
        if (!this.m.pace) this.m.pace = { p95: 0, max: 0, worst: null };
        if (behind > this.m.pace.max) { this.m.pace.max = round(behind, 0); this.m.pace.worst = { t: round(f.t, 1), what: `Z ${Math.round(behind)} cm behind`, value: round(behind, 0) }; }
      }
    }

    // 7. squad split by a wall (v2.1): leader -> slot / position, and chain neighbours (actual positions)
    // The trail column (guard level 2) bends round a wall end on the leader's own path: there the chain of neighbours (slots and
    // positions) must hold - the leader's straight line to the tail may clip the end it wraps.
    const cut: string[] = [];
    const trailColumn = f.guardLevel === 2;
    if (!trailColumn) for (const mem of f.members.slice(1)) {
      if (segmentBlocked(leader.pos, mem.slot, this.segs, 0)) cut.push(`${mem.role}'s slot`);
      else if (segmentBlocked(leader.pos, mem.pos, this.segs, 0)) cut.push(mem.role);
    }
    // Chain neighbours by PLACE (who holds which place changes with the minimal-travel assignment).
    for (const [a, b] of FormationGuardRules.ChainPairs(trailColumn ? 'column' : f.formation, this.n)) {
      const A = f.members.find((x) => x.place === a), B = f.members.find((x) => x.place === b);
      if (!A || !B) continue;
      if (segmentBlocked(A.pos, B.pos, this.segs, 0)) cut.push(`${a}-${b}`);
      else if (trailColumn && segmentBlocked(A.slot, B.slot, this.segs, 0)) cut.push(`${a}-${b} slots`);
    }
    if (cut.length) {
      this.splitTimer += SIM_DT;
      if (this.splitTimer >= LIMITS.splitSeconds && !this.splitCounted) { this.splitCounted = true; this.m.split.events++; }
      if (this.splitTimer >= LIMITS.splitSeconds && (!this.m.split.worst || this.splitTimer > this.m.split.worst.value)) {
        this.m.split.worst = { t: round(f.t, 1), what: `${f.formation}${f.requested !== f.formation ? ` (asked ${f.requested})` : ''}: wall cuts ${cut.join(', ')}`, value: round(this.splitTimer, 2) };
      }
    } else { this.splitTimer = 0; this.splitCounted = false; }

    // 8. the leader's trail (the pass side is judged at the end)
    const last = this.trail[this.trail.length - 1];
    if (!last || dist(last, leader.pos) >= 25) this.trail.push(leader.pos);

    // 9. corner: the leader passes a free wall end beside him on patrol -> someone looks round it at that moment
    if (f.mode === 'walk') {
      const fov = cfg(this.c, 'sight.fov');
      this.segs.forEach((w, i) => {
        for (const end of [0, 1] as const) {
          const key = `${i}:${end}`;
          const s = cornerAhead(this.segs, i, end, leader.pos, f.heading, this.c);
          const prev = this.cornerPrev.get(key);
          if (s === null) { this.cornerPrev.delete(key); continue; }
          this.cornerPrev.set(key, s);
          if (prev === undefined || !(prev > 0 && s <= 0)) continue;
          const E = end === 0 ? w.a : w.b, O = end === 0 ? w.b : w.a;
          const u = scale(sub(E, O), 1 / Math.max(1e-6, dist(E, O)));
          const nrm: Vec2 = [-u[1], u[0]];
          const side = (leader.pos[0] - E[0]) * nrm[0] + (leader.pos[1] - E[1]) * nrm[1] >= 0 ? 1 : -1;
          const G = add(E, scale(u, (w.thickness ?? 0) / 2 + CORNER_GRAZE));
          const H = add(G, scale(nrm, -side * LIMITS.cornerPeek));
          const sees = (p: Vec2) => f.members.some((mem) => angleDiff(mem.yaw, yawOf(sub(p, mem.pos))) <= fov / 2 - LIMITS.cornerMargin);
          this.m.corner.ends++;
          if (!sees(G) && !sees(H)) {
            this.m.corner.missed++;
            this.m.corner.worst ??= { t: round(f.t, 1), what: `nobody looked round the ${end === 0 ? 'a' : 'b'} end of ${this.walls[i].id}`, value: this.m.corner.missed };
          }
        }
      });
    }

    // 10. coverage (v2.2): the largest arc nobody's cone covers, walls (within look.wall_dist of a member, +-45 deg round the
    // closest point) excluded, stays within coverage.max_gap_deg
    if (this.m.cover && f.mode === 'walk') {
      const fov = cfg(this.c, 'sight.fov'), reach = cfg(this.c, 'look.wall_dist'), step = LIMITS.coverStep;
      const blocked: number[] = [];
      for (const mem of f.members) for (const w of this.segs) {
        if (distToWall(mem.pos, w) > reach) continue;
        const q = closestOnSegment(mem.pos, w.a, w.b).point;
        if (dist(q, mem.pos) > 1e-6) blocked.push(yawOf(sub(q, mem.pos)));
      }
      const n = 360 / step;
      const open = Array.from({ length: n }, (_, i) => {
        const a = i * step - 180;
        return !f.members.some((mem) => angleDiff(mem.yaw, a) <= fov / 2) && !blocked.some((b) => angleDiff(b, a) < LIMITS.stareAngle);
      });
      let gap = 0;
      if (open.every(Boolean)) gap = 360;
      else {
        const i0 = open.findIndex((o) => !o);
        let run = 0;
        for (let k = 1; k <= n; k++) { if (open[(i0 + k) % n]) { run++; gap = Math.max(gap, run * step); } else run = 0; }
      }
      this.m.cover.walkTime += SIM_DT;
      if (gap <= cfg(this.c, 'coverage.max_gap_deg')) this.m.cover.okTime += SIM_DT;
      else if (!this.m.cover.worst || gap > this.m.cover.worst.value) this.m.cover.worst = { t: round(f.t, 1), what: `${gap} deg open (${f.formation})`, value: gap };
    }

    // 11. wall slide (v2.2): the order along the wall kept, the open half covered
    if (f.slide) {
      const D = dirOf(f.slide.travelYaw);
      const along = f.slide.order.map((p) => { const m = f.members.find((x) => x.place === p)!; return m.pos[0] * D[0] + m.pos[1] * D[1]; });
      const broken = along.some((v, i) => i > 0 && v > along[i - 1] + LIMITS.slideOrderTol);
      if (broken) {
        this.slideBreak += SIM_DT;
        if (this.slideBreak >= LIMITS.splitSeconds && !this.slideCounted) { this.slideCounted = true; this.m.slide.orderBreaks++; this.m.slide.worst ??= { t: round(f.t, 1), what: 'order along the wall broken', value: 1 }; }
      } else { this.slideBreak = 0; this.slideCounted = false; }
      const fov = cfg(this.c, 'sight.fov');
      let seen = 0, total = 0;
      for (let a = -90; a <= 90; a += LIMITS.coverStep) {
        total++;
        if (f.members.some((m) => angleDiff(m.yaw, f.slide!.normalYaw + a) <= fov / 2)) seen++;
      }
      this.m.slide.time += SIM_DT;
      if ((100 * seen) / total >= LIMITS.slideOpenPct) this.m.slide.okTime += SIM_DT;
    }

    // 13. wall end (v2.4, user bug report 2026-10-26): the standing wall layout holds the whole open arc round it - of the probe
    // ring (wall_end.probe_dist round the members) the points some member sees, inside the cone of one who sees them (the flanks
    // round a wall's end included)
    if (this.m.wallArc && f.formation === 'wall' && !f.slide && f.mode === 'wall'
      && f.members.every((mem) => dist(mem.pos, mem.slot) <= LIMITS.wallArcSettleCm)) {
      const arc = openArcCoverage(this.segs, f.members.map((mem) => ({ pos: mem.pos, yaw: mem.yaw })), cfg(this.c, 'sight.fov'), cfg(this.c, 'wall_end.probe_dist'));
      this.m.wallArc.time += SIM_DT;
      if (arc.pct >= LIMITS.wallArcPct) this.m.wallArc.okTime += SIM_DT;
      if (!this.m.wallArc.worst || arc.pct < this.m.wallArc.worst.value) {
        this.m.wallArc.worst = { t: round(f.t, 1), what: `${round(arc.pct, 1)} % of the open arc held (${arc.covered} / ${arc.open} probe points)`, value: round(arc.pct, 1) };
      }
    }

    // 14. covered approach (v2.5, user rule 2026-10-26): on the way into the wall layout somebody always holds the rear (away from the
    // wall, the way they came) and a flank (3 men: one, 4: both - along the wall line)
    if (this.m.approach && f.approach && f.wallNormal !== null) {
      this.approachStart ??= f.t;
      if (f.t - this.approachStart >= LIMITS.approachGrace - 1e-9) {
        const lim = cfg(this.c, 'sight.fov') / 2 - LIMITS.rearMargin;
        const held = (yaw: number) => f.members.some((mem) => angleDiff(mem.yaw, yaw) <= lim);
        this.m.approach.time += SIM_DT;
        if (!held(f.wallNormal)) {
          this.m.approach.rearMiss += SIM_DT;
          this.m.approach.worst ??= { t: round(f.t, 1), what: `nobody holds the rear on the way to the wall (${f.members.map((mem) => `${mem.place} ${mem.approachDuty ?? '-'} ${Math.round(mem.yaw)}`).join(', ')})`, value: 1 };
        }
        const left = held(f.wallNormal - 90), right = held(f.wallNormal + 90);
        if (left || right) this.approachFlank.any += SIM_DT;
        if (left) this.approachFlank.left += SIM_DT;
        if (right) this.approachFlank.right += SIM_DT;
      }
    } else this.approachStart = null;

    // stuck (info)
    for (const mem of f.members.slice(1)) {
      if (dist(mem.pos, mem.slot) > LIMITS.stuckDist) {
        const s = (this.stuckTimer.get(mem.role) ?? 0) + SIM_DT;
        this.stuckTimer.set(mem.role, s);
        if (Math.abs(s - LIMITS.stuckSeconds) < SIM_DT / 2) {
          this.m.stuck.events++;
          this.m.stuck.worst ??= { t: round(f.t, 1), what: `${mem.role} ${Math.round(dist(mem.pos, mem.slot))} cm from his slot`, value: s };
        }
      } else this.stuckTimer.set(mem.role, 0);
    }
  }

  private unexpected(t: number, what: string): void {
    this.m.flicker.unexpected++;
    this.m.flicker.worst = { t: round(t, 1), what, value: this.m.flicker.unexpected };
  }

  finish(): RunMetrics {
    const m = this.m;
    if (m.rearCover) m.rearCover.pct = m.rearCover.walkTime > 0 ? round((100 * this.rearWatched) / m.rearCover.walkTime, 1) : 100;
    m.flicker.per100m = m.distance > 0 ? round(m.flicker.unexpected / (m.distance / 10000), 2) : 0;
    m.expect = (this.route.expect ?? []).map((e) => {
      const acc = this.expectAcc.get(e.zone) ?? { settled: 0, match: 0 };
      return { zone: e.zone, formation: e.formation, settled: round(acc.settled, 2), pct: acc.settled > 0 ? round((100 * acc.match) / acc.settled, 1) : 0 };
    });
    if (m.pace) {
      const s = [...this.behind].sort((a, b) => a - b);
      m.pace.p95 = round(s[Math.min(s.length - 1, Math.floor(s.length * 0.95))] ?? 0, 0);
    }
    if (this.trail.length < 2 || dist(this.trail[this.trail.length - 1], this.trail[0]) < 1) this.trail.push(this.trail[0] ?? [0, 0]);
    m.bypass = (this.route.expect_bypass ?? []).map((e) => {
      const i = this.layout.walls.findIndex((w) => w.id === e.wall);
      return { wall: e.wall, want: e.wall_on, got: wallSideOf(this.trail, this.segs[i]) };
    });
    if (m.cover) m.cover.pct = m.cover.walkTime > 0 ? round((100 * m.cover.okTime) / m.cover.walkTime, 1) : 100;
    m.slide.pct = m.slide.time > 0 ? round((100 * m.slide.okTime) / m.slide.time, 1) : 100;
    if (m.wallArc) m.wallArc.pct = m.wallArc.time > 0 ? round((100 * m.wallArc.okTime) / m.wallArc.time, 1) : 100;
    if (m.approach && m.approach.time > 0) {
      const pct = (v: number) => round((100 * v) / m.approach!.time, 1);
      m.approach.flankPct = pct(this.approachFlank.any); m.approach.leftPct = pct(this.approachFlank.left); m.approach.rightPct = pct(this.approachFlank.right);
      m.approach.rearMiss = round(m.approach.rearMiss, 2); m.approach.time = round(m.approach.time, 2);
    }
    m.duration = round(m.duration, 2);
    m.distance = round(m.distance, 0);
    return m;
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Series summary (what the report, the tuner and the model see - never the raw frames)

export interface CheckLine {
  id: number;
  name: string;
  pass: number;
  fail: number;
  /** Informational only (does not fail the series). */
  info?: boolean;
  worst: string;
}

export interface SeriesSummary {
  runs: number;
  checks: CheckLine[];
  /** 0..1, weighted smooth pass measure of the failing-capable checks (the tuner maximises it; 1 = all green). */
  score: number;
  allGreen: boolean;
}

const runTag = (r: RunMetrics) => `${r.route} n${r.members} s${r.seed}`;

function worstOf(runs: RunMetrics[], pick: (r: RunMetrics) => Worst | null | undefined, bigger = true): string {
  let best: { r: RunMetrics; w: Worst } | null = null;
  for (const r of runs) {
    const w = pick(r);
    if (w && (!best || (bigger ? w.value > best.w.value : w.value < best.w.value))) best = { r, w };
  }
  return best ? `${runTag(best.r)} t=${best.w.t}s ${best.w.what}` : '-';
}

export const CHECK_WEIGHTS: Record<number, number> = { 1: 2, 2: 2, 3: 1, 4: 2, 5: 2, 6: 1, 7: 3, 8: 2, 9: 1, 10: 2, 11: 2, 13: 2, 14: 2 };

/** v2.5 check 14 passes: the rear always held on the approach; 3+ men a flank, 4+ each flank, >= LIMITS.approachFlankPct of it. */
export function approachOk(r: RunMetrics): boolean {
  const a = r.approach;
  if (!a || a.time <= 0) return true;
  return a.rearMiss <= 1e-9 && (r.members < 3 || a.flankPct >= LIMITS.approachFlankPct)
    && (r.members < 4 || (a.leftPct >= LIMITS.approachFlankPct && a.rightPct >= LIMITS.approachFlankPct));
}

export function summarize(runs: RunMetrics[]): SeriesSummary {
  const line = (id: number, name: string, applicable: RunMetrics[], ok: (r: RunMetrics) => boolean, worst: string, info = false): CheckLine => {
    const pass = applicable.filter(ok).length;
    return { id, name, pass, fail: applicable.length - pass, worst, info };
  };
  // v2.2 (user 2026-10-25): rear cover is required with 3 members too (the triad's Z walks backwards looking at 6).
  const four = runs.filter((r) => r.members >= 3 && r.rearCover && r.rearCover.walkTime > 0);
  const withExpect = runs.flatMap((r) => r.expect.map((e) => ({ r, e })));
  const expectFail = withExpect.filter(({ e }) => e.pct < LIMITS.expectPct).sort((a, b) => a.e.pct - b.e.pct)[0];
  const paceRuns = runs.filter((r) => r.pace);
  const bypassRuns = runs.filter((r) => r.bypass.length);
  const bypassFail = bypassRuns.find((r) => r.bypass.some((b) => b.got !== b.want));
  const cornerRuns = runs.filter((r) => r.corner.ends > 0);
  const coverRuns = runs.filter((r) => r.cover && r.cover.walkTime > 0);
  const slideRuns = runs.filter((r) => r.slide.time > 0);
  const arcRuns = runs.filter((r) => r.wallArc && r.wallArc.time > 0);
  const approachRuns = runs.filter((r) => r.approach && r.approach.time > 0);
  const checks: CheckLine[] = [
    line(1, `wall stare (<${LIMITS.stareDist} cm, ${LIMITS.stareAngle} deg, >=${LIMITS.stareMinSeconds} s)`, runs,
      (r) => r.wallStare.events === 0, worstOf(runs, (r) => r.wallStare.worst)),
    line(2, `rear cover 5-7 o'clock, 3-4 men (>=${LIMITS.rearCoverPct} % of patrol)`, four,
      (r) => r.rearCover!.pct >= LIMITS.rearCoverPct,
      four.length ? (() => { const w = [...four].sort((a, b) => a.rearCover!.pct - b.rearCover!.pct)[0]; return `${runTag(w)} ${w.rearCover!.pct} %`; })() : '-'),
    line(3, `flicker (unexpected changes <=${LIMITS.flickerPer100m} / 100 m)`, runs,
      (r) => r.flicker.per100m <= LIMITS.flickerPer100m, worstOf(runs, (r) => r.flicker.worst)),
    { id: 4, name: `expected formation per zone (>=${LIMITS.expectPct} % settled)`, pass: withExpect.length - withExpect.filter(({ e }) => e.pct < LIMITS.expectPct).length,
      fail: withExpect.filter(({ e }) => e.pct < LIMITS.expectPct).length,
      worst: expectFail ? `${runTag(expectFail.r)} ${expectFail.e.zone} wants ${expectFail.e.formation}: ${expectFail.e.pct} %${expectFail.e.settled === 0 ? ' (never settled there)' : ''}` : '-' },
    line(5, `slots walkable + spacing >= min (${LIMITS.spacingMinSeconds} s)`, runs,
      (r) => r.slots.invalidTicks === 0 && r.slots.spacingEvents === 0, worstOf(runs, (r) => r.slots.worst)),
    line(6, `pace: rear man <=${LIMITS.paceBehind} cm behind his slot (p95)`, paceRuns,
      (r) => r.pace!.p95 <= LIMITS.paceBehind, worstOf(paceRuns, (r) => r.pace!.worst)),
    line(7, `squad never split by a wall (>=${LIMITS.splitSeconds} s)`, runs,
      (r) => r.split.events === 0, worstOf(runs, (r) => r.split.worst)),
    line(8, 'passes on the shielded side when a threat is known (detour <= bypass.max_detour)', bypassRuns,
      (r) => r.bypass.every((b) => b.got === b.want),
      bypassFail ? (() => { const b = bypassFail.bypass.find((x) => x.got !== x.want)!; return `${runTag(bypassFail)} ${b.wall}: wall on the ${b.got}, wants ${b.want}`; })() : '-'),
    line(9, 'corner man looks round a passed wall end in time', cornerRuns,
      (r) => r.corner.missed === 0, worstOf(cornerRuns, (r) => r.corner.worst)),
    line(10, `coverage: largest open uncovered arc <= coverage.max_gap_deg, 3-4 men (>=${LIMITS.coverPct} % of patrol)`, coverRuns,
      (r) => r.cover!.pct >= LIMITS.coverPct,
      coverRuns.length ? (() => { const w = [...coverRuns].sort((a, b) => a.cover!.pct - b.cover!.pct)[0]; return `${runTag(w)} ${w.cover!.pct} %${w.cover!.worst ? `, worst t=${w.cover!.worst.t}s ${w.cover!.worst.what}` : ''}`; })() : '-'),
    line(11, `wall slide: order kept, open half covered (>=${LIMITS.slideOpenPct} % of it, >=90 % of the slide)`, slideRuns,
      (r) => r.slide.orderBreaks === 0 && r.slide.pct >= 90,
      slideRuns.length ? (() => { const w = [...slideRuns].sort((a, b) => a.slide.pct - b.slide.pct - (b.slide.orderBreaks - a.slide.orderBreaks) * 1000)[0]; return `${runTag(w)} ${w.slide.pct} %${w.slide.orderBreaks ? `, order broken ${w.slide.orderBreaks}x` : ''}`; })() : '-'),
    line(13, `wall end: the standing wall layout holds the open arc round it, 3-4 men (>=${LIMITS.wallArcPct} % of the probe ring, >=${LIMITS.wallArcTimePct} % of the time)`, arcRuns,
      (r) => r.wallArc!.pct >= LIMITS.wallArcTimePct,
      arcRuns.length ? (() => {
        const w = [...arcRuns].sort((a, b) => a.wallArc!.pct - b.wallArc!.pct || (a.wallArc!.worst?.value ?? 100) - (b.wallArc!.worst?.value ?? 100))[0];
        return `${runTag(w)} ${w.wallArc!.pct} %${w.wallArc!.worst ? `, worst t=${w.wallArc!.worst.t}s ${w.wallArc!.worst.what}` : ''}`;
      })() : '-'),
    line(14, `covered approach to the wall: the rear always held, a flank (3) / both (4) >=${LIMITS.approachFlankPct} % of it`, approachRuns, approachOk,
      approachRuns.length ? (() => {
        const bad = approachRuns.find((r) => !approachOk(r)) ?? [...approachRuns].sort((a, b) => a.approach!.flankPct - b.approach!.flankPct)[0];
        const a = bad.approach!;
        return `${runTag(bad)} rear missed ${a.rearMiss} s of ${a.time} s, flank ${a.flankPct} % (left ${a.leftPct} / right ${a.rightPct})${a.worst ? `, t=${a.worst.t}s ${a.worst.what}` : ''}`;
      })() : '-'),
    line(12, `info: stuck followers (>${LIMITS.stuckDist} cm for ${LIMITS.stuckSeconds} s), slot fallbacks`, runs, (r) => r.stuck.events === 0,
      `${worstOf(runs, (r) => r.stuck.worst)}; fallback ticks ${runs.reduce((a, r) => a + r.slots.fallbackTicks, 0)}`, true),
  ];
  // Smooth score: a passing run counts 1, a failing one by how close it came, so the tuner sees partial progress.
  const mean = (xs: number[]) => (xs.length ? xs.reduce((x, y) => x + y, 0) / xs.length : null);
  const soft: Record<number, number | null> = {
    1: mean(runs.map((r) => 1 / (1 + r.wallStare.events))),
    2: mean(four.map((r) => Math.min(1, r.rearCover!.pct / LIMITS.rearCoverPct))),
    3: mean(runs.map((r) => 1 / (1 + Math.max(0, r.flicker.per100m - LIMITS.flickerPer100m)))),
    4: mean(withExpect.map(({ e }) => Math.min(1, e.pct / LIMITS.expectPct))),
    5: mean(runs.map((r) => (r.slots.invalidTicks === 0 && r.slots.spacingEvents === 0 ? 1 : 0.5 / (1 + r.slots.spacingEvents + r.slots.invalidTicks / 30)))),
    6: mean(paceRuns.map((r) => Math.min(1, LIMITS.paceBehind / Math.max(1, r.pace!.p95)))),
    7: mean(runs.map((r) => 1 / (1 + r.split.events))),
    8: mean(bypassRuns.map((r) => r.bypass.filter((b) => b.got === b.want).length / r.bypass.length)),
    9: mean(cornerRuns.map((r) => 1 - r.corner.missed / r.corner.ends)),
    10: mean(coverRuns.map((r) => Math.min(1, r.cover!.pct / LIMITS.coverPct))),
    11: mean(slideRuns.map((r) => (r.slide.orderBreaks ? 0.5 : 1) * Math.min(1, r.slide.pct / 90))),
    13: mean(arcRuns.map((r) => Math.min(1, r.wallArc!.pct / LIMITS.wallArcTimePct))),
    14: mean(approachRuns.map((r) => (approachOk(r) ? 1 : 0.5))),
  };
  let num = 0, den = 0;
  for (const [id, v] of Object.entries(soft)) {
    if (v === null) continue;
    const w = CHECK_WEIGHTS[Number(id)] ?? 1;
    num += w * v; den += w;
  }
  const score = den ? round(num / den, 4) : 1;
  return { runs: runs.length, checks, score, allGreen: checks.every((ch) => ch.info || ch.fail === 0) };
}

export interface ReportHeader {
  layout: string;
  routes: number;
  members: number[];
  seeds: number;
  seed: number;
  configHash: string;
  date: string;
  /** v2.1: the manual menu choice of every run (absent = auto). */
  manual?: string;
}

/** The <= 30-line report (FORMATION_CONTRACT.md 4): the only thing a coding agent reads. */
export function renderReport(h: ReportHeader, s: SeriesSummary, advice: string[] | null): string {
  const lines = [
    `# Movement bot report - ${h.layout}`,
    `layout ${h.layout} | routes ${h.routes} | runs ${s.runs} (members ${h.members.join(',')} x seeds ${h.seeds}) | seed ${h.seed} | config ${h.configHash} | ${h.date}${h.manual ? ` | manual ${h.manual}` : ''}`,
    `result: ${s.allGreen ? 'ALL GREEN' : 'FAILURES'} | score ${s.score}`,
    '',
    '| # | check | pass | fail | worst case |',
    '|---|---|---|---|---|',
    ...s.checks.map((c) => `| ${c.id} | ${c.name} | ${c.pass} | ${c.info ? '-' : c.fail} | ${c.worst} |`),
    '',
    advice && advice.length ? 'model advice:' : 'no model advice',
    ...(advice ?? []).slice(0, 10).map((a) => `- ${a}`),
    '',
    'raw runs: runs.jsonl (not for agents)',
  ];
  return lines.slice(0, 30).join('\n') + '\n';
}

/**
 * v2.1: auto + every manual menu item on every route - fail counts per check. A manual formation ignores the terrain on
 * purpose, so only checks 1 (wall stare), 2 (rear cover - v2.2: every formation with 3+ has a rear guard), 5 (slots / spacing), 7 (squad split), 10 (coverage),
 * 11 (slide) and 13 (v2.4: the wall layout's open arc - a manual formation never stands in it) gate it; the rest is shown for information.
 */
export function renderManualMatrix(h: ReportHeader, rows: { choice: string; summary: SeriesSummary }[]): string {
  const ids = rows[0]?.summary.checks.filter((c) => !c.info).map((c) => c.id) ?? [];
  const gated = (choice: string, id: number) => choice === 'auto' || [1, 2, 5, 7, 10, 11, 13, 14].includes(id);
  const cell = (choice: string, ch: CheckLine | undefined) => !ch || ch.pass + ch.fail === 0 ? '-' : gated(choice, ch.id) ? String(ch.fail) : `(${ch.fail})`;
  const lines = [
    `# Movement bot - auto + every manual formation - ${h.layout}`,
    `layout ${h.layout} | routes ${h.routes} | members ${h.members.join(',')} x seeds ${h.seeds} | seed ${h.seed} | config ${h.configHash} | ${h.date}`,
    'cells = failing runs per check; (n) = not gating for a manual formation (it ignores the terrain on purpose)',
    '',
    `| choice | runs | ${ids.map((i) => `#${i}`).join(' | ')} | score | gate |`,
    `|---|---|${ids.map(() => '---').join('|')}|---|---|`,
    ...rows.map((r) => {
      const ok = r.choice === 'auto' ? r.summary.allGreen : r.summary.checks.filter((c) => [1, 2, 5, 7, 10, 11, 13, 14].includes(c.id)).every((c) => c.fail === 0);
      return `| ${r.choice} | ${r.summary.runs} | ${ids.map((i) => cell(r.choice, r.summary.checks.find((c) => c.id === i))).join(' | ')} | ${r.summary.score} | ${ok ? 'PASS' : 'FAIL'} |`;
    }),
    '',
    'worst split (#7) per choice:',
    ...rows.map((r) => `- ${r.choice}: ${r.summary.checks.find((c) => c.id === 7)?.worst ?? '-'}`),
  ];
  return lines.slice(0, 30).join('\n') + '\n';
}
