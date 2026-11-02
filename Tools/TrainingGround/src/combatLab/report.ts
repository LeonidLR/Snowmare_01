// Training Ground department «Боевая лаборатория», part C: the bot runs' report. Parses the run records the playtest bot writes in route
// mode (Source/CodexTactics/Private/Bot/CombatLabRunCollector.cpp CombatLabRules::MakeRunRecord, schema codex.combatlab_run/1; one line
// per run in Saved/Telemetry/combat_lab/<batch>/runs.jsonl) and builds the per-run rows and the aggregate. Pure (tests: report.test.ts).

export const RUN_SCHEMA = 'codex.combatlab_run/1';

export interface StuckEvent { time: number; position: [number, number]; waypoint: number; label: string; last_order: string; reason: string; skipped: boolean }
/** A drop through a roof / floor hole on the route (generated maps): `fell` = the leader physically fell through a drop nav link, `scripted` = the bot teleported the squad because the drop failed. */
export interface DropEvent { waypoint: number; label: string; via: string; fell: boolean; scripted: boolean; seconds: number }
export interface ContactRow { id: number; kind: ContactKind; trigger: string; time: number; position: [number, number]; formation: string; watched_at_start: boolean; unwatched_strike: boolean; reaction_sec: number }
export type ContactKind = 'patrol' | 'ambush' | 'coordinated';
export interface MemberRow { name: string; changes: number; standing: number; crouching: number; prone: number; damage_taken: number; damage_absorbed: number; rounds: number; climbs_up: number; knockdowns: number }

export interface RunRecord {
  schema: string;
  kind: 'combat_lab';
  result: string;
  run: number;
  bot_seed: number;
  pressure_seed: number;
  level_id: string;
  map: string;
  timestamp_utc: string;
  duration_sec: number;
  planned_sec: number;
  real_sec: number;
  cheats: { immortal: boolean; infinite_ammo: boolean };
  route: {
    name: string; loop: boolean; waypoints: number; lap_length_m: number; completed: boolean; progress: number; laps: number; reached: number; skipped: number;
    distance_m: number; furthest_index: number; furthest_label: string; current_index: number; current_label: string; last_order: string; end_position: [number, number];
    lap_list: { seconds: number; reached: number; skipped: number }[]; stuck: StuckEvent[];
    /** Drops through roof / floor holes; OPTIONAL: records of the lab map and older records have none. */
    drops?: DropEvent[];
  };
  contacts: { total: number; patrol: number; ambush: number; coordinated: number; unwatched_strikes: number; never_looked: number; reaction_mean_sec: number; reaction_max_sec: number; list: ContactRow[] };
  formations: { changes: number; entered: Record<string, number>; seconds: Record<string, number> };
  wall_hugs: number;
  /** Cover at a wall in a fight (decision 5, 2026-11-01 night): orders, covers taken, seconds there. Absent in records before it. */
  cover?: { orders: number; taken: number; seconds: number };
  stances: { changes: number; per_min: number; members: MemberRow[] };
  damage: { taken: number; absorbed: number };
  ammo: { rounds: number; infinite: boolean };
  ladder: { climbs_up: number; climbs_down: number };
  knockdowns: number;
  fights: { count: number; seconds: number };
  pressure: {
    events: number; spawned: number; sprung_by_watch: number; relocations: number;
    /** Decisions 1-2 (2026-11-01 night); absent in records before them. */
    sprung_by_hold?: number; runs_spotted?: number; no_route?: number; followers?: number;
  };
}

const isObj = (v: unknown): v is Record<string, unknown> => typeof v === 'object' && v !== null && !Array.isArray(v);

/** Why a parsed line is no run record ('' = it is one). */
export function recordProblem(v: unknown): string {
  if (!isObj(v)) return 'не объект';
  if (v.schema !== RUN_SCHEMA) return `схема не ${RUN_SCHEMA}`;
  for (const key of ['route', 'contacts', 'formations', 'stances', 'damage', 'ammo', 'ladder', 'fights']) if (!isObj(v[key])) return `нет блока ${key}`;
  if (typeof v.duration_sec !== 'number') return 'нет duration_sec';
  return '';
}

