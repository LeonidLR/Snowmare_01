import React, { useEffect, useState } from 'react';
import { Shield, Save, RotateCcw, Loader2, AlertTriangle, CheckCircle2 } from 'lucide-react';
import { SquadROE } from '../types';

// Commander Mode tactical ROE (Sprint 07-E): the 13 parameters of Content/Data/AI/squad_roe.json (GET / POST
// /api/squad-roe). The game reads them at the start of a level (SquadROE); Ctrl + T in the game turns the mode on.

const DEFAULTS: SquadROE = {
  anchor_radius_meters: 7,
  leash_strictness: 'Flexible',
  prefer_high_ground: true,
  open_ground_stance: 'Crouch',
  cover_stance: 'Crouch',
  sniper_reaction: 'DiveToCover',
  target_priority_policy: 'ThreatLevel',
  flank_defense_angle_deg: 75,
  aid_health_threshold_pct: 25,
  require_safe_route_for_aid: true,
  reserve_personal_medkit: true,
  auto_reload_threshold_pct: 25,
  emergency_sidearm_dist_m: 3.5,
};

type NumberField = { kind: 'number'; key: keyof SquadROE; label: string; hint: string; step: number; min: number; max: number };
type BoolField = { kind: 'bool'; key: keyof SquadROE; label: string; hint: string };
type ChoiceField = { kind: 'choice'; key: keyof SquadROE; label: string; hint: string; options: { value: string; label: string }[] };
type Field = NumberField | BoolField | ChoiceField;

const SECTIONS: { title: string; fields: Field[] }[] = [
  {
    title: 'Якорь и поводок',
    fields: [
      { kind: 'number', key: 'anchor_radius_meters', label: 'Радиус якоря, м', hint: 'боец действует только в этом радиусе от точки приказа', step: 0.5, min: 1, max: 30 },
      { kind: 'choice', key: 'leash_strictness', label: 'Строгость поводка', hint: 'гибкий — до 10 м ради перевязки раненого', options: [
        { value: 'Flexible', label: 'Гибкий (до 10 м для помощи)' }, { value: 'Strict', label: 'Строгий' } ] },
      { kind: 'bool', key: 'prefer_high_ground', label: 'Предпочитать высоту', hint: 'укрытие на возвышении (+15 % урона, +25 % дальности)' },
    ],
  },
  {
    title: 'Стойки',
    fields: [
      { kind: 'choice', key: 'open_ground_stance', label: 'Стойка на открытой местности', hint: 'когда рядом нет укрытия', options: [
        { value: 'Crouch', label: 'Присед' }, { value: 'Prone', label: 'Лёжа' }, { value: 'Standing', label: 'Стоя' } ] },
      { kind: 'choice', key: 'cover_stance', label: 'Стойка за баррикадой', hint: 'за укрытием', options: [
        { value: 'Crouch', label: 'Присед' }, { value: 'Standing', label: 'Стоя' } ] },
      { kind: 'choice', key: 'sniper_reaction', label: 'Реакция на снайпера', hint: 'когда лазер стрелка на бойце', options: [
        { value: 'DiveToCover', label: 'В укрытие (лёжа, если укрытия нет)' }, { value: 'DropProne', label: 'Сразу лечь' } ] },
    ],
  },
  {
    title: 'Огонь',
    fields: [
      { kind: 'choice', key: 'target_priority_policy', label: 'Приоритет целей', hint: 'враг в упор (ближе дистанции пистолета) всегда первый', options: [
        { value: 'ThreatLevel', label: 'По угрозе (стрелки, плеватели → гончие → остальные)' }, { value: 'ClosestFirst', label: 'Ближайший' },
        { value: 'LowestHP', label: 'Самый раненый' }, { value: 'AssistLeader', label: 'Цель лидера' } ] },
      { kind: 'number', key: 'flank_defense_angle_deg', label: 'Угол фланга, °', hint: 'враг дальше этого угла от укрытия — смена стороны укрытия', step: 5, min: 0, max: 180 },
      { kind: 'number', key: 'auto_reload_threshold_pct', label: 'Перезарядка ниже, % магазина', hint: 'перезаряжается в укрытии (или когда врагов нет ближе 8 м)', step: 5, min: 0, max: 100 },
      { kind: 'number', key: 'emergency_sidearm_dist_m', label: 'Пистолет в упор, м', hint: 'враг ближе и магазин винтовки пуст — пистолет / дробовик', step: 0.5, min: 0, max: 15 },
    ],
  },
  {
    title: 'Полевая медицина',
    fields: [
      { kind: 'number', key: 'aid_health_threshold_pct', label: 'Помощь раненому ниже, % HP', hint: 'союзник с меньшим здоровьем получает аптечку', step: 5, min: 0, max: 100 },
      { kind: 'bool', key: 'require_safe_route_for_aid', label: 'Только безопасный маршрут', hint: 'нет лазера снайпера и врагов ближе 6 м к раненому' },
      { kind: 'bool', key: 'reserve_personal_medkit', label: 'Беречь свою аптечку', hint: 'при своём HP ниже 50 % последнюю аптечку не отдаёт' },
    ],
  },
];

