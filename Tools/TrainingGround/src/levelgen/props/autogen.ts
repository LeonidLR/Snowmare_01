// Training Ground / level generator: deterministic automatic props generation for generated and expanded maps.
// Task: 076 (generated-map-props). Docs: docs/design/levelgen/PROPS_LAYERS.md.
// Constants and density scaling: Content/Data/LevelGen/props_autogen.json.

import * as fs from 'node:fs';
import * as path from 'node:path';
import type { GenLayout } from '../schema.ts';
import type { LevelPropsFile, PropLayer, PropObject } from './schema.ts';
import { PROPS_SCHEMA, validateProps } from './schema.ts';
import { PropsLayoutView } from './layoutView.ts';
import { applyOverrides, rerollAll } from './placement.ts';
import { runFastGate, type FastGateResult } from './fastGate.ts';
import { validateStatic, type PropsValidationIssue } from './validator.ts';
import { buildPowerLayer, validatePowerLinks, type PowerBuildResult, type PowerLinkIssue } from './power.ts';
import { mapNameFor } from '../node/buildPipeline.ts';

export interface PropsAutogenLayerConfig {
  preset: string;
  count_mode?: string;
  per_100m2?: number;
  per_10000m2?: number;
  base_max?: number;
  min_count?: number;
  max_count?: number;
  min_field_count?: number;
  max_field_count?: number;
  field_per_10000m2?: number;
  doorway_percent?: number;
  crate_percent?: number;
  budget_per_10000m2?: number;
  min_budget?: number;
  max_budget?: number;
  where?: Record<string, unknown>;
}

export interface PropsAutogenConfig {
  schema?: string;
  unit_area_m2?: number;
  reference?: {
    map: string;
    layout: string;
    area_m2: number;
  };
  layers?: {
    crates?: PropsAutogenLayerConfig;
    barrels?: PropsAutogenLayerConfig;
    turrets?: PropsAutogenLayerConfig;
    mines?: PropsAutogenLayerConfig;
    loot?: PropsAutogenLayerConfig;
    /** TURRET_POWER.md phase 1: generators + links + jerrycans (power.ts; the preset holds every number). Absent = no power layer. */
    power?: PropsAutogenLayerConfig;
  };
  zone_multipliers?: Record<string, number>;
}

export interface GeneratePropsOptions {
  layout: GenLayout;
  balance?: Record<string, unknown>;
  autogenConfig?: PropsAutogenConfig;
  seed?: number;
  sourceProps?: LevelPropsFile | null;
  mapName?: string;
  layoutName?: string;
  projectRoot?: string;
}

export interface AutogenPropsResult {
  props: LevelPropsFile;
  fastGateResult: FastGateResult;
  /** Objects the static validator left out (the game would drop them at load: PropsValidator.cpp). */
  droppedByValidator: PropsValidationIssue[];
  /** The power layer build (null when the config has no power layer). */
  power: PowerBuildResult | null;
  /** Power network warnings (validatePowerLinks; nothing dropped). */
  powerIssues: PowerLinkIssue[];
  areaM2: number;
  areaFactor: number;
}

export const DEFAULT_AUTOGEN_CONFIG: PropsAutogenConfig = {
  unit_area_m2: 10000,
  reference: {
    map: 'L_Gen_Depot_1',
    layout: 'gen_ref_01_02_depot_1',
    area_m2: 21948,
  },
  layers: {
    crates: {
      preset: 'crates_default',
      count_mode: 'density',
      per_100m2: 0.25,
      base_max: 80,
      min_count: 8,
      max_count: 300,
      where: {
        near: [
          { max_cm: 150, weight: 3, what: 'wall' },
          { max_cm: 200, weight: 2, what: 'cover' },
        ],
        walk_layers: ['ground', 'floor:*'],
      },
    },
    barrels: {
      preset: 'barrels_default',
      per_10000m2: 3.64,
      min_count: 4,
      max_count: 80,
      where: {
        near: [{ max_cm: 120, weight: 2, what: 'wall' }],
      },
    },
    turrets: {
      preset: 'turrets_default',
      per_10000m2: 0.91,
      min_count: 1,
      max_count: 12,
    },
    mines: {
      preset: 'mines_default',
      field_per_10000m2: 1.82,
      min_field_count: 2,
      max_field_count: 30,
      doorway_percent: 25,
      crate_percent: 10,
    },
    loot: {
      preset: 'loot_default',
      budget_per_10000m2: 273.0,
      min_budget: 300,
      max_budget: 3600,
    },
    power: {
      preset: 'power_default',
    },
  },
  zone_multipliers: {
    industrial: 1.0,
    yard: 1.0,
    open: 0.8,
    street: 0.6,
  },
};

