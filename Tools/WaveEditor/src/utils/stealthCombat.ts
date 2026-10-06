// «Скрытность и бой» (Stealth & Combat) tab: schema, validation and patch rules (pure; tests: stealthCombat.test.ts).
//
// Files (keys exactly as the game reads them):
//   Content/Data/AI/enemy_perception.json — Source/CodexTactics/Private/Data/EnemyPerception.cpp (ParamsFromJson /
//     SearchFromJson; a missing key keeps PerceptionRules::GetArchetypeDefaults, smell counts only for FROST_HOUND).
//   Content/Data/AI/horde.json — Source/CodexTactics/Private/Combat/HordeRules.cpp (HordeRules::ApplyJson).
//   Content/Data/LevelJson/<level>.json — LevelJsonRules.cpp: combat_start, patrol_search_seconds, horde_enabled, horde.
//   Content/Data/AI/ai_tuning.json "cvars" — Data/AITuning.cpp (every "Codex." console variable, string values).

export type Json = null | boolean | number | string | Json[] | { [key: string]: Json };
export type JsonObject = { [key: string]: Json };

export const isJsonObject = (v: unknown): v is JsonObject => typeof v === 'object' && v !== null && !Array.isArray(v);

// ---------------------------------------------------------------------------------------------------------------------
// Enemy perception

export interface NumField {
  key: string;
  label: string;
  unit: string;
  hint: string;
  step: number;
  min: number;
  max: number;
  integer?: boolean;
}

export const PERCEPTION_ARCHETYPES: { key: string; label: string }[] = [
  { key: 'FROST_HOUND', label: 'Ледяная гончая' },
  { key: 'MARKSMAN', label: 'Снайпер (Marksman)' },
  { key: 'SPITTER', label: 'Плеватель' },
  { key: 'BRUTE', label: 'Ледяной громила' },
  { key: 'CUTTER', label: 'Механо-гончая Cutter' },
  { key: 'FROSTBITTEN', label: 'Промёрзший' },
];

/** The only archetype whose smell_radius_m counts (PerceptionRules::CanSmell). */
export const SMELL_ARCHETYPE = 'FROST_HOUND';

export const PERCEPTION_FIELDS: NumField[] = [
  { key: 'sight_range_m', label: 'Дальность зрения', unit: 'м', hint: 'видит стоящего бойца на этом расстоянии', step: 1, min: 0, max: 200 },
  { key: 'sight_half_angle_deg', label: 'Полуугол обзора', unit: '°', hint: 'половина угла поля зрения (90 = полусфера)', step: 5, min: 0, max: 180 },
  { key: 'visibility_standing', label: 'Заметность стоя', unit: '×', hint: 'множитель дальности зрения по стоящему', step: 0.05, min: 0, max: 2 },
  { key: 'visibility_crouching', label: 'Заметность в приседе', unit: '×', hint: 'множитель дальности зрения по присевшему', step: 0.05, min: 0, max: 2 },
  { key: 'visibility_prone', label: 'Заметность лёжа', unit: '×', hint: 'множитель дальности зрения по лежащему', step: 0.05, min: 0, max: 2 },
  { key: 'time_to_detect_seconds', label: 'Время обнаружения', unit: 'с', hint: 'сколько надо смотреть на бойца, чтобы поднять тревогу', step: 0.1, min: 0, max: 30 },
  { key: 'suspicion_decay_per_second', label: 'Спад подозрения', unit: '/с', hint: 'насколько быстро гаснет подозрение без контакта', step: 0.05, min: 0, max: 10 },
  { key: 'proximity_m', label: 'Вплотную', unit: 'м', hint: 'ближе — замечает сразу, в любой стойке и за спиной', step: 0.5, min: 0, max: 20 },
  { key: 'hear_walk_m', label: 'Слух: шаг', unit: 'м', hint: 'слышит идущего бойца', step: 0.5, min: 0, max: 200 },
  { key: 'hear_run_m', label: 'Слух: бег', unit: 'м', hint: 'слышит бегущего бойца', step: 0.5, min: 0, max: 200 },
  { key: 'hear_crouch_walk_m', label: 'Слух: шаг в приседе', unit: 'м', hint: 'слышит крадущегося бойца', step: 0.5, min: 0, max: 200 },
  { key: 'hear_crawl_m', label: 'Слух: ползком', unit: 'м', hint: 'слышит ползущего бойца', step: 0.5, min: 0, max: 200 },
  { key: 'hear_gunshot_m', label: 'Слух: выстрел', unit: 'м', hint: 'слышит выстрел отряда', step: 1, min: 0, max: 300 },
  { key: 'hear_explosion_m', label: 'Слух: граната / взрыв', unit: 'м', hint: 'слышит взрыв гранаты', step: 1, min: 0, max: 300 },
  { key: 'smell_radius_m', label: 'Нюх', unit: 'м', hint: 'чует бойца сквозь стены (только гончая)', step: 0.5, min: 0, max: 100 },
];

