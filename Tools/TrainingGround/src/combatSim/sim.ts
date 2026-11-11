// Training Ground / combat simulator: ONE RUN (docs/design/levelgen/COMBAT_SIM.md). The squad replays its track (squadTrack.ts) and
// pauses for fights as the bot's route mode does (cover at a wall first - BotCoverRules); the enemy pressure follows
// UCombatPressureSubsystem step by step with the ported rules (rules.ts): the schedule, the event kinds and types, patrol spawn spots and
// loops, the hiding-spot selector, coordinated plans and their phases, the watch rule, hold limits, relocation runs, followers,
// recycling. Fights are simplified but explicit: 2.5D lines of sight, who saw whom first, reaction time, hits from an uncovered side,
// time under fire, cover reached, DPS / HP with the cheats (immortal + infinite ammo by default, as the Unreal batches). Seeded and
// deterministic: the same inputs give the same result. Pure (no DOM / Node).

import type { Vec2 } from '../movement/geometry.ts';
import { add, dirOf, dist, normYaw, rng, scale, segmentBlocked, sub, yawOf } from '../movement/geometry.ts';
import type { EnemyStats, SimCheats, SimConstants } from './config.ts';
import { DEFAULT_SIM_CONSTANTS, ENEMY_STATS } from './config.ts';
import type { AmbushSpot, EnemyType, Party, PlanPhase, PressureBox, RouteCandidate, SpawnKind, SpotKind, Watcher } from './rules.ts';
import { AmbushPlanRules, AmbushRelocateRules, AmbushSpotRules, AmbushWatchRules, CombatPressureRules, DEFAULT_HEIGHT_SIGHT, HeightSightRules, PLAN_PARTIES, SightConditionsRules, WeaponRules, findDeltaAngleDeg } from './rules.ts';
import type { SquadTrack, TrackFrame } from './squadTrack.ts';
import { frameHeading } from './squadTrack.ts';
import type { SimWorld } from './world.ts';

// --- public types ---------------------------------------------------------------------------------------------------------------------

export type ContactKind = 'patrol' | 'ambush' | 'coordinated';
export type ContactTrigger = 'seen' | 'engaged' | 'sprung';

export interface SimContact {
  id: number;
  kind: ContactKind;
  trigger: ContactTrigger;
  time: number;
  position: Vec2;
  formation: string;
  watched_at_start: boolean;
  unwatched_strike: boolean;
  /** Seconds from the contact to the first look covering it (0 = watched at once; -1 = never). */
  reaction_sec: number;
  zone: string | null;
  /** Seconds from the group's spawn to the contact. */
  since_spawn_sec: number;
  enemies: number;
}

/** Why an ambush event did not take a hiding spot (per spot), or a coordinated plan failed. */
export type SpotRejectReason = 'too_close' | 'too_far' | 'near_squad' | 'wall_clearance' | 'out_of_bounds' | 'off_nav' | 'not_concealed' | 'unreachable' | 'behind';

export interface SpotDiagnostic {
  index: number;
  kind: SpotKind;
  pos: Vec2;
  /** The spot had no wall box within the standoff + 1 m: an authored point in the open. */
  no_wall: boolean;
  /** Ambush events that looked at it, chose it, how often its group sprang by the watch / after the hold / was relocated away / never struck. */
  considered: number;
  chosen: number;
  sprung_by_watch: number;
  sprung_by_hold: number;
  relocated: number;
  never_struck: number;
  rejected: Partial<Record<SpotRejectReason, number>>;
}

export interface CoordinatedAttempt {
  time: number;
  outcome: 'ok' | 'no_room' | 'no_layout' | 'unreachable';
  /** no_layout: the parties without a concealed candidate in their sector. */
  missing: Party[];
  /** Concealed, valid candidates per sector round the anchor. */
  sectors: Record<Party, number>;
  candidates: number;
  alive: number;
}

export interface EventDiagnostic {
  time: number;
  picked: SpawnKind;
  /** What came of it. */
  outcome: 'patrol' | 'ambush' | 'coordinated' | 'ambush_fallback_patrol' | 'coordinated_fallback_ambush' | 'coordinated_fallback_patrol' | 'no_spot' | 'no_room';
  size: number;
  spot_kind?: SpotKind;
  spot_index?: number;
  /** Ambush events: how many hiding spots passed each stage. */
  funnel?: { in_band: number; clear: number; concealed: number; reachable: number };
  /** The squad's waypoint / layer at the event. */
  waypoint: number;
  layer: string;
}

export interface RunMetrics {
  seed: number;
  duration_sec: number;
  contacts: number;
  by_kind: Record<ContactKind, number>;
  by_trigger: Record<ContactTrigger, number>;
  unwatched_strikes: number;
  never_looked: number;
  reaction_mean_sec: number;
  first_sight_sec: number;
  fights: number;
  fight_seconds: number;
  formation_changes: number;
  wall_hugs: number;
  cover_orders: number;
  cover_taken: number;
  cover_seconds: number;
  knockdowns: number;
  damage_taken: number;
  damage_absorbed: number;
  rounds: number;
  enemies_spawned: number;
  enemies_killed: number;
  route: { laps: number; progress: number; distance_m: number; furthest_waypoint: number };
  pressure: { events: number; events_delayed: number; sprung_by_watch: number; sprung_by_hold: number; relocations: number; runs_spotted: number; no_route: number; followers: number; recycled: number; lost: number };
  /** Seconds some operative stood under an enemy's fire / in melee reach. */
  under_fire_seconds: number;
  /** Hits that landed from a side no look covered (the operative's body / look / aim cones). */
  unwatched_hits: number;
}

export interface RunResult {
  metrics: RunMetrics;
  contacts: SimContact[];
  events: EventDiagnostic[];
  coordinated: CoordinatedAttempt[];
  spots: SpotDiagnostic[];
  /** Contact positions for the heat map. */
  contact_positions: Vec2[];
}

export interface RunOptions {
  world: SimWorld;
  track: SquadTrack;
  seed: number;
  durationSec: number;
  constants?: Partial<SimConstants>;
  cheats?: Partial<SimCheats>;
  stats?: Partial<Record<EnemyType, EnemyStats>>;
  /** Called every tick (the UI / debugging); off by default. */
  onTick?: (state: { t: number; squad: Vec2[]; enemies: { pos: Vec2; type: EnemyType; state: EnemyState; group: number }[]; inFight: boolean }) => void;
}

// --- internal state -------------------------------------------------------------------------------------------------------------------

type EnemyState = 'patrol' | 'hidden' | 'holding' | 'relocating' | 'engaged' | 'dead' | 'removed';

interface Enemy {
  id: number;
  type: EnemyType;
  stats: EnemyStats;
  group: number;
  plan: number;
  party: Party;
  kind: 'patrol' | 'ambush';
  state: EnemyState;
  pos: Vec2;
  yaw: number;
  hp: number;
  spawnedAt: number;
  /** Patrols: the loop (the leader) / the leader followed (escorts). */
  loop: Vec2[];
  loopIndex: number;
  waitLeft: number;
  escortOf: number;
  suspicion: number;
  /** Fighting: the operative targeted, the next attack time, the path to him. */
  target: number;
  nextAttack: number;
  path: Vec2[] | null;
  repathAt: number;
  /** A hound's run-up at full speed, s; its last pounce. */
  runUp: number;
  lastPounce: number;
  holdSeconds: number;
  farSeconds: number;
  suppressor: boolean;
  follower: boolean;
  guardPost: Vec2;
  /** Engaged, neither reaching nor seeing the squad nor coming nearer (a lost enemy gives up after lostAfterSeconds); the nearest it got. */
  lostSeconds: number;
  chaseBestCm: number;
  /** Steps refused by the nav grid in a row, s. */
  stuckSeconds: number;
}

interface Group {
  id: number;
  kind: SpawnKind;
  spotKind: SpotKind;
  spotIndex: number;
  plan: number;
  party: Party;
  holdLimit: number;
  bypassSeconds: number;
  noRouteSearches: number;
  spawnedAt: number;
  members: number[];
  contact: number;
  /** For the spot diagnostics: how the group ended. */
  sprung: '' | 'watch' | 'hold' | 'engaged' | 'plan' | 'spotted';
  relocated: boolean;
}

interface Plan {
  id: number;
  anchor: Vec2;
  heading: number;
  partyGroups: number[];
  partySpots: Vec2[];
  phase: PlanPhase;
  phaseSeconds: number;
  holdSeconds: number;
  holdLimit: number;
  bypassedSeconds: number;
  noRouteSearches: number;
  dissolved: boolean;
  spottedOnRoute: boolean;
  /** A party was engaged otherwise (shot at / seen and fired on): the C++ TickPlan's bAnyEngaged - the plan springs (flank block). */
  engagedOtherwise: boolean;
  flankAt: number;
  relocations: number;
}

interface Run {
  group: number;
  plan: number;
  members: number[];
  legs: Vec2[][];
  leg: number[];
  elapsed: number;
  limit: number;
  speed: number;
}

interface Operative {
  pos: Vec2;
  bodyYaw: number;
  lookYaw: number;
  level: number;
  target: number;
  nextShot: number;
  sawAt: number;
  knockedUntil: number;
  immuneUntil: number;
  health: number;
  absorbed: number;
  taken: number;
  rounds: number;
  knockdowns: number;
  underFire: boolean;
}

interface HideChoice { index: number; feet: Vec2; kind: SpotKind; aheadCos: number; score: number }

const OPERATIVE_HP = 100;
const PROXIMITY_CM = 300;
const PACK_ALERT_CM = 1500;
const RECYCLE_STEP = 0.5;

