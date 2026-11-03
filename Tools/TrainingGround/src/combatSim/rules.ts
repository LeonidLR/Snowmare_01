// Training Ground / combat simulator (docs/design/levelgen/COMBAT_SIM.md): FAITHFUL ports of the game's pure C++ pressure rules -
// CombatPressureRules, AmbushSpotRules, AmbushWatchRules, AmbushPlanRules, AmbushRelocateRules (Source/CodexTactics/Public/Combat/*,
// Public/AI/AmbushWatchRules.h). Same inputs -> same outputs as the C++ (tested in combatSim.test.ts on the C++ tests' hand-made
// cases). The C++ is the user-approved reference: change it there first, then here. Pure (no DOM / Node), UE conventions (cm, yaw =
// atan2(dy, dx) degrees). Plain objects, not TS namespaces (Node runs the sources in strip-only mode).

import type { Vec2 } from '../movement/geometry.ts';
import { dist, sub } from '../movement/geometry.ts';

// --- angles (FMath) -----------------------------------------------------------------------------------------------------------------

/** FMath::FindDeltaAngleDegrees: the signed shortest delta A1 -> A2 in (-180, 180]. */
export function findDeltaAngleDeg(a1: number, a2: number): number {
  let delta = a2 - a1;
  if (delta > 180) delta -= 360;
  else if (delta < -180) delta += 360;
  return delta;
}

/** FMath::UnwindDegrees: into (-180, 180]. */
export function unwindDeg(a: number): number {
  let r = a;
  while (r > 180) r -= 360;
  while (r < -180) r += 360;
  return r;
}

const DEG = Math.PI / 180;
const MAX_FLT = 3.4028235e38;

function normalizeAxis(a: Vec2): Vec2 {
  const L = Math.hypot(a[0], a[1]);
  return L < 1e-8 ? [1, 0] : [a[0] / L, a[1] / L];
}

// --- types --------------------------------------------------------------------------------------------------------------------------

/** FPressureBox: a wall / obstacle as a 2D oriented box (the level's blocking static meshes). */
export interface PressureBox {
  center: Vec2;
  /** Unit axis of the box's local X (local Y = rotated +90 deg). */
  axisX: Vec2;
  /** Half sizes along the local X / Y, cm. */
  halfSize: Vec2;
  /** Height, cm (0 = unknown). */
  heightCm: number;
}

export type SpawnKind = 'patrol' | 'ambush' | 'coordinated';
export type EnemyType = 'HOUND' | 'FROSTBITTEN' | 'SPITTER' | 'CUTTER' | 'MARKSMAN';
export const ENEMY_TYPES: readonly EnemyType[] = ['HOUND', 'FROSTBITTEN', 'SPITTER', 'CUTTER', 'MARKSMAN'];
export interface TypeWeight { type: EnemyType; weight: number }

export type SpawnVerdict = 'ok' | 'near_wall' | 'near_squad' | 'out_of_bounds';
export type EventDecision = 'not_due' | 'spawn' | 'delayed_cap_full';

export interface Bounds2 { min: Vec2; max: Vec2 }

/** FPressureSpotInput. */
export interface SpotInput {
  kind: SpawnKind;
  distanceCm: number;
  minDistanceCm: number;
  maxDistanceCm: number;
  aheadCos: number;
  concealed: boolean;
  watched: boolean;
  kindBonus: number;
}

// --- AmbushWatchRules (first: the others use its distances) -----------------------------------------------------------------------------

export type LookSource = 'body' | 'sector' | 'aim';
export interface Watcher { position: Vec2; lookYawDeg: number; source: LookSource }

/** FAmbushWatchParams. */
export interface WatchParams { watchHalfAngleDeg: number; watchRangeCm: number; strikeRangeCm: number; holdStrikeMinSeconds: number; holdStrikeMaxSeconds: number }
export const DEFAULT_WATCH_PARAMS: WatchParams = { watchHalfAngleDeg: 45, watchRangeCm: 5000, strikeRangeCm: 1800, holdStrikeMinSeconds: 60, holdStrikeMaxSeconds: 90 };

export type AmbushDecision = 'wait' | 'hold' | 'spring';

