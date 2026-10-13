import React from 'react';
import { DoorClosed, FlaskConical, AlertTriangle, Snowflake } from 'lucide-react';
import {
  COLD_RATE_HINT, COLD_RATE_MAX, COLD_RATE_MIN, DEMO_DEFAULT_STEPS, DEMO_HINTS, GATE_FIELDS, GATE_MODE_OPTIONS, GateOpenMode, LevelLike, Tri,
  getColdRateMult, getDemo, getGate, isGateFieldInvalid, setColdRateMult, setDemoEnabled, setDemoKey, setGateKey, validateColdRate,
  validateDemo, validateGate,
} from '../utils/levelSettings';

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
