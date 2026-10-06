import React, { useCallback, useEffect, useMemo, useState } from 'react';
import { Eye, Skull, Map as MapIcon, Terminal, Save, RotateCcw, Loader2, AlertTriangle, CheckCircle2, Info } from 'lucide-react';
import {
  COMBAT_START_OPTIONS, CVARS, CombatStart, HORDE_ARCHETYPES, HORDE_BOOL_FIELDS, HORDE_NUMBER_FIELDS, Json, JsonObject,
  LevelEncounter, LevelEncounterPatch, NumField, PERCEPTION_ARCHETYPES, PERCEPTION_FIELDS, SEARCH_FIELDS, SMELL_ARCHETYPE,
  buildLevelPatch, effectiveHorde, isJsonObject, validateCVarEdits, validateHorde, validateLevelPatch, validatePerception,
} from '../utils/stealthCombat';

// «Скрытность и бой» (Stealth & Combat) tab: enemy perception (enemy_perception.json), horde defaults (horde.json), the
// selected level's combat_start / patrol_search_seconds / horde_enabled / horde override (level JSON) and the Codex.*
// console knobs (stealth ones editable in ai_tuning.json). Endpoints in vite.config.ts; rules in utils/stealthCombat.ts.

type Status = { kind: 'idle' | 'saving' | 'ok' | 'error'; text?: string };
type Toast = { ok: boolean; text: string } | null;

const clone = <T,>(v: T): T => JSON.parse(JSON.stringify(v));
const same = (a: unknown, b: unknown) => JSON.stringify(a) === JSON.stringify(b);

async function apiJson(url: string, init?: RequestInit): Promise<any> {
  const res = await fetch(url, init);
  const body = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(body.error || res.statusText);
  return body;
}
const post = (url: string, payload: unknown) =>
  apiJson(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) });

const inputCls = (changed: boolean, invalid = false) =>
  `w-full bg-slate-800 border rounded px-2 py-1 text-sm font-semibold text-slate-100 placeholder:text-slate-500 placeholder:font-normal focus:outline-none focus:border-emerald-500 disabled:opacity-40 ${
    invalid ? 'border-rose-500' : changed ? 'border-amber-500/80' : 'border-slate-700'
  }`;

const NumInput: React.FC<{
  value: Json | undefined; field: NumField; changed: boolean; disabled?: boolean; placeholder?: string;
  onChange: (v: number | undefined) => void;
}> = ({ value, field, changed, disabled, placeholder, onChange }) => {
  const num = typeof value === 'number' ? value : undefined;
  const invalid = num !== undefined && (!Number.isFinite(num) || num < field.min || num > field.max || (field.integer === true && !Number.isInteger(num)));
  return (
    <input
      type="number"
      step={field.step}
      min={field.min}
      max={field.max}
      disabled={disabled}
      placeholder={placeholder}
      title={`${field.label}, ${field.unit}: ${field.hint} (${field.min}–${field.max})`}
      value={num === undefined || Number.isNaN(num) ? '' : num}
      onChange={(e) => onChange(e.target.value === '' ? (placeholder ? undefined : NaN) : Number(e.target.value))}
      className={inputCls(changed, invalid)}
    />
  );
};