export const SEARCH_FIELDS: NumField[] = [
  { key: 'duration_seconds', label: 'Длительность поиска', unit: 'с', hint: 'сколько патруль ищет отряд после ловушки / шума, затем возвращается на маршрут', step: 5, min: 0, max: 600 },
  { key: 'sweep_radius_m', label: 'Радиус прочёсывания', unit: 'м', hint: 'случайные точки обыска в этом радиусе от места тревоги', step: 1, min: 0, max: 100 },
  { key: 'speed_multiplier', label: 'Скорость поиска', unit: '×', hint: 'темп = шаг патруля × это (не выше обычной скорости)', step: 0.1, min: 0, max: 5 },
  { key: 'perception_multiplier', label: 'Восприятие при поиске', unit: '×', hint: 'зрение / слух / нюх × это, пока ищут', step: 0.05, min: 0, max: 5 },
  { key: 'look_around_seconds', label: 'Осмотр точки', unit: 'с', hint: 'сколько осматривается в каждой точке', step: 0.5, min: 0, max: 30 },
  { key: 'arrive_m', label: 'Точка достигнута', unit: 'м', hint: 'на таком расстоянии точка обыска считается пройденной', step: 0.5, min: 0, max: 20 },
  { key: 'leg_timeout_seconds', label: 'Таймаут перехода', unit: 'с', hint: 'не дошёл до точки за это время — следующая точка', step: 1, min: 0, max: 120 },
];

function checkNumber(errors: string[], where: string, value: unknown, field: NumField, required: boolean): void {
  if (value === undefined) {
    if (required) errors.push(`${where}: нет значения`);
    return;
  }
  if (typeof value !== 'number' || !Number.isFinite(value)) {
    errors.push(`${where}: не число`);
    return;
  }
  if (value < field.min) errors.push(`${where}: меньше ${field.min}`);
  if (value > field.max) errors.push(`${where}: больше ${field.max}`);
  if (field.integer && !Number.isInteger(value)) errors.push(`${where}: нужно целое`);
}

/** Problems of an enemy_perception.json document (empty = can be saved). */
export function validatePerception(doc: unknown): string[] {
  const errors: string[] = [];
  if (!isJsonObject(doc)) return ['файл не является JSON-объектом'];
  const search = doc.search;
  if (search !== undefined) {
    if (!isJsonObject(search)) errors.push('search: должен быть объектом');
    else for (const f of SEARCH_FIELDS) checkNumber(errors, `Поиск / ${f.label}`, search[f.key], f, false);
  }
  const archetypes = doc.archetypes;
  if (archetypes !== undefined) {
    if (!isJsonObject(archetypes)) errors.push('archetypes: должен быть объектом');
    else {
      for (const [type, params] of Object.entries(archetypes)) {
        if (!isJsonObject(params)) {
          errors.push(`${type}: должен быть объектом`);
          continue;
        }
        for (const f of PERCEPTION_FIELDS) checkNumber(errors, `${type} / ${f.label}`, params[f.key], f, false);
      }
    }
  }
  return errors;
}

// ---------------------------------------------------------------------------------------------------------------------
// Horde

/** Built-in FHordeConfig (Source/CodexTactics/Public/Combat/HordeRules.h) — what a missing key of horde.json means. */
export const HORDE_BUILTIN: JsonObject = {
  enabled: true,
  trigger_seconds: 240,
  repeats: false,
  repeat_seconds: 120,
  count: 8,
  count_increase_per_repeat: 0,
  composition: { FROST_HOUND: 5, FROSTBITTEN: 3 },
  min_distance_m: 25,
  max_distance_m: 45,
  cluster_radius_m: 4,
  prefer_out_of_sight: true,
  spawn_samples: 32,
  apply_wave_modifiers: true,
  warning_seconds: 6,
  warning_sound: '',
};

