export type EnemyType = "HOUND" | "SPITTER" | "BRUTE" | "FROSTBITTEN" | "CUTTER" | "MARKSMAN";
export type SpawnLane = "NORTH_GATE" | "WEST_FLANK" | "EAST_FLANK" | "ANY";

export interface EnemyCustomStats {
  health?: number;
  damage?: number;
  speed?: number;
  attack_range?: number;
  attack_cooldown?: number;
}

export interface EnemySpawn {
  enemy_type: EnemyType;
  count: number;
  spawn_lane: SpawnLane;
  spawn_delay_sec: number;
  initial_delay_sec: number;
  custom_stats?: EnemyCustomStats;
}

export interface WaveModifiers {
  enemy_hp_mult: number;
  enemy_damage_mult: number;
  enemy_speed_mult: number;
  cold_drain_mult: number;
}

export interface WaveConfig {
  wave_index: number;
  name?: string;
  is_active?: boolean;
  max_simultaneous_enemies: number;
  spawns: EnemySpawn[];
  wave_modifiers: WaveModifiers;
}

export type ResourceTier = 'MINIMAL' | 'STANDARD' | 'SECRET_ONLY' | 'MAXIMAL';

export type SimulationMode = 'EXPLORE_AND_COLLECT' | 'STARTING_UNIQUE' | 'EDITOR_PRESET';
export type PresetTier = 'MINIMAL' | 'STANDARD' | 'MAXIMAL' | 'CUSTOM';

export interface SquadLoadoutConfig {
  simulation_mode: SimulationMode;
  preset_tier: PresetTier;
  turrets_count: number;
  barricades_count: number;
  mines_count: number;
  medkits_count: number;
  m16_ammo: number;
  pistol_ammo: number;
  canned_food: number;
  matches: number;
  /** Grenades per operative (the game reads it; -1 / absent: the operatives keep their own). */
  grenades_count?: number;
}

/** Content/Data/Weapons/weapons_tuning.json (the game applies it at start). Metres / seconds. */
export interface WeaponTuningEntry {
  name?: string;
  base_damage: number;
  attack_range_m: number;
  fire_rate: number;
  armor_penetration: number;
  max_clip_size: number;
  default_reserve_ammo: number;
  reload_time: number;
  status_duration: number;
  status_tick_damage: number;
  max_range_cells: number;
  base_hit_chances: number[];
  distance_damage_multipliers: number[];
}

export interface GrenadeTuning {
  damage: number;
  effect_radius_m: number;
  throw_range_m: number;
  max_carried: number;
}

/** An enemy's ranged weapon (enemy_weapons.<id>): real time + turn based (tb_*). Metres / seconds / cells. */
export interface EnemyRangedWeapon {
  damage: number;
  base_accuracy: number;
  aim_duration: number;
  shot_cooldown: number;
  crit_chance: number;
  crit_multiplier: number;
  prone_accuracy_bonus: number;
  crouch_accuracy_bonus: number;
  preferred_min_range_m: number;
  preferred_max_range_m: number;
  tb_damage_scale: number;
  tb_attack_ap: number;
  tb_min_range_cells: number;
  tb_max_range_cells: number;
  tb_base_hit_chance: number;
  tb_hit_falloff_per_cell: number;
}

/** turn_based_rules: multipliers of the turn-based combat (the game applies them when a fight starts). */
export interface TurnBasedRulesTuning {
  crouch_move_cost_multiplier: number;
  cover_fire_accuracy_multiplier: number;
  enemy_fire_at_cover_multiplier: number;
  crouch_damage_multiplier: number;
  prone_damage_multiplier: number;
}

export interface WeaponTuningFile {
  comment?: string;
  weapons: Record<string, WeaponTuningEntry>;
  grenade: GrenadeTuning;
  turn_based_rules?: TurnBasedRulesTuning;
  enemy_weapons?: Record<string, EnemyRangedWeapon>;
}

