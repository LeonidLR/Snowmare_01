import React, { useEffect, useState } from 'react';
import { Crosshair, Bomb, Save, RotateCcw, Loader2, AlertTriangle, CheckCircle2 } from 'lucide-react';
import { WeaponTuningFile, WeaponTuningEntry, GrenadeTuning, TurnBasedRulesTuning } from '../types';

// Tab order and labels (weapon ids = WeaponId of DA_Weapon_*); ids not listed here are appended.
const WEAPON_LABELS: Record<string, string> = {
  pistol: 'Пистолет',
  m16: 'M16',
  shotgun: 'Дробовик',
  plasma_carbine: 'Плазменный карабин',
  flamethrower: 'Огнемёт',
  cryo_emitter: 'Крио-эмиттер',
  knife: 'Нож',
  hammer: 'Молот',
  grenade: 'Граната (пошаговый бой)',
};
const GRENADE_TAB = '__grenade__';

type NumberField = Exclude<keyof WeaponTuningEntry, 'name' | 'base_hit_chances' | 'distance_damage_multipliers'>;

const WEAPON_FIELDS: { key: NumberField; label: string; hint: string; step: number; min: number; max: number }[] = [
  { key: 'base_damage', label: 'Урон', hint: 'базовый урон одного выстрела / удара', step: 1, min: 0, max: 500 },
  { key: 'attack_range_m', label: 'Дальность, м', hint: 'дальность стрельбы в реальном времени (лёжа x1.35, сидя x1.15)', step: 0.5, min: 1, max: 60 },
  { key: 'fire_rate', label: 'Пауза между выстрелами, с', hint: 'меньше — чаще стреляет', step: 0.05, min: 0.05, max: 5 },
  { key: 'armor_penetration', label: 'Пробитие брони', hint: '0 — броня держит полностью, 1 — игнорирует броню', step: 0.05, min: 0, max: 1 },
  { key: 'max_clip_size', label: 'Магазин', hint: 'патронов в магазине', step: 1, min: 1, max: 200 },
  { key: 'default_reserve_ammo', label: 'Запас патронов', hint: 'стартовый запас (если уровень не задаёт свой)', step: 1, min: 0, max: 999 },
  { key: 'reload_time', label: 'Перезарядка, с', hint: 'время перезарядки', step: 0.1, min: 0.1, max: 10 },
  { key: 'status_duration', label: 'Длительность эффекта, с', hint: 'горение / заморозка / шок (если оружие накладывает эффект)', step: 0.5, min: 0, max: 30 },
  { key: 'status_tick_damage', label: 'Урон эффекта в секунду', hint: 'периодический урон эффекта', step: 1, min: 0, max: 100 },
  { key: 'max_range_cells', label: 'Дальность в пошаговом, клеток', hint: 'дальность на сетке Gorky 17', step: 1, min: 1, max: 20 },
];

const GRENADE_FIELDS: { key: keyof GrenadeTuning; label: string; hint: string; step: number; min: number; max: number }[] = [
  { key: 'damage', label: 'Урон', hint: 'урон в центре взрыва (к краю падает)', step: 1, min: 0, max: 500 },
  { key: 'effect_radius_m', label: 'Радиус взрыва, м', hint: 'радиус поражения', step: 0.25, min: 0.5, max: 15 },
  { key: 'throw_range_m', label: 'Дальность броска, м', hint: 'как далеко боец бросает (лёжа меньше)', step: 0.5, min: 2, max: 40 },
  { key: 'max_carried', label: 'Лимит переноски', hint: 'сколько гранат боец может нести', step: 1, min: 1, max: 20 },
];

const RULES_TAB = '__rules__';
const DEFAULT_RULES: TurnBasedRulesTuning = {
  crouch_move_cost_multiplier: 2,
  cover_fire_accuracy_multiplier: 0.75,
  enemy_fire_at_cover_multiplier: 0.6,
};
const RULE_FIELDS: { key: keyof TurnBasedRulesTuning; label: string; hint: string; step: number; min: number; max: number }[] = [
  { key: 'crouch_move_cost_multiplier', label: 'Цена шага в присядку / лёжа, ×', hint: 'во сколько раз дороже AP за клетку, чем стоя (целое)', step: 1, min: 1, max: 4 },
  { key: 'cover_fire_accuracy_multiplier', label: 'Меткость из-за баррикады, ×', hint: 'боец стреляет мимо своей баррикады (1 — без штрафа)', step: 0.05, min: 0.1, max: 1 },
  { key: 'enemy_fire_at_cover_multiplier', label: 'Попадание врагов по бойцу за баррикадой, ×', hint: 'шанс дальнобойных врагов (1 — баррикада не мешает)', step: 0.05, min: 0, max: 1 },
];

const listToText = (values: number[]) => values.join(', ');
const textToList = (text: string) =>
  text.split(/[,;\s]+/).map((v) => v.trim()).filter((v) => v !== '').map(Number).filter((v) => !Number.isNaN(v));

