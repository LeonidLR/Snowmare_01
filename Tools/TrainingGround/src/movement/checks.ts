// Training Ground / movement: the bot's checks (FORMATION_CONTRACT.md 4) over simulator frames, per run and per series.
// Thresholds not fixed by the contract are named constants here and printed in the report.

import type { FormationConfig } from './config.ts';
import { cfg } from './config.ts';
import type { Vec2 } from './geometry.ts';
import { angleDiff, closestOnSegment, dirOf, dist, distToWall, round, sub, yawOf } from './geometry.ts';
import type { Formation, Layout, LayoutRoute } from './layout.ts';
import { wallSegments, zoneAt } from './layout.ts';
import type { Frame } from './sim.ts';
import { SIM_DT } from './sim.ts';

export const LIMITS = {
  stareDist: 200, // cm (contract)
  stareAngle: 45, // deg (contract)
  stareMinSeconds: 0.3, // a glance while turning is not a stare
  rearCoverPct: 90, // % of patrol time, 4 members (contract)
  rearMargin: 30, // the 5-7 o'clock sector (60 deg) must sit inside a cone: centre within fov / 2 - 30
  flickerPer100m: 1, // unexpected formation changes per 100 m (contract)
  flickerRevert: 2, // a change undone within 2 x classify.hold_time is a flicker
  expectLookAhead: 500, // cm: a change into the formation expected just ahead counts as expected
  expectSettleExtra: 0.5, // s after entering a zone (plus classify.hold_time) before matching counts
  expectPct: 80, // % of settled time in the zone with the expected formation
  spacingMinSeconds: 0.5,
  paceBehind: 150, // cm (contract), checked on the 95th percentile
  stuckDist: 600,
  stuckSeconds: 3,
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
  private lastChange: { t: number; from: Formation; what: string } | null = null;
  private zoneEnter = new Map<string, number>();
  private expectAcc = new Map<string, { settled: number; match: number }>();
  private spacingTimer = new Map<string, number>();
  private stuckTimer = new Map<string, number>();
  private behind: number[] = [];
  private rearWatched = 0;

  constructor(layout: Layout, route: LayoutRoute, memberCount: number, seed: number, config: FormationConfig) {
    this.layout = layout; this.route = route; this.n = memberCount; this.c = config;
    this.walls = layout.walls.map((w, i) => ({ id: w.id, seg: wallSegments(layout)[i] }));
    this.m = {
      route: route.id, members: memberCount, seed, duration: 0, distance: 0,
      wallStare: { events: 0, worst: null },
      rearCover: memberCount >= 3 ? { walkTime: 0, pct: 100 } : null,
      flicker: { changes: 0, unexpected: 0, per100m: 0, worst: null },
      expect: [],
      slots: { fallbackTicks: 0, invalidTicks: 0, spacingEvents: 0, worst: null },
      pace: null,
      stuck: { events: 0, worst: null },
    };
  }

  onFrame(f: Frame): void {
    this.m.duration = f.t;
    this.m.distance = f.leaderDistance;
    const leader = f.members[0];

    // 1. wall stare
    for (const mem of f.members) {
      let staring = '';
      for (const w of this.walls) {
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
    const zone = zoneAt(this.layout, leader.pos);
    // A change is unexpected when it contradicts the expectation of the zone the leader is in / about to enter, or when it
    // is reverted within LIMITS.flickerRevert x hold_time (a real flicker); a lasting change on unlabelled ground is fine.
    if (this.prevFormation && f.formation !== this.prevFormation) {
      this.m.flicker.changes++;
      const ahead: Vec2 = [leader.pos[0] + dirOf(f.heading)[0] * LIMITS.expectLookAhead, leader.pos[1] + dirOf(f.heading)[1] * LIMITS.expectLookAhead];
      const zones = [zone, zoneAt(this.layout, ahead)].filter(Boolean).map((z) => z!.id);
      const wants = (this.route.expect ?? []).filter((e) => zones.includes(e.zone));
      const what = `${this.prevFormation} -> ${f.formation}${zone ? ` in ${zone.id}` : ''}`;
      const reverted = this.lastChange && f.t - this.lastChange.t < LIMITS.flickerRevert * cfg(this.c, 'classify.hold_time') &&
        sameFormation(this.lastChange.from, f.formation);
      if (reverted) this.unexpected(f.t, `${this.lastChange!.what}, back after ${round(f.t - this.lastChange!.t, 1)} s`);
      else if (wants.length && !wants.some((e) => sameFormation(e.formation, f.formation))) this.unexpected(f.t, `${what} (zone wants ${wants[0].formation})`);
      this.lastChange = { t: f.t, from: this.prevFormation, what };
    }
    this.prevFormation = f.formation;
    for (const e of this.route.expect ?? []) {
      const key = e.zone;
      if (zone?.id === key) {
        if (!this.zoneEnter.has(key)) this.zoneEnter.set(key, f.t);
        if (f.t - this.zoneEnter.get(key)! >= cfg(this.c, 'classify.hold_time') + LIMITS.expectSettleExtra) {
          const acc = this.expectAcc.get(key) ?? { settled: 0, match: 0 };
          acc.settled += SIM_DT;
          if (sameFormation(f.formation, e.formation)) acc.match += SIM_DT;
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
    if (this.n >= 4 && f.mode === 'walk') {
      const z = f.members.find((mem) => mem.role === 'Z');
      if (z) {
        const h = dirOf(f.heading), d = sub(z.slot, z.pos);
        const behind = d[0] * h[0] + d[1] * h[1];
        this.behind.push(Math.max(0, behind));
        if (!this.m.pace) this.m.pace = { p95: 0, max: 0, worst: null };
        if (behind > this.m.pace.max) { this.m.pace.max = round(behind, 0); this.m.pace.worst = { t: round(f.t, 1), what: `Z ${Math.round(behind)} cm behind`, value: round(behind, 0) }; }
      }
    }

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
  /** 0..1, weighted pass rate of the failing-capable checks (the tuner maximises it). */
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

export const CHECK_WEIGHTS: Record<number, number> = { 1: 2, 2: 2, 3: 1, 4: 2, 5: 2, 6: 1 };

export function summarize(runs: RunMetrics[]): SeriesSummary {
  const line = (id: number, name: string, applicable: RunMetrics[], ok: (r: RunMetrics) => boolean, worst: string, info = false): CheckLine => {
    const pass = applicable.filter(ok).length;
    return { id, name, pass, fail: applicable.length - pass, worst, info };
  };
  const four = runs.filter((r) => r.members >= 4 && r.rearCover && r.rearCover.walkTime > 0);
  const three = runs.filter((r) => r.members === 3 && r.rearCover && r.rearCover.walkTime > 0);
  const withExpect = runs.flatMap((r) => r.expect.map((e) => ({ r, e })));
  const expectFail = withExpect.filter(({ e }) => e.pct < LIMITS.expectPct).sort((a, b) => a.e.pct - b.e.pct)[0];
  const paceRuns = runs.filter((r) => r.pace);
  const checks: CheckLine[] = [
    line(1, `wall stare (<${LIMITS.stareDist} cm, ${LIMITS.stareAngle} deg, >=${LIMITS.stareMinSeconds} s)`, runs,
      (r) => r.wallStare.events === 0, worstOf(runs, (r) => r.wallStare.worst)),
    line(2, `rear cover 5-7 o'clock, 4 men (>=${LIMITS.rearCoverPct} % of patrol)`, four,
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
    line(7, `info: rear cover with 3 men (gap kept by design)`, three, () => true,
      three.length ? `min ${Math.min(...three.map((r) => r.rearCover!.pct))} % / avg ${round(three.reduce((a, r) => a + r.rearCover!.pct, 0) / three.length, 1)} %` : '-', true),
    line(8, `info: stuck followers (>${LIMITS.stuckDist} cm for ${LIMITS.stuckSeconds} s), slot fallbacks`, runs, (r) => r.stuck.events === 0,
      `${worstOf(runs, (r) => r.stuck.worst)}; fallback ticks ${runs.reduce((a, r) => a + r.slots.fallbackTicks, 0)}`, true),
  ];
  let num = 0, den = 0;
  for (const ch of checks) {
    if (ch.info) continue;
    const total = ch.pass + ch.fail;
    if (!total) continue;
    const w = CHECK_WEIGHTS[ch.id] ?? 1;
    num += (w * ch.pass) / total; den += w;
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
}

/** The <= 30-line report (FORMATION_CONTRACT.md 4): the only thing a coding agent reads. */
export function renderReport(h: ReportHeader, s: SeriesSummary, advice: string[] | null): string {
  const lines = [
    `# Movement bot report - ${h.layout}`,
    `layout ${h.layout} | routes ${h.routes} | runs ${s.runs} (members ${h.members.join(',')} x seeds ${h.seeds}) | seed ${h.seed} | config ${h.configHash} | ${h.date}`,
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
