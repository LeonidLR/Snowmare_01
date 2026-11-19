// Training Ground department «Генератор уровней» -> «Управление картой» (task T-020): the weather section of the CURRENT map.
// Reads and writes ONLY the `environment` block and `cold_rate_mult` of Content/Data/LevelJson/<Map>.json through the dev server
// (server/levelgenApi.ts -> src/levelgen/node/levelEnvironment.ts). Nothing is written until the designer presses «Сохранить погоду».
// The controls are the Wave Editor's own weather panel (components/LevelSettingsPanels.tsx), moved here from the «Волны» tab.
import React, { useCallback, useEffect, useMemo, useState } from 'react';
import { CheckCircle2, AlertTriangle, ChevronDown, ChevronRight, RotateCcw, Save } from 'lucide-react';
import { LevelColdPanel, LevelEnvironmentPanel } from '../components/LevelSettingsPanels';
import { getColdRateMult, getEnvironment, validateColdRate, validateEnvironment, WEATHER_STRENGTH_DEFAULT } from '../utils/levelSettings';
import type { LevelLike } from '../utils/levelSettings';
import { jsonEqual } from '../utils/jsonTextPatch';

interface Props {
  /** The current map (null: none). */
  map: string | null;
  /** The map has a level JSON (the section is read-only without it). */
  hasLevelJson: boolean;
  onDirtyChange?: (dirty: boolean) => void;
}

const OPEN_KEY = 'tg.mapWeather.open';

/** One line with the values the section holds (shown while it is folded, so the layout stays at the top of the page). */
export function weatherSummary(level: LevelLike | null): string {
  if (!level) return '';
  const env = getEnvironment(level);
  const parts = [
    `погода: ${env.weather ?? 'по умолчанию'}${env.climate === 'temperate' ? ' (умеренный)' : ''}`,
    `час: ${env.start_hour ?? 'авто'}`,
    env.locked ? 'зафиксирована' : 'живая лестница',
    `сила: ${env.weather_strength ?? WEATHER_STRENGTH_DEFAULT}`,
    `холод: ×${getColdRateMult(level)}`,
  ];
  return parts.join(' · ');
}

/** What the server file holds, as the editable "level" the panels work on ({ environment?, cold_rate_mult? }). */
const toLevel = (d: { environment: Record<string, unknown> | null; cold_rate_mult: number | null }): LevelLike => ({
  ...(d.environment ? { environment: d.environment } : {}),
  ...(d.cold_rate_mult !== null ? { cold_rate_mult: d.cold_rate_mult } : {}),
});

