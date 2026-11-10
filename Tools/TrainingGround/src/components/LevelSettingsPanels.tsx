import React from 'react';
import { DoorClosed, FlaskConical, AlertTriangle, Snowflake, Skull, CloudSun } from 'lucide-react';
import {
  COLD_RATE_HINT, COLD_RATE_MAX, COLD_RATE_MIN, DEMO_DEFAULT_STEPS, DEMO_HINTS, GATE_FIELDS, GATE_MODE_OPTIONS, GateOpenMode, LevelLike, Tri,
  getColdRateMult, getDemo, getGate, isGateFieldInvalid, setColdRateMult, setDemoEnabled, setDemoKey, setGateKey, validateColdRate,
  validateDemo, validateGate,
  HORDE_AFTER_FIREFIGHTS_MIN, getHordeAfterFirefights, getHordeEnabled, setHordeAfterFirefights, setHordeEnabled, validateHordeAfterFirefights,
  HORDE_SPAWN_DISTANCE_DEFAULT, HORDE_SPAWN_DISTANCE_MAX, HORDE_SPAWN_DISTANCE_MIN, HordeWaveFeed, describeHordeWaves, getHordeSpawnDistance,
  getHordeWaveFeed, hasOwnHordeSpawnDistance, setHordeSpawnDistance, validateHordeSpawnDistance,
  getEnvironment, setEnvironmentKey, validateEnvironment, ARCTIC_WEATHER_OPTIONS, TEMPERATE_WEATHER_OPTIONS, CLIMATE_OPTIONS,
} from '../utils/levelSettings';

/** Game designer 2026-11-09: what the waves of this level do for the firefight horde (HordeRules::ShouldWavesFeedHorde). */
const HORDE_FEED_TEXT: Record<HordeWaveFeed, string> = {
  waves: 'combat_start "ambush": волны этого уровня НЕ играются как скриптовые — они по порядку и есть орды после перестрелок (орда 1 = волна 1, орда 2 = волна 2, орда 3 = волна 3, дальше повторяется последняя). Действуют max_simultaneous_enemies (сколько живых одновременно, остальные подходят по мере гибели), wave_modifiers и custom_stats; spawn_delay_sec / initial_delay_sec не используются.',
  map: 'combat_start "auto": если на карте есть патрули / расставленные враги, это засадный уровень — волны кормят орду по порядку (как у "ambush"); иначе волны — скриптовые бои, а орда берёт count / composition из horde.json.',
  scripted: 'combat_start "button": волны — скриптовые бои уровня; орда после перестрелок берёт count / composition из horde.json (или блока horde уровня).',
  off: 'В блоке horde уровня waves_feed_horde: false — орда всегда берёт count / composition из horde.json / блока horde.',
  none: 'Активных волн нет — орда берёт count / composition из horde.json (или блока horde уровня).',
};

// Level-wide settings (not per wave): «Бластовая дверь» (gate_* keys), «Демо / туториал» (the "demo" object) and
// «Холод уровня» (cold_rate_mult).
// Both edit the level JSON in place (App state -> /api/save-stage); keys the panels do not touch, and an untouched
// "demo" object, are saved as they were loaded. Rules and validation: utils/levelSettings.ts.

interface PanelProps {
  config: LevelLike;
  waveCount: number;
  onChange: (next: any) => void;
}

const fieldCls = (invalid: boolean) =>
  `bg-slate-800 border rounded px-2 py-1 text-xs font-semibold text-slate-100 focus:outline-none focus:border-emerald-500 disabled:opacity-40 ${
    invalid ? 'border-rose-500' : 'border-slate-700'
  }`;

const Errors: React.FC<{ errors: string[] }> = ({ errors }) =>
  errors.length === 0 ? null : (
    <div className="mt-2 flex items-start gap-1.5 text-[11px] text-rose-300">
      <AlertTriangle className="w-3.5 h-3.5 mt-0.5 shrink-0" />
      <span>{errors.join('; ')}</span>
    </div>
  );