/** The run records of a runs.jsonl text (other / broken lines are counted, not thrown). */
export function parseRunsJsonl(text: string): { runs: RunRecord[]; skipped: number } {
  const runs: RunRecord[] = [];
  let skipped = 0;
  for (const line of text.split(/\r?\n/)) {
    if (!line.trim()) continue;
    try {
      const v = JSON.parse(line.replace(/^﻿/, ''));
      if (recordProblem(v)) skipped++;
      else runs.push(v as RunRecord);
    } catch {
      skipped++;
    }
  }
  runs.sort((a, b) => a.run - b.run);
  return { runs, skipped };
}

/** The drops of a run ([] when the record has none or the field is malformed). */
export function runDrops(run: RunRecord): DropEvent[] {
  const list: unknown = run.route.drops;
  return Array.isArray(list) ? list.filter((d): d is DropEvent => isObj(d) && typeof d.waypoint === 'number') : [];
}

/** Where / why the route stopped for the report: the first stuck event that skipped a waypoint, else the last stuck event. */
export function worstStuck(run: RunRecord): StuckEvent | undefined {
  return run.route.stuck.find((s) => s.skipped) ?? run.route.stuck[run.route.stuck.length - 1];
}

const REASONS: Record<string, string> = {
  no_move: 'стоит на месте', leg_timeout: 'не доходит (обход / петляет)', order_failed: 'приказ не отдан', action_failed: 'действие не удалось',
  no_ground_hit: 'нет земли под точкой', wall_click_missed: 'клик мимо стены', wall_not_reached: 'не встал к стене', no_ladder: 'нет лестницы',
  roof_click_missed: 'клик мимо крыши', climb_up_timeout: 'не поднялся', climb_down_timeout: 'не спустился', ground_click_missed: 'клик мимо земли', no_leader: 'нет лидера',
};

/** "no_move" / "action_failed:climb_up_timeout" in words. */
export function reasonText(reason: string): string {
  const [head, tail] = reason.split(':');
  return tail ? `${REASONS[head] ?? head}: ${REASONS[tail] ?? tail}` : (REASONS[head] ?? head);
}

export interface RunRow {
  run: number;
  result: string;
  minutes: number;
  completed: boolean;
  progressPct: number;
  laps: number;
  distanceM: number;
  skipped: number;
  stuck: number;
  stuckWhere: string;
  drops: number;
  dropsFell: number;
  dropsScripted: number;
  contacts: number;
  patrol: number;
  ambush: number;
  coordinated: number;
  unwatched: number;
  reactionMean: number;
  formationChanges: number;
  wallHugs: number;
  coverTaken: number;
  coverOrders: number;
  coverSeconds: number;
  stanceChanges: number;
  stancePerMin: number;
  damage: number;
  absorbed: number;
  rounds: number;
  climbs: number;
  knockdowns: number;
}

export function runRow(run: RunRecord): RunRow {
  const stuck = worstStuck(run);
  return {
    run: run.run,
    result: run.result,
    minutes: run.duration_sec / 60,
    completed: run.route.completed,
    progressPct: Math.round(run.route.progress * 100),
    laps: run.route.laps,
    distanceM: run.route.distance_m,
    skipped: run.route.skipped,
    stuck: run.route.stuck.length,
    stuckWhere: stuck ? `#${stuck.waypoint} ${stuck.label} (${Math.round(stuck.position[0])}, ${Math.round(stuck.position[1])}) — ${reasonText(stuck.reason)}; приказ: ${stuck.last_order || '—'}` : '',
    drops: runDrops(run).length,
    dropsFell: runDrops(run).filter((d) => d.fell).length,
    dropsScripted: runDrops(run).filter((d) => d.scripted).length,
    contacts: run.contacts.total,
    patrol: run.contacts.patrol,
    ambush: run.contacts.ambush,
    coordinated: run.contacts.coordinated,
    unwatched: run.contacts.unwatched_strikes,
    reactionMean: run.contacts.reaction_mean_sec,
    formationChanges: run.formations.changes,
    wallHugs: run.wall_hugs,
    coverTaken: run.cover?.taken ?? 0,
    coverOrders: run.cover?.orders ?? 0,
    coverSeconds: run.cover?.seconds ?? 0,
    stanceChanges: run.stances.changes,
    stancePerMin: run.stances.per_min,
    damage: run.damage.taken,
    absorbed: run.damage.absorbed,
    rounds: run.ammo.rounds,
    climbs: run.ladder.climbs_up,
    knockdowns: run.knockdowns,
  };
}

