// Training Ground / level generator: pure deterministic props placement.
// Docs: docs/design/levelgen/PROPS_LAYERS.md sections 0, 2.2, 2.3, 2.5, 5, 7.
// C++ reference: Source/CodexTactics/Private/LevelGen/PropsPlacementRules.cpp and PropsPlacementC4.cpp

import type { LevelPropsFile, PropLayer, PropObject, PropSubReport } from './schema.ts';
import {
  PropsLayoutView,
  sinQ,
  cosQ,
  isqrt,
  floorDiv,
  SIGHT_SCALE,
} from './layoutView.ts';
import type { PropsRoute, PropsSightSegment, PropsExclusionParams } from './layoutView.ts';
import { PropsRng, streamSeed } from './rng.ts';
import { runFastGate } from './fastGate.ts';
import type { GenLayout } from '../schema.ts';

export const LATTICE_CM = 50;
export const MAX_WEIGHT = 1000;
export const KIND_CRATES = 'crates';
export const KIND_BARRELS = 'barrels';
export const KIND_TURRETS = 'turrets';
export const KIND_MINES = 'mines';
export const KIND_LOOT = 'loot';
export const VARIANT_BARREL = 'barrel';
export const VARIANT_BARREL_EXPLOSIVE = 'barrel_explosive';
export const CLUSTER_FIELD = 'cluster';
export const COVERED_FIELD = 'covered_m';
export const WIDTH_FIELD = 'width_cm';
export const TURRET_YAW_STEP_DEG = 15;
export const ROUTE_SAMPLE_CM = 100;

export interface PropsNearClause {
  what: 'wall' | 'cover' | string;
  max_cm: number;
  weight: number;
}

export interface PropsTurretRules {
  routeId: string;
  rangeMinCm: number;
  rangeMaxCm: number;
  arcDeg: number;
  minCoveredM: number;
  behindCoverGapCm: number;
}

export interface PropsMineDoorRules {
  enabled: boolean;
  mode: 'count' | 'percent';
  value: number;
  kinds: string[];
  allowMandatory: boolean;
  insideCm: number;
  trigger: string;
  variant: string;
}

export interface PropsMineCrateRules {
  enabled: boolean;
  sourceLayer: string;
  percent: number;
  trigger: string;
  variant: string;
}

export interface PropsCommonRules {
  countMode: string;
  countValue: number;
  densityMilli: number;
  countMax: number;
  budgetCap: number;
  zones: string[];
  walkLayers: string[];
  near: PropsNearClause[];
  variants: Array<{ key: string; weight: number }>;
  minSpacingCm: number;
  minGapAnyCm: number;
  wallGapCm: number;
  exclude: Record<string, unknown>;
  anchors: string[];
  halfSizeCm: Map<string, number>;
}

export interface PropsMineFieldRules {
  enabled: boolean;
  common: PropsCommonRules;
  trigger: string;
  variant: string;
}

export interface PropsLootRules {
  budgetValue: number;
  tables: string[];
  tableValues: number[];
  containersShareMilli: number;
  containersBudgetMilli: number;
  sourceLayer: string;
  containerVariant: string;
  farMinCm: number;
  farMaxCm: number;
  pileVariant: string;
  roofTable: string;
  roofTableValue: number;
  weightNear: number;
  weightBand: number;
  weightFar: number;
}

export interface PropsRules extends PropsCommonRules {
  kind: string;
  clusterMin: number;
  clusterMax: number;
  ringMinCm: number;
  ringMaxCm: number;
  explosiveShareMilli: number;
  explosiveMinGapCm: number;
  explosiveNeverSpawnCm: number;
  explosiveNeverEndCm: number;
  turret: PropsTurretRules;
  mineDoors: PropsMineDoorRules;
  mineCrates: PropsMineCrateRules;
  mineField: PropsMineFieldRules;
  loot: PropsLootRules;
}

export interface PropsRejectCounts {
  blocked: number;
  spacing: number;
  route: number;
  spawn: number;
  ladder: number;
  door: number;
  keepout: number;
  other_layer: number;
  other: number;
  coverage: number;
}

export interface PropsPlacementReport {
  requested: number;
  placed: number;
  kept: number;
  short_by: number;
  eligible: number;
  status: 'ok' | 'short';
  gate: string;
  gate_leg?: string;
  gate_blockers?: string[];
  rejected: PropsRejectCounts;
  warnings: string[];
  kind?: string;
  subA?: PropSubReport;
  subB?: PropSubReport;
  subC?: PropSubReport;
  budgetValue?: number;
  budgetSpent?: number;
  bBudgetShort?: boolean;
}

export interface PropsResolvedObject {
  layerId: string;
  object: PropObject;
  bHand: boolean;
  bPinned: boolean;
  zOffsetCm?: number;
}

export interface PropsKeepOut {
  id: string;
  layerId: string;
  pos: [number, number];
  radiusCm: number;
}

export interface PropsPlacementInput {
  view: PropsLayoutView | null;
  rules: PropsRules;
  layerId: string;
  seed: number;
  kept: PropObject[];
  keepOuts: PropsKeepOut[];
  otherObjects: PropObject[];
  otherLayerIds: string[];
  layerKinds: Map<string, string>;
  reservedIds: string[];
}

export interface PropsPlacementResult {
  objects: PropObject[];
  report: PropsPlacementReport;
  rngCalls?: number;
}

// ---------------- Helper Functions ----------------

export function distSq(a: [number, number], b: [number, number]): bigint {
  const dx = BigInt(a[0] - b[0]);
  const dy = BigInt(a[1] - b[1]);
  return dx * dx + dy * dy;
}

export function closer(a: [number, number], b: [number, number], r: number): boolean {
  if (r <= 0) return false;
  const rBig = BigInt(r);
  return distSq(a, b) < rBig * rBig;
}

export function within(a: [number, number], b: [number, number], r: number): boolean {
  if (r <= 0) return false;
  const rBig = BigInt(r);
  return distSq(a, b) <= rBig * rBig;
}

export function ceilStep(v: number): number {
  const s = LATTICE_CM;
  return v >= 0 ? Math.floor((v + s - 1) / s) * s : -Math.floor(-v / s) * s;
}

export function floorStep(v: number): number {
  const s = LATTICE_CM;
  return v >= 0 ? Math.floor(v / s) * s : -Math.floor((-v + s - 1) / s) * s;
}

export function snappedCentre(min: [number, number], max: [number, number]): [number, number] {
  return [
    floorStep(floorDiv(BigInt(min[0]) + BigInt(max[0]), 2n)),
    floorStep(floorDiv(BigInt(min[1]) + BigInt(max[1]), 2n)),
  ];
}

export function isRoofLayer(view: PropsLayoutView, layer: string): boolean {
  const l = view.findLayer(layer);
  return l ? l.kind === 'roof' : false;
}

export function matchLayer(pattern: string, name: string): boolean {
  if (pattern.endsWith('*')) {
    return name.startsWith(pattern.slice(0, -1));
  }
  return pattern === name;
}

export function matchAnyLayer(patterns: string[], name: string): boolean {
  for (const p of patterns) {
    if (matchLayer(p, name)) return true;
  }
  return false;
}

export function mergeObjects(
  base: Record<string, unknown> | undefined,
  over: Record<string, unknown> | undefined
): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  if (base) {
    for (const k of Object.keys(base).sort()) {
      out[k] = base[k];
    }
  }
  if (over) {
    for (const k of Object.keys(over).sort()) {
      const bVal = out[k];
      const oVal = over[k];
      if (
        bVal &&
        typeof bVal === 'object' &&
        !Array.isArray(bVal) &&
        oVal &&
        typeof oVal === 'object' &&
        !Array.isArray(oVal)
      ) {
        out[k] = mergeObjects(bVal as Record<string, unknown>, oVal as Record<string, unknown>);
      } else {
        out[k] = oVal;
      }
    }
  }
  return out;
}

function halfOf(rules: PropsCommonRules, variant: string): number {
  return rules.halfSizeCm.get(variant) ?? 0;
}

function maxVariantHalf(rules: PropsCommonRules): number {
  let h = 0;
  for (const v of rules.variants) {
    h = Math.max(h, halfOf(rules, v.key));
  }
  return h;
}

export function clusterOf(o: PropObject): string {
  const c = o[CLUSTER_FIELD];
  if (typeof c === 'string' && c.length > 0) return c;
  return o.id;
}

// ---------------- Overrides ----------------

const G_HAND_PREFIX = 'hand_';
const G_HAND_ORIGIN = 'hand';

function findLayerForId(file: LevelPropsFile, id?: string): PropLayer | undefined {
  if (!id) return undefined;
  const name = id.startsWith(G_HAND_PREFIX) ? id.slice(G_HAND_PREFIX.length) : id;
  let best: PropLayer | undefined;
  for (const layer of file.layers) {
    if (name.startsWith(`${layer.id}_`) && (!best || layer.id.length > best.id.length)) {
      best = layer;
    }
  }
  return best;
}

function spacingOf(layer: PropLayer | undefined): number {
  if (!layer || !layer.rules) return 0;
  const sp = layer.rules.min_spacing_cm;
  return typeof sp === 'number' ? Math.max(0, Math.floor(sp)) : 0;
}

function applySetFields(fields: Record<string, unknown>, _id: string, o: PropObject): void {
  for (const k of Object.keys(fields).sort()) {
    const v = fields[k];
    if (k === 'id') continue;
    if (k === 'variant') {
      if (typeof v === 'string' && v.length > 0) o.variant = v;
    } else if (k === 'walk_layer' || k === 'origin' || k === 'attach' || k === 'door' || k === 'trigger' || k === 'loot_table') {
      if (v === null) {
        delete (o as Record<string, unknown>)[k];
      } else if (typeof v === 'string') {
        (o as Record<string, unknown>)[k] = v;
      }
    } else if (k === 'yaw') {
      if (v === null) o.yaw = 0;
      else if (typeof v === 'number') o.yaw = Math.floor(v);
    } else if (k === 'arc') {
      if (v === null) delete o.arc;
      else if (typeof v === 'number') o.arc = Math.floor(v);
    } else if (k === 'locked') {
      if (v === null) o.locked = false;
      else if (typeof v === 'boolean') o.locked = v;
    } else if (k === 'pos') {
      if (Array.isArray(v) && v.length === 2 && typeof v[0] === 'number' && typeof v[1] === 'number') {
        o.pos = [Math.floor(v[0]), Math.floor(v[1])];
      }
    } else if (v === null) {
      delete (o as Record<string, unknown>)[k];
    } else {
      (o as Record<string, unknown>)[k] = v;
    }
  }
}

