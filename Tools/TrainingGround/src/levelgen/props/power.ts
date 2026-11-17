// Training Ground / level generator: the POWER layer of a generated map - diesel generators linked by cable to the map's turrets,
// their start states and the jerrycans in the loot (docs/design/TURRET_POWER.md phase 1, designer decisions 2026-11-17 section 15).
// Config: Content/Data/Balance/props_balance.json presets.power_default (all tunable). Deterministic (PropsRng seeded by the layer seed).
// The game reads only the resulting objects: a generator's "start" / "fuel_l" / "owner" / "links", a turret's "generator"
// (UCodexLevelPropsSubsystem links them; APowerGeneratorActor runs the network). The link checks (validatePowerLinks) mirror the C++
// FPropsValidator::ValidatePowerLinks.

import type { PropsLayoutView } from './layoutView.ts';
import type { PropsResolvedObject } from './placement.ts';
import { applyOverrides } from './placement.ts';
import { PropsRng, fnv1a, streamSeed } from './rng.ts';
import type { LevelPropsFile, PropLayer, PropObject } from './schema.ts';
import { doorwayCorridorBlocking, ladderOrStairBlocking, variantHalfSize } from './validator.ts';

export const POWER_LAYER_ID = 'power';
export const POWER_PRESET = 'power_default';

/** Generator start states (C++ EGeneratorStart / PowerRules::ParseStart), in the quota order. */
export const GENERATOR_STARTS = ['running_hostile', 'running_neutral', 'fuelled_off', 'dry', 'broken'] as const;
export type GeneratorStart = (typeof GENERATOR_STARTS)[number];

export interface PowerConfig {
  variant: string;
  maxGenerators: number;
  minSpacingCm: number;
  minGapAnyCm: number;
  spawnCm: number;
  ringMinCm: number;
  ringMaxCm: number;
  ringStepCm: number;
  angleStepDeg: number;
  /** [network size, draw weight], ascending size. */
  sizeWeights: [number, number][];
  cableMinCm: number;
  cableMaxCm: number;
  groupReachCm: number;
  startShares: Record<GeneratorStart, number>;
  residualL: [number, number];
  runningTankFrac: [number, number];
  brokenWithFuelShare: number;
  tankL: number;
  noHostileWithinSpawnCm: number;
  jerrycansPerGenerator: number;
  jerrycansMin: number;
  fuelTable: string;
  farFromRouteCm: number;
}

export const DEFAULT_POWER_CONFIG: PowerConfig = {
  variant: 'generator_diesel',
  maxGenerators: 10,
  minSpacingCm: 1500,
  minGapAnyCm: 100,
  spawnCm: 4000,
  ringMinCm: 800,
  ringMaxCm: 2400,
  ringStepCm: 200,
  angleStepDeg: 15,
  sizeWeights: [
    [1, 3],
    [2, 4],
    [3, 2],
    [4, 1],
  ],
  cableMinCm: 800,
  cableMaxCm: 4000,
  groupReachCm: 4000,
  startShares: { running_hostile: 0.15, running_neutral: 0.1, fuelled_off: 0.15, dry: 0.4, broken: 0.2 },
  residualL: [3, 8],
  runningTankFrac: [0.5, 1.0],
  brokenWithFuelShare: 0.5,
  tankL: 20,
  noHostileWithinSpawnCm: 6000,
  jerrycansPerGenerator: 0.4,
  jerrycansMin: 1,
  fuelTable: 'fuel_rare',
  farFromRouteCm: 1400,
};

function obj(v: unknown): Record<string, unknown> | undefined {
  return v && typeof v === 'object' && !Array.isArray(v) ? (v as Record<string, unknown>) : undefined;
}

function num(v: unknown, fallback: number): number {
  return typeof v === 'number' && Number.isFinite(v) ? v : fallback;
}

function pair(v: unknown, fallback: [number, number]): [number, number] {
  return Array.isArray(v) && v.length >= 2 && typeof v[0] === 'number' && typeof v[1] === 'number' ? [v[0], v[1]] : fallback;
}

