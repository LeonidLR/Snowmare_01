// Training Ground / level generator: the props autogen configuration (Content/Data/LevelGen/props_autogen.json) - types, the built-in
// default and the route clearance lookup. Pure (no fs): shared by autogen.ts (Node) and the browser's re-roll (routeClearance.ts).

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
  route_clear_m?: number;
  route_corridor_m?: number;
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
      route_corridor_m: 2,
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
      route_corridor_m: 2,
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
      route_clear_m: 6,
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

/** A route clearance of a layer in cm: the config value, else the built-in default, else 0 (no clearance). */
export function clearanceCm(
  cfg: PropsAutogenConfig,
  kind: 'crates' | 'barrels' | 'mines',
  key: 'route_clear_m' | 'route_corridor_m'
): number {
  const m = cfg.layers?.[kind]?.[key] ?? DEFAULT_AUTOGEN_CONFIG.layers?.[kind]?.[key] ?? 0;
  return Math.max(0, Math.round(m * 100));
}