/** Runs one seeded combat simulation of the track against the pressure. */
export function runCombatSim(o: RunOptions): RunResult {
  const world = o.world, track = o.track, cfg = world.config;
  const K: SimConstants = { ...DEFAULT_SIM_CONSTANTS, ...(o.constants ?? {}) };
  const cheats: SimCheats = { immortal: true, infiniteAmmo: true, ...(o.cheats ?? {}) };
  const stats = Object.fromEntries(Object.entries({ ...ENEMY_STATS, ...(o.stats ?? {}) } as Record<EnemyType, EnemyStats>).map(([k, v]) => [k, { ...v, hp: v.hp * K.enemyHpScale }])) as Record<EnemyType, EnemyStats>;
  const dt = K.dt;
  const stream = rng((o.seed >>> 0) * 2654435761 >>> 0);
  const fightRng = rng(((o.seed * 31 + 7) >>> 0) * 2246822519 >>> 0);
  const frand = () => stream();
  const frandRange = (a: number, b: number) => a + (b - a) * stream();
  const holdDraw = (key: number) => rng(((cfg.seed * 7919 + key) >>> 0) * 3266489917 >>> 0)();
  const boxes = world.boxes;
  const spots = world.hideSpots;
  const components = navComponents(world);

  // ---- squad -----------------------------------------------------------------------------------------------------------------------
  const members = track.members;
  const ops: Operative[] = track.frames[0].members.map((m) => ({ pos: m.pos, bodyYaw: m.bodyYaw, lookYaw: m.lookYaw, level: 0, target: -1, nextShot: 0, sawAt: -1, knockedUntil: -1, immuneUntil: -1, health: OPERATIVE_HP, absorbed: 0, taken: 0, rounds: 0, knockdowns: 0, underFire: false }));
  let trackT = 0; // seconds into the track (wraps per lap)
  let laps = 0, furthest = 0, distanceCm = 0;
  let frame: TrackFrame = track.frames[0];
  let frameIndex = 0;
  let heading = frameHeading(track, 0);
  // fight pause: 'walk' (the track), 'to_cover' (walking to the wall), 'at_wall', 'halt'
  let squadMode: 'walk' | 'to_cover' | 'at_wall' | 'halt' = 'walk';
  let pauseStart = 0, coverSlots: { pos: Vec2; look: number }[] = [], coverFrom: Vec2[] = [], coverStart = 0, coverTravel = 0;
  let coverTaken = false;
  let formationLabel = frame.formation, labelSince = 0, labelHeld = frame.formation, formationChanges = 0;
  let wallHugs = 0, atWallSince = -10, wasAtWall = false;
  let coverOrders = 0, coverTakenCount = 0, coverSeconds = 0;

  // ---- enemies ---------------------------------------------------------------------------------------------------------------------
  const enemies: Enemy[] = [];
  const groups: Group[] = [];
  const plans: Plan[] = [];
  const runs: Run[] = [];
  const followers = new Set<number>();
  let nextGroup = 1, nextPlan = 1, nextEnemy = 1;
  let clock = 0, dueAt = cfg.firstEventSeconds, delayLogged = false;
  const contacts: SimContact[] = [];
  const contactIndex = new Map<number, number>();
  const events: EventDiagnostic[] = [];
  const coordinated: CoordinatedAttempt[] = [];
  const spotDiag: SpotDiagnostic[] = spots.map((s, i) => ({ index: i, kind: s.kind, pos: s.position, no_wall: s.kind === 'authored' && CombatPressureRules.clearance(s.position, boxes) > cfg.spots.wallStandoffCm + 100, considered: 0, chosen: 0, sprung_by_watch: 0, sprung_by_hold: 0, relocated: 0, never_struck: 0, rejected: {} }));
  const P = { events: 0, eventsDelayed: 0, sprungByWatch: 0, sprungByHold: 0, relocations: 0, runsSpotted: 0, noRoute: 0, followers: 0, recycled: 0, lost: 0, spawned: 0, killed: 0 };
  let fights = 0, fightSeconds = 0, inFight = false, wasInFight = false, knockdowns = 0, underFireSeconds = 0, unwatchedHits = 0, firstSight = -1;
  let recycleTimer = 0, followTimer = 0;
  let lastShotAt = -100;

  const reject = (i: number, why: SpotRejectReason) => { const d = spotDiag[i]; d.rejected[why] = (d.rejected[why] ?? 0) + 1; };
  let alive: Enemy[] = [];
  const refreshAlive = () => { alive = enemies.filter((e) => e.state !== 'dead' && e.state !== 'removed'); };
  const living = () => alive;
  const aliveCount = () => alive.length;
  const groupById = (id: number): Group | undefined => groups[id - 1];
  const squadPositions = (): Vec2[] => ops.map((m) => m.pos);
  const leaderPos = () => ops[0].pos;

  const watchers = (): Watcher[] => {
    const out: Watcher[] = [];
    for (const m of ops) {
      const aim = m.target >= 0 && enemyById(m.target) ? AmbushWatchRules.bearingDeg(m.pos, enemyById(m.target)!.pos) : null;
      out.push(...AmbushWatchRules.memberLooks(m.pos, m.bodyYaw, aim, m.lookYaw));
    }
    return out;
  };
  const enemyById = (id: number): Enemy | undefined => enemies[id - 1];

  /** IsVisibleToSquad: some operative's eye has a clear line to the point's chest height. */
  const visibleToSquad = (p: Vec2, levelZ = 0): boolean => {
    for (const m of ops) if (world.los(m.pos, m.level * 300 + K.eyeLiftCm, p, levelZ + K.targetLiftCm)) return true;
    return false;
  };
  /**
   * UTacticalSightSubsystem::GetSightRangeCm (2026-11-02): MaxSightCm x the height level's multiplier - his eye over the ground below him (the
   * sim's buildings stand on the street: level x 300 + the eye; in the -3 m cut the cut floor is his ground, level 0).
   * T10: When useSightConditions is enabled, base sight range scales with weather and sun elevation / hour.
   */
  const squadSightRangeCm = (m: Operative): number => {
    const baseSightCm = K.useSightConditions && K.sightConditions
      ? SightConditionsRules.squadSightRangeCm(
          K.sightConditions,
          K.weather ?? 'clear',
          K.sunElevationDeg ?? (K.hour !== undefined ? SightConditionsRules.sunElevationFromHour(K.hour) : 45),
        )
      : K.sightRangeCm;
    return baseSightCm * HeightSightRules.rangeMultiplierAt(DEFAULT_HEIGHT_SIGHT, Math.max(0, m.level) * 300 + K.sightEyeCm);
  };
  /** IsPointInSquadSight: the real eyes (any direction), within his sight range (MaxSightCm x his height bonus). */
  const pointInSquadSight = (p: Vec2, z: number): boolean => {
    for (const m of ops) {
      const ez = m.level * 300 + K.sightEyeCm;
      if (Math.hypot(dist(m.pos, p), ez - z) > squadSightRangeCm(m)) continue;
      if (world.los(m.pos, ez, p, z)) return true;
    }
    return false;
  };
  /** IsSpottedBySquad: an operative's field of view (one of his looks +- the half angle), range and a clear line. */
  const spottedBySquad = (e: Enemy): boolean => {
    for (const m of ops) {
      const d = dist(m.pos, e.pos);
      if (d > squadSightRangeCm(m)) continue;
      const bearing = AmbushWatchRules.bearingDeg(m.pos, e.pos);
      const looks = [m.bodyYaw, m.lookYaw, ...(m.target >= 0 && enemyById(m.target) ? [AmbushWatchRules.bearingDeg(m.pos, enemyById(m.target)!.pos)] : [])];
      if (!looks.some((l) => Math.abs(findDeltaAngleDeg(l, bearing)) <= K.sightHalfAngleDeg)) continue;
      if (world.los(m.pos, m.level * 300 + K.sightEyeCm, e.pos, K.targetLiftCm)) return true;
    }
    return false;
  };
  const groupSeen = (g: Group): boolean => g.members.some((id) => { const e = enemyById(id)!; return e.state !== 'dead' && e.state !== 'removed' && visibleToSquad(e.pos); });
  const reachableFromSquad = (p: Vec2): boolean => components.same(p, leaderGround());
  /** The leader's ground point: his position, or - off the ground - the route's last ground point he passed. */
  let lastGround: Vec2 = track.frames[0].members[0].pos;
  const leaderGround = () => lastGround;

  // ---- spawn helpers (UCombatPressureSubsystem) ----------------------------------------------------------------------------------------
  const groupSpots = (feet: Vec2, count: number, concealed: boolean): Vec2[] => {
    const out: Vec2[] = [feet];
    const squad = squadPositions();
    const clearance = concealed ? world.hideClearanceCm : cfg.wallClearanceCm;
    for (let i = 1; out.length < count && i < 12; i++) {
      const p = add(feet, CombatPressureRules.groupOffset(i, K.groupSpacingCm));
      if (CombatPressureRules.checkSpawnPoint(p, boxes, clearance, squad, 0, world.bounds) !== 'ok') continue;
      if (!world.projectFeet(p) || CombatPressureRules.clearance(p, boxes) < clearance) continue;
      if (concealed && visibleToSquad(p)) continue;
      if (segmentBlocked(feet, p, world.ground.walls, 0)) continue;
      out.push(p);
    }
    return out;
  };

  const spawnOne = (type: EnemyType, feet: Vec2, faceTowards: Vec2, kind: 'patrol' | 'ambush', group: number, plan: number, party: Party): Enemy => {
    const e: Enemy = {
      id: nextEnemy++, type, stats: stats[type], group, plan, party, kind, state: kind === 'patrol' ? 'patrol' : 'hidden', pos: feet, yaw: yawOf(sub(faceTowards, feet)), hp: stats[type].hp, spawnedAt: clock,
      loop: [], loopIndex: 0, waitLeft: 0, escortOf: -1, suspicion: 0, target: -1, nextAttack: 0, path: null, repathAt: 0, runUp: 0, lastPounce: -100, holdSeconds: 0, farSeconds: 0, suppressor: false, follower: false, guardPost: feet, lostSeconds: 0, chaseBestCm: Infinity, stuckSeconds: 0,
    };
    enemies.push(e);
    alive.push(e);
    P.spawned++;
    return e;
  };

  const makeLoop = (feet: Vec2): Vec2[] => {
    let points: Vec2[] = [feet];
    const phase = frandRange(0, 360);
    const clockwise = frand() < 0.5;
    for (const radius of [cfg.patrolRadiusCm, cfg.patrolRadiusCm * 0.6]) {
      points = [feet];
      const loop = CombatPressureRules.patrolLoop(feet, radius, cfg.patrolPoints, phase);
      if (clockwise) loop.reverse();
      for (const p of loop) {
        if (CombatPressureRules.clearance(p, boxes) < cfg.wallClearanceCm || (world.bounds && !(p[0] >= world.bounds.min[0] && p[0] <= world.bounds.max[0] && p[1] >= world.bounds.min[1] && p[1] <= world.bounds.max[1]))) continue;
        if (world.projectFeet(p) && components.same(p, feet)) points.push(p);
      }
      if (points.length >= 3) break;
    }
    return points;
  };

  const spawnPatrolGroup = (feet: Vec2, types: EnemyType[]): Group => {
    const g: Group = { id: nextGroup++, kind: 'patrol', spotKind: 'none', spotIndex: -1, plan: 0, party: 'single', holdLimit: 0, bypassSeconds: 0, noRouteSearches: 0, spawnedAt: clock, members: [], contact: -1, sprung: '', relocated: false };
    const spotsAt = groupSpots(feet, types.length, false);
    const loop = makeLoop(feet);
    const ahead = loop.length > 1 ? loop[1] : add(feet, [100, 0]);
    let leader: Enemy | null = null;
    for (let i = 0; i < spotsAt.length; i++) {
      const e = spawnOne(types[i], spotsAt[i], ahead, 'patrol', g.id, 0, 'single');
      if (!leader) { leader = e; e.loop = loop; e.loopIndex = 1 % Math.max(1, loop.length); e.waitLeft = 0; } else e.escortOf = leader.id;
      g.members.push(e.id);
    }
    groups.push(g);
    return g;
  };

  const spawnAmbushGroup = (feet: Vec2, types: EnemyType[], spotKind: SpotKind, spotIndex: number, plan: number, party: Party): Group => {
    const g: Group = { id: nextGroup++, kind: plan ? 'coordinated' : 'ambush', spotKind, spotIndex, plan, party, holdLimit: 0, bypassSeconds: 0, noRouteSearches: 0, spawnedAt: clock, members: [], contact: -1, sprung: '', relocated: false };
    if (!plan) g.holdLimit = AmbushWatchRules.holdLimitSeconds(holdDraw(g.id), cfg.watch);
    const spotsAt = groupSpots(feet, types.length, true);
    for (let i = 0; i < spotsAt.length; i++) {
      const e = spawnOne(types[i], spotsAt[i], leaderPos(), 'ambush', g.id, plan, party);
      g.members.push(e.id);
    }
    if (spotIndex >= 0) spotDiag[spotIndex].chosen++;
    groups.push(g);
    return g;
  };

  /** FindSpot(Patrol): 72 random candidates round the leader, the best reachable one. */
  const findPatrolSpot = (): Vec2 | null => {
    const squad = squadPositions();
    const w = watchers();
    const leader = leaderPos();
    const headingYaw = heading;
    const minD = cfg.minSquadDistanceCm, maxD = cfg.maxSquadDistanceCm;
    const cands: { feet: Vec2; score: number }[] = [];
    for (let t = 0; t < K.spotCandidates; t++) {
      const yaw = frand() < 0.7 ? headingYaw + frandRange(-80, 80) : frandRange(-180, 180);
      const distance = frandRange(minD, maxD);
      const p = add(leader, scale(dirOf(yaw), distance));
      if (CombatPressureRules.checkSpawnPoint(p, boxes, cfg.wallClearanceCm, squad, minD, world.bounds) !== 'ok') continue;
      if (!world.projectFeet(p)) continue;
      const score = CombatPressureRules.scoreSpot({ kind: 'patrol', distanceCm: distance, minDistanceCm: minD, maxDistanceCm: maxD, aheadCos: Math.cos(findDeltaAngleDeg(headingYaw, yaw) * Math.PI / 180), concealed: !visibleToSquad(p), watched: AmbushWatchRules.isWatched(w, p, cfg.watch), kindBonus: 0 });
      if (score > -100) cands.push({ feet: p, score: score + frandRange(0, 0.05) });
    }
    cands.sort((a, b) => b.score - a.score);
    for (let i = 0; i < Math.min(6, cands.length); i++) if (reachableFromSquad(cands[i].feet)) return cands[i].feet;
    return null;
  };

  /** RankHideSpots / FindHideSpot: every valid, concealed hiding spot in the band, scored; with the per-spot diagnostics. */
  const rankHideSpots = (minD: number, maxD: number, minAheadCos: number, avoid: Vec2[], avoidCm: number, diag: boolean, funnel?: EventDiagnostic['funnel']): HideChoice[] => {
    const squad = squadPositions();
    const w = watchers();
    const leader = leaderPos();
    const hv = dirOf(heading);
    const clearance = world.hideClearanceCm;
    const out: HideChoice[] = [];
    for (let i = 0; i < spots.length; i++) {
      const s = spots[i];
      const distance = dist(leader, s.position);
      if (diag) spotDiag[i].considered++;
      if (distance < minD) { if (diag) reject(i, 'too_close'); continue; }
      if (distance > maxD) { if (diag) reject(i, 'too_far'); continue; }
      if (funnel) funnel.in_band++;
      const verdict = CombatPressureRules.checkSpawnPoint(s.position, boxes, clearance, squad, minD, world.bounds);
      if (verdict !== 'ok') { if (diag) reject(i, verdict === 'near_wall' ? 'wall_clearance' : verdict === 'near_squad' ? 'near_squad' : 'out_of_bounds'); continue; }
      if (avoid.some((a) => dist(a, s.position) < avoidCm)) continue;
      const aheadCos = distance > 1 ? (hv[0] * (s.position[0] - leader[0]) + hv[1] * (s.position[1] - leader[1])) / distance : 1;
      if (aheadCos < minAheadCos) { if (diag) reject(i, 'behind'); continue; }
      if (!world.projectFeet(s.position)) { if (diag) reject(i, 'off_nav'); continue; }
      if (funnel) funnel.clear++;
      if (visibleToSquad(s.position)) { if (diag) reject(i, 'not_concealed'); continue; }
      if (funnel) funnel.concealed++;
      const score = CombatPressureRules.scoreSpot({ kind: 'ambush', distanceCm: distance, minDistanceCm: minD, maxDistanceCm: maxD, aheadCos, concealed: true, watched: AmbushWatchRules.isWatched(w, s.position, cfg.watch), kindBonus: AmbushSpotRules.kindBonus(s.kind, cfg.spots) });
      out.push({ index: i, feet: s.position, kind: s.kind, aheadCos, score });
    }
    out.sort((a, b) => b.score - a.score);
    return out;
  };

  const findHideSpot = (funnel?: EventDiagnostic['funnel']): HideChoice | null => {
    const ranked = rankHideSpots(cfg.ambushMinDistanceCm, cfg.ambushMaxDistanceCm, -1, [], 0, true, funnel).map((c) => ({ ...c, score: c.score + frandRange(0, 0.05) })).sort((a, b) => b.score - a.score);
    for (let i = 0; i < Math.min(6, ranked.length); i++) {
      if (reachableFromSquad(ranked[i].feet)) { if (funnel) funnel.reachable++; return ranked[i]; }
      reject(ranked[i].index, 'unreachable');
    }
    return null;
  };

  /** PlanLayout: four concealed, valid, reachable hiding spots, one per sector round the anchor. */
  const planLayout = (attempt: CoordinatedAttempt): { anchor: Vec2; heading: number; spots: Vec2[]; kinds: SpotKind[]; indices: number[] } | null => {
    const plan = cfg.coordinated;
    const squad = squadPositions();
    const leader = leaderPos();
    const anchor = AmbushPlanRules.anchor(leader, heading, plan);
    const clearance = world.hideClearanceCm;
    let feets: Vec2[] = [], kinds: SpotKind[] = [], idx: number[] = [];
    for (let i = 0; i < spots.length; i++) {
      const s = spots[i];
      if (dist(leader, s.position) > plan.maxSquadDistanceCm) continue;
      if (CombatPressureRules.checkSpawnPoint(s.position, boxes, clearance, squad, plan.minSquadDistanceCm, world.bounds) !== 'ok') continue;
      if (!world.projectFeet(s.position) || visibleToSquad(s.position)) continue;
      feets.push(s.position); kinds.push(s.kind); idx.push(i);
      attempt.sectors[AmbushPlanRules.sectorOf(anchor, heading, s.position)]++;
    }
    attempt.candidates = feets.length;
    for (let a = 0; a < 4; a++) {
      const assigned = AmbushPlanRules.assignSpots(anchor, heading, feets, plan);
      if (!AmbushPlanRules.isComplete(assigned)) {
        attempt.outcome = 'no_layout';
        attempt.missing = PLAN_PARTIES.filter((_, k) => assigned[k] === -1);
        return null;
      }
      const bad = assigned.find((i) => !reachableFromSquad(feets[i]));
      if (bad !== undefined) {
        feets = feets.filter((_, i) => i !== bad); kinds = kinds.filter((_, i) => i !== bad); idx = idx.filter((_, i) => i !== bad);
        continue;
      }
      attempt.outcome = 'ok';
      return { anchor, heading, spots: assigned.map((i) => feets[i]), kinds: assigned.map((i) => kinds[i]), indices: assigned.map((i) => idx[i]) };
    }
    attempt.outcome = 'unreachable';
    return null;
  };

  const pickTypes = (n: number): EnemyType[] => Array.from({ length: n }, () => CombatPressureRules.pickType(cfg.typeWeights, frand()));
  const ensureRanged = (types: EnemyType[]) => {
    const ranged = CombatPressureRules.effectiveWeights(cfg.typeWeights).filter((w) => AmbushPlanRules.isRanged(w.type));
    if (ranged.length) AmbushPlanRules.ensureRanged(types, CombatPressureRules.pickType(ranged, frand()), cfg.coordinated.minRanged);
  };

  const triggerCoordinated = (bodies: EnemyType[]): Group[] | null => {
    const alive = aliveCount();
    const attempt: CoordinatedAttempt = { time: clock, outcome: 'ok', missing: [], sectors: { single: 0, flank_left: 0, flank_right: 0, front: 0, rear: 0 }, candidates: 0, alive };
    if (bodies.length < 4) { attempt.outcome = 'no_room'; coordinated.push(attempt); return null; }
    const layout = planLayout(attempt);
    coordinated.push(attempt);
    if (!layout) return null;
    const plan: Plan = { id: nextPlan++, anchor: layout.anchor, heading: layout.heading, partyGroups: [0, 0, 0, 0], partySpots: layout.spots, phase: 'waiting', phaseSeconds: 0, holdSeconds: 0, holdLimit: AmbushWatchRules.holdLimitSeconds(holdDraw(100000 + nextPlan - 1), cfg.watch), bypassedSeconds: 0, noRouteSearches: 0, dissolved: false, spottedOnRoute: false, engagedOtherwise: false, flankAt: -1, relocations: 0 };
    const out: Group[] = [];
    for (let slot = 0; slot < 4; slot++) {
      const party = AmbushPlanRules.planParty(slot);
      const partyTypes = bodies.filter((_, i) => AmbushPlanRules.partyOf(i) === party);
      const g = spawnAmbushGroup(layout.spots[slot], partyTypes, layout.kinds[slot], layout.indices[slot], plan.id, party);
      plan.partyGroups[slot] = g.id;
      out.push(g);
    }
    plans.push(plan);
    return out;
  };

  const triggerEvent = (): boolean => {
    const randKind = frand(), randCoordinated = frand();
    let picked = CombatPressureRules.pickEventKind(randKind, randCoordinated, cfg.patrolShare, cfg.coordinated.share);
    const ev: EventDiagnostic = { time: clock, picked, outcome: 'patrol', size: 0, waypoint: frame.waypoint, layer: frame.layer };
    events.push(ev);
    if (picked === 'coordinated') {
      const count = AmbushPlanRules.planBodies(frand(), cfg.coordinated, aliveCount());
      const bodies = pickTypes(count);
      if (bodies.length) ensureRanged(bodies);
      const out = triggerCoordinated(bodies);
      if (out) { ev.outcome = 'coordinated'; ev.size = bodies.length; return true; }
      picked = 'ambush';
      ev.outcome = 'coordinated_fallback_ambush';
    }
    const size = CombatPressureRules.groupSize(frand(), cfg.groupMin, cfg.groupMax, aliveCount(), cfg.maxAlive);
    ev.size = size;
    if (size <= 0) { ev.outcome = 'no_room'; return false; }
    const types = pickTypes(size);
    if (picked === 'ambush') {
      ev.funnel = { in_band: 0, clear: 0, concealed: 0, reachable: 0 };
      const hide = findHideSpot(ev.funnel);
      if (hide) {
        spawnAmbushGroup(hide.feet, types, hide.kind, hide.index, 0, 'single');
        ev.outcome = ev.outcome === 'coordinated_fallback_ambush' ? 'coordinated_fallback_ambush' : 'ambush';
        ev.spot_kind = hide.kind; ev.spot_index = hide.index;
        return true;
      }
      ev.outcome = ev.outcome === 'coordinated_fallback_ambush' ? 'coordinated_fallback_patrol' : 'ambush_fallback_patrol';
    }
    const spot = findPatrolSpot();
    if (!spot) { ev.outcome = 'no_spot'; return false; }
    spawnPatrolGroup(spot, types);
    return true;
  };

  // ---- ambush logic (TickAmbushers / TickPlan / TickRelocation / TickRuns / followers) ----------------------------------------------
  const springMember = (e: Enemy) => {
    if (e.state === 'dead' || e.state === 'removed' || e.state === 'engaged') return;
    e.state = 'engaged';
    e.suppressor = e.stats.ranged && e.kind === 'ambush';
    e.follower = false;
  };
  const spring = (g: Group, why: Group['sprung']) => {
    if (!g.sprung) g.sprung = why;
    for (const id of g.members) springMember(enemyById(id)!);
  };
  const groupLiving = (g: Group): Enemy[] => g.members.map((id) => enemyById(id)!).filter((e) => e.state !== 'dead' && e.state !== 'removed');
  const groupRunning = (gid: number) => runs.some((r) => r.group === gid);

  const evaluateRoute = (g: Group, dest: Vec2, destConcealed: boolean, score: number): { cand: RouteCandidate; path: Vec2[] } => {
    const cand: RouteCandidate = { score, pathCm: 0, reachable: false, seenSamples: 0, destinationConcealed: destConcealed };
    const first = groupLiving(g).find((e) => e.state !== 'engaged');
    if (!first) return { cand, path: [] };
    const path = components.same(first.pos, dest) ? world.path(first.pos, dest) : null;
    if (!path || path.length < 2) return { cand, path: [] };
    cand.reachable = true;
    cand.pathCm = AmbushRelocateRules.pathLength(path);
    if (cand.pathCm > cfg.relocate.maxRouteCm) return { cand, path };
    for (const s of AmbushRelocateRules.samplePath(path, cfg.relocate.routeSampleCm)) {
      if (pointInSquadSight(s, 100)) { cand.seenSamples = 1; break; }
    }
    return { cand, path };
  };

  const findRelocationRoute = (g: Group): { hide: HideChoice; path: Vec2[]; cand: RouteCandidate } | null => {
    const first = groupLiving(g).find((e) => e.state !== 'engaged');
    if (!first) return null;
    const ranked = rankHideSpots(cfg.ambushMinDistanceCm, cfg.ambushMaxDistanceCm, cfg.relocate.minAheadCos, [first.pos], K.relocateAvoidCm, false);
    const cands: RouteCandidate[] = [], paths: Vec2[][] = [];
    for (let i = 0; i < ranked.length && i < K.relocateRoutes; i++) {
      const r = evaluateRoute(g, ranked[i].feet, AmbushRelocateRules.isGoodDestination(ranked[i].aheadCos, true, cfg.relocate), ranked[i].score);
      cands.push(r.cand); paths.push(r.path);
    }
    const best = AmbushRelocateRules.pickRoute(cands, cfg.relocate);
    if (best === -1) return null;
    return { hide: ranked[best], path: paths[best], cand: cands[best] };
  };

  const startRun = (g: Group, plan: number, route: { hide: HideChoice; path: Vec2[]; cand: RouteCandidate }) => {
    const runners = groupLiving(g).filter((e) => e.state !== 'engaged');
    if (!runners.length || route.path.length < 2) return;
    let finals = groupSpots(route.hide.feet, runners.length, true);
    if (finals.length < runners.length) finals = groupSpots(route.hide.feet, runners.length, false);
    let speed = Infinity;
    const legs: Vec2[][] = [];
    for (let i = 0; i < runners.length; i++) {
      runners[i].state = 'relocating';
      const l = route.path.slice(1, -1);
      l.push(finals[Math.min(i, finals.length - 1)]);
      legs.push(l);
      speed = Math.min(speed, runners[i].stats.speed * cfg.relocate.runSpeedScale);
    }
    speed = Math.max(150, speed);
    runs.push({ group: g.id, plan, members: runners.map((e) => e.id), legs, leg: runners.map(() => 0), elapsed: 0, limit: AmbushRelocateRules.runLimitSeconds(route.cand.pathCm, speed), speed });
    g.relocated = true;
    if (g.spotIndex >= 0) spotDiag[g.spotIndex].relocated++;
    g.spotIndex = route.hide.index;
    g.spotKind = route.hide.kind;
    P.relocations++;
  };

  const convertToFollower = (g: Group) => {
    let n = 0;
    for (const e of groupLiving(g)) {
      if (e.state === 'engaged') continue;
      e.kind = 'patrol'; e.state = 'patrol'; e.follower = true; e.loop = []; n++;
    }
    if (!n) return;
    followers.add(g.id);
    g.bypassSeconds = 0; g.noRouteSearches = 0;
    P.followers++;
  };

  const tickRelocation = (g: Group, first: Enemy, squad: Vec2[]) => {
    const angle = AmbushWatchRules.angleOffDeg(heading, AmbushWatchRules.bearingDeg(leaderPos(), first.pos));
    const nearest = AmbushWatchRules.nearestDistance(squad, first.pos);
    g.bypassSeconds = AmbushRelocateRules.stepBypassedSeconds(AmbushRelocateRules.isBypassed(nearest, angle, cfg.watch.strikeRangeCm, cfg.relocate), g.bypassSeconds, dt);
    const decision = AmbushRelocateRules.decide(g.bypassSeconds, g.bypassSeconds >= cfg.relocate.afterSeconds && groupSeen(g), cfg.relocate);
    if (decision !== 'relocate') return;
    g.bypassSeconds = 0;
    const route = findRelocationRoute(g);
    if (!route) {
      P.noRoute++;
      g.noRouteSearches++;
      if (AmbushRelocateRules.noRouteOutcome(g.noRouteSearches, cfg.relocate) === 'patrol') convertToFollower(g);
      return;
    }
    g.noRouteSearches = 0;
    startRun(g, 0, route);
  };

  const tickAmbushers = (squad: Vec2[], w: Watcher[]) => {
    for (const g of groups) {
      if (g.plan !== 0 || g.kind === 'patrol') continue;
      const alive = groupLiving(g).filter((e) => e.state !== 'engaged' && e.state !== 'relocating');
      if (!alive.length) continue;
      const first = alive[0];
      if (groupLiving(g).some((e) => e.state === 'engaged')) { spring(g, 'engaged'); continue; }
      const decision = AmbushWatchRules.decide(w, squad, first.pos, cfg.watch);
      if (decision === 'spring') {
        P.sprungByWatch++;
        if (g.spotIndex >= 0) spotDiag[g.spotIndex].sprung_by_watch++;
        spring(g, 'watch');
        continue;
      }
      let held = 0;
      for (const e of alive) {
        e.state = decision === 'hold' ? 'holding' : 'hidden';
        e.holdSeconds += decision === 'hold' ? dt : 0;
        held = Math.max(held, e.holdSeconds);
      }
      if (decision === 'hold' && AmbushWatchRules.heldTooLong(held, g.holdLimit)) {
        P.sprungByHold++;
        if (g.spotIndex >= 0) spotDiag[g.spotIndex].sprung_by_hold++;
        spring(g, 'hold');
        continue;
      }
      if (decision === 'wait') tickRelocation(g, first, squad);
      else g.bypassSeconds = 0;
    }
    for (const plan of plans) if (!plan.dissolved) tickPlan(plan, squad, w);
    tickRuns();
    followTimer += dt;
    if (followTimer >= K.followStepSeconds) { followTimer = 0; tickFollowers(); }
  };

  const tickPlan = (plan: Plan, squad: Vec2[], w: Watcher[]) => {
    const params = cfg.coordinated;
    const tick = { phase: plan.phase, secondsInPhase: plan.phaseSeconds, nearestSquadCm: AmbushWatchRules.nearestDistance(squad, plan.anchor), flanksUnwatched: 0, anyEngaged: false, flankMembersAlive: 0, flankMembersInPlace: 0, holdSeconds: plan.holdSeconds, holdLimitSeconds: plan.holdLimit };
    let unsprung = 0;
    const unwatchedFlanks = new Set<number>();
    const planEnemies = enemies.filter((e) => e.plan === plan.id && e.state !== 'dead' && e.state !== 'removed');
    for (const e of planEnemies) {
      if (AmbushPlanRules.isFlank(e.party)) {
        tick.flankMembersAlive++;
        tick.flankMembersInPlace += AmbushWatchRules.nearestDistance(squad, e.pos) <= params.blockDistanceCm ? 1 : 0;
      }
      if (e.state === 'engaged') continue;
      unsprung++;
      if (AmbushPlanRules.isFlank(e.party) && !AmbushWatchRules.isWatched(w, e.pos, cfg.watch)) unwatchedFlanks.add(e.group);
    }
    if (unsprung === 0) return;
    tick.flanksUnwatched = unwatchedFlanks.size;
    tick.anyEngaged ||= plan.spottedOnRoute || plan.engagedOtherwise;
    const running = plan.partyGroups.some((gid) => groupRunning(gid));
    if (running && !tick.anyEngaged) return;
    if (plan.phase === 'waiting' && !tick.anyEngaged) {
      const angle = AmbushWatchRules.angleOffDeg(heading, AmbushWatchRules.bearingDeg(leaderPos(), plan.anchor));
      const bypassed = AmbushRelocateRules.isBypassed(tick.nearestSquadCm, angle, params.triggerRangeCm, cfg.relocate);
      plan.bypassedSeconds = AmbushRelocateRules.stepBypassedSeconds(bypassed, plan.bypassedSeconds, dt);
      const seen = plan.partyGroups.some((gid) => { const g = groupById(gid); return g ? groupSeen(g) : false; });
      if (AmbushRelocateRules.decide(plan.bypassedSeconds, seen, cfg.relocate) === 'relocate') {
        plan.bypassedSeconds = 0;
        const attempt: CoordinatedAttempt = { time: clock, outcome: 'ok', missing: [], sectors: { single: 0, flank_left: 0, flank_right: 0, front: 0, rear: 0 }, candidates: 0, alive: aliveCount() };
        const fresh = planLayout(attempt);
        const routes: { g: Group; r: ReturnType<typeof evaluateRoute>; hide: HideChoice }[] = [];
        let allConcealed = !!fresh;
        if (fresh) {
          for (let slot = 0; slot < 4; slot++) {
            const g = groupById(plan.partyGroups[slot]);
            if (!g || !groupLiving(g).length) continue;
            const to = fresh.spots[slot];
            const d = dist(leaderPos(), to);
            const hv = dirOf(heading);
            const hide: HideChoice = { index: fresh.indices[slot], feet: to, kind: fresh.kinds[slot], aheadCos: d > 1 ? (hv[0] * (to[0] - leaderPos()[0]) + hv[1] * (to[1] - leaderPos()[1])) / d : 1, score: 0 };
            const r = evaluateRoute(g, to, !visibleToSquad(to), 0);
            allConcealed &&= AmbushRelocateRules.isConcealedRoute(r.cand, cfg.relocate);
            routes.push({ g, r, hide });
          }
        }
        if (fresh && allConcealed) {
          plan.noRouteSearches = 0;
          for (const x of routes) if (x.r.path.length) startRun(x.g, plan.id, { hide: x.hide, path: x.r.path, cand: x.r.cand });
          plan.anchor = fresh.anchor; plan.heading = fresh.heading; plan.partySpots = fresh.spots;
          plan.relocations++;
        } else if (fresh && AmbushRelocateRules.noRouteOutcome(++plan.noRouteSearches, cfg.relocate) === 'stay') {
          P.noRoute++;
        } else {
          P.noRoute += fresh ? 1 : 0;
          plan.dissolved = true;
          for (const e of planEnemies) { e.plan = 0; e.party = 'single'; }
          for (const gid of plan.partyGroups) {
            const g = groupById(gid);
            if (g) { g.plan = 0; g.party = 'single'; if (!g.holdLimit) g.holdLimit = AmbushWatchRules.holdLimitSeconds(holdDraw(g.id), cfg.watch); }
          }
        }
        return;
      }
    } else plan.bypassedSeconds = 0;

    const next = AmbushPlanRules.nextPhase(tick, params);
    if (next !== plan.phase) {
      if (next === 'flank_block' && plan.flankAt < 0) {
        plan.flankAt = clock;
        const byHold = !tick.anyEngaged && tick.flanksUnwatched === 0;
        P.sprungByWatch += !tick.anyEngaged && !byHold ? 1 : 0;
        P.sprungByHold += byHold ? 1 : 0;
        for (const gid of plan.partyGroups) { const g = groupById(gid); if (g && !g.sprung) g.sprung = tick.anyEngaged ? 'engaged' : byHold ? 'hold' : 'watch'; }
      }
      plan.phase = next;
      plan.phaseSeconds = 0;
    } else plan.phaseSeconds += dt;
    plan.holdSeconds += plan.phase === 'holding' ? dt : 0;
    for (const e of planEnemies) {
      if (e.state === 'engaged') continue;
      if (AmbushPlanRules.springs(plan.phase, e.party, AmbushPlanRules.isRanged(e.type))) springMember(e);
      else if (e.state === 'relocating') continue;
      else { e.state = plan.phase === 'holding' ? 'holding' : 'hidden'; e.holdSeconds += plan.phase === 'holding' ? dt : 0; }
    }
  };

  const tickRuns = () => {
    for (let ri = runs.length - 1; ri >= 0; ri--) {
      const run = runs[ri];
      run.elapsed += dt;
      let spotted = false, allArrived = true, running = 0;
      for (let i = 0; i < run.members.length; i++) {
        const e = enemyById(run.members[i])!;
        if (e.state !== 'relocating') continue;
        running++;
        const legs = run.legs[i];
        while (run.leg[i] + 1 < legs.length && dist(e.pos, legs[run.leg[i]]) <= K.runLegAcceptCm) run.leg[i]++;
        const goal = legs[run.leg[i]];
        const step = run.speed * dt;
        const d = dist(e.pos, goal);
        if (d > 1e-6) { e.yaw = yawOf(sub(goal, e.pos)); e.pos = d <= step ? goal : add(e.pos, scale(sub(goal, e.pos), step / d)); }
        spotted ||= spottedBySquad(e);
        allArrived &&= run.leg[i] + 1 === legs.length && dist(e.pos, legs[legs.length - 1]) <= K.runArriveCm;
      }
      if (running === 0) { runs.splice(ri, 1); continue; }
      const outcome = AmbushRelocateRules.stepRun(spotted, allArrived, run.elapsed, run.limit);
      if (outcome === 'running') continue;
      const g = groupById(run.group)!;
      for (const id of run.members) {
        const e = enemyById(id)!;
        if (e.state !== 'relocating') continue;
        e.state = 'hidden';
        if (outcome !== 'spotted') e.yaw = yawOf(sub(leaderPos(), e.pos));
      }
      if (outcome === 'spotted') {
        P.runsSpotted++;
        if (run.plan > 0) { const plan = plans.find((p) => p.id === run.plan); if (plan) plan.spottedOnRoute = true; }
        spring(g, 'spotted');
      }
      runs.splice(ri, 1);
    }
  };

  const tickFollowers = () => {
    for (const gid of followers) {
      const g = groupById(gid);
      if (!g) continue;
      let i = 0;
      for (const e of groupLiving(g)) {
        if (e.state !== 'patrol' || !e.follower) continue;
        const want = add(leaderGround(), CombatPressureRules.groupOffset(i++, K.groupSpacingCm));
        const goal = world.projectFeet(want) ? want : leaderGround();
        if (dist(goal, e.guardPost) >= K.followRepostCm) { e.guardPost = goal; e.path = null; }
      }
    }
  };

  const tickRecycle = (squad: Vec2[], step: number) => {
    for (const e of living()) {
      const nearest = AmbushWatchRules.nearestDistance(squad, e.pos);
      e.farSeconds = nearest > cfg.recycleDistanceCm ? e.farSeconds + step : 0;
      if (e.farSeconds > 0 && CombatPressureRules.shouldRecycle(e.state === 'engaged', visibleToSquad(e.pos), nearest, e.farSeconds, cfg.recycleDistanceCm, cfg.recycleAfterSeconds)) {
        e.state = 'removed';
        P.recycled++;
      }
    }
  };

  // ---- enemy movement / perception / attacks --------------------------------------------------------------------------------------------
  const moveTowards = (e: Enemy, goal: Vec2, speed: number): boolean => {
    // Straight when no wall / tall cover (widened by the agent) touches the way - the same exact test the A* path's corners satisfy, through
    // the nav grid's wall index -, else along the A* path: planned when there is none, when the goal moved > 3 m from the path's end or
    // when the enemy is stuck, at most every 2 s (the enemy walks the old path to its end and then straight when the goal only crept).
    const straight = !world.nav.wallsBlock(e.pos, goal);
    let next = goal;
    if (!straight) {
      const stale = !e.path || dist(e.path[e.path.length - 1], goal) > 300 || e.stuckSeconds >= 0.5;
      if (stale && clock >= e.repathAt) {
        e.path = components.same(e.pos, goal) ? world.path(e.pos, goal) : null;
        e.repathAt = clock + 2;
      }
      if (e.path && e.path.length > 1) {
        while (e.path.length > 2 && dist(e.pos, e.path[1]) < 60) e.path.splice(1, 1);
        next = e.path[1];
      } else if (!e.path) return false;
    }
    const d = dist(e.pos, next);
    const step = speed * dt;
    if (d <= 1e-6) return true;
    e.yaw = yawOf(sub(next, e.pos));
    const cand = d <= step ? next : add(e.pos, scale(sub(next, e.pos), step / d));
    if (!world.nav.wallsBlock(cand, cand) || world.nav.wallsBlock(e.pos, e.pos)) { e.pos = cand; e.stuckSeconds = 0; }
    else {
      // the step runs into a cover's / wall's widened band (a spot 0.8 m off a wall, a group mate's offset): slide towards the nearest free
      // cell of the nav grid instead of standing there for ever
      e.stuckSeconds += dt;
      const free = nearestFreeCell(e.pos);
      if (free) { const fd = dist(e.pos, free); e.pos = fd <= step ? free : add(e.pos, scale(sub(free, e.pos), step / fd)); e.path = null; }
    }
    return dist(e.pos, goal) <= 60;
  };

  /** The centre of the nearest free nav cell within 3 cells (the pressure's ProjectPointToNavigation), or null. */
  const nearestFreeCell = (p: Vec2): Vec2 | null => {
    const nav = world.nav;
    const [ci, cj] = nav.cellOf(p);
    let best: Vec2 | null = null, bestD = Infinity;
    for (let dj = -3; dj <= 3; dj++) for (let di = -3; di <= 3; di++) {
      const i = ci + di, j = cj + dj;
      if (!nav.inside(i, j) || nav.blocked[j * nav.w + i]) continue;
      const c = nav.center(i, j);
      const d = dist(c, p);
      if (d < bestD && d > 1) { bestD = d; best = c; }
    }
    return best;
  };

  const perceive = (e: Enemy): boolean => {
    // Patrol perception (enemy_perception.json): sight (range, cone, a clear line, time to detect), proximity.
    let seeing = false;
    const enemySightCm = K.useSightConditions && K.sightConditions
      ? SightConditionsRules.enemySightRangeCm(
          e.stats.sightRangeM,
          e.type === 'MARKSMAN',
          K.sightConditions,
          K.weather ?? 'clear',
          K.sunElevationDeg ?? (K.hour !== undefined ? SightConditionsRules.sunElevationFromHour(K.hour) : 45),
        )
      : e.stats.sightRangeM * 100;
    for (const m of ops) {
      const d = dist(e.pos, m.pos);
      if (d <= PROXIMITY_CM && m.level === 0) return true;
      // the enemies stand on the street in the sim: level 0, x1 (an armed human on a roof would get the squad's height bonus - UE only)
      if (d > enemySightCm) continue;
      if (Math.abs(findDeltaAngleDeg(e.yaw, AmbushWatchRules.bearingDeg(e.pos, m.pos))) > e.stats.sightHalfAngleDeg) continue;
      if (world.los(e.pos, K.eyeLiftCm, m.pos, m.level * 300 + K.targetLiftCm)) { seeing = true; break; }
    }
    e.suspicion = seeing ? e.suspicion + dt / Math.max(0.1, e.stats.timeToDetectSeconds) : Math.max(0, e.suspicion - 0.5 * dt);
    return e.suspicion >= 1;
  };

  const engage = (e: Enemy, why: 'perceived' | 'shot' | 'pack') => {
    if (e.state === 'engaged' || e.state === 'dead' || e.state === 'removed') return;
    const g = groupById(e.group);
    if (e.kind === 'ambush' && g) {
      // shot at / spotted: the whole group fights; a plan's party engaged otherwise springs the plan (TickPlan's bAnyEngaged)
      if (e.plan > 0) { const plan = plans.find((p) => p.id === e.plan); if (plan && !plan.dissolved) plan.engagedOtherwise = true; }
      spring(g, 'engaged');
      return;
    }
    e.state = 'engaged';
    e.follower = false;
    void why;
    // the pack alert: mates within 15 m join
    if (g) for (const other of groupLiving(g)) if (other !== e && other.state !== 'engaged' && dist(other.pos, e.pos) <= PACK_ALERT_CM) { other.state = 'engaged'; other.follower = false; }
  };

  const pickTarget = (e: Enemy): Operative | null => {
    let best: Operative | null = null, bestD = Infinity;
    for (const m of ops) { const d = dist(e.pos, m.pos); if (d < bestD) { bestD = d; best = m; } }
    if (best) e.target = ops.indexOf(best);
    return best;
  };

  const hitOperative = (e: Enemy, m: Operative, damage: number, melee: boolean) => {
    const idx = ops.indexOf(m);
    m.underFire = true;
    // a hit from a side no look of his covers
    const bearing = AmbushWatchRules.bearingDeg(m.pos, e.pos);
    const looks = [m.bodyYaw, m.lookYaw, ...(m.target >= 0 && enemyById(m.target) ? [AmbushWatchRules.bearingDeg(m.pos, enemyById(m.target)!.pos)] : [])];
    if (!looks.some((l) => Math.abs(findDeltaAngleDeg(l, bearing)) <= cfg.watch.watchHalfAngleDeg)) unwatchedHits++;
    if (cheats.immortal) m.absorbed += damage; else { m.taken += damage; m.health = Math.max(0, m.health - damage); }
    // knockdowns (KnockdownRules): a heavy hit >= the threshold with the chance; a hound's pounce; the Cutter's leap
    let chance = 0;
    if (damage >= K.knockdownDamage) chance = Math.max(chance, 0.5);
    if (melee && e.type === 'HOUND' && e.runUp >= K.houndPounceRunUpSeconds && clock - e.lastPounce >= K.houndPounceCooldownSeconds) chance = Math.max(chance, e.stats.knockdownChance);
    if (melee && e.type === 'CUTTER') chance = Math.max(chance, e.stats.knockdownChance * 0.5);
    if (chance > 0 && clock >= m.immuneUntil && clock >= m.knockedUntil && fightRng() < chance) {
      m.knockedUntil = clock + K.knockdownSeconds;
      m.immuneUntil = m.knockedUntil + K.knockdownImmunitySeconds;
      m.knockdowns++;
      knockdowns++;
      if (e.type === 'HOUND') e.lastPounce = clock;
    }
    void idx;
  };

  const tickEngaged = (e: Enemy) => {
    const m = pickTarget(e);
    if (!m) return;
    const d = dist(e.pos, m.pos);
    const sameLevel = m.level === 0;
    // lost: it can neither see any operative nor get nearer (unreachable, the squad up on a roof) - it gives up after a while
    const sees = world.los(e.pos, K.eyeLiftCm, m.pos, m.level * 300 + K.targetLiftCm);
    const inReach = d <= e.stats.attackRangeCm && (e.stats.ranged ? sees : sameLevel);
    // lost: no sight, no reach and no PROGRESS - it has not come a metre nearer its target than it ever was in this chase (stuck, no path,
    // the squad up on a roof, a chase the squad outwalks) - for lostAfterSeconds: it gives up (the game's enemy loses its target and
    // returns to its patrol / search; the simulator removes it and counts it)
    if (d < e.chaseBestCm - 100) { e.chaseBestCm = d; e.lostSeconds = 0; }
    else if (!sees && !inReach) e.lostSeconds += dt;
    else e.lostSeconds = 0;
    if (e.lostSeconds >= K.lostAfterSeconds) { e.state = 'removed'; P.lost++; return; }
    if (e.stats.ranged) {
      const clear = world.los(e.pos, K.eyeLiftCm, m.pos, m.level * 300 + K.targetLiftCm);
      const inRange = d <= e.stats.attackRangeCm;
      if (!clear || d > e.stats.preferredRangeCm * 1.1) {
        // close in to the preferred range / a clear line
        if (d > e.stats.preferredRangeCm * 0.9 || !clear) moveTowards(e, m.pos, e.stats.speed);
      }
      if (clear && inRange && clock >= e.nextAttack) {
        e.nextAttack = clock + e.stats.cooldown;
        const covered = squadMode === 'at_wall' && coverTaken;
        const acc = e.stats.accuracy * (covered ? K.coverHitMultiplier : 1) * (clock < m.knockedUntil ? 1.33 : 1);
        m.underFire = true;
        if (fightRng() < acc) hitOperative(e, m, e.stats.damage, false);
      }
      return;
    }
    // melee: run to the target, strike in reach with a clear line (MeleeLineRules: no blow through a wall)
    if (!sameLevel) { moveTowards(e, m.pos, e.stats.speed); e.runUp = 0; return; }
    if (d > e.stats.attackRangeCm) {
      const before = e.pos;
      moveTowards(e, m.pos, e.stats.speed);
      e.runUp = dist(before, e.pos) >= e.stats.speed * dt * 0.9 ? e.runUp + dt : 0;
      e.nextAttack = Math.max(e.nextAttack, clock + K.meleeWindupSeconds); // the first blow after arriving winds up
      return;
    }
    if (clock >= e.nextAttack && !segmentBlocked(e.pos, m.pos, world.ground.walls, 0)) {
      e.nextAttack = clock + e.stats.cooldown * K.meleeCadence;
      m.underFire = true;
      hitOperative(e, m, e.stats.damage, true);
      e.runUp = 0;
    }
  };

  const tickPatrol = (e: Enemy) => {
    if (e.follower) {
      moveTowards(e, e.guardPost, e.stats.patrolSpeed * 1.25);
    } else if (e.escortOf >= 0) {
      const leader = enemyById(e.escortOf);
      if (leader && leader.state !== 'dead' && leader.state !== 'removed') {
        const k = Math.max(1, groupById(e.group)?.members.indexOf(e.id) ?? 1);
        const want = add(leader.pos, CombatPressureRules.groupOffset(k, K.groupSpacingCm));
        if (dist(e.pos, want) > 100) moveTowards(e, want, e.stats.patrolSpeed * K.patrolPace * 1.2);
        else e.yaw = leader.yaw;
      }
    } else if (e.loop.length > 1) {
      if (e.waitLeft > 0) { e.waitLeft -= dt; return; }
      const goal = e.loop[e.loopIndex % e.loop.length];
      if (moveTowards(e, goal, e.stats.patrolSpeed * K.patrolPace)) { e.loopIndex = (e.loopIndex + 1) % e.loop.length; e.waitLeft = cfg.patrolWaitSeconds; }
    }
    if (perceive(e)) engage(e, 'perceived');
  };

  // ---- the squad's fire (Aggressive posture: the nearest visible enemy in rifle range) ----------------------------------------------------
  const squadFire = () => {
    const cells = K.rifleHitChances;
    for (let i = 0; i < ops.length; i++) {
      const m = ops[i];
      if (clock < m.knockedUntil) { m.target = -1; continue; }
      const weaponId = K.squadWeapons?.[i] ?? 'm16';
      const weapon = (K.useWeaponRanges && K.weaponsConfig?.weapons[weaponId]) ? K.weaponsConfig.weapons[weaponId] : null;
      const maxRangeCm = weapon ? weapon.attackRangeM * 100 : K.rifleRangeCm;
      const returnFireRangeCm = weapon ? weapon.attackRangeM * 100 * 1.1 : K.rifleReturnFireRangeCm;
      const weaponDamage = weapon ? weapon.baseDamage : K.rifleDamage;
      const weaponInterval = weapon && weapon.fireRate > 0
        ? (weapon.name.toLowerCase().includes('sniper') ? weapon.fireRate : (weapon.fireRate <= 1.0 ? 1 / weapon.fireRate : weapon.fireRate))
        : K.rifleInterval;

      // the nearest enemy he can see (within the rifle's range; a fighting squad turns to every engaged enemy in sight)
      let best: Enemy | null = null, bestD = Infinity;
      for (const e of living()) {
        if (e.state === 'dead' || e.state === 'removed') continue; // killed by a mate's shot this very tick
        if (e.state === 'hidden' || e.state === 'holding') {
          // a hidden ambusher is seen only inside his field of view
          if (!spottedBySquad(e)) continue;
        }
        const d = dist(m.pos, e.pos);
        if (d > (e.state === 'engaged' ? returnFireRangeCm : maxRangeCm) || d >= bestD) continue;
        if (!world.los(m.pos, m.level * 300 + K.sightEyeCm, e.pos, K.targetLiftCm)) continue;
        best = e; bestD = d;
      }
      if (!best) { m.target = -1; m.sawAt = -1; continue; }
      if (m.target !== best.id) { if (m.sawAt < 0 || m.target !== best.id) m.sawAt = m.sawAt < 0 ? clock : m.sawAt; m.target = best.id; }
      if (firstSight < 0) firstSight = clock;
      if (clock < m.sawAt + K.reactionSeconds || clock < m.nextShot) continue;
      m.nextShot = clock + weaponInterval;
      m.rounds++;
      lastShotAt = clock;
      m.bodyYaw = AmbushWatchRules.bearingDeg(m.pos, best.pos);
      const cell = Math.min(cells.length - 1, Math.floor(bestD / K.rifleCellCm));
      if (best.state !== 'engaged') engage(best, 'shot');
      let hitChance = cells[cell];
      if (weapon && K.weaponsConfig) {
        hitChance *= WeaponRules.falloffMultiplier(
          bestD / 100,
          weapon.effectiveRangeM,
          weapon.attackRangeM,
          K.weaponsConfig.rangeFalloff.floorMultiplier,
          K.weaponsConfig.rangeFalloff.exponent,
        );
      }
      if (fightRng() < hitChance) {
        best.hp -= weaponDamage * (1 - K.enemyArmor);
        if (!best.stats.ranged) best.nextAttack = Math.max(best.nextAttack, clock + K.hitStaggerSeconds); // the hit reaction interrupts the blow
        if (best.hp <= 0) { best.state = 'dead'; P.killed++; }
      }
    }
  };

  // ---- the squad on its track / in a fight ---------------------------------------------------------------------------------------------
  const applyFrame = (f: TrackFrame) => {
    for (let i = 0; i < ops.length; i++) {
      const m = f.members[Math.min(i, f.members.length - 1)];
      ops[i].pos = m.pos; ops[i].bodyYaw = m.bodyYaw; ops[i].lookYaw = m.lookYaw; ops[i].level = f.level;
    }
    if (f.layer === 'ground') lastGround = f.members[0].pos;
  };

  const pickCover = (threat: Vec2): { face: Vec2; normal: Vec2; distance: number } | null => {
    if (K.coverSearchCm <= 0) return null;
    const leader = leaderPos();
    let best: { face: Vec2; normal: Vec2; distance: number; score: number } | null = null;
    for (const b of boxes) {
      const { axis, halfLength, halfThickness } = AmbushSpotRules.longAxis(b);
      if (2 * halfLength < K.coverMinWallCm || (b.heightCm > 0 && b.heightCm < K.coverMinHeightCm)) continue;
      const side: Vec2 = [-axis[1], axis[0]];
      for (const sign of [1, -1] as const) {
        const n: Vec2 = [side[0] * sign, side[1] * sign];
        const toLeader = sub(leader, b.center), toThreat = sub(threat, b.center);
        if (toLeader[0] * n[0] + toLeader[1] * n[1] <= 0) continue; // the leader on this face's side
        if (toThreat[0] * n[0] + toThreat[1] * n[1] >= 0) continue; // the threat beyond the wall
        const along = Math.max(-halfLength + K.coverEndInsetCm, Math.min(halfLength - K.coverEndInsetCm, toLeader[0] * axis[0] + toLeader[1] * axis[1]));
        const face = AmbushSpotRules.boxPoint(b, along, sign * halfThickness);
        const dThreat = dist(face, threat);
        if (dThreat < K.coverMinThreatCm) continue;
        const facing = (-(n[0]) * (threat[0] - face[0] - n[0] * 0) - n[1] * (threat[1] - face[1])) / Math.max(1, dThreat);
        if (facing < K.coverMinFacingCos) continue;
        const distance = dist(leader, face);
        if (distance > K.coverSearchCm) continue;
        const score = facing - 0.8 * (distance / K.coverSearchCm);
        if (!best || score > best.score) best = { face, normal: n, distance, score };
      }
    }
    return best;
  };

  const startFightPause = () => {
    pauseStart = clock;
    const threat = living().filter((e) => e.state === 'engaged').sort((a, b) => dist(a.pos, leaderPos()) - dist(b.pos, leaderPos()))[0] ?? living()[0];
    const cover = threat ? pickCover(threat.pos) : null;
    if (cover) {
      coverOrders++;
      squadMode = 'to_cover';
      coverFrom = ops.map((m) => m.pos);
      const along: Vec2 = [-cover.normal[1], cover.normal[0]];
      const standoff = add(cover.face, scale(cover.normal, 45));
      coverSlots = ops.map((_, i) => ({ pos: add(standoff, scale(along, (i - (ops.length - 1) / 2) * 100)), look: normYaw(yawOf(scale(cover.normal, -1)) + (i === 0 ? 0 : i % 2 ? 60 : -60)) }));
      coverTravel = Math.max(...coverFrom.map((p, i) => dist(p, coverSlots[i].pos))) / K.squadWalkSpeedCmS;
      coverStart = clock;
      coverTaken = false;
    } else {
      squadMode = 'halt';
    }
  };

  const tickSquad = () => {
    const fightNow = living().some((e) => e.state === 'engaged');
    if (fightNow && squadMode === 'walk') startFightPause();
    if (!fightNow && squadMode !== 'walk') { squadMode = 'walk'; coverTaken = false; }
    if (squadMode === 'walk') {
      trackT += dt;
      while (trackT >= track.lapSeconds) { trackT -= track.lapSeconds; laps++; }
      const idx = Math.min(track.frames.length - 1, Math.floor(trackT * track.hz));
      const prev = frame;
      frameIndex = idx;
      frame = track.frames[idx];
      distanceCm += dist(prev.members[0].pos, frame.members[0].pos) < 500 ? dist(prev.members[0].pos, frame.members[0].pos) : 0;
      furthest = Math.max(furthest, frame.waypoint);
      heading = frameHeading(track, idx);
      applyFrame(frame);
      // formation changes (a label held >= 0.5 s), wall hugs (at the wall after >= 1 s off)
      if (frame.formation !== labelHeld) { labelHeld = frame.formation; labelSince = clock; }
      else if (clock - labelSince >= 0.5 && labelHeld !== formationLabel) { formationLabel = labelHeld; formationChanges++; }
      const atWall = frame.atWall;
      if (atWall && !wasAtWall && clock - atWallSince >= 1) wallHugs++;
      if (!atWall && wasAtWall) atWallSince = clock;
      wasAtWall = atWall;
      return;
    }
    // a fight: the squad holds (the bot's cover at a wall, or a halt) until the fight ends or the hold runs out, then walks on firing
    const hold = squadMode === 'halt' ? K.fightHoldSeconds : K.coverHoldSeconds;
    if (clock - pauseStart >= hold) { squadMode = 'walk'; return; }
    if (squadMode === 'to_cover') {
      const k = coverTravel > 0 ? Math.min(1, (clock - coverStart) / coverTravel) : 1;
      for (let i = 0; i < ops.length; i++) {
        ops[i].pos = add(coverFrom[i], scale(sub(coverSlots[i].pos, coverFrom[i]), k));
        const mv = sub(coverSlots[i].pos, coverFrom[i]);
        ops[i].bodyYaw = Math.hypot(mv[0], mv[1]) > 1 ? yawOf(mv) : ops[i].bodyYaw;
        ops[i].lookYaw = coverSlots[i].look;
      }
      if (k >= 1) {
        squadMode = 'at_wall';
        coverTaken = true;
        coverTakenCount++;
        if (clock - atWallSince >= 1) wallHugs++;
        wasAtWall = true;
        if (labelHeld !== 'wall_layout') { labelHeld = 'wall_layout'; labelSince = clock; }
      }
    } else if (squadMode === 'at_wall') {
      coverSeconds += dt;
      for (let i = 0; i < ops.length; i++) { ops[i].bodyYaw = coverSlots[i].look; ops[i].lookYaw = coverSlots[i].look; }
      if (clock - labelSince >= 0.5 && labelHeld !== formationLabel) { formationLabel = labelHeld; formationChanges++; }
    }
  };

  // ---- contacts (FCombatLabRunCollector::SampleContacts) -----------------------------------------------------------------------------
  const sampleContacts = (squad: Vec2[], wBefore: Watcher[]) => {
    const w = watchers(); // the looks after the squad turned to fire (the collector samples the game's frame)
    void wBefore;
    const present = new Set<number>();
    const byId = new Map<number, { kind: ContactKind; position: Vec2; engaged: boolean; sprung: boolean; watched: boolean; seen: boolean; alive: number; spawnedAt: number }>();
    for (const e of living()) {
      const g = groupById(e.group)!;
      const id = g.plan > 0 || g.kind === 'coordinated' ? 100000 + (g.plan || g.id) : g.id;
      let s = byId.get(id);
      if (!s) { s = { kind: g.plan > 0 || g.kind === 'coordinated' ? 'coordinated' : g.kind === 'patrol' ? 'patrol' : 'ambush', position: e.pos, engaged: false, sprung: false, watched: false, seen: false, alive: 0, spawnedAt: g.spawnedAt }; byId.set(id, s); }
      s.alive++;
      s.engaged ||= e.state === 'engaged';
      s.sprung ||= e.state === 'engaged' && (g.sprung === 'watch' || g.sprung === 'hold' || g.sprung === 'plan');
      // a look covers it, or - as the game's operatives acquire a revealed enemy at once - an engaged member some operative has in sight
      s.watched ||= AmbushWatchRules.isWatched(w, e.pos, cfg.watch) || (e.state === 'engaged' && AmbushWatchRules.nearestDistance(squad, e.pos) <= K.sightRangeCm && visibleToSquad(e.pos));
      if (!s.seen && !contactIndex.has(id) && AmbushWatchRules.nearestDistance(squad, e.pos) <= K.contactRangeCm) s.seen = visibleToSquad(e.pos);
      present.add(id);
    }
    for (const [id, s] of byId) {
      const known = contactIndex.get(id);
      if (known !== undefined) {
        const c = contacts[known];
        if (c.reaction_sec < 0 && s.watched) c.reaction_sec = Math.round((clock - c.time) * 100) / 100;
        continue;
      }
      if (!s.seen && !s.engaged && !s.sprung) continue;
      const c: SimContact = {
        id: contacts.length + 1, kind: s.kind, trigger: s.sprung ? 'sprung' : s.engaged ? 'engaged' : 'seen', time: Math.round(clock * 100) / 100, position: s.position, formation: formationLabel,
        watched_at_start: s.watched, unwatched_strike: s.sprung && !s.watched, reaction_sec: s.watched ? 0 : -1, zone: world.zoneAt(s.position), since_spawn_sec: Math.round((clock - s.spawnedAt) * 10) / 10, enemies: s.alive,
      };
      contactIndex.set(id, contacts.length);
      contacts.push(c);
    }
  };

  // ---- the main loop -----------------------------------------------------------------------------------------------------------------
  applyFrame(frame);
  const steps = Math.round(o.durationSec / dt);
  for (let step = 0; step < steps; step++) {
    clock = Math.round((step + 1) * dt * 1000) / 1000;
    refreshAlive();
    tickSquad();
    const squad = squadPositions();
    const w = watchers();
    for (const m of ops) m.underFire = false;
    // enemies
    for (const e of enemies) {
      if (e.state === 'dead' || e.state === 'removed') continue;
      if (e.state === 'engaged') tickEngaged(e);
      else if (e.state === 'patrol') tickPatrol(e);
    }
    // gunshots this tick: every patrol (its own senses) within its hearing joins the fight
    if (lastShotAt === clock - dt || lastShotAt === clock) {
      for (const e of living()) if (e.state === 'patrol' && AmbushWatchRules.nearestDistance(squad, e.pos) <= e.stats.hearGunshotM * 100) engage(e, 'perceived');
    }
    squadFire();
    refreshAlive();
    tickAmbushers(squad, w);
    recycleTimer += dt;
    if (recycleTimer >= RECYCLE_STEP) { tickRecycle(squad, recycleTimer); recycleTimer = 0; }
    // the schedule
    switch (CombatPressureRules.decideEvent(clock, dueAt, aliveCount(), cfg.maxAlive)) {
      case 'not_due': break;
      case 'delayed_cap_full': if (!delayLogged) { delayLogged = true; P.eventsDelayed++; } break;
      case 'spawn': {
        delayLogged = false;
        const spawned = triggerEvent();
        dueAt = clock + (spawned ? cfg.intervalSeconds : Math.min(10, cfg.intervalSeconds));
        P.events += spawned ? 1 : 0;
        break;
      }
    }
    // fights / under fire
    refreshAlive();
    inFight = living().some((e) => e.state === 'engaged');
    if (inFight && !wasInFight) fights++;
    if (inFight) fightSeconds += dt;
    wasInFight = inFight;
    if (ops.some((m) => m.underFire)) underFireSeconds += dt;
    sampleContacts(squad, w);
    if (o.onTick) o.onTick({ t: clock, squad, enemies: living().map((e) => ({ pos: e.pos, type: e.type, state: e.state, group: e.group })), inFight });
  }
  for (const g of groups) if ((g.kind === 'ambush' || g.kind === 'coordinated') && !g.sprung && g.spotIndex >= 0) spotDiag[g.spotIndex].never_struck++;

  const reactions = contacts.filter((c) => c.reaction_sec >= 0).map((c) => c.reaction_sec);
  const byKind: Record<ContactKind, number> = { patrol: 0, ambush: 0, coordinated: 0 };
  const byTrigger: Record<ContactTrigger, number> = { seen: 0, engaged: 0, sprung: 0 };
  for (const c of contacts) { byKind[c.kind]++; byTrigger[c.trigger]++; }
  const r2 = (v: number) => Math.round(v * 100) / 100;
  const metrics: RunMetrics = {
    seed: o.seed, duration_sec: o.durationSec, contacts: contacts.length, by_kind: byKind, by_trigger: byTrigger,
    unwatched_strikes: contacts.filter((c) => c.unwatched_strike).length, never_looked: contacts.filter((c) => c.reaction_sec < 0).length,
    reaction_mean_sec: reactions.length ? r2(reactions.reduce((a, b) => a + b, 0) / reactions.length) : 0, first_sight_sec: r2(firstSight),
    fights, fight_seconds: r2(fightSeconds), formation_changes: formationChanges, wall_hugs: wallHugs, cover_orders: coverOrders, cover_taken: coverTakenCount, cover_seconds: r2(coverSeconds),
    knockdowns, damage_taken: r2(ops.reduce((a, m) => a + m.taken, 0)), damage_absorbed: r2(ops.reduce((a, m) => a + m.absorbed, 0)), rounds: ops.reduce((a, m) => a + m.rounds, 0),
    enemies_spawned: P.spawned, enemies_killed: P.killed,
    route: { laps, progress: r2(Math.min(1, (laps * track.frames.length + frameIndex) / track.frames.length)), distance_m: Math.round(distanceCm / 100), furthest_waypoint: furthest },
    pressure: { events: P.events, events_delayed: P.eventsDelayed, sprung_by_watch: P.sprungByWatch, sprung_by_hold: P.sprungByHold, relocations: P.relocations, runs_spotted: P.runsSpotted, no_route: P.noRoute, followers: P.followers, recycled: P.recycled, lost: P.lost },
    under_fire_seconds: r2(underFireSeconds), unwatched_hits: unwatchedHits,
  };
  void members;
  return { metrics, contacts, events, coordinated, spots: spotDiag, contact_positions: contacts.map((c) => c.position) };
}

