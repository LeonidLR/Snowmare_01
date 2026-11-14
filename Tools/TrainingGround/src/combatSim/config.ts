// Training Ground / combat simulator: the pressure configuration read from a level JSON "pressure" block exactly as the game parses it
// (LevelJsonRules::ParsePressureSettings; the field table, defaults and clamps come from src/combatLab/pressureSettings.ts, golden-checked
// against the C++), the SIMULATOR'S OWN constants (the simplified fight model, docs/design/levelgen/COMBAT_SIM.md "Constants") and the
// enemy archetype stats it fights with. Pure.

import { fieldByPath, fieldValue, isJsonObject, typeWeights } from '../combatLab/pressureSettings.ts';
import type { AmbushSpotParams, EnemyType, PlanParams, RelocateParams, SightConditionsConfig, TypeWeight, WatchParams, WeaponsConfig } from './rules.ts';
import { DEFAULT_SIGHT_CONDITIONS, DEFAULT_WEAPONS } from './rules.ts';
import type { Vec2 } from '../movement/geometry.ts';

/** FCombatPressureConfig. */
export interface PressureConfig {
  enabled: boolean;
  intervalSeconds: number;
  firstEventSeconds: number;
  groupMin: number;
  groupMax: number;
  maxAlive: number;
  patrolShare: number;
  seed: number;
  typeWeights: TypeWeight[];
  wallClearanceCm: number;
  minSquadDistanceCm: number;
  maxSquadDistanceCm: number;
  patrolPoints: number;
  patrolRadiusCm: number;
  patrolWaitSeconds: number;
  ambushMinDistanceCm: number;
  ambushMaxDistanceCm: number;
  watch: WatchParams;
  spots: AmbushSpotParams;
  authoredPoints: Vec2[];
  coordinated: PlanParams;
  relocate: RelocateParams;
  recycleDistanceCm: number;
  recycleAfterSeconds: number;
}

const num = (pressure: unknown, path: string): number => fieldValue(pressure, fieldByPath(path)!) as number;
const bool = (pressure: unknown, path: string): boolean => fieldValue(pressure, fieldByPath(path)!) as boolean;
/** The C++ "at least the other + offset" clamps (FMath::Max). */
const atLeast = (v: number, floor: number) => Math.max(v, floor);

/**
 * The config of a "pressure" block (the level JSON's; absent keys = the C++ defaults; the C++ clamps applied: a min floor, "max >= min
 * (+ offset)"). An absent / non-object block = the pressure OFF (as the game).
 */
