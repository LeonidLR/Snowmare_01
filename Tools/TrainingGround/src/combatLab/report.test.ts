// Combat lab part C (bot run report): parsing, per-run rows, the aggregate maths, the run form (node --test; `npm test`).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  aggregate, batchId, batchProgress, median, parseRunsJsonl, realTimeoutSec, reasonText, recordProblem, reportMarkdown, runRow,
  validateRunRequest, worstStuck, RUN_DEFAULTS,
} from './report.ts';
import type { ContactRow, RunRecord } from './report.ts';

function contact(kind: ContactRow['kind'], reaction: number, formation = 'diamond', unwatched = false): ContactRow {
  return { id: 1, kind, trigger: unwatched ? 'sprung' : 'seen', time: 10, position: [0, 0], formation, watched_at_start: !unwatched && reaction === 0, unwatched_strike: unwatched, reaction_sec: reaction };
}

/** A record shaped like CombatLabRules::MakeRunRecord's. */
function record(run: number, over: Partial<RunRecord> = {}): RunRecord {
  return {
    schema: 'codex.combatlab_run/1', kind: 'combat_lab', result: 'COMPLETE', run, bot_seed: run, pressure_seed: 1100 + run, level_id: 'L_FormationLabCombat',
    map: 'L_FormationLabCombat', timestamp_utc: '2026-11-01T12:00:00Z', duration_sec: 120, planned_sec: 120, real_sec: 80,
    cheats: { immortal: true, infinite_ammo: true },
    route: {
      name: 'loop', loop: true, waypoints: 21, lap_length_m: 453, completed: false, progress: 0.25, laps: 0, reached: 6, skipped: 0, distance_m: 210,
      furthest_index: 5, furthest_label: 'lane E north leg', current_index: 6, current_label: 'V2', last_order: 'move: move to #6', end_position: [-300, 4800],
      lap_list: [], stuck: [],
    },
    contacts: { total: 2, patrol: 1, ambush: 1, coordinated: 0, unwatched_strikes: 1, never_looked: 0, reaction_mean_sec: 1, reaction_max_sec: 2, list: [contact('patrol', 0), contact('ambush', 2, 'column', true)] },
    formations: { changes: 3, entered: { diamond: 2, column: 1, wall_layout: 1 }, seconds: { diamond: 80, column: 30, wall_layout: 10 } },
    wall_hugs: 1,
    cover: { orders: 2, taken: 1, seconds: 12.5 },
    stances: { changes: 4, per_min: 2, members: [{ name: 'Commander', changes: 4, standing: 2, crouching: 2, prone: 0, damage_taken: 0, damage_absorbed: 30, rounds: 40, climbs_up: 1, knockdowns: 0 }] },
    damage: { taken: 0, absorbed: 30 },
    ammo: { rounds: 40, infinite: true },
    ladder: { climbs_up: 1, climbs_down: 1 },
    knockdowns: 1,
    fights: { count: 2, seconds: 25 },
    pressure: { events: 1, spawned: 3, sprung_by_watch: 1, relocations: 2, sprung_by_hold: 1, runs_spotted: 1, no_route: 1, followers: 0 },
    ...over,
  };
}

test('parseRunsJsonl: records sorted by run, other / broken lines counted', () => {
  const text = [JSON.stringify(record(2)), '', 'not json', JSON.stringify({ schema: 'other' }), `﻿${JSON.stringify(record(1))}`].join('\r\n');
  const { runs, skipped } = parseRunsJsonl(text);
  assert.deepEqual(runs.map((r) => r.run), [1, 2]);
  assert.equal(skipped, 2);
  assert.match(recordProblem({ schema: 'codex.combatlab_run/1' }), /route/);
  assert.equal(recordProblem(record(1)), '');
});