/** presets.power_default of props_balance.json over the defaults. */
export function readPowerConfig(balance?: Record<string, unknown>): PowerConfig {
  const d = DEFAULT_POWER_CONFIG;
  const preset = obj(obj(balance?.presets)?.[POWER_PRESET]);
  if (!preset) return { ...d, sizeWeights: d.sizeWeights.map((p) => [p[0], p[1]] as [number, number]), startShares: { ...d.startShares } };
  const g = obj(preset.generators) ?? {};
  const l = obj(preset.links) ?? {};
  const s = obj(preset.start_state) ?? {};
  const f = obj(preset.fuel) ?? {};
  const ring = pair(g.ring_cm, [d.ringMinCm, d.ringMaxCm]);
  const cable = pair(l.cable_cm, [d.cableMinCm, d.cableMaxCm]);
  let sizeWeights = d.sizeWeights;
  const tpg = obj(l.turrets_per_generator);
  if (tpg) {
    const parsed: [number, number][] = [];
    for (const [k, w] of Object.entries(tpg)) {
      const size = Number.parseInt(k, 10);
      if (Number.isFinite(size) && size >= 1 && typeof w === 'number' && w > 0) parsed.push([size, Math.round(w)]);
    }
    if (parsed.length > 0) sizeWeights = parsed.sort((a, b) => a[0] - b[0]);
  }
  const shares = { ...d.startShares };
  for (const k of GENERATOR_STARTS) shares[k] = Math.max(0, num(s[k], d.startShares[k]));
  return {
    variant: typeof g.variant === 'string' ? g.variant : d.variant,
    maxGenerators: Math.max(0, Math.floor(num(g.max_count, d.maxGenerators))),
    minSpacingCm: num(g.min_spacing_cm, d.minSpacingCm),
    minGapAnyCm: num(g.min_gap_any_cm, d.minGapAnyCm),
    spawnCm: num(g.spawn_cm, d.spawnCm),
    ringMinCm: ring[0],
    ringMaxCm: ring[1],
    ringStepCm: Math.max(1, num(g.ring_step_cm, d.ringStepCm)),
    angleStepDeg: Math.max(1, num(g.angle_step_deg, d.angleStepDeg)),
    sizeWeights,
    cableMinCm: cable[0],
    cableMaxCm: cable[1],
    groupReachCm: num(l.group_reach_cm, d.groupReachCm),
    startShares: shares,
    residualL: pair(s.residual_l, d.residualL),
    runningTankFrac: pair(s.running_tank_frac, d.runningTankFrac),
    brokenWithFuelShare: num(s.broken_with_fuel_share, d.brokenWithFuelShare),
    tankL: num(s.tank_l, d.tankL),
    noHostileWithinSpawnCm: num(s.no_hostile_within_spawn_cm, d.noHostileWithinSpawnCm),
    jerrycansPerGenerator: num(f.jerrycans_per_generator, d.jerrycansPerGenerator),
    jerrycansMin: Math.max(0, Math.floor(num(f.min, d.jerrycansMin))),
    fuelTable: typeof f.table === 'string' ? f.table : d.fuelTable,
    farFromRouteCm: num(f.far_from_route_cm, d.farFromRouteCm),
  };
}

/** Largest-remainder counts of the start states for `generators` networks (ties: GENERATOR_STARTS order). Sums to `generators`. */
export function startQuotas(cfg: PowerConfig, generators: number): Record<GeneratorStart, number> {
  const out = { running_hostile: 0, running_neutral: 0, fuelled_off: 0, dry: 0, broken: 0 } as Record<GeneratorStart, number>;
  if (generators <= 0) return out;
  const total = GENERATOR_STARTS.reduce((sum, k) => sum + cfg.startShares[k], 0);
  if (total <= 0) {
    out.dry = generators;
    return out;
  }
  const exact = GENERATOR_STARTS.map((k) => (cfg.startShares[k] / total) * generators);
  let given = 0;
  GENERATOR_STARTS.forEach((k, i) => {
    out[k] = Math.floor(exact[i] + 1e-9);
    given += out[k];
  });
  const order = GENERATOR_STARTS.map((k, i) => ({ k, rem: exact[i] - out[k], i })).sort((a, b) => b.rem - a.rem || a.i - b.i);
  for (let n = 0; given < generators; n++, given++) out[order[n % order.length].k]++;
  return out;
}

/** Weighted network size draw (one nextBelow). */
export function drawNetworkSize(cfg: PowerConfig, rng: PropsRng): number {
  const total = cfg.sizeWeights.reduce((sum, p) => sum + p[1], 0);
  if (total <= 0) return 1;
  let pick = rng.nextBelow(total);
  for (const [size, w] of cfg.sizeWeights) {
    if (pick < w) return size;
    pick -= w;
  }
  return cfg.sizeWeights[cfg.sizeWeights.length - 1][0];
}

function dist(a: [number, number], b: [number, number]): number {
  return Math.hypot(a[0] - b[0], a[1] - b[1]);
}