/** Level-JSON enemy types the horde can spawn (LevelJsonRules::ParseEnemyType; HOUND is an alias of FROST_HOUND). */
export const HORDE_ARCHETYPES: { key: string; label: string }[] = [
  { key: 'FROST_HOUND', label: 'Ледяная гончая' },
  { key: 'FROSTBITTEN', label: 'Промёрзший' },
  { key: 'BRUTE', label: 'Ледяной громила' },
  { key: 'SPITTER', label: 'Плеватель' },
  { key: 'CUTTER', label: 'Механо-гончая Cutter' },
  { key: 'MARKSMAN', label: 'Снайпер' },
  { key: 'CRYO_DRONE', label: 'Крио-дрон' },
];

export const HORDE_NUMBER_FIELDS: NumField[] = [
  { key: 'trigger_seconds', label: 'Орда через', unit: 'с', hint: 'секунды боя в реальном времени (пауза, пошаговый бой, диалоги не считаются)', step: 10, min: 1, max: 3600 },
  { key: 'repeat_seconds', label: 'Повтор каждые', unit: 'с', hint: 'период следующих орд (если «Повторять» включено)', step: 10, min: 1, max: 3600 },
  { key: 'count', label: 'Размер орды', unit: 'шт', hint: 'врагов в первой орде', step: 1, min: 1, max: 200, integer: true },
  { key: 'count_increase_per_repeat', label: 'Прирост за повтор', unit: 'шт', hint: '+ врагов к каждой следующей орде того же боя', step: 1, min: 0, max: 100, integer: true },
  { key: 'min_distance_m', label: 'Мин. дистанция', unit: 'м', hint: 'ближайшая точка появления от центра отряда', step: 1, min: 0, max: 500 },
  { key: 'max_distance_m', label: 'Макс. дистанция', unit: 'м', hint: 'дальняя граница точки появления', step: 1, min: 0, max: 500 },
  { key: 'cluster_radius_m', label: 'Радиус группы', unit: 'м', hint: 'орда собирается в этом радиусе от точки появления', step: 0.5, min: 0, max: 50 },
  { key: 'spawn_samples', label: 'Проб точек', unit: 'шт', hint: 'случайных кандидатов на навмеше (1–256)', step: 1, min: 1, max: 256, integer: true },
  { key: 'warning_seconds', label: 'Предупреждение', unit: 'с', hint: 'сколько HUD показывает «ОРДА!» и стрелку', step: 1, min: 0, max: 60 },
];

export const HORDE_BOOL_FIELDS: { key: string; label: string; hint: string }[] = [
  { key: 'enabled', label: 'Орда включена', hint: 'глобально; уровень может выключить её флагом horde_enabled' },
  { key: 'repeats', label: 'Повторять', hint: 'после первой орды — новые каждые «Повтор каждые» секунд' },
  { key: 'prefer_out_of_sight', label: 'Вне поля зрения', hint: 'точка, которую не видит ни один боец, важнее видимой' },
  { key: 'apply_wave_modifiers', label: 'Модификаторы волны', hint: 'HP / урон / скорость текущей волны (сложность уровня) действуют и на орду' },
];

/** Problems of a horde config (whole horde.json or a level override merged over it). */
export function validateHorde(cfg: unknown): string[] {
  const errors: string[] = [];
  if (!isJsonObject(cfg)) return ['орда: не JSON-объект'];
  for (const f of HORDE_NUMBER_FIELDS) checkNumber(errors, `Орда / ${f.label}`, cfg[f.key], f, false);
  for (const f of HORDE_BOOL_FIELDS) {
    if (cfg[f.key] !== undefined && typeof cfg[f.key] !== 'boolean') errors.push(`Орда / ${f.label}: нужно да/нет`);
  }
  const min = typeof cfg.min_distance_m === 'number' ? cfg.min_distance_m : (HORDE_BUILTIN.min_distance_m as number);
  const max = typeof cfg.max_distance_m === 'number' ? cfg.max_distance_m : (HORDE_BUILTIN.max_distance_m as number);
  if (min > max) errors.push('Орда: мин. дистанция больше макс.');
  if (cfg.warning_sound !== undefined && typeof cfg.warning_sound !== 'string') errors.push('Орда / звук: нужна строка');
  if (cfg.composition !== undefined) {
    if (!isJsonObject(cfg.composition)) errors.push('Орда / состав: должен быть объектом');
    else {
      let positive = 0;
      for (const [type, w] of Object.entries(cfg.composition)) {
        if (typeof w !== 'number' || !Number.isFinite(w) || w < 0) errors.push(`Орда / состав ${type}: вес должен быть >= 0`);
        else if (w > 0) positive++;
      }
      if (positive === 0) errors.push('Орда / состав: нужен хотя бы один вес > 0');
    }
  }
  return errors;
}

