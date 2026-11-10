// Wave Editor level settings (not per wave): the blast door ("gate_*" keys) and the Demo / tutorial flags ("demo" object).
// Pure logic, no React: tests in levelSettings.test.ts (`npm test`). The game reads these in LevelJsonRules.cpp
// (gate_open_mode, gate_progress_per_second, gate_max_progress_before_final, gate_full_open_after_wave; ParseDemoSettings).
// Round-trip safety: every function returns a NEW level object that keeps all other keys (including an untouched "demo").

import type { EnvironmentConfig, LevelLoadouts, LevelKit, OperativeKit } from '../types.ts';

export type GateOpenMode = 'generator_waves' | 'instant';
export type LevelLike = Record<string, any>;

export const GATE_DEFAULTS = {
  gate_open_mode: 'generator_waves' as GateOpenMode,
  gate_progress_per_second: 0.004,
  gate_max_progress_before_final: 0.9,
  gate_full_open_after_wave: 0,
};

export interface GateField {
  key: 'gate_progress_per_second' | 'gate_max_progress_before_final' | 'gate_full_open_after_wave';
  label: string;
  hint: string;
  min: number;
  max: number;
  step: number;
  integer?: boolean;
  unit: string;
}

export const GATE_MODE_OPTIONS: { value: GateOpenMode; label: string; hint: string }[] = [
  {
    value: 'generator_waves',
    label: 'Генератор + волны',
    hint: 'generator_waves: the terminal only unlocks the servos; the door stays visually closed and opens fully only after the final wave is cleared with the generator working. A rotating beacon shows the process.',
  },
  { value: 'instant', label: 'Сразу', hint: 'instant: the terminal opens the door at once (Godot behaviour).' },
];

export const GATE_FIELDS: GateField[] = [
  {
    key: 'gate_progress_per_second', label: 'Прогресс в секунду', unit: '1/с', min: 0, max: 1, step: 0.001,
    hint: 'gate_progress_per_second: internal door progress per second of wave fight while the generator works. Must be > 0 (default 0.004 = 0.4 %/s).',
  },
  {
    key: 'gate_max_progress_before_final', label: 'Потолок до финала', unit: '0–1', min: 0, max: 1, step: 0.05,
    hint: 'gate_max_progress_before_final: most the progress reaches before the final wave is cleared; wave N of F caps at this × N / F (0.9 = 30 / 60 / 90 % for 3 waves). Engine clamps to 0.99.',
  },
  {
    key: 'gate_full_open_after_wave', label: 'Открыть после волны', unit: 'волна', min: 0, max: 99, step: 1, integer: true,
    hint: 'gate_full_open_after_wave: the wave whose clearing opens the door fully. 0 = the last wave of the level.',
  },
];

export interface GateValues {
  gate_open_mode: GateOpenMode;
  gate_progress_per_second: number;
  gate_max_progress_before_final: number;
  gate_full_open_after_wave: number;
}

/** Effective gate values (absent key = the game default). */
export function getGate(level: LevelLike): GateValues {
  const num = (key: GateField['key']): number => (typeof level[key] === 'number' ? (level[key] as number) : GATE_DEFAULTS[key]);
  return {
    gate_open_mode: level.gate_open_mode === 'instant' ? 'instant' : 'generator_waves',
    gate_progress_per_second: num('gate_progress_per_second'),
    gate_max_progress_before_final: num('gate_max_progress_before_final'),
    gate_full_open_after_wave: num('gate_full_open_after_wave'),
  };
}

/** Validation errors for the gate values (empty = ok). waveCount bounds gate_full_open_after_wave. */
export function validateGate(values: GateValues, waveCount: number): string[] {
  const errors: string[] = [];
  if (values.gate_open_mode !== 'generator_waves' && values.gate_open_mode !== 'instant') errors.push('Режим двери: generator_waves | instant');
  if (values.gate_open_mode === 'generator_waves') {
    const rate = values.gate_progress_per_second;
    if (!Number.isFinite(rate) || rate <= 0) errors.push('Прогресс в секунду должен быть > 0');
    const cap = values.gate_max_progress_before_final;
    if (!Number.isFinite(cap) || cap < 0 || cap > 1) errors.push('Потолок до финала должен быть в диапазоне 0..1');
    const wave = values.gate_full_open_after_wave;
    if (!Number.isInteger(wave) || wave < 0 || wave > Math.max(0, waveCount)) {
      errors.push(`Открыть после волны: целое число 0..${Math.max(0, waveCount)} (0 = последняя)`);
    }
  }
  return errors;
}