export const AmbushWatchRules = {
  bearingDeg(from: Vec2, to: Vec2): number {
    const d = sub(to, from);
    return Math.abs(d[0]) < 1e-4 && Math.abs(d[1]) < 1e-4 ? 0 : Math.atan2(d[1], d[0]) / DEG;
  },

  angleOffDeg(lookYawDeg: number, bearing: number): number {
    return Math.abs(findDeltaAngleDeg(lookYawDeg, bearing));
  },

  /** Index of the first watcher in range whose sector (look +- half angle) holds the bearing to the ambusher; -1 = unwatched. */
  findWatcher(watchers: readonly Watcher[], ambusher: Vec2, p: WatchParams): number {
    for (let i = 0; i < watchers.length; i++) {
      const w = watchers[i];
      if (dist(w.position, ambusher) > p.watchRangeCm) continue;
      if (AmbushWatchRules.angleOffDeg(w.lookYawDeg, AmbushWatchRules.bearingDeg(w.position, ambusher)) <= p.watchHalfAngleDeg) return i;
    }
    return -1;
  },

  isWatched(watchers: readonly Watcher[], ambusher: Vec2, p: WatchParams): boolean {
    return AmbushWatchRules.findWatcher(watchers, ambusher, p) !== -1;
  },

  watchMarginDeg(watchers: readonly Watcher[], ambusher: Vec2, p: WatchParams): number {
    let margin = 180;
    for (const w of watchers) {
      if (dist(w.position, ambusher) <= p.watchRangeCm) margin = Math.min(margin, AmbushWatchRules.angleOffDeg(w.lookYawDeg, AmbushWatchRules.bearingDeg(w.position, ambusher)) - p.watchHalfAngleDeg);
    }
    return margin;
  },

  /** Every look of one member: body, aim (when he aims at an enemy), formation sector look; looks within dedupeDeg of one listed are dropped. */
  memberLooks(position: Vec2, bodyYawDeg: number, aimYawDeg: number | null, sectorYawDeg: number | null, dedupeDeg = 5): Watcher[] {
    const out: Watcher[] = [];
    const add = (yaw: number, source: LookSource) => {
      for (const e of out) if (AmbushWatchRules.angleOffDeg(e.lookYawDeg, yaw) <= dedupeDeg) return;
      out.push({ position, lookYawDeg: unwindDeg(yaw), source });
    };
    add(bodyYawDeg, 'body');
    if (aimYawDeg !== null) add(aimYawDeg, 'aim');
    if (sectorYawDeg !== null) add(sectorYawDeg, 'sector');
    return out;
  },

  nearestDistance(positions: readonly Vec2[], p: Vec2): number {
    let best = MAX_FLT;
    for (const q of positions) best = Math.min(best, dist(q, p));
    return best;
  },

  decide(watchers: readonly Watcher[], squad: readonly Vec2[], ambusher: Vec2, p: WatchParams): AmbushDecision {
    if (!squad.length || AmbushWatchRules.nearestDistance(squad, ambusher) > p.strikeRangeCm) return 'wait';
    return AmbushWatchRules.isWatched(watchers, ambusher, p) ? 'hold' : 'spring';
  },

  /** The middle of the widest unwatched arc (looks as seen from the squad's middle); null when everything is covered; 0 with no watchers. */
  widestUnwatchedBearing(watchers: readonly Watcher[], p: WatchParams): number | null {
    if (!watchers.length) return 0;
    const half = Math.min(180, Math.max(0, p.watchHalfAngleDeg));
    if (half >= 180) return null;
    const arcs: [number, number][] = [];
    for (const w of watchers) {
      const start = (((w.lookYawDeg - half + 720) % 360) + 360) % 360;
      const end = start + 2 * half;
      if (end <= 360) arcs.push([start, end]);
      else { arcs.push([start, 360]); arcs.push([0, end - 360]); }
    }
    arcs.sort((a, b) => a[0] - b[0]);
    const merged: [number, number][] = [];
    for (const arc of arcs) {
      if (merged.length && arc[0] <= merged[merged.length - 1][1]) merged[merged.length - 1][1] = Math.max(merged[merged.length - 1][1], arc[1]);
      else merged.push([arc[0], arc[1]]);
    }
    let bestGap = 0, bestMid = 0;
    for (let i = 0; i < merged.length; i++) {
      const gapStart = merged[i][1];
      const gapEnd = i + 1 < merged.length ? merged[i + 1][0] : merged[0][0] + 360;
      const gap = gapEnd - gapStart;
      if (gap > bestGap) { bestGap = gap; bestMid = gapStart + gap * 0.5; }
    }
    if (bestGap <= 1e-4) return null;
    return unwindDeg(bestMid);
  },

  /** One ambush's hold limit from rand01: min..max (decision 2, 2026-11-01 night); 0 = never (max <= 0). */
  holdLimitSeconds(rand01: number, p: WatchParams): number {
    if (p.holdStrikeMaxSeconds <= 0) return 0;
    const min = Math.min(p.holdStrikeMaxSeconds, Math.max(0, p.holdStrikeMinSeconds));
    const k = Math.min(1, Math.max(0, rand01));
    return min + (p.holdStrikeMaxSeconds - min) * k;
  },

  heldTooLong(holdSeconds: number, limitSeconds: number): boolean {
    return limitSeconds > 0 && holdSeconds >= limitSeconds;
  },
};

