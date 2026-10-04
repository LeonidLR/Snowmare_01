import React, { useEffect, useState } from 'react';
import { Target, Save, RotateCcw, Loader2, AlertTriangle, CheckCircle2 } from 'lucide-react';
import { WeaponTuningFile, EnemyRangedWeapon } from '../types';

// Enemy ranged weapons (enemy_weapons.<id> in weapons_tuning.json). For now the marksman's rifle; the spitter's acid,
// blasters and projectiles come later as more tabs.
const ENEMY_WEAPON_LABELS: Record<string, string> = {
  marksman_rifle: 'Винтовка стрелка',
};

type Field = { key: keyof EnemyRangedWeapon; label: string; hint: string; step: number; min: number; max: number };

const REALTIME: Field[] = [
  { key: 'damage', label: 'Урон выстрела', hint: 'урон одного попадания', step: 1, min: 0, max: 300 },
  { key: 'base_accuracy', label: 'Меткость', hint: 'шанс попасть стоя по стоящей цели в открытую (0..1)', step: 0.05, min: 0, max: 1 },
  { key: 'aim_duration', label: 'Прицеливание, с', hint: 'видимый лазер до выстрела — время игроку среагировать', step: 0.1, min: 0.2, max: 6 },
  { key: 'shot_cooldown', label: 'Пауза после выстрела, с', hint: 'до следующего прицеливания', step: 0.1, min: 0, max: 10 },
  { key: 'crit_chance', label: 'Шанс крита', hint: '0..1', step: 0.05, min: 0, max: 1 },
  { key: 'crit_multiplier', label: 'Множитель крита', hint: 'урон крита = урон × множитель', step: 0.1, min: 1, max: 5 },
  { key: 'prone_accuracy_bonus', label: 'Бонус меткости лёжа', hint: '× к меткости, когда стрелок лежит', step: 0.05, min: 0.5, max: 3 },
  { key: 'crouch_accuracy_bonus', label: 'Бонус меткости сидя', hint: '× к меткости, когда стрелок присел', step: 0.05, min: 0.5, max: 3 },
  { key: 'preferred_min_range_m', label: 'Дистанция боя от, м', hint: 'ближе — отходит (с ограничением отхода)', step: 1, min: 5, max: 60 },
  { key: 'preferred_max_range_m', label: 'Дистанция боя до, м', hint: 'дальше — сближается', step: 1, min: 5, max: 80 },
];

const TURN_BASED: Field[] = [
  { key: 'tb_damage_scale', label: 'Урон (× базовый урон врага)', hint: 'базовый урон врага в пошаговом — 18', step: 0.1, min: 0, max: 5 },
  { key: 'tb_attack_ap', label: 'Цена выстрела, AP', hint: 'сколько очков действия стоит выстрел', step: 1, min: 1, max: 8 },
  { key: 'tb_min_range_cells', label: 'Мин. дальность, клеток', hint: 'ближе не стреляет', step: 1, min: 1, max: 20 },
  { key: 'tb_max_range_cells', label: 'Макс. дальность, клеток', hint: 'дальше не стреляет', step: 1, min: 1, max: 30 },
  { key: 'tb_base_hit_chance', label: 'Шанс попадания', hint: 'на мин. дистанции по стоящей цели без укрытия', step: 0.05, min: 0.05, max: 0.95 },
  { key: 'tb_hit_falloff_per_cell', label: 'Падение шанса за клетку', hint: 'за каждую клетку дальше минимальной', step: 0.01, min: 0, max: 0.2 },
];