/** Keys of edited whose value differs from base (deep), as a new object — the level "horde" override to write. */
export function diffOverride(base: JsonObject, edited: JsonObject): JsonObject {
  const out: JsonObject = {};
  for (const [k, v] of Object.entries(edited)) {
    if (JSON.stringify(sortKeys(base[k])) !== JSON.stringify(sortKeys(v))) out[k] = v;
  }
  return out;
}

function sortKeys(v: unknown): unknown {
  if (Array.isArray(v)) return v.map(sortKeys);
  if (isJsonObject(v)) return Object.fromEntries(Object.keys(v).sort().map((k) => [k, sortKeys(v[k])]));
  return v;
}

/** Built-in values, then the horde.json defaults, then a level override (the game's ResolveForLevel order). */
export function effectiveHorde(defaults: JsonObject, override?: Json): JsonObject {
  const out: JsonObject = { ...HORDE_BUILTIN, ...stripComment(defaults) };
  if (isJsonObject(override)) Object.assign(out, override);
  return out;
}

function stripComment(o: JsonObject): JsonObject {
  const rest: JsonObject = { ...o };
  delete rest.comment;
  return rest;
}

// ---------------------------------------------------------------------------------------------------------------------
// Per-level settings

export type CombatStart = 'auto' | 'ambush' | 'button';

export const COMBAT_START_OPTIONS: { value: CombatStart; label: string; hint: string }[] = [
  { value: 'auto', label: 'Авто', hint: 'засада, если на карте есть патрули; иначе бой начинается кнопкой' },
  { value: 'ambush', label: 'Засада', hint: 'отряд исследует уровень, бой начинается, когда враги заметят отряд (или отряд откроет огонь)' },
  { value: 'button', label: 'Кнопка', hint: 'классика: подготовка, бой стартует по кнопке / таймеру подготовки' },
];

/** The per-level keys this tab edits, as present in the level JSON (undefined = key absent). */
export interface LevelEncounter {
  combat_start?: string;
  patrol_search_seconds?: number;
  horde_enabled?: boolean;
  horde?: Json;
}

export const LEVEL_ENCOUNTER_KEYS = ['combat_start', 'patrol_search_seconds', 'horde_enabled', 'horde'] as const;

/** A patch: a value sets the key, null removes it, a missing key leaves it alone. */
export type LevelEncounterPatch = { [K in (typeof LEVEL_ENCOUNTER_KEYS)[number]]?: Json };

export function pickLevelEncounter(level: unknown): LevelEncounter {
  const out: LevelEncounter = {};
  if (!isJsonObject(level)) return out;
  if (typeof level.combat_start === 'string') out.combat_start = level.combat_start;
  if (typeof level.patrol_search_seconds === 'number') out.patrol_search_seconds = level.patrol_search_seconds;
  if (typeof level.horde_enabled === 'boolean') out.horde_enabled = level.horde_enabled;
  if (level.horde !== undefined) out.horde = level.horde;
  return out;
}

/** The level object with the patch applied; all other keys (and their order) kept. Unknown patch keys are ignored. */
export function applyLevelEncounterPatch<T extends object>(level: T, patch: LevelEncounterPatch): T {
  const out: Record<string, unknown> = { ...(level as Record<string, unknown>) };
  for (const key of LEVEL_ENCOUNTER_KEYS) {
    if (!Object.prototype.hasOwnProperty.call(patch, key)) continue;
    const v = patch[key];
    if (v === null || v === undefined) delete out[key];
    else out[key] = v;
  }
  return out as T;
}