export function applyOverrides(file: LevelPropsFile): {
  objects: PropsResolvedObject[];
  keepOuts: PropsKeepOut[];
} {
  const result: { objects: PropsResolvedObject[]; keepOuts: PropsKeepOut[] } = {
    objects: [],
    keepOuts: [],
  };

  const disabledIds = new Set<string>();
  for (const layer of file.layers) {
    for (const obj of layer.objects) {
      if (!layer.enabled) {
        disabledIds.add(obj.id);
        continue;
      }
      const bHand = obj.origin === G_HAND_ORIGIN;
      result.objects.push({
        layerId: layer.id,
        object: { ...obj },
        bHand,
        bPinned: bHand,
      });
    }
  }

  const findIdx = (id: string) => result.objects.findIndex((r) => r.object.id === id);
  const inDisabledLayer = (id: string) => {
    const layer = findLayerForId(file, id);
    return disabledIds.has(id) || Boolean(layer && !layer.enabled);
  };

  if (Array.isArray(file.overrides)) {
    for (const rawOp of file.overrides) {
      const op = rawOp as Record<string, unknown>;
      const opObj = op.object && typeof op.object === 'object' ? (op.object as Record<string, unknown>) : null;
      const opId =
        typeof op.id === 'string'
          ? op.id
          : typeof opObj?.id === 'string'
          ? (opObj.id as string)
          : '';
      if (!opId) continue;
      const idx = findIdx(opId);
      if (idx === -1 && inDisabledLayer(opId)) {
        continue;
      }
      switch (op.op) {
        case 'move': {
          if (idx === -1) break;
          const r = result.objects[idx];
          if (Array.isArray(op.pos) && op.pos.length === 2) {
            r.object.pos = [Math.floor(Number(op.pos[0])), Math.floor(Number(op.pos[1]))];
          }
          if (typeof op.walk_layer === 'string') r.object.walk_layer = op.walk_layer;
          if (typeof op.yaw === 'number') r.object.yaw = Math.floor(op.yaw);
          if (typeof op.z_offset === 'number') r.zOffsetCm = Math.floor(op.z_offset);
          r.bPinned = true;
          break;
        }
        case 'delete': {
          const bHand = opId.startsWith(G_HAND_PREFIX) || (idx !== -1 && result.objects[idx].bHand);
          if (!bHand && !result.keepOuts.some((k) => k.id === opId)) {
            if (op.pos || idx !== -1) {
              const layer =
                idx !== -1
                  ? file.layers.find((l) => l.id === result.objects[idx].layerId)
                  : findLayerForId(file, opId);
              const posArr =
                Array.isArray(op.pos) && op.pos.length === 2
                  ? ([Math.floor(Number(op.pos[0])), Math.floor(Number(op.pos[1]))] as [number, number])
                  : result.objects[idx].object.pos;
              result.keepOuts.push({
                id: opId,
                layerId: layer ? layer.id : '',
                pos: posArr,
                radiusCm: spacingOf(layer),
              });
            }
          }
          if (idx !== -1) {
            result.objects.splice(idx, 1);
          }
          break;
        }
        case 'add': {
          if (idx !== -1 || !opObj) break;
          const layer = findLayerForId(file, opId);
          const addObj: PropObject = {
            ...(opObj as unknown as PropObject),
            origin: G_HAND_ORIGIN,
            locked: false,
          };
          result.objects.push({
            layerId: layer ? layer.id : '',
            object: addObj,
            bHand: true,
            bPinned: true,
          });
          break;
        }
        case 'set': {
          if (idx === -1) break;
          if (op.fields && typeof op.fields === 'object') {
            applySetFields(op.fields as Record<string, unknown>, opId, result.objects[idx].object);
          }
          result.objects[idx].bPinned = true;
          break;
        }
      }
    }
  }

  return result;
}

export function buildExclusionParams(ex?: Record<string, unknown>): PropsExclusionParams {
  if (!ex) {
    return {
      spawnCm: 0,
      endCm: 0,
      ladderCm: 0,
      stairCm: 0,
      doorCm: 0,
      routeMandatoryCm: 0,
      routeVariantsCm: 0,
      crossing: false,
      holeRimCm: 0,
    };
  }
  const routeCm = ex.route_cm;
  let routeMandatoryCm = 0;
  let routeVariantsCm = 0;
  if (typeof routeCm === 'number') {
    routeMandatoryCm = Math.max(0, Math.floor(routeCm));
    routeVariantsCm = routeMandatoryCm;
  } else if (routeCm && typeof routeCm === 'object') {
    const rObj = routeCm as Record<string, unknown>;
    if (typeof rObj.mandatory === 'number') {
      routeMandatoryCm = Math.max(0, Math.floor(rObj.mandatory));
    }
    if (typeof rObj.variants === 'number') {
      routeVariantsCm = Math.max(0, Math.floor(rObj.variants));
    }
  }

  return {
    spawnCm: typeof ex.spawn_cm === 'number' ? Math.max(0, Math.floor(ex.spawn_cm)) : 0,
    endCm: typeof ex.end_cm === 'number' ? Math.max(0, Math.floor(ex.end_cm)) : 0,
    ladderCm: typeof ex.ladder_cm === 'number' ? Math.max(0, Math.floor(ex.ladder_cm)) : 0,
    stairCm: typeof ex.stair_cm === 'number' ? Math.max(0, Math.floor(ex.stair_cm)) : 0,
    doorCm: typeof ex.door_cm === 'number' ? Math.max(0, Math.floor(ex.door_cm)) : 0,
    routeMandatoryCm,
    routeVariantsCm,
    crossing: Boolean(ex.crossing),
    holeRimCm: typeof ex.hole_rim_cm === 'number' ? Math.max(0, Math.floor(ex.hole_rim_cm)) : 0,
  };
}

const VALID_ANCHORS = new Set([
  'ambush_points',
  'roof_edges',
  'behind_tall_cover',
  'dead_ends',
  'roof_centres',
  'building_interiors',
]);

function defaultTurretRules(): PropsTurretRules {
  return {
    routeId: '',
    rangeMinCm: 0,
    rangeMaxCm: 0,
    arcDeg: 360,
    minCoveredM: 0,
    behindCoverGapCm: 0,
  };
}

function defaultMineDoorRules(): PropsMineDoorRules {
  return {
    enabled: false,
    mode: 'percent',
    value: 0,
    kinds: [],
    allowMandatory: false,
    insideCm: 0,
    trigger: 'tripwire',
    variant: 'mine_tripwire',
  };
}

function defaultMineCrateRules(): PropsMineCrateRules {
  return {
    enabled: false,
    sourceLayer: 'crates',
    percent: 0,
    trigger: 'crate_open',
    variant: 'mine_crate',
  };
}

function defaultLootRules(): PropsLootRules {
  return {
    budgetValue: 0,
    tables: [],
    tableValues: [],
    containersShareMilli: 0,
    containersBudgetMilli: 1000,
    sourceLayer: 'crates',
    containerVariant: 'loot_crate',
    farMinCm: 0,
    farMaxCm: 0,
    pileVariant: 'loot_pile',
    roofTable: '',
    roofTableValue: 0,
    weightNear: 1,
    weightBand: 1,
    weightFar: 1,
  };
}

function resolveExcludePreset(
  container: Record<string, unknown>,
  presets: Record<string, Record<string, unknown>>
): Record<string, unknown> {
  const ex = container.exclude as Record<string, unknown> | undefined;
  if (!ex || typeof ex.$preset !== 'string') {
    return ex ?? {};
  }
  const refPreset = presets[ex.$preset]?.exclude as Record<string, unknown> | undefined;
  const rest = { ...ex };
  delete rest.$preset;
  if (refPreset) {
    return mergeObjects(refPreset, rest);
  }
  return rest;
}

function readWhere(
  whereObj: Record<string, unknown>,
  out: {
    zones: string[];
    walkLayers: string[];
    near: PropsNearClause[];
    anchors: string[];
  }
): void {
  if (Array.isArray(whereObj.zones)) {
    out.zones = whereObj.zones.map(String);
  }
  if (Array.isArray(whereObj.walk_layers)) {
    out.walkLayers = whereObj.walk_layers.map(String);
  }
  if (Array.isArray(whereObj.near)) {
    out.near = [];
    for (const n of whereObj.near) {
      if (n && typeof n === 'object') {
        const what = String(n.what ?? '');
        if (what === 'wall' || what === 'cover') {
          out.near.push({
            what,
            max_cm: typeof n.max_cm === 'number' ? Math.max(0, Math.floor(n.max_cm)) : 0,
            weight: typeof n.weight === 'number' ? Math.max(1, Math.min(MAX_WEIGHT, Math.floor(n.weight))) : 1,
          });
        }
      }
    }
  }
  if (Array.isArray(whereObj.anchors)) {
    out.anchors = [];
    for (const a of whereObj.anchors) {
      const s = String(a);
      if (VALID_ANCHORS.has(s)) {
        out.anchors.push(s);
      }
    }
  }
}

function readCommon(
  merged: Record<string, unknown>,
  presets: Record<string, Record<string, unknown>>,
  halfSizeCm: Map<string, number>,
  budgetCap: number
): PropsCommonRules {
  const countObj = (merged.count ?? {}) as Record<string, unknown>;
  const excludeResolved = resolveExcludePreset(merged, presets);

  const out: PropsCommonRules = {
    countMode: String(countObj.mode ?? 'count'),
    countValue: typeof countObj.value === 'number' ? Math.max(0, Math.floor(countObj.value)) : 0,
    densityMilli: typeof countObj.per_100m2 === 'number' ? Math.round(countObj.per_100m2 * 1000) : 0,
    countMax: typeof countObj.max === 'number' ? Math.max(0, Math.floor(countObj.max)) : 2147483647,
    budgetCap,
    zones: [],
    walkLayers: ['ground'],
    near: [],
    variants: [],
    minSpacingCm: typeof merged.min_spacing_cm === 'number' ? Math.max(0, Math.floor(merged.min_spacing_cm)) : 0,
    minGapAnyCm: typeof merged.min_gap_any_cm === 'number' ? Math.max(0, Math.floor(merged.min_gap_any_cm)) : 0,
    wallGapCm: 0,
    exclude: excludeResolved,
    anchors: [],
    halfSizeCm,
  };
  out.wallGapCm = typeof merged.wall_gap_cm === 'number' ? Math.max(0, Math.floor(merged.wall_gap_cm)) : out.minGapAnyCm;

  if (merged.where && typeof merged.where === 'object') {
    readWhere(merged.where as Record<string, unknown>, out);
  }

  const varObj = (merged.variants ?? {}) as Record<string, unknown>;
  for (const k of Object.keys(varObj).sort()) {
    const w = varObj[k];
    if (typeof w === 'number' && w > 0) {
      out.variants.push({ key: k, weight: Math.floor(w) });
    }
  }

  return out;
}