/** Finds the project root directory from cwd or path segments. */
export function findProjectRoot(startDir = process.cwd()): string {
  let cur = path.resolve(startDir);
  for (let i = 0; i < 8; i++) {
    if (fs.existsSync(path.join(cur, 'Content')) && fs.existsSync(path.join(cur, 'Source'))) {
      return cur;
    }
    const parent = path.dirname(cur);
    if (parent === cur) break;
    cur = parent;
  }
  return path.resolve(startDir);
}

/** Loads Content/Data/LevelGen/props_autogen.json or returns default fallback. */
export function loadAutogenConfig(projectRoot?: string): PropsAutogenConfig {
  const root = projectRoot ?? findProjectRoot();
  const cfgPath = path.join(root, 'Content/Data/LevelGen/props_autogen.json');
  try {
    if (fs.existsSync(cfgPath)) {
      return JSON.parse(fs.readFileSync(cfgPath, 'utf8')) as PropsAutogenConfig;
    }
  } catch {
    // Return fallback on read error
  }
  return DEFAULT_AUTOGEN_CONFIG;
}

/** Loads Content/Data/Balance/props_balance.json. */
export function loadPropsBalance(projectRoot?: string): Record<string, unknown> {
  const root = projectRoot ?? findProjectRoot();
  const balPath = path.join(root, 'Content/Data/Balance/props_balance.json');
  try {
    if (fs.existsSync(balPath)) {
      return JSON.parse(fs.readFileSync(balPath, 'utf8')) as Record<string, unknown>;
    }
  } catch {
    // Return empty on error
  }
  return {};
}

/**
 * Resolves the target map name for a layout name.
 * Reads existing Content/Data/LevelJson/*.json files to find matching levelgen_layout,
 * otherwise derives via mapNameFor(layoutName).
 */
export function targetMapNameForLayout(layoutName: string, projectRoot?: string): string {
  const root = projectRoot ?? findProjectRoot();
  const levelJsonDir = path.join(root, 'Content/Data/LevelJson');
  if (fs.existsSync(levelJsonDir)) {
    try {
      const files = fs.readdirSync(levelJsonDir);
      for (const f of files) {
        if (!f.endsWith('.json')) continue;
        const full = path.join(levelJsonDir, f);
        try {
          const content = JSON.parse(fs.readFileSync(full, 'utf8')) as { level_id?: string; levelgen_layout?: string };
          if (content.levelgen_layout) {
            const base = path.basename(content.levelgen_layout, '.json');
            if (base === layoutName && content.level_id) {
              return content.level_id;
            }
          }
        } catch {
          // ignore malformed level json
        }
      }
    } catch {
      // ignore readdir error
    }
  }

  return mapNameFor(layoutName);
}

/** Checks whether a props file can be automatically overwritten (must have source: "auto"). */
export function canOverwritePropsFile(existingProps: LevelPropsFile): boolean {
  return existingProps.source === 'auto';
}

/** Checks whether point is inside rectangle bounds [min, max]. */
function isPointInRect(p: [number, number], min: [number, number], max: [number, number]): boolean {
  return p[0] >= min[0] && p[0] <= max[0] && p[1] >= min[1] && p[1] <= max[1];
}

/**
 * Generates props for any generated, expanded, or re-rolled layout.
 * Pure, deterministic, respect density configs, keeps original props for expanded maps,
 * and passes validator and fast gate.
 */