const SectionHeader: React.FC<{
  icon: React.ReactNode; title: string; subtitle: string; dirty: boolean; valid: boolean; status: Status;
  onSave: () => void; onUndo: () => void; extra?: React.ReactNode;
}> = ({ icon, title, subtitle, dirty, valid, status, onSave, onUndo, extra }) => (
  <div className="flex flex-wrap items-center justify-between gap-3 pb-3 border-b border-slate-800 mb-3">
    <div className="flex items-center gap-3">
      <div className="p-2 bg-indigo-500/10 text-indigo-300 rounded-lg border border-indigo-500/20">{icon}</div>
      <div>
        <h3 className="text-base font-semibold text-slate-100">{title}</h3>
        <p className="text-xs text-slate-400">{subtitle}</p>
      </div>
    </div>
    <div className="flex items-center gap-2">
      {extra}
      <button onClick={onUndo} disabled={!dirty}
        className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-bold bg-slate-800 text-slate-300 disabled:opacity-40 hover:bg-slate-700">
        <RotateCcw className="w-3.5 h-3.5" /> Отменить
      </button>
      <button onClick={onSave} disabled={!dirty || !valid || status.kind === 'saving'}
        title={!valid ? 'Исправьте ошибки ввода' : undefined}
        className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-bold bg-indigo-400 text-slate-950 disabled:opacity-40 hover:bg-indigo-300">
        {status.kind === 'saving' ? <Loader2 className="w-3.5 h-3.5 animate-spin" /> : <Save className="w-3.5 h-3.5" />} Сохранить
      </button>
    </div>
  </div>
);

const StatusLine: React.FC<{ status: Status; errors: string[] }> = ({ status, errors }) => (
  <>
    {errors.length > 0 && (
      <div className="mb-3 text-xs text-rose-300 flex items-start gap-1.5">
        <AlertTriangle className="w-3.5 h-3.5 mt-0.5 shrink-0" />
        <span>{errors.slice(0, 4).join(' · ')}{errors.length > 4 ? ` · ещё ${errors.length - 4}` : ''}</span>
      </div>
    )}
    {status.kind === 'ok' && <div className="mb-3 text-xs text-emerald-300 flex items-center gap-1.5"><CheckCircle2 className="w-3.5 h-3.5" /> {status.text}</div>}
    {status.kind === 'error' && <div className="mb-3 text-xs text-rose-300 flex items-center gap-1.5"><AlertTriangle className="w-3.5 h-3.5" /> {status.text}</div>}
  </>
);

/** Horde fields over an effective config; changedFn marks a field as changed (amber), onSet edits one key. */
const HordeFields: React.FC<{
  cfg: JsonObject; changedFn: (key: string) => boolean; onSet: (key: string, value: Json | undefined) => void; disabled?: boolean;
}> = ({ cfg, changedFn, onSet, disabled }) => {
  const composition = isJsonObject(cfg.composition) ? cfg.composition : {};
  const mixKeys = [...HORDE_ARCHETYPES.map((a) => a.key), ...Object.keys(composition).filter((k) => !HORDE_ARCHETYPES.some((a) => a.key === k))];
  const setWeight = (type: string, w: number | undefined) => {
    const next: JsonObject = { ...composition };
    if (w === undefined || w === 0) delete next[type];
    else next[type] = w;
    onSet('composition', next);
  };
  return (
    <div className={disabled ? 'opacity-50 pointer-events-none' : ''}>
      <div className="grid grid-cols-2 sm:grid-cols-4 lg:grid-cols-5 gap-3 mb-3">
        {HORDE_NUMBER_FIELDS.map((f) => (
          <div key={f.key} className="bg-slate-900/80 p-2.5 rounded-lg border border-slate-700/60" title={f.hint}>
            <div className="text-xs font-medium text-slate-300 mb-1">{f.label}, {f.unit}</div>
            <NumInput value={cfg[f.key]} field={f} changed={changedFn(f.key)} onChange={(v) => onSet(f.key, v ?? NaN)} />
            <span className="text-[10px] text-slate-500 block mt-1">{f.hint}</span>
          </div>
        ))}
        <div className="bg-slate-900/80 p-2.5 rounded-lg border border-slate-700/60">
          <div className="text-xs font-medium text-slate-300 mb-1">Звук предупреждения</div>
          <input type="text" value={typeof cfg.warning_sound === 'string' ? cfg.warning_sound : ''} placeholder="/Game/Audio/… (пусто — без звука)"
            onChange={(e) => onSet('warning_sound', e.target.value)} className={inputCls(changedFn('warning_sound'))} />
          <span className="text-[10px] text-slate-500 block mt-1">путь к звуковому ассету, проигрывается один раз</span>
        </div>
      </div>
      <div className="flex flex-wrap gap-4 mb-3">
        {HORDE_BOOL_FIELDS.map((f) => (
          <label key={f.key} title={f.hint} className={`flex items-center gap-2 text-sm cursor-pointer px-2 py-1 rounded border ${changedFn(f.key) ? 'border-amber-500/70' : 'border-transparent'} text-slate-100`}>
            <input type="checkbox" checked={cfg[f.key] === true} onChange={(e) => onSet(f.key, e.target.checked)} className="accent-indigo-400 w-4 h-4" />
            {f.label}
          </label>
        ))}
      </div>
      <div className="text-xs font-bold text-slate-400 uppercase tracking-wide mb-2">Состав орды — веса (доли не обязаны давать 1; 0 — не появляется)</div>
      <div className={`grid grid-cols-2 sm:grid-cols-4 lg:grid-cols-7 gap-2 p-2 rounded-lg border ${changedFn('composition') ? 'border-amber-500/70' : 'border-slate-800'}`}>
        {mixKeys.map((type) => {
          const meta = HORDE_ARCHETYPES.find((a) => a.key === type);
          const w = composition[type];
          return (
            <div key={type}>
              <div className="text-[11px] text-slate-300 mb-0.5" title={type}>{meta ? meta.label : type}</div>
              <NumInput value={typeof w === 'number' ? w : 0} field={{ key: type, label: type, unit: '', hint: 'вес в составе орды', step: 1, min: 0, max: 1000 }}
                changed={false} onChange={(v) => setWeight(type, v === undefined || Number.isNaN(v) ? 0 : v)} />
            </div>
          );
        })}
      </div>
    </div>
  );
};