/** Sets one gate key (undefined removes it = the game default). The rest of the level is kept. */
export function setGateKey<K extends keyof GateValues>(level: LevelLike, key: K, value: GateValues[K] | undefined): LevelLike {
  const next: LevelLike = { ...level };
  if (value === undefined || (typeof value === 'number' && Number.isNaN(value))) delete next[key];
  else next[key] = value;
  return next;
}

// ---- Demo / tutorial ("demo" object, FDemoModeSettings) ----

export type Tri = 'default' | 'on' | 'off';

export interface DemoValues {
  enabled: boolean;
  panic: boolean;
  vision_cones: Tri;
  tutorial: boolean;
  tutorial_steps: string;
  hud: 'default' | 'demo';
  intro_dialogue: boolean;
}

export const DEMO_DEFAULT_STEPS = 'demo_01_steps.json';

export const DEMO_HINTS = {
  enabled: 'demo: a "demo" object marks the level as the demo level. No object = normal game, every flag below off.',
  panic: 'panic: false switches Panic / stress off on every operative (default true).',
  vision_cones: 'vision_cones: on / off forces every operative and enemy vision cone; default = the Blueprint settings.',
  tutorial: 'tutorial: true runs the tutorial steps file below (the player\'s tutorial ON/OFF setting still applies).',
  tutorial_steps: `tutorial_steps: Content/Data/Tutorial/<file> (default ${DEMO_DEFAULT_STEPS}).`,
  hud: 'hud: "demo" hides the debug squad panel and the labels above operatives.',
  intro_dialogue: 'intro_dialogue: false skips the blocking mission-start briefing window (its content moves into the tutorial walk-and-talk). With a demo object and no key the game defaults to false.',
};

/** Effective demo flags (absent keys = the game defaults of FDemoModeSettings / ParseDemoSettings). */
export function getDemo(level: LevelLike): DemoValues {
  const demo = level.demo;
  if (demo === null || typeof demo !== 'object' || Array.isArray(demo)) {
    return { enabled: false, panic: true, vision_cones: 'default', tutorial: false, tutorial_steps: DEMO_DEFAULT_STEPS, hud: 'default', intro_dialogue: true };
  }
  return {
    enabled: true,
    panic: typeof demo.panic === 'boolean' ? demo.panic : true,
    vision_cones: typeof demo.vision_cones === 'boolean' ? (demo.vision_cones ? 'on' : 'off') : 'default',
    tutorial: typeof demo.tutorial === 'boolean' ? demo.tutorial : false,
    tutorial_steps: typeof demo.tutorial_steps === 'string' && demo.tutorial_steps !== '' ? demo.tutorial_steps : DEMO_DEFAULT_STEPS,
    hud: typeof demo.hud === 'string' && demo.hud.toLowerCase() === 'demo' ? 'demo' : 'default',
    intro_dialogue: typeof demo.intro_dialogue === 'boolean' ? demo.intro_dialogue : false,
  };
}

export function validateDemo(values: DemoValues): string[] {
  const errors: string[] = [];
  if (!values.enabled) return errors;
  const file = values.tutorial_steps;
  if (!file.endsWith('.json') || /[\\/]/.test(file) || file.includes('..')) {
    errors.push('Файл шагов туториала: имя *.json из Content/Data/Tutorial (без пути)');
  }
  return errors;
}

/** Creates (enabled) or removes the "demo" object. A new object is written with the effective defaults made explicit. */
export function setDemoEnabled(level: LevelLike, enabled: boolean): LevelLike {
  const next: LevelLike = { ...level };
  if (!enabled) {
    delete next.demo;
  } else if (next.demo === null || typeof next.demo !== 'object' || Array.isArray(next.demo)) {
    next.demo = { panic: true, tutorial: false, intro_dialogue: false };
  }
  return next;
}

/**
 * Sets one key of the "demo" object (undefined removes it). Other keys of the object, including ones this editor does not
 * know, stay. Does nothing when the level has no demo object.
 */
export function setDemoKey(level: LevelLike, key: string, value: boolean | string | undefined): LevelLike {
  if (level.demo === null || typeof level.demo !== 'object' || Array.isArray(level.demo)) return level;
  const demo: Record<string, any> = { ...level.demo };
  if (value === undefined) delete demo[key];
  else demo[key] = value;
  return { ...level, demo };
}

