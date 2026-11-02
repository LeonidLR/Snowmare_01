// Training Ground department «Боевая лаборатория» (game designer 2026-11-01, docs/design/COMBAT_LAB.md section 7), part A: the level JSON
// blocks "pressure" and "cheats" of L_FormationLabCombat.json. Pure (no DOM / Node APIs; tests: pressureSettings.test.ts).
//
// The field table mirrors the C++ parser LevelJsonRules::ParsePressureSettings (Source/CodexTactics/Private/Data/LevelJsonRules.cpp) and
// the defaults of FCombatPressureConfig / FAmbushSpotParams / FAmbushPlanParams / FAmbushRelocateParams. The page REJECTS what the C++ would
// clamp: `min` is never below the C++ floor (cppClamp 'min' / 'both': the C++ raises a smaller value to it), `max` never above the C++
// ceiling (cppClamp 'max' / 'both'), integers are integers (the C++ rounds), `minFrom` is the C++ "at least the other + offset" rule.
// `npm run golden` exports the table (golden/combatlab_pressure_fields.json) and the C++ test CodexTactics.LevelJson.PressureFieldsGolden
// checks every default, min and max against the parser - so the two cannot drift apart.

export type Json = null | boolean | number | string | Json[] | { [key: string]: Json };
export type JsonObject = { [key: string]: Json };

export const isJsonObject = (v: unknown): v is JsonObject => typeof v === 'object' && v !== null && !Array.isArray(v);

export type FieldKind = 'number' | 'integer' | 'bool';
/** Where the C++ parser clamps this field: the page's min / max are exactly those bounds there. */
export type CppClamp = 'none' | 'min' | 'max' | 'both';

export interface PressureField {
  /** Dot path inside "pressure" (e.g. "ambush.coordinated.share"). */
  path: string;
  group: FieldGroupId;
  label: string;
  hint?: string;
  unit?: string;
  kind: FieldKind;
  /** The C++ default (the value of an absent key). */
  def: number | boolean;
  min?: number;
  max?: number;
  /** value >= (the other field's value) + offset (the C++ FMath::Max(other + offset, value)). */
  minFrom?: { path: string; offset: number };
  cppClamp?: CppClamp;
  step?: number;
  /** Shown under «Подробнее» (the designer's list first). */
  advanced?: boolean;
}

export type FieldGroupId = 'schedule' | 'groups' | 'shares' | 'ambush' | 'spots' | 'coordinated' | 'relocate' | 'patrol' | 'recycle';

export const FIELD_GROUPS: { id: FieldGroupId; title: string; hint: string }[] = [
  { id: 'schedule', title: 'Расписание', hint: 'Когда приходят группы: первая и дальше через интервал (часы стоят в пошаговом бою).' },
  { id: 'groups', title: 'Группы и лимиты', hint: 'Сколько врагов в группе и сколько живых одновременно.' },
  { id: 'shares', title: 'Доли событий', hint: 'Патруль или засада; часть засад — скоординированные (фланги, затем тыл и фронт).' },
  { id: 'ambush', title: 'Засады', hint: 'Где прячутся и когда бьют: конус взгляда отряда, дальность удара.' },
  { id: 'spots', title: 'Места засад', hint: 'Концы стен, проёмы, точки дизайнера (AmbushSpotRules).' },
  { id: 'coordinated', title: 'Скоординированные засады', hint: 'План из 4–6 тел: фланги блокируют, стрелки подавляют, затем тыл и фронт.' },
  { id: 'relocate', title: 'Перенос засад', hint: 'Обойдённая засада бежит вперёд по скрытому маршруту (вне взгляда отряда, вышки тоже); заметили по пути — бьёт сразу; скрытого маршрута нет — остаётся, потом идёт патрулём за отрядом. Без телепорта.' },
  { id: 'patrol', title: 'Патрули', hint: 'Петля патруля вокруг точки появления.' },
  { id: 'recycle', title: 'Уборка', hint: 'Брошенные далеко позади враги убираются, освобождая место под лимитом.' },
];