/** Connected components of the enemies' nav grid (a flood fill once per world): the pressure's "reachable on foot" in O(1). */
function navComponents(world: SimWorld): { same: (a: Vec2, b: Vec2) => boolean } {
  const nav = world.nav;
  const W = nav.w, H = nav.h;
  const comp = new Int32Array(W * H).fill(-1);
  let next = 0;
  const stack: number[] = [];
  for (let s = 0; s < W * H; s++) {
    if (nav.blocked[s] || comp[s] !== -1) continue;
    comp[s] = next;
    stack.push(s);
    while (stack.length) {
      const k = stack.pop()!;
      const ki = k % W, kj = Math.floor(k / W);
      for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) {
        if (!di && !dj) continue;
        const ni = ki + di, nj = kj + dj;
        if (ni < 0 || nj < 0 || ni >= W || nj >= H) continue;
        const nk = nj * W + ni;
        if (nav.blocked[nk] || comp[nk] !== -1) continue;
        if (di && dj && (nav.blocked[kj * W + ni] || nav.blocked[nj * W + ki])) continue;
        comp[nk] = next;
        stack.push(nk);
      }
    }
    next++;
  }
  const at = (p: Vec2): number => {
    const [i, j] = nav.cellOf(p);
    // the nearest free cell within 2 (the pressure's ProjectPointToNavigation extent)
    for (let r = 0; r <= 2; r++) for (let dj = -r; dj <= r; dj++) for (let di = -r; di <= r; di++) {
      const ni = i + di, nj = j + dj;
      if (ni < 0 || nj < 0 || ni >= W || nj >= H) continue;
      const c = comp[nj * W + ni];
      if (c !== -1) return c;
    }
    return -1;
  };
  return { same: (a, b) => { const ca = at(a); return ca !== -1 && ca === at(b); } };
}

/** The obstacle boxes a run uses (exported for tests / the UI). */
export const worldBoxes = (world: SimWorld): readonly PressureBox[] => world.boxes;
export type { AmbushSpot };