function byHash(a: PropObject, b: PropObject): number {
  const ha = fnv1a(a.id);
  const hb = fnv1a(b.id);
  if (ha !== hb) return ha < hb ? -1 : 1;
  return a.id < b.id ? -1 : a.id > b.id ? 1 : 0;
}

export interface PowerNetwork {
  generator: PropObject;
  turretIds: string[];
}

export interface PowerBuildResult {
  layer: PropLayer;
  networks: PowerNetwork[];
  /** Turrets no generator could reach (dark for good; a validator warning). */
  unlinked: string[];
  /** Loot piles that now hold a jerrycan (table fuel_rare). */
  fuelPileIds: string[];
  /** Jerrycans wanted but no loose pile was left for them. */
  fuelShortfall: number;
}

/**
 * Builds the power layer of `file` in place: replaces any existing 'power' layer, links every turret it can (sets the turret's
 * "generator", "powered", "faction"), and turns some loose loot piles into jerrycans. Deterministic for (file, view, balance, seed).
 */
export function buildPowerLayer(
  file: LevelPropsFile,
  view: PropsLayoutView,
  balance: Record<string, unknown>,
  seed: number,
): PowerBuildResult {
  const cfg = readPowerConfig(balance);
  file.layers = file.layers.filter((l) => l.kind !== POWER_LAYER_ID);
  const kindOf = new Map(file.layers.map((l) => [l.id, l.kind as string]));
  const resolved = applyOverrides(file).objects;
  const turrets = resolved.filter((r) => kindOf.get(r.layerId) === 'turrets').map((r) => r.object);
  const others = resolved.map((r) => r.object);
  const half = variantHalfSize(cfg.variant, balance);
  const rng = new PropsRng(streamSeed(seed, POWER_LAYER_ID));

  const order = [...turrets].sort(byHash);
  const assigned = new Set<string>();
  const unlinked: string[] = [];
  const placed: { pos: [number, number]; members: PropObject[] }[] = [];
  const angleCount = Math.max(1, Math.floor(360 / cfg.angleStepDeg));

  const spotOk = (p: [number, number], members: PropObject[]): boolean => {
    const probe: PropObject = { id: '_power_probe', variant: cfg.variant, pos: p, walk_layer: 'ground' };
    if (!view.isFree(p, 'ground', half)) return false;
    if (doorwayCorridorBlocking(view, probe, half) || ladderOrStairBlocking(view, probe, half)) return false;
    if (dist(p, view.spawnPos) < cfg.spawnCm) return false;
    for (const m of members) {
      const d = dist(p, m.pos);
      if (d < cfg.cableMinCm || d > cfg.cableMaxCm) return false;
    }
    for (const o of others) {
      if (dist(p, o.pos) < half + variantHalfSize(o.variant, balance) + cfg.minGapAnyCm) return false;
    }
    for (const g of placed) {
      if (dist(p, g.pos) < cfg.minSpacingCm) return false;
    }
    return true;
  };

  const findSpot = (members: PropObject[]): [number, number] | null => {
    const cx = Math.floor(members.reduce((s, m) => s + m.pos[0], 0) / members.length);
    const cy = Math.floor(members.reduce((s, m) => s + m.pos[1], 0) / members.length);
    const a0 = rng.nextBelow(angleCount);
    for (let r = cfg.ringMinCm; r <= cfg.ringMaxCm; r += cfg.ringStepCm) {
      for (let k = 0; k < angleCount; k++) {
        const deg = ((a0 + k) % angleCount) * cfg.angleStepDeg;
        const rad = (deg * Math.PI) / 180;
        const p: [number, number] = [Math.round(cx + r * Math.cos(rad)), Math.round(cy + r * Math.sin(rad))];
        if (spotOk(p, members)) return p;
      }
    }
    return null;
  };

  for (const t of order) {
    if (assigned.has(t.id)) continue;
    if (placed.length >= cfg.maxGenerators) {
      assigned.add(t.id);
      unlinked.push(t.id);
      continue;
    }
    const size = drawNetworkSize(cfg, rng);
    const near = order
      .filter((o) => o.id !== t.id && !assigned.has(o.id) && dist(o.pos, t.pos) <= cfg.groupReachCm)
      .sort((a, b) => dist(a.pos, t.pos) - dist(b.pos, t.pos) || (a.id < b.id ? -1 : a.id > b.id ? 1 : 0));
    const members = [t, ...near.slice(0, Math.max(0, size - 1))];
    let spot = findSpot(members);
    while (!spot && members.length > 1) {
      members.pop();
      spot = findSpot(members);
    }
    if (!spot) {
      assigned.add(t.id);
      unlinked.push(t.id);
      continue;
    }
    for (const m of members) assigned.add(m.id);
    placed.push({ pos: spot, members });
  }

  // Start states: quota counts, shuffled over the networks; a running hostile network keeps away from the spawn (swap).
  const quotas = startQuotas(cfg, placed.length);
  const states: GeneratorStart[] = [];
  for (const k of GENERATOR_STARTS) for (let i = 0; i < quotas[k]; i++) states.push(k);
  rng.shuffle(states);
  const nearSpawn = (n: { pos: [number, number]; members: PropObject[] }): boolean =>
    dist(n.pos, view.spawnPos) < cfg.noHostileWithinSpawnCm || n.members.some((m) => dist(m.pos, view.spawnPos) < cfg.noHostileWithinSpawnCm);
  for (let i = 0; i < states.length; i++) {
    if (states[i] !== 'running_hostile' || !nearSpawn(placed[i])) continue;
    const j = states.findIndex((s, idx) => s !== 'running_hostile' && !nearSpawn(placed[idx]));
    if (j >= 0) {
      states[i] = states[j];
      states[j] = 'running_hostile';
    } else {
      states[i] = 'dry'; // nowhere fair for it: one dark network more
    }
  }

  const range = (lo: number, hi: number) => lo + rng.nextBelow(Math.max(0, Math.floor(hi) - Math.floor(lo)) + 1);
  const seedHex = (seed >>> 0).toString(16).padStart(8, '0');
  const turretById = new Map<string, PropObject>();
  for (const l of file.layers) if (l.kind === 'turrets') for (const o of l.objects) turretById.set(o.id, o);
  const networks: PowerNetwork[] = [];
  placed.forEach((n, i) => {
    const start = states[i] ?? 'dry';
    let fuel = 0;
    if (start === 'running_hostile' || start === 'running_neutral') {
      const milli = Math.round(cfg.runningTankFrac[0] * 1000) + rng.nextBelow(Math.max(0, Math.round((cfg.runningTankFrac[1] - cfg.runningTankFrac[0]) * 1000)) + 1);
      fuel = Math.max(1, Math.round((cfg.tankL * milli) / 1000));
    } else if (start === 'fuelled_off') {
      fuel = range(cfg.residualL[0], cfg.residualL[1]);
    } else if (start === 'broken') {
      fuel = rng.nextBelow(1000) < Math.round(cfg.brokenWithFuelShare * 1000) ? range(cfg.residualL[0], cfg.residualL[1]) : 0;
    }
    const ids = [...n.members].map((m) => m.id).sort();
    const cx = n.members.reduce((s, m) => s + m.pos[0], 0) / n.members.length;
    const cy = n.members.reduce((s, m) => s + m.pos[1], 0) / n.members.length;
    const yaw = ((Math.round((Math.atan2(cy - n.pos[1], cx - n.pos[0]) * 180) / Math.PI) % 360) + 360) % 360;
    const generator: PropObject = {
      id: `${POWER_LAYER_ID}_${seedHex}_${i + 1}`,
      variant: cfg.variant,
      pos: n.pos,
      walk_layer: 'ground',
      yaw,
      start,
      fuel_l: fuel,
      links: ids,
      cable_cm: ids.map((id) => Math.round(dist(n.pos, turretById.get(id)?.pos ?? n.pos))),
    };
    if (start === 'running_hostile') generator.owner = 'hostile';
    const running = start === 'running_hostile' || start === 'running_neutral';
    for (const id of ids) {
      const t = turretById.get(id);
      if (!t) continue;
      t.generator = generator.id;
      t.powered = running;
      t.faction = start === 'running_hostile' ? 'hostile' : 'neutral';
    }
    networks.push({ generator, turretIds: ids });
  });
  // A turret left without a generator is dark for good: no power line (its own rule would power a share of them).
  for (const id of unlinked) {
    const t = turretById.get(id);
    if (t) {
      delete t.generator;
      t.powered = false;
      t.faction = 'neutral';
    }
  }

  // Jerrycans: loose loot piles (far from the route first) get the fuel table.
  let fuelPileIds: string[] = [];
  let fuelShortfall = 0;
  if (networks.length > 0) {
    const want = Math.max(cfg.jerrycansMin, Math.round(cfg.jerrycansPerGenerator * networks.length));
    const piles: PropObject[] = [];
    for (const l of file.layers) {
      if (l.kind !== 'loot') continue;
      for (const o of l.objects) if (!o.attach && o.variant.startsWith('loot_pile')) piles.push(o);
    }
    const far = (o: PropObject) => view.distanceToMandatoryRouteCm(o.pos, o.walk_layer ?? 'ground') >= cfg.farFromRouteCm;
    piles.sort((a, b) => (far(a) === far(b) ? byHash(a, b) : far(a) ? -1 : 1));
    const chosen = piles.slice(0, want);
    for (const o of chosen) o.loot_table = cfg.fuelTable;
    fuelPileIds = chosen.map((o) => o.id);
    fuelShortfall = want - chosen.length;
  }

  const layer: PropLayer = {
    id: POWER_LAYER_ID,
    kind: POWER_LAYER_ID as PropLayer['kind'],
    enabled: true,
    seed,
    locked: false,
    preset: POWER_PRESET,
    rules: {},
    objects: networks.map((n) => n.generator),
  };
  file.layers.push(layer);
  return { layer, networks, unlinked, fuelPileIds, fuelShortfall };
}

