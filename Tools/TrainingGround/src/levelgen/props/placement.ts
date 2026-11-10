// Training Ground / level generator: pure deterministic props placement (crates and barrels).
// Docs: docs/design/levelgen/PROPS_LAYERS.md sections 0, 2.2, 2.3, 2.5, 7.
// C++ reference: Source/CodexTactics/Private/LevelGen/PropsPlacementRules.cpp

import type { LevelPropsFile, PropLayer, PropObject } from './schema.ts';
import { PropsLayoutView } from './layoutView.ts';
import { PropsRng, streamSeed } from './rng.ts';

export const LATTICE_CM = 50;
export const MAX_WEIGHT = 1000;
export const KIND_CRATES = 'crates';
export const KIND_BARRELS = 'barrels';
export const VARIANT_BARREL = 'barrel';
export const VARIANT_BARREL_EXPLOSIVE = 'barrel_explosive';
export const CLUSTER_FIELD = 'cluster';

export interface PropsNearClause {
  what: 'wall' | 'cover' | string;
  max_cm: number;
  weight: number;
}

export interface PropsRules {
  kind: string;
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
  clusterMin: number;
  clusterMax: number;
  ringMinCm: number;
  ringMaxCm: number;
  explosiveShareMilli: number;
  explosiveMinGapCm: number;
  explosiveNeverSpawnCm: number;
  explosiveNeverEndCm: number;
  halfSizeCm: Map<string, number>;
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
}