export const StealthCombatPanel: React.FC<{
  levelId: string;
  levelName: string;
  /** The level's keys were written: mirror them into the editor's in-memory level (so «Сохранить» keeps them). */
  onLevelPatched: (patch: LevelEncounterPatch) => void;
}> = ({ levelId, levelName, onLevelPatched }) => {
  const [loadError, setLoadError] = useState<string | null>(null);
  const [toast, setToast] = useState<Toast>(null);
  const showToast = useCallback((ok: boolean, text: string) => {
    setToast({ ok, text });
    window.setTimeout(() => setToast(null), 4000);
  }, []);

  // --- enemy_perception.json
  const [percSaved, setPercSaved] = useState<JsonObject | null>(null);
  const [perc, setPerc] = useState<JsonObject | null>(null);
  const [percStatus, setPercStatus] = useState<Status>({ kind: 'idle' });
  // --- horde.json
  const [hordeSaved, setHordeSaved] = useState<JsonObject | null>(null);
  const [horde, setHorde] = useState<JsonObject | null>(null);
  const [hordeStatus, setHordeStatus] = useState<Status>({ kind: 'idle' });
  // --- level JSON
  const [levelSaved, setLevelSaved] = useState<LevelEncounter | null>(null);
  const [levelError, setLevelError] = useState<string | null>(null);
  const [combatStart, setCombatStart] = useState<CombatStart>('auto');
  const [searchOverride, setSearchOverride] = useState<number | null>(null);
  const [hordeEnabled, setHordeEnabled] = useState(true);
  const [hordeOverride, setHordeOverride] = useState<JsonObject>({});
  const [levelStatus, setLevelStatus] = useState<Status>({ kind: 'idle' });
  // --- ai_tuning.json cvars
  const [cvarsSaved, setCvarsSaved] = useState<Record<string, string>>({});
  const [cvars, setCvars] = useState<Record<string, string>>({});
  const [cvarStatus, setCvarStatus] = useState<Status>({ kind: 'idle' });

  const cvarStrings = (raw: unknown): Record<string, string> => {
    const out: Record<string, string> = {};
    if (isJsonObject(raw)) for (const [k, v] of Object.entries(raw)) out[k] = String(v);
    return out;
  };

  useEffect(() => {
    Promise.all([apiJson('/api/enemy-perception'), apiJson('/api/horde'), apiJson('/api/ai-tuning')])
      .then(([p, h, t]) => {
        setPercSaved(p); setPerc(clone(p));
        setHordeSaved(h); setHorde(clone(h));
        const c = cvarStrings(t.cvars);
        setCvarsSaved(c); setCvars({ ...c });
      })
      .catch((e) => setLoadError(String(e.message || e)));
  }, []);

  const resetLevelForm = (values: LevelEncounter) => {
    const cs = values.combat_start;
    setCombatStart(cs === 'ambush' || cs === 'button' ? cs : 'auto');
    setSearchOverride(typeof values.patrol_search_seconds === 'number' && values.patrol_search_seconds >= 0 ? values.patrol_search_seconds : null);
    setHordeEnabled(values.horde_enabled !== false);
    setHordeOverride(isJsonObject(values.horde) ? clone(values.horde) : {});
  };

  useEffect(() => {
    setLevelSaved(null);
    setLevelError(null);
    setLevelStatus({ kind: 'idle' });
    apiJson(`/api/level-encounter?id=${encodeURIComponent(levelId)}`)
      .then((r) => { setLevelSaved(r.values); resetLevelForm(r.values); })
      .catch((e) => setLevelError(String(e.message || e)));
  }, [levelId]);

  // ---------------- derived
  const percErrors = useMemo(() => (perc ? validatePerception(perc) : []), [perc]);
  const percDirty = !same(perc, percSaved);
  const hordeErrors = useMemo(() => (horde ? validateHorde(effectiveHorde(horde)) : []), [horde]);
  const hordeDirty = !same(horde, hordeSaved);
  const hordeDefaults = useMemo(() => effectiveHorde(hordeSaved ?? {}), [hordeSaved]);
  const levelHordeEffective = useMemo(() => effectiveHorde(hordeSaved ?? {}, hordeOverride), [hordeSaved, hordeOverride]);
  const levelPatch = useMemo<LevelEncounterPatch>(() => (levelSaved
    ? buildLevelPatch(levelSaved, { combat_start: combatStart, patrol_search_seconds: searchOverride, horde_enabled: hordeEnabled, horde: hordeOverride })
    : {}), [levelSaved, combatStart, searchOverride, hordeEnabled, hordeOverride]);
  const levelErrors = useMemo(() => validateLevelPatch(
    { ...levelPatch, horde: Object.keys(hordeOverride).length > 0 ? hordeOverride : null }, hordeSaved ?? {}), [levelPatch, hordeOverride, hordeSaved]);
  const levelDirty = Object.keys(levelPatch).length > 0;
  const cvarErrors = useMemo(() => validateCVarEdits(Object.fromEntries(CVARS.filter((c) => c.editable).map((c) => [c.name, cvars[c.name] ?? '']))), [cvars]);
  const cvarDirty = CVARS.some((c) => c.editable && (cvars[c.name] ?? '') !== (cvarsSaved[c.name] ?? ''));

  if (loadError) {
    return (
      <div className="bg-slate-900 border border-rose-800 rounded-xl p-6 text-rose-300 flex items-center gap-2">
        <AlertTriangle className="w-4 h-4" /> {loadError} (нужен запущенный dev-сервер: npm run dev)
      </div>
    );
  }
  if (!perc || !horde) {
    return (
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-6 text-slate-400 flex items-center gap-2">
        <Loader2 className="w-4 h-4 animate-spin" /> Загрузка настроек скрытности и боя…
      </div>
    );
  }

  // ---------------- actions
  const runSave = async (setStatus: (s: Status) => void, work: () => Promise<string>) => {
    setStatus({ kind: 'saving' });
    try {
      const text = await work();
      setStatus({ kind: 'ok', text });
      showToast(true, text);
    } catch (e: any) {
      const text = `Ошибка сохранения: ${String(e.message || e)}`;
      setStatus({ kind: 'error', text });
      showToast(false, text);
    }
  };

  const savePerception = () => runSave(setPercStatus, async () => {
    await post('/api/enemy-perception', perc);
    setPercSaved(clone(perc));
    return 'enemy_perception.json сохранён. Игра применит при следующем запуске уровня.';
  });
  const saveHorde = () => runSave(setHordeStatus, async () => {
    await post('/api/horde', horde);
    setHordeSaved(clone(horde));
    return 'horde.json сохранён. Игра применит при следующем запуске уровня.';
  });
  const saveLevel = () => runSave(setLevelStatus, async () => {
    const r = await post('/api/level-encounter', { id: levelId, patch: levelPatch });
    setLevelSaved(r.values);
    resetLevelForm(r.values);
    onLevelPatched(levelPatch);
    return `Уровень ${levelId} сохранён (${(r.written as string[]).join(', ') || 'без изменений'}).`;
  });
  const saveCvars = () => runSave(setCvarStatus, async () => {
    const payload: Record<string, string | null> = {};
    for (const c of CVARS) if (c.editable) payload[c.name] = (cvars[c.name] ?? '').trim() === '' ? null : cvars[c.name];
    const r = await post('/api/ai-tuning', { cvars: payload });
    const c = cvarStrings(r.cvars);
    setCvarsSaved(c); setCvars({ ...c });
    return 'ai_tuning.json сохранён (cvars). Игра применит при следующем запуске.';
  });

  // ---------------- perception editing
  const archetypes = isJsonObject(perc.archetypes) ? perc.archetypes : {};
  const savedArchetypes = percSaved && isJsonObject(percSaved.archetypes) ? percSaved.archetypes : {};
  const search = isJsonObject(perc.search) ? perc.search : {};
  const savedSearch = percSaved && isJsonObject(percSaved.search) ? percSaved.search : {};
  const archetypeCols = [...PERCEPTION_ARCHETYPES, ...Object.keys(archetypes).filter((k) => !PERCEPTION_ARCHETYPES.some((a) => a.key === k)).map((k) => ({ key: k, label: k }))];
  const setPercValue = (type: string, key: string, v: number | undefined) => {
    const next = clone(perc);
    const arch = isJsonObject(next.archetypes) ? next.archetypes : (next.archetypes = {});
    const entry = isJsonObject(arch[type]) ? (arch[type] as JsonObject) : (arch[type] = {});
    if (v === undefined) delete entry[key];
    else entry[key] = v;
    setPerc(next);
  };
  const setSearchValue = (key: string, v: number | undefined) => {
    const next = clone(perc);
    const s = isJsonObject(next.search) ? next.search : (next.search = {});
    if (v === undefined) delete s[key];
    else s[key] = v;
    setPerc(next);
  };

  // ---------------- horde defaults editing
  const setHordeKey = (key: string, value: Json | undefined) => {
    const next = { ...horde };
    if (value === undefined) delete next[key];
    else next[key] = value;
    setHorde(next);
  };

  // ---------------- level horde override editing: only keys the user changed (or that the file already overrides)
  const originalOverride = levelSaved && isJsonObject(levelSaved.horde) ? levelSaved.horde : {};
  const setOverrideKey = (key: string, value: Json | undefined) => {
    const next = { ...hordeOverride };
    if (value === undefined || (same(value, hordeDefaults[key]) && !(key in originalOverride))) delete next[key];
    else next[key] = value;
    setHordeOverride(next);
  };
  const defaultSearchSeconds = typeof savedSearch.duration_seconds === 'number' ? savedSearch.duration_seconds : 60;

  return (
    <div className="flex flex-col gap-6">
      {toast && (
        <div className={`fixed bottom-6 right-6 z-50 px-4 py-2.5 rounded-lg shadow-xl text-sm flex items-center gap-2 border ${
          toast.ok ? 'bg-emerald-950/95 border-emerald-700 text-emerald-200' : 'bg-rose-950/95 border-rose-700 text-rose-200'}`}>
          {toast.ok ? <CheckCircle2 className="w-4 h-4" /> : <AlertTriangle className="w-4 h-4" />} {toast.text}
        </div>
      )}

      {/* 1. Enemy perception */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-xl">
        <SectionHeader icon={<Eye className="w-5 h-5" />} title="Восприятие врагов (патрули, до боя)"
          subtitle="Content/Data/AI/enemy_perception.json — зрение, слух, нюх по типам врагов; пустое поле = встроенное значение игры"
          dirty={percDirty} valid={percErrors.length === 0} status={percStatus} onSave={savePerception}
          onUndo={() => percSaved && setPerc(clone(percSaved))} />
        <StatusLine status={percStatus} errors={percErrors} />
        <div className="overflow-x-auto">
          <table className="w-full text-xs">
            <thead>
              <tr className="text-slate-400">
                <th className="text-left font-semibold py-1 pr-2 min-w-[180px]">Параметр</th>
                {archetypeCols.map((a) => <th key={a.key} className="text-left font-semibold py-1 px-1 min-w-[88px]" title={a.key}>{a.label}</th>)}
              </tr>
            </thead>
            <tbody>
              {PERCEPTION_FIELDS.map((f) => (
                <tr key={f.key} className="border-t border-slate-800/70">
                  <td className="py-1 pr-2 text-slate-300" title={f.hint}>
                    {f.label}, {f.unit}
                    <span className="block text-[10px] text-slate-500">{f.hint}</span>
                  </td>
                  {archetypeCols.map((a) => {
                    const entry = isJsonObject(archetypes[a.key]) ? (archetypes[a.key] as JsonObject) : {};
                    const savedEntry = isJsonObject(savedArchetypes[a.key]) ? (savedArchetypes[a.key] as JsonObject) : {};
                    const noSmell = f.key === 'smell_radius_m' && a.key !== SMELL_ARCHETYPE;
                    return (
                      <td key={a.key} className="py-1 px-1" title={noSmell ? 'нюх есть только у ледяной гончей — игра считает 0' : undefined}>
                        <NumInput value={noSmell ? 0 : entry[f.key]} field={f} disabled={noSmell} placeholder="по умолч."
                          changed={!same(entry[f.key], savedEntry[f.key])} onChange={(v) => setPercValue(a.key, f.key, v)} />
                      </td>
                    );
                  })}
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <div className="text-xs font-bold text-slate-400 uppercase tracking-wide mt-4 mb-2">Поиск после ловушки / шума (search)</div>
        <div className="grid grid-cols-2 sm:grid-cols-4 lg:grid-cols-7 gap-3">
          {SEARCH_FIELDS.map((f) => (
            <div key={f.key} className="bg-slate-900/80 p-2.5 rounded-lg border border-slate-700/60" title={f.hint}>
              <div className="text-xs font-medium text-slate-300 mb-1">{f.label}, {f.unit}</div>
              <NumInput value={search[f.key]} field={f} placeholder="по умолч." changed={!same(search[f.key], savedSearch[f.key])}
                onChange={(v) => setSearchValue(f.key, v)} />
              <span className="text-[10px] text-slate-500 block mt-1">{f.hint}</span>
            </div>
          ))}
        </div>
      </div>

      {/* 2. Horde defaults */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-xl">
        <SectionHeader icon={<Skull className="w-5 h-5" />} title="Орда — значения по умолчанию"
          subtitle="Content/Data/AI/horde.json — орда после долгого боя в реальном времени; уровень может выключить или переопределить"
          dirty={hordeDirty} valid={hordeErrors.length === 0} status={hordeStatus} onSave={saveHorde}
          onUndo={() => hordeSaved && setHorde(clone(hordeSaved))} />
        <StatusLine status={hordeStatus} errors={hordeErrors} />
        <HordeFields cfg={effectiveHorde(horde)} changedFn={(k) => !same(horde[k], hordeSaved?.[k])} onSet={setHordeKey} />
      </div>

      {/* 3. Per-level */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-xl">
        <SectionHeader icon={<MapIcon className="w-5 h-5" />} title={`Уровень: ${levelName || levelId}`}
          subtitle={`Content/Data/LevelJson/${levelId}.json — только ключи combat_start, patrol_search_seconds, horde_enabled, horde; остальное не трогается`}
          dirty={levelDirty} valid={levelErrors.length === 0 && !levelError} status={levelStatus} onSave={saveLevel}
          onUndo={() => levelSaved && resetLevelForm(levelSaved)} />
        {levelError ? (
          <div className="text-xs text-rose-300 flex items-center gap-1.5"><AlertTriangle className="w-3.5 h-3.5" /> {levelError}</div>
        ) : !levelSaved ? (
          <div className="text-xs text-slate-400 flex items-center gap-1.5"><Loader2 className="w-3.5 h-3.5 animate-spin" /> Загрузка уровня…</div>
        ) : (
          <>
            <StatusLine status={levelStatus} errors={levelErrors} />
            <div className="grid grid-cols-1 lg:grid-cols-3 gap-3 mb-4">
              <div className="bg-slate-900/80 p-3 rounded-lg border border-slate-700/60 lg:col-span-2">
                <div className="text-xs font-medium text-slate-300 mb-2">Начало боя (combat_start)</div>
                <div className="flex flex-col gap-1.5">
                  {COMBAT_START_OPTIONS.map((o) => (
                    <label key={o.value} className="flex items-start gap-2 text-sm text-slate-100 cursor-pointer">
                      <input type="radio" name="combat_start" checked={combatStart === o.value} onChange={() => setCombatStart(o.value)} className="accent-indigo-400 mt-1" />
                      <span><b>{o.label}</b> <span className="text-xs text-slate-400">— {o.hint}</span></span>
                    </label>
                  ))}
                </div>
              </div>
              <div className="bg-slate-900/80 p-3 rounded-lg border border-slate-700/60 flex flex-col gap-3">
                <div>
                  <label className="flex items-center gap-2 text-xs font-medium text-slate-300 mb-1 cursor-pointer">
                    <input type="checkbox" checked={searchOverride !== null} className="accent-indigo-400"
                      onChange={(e) => setSearchOverride(e.target.checked ? defaultSearchSeconds : null)} />
                    Своё время поиска патруля, с
                  </label>
                  <NumInput value={searchOverride ?? defaultSearchSeconds} disabled={searchOverride === null}
                    field={{ key: 'patrol_search_seconds', label: 'Поиск патруля', unit: 'с', hint: 'сколько патрули ищут отряд на этом уровне', step: 5, min: 0, max: 600 }}
                    changed={searchOverride !== (levelSaved.patrol_search_seconds ?? null)}
                    onChange={(v) => setSearchOverride(v === undefined ? NaN : v)} />
                  <span className="text-[10px] text-slate-500 block mt-1">выключено — общее значение {defaultSearchSeconds} с из enemy_perception.json</span>
                </div>
                <label className="flex items-center gap-2 text-sm text-slate-100 cursor-pointer" title="false — орды на этом уровне нет">
                  <input type="checkbox" checked={hordeEnabled} onChange={(e) => setHordeEnabled(e.target.checked)} className="accent-indigo-400 w-4 h-4" />
                  Орда на этом уровне (horde_enabled)
                </label>
              </div>
            </div>
            <div className="flex flex-wrap items-center justify-between gap-2 mb-2">
              <div className="text-xs font-bold text-slate-400 uppercase tracking-wide">
                Переопределение орды для уровня (horde) — {Object.keys(hordeOverride).length > 0
                  ? <span className="text-amber-300 normal-case">свои значения: {Object.keys(hordeOverride).join(', ')}</span>
                  : <span className="text-slate-500 normal-case">нет, действуют значения horde.json</span>}
              </div>
              <button onClick={() => setHordeOverride({})} disabled={Object.keys(hordeOverride).length === 0}
                className="px-3 py-1 rounded-lg text-xs font-bold bg-slate-800 text-slate-300 disabled:opacity-40 hover:bg-slate-700">
                Сбросить к умолчаниям
              </button>
            </div>
            <HordeFields cfg={levelHordeEffective} changedFn={(k) => k in hordeOverride} onSet={setOverrideKey} disabled={!hordeEnabled} />
          </>
        )}
      </div>

      {/* 4. Console variables */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-xl">
        <SectionHeader icon={<Terminal className="w-5 h-5" />} title="Консольные переменные Codex.*"
          subtitle="Ручки скрытности пишутся в Content/Data/AI/ai_tuning.json (cvars), игра применяет их при старте; пусто = значение по умолчанию"
          dirty={cvarDirty} valid={cvarErrors.length === 0} status={cvarStatus} onSave={saveCvars}
          onUndo={() => setCvars({ ...cvarsSaved })} />
        <StatusLine status={cvarStatus} errors={cvarErrors} />
        <div className="mb-3 text-[11px] text-slate-400 flex items-start gap-1.5">
          <Info className="w-3.5 h-3.5 mt-0.5 shrink-0" />
          <span>
            Порядок силы: <code>-dpcvars=</code> в командной строке &gt; ai_tuning.json &gt; значение в коде. Запуск с <code>-NoAITuning</code> пропускает файл.
            Внимание: Jev AI-коуч (<code>Scripts/Tools/jev_ai_coach.py</code>) перезаписывает ai_tuning.json целиком — после его прогона проверьте значения здесь.
            Остальные переменные — только справка: задавайте их в консоли игры (~) или через <code>-dpcvars=</code>.
          </span>
        </div>
        <div className="overflow-x-auto">
          <table className="w-full text-xs">
            <thead>
              <tr className="text-slate-400 text-left">
                <th className="py-1 pr-2 font-semibold">Переменная</th>
                <th className="py-1 pr-2 font-semibold">По умолч.</th>
                <th className="py-1 pr-2 font-semibold min-w-[120px]">ai_tuning.json</th>
                <th className="py-1 pr-2 font-semibold">Что делает</th>
                <th className="py-1 font-semibold">Где задана</th>
              </tr>
            </thead>
            <tbody>
              {CVARS.map((c) => {
                const value = cvars[c.name] ?? '';
                const changed = value !== (cvarsSaved[c.name] ?? '');
                const n = Number(value);
                const invalid = value.trim() !== '' && (!Number.isFinite(n) || (c.min !== undefined && n < c.min) || (c.max !== undefined && n > c.max));
                return (
                  <tr key={c.name} className="border-t border-slate-800/70 align-top">
                    <td className="py-1 pr-2 font-mono text-slate-200">{c.name}</td>
                    <td className="py-1 pr-2 text-slate-400">{c.defaultValue}{c.unit ? ` ${c.unit}` : ''}</td>
                    <td className="py-1 pr-2">
                      {c.editable ? (
                        <input type="text" inputMode="decimal" value={value} placeholder={c.defaultValue} title={`${c.min}…${c.max}`}
                          onChange={(e) => setCvars({ ...cvars, [c.name]: e.target.value })} className={inputCls(changed, invalid)} />
                      ) : (
                        <span className="text-slate-500">{cvarsSaved[c.name] ?? '— (консоль)'}</span>
                      )}
                    </td>
                    <td className="py-1 pr-2 text-slate-300">{c.description}</td>
                    <td className="py-1 text-slate-500 font-mono">{c.source}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
        <div className="mt-3 text-[11px] text-slate-500">
          Команды: <code>CodexTactics.DumpPerception</code> — таблица восприятия в лог; <code>CodexTactics.Horde.Release</code> — выпустить орду сейчас.
        </div>
      </div>
    </div>
  );
};