export function buildRules(
  balance: Record<string, unknown>,
  layer: PropLayer
): PropsRules {
  const presets = (balance.presets ?? {}) as Record<string, Record<string, unknown>>;
  const presetName = layer.preset || `${layer.kind}_default`;
  const preset = presets[presetName];

  let merged = mergeObjects(preset, layer.rules as unknown as Record<string, unknown>);

  const halfSizeCm = new Map<string, number>();
  const kindsObj = (balance.kinds ?? {}) as Record<string, Record<string, unknown>>;
  for (const k of Object.keys(kindsObj).sort()) {
    const kindInfo = kindsObj[k];
    if (kindInfo && typeof kindInfo === 'object') {
      if (typeof kindInfo.radius_cm === 'number') {
        halfSizeCm.set(k, Math.max(0, Math.floor(kindInfo.radius_cm)));
      } else if (Array.isArray(kindInfo.size_cm) && kindInfo.size_cm.length >= 2) {
        const x = Math.floor(kindInfo.size_cm[0]);
        const y = Math.floor(kindInfo.size_cm[1]);
        halfSizeCm.set(k, Math.floor((Math.max(x, y) + 1) / 2));
      }
    }
  }

  const budgets = (balance.budgets ?? {}) as Record<string, number>;
  const budgetCap = typeof budgets[layer.kind] === 'number' ? Math.floor(budgets[layer.kind]) : 2147483647;

  const common = readCommon(merged, presets, halfSizeCm, budgetCap);

  const clusterObj = (merged.cluster ?? {}) as Record<string, unknown>;
  const explosiveObj = (merged.explosive ?? {}) as Record<string, unknown>;

  let clusterMin = typeof clusterObj.min === 'number' ? Math.max(1, Math.floor(clusterObj.min)) : 1;
  let clusterMax = typeof clusterObj.max === 'number' ? Math.max(clusterMin, Math.floor(clusterObj.max)) : clusterMin;

  let ringMinCm = 0;
  let ringMaxCm = 0;
  if (Array.isArray(clusterObj.ring_cm) && clusterObj.ring_cm.length === 2) {
    const r0 = Math.floor(clusterObj.ring_cm[0]);
    const r1 = Math.floor(clusterObj.ring_cm[1]);
    if (r0 >= 0 && r1 >= r0) {
      ringMinCm = r0;
      ringMaxCm = r1;
    }
  }

  let explosiveShareMilli = 0;
  if (typeof explosiveObj.share === 'number') {
    explosiveShareMilli = Math.max(0, Math.min(1000, Math.round(explosiveObj.share * 1000)));
  }

  const neverObj = (explosiveObj.never_within_cm_of ?? {}) as Record<string, number>;

  // Turrets
  const turretRules = defaultTurretRules();
  if (layer.kind === KIND_TURRETS && merged.cover_route && typeof merged.cover_route === 'object') {
    const coverObj = merged.cover_route as Record<string, unknown>;
    if (typeof coverObj.route === 'string') turretRules.routeId = coverObj.route;
    if (Array.isArray(coverObj.range_cm) && coverObj.range_cm.length === 2) {
      const r0 = Math.max(0, Math.floor(Number(coverObj.range_cm[0])));
      const r1 = Math.max(0, Math.floor(Number(coverObj.range_cm[1])));
      if (r1 >= r0) {
        turretRules.rangeMinCm = r0;
        turretRules.rangeMaxCm = r1;
      }
    }
    if (typeof coverObj.arc_deg === 'number') {
      turretRules.arcDeg = Math.max(1, Math.min(360, Math.floor(coverObj.arc_deg)));
    }
    if (typeof coverObj.min_covered_m === 'number') {
      turretRules.minCoveredM = Math.max(0, Math.floor(coverObj.min_covered_m));
    }
    if (typeof coverObj.behind_cover_gap_cm === 'number') {
      turretRules.behindCoverGapCm = Math.max(0, Math.floor(coverObj.behind_cover_gap_cm));
    }
  }

  // Mines
  const mineDoors = defaultMineDoorRules();
  const mineCrates = defaultMineCrateRules();
  let mineFieldEnabled = false;
  let fieldTrigger = 'proximity';
  let fieldVariant = 'mine_proximity';
  let fieldCommon: PropsCommonRules = {
    countMode: 'count',
    countValue: 0,
    densityMilli: 0,
    countMax: 2147483647,
    budgetCap,
    zones: [],
    walkLayers: ['ground'],
    near: [],
    variants: [],
    minSpacingCm: 0,
    minGapAnyCm: 0,
    wallGapCm: 0,
    exclude: {},
    anchors: [],
    halfSizeCm,
  };

  if (layer.kind === KIND_MINES) {
    if (merged.doorways && typeof merged.doorways === 'object') {
      const doorsObj = merged.doorways as Record<string, unknown>;
      mineDoors.enabled = true;
      if (doorsObj.mode === 'count' || doorsObj.mode === 'percent') {
        mineDoors.mode = doorsObj.mode;
      }
      if (typeof doorsObj.value === 'number') {
        mineDoors.value = Math.max(0, Math.floor(doorsObj.value));
      }
      if (Array.isArray(doorsObj.kinds)) {
        mineDoors.kinds = doorsObj.kinds
          .map(String)
          .filter((k) => k === 'building_door' || k === 'gate');
      }
      mineDoors.allowMandatory = doorsObj.mandatory_route === 'allow';
      if (typeof doorsObj.inside_cm === 'number') {
        mineDoors.insideCm = Math.max(0, Math.floor(doorsObj.inside_cm));
      }
      if (typeof doorsObj.trigger === 'string') {
        mineDoors.trigger = doorsObj.trigger;
      }
      if (typeof doorsObj.variant === 'string') {
        mineDoors.variant = doorsObj.variant;
      }
    }

    if (merged.crates && typeof merged.crates === 'object') {
      const cratesObj = merged.crates as Record<string, unknown>;
      mineCrates.enabled = true;
      if (typeof cratesObj.source_layer === 'string') {
        mineCrates.sourceLayer = cratesObj.source_layer;
      }
      if (typeof cratesObj.percent === 'number') {
        mineCrates.percent = Math.max(0, Math.floor(cratesObj.percent));
      }
      if (typeof cratesObj.trigger === 'string') {
        mineCrates.trigger = cratesObj.trigger;
      }
      if (typeof cratesObj.variant === 'string') {
        mineCrates.variant = cratesObj.variant;
      }
    }

    if (merged.field && typeof merged.field === 'object') {
      mineFieldEnabled = true;
      const fieldIn = merged.field as Record<string, unknown>;
      const fieldCopy = mergeObjects(fieldIn, undefined);
      fieldCommon = readCommon(fieldCopy, presets, halfSizeCm, budgetCap);
      if (typeof fieldIn.trigger === 'string') {
        fieldTrigger = fieldIn.trigger;
      }
      if (typeof fieldIn.variant === 'string') {
        fieldVariant = fieldIn.variant;
      }
    }
  }

  // Loot
  const lootRules = defaultLootRules();
  if (layer.kind === KIND_LOOT) {
    if (typeof merged.budget_value === 'number') {
      lootRules.budgetValue = Math.max(0, Math.floor(merged.budget_value));
    }
    const lootTablesObj = (balance.loot_tables ?? {}) as Record<string, Record<string, unknown>>;
    const getTableValue = (name: string): number => {
      const t = lootTablesObj[name];
      if (t && typeof t.value === 'number' && t.value > 0) {
        return Math.floor(t.value);
      }
      return 0;
    };

    if (Array.isArray(merged.tables)) {
      for (const tName of merged.tables) {
        if (typeof tName === 'string') {
          const val = getTableValue(tName);
          if (val > 0 && !lootRules.tables.includes(tName)) {
            lootRules.tables.push(tName);
            lootRules.tableValues.push(val);
          }
        }
      }
    }

    if (merged.containers && typeof merged.containers === 'object') {
      const contObj = merged.containers as Record<string, unknown>;
      if (typeof contObj.crates_share === 'number') {
        lootRules.containersShareMilli = Math.max(0, Math.min(1000, Math.round(contObj.crates_share * 1000)));
      }
      if (typeof contObj.budget_share === 'number') {
        lootRules.containersBudgetMilli = Math.max(0, Math.min(1000, Math.round(contObj.budget_share * 1000)));
      }
      if (typeof contObj.source_layer === 'string') {
        lootRules.sourceLayer = contObj.source_layer;
      }
      if (typeof contObj.variant === 'string') {
        lootRules.containerVariant = contObj.variant;
      }
    }

    if (merged.loose && typeof merged.loose === 'object') {
      const looseObj = merged.loose as Record<string, unknown>;
      if (looseObj.where && typeof looseObj.where === 'object') {
        readWhere(looseObj.where as Record<string, unknown>, common);
      }
      if (Array.isArray(looseObj.prefer_far_from_route_cm) && looseObj.prefer_far_from_route_cm.length === 2) {
        const r0 = Math.max(0, Math.floor(Number(looseObj.prefer_far_from_route_cm[0])));
        const r1 = Math.max(0, Math.floor(Number(looseObj.prefer_far_from_route_cm[1])));
        if (r1 >= r0) {
          lootRules.farMinCm = r0;
          lootRules.farMaxCm = r1;
        }
      }
      if (typeof looseObj.variant === 'string') {
        lootRules.pileVariant = looseObj.variant;
      }
      if (typeof looseObj.roof_table === 'string' && looseObj.roof_table.length > 0) {
        const val = getTableValue(looseObj.roof_table);
        if (val > 0) {
          lootRules.roofTable = looseObj.roof_table;
          lootRules.roofTableValue = val;
        }
      }
      if (looseObj.weights && typeof looseObj.weights === 'object') {
        const weightsObj = looseObj.weights as Record<string, unknown>;
        if (typeof weightsObj.near === 'number') {
          lootRules.weightNear = Math.max(1, Math.min(MAX_WEIGHT, Math.floor(weightsObj.near)));
        }
        if (typeof weightsObj.band === 'number') {
          lootRules.weightBand = Math.max(1, Math.min(MAX_WEIGHT, Math.floor(weightsObj.band)));
        }
        if (typeof weightsObj.far === 'number') {
          lootRules.weightFar = Math.max(1, Math.min(MAX_WEIGHT, Math.floor(weightsObj.far)));
        }
      }
    }
  }

  return {
    ...common,
    kind: layer.kind,
    clusterMin,
    clusterMax,
    ringMinCm,
    ringMaxCm,
    explosiveShareMilli,
    explosiveMinGapCm: typeof explosiveObj.min_gap_cm === 'number' ? Math.max(0, Math.floor(explosiveObj.min_gap_cm)) : 0,
    explosiveNeverSpawnCm: typeof neverObj.spawn === 'number' ? Math.max(0, Math.floor(neverObj.spawn)) : 0,
    explosiveNeverEndCm: typeof neverObj.end === 'number' ? Math.max(0, Math.floor(neverObj.end)) : 0,
    turret: turretRules,
    mineDoors,
    mineCrates,
    mineField: {
      enabled: mineFieldEnabled,
      common: fieldCommon,
      trigger: fieldTrigger,
      variant: fieldVariant,
    },
    loot: lootRules,
  };
}

// ---------------- MakeInput ----------------

export function makeInput(
  file: LevelPropsFile,
  balance: Record<string, unknown>,
  view: PropsLayoutView,
  layerId: string
): PropsPlacementInput {
  const layer = file.layers.find((l) => l.id === layerId);
  if (!layer) {
    throw new Error(`props: no layer '${layerId}'`);
  }
  const rules = buildRules(balance, layer);
  const resolved = applyOverrides(file);

  const kept: PropObject[] = [];
  const otherObjects: PropObject[] = [];
  const otherLayerIds: string[] = [];
  const reservedIds: string[] = [];

  for (const r of resolved.objects) {
    if (r.layerId === layerId) {
      if (r.object.locked || r.bPinned || r.bHand) {
        kept.push(r.object);
        reservedIds.push(r.object.id);
      }
    } else {
      otherObjects.push(r.object);
      otherLayerIds.push(r.layerId);
      reservedIds.push(r.object.id);
    }
  }

  const layerKinds = new Map<string, string>();
  for (const l of file.layers) {
    layerKinds.set(l.id, l.kind);
  }

  const keepOuts: PropsKeepOut[] = [];
  for (const k of resolved.keepOuts) {
    if (k.layerId === layerId) {
      keepOuts.push(k);
    }
  }

  if (Array.isArray(file.overrides)) {
    for (const op of file.overrides) {
      if ('id' in op && typeof op.id === 'string' && !reservedIds.includes(op.id)) {
        reservedIds.push(op.id);
      }
      if (
        op.op === 'add' &&
        op.object &&
        typeof op.object.id === 'string' &&
        !reservedIds.includes(op.object.id)
      ) {
        reservedIds.push(op.object.id);
      }
    }
  }

  for (const l of file.layers) {
    if (l.id !== layerId) {
      for (const o of l.objects) {
        if (!reservedIds.includes(o.id)) {
          reservedIds.push(o.id);
        }
      }
    }
  }

  return {
    view,
    rules,
    layerId: layer.id,
    seed: layer.seed,
    kept,
    keepOuts,
    otherObjects,
    otherLayerIds,
    layerKinds,
    reservedIds,
  };
}

// ---------------- Place Algorithm & Data Structures ----------------

export const EReject = {
  None: 0,
  Blocked: 1,
  Route: 2,
  Spawn: 3,
  Ladder: 4,
  Door: 5,
  Keepout: 6,
  OtherLayer: 7,
  Spacing: 8,
  Other: 9,
} as const;
export type EReject = (typeof EReject)[keyof typeof EReject];