export const BlastDoorPanel: React.FC<PanelProps> = ({ config, waveCount, onChange }) => {
  const gate = getGate(config);
  const errors = validateGate(gate, waveCount);
  const instant = gate.gate_open_mode === 'instant';
  return (
    <div className="bg-slate-900 border border-slate-800 rounded-xl p-4" title="Level JSON keys gate_open_mode, gate_progress_per_second, gate_max_progress_before_final, gate_full_open_after_wave">
      <div className="flex items-center gap-2 mb-3">
        <DoorClosed className="w-4 h-4 text-amber-400" />
        <h3 className="text-sm font-bold text-slate-100">Бластовая дверь</h3>
        <span className="text-[11px] text-slate-500">настройки уровня, не волны</span>
      </div>
      <div className="flex flex-wrap items-end gap-4 text-xs">
        <label className="flex flex-col gap-1" title={GATE_MODE_OPTIONS.find((o) => o.value === gate.gate_open_mode)?.hint}>
          <span className="text-slate-500">Режим</span>
          <select
            value={gate.gate_open_mode}
            onChange={(e) => {
              const mode = e.target.value as GateOpenMode;
              // The default mode is written as an absent key only when the file never had one.
              onChange(setGateKey(config, 'gate_open_mode', mode));
            }}
            className={fieldCls(false)}
          >
            {GATE_MODE_OPTIONS.map((o) => (
              <option key={o.value} value={o.value} title={o.hint}>{o.label}</option>
            ))}
          </select>
        </label>
        {GATE_FIELDS.map((field) => {
          const value = gate[field.key];
          const invalid = !instant && isGateFieldInvalid(field.key, gate, waveCount);
          return (
            <label key={field.key} className="flex flex-col gap-1" title={field.hint}>
              <span className="text-slate-500">{field.label} <span className="text-slate-600">({field.unit})</span></span>
              <input
                type="number"
                min={field.min}
                max={field.key === 'gate_full_open_after_wave' ? Math.max(1, waveCount) : field.max}
                step={field.step}
                disabled={instant}
                value={Number.isNaN(value) ? '' : value}
                onChange={(e) => {
                  const text = e.target.value;
                  onChange(setGateKey(config, field.key, text === '' ? NaN : Number(text)) as LevelLike);
                }}
                className={`${fieldCls(invalid)} w-28`}
              />
            </label>
          );
        })}
      </div>
      {instant ? (
        <p className="mt-2 text-[11px] text-slate-500">Режим «Сразу»: терминал открывает дверь мгновенно, остальные поля не используются.</p>
      ) : (
        <p className="mt-2 text-[11px] text-slate-500">
          Дверь визуально закрыта до конца последней волны (открытие по прогрессу не видно); вращающийся маяк работает, пока генератор жив.
        </p>
      )}
      <Errors errors={instant ? [] : errors} />
    </div>
  );
};

const TRI_LABELS: Record<Tri, string> = { default: 'как в Blueprint', on: 'включены', off: 'выключены' };