export function parsePressureConfig(pressure: unknown): PressureConfig {
  const p = isJsonObject(pressure) ? pressure : {};
  const weights = typeWeights(p);
  const types: TypeWeight[] = (Object.entries(weights) as [EnemyType, number][]).filter(([, w]) => w > 0).map(([type, weight]) => ({ type, weight }));
  const authored: Vec2[] = [];
  const pts = isJsonObject(p.ambush) ? (p.ambush as Record<string, unknown>).points : undefined;
  if (Array.isArray(pts)) for (const q of pts) if (Array.isArray(q) && q.length === 2 && typeof q[0] === 'number' && typeof q[1] === 'number') authored.push([q[0], q[1]]);
  const groupMin = Math.max(1, Math.round(num(p, 'group_min')));
  const ambushMin = Math.max(0, num(p, 'ambush.min_squad_distance_cm'));
  const patrolMin = Math.max(0, num(p, 'min_squad_distance_cm'));
  const planMin = Math.max(0, num(p, 'ambush.coordinated.min_squad_distance_cm'));
  const bodiesMin = Math.max(4, Math.round(num(p, 'ambush.coordinated.bodies_min')));
  const holdMin = Math.max(0, num(p, 'ambush.hold_strike_min_sec'));
  const doorMin = Math.max(0, num(p, 'ambush.spots.doorway_min_cm'));
  const assaultMin = Math.max(0, num(p, 'ambush.coordinated.assault_min_sec'));
  return {
    enabled: isJsonObject(pressure) && bool(p, 'enabled'),
    intervalSeconds: Math.max(5, num(p, 'interval_sec')),
    firstEventSeconds: Math.max(0, num(p, 'first_event_sec')),
    groupMin,
    groupMax: atLeast(Math.round(num(p, 'group_max')), groupMin),
    maxAlive: Math.max(1, Math.round(num(p, 'max_alive'))),
    patrolShare: Math.min(1, Math.max(0, num(p, 'patrol_share'))),
    seed: Math.round(num(p, 'seed')),
    typeWeights: types,
    wallClearanceCm: Math.max(0, num(p, 'wall_clearance_cm')),
    minSquadDistanceCm: patrolMin,
    maxSquadDistanceCm: atLeast(num(p, 'max_squad_distance_cm'), patrolMin + 100),
    patrolPoints: Math.min(12, Math.max(2, Math.round(num(p, 'patrol.points')))),
    patrolRadiusCm: Math.max(200, num(p, 'patrol.radius_cm')),
    patrolWaitSeconds: Math.max(0, num(p, 'patrol.wait_sec')),
    ambushMinDistanceCm: ambushMin,
    ambushMaxDistanceCm: atLeast(num(p, 'ambush.max_squad_distance_cm'), ambushMin + 100),
    watch: {
      watchHalfAngleDeg: Math.min(180, Math.max(0, num(p, 'ambush.watch_half_angle_deg'))),
      watchRangeCm: Math.max(0, num(p, 'ambush.watch_range_cm')),
      strikeRangeCm: Math.max(100, num(p, 'ambush.strike_range_cm')),
      holdStrikeMinSeconds: holdMin,
      holdStrikeMaxSeconds: atLeast(num(p, 'ambush.hold_strike_max_sec'), holdMin),
    },
    spots: {
      wallStandoffCm: Math.max(50, num(p, 'ambush.spots.wall_standoff_cm')),
      endInsetCm: Math.max(0, num(p, 'ambush.spots.end_inset_cm')),
      minWallLengthCm: Math.max(0, num(p, 'ambush.spots.min_wall_length_cm')),
      joinedGapCm: Math.max(1, num(p, 'ambush.spots.joined_gap_cm')),
      doorwayMinCm: doorMin,
      doorwayMaxCm: atLeast(num(p, 'ambush.spots.doorway_max_cm'), doorMin),
      authoredBonus: num(p, 'ambush.spots.authored_bonus'),
      doorwayBonus: num(p, 'ambush.spots.doorway_bonus'),
      mergeCm: Math.max(0, num(p, 'ambush.spots.merge_cm')),
    },
    authoredPoints: authored,
    coordinated: {
      share: Math.min(1, Math.max(0, num(p, 'ambush.coordinated.share'))),
      bodiesMin,
      bodiesMax: atLeast(Math.round(num(p, 'ambush.coordinated.bodies_max')), bodiesMin),
      maxAlive: Math.max(4, Math.round(num(p, 'ambush.coordinated.max_alive'))),
      minSquadDistanceCm: planMin,
      maxSquadDistanceCm: atLeast(num(p, 'ambush.coordinated.max_squad_distance_cm'), planMin + 100),
      anchorAheadCm: num(p, 'ambush.coordinated.anchor_ahead_cm'),
      flankOffsetCm: Math.max(100, num(p, 'ambush.coordinated.flank_offset_cm')),
      frontOffsetCm: Math.max(100, num(p, 'ambush.coordinated.front_offset_cm')),
      rearOffsetCm: Math.max(100, num(p, 'ambush.coordinated.rear_offset_cm')),
      minSeparationCm: Math.max(0, num(p, 'ambush.coordinated.min_separation_cm')),
      triggerRangeCm: Math.max(100, num(p, 'ambush.coordinated.trigger_range_cm')),
      assaultMinSeconds: assaultMin,
      assaultMaxSeconds: atLeast(num(p, 'ambush.coordinated.assault_max_sec'), assaultMin),
      blockDistanceCm: Math.max(100, num(p, 'ambush.coordinated.block_distance_cm')),
      minRanged: Math.min(6, Math.max(0, Math.round(num(p, 'ambush.coordinated.min_ranged')))),
    },
    relocate: {
      enabled: bool(p, 'ambush.relocate.enabled'),
      afterSeconds: Math.max(0, num(p, 'ambush.relocate.after_sec')),
      behindAngleDeg: Math.min(180, Math.max(0, num(p, 'ambush.relocate.behind_angle_deg'))),
      farCm: Math.max(0, num(p, 'ambush.relocate.far_cm')),
      minAheadCos: Math.min(1, Math.max(-1, num(p, 'ambush.relocate.min_ahead_cos'))),
      routeSampleCm: Math.max(25, num(p, 'ambush.relocate.route_sample_cm')),
      maxRouteCm: Math.max(500, num(p, 'ambush.relocate.max_route_cm')),
      runSpeedScale: Math.min(2, Math.max(0.3, num(p, 'ambush.relocate.run_speed_scale'))),
      noRouteTries: Math.max(1, Math.round(num(p, 'ambush.relocate.no_route_tries'))),
      noRoutePatrol: bool(p, 'ambush.relocate.no_route_patrol'),
    },
    recycleDistanceCm: Math.max(0, num(p, 'recycle.distance_cm')),
    recycleAfterSeconds: Math.max(0, num(p, 'recycle.after_sec')),
  };
}