interface Candidate {
  pos: [number, number];
  layer: number;
  weight: number;
  yaw?: number;
  score?: number;
}

export function countReject(c: PropsRejectCounts, r: EReject): void {
  switch (r) {
    case EReject.Blocked: c.blocked++; break;
    case EReject.Route: c.route++; break;
    case EReject.Spawn: c.spawn++; break;
    case EReject.Ladder: c.ladder++; break;
    case EReject.Door: c.door++; break;
    case EReject.Keepout: c.keepout++; break;
    case EReject.OtherLayer: c.other_layer++; break;
    case EReject.Spacing: c.spacing++; break;
    case EReject.Other: c.other++; break;
  }
}

function nextId(layerId: string, seed: number, reservedIds: string[], usedIds: Set<string>, counter: { val: number }): string {
  const seedHex = (seed >>> 0).toString(16).padStart(8, '0');
  for (;;) {
    const id = `${layerId}_${seedHex}_${counter.val++}`;
    if (!reservedIds.includes(id) && !usedIds.has(id)) {
      usedIds.add(id);
      return id;
    }
  }
}

function drawWeighted(rng: PropsRng, cands: Candidate[], alive: boolean[], total: bigint): number {
  const roll = BigInt(rng.nextBelow(Number(total)));
  let cum = 0n;
  for (let i = 0; i < cands.length; i++) {
    if (alive[i]) {
      cum += BigInt(cands[i].weight);
      if (roll < cum) {
        return i;
      }
    }
  }
  return -1;
}

function dropAround(cands: Candidate[], alive: boolean[], picked: number, spacing: number): bigint {
  let removed = 0n;
  const p = cands[picked].pos;
  for (let i = 0; i < cands.length; i++) {
    if (alive[i] && (i === picked || closer(cands[i].pos, p, spacing))) {
      alive[i] = false;
      removed += BigInt(cands[i].weight);
    }
  }
  return removed;
}

function computeRequested(r: PropsCommonRules, eligible: number, zoneCount: number): number {
  let total = 0;
  if (r.countMode === 'density') {
    total = Math.floor((r.densityMilli * eligible + 200000) / 400000);
  } else if (r.countMode === 'per_zone') {
    total = r.countValue * zoneCount;
  } else {
    total = r.countValue;
  }
  return Math.max(0, Math.min(total, r.countMax, r.budgetCap));
}

// ---------------- C4 Route & Arc Helpers ----------------

interface RouteSample {
  pos: [number, number];
  layer: string;
}

function coverRoute(view: PropsLayoutView, t: PropsTurretRules): PropsRoute | undefined {
  if (t.routeId.length > 0) {
    return view.findRoute(t.routeId);
  }
  const mandatory = view.getMandatoryRouteIds();
  return mandatory.length > 0 ? view.findRoute(mandatory[0]) : undefined;
}

function routeSamples(route: PropsRoute): RouteSample[] {
  const out: RouteSample[] = [];
  for (let i = 0; i + 1 < route.points.length; i++) {
    if (route.layers[i] !== route.layers[i + 1]) {
      continue;
    }
    const a = route.points[i];
    const dx = BigInt(route.points[i + 1][0] - a[0]);
    const dy = BigInt(route.points[i + 1][1] - a[1]);
    const len = isqrt(dx * dx + dy * dy);
    if (len === 0n) {
      continue;
    }
    const n = (len + 99n) / 100n;
    for (let k = 0n; k < n; k++) {
      out.push({
        pos: [a[0] + floorDiv(dx * k, n), a[1] + floorDiv(dy * k, n)],
        layer: route.layers[i],
      });
    }
  }
  return out;
}

class FArc {
  sx: bigint;
  sy: bigint;
  ex: bigint;
  ey: bigint;
  arc: number;

  constructor(yaw: number, inArc: number) {
    this.arc = inArc;
    const start = yaw - Math.floor(inArc / 2);
    this.sx = cosQ(start);
    this.sy = sinQ(start);
    this.ex = cosQ(start + inArc);
    this.ey = sinQ(start + inArc);
  }

  contains(d: [number, number]): boolean {
    if (this.arc >= 360) {
      return true;
    }
    const x = BigInt(d[0]);
    const y = BigInt(d[1]);
    if (this.arc <= 180) {
      return this.sx * y - this.sy * x >= 0n && x * this.ey - y * this.ex >= 0n;
    }
    return !(this.ex * y - this.ey * x > 0n && x * this.sy - y * this.sx > 0n);
  }
}

class FCoverage {
  view: PropsLayoutView;
  t: PropsTurretRules;
  route?: PropsRoute;
  samples: RouteSample[] = [];
  blockers = new Map<string, PropsSightSegment[]>();
  arcs: FArc[] = [];

  constructor(view: PropsLayoutView, t: PropsTurretRules) {
    this.view = view;
    this.t = t;
    this.route = coverRoute(view, t);
    if (this.route) {
      this.samples = routeSamples(this.route);
    }
    const arcDeg = Math.max(1, Math.min(360, t.arcDeg));
    for (let yaw = 0; yaw < 360; yaw += TURRET_YAW_STEP_DEG) {
      this.arcs.push(new FArc(yaw, arcDeg));
    }
  }

  score(p: [number, number], layer: string): { score: number; yaw: number } {
    const l = this.view.findLayer(layer);
    if (!this.route || !l) {
      return { score: 0, yaw: 0 };
    }
    const bRoof = l.kind === 'roof';
    let b = this.blockers.get(layer);
    if (!b) {
      b = this.view.getSightBlockers(layer);
      this.blockers.set(layer, b);
    }
    const min2 = BigInt(this.t.rangeMinCm) * BigInt(this.t.rangeMinCm);
    const max2 = BigInt(this.t.rangeMaxCm) * BigInt(this.t.rangeMaxCm);
    const seen: [number, number][] = [];
    for (const s of this.samples) {
      if (s.layer !== layer && !(bRoof && s.layer === 'ground')) {
        continue;
      }
      const d2 = distSq(p, s.pos);
      if (d2 < min2 || d2 > max2 || PropsLayoutView.isSightBlocked(p, s.pos, b)) {
        continue;
      }
      seen.push([s.pos[0] - p[0], s.pos[1] - p[1]]);
    }
    let best = 0;
    let outYaw = 0;
    for (let i = 0; i < this.arcs.length; i++) {
      let n = 0;
      for (const d of seen) {
        if (this.arcs[i].contains(d)) n++;
      }
      if (n > best) {
        best = n;
        outYaw = i * TURRET_YAW_STEP_DEG;
      }
    }
    return { score: best, yaw: outYaw };
  }
}

// ---------------- Anchor Collection ----------------

interface Anchor {
  pos: [number, number];
  layer: string;
}

function gatherAnchors(
  view: PropsLayoutView,
  layerNames: string[],
  rules: PropsRules,
  farSideRoute?: PropsRoute,
  behindGapCm: number = 0
): Anchor[] {
  const out: Anchor[] = [];
  const cfg = view.config;
  const ground = 'ground';
  const step = LATTICE_CM;

  for (const name of rules.anchors) {
    if (name === 'ambush_points') {
      for (const a of view.ambushPoints) {
        out.push({ pos: a.pos, layer: ground });
      }
    } else if (name === 'roof_edges' || name === 'roof_centres') {
      for (const layer of layerNames) {
        if (!isRoofLayer(view, layer)) continue;
        const extent = view.getLayerExtent(layer);
        if (!extent) continue;
        if (name === 'roof_centres') {
          out.push({ pos: snappedCentre(extent.min, extent.max), layer });
          continue;
        }
        // roof_edges
        for (let y = ceilStep(extent.min[1]); y <= extent.max[1]; y += step) {
          for (let x = ceilStep(extent.min[0]); x <= extent.max[0]; x += step) {
            const edge = Math.min(x - extent.min[0], extent.max[0] - x, y - extent.min[1], extent.max[1] - y);
            if (edge >= cfg.roofEdgeInsetCm && edge < cfg.roofEdgeInsetCm + step) {
              out.push({ pos: [x, y], layer });
            }
          }
        }
      }
    } else if (name === 'behind_tall_cover') {
      const K = SIGHT_SCALE;
      for (const c of view.getCovers()) {
        if (c.heightCm < cfg.tallCoverMinCm) continue;
        const centreDist = farSideRoute ? view.distanceToRouteCm(c.pos, farSideRoute, ground) : 0;
        const cs = cosQ(c.yaw);
        const sn = sinQ(c.yaw);
        const lx = BigInt(c.sizeX) + 2n * BigInt(behindGapCm);
        const ly = BigInt(c.sizeY) + 2n * BigInt(behindGapCm);
        const offsets: [number, number][] = [
          [floorDiv(cs * lx, K), floorDiv(sn * lx, K)],
          [floorDiv(-cs * lx, K), floorDiv(-sn * lx, K)],
          [floorDiv(-sn * ly, K), floorDiv(cs * ly, K)],
          [floorDiv(sn * ly, K), floorDiv(-cs * ly, K)],
        ];
        for (const off of offsets) {
          const q: [number, number] = [c.pos[0] + off[0], c.pos[1] + off[1]];
          if (farSideRoute && !(view.distanceToRouteCm(q, farSideRoute, ground) > centreDist)) {
            continue;
          }
          out.push({ pos: q, layer: ground });
        }
      }
    } else if (name === 'dead_ends') {
      for (const z of view.zones) {
        if (z.kind !== 'wall_end' || z.poly.length < 3) continue;
        let minX = z.poly[0][0];
        let maxX = z.poly[0][0];
        let minY = z.poly[0][1];
        let maxY = z.poly[0][1];
        for (const p of z.poly) {
          minX = Math.min(minX, p[0]);
          maxX = Math.max(maxX, p[0]);
          minY = Math.min(minY, p[1]);
          maxY = Math.max(maxY, p[1]);
        }
        for (let y = ceilStep(minY); y <= maxY; y += step) {
          for (let x = ceilStep(minX); x <= maxX; x += step) {
            if (view.isInZone([x, y], z.id)) {
              out.push({ pos: [x, y], layer: ground });
            }
          }
        }
      }
    } else if (name === 'building_interiors') {
      for (const b of view.getBuildings()) {
        if (!b.bEnterable) continue;
        const c = snappedCentre(b.min, b.max);
        out.push({ pos: c, layer: ground });
        for (let n = 1; !b.bHollow && n < b.floors; n++) {
          out.push({ pos: c, layer: `floor:${b.id}:${n}` });
        }
      }
    }
  }
  return out;
}

function filterAnchors(
  view: PropsLayoutView,
  layerNames: string[],
  inZones: (p: [number, number]) => boolean,
  staticReject: (p: [number, number], layer: string, h: number) => EReject,
  anchors: Anchor[],
  half: number,
  spaced: PropObject[],
  spacingCm: number,
  counts: { eligible: number },
  rejected: PropsRejectCounts,
  out: Candidate[]
): void {
  const all: Candidate[] = [];
  for (const a of anchors) {
    const layerIndex = layerNames.indexOf(a.layer);
    if (layerIndex === -1 || !view.isWalkable(a.pos, a.layer) || !inZones(a.pos)) {
      continue;
    }
    all.push({
      pos: a.pos,
      layer: layerIndex,
      weight: 1,
    });
  }

  all.sort((a, b) => {
    if (a.pos[1] !== b.pos[1]) return a.pos[1] - b.pos[1];
    if (a.pos[0] !== b.pos[0]) return a.pos[0] - b.pos[0];
    const nameA = layerNames[a.layer];
    const nameB = layerNames[b.layer];
    return nameA < nameB ? -1 : nameA > nameB ? 1 : 0;
  });

  for (let i = 0; i < all.length; i++) {
    if (
      i > 0 &&
      all[i].pos[0] === all[i - 1].pos[0] &&
      all[i].pos[1] === all[i - 1].pos[1] &&
      all[i].layer === all[i - 1].layer
    ) {
      continue;
    }
    counts.eligible++;
    let reject = staticReject(all[i].pos, layerNames[all[i].layer], half);
    if (reject === EReject.None) {
      for (const k of spaced) {
        if (closer(all[i].pos, k.pos, spacingCm)) {
          reject = EReject.Spacing;
          break;
        }
      }
    }
    if (reject !== EReject.None) {
      countReject(rejected, reject);
      continue;
    }
    out.push(all[i]);
  }
}