export interface Aggregate {
  runs: number;
  minutes: number;
  routeCompleted: number;
  progressMeanPct: number;
  lapsTotal: number;
  distanceM: number;
  skipped: number;
  stuck: number;
  /** Stuck events grouped by waypoint label: where the bot gets stuck most. */
  stuckByWaypoint: { label: string; count: number; reasons: string[] }[];
  /** Drops through roof / floor holes: all, physically fallen, scripted (the bot teleported the squad after a failed drop). */
  drops: number;
  dropsFell: number;
  dropsScripted: number;
  contacts: number;
  byKind: Record<ContactKind, number>;
  unwatchedStrikes: number;
  neverLooked: number;
  /** Mean / median / max over every contact somebody looked at (s). */
  reactionMean: number;
  reactionMedian: number;
  reactionMax: number;
  /** Contacts per formation at the contact. */
  contactFormations: Record<string, number>;
  formationChanges: number;
  formationsEntered: Record<string, number>;
  formationSeconds: Record<string, number>;
  wallHugs: number;
  /** Cover at a wall in fights (decision 5): orders, taken, seconds at the wall. */
  coverOrders: number;
  coverTaken: number;
  coverSeconds: number;
  /** The ambushes (decisions 1-2): relocation runs, runs spotted on the way, searches with no concealed route, groups turned into following patrols, strikes after the hold limit. */
  relocations: number;
  runsSpotted: number;
  noRoute: number;
  followers: number;
  sprungByHold: number;
  stanceChanges: number;
  stancePerMin: number;
  /** Stance changes per operative name over all runs. */
  stanceByMember: Record<string, { changes: number; standing: number; crouching: number; prone: number }>;
  damage: number;
  absorbed: number;
  rounds: number;
  infiniteAmmo: boolean;
  immortal: boolean;
  climbsUp: number;
  climbsDown: number;
  knockdowns: number;
  fights: number;
  fightSeconds: number;
  results: Record<string, number>;
}

const sum = (xs: number[]) => xs.reduce((a, b) => a + b, 0);
const round2 = (x: number) => Math.round(x * 100) / 100;

export function median(xs: number[]): number {
  if (xs.length === 0) return 0;
  const s = [...xs].sort((a, b) => a - b);
  const mid = Math.floor(s.length / 2);
  return s.length % 2 ? s[mid] : (s[mid - 1] + s[mid]) / 2;
}

function addInto(target: Record<string, number>, source: Record<string, number>): void {
  for (const [k, v] of Object.entries(source)) target[k] = (target[k] ?? 0) + v;
}