/** The table (order = the page's order). */
export const PRESSURE_FIELDS: PressureField[] = [
  { path: 'enabled', group: 'schedule', label: 'Давление включено', kind: 'bool', def: true, hint: 'false — карта без врагов (как Codex.Pressure.Enabled 0)' },
  { path: 'first_event_sec', group: 'schedule', label: 'Первая группа', unit: 'с', kind: 'number', def: 30, min: 0, max: 1800, cppClamp: 'min', step: 5 },
  { path: 'interval_sec', group: 'schedule', label: 'Интервал между группами', unit: 'с', kind: 'number', def: 120, min: 5, max: 1800, cppClamp: 'min', step: 5 },
  { path: 'seed', group: 'schedule', label: 'Сид', hint: 'одинаковый сид = одинаковые события; бот берёт сид + номер забега', kind: 'integer', def: 1, min: 0, max: 999999, cppClamp: 'none' },

  { path: 'group_min', group: 'groups', label: 'Группа: минимум', kind: 'integer', def: 1, min: 1, max: 12, cppClamp: 'min' },
  { path: 'group_max', group: 'groups', label: 'Группа: максимум', kind: 'integer', def: 3, max: 12, minFrom: { path: 'group_min', offset: 0 }, cppClamp: 'min' },
  { path: 'max_alive', group: 'groups', label: 'Лимит живых', hint: 'обычные события ждут, пока лимит заполнен', kind: 'integer', def: 4, min: 1, max: 30, cppClamp: 'min' },
  { path: 'ambush.coordinated.max_alive', group: 'groups', label: 'Лимит живых для скоординированных', hint: 'свой лимит плана (живые + его тела)', kind: 'integer', def: 6, min: 4, max: 30, cppClamp: 'min' },

  { path: 'patrol_share', group: 'shares', label: 'Доля патрулей', hint: 'остальное — засады', kind: 'number', def: 0.6, min: 0, max: 1, cppClamp: 'both', step: 0.05 },
  { path: 'ambush.coordinated.share', group: 'shares', label: 'Доля скоординированных среди засад', kind: 'number', def: 0.35, min: 0, max: 1, cppClamp: 'both', step: 0.05 },

  { path: 'ambush.watch_half_angle_deg', group: 'ambush', label: 'Конус взгляда (половина)', hint: 'засада бьёт, когда ни один взгляд отряда не накрывает её ± этот угол', unit: '°', kind: 'number', def: 45, min: 0, max: 180, cppClamp: 'both', step: 5 },
  { path: 'ambush.watch_range_cm', group: 'ambush', label: 'Дальность взгляда', unit: 'см', kind: 'number', def: 5000, min: 0, max: 20000, cppClamp: 'min', step: 100, advanced: true },
  { path: 'ambush.strike_range_cm', group: 'ambush', label: 'Дальность удара засады', unit: 'см', kind: 'number', def: 1800, min: 100, max: 10000, cppClamp: 'min', step: 100 },
  { path: 'ambush.hold_strike_min_sec', group: 'ambush', label: 'Удар после удержания: от', hint: 'засада, на которую всё время смотрят, всё равно бьёт через случайные от..до секунд удержания', unit: 'с', kind: 'number', def: 60, min: 0, max: 1800, cppClamp: 'min', step: 5 },
  { path: 'ambush.hold_strike_max_sec', group: 'ambush', label: 'Удар после удержания: до', hint: '0 (и «от» 0) — не бьёт никогда', unit: 'с', kind: 'number', def: 90, max: 1800, minFrom: { path: 'ambush.hold_strike_min_sec', offset: 0 }, cppClamp: 'min', step: 5 },
  { path: 'ambush.min_squad_distance_cm', group: 'ambush', label: 'Засада от отряда: от', unit: 'см', kind: 'number', def: 1400, min: 0, max: 20000, cppClamp: 'min', step: 100, advanced: true },
  { path: 'ambush.max_squad_distance_cm', group: 'ambush', label: 'Засада от отряда: до', unit: 'см', kind: 'number', def: 3000, max: 20000, minFrom: { path: 'ambush.min_squad_distance_cm', offset: 100 }, cppClamp: 'min', step: 100, advanced: true },
  { path: 'wall_clearance_cm', group: 'ambush', label: 'Отступ спавна от стен', hint: 'любой точки появления', unit: 'см', kind: 'number', def: 250, min: 0, max: 2000, cppClamp: 'min', step: 10, advanced: true },

  { path: 'ambush.spots.wall_standoff_cm', group: 'spots', label: 'Отступ места засады от стены', hint: '80 — засада прижимается к стене (правило 2,5 м остаётся для патрулей)', unit: 'см', kind: 'number', def: 80, min: 50, max: 2000, cppClamp: 'min', step: 10 },
  { path: 'ambush.spots.end_inset_cm', group: 'spots', label: 'Отступ от конца стены', unit: 'см', kind: 'number', def: 100, min: 0, max: 2000, cppClamp: 'min', step: 10, advanced: true },
  { path: 'ambush.spots.min_wall_length_cm', group: 'spots', label: 'Минимальная длина стены', unit: 'см', kind: 'number', def: 150, min: 0, max: 5000, cppClamp: 'min', step: 10, advanced: true },
  { path: 'ambush.spots.joined_gap_cm', group: 'spots', label: 'Зазор «стены сходятся»', unit: 'см', kind: 'number', def: 30, min: 1, max: 500, cppClamp: 'min', step: 5, advanced: true },
  { path: 'ambush.spots.doorway_min_cm', group: 'spots', label: 'Проём: от', unit: 'см', kind: 'number', def: 80, min: 0, max: 5000, cppClamp: 'min', step: 10, advanced: true },
  { path: 'ambush.spots.doorway_max_cm', group: 'spots', label: 'Проём: до', unit: 'см', kind: 'number', def: 600, max: 5000, minFrom: { path: 'ambush.spots.doorway_min_cm', offset: 0 }, cppClamp: 'min', step: 10, advanced: true },
  { path: 'ambush.spots.authored_bonus', group: 'spots', label: 'Бонус точки дизайнера', kind: 'number', def: 0.5, min: -5, max: 5, cppClamp: 'none', step: 0.05, advanced: true },
  { path: 'ambush.spots.doorway_bonus', group: 'spots', label: 'Бонус проёма', kind: 'number', def: 0.25, min: -5, max: 5, cppClamp: 'none', step: 0.05, advanced: true },
  { path: 'ambush.spots.merge_cm', group: 'spots', label: 'Слияние близких мест', unit: 'см', kind: 'number', def: 100, min: 0, max: 2000, cppClamp: 'min', step: 10, advanced: true },

  { path: 'ambush.coordinated.assault_min_sec', group: 'coordinated', label: 'Штурм после флангов: не раньше', unit: 'с', kind: 'number', def: 1.5, min: 0, max: 60, cppClamp: 'min', step: 0.5 },
  { path: 'ambush.coordinated.assault_max_sec', group: 'coordinated', label: 'Штурм после флангов: не позже', unit: 'с', kind: 'number', def: 4, max: 120, minFrom: { path: 'ambush.coordinated.assault_min_sec', offset: 0 }, cppClamp: 'min', step: 0.5 },
  { path: 'ambush.coordinated.bodies_min', group: 'coordinated', label: 'Тел в плане: от', kind: 'integer', def: 4, min: 4, max: 12, cppClamp: 'min' },
  { path: 'ambush.coordinated.bodies_max', group: 'coordinated', label: 'Тел в плане: до', kind: 'integer', def: 6, max: 12, minFrom: { path: 'ambush.coordinated.bodies_min', offset: 0 }, cppClamp: 'min' },
  { path: 'ambush.coordinated.min_ranged', group: 'coordinated', label: 'Стрелков в плане не меньше', kind: 'integer', def: 1, min: 0, max: 6, cppClamp: 'both' },
  { path: 'ambush.coordinated.trigger_range_cm', group: 'coordinated', label: 'Дальность срабатывания', unit: 'см', kind: 'number', def: 1800, min: 100, max: 10000, cppClamp: 'min', step: 100, advanced: true },
  { path: 'ambush.coordinated.block_distance_cm', group: 'coordinated', label: 'Фланг «дошёл» ближе', unit: 'см', kind: 'number', def: 900, min: 100, max: 5000, cppClamp: 'min', step: 50, advanced: true },
  { path: 'ambush.coordinated.min_squad_distance_cm', group: 'coordinated', label: 'План от отряда: от', unit: 'см', kind: 'number', def: 1000, min: 0, max: 20000, cppClamp: 'min', step: 100, advanced: true },
  { path: 'ambush.coordinated.max_squad_distance_cm', group: 'coordinated', label: 'План от отряда: до', unit: 'см', kind: 'number', def: 3000, max: 20000, minFrom: { path: 'ambush.coordinated.min_squad_distance_cm', offset: 100 }, cppClamp: 'min', step: 100, advanced: true },
  { path: 'ambush.coordinated.anchor_ahead_cm', group: 'coordinated', label: 'Якорь впереди лидера', unit: 'см', kind: 'number', def: 500, min: -5000, max: 5000, cppClamp: 'none', step: 50, advanced: true },
  { path: 'ambush.coordinated.flank_offset_cm', group: 'coordinated', label: 'Фланги в стороны', unit: 'см', kind: 'number', def: 1500, min: 100, max: 10000, cppClamp: 'min', step: 100, advanced: true },
  { path: 'ambush.coordinated.front_offset_cm', group: 'coordinated', label: 'Фронт вперёд', unit: 'см', kind: 'number', def: 1600, min: 100, max: 10000, cppClamp: 'min', step: 100, advanced: true },
  { path: 'ambush.coordinated.rear_offset_cm', group: 'coordinated', label: 'Тыл назад', unit: 'см', kind: 'number', def: 1500, min: 100, max: 10000, cppClamp: 'min', step: 100, advanced: true },
  { path: 'ambush.coordinated.min_separation_cm', group: 'coordinated', label: 'Партии не ближе друг к другу', unit: 'см', kind: 'number', def: 600, min: 0, max: 5000, cppClamp: 'min', step: 50, advanced: true },

  { path: 'ambush.relocate.enabled', group: 'relocate', label: 'Перенос включён', kind: 'bool', def: true },
  { path: 'ambush.relocate.after_sec', group: 'relocate', label: 'Задержка переноса', unit: 'с', kind: 'number', def: 15, min: 0, max: 600, cppClamp: 'min', step: 1 },
  { path: 'ambush.relocate.far_cm', group: 'relocate', label: 'Дистанция «обойдена»', hint: 'дальше этого от отряда — засада считается обойдённой', unit: 'см', kind: 'number', def: 3500, min: 0, max: 20000, cppClamp: 'min', step: 100 },
  { path: 'ambush.relocate.behind_angle_deg', group: 'relocate', label: 'Угол «позади»', unit: '°', kind: 'number', def: 100, min: 0, max: 180, cppClamp: 'both', step: 5, advanced: true },
  { path: 'ambush.relocate.min_ahead_cos', group: 'relocate', label: 'Новое место впереди (cos)', kind: 'number', def: 0.2, min: -1, max: 1, cppClamp: 'both', step: 0.05, advanced: true },
  { path: 'ambush.relocate.no_route_patrol', group: 'relocate', label: 'Нет скрытого маршрута → патруль за отрядом', hint: 'false — засада только остаётся на месте (до уборки)', kind: 'bool', def: true },
  { path: 'ambush.relocate.no_route_tries', group: 'relocate', label: 'Поисков без маршрута до патруля', kind: 'integer', def: 2, min: 1, max: 20, cppClamp: 'min' },
  { path: 'ambush.relocate.max_route_cm', group: 'relocate', label: 'Самый длинный маршрут', unit: 'см', kind: 'number', def: 10000, min: 500, max: 30000, cppClamp: 'min', step: 100, advanced: true },
  { path: 'ambush.relocate.route_sample_cm', group: 'relocate', label: 'Шаг проверки взгляда по маршруту', unit: 'см', kind: 'number', def: 100, min: 25, max: 1000, cppClamp: 'min', step: 25, advanced: true },
  { path: 'ambush.relocate.run_speed_scale', group: 'relocate', label: 'Темп бега (доля боевой скорости)', kind: 'number', def: 1, min: 0.3, max: 2, cppClamp: 'both', step: 0.05, advanced: true },

  { path: 'min_squad_distance_cm', group: 'patrol', label: 'Патруль от отряда: от', unit: 'см', kind: 'number', def: 2500, min: 0, max: 20000, cppClamp: 'min', step: 100, advanced: true },
  { path: 'max_squad_distance_cm', group: 'patrol', label: 'Патруль от отряда: до', unit: 'см', kind: 'number', def: 5000, max: 20000, minFrom: { path: 'min_squad_distance_cm', offset: 100 }, cppClamp: 'min', step: 100, advanced: true },
  { path: 'patrol.points', group: 'patrol', label: 'Точек в петле', kind: 'integer', def: 4, min: 2, max: 12, cppClamp: 'both', advanced: true },
  { path: 'patrol.radius_cm', group: 'patrol', label: 'Радиус петли', unit: 'см', kind: 'number', def: 1200, min: 200, max: 10000, cppClamp: 'min', step: 100, advanced: true },
  { path: 'patrol.wait_sec', group: 'patrol', label: 'Пауза на точке', unit: 'с', kind: 'number', def: 2, min: 0, max: 60, cppClamp: 'min', step: 0.5, advanced: true },

  { path: 'recycle.distance_cm', group: 'recycle', label: 'Дальше отряда', unit: 'см', kind: 'number', def: 6000, min: 0, max: 30000, cppClamp: 'min', step: 100, advanced: true },
  { path: 'recycle.after_sec', group: 'recycle', label: 'Дольше', unit: 'с', kind: 'number', def: 60, min: 0, max: 1800, cppClamp: 'min', step: 5, advanced: true },
];