function gatherLattice(
  view: PropsLayoutView,
  layerNames: string[],
  inZones: (p: [number, number]) => boolean,
  staticReject: (p: [number, number], layer: string, h: number) => EReject,
  weightAt: (p: [number, number], layer: string, h: number) => number,
  half: number,
  spaced: PropObject[],
  spacingCm: number,
  counts: { eligible: number },
  rejected: PropsRejectCounts,
  out: Candidate[]
): void {
  for (let layerIndex = 0; layerIndex < layerNames.length; layerIndex++) {
    const layer = layerNames[layerIndex];
    const extent = view.getLayerExtent(layer);
    if (!extent) continue;
    for (let y = ceilStep(extent.min[1]); y <= extent.max[1]; y += LATTICE_CM) {
      for (let x = ceilStep(extent.min[0]); x <= extent.max[0]; x += LATTICE_CM) {
        const p: [number, number] = [x, y];
        if (!view.isWalkable(p, layer) || !inZones(p)) {
          continue;
        }
        counts.eligible++;
        let reject = staticReject(p, layer, half);
        if (reject === EReject.None) {
          for (const k of spaced) {
            if (closer(p, k.pos, spacingCm)) {
              reject = EReject.Spacing;
              break;
            }
          }
        }
        if (reject !== EReject.None) {
          countReject(rejected, reject);
          continue;
        }
        out.push({
          pos: p,
          layer: layerIndex,
          weight: weightAt(p, layer, half),
        });
      }
    }
  }

  out.sort((a, b) => {
    if (a.pos[1] !== b.pos[1]) return a.pos[1] - b.pos[1];
    if (a.pos[0] !== b.pos[0]) return a.pos[0] - b.pos[0];
    const nameA = layerNames[a.layer];
    const nameB = layerNames[b.layer];
    return nameA < nameB ? -1 : nameA > nameB ? 1 : 0;
  });
}

// ---------------- C4 Mines & Loot Helpers ----------------

function sourceObjects(input: PropsPlacementInput, layerId: string): number[] {
  const out: number[] = [];
  for (let i = 0; i < input.otherObjects.length; i++) {
    if (input.otherLayerIds[i] === layerId) {
      out.push(i);
    }
  }
  out.sort((a, b) => {
    const idA = input.otherObjects[a].id;
    const idB = input.otherObjects[b].id;
    return idA < idB ? -1 : idA > idB ? 1 : 0;
  });
  return out;
}

function findOther(input: PropsPlacementInput, layerId: string, id: string): number {
  for (let i = 0; i < input.otherObjects.length; i++) {
    if (input.otherLayerIds[i] === layerId && input.otherObjects[i].id === id) {
      return i;
    }
  }
  return -1;
}

function minedIds(input: PropsPlacementInput): Set<string> {
  const out = new Set<string>();
  for (let i = 0; i < input.otherObjects.length; i++) {
    const kind = input.layerKinds.get(input.otherLayerIds[i]);
    if (kind === KIND_MINES && input.otherObjects[i].attach) {
      out.add(input.otherObjects[i].attach!);
    }
  }
  return out;
}

function doorMinePos(w: PropsLayoutView['doorways'][0], insideCm: number): [number, number] {
  return [
    w.pos[0] - w.outward[0] * insideCm,
    w.pos[1] - w.outward[1] * insideCm,
  ];
}

function doorMineYaw(w: PropsLayoutView['doorways'][0]): number {
  return w.along[0] !== 0 ? 90 : 0;
}

function doorKindName(w: PropsLayoutView['doorways'][0]): string {
  return w.kind === 'gate' ? 'gate' : 'building_door';
}

function subShort(s: PropSubReport): number {
  return Math.max(0, s.requested - s.kept - s.placed);
}

function tableValueOf(rules: PropsLootRules, table: string): number {
  const idx = rules.tables.indexOf(table);
  if (idx !== -1) {
    return rules.tableValues[idx];
  }
  if (rules.roofTable.length > 0 && rules.roofTable === table) {
    return rules.roofTableValue;
  }
  return 0;
}

// ---------------- Place Turrets ----------------

export function placeTurrets(input: PropsPlacementInput, result: PropsPlacementResult): void {
  const report = result.report;
  const r = input.rules;
  const finish = () => {
    report.short_by = Math.max(0, report.requested - report.kept - report.placed);
  };
  if (r.variants.length === 0) {
    report.warnings.push('no_variants');
    return;
  }
  if (!input.view) {
    report.warnings.push('no_layout');
    return;
  }
  const view = input.view;
  const half = maxVariantHalf(r);
  const coverage = new FCoverage(view, r.turret);

  const shapes = view.buildExclusions(buildExclusionParams(r.exclude));
  const layerNames: string[] = [];
  for (const l of view.layers) {
    if (matchAnyLayer(r.walkLayers, l.name)) {
      layerNames.push(l.name);
    }
  }
  const otherHalf: number[] = input.otherObjects.map((o) => halfOf(r, o.variant));
  const keepOutRadius = (k: PropsKeepOut) => (k.radiusCm > 0 ? k.radiusCm : r.minSpacingCm);

  const staticReject = (p: [number, number], layer: string, h: number): EReject => {
    if (view.isBlocked(p, layer, h + r.wallGapCm)) {
      return EReject.Blocked;
    }
    for (const s of shapes) {
      if (PropsLayoutView.isExcluded([s], p, layer)) {
        switch (s.kind) {
          case 'spawn':
          case 'end':
            return EReject.Spawn;
          case 'ladder_foot':
          case 'ladder_top':
          case 'stair':
            return EReject.Ladder;
          case 'door':
            return EReject.Door;
          case 'route':
          case 'crossing':
            return EReject.Route;
          default:
            return EReject.Other;
        }
      }
    }
    for (const k of input.keepOuts) {
      if (closer(p, k.pos, keepOutRadius(k))) {
        return EReject.Keepout;
      }
    }
    for (let i = 0; i < input.otherObjects.length; i++) {
      if (input.otherObjects[i].attach) continue;
      if (closer(p, input.otherObjects[i].pos, h + otherHalf[i] + r.minGapAnyCm)) {
        return EReject.OtherLayer;
      }
    }
    return EReject.None;
  };

  const inZones = (p: [number, number]): boolean => {
    if (r.zones.length === 0 || r.zones.includes('*')) return true;
    for (const z of r.zones) {
      if (view.isInZone(p, z)) return true;
    }
    return false;
  };

  const zoneCount = (): number => {
    if (r.zones.length === 0 || r.zones.includes('*')) return view.zones.length;
    let n = 0;
    for (const z of r.zones) {
      if (view.findZone(z)) n++;
    }
    return n;
  };

  const anchors = gatherAnchors(view, layerNames, r, coverage.route, r.turret.behindCoverGapCm);
  const eligibleCounts = { eligible: 0 };
  const filtered: Candidate[] = [];
  filterAnchors(
    view,
    layerNames,
    inZones,
    staticReject,
    anchors,
    half,
    input.kept,
    r.minSpacingCm,
    eligibleCounts,
    report.rejected,
    filtered
  );
  report.eligible = eligibleCounts.eligible;
  report.requested = computeRequested(r, report.eligible, zoneCount());
  const target = Math.max(0, report.requested - report.kept);
  if (!coverage.route) {
    report.warnings.push('no_cover_route');
    filtered.length = 0;
  }
  const minScore = Math.max(1, r.turret.minCoveredM);
  const cands: Candidate[] = [];
  for (const c of filtered) {
    const res = coverage.score(c.pos, layerNames[c.layer]);
    c.score = res.score;
    c.yaw = res.yaw;
    if (c.score < minScore) {
      report.rejected.coverage++;
      continue;
    }
    c.weight = Math.min(c.score, MAX_WEIGHT);
    cands.push(c);
  }
  if (target > 0 && cands.length === 0) {
    report.warnings.push('no_candidates');
  }

  let totalWeight = 0n;
  for (const c of cands) {
    totalWeight += BigInt(c.weight);
  }
  const alive = new Array(cands.length).fill(true);
  const rng = new PropsRng(streamSeed(input.seed, input.layerId));
  const idCounter = { val: 0 };
  const usedIds = new Set<string>();
  const variantTotal = r.variants.reduce((sum, v) => sum + v.weight, 0);

  let placed = 0;
  while (placed < target && totalWeight > 0n) {
    const pick = drawWeighted(rng, cands, alive, totalWeight);
    if (pick === -1) break;
    const c = cands[pick];
    const walkLayer = layerNames[c.layer];

    const variantRoll = rng.nextBelow(variantTotal);
    let cum = 0;
    let pickedVariant = r.variants[0].key;
    for (const v of r.variants) {
      cum += v.weight;
      if (variantRoll < cum) {
        pickedVariant = v.key;
        break;
      }
    }

    const id = nextId(input.layerId, input.seed, input.reservedIds, usedIds, idCounter);
    result.objects.push({
      id,
      variant: pickedVariant,
      pos: c.pos,
      walk_layer: walkLayer,
      yaw: c.yaw ?? 0,
      arc: Math.max(1, Math.min(360, r.turret.arcDeg)),
      locked: false,
      covered_m: c.score ?? 0,
    });
    placed++;
    totalWeight -= dropAround(cands, alive, pick, r.minSpacingCm);
  }
  report.placed = placed;
  finish();
  result.rngCalls = rng.calls;
}

// ---------------- Place Mines ----------------

