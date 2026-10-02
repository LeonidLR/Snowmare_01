import React from 'react';
import { Sliders, Clock, Shield, Flame, Snowflake, PackageOpen, Box, KeyRound, Power } from 'lucide-react';
import { LevelConfig } from '../types';

interface Props {
  config: LevelConfig;
  onChangeConfig: (newConfig: LevelConfig) => void;
}

export const GlobalCurveControls: React.FC<Props> = ({
  config,
  onChangeConfig,
}) => {
  const lastWave = config.waves[config.waves.length - 1];
  const finalHpMult = lastWave ? (lastWave.wave_modifiers?.enemy_hp_mult || 1.0) : 1.0;
  const finalDmgMult = lastWave ? (lastWave.wave_modifiers?.enemy_damage_mult || 1.0) : 1.0;

  const handleScaleHpCurve = (targetFinalMult: number) => {
    const waveCount = config.waves.length;
    const newWaves = config.waves.map((w, idx) => {
      const step = waveCount > 1 ? idx / (waveCount - 1) : 0;
      const newMult = Number((1.0 + step * (targetFinalMult - 1.0)).toFixed(2));
      return {
        ...w,
        wave_modifiers: {
          ...w.wave_modifiers,
          enemy_hp_mult: newMult
        }
      };
    });
    onChangeConfig({ ...config, waves: newWaves });
  };

  const handleScaleDmgCurve = (targetFinalMult: number) => {
    const waveCount = config.waves.length;
    const newWaves = config.waves.map((w, idx) => {
      const step = waveCount > 1 ? idx / (waveCount - 1) : 0;
      const newMult = Number((1.0 + step * (targetFinalMult - 1.0)).toFixed(2));
      return {
        ...w,
        wave_modifiers: {
          ...w.wave_modifiers,
          enemy_damage_mult: newMult
        }
      };
    });
    onChangeConfig({ ...config, waves: newWaves });
  };

  const handleSetGlobalCold = (mult: number) => {
    const newWaves = config.waves.map((w) => ({
      ...w,
      wave_modifiers: {
        ...w.wave_modifiers,
        cold_drain_mult: mult
      }
    }));
    onChangeConfig({ ...config, waves: newWaves });
  };

  return (
    <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-xl flex flex-col gap-4">
      <div className="flex items-center justify-between border-b border-slate-800 pb-3">
        <div className="flex items-center gap-2">
          <Sliders className="w-5 h-5 text-sky-400" />
          <h2 className="text-base font-semibold text-slate-100">Глобальные параметры боя</h2>
        </div>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
        {/* Холод */}
        <div className="bg-slate-950/70 border border-slate-800 rounded-lg p-3 flex flex-col gap-2">
          <div className="flex justify-between items-center text-xs">
            <span className="text-slate-300 font-medium flex items-center gap-1">
              <Snowflake className="w-3.5 h-3.5 text-cyan-400" />
              Окружение локации (Сила холода):
            </span>
            <span className="text-cyan-400 font-bold bg-cyan-950/60 px-2 py-0.5 rounded border border-cyan-800/50">
              {(config.waves[0]?.wave_modifiers?.cold_drain_mult ?? 1.0).toFixed(1)}x
            </span>
          </div>
          <div className="grid grid-cols-4 gap-1 text-[10px]">
            <button
              onClick={() => handleSetGlobalCold(0.0)}
              className="py-1 px-1 rounded bg-slate-900 hover:bg-slate-800 border border-slate-700 text-amber-300 font-semibold transition-all text-center cursor-pointer"
              title="Закрытое помещение / отапливаемый бункер (холод 0.0x)"
            >
              🏠 Бункер (0x)
            </button>
            <button
              onClick={() => handleSetGlobalCold(1.0)}
              className="py-1 px-1 rounded bg-slate-900 hover:bg-slate-800 border border-slate-700 text-sky-300 font-semibold transition-all text-center cursor-pointer"
              title="Стандартный мороз на открытом воздухе (1.0x)"
            >
              ❄️ Мороз (1x)
            </button>
            <button
              onClick={() => handleSetGlobalCold(2.5)}
              className="py-1 px-1 rounded bg-slate-900 hover:bg-slate-800 border border-slate-700 text-cyan-200 font-semibold transition-all text-center cursor-pointer"
              title="Экстремальный снежный буран (2.5x)"
            >
              🌨️ Буран (2.5x)
            </button>
            <button
              onClick={() => handleSetGlobalCold(4.0)}
              className="py-1 px-1 rounded bg-slate-900 hover:bg-slate-800 border border-slate-700 text-purple-300 font-semibold transition-all text-center cursor-pointer"
              title="Аномальный крио-шторм (4.0x)"
            >
              🌪️ Шторм (4x)
            </button>
          </div>
          <input
            type="range"
            min={0.0}
            max={5.0}
            step={0.1}
            value={config.waves[0]?.wave_modifiers?.cold_drain_mult ?? 1.0}
            onChange={(e) => handleSetGlobalCold(Number(e.target.value))}
            className="w-full h-1.5 bg-slate-800 rounded-lg cursor-pointer accent-cyan-400 mt-1"
            title="Задать силу холода одновременно для всех волн уровня"
          />
        </div>

        {/* HP */}
        <div className="bg-slate-950/70 border border-slate-800 rounded-lg p-3 flex flex-col gap-2">
          <div className="flex justify-between items-center text-xs">
            <span className="text-slate-300 font-medium flex items-center gap-1">
              <Shield className="w-3.5 h-3.5 text-emerald-400" />
              Скейлинг здоровья орды (HP):
            </span>
            <span className="text-emerald-400 font-bold bg-emerald-950/60 px-2 py-0.5 rounded border border-emerald-800/50">
              до {finalHpMult}x в финале
            </span>
          </div>
          <input
            type="range"
            min={1.0}
            max={2.5}
            step={0.05}
            value={finalHpMult}
            onChange={(e) => handleScaleHpCurve(Number(e.target.value))}
            className="w-full h-1.5 bg-slate-800 rounded-lg cursor-pointer accent-emerald-400"
          />
        </div>

        {/* Damage */}
        <div className="bg-slate-950/70 border border-slate-800 rounded-lg p-3 flex flex-col gap-2">
          <div className="flex justify-between items-center text-xs">
            <span className="text-slate-300 font-medium flex items-center gap-1">
              <Flame className="w-3.5 h-3.5 text-orange-400" />
              Скейлинг урона врагов:
            </span>
            <span className="text-orange-400 font-bold bg-orange-950/60 px-2 py-0.5 rounded border border-orange-800/50">
              до {finalDmgMult}x в финале
            </span>
          </div>
          <input
            type="range"
            min={1.0}
            max={2.2}
            step={0.05}
            value={finalDmgMult}
            onChange={(e) => handleScaleDmgCurve(Number(e.target.value))}
            className="w-full h-1.5 bg-slate-800 rounded-lg cursor-pointer accent-orange-400"
          />
        </div>

        {/* Prep duration */}
        <div className="bg-slate-950/70 border border-slate-800 rounded-lg p-3 flex flex-col gap-2">
          <div className="flex justify-between items-center text-xs">
            <span className="text-slate-300 font-medium flex items-center gap-1">
              <Clock className="w-3.5 h-3.5 text-blue-400" />
              Время начальной подготовки:
            </span>
            <span className="text-blue-400 font-bold bg-blue-950/60 px-2 py-0.5 rounded border border-blue-800/50">
              {config.prep_phase_duration} сек
            </span>
          </div>
          <input
            type="range"
            min={15}
            max={120}
            step={5}
            value={config.prep_phase_duration}
            onChange={(e) => onChangeConfig({ ...config, prep_phase_duration: Number(e.target.value) })}
            className="w-full h-1.5 bg-slate-800 rounded-lg cursor-pointer accent-blue-400"
          />
        </div>

        {/* Rest duration */}
        <div className="bg-slate-950/70 border border-slate-800 rounded-lg p-3 flex flex-col gap-2">
          <div className="flex justify-between items-center text-xs">
            <span className="text-slate-300 font-medium flex items-center gap-1">
              <Clock className="w-3.5 h-3.5 text-indigo-400" />
              Передышка между волнами:
            </span>
            <span className="text-indigo-400 font-bold bg-indigo-950/60 px-2 py-0.5 rounded border border-indigo-800/50">
              {config.wave_rest_duration} сек
            </span>
          </div>
          <input
            type="range"
            min={5}
            max={60}
            step={5}
            value={config.wave_rest_duration}
            onChange={(e) => onChangeConfig({ ...config, wave_rest_duration: Number(e.target.value) })}
            className="w-full h-1.5 bg-slate-800 rounded-lg cursor-pointer accent-indigo-400"
          />
        </div>
      </div>
      {/* Моделирование исследования и сбора ресурсов (2 независимых источника) */}
      <div className="border-t border-slate-800/80 pt-4 flex flex-col gap-3">
        <div className="flex flex-wrap items-center justify-between gap-2">
          <div className="flex items-center gap-2">
            <PackageOpen className="w-4 h-4 text-amber-400" />
            <h3 className="text-xs text-slate-200 font-bold">
              Сбор ресурсов на локации (Моделирование исследования):
            </h3>
          </div>

          {/* Динамический бейдж одного из 4 статусов */}
          {(() => {
            const hasStd = config.standard_loot_found ?? true;
            const hasSec = config.puzzle_secret_found ?? false;

            if (!hasStd && !hasSec) {
              return (
                <span className="text-xs font-bold px-3 py-1 rounded-full bg-slate-900 text-slate-300 border border-slate-700 shadow-sm flex items-center gap-1.5">
                  <span className="w-2 h-2 rounded-full bg-slate-500" />
                  🛑 Исходный остаток (Минимум)
                </span>
              );
            }
            if (hasStd && !hasSec) {
              return (
                <span className="text-xs font-bold px-3 py-1 rounded-full bg-sky-950/80 text-sky-300 border border-sky-600/60 shadow-sm flex items-center gap-1.5">
                  <span className="w-2 h-2 rounded-full bg-sky-400 animate-pulse" />
                  ⚖️ Стандартный сбор (Обычные ящики)
                </span>
              );
            }
            if (!hasStd && hasSec) {
              return (
                <span className="text-xs font-bold px-3 py-1 rounded-full bg-amber-950/80 text-amber-300 border border-amber-600/60 shadow-sm flex items-center gap-1.5">
                  <span className="w-2 h-2 rounded-full bg-amber-400 animate-pulse" />
                  🧩 Только редкий тайник (Обычные ящики пропущены)
                </span>
              );
            }
            return (
              <span className="text-xs font-bold px-3 py-1 rounded-full bg-emerald-950/90 text-emerald-300 border border-emerald-500/80 shadow-md shadow-emerald-500/20 flex items-center gap-1.5">
                <span className="w-2 h-2 rounded-full bg-emerald-400 animate-pulse" />
                💎 Полная зачистка 100% (Максимум)
              </span>
            );
          })()}
        </div>

        {/* 2 Независимых переключателя */}
        <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
          {/* 1. Обычные ящики локации (Standard) */}
          <div
            className={`p-3.5 rounded-xl border transition-all flex flex-col justify-between gap-3 ${
              (config.standard_loot_found ?? true)
                ? 'bg-sky-950/30 border-sky-600/60 shadow-md shadow-sky-950/40 ring-1 ring-sky-500/30'
                : 'bg-slate-950/40 border-slate-800 text-slate-500 opacity-70'
            }`}
          >
            <div className="flex items-center justify-between">
              <div className="flex items-center gap-2">
                <Box className={`w-4 h-4 ${(config.standard_loot_found ?? true) ? 'text-sky-400' : 'text-slate-500'}`} />
                <div>
                  <span className={`text-xs font-black ${(config.standard_loot_found ?? true) ? 'text-sky-300' : 'text-slate-400'}`}>
                    🟦 Обычные ящики локации (Standard)
                  </span>
                  <div className="text-[10px] text-slate-400">Синий тир редкости • Расходники и базовый лут</div>
                </div>
              </div>

              <button
                type="button"
                onClick={() => onChangeConfig({
                  ...config,
                  standard_loot_found: !(config.standard_loot_found ?? true)
                })}
                className={`px-3 py-1 rounded-full text-xs font-black tracking-wider flex items-center gap-1.5 transition-all cursor-pointer ${
                  (config.standard_loot_found ?? true)
                    ? 'bg-sky-500 text-slate-950 hover:bg-sky-400 shadow-sm shadow-sky-500/30'
                    : 'bg-slate-800 text-slate-400 hover:bg-slate-700 hover:text-slate-200 border border-slate-700'
                }`}
              >
                <Power className="w-3 h-3" />
                {(config.standard_loot_found ?? true) ? 'НАЙДЕНЫ' : 'ПРОПУЩЕНЫ'}
              </button>
            </div>

            <p className="text-[11px] text-slate-400 leading-tight">
              {(config.standard_loot_found ?? true)
                ? 'Игрок собрал базовые контейнеры снабжения по основному пути: боезапас пополнен, есть расходные материалы.'
                : 'Игрок не открывал обычные ящики по пути. В бой идут только патроны и аптечки, оставшиеся с прошлого этапа.'}
            </p>
          </div>

          {/* 2. Тайники за загадками (Maximal) */}
          <div
            className={`p-3.5 rounded-xl border transition-all flex flex-col justify-between gap-3 ${
              (config.puzzle_secret_found ?? false)
                ? 'bg-amber-950/30 border-amber-500/70 shadow-md shadow-amber-950/40 ring-1 ring-amber-500/40'
                : 'bg-slate-950/40 border-slate-800 text-slate-500 opacity-70'
            }`}
          >
            <div className="flex items-center justify-between">
              <div className="flex items-center gap-2">
                <KeyRound className={`w-4 h-4 ${(config.puzzle_secret_found ?? false) ? 'text-amber-400' : 'text-slate-500'}`} />
                <div>
                  <span className={`text-xs font-black ${(config.puzzle_secret_found ?? false) ? 'text-amber-300' : 'text-slate-400'}`}>
                    🟨 Тайники за загадками (Maximal)
                  </span>
                  <div className="text-[10px] text-slate-400">Золотой тир редкости • Редкие награды и спецсредства</div>
                </div>
              </div>

              <button
                type="button"
                onClick={() => onChangeConfig({
                  ...config,
                  puzzle_secret_found: !(config.puzzle_secret_found ?? false)
                })}
                className={`px-3 py-1 rounded-full text-xs font-black tracking-wider flex items-center gap-1.5 transition-all cursor-pointer ${
                  (config.puzzle_secret_found ?? false)
                    ? 'bg-amber-400 text-slate-950 hover:bg-amber-300 shadow-sm shadow-amber-400/40'
                    : 'bg-slate-800 text-slate-400 hover:bg-slate-700 hover:text-slate-200 border border-slate-700'
                }`}
              >
                <Power className="w-3 h-3" />
                {(config.puzzle_secret_found ?? false) ? 'РАЗГАДАНО' : 'НЕ НАЙДЕНО'}
              </button>
            </div>

            <p className="text-[11px] text-slate-400 leading-tight">
              {(config.puzzle_secret_found ?? false)
                ? 'Игрок решил сложную загадку локации и открыл секретный крио-сейф: получены редкие средства (турели, мины, аптечки).'
                : 'Секрет не разгадан или пропущен. Доступ к редким специальным ресурсам заблокирован.'}
            </p>
          </div>
        </div>
      </div>
    </div>
  );
};