export interface LevelConfig {
  $schema?: string;
  level_id: string;
  level_name: string;
  description: string;
  prep_phase_duration: number;
  wave_rest_duration: number;
  standard_loot_found?: boolean;
  puzzle_secret_found?: boolean;
  squad_loadout?: SquadLoadoutConfig;
  waves: WaveConfig[];
}

export interface EnemyMeta {
  name: string;
  baseHp: number;
  baseDps: number;
  speed: number;
  color: string;
  icon: string;
}

export const ENEMY_DB: Record<EnemyType, EnemyMeta> = {
  HOUND: {
    name: "Ледяная гончая",
    baseHp: 45,
    baseDps: 12,
    speed: 5.4,
    color: "#38bdf8", // Sky blue
    icon: "🐺"
  },
  SPITTER: {
    name: "Ледяной стрелок",
    baseHp: 70,
    baseDps: 18,
    speed: 3.2,
    color: "#a855f7", // Purple
    icon: "🏹"
  },
  BRUTE: {
    name: "Ледяной громила",
    baseHp: 220,
    baseDps: 35,
    speed: 1.8,
    color: "#f97316", // Orange
    icon: "❄️"
  },
  FROSTBITTEN: {
    name: "Промёрзший",
    baseHp: 80,
    baseDps: 20,
    speed: 3.0,
    color: "#94a3b8", // Slate
    icon: "🧟"
  },
  CUTTER: {
    name: "Механо-гончая Cutter",
    baseHp: 75,
    baseDps: 18,
    speed: 6.2,
    color: "#f43f5e", // Rose crimson
    icon: "🐺"
  },
  // UE-only (AMarksmanEnemyCharacter): 45 dmg per 2 s aim + 2.5 s cooldown at ~60 % hit chance.
  MARKSMAN: {
    name: "Снайпер",
    baseHp: 80,
    baseDps: 6,
    speed: 3.2,
    color: "#84cc16", // Lime
    icon: "🎯"
  }
};

export interface WeaponMemberTelemetry {
  character_name: string;
  dominant_weapon: string;
  first_pistol_wave: number;
  total_shots_m16: number;
  total_damage_m16: number;
  total_shots_pistol: number;
  total_damage_pistol: number;
  total_strikes_knife: number;
  total_damage_knife: number;
  by_wave?: {
    [wave: number]: {
      [wep_id: string]: {
        shots: number;
        damage: number;
      };
    };
  };
}

export interface DeployableTelemetrySummary {
  turrets: {
    survived: number;
    avg_hp_pct: number;
    kills: number;
    damage: number;
  };
  barricades: {
    survived: number;
    avg_hp_pct: number;
    kills: number;
    damage: number;
  };
  mines: {
    detonated: number;
    kills: number;
    damage: number;
  };
}

export interface RemainingResourcesTelemetry {
  medkits: number;
  ammo_m16: number;
  ammo_pistol: number;
  canned_food: number;
  chocolate: number;
  matches: number;
}

export interface MemberColdTelemetry {
  name: string;
  final_cold_pct: number;
  cold_damage_taken: number;
  extreme_cold_time_sec: number;
}

export interface BotPreset {
  id: string;
  name: string;
  description?: string;
  runs: number;
  profile: 'CASUAL' | 'NORMAL' | 'VETERAN';
  speed: number;
}

export interface TelemetryRun {
  session_id: string;
  timestamp_utc: string;
  tester_profile: string;
  level_id: string;
  result: 'VICTORY' | 'DEFEAT';
  waves_cleared: number;
  total_waves: number;
  run_duration_sec: number;
  squad_avg_cold_percent?: number;
  standard_loot_found?: boolean;
  puzzle_secret_found?: boolean;
  remaining_resources?: RemainingResourcesTelemetry;
  deployables_summary?: DeployableTelemetrySummary;
  squad_cold_stats?: MemberColdTelemetry[];
  weapon_analytics?: WeaponMemberTelemetry[];
  death_context?: {
    failed_wave: number;
    cause: string;
    squad_cold_at_death?: number;
  } | null;
}