test('route.drops: optional, aggregated (total / fell / scripted), shown in the Markdown; older records parse and show nothing', () => {
  const old = record(1);
  assert.equal(recordProblem(old), '');
  assert.equal(runRow(old).drops, 0);
  assert.equal(aggregate([old]).drops, 0);
  assert.ok(!reportMarkdown([old]).includes('Drops'));
  const withDrops = record(2, {
    route: { ...old.route, drops: [
      { waypoint: 7, label: 'hole roof 0', via: 'hole_roof_0', fell: true, scripted: false, seconds: 4.2 },
      { waypoint: 9, label: 'hole floor 2', via: 'hole_floor_2', fell: false, scripted: true, seconds: 12 },
      { waypoint: 11, label: 'hole roof 1', via: 'hole_roof_1', fell: true, scripted: false, seconds: 3 },
    ] },
  });
  const { runs } = parseRunsJsonl([JSON.stringify(old), JSON.stringify(withDrops)].join('\n'));
  assert.equal(runs.length, 2);
  const a = aggregate(runs);
  assert.deepEqual([a.drops, a.dropsFell, a.dropsScripted], [3, 2, 1]);
  const row = runRow(withDrops);
  assert.deepEqual([row.drops, row.dropsFell, row.dropsScripted], [3, 2, 1]);
  const md = reportMarkdown(runs, { id: 'b1', level: 'L_Gen_Depot_1' });
  assert.match(md, /^# Combat lab bot report — b1 \(L_Gen_Depot_1\)/);
  assert.match(md, /Drops through roof \/ floor holes: 3 \(fell physically 2, scripted after a failed drop 1\)/);
  assert.match(md, /run 2: #9 hole floor 2 via hole_floor_2 - SCRIPTED/);
  // A malformed field is ignored, not thrown.
  const broken = record(3, { route: { ...old.route, drops: 'nope' as unknown as undefined } });
  assert.equal(aggregate([broken]).drops, 0);
});

test('runRow: the route, where / why it got stuck', () => {
  const stuck = { time: 50, position: [123.4, -56.7] as [number, number], waypoint: 3, label: 'tower T_NW', last_order: 'climb: up Ladder_2', reason: 'action_failed:climb_up_timeout', skipped: true };
  const r = record(1, { route: { ...record(1).route, stuck: [{ ...stuck, skipped: false, reason: 'no_move', waypoint: 2, label: 'W_C_4' }, stuck] } });
  assert.equal(worstStuck(r)?.label, 'tower T_NW', 'the event that gave a waypoint up wins');
  const row = runRow(r);
  assert.equal(row.progressPct, 25);
  assert.equal(row.stuck, 2);
  assert.match(row.stuckWhere, /#3 tower T_NW \(123, -57\)/);
  assert.match(row.stuckWhere, /действие не удалось: не поднялся/);
  assert.match(row.stuckWhere, /climb: up Ladder_2/);
  assert.equal(runRow(record(2)).stuckWhere, '');
  assert.equal(reasonText('no_move'), 'стоит на месте');
  assert.equal(reasonText('order_failed:no_ground_hit'), 'приказ не отдан: нет земли под точкой');
  assert.equal(reasonText('weird'), 'weird');
});

test('aggregate: sums, reaction mean / median / max over looked-at contacts, formations, stances per minute', () => {
  const never = record(2, {
    route: { ...record(2).route, completed: true, progress: 1, laps: 1, skipped: 0 },
    contacts: { ...record(2).contacts, total: 3, list: [contact('coordinated', -1, 'wall_layout', true), contact('ambush', 4), contact('patrol', 6)] },
    formations: { changes: 1, entered: { column: 2 }, seconds: { column: 120 } },
    stances: { changes: 2, per_min: 1, members: [{ name: 'Commander', changes: 1, standing: 0, crouching: 1, prone: 0, damage_taken: 0, damage_absorbed: 0, rounds: 0, climbs_up: 0, knockdowns: 0 },
      { name: 'Engineer', changes: 1, standing: 0, crouching: 0, prone: 1, damage_taken: 0, damage_absorbed: 0, rounds: 0, climbs_up: 0, knockdowns: 0 }] },
    result: 'ABORTED',
  });
  const a = aggregate([record(1), never]);
  assert.equal(a.runs, 2);
  assert.equal(a.minutes, 4);
  assert.equal(a.routeCompleted, 1);
  assert.equal(a.progressMeanPct, 63, '(25 + 100) / 2 rounded');
  assert.equal(a.contacts, 5);
  assert.deepEqual(a.byKind, { patrol: 2, ambush: 2, coordinated: 1 });
  assert.equal(a.unwatchedStrikes, 2);
  assert.equal(a.neverLooked, 1);
  assert.equal(a.reactionMean, 3, '(0 + 2 + 4 + 6) / 4');
  assert.equal(a.reactionMedian, 3);
  assert.equal(a.reactionMax, 6);
  assert.deepEqual(a.contactFormations, { diamond: 3, column: 1, wall_layout: 1 });
  assert.equal(a.formationChanges, 4);
  assert.deepEqual(a.formationsEntered, { diamond: 2, column: 3, wall_layout: 1 });
  assert.equal(a.formationSeconds.column, 150);
  assert.equal(a.stanceChanges, 6);
  assert.equal(a.stancePerMin, 1.5, '6 changes / 4 min');
  assert.deepEqual(a.stanceByMember.Commander, { changes: 5, standing: 2, crouching: 3, prone: 0 });
  assert.equal(a.stanceByMember.Engineer.prone, 1);
  assert.equal(a.absorbed, 60);
  assert.equal(a.rounds, 80);
  assert.equal(a.climbsUp, 2);
  assert.equal(a.knockdowns, 2);
  assert.equal(a.wallHugs, 2);
  assert.deepEqual(a.results, { COMPLETE: 1, ABORTED: 1 });
  assert.equal(aggregate([]).runs, 0);
  assert.equal(aggregate([]).reactionMean, 0);
  assert.equal(median([3, 1, 2]), 2);
  assert.equal(median([]), 0);
});

test('stuck events grouped by waypoint, most first', () => {
  const s = (waypoint: number, label: string, reason: string) => ({ time: 1, position: [0, 0] as [number, number], waypoint, label, last_order: '', reason, skipped: false });
  const a = aggregate([
    record(1, { route: { ...record(1).route, stuck: [s(3, 'T_NW', 'no_move'), s(3, 'T_NW', 'leg_timeout')] } }),
    record(2, { route: { ...record(2).route, stuck: [s(3, 'T_NW', 'no_move'), s(7, 'V2', 'no_move')] } }),
  ]);
  assert.equal(a.stuck, 4);
  assert.deepEqual(a.stuckByWaypoint.map((x) => [x.label, x.count]), [['#3 T_NW', 3], ['#7 V2', 1]]);
  assert.deepEqual(a.stuckByWaypoint[0].reasons, ['стоит на месте', 'не доходит (обход / петляет)']);
});

test('reportMarkdown: a row per run, the aggregate lines, the Jev digest', () => {
  const md = reportMarkdown([record(1), record(2)], { id: '20261101_120000', preset: 'default', jev: 'line 1\nline 2\nline 3\n' });
  assert.match(md, /# Combat lab bot report — 20261101_120000/);
  assert.match(md, /\| 1 \| COMPLETE \| 2\.0 \| not completed \| 25 % \|/);
  assert.match(md, /Contacts: 4 \(patrol 2, ambush 2, coordinated 0\); ambushes from an unwatched side 2/);
  assert.match(md, /absorbed by immortality 60/);
  assert.match(md, /that would have been spent 80/);
  assert.match(md, /## Jev digest\n\n```\nline 1\nline 2\nline 3\n```/);
  assert.ok(md.split('\n').length < 60, 'short enough for the coordinator');
});

test('run form: defaults 4 x 10 min, 2 at a time; limits; timeout; batch id; progress', () => {
  assert.deepEqual(RUN_DEFAULTS, { runs: 4, minutes: 10, parallel: 2 });
  assert.deepEqual(validateRunRequest(RUN_DEFAULTS), []);
  assert.deepEqual(validateRunRequest({ runs: 2, minutes: 0.5 * 4, parallel: 2 }), []);
  assert.equal(validateRunRequest({ runs: 0, minutes: 10, parallel: 2 }).length, 1);
  assert.equal(validateRunRequest({ runs: 2.5, minutes: 10, parallel: 2 }).length, 1);
  assert.equal(validateRunRequest({ runs: 2, minutes: 61, parallel: 2 }).length, 1);
  assert.equal(validateRunRequest({ runs: 2, minutes: 10, parallel: 5 }).length, 1);
  assert.equal(validateRunRequest(null).length, 1);
  assert.equal(realTimeoutSec(10), 2700);
  assert.equal(batchId(new Date(Date.UTC(2026, 10, 1, 9, 5, 7))), '20261101_090507');
  const live = (time: number) => ({ run: 1, time_sec: time, planned_sec: 120, real_sec: 0, waypoint: 0, label: '', laps: 0, progress: 0, stuck: 0, contacts: 0, fight: false, formation: '', last_order: '' });
  assert.equal(batchProgress(4, 1, [live(60), live(120)]), 0.625);
  assert.equal(batchProgress(0, 0, []), 0);
  assert.equal(batchProgress(2, 2, [live(500)]), 1);
});

test('cover at a wall and the ambush counters (decisions 1, 2, 5 of 2026-11-01 night); old records without them read as 0', () => {
  const runs = [record(1), record(2)];
  const row = runRow(runs[0]);
  assert.equal(row.coverTaken, 1);
  assert.equal(row.coverOrders, 2);
  assert.equal(row.coverSeconds, 12.5);
  const a = aggregate(runs);
  assert.equal(a.coverTaken, 2);
  assert.equal(a.coverOrders, 4);
  assert.equal(a.coverSeconds, 25);
  assert.equal(a.relocations, 4);
  assert.equal(a.runsSpotted, 2);
  assert.equal(a.sprungByHold, 2);
  const md = reportMarkdown(runs);
  assert.match(md, /Cover taken at a wall in fights: 2 of 4 orders, 25 s/);
  assert.match(md, /relocation runs 4 \(spotted on the way 2\)/);
  assert.match(md, /\| 1\/2 \(13\) \|/);
  const old = record(3);
  delete old.cover;
  old.pressure = { events: 1, spawned: 3, sprung_by_watch: 1, relocations: 0 };
  assert.equal(runRow(old).coverTaken, 0);
  assert.equal(aggregate([old]).followers, 0);
});