export interface PropsPlacementReport {
  requested: number;
  placed: number;
  kept: number;
  short_by: number;
  eligible: number;
  status: 'ok' | 'short';
  gate: string;
  rejected: PropsRejectCounts;
  warnings: string[];
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

function halfOf(rules: PropsRules, variant: string): number {
  return rules.halfSizeCm.get(variant) ?? 0;
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
              const posArr = Array.isArray(op.pos) && op.pos.length === 2
                ? [Math.floor(Number(op.pos[0])), Math.floor(Number(op.pos[1]))] as [number, number]
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

// ---------------- BuildRules ----------------

export function buildRules(
  balance: Record<string, unknown>,
  layer: PropLayer
): PropsRules {
  const presets = (balance.presets ?? {}) as Record<string, Record<string, unknown>>;
  const presetName = layer.preset || `${layer.kind}_default`;
  const preset = presets[presetName];

  let merged = mergeObjects(preset, layer.rules as unknown as Record<string, unknown>);

  // exclude.$preset merge
  const ex = merged.exclude as Record<string, unknown> | undefined;
  if (ex && typeof ex.$preset === 'string') {
    const exPresetName = ex.$preset;
    const baseExclude = presets[exPresetName]?.exclude as Record<string, unknown> | undefined;
    if (baseExclude) {
      const mergedExclude = mergeObjects(baseExclude, ex);
      delete mergedExclude.$preset;
      merged.exclude = mergedExclude;
    }
  }

  const countObj = (merged.count ?? {}) as Record<string, unknown>;
  const whereObj = (merged.where ?? {}) as Record<string, unknown>;
  const excludeObj = (merged.exclude ?? {}) as Record<string, unknown>;
  const clusterObj = (merged.cluster ?? {}) as Record<string, unknown>;
  const explosiveObj = (merged.explosive ?? {}) as Record<string, unknown>;

  const nearList: PropsNearClause[] = [];
  if (Array.isArray(whereObj.near)) {
    for (const n of whereObj.near) {
      if (n && typeof n === 'object') {
        const what = String(n.what ?? '');
        if (what === 'wall' || what === 'cover') {
          nearList.push({
            what,
            max_cm: typeof n.max_cm === 'number' ? Math.max(0, Math.floor(n.max_cm)) : 0,
            weight: typeof n.weight === 'number' ? Math.max(1, Math.min(MAX_WEIGHT, Math.floor(n.weight))) : 1,
          });
        }
      }
    }
  }

  const variantsList: Array<{ key: string; weight: number }> = [];
  const varObj = (merged.variants ?? {}) as Record<string, unknown>;
  for (const k of Object.keys(varObj).sort()) {
    const w = varObj[k];
    if (typeof w === 'number' && w > 0) {
      variantsList.push({ key: k, weight: Math.floor(w) });
    }
  }

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

  const minGapAnyCm = typeof merged.min_gap_any_cm === 'number' ? Math.max(0, Math.floor(merged.min_gap_any_cm)) : 0;
  const wallGapCm = typeof merged.wall_gap_cm === 'number' ? Math.max(0, Math.floor(merged.wall_gap_cm)) : minGapAnyCm;

  return {
    kind: layer.kind,
    countMode: String(countObj.mode ?? 'count'),
    countValue: typeof countObj.value === 'number' ? Math.max(0, Math.floor(countObj.value)) : 0,
    densityMilli: typeof countObj.per_100m2 === 'number' ? Math.round(countObj.per_100m2 * 1000) : 0,
    countMax: typeof countObj.max === 'number' ? Math.max(0, Math.floor(countObj.max)) : 2147483647,
    budgetCap,
    zones: Array.isArray(whereObj.zones) ? whereObj.zones.map(String) : [],
    walkLayers: Array.isArray(whereObj.walk_layers) ? whereObj.walk_layers.map(String) : ['ground'],
    near: nearList,
    variants: variantsList,
    minSpacingCm: typeof merged.min_spacing_cm === 'number' ? Math.max(0, Math.floor(merged.min_spacing_cm)) : 0,
    minGapAnyCm,
    wallGapCm,
    exclude: excludeObj,
    clusterMin,
    clusterMax,
    ringMinCm,
    ringMaxCm,
    explosiveShareMilli,
    explosiveMinGapCm: typeof explosiveObj.min_gap_cm === 'number' ? Math.max(0, Math.floor(explosiveObj.min_gap_cm)) : 0,
    explosiveNeverSpawnCm: typeof neverObj.spawn === 'number' ? Math.max(0, Math.floor(neverObj.spawn)) : 0,
    explosiveNeverEndCm: typeof neverObj.end === 'number' ? Math.max(0, Math.floor(neverObj.end)) : 0,
    halfSizeCm,
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
  const reservedIds: string[] = [];

  for (const r of resolved.objects) {
    if (r.layerId === layerId) {
      if (r.object.locked || r.bPinned || r.bHand) {
        kept.push(r.object);
        reservedIds.push(r.object.id);
      }
    } else {
      otherObjects.push(r.object);
      reservedIds.push(r.object.id);
    }
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
    reservedIds,
  };
}

// ---------------- Place Algorithm ----------------

const EReject = {
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
type EReject = (typeof EReject)[keyof typeof EReject];

interface Candidate {
  pos: [number, number];
  layer: number;
  weight: number;
}

function countReject(c: PropsRejectCounts, r: EReject): void {
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

export function place(input: PropsPlacementInput): PropsPlacementResult {
  const report: PropsPlacementReport = {
    requested: 0,
    placed: 0,
    kept: input.kept.length,
    short_by: 0,
    eligible: 0,
    status: 'ok',
    gate: 'n/a',
    rejected: {
      blocked: 0,
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
  const ex = r.exclude;
  const shapes = view.buildExclusions({
    spawnCm: typeof ex.spawn_cm === 'number' ? Math.floor(ex.spawn_cm) : 0,
    endCm: typeof ex.end_cm === 'number' ? Math.floor(ex.end_cm) : 0,
    ladderCm: typeof ex.ladder_cm === 'number' ? Math.floor(ex.ladder_cm) : 0,
    stairCm: typeof ex.stair_cm === 'number' ? Math.floor(ex.stair_cm) : 0,
    doorCm: typeof ex.door_cm === 'number' ? Math.floor(ex.door_cm) : 0,
    routeMandatoryCm:
      typeof ex.route_cm === 'number'
        ? Math.floor(ex.route_cm)
        : typeof (ex.route_cm as Record<string, unknown>)?.mandatory === 'number'
        ? Math.floor((ex.route_cm as Record<string, unknown>).mandatory as number)
        : 0,
    routeVariantsCm:
      typeof ex.route_cm === 'number'
        ? Math.floor(ex.route_cm)
        : typeof (ex.route_cm as Record<string, unknown>)?.variants === 'number'
        ? Math.floor((ex.route_cm as Record<string, unknown>).variants as number)
        : 0,
    crossing: Boolean(ex.crossing),
    holeRimCm: typeof ex.hole_rim_cm === 'number' ? Math.floor(ex.hole_rim_cm) : 0,
  });

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
        report.eligible++;
        let reject = staticReject(p, layer, half);
        if (reject === EReject.None) {
          for (const k of input.kept) {
            if (closer(p, k.pos, r.minSpacingCm)) {
              reject = EReject.Spacing;
              break;
            }
          }
        }
        if (reject !== EReject.None) {
          countReject(report.rejected, reject);
          continue;
        }
        cands.push({
          pos: p,
          layer: layerIndex,
          weight: weightAt(p, layer, half),
        });
      }
    }
  }

  // Stable sort candidates by Y, X, layer name ordinal
  cands.sort((a, b) => {
    if (a.pos[1] !== b.pos[1]) return a.pos[1] - b.pos[1];
    if (a.pos[0] !== b.pos[0]) return a.pos[0] - b.pos[0];
    const nameA = layerNames[a.layer];
    const nameB = layerNames[b.layer];
    return nameA < nameB ? -1 : nameA > nameB ? 1 : 0;
  });

  // Calculate target
  let total = 0;
  if (r.countMode === 'density') {
    total = Math.floor((r.densityMilli * report.eligible + 200000) / 400000);
  } else if (r.countMode === 'per_zone') {
    total = r.countValue * zoneCount();
  } else {
    total = r.countValue;
  }
  total = Math.max(0, Math.min(total, r.countMax, r.budgetCap));
  report.requested = total;
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
  return {
    eligible: report.eligible,
    gate: report.gate,
    kept: report.kept,
    placed: report.placed,
    rejected: {
      blocked: report.rejected.blocked,
      door: report.rejected.door,
      keepout: report.rejected.keepout,
      ladder: report.rejected.ladder,
      other: report.rejected.other,
      other_layer: report.rejected.other_layer,
      route: report.rejected.route,
      spacing: report.rejected.spacing,
      spawn: report.rejected.spawn,
    },
    requested: report.requested,
    short_by: report.short_by,
    status: report.status,
    warnings: [...report.warnings],
  };
}

// ---------------- RerollLayer ----------------

export function rerollLayer(
  file: LevelPropsFile,
  balance: Record<string, unknown>,
  view: PropsLayoutView,
  layerId: string,
  seed: number
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
  if (layer.kind !== KIND_CRATES && layer.kind !== KIND_BARRELS) {
    throw new Error(`props: layer '${layerId}' kind '${layer.kind}' is placed by C4 (not yet)`);
  }

  layer.seed = seed >>> 0;
  const input = makeInput(file, balance, view, layerId);
  const result = place(input);

  const stored: PropObject[] = [];
  for (let i = 0; i < result.objects.length; i++) {
    const o = result.objects[i];
    if (i < input.kept.length) {
      const record = layer.objects.find((s) => s.id === o.id);
      if (record) {
        stored.push(record);
      }
      continue;
    }
    stored.push(o);
  }

  layer.objects = stored;
  layer.report = reportToJson(result.report);

  return result;
}
