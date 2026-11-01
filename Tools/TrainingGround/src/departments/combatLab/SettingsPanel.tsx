// Training Ground «Боевая лаборатория», part A: the level JSON blocks "pressure" / "cheats" of L_FormationLabCombat.json - every field with
// its default and range, validated by the same rules as the C++ parser (src/combatLab/pressureSettings.ts), named presets applied with one
// click, saved through /api/combat-lab/settings (a backup + the diff of what changed).
import React, { useMemo, useState } from 'react';
import { AlertTriangle, CheckCircle2, RotateCcw, Save, Star } from 'lucide-react';
import type { CombatLabPresetFile, CombatLabSettings, PressureField, SettingChange } from '../../combatLab/pressureSettings';
import {
  CHEAT_FIELDS, FIELD_GROUPS, PRESSURE_FIELDS, PRESSURE_TYPES, diffSettings, eventShares, fieldError, fieldMin, fieldValue, getAt, matchingPreset,
  presetSettings, setAt, typeShares, typeWeights, validateSettings,
} from '../../combatLab/pressureSettings';
import { Bars, Card, btnGhost, btnPrimary, inputCls, pct } from './ui';

const show = (v: unknown) => (v === undefined ? '—' : typeof v === 'object' ? JSON.stringify(v) : String(v));

const NumberField: React.FC<{ field: PressureField; pressure: CombatLabSettings['pressure']; onChange: (v: number | boolean | undefined) => void }> = ({ field, pressure, onChange }) => {
  const raw = getAt(pressure, field.path);
  const error = fieldError(pressure, field);
  const min = fieldMin(pressure, field);
  if (field.kind === 'bool') {
    const value = fieldValue(pressure, field) as boolean;
    return (
      <label className="flex items-center gap-2 text-sm text-slate-200" title={field.hint}>
        <input type="checkbox" checked={value} onChange={(e) => onChange(e.target.checked)} className="accent-cyan-500" />
        {field.label}
        {raw === undefined && <span className="text-[11px] text-slate-500">(по умолчанию)</span>}
      </label>
    );
  }
  return (
    <label className="flex flex-col gap-1 text-xs text-slate-400 min-w-0" title={field.hint}>
      <span className="flex items-baseline justify-between gap-2">
        <span className="text-slate-300">{field.label}{field.unit ? `, ${field.unit}` : ''}</span>
        {raw !== undefined && raw !== field.def && <button type="button" className="text-[11px] text-cyan-400 hover:underline" onClick={() => onChange(field.def as number)}>сброс</button>}
      </span>
      <input type="number" className={`${inputCls} ${error ? 'border-rose-500' : ''}`} step={field.step ?? (field.kind === 'integer' ? 1 : 0.1)}
        value={typeof raw === 'number' ? raw : ''} placeholder={String(field.def)}
        onChange={(e) => onChange(e.target.value === '' ? undefined : Number(e.target.value))} />
      <span className={`text-[11px] ${error ? 'text-rose-400' : 'text-slate-500'}`}>
        {error || `по умолчанию ${field.def}; ${min !== undefined ? min : '…'}–${field.max ?? '…'}`}{field.hint && !error ? ` · ${field.hint}` : ''}
      </span>
    </label>
  );
};

interface Props {
  base: CombatLabSettings | null;
  settings: CombatLabSettings | null;
  presets: CombatLabPresetFile | null;
  onChange: (next: CombatLabSettings) => void;
  onSaved: (saved: CombatLabSettings) => void;
  onPresetsChanged: (file: CombatLabPresetFile) => void;
  file: string;
}