/** One gate field is out of range (only meaningful in generator_waves mode). */
export function isGateFieldInvalid(key: GateField['key'], values: GateValues, waveCount: number): boolean {
  const value = values[key];
  if (!Number.isFinite(value)) return true;
  if (key === 'gate_progress_per_second') return value <= 0;
  if (key === 'gate_max_progress_before_final') return value < 0 || value > 1;
  return !Number.isInteger(value) || value < 0 || value > Math.max(0, waveCount);
}

// ---- Level cold ("cold_rate_mult", FLevelCombatConfig::ColdRateMult) ----

export const COLD_RATE_DEFAULT = 1.0;
export const COLD_RATE_MIN = 0;
export const COLD_RATE_MAX = 3;
export const COLD_RATE_HINT =
  'cold_rate_mult: level-wide multiplier of the cold build-up in exploration and fights (UE ColdRules). Multiplied with each wave\'s ' +
  'cold_drain_mult and camera zones, never instead of them. 1 = unchanged (absent key); 0.8 = 20 % less cold (Demo 01 Frozen Lake). ' +
  'Warming, the sprint warm-up and scripted cold shocks (tutorial gust) are not scaled.';

/** Effective level cold multiplier (absent / invalid key = 1). */
export function getColdRateMult(level: LevelLike): number {
  return typeof level.cold_rate_mult === 'number' ? level.cold_rate_mult : COLD_RATE_DEFAULT;
}

/** Sets "cold_rate_mult"; undefined / NaN / exactly 1 removes the key (the game default). The rest of the level is kept. */
export function setColdRateMult(level: LevelLike, value: number | undefined): LevelLike {
  const next: LevelLike = { ...level };
  if (value === undefined || Number.isNaN(value) || value === COLD_RATE_DEFAULT) delete next.cold_rate_mult;
  else next.cold_rate_mult = value;
  return next;
}

export function validateColdRate(value: number): string[] {
  if (!Number.isFinite(value) || value < COLD_RATE_MIN || value > COLD_RATE_MAX) {
    return [`Холод уровня: число ${COLD_RATE_MIN}..${COLD_RATE_MAX} (1 = как обычно)`];
  }
  return [];
}

/** All level-settings errors of a level (gate + demo + cold + horde + environment + loadouts + kit); used to block the save. */
export function validateLevelSettings(level: LevelLike): string[] {
  const waves = Array.isArray(level.waves) ? level.waves.length : 0;
  const hordeErrors = typeof level.horde_after_firefights === 'number'
    ? validateHordeAfterFirefights(level.horde_after_firefights)
    : [];
  return [
    ...validateGate(getGate(level), waves),
    ...validateDemo(getDemo(level)),
    ...validateColdRate(getColdRateMult(level)),
    ...hordeErrors,
    ...(hasOwnHordeSpawnDistance(level) ? validateHordeSpawnDistance(getHordeSpawnDistance(level)) : []),
    ...validateEnvironment(getEnvironment(level)),
    ...validateLoadouts(level.loadouts),
    ...validateKit(level.kit),
  ];
}

// ---- Level Horde ("horde_enabled", "horde_after_firefights") ----

/** Game designer 2026-11-09: the horde after 10 firefights in a row (was 6); FirefightRules::DefaultAfterFirefights. */
export const HORDE_AFTER_FIREFIGHTS_DEFAULT = 10;
export const HORDE_AFTER_FIREFIGHTS_MIN = 1;

export function getHordeEnabled(level: LevelLike): boolean {
  return level.horde_enabled !== false;
}

export function setHordeEnabled(level: LevelLike, enabled: boolean): LevelLike {
  const next: LevelLike = { ...level };
  if (enabled) {
    if (next.horde_enabled === false) delete next.horde_enabled;
  } else {
    next.horde_enabled = false;
  }
  return next;
}

export function getHordeAfterFirefights(level: LevelLike): number {
  return typeof level.horde_after_firefights === 'number' && Number.isFinite(level.horde_after_firefights)
    ? level.horde_after_firefights
    : HORDE_AFTER_FIREFIGHTS_DEFAULT;
}