/** The five types the pressure spawns (CombatPressureRules::IsAllowedType); "FROST_HOUND" is the C++ alias of HOUND. */
export const PRESSURE_TYPES: { key: string; label: string }[] = [
  { key: 'HOUND', label: 'Гончая' },
  { key: 'FROSTBITTEN', label: 'Промёрзший' },
  { key: 'SPITTER', label: 'Плевун' },
  { key: 'CUTTER', label: 'Резак' },
  { key: 'MARKSMAN', label: 'Стрелок' },
];
const TYPE_ALIASES: Record<string, string> = { HOUND: 'HOUND', FROST_HOUND: 'HOUND', FROSTBITTEN: 'FROSTBITTEN', SPITTER: 'SPITTER', CUTTER: 'CUTTER', MARKSMAN: 'MARKSMAN' };
/** Known to the level parser but never spawned by the pressure (the C++ refuses them in "types"). */
export const FORBIDDEN_TYPES = ['BRUTE', 'CRYO_DRONE'];
export const TYPE_WEIGHT_MAX = 100;

export const CHEAT_FIELDS: { key: 'immortal' | 'infinite_ammo'; label: string; hint: string }[] = [
  { key: 'immortal', label: 'Бессмертие отряда', hint: 'урон не снимает здоровье (нокдаун от тяжёлого удара остаётся); отчёт считает поглощённый урон' },
  { key: 'infinite_ammo', label: 'Бесконечные патроны', hint: 'обойма не пустеет; отчёт считает патроны, которые были бы потрачены' },
];