export function placeMines(input: PropsPlacementInput, result: PropsPlacementResult): void {
  const report = result.report;
  const r = input.rules;
  if (!input.view) {
    report.warnings.push('no_layout');
    return;
  }
  const view = input.view;
  const doors: PropSubReport = { eligible: 0, requested: 0, placed: 0, kept: 0, dropped: 0, forbidden: 0 };
  const crates: PropSubReport = { eligible: 0, requested: 0, placed: 0, kept: 0, dropped: 0, forbidden: 0 };
  const field: PropSubReport = { eligible: 0, requested: 0, placed: 0, kept: 0, dropped: 0, forbidden: 0 };

  const kept: PropObject[] = [];
  const minedDoors = new Set<string>();
  const minedCrates = new Set<string>();
  for (const k of input.kept) {
    if (k.door) {
      if (!view.findDoorway(k.door)) {
        report.warnings.push(`door_missing ${k.id}`);
        doors.dropped++;
        continue;
      }
      doors.kept++;
      minedDoors.add(k.door);
    } else if (k.attach) {
      if (findOther(input, r.mineCrates.sourceLayer, k.attach) === -1) {
        report.warnings.push(`attach_dropped ${k.id}`);
        crates.dropped++;
        continue;
      }
      crates.kept++;
      minedCrates.add(k.attach);
    } else {
      field.kept++;
    }
    kept.push(k);
  }
  result.objects = [...kept];
  report.kept = kept.length;

  const shapes = view.buildExclusions(buildExclusionParams(r.exclude));
  const keepOutRadius = (k: PropsKeepOut) => (k.radiusCm > 0 ? k.radiusCm : r.minSpacingCm);
  const rng = new PropsRng(streamSeed(input.seed, input.layerId));
  const idCounter = { val: 0 };
  const usedIds = new Set<string>();
  let budget = r.budgetCap;
  const take = (target: number): number => {
    const requested = Math.max(0, Math.min(target, budget));
    budget -= requested;
    return requested;
  };

  // 1. doorways
  if (r.mineDoors.enabled) {
    const d = r.mineDoors;
    const ways = view.doorways;
    const free: number[] = [];
    for (let i = 0; i < ways.length; i++) {
      const w = ways[i];
      if (d.kinds.length > 0 && !d.kinds.includes(doorKindName(w))) {
        continue;
      }
      if (w.bOnMandatoryRoute && !d.allowMandatory) {
        doors.forbidden++;
        continue;
      }
      if (minedDoors.has(w.id)) {
        doors.eligible++;
        continue;
      }
      const pos = doorMinePos(w, d.insideCm);
      let bExcluded = false;
      for (const s of shapes) {
        if (PropsLayoutView.isExcluded([s], pos, w.walkLayer)) {
          switch (s.kind) {
            case 'spawn':
            case 'end':
              report.rejected.spawn++;
              break;
            case 'ladder_foot':
            case 'ladder_top':
            case 'stair':
              report.rejected.ladder++;
              break;
            case 'door':
              report.rejected.door++;
              break;
            case 'route':
            case 'crossing':
              report.rejected.route++;
              break;
            default:
              report.rejected.other++;
              break;
          }
          bExcluded = true;
          break;
        }
      }
      if (bExcluded) continue;
      if (input.keepOuts.some((k) => closer(pos, k.pos, keepOutRadius(k)))) {
        report.rejected.keepout++;
        continue;
      }
      doors.eligible++;
      free.push(i);
    }
    const target = d.mode === 'count' ? d.value : Math.floor((d.value * doors.eligible + 99) / 100);
    doors.requested = take(target);
    const want = Math.max(0, doors.requested - doors.kept);
    const order = Array.from({ length: free.length }, (_, i) => i);
    rng.shuffle(order);
    for (let k = 0; k < order.length && doors.placed < want; k++) {
      const w = ways[free[order[k]]];
      const id = nextId(input.layerId, input.seed, input.reservedIds, usedIds, idCounter);
      result.objects.push({
        id,
        variant: d.variant,
        pos: doorMinePos(w, d.insideCm),
        walk_layer: w.walkLayer,
        yaw: doorMineYaw(w),
        door: w.id,
        trigger: d.trigger,
        width_cm: w.widthCm > 0 ? w.widthCm : view.config.doorFallbackWidthCm,
        locked: false,
      });
      doors.placed++;
    }
  }

  // 2. crates
  if (r.mineCrates.enabled) {
    const c = r.mineCrates;
    if (!input.layerKinds.has(c.sourceLayer)) {
      report.warnings.push(`no_source_layer ${c.sourceLayer}`);
    }
    const free: number[] = [];
    for (const index of sourceObjects(input, c.sourceLayer)) {
      const crate = input.otherObjects[index];
      if (crate.locked) continue;
      crates.eligible++;
      if (!minedCrates.has(crate.id)) {
        free.push(index);
      }
    }
    crates.requested = take(Math.floor((c.percent * crates.eligible + 99) / 100));
    const want = Math.max(0, crates.requested - crates.kept);
    const order = Array.from({ length: free.length }, (_, i) => i);
    rng.shuffle(order);
    for (let k = 0; k < order.length && crates.placed < want; k++) {
      const crate = input.otherObjects[free[order[k]]];
      const id = nextId(input.layerId, input.seed, input.reservedIds, usedIds, idCounter);
      result.objects.push({
        id,
        variant: c.variant,
        pos: crate.pos,
        walk_layer: crate.walk_layer,
        yaw: 0,
        attach: crate.id,
        trigger: c.trigger,
        locked: false,
      });
      crates.placed++;
    }
  }

  // 3. field
  if (r.mineField.enabled) {
    const f = r.mineField;
    const fieldShapes = view.buildExclusions(buildExclusionParams(f.common.exclude));
    const fieldLayerNames: string[] = [];
    for (const l of view.layers) {
      if (matchAnyLayer(f.common.walkLayers, l.name)) {
        fieldLayerNames.push(l.name);
      }
    }
    const fieldOtherHalf: number[] = input.otherObjects.map((o) => halfOf(f.common, o.variant));
    const fieldKeepOutRadius = (k: PropsKeepOut) => (k.radiusCm > 0 ? k.radiusCm : f.common.minSpacingCm);

    const fieldStaticReject = (p: [number, number], layer: string, h: number): EReject => {
      if (view.isBlocked(p, layer, h + f.common.wallGapCm)) {
        return EReject.Blocked;
      }
      for (const s of fieldShapes) {
        if (PropsLayoutView.isExcluded([s], p, layer)) {
          switch (s.kind) {
            case 'spawn':
            case 'end':
              return EReject.Spawn;
            case 'ladder_foot':
            case 'ladder_top':
            case 'stair':
              return EReject.Ladder;
            case 'door':
              return EReject.Door;
            case 'route':
            case 'crossing':
              return EReject.Route;
            default:
              return EReject.Other;
          }
        }
      }
      for (const k of input.keepOuts) {
        if (closer(p, k.pos, fieldKeepOutRadius(k))) {
          return EReject.Keepout;
        }
      }
      for (let i = 0; i < input.otherObjects.length; i++) {
        if (input.otherObjects[i].attach) continue;
        if (closer(p, input.otherObjects[i].pos, h + fieldOtherHalf[i] + f.common.minGapAnyCm)) {
          return EReject.OtherLayer;
        }
      }
      return EReject.None;
    };

    const fieldInZones = (p: [number, number]): boolean => {
      if (f.common.zones.length === 0 || f.common.zones.includes('*')) return true;
      for (const z of f.common.zones) {
        if (view.isInZone(p, z)) return true;
      }
      return false;
    };

    const fieldZoneCount = (): number => {
      if (f.common.zones.length === 0 || f.common.zones.includes('*')) return view.zones.length;
      let n = 0;
      for (const z of f.common.zones) {
        if (view.findZone(z)) n++;
      }
      return n;
    };

    const fieldWeightAt = (p: [number, number], layer: string, h: number): number => {
      let w = 1n;
      for (const c of f.common.near) {
        const bNear =
          c.what === 'wall'
            ? view.isNearWall(p, layer, h + c.max_cm)
            : c.what === 'cover'
            ? view.isNearCover(p, layer, h + c.max_cm)
            : false;
        if (bNear) {
          const mult = BigInt(Math.max(1, Math.min(MAX_WEIGHT, c.weight)));
          w = w * mult;
          if (w > BigInt(MAX_WEIGHT)) w = BigInt(MAX_WEIGHT);
        }
      }
      return Number(w);
    };

    const spaced = [...result.objects];
    const fieldEligibleCounts = { eligible: 0 };
    const cands: Candidate[] = [];
    gatherLattice(
      view,
      fieldLayerNames,
      fieldInZones,
      fieldStaticReject,
      fieldWeightAt,
      halfOf(f.common, f.variant),
      spaced,
      f.common.minSpacingCm,
      fieldEligibleCounts,
      report.rejected,
      cands
    );
    field.eligible = fieldEligibleCounts.eligible;
    field.requested = take(computeRequested(f.common, field.eligible, fieldZoneCount()));
    const want = Math.max(0, field.requested - field.kept);
    if (want > 0 && cands.length === 0) {
      report.warnings.push('no_candidates');
    }
    let totalWeight = 0n;
    for (const c of cands) {
      totalWeight += BigInt(c.weight);
    }
    const alive = new Array(cands.length).fill(true);
    while (field.placed < want && totalWeight > 0n) {
      const pick = drawWeighted(rng, cands, alive, totalWeight);
      if (pick === -1) break;
      const id = nextId(input.layerId, input.seed, input.reservedIds, usedIds, idCounter);
      result.objects.push({
        id,
        variant: f.variant,
        pos: cands[pick].pos,
        walk_layer: fieldLayerNames[cands[pick].layer],
        yaw: 0,
        trigger: f.trigger,
        locked: false,
      });
      field.placed++;
      totalWeight -= dropAround(cands, alive, pick, f.common.minSpacingCm);
    }
  }

  report.subA = doors;
  report.subB = crates;
  report.subC = field;
  report.eligible = 0;
  report.requested = doors.requested + crates.requested + field.requested;
  report.placed = doors.placed + crates.placed + field.placed;
  report.short_by = subShort(doors) + subShort(crates) + subShort(field);
  report.status = report.short_by > 0 ? 'short' : 'ok';
  result.rngCalls = rng.calls;
}

// ---------------- Place Loot ----------------