export const SettingsPanel: React.FC<Props> = ({ base, settings, presets, onChange, onSaved, onPresetsChanged, file }) => {
  const [advanced, setAdvanced] = useState(false);
  const [message, setMessage] = useState<{ tone: 'ok' | 'bad'; text: string; diff?: SettingChange[]; backup?: string | null } | null>(null);
  const [busy, setBusy] = useState(false);
  const errors = useMemo(() => (settings ? validateSettings(settings) : []), [settings]);
  const pending = useMemo(() => (base && settings ? diffSettings(base, settings) : []), [base, settings]);
  const active = settings ? matchingPreset(presets, settings) : undefined;

  if (!settings) return <Card title="Настройки уровня"><p className="text-sm text-slate-400">Загрузка…</p></Card>;
  const pressure = settings.pressure;
  const set = (path: string, value: number | boolean | undefined) => onChange({ ...settings, pressure: setAt(pressure, path, value) });
  const shares = eventShares(pressure);
  const weights = typeWeights(pressure);
  const tShares = typeShares(pressure);
  const authored = getAt(pressure, 'ambush.points');

  const save = async () => {
    setBusy(true);
    setMessage(null);
    try {
      const r = await fetch('/api/combat-lab/settings', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ settings, base }) });
      const d = await r.json();
      if (!r.ok) setMessage({ tone: 'bad', text: d.error ?? `ошибка ${r.status}`, diff: d.diff });
      else {
        setMessage({ tone: 'ok', text: d.diff.length ? `Сохранено в ${file}` : 'Изменений нет', diff: d.diff, backup: d.backup });
        onSaved(d.settings ?? settings);
      }
    } catch (e) {
      setMessage({ tone: 'bad', text: `Нет связи с сервером: ${(e as Error).message}` });
    }
    setBusy(false);
  };

  const savePreset = async () => {
    const name = window.prompt('Имя пресета (например «Fast patrols»):', '');
    if (!name) return;
    const id = name.trim().toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '').slice(0, 40) || `preset_${Date.now()}`;
    const r = await fetch('/api/combat-lab/presets', { method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ preset: { id, name: name.trim(), description: 'Saved from the Training Ground', pressure, cheats: settings.cheats } }) });
    const d = await r.json();
    if (!r.ok) setMessage({ tone: 'bad', text: d.error ?? 'пресет не сохранён' });
    else { onPresetsChanged(d); setMessage({ tone: 'ok', text: `Пресет «${name.trim()}» сохранён (Content/Data/CombatLab/presets.json)` }); }
  };

  return (
    <div className="flex flex-col gap-4">
      <Card title="Пресеты" hint="Один клик ставит все настройки давления и читов; «Сохранить в уровень» пишет их в файл уровня."
        right={<button className={btnGhost} onClick={savePreset} disabled={errors.length > 0}><Star size={14} />Сохранить как пресет</button>}>
        <div className="flex flex-wrap gap-2">
          {(presets?.presets ?? []).map((p) => (
            <button key={p.id} title={p.description} onClick={() => onChange(presetSettings(p))}
              className={`px-3 py-1.5 rounded-lg text-sm border ${active === p.id ? 'bg-cyan-600 border-cyan-500 text-slate-950 font-semibold' : 'bg-slate-950 border-slate-700 text-slate-200 hover:bg-slate-800'}`}>
              {p.name}
            </button>
          ))}
          {!active && <span className="px-3 py-1.5 text-sm text-amber-300">свои настройки</span>}
        </div>
      </Card>

      <div className="grid gap-4 xl:grid-cols-2">
        <Card title="Доли событий и типы врагов" hint="Что приносит каждое событие: патруль, одиночная засада или скоординированная.">
          <Bars rows={[
            { label: 'Патруль', value: pct(shares.patrol), share: shares.patrol, color: 'bg-sky-400' },
            { label: 'Засада', value: pct(shares.ambush), share: shares.ambush, color: 'bg-orange-500' },
            { label: 'Скоординированная', value: pct(shares.coordinated), share: shares.coordinated, color: 'bg-fuchsia-400' },
          ]} />
          <div className="grid grid-cols-2 sm:grid-cols-5 gap-2">
            {PRESSURE_TYPES.map((t) => {
              const raw = getAt(pressure, `types.${t.key}`);
              return (
                <label key={t.key} className="flex flex-col gap-1 text-xs text-slate-400">
                  <span className="text-slate-300">{t.label}</span>
                  <input type="number" min={0} step={0.5} className={inputCls} value={typeof raw === 'number' ? raw : ''} placeholder={String(weights[t.key])}
                    onChange={(e) => set(`types.${t.key}`, e.target.value === '' ? undefined : Number(e.target.value))} />
                  <span className="text-[11px] text-slate-500">вес · {pct(tShares[t.key])}</span>
                </label>
              );
            })}
          </div>
          <p className="text-[11px] text-slate-500">Громила и крио-дрон давлением не спавнятся никогда. Все веса 0 = все пять поровну.</p>
        </Card>
        <Card title="Читы" hint="Для автотестов: отчёт всё равно считает поглощённый урон и патроны, которые были бы потрачены.">
          {CHEAT_FIELDS.map((c) => (
            <label key={c.key} className="flex items-start gap-2 text-sm text-slate-200">
              <input type="checkbox" className="accent-cyan-500 mt-1" checked={settings.cheats[c.key]}
                onChange={(e) => onChange({ ...settings, cheats: { ...settings.cheats, [c.key]: e.target.checked } })} />
              <span>{c.label}<span className="block text-[11px] text-slate-500">{c.hint}</span></span>
            </label>
          ))}
          <p className="text-xs text-slate-400">Точек засады дизайнера: {Array.isArray(authored) ? authored.length : 0} (их правка — в редакторе карты, следующий этап).</p>
        </Card>
      </div>

      <div className="flex items-center justify-between">
        <span className="text-xs text-slate-500">Поля без значения берут значение по умолчанию игры (серый текст).</span>
        <label className="flex items-center gap-2 text-sm text-slate-300"><input type="checkbox" className="accent-cyan-500" checked={advanced} onChange={(e) => setAdvanced(e.target.checked)} />Подробнее (все параметры)</label>
      </div>
      <div className="grid gap-4 md:grid-cols-2 2xl:grid-cols-3">
        {FIELD_GROUPS.map((group) => {
          const fields = PRESSURE_FIELDS.filter((f) => f.group === group.id && (advanced || !f.advanced));
          if (fields.length === 0) return null;
          return (
            <Card key={group.id} title={group.title} hint={group.hint}>
              <div className="grid gap-3 sm:grid-cols-2">
                {fields.map((f) => <NumberField key={f.path} field={f} pressure={pressure} onChange={(v) => set(f.path, v)} />)}
              </div>
            </Card>
          );
        })}
      </div>

      <Card title="Сохранение" hint={`Пишет только изменённые значения в ${file}; копия прежнего файла — в Saved/TrainingGround/combat_lab/backups.`}
        right={<div className="flex gap-2">
          <button className={btnGhost} onClick={() => base && onChange(base)} disabled={!pending.length}><RotateCcw size={14} />Сбросить</button>
          <button className={btnPrimary} onClick={save} disabled={busy || errors.length > 0 || !pending.length}><Save size={14} />Сохранить в уровень</button>
        </div>}>
        {errors.length > 0 && (
          <ul className="text-sm text-rose-300 flex flex-col gap-1">
            {errors.map((e) => <li key={e} className="flex gap-2"><AlertTriangle size={14} className="mt-0.5 shrink-0" />{e}</li>)}
          </ul>
        )}
        {pending.length > 0 ? (
          <div className="text-xs">
            <div className="text-slate-400 mb-1">Несохранённые изменения ({pending.length}):</div>
            <ul className="font-mono text-slate-300 flex flex-col gap-0.5 max-h-48 overflow-y-auto">
              {pending.map((d) => <li key={d.path}>{d.path}: <span className="text-rose-300">{show(d.from)}</span> → <span className="text-emerald-300">{show(d.to)}</span></li>)}
            </ul>
          </div>
        ) : <p className="text-xs text-slate-500">Изменений нет: на странице то же, что в файле уровня.</p>}
        {message && (
          <div className={`text-sm rounded-lg border px-3 py-2 ${message.tone === 'ok' ? 'border-emerald-800 bg-emerald-950/40 text-emerald-200' : 'border-rose-800 bg-rose-950/40 text-rose-200'}`}>
            <div className="flex items-center gap-2">{message.tone === 'ok' ? <CheckCircle2 size={15} /> : <AlertTriangle size={15} />}{message.text}</div>
            {message.backup && <div className="text-xs text-slate-400 mt-1">Резервная копия: {message.backup}</div>}
            {message.diff && message.diff.length > 0 && (
              <ul className="font-mono text-xs mt-1 text-slate-300">
                {message.diff.map((d) => <li key={d.path}>{d.path}: {show(d.from)} → {show(d.to)}</li>)}
              </ul>
            )}
          </div>
        )}
      </Card>
    </div>
  );
};