export const DemoPanel: React.FC<PanelProps> = ({ config, onChange }) => {
  const demo = getDemo(config);
  const errors = validateDemo(demo);
  const set = (key: string, value: boolean | string | undefined) => onChange(setDemoKey(config, key, value));
  return (
    <div className="bg-slate-900 border border-slate-800 rounded-xl p-4" title='Level JSON object "demo" (FDemoModeSettings, UDemoModeSubsystem)'>
      <div className="flex items-center gap-2 mb-3">
        <FlaskConical className="w-4 h-4 text-violet-400" />
        <h3 className="text-sm font-bold text-slate-100">Демо / туториал</h3>
        <label className="ml-2 flex items-center gap-1.5 text-xs text-slate-300" title={DEMO_HINTS.enabled}>
          <input type="checkbox" checked={demo.enabled} onChange={(e) => onChange(setDemoEnabled(config, e.target.checked))} />
          демо-уровень
        </label>
      </div>
      {demo.enabled && (
        <div className="flex flex-wrap items-end gap-4 text-xs">
          <label className="flex items-center gap-1.5 text-slate-300" title={DEMO_HINTS.panic}>
            <input type="checkbox" checked={demo.panic} onChange={(e) => set('panic', e.target.checked)} />
            паника
          </label>
          <label className="flex flex-col gap-1" title={DEMO_HINTS.vision_cones}>
            <span className="text-slate-500">Конусы зрения</span>
            <select
              value={demo.vision_cones}
              onChange={(e) => {
                const v = e.target.value as Tri;
                set('vision_cones', v === 'default' ? undefined : v === 'on');
              }}
              className={fieldCls(false)}
            >
              {(Object.keys(TRI_LABELS) as Tri[]).map((t) => (
                <option key={t} value={t}>{TRI_LABELS[t]}</option>
              ))}
            </select>
          </label>
          <label className="flex items-center gap-1.5 text-slate-300" title={DEMO_HINTS.tutorial}>
            <input type="checkbox" checked={demo.tutorial} onChange={(e) => set('tutorial', e.target.checked)} />
            туториал
          </label>
          <label className="flex flex-col gap-1" title={DEMO_HINTS.tutorial_steps}>
            <span className="text-slate-500">Файл шагов</span>
            <input
              type="text"
              value={demo.tutorial_steps}
              disabled={!demo.tutorial}
              onChange={(e) => set('tutorial_steps', e.target.value === DEMO_DEFAULT_STEPS ? undefined : e.target.value)}
              className={`${fieldCls(errors.length > 0)} w-52`}
            />
          </label>
          <label className="flex flex-col gap-1" title={DEMO_HINTS.hud}>
            <span className="text-slate-500">HUD</span>
            <select value={demo.hud} onChange={(e) => set('hud', e.target.value === 'demo' ? 'demo' : undefined)} className={fieldCls(false)}>
              <option value="default">как обычно</option>
              <option value="demo">демо (без отладочных панелей)</option>
            </select>
          </label>
          <label className="flex items-center gap-1.5 text-slate-300" title={DEMO_HINTS.intro_dialogue}>
            <input type="checkbox" checked={demo.intro_dialogue} onChange={(e) => set('intro_dialogue', e.target.checked)} />
            вступительный брифинг (окно)
          </label>
        </div>
      )}
      <Errors errors={errors} />
    </div>
  );
};

export const LevelColdPanel: React.FC<PanelProps> = ({ config, onChange }) => {
  const mult = getColdRateMult(config);
  const errors = validateColdRate(mult);
  const percent = Number.isFinite(mult) ? Math.round((mult - 1) * 100) : 0;
  return (
    <div className="bg-slate-900 border border-slate-800 rounded-xl p-4" title={COLD_RATE_HINT}>
      <div className="flex items-center gap-2 mb-3">
        <Snowflake className="w-4 h-4 text-sky-400" />
        <h3 className="text-sm font-bold text-slate-100">Холод уровня</h3>
        <span className="text-[11px] text-slate-500">cold_rate_mult, весь уровень (разведка и бой)</span>
      </div>
      <div className="flex flex-wrap items-end gap-4 text-xs">
        <label className="flex flex-col gap-1" title={COLD_RATE_HINT}>
          <span className="text-slate-500">Множитель накопления холода <span className="text-slate-600">(×)</span></span>
          <input
            type="number"
            min={COLD_RATE_MIN}
            max={COLD_RATE_MAX}
            step={0.05}
            value={Number.isNaN(mult) ? '' : mult}
            onChange={(e) => onChange(setColdRateMult(config, e.target.value === '' ? undefined : Number(e.target.value)))}
            className={`${fieldCls(errors.length > 0)} w-28`}
          />
        </label>
        <span className="text-slate-400 pb-1">
          {percent === 0 ? 'как обычно' : percent < 0 ? `на ${-percent} % меньше холода` : `на ${percent} % больше холода`}
        </span>
      </div>
      <p className="mt-2 text-[11px] text-slate-500">
        Умножается на cold_drain_mult каждой волны и на зоны камеры. Обогрев, разогрев бегом и скриптовые порывы ветра туториала не масштабируются.
      </p>
      <Errors errors={errors} />
    </div>
  );
};