// --- HeightSightRules (SightRules::HeightLevel / HeightRangeMultiplier) -------------------------------------------------------------

/**
 * The sight range from a height (Source/CodexTactics/Public/Combat/SightRules.h, game designer 2026-11-02; the NUMBERS ARE PENDING the user's
 * confirmation): an observer's height level = floor(his eye height above the ground below him / levelStepCm) - the ground and the -3 m cut
 * are level 0 - and his sight range x (1 + bonusPerLevel x level) for level >= 1, at most maxMultiplier. One model for the squad and the armed
 * human enemies. Data: Content/Data/AI/enemy_perception.json "height_sight" (these defaults mirror it).
 */
export interface HeightSightConfig { levelStepCm: number; bonusPerLevel: number; maxMultiplier: number }
export const DEFAULT_HEIGHT_SIGHT: HeightSightConfig = { levelStepCm: 300, bonusPerLevel: 0.25, maxMultiplier: 2 };

export const HeightSightRules = {
  /** SightRules::HeightLevel: floor(h / step), never below 0 (a non-positive step: 0). */
  heightLevel(eyeAboveGroundCm: number, levelStepCm = 300): number {
    if (!(levelStepCm > 0) || !Number.isFinite(eyeAboveGroundCm)) return 0;
    return Math.max(0, Math.floor(eyeAboveGroundCm / levelStepCm));
  },

  /** SightRules::HeightRangeMultiplier: 1 + bonus x level for level >= 1, capped at maxMultiplier, never below 1. */
  rangeMultiplier(c: HeightSightConfig, level: number): number {
    if (level <= 0) return 1;
    const m = 1 + Math.max(c.bonusPerLevel, 0) * level;
    return Math.min(Math.max(m, 1), Math.max(c.maxMultiplier, 1));
  },

  /** SightRules::HeightRangeMultiplierAt. */
  rangeMultiplierAt(c: HeightSightConfig, eyeAboveGroundCm: number): number {
    return HeightSightRules.rangeMultiplier(c, HeightSightRules.heightLevel(eyeAboveGroundCm, c.levelStepCm));
  },
};

// --- CombatPressureRules --------------------------------------------------------------------------------------------------------------