/** The game's cheats block (COMBAT_LAB.md section 3): both ON by default in the simulator, like the Unreal bot runs. */
export interface SimCheats { immortal: boolean; infiniteAmmo: boolean }

/**
 * The simulator's enemy archetype stats (the game's are the user's DA_Enemy_* assets - Unreal is the master copy; these are the
 * simulator's stand-ins, calibrated against the Unreal batches, docs/design/levelgen/COMBAT_SIM.md "Constants"). cm, cm/s, s, HP.
 */
export interface EnemyStats {
  hp: number;
  /** Combat move speed (BaseWalkSpeed), cm/s; the patrol walk = patrolSpeed. */
  speed: number;
  patrolSpeed: number;
  ranged: boolean;
  attackRangeCm: number;
  /** Ranged: the standoff it keeps, cm (0 = none). */
  preferredRangeCm: number;
  damage: number;
  cooldown: number;
  /** Ranged hit chance at the preferred range (melee: always hits when in range). */
  accuracy: number;
  /** A hit can knock an operative down (heavy hit / pounce), probability per qualifying hit. */
  knockdownChance: number;
  /** enemy_perception.json: sight range (m), half angle (deg), time to detect (s), gunshot hearing (m). */
  sightRangeM: number;
  sightHalfAngleDeg: number;
  timeToDetectSeconds: number;
  hearGunshotM: number;
}

export const ENEMY_STATS: Record<EnemyType, EnemyStats> = {
  HOUND: { hp: 60, speed: 540, patrolSpeed: 200, ranged: false, attackRangeCm: 180, preferredRangeCm: 0, damage: 12, cooldown: 1.0, accuracy: 1, knockdownChance: 0.35, sightRangeM: 15, sightHalfAngleDeg: 70, timeToDetectSeconds: 0.6, hearGunshotM: 45 },
  FROSTBITTEN: { hp: 90, speed: 260, patrolSpeed: 150, ranged: false, attackRangeCm: 180, preferredRangeCm: 0, damage: 14, cooldown: 1.4, accuracy: 1, knockdownChance: 0, sightRangeM: 20, sightHalfAngleDeg: 50, timeToDetectSeconds: 1.0, hearGunshotM: 35 },
  SPITTER: { hp: 65, speed: 300, patrolSpeed: 160, ranged: true, attackRangeCm: 1500, preferredRangeCm: 1200, damage: 10, cooldown: 2.0, accuracy: 0.7, knockdownChance: 0, sightRangeM: 25, sightHalfAngleDeg: 50, timeToDetectSeconds: 0.8, hearGunshotM: 40 },
  CUTTER: { hp: 80, speed: 420, patrolSpeed: 180, ranged: false, attackRangeCm: 150, preferredRangeCm: 0, damage: 22, cooldown: 1.5, accuracy: 1, knockdownChance: 0.25, sightRangeM: 25, sightHalfAngleDeg: 55, timeToDetectSeconds: 0.7, hearGunshotM: 40 },
  MARKSMAN: { hp: 80, speed: 300, patrolSpeed: 160, ranged: true, attackRangeCm: 4500, preferredRangeCm: 2800, damage: 45, cooldown: 4.5 /* aim_duration 2 + shot_cooldown 2.5 */, accuracy: 0.6, knockdownChance: 0.5, sightRangeM: 45, sightHalfAngleDeg: 30, timeToDetectSeconds: 1.0, hearGunshotM: 40 },
};

/**
 * The simulator's own constants (not the game's data; tuned in the calibration, COMBAT_SIM.md). Every number the fight model needs that
 * the pressure config does not carry.
 */
