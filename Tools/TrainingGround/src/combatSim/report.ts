// Training Ground / combat simulator: the <= 40-line report.md of a batch (the only thing an agent reads) and the calibration table
// (sim vs an Unreal combat-lab batch's aggregate.json). Pure.

import type { Aggregate as UnrealAggregate } from '../combatLab/report.ts';
import type { CombatSimAggregate, CombatSimBatch, SpotSummary } from './schema.ts';

const pct = (v: number) => `${Math.round(v * 100)} %`;
const f1 = (v: number) => (Math.round(v * 10) / 10).toString();

/** The report lines (<= 40). */
export function renderReport(b: Omit<CombatSimBatch, 'report'>): string[] {
  const a = b.aggregate;
  const lines: string[] = [];
  const totalRuns = Math.max(1, a.runs);
  lines.push(`# Combat sim - ${b.layout.name} (${a.runs} seeds x ${f1(a.minutesPerRun)} min, ${b.layout.members} operatives; immortal ${b.cheats.immortal ? 'on' : 'off'}, infinite ammo ${b.cheats.infinite_ammo ? 'on' : 'off'}; ${Math.round(b.wall_ms / 1000)} s wall)`);
  lines.push(`per run: contacts ${f1(a.contacts)} (patrol ${f1(a.byKind.patrol)} / ambush ${f1(a.byKind.ambush)} / coordinated ${f1(a.byKind.coordinated)}; triggers seen ${f1(a.byTrigger.seen)} / engaged ${f1(a.byTrigger.engaged)} / sprung ${f1(a.byTrigger.sprung)}), unwatched strikes ${f1(a.unwatchedStrikes)}, first look ${f1(a.reactionMean)} s, first sight at ${a.firstSightMeanSec < 0 ? '-' : f1(a.firstSightMeanSec)} s`);
  lines.push(`per run: fights ${f1(a.fights)} (${f1(a.fightSeconds)} s, ${f1(a.fightSecondsPerFight)} s each), knockdowns ${f1(a.knockdowns)}, damage ${f1(a.damage)} / absorbed ${f1(a.absorbed)}, rounds ${f1(a.rounds)}, under fire ${f1(a.underFireSeconds)} s, unwatched hits ${f1(a.unwatchedHits)}, enemies ${f1(a.enemiesSpawned)} spawned / ${f1(a.enemiesKilled)} killed`);
  lines.push(`per run: formation changes ${f1(a.formationChanges)}, wall hugs ${f1(a.wallHugs)}, cover ${f1(a.coverTaken)} of ${f1(a.coverOrders)} orders (${f1(a.coverSeconds)} s at the wall), route ${f1(a.routeLaps)} laps / progress ${pct(a.routeProgress)} / ${f1(a.distanceM)} m`);
  const p = a.pressure;
  lines.push(`pressure per run: events ${f1(p.events)} (delayed by the cap ${f1(p.events_delayed)}), sprung by the watch ${f1(p.sprung_by_watch)} / after the hold ${f1(p.sprung_by_hold)}, relocations ${f1(p.relocations)} (spotted ${f1(p.runs_spotted)}), no route ${f1(p.no_route)}, followers ${f1(p.followers)}, recycled ${f1(p.recycled)}, lost (stuck / unreachable) ${f1(p.lost)}`);
  const eo = a.eventOutcomes;
  const totalEvents = Object.values(eo).reduce((s, v) => s + v, 0);
  lines.push(`events (${totalEvents} in all): patrol ${eo.patrol}, ambush ${eo.ambush}, coordinated ${eo.coordinated}; FALLBACKS: ambush -> patrol ${eo.ambush_fallback_patrol}, coordinated -> ambush ${eo.coordinated_fallback_ambush}, coordinated -> patrol ${eo.coordinated_fallback_patrol}; no spot ${eo.no_spot}, no room ${eo.no_room}`);
  const co = a.coordinatedOutcomes;
  const cm = a.coordinatedMissing;
  lines.push(`coordinated attempts: ok ${co.ok}, no room ${co.no_room}, no layout ${co.no_layout} (missing a concealed spot: flank left ${cm.flank_left}, flank right ${cm.flank_right}, front ${cm.front}, rear ${cm.rear}), unreachable ${co.unreachable}`);
  const sr = a.spotRejections;
  const inBand = sr.near_squad + sr.wall_clearance + sr.out_of_bounds + sr.off_nav + sr.not_concealed + sr.unreachable + sr.behind;
  lines.push(`hiding spots at ambush events: out of the band ${sr.too_far + sr.too_close} (too far ${sr.too_far}, too close ${sr.too_close}); in the band ${inBand} rejected - not concealed ${sr.not_concealed}, too near an operative ${sr.near_squad}, wall clearance ${sr.wall_clearance}, off the navmesh ${sr.off_nav}, out of bounds ${sr.out_of_bounds}, unreachable ${sr.unreachable}, behind ${sr.behind}`);
  const spots = b.spots;
  const byKind = (k: string) => spots.filter((s) => s.kind === k);
  const chosen = spots.filter((s) => s.chosen > 0);
  lines.push(`spots: ${spots.length} (authored ${byKind('authored').length}, doorway ${byKind('doorway').length}, wall end ${byKind('wall_end').length}); ever chosen ${chosen.length}; authored points in the open (no wall) ${spots.filter((s) => s.no_wall).length}; never in the band ${spots.filter((s) => s.main_reason === 'never_in_band').length}`);
  const top = [...chosen].sort((x, y) => y.chosen - x.chosen).slice(0, 6);
  if (top.length) lines.push(`most chosen: ${top.map((s) => `#${s.index} ${s.kind} (${Math.round(s.pos[0])}, ${Math.round(s.pos[1])}) x${s.chosen} - watch ${s.sprung_by_watch}, hold ${s.sprung_by_hold}, relocated ${s.relocated}, never struck ${s.never_struck}`).join('; ')}`);
  const reasons = new Map<string, number>();
  for (const s of spots) if (s.chosen === 0) reasons.set(s.main_reason, (reasons.get(s.main_reason) ?? 0) + 1);
  lines.push(`never chosen by main reason: ${[...reasons.entries()].sort((x, y) => y[1] - x[1]).map(([k, v]) => `${k} ${v}`).join(', ') || '-'}`);
  const zones = Object.entries(b.heat.zones).sort((x, y) => y[1] - x[1]).slice(0, 8);
  lines.push(`contacts by zone (all runs): ${zones.map(([z, n]) => `${z || 'outside'} ${n}`).join(', ') || '-'}`);
  const hot = b.heat.cells.slice(0, 5);
  lines.push(`hottest ${b.heat.cell_cm / 100} m cells: ${hot.map((c) => `(${Math.round(b.heat.origin[0] + (c.i + 0.5) * b.heat.cell_cm)}, ${Math.round(b.heat.origin[1] + (c.j + 0.5) * b.heat.cell_cm)}) x${c.count}`).join('; ') || '-'}`);
  lines.push(`contacts per run: ${a.contactsHistogram.map((n, k) => `${k}:${n}`).join(' ')}; ambush-type per run: ${a.ambushHistogram.map((n, k) => `${k}:${n}`).join(' ')}`);
  void totalRuns;
  return lines.slice(0, 40);
}

/** One row of the calibration table. */
export interface CalibrationRow {
  metric: string;
  /** Per Unreal-batch total (the sim's per-run mean x the Unreal batch's runs). */
  sim: number;
  unreal: number;
  ratio: number | null;
  /** Inside the accepted band [low, high] of the ratio. */
  ok: boolean;
  band: [number, number];
  main: boolean;
}

/** The main metrics are held to 0.6..1.6, the rest reported. */
const MAIN_BAND: [number, number] = [0.6, 1.6];
const WIDE_BAND: [number, number] = [0.3, 3];

/** Sim per-run means scaled to the Unreal batch's run count against its aggregate totals. */
export function calibrationTable(sim: CombatSimAggregate, unreal: UnrealAggregate): CalibrationRow[] {
  const runs = Math.max(1, unreal.runs);
  const rows: { metric: string; sim: number; unreal: number; main: boolean }[] = [
    { metric: 'contacts', sim: sim.contacts * runs, unreal: unreal.contacts, main: true },
    { metric: 'contacts.patrol', sim: sim.byKind.patrol * runs, unreal: unreal.byKind.patrol, main: true },
    { metric: 'contacts.ambush', sim: sim.byKind.ambush * runs, unreal: unreal.byKind.ambush, main: true },
    { metric: 'contacts.coordinated', sim: sim.byKind.coordinated * runs, unreal: unreal.byKind.coordinated, main: true },
    { metric: 'fights', sim: sim.fights * runs, unreal: unreal.fights, main: true },
    { metric: 'fight_seconds', sim: sim.fightSeconds * runs, unreal: unreal.fightSeconds, main: true },
    { metric: 'unwatched_strikes', sim: sim.unwatchedStrikes * runs, unreal: unreal.unwatchedStrikes, main: false },
    { metric: 'reaction_mean_sec', sim: sim.reactionMean, unreal: unreal.reactionMean, main: false },
    { metric: 'knockdowns', sim: sim.knockdowns * runs, unreal: unreal.knockdowns, main: false },
    { metric: 'damage_absorbed', sim: sim.absorbed * runs, unreal: unreal.absorbed, main: false },
    { metric: 'rounds', sim: sim.rounds * runs, unreal: unreal.rounds, main: false },
    { metric: 'formation_changes', sim: sim.formationChanges * runs, unreal: unreal.formationChanges, main: false },
    { metric: 'wall_hugs', sim: sim.wallHugs * runs, unreal: unreal.wallHugs, main: false },
    { metric: 'cover_orders', sim: sim.coverOrders * runs, unreal: unreal.coverOrders, main: false },
    { metric: 'cover_taken', sim: sim.coverTaken * runs, unreal: unreal.coverTaken, main: false },
    { metric: 'cover_seconds', sim: sim.coverSeconds * runs, unreal: unreal.coverSeconds, main: false },
    { metric: 'route_laps', sim: sim.routeLaps * runs, unreal: unreal.lapsTotal, main: false },
    { metric: 'relocations', sim: sim.pressure.relocations * runs, unreal: unreal.relocations, main: false },
    { metric: 'sprung_by_hold', sim: sim.pressure.sprung_by_hold * runs, unreal: unreal.sprungByHold, main: false },
  ];
  return rows.map((r) => {
    const band = r.main ? MAIN_BAND : WIDE_BAND;
    const ratio = r.unreal > 0 ? Math.round((r.sim / r.unreal) * 100) / 100 : r.sim > 0 ? null : 1;
    // both zero = a match; Unreal zero and sim small (< 1 per batch) = fine
    const ok = ratio === null ? r.sim < 1 : ratio >= band[0] && ratio <= band[1];
    return { metric: r.metric, sim: Math.round(r.sim * 100) / 100, unreal: Math.round(r.unreal * 100) / 100, ratio, ok, band, main: r.main };
  });
}

export function calibrationMarkdown(rows: CalibrationRow[], title: string): string {
  const lines = [`# ${title}`, '', '| Metric | Sim (per Unreal batch) | Unreal | Ratio sim / Unreal | Band | OK |', '|---|---|---|---|---|---|'];
  for (const r of rows) lines.push(`| ${r.metric}${r.main ? ' *' : ''} | ${r.sim} | ${r.unreal} | ${r.ratio === null ? 'inf' : r.ratio} | ${r.band[0]}..${r.band[1]} | ${r.ok ? 'yes' : 'NO'} |`);
  const main = rows.filter((r) => r.main);
  lines.push('', `Main metrics (*) within the band: ${main.filter((r) => r.ok).length} of ${main.length}; all: ${rows.filter((r) => r.ok).length} of ${rows.length}.`);
  return `${lines.join('\n')}\n`;
}

/** Spots that explain the ambush shortage, for the doc / the panel: the top reasons with counts. */
export function spotReasonSummary(spots: SpotSummary[]): { reason: string; count: number }[] {
  const m = new Map<string, number>();
  for (const s of spots) m.set(s.main_reason, (m.get(s.main_reason) ?? 0) + 1);
  return [...m.entries()].map(([reason, count]) => ({ reason, count })).sort((a, b) => b.count - a.count);
}