export const EnemyWeaponsPanel: React.FC = () => {
  const [data, setData] = useState<WeaponTuningFile | null>(null);
  const [saved, setSaved] = useState<WeaponTuningFile | null>(null);
  const [tab, setTab] = useState<string>('marksman_rifle');
  const [status, setStatus] = useState<{ kind: 'idle' | 'loading' | 'saving' | 'ok' | 'error'; text?: string }>({ kind: 'loading' });

  useEffect(() => {
    fetch('/api/get-weapons')
      .then(async (res) => {
        const body = await res.json();
        if (!res.ok) throw new Error(body.error || res.statusText);
        if (!body.enemy_weapons) throw new Error('В weapons_tuning.json нет блока enemy_weapons: обновите файл командой CodexTactics.DumpWeaponTuning');
        setData(body);
        setSaved(JSON.parse(JSON.stringify(body)));
        setStatus({ kind: 'idle' });
      })
      .catch((e) => setStatus({ kind: 'error', text: String(e.message || e) }));
  }, []);

  if (status.kind === 'loading') {
    return (
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-6 text-slate-400 flex items-center gap-2">
        <Loader2 className="w-4 h-4 animate-spin" /> Загрузка оружия врагов…
      </div>
    );
  }
  if (!data || !data.enemy_weapons) {
    return (
      <div className="bg-slate-900 border border-rose-800 rounded-xl p-6 text-rose-300 flex items-center gap-2">
        <AlertTriangle className="w-4 h-4" /> {status.text}
      </div>
    );
  }

  const weapons = data.enemy_weapons;
  const weapon = weapons[tab];
  const savedWeapon = saved?.enemy_weapons?.[tab];
  const dirty = JSON.stringify(data) !== JSON.stringify(saved);

  const setField = (key: keyof EnemyRangedWeapon, value: number) =>
    setData({ ...data, enemy_weapons: { ...weapons, [tab]: { ...weapon, [key]: value } } });

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

  const grid = (title: string, fields: Field[]) => (
    <div className="mb-4">
      <div className="text-xs font-bold text-slate-400 uppercase tracking-wide mb-2">{title}</div>
      <div className="grid grid-cols-2 md:grid-cols-3 lg:grid-cols-5 gap-3">
        {fields.map((f) => {
          const changed = savedWeapon !== undefined && savedWeapon[f.key] !== weapon[f.key];
          return (
            <div key={f.key} className={`bg-slate-900/80 p-3 rounded-lg border ${changed ? 'border-amber-500/70' : 'border-slate-700/60'}`}>
              <div className="text-xs font-medium text-slate-300 mb-1">{f.label}</div>
              <input
                type="number"
                step={f.step}
                min={f.min}
                max={f.max}
                value={weapon[f.key]}
                onChange={(e) => setField(f.key, e.target.value === '' ? 0 : Number(e.target.value))}
                className="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1 text-sm font-semibold text-slate-100 focus:outline-none focus:border-orange-500"
              />
              <span className="text-[10px] text-slate-500 block mt-1">{f.hint}</span>
            </div>
          );
        })}
      </div>
    </div>
  );

  return (
    <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-xl">
      <div className="flex flex-wrap items-center justify-between gap-3 pb-4 border-b border-slate-800 mb-4">
        <div className="flex items-center gap-3">
          <div className="p-2 bg-orange-500/10 text-orange-400 rounded-lg border border-orange-500/20">
            <Target className="w-5 h-5" />
          </div>
          <div>
            <h3 className="text-base font-semibold text-slate-100">Дальнобойное оружие врагов</h3>
            <p className="text-xs text-slate-400">weapons_tuning.json → enemy_weapons. Пока винтовка стрелка; кислота и снаряды — позже</p>
          </div>
        </div>
        <div className="flex items-center gap-2">
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
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-bold bg-orange-500 text-slate-950 disabled:opacity-40 hover:bg-orange-400"
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
        {Object.keys(weapons).map((id) => (
          <button
            key={id}
            onClick={() => setTab(id)}
            className={`px-3 py-1.5 rounded-lg text-xs font-bold border transition-all ${
              tab === id ? 'bg-orange-500 text-slate-950 border-orange-400' : 'bg-slate-800/60 text-slate-300 border-slate-700 hover:bg-slate-800'
            }`}
          >
            {ENEMY_WEAPON_LABELS[id] || id}
          </button>
        ))}
      </div>

      {weapon && (
        <>
          {grid('Реальное время', REALTIME)}
          {grid('Пошаговый бой', TURN_BASED)}
          <div className="text-[11px] text-slate-500">
            Тренер ИИ (jev_ai_coach.py) в своих экспериментах может временно перекрывать эти значения консольными переменными Codex.Marksman.*.
          </div>
        </>
      )}
    </div>
  );
};
