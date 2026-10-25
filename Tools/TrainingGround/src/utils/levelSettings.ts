// Wave Editor level settings (not per wave): the blast door ("gate_*" keys) and the Demo / tutorial flags ("demo" object).
// Pure logic, no React: tests in levelSettings.test.ts (`npm test`). The game reads these in LevelJsonRules.cpp
// (gate_open_mode, gate_progress_per_second, gate_max_progress_before_final, gate_full_open_after_wave; ParseDemoSettings).
// Round-trip safety: every function returns a NEW level object that keeps all other keys (including an untouched "demo").

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

/** All level-settings errors of a level (gate + demo + cold); used to block the save. */
export function validateLevelSettings(level: LevelLike): string[] {
  const waves = Array.isArray(level.waves) ? level.waves.length : 0;
  return [...validateGate(getGate(level), waves), ...validateDemo(getDemo(level)), ...validateColdRate(getColdRateMult(level))];
}