export interface SimConstants {
  /** The simulation step, s (the game's ambush / pressure tick is 0.1 s). */
  dt: number;
  /** The operatives' eye over their feet and an enemy's chest over its feet for the pressure's sight lines (CombatPressureLocal), cm. */
  eyeLiftCm: number;
  targetLiftCm: number;
  /** The real eyes for "spotted" / sight (UTacticalSightSubsystem: a standing man's eye), cm. */
  sightEyeCm: number;
  /** An operative sees an enemy within this (MaxSightCm) and within +- half angle of one of his looks (UTacticalSightSubsystem::CanOperativeSee: the stance FOV 140 deg round his facing), cm / deg. */
  sightRangeCm: number;
  sightHalfAngleDeg: number;
  /** A contact opens when a group member has a line of sight to the squad within this (the bot's ContactRangeCm), cm. */
  contactRangeCm: number;
  /** Squad members' spacing round a group's point (CombatPressureLocal::GroupSpacingCm), cm. */
  groupSpacingCm: number;
  /** Spawn-spot candidates per patrol event (CombatPressureLocal::SpotCandidates). */
  spotCandidates: number;
  /** Relocation: new spot >= this from the old one, routes tried per search (CombatPressureLocal). */
  relocateAvoidCm: number;
  relocateRoutes: number;
  runLegAcceptCm: number;
  runArriveCm: number;
  followStepSeconds: number;
  followRepostCm: number;
  /** The squad's rifle (weapons_tuning.json m16): damage, shot interval, range, the hit chance per 3 m cell, armour reduction. */
  rifleDamage: number;
  rifleInterval: number;
  /** Opens fire on an enemy within this (attack_range_m 25); returns fire at an ENGAGED attacker in sight up to returnFireRangeCm. */
  rifleRangeCm: number;
  rifleReturnFireRangeCm: number;
  rifleCellCm: number;
  rifleHitChances: number[];
  enemyArmor: number;
  /** An operative's reaction: from seeing an enemy to the first shot, s. */
  reactionSeconds: number;
  /** The bot's cover at a wall (BotCoverRules defaults / the level JSON "bot_route"."fight_cover"). */
  coverSearchCm: number;
  coverHoldSeconds: number;
  coverMinWallCm: number;
  coverMinHeightCm: number;
  coverMinThreatCm: number;
  coverMinFacingCos: number;
  coverEndInsetCm: number;
  /** No wall: the squad halts where it stands this long at most (bot_route.fight_hold_sec), s. */
  fightHoldSeconds: number;
  /** The squad's walk in a fight (pace.walk_speed), cm/s. */
  squadWalkSpeedCmS: number;
  /** In cover the far side's ranged hits are reduced to this share; melee needs the enemy on the squad's side. */
  coverHitMultiplier: number;
  /** A heavy hit (>= this damage) may knock down (KnockdownRules: 40 HP, then the chance), the time down (fall + downed + get-up), s. */
  knockdownDamage: number;
  knockdownSeconds: number;
  knockdownImmunitySeconds: number;
  /** A hound's pounce needs this much run-up (KnockdownRules), and knocks down at most this often, s. */
  houndPounceRunUpSeconds: number;
  houndPounceCooldownSeconds: number;
  /** The route-mode squad hold at the end wall (the bot_route's `wall` action hold), s. */
  endWallHoldSeconds: number;
  /** Seconds a hole drop takes (the Unreal runs: 2.7-3.0 s). */
  dropSeconds: number;
  /** Patrol escorts / followers: pace factor of the group's walk. */
  patrolPace: number;
  /** An engaged enemy that can neither reach nor see the squad this long is LOST (it gives up; counted apart), s. */
  lostAfterSeconds: number;
  /** A rifle hit staggers a melee enemy: its next blow waits at least this long (the hit-reaction montage), s. */
  hitStaggerSeconds: number;
  /** The enemy stand-ins' HP x this (the calibration knob for the fight length / rounds per kill; the game's HP live in the DA_Enemy_* assets). */
  enemyHpScale: number;
  /** A melee enemy's blows come every cooldown x this (the attack animation, the re-positioning round the squad) and its first blow after a wind-up, s. */
  meleeCadence: number;
  meleeWindupSeconds: number;
  /** T10: Sight conditions (weather / time of day). When true, sight range scales with weather & hour / sun elevation. */
  useSightConditions?: boolean;
  weather?: string;
  sunElevationDeg?: number;
  hour?: number;
  sightConditions?: SightConditionsConfig;
  /** T10: Weapon ranges & engagement bands. When true, weapon attack range, effective range and falloff are used. */
  useWeaponRanges?: boolean;
  weaponsConfig?: WeaponsConfig;
  squadWeapons?: string[];
}