export interface CombatLabCheats { immortal: boolean; infinite_ammo: boolean }
export interface CombatLabSettings { pressure: JsonObject; cheats: CombatLabCheats }

export const fieldByPath = (path: string): PressureField | undefined => PRESSURE_FIELDS.find((f) => f.path === path);

/** The raw value at a dot path (undefined when absent). */
export function getAt(obj: unknown, path: string): Json | undefined {
  let node: unknown = obj;
  for (const key of path.split('.')) {
    if (!isJsonObject(node) || !(key in node)) return undefined;
    node = node[key];
  }
  return node as Json;
}

/** A copy of obj with the value at path set (undefined removes the key); objects on the way are created / copied, obj is not mutated. */
export function setAt(obj: JsonObject, path: string, value: Json | undefined): JsonObject {
  const [key, ...rest] = path.split('.');
  const out: JsonObject = { ...obj };
  if (rest.length === 0) {
    if (value === undefined) delete out[key];
    else out[key] = value;
    return out;
  }
  const child = isJsonObject(obj[key]) ? (obj[key] as JsonObject) : {};
  out[key] = setAt(child, rest.join('.'), value);
  return out;
}

/** The value in force (the C++ default when the key is absent). */
export function fieldValue(pressure: unknown, field: PressureField): number | boolean {
  const raw = getAt(pressure, field.path);
  if (field.kind === 'bool') return typeof raw === 'boolean' ? raw : (field.def as boolean);
  return typeof raw === 'number' ? raw : (field.def as number);
}