export const LevelHordePanel: React.FC<PanelProps> = ({ config, onChange }) => {
  const enabled = getHordeEnabled(config);
  const firefights = getHordeAfterFirefights(config);
  const errors = [
    ...(config.horde_after_firefights !== undefined ? validateHordeAfterFirefights(config.horde_after_firefights) : []),
    ...(hasOwnHordeSpawnDistance(config) ? validateHordeSpawnDistance(getHordeSpawnDistance(config)) : []),
  ];
  const distance = getHordeSpawnDistance(config);
  const feed = getHordeWaveFeed(config);
  const waveLines = feed === 'waves' || feed === 'map' ? describeHordeWaves(config) : [];

  return (
    <div className="bg-slate-900 border border-slate-800 rounded-xl p-4" title="Level JSON keys horde_enabled, horde_after_firefights">
      <div className="flex items-center gap-2 mb-3">
        <Skull className="w-4 h-4 text-rose-400" />
        <h3 className="text-sm font-bold text-slate-100">Орда уровня</h3>
        <span className="text-[11px] text-slate-500">horde_enabled, horde_after_firefights</span>
      </div>
      <div className="flex flex-wrap items-end gap-4 text-xs">
        <label className="flex items-center gap-1.5 text-slate-300 cursor-pointer">
          <input
            type="checkbox"
            checked={enabled}
            onChange={(e) => onChange(setHordeEnabled(config, e.target.checked))}
            className="accent-rose-500 w-4 h-4"
          />
          Орда включена
        </label>
        <label className="flex flex-col gap-1" title="Порог вызова орды: орда приходит после N перестрелок подряд (по умолчанию 10, мин. 1; подсказка штаба за одну до порога)">
          <span className="text-slate-500">Horde after N firefights <span className="text-slate-600">(боёв)</span></span>
          <input
            type="number"
            min={HORDE_AFTER_FIREFIGHTS_MIN}
            step={1}
            disabled={!enabled}
            value={Number.isNaN(firefights) ? '' : firefights}
            onChange={(e) => {
              const val = e.target.value;
              onChange(setHordeAfterFirefights(config, val === '' ? undefined : Number(val)));
            }}
            className={`${fieldCls(config.horde_after_firefights !== undefined && validateHordeAfterFirefights(config.horde_after_firefights).length > 0)} w-28`}
          />
        </label>
        <label className="flex flex-col gap-1" title={`Орда после перестрелок появляется так далеко от центра отряда (+- 40 м), в новом месте каждый раз (направление не ближе 90 градусов к прошлому), вне поля зрения отряда. Карта меньше — берётся самая дальняя достижимая точка вне поля зрения. Пусто = horde.json (${HORDE_SPAWN_DISTANCE_DEFAULT} м). Ключ уровня: horde.firefight_spawn_distance_m`}>
          <span className="text-slate-500">Дистанция орды <span className="text-slate-600">(м)</span></span>
          <input
            type="number"
            min={HORDE_SPAWN_DISTANCE_MIN}
            max={HORDE_SPAWN_DISTANCE_MAX}
            step={10}
            disabled={!enabled}
            value={hasOwnHordeSpawnDistance(config) ? distance : ''}
            placeholder={String(HORDE_SPAWN_DISTANCE_DEFAULT)}
            onChange={(e) => {
              const val = e.target.value;
              onChange(setHordeSpawnDistance(config, val === '' ? undefined : Number(val)));
            }}
            className={`${fieldCls(hasOwnHordeSpawnDistance(config) && validateHordeSpawnDistance(distance).length > 0)} w-28`}
          />
        </label>
      </div>
      <div className="mt-2 rounded border border-rose-900/60 bg-rose-950/30 px-2 py-1.5 text-[11px] text-rose-200" title="HordeRules::ShouldWavesFeedHorde">
        <div className="font-semibold">Волны → орда</div>
        <div className="text-rose-200/80">{HORDE_FEED_TEXT[feed]}</div>
        {waveLines.length > 0 && (
          <ul className="mt-1 list-disc pl-4 text-rose-100/90">
            {waveLines.map((line) => <li key={line}>{line}</li>)}
          </ul>
        )}
        <div className="mt-1 text-rose-200/70">Орда появляется ~{distance} м от отряда (+- 40 м) в новом месте каждый раз; если карта меньше — в самой дальней достижимой точке вне поля зрения. SPITTER в орде не бывает — его заменяет MILITARY_ZOMBIE.</div>
      </div>
      <p className="mt-2 text-[11px] text-slate-500">
        UE: орда после N перестрелок ПОДРЯД (horde_after_firefights, default 10, min 1) — единственный вызов орды с 2026-11-09 (таймера боя больше нет); проскользнуть мимо группы врагов незамеченным — счёт с нуля; horde_enabled: false — никогда (Депо: вкл., Озеро: выкл.).
      </p>
      <Errors errors={errors} />
    </div>
  );
};