export const CombatPressureRules = {
  isAllowedType(type: string): type is EnemyType {
    return (ENEMY_TYPES as readonly string[]).includes(type);
  },

  /** The allowed types with a positive weight (empty / all-zero = the five at weight 1). */
  effectiveWeights(weights: readonly TypeWeight[]): TypeWeight[] {
    const out = weights.filter((w) => CombatPressureRules.isAllowedType(w.type) && w.weight > 0).map((w) => ({ ...w }));
    return out.length ? out : ENEMY_TYPES.map((type) => ({ type, weight: 1 }));
  },

  decideEvent(nowSeconds: number, dueAtSeconds: number, alive: number, maxAlive: number): EventDecision {
    if (nowSeconds < dueAtSeconds) return 'not_due';
    return alive < Math.max(1, maxAlive) ? 'spawn' : 'delayed_cap_full';
  },

  groupSize(rand01: number, min: number, max: number, alive: number, maxAlive: number): number {
    const low = Math.max(1, min), high = Math.max(low, max);
    const size = Math.min(high, Math.max(low, low + Math.floor(Math.min(0.9999, Math.max(0, rand01)) * (high - low + 1))));
    return Math.min(Math.max(size, 0), Math.max(0, maxAlive - alive));
  },

  pickKind(rand01: number, patrolShare: number): SpawnKind {
    return rand01 < Math.min(1, Math.max(0, patrolShare)) ? 'patrol' : 'ambush';
  },

  pickEventKind(rand01: number, randCoordinated01: number, patrolShare: number, coordinatedShare: number): SpawnKind {
    if (CombatPressureRules.pickKind(rand01, patrolShare) === 'patrol') return 'patrol';
    return randCoordinated01 < Math.min(1, Math.max(0, coordinatedShare)) ? 'coordinated' : 'ambush';
  },

  pickType(weights: readonly TypeWeight[], rand01: number): EnemyType {
    const eff = CombatPressureRules.effectiveWeights(weights);
    const total = eff.reduce((s, w) => s + w.weight, 0);
    let pick = Math.min(0.9999, Math.max(0, rand01)) * total;
    for (const w of eff) {
      if (pick < w.weight) return w.type;
      pick -= w.weight;
    }
    return eff[eff.length - 1].type;
  },

  distanceToBox(p: Vec2, box: PressureBox): number {
    const ax = normalizeAxis(box.axisX);
    const ay: Vec2 = [-ax[1], ax[0]];
    const d = sub(p, box.center);
    const lx = d[0] * ax[0] + d[1] * ax[1], ly = d[0] * ay[0] + d[1] * ay[1];
    const dx = Math.max(0, Math.abs(lx) - box.halfSize[0]), dy = Math.max(0, Math.abs(ly) - box.halfSize[1]);
    return Math.sqrt(dx * dx + dy * dy);
  },

  clearance(p: Vec2, boxes: readonly PressureBox[]): number {
    let best = MAX_FLT;
    for (const b of boxes) best = Math.min(best, CombatPressureRules.distanceToBox(p, b));
    return best;
  },

  /** Inside the bounds, at least clearanceCm from every box, at least minSquadDistanceCm from every operative - the first failing reason. */
  checkSpawnPoint(p: Vec2, boxes: readonly PressureBox[], clearanceCm: number, squad: readonly Vec2[], minSquadDistanceCm: number, bounds: Bounds2 | null): SpawnVerdict {
    if (bounds && !(p[0] >= bounds.min[0] && p[0] <= bounds.max[0] && p[1] >= bounds.min[1] && p[1] <= bounds.max[1])) return 'out_of_bounds';
    if (CombatPressureRules.clearance(p, boxes) < clearanceCm) return 'near_wall';
    if (AmbushWatchRules.nearestDistance(squad, p) < minSquadDistanceCm) return 'near_squad';
    return 'ok';
  },

  /** Ambushers MUST be concealed (-1000 otherwise), ahead strongly preferred, the band's middle; patrols: ahead, out of sight / looks. */
  scoreSpot(i: SpotInput): number {
    const band = Math.max(1, i.maxDistanceCm - i.minDistanceCm);
    const mid = (i.minDistanceCm + i.maxDistanceCm) * 0.5;
    const offBand = i.distanceCm < i.minDistanceCm || i.distanceCm > i.maxDistanceCm ? 1 : 0;
    const fromMid = Math.abs(i.distanceCm - mid) / band;
    if (i.kind === 'ambush') {
      if (!i.concealed) return -1000;
      return 2 * i.aheadCos - fromMid - 3 * offBand + i.kindBonus;
    }
    return i.aheadCos * 0.75 + (i.concealed ? 1 : 0) - (i.watched ? 0.5 : 0) - 0.5 * fromMid - 3 * offBand;
  },

  patrolLoop(center: Vec2, radiusCm: number, points: number, phaseDeg: number): Vec2[] {
    const out: Vec2[] = [];
    const count = Math.max(2, points);
    for (let i = 0; i < count; i++) {
      const a = (phaseDeg + (360 * i) / count) * DEG;
      out.push([center[0] + Math.cos(a) * radiusCm, center[1] + Math.sin(a) * radiusCm]);
    }
    return out;
  },

  groupOffset(index: number, spacingCm: number): Vec2 {
    if (index <= 0) return [0, 0];
    const a = (150 + 120 * (index - 1)) * DEG;
    const ring = spacingCm * (1 + Math.floor((index - 1) / 3));
    return [Math.cos(a) * ring, Math.sin(a) * ring];
  },

  shouldRecycle(engaged: boolean, visibleToSquad: boolean, nearestSquadCm: number, farSeconds: number, recycleDistanceCm: number, recycleAfterSeconds: number): boolean {
    return !engaged && !visibleToSquad && nearestSquadCm > recycleDistanceCm && farSeconds >= recycleAfterSeconds;
  },
};

// --- AmbushSpotRules ------------------------------------------------------------------------------------------------------------------

export type SpotKind = 'none' | 'wall_end' | 'doorway' | 'authored';

/** FAmbushSpotParams (level JSON "pressure"."ambush"."spots"). */
export interface AmbushSpotParams {
  wallStandoffCm: number;
  endInsetCm: number;
  minWallLengthCm: number;
  joinedGapCm: number;
  doorwayMinCm: number;
  doorwayMaxCm: number;
  authoredBonus: number;
  doorwayBonus: number;
  mergeCm: number;
}
export const DEFAULT_SPOT_PARAMS: AmbushSpotParams = { wallStandoffCm: 80, endInsetCm: 100, minWallLengthCm: 150, joinedGapCm: 30, doorwayMinCm: 80, doorwayMaxCm: 600, authoredBonus: 0.5, doorwayBonus: 0.25, mergeCm: 100 };

export interface AmbushSpot { position: Vec2; kind: SpotKind; box: number; normal: Vec2 }
export interface WallEnd { box: number; sign: 1 | -1; point: Vec2; outward: Vec2 }