export interface PowerLinkIssue {
  objectId: string;
  reason: string;
}

/**
 * The power-network checks of the props validator (warnings: nothing is dropped; a broken link leaves the turret dark). Mirrors
 * FPropsValidator::ValidatePowerLinks. `spawnPos` (optional) adds the fairness rule "no running hostile network near the spawn".
 */
export function validatePowerLinks(
  objects: PropsResolvedObject[],
  kindOfLayer: Map<string, string>,
  balance?: Record<string, unknown>,
  spawnPos?: [number, number],
): PowerLinkIssue[] {
  const cfg = readPowerConfig(balance);
  const maxTurrets = cfg.sizeWeights.reduce((m, p) => Math.max(m, p[0]), 1);
  const issues: PowerLinkIssue[] = [];
  const byId = new Map(objects.map((r) => [r.object.id, r]));
  const isTurret = (r?: PropsResolvedObject) => !!r && (kindOfLayer.get(r.layerId) === 'turrets' || r.object.variant.startsWith('turret'));
  const generators = objects.filter((r) => kindOfLayer.get(r.layerId) === POWER_LAYER_ID || r.object.variant.startsWith('generator'));
  const linkedBy = new Map<string, string>();
  const sorted = [...generators].sort((a, b) => (a.object.id < b.object.id ? -1 : a.object.id > b.object.id ? 1 : 0));
  for (const g of sorted) {
    const o = g.object;
    if (o.start !== undefined && (typeof o.start !== 'string' || !(GENERATOR_STARTS as readonly string[]).includes(o.start))) {
      issues.push({ objectId: o.id, reason: `unknown generator start '${String(o.start)}'` });
    }
    const links = Array.isArray(o.links) ? (o.links as unknown[]).filter((x): x is string => typeof x === 'string') : [];
    if (links.length > maxTurrets) issues.push({ objectId: o.id, reason: `feeds ${links.length} turrets (max ${maxTurrets})` });
    for (const id of links) {
      const t = byId.get(id);
      if (!isTurret(t)) {
        issues.push({ objectId: o.id, reason: `link '${id}' is not a turret of the map` });
        continue;
      }
      const d = Math.round(dist(o.pos, t!.object.pos));
      if (d > cfg.cableMaxCm) issues.push({ objectId: o.id, reason: `cable to '${id}' is ${d} cm (max ${cfg.cableMaxCm})` });
      const prev = linkedBy.get(id);
      if (prev) issues.push({ objectId: id, reason: `turret linked by '${prev}' and '${o.id}'` });
      else linkedBy.set(id, o.id);
    }
    if (spawnPos && o.start === 'running_hostile') {
      const near = dist(o.pos, spawnPos) < cfg.noHostileWithinSpawnCm || links.some((id) => {
        const t = byId.get(id);
        return !!t && dist(t.object.pos, spawnPos) < cfg.noHostileWithinSpawnCm;
      });
      if (near) issues.push({ objectId: o.id, reason: `running hostile network within ${cfg.noHostileWithinSpawnCm} cm of the spawn` });
    }
  }
  if (generators.length > 0) {
    const turretsSorted = objects.filter((r) => isTurret(r)).sort((a, b) => (a.object.id < b.object.id ? -1 : a.object.id > b.object.id ? 1 : 0));
    for (const t of turretsSorted) {
      const g = t.object.generator;
      if (typeof g === 'string' && g.length > 0) {
        if (linkedBy.get(t.object.id) !== g) issues.push({ objectId: t.object.id, reason: `generator '${g}' does not list this turret` });
      } else if (!linkedBy.has(t.object.id)) {
        issues.push({ objectId: t.object.id, reason: 'no power line (no generator feeds it: dark for good)' });
      }
    }
  }
  return issues;
}