export const DEFAULT_SIM_CONSTANTS: SimConstants = {
  dt: 0.1,
  eyeLiftCm: 150, // the actor location (capsule centre ~90) + EyeLiftCm 60
  targetLiftCm: 110,
  sightEyeCm: 160,
  sightRangeCm: 6000,
  sightHalfAngleDeg: 70, // the game's standing / crouching FOV is 140 deg in all (Codex.Sight.FovStandDeg; the fight keeps it, FovFight 1)
  contactRangeCm: 3000,
  groupSpacingCm: 220,
  spotCandidates: 72,
  relocateAvoidCm: 600,
  relocateRoutes: 8,
  runLegAcceptCm: 150,
  runArriveCm: 120,
  followStepSeconds: 2,
  followRepostCm: 300,
  rifleDamage: 18,
  rifleInterval: 1.54, // weapons_tuning.json fire_rate 0.65 read as shots / s (1.54 s a shot): with the HP scale the calibration's rounds, fight time and knockdowns land in the band
  rifleRangeCm: 2500,
  rifleReturnFireRangeCm: 3500,
  rifleCellCm: 312.5,
  rifleHitChances: [0.95, 0.85, 0.75, 0.65, 0.5, 0.5, 0.5, 0.5, 0.5, 0.47, 0.44, 0.41, 0.38, 0.35, 0.32, 0.29, 0.26],
  enemyArmor: 0.1,
  reactionSeconds: 0.6,
  coverSearchCm: 1500,
  coverHoldSeconds: 45,
  coverMinWallCm: 200,
  coverMinHeightCm: 120,
  coverMinThreatCm: 400,
  coverMinFacingCos: 0.5,
  coverEndInsetCm: 60,
  fightHoldSeconds: 20,
  squadWalkSpeedCmS: 220,
  coverHitMultiplier: 0.35,
  knockdownDamage: 40,
  knockdownSeconds: 2.2,
  knockdownImmunitySeconds: 1.5,
  houndPounceRunUpSeconds: 1,
  houndPounceCooldownSeconds: 12,
  endWallHoldSeconds: 3,
  dropSeconds: 2.8,
  patrolPace: 1,
  lostAfterSeconds: 30,
  hitStaggerSeconds: 0.6,
  enemyHpScale: 1.5, // the stand-in HP x 1.5 (calibration 2026-11-02: fights 0.8 x the Unreal length, rounds 1.0 x, knockdowns 0.9 x - at x 1 the fights were 0.44 x)
  meleeCadence: 2.5,
  meleeWindupSeconds: 1.0,
  useSightConditions: false,
  weather: 'clear',
  hour: 12,
  sightConditions: DEFAULT_SIGHT_CONDITIONS,
  useWeaponRanges: false,
  weaponsConfig: DEFAULT_WEAPONS,
  squadWeapons: ['m16', 'shotgun', 'sniper_rifle'],
};

/** The fight-cover block of a level JSON "bot_route" (BotCoverRules params) applied over the constants. */
export function withBotRoute(constants: SimConstants, botRoute: unknown): SimConstants {
  if (!isJsonObject(botRoute)) return constants;
  const out = { ...constants };
  if (typeof botRoute.fight_hold_sec === 'number') out.fightHoldSeconds = botRoute.fight_hold_sec;
  const fc = botRoute.fight_cover;
  if (isJsonObject(fc)) {
    if (typeof fc.search_cm === 'number') out.coverSearchCm = fc.search_cm;
    if (typeof fc.hold_sec === 'number') out.coverHoldSeconds = fc.hold_sec;
    if (typeof fc.min_wall_cm === 'number') out.coverMinWallCm = fc.min_wall_cm;
    if (typeof fc.min_height_cm === 'number') out.coverMinHeightCm = fc.min_height_cm;
    if (typeof fc.min_threat_cm === 'number') out.coverMinThreatCm = fc.min_threat_cm;
    if (typeof fc.min_facing_cos === 'number') out.coverMinFacingCos = fc.min_facing_cos;
    if (fc.enabled === false) out.coverSearchCm = 0;
  }
  return out;
}

/** The environment block of a level JSON (weather, hour, sun elevation) applied over the constants. */
export function withEnvironment(constants: SimConstants, environment: unknown): SimConstants {
  if (!isJsonObject(environment)) return constants;
  const out = { ...constants };
  if (typeof environment.weather === 'string') out.weather = environment.weather;
  if (typeof environment.hour === 'number') out.hour = environment.hour;
  if (typeof environment.sun_elevation_deg === 'number') out.sunElevationDeg = environment.sun_elevation_deg;
  return out;
}