export function aggregate(runs: RunRecord[]): Aggregate {
  const contacts = runs.flatMap((r) => r.contacts.list);
  const drops = runs.flatMap(runDrops);
  const reactions = contacts.filter((c) => c.reaction_sec >= 0).map((c) => c.reaction_sec);
  const minutes = sum(runs.map((r) => r.duration_sec)) / 60;
  const stuckBy = new Map<string, { count: number; reasons: Set<string> }>();
  for (const s of runs.flatMap((r) => r.route.stuck)) {
    const key = `#${s.waypoint} ${s.label}`;
    const entry = stuckBy.get(key) ?? { count: 0, reasons: new Set<string>() };
    entry.count++;
    entry.reasons.add(reasonText(s.reason));
    stuckBy.set(key, entry);
  }
  const entered: Record<string, number> = {};
  const seconds: Record<string, number> = {};
  const contactFormations: Record<string, number> = {};
  const stanceByMember: Aggregate['stanceByMember'] = {};
  const results: Record<string, number> = {};
  for (const r of runs) {
    addInto(entered, r.formations.entered);
    addInto(seconds, r.formations.seconds);
    results[r.result] = (results[r.result] ?? 0) + 1;
    for (const m of r.stances.members) {
      const e = stanceByMember[m.name] ?? { changes: 0, standing: 0, crouching: 0, prone: 0 };
      e.changes += m.changes; e.standing += m.standing; e.crouching += m.crouching; e.prone += m.prone;
      stanceByMember[m.name] = e;
    }
  }
  for (const c of contacts) contactFormations[c.formation || '—'] = (contactFormations[c.formation || '—'] ?? 0) + 1;
  const stanceChanges = sum(runs.map((r) => r.stances.changes));
  return {
    runs: runs.length,
    minutes: round2(minutes),
    routeCompleted: runs.filter((r) => r.route.completed).length,
    progressMeanPct: runs.length ? Math.round(sum(runs.map((r) => r.route.progress)) / runs.length * 100) : 0,
    lapsTotal: sum(runs.map((r) => r.route.laps)),
    distanceM: round2(sum(runs.map((r) => r.route.distance_m))),
    skipped: sum(runs.map((r) => r.route.skipped)),
    stuck: sum(runs.map((r) => r.route.stuck.length)),
    stuckByWaypoint: [...stuckBy.entries()].map(([label, e]) => ({ label, count: e.count, reasons: [...e.reasons] })).sort((a, b) => b.count - a.count || a.label.localeCompare(b.label)),
    drops: drops.length,
    dropsFell: drops.filter((d) => d.fell).length,
    dropsScripted: drops.filter((d) => d.scripted).length,
    contacts: contacts.length,
    byKind: { patrol: contacts.filter((c) => c.kind === 'patrol').length, ambush: contacts.filter((c) => c.kind === 'ambush').length, coordinated: contacts.filter((c) => c.kind === 'coordinated').length },
    unwatchedStrikes: contacts.filter((c) => c.unwatched_strike).length,
    neverLooked: contacts.filter((c) => c.reaction_sec < 0).length,
    reactionMean: reactions.length ? round2(sum(reactions) / reactions.length) : 0,
    reactionMedian: round2(median(reactions)),
    reactionMax: reactions.length ? round2(Math.max(...reactions)) : 0,
    contactFormations,
    formationChanges: sum(runs.map((r) => r.formations.changes)),
    formationsEntered: entered,
    formationSeconds: Object.fromEntries(Object.entries(seconds).map(([k, v]) => [k, round2(v)])),
    wallHugs: sum(runs.map((r) => r.wall_hugs)),
    coverOrders: sum(runs.map((r) => r.cover?.orders ?? 0)),
    coverTaken: sum(runs.map((r) => r.cover?.taken ?? 0)),
    coverSeconds: round2(sum(runs.map((r) => r.cover?.seconds ?? 0))),
    relocations: sum(runs.map((r) => r.pressure?.relocations ?? 0)),
    runsSpotted: sum(runs.map((r) => r.pressure?.runs_spotted ?? 0)),
    noRoute: sum(runs.map((r) => r.pressure?.no_route ?? 0)),
    followers: sum(runs.map((r) => r.pressure?.followers ?? 0)),
    sprungByHold: sum(runs.map((r) => r.pressure?.sprung_by_hold ?? 0)),
    stanceChanges,
    stancePerMin: minutes > 0 ? round2(stanceChanges / minutes) : 0,
    stanceByMember,
    damage: round2(sum(runs.map((r) => r.damage.taken))),
    absorbed: round2(sum(runs.map((r) => r.damage.absorbed))),
    rounds: sum(runs.map((r) => r.ammo.rounds)),
    infiniteAmmo: runs.some((r) => r.ammo.infinite),
    immortal: runs.some((r) => r.cheats.immortal),
    climbsUp: sum(runs.map((r) => r.ladder.climbs_up)),
    climbsDown: sum(runs.map((r) => r.ladder.climbs_down)),
    knockdowns: sum(runs.map((r) => r.knockdowns)),
    fights: sum(runs.map((r) => r.fights.count)),
    fightSeconds: round2(sum(runs.map((r) => r.fights.seconds))),
    results,
  };
}

const FORMATION_NAMES: Record<string, string> = {
  diamond: 'ромб', wedge: 'клин', pair: 'пара', column: 'колонна', line: 'линия', echelon_left: 'уступ влево', echelon_right: 'уступ вправо',
  wall_layout: 'у стены', hedgehog: 'ёж', blizzard: 'метель', custom: 'своё', '—': '—',
};
export const formationName = (key: string) => FORMATION_NAMES[key] ?? key;
export const KIND_NAMES: Record<ContactKind, string> = { patrol: 'патруль', ambush: 'засада', coordinated: 'скоординированная' };