const spotPriority = (k: SpotKind) => (k === 'authored' ? 3 : k === 'doorway' ? 2 : k === 'wall_end' ? 1 : 0);

/** Adds the spot unless one within mergeCm is there already (a higher kind replaces it). */
function addMergedSpot(out: AmbushSpot[], spot: AmbushSpot, mergeCm: number): void {
  for (let i = 0; i < out.length; i++) {
    if (dist(out[i].position, spot.position) <= mergeCm) {
      if (spotPriority(spot.kind) > spotPriority(out[i].kind)) out[i] = spot;
      return;
    }
  }
  out.push(spot);
}

export const AmbushSpotRules = {
  /** The wall clearance a hiding spot keeps: min(the level's wall clearance, the standoff - 10 cm). */
  hideClearanceCm(wallClearanceCm: number, p: AmbushSpotParams): number {
    return Math.max(0, Math.min(wallClearanceCm, p.wallStandoffCm - 10));
  },

  longAxis(box: PressureBox): { axis: Vec2; halfLength: number; halfThickness: number } {
    const ax = normalizeAxis(box.axisX);
    if (box.halfSize[1] > box.halfSize[0]) return { axis: [-ax[1], ax[0]], halfLength: box.halfSize[1], halfThickness: box.halfSize[0] };
    return { axis: ax, halfLength: box.halfSize[0], halfThickness: box.halfSize[1] };
  },

  boxPoint(box: PressureBox, along: number, across: number): Vec2 {
    const { axis } = AmbushSpotRules.longAxis(box);
    const side: Vec2 = [-axis[1], axis[0]];
    return [box.center[0] + axis[0] * along + side[0] * across, box.center[1] + axis[1] * along + side[1] * across];
  },

  /** Every free end of every wall (length >= minWallLength): the point joinedGap beyond it touches no other box. */
  freeEnds(boxes: readonly PressureBox[], p: AmbushSpotParams): WallEnd[] {
    const out: WallEnd[] = [];
    for (let i = 0; i < boxes.length; i++) {
      const { axis, halfLength } = AmbushSpotRules.longAxis(boxes[i]);
      if (2 * halfLength < p.minWallLengthCm) continue;
      for (const sign of [1, -1] as const) {
        const beyond = AmbushSpotRules.boxPoint(boxes[i], sign * (halfLength + p.joinedGapCm), 0);
        let joined = false;
        for (let o = 0; o < boxes.length && !joined; o++) joined = o !== i && CombatPressureRules.distanceToBox(beyond, boxes[o]) < p.joinedGapCm;
        if (!joined) out.push({ box: i, sign, point: AmbushSpotRules.boxPoint(boxes[i], sign * halfLength, 0), outward: [axis[0] * sign, axis[1] * sign] });
      }
    }
    return out;
  },

  /** Two free ends of different, parallel (20 deg) walls facing each other across a gap of doorwayMin..doorwayMax. */
  isDoorwayPair(a: WallEnd, b: WallEnd, p: AmbushSpotParams): { ok: boolean; gapCm: number } {
    if (a.box === b.box) return { ok: false, gapCm: 0 };
    const delta = sub(b.point, a.point);
    const gap = Math.hypot(delta[0], delta[1]);
    if (gap < p.doorwayMinCm || gap > p.doorwayMaxCm) return { ok: false, gapCm: gap };
    const parallel = Math.abs(a.outward[0] * b.outward[0] + a.outward[1] * b.outward[1]);
    if (parallel < Math.cos(20 * DEG)) return { ok: false, gapCm: gap };
    const ok = delta[0] * a.outward[0] + delta[1] * a.outward[1] >= 0.8 * gap && -(delta[0] * b.outward[0] + delta[1] * b.outward[1]) >= 0.8 * gap;
    return { ok, gapCm: gap };
  },

  /** Per free end one spot on each face (endInset inside the end, wallStandoff off the face); Doorway beside a gap. */
  wallSpots(boxes: readonly PressureBox[], p: AmbushSpotParams): AmbushSpot[] {
    const ends = AmbushSpotRules.freeEnds(boxes, p);
    const out: AmbushSpot[] = [];
    for (const end of ends) {
      let doorway = false;
      for (const other of ends) doorway ||= AmbushSpotRules.isDoorwayPair(end, other, p).ok;
      const { axis, halfLength, halfThickness } = AmbushSpotRules.longAxis(boxes[end.box]);
      const along = end.sign * Math.max(0, halfLength - p.endInsetCm);
      const side: Vec2 = [-axis[1], axis[0]];
      for (const face of [1, -1] as const) {
        addMergedSpot(out, { position: AmbushSpotRules.boxPoint(boxes[end.box], along, face * (halfThickness + p.wallStandoffCm)), kind: doorway ? 'doorway' : 'wall_end', box: end.box, normal: [side[0] * face, side[1] * face] }, p.mergeCm);
      }
    }
    return out;
  },

  /** Authored points + wall spots, merged (within mergeCm one; the higher kind wins). */
  candidates(boxes: readonly PressureBox[], authored: readonly Vec2[], p: AmbushSpotParams): AmbushSpot[] {
    const out: AmbushSpot[] = [];
    for (const a of authored) addMergedSpot(out, { position: a, kind: 'authored', box: -1, normal: [0, 0] }, p.mergeCm);
    for (const s of AmbushSpotRules.wallSpots(boxes, p)) addMergedSpot(out, s, p.mergeCm);
    return out;
  },

  kindBonus(k: SpotKind, p: AmbushSpotParams): number {
    return k === 'authored' ? p.authoredBonus : k === 'doorway' ? p.doorwayBonus : 0;
  },
};