export function placeLoot(input: PropsPlacementInput, result: PropsPlacementResult): void {
  const report = result.report;
  const r = input.rules;
  const l = r.loot;
  if (!input.view) {
    report.warnings.push('no_layout');
    return;
  }
  const view = input.view;
  const containers: PropSubReport = { eligible: 0, requested: 0, placed: 0, kept: 0, dropped: 0, forbidden: 0 };
  const loose: PropSubReport = { eligible: 0, requested: 0, placed: 0, kept: 0, dropped: 0, forbidden: 0 };
  report.budgetValue = l.budgetValue;

  const kept: PropObject[] = [];
  const filled = new Set<string>();
  let keptValue = 0;
  let containerSpent = 0;

  for (const k of input.kept) {
    if (k.attach) {
      if (findOther(input, l.sourceLayer, k.attach) === -1) {
        report.warnings.push(`attach_dropped ${k.id}`);
        containers.dropped++;
        continue;
      }
      containers.kept++;
      filled.add(k.attach);
    } else {
      loose.kept++;
    }
    const val = tableValueOf(l, k.loot_table ?? '');
    if (val === 0) {
      report.warnings.push(`unknown_table ${k.id}`);
    }
    keptValue += val;
    containerSpent += k.attach ? val : 0;
    kept.push(k);
  }
  result.objects = [...kept];
  report.kept = kept.length;

  const budgetForNew = Math.max(0, l.budgetValue - keptValue);
  let remaining = budgetForNew;
  let countLeft = Math.max(0, r.budgetCap - report.kept);

  const groundTables: number[] = [];
  for (let i = 0; i < l.tables.length; i++) {
    if (!l.roofTable || l.tables[i] !== l.roofTable) {
      groundTables.push(i);
    }
  }
  if (groundTables.length === 0) {
    for (let i = 0; i < l.tables.length; i++) {
      groundTables.push(i);
    }
  }

  let cheapest = 2147483647;
  for (const i of groundTables) {
    cheapest = Math.min(cheapest, l.tableValues[i]);
  }
  if (l.roofTable.length > 0) {
    cheapest = Math.min(cheapest, l.roofTableValue);
  }
  if (l.tables.length === 0) {
    report.warnings.push('no_tables');
  }

  const affordable = (options: number[], limit: number): number[] => {
    return options.filter((i) => l.tableValues[i] <= limit);
  };
  const containerCap = Math.floor((l.budgetValue * l.containersBudgetMilli) / 1000);

  const rng = new PropsRng(streamSeed(input.seed, input.layerId));
  const idCounter = { val: 0 };
  const usedIds = new Set<string>();

  // 1. containers
  {
    const mined = minedIds(input);
    const free: number[] = [];
    for (const index of sourceObjects(input, l.sourceLayer)) {
      const crate = input.otherObjects[index];
      if (crate.locked || mined.has(crate.id)) continue;
      containers.eligible++;
      if (!filled.has(crate.id)) {
        free.push(index);
      }
    }
    containers.requested = Math.floor((l.containersShareMilli * containers.eligible + 500) / 1000);
    const want = Math.max(0, containers.requested - containers.kept);
    const order = Array.from({ length: free.length }, (_, i) => i);
    rng.shuffle(order);
    for (let k = 0; k < order.length && containers.placed < want && countLeft > 0; k++) {
      const options = affordable(groundTables, Math.min(remaining, containerCap - containerSpent));
      if (options.length === 0) break;
      const table = options[rng.nextBelow(options.length)];
      const crate = input.otherObjects[free[order[k]]];
      const id = nextId(input.layerId, input.seed, input.reservedIds, usedIds, idCounter);
      result.objects.push({
        id,
        variant: l.containerVariant,
        pos: crate.pos,
        walk_layer: crate.walk_layer,
        yaw: 0,
        attach: crate.id,
        loot_table: l.tables[table],
        locked: false,
      });
      remaining -= l.tableValues[table];
      containerSpent += l.tableValues[table];
      countLeft--;
      containers.placed++;
    }
  }

  // 2. loose piles
  {
    const shapes = view.buildExclusions(buildExclusionParams(r.exclude));
    const layerNames: string[] = [];
    for (const lay of view.layers) {
      if (matchAnyLayer(r.walkLayers, lay.name)) {
        layerNames.push(lay.name);
      }
    }
    const otherHalf: number[] = input.otherObjects.map((o) => halfOf(r, o.variant));
    const keepOutRadius = (k: PropsKeepOut) => (k.radiusCm > 0 ? k.radiusCm : r.minSpacingCm);

    const staticReject = (p: [number, number], layer: string, h: number): EReject => {
      if (view.isBlocked(p, layer, h + r.wallGapCm)) {
        return EReject.Blocked;
      }
      for (const s of shapes) {
        if (PropsLayoutView.isExcluded([s], p, layer)) {
          switch (s.kind) {
            case 'spawn':
            case 'end':
              return EReject.Spawn;
            case 'ladder_foot':
            case 'ladder_top':
            case 'stair':
              return EReject.Ladder;
            case 'door':
              return EReject.Door;
            case 'route':
            case 'crossing':
              return EReject.Route;
            default:
              return EReject.Other;
          }
        }
      }
      for (const k of input.keepOuts) {
        if (closer(p, k.pos, keepOutRadius(k))) {
          return EReject.Keepout;
        }
      }
      for (let i = 0; i < input.otherObjects.length; i++) {
        if (input.otherObjects[i].attach) continue;
        if (closer(p, input.otherObjects[i].pos, h + otherHalf[i] + r.minGapAnyCm)) {
          return EReject.OtherLayer;
        }
      }
      return EReject.None;
    };

    const inZones = (p: [number, number]): boolean => {
      if (r.zones.length === 0 || r.zones.includes('*')) return true;
      for (const z of r.zones) {
        if (view.isInZone(p, z)) return true;
      }
      return false;
    };

    const anchors = gatherAnchors(view, layerNames, r, undefined, 0);
    const cands: Candidate[] = [];
    const looseEligibleCounts = { eligible: 0 };
    filterAnchors(
      view,
      layerNames,
      inZones,
      staticReject,
      anchors,
      halfOf(r, l.pileVariant),
      result.objects,
      r.minSpacingCm,
      looseEligibleCounts,
      report.rejected,
      cands
    );
    loose.eligible = looseEligibleCounts.eligible;
    for (const c of cands) {
      const d = view.distanceToMandatoryRouteAnyLayerCm(c.pos);
      c.weight = d < l.farMinCm ? l.weightNear : d <= l.farMaxCm ? l.weightBand : l.weightFar;
    }
    let totalWeight = 0n;
    for (const c of cands) {
      totalWeight += BigInt(c.weight);
    }
    const alive = new Array(cands.length).fill(true);
    while (countLeft > 0 && totalWeight > 0n && remaining >= cheapest) {
      const pick = drawWeighted(rng, cands, alive, totalWeight);
      if (pick === -1) break;
      const c = cands[pick];
      const layer = layerNames[c.layer];
      let table = '';
      let value = 0;
      if (l.roofTable.length > 0 && isRoofLayer(view, layer)) {
        if (l.roofTableValue <= remaining) {
          table = l.roofTable;
          value = l.roofTableValue;
        }
      } else {
        const options = affordable(groundTables, remaining);
        if (options.length > 0) {
          const i = options[rng.nextBelow(options.length)];
          table = l.tables[i];
          value = l.tableValues[i];
        }
      }
      if (!table) {
        alive[pick] = false;
        totalWeight -= BigInt(c.weight);
        continue;
      }
      const id = nextId(input.layerId, input.seed, input.reservedIds, usedIds, idCounter);
      result.objects.push({
        id,
        variant: l.pileVariant,
        pos: c.pos,
        walk_layer: layer,
        yaw: 0,
        loot_table: table,
        locked: false,
      });
      remaining -= value;
      countLeft--;
      loose.placed++;
      totalWeight -= dropAround(cands, alive, pick, r.minSpacingCm);
    }
  }

  report.subA = containers;
  report.subB = loose;
  report.eligible = 0;
  report.placed = containers.placed + loose.placed;
  report.requested = report.kept + report.placed;
  report.short_by = 0;
  report.status = 'ok';
  report.budgetSpent = Math.min(keptValue + budgetForNew - remaining, 2147483647);
  if (cheapest !== 2147483647 && remaining >= cheapest) {
    report.bBudgetShort = true;
    report.warnings.push(`budget_left ${remaining}`);
  }
  result.rngCalls = rng.calls;
}

// ---------------- Place Algorithm (Router & Crates/Barrels) ----------------

export function place(input: PropsPlacementInput): PropsPlacementResult {
  const report: PropsPlacementReport = {
    requested: 0,
    placed: 0,
    kept: input.kept.length,
    short_by: 0,
    eligible: 0,
    status: 'ok',
    gate: 'n/a',
    kind: input.rules.kind,
    rejected: {
      blocked: 0,
      coverage: 0,
      door: 0,
      keepout: 0,
      ladder: 0,
      other: 0,
      other_layer: 0,
      route: 0,
      spacing: 0,
      spawn: 0,
    },
    warnings: [],
  };

  const result: PropsPlacementResult = {
    objects: [...input.kept],
    report,
  };

  const r = input.rules;
  if (r.kind === KIND_TURRETS) {
    placeTurrets(input, result);
    return result;
  }
  if (r.kind === KIND_MINES) {
    placeMines(input, result);
    return result;
  }
  if (r.kind === KIND_LOOT) {
    placeLoot(input, result);
    return result;
  }

  const bCrates = r.kind === KIND_CRATES;
  const bBarrels = r.kind === KIND_BARRELS;

  if (!bCrates && !bBarrels) {
    report.warnings.push(`kind_unsupported ${r.kind}`);
    return result;
  }
  if (!input.view) {
    report.warnings.push('no_layout');
    return result;
  }
  if (bCrates && r.variants.length === 0) {
    report.warnings.push('no_variants');
    return result;
  }

  const view = input.view;
  const half = bBarrels
    ? Math.max(halfOf(r, VARIANT_BARREL), halfOf(r, VARIANT_BARREL_EXPLOSIVE))
    : r.variants.reduce((h, v) => Math.max(h, halfOf(r, v.key)), 0);

  // Build exclusions
  const shapes = view.buildExclusions(buildExclusionParams(r.exclude));

  const layerNames: string[] = [];
  for (const l of view.layers) {
    if (matchAnyLayer(r.walkLayers, l.name)) {
      layerNames.push(l.name);
    }
  }

  const otherHalf: number[] = input.otherObjects.map((o) => halfOf(r, o.variant));
  const keepOutRadius = (k: PropsKeepOut) => (k.radiusCm > 0 ? k.radiusCm : r.minSpacingCm);

  const staticReject = (p: [number, number], layer: string, h: number): EReject => {
    if (view.isBlocked(p, layer, h + r.wallGapCm)) {
      return EReject.Blocked;
    }
    for (const s of shapes) {
      if (PropsLayoutView.isExcluded([s], p, layer)) {
        switch (s.kind) {
          case 'spawn':
          case 'end':
            return EReject.Spawn;
          case 'ladder_foot':
          case 'ladder_top':
          case 'stair':
            return EReject.Ladder;
          case 'door':
            return EReject.Door;
          case 'route':
          case 'crossing':
            return EReject.Route;
          default:
            return EReject.Other;
        }
      }
    }
    for (const k of input.keepOuts) {
      if (closer(p, k.pos, keepOutRadius(k))) {
        return EReject.Keepout;
      }
    }
    for (let i = 0; i < input.otherObjects.length; i++) {
      if (input.otherObjects[i].attach) continue;
      if (closer(p, input.otherObjects[i].pos, h + otherHalf[i] + r.minGapAnyCm)) {
        return EReject.OtherLayer;
      }
    }
    return EReject.None;
  };

  const inZones = (p: [number, number]): boolean => {
    if (r.zones.length === 0 || r.zones.includes('*')) return true;
    for (const z of r.zones) {
      if (view.isInZone(p, z)) return true;
    }
    return false;
  };

  const zoneCount = (): number => {
    if (r.zones.length === 0 || r.zones.includes('*')) return view.zones.length;
    let n = 0;
    for (const z of r.zones) {
      if (view.findZone(z)) n++;
    }
    return n;
  };

  const weightAt = (p: [number, number], layer: string, h: number): number => {
    let w = 1n;
    for (const c of r.near) {
      const bNear =
        c.what === 'wall'
          ? view.isNearWall(p, layer, h + c.max_cm)
          : c.what === 'cover'
          ? view.isNearCover(p, layer, h + c.max_cm)
          : false;
      if (bNear) {
        const mult = BigInt(Math.max(1, Math.min(MAX_WEIGHT, c.weight)));
        w = w * mult;
        if (w > BigInt(MAX_WEIGHT)) w = BigInt(MAX_WEIGHT);
      }
    }
    return Number(w);
  };

  // Generate candidates
  const cands: Candidate[] = [];
  const eligibleCounts = { eligible: 0 };
  gatherLattice(
    view,
    layerNames,
    inZones,
    staticReject,
    weightAt,
    half,
    input.kept,
    r.minSpacingCm,
    eligibleCounts,
    report.rejected,
    cands
  );
  report.eligible = eligibleCounts.eligible;
  report.requested = computeRequested(r, report.eligible, zoneCount());
  const target = Math.max(0, report.requested - report.kept);

  let totalWeight = 0n;
  for (const c of cands) {
    totalWeight += BigInt(c.weight);
  }
  const alive = new Array(cands.length).fill(true);
  if (target > 0 && cands.length === 0) {
    report.warnings.push('no_candidates');
  }

  const rng = new PropsRng(streamSeed(input.seed, input.layerId));
  const idCounter = { val: 0 };
  const usedIds = new Set<string>();
  const newObjects: PropObject[] = [];
  const wallClause = r.near.find((c) => c.what === 'wall');

  if (bCrates) {
    const variantTotal = r.variants.reduce((sum, v) => sum + v.weight, 0);
    while (newObjects.length < target && totalWeight > 0n) {
      const pick = drawWeighted(rng, cands, alive, totalWeight);
      if (pick === -1) break;
      const c = cands[pick];
      const walkLayer = layerNames[c.layer];

      const variantRoll = rng.nextBelow(variantTotal);
      let cum = 0;
      let pickedVariant = r.variants[0].key;
      for (const v of r.variants) {
        cum += v.weight;
        if (variantRoll < cum) {
          pickedVariant = v.key;
          break;
        }
      }

      let yaw = 0;
      let axisFound: { alongX: boolean } | null = null;
      if (wallClause) {
        axisFound = view.findNearestWallAxis(c.pos, walkLayer, half + wallClause.max_cm);
      }
      if (axisFound) {
        yaw = axisFound.alongX ? 0 : 90;
      } else {
        yaw = (rng.nextU32() % 4) * 90;
      }

      const id = nextId(input.layerId, input.seed, input.reservedIds, usedIds, idCounter);
      newObjects.push({
        id,
        variant: pickedVariant,
        pos: c.pos,
        walk_layer: walkLayer,
        yaw,
        locked: false,
      });
      totalWeight -= dropAround(cands, alive, pick, r.minSpacingCm);
    }
  } else {
    // Barrels
    const ring: [number, number][] = [];
    const reach = Math.floor(r.ringMaxCm / LATTICE_CM) * LATTICE_CM;
    for (let dy = -reach; dy <= reach; dy += LATTICE_CM) {
      for (let dx = -reach; dx <= reach; dx += LATTICE_CM) {
        const d2 = dx * dx + dy * dy;
        if (d2 >= r.ringMinCm * r.ringMinCm && d2 <= r.ringMaxCm * r.ringMaxCm && d2 > 0) {
          ring.push([dx, dy]);
        }
      }
    }
    const barrelHalf = halfOf(r, VARIANT_BARREL);
    const memberGap = Math.max(r.ringMinCm, 2 * barrelHalf);

    const memberOk = (q: [number, number], layer: string): boolean => {
      if (!view.isWalkable(q, layer) || staticReject(q, layer, barrelHalf) !== EReject.None) {
        return false;
      }
      for (const k of input.kept) {
        if (closer(q, k.pos, memberGap)) return false;
      }
      for (const o of newObjects) {
        if (closer(q, o.pos, memberGap)) return false;
      }
      return true;
    };

    const clusters: number[][] = [];
    while (newObjects.length < target && totalWeight > 0n) {
      const pick = drawWeighted(rng, cands, alive, totalWeight);
      if (pick === -1) break;
      const c = cands[pick];
      const layer = layerNames[c.layer];
      let size = r.clusterMin + rng.nextBelow(r.clusterMax - r.clusterMin + 1);
      size = Math.min(size, target - newObjects.length);
      const members: number[] = [];

      const anchorId = nextId(input.layerId, input.seed, input.reservedIds, usedIds, idCounter);
      const anchor: PropObject = {
        id: anchorId,
        variant: VARIANT_BARREL,
        pos: c.pos,
        walk_layer: layer,
        yaw: 0,
        locked: false,
        [CLUSTER_FIELD]: anchorId,
      };
      members.push(newObjects.length);
      newObjects.push(anchor);

      for (let m = 1; m < size; m++) {
        const valid: [number, number][] = [];
        for (const off of ring) {
          const q: [number, number] = [c.pos[0] + off[0], c.pos[1] + off[1]];
          if (memberOk(q, layer)) {
            valid.push(q);
          }
        }
        if (valid.length === 0) break;
        const qPicked = valid[rng.nextBelow(valid.length)];
        const memId = nextId(input.layerId, input.seed, input.reservedIds, usedIds, idCounter);
        const memObj: PropObject = {
          id: memId,
          variant: VARIANT_BARREL,
          pos: qPicked,
          walk_layer: layer,
          yaw: 0,
          locked: false,
          [CLUSTER_FIELD]: anchorId,
        };
        members.push(newObjects.length);
        newObjects.push(memObj);
      }
      clusters.push(members);
      totalWeight -= dropAround(cands, alive, pick, r.minSpacingCm);
    }

    // Explosive clusters
    const wanted = Math.floor((r.explosiveShareMilli * clusters.length + 500) / 1000);
    if (wanted > 0) {
      const order = Array.from({ length: clusters.length }, (_, i) => i);
      rng.shuffle(order);
      const explosives: [number, number][] = [];
      for (const k of input.kept) {
        if (k.variant === VARIANT_BARREL_EXPLOSIVE) {
          explosives.push(k.pos);
        }
      }
      let chosen = 0;
      for (const clusterIndex of order) {
        if (chosen === wanted) break;
        let bOk = true;
        for (const memberIndex of clusters[clusterIndex]) {
          const p = newObjects[memberIndex].pos;
          bOk =
            bOk &&
            !within(p, view.spawnPos, r.explosiveNeverSpawnCm) &&
            !within(p, view.endPos, r.explosiveNeverEndCm);
          for (const e of explosives) {
            bOk = bOk && !closer(p, e, r.explosiveMinGapCm);
          }
        }
        if (!bOk) continue;
        for (const memberIndex of clusters[clusterIndex]) {
          newObjects[memberIndex].variant = VARIANT_BARREL_EXPLOSIVE;
          explosives.push(newObjects[memberIndex].pos);
        }
        chosen++;
      }
      if (chosen < wanted) {
        report.warnings.push(`explosive_short ${wanted - chosen}`);
      }
    }
  }

  report.placed = newObjects.length;
  result.objects.push(...newObjects);
  report.short_by = Math.max(0, report.requested - report.kept - report.placed);
  report.status = report.short_by > 0 ? 'short' : 'ok';
  result.rngCalls = rng.calls;

  return result;
}