export function setHordeAfterFirefights(level: LevelLike, value: number | undefined): LevelLike {
  const next: LevelLike = { ...level };
  if (value === undefined || Number.isNaN(value)) {
    delete next.horde_after_firefights;
  } else {
    next.horde_after_firefights = Math.round(value);
  }
  return next;
}

export function validateHordeAfterFirefights(value: number): string[] {
  if (!Number.isInteger(value) || value < HORDE_AFTER_FIREFIGHTS_MIN) {
    return [`Орда после N перестрелок: целое число >= ${HORDE_AFTER_FIREFIGHTS_MIN} (по умолчанию ${HORDE_AFTER_FIREFIGHTS_DEFAULT})`];
  }
  return [];
}

// ---- The firefight horde: distance + the waves that feed it (game designer play-test 2026-11-09) ----
// The game: HordeRules::ShouldWavesFeedHorde / WaveForHorde / PlanFromWave (Source/CodexTactics/Private/Combat/HordeRules.cpp),
// horde.json firefight_spawn_distance_m (300) / waves_feed_horde (true); the level "horde" object overrides them for one level.

/** horde.json firefight_spawn_distance_m: the firefight horde appears this far from the squad (+- firefight_spawn_spread_m 40). */
export const HORDE_SPAWN_DISTANCE_DEFAULT = 300;
export const HORDE_SPAWN_DISTANCE_MIN = 25;
export const HORDE_SPAWN_DISTANCE_MAX = 2000;

function hordeOverride(level: LevelLike): Record<string, any> | undefined {
  const horde = level.horde;
  return horde && typeof horde === 'object' && !Array.isArray(horde) ? horde : undefined;
}

/** The level's firefight horde distance in metres (its "horde" override, else the horde.json default 300). */
export function getHordeSpawnDistance(level: LevelLike): number {
  const value = hordeOverride(level)?.firefight_spawn_distance_m;
  return typeof value === 'number' && Number.isFinite(value) ? value : HORDE_SPAWN_DISTANCE_DEFAULT;
}

/** The level has its own distance (in its "horde" override). */
export function hasOwnHordeSpawnDistance(level: LevelLike): boolean {
  return typeof hordeOverride(level)?.firefight_spawn_distance_m === 'number';
}

/** Writes / removes "horde": { "firefight_spawn_distance_m" } (an emptied "horde" object is removed); other keys kept, input not mutated. */
export function setHordeSpawnDistance(level: LevelLike, value: number | undefined): LevelLike {
  const next: LevelLike = { ...level };
  const horde: Record<string, any> = { ...(hordeOverride(level) ?? {}) };
  if (value === undefined || Number.isNaN(value)) {
    delete horde.firefight_spawn_distance_m;
  } else {
    horde.firefight_spawn_distance_m = value;
  }
  if (Object.keys(horde).length === 0) delete next.horde;
  else next.horde = horde;
  return next;
}

export function validateHordeSpawnDistance(value: number): string[] {
  if (!Number.isFinite(value) || value < HORDE_SPAWN_DISTANCE_MIN || value > HORDE_SPAWN_DISTANCE_MAX) {
    return [`Дистанция орды: ${HORDE_SPAWN_DISTANCE_MIN}..${HORDE_SPAWN_DISTANCE_MAX} м (по умолчанию ${HORDE_SPAWN_DISTANCE_DEFAULT})`];
  }
  return [];
}

/**
 * Whether the level's waves feed the firefight horde (the game decides at run time - HordeRules::ShouldWavesFeedHorde):
 * 'waves' - combat_start "ambush": the waves are never played as scripted waves, they feed the hordes in order;
 * 'scripted' - combat_start "button": the waves are the scripted fights, the horde uses the horde.json / "horde" count + composition;
 * 'map' - "auto": an ambush level when the map has patrols / placed enemies (then 'waves'), else 'scripted';
 * 'off' - the level's "horde" override says waves_feed_horde: false; 'none' - no active waves (the horde.json count + composition).
 */
export type HordeWaveFeed = 'waves' | 'scripted' | 'map' | 'off' | 'none';

export function activeWaves(level: LevelLike): Record<string, any>[] {
  return (Array.isArray(level.waves) ? level.waves : []).filter((w: any) => w && typeof w === 'object' && w.is_active !== false);
}