// --- AmbushPlanRules ------------------------------------------------------------------------------------------------------------------

export type Party = 'single' | 'flank_left' | 'flank_right' | 'front' | 'rear';
export type PlanPhase = 'waiting' | 'holding' | 'flank_block' | 'assault';

/** FAmbushPlanParams (level JSON "pressure"."ambush"."coordinated"). */
export interface PlanParams {
  share: number;
  bodiesMin: number;
  bodiesMax: number;
  maxAlive: number;
  minSquadDistanceCm: number;
  maxSquadDistanceCm: number;
  anchorAheadCm: number;
  flankOffsetCm: number;
  frontOffsetCm: number;
  rearOffsetCm: number;
  minSeparationCm: number;
  triggerRangeCm: number;
  assaultMinSeconds: number;
  assaultMaxSeconds: number;
  blockDistanceCm: number;
  minRanged: number;
}
export const DEFAULT_PLAN_PARAMS: PlanParams = { share: 0.35, bodiesMin: 4, bodiesMax: 6, maxAlive: 6, minSquadDistanceCm: 1000, maxSquadDistanceCm: 3000, anchorAheadCm: 500, flankOffsetCm: 1500, frontOffsetCm: 1600, rearOffsetCm: 1500, minSeparationCm: 600, triggerRangeCm: 1800, assaultMinSeconds: 1.5, assaultMaxSeconds: 4, blockDistanceCm: 900, minRanged: 1 };

/** FAmbushPlanTick. */
export interface PlanTick {
  phase: PlanPhase;
  secondsInPhase: number;
  nearestSquadCm: number;
  flanksUnwatched: number;
  anyEngaged: boolean;
  flankMembersAlive: number;
  flankMembersInPlace: number;
  holdSeconds: number;
  holdLimitSeconds: number;
}

export const PLAN_PARTIES: readonly Party[] = ['flank_left', 'flank_right', 'front', 'rear'];