// ---------------- Report to JSON ----------------

export function reportToJson(report: PropsPlacementReport): Record<string, unknown> {
  const rejectedObj: Record<string, unknown> = {
    blocked: report.rejected.blocked,
    door: report.rejected.door,
    keepout: report.rejected.keepout,
    ladder: report.rejected.ladder,
    other: report.rejected.other,
    other_layer: report.rejected.other_layer,
    route: report.rejected.route,
    spacing: report.rejected.spacing,
    spawn: report.rejected.spawn,
  };

  const json: Record<string, unknown> = {
    eligible: report.eligible,
    gate: report.gate,
    kept: report.kept,
    placed: report.placed,
    rejected: rejectedObj,
    requested: report.requested,
    short_by: report.short_by,
    status: report.status,
    warnings: [...report.warnings],
  };

  const sub = (s?: PropSubReport) => {
    if (!s) return { dropped: 0, eligible: 0, forbidden: 0, kept: 0, placed: 0, requested: 0 };
    return {
      dropped: s.dropped,
      eligible: s.eligible,
      forbidden: s.forbidden,
      kept: s.kept,
      placed: s.placed,
      requested: s.requested,
    };
  };

  if (report.kind === KIND_TURRETS) {
    rejectedObj.coverage = report.rejected.coverage ?? 0;
  } else if (report.kind === KIND_MINES) {
    json.doorways = sub(report.subA);
    json.crates = sub(report.subB);
    json.field = sub(report.subC);
  } else if (report.kind === KIND_LOOT) {
    json.containers = sub(report.subA);
    json.loose = sub(report.subB);
    const value = report.budgetValue ?? 0;
    const spent = report.budgetSpent ?? 0;
    json.budget = {
      left: Math.max(0, value - spent),
      spent,
      value,
    };
  }

  if (report.gate_leg !== undefined) {
    json.gate_leg = report.gate_leg;
  }
  if (report.gate_blockers !== undefined) {
    json.gate_blockers = [...report.gate_blockers];
  }
  return json;
}

// ---------------- RerollLayer ----------------

export function rerollLayer(
  file: LevelPropsFile,
  balance: Record<string, unknown>,
  view: PropsLayoutView,
  layerId: string,
  seed: number,
  layout?: GenLayout
): PropsPlacementResult {
  if (file.frozen) {
    throw new Error('props: the file is frozen; re-roll refused');
  }
  const layer = file.layers.find((l) => l.id === layerId);
  if (!layer) {
    throw new Error(`props: no layer '${layerId}'`);
  }
  if (layer.locked) {
    throw new Error(`props: layer '${layerId}' is locked; re-roll refused`);
  }
  if (
    layer.kind !== KIND_CRATES &&
    layer.kind !== KIND_BARRELS &&
    layer.kind !== KIND_TURRETS &&
    layer.kind !== KIND_MINES &&
    layer.kind !== KIND_LOOT
  ) {
    throw new Error(`props: layer '${layerId}' kind '${layer.kind}' cannot be placed`);
  }

  const layerIds = (resolved: { objects: PropsResolvedObject[] }) => {
    const ids = new Set<string>();
    for (const r of resolved.objects) {
      if (r.layerId === layerId) {
        ids.add(r.object.id);
      }
    }
    return ids;
  };
  const idsBefore = layerIds(applyOverrides(file));

  layer.seed = seed >>> 0;
  const input = makeInput(file, balance, view, layerId);
  const result = place(input);

  const stored: PropObject[] = [];
  for (let i = 0; i < result.objects.length; i++) {
    const o = result.objects[i];
    // a kept object, by id (the C4 kinds drop kept objects whose crate / door is gone)
    if (input.kept.some((k) => k.id === o.id)) {
      const record = layer.objects.find((s) => s.id === o.id);
      if (record) {
        stored.push(record);
      }
      continue;
    }
    stored.push(o);
  }

  layer.objects = stored;

  // other layers' objects attached to an object of this layer that the re-roll removed: reported, dropped by their own re-roll / C5
  const after = applyOverrides(file);
  const idsAfter = layerIds(after);
  for (const r of after.objects) {
    if (
      r.layerId !== layerId &&
      r.object.attach &&
      idsBefore.has(r.object.attach) &&
      !idsAfter.has(r.object.attach)
    ) {
      result.report.warnings.push(`attach_orphaned ${r.object.id}`);
    }
  }

  layer.report = reportToJson(result.report);

  if (layout) {
    const gateRes = runFastGate(layout, file, balance, layerId);
    layer.report.gate = gateRes.gate;
    if (gateRes.gate === 'fail') {
      layer.report.gate_leg = gateRes.gate_leg;
      if (gateRes.gate_blockers) {
        layer.report.gate_blockers = [...gateRes.gate_blockers];
      }
    }
  }

  return result;
}

// ---------------- RerollAll & Utilities ----------------

export function rerollAll(
  file: LevelPropsFile,
  balance: Record<string, unknown>,
  view: PropsLayoutView,
  layout?: GenLayout
): boolean {
  let bOk = true;
  for (const kind of [KIND_CRATES, KIND_BARRELS, KIND_TURRETS, KIND_MINES, KIND_LOOT]) {
    const ids: string[] = [];
    for (const l of file.layers) {
      if (l.kind === kind && l.enabled && !l.locked) {
        ids.push(l.id);
      }
    }
    for (const id of ids) {
      const l = file.layers.find((x) => x.id === id);
      if (l) {
        try {
          rerollLayer(file, balance, view, id, l.seed, layout);
        } catch {
          bOk = false;
        }
      }
    }
  }
  return bOk;
}

export function resolveCrateColors(
  file: LevelPropsFile,
  balance: Record<string, unknown>
): Map<string, string> {
  const colors = (balance.crate_colors ?? {}) as Record<string, string>;
  const safe = colors.safe ?? 'blue';
  const dangerous = colors.dangerous ?? 'red';

  const kindOf = (layerId: string) => {
    const l = file.layers.find((x) => x.id === layerId);
    return l ? l.kind : '';
  };

  const resolved = applyOverrides(file);
  const mined = new Set<string>();
  for (const r of resolved.objects) {
    if (kindOf(r.layerId) === KIND_MINES && r.object.attach) {
      mined.add(r.object.attach);
    }
  }

  const out = new Map<string, string>();
  for (const r of resolved.objects) {
    if (kindOf(r.layerId) === KIND_CRATES) {
      out.set(r.object.id, mined.has(r.object.id) ? dangerous : safe);
    }
  }
  return out;
}