export const CommanderROEPanel: React.FC = () => {
  const [data, setData] = useState<SquadROE | null>(null);
  const [saved, setSaved] = useState<SquadROE | null>(null);
  const [status, setStatus] = useState<{ kind: 'idle' | 'loading' | 'saving' | 'ok' | 'error'; text?: string }>({ kind: 'loading' });

  useEffect(() => {
    fetch('/api/squad-roe')
      .then(async (res) => {
        const body = await res.json();
        if (!res.ok) throw new Error(body.error || res.statusText);
        const roe = { ...DEFAULTS, ...body } as SquadROE;
        setData(roe);
        setSaved(JSON.parse(JSON.stringify(roe)));
        setStatus({ kind: 'idle' });
      })
      .catch((e) => setStatus({ kind: 'error', text: String(e.message || e) }));
  }, []);

  if (status.kind === 'loading') {
    return (
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-6 text-slate-400 flex items-center gap-2">
        <Loader2 className="w-4 h-4 animate-spin" /> Загрузка тактики отряда…
      </div>
    );
  }
  if (!data) {
    return (
      <div className="bg-slate-900 border border-rose-800 rounded-xl p-6 text-rose-300 flex items-center gap-2">
        <AlertTriangle className="w-4 h-4" /> {status.text}
      </div>
    );
  }

  const dirty = JSON.stringify(data) !== JSON.stringify(saved);
  const setField = (key: keyof SquadROE, value: number | boolean | string) => setData({ ...data, [key]: value } as SquadROE);

  const save = async () => {
    setStatus({ kind: 'saving' });
    try {
      const res = await fetch('/api/squad-roe', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(data) });
      const body = await res.json();
      if (!res.ok) throw new Error(body.error || res.statusText);
      setSaved(JSON.parse(JSON.stringify(data)));
      setStatus({ kind: 'ok', text: 'Сохранено. Игра применит при следующем запуске уровня.' });
    } catch (e: any) {
      setStatus({ kind: 'error', text: String(e.message || e) });
    }
  };

  const input = (f: Field) => {
    const value = data[f.key];
    if (f.kind === 'bool') {
      return (
        <label className="flex items-center gap-2 text-sm text-slate-100 cursor-pointer">
          <input type="checkbox" checked={Boolean(value)} onChange={(e) => setField(f.key, e.target.checked)} className="accent-emerald-500 w-4 h-4" />
          {value ? 'Да' : 'Нет'}
        </label>
      );
    }
    if (f.kind === 'choice') {
      return (
        <select
          value={String(value)}
          onChange={(e) => setField(f.key, e.target.value)}
          className="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1 text-sm font-semibold text-slate-100 focus:outline-none focus:border-emerald-500"
        >
          {f.options.map((o) => (
            <option key={o.value} value={o.value}>{o.label}</option>
          ))}
        </select>
      );
    }
    return (
      <input
        type="number"
        step={f.step}
        min={f.min}
        max={f.max}
        value={Number(value)}
        onChange={(e) => setField(f.key, e.target.value === '' ? 0 : Number(e.target.value))}
        className="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1 text-sm font-semibold text-slate-100 focus:outline-none focus:border-emerald-500"
      />
    );
  };

  return (
    <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-xl">
      <div className="flex flex-wrap items-center justify-between gap-3 pb-4 border-b border-slate-800 mb-4">
        <div className="flex items-center gap-3">
          <div className="p-2 bg-emerald-500/10 text-emerald-400 rounded-lg border border-emerald-500/20">
            <Shield className="w-5 h-5" />
          </div>
          <div>
            <h3 className="text-base font-semibold text-slate-100">Тактика отряда — режим командира</h3>
            <p className="text-xs text-slate-400">squad_roe.json. В игре: Ctrl + T — автономия вкл/выкл, Пробел (тактическая пауза) — ручной контроль</p>
          </div>
        </div>
        <div className="flex items-center gap-2">
          <button
            onClick={() => setData({ ...DEFAULTS })}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-bold bg-slate-800 text-slate-300 hover:bg-slate-700"
          >
            По умолчанию
          </button>
          <button
            onClick={() => saved && setData(JSON.parse(JSON.stringify(saved)))}
            disabled={!dirty}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-bold bg-slate-800 text-slate-300 disabled:opacity-40 hover:bg-slate-700"
          >
            <RotateCcw className="w-3.5 h-3.5" /> Отменить
          </button>
          <button
            onClick={save}
            disabled={!dirty || status.kind === 'saving'}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-bold bg-emerald-500 text-slate-950 disabled:opacity-40 hover:bg-emerald-400"
          >
            {status.kind === 'saving' ? <Loader2 className="w-3.5 h-3.5 animate-spin" /> : <Save className="w-3.5 h-3.5" />} Сохранить
          </button>
        </div>
      </div>

      {status.kind === 'ok' && (
        <div className="mb-3 text-xs text-emerald-300 flex items-center gap-1.5"><CheckCircle2 className="w-3.5 h-3.5" /> {status.text}</div>
      )}
      {status.kind === 'error' && (
        <div className="mb-3 text-xs text-rose-300 flex items-center gap-1.5"><AlertTriangle className="w-3.5 h-3.5" /> {status.text}</div>
      )}

      {SECTIONS.map((section) => (
        <div key={section.title} className="mb-4">
          <div className="text-xs font-bold text-slate-400 uppercase tracking-wide mb-2">{section.title}</div>
          <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-3">
            {section.fields.map((f) => {
              const changed = saved !== null && saved[f.key] !== data[f.key];
              return (
                <div key={f.key} className={`bg-slate-900/80 p-3 rounded-lg border ${changed ? 'border-amber-500/70' : 'border-slate-700/60'}`}>
                  <div className="text-xs font-medium text-slate-300 mb-1">{f.label}</div>
                  {input(f)}
                  <span className="text-[10px] text-slate-500 block mt-1">{f.hint}</span>
                </div>
              );
            })}
          </div>
        </div>
      ))}
    </div>
  );
};