export const AmbushPlanRules = {
  planParty(slot: number): Party {
    return PLAN_PARTIES[slot] ?? 'single';
  },

  isFlank(p: Party): boolean {
    return p === 'flank_left' || p === 'flank_right';
  },

  isRanged(t: EnemyType): boolean {
    return t === 'SPITTER' || t === 'MARKSMAN';
  },

  /** 0 FlankLeft, 1 FlankRight, 2 Front, 3 Rear, then Front / Rear alternately. */
  partyOf(memberIndex: number): Party {
    return memberIndex < 4 ? AmbushPlanRules.planParty(Math.max(0, memberIndex)) : (memberIndex - 4) % 2 === 0 ? 'front' : 'rear';
  },

  /** Bodies of a new plan: bodiesMin..bodiesMax cut to the room under maxAlive; 0 when there is no room for 4. */
  planBodies(rand01: number, p: PlanParams, alive: number): number {
    const low = Math.max(4, p.bodiesMin), high = Math.max(low, p.bodiesMax);
    const size = Math.min(high, Math.max(low, low + Math.floor(Math.min(0.9999, Math.max(0, rand01)) * (high - low + 1))));
    const room = Math.max(0, p.maxAlive - Math.max(0, alive));
    return room < 4 ? 0 : Math.min(size, room);
  },

  /** Ensures minRanged ranged members: melee FRONT bodies swapped first, then the rear's; returns how many were swapped. */
  ensureRanged(types: EnemyType[], ranged: EnemyType, minRanged: number): number {
    let have = types.filter(AmbushPlanRules.isRanged).length;
    if (!AmbushPlanRules.isRanged(ranged)) return 0;
    let swapped = 0;
    for (const party of ['front', 'rear'] as const) {
      for (let i = 0; i < types.length && have < minRanged; i++) {
        if (AmbushPlanRules.partyOf(i) === party && !AmbushPlanRules.isRanged(types[i])) { types[i] = ranged; have++; swapped++; }
      }
    }
    return swapped;
  },

  anchor(leader: Vec2, headingYawDeg: number, p: PlanParams): Vec2 {
    const r = headingYawDeg * DEG;
    return [leader[0] + Math.cos(r) * p.anchorAheadCm, leader[1] + Math.sin(r) * p.anchorAheadCm];
  },

  slotPoint(anchorPt: Vec2, headingYawDeg: number, party: Party, p: PlanParams): Vec2 {
    let turn = 0, offset = p.frontOffsetCm;
    if (party === 'flank_right') { turn = 90; offset = p.flankOffsetCm; }
    else if (party === 'flank_left') { turn = -90; offset = p.flankOffsetCm; }
    else if (party === 'rear') { turn = 180; offset = p.rearOffsetCm; }
    const r = (headingYawDeg + turn) * DEG;
    return [anchorPt[0] + Math.cos(r) * offset, anchorPt[1] + Math.sin(r) * offset];
  },

  /** Front within +-45 deg of the heading, FlankRight 45..135, Rear beyond 135, FlankLeft -135..-45. */
  sectorOf(anchorPt: Vec2, headingYawDeg: number, point: Vec2): Party {
    const d = sub(point, anchorPt);
    const bearing = Math.abs(d[0]) < 1e-4 && Math.abs(d[1]) < 1e-4 ? headingYawDeg : Math.atan2(d[1], d[0]) / DEG;
    const rel = findDeltaAngleDeg(headingYawDeg, bearing);
    if (Math.abs(rel) <= 45) return 'front';
    if (Math.abs(rel) >= 135) return 'rear';
    return rel > 0 ? 'flank_right' : 'flank_left';
  },

  /** One candidate index per party (PLAN_PARTIES order; -1 = none): nearest to its slot in its sector, >= minSeparation from the taken ones. */
  assignSpots(anchorPt: Vec2, headingYawDeg: number, cands: readonly Vec2[], p: PlanParams): number[] {
    const out = [-1, -1, -1, -1];
    const taken: Vec2[] = [];
    for (let slot = 0; slot < 4; slot++) {
      const party = AmbushPlanRules.planParty(slot);
      const want = AmbushPlanRules.slotPoint(anchorPt, headingYawDeg, party, p);
      let best = MAX_FLT;
      for (let i = 0; i < cands.length; i++) {
        if (AmbushPlanRules.sectorOf(anchorPt, headingYawDeg, cands[i]) !== party) continue;
        let tooClose = false;
        for (const u of taken) tooClose ||= dist(u, cands[i]) < p.minSeparationCm;
        const d = dist(cands[i], want);
        if (!tooClose && d < best) { best = d; out[slot] = i; }
      }
      if (out[slot] !== -1) taken.push(cands[out[slot]]);
    }
    return out;
  },

  isComplete(assigned: readonly number[]): boolean {
    return assigned.length >= 4 && !assigned.includes(-1);
  },

  nextPhase(t: PlanTick, p: PlanParams): PlanPhase {
    switch (t.phase) {
      case 'waiting':
      case 'holding':
        if (t.anyEngaged) return 'flank_block';
        if (t.nearestSquadCm > p.triggerRangeCm) return 'waiting';
        if (t.flanksUnwatched > 0) return 'flank_block';
        return AmbushWatchRules.heldTooLong(t.holdSeconds, t.holdLimitSeconds) ? 'flank_block' : 'holding';
      case 'flank_block': {
        if (t.secondsInPhase >= p.assaultMaxSeconds) return 'assault';
        const blocked = t.flankMembersAlive <= 0 || t.flankMembersInPlace >= t.flankMembersAlive;
        return t.secondsInPhase >= p.assaultMinSeconds && blocked ? 'assault' : 'flank_block';
      }
      default:
        return 'assault';
    }
  },

  /** A member strikes in the phase: flanks and every ranged member (the suppressors) from the flank block, everyone in the assault. */
  springs(phase: PlanPhase, party: Party, ranged: boolean): boolean {
    if (phase === 'assault') return true;
    if (phase === 'flank_block') return AmbushPlanRules.isFlank(party) || ranged || party === 'single';
    return false;
  },
};

// --- AmbushRelocateRules --------------------------------------------------------------------------------------------------------------