export function getHordeWaveFeed(level: LevelLike): HordeWaveFeed {
  if (hordeOverride(level)?.waves_feed_horde === false) return 'off';
  if (activeWaves(level).length === 0) return 'none';
  const start = typeof level.combat_start === 'string' ? level.combat_start.trim().toLowerCase() : 'auto';
  if (start === 'ambush') return 'waves';
  if (start === 'button') return 'scripted';
  return 'map';
}

/** The 0-based active wave of the level's N-th firefight horde (1-based): min(N, waves) - 1 (the last wave repeats); -1 without waves. */
export function waveForHorde(hordeOrdinal: number, activeWaveCount: number): number {
  if (activeWaveCount <= 0) return -1;
  return Math.min(Math.max(1, Math.round(hordeOrdinal)), activeWaveCount) - 1;
}

/** One line per active wave: "Орда 1 ← волна 1: 4 FROSTBITTEN + 30 HOUND, до 2 одновременно" (SPITTER shown as MILITARY_ZOMBIE). */
export function describeHordeWaves(level: LevelLike): string[] {
  const waves = activeWaves(level);
  return waves.map((wave, index) => {
    const parts: string[] = [];
    let swaps = 0;
    for (const spawn of Array.isArray(wave.spawns) ? wave.spawns : []) {
      const count = typeof spawn?.count === 'number' ? spawn.count : 1;
      if (count <= 0) continue;
      const type = String(spawn?.enemy_type ?? '?').toUpperCase();
      if (type === 'SPITTER') swaps += count;
      parts.push(`${count} ${type === 'SPITTER' ? 'MILITARY_ZOMBIE' : type}`);
    }
    const cap = typeof wave.max_simultaneous_enemies === 'number' ? Math.max(1, Math.round(wave.max_simultaneous_enemies)) : 8;
    const label = typeof wave.wave_index === 'number' ? wave.wave_index : index + 1;
    const repeat = index === waves.length - 1 ? ` (и все следующие орды)` : '';
    const spitter = swaps > 0 ? `; SPITTER → MILITARY_ZOMBIE` : '';
    return `Орда ${index + 1}${repeat} ← волна ${label}: ${parts.join(' + ') || 'никого'}, до ${cap} одновременно${spitter}`;
  });
}

// ---- Environment ("environment" object, FEnvironmentLevelSettings) ----

export const ARCTIC_WEATHER_OPTIONS: { value: string; label: string }[] = [
  { value: 'Clear', label: 'Ясно (Clear)' },
  { value: 'Overcast', label: 'Пасмурно (Overcast)' },
  { value: 'Snowfall', label: 'Снегопад (Snowfall)' },
  { value: 'Blizzard', label: 'Метель (Blizzard)' },
];

export const TEMPERATE_WEATHER_OPTIONS: { value: string; label: string }[] = [
  ...ARCTIC_WEATHER_OPTIONS,
  { value: 'Sunny', label: 'Солнечно (Sunny)' },
  { value: 'SunnyDay', label: 'Ясный день (SunnyDay)' },
  { value: 'Rain', label: 'Дождь (Rain)' },
  { value: 'Storm', label: 'Шторм (Storm)' },
];

export const CLIMATE_OPTIONS: { value: 'arctic' | 'temperate'; label: string }[] = [
  { value: 'arctic', label: 'Арктический (arctic, по умолчанию)' },
  { value: 'temperate', label: 'Умеренный (temperate, сюжетные уровни)' },
];

export function getEnvironment(level: LevelLike): EnvironmentConfig {
  const env = level.environment;
  return env && typeof env === 'object' && !Array.isArray(env) ? { ...env } : {};
}

export function setEnvironmentKey<K extends keyof EnvironmentConfig>(
  level: LevelLike,
  key: K,
  value: EnvironmentConfig[K] | undefined
): LevelLike {
  const next: LevelLike = { ...level };
  const env: Record<string, any> = { ...(getEnvironment(level) as Record<string, any>) };

  if (
    value === undefined ||
    (typeof value === 'number' && Number.isNaN(value)) ||
    (key === 'override_carry' && value === false) ||
    (key === 'locked' && value === false) ||
    (key === 'visuals' && value === true)
  ) {
    delete env[key];
  } else {
    env[key] = value;
  }

  if (Object.keys(env).length === 0) {
    delete next.environment;
  } else {
    next.environment = env;
  }
  return next;
}