export const WeaponTuningPanel: React.FC = () => {
  const [data, setData] = useState<WeaponTuningFile | null>(null);
  const [saved, setSaved] = useState<WeaponTuningFile | null>(null);
  const [tab, setTab] = useState<string>('m16');
  const [status, setStatus] = useState<{ kind: 'idle' | 'loading' | 'saving' | 'ok' | 'error'; text?: string }>({ kind: 'loading' });

  useEffect(() => {
    fetch('/api/get-weapons')
      .then(async (res) => {
        const body = await res.json();
        if (!res.ok) throw new Error(body.error || res.statusText);
        setData(body);
        setSaved(JSON.parse(JSON.stringify(body)));
        setStatus({ kind: 'idle' });
        if (!body.weapons?.m16) setTab(Object.keys(body.weapons || {})[0] || GRENADE_TAB);
      })
      .catch((e) => setStatus({ kind: 'error', text: String(e.message || e) }));
  }, []);

  if (status.kind === 'loading') {
    return (
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-6 text-slate-400 flex items-center gap-2">
        <Loader2 className="w-4 h-4 animate-spin" /> Загрузка параметров оружия…
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

  const ids = [
    ...Object.keys(WEAPON_LABELS).filter((id) => data.weapons[id]),
    ...Object.keys(data.weapons).filter((id) => !WEAPON_LABELS[id]),
  ];
  const dirty = JSON.stringify(data) !== JSON.stringify(saved);

  const setWeapon = (id: string, patch: Partial<WeaponTuningEntry>) =>
    setData({ ...data, weapons: { ...data.weapons, [id]: { ...data.weapons[id], ...patch } } });
  const setGrenade = (patch: Partial<GrenadeTuning>) => setData({ ...data, grenade: { ...data.grenade, ...patch } });
  const rules = { ...DEFAULT_RULES, ...(data.turn_based_rules || {}) };
  const savedRules = { ...DEFAULT_RULES, ...(saved?.turn_based_rules || {}) };
  const setRules = (patch: Partial<TurnBasedRulesTuning>) => setData({ ...data, turn_based_rules: { ...rules, ...patch } });

  const save = async () => {
    setStatus({ kind: 'saving' });
    try {
      const res = await fetch('/api/save-weapons', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(data) });
      const body = await res.json();
      if (!res.ok) throw new Error(body.error || res.statusText);
      setSaved(JSON.parse(JSON.stringify(data)));
      setStatus({ kind: 'ok', text: 'Сохранено. Игра применит при следующем запуске уровня.' });
    } catch (e: any) {
      setStatus({ kind: 'error', text: String(e.message || e) });
    }
  };

  const reset = () => saved && setData(JSON.parse(JSON.stringify(saved)));

  const field = (label: string, hint: string, value: number, step: number, min: number, max: number, onChange: (v: number) => void, changed: boolean) => (
    <div key={label} className={`bg-slate-900/80 p-3 rounded-lg border ${changed ? 'border-amber-500/70' : 'border-slate-700/60'}`}>
      <div className="text-xs font-medium text-slate-300 mb-1">{label}</div>
      <input
        type="number"
        step={step}
        min={min}
        max={max}
        value={value}
        onChange={(e) => onChange(e.target.value === '' ? 0 : Number(e.target.value))}
        className="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1 text-sm font-semibold text-slate-100 focus:outline-none focus:border-rose-500"
      />
      <span className="text-[10px] text-slate-500 block mt-1">{hint}</span>
    </div>
  );

  const weapon = tab !== GRENADE_TAB ? data.weapons[tab] : undefined;
  const savedWeapon = tab !== GRENADE_TAB ? saved?.weapons[tab] : undefined;

  return (
    <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-xl">
      <div className="flex flex-wrap items-center justify-between gap-3 pb-4 border-b border-slate-800 mb-4">
        <div className="flex items-center gap-3">
          <div className="p-2 bg-rose-500/10 text-rose-400 rounded-lg border border-rose-500/20">
            <Crosshair className="w-5 h-5" />
          </div>
          <div>
            <h3 className="text-base font-semibold text-slate-100">Мощность оружия отряда</h3>
            <p className="text-xs text-slate-400">Content/Data/Weapons/weapons_tuning.json — игра применяет при запуске уровня</p>
          </div>
        </div>
        <div className="flex items-center gap-2">
          <button
            onClick={reset}
            disabled={!dirty}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-bold bg-slate-800 text-slate-300 disabled:opacity-40 hover:bg-slate-700"
          >
            <RotateCcw className="w-3.5 h-3.5" /> Отменить
          </button>
          <button
            onClick={save}
            disabled={!dirty || status.kind === 'saving'}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-bold bg-rose-500 text-slate-950 disabled:opacity-40 hover:bg-rose-400"
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

      <div className="flex flex-wrap gap-1.5 mb-4">
        {ids.map((id) => (
          <button
            key={id}
            onClick={() => setTab(id)}
            className={`px-3 py-1.5 rounded-lg text-xs font-bold border transition-all ${
              tab === id ? 'bg-rose-500 text-slate-950 border-rose-400' : 'bg-slate-800/60 text-slate-300 border-slate-700 hover:bg-slate-800'
            }`}
          >
            {WEAPON_LABELS[id] || data.weapons[id].name || id}
          </button>
        ))}
        <button
          onClick={() => setTab(GRENADE_TAB)}
          className={`px-3 py-1.5 rounded-lg text-xs font-bold border transition-all flex items-center gap-1 ${
            tab === GRENADE_TAB ? 'bg-lime-400 text-slate-950 border-lime-300' : 'bg-slate-800/60 text-slate-300 border-slate-700 hover:bg-slate-800'
          }`}
        >
          <Bomb className="w-3.5 h-3.5" /> Гранаты
        </button>
        <button
          onClick={() => setTab(RULES_TAB)}
          className={`px-3 py-1.5 rounded-lg text-xs font-bold border transition-all ${
            tab === RULES_TAB ? 'bg-sky-400 text-slate-950 border-sky-300' : 'bg-slate-800/60 text-slate-300 border-slate-700 hover:bg-slate-800'
          }`}
        >
          Правила пошагового боя
        </button>
      </div>

      {weapon && (
        <>
          <div className="grid grid-cols-2 md:grid-cols-3 lg:grid-cols-5 gap-3">
            {WEAPON_FIELDS.map((f) =>
              field(f.label, f.hint, weapon[f.key], f.step, f.min, f.max, (v) => setWeapon(tab, { [f.key]: v } as Partial<WeaponTuningEntry>),
                savedWeapon !== undefined && savedWeapon[f.key] !== weapon[f.key]))}
          </div>
          <div className="grid grid-cols-1 md:grid-cols-2 gap-3 mt-3">
            <div className="bg-slate-900/80 p-3 rounded-lg border border-slate-700/60">
              <div className="text-xs font-medium text-slate-300 mb-1">Шанс попадания в пошаговом (по клеткам: 1, 2, 3…)</div>
              <input
                type="text"
                defaultValue={listToText(weapon.base_hit_chances)}
                key={`${tab}-hit-${listToText(weapon.base_hit_chances)}`}
                onBlur={(e) => setWeapon(tab, { base_hit_chances: textToList(e.target.value) })}
                className="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1 text-sm font-mono text-slate-100 focus:outline-none focus:border-rose-500"
              />
              <span className="text-[10px] text-slate-500 block mt-1">через запятую, 0..1</span>
            </div>
            <div className="bg-slate-900/80 p-3 rounded-lg border border-slate-700/60">
              <div className="text-xs font-medium text-slate-300 mb-1">Множитель урона по дистанции (по клеткам)</div>
              <input
                type="text"
                defaultValue={listToText(weapon.distance_damage_multipliers)}
                key={`${tab}-dmg-${listToText(weapon.distance_damage_multipliers)}`}
                onBlur={(e) => setWeapon(tab, { distance_damage_multipliers: textToList(e.target.value) })}
                className="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1 text-sm font-mono text-slate-100 focus:outline-none focus:border-rose-500"
              />
              <span className="text-[10px] text-slate-500 block mt-1">через запятую, 1 = полный урон</span>
            </div>
          </div>
        </>
      )}

      {tab === RULES_TAB && (
        <div className="grid grid-cols-1 md:grid-cols-3 gap-3">
          {RULE_FIELDS.map((f) =>
            field(f.label, f.hint, rules[f.key], f.step, f.min, f.max, (v) => setRules({ [f.key]: v } as Partial<TurnBasedRulesTuning>),
              savedRules[f.key] !== rules[f.key]))}
          <div className="col-span-1 md:col-span-3 text-[11px] text-slate-500">
            Применяется при начале пошагового боя. Урон по сидящему (×0.70) и лежащему (×0.50) задаётся в балансе игры (DA_Balance).
          </div>
        </div>
      )}

      {tab === GRENADE_TAB && (
        <div className="grid grid-cols-2 md:grid-cols-4 gap-3">
          {GRENADE_FIELDS.map((f) =>
            field(f.label, f.hint, data.grenade[f.key], f.step, f.min, f.max, (v) => setGrenade({ [f.key]: v } as Partial<GrenadeTuning>),
              saved !== null && saved.grenade[f.key] !== data.grenade[f.key]))}
          <div className="col-span-2 md:col-span-4 text-[11px] text-slate-500">
            Количество гранат у бойцов на старте задаётся в «Снаряжении отряда» на вкладке волн.
          </div>
        </div>
      )}
    </div>
  );
};