/** FAmbushRelocateParams (level JSON "pressure"."ambush"."relocate"). */
export interface RelocateParams {
  enabled: boolean;
  afterSeconds: number;
  behindAngleDeg: number;
  farCm: number;
  minAheadCos: number;
  routeSampleCm: number;
  maxRouteCm: number;
  runSpeedScale: number;
  noRouteTries: number;
  noRoutePatrol: boolean;
}
export const DEFAULT_RELOCATE_PARAMS: RelocateParams = { enabled: true, afterSeconds: 15, behindAngleDeg: 100, farCm: 3500, minAheadCos: 0.2, routeSampleCm: 100, maxRouteCm: 10000, runSpeedScale: 1, noRouteTries: 2, noRoutePatrol: true };

export type RelocateDecision = 'stay' | 'wait_unseen' | 'relocate';
export type NoRouteOutcome = 'stay' | 'patrol';
export type RunStep = 'running' | 'arrived' | 'spotted' | 'timed_out';

/** FAmbushRouteCandidate. */
export interface RouteCandidate { score: number; pathCm: number; reachable: boolean; seenSamples: number; destinationConcealed: boolean }

export const AmbushRelocateRules = {
  /** Beyond the strike range AND behind the squad's way (angle off the heading > behindAngle) or farther than farCm. */
  isBypassed(nearestSquadCm: number, angleOffHeadingDeg: number, strikeRangeCm: number, p: RelocateParams): boolean {
    if (nearestSquadCm <= strikeRangeCm) return false;
    return Math.abs(angleOffHeadingDeg) > p.behindAngleDeg || nearestSquadCm > p.farCm;
  },

  stepBypassedSeconds(bypassed: boolean, seconds: number, delta: number): number {
    return bypassed ? seconds + Math.max(0, delta) : 0;
  },

  decide(bypassedSeconds: number, seenNow: boolean, p: RelocateParams): RelocateDecision {
    if (!p.enabled || bypassedSeconds < p.afterSeconds) return 'stay';
    return seenNow ? 'wait_unseen' : 'relocate';
  },

  isGoodDestination(aheadCos: number, concealed: boolean, p: RelocateParams): boolean {
    return concealed && aheadCos >= p.minAheadCos;
  },

  /** Points every stepCm along the polyline (its first and last point included). */
  samplePath(path: readonly Vec2[], stepCm: number): Vec2[] {
    const out: Vec2[] = [];
    if (!path.length) return out;
    const step = Math.max(10, stepCm);
    out.push(path[0]);
    let walked = 0;
    for (let i = 1; i < path.length; i++) {
      const from = path[i - 1];
      const seg = sub(path[i], from);
      const length = Math.hypot(seg[0], seg[1]);
      if (length <= 1e-4) continue;
      let at = step - walked;
      while (at < length) {
        out.push([from[0] + seg[0] * (at / length), from[1] + seg[1] * (at / length)]);
        at += step;
      }
      walked = length - (at - step);
    }
    if (path.length > 1 && dist(out[out.length - 1], path[path.length - 1]) > 1) out.push(path[path.length - 1]);
    return out;
  },

  pathLength(path: readonly Vec2[]): number {
    let L = 0;
    for (let i = 1; i < path.length; i++) L += dist(path[i - 1], path[i]);
    return L;
  },

  isConcealedRoute(r: RouteCandidate, p: RelocateParams): boolean {
    return r.reachable && r.seenSamples === 0 && r.destinationConcealed && r.pathCm <= p.maxRouteCm;
  },

  /** The best concealed route (highest score; a tie -> the shorter path); -1 when none. */
  pickRoute(routes: readonly RouteCandidate[], p: RelocateParams): number {
    let best = -1;
    for (let i = 0; i < routes.length; i++) {
      if (!AmbushRelocateRules.isConcealedRoute(routes[i], p)) continue;
      if (best === -1 || routes[i].score > routes[best].score + 1e-4 || (Math.abs(routes[i].score - routes[best].score) <= 1e-4 && routes[i].pathCm < routes[best].pathCm)) best = i;
    }
    return best;
  },

  noRouteOutcome(failedSearches: number, p: RelocateParams): NoRouteOutcome {
    return p.noRoutePatrol && failedSearches >= Math.max(1, p.noRouteTries) ? 'patrol' : 'stay';
  },

  /** Twice the path at the speed plus 10 s. */
  runLimitSeconds(pathCm: number, speedCmS: number): number {
    return (2 * Math.max(0, pathCm)) / Math.max(50, speedCmS) + 10;
  },

  stepRun(spotted: boolean, allArrived: boolean, elapsed: number, limit: number): RunStep {
    if (spotted) return 'spotted';
    if (allArrived) return 'arrived';
    return elapsed >= limit ? 'timed_out' : 'running';
  },
};