export const LevelEnvironmentPanel: React.FC<{ config: LevelLike; onChange: (next: any) => void }> = ({ config, onChange }) => {
  const env = getEnvironment(config);
  const errors = validateEnvironment(env);
  const isTemperate = env.climate === 'temperate';
  const weatherOptions = isTemperate ? TEMPERATE_WEATHER_OPTIONS : ARCTIC_WEATHER_OPTIONS;

  return (
    <div className="bg-slate-900 border border-slate-800 rounded-xl p-4" title="Level JSON object 'environment' (FEnvironmentLevelSettings)">
      <div className="flex items-center gap-2 mb-3">
        <CloudSun className="w-4 h-4 text-amber-300" />
        <h3 className="text-sm font-bold text-slate-100">Погода и время суток уровня</h3>
        <span className="text-[11px] text-slate-500">environment</span>
      </div>

      {/* Флаги фиксации и приоритета */}
      <div className="flex flex-wrap items-center gap-4 mb-3 text-xs">
        <label className="flex items-center gap-1.5 text-slate-200 cursor-pointer" title="locked = погода и стрелки часов заморожены на весь уровень; на зафиксированном уровне погода не меняет холод">
          <input
            type="checkbox"
            checked={env.locked === true}
            onChange={(e) => onChange(setEnvironmentKey(config, 'locked', e.target.checked))}
            className="accent-amber-400 w-4 h-4"
          />
          <span className="font-semibold">Зафиксировать погоду и часы (locked)</span>
        </label>

        <label className="flex items-center gap-1.5 text-amber-200 cursor-pointer" title="Погода уровня важнее перенесённой (override_carry): при переходе из кампании погода и время старта уровня имеют приоритет над перенесённым состоянием (даже если уровень не зафиксирован)">
          <input
            type="checkbox"
            checked={env.override_carry === true}
            onChange={(e) => onChange(setEnvironmentKey(config, 'override_carry', e.target.checked))}
            className="accent-amber-500 w-4 h-4"
          />
          <span className="font-semibold">Погода уровня важнее перенесённой</span>
        </label>

        <label className="flex items-center gap-1.5 text-slate-400 cursor-pointer" title="visuals: false отключает презентер погоды (остаётся авторское освещение карты)">
          <input
            type="checkbox"
            checked={env.visuals !== false}
            onChange={(e) => onChange(setEnvironmentKey(config, 'visuals', e.target.checked ? undefined : false))}
            className="accent-slate-500 w-4 h-4"
          />
          <span>Визуализатор погоды</span>
        </label>
      </div>

      {/* Селекторы параметров */}
      <div className="flex flex-wrap items-end gap-4 text-xs">
        <label className="flex flex-col gap-1">
          <span className="text-slate-500">Климат (climate)</span>
          <select
            value={env.climate || 'arctic'}
            onChange={(e) => {
              const val = e.target.value as 'arctic' | 'temperate';
              let nextConf = setEnvironmentKey(config, 'climate', val === 'arctic' ? undefined : val);
              // If switched to arctic and weather was temperate-only, reset weather to Overcast
              if (val === 'arctic' && env.weather && !['Clear', 'Overcast', 'Snowfall', 'Blizzard'].includes(env.weather)) {
                nextConf = setEnvironmentKey(nextConf, 'weather', 'Overcast');
              }
              onChange(nextConf);
            }}
            className={fieldCls(false)}
          >
            {CLIMATE_OPTIONS.map((c) => (
              <option key={c.value} value={c.value}>{c.label}</option>
            ))}
          </select>
        </label>

        <label className="flex flex-col gap-1">
          <span className="text-slate-500">Погода на старте (weather)</span>
          <select
            value={env.weather || ''}
            onChange={(e) => onChange(setEnvironmentKey(config, 'weather', (e.target.value || undefined) as any))}
            className={fieldCls(false)}
          >
            <option value="">По умолчанию (Overcast)</option>
            {weatherOptions.map((w) => (
              <option key={w.value} value={w.value}>{w.label}</option>
            ))}
          </select>
        </label>

        <label className="flex flex-col gap-1" title="start_hour: час старта уровня (0..24), например 23 (Ночь) или 12 (День)">
          <span className="text-slate-500">Время суток <span className="text-slate-600">(0..24 ч)</span></span>
          <input
            type="number"
            min={0}
            max={24}
            step={0.5}
            placeholder="по умолчанию"
            value={env.start_hour !== undefined ? env.start_hour : ''}
            onChange={(e) => onChange(setEnvironmentKey(config, 'start_hour', e.target.value === '' ? undefined : Number(e.target.value)))}
            className={`${fieldCls(env.start_hour !== undefined && (env.start_hour < 0 || env.start_hour > 24))} w-28`}
          />
        </label>

        <label className="flex flex-col gap-1" title="snow_cover: стартовый снежный покров 0..1">
          <span className="text-slate-500">Снег <span className="text-slate-600">(0..1)</span></span>
          <input
            type="number"
            min={0}
            max={1}
            step={0.05}
            placeholder="по умолчанию"
            value={env.snow_cover !== undefined ? env.snow_cover : ''}
            onChange={(e) => onChange(setEnvironmentKey(config, 'snow_cover', e.target.value === '' ? undefined : Number(e.target.value)))}
            className={`${fieldCls(env.snow_cover !== undefined && (env.snow_cover < 0 || env.snow_cover > 1))} w-24`}
          />
        </label>

        {isTemperate && (
          <>
            <label className="flex flex-col gap-1" title="wetness: намокание 0..1 (для умеренного климата)">
              <span className="text-slate-500">Влажность <span className="text-slate-600">(0..1)</span></span>
              <input
                type="number"
                min={0}
                max={1}
                step={0.05}
                placeholder="0"
                value={env.wetness !== undefined ? env.wetness : ''}
                onChange={(e) => onChange(setEnvironmentKey(config, 'wetness', e.target.value === '' ? undefined : Number(e.target.value)))}
                className={`${fieldCls(env.wetness !== undefined && (env.wetness < 0 || env.wetness > 1))} w-24`}
              />
            </label>

            <label className="flex flex-col gap-1" title="puddles: лужи 0..1 (для умеренного климата)">
              <span className="text-slate-500">Лужи <span className="text-slate-600">(0..1)</span></span>
              <input
                type="number"
                min={0}
                max={1}
                step={0.05}
                placeholder="0"
                value={env.puddles !== undefined ? env.puddles : ''}
                onChange={(e) => onChange(setEnvironmentKey(config, 'puddles', e.target.value === '' ? undefined : Number(e.target.value)))}
                className={`${fieldCls(env.puddles !== undefined && (env.puddles < 0 || env.puddles > 1))} w-24`}
              />
            </label>
          </>
        )}

        <label className="flex flex-col gap-1" title="exposure_bias: коррекция экспозиции (-4..+4 EV)">
          <span className="text-slate-500">Экспозиция <span className="text-slate-600">(EV)</span></span>
          <input
            type="number"
            min={-4}
            max={4}
            step={0.2}
            placeholder="0"
            value={env.exposure_bias !== undefined ? env.exposure_bias : ''}
            onChange={(e) => onChange(setEnvironmentKey(config, 'exposure_bias', e.target.value === '' ? undefined : Number(e.target.value)))}
            className={`${fieldCls(env.exposure_bias !== undefined && (env.exposure_bias < -4 || env.exposure_bias > 4))} w-20`}
          />
        </label>
      </div>

      <p className="mt-2 text-[11px] text-slate-500">
        locked = погода и стрелки часов заморожены на весь уровень; на зафиксированном уровне погода не меняет холод. Галочка «Погода уровня важнее перенесённой» заставляет уровень запускать указанную погоду и час даже при переходе отряда из кампании.
      </p>
      <Errors errors={errors} />
    </div>
  );
};