const pairs = (o: Record<string, number>, name: (k: string) => string = (k) => k) =>
  Object.entries(o).sort((a, b) => b[1] - a[1]).map(([k, v]) => `${name(k)} ${v}`).join(', ') || '—';

/** The batch report as Markdown (Saved/Telemetry/combat_lab/<batch>/report.md; the coordinator reads it, ~40 lines). */
export function reportMarkdown(runs: RunRecord[], batch: { id?: string; preset?: string; jev?: string; level?: string } = {}): string {
  const a = aggregate(runs);
  const lines: string[] = [];
  lines.push(`# Combat lab bot report${batch.id ? ` — ${batch.id}` : ''}${batch.level ? ` (${batch.level})` : ''}`);
  lines.push('');
  lines.push(`Runs ${a.runs} (${pairs(a.results)}), ${a.minutes.toFixed(1)} game min${batch.preset ? `, preset ${batch.preset}` : ''}; immortal ${a.immortal ? 'on' : 'off'}, infinite ammo ${a.infiniteAmmo ? 'on' : 'off'}.`);
  lines.push('');
  lines.push('| Run | Result | Min | Route | Progress | Laps | Stuck | Contacts p/a/c | Unwatched | React s | Form. ch. | Wall hugs | Cover (s) | Stances (/min) | Dmg / absorbed | Rounds | Climbs | KD |');
  lines.push('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|');
  for (const r of runs.map(runRow)) {
    lines.push(`| ${r.run} | ${r.result} | ${r.minutes.toFixed(1)} | ${r.completed ? 'completed' : 'not completed'} | ${r.progressPct} % | ${r.laps} | ${r.stuck} | ${r.patrol}/${r.ambush}/${r.coordinated} | ${r.unwatched} | ${r.reactionMean.toFixed(1)} | ${r.formationChanges} | ${r.wallHugs} | ${r.coverTaken}/${r.coverOrders} (${r.coverSeconds.toFixed(0)}) | ${r.stanceChanges} (${r.stancePerMin.toFixed(1)}) | ${Math.round(r.damage)} / ${Math.round(r.absorbed)} | ${r.rounds} | ${r.climbs} | ${r.knockdowns} |`);
  }
  lines.push('');
  lines.push(`- Route: completed in ${a.routeCompleted} of ${a.runs} runs, mean progress ${a.progressMeanPct} %, ${a.lapsTotal} laps, ${a.distanceM.toFixed(0)} m walked, ${a.skipped} waypoints skipped, ${a.stuck} stuck events.`);
  for (const s of a.stuckByWaypoint.slice(0, 5)) lines.push(`  - stuck at ${s.label}: ${s.count}x (${s.reasons.join('; ')})`);
  for (const r of runs) {
    const w = worstStuck(r);
    if (w) lines.push(`  - run ${r.run}: ${w.skipped ? 'gave up' : 'stuck'} at #${w.waypoint} ${w.label} (${Math.round(w.position[0])}, ${Math.round(w.position[1])}) at ${w.time.toFixed(0)} s - ${w.reason}; last order: ${w.last_order || '-'}`);
  }
  if (a.drops > 0) {
    lines.push(`- Drops through roof / floor holes: ${a.drops} (fell physically ${a.dropsFell}, scripted after a failed drop ${a.dropsScripted}).`);
    for (const r of runs) for (const d of runDrops(r)) lines.push(`  - run ${r.run}: #${d.waypoint} ${d.label} via ${d.via || '-'} - ${d.scripted ? 'SCRIPTED (the drop failed)' : d.fell ? 'fell' : 'did not fall'} in ${d.seconds.toFixed(1)} s`);
  }
  lines.push(`- Contacts: ${a.contacts} (patrol ${a.byKind.patrol}, ambush ${a.byKind.ambush}, coordinated ${a.byKind.coordinated}); ambushes from an unwatched side ${a.unwatchedStrikes}; never looked at ${a.neverLooked}.`);
  lines.push(`- Time to the first look: mean ${a.reactionMean.toFixed(1)} s, median ${a.reactionMedian.toFixed(1)} s, max ${a.reactionMax.toFixed(1)} s. Formation at the contacts: ${pairs(a.contactFormations)}.`);
  lines.push(`- Formation changes: ${a.formationChanges}; entered: ${pairs(a.formationsEntered)}.`);
  lines.push(`- Cover taken at a wall in fights: ${a.coverTaken} of ${a.coverOrders} orders, ${a.coverSeconds.toFixed(0)} s at the wall.`);
  lines.push(`- Ambushes: relocation runs ${a.relocations} (spotted on the way ${a.runsSpotted}), no concealed route ${a.noRoute}x, became following patrols ${a.followers}, struck after the hold limit ${a.sprungByHold}.`);
  lines.push(`- Wall hugs: ${a.wallHugs}. Stance changes: ${a.stanceChanges} (${a.stancePerMin.toFixed(1)}/min); per operative: ${Object.entries(a.stanceByMember).map(([n, s]) => `${n} ${s.changes} (stand ${s.standing}, crouch ${s.crouching}, prone ${s.prone})`).join(', ') || '-'}.`);
  lines.push(`- Damage taken ${a.damage.toFixed(0)}${a.immortal ? `, absorbed by immortality ${a.absorbed.toFixed(0)}` : ''}; rounds ${a.infiniteAmmo ? 'that would have been spent' : 'spent'} ${a.rounds}; ladder climbs ${a.climbsUp} up / ${a.climbsDown} down; knockdowns ${a.knockdowns}; fights ${a.fights} (${a.fightSeconds.toFixed(0)} s).`);
  if (batch.jev) {
    lines.push('');
    lines.push('## Jev digest');
    lines.push('');
    lines.push('```');
    lines.push(batch.jev.trim());
    lines.push('```');
  }
  return `${lines.join('\n')}\n`;
}