/** The lower bound in force for a field (its own min, or the other field + offset). */
export function fieldMin(pressure: unknown, field: PressureField): number | undefined {
  if (field.minFrom) {
    const other = fieldByPath(field.minFrom.path);
    return other ? (fieldValue(pressure, other) as number) + field.minFrom.offset : undefined;
  }
  return field.min;
}

function fmt(n: number): string {
  return Number.isInteger(n) ? String(n) : String(Math.round(n * 1000) / 1000);
}

/** One field's problem ('' = fine). */
export function fieldError(pressure: unknown, field: PressureField): string {
  const raw = getAt(pressure, field.path);
  if (raw === undefined) return '';
  if (field.kind === 'bool') return typeof raw === 'boolean' ? '' : 'нужно true / false';
  if (typeof raw !== 'number' || !Number.isFinite(raw)) return 'нужно число';
  if (field.kind === 'integer' && !Number.isInteger(raw)) return 'нужно целое';
  const min = fieldMin(pressure, field);
  if (min !== undefined && raw < min) {
    return field.minFrom ? `не меньше ${fmt(min)} («${fieldByPath(field.minFrom.path)?.label}»${field.minFrom.offset ? ` + ${fmt(field.minFrom.offset)}` : ''})` : `не меньше ${fmt(min)}`;
  }
  if (field.max !== undefined && raw > field.max) return `не больше ${fmt(field.max)}`;
  return '';
}