/** Problems of a level patch (empty = can be saved); a horde override is checked merged over hordeDefaults. */
export function validateLevelPatch(patch: unknown, hordeDefaults: JsonObject = {}): string[] {
  const errors: string[] = [];
  if (!isJsonObject(patch)) return ['патч уровня: не объект'];
  for (const key of Object.keys(patch)) {
    if (!(LEVEL_ENCOUNTER_KEYS as readonly string[]).includes(key)) errors.push(`патч уровня: ключ ${key} не разрешён`);
  }
  const cs = patch.combat_start;
  if (cs !== undefined && cs !== null && !COMBAT_START_OPTIONS.some((o) => o.value === cs)) errors.push('Начало боя: auto | ambush | button');
  const ps = patch.patrol_search_seconds;
  if (ps !== undefined && ps !== null && (typeof ps !== 'number' || !Number.isFinite(ps) || ps < 0 || ps > 600)) {
    errors.push('Поиск патруля: 0–600 с');
  }
  const he = patch.horde_enabled;
  if (he !== undefined && he !== null && typeof he !== 'boolean') errors.push('Орда на уровне: да/нет');
  const h = patch.horde;
  if (h !== undefined && h !== null) {
    if (!isJsonObject(h)) errors.push('Переопределение орды: должно быть объектом');
    else errors.push(...validateHorde(effectiveHorde(hordeDefaults, h)));
  }
  return errors;
}

/**
 * The patch that turns current (what the level file has) into the edited form. Defaults are written as an absent key
 * (the game's default): switching to combat_start "auto", horde_enabled true or an empty search time removes the key.
 */
export function buildLevelPatch(
  current: LevelEncounter,
  edited: { combat_start: CombatStart; patrol_search_seconds: number | null; horde_enabled: boolean; horde: JsonObject | null },
): LevelEncounterPatch {
  const patch: LevelEncounterPatch = {};
  const curStart = current.combat_start ?? 'auto';
  if (edited.combat_start !== curStart) patch.combat_start = edited.combat_start === 'auto' ? null : edited.combat_start;

  if (edited.patrol_search_seconds === null) {
    if (current.patrol_search_seconds !== undefined) patch.patrol_search_seconds = null;
  } else if (edited.patrol_search_seconds !== current.patrol_search_seconds) {
    patch.patrol_search_seconds = edited.patrol_search_seconds;
  }

  const curEnabled = current.horde_enabled ?? true;
  if (edited.horde_enabled !== curEnabled) patch.horde_enabled = edited.horde_enabled ? null : false;

  const editedHorde = edited.horde && Object.keys(edited.horde).length > 0 ? edited.horde : null;
  const curHorde = current.horde === undefined ? null : current.horde;
  if (JSON.stringify(sortKeys(editedHorde)) !== JSON.stringify(sortKeys(curHorde))) patch.horde = editedHorde;
  return patch;
}

// ---------------------------------------------------------------------------------------------------------------------
// Console variables

export interface CVarInfo {
  name: string;
  defaultValue: string;
  unit: string;
  description: string;
  source: string;
  /** Editable here (written to ai_tuning.json "cvars"). */
  editable?: boolean;
  min?: number;
  max?: number;
}

