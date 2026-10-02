import React from 'react';
import { SquadLoadoutConfig, SimulationMode, PresetTier } from '../types';
import { Shield, Compass, Sliders, Box, Crosshair, Wrench, Bomb, Heart, Flame } from 'lucide-react';

interface SquadLoadoutControlsProps {
  loadout?: SquadLoadoutConfig;
  onChange: (updated: SquadLoadoutConfig) => void;
}

const DEFAULT_LOADOUT: SquadLoadoutConfig = {
  simulation_mode: 'EXPLORE_AND_COLLECT',
  preset_tier: 'STANDARD',
  turrets_count: 1,
  barricades_count: 2,
  mines_count: 2,
  medkits_count: 2,
  m16_ammo: 120,
  pistol_ammo: 48,
  canned_food: 4,
  matches: 3,
};

export const SquadLoadoutControls: React.FC<SquadLoadoutControlsProps> = ({
  loadout,
  onChange,
}) => {
  const current = { ...DEFAULT_LOADOUT, ...loadout };

  const handleModeChange = (mode: SimulationMode) => {
    onChange({ ...current, simulation_mode: mode });
  };

  const handlePresetChange = (tier: PresetTier) => {
    let t = current.turrets_count;
    let b = current.barricades_count;
    let m = current.mines_count;
    let med = current.medkits_count;
    let m16 = current.m16_ammo;
    let p = current.pistol_ammo;

    if (tier === 'MINIMAL') {
      t = 0; b = 0; m = 0; med = 0; m16 = 60; p = 24;
    } else if (tier === 'STANDARD') {
      t = 1; b = 2; m = 2; med = 2; m16 = 120; p = 48;
    } else if (tier === 'MAXIMAL') {
      t = 2; b = 4; m = 5; med = 4; m16 = 240; p = 96;
    }

    onChange({
      ...current,
      preset_tier: tier,
      turrets_count: t,
      barricades_count: b,
      mines_count: m,
      medkits_count: med,
      m16_ammo: m16,
      pistol_ammo: p,
    });
  };

  const updateField = (field: keyof SquadLoadoutConfig, value: number) => {
    onChange({
      ...current,
      preset_tier: 'CUSTOM',
      [field]: Math.max(0, value),
    });
  };

  return (
    <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 mb-6 shadow-xl">
      <div className="flex items-center justify-between pb-4 border-b border-slate-800 mb-4">
        <div className="flex items-center space-x-3">
          <div className="p-2 bg-indigo-500/10 text-indigo-400 rounded-lg border border-indigo-500/20">
            <Sliders className="w-5 h-5" />
          </div>
          <div>
            <h3 className="text-base font-semibold text-slate-100 flex items-center gap-2">
              Снаряжение отряда и Режим симуляции
              <span className="text-xs px-2 py-0.5 bg-indigo-900/50 text-indigo-300 rounded border border-indigo-700/50 font-mono">
                {current.simulation_mode}
              </span>
            </h3>
            <p className="text-xs text-slate-400">
              Управление сбором объектов на карте, выдачей стартовых турелей/баррикад/мин и пресетами для симуляции
            </p>
          </div>
        </div>
      </div>

      {/* 3 Режима симуляции */}
      <div className="grid grid-cols-1 md:grid-cols-3 gap-3 mb-5">
        {/* Режим 1: Автономный сбор */}
        <div
          onClick={() => handleModeChange('EXPLORE_AND_COLLECT')}
          className={`cursor-pointer rounded-xl p-3.5 border transition-all ${
            current.simulation_mode === 'EXPLORE_AND_COLLECT'
              ? 'bg-emerald-950/30 border-emerald-500 text-emerald-300 ring-1 ring-emerald-500/50'
              : 'bg-slate-800/40 border-slate-700/60 hover:bg-slate-800 hover:border-slate-600 text-slate-300'
          }`}
        >
          <div className="flex items-center gap-2.5 font-semibold text-sm mb-1.5">
            <Compass className="w-4 h-4 text-emerald-400 shrink-0" />
            <span>1. Автономный сбор</span>
          </div>
          <p className="text-xs text-slate-400 leading-relaxed">
            Бот самостоятельно обходит всю карту, собирает все ящики и инженерные объекты на дистанции 2.0м.
          </p>
        </div>

        {/* Режим 2: Стартовые уникальные объекты */}
        <div
          onClick={() => handleModeChange('STARTING_UNIQUE')}
          className={`cursor-pointer rounded-xl p-3.5 border transition-all ${
            current.simulation_mode === 'STARTING_UNIQUE'
              ? 'bg-amber-950/30 border-amber-500 text-amber-300 ring-1 ring-amber-500/50'
              : 'bg-slate-800/40 border-slate-700/60 hover:bg-slate-800 hover:border-slate-600 text-slate-300'
          }`}
        >
          <div className="flex items-center gap-2.5 font-semibold text-sm mb-1.5">
            <Shield className="w-4 h-4 text-amber-400 shrink-0" />
            <span>2. Стартовые уникальные</span>
          </div>
          <p className="text-xs text-slate-400 leading-relaxed">
            Пропуск сбора по карте. Мгновенный переход к бою с выданными уникальными средствами из настроек бойцов.
          </p>
        </div>

        {/* Режим 3: Пресет из Wave Editor */}
        <div
          onClick={() => handleModeChange('EDITOR_PRESET')}
          className={`cursor-pointer rounded-xl p-3.5 border transition-all ${
            current.simulation_mode === 'EDITOR_PRESET'
              ? 'bg-cyan-950/30 border-cyan-500 text-cyan-300 ring-1 ring-cyan-500/50'
              : 'bg-slate-800/40 border-slate-700/60 hover:bg-slate-800 hover:border-slate-600 text-slate-300'
          }`}
        >
          <div className="flex items-center gap-2.5 font-semibold text-sm mb-1.5">
            <Box className="w-4 h-4 text-cyan-400 shrink-0" />
            <span>3. Пресет Wave Editor</span>
          </div>
          <p className="text-xs text-slate-400 leading-relaxed">
            Точный контроль всех ресурсов и объектов из редактора (минимальный, стандартный, максимальный или кастомный).
          </p>
        </div>
      </div>

      {/* Панель настройки ресурсов */}
      <div className={`p-4 rounded-xl border ${current.simulation_mode === 'EDITOR_PRESET' ? 'bg-slate-800/60 border-cyan-800/60' : 'bg-slate-800/20 border-slate-800'}`}>
        <div className="flex flex-wrap items-center justify-between gap-3 mb-4 pb-3 border-b border-slate-700/60">
          <div className="text-xs font-semibold text-slate-300 flex items-center gap-2">
            <span>Пресет снаряжения:</span>
            <div className="inline-flex rounded-lg bg-slate-900 p-0.5 border border-slate-700">
              {(['MINIMAL', 'STANDARD', 'MAXIMAL', 'CUSTOM'] as PresetTier[]).map((tier) => (
                <button
                  key={tier}
                  type="button"
                  onClick={() => handlePresetChange(tier)}
                  className={`px-2.5 py-1 text-xs rounded-md font-medium transition-all ${
                    current.preset_tier === tier
                      ? 'bg-cyan-600 text-white shadow-sm'
                      : 'text-slate-400 hover:text-slate-200'
                  }`}
                >
                  {tier === 'MINIMAL' && 'Минимальный'}
                  {tier === 'STANDARD' && 'Стандартный'}
                  {tier === 'MAXIMAL' && 'Максимальный'}
                  {tier === 'CUSTOM' && 'Пользовательский'}
                </button>
              ))}
            </div>
          </div>
          <span className="text-[11px] text-slate-400">
            {current.simulation_mode !== 'EDITOR_PRESET' ? '💡 Выберите режим 3 для применения этих точных чисел в симуляции' : '✅ Эти значения будут выданы отряду перед боем'}
          </span>
        </div>

        {/* Сетка параметров уникальных объектов и ресурсов */}
        <div className="grid grid-cols-2 sm:grid-cols-4 lg:grid-cols-8 gap-3">
          {/* Турели */}
          <div className="bg-slate-900/80 p-2.5 rounded-lg border border-slate-700/60">
            <div className="flex items-center gap-1.5 text-xs text-amber-400 mb-1 font-medium">
              <Crosshair className="w-3.5 h-3.5" />
              <span>Турели</span>
            </div>
            <input
              type="number"
              min="0"
              max="5"
              value={current.turrets_count}
              onChange={(e) => updateField('turrets_count', parseInt(e.target.value) || 0)}
              className="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1 text-sm font-semibold text-slate-100 focus:outline-none focus:border-cyan-500"
            />
            <span className="text-[10px] text-slate-500 block mt-1">Командир (макс: 2)</span>
          </div>

          {/* Баррикады */}
          <div className="bg-slate-900/80 p-2.5 rounded-lg border border-slate-700/60">
            <div className="flex items-center gap-1.5 text-xs text-blue-400 mb-1 font-medium">
              <Wrench className="w-3.5 h-3.5" />
              <span>Баррикады</span>
            </div>
            <input
              type="number"
              min="0"
              max="8"
              value={current.barricades_count}
              onChange={(e) => updateField('barricades_count', parseInt(e.target.value) || 0)}
              className="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1 text-sm font-semibold text-slate-100 focus:outline-none focus:border-cyan-500"
            />
            <span className="text-[10px] text-slate-500 block mt-1">Инженер (макс: 4)</span>
          </div>

          {/* Мины */}
          <div className="bg-slate-900/80 p-2.5 rounded-lg border border-slate-700/60">
            <div className="flex items-center gap-1.5 text-xs text-red-400 mb-1 font-medium">
              <Bomb className="w-3.5 h-3.5" />
              <span>Мины</span>
            </div>
            <input
              type="number"
              min="0"
              max="10"
              value={current.mines_count}
              onChange={(e) => updateField('mines_count', parseInt(e.target.value) || 0)}
              className="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1 text-sm font-semibold text-slate-100 focus:outline-none focus:border-cyan-500"
            />
            <span className="text-[10px] text-slate-500 block mt-1">Сапёр (макс: 5)</span>
          </div>

          {/* Аптечки */}
          <div className="bg-slate-900/80 p-2.5 rounded-lg border border-slate-700/60">
            <div className="flex items-center gap-1.5 text-xs text-emerald-400 mb-1 font-medium">
              <Heart className="w-3.5 h-3.5" />
              <span>Аптечки</span>
            </div>
            <input
              type="number"
              min="0"
              max="10"
              value={current.medkits_count}
              onChange={(e) => updateField('medkits_count', parseInt(e.target.value) || 0)}
              className="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1 text-sm font-semibold text-slate-100 focus:outline-none focus:border-cyan-500"
            />
            <span className="text-[10px] text-slate-500 block mt-1">Шт. в запасе</span>
          </div>

          {/* Патроны M16 */}
          <div className="bg-slate-900/80 p-2.5 rounded-lg border border-slate-700/60">
            <div className="flex items-center gap-1.5 text-xs text-purple-400 mb-1 font-medium">
              <span>Патроны M16</span>
            </div>
            <input
              type="number"
              min="0"
              step="30"
              max="600"
              value={current.m16_ammo}
              onChange={(e) => updateField('m16_ammo', parseInt(e.target.value) || 0)}
              className="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1 text-sm font-semibold text-slate-100 focus:outline-none focus:border-cyan-500"
            />
            <span className="text-[10px] text-slate-500 block mt-1">Автомат</span>
          </div>

          {/* Патроны Пистолет */}
          <div className="bg-slate-900/80 p-2.5 rounded-lg border border-slate-700/60">
            <div className="flex items-center gap-1.5 text-xs text-indigo-400 mb-1 font-medium">
              <span>Пистолет</span>
            </div>
            <input
              type="number"
              min="0"
              step="12"
              max="300"
              value={current.pistol_ammo}
              onChange={(e) => updateField('pistol_ammo', parseInt(e.target.value) || 0)}
              className="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1 text-sm font-semibold text-slate-100 focus:outline-none focus:border-cyan-500"
            />
            <span className="text-[10px] text-slate-500 block mt-1">Пистолет</span>
          </div>

          {/* Консервы */}
          <div className="bg-slate-900/80 p-2.5 rounded-lg border border-slate-700/60">
            <div className="flex items-center gap-1.5 text-xs text-yellow-400 mb-1 font-medium">
              <span>Консервы</span>
            </div>
            <input
              type="number"
              min="0"
              max="20"
              value={current.canned_food}
              onChange={(e) => updateField('canned_food', parseInt(e.target.value) || 0)}
              className="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1 text-sm font-semibold text-slate-100 focus:outline-none focus:border-cyan-500"
            />
            <span className="text-[10px] text-slate-500 block mt-1">Еда</span>
          </div>

          {/* Спички */}
          <div className="bg-slate-900/80 p-2.5 rounded-lg border border-slate-700/60">
            <div className="flex items-center gap-1.5 text-xs text-orange-400 mb-1 font-medium">
              <Flame className="w-3.5 h-3.5" />
              <span>Спички</span>
            </div>
            <input
              type="number"
              min="0"
              max="20"
              value={current.matches}
              onChange={(e) => updateField('matches', parseInt(e.target.value) || 0)}
              className="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1 text-sm font-semibold text-slate-100 focus:outline-none focus:border-cyan-500"
            />
            <span className="text-[10px] text-slate-500 block mt-1">Обогрев</span>
          </div>
        </div>
      </div>
    </div>
  );
};