export const MapWeatherSection: React.FC<Props> = ({ map, hasLevelJson, onDirtyChange }) => {
  const [file, setFile] = useState('');
  const [saved, setSaved] = useState<LevelLike | null>(null);
  const [level, setLevel] = useState<LevelLike | null>(null);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const [busy, setBusy] = useState(false);
  const [open, setOpen] = useState<boolean>(() => { try { return localStorage.getItem(OPEN_KEY) === '1'; } catch { return false; } });
  const toggle = () => setOpen((o) => { try { localStorage.setItem(OPEN_KEY, o ? '0' : '1'); } catch { /* per-viewer convenience only */ } return !o; });

  const load = useCallback(async (name: string) => {
    setBusy(true);
    setMsg(null);
    try {
      const r = await fetch(`/api/levelgen/map-environment?map=${encodeURIComponent(name)}`);
      const d = await r.json();
      if (!r.ok) throw new Error(d.error ?? r.statusText);
      const lv = toLevel(d);
      setFile(d.file);
      setSaved(lv);
      setLevel(lv);
    } catch (e) {
      setSaved(null);
      setLevel(null);
      setMsg({ ok: false, text: e instanceof Error ? e.message : String(e) });
    }
    setBusy(false);
  }, []);

  useEffect(() => {
    setSaved(null);
    setLevel(null);
    setMsg(null);
    if (map && hasLevelJson) void load(map);
  }, [map, hasLevelJson, load]);

  const dirty = useMemo(() => !!saved && !!level && !jsonEqual(saved, level), [saved, level]);
  useEffect(() => { onDirtyChange?.(dirty); }, [dirty, onDirtyChange]);
  const errors = useMemo(() => (level ? [...validateEnvironment(getEnvironment(level)), ...validateColdRate(getColdRateMult(level))] : []), [level]);

  const save = async () => {
    if (!map || !level || errors.length > 0) return;
    setBusy(true);
    setMsg(null);
    try {
      const environment = level.environment && Object.keys(level.environment).length > 0 ? level.environment : null;
      const r = await fetch('/api/levelgen/map-environment', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ map, environment, cold_rate_mult: typeof level.cold_rate_mult === 'number' ? level.cold_rate_mult : null }),
      });
      const d = await r.json();
      if (!r.ok) throw new Error(d.error ?? r.statusText);
      await load(map);
      setMsg({ ok: true, text: d.changed ? `Сохранено в ${d.file}: погода и холод карты записаны, остальные ключи файла не тронуты.` : `${d.file}: без изменений, файл не тронут.` });
    } catch (e) {
      setMsg({ ok: false, text: e instanceof Error ? e.message : String(e) });
    }
    setBusy(false);
  };

  if (!map) return null;
  return (
    <section className="bg-slate-900 border border-slate-800 rounded-2xl p-4 flex flex-col gap-3" data-testid="map-weather-section">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <button type="button" onClick={toggle} className="text-left flex items-start gap-2" aria-expanded={open} data-testid="weather-toggle">
          {open ? <ChevronDown size={15} className="mt-0.5 text-slate-400" /> : <ChevronRight size={15} className="mt-0.5 text-slate-400" />}
          <span>
            <span className="block text-xs font-bold tracking-widest uppercase text-slate-400">Погода и холод карты</span>
            <span className="block text-[11px] text-slate-500 mt-0.5">
              Карта <span className="font-mono text-cyan-300">{map}</span>
              {file && <> · файл <span className="font-mono">Content/Data/LevelJson/{file}</span> (пишутся только блок environment и cold_rate_mult)</>}
            </span>
            {!open && level && <span className="block text-[11px] text-slate-300 mt-1 font-mono" data-testid="weather-summary">{weatherSummary(level)}</span>}
          </span>
        </button>
        <div className="flex items-center gap-2">
          {dirty && <span className="px-2 py-0.5 rounded text-[11px] bg-amber-950/80 border border-amber-800 text-amber-300">Есть несохранённые правки</span>}
          <button
            onClick={() => saved && setLevel(saved)}
            disabled={!dirty || busy}
            className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-slate-800 hover:bg-slate-700 border border-slate-700 text-xs text-slate-200 disabled:opacity-40"
          >
            <RotateCcw size={13} />Отменить правки
          </button>
          <button
            onClick={save}
            disabled={!dirty || busy || errors.length > 0}
            data-testid="weather-save"
            className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-emerald-600 hover:bg-emerald-500 text-slate-950 font-semibold text-xs disabled:opacity-40"
          >
            <Save size={13} />Сохранить погоду
          </button>
        </div>
      </div>

      {!hasLevelJson && (
        <p className="text-xs text-amber-300">
          У карты нет level JSON (Content/Data/LevelJson/{map}.json): погоду записывать некуда. Он создаётся при сборке карты в Unreal.
        </p>
      )}
      {msg && (
        <div className={`flex items-center gap-2 p-2.5 rounded-lg border text-xs ${msg.ok ? 'bg-emerald-950/80 border-emerald-800 text-emerald-300' : 'bg-rose-950/80 border-rose-800 text-rose-300'}`} data-testid="weather-msg">
          {msg.ok ? <CheckCircle2 size={14} /> : <AlertTriangle size={14} />}
          <span>{msg.text}</span>
        </div>
      )}

      {open && level && (
        <div className="grid gap-4 xl:grid-cols-[minmax(0,2fr)_minmax(0,1fr)]">
          <LevelEnvironmentPanel config={level} onChange={(next) => setLevel(next)} />
          <div className="flex flex-col gap-2">
            {/* waveCount is not used by the cold panel */}
            <LevelColdPanel config={level} waveCount={0} onChange={(next) => setLevel(next)} />
            {saved && !saved.environment && (
              <p className="text-[11px] text-slate-500">В файле карты нет блока environment: игра берёт погоду и время суток по умолчанию (живая лестница погоды). Значения ниже появятся в файле после «Сохранить погоду».</p>
            )}
          </div>
        </div>
      )}
    </section>
  );
};
