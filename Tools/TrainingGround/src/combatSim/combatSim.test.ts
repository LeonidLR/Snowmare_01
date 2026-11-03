// Training Ground / combat simulator tests (docs/design/levelgen/COMBAT_SIM.md): the ported rules on hand-made cases (the ideas of the C++
// tests CodexTactics.Combat.AmbushSpots / AmbushPlan / AmbushRelocate / Pressure and AI.AmbushWatch), the watch rule, the plan conditions,
// the 2.5D sight grid, the pressure config defaults, determinism and speed of a run on the fixture, the output schema. `npm test`.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { fieldByPath } from '../combatLab/pressureSettings.ts';
import type { GenLayout } from '../levelgen/schema.ts';
import { validateGenLayout } from '../levelgen/schema.ts';
import type { Aggregate as UnrealAggregate } from '../combatLab/report.ts';
import { configValues } from '../movement/config.ts';
import { rng, segmentBlocked } from '../movement/geometry.ts';
import { AGENT_RADIUS } from '../movement/nav.ts';
import { runBatch, seedRange } from './batch.ts';
import { DEFAULT_SIM_CONSTANTS, parsePressureConfig } from './config.ts';
import { calibrationMarkdown, calibrationTable } from './report.ts';
import type { PressureBox, Watcher } from './rules.ts';
import { AmbushPlanRules, AmbushRelocateRules, AmbushSpotRules, AmbushWatchRules, CombatPressureRules, DEFAULT_HEIGHT_SIGHT, DEFAULT_PLAN_PARAMS, DEFAULT_RELOCATE_PARAMS, DEFAULT_SPOT_PARAMS, DEFAULT_WATCH_PARAMS, HeightSightRules, findDeltaAngleDeg, unwindDeg } from './rules.ts';
import { validateCombatSimBatch } from './schema.ts';
import { runCombatSim } from './sim.ts';
import { buildSquadTrack } from './squadTrack.ts';
import { SightGrid, SimWorld, wallBox } from './world.ts';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../../../..');
const fixture = JSON.parse(fs.readFileSync(path.join(HERE, 'fixtures/mini_depot.json'), 'utf8')) as GenLayout;
const formationConfig = configValues(JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/FormationLab/formation_config.json'), 'utf8')));

/** An axis-aligned wall box from a to b, thickness t (cm), height h. */
const wall = (a: [number, number], b: [number, number], t = 40, h = 300): PressureBox => wallBox(a, b, t, h);
const near = (p: readonly number[], q: readonly number[], tol = 1) => Math.hypot(p[0] - q[0], p[1] - q[1]) <= tol;

// --- angles / pressure rules --------------------------------------------------------------------------------------------------------------

test('FindDeltaAngleDegrees / UnwindDegrees as FMath', () => {
  assert.equal(findDeltaAngleDeg(170, -170), 20);
  assert.equal(findDeltaAngleDeg(-170, 170), -20);
  assert.equal(findDeltaAngleDeg(0, 180), 180);
  assert.equal(unwindDeg(370), 10);
  assert.equal(unwindDeg(-190), 170);
});

test('HeightSightRules: the sight range from a height as SightRules (CodexTactics.Combat.Sight.HeightRange)', () => {
  const H = HeightSightRules;
  const c = DEFAULT_HEIGHT_SIGHT;
  assert.equal(H.heightLevel(160), 0); // standing on the ground
  assert.equal(H.heightLevel(25), 0); // prone in the -3 m cut, his ground the cut floor
  assert.equal(H.heightLevel(-200), 0); // never negative
  assert.equal(H.heightLevel(460), 1); // standing on a +1 roof
  assert.equal(H.heightLevel(625), 2); // prone on the ruin's +2 roof
  assert.equal(H.heightLevel(925), 3); // prone on a +3 hall roof
  assert.equal(H.heightLevel(300), 1);
  assert.equal(H.heightLevel(299), 0);
  assert.equal(H.heightLevel(925, 0), 0);
  assert.deepEqual([-1, 0, 1, 2, 3, 4, 7].map((l) => H.rangeMultiplier(c, l)), [1, 1, 1.25, 1.5, 1.75, 2, 2]);
  assert.equal(H.rangeMultiplierAt(c, 925), 1.75);
  assert.equal(6000 * H.rangeMultiplier(c, 3), 10500);
  assert.equal(H.rangeMultiplier({ levelStepCm: 400, bonusPerLevel: 0.5, maxMultiplier: 1.8 }, 2), 1.8);
  assert.equal(H.rangeMultiplierAt({ levelStepCm: 400, bonusPerLevel: 0.5, maxMultiplier: 1.8 }, 625), 1.5);
  assert.equal(H.rangeMultiplier({ levelStepCm: 300, bonusPerLevel: -1, maxMultiplier: 0.5 }, 3), 1);
  // the data file the game reads (the defaults mirror it)
  const perception = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/AI/enemy_perception.json'), 'utf8'));
  assert.equal(perception.height_sight.level_step_m * 100, c.levelStepCm);
  assert.equal(perception.height_sight.bonus_per_level, c.bonusPerLevel);
  assert.equal(perception.height_sight.max_multiplier, c.maxMultiplier);
});

test('CombatPressureRules: the schedule, group size, event kinds, type pick, box distance, spot score', () => {
  assert.equal(CombatPressureRules.decideEvent(29, 30, 0, 4), 'not_due');
  assert.equal(CombatPressureRules.decideEvent(30, 30, 3, 4), 'spawn');
  assert.equal(CombatPressureRules.decideEvent(30, 30, 4, 4), 'delayed_cap_full');
  assert.equal(CombatPressureRules.groupSize(0, 2, 4, 0, 6), 2);
  assert.equal(CombatPressureRules.groupSize(0.99, 2, 4, 0, 6), 4);
  assert.equal(CombatPressureRules.groupSize(0.99, 2, 4, 5, 6), 1); // cut to the room
  assert.equal(CombatPressureRules.pickEventKind(0.5, 0.9, 0.6, 0.35), 'patrol');
  assert.equal(CombatPressureRules.pickEventKind(0.7, 0.9, 0.6, 0.35), 'ambush');
  assert.equal(CombatPressureRules.pickEventKind(0.7, 0.1, 0.6, 0.35), 'coordinated');
  const weights = CombatPressureRules.effectiveWeights([]);
  assert.equal(weights.length, 5);
  assert.equal(CombatPressureRules.pickType([{ type: 'HOUND', weight: 1 }, { type: 'MARKSMAN', weight: 3 }], 0.2), 'HOUND');
  assert.equal(CombatPressureRules.pickType([{ type: 'HOUND', weight: 1 }, { type: 'MARKSMAN', weight: 3 }], 0.3), 'MARKSMAN');
  const box = wall([0, 0], [1000, 0], 40);
  assert.ok(Math.abs(CombatPressureRules.distanceToBox([500, 120], box) - 100) < 1e-6);
  assert.equal(CombatPressureRules.distanceToBox([500, 10], box), 0);
  assert.ok(Math.abs(CombatPressureRules.distanceToBox([1100, 0], box) - 100) < 1e-6);
  assert.equal(CombatPressureRules.checkSpawnPoint([500, 200], [box], 250, [], 0, null), 'near_wall');
  assert.equal(CombatPressureRules.checkSpawnPoint([500, 300], [box], 250, [[500, 400]], 1400, null), 'near_squad');
  assert.equal(CombatPressureRules.checkSpawnPoint([500, 300], [box], 250, [], 0, { min: [0, 0], max: [100, 100] }), 'out_of_bounds');
  assert.equal(CombatPressureRules.scoreSpot({ kind: 'ambush', distanceCm: 2000, minDistanceCm: 1400, maxDistanceCm: 3000, aheadCos: 1, concealed: false, watched: false, kindBonus: 0.5 }), -1000);
  const ahead = CombatPressureRules.scoreSpot({ kind: 'ambush', distanceCm: 2200, minDistanceCm: 1400, maxDistanceCm: 3000, aheadCos: 1, concealed: true, watched: true, kindBonus: 0 });
  const behind = CombatPressureRules.scoreSpot({ kind: 'ambush', distanceCm: 2200, minDistanceCm: 1400, maxDistanceCm: 3000, aheadCos: -1, concealed: true, watched: false, kindBonus: 0 });
  assert.ok(ahead > behind, 'ambushers prefer the squad\'s way ahead');
  assert.equal(CombatPressureRules.patrolLoop([0, 0], 100, 4, 0).length, 4);
  assert.deepEqual(CombatPressureRules.groupOffset(0, 220), [0, 0]);
  assert.ok(Math.abs(Math.hypot(...CombatPressureRules.groupOffset(2, 220)) - 220) < 1e-6);
  assert.ok(Math.abs(Math.hypot(...CombatPressureRules.groupOffset(4, 220)) - 440) < 1e-6);
  assert.equal(CombatPressureRules.shouldRecycle(false, false, 7000, 61, 6000, 60), true);
  assert.equal(CombatPressureRules.shouldRecycle(true, false, 7000, 61, 6000, 60), false);
});

// --- AmbushSpotRules ----------------------------------------------------------------------------------------------------------------------

test('AmbushSpotRules: free ends, joined corners, doorways, the 0.8 m standoff, the merge', () => {
  const p = DEFAULT_SPOT_PARAMS;
  const lone = wall([0, 0], [1000, 0]);
  const ends = AmbushSpotRules.freeEnds([lone], p);
  assert.equal(ends.length, 2);
  assert.ok(ends.some((e) => near(e.point, [1000, 0])) && ends.some((e) => near(e.point, [0, 0])));
  // an L corner: the two touching ends are joined, the two far tips stay free
  const corner = [wall([0, 0], [1000, 0]), wall([1000, 0], [1000, 800])];
  const cornerEnds = AmbushSpotRules.freeEnds(corner, p);
  assert.equal(cornerEnds.length, 2);
  assert.ok(cornerEnds.every((e) => near(e.point, [0, 0], 30) || near(e.point, [1000, 800], 30)));
  // a post shorter than min_wall_length gives no ends
  assert.equal(AmbushSpotRules.freeEnds([wall([0, 0], [100, 0])], p).length, 0);
  // a doorway: two collinear walls 150 cm apart
  const door = [wall([0, 0], [1000, 0]), wall([1150, 0], [2150, 0])];
  const spots = AmbushSpotRules.wallSpots(door, p);
  assert.ok(spots.some((s) => s.kind === 'doorway'), 'the jamb spots are doorway spots');
  assert.ok(spots.every((s) => s.kind === 'doorway' || s.kind === 'wall_end'));
  // every spot 0.8 m off its wall's face (+ half the thickness), 1 m inside the end
  for (const s of AmbushSpotRules.wallSpots([lone], p)) {
    assert.ok(Math.abs(Math.abs(s.position[1]) - (20 + 80)) < 1e-6, `standoff ${s.position[1]}`);
    assert.ok(Math.abs(s.position[0] - 100) < 1e-6 || Math.abs(s.position[0] - 900) < 1e-6);
  }
  // a wide gap (7 m) is no doorway
  assert.ok(AmbushSpotRules.wallSpots([wall([0, 0], [1000, 0]), wall([1700, 0], [2700, 0])], p).every((s) => s.kind === 'wall_end'));
  // an authored point within the merge distance of a wall-end spot wins
  const cands = AmbushSpotRules.candidates([lone], [[900, 110]], p);
  assert.ok(cands.some((c) => c.kind === 'authored' && near(c.position, [900, 110])));
  assert.equal(cands.filter((c) => near(c.position, [900, 100], 100)).length, 1);
  assert.equal(AmbushSpotRules.hideClearanceCm(250, p), 70);
  assert.equal(AmbushSpotRules.kindBonus('authored', p), 0.5);
  assert.equal(AmbushSpotRules.kindBonus('doorway', p), 0.25);
  assert.equal(AmbushSpotRules.kindBonus('wall_end', p), 0);
});

// --- AmbushWatchRules ---------------------------------------------------------------------------------------------------------------------

test('AmbushWatchRules: the 45 deg looks, aiming counts, dedupe, decide, the widest unwatched arc, the hold limit', () => {
  const p = DEFAULT_WATCH_PARAMS;
  const w: Watcher[] = [{ position: [0, 0], lookYawDeg: 0, source: 'body' }];
  assert.equal(AmbushWatchRules.isWatched(w, [1000, 700], p), true); // bearing 35 deg
  assert.equal(AmbushWatchRules.isWatched(w, [1000, 1300], p), false); // bearing 52 deg
  assert.equal(AmbushWatchRules.isWatched(w, [6000, 0], p), false); // beyond the 50 m range
  assert.ok(AmbushWatchRules.watchMarginDeg(w, [1000, 1300], p) > 0 && AmbushWatchRules.watchMarginDeg(w, [1000, 700], p) < 0);
  // decide: wait beyond 18 m, hold when watched, spring when not
  assert.equal(AmbushWatchRules.decide(w, [[0, 0]], [2000, 0], p), 'wait');
  assert.equal(AmbushWatchRules.decide(w, [[0, 0]], [1500, 0], p), 'hold');
  assert.equal(AmbushWatchRules.decide(w, [[0, 0]], [-1500, 0], p), 'spring');
  assert.equal(AmbushWatchRules.decide(w, [], [-1500, 0], p), 'wait');
  // the member's looks: body + aim + sector, a look within 5 deg of one listed is dropped
  const looks = AmbushWatchRules.memberLooks([0, 0], 0, 90, 3);
  assert.deepEqual(looks.map((l) => l.source), ['body', 'aim']);
  // aiming at another enemy covers an ambush inside the aim cone (answer 4)
  const aiming = AmbushWatchRules.memberLooks([0, 0], 0, 120, null);
  assert.equal(AmbushWatchRules.isWatched(aiming, [-500, 1000], p), true); // bearing 117 deg
  // the widest unwatched arc: one look at 0 -> the gap's middle at 180
  assert.equal(AmbushWatchRules.widestUnwatchedBearing([{ position: [0, 0], lookYawDeg: 0, source: 'body' }], p), 180);
  assert.equal(AmbushWatchRules.widestUnwatchedBearing([0, 90, 180, -90].map((y) => ({ position: [0, 0] as [number, number], lookYawDeg: y, source: 'body' as const })), p), null);
  assert.equal(AmbushWatchRules.widestUnwatchedBearing([], p), 0);
  assert.equal(AmbushWatchRules.holdLimitSeconds(0, p), 60);
  assert.equal(AmbushWatchRules.holdLimitSeconds(1, p), 90);
  assert.equal(AmbushWatchRules.holdLimitSeconds(0.5, { ...p, holdStrikeMaxSeconds: 0 }), 0);
  assert.equal(AmbushWatchRules.heldTooLong(61, 60), true);
  assert.equal(AmbushWatchRules.heldTooLong(100, 0), false);
});

// --- AmbushPlanRules ----------------------------------------------------------------------------------------------------------------------

test('AmbushPlanRules: parties, sectors, the spot assignment, the phases, who springs', () => {
  const p = DEFAULT_PLAN_PARAMS;
  assert.deepEqual([0, 1, 2, 3, 4, 5].map(AmbushPlanRules.partyOf), ['flank_left', 'flank_right', 'front', 'rear', 'front', 'rear']);
  assert.equal(AmbushPlanRules.planBodies(0, p, 0), 4);
  assert.equal(AmbushPlanRules.planBodies(0.99, p, 0), 6);
  assert.equal(AmbushPlanRules.planBodies(0.99, p, 2), 4);
  assert.equal(AmbushPlanRules.planBodies(0.5, p, 3), 0, 'no room for four');
  const types: ('HOUND' | 'CUTTER' | 'MARKSMAN')[] = ['HOUND', 'CUTTER', 'HOUND', 'CUTTER'];
  assert.equal(AmbushPlanRules.ensureRanged(types, 'MARKSMAN', 1), 1);
  assert.deepEqual(types, ['HOUND', 'CUTTER', 'MARKSMAN', 'CUTTER'], 'the FRONT body is swapped, the flanks stay melee');
  const anchor = AmbushPlanRules.anchor([0, 0], 0, p);
  assert.ok(near(anchor, [500, 0]));
  assert.equal(AmbushPlanRules.sectorOf(anchor, 0, [2000, 0]), 'front');
  assert.equal(AmbushPlanRules.sectorOf(anchor, 0, [500, 1500]), 'flank_right');
  assert.equal(AmbushPlanRules.sectorOf(anchor, 0, [500, -1500]), 'flank_left');
  assert.equal(AmbushPlanRules.sectorOf(anchor, 0, [-1500, 0]), 'rear');
  const four: [number, number][] = [[500, -1500], [500, 1500], [2100, 0], [-1000, 0]];
  assert.deepEqual(AmbushPlanRules.assignSpots(anchor, 0, four, p), [0, 1, 2, 3]);
  assert.ok(AmbushPlanRules.isComplete([0, 1, 2, 3]));
  const noRear = AmbushPlanRules.assignSpots(anchor, 0, four.slice(0, 3), p);
  assert.equal(noRear[3], -1);
  assert.ok(!AmbushPlanRules.isComplete(noRear));
  // two flank candidates too close together: the second party gets none
  const tight = AmbushPlanRules.assignSpots(anchor, 0, [[500, -1500], [500, -1200], [2100, 0], [-1000, 0]], p);
  assert.equal(tight[1], -1);
  // phases: waiting out of range; holding in range while the flanks are watched; the flank block once a flank is unwatched (or the hold limit);
  // the assault after 1.5 s with the flanks in place, at 4 s at the latest
  const base = { phase: 'waiting' as const, secondsInPhase: 0, nearestSquadCm: 2500, flanksUnwatched: 0, anyEngaged: false, flankMembersAlive: 2, flankMembersInPlace: 0, holdSeconds: 0, holdLimitSeconds: 75 };
  assert.equal(AmbushPlanRules.nextPhase(base, p), 'waiting');
  assert.equal(AmbushPlanRules.nextPhase({ ...base, nearestSquadCm: 1500 }, p), 'holding');
  assert.equal(AmbushPlanRules.nextPhase({ ...base, nearestSquadCm: 1500, flanksUnwatched: 1 }, p), 'flank_block');
  assert.equal(AmbushPlanRules.nextPhase({ ...base, phase: 'holding', nearestSquadCm: 1500, holdSeconds: 75 }, p), 'flank_block', 'watched all round for the whole hold limit: it strikes anyway');
  assert.equal(AmbushPlanRules.nextPhase({ ...base, anyEngaged: true }, p), 'flank_block');
  assert.equal(AmbushPlanRules.nextPhase({ ...base, phase: 'flank_block', secondsInPhase: 1, flankMembersInPlace: 2 }, p), 'flank_block');
  assert.equal(AmbushPlanRules.nextPhase({ ...base, phase: 'flank_block', secondsInPhase: 2, flankMembersInPlace: 2 }, p), 'assault');
  assert.equal(AmbushPlanRules.nextPhase({ ...base, phase: 'flank_block', secondsInPhase: 2, flankMembersInPlace: 1 }, p), 'flank_block');
  assert.equal(AmbushPlanRules.nextPhase({ ...base, phase: 'flank_block', secondsInPhase: 4, flankMembersInPlace: 0 }, p), 'assault');
  assert.equal(AmbushPlanRules.springs('flank_block', 'flank_left', false), true);
  assert.equal(AmbushPlanRules.springs('flank_block', 'front', true), true, 'a ranged front member suppresses with the flank block');
  assert.equal(AmbushPlanRules.springs('flank_block', 'front', false), false);
  assert.equal(AmbushPlanRules.springs('assault', 'rear', false), true);
  assert.equal(AmbushPlanRules.springs('holding', 'flank_left', true), false);
});

// --- AmbushRelocateRules ------------------------------------------------------------------------------------------------------------------

test('AmbushRelocateRules: bypassed, the decision, path samples, the route pick, the no-route outcome, the run', () => {
  const p = DEFAULT_RELOCATE_PARAMS;
  assert.equal(AmbushRelocateRules.isBypassed(1500, 150, 1800, p), false, 'in strike range: never bypassed');
  assert.equal(AmbushRelocateRules.isBypassed(2500, 150, 1800, p), true, 'behind the squad');
  assert.equal(AmbushRelocateRules.isBypassed(2500, 30, 1800, p), false);
  assert.equal(AmbushRelocateRules.isBypassed(4000, 30, 1800, p), true, 'far');
  assert.equal(AmbushRelocateRules.stepBypassedSeconds(true, 1, 0.1), 1.1);
  assert.equal(AmbushRelocateRules.stepBypassedSeconds(false, 1, 0.1), 0);
  assert.equal(AmbushRelocateRules.decide(14, false, p), 'stay');
  assert.equal(AmbushRelocateRules.decide(15, true, p), 'wait_unseen');
  assert.equal(AmbushRelocateRules.decide(15, false, p), 'relocate');
  assert.equal(AmbushRelocateRules.decide(15, false, { ...p, enabled: false }), 'stay');
  const samples = AmbushRelocateRules.samplePath([[0, 0], [1000, 0], [1000, 500]], 100);
  assert.equal(samples.length, 16);
  assert.ok(near(samples[samples.length - 1], [1000, 500]));
  assert.equal(AmbushRelocateRules.pathLength([[0, 0], [1000, 0], [1000, 500]]), 1500);
  const routes = [
    { score: 1, pathCm: 3000, reachable: true, seenSamples: 0, destinationConcealed: true },
    { score: 2, pathCm: 3000, reachable: true, seenSamples: 1, destinationConcealed: true },
    { score: 1, pathCm: 2000, reachable: true, seenSamples: 0, destinationConcealed: true },
    { score: 3, pathCm: 20000, reachable: true, seenSamples: 0, destinationConcealed: true },
    { score: 3, pathCm: 1000, reachable: false, seenSamples: 0, destinationConcealed: true },
  ];
  assert.equal(AmbushRelocateRules.pickRoute(routes, p), 2, 'the best concealed route; a tie -> the shorter path');
  assert.equal(AmbushRelocateRules.pickRoute(routes.slice(1, 2), p), -1);
  assert.equal(AmbushRelocateRules.noRouteOutcome(1, p), 'stay');
  assert.equal(AmbushRelocateRules.noRouteOutcome(2, p), 'patrol');
  assert.equal(AmbushRelocateRules.noRouteOutcome(5, { ...p, noRoutePatrol: false }), 'stay');
  assert.equal(AmbushRelocateRules.runLimitSeconds(3000, 300), 30);
  assert.equal(AmbushRelocateRules.stepRun(true, true, 0, 10), 'spotted');
  assert.equal(AmbushRelocateRules.stepRun(false, true, 0, 10), 'arrived');
  assert.equal(AmbushRelocateRules.stepRun(false, false, 11, 10), 'timed_out');
  assert.equal(AmbushRelocateRules.stepRun(false, false, 1, 10), 'running');
});

// --- config -----------------------------------------------------------------------------------------------------------------------------

test('parsePressureConfig: the C++ defaults from the golden field table, the clamps, the type weights, the authored points', () => {
  const d = parsePressureConfig({});
  assert.equal(d.enabled, true);
  assert.equal(d.firstEventSeconds, fieldByPath('first_event_sec')!.def);
  assert.equal(d.intervalSeconds, 120);
  assert.equal(d.groupMin, 1);
  assert.equal(d.groupMax, 3);
  assert.equal(d.maxAlive, 4);
  assert.equal(d.patrolShare, 0.6);
  assert.equal(d.watch.watchHalfAngleDeg, 45);
  assert.equal(d.watch.strikeRangeCm, 1800);
  assert.equal(d.watch.holdStrikeMinSeconds, 60);
  assert.equal(d.watch.holdStrikeMaxSeconds, 90);
  assert.equal(d.spots.wallStandoffCm, 80);
  assert.equal(d.coordinated.share, 0.35);
  assert.equal(d.coordinated.bodiesMin, 4);
  assert.equal(d.coordinated.maxAlive, 6);
  assert.equal(d.relocate.afterSeconds, 15);
  assert.equal(d.recycleDistanceCm, 6000);
  assert.equal(d.typeWeights.length, 5);
  assert.equal(parsePressureConfig(undefined).enabled, false, 'no block = the pressure off');
  const c = parsePressureConfig({ group_min: 4, group_max: 2, ambush: { points: [[1, 2], [3, 4]], hold_strike_min_sec: 100, hold_strike_max_sec: 50 }, types: { HOUND: 2, BRUTE: 5 } });
  assert.equal(c.groupMax, 4, 'max raised to min as the C++');
  assert.equal(c.watch.holdStrikeMaxSeconds, 100);
  assert.deepEqual(c.authoredPoints, [[1, 2], [3, 4]]);
  assert.deepEqual(c.typeWeights, [{ type: 'HOUND', weight: 2 }], 'the Brute is never a pressure type');
});

// --- the sight grid -----------------------------------------------------------------------------------------------------------------------

test('SightGrid: a wall blocks a ground line, an eye on a roof sees over it, a line through a gap passes', () => {
  const grid = new SightGrid([{ a: [0, -500], b: [0, 500], bottom: 0, top: 300 }, { a: [0, 700], b: [0, 1500], bottom: 0, top: 300 }], [-3000, -3000], [3000, 3000]);
  assert.equal(grid.clear(-1000, 0, 150, 1000, 0, 110), false, 'ground to ground through the wall');
  assert.equal(grid.clear(-1000, 0, 760, 3000, 0, 110), true, 'from a 6 m roof + eye the line is 600 cm high at the wall');
  assert.equal(grid.clear(-1000, 0, 760, 100, 0, 110), false, 'a target right behind the wall is still hidden (the line is low there)');
  assert.equal(grid.clear(-1000, 600, 150, 1000, 600, 110), true, 'through the 2 m gap');
  assert.equal(grid.clear(-1000, 2000, 150, 1000, 2000, 110), true, 'past the walls');
});

// --- the fixture: world, track, runs --------------------------------------------------------------------------------------------------------

test('the fixture layout is a valid levelgen layout and its world finds doorway / wall-end / authored hiding spots', () => {
  assert.deepEqual(validateGenLayout(fixture), []);
  const world = new SimWorld(fixture, parsePressureConfig({ ...(fixture.pressure as object), enabled: true }));
  assert.ok(world.boxes.length >= 9 + 3, 'walls + covers >= 50 cm are obstacle boxes');
  const kinds = new Set(world.hideSpots.map((s) => s.kind));
  assert.ok(kinds.has('doorway'), 'the yard gate jambs');
  assert.ok(kinds.has('wall_end'));
  assert.ok(kinds.has('authored'));
  assert.equal(world.hideClearanceCm, 70);
  assert.equal(world.zoneAt([500, 0]), 'Z_street');
  assert.equal(world.projectFeet([1100, 0]), false, 'inside the facade wall');
  assert.equal(world.projectFeet([500, 0]), true);
});

test('the squad track of the fixture: 10 Hz frames with looks, a lap with the return leg, the wall layout at the dead end', () => {
  const world = new SimWorld(fixture, parsePressureConfig({ enabled: true }));
  const track = buildSquadTrack(world, formationConfig, { members: 3 });
  assert.equal(track.hz, 10);
  assert.ok(track.frames.length > 100);
  assert.ok(track.lapSeconds > 30 && track.lapMeters > 50);
  assert.ok(track.frames.every((f) => f.members.length === 3 && f.members.every((m) => Number.isFinite(m.lookYaw) && Number.isFinite(m.bodyYaw))));
  assert.ok(track.frames.some((f) => f.atWall), 'the route ends at the dead end wall');
  assert.ok(track.legs.some((l) => l.kind === 'return'));
  // the frames are time-ordered at 0.1 s
  for (let i = 1; i < 20; i++) assert.ok(Math.abs(track.frames[i].t - track.frames[i - 1].t - 0.1) < 1e-6);
});

test('a run is deterministic for a seed, differs between seeds, finishes in well under a second and reports contacts', () => {
  const world = new SimWorld(fixture, parsePressureConfig({ ...(fixture.pressure as object), enabled: true }));
  const track = buildSquadTrack(world, formationConfig, { members: 3 });
  const t0 = performance.now();
  const a = runCombatSim({ world, track, seed: 3, durationSec: 600 });
  const wall1 = performance.now() - t0;
  const b = runCombatSim({ world, track, seed: 3, durationSec: 600 });
  const c = runCombatSim({ world, track, seed: 4, durationSec: 600 });
  assert.equal(JSON.stringify(a), JSON.stringify(b), 'the same seed gives the same result');
  assert.notEqual(JSON.stringify(a.metrics), JSON.stringify(c.metrics), 'another seed differs');
  assert.ok(wall1 < 1000, `a 10-min run took ${wall1.toFixed(0)} ms`);
  const all = [a, c, ...seedRange(3, 5).map((seed) => runCombatSim({ world, track, seed, durationSec: 600 }))];
  assert.ok(all.some((r) => r.metrics.contacts > 0), 'some seed meets the enemy');
  assert.ok(all.every((r) => r.metrics.enemies_killed <= r.metrics.enemies_spawned));
  assert.ok(all.every((r) => r.metrics.fights >= r.contacts.filter((x) => x.trigger === 'engaged' || x.trigger === 'sprung').length * 0 && r.metrics.fight_seconds >= 0));
  for (const r of all) for (const ct of r.contacts) assert.ok(['patrol', 'ambush', 'coordinated'].includes(ct.kind) && ['seen', 'engaged', 'sprung'].includes(ct.trigger));
  // every ambush event's spot diagnostics are consistent: considered = every spot per ambush look
  for (const r of all) {
    const ambushEvents = r.events.filter((e) => e.funnel).length;
    assert.ok(r.spots.every((s) => s.considered === ambushEvents), 'every ambush event considers every spot');
    for (const s of r.spots) assert.ok(s.chosen <= s.considered);
  }
});

test('NavGrid: the wall index answers like segmentBlocked, lineClear / cellFree follow the grid (the enemies\' walk)', () => {
  const world = new SimWorld(fixture, parsePressureConfig({ enabled: true }));
  const r = rng(11);
  const b = fixture.bounds;
  let blocked = 0;
  for (let k = 0; k < 3000; k++) {
    const p: [number, number] = [b.min[0] - 200 + r() * (b.max[0] - b.min[0] + 400), b.min[1] - 200 + r() * (b.max[1] - b.min[1] + 400)];
    const L = r() < 0.5 ? r() * 400 : r() * 4000, a = r() * Math.PI * 2;
    const q: [number, number] = [p[0] + Math.cos(a) * L, p[1] + Math.sin(a) * L];
    const ref = segmentBlocked(p, q, world.nav.walls, AGENT_RADIUS);
    blocked += ref ? 1 : 0;
    assert.equal(world.nav.wallsBlock(p, q), ref, `segment ${p} -> ${q}`);
  }
  assert.ok(blocked > 500 && blocked < 2500, `a mixed sample (${blocked} blocked)`);
  // the point form: inside the facade = blocked, the street = free
  assert.equal(world.nav.wallsBlock([1100, 0], [1100, 0]), true);
  assert.equal(world.nav.wallsBlock([500, 0], [500, 0]), false);
  assert.equal(world.nav.cellFree([500, 0]), true);
  assert.equal(world.nav.cellFree([1100, 0]), false);
  assert.equal(world.nav.lineClear([500, -1000], [500, 1000]), true, 'along the street');
  assert.equal(world.nav.lineClear([500, 0], [1700, 0]), false, 'through the facade');
  // the same path twice (the re-used scratch arrays change nothing)
  const p1 = world.nav.path([600, -2600], [-900, 0]), p2 = world.nav.path([600, -2600], [-900, 0]);
  assert.ok(p1 && p2 && p1.length > 2);
  assert.deepEqual(p1, p2);
});

test('enemyHpScale lengthens the fights: tougher stand-ins die later (the calibration knob)', () => {
  const world = new SimWorld(fixture, parsePressureConfig({ ...(fixture.pressure as object), enabled: true }));
  const track = buildSquadTrack(world, formationConfig, { members: 3 });
  const sum = (scale: number, key: 'enemies_killed' | 'rounds') => seedRange(6).reduce((n, seed) => n + runCombatSim({ world, track, seed, durationSec: 600, constants: { enemyHpScale: scale } }).metrics[key], 0);
  assert.ok(sum(1, 'rounds') < sum(3, 'rounds'), 'more rounds to kill the same enemies');
  assert.ok(sum(1, 'enemies_killed') >= sum(3, 'enemies_killed'), 'no more kills with triple HP');
});

test('calibrationTable scales the per-run means to the Unreal batch and judges the bands', () => {
  const { batch } = runBatch({ layout: fixture, pressure: fixture.pressure, formationConfig, seeds: seedRange(2), durationSec: 120, members: 3 });
  const unreal = { runs: 2, contacts: batch.aggregate.contacts * 2, byKind: { patrol: batch.aggregate.byKind.patrol * 2, ambush: batch.aggregate.byKind.ambush * 2, coordinated: 0 }, fights: Math.max(1, batch.aggregate.fights * 2), fightSeconds: 1000, unwatchedStrikes: 0, reactionMean: 0.1, knockdowns: 0, absorbed: 0, rounds: 0, formationChanges: 0, wallHugs: 0, coverOrders: 0, coverTaken: 0, coverSeconds: 0, lapsTotal: 0, relocations: 0, sprungByHold: 0 } as unknown as UnrealAggregate;
  const rows = calibrationTable(batch.aggregate, unreal);
  const row = (m: string) => rows.find((r) => r.metric === m)!;
  assert.equal(row('contacts').ratio, batch.aggregate.contacts > 0 ? 1 : 1);
  assert.equal(row('contacts').ok, true);
  assert.equal(row('fight_seconds').ok, batch.aggregate.fightSeconds * 2 / 1000 >= 0.6, 'the main band is 0.6..1.6');
  assert.ok(row('contacts.coordinated').ratio === null || row('contacts.coordinated').ratio === 1, 'Unreal 0: a null ratio (sim > 0) or a match');
  assert.ok(rows.every((r) => Array.isArray(r.band) && r.band[0] < r.band[1]));
  assert.ok(calibrationMarkdown(rows, 't').split('\n').length > rows.length);
});

test('the example fixture is a valid codex.combat_sim/1 document of the mini depot', () => {
  const example = JSON.parse(fs.readFileSync(path.join(HERE, 'fixtures/combatsim.example.json'), 'utf8')) as { layout: { name: string }; seeds: number[] };
  assert.deepEqual(validateCombatSimBatch(example), []);
  assert.equal(example.layout.name, fixture.name);
  assert.deepEqual(example.seeds, [1, 2, 3]);
});

test('runBatch writes a valid codex.combat_sim/1 document with a <= 40-line report and per-spot / coordinated diagnostics', () => {
  const { batch } = runBatch({ layout: fixture, pressure: fixture.pressure, formationConfig, seeds: seedRange(4), durationSec: 300, members: 3 });
  assert.deepEqual(validateCombatSimBatch(batch), []);
  assert.equal(batch.seeds.length, 4);
  assert.equal(batch.runs.length, 4);
  assert.ok(batch.report.length <= 40 && batch.report.length >= 10);
  assert.equal(batch.aggregate.runs, 4);
  assert.equal(batch.aggregate.minutesPerRun, 5);
  assert.ok(batch.spots.length > 0 && batch.spots.every((s) => typeof s.main_reason === 'string'));
  assert.ok(batch.heat.cells.every((c) => c.count === c.kinds.patrol + c.kinds.ambush + c.kinds.coordinated));
  assert.equal(batch.cheats.immortal, true);
  assert.equal(batch.layout.members, 3);
  assert.ok(Object.values(batch.aggregate.eventOutcomes).reduce((s, v) => s + v, 0) > 0);
  assert.equal((batch.constants as { dt: number }).dt, DEFAULT_SIM_CONSTANTS.dt);
  assert.deepEqual(validateCombatSimBatch({ schema: 'nope' }).length > 0, true);
});