export function generatePropsForLayout(options: GeneratePropsOptions): AutogenPropsResult {
  const { layout } = options;
  const projectRoot = options.projectRoot ?? findProjectRoot();
  const cfg = options.autogenConfig ?? loadAutogenConfig(projectRoot);
  const balance = options.balance ?? loadPropsBalance(projectRoot);

  const boundsMin = layout.bounds?.min ?? [-6200, -8850];
  const boundsMax = layout.bounds?.max ?? [6200, 8850];
  const widthM = Math.max(1, Math.abs(boundsMax[0] - boundsMin[0]) / 100);
  const heightM = Math.max(1, Math.abs(boundsMax[1] - boundsMin[1]) / 100);
  const areaM2 = widthM * heightM;
  const unitAreaM2 = cfg.unit_area_m2 ?? 10000;
  const areaUnits = areaM2 / unitAreaM2;
  const refArea = cfg.reference?.area_m2 ?? 21948;
  const areaFactor = Math.max(1, areaM2 / refArea);

  const seed = options.seed ?? (typeof layout.seed === 'number' ? layout.seed : 1);
  const layoutName = options.layoutName ?? layout.name ?? 'Map';
  const mapName = options.mapName ?? targetMapNameForLayout(layoutName, projectRoot);

  // Determine base area if expanded from an existing layout
  let baseBoundsMin: [number, number] | null = null;
  let baseBoundsMax: [number, number] | null = null;
  const sourceProps = options.sourceProps ?? null;

  if (sourceProps) {
    if (sourceProps.layout) {
      const baseLayoutFile = path.join(projectRoot, 'Content/Data/LevelGen/accepted', `${sourceProps.layout}.json`);
      try {
        if (fs.existsSync(baseLayoutFile)) {
          const baseLayout = JSON.parse(fs.readFileSync(baseLayoutFile, 'utf8')) as GenLayout;
          if (baseLayout.bounds?.min && baseLayout.bounds?.max) {
            baseBoundsMin = [baseLayout.bounds.min[0], baseLayout.bounds.min[1]];
            baseBoundsMax = [baseLayout.bounds.max[0], baseLayout.bounds.max[1]];
          }
        }
      } catch {
        // ignore read error
      }
    }
    if (!baseBoundsMin || !baseBoundsMax) {
      // Default to reference Depot bounds
      baseBoundsMin = [-6200, -8850];
      baseBoundsMax = [6200, 8850];
    }
  }

  // Configure layers with density scaling
  const layerKinds = ['crates', 'barrels', 'turrets', 'mines', 'loot'] as const;
  const layers: PropLayer[] = [];
  const originallyLocked = new Map<string, boolean>();

  for (const kind of layerKinds) {
    const lcfg = cfg.layers?.[kind] ?? DEFAULT_AUTOGEN_CONFIG.layers![kind]!;
    let rules: Record<string, unknown> = {};

    switch (kind) {
      case 'crates': {
        const baseMax = lcfg.base_max ?? 80;
        const maxCrates = Math.min(lcfg.max_count ?? 300, Math.round(baseMax * areaFactor));
        rules = {
          count: {
            mode: lcfg.count_mode ?? 'density',
            per_100m2: lcfg.per_100m2 ?? 0.25,
            max: maxCrates,
          },
          ...(lcfg.where ? { where: lcfg.where } : {}),
        };
        break;
      }
      case 'barrels': {
        const barrelRate = lcfg.per_10000m2 ?? 3.64;
        const barrelCount = Math.min(
          lcfg.max_count ?? 80,
          Math.max(lcfg.min_count ?? 4, Math.round(barrelRate * areaUnits))
        );
        rules = {
          count: { mode: 'count', value: barrelCount },
          ...(lcfg.where ? { where: lcfg.where } : {}),
        };
        break;
      }
      case 'turrets': {
        const turretRate = lcfg.per_10000m2 ?? 0.91;
        const turretCount = Math.min(
          lcfg.max_count ?? 12,
          Math.max(lcfg.min_count ?? 1, Math.round(turretRate * areaUnits))
        );
        rules = {
          count: { mode: 'count', value: turretCount },
        };
        break;
      }
      case 'mines': {
        const fieldRate = lcfg.field_per_10000m2 ?? 1.82;
        const fieldCount = Math.min(
          lcfg.max_field_count ?? 30,
          Math.max(lcfg.min_field_count ?? 2, Math.round(fieldRate * areaUnits))
        );
        rules = {
          field: { count: { mode: 'count', value: fieldCount } },
          doorways: { mode: 'percent', value: lcfg.doorway_percent ?? 25 },
          crates: { percent: lcfg.crate_percent ?? 10 },
        };
        break;
      }
      case 'loot': {
        const budgetRate = lcfg.budget_per_10000m2 ?? 273.0;
        const budgetValue = Math.min(
          lcfg.max_budget ?? 3600,
          Math.max(lcfg.min_budget ?? 300, Math.round(budgetRate * areaUnits))
        );
        rules = {
          budget_value: budgetValue,
        };
        break;
      }
    }

    // Retain existing objects from sourceProps within base bounds
    const initialObjects: PropObject[] = [];
    if (sourceProps && baseBoundsMin && baseBoundsMax) {
      const resolved = applyOverrides(sourceProps);
      for (const r of resolved.objects) {
        if (r.layerId === kind && isPointInRect(r.object.pos, baseBoundsMin, baseBoundsMax)) {
          originallyLocked.set(r.object.id, Boolean(r.object.locked));
          // Mark locked for placement.ts to treat as kept
          initialObjects.push({
            ...r.object,
            locked: true,
          });
        }
      }
    }

    layers.push({
      id: kind,
      kind,
      enabled: true,
      seed,
      locked: false,
      preset: lcfg.preset,
      rules,
      objects: initialObjects,
    });
  }

  const propsFile: LevelPropsFile = {
    schema: PROPS_SCHEMA,
    map: mapName,
    layout: layoutName,
    layout_hash: '0000000000000000000000000000000000000000',
    revision: 1,
    frozen: false,
    source: 'auto',
    layers,
    overrides: [],
  };

  const layoutConfig = PropsLayoutView.readConfig(balance);
  const view = PropsLayoutView.build(layout as unknown as Record<string, unknown>, layoutConfig);

  // Deterministically place all layers
  rerollAll(propsFile, balance, view, layout);

  // Restore original locked flags for kept objects that were not locked in source
  if (sourceProps) {
    for (const l of propsFile.layers) {
      for (const o of l.objects) {
        if (originallyLocked.has(o.id)) {
          o.locked = originallyLocked.get(o.id) ?? false;
        }
      }
    }
  }

  // G003b: leave out what the game's static validator (FPropsValidator) would drop at load - e.g. a Depot crate kept from the
  // source props that now overlaps a Depot cover, a mine inside a stair keep-out disk / a doorway corridor - so the TG shows what
  // the game spawns. A mines / loot object attached to a dropped crate goes with it (the validator's pass 2).
  const resolvedForCheck = applyOverrides(propsFile);
  const staticCheck = validateStatic(view, resolvedForCheck.objects, resolvedForCheck.keepOuts, balance);
  const droppedIds = new Set(staticCheck.dropped.map((r) => r.object.id));
  if (droppedIds.size > 0) {
    for (const l of propsFile.layers) {
      l.objects = l.objects.filter((o) => !droppedIds.has(o.id));
    }
  }

  // TURRET_POWER.md phase 1: generators linked to the turrets that survived the validator, their start states, jerrycans in the loot.
  let power: PowerBuildResult | null = null;
  let powerIssues: PowerLinkIssue[] = [];
  if (cfg.layers?.power) {
    power = buildPowerLayer(propsFile, view, balance, seed);
    const kindOfLayer = new Map(propsFile.layers.map((l) => [l.id, l.kind as string]));
    powerIssues = validatePowerLinks(applyOverrides(propsFile).objects, kindOfLayer, balance, view.spawnPos);
  }

  // Validation passes
  const errs = validateProps(propsFile);
  if (errs.length > 0) {
    throw new Error(`autogen props validation failed: ${errs.join(', ')}`);
  }

  const fastGateResult = runFastGate(layout, propsFile, balance);

  return {
    props: propsFile,
    fastGateResult,
    droppedByValidator: staticCheck.issues,
    power,
    powerIssues,
    areaM2,
    areaFactor,
  };
}