// --- The run form (part C) -------------------------------------------------------------------------------------------------------------

export interface RunRequest { runs: number; minutes: number; parallel: number }
export const RUN_DEFAULTS: RunRequest = { runs: 4, minutes: 10, parallel: 2 };
export const RUN_LIMITS = { runs: [1, 20], minutes: [1, 60], parallel: [1, 4] } as const;

export function validateRunRequest(req: unknown): string[] {
  if (!isObj(req)) return ['ожидается { runs, minutes, parallel }'];
  const errors: string[] = [];
  for (const key of ['runs', 'minutes', 'parallel'] as const) {
    const v = req[key];
    const [lo, hi] = RUN_LIMITS[key];
    if (typeof v !== 'number' || !Number.isFinite(v)) errors.push(`${key}: нужно число`);
    else if (key !== 'minutes' && !Number.isInteger(v)) errors.push(`${key}: нужно целое`);
    else if (v < lo || v > hi) errors.push(`${key}: от ${lo} до ${hi}`);
  }
  return errors;
}

/** Real seconds a run may take before the batch kills it (the runs are fixed-step; a slow machine plays slower than real time). */
export function realTimeoutSec(minutes: number): number {
  return Math.round(minutes * 60 * 4 + 300);
}

/** A batch folder name: yyyyMMdd_HHmmss (UTC). */
export function batchId(now: Date): string {
  const p = (n: number) => String(n).padStart(2, '0');
  return `${now.getUTCFullYear()}${p(now.getUTCMonth() + 1)}${p(now.getUTCDate())}_${p(now.getUTCHours())}${p(now.getUTCMinutes())}${p(now.getUTCSeconds())}`;
}

/** A live snapshot of a running game (<report>.live.json, written every 5 game s). */
export interface LiveRun { run: number; time_sec: number; planned_sec: number; real_sec: number; waypoint: number; label: string; laps: number; progress: number; stuck: number; contacts: number; fight: boolean; formation: string; last_order: string }

/** Overall batch progress 0..1 from the finished runs and the live ones. */
export function batchProgress(totalRuns: number, finished: number, live: LiveRun[]): number {
  if (totalRuns <= 0) return 0;
  const partial = live.reduce((acc, l) => acc + (l.planned_sec > 0 ? Math.min(1, l.time_sec / l.planned_sec) : 0), 0);
  return Math.min(1, (finished + partial) / totalRuns);
}