/** The type weights in force: absent -> 1 each (the C++ "empty list = the five equally"). */
export function typeWeights(pressure: unknown): Record<string, number> {
  const types = getAt(pressure, 'types');
  const out: Record<string, number> = {};
  for (const t of PRESSURE_TYPES) out[t.key] = 0;
  if (!isJsonObject(types)) {
    for (const t of PRESSURE_TYPES) out[t.key] = 1;
    return out;
  }
  for (const [key, value] of Object.entries(types)) {
    const canonical = TYPE_ALIASES[key.toUpperCase()];
    if (canonical && typeof value === 'number' && value > 0) out[canonical] += value;
  }
  if (Object.values(out).every((v) => v <= 0)) for (const t of PRESSURE_TYPES) out[t.key] = 1; // all zero = all equal (EffectiveWeights)
  return out;
}

/** The type weights' share of each type 0..1 (what the seeded pick does). */
export function typeShares(pressure: unknown): Record<string, number> {
  const weights = typeWeights(pressure);
  const total = Object.values(weights).reduce((a, b) => a + b, 0);
  return Object.fromEntries(Object.entries(weights).map(([k, v]) => [k, total > 0 ? v / total : 0]));
}

/** Patrol / single ambush / coordinated plan shares of the events (CombatPressureRules::PickEventKind). */
export function eventShares(pressure: unknown): { patrol: number; ambush: number; coordinated: number } {
  const patrol = fieldValue(pressure, fieldByPath('patrol_share')!) as number;
  const coordinated = fieldValue(pressure, fieldByPath('ambush.coordinated.share')!) as number;
  return { patrol, ambush: (1 - patrol) * (1 - coordinated), coordinated: (1 - patrol) * coordinated };
}

/** Every problem of a "pressure" block (empty = the C++ parses it as written, nothing clamped). */
export function validatePressure(pressure: unknown): string[] {
  if (!isJsonObject(pressure)) return ['pressure: должен быть объектом'];
  const errors: string[] = [];
  for (const parent of ['patrol', 'ambush', 'ambush.spots', 'ambush.coordinated', 'ambush.relocate', 'recycle', 'types']) {
    const node = getAt(pressure, parent);
    if (node !== undefined && !isJsonObject(node)) errors.push(`${parent}: должен быть объектом`);
  }
  for (const field of PRESSURE_FIELDS) {
    const problem = fieldError(pressure, field);
    if (problem) errors.push(`${field.label} (${field.path}): ${problem}`);
  }
  const types = getAt(pressure, 'types');
  if (isJsonObject(types)) {
    const seen = new Set<string>();
    for (const [key, value] of Object.entries(types)) {
      const upper = key.toUpperCase();
      const canonical = TYPE_ALIASES[upper];
      if (FORBIDDEN_TYPES.includes(upper)) errors.push(`types.${key}: давление никогда не спавнит ${upper} (только HOUND, FROSTBITTEN, SPITTER, CUTTER, MARKSMAN)`);
      else if (!canonical) errors.push(`types.${key}: неизвестный тип врага`);
      else if (seen.has(canonical)) errors.push(`types.${key}: тип ${canonical} указан дважды`);
      if (canonical) seen.add(canonical);
      if (typeof value !== 'number' || !Number.isFinite(value)) errors.push(`types.${key}: вес — число`);
      else if (value < 0 || value > TYPE_WEIGHT_MAX) errors.push(`types.${key}: вес от 0 до ${TYPE_WEIGHT_MAX}`);
    }
  }
  const points = getAt(pressure, 'ambush.points');
  if (points !== undefined) {
    if (!Array.isArray(points)) errors.push('ambush.points: список [[x, y], ...]');
    else points.forEach((p, i) => {
      if (!Array.isArray(p) || p.length !== 2 || p.some((v) => typeof v !== 'number' || !Number.isFinite(v))) errors.push(`ambush.points[${i}]: точка [x, y] (см мира)`);
    });
  }
  return errors;
}