export function validateEnvironment(env: EnvironmentConfig): string[] {
  const errors: string[] = [];
  if (env.start_hour !== undefined) {
    if (!Number.isFinite(env.start_hour) || env.start_hour < 0 || env.start_hour > 24) {
      errors.push('Час старта погоды: число от 0 до 24');
    }
  }
  if (env.climate !== undefined && env.climate !== 'arctic' && env.climate !== 'temperate') {
    errors.push("Климат: 'arctic' или 'temperate'");
  }
  if (env.weather !== undefined) {
    const isTemperate = env.climate === 'temperate';
    const valid = isTemperate
      ? ['Clear', 'Overcast', 'Snowfall', 'Blizzard', 'Sunny', 'SunnyDay', 'Rain', 'Storm']
      : ['Clear', 'Overcast', 'Snowfall', 'Blizzard'];
    if (!valid.includes(env.weather)) {
      errors.push(
        isTemperate
          ? 'Погода: Clear, Overcast, Snowfall, Blizzard, Sunny, SunnyDay, Rain или Storm'
          : 'Погода для арктики: Clear, Overcast, Snowfall или Blizzard'
      );
    }
  }
  if (env.snow_cover !== undefined) {
    if (!Number.isFinite(env.snow_cover) || env.snow_cover < 0 || env.snow_cover > 1) {
      errors.push('Снежный покров: число от 0 до 1');
    }
  }
  if (env.wetness !== undefined) {
    if (!Number.isFinite(env.wetness) || env.wetness < 0 || env.wetness > 1) {
      errors.push('Влажность: число от 0 до 1');
    }
  }
  if (env.puddles !== undefined) {
    if (!Number.isFinite(env.puddles) || env.puddles < 0 || env.puddles > 1) {
      errors.push('Лужи: число от 0 до 1');
    }
  }
  if (env.exposure_bias !== undefined) {
    if (!Number.isFinite(env.exposure_bias) || env.exposure_bias < -4 || env.exposure_bias > 4) {
      errors.push('Экспозиция (EV): число от -4 до 4');
    }
  }
  return errors;
}

// ---- Per-Level Weapons ("loadouts") ----

export const AVAILABLE_WEAPONS: { id: string; name: string }[] = [
  { id: 'm16', name: 'M16' },
  { id: 'pistol', name: 'Пистолет' },
  { id: 'shotgun', name: 'Дробовик' },
  { id: 'sniper_rifle', name: 'Снайперская винтовка' },
  { id: 'rpg7', name: 'РПГ-7' },
  { id: 'grenade', name: 'Граната' },
];

export const STANDARD_OPERATIVES: { key: string; role: string; label: string; defaultWeapons: string[] }[] = [
  { key: 'commander', role: 'Командир', label: 'Командир (Commander / slot_0)', defaultWeapons: ['m16', 'pistol'] },
  { key: 'engineer', role: 'Инженер', label: 'Инженер (Engineer / slot_1)', defaultWeapons: ['shotgun', 'pistol'] },
  { key: 'medic_sapper', role: 'Медик-сапёр', label: 'Медик-сапёр (Medic-Sapper / slot_2)', defaultWeapons: ['m16', 'pistol'] },
  { key: 'recruit', role: 'Новобранец', label: 'Новобранец (Recruit)', defaultWeapons: ['pistol'] },
];

export function getLoadouts(level: LevelLike): LevelLoadouts {
  const l = level.loadouts;
  return l && typeof l === 'object' && !Array.isArray(l) ? { ...l } : {};
}

export function setOperativeLoadout(level: LevelLike, operativeKey: string, weapons: string[] | undefined): LevelLike {
  const next: LevelLike = { ...level };
  const loadouts: Record<string, string[]> = { ...(getLoadouts(level) as Record<string, string[]>) };
  const cleanKey = operativeKey.trim().toLowerCase();

  const cleanWeapons = (weapons || []).map((w) => w.trim().toLowerCase()).filter(Boolean);
  if (cleanWeapons.length === 0) {
    delete loadouts[cleanKey];
  } else {
    loadouts[cleanKey] = cleanWeapons;
  }

  if (Object.keys(loadouts).length === 0) {
    delete next.loadouts;
  } else {
    next.loadouts = loadouts;
  }
  return next;
}

export function validateLoadouts(loadouts: LevelLoadouts | undefined): string[] {
  if (!loadouts || typeof loadouts !== 'object') return [];
  const errors: string[] = [];
  for (const [key, list] of Object.entries(loadouts)) {
    if (!Array.isArray(list)) {
      errors.push(`Оружие для ${key}: должен быть список`);
      continue;
    }
    for (const w of list) {
      if (typeof w !== 'string' || !w.trim()) {
        errors.push(`Оружие для ${key}: недопустимый идентификатор оружия`);
      }
    }
  }
  return errors;
}