export const CVARS: CVarInfo[] = [
  { name: 'Codex.Perception.SightRangeScale', defaultValue: '1', unit: '×', description: 'множитель дальности зрения всех врагов', source: 'Data/EnemyPerception.cpp', editable: true, min: 0, max: 5 },
  { name: 'Codex.Perception.FovScale', defaultValue: '1', unit: '×', description: 'множитель полуугла обзора (не больше 180°)', source: 'Data/EnemyPerception.cpp', editable: true, min: 0, max: 5 },
  { name: 'Codex.Perception.ProneVisibilityScale', defaultValue: '1', unit: '×', description: 'множитель заметности лежащего бойца', source: 'Data/EnemyPerception.cpp', editable: true, min: 0, max: 5 },
  { name: 'Codex.Perception.HearingScale', defaultValue: '1', unit: '×', description: 'множитель всех радиусов слуха (шаги, выстрелы, гранаты)', source: 'Data/EnemyPerception.cpp', editable: true, min: 0, max: 5 },
  { name: 'Codex.Perception.HearingOcclusion', defaultValue: '-1', unit: '×', description: 'множитель радиуса шагов за каждую стену между врагом и бойцом (до 3 стен; -1 = данные, 0.5)', source: 'Data/EnemyPerception.cpp', editable: true, min: -1, max: 1 },
  { name: 'Codex.Perception.SmellScale', defaultValue: '1', unit: '×', description: 'множитель нюха гончих', source: 'Data/EnemyPerception.cpp', editable: true, min: 0, max: 5 },
  { name: 'Codex.Perception.TimeToDetectScale', defaultValue: '1', unit: '×', description: 'множитель времени обнаружения', source: 'Data/EnemyPerception.cpp', editable: true, min: 0, max: 10 },
  { name: 'Codex.Patrol.SearchSeconds', defaultValue: '-1', unit: 'с', description: 'время поиска патруля (-1 = JSON уровня / enemy_perception.json); главнее уровня', source: 'Data/EnemyPerception.cpp', editable: true, min: -1, max: 600 },
  { name: 'Codex.Patrol.SearchRadius', defaultValue: '-1', unit: 'см', description: 'радиус прочёсывания в САНТИМЕТРАХ (-1 = данные)', source: 'Data/EnemyPerception.cpp', editable: true, min: -1, max: 10000 },
  { name: 'Codex.Patrol.SearchSpeedScale', defaultValue: '1', unit: '×', description: 'множитель скорости поиска', source: 'Data/EnemyPerception.cpp', editable: true, min: 0, max: 5 },
  { name: 'Codex.Horde.TriggerSeconds', defaultValue: '-1', unit: 'с', description: 'секунды боя до первой орды (-1 = horde.json / уровень)', source: 'Combat/HordeSubsystem.cpp' },
  { name: 'Codex.Horde.RepeatSeconds', defaultValue: '-1', unit: 'с', description: '> 0 — включает повтор орд с этим периодом (-1 = данные)', source: 'Combat/HordeSubsystem.cpp' },
  { name: 'Codex.Horde.Enabled', defaultValue: '-1', unit: '', description: '1 вкл, 0 выкл, -1 = данные (horde.json && horde_enabled уровня)', source: 'Combat/HordeSubsystem.cpp' },
  { name: 'Codex.Posture.DefensiveSquadWide', defaultValue: '0', unit: '', description: '1: атака на любого бойца провоцирует всех «Оборонительных»; 0: только атакованного', source: 'Characters/SquadSubsystem.cpp' },
  { name: 'Codex.Posture.AggressiveExplorationFire', defaultValue: '1', unit: '', description: '1: «Агрессивные» сами открывают огонь при исследовании уровня-засады (начинают бой); 0: никогда', source: 'Characters/SquadSubsystem.cpp' },
  { name: 'Codex.RealTimeOrders', defaultValue: '1', unit: '', description: '1: приказы в бою в реальном времени выполняются сразу (RTS); 0: только в тактической паузе', source: 'Core/CodexTacticsPlayerController.cpp' },
];

export const EDITABLE_CVARS = CVARS.filter((c) => c.editable).map((c) => c.name);

/** Problems of the edited cvar values ('' = remove the key = the game default). */
export function validateCVarEdits(edits: Record<string, string>): string[] {
  const errors: string[] = [];
  for (const [name, raw] of Object.entries(edits)) {
    const info = CVARS.find((c) => c.name === name);
    if (!info || !info.editable) {
      errors.push(`${name}: нельзя менять отсюда`);
      continue;
    }
    if (raw.trim() === '') continue;
    const v = Number(raw);
    if (!Number.isFinite(v)) errors.push(`${name}: не число`);
    else if ((info.min !== undefined && v < info.min) || (info.max !== undefined && v > info.max)) errors.push(`${name}: ${info.min}…${info.max}`);
  }
  return errors;
}

/**
 * ai_tuning.json with the whitelisted cvars set ('' or null removes the key); every other key — the coach's result,
 * other Codex.* knobs, unknown keys — is kept. Values are strings, as AITuning::ApplyJson expects.
 */
export function mergeAiTuningCvars(doc: unknown, edits: Record<string, string | null>): JsonObject {
  const out: JsonObject = isJsonObject(doc) ? { ...doc } : {};
  const cvars: JsonObject = isJsonObject(out.cvars) ? { ...out.cvars } : {};
  for (const [name, raw] of Object.entries(edits)) {
    if (!EDITABLE_CVARS.includes(name)) continue;
    if (raw === null || raw.trim() === '') delete cvars[name];
    else cvars[name] = String(Number(raw));
  }
  out.cvars = cvars;
  return out;
}