export function validateCheats(cheats: unknown): string[] {
  if (!isJsonObject(cheats)) return ['cheats: должен быть объектом'];
  const errors: string[] = [];
  for (const f of CHEAT_FIELDS) {
    if (cheats[f.key] !== undefined && typeof cheats[f.key] !== 'boolean') errors.push(`cheats.${f.key}: true / false`);
  }
  return errors;
}

export function validateSettings(settings: unknown): string[] {
  if (!isJsonObject(settings)) return ['ожидается { pressure, cheats }'];
  return [...validatePressure(settings.pressure), ...validateCheats(settings.cheats)];
}

/** The two blocks of a level JSON as the page edits them (absent cheats = off, the C++ default). */
export function pickSettings(level: unknown): CombatLabSettings {
  const pressure = isJsonObject(level) && isJsonObject(level.pressure) ? (level.pressure as JsonObject) : {};
  const rawCheats = isJsonObject(level) && isJsonObject(level.cheats) ? level.cheats : {};
  return { pressure, cheats: { immortal: rawCheats.immortal === true, infinite_ammo: rawCheats.infinite_ammo === true } };
}

/** The level JSON with the two blocks replaced (every other key kept; the input is not mutated). */
export function applySettings<T extends object>(level: T, settings: CombatLabSettings): T {
  const out: Record<string, unknown> = { ...(level as Record<string, unknown>) };
  out.cheats = { ...(isJsonObject(out.cheats) ? out.cheats : {}), immortal: settings.cheats.immortal, infinite_ammo: settings.cheats.infinite_ammo };
  out.pressure = settings.pressure;
  return out as T;
}

export interface SettingChange { path: string; from: Json | undefined; to: Json | undefined }

/** Leaf-by-leaf differences (arrays compared whole), sorted by path - the save's diff. */
export function diffJson(before: unknown, after: unknown, prefix = ''): SettingChange[] {
  if (isJsonObject(before) && isJsonObject(after)) {
    const keys = [...new Set([...Object.keys(before), ...Object.keys(after)])].sort();
    return keys.flatMap((k) => diffJson(before[k], after[k], prefix ? `${prefix}.${k}` : k));
  }
  if (isJsonObject(before) && after === undefined) return diffJson(before, {}, prefix);
  if (before === undefined && isJsonObject(after)) return diffJson({}, after, prefix);
  return JSON.stringify(before) === JSON.stringify(after) ? [] : [{ path: prefix, from: before as Json | undefined, to: after as Json | undefined }];
}

/** The diff of two settings as "pressure.x" / "cheats.y" paths. */
export function diffSettings(before: CombatLabSettings, after: CombatLabSettings): SettingChange[] {
  return [...diffJson(before.pressure, after.pressure, 'pressure'), ...diffJson(before.cheats, after.cheats, 'cheats')];
}

// --- Presets (Content/Data/CombatLab/presets.json, schema codex.combatlab_presets/1) ---------------------------------------------------

export interface CombatLabPreset { id: string; name: string; description?: string; pressure: JsonObject; cheats: CombatLabCheats }
export interface CombatLabPresetFile { schema: string; comment?: string; presets: CombatLabPreset[] }
export const PRESETS_SCHEMA = 'codex.combatlab_presets/1';
export const PRESET_ID = /^[a-z0-9_-]{1,40}$/;

export function validatePreset(preset: unknown): string[] {
  if (!isJsonObject(preset)) return ['пресет: должен быть объектом'];
  const errors: string[] = [];
  if (typeof preset.id !== 'string' || !PRESET_ID.test(preset.id)) errors.push('id: латиница в нижнем регистре, цифры, _ и -');
  if (typeof preset.name !== 'string' || !preset.name.trim()) errors.push('name: нужно имя');
  return [...errors, ...validatePressure(preset.pressure).map((e) => `pressure: ${e}`), ...validateCheats(preset.cheats).map((e) => `cheats: ${e}`)];
}