// ---- Per-Level Starting Kit ("kit") ----

export function getKit(level: LevelLike): LevelKit {
  const k = level.kit;
  return k && typeof k === 'object' && !Array.isArray(k) ? { ...k } : {};
}

export function setOperativeKit(level: LevelLike, operativeKey: string, kit: OperativeKit | undefined): LevelLike {
  const next: LevelLike = { ...level };
  const fullKit: Record<string, any> = { ...(getKit(level) as Record<string, any>) };
  const cleanKey = operativeKey.trim().toLowerCase();

  if (!kit) {
    delete fullKit[cleanKey];
  } else {
    // Preserve any existing unknown per-operative keys (Claude review defect #1)
    const existingOp = fullKit[cleanKey] && typeof fullKit[cleanKey] === 'object' ? fullKit[cleanKey] : {};
    const cleaned: Record<string, any> = { ...existingOp, ...kit };

    if (typeof kit.grenades === 'number' && !Number.isNaN(kit.grenades)) {
      cleaned.grenades = Math.max(0, Math.round(kit.grenades));
    } else if (kit.grenades === undefined) {
      delete cleaned.grenades;
    }

    if (typeof kit.medkits === 'number' && !Number.isNaN(kit.medkits)) {
      cleaned.medkits = Math.max(0, Math.round(kit.medkits));
    } else if (kit.medkits === undefined) {
      delete cleaned.medkits;
    }

    if (typeof kit.heat_packs === 'number' && !Number.isNaN(kit.heat_packs)) {
      cleaned.heat_packs = Math.max(0, Math.round(kit.heat_packs));
    } else if (kit.heat_packs === undefined) {
      delete cleaned.heat_packs;
    }

    if (kit.ammo && typeof kit.ammo === 'object') {
      const existingAmmo = existingOp.ammo && typeof existingOp.ammo === 'object' ? existingOp.ammo : {};
      const cleanAmmo: Record<string, number> = { ...existingAmmo };
      for (const [w, count] of Object.entries(kit.ammo)) {
        if (typeof count === 'number' && !Number.isNaN(count) && count >= 0) {
          cleanAmmo[w.trim().toLowerCase()] = Math.round(count);
        } else if (count === undefined) {
          delete cleanAmmo[w.trim().toLowerCase()];
        }
      }
      if (Object.keys(cleanAmmo).length > 0) {
        cleaned.ammo = cleanAmmo;
      } else {
        delete cleaned.ammo;
      }
    } else if (kit.ammo === undefined && !existingOp.ammo) {
      delete cleaned.ammo;
    }

    if (Object.keys(cleaned).length === 0) {
      delete fullKit[cleanKey];
    } else {
      fullKit[cleanKey] = cleaned;
    }
  }

  if (Object.keys(fullKit).length === 0) {
    delete next.kit;
  } else {
    next.kit = fullKit;
  }
  return next;
}

export function validateKit(kit: LevelKit | undefined): string[] {
  if (!kit || typeof kit !== 'object') return [];
  const errors: string[] = [];
  for (const [key, opKit] of Object.entries(kit)) {
    if (!opKit || typeof opKit !== 'object') continue;
    if (opKit.grenades !== undefined && (!Number.isInteger(opKit.grenades) || opKit.grenades < 0)) {
      errors.push(`Снаряжение ${key}: гранаты >= 0`);
    }
    if (opKit.medkits !== undefined && (!Number.isInteger(opKit.medkits) || opKit.medkits < 0)) {
      errors.push(`Снаряжение ${key}: аптечки >= 0`);
    }
    if (opKit.heat_packs !== undefined && (!Number.isInteger(opKit.heat_packs) || opKit.heat_packs < 0)) {
      errors.push(`Снаряжение ${key}: теплопакеты >= 0`);
    }
    if (opKit.ammo && typeof opKit.ammo === 'object') {
      for (const [w, count] of Object.entries(opKit.ammo)) {
        if (!Number.isInteger(count) || count < 0) {
          errors.push(`Снаряжение ${key}: патроны для ${w} >= 0`);
        }
      }
    }
  }
  return errors;
}