export function validatePresetFile(file: unknown): string[] {
  if (!isJsonObject(file) || file.schema !== PRESETS_SCHEMA || !Array.isArray(file.presets)) return [`ожидается { schema: "${PRESETS_SCHEMA}", presets: [...] }`];
  const errors: string[] = [];
  const ids = new Set<string>();
  file.presets.forEach((p, i) => {
    for (const e of validatePreset(p)) errors.push(`presets[${i}] ${e}`);
    const id = isJsonObject(p) ? p.id : undefined;
    if (typeof id === 'string') {
      if (ids.has(id)) errors.push(`presets[${i}]: id «${id}» повторяется`);
      ids.add(id);
    }
  });
  return errors;
}

/** The settings a preset sets (deep copies: editing them never touches the preset). */
export function presetSettings(preset: CombatLabPreset): CombatLabSettings {
  return { pressure: JSON.parse(JSON.stringify(preset.pressure)), cheats: { immortal: preset.cheats.immortal === true, infinite_ammo: preset.cheats.infinite_ammo === true } };
}

/** The file with the preset added or replaced (by id). */
export function upsertPreset(file: CombatLabPresetFile, preset: CombatLabPreset): CombatLabPresetFile {
  const presets = file.presets.filter((p) => p.id !== preset.id);
  const at = file.presets.findIndex((p) => p.id === preset.id);
  presets.splice(at >= 0 ? at : presets.length, 0, preset);
  return { ...file, presets };
}

/** The lab map: the default of the combat lab and the map whose own ambush points the shared presets carry. */
export const LAB_MAP_ID = 'L_FormationLabCombat';

/** The id of the preset these settings equal (undefined = custom; `mapId` = the map the settings belong to). */
export function matchingPreset(file: CombatLabPresetFile | null, settings: CombatLabSettings, mapId: string = LAB_MAP_ID): string | undefined {
  // ambush.points are map data: on a map other than the lab (or for a preset saved without points) they are not compared.
  const strip = (pressure: JsonObject, ignore: boolean) => (ignore && getAt(pressure, 'ambush.points') !== undefined ? setAt(pressure, 'ambush.points', undefined) : pressure);
  return file?.presets.find((p) => {
    const ignore = mapId !== LAB_MAP_ID || getAt(p.pressure, 'ambush.points') === undefined;
    return JSON.stringify(sortKeys(strip(p.pressure, ignore))) === JSON.stringify(sortKeys(strip(settings.pressure, ignore)))
      && p.cheats.immortal === settings.cheats.immortal && p.cheats.infinite_ammo === settings.cheats.infinite_ammo;
  })?.id;
}

function sortKeys(value: unknown): unknown {
  if (Array.isArray(value)) return value.map(sortKeys);
  if (isJsonObject(value)) return Object.fromEntries(Object.keys(value).sort().map((k) => [k, sortKeys(value[k])]));
  return value;
}

// --- The golden for the C++ parity test (CodexTactics.LevelJson.PressureFieldsGolden) ----------------------------------------------------

export const FIELDS_GOLDEN_SCHEMA = 'codex.combatlab_fields/1';
export const FIELDS_GOLDEN_FILE = 'combatlab_pressure_fields.json';

/** The table as the C++ test reads it: every field's default, accepted range and where the parser clamps. */
export function buildFieldsGolden(): object {
  return {
    schema: FIELDS_GOLDEN_SCHEMA,
    comment: 'Generated by Tools/TrainingGround (npm run golden) from src/combatLab/pressureSettings.ts PRESSURE_FIELDS - edit there. Checked by CodexTactics.LevelJson.PressureFieldsGolden against LevelJsonRules::ParsePressureSettings.',
    fields: PRESSURE_FIELDS.map((f) => ({
      path: f.path, kind: f.kind, def: f.def,
      ...(f.min !== undefined ? { min: f.min } : {}),
      ...(f.max !== undefined ? { max: f.max } : {}),
      ...(f.minFrom ? { min_from: f.minFrom.path, min_offset: f.minFrom.offset } : {}),
      cpp_clamp: f.cppClamp ?? 'none',
    })),
    types: { allowed: PRESSURE_TYPES.map((t) => t.key), forbidden: FORBIDDEN_TYPES },
  };
}

export function fieldsGoldenText(): string {
  return `${JSON.stringify(buildFieldsGolden(), null, 2)}\n`;
}
