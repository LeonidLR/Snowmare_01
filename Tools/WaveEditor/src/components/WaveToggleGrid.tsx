import React from 'react';
import { WaveConfig } from '../types';
import { Layers, Power, Snowflake, Users } from 'lucide-react';

interface Props {
  waves: WaveConfig[];
  selectedWaveIndex: number;
  onSelectWave: (index: number) => void;
  onToggleWaveActive: (index: number, active: boolean) => void;
}

export const WaveToggleGrid: React.FC<Props> = ({
  waves,
  selectedWaveIndex,
  onSelectWave,
  onToggleWaveActive,
}) => {
  // 12 фиксированных слотов волн
  const slots = Array.from({ length: 12 }, (_, i) => i + 1);

  // Находим конфиг волны для слота
  const getWaveForSlot = (index: number): WaveConfig | undefined => {
    return waves.find((w) => w.wave_index === index);
  };

  const isWaveActive = (index: number): boolean => {
    const w = getWaveForSlot(index);
    if (!w) return false;
    return w.is_active !== false;
  };

  const activeCount = slots.filter((idx) => isWaveActive(idx)).length;

  return (
    <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-xl flex flex-col gap-3">
      <div className="flex flex-wrap items-center justify-between gap-3 border-b border-slate-800 pb-3">
        <div className="flex items-center gap-2">
          <Layers className="w-5 h-5 text-sky-400" />
          <div>
            <h2 className="text-base font-bold text-slate-100 flex items-center gap-2">
              Управление 12 волнами боя
              <span className="text-xs font-normal bg-sky-950/80 text-sky-300 border border-sky-800/60 px-2.5 py-0.5 rounded-full">
                Активно в бою: {activeCount} из 12
              </span>
            </h2>
            <p className="text-xs text-slate-400 mt-0.5">
              Включайте и выключайте волны кнопками. В боевой уровень войдут только включенные волны.
            </p>
          </div>
        </div>

        <div className="flex items-center gap-2 text-xs">
          <span className="flex items-center gap-1 text-emerald-400 font-semibold bg-emerald-950/40 border border-emerald-800/40 px-2 py-1 rounded">
            <span className="w-2 h-2 rounded-full bg-emerald-400 animate-pulse" />
            ВКЛ — участвует в бою
          </span>
          <span className="flex items-center gap-1 text-slate-400 font-semibold bg-slate-950 border border-slate-800 px-2 py-1 rounded">
            <span className="w-2 h-2 rounded-full bg-slate-600" />
            ВЫКЛ — отключена
          </span>
        </div>
      </div>

      {/* Сетка 12 отдельных кнопок-волн */}
      <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-4 lg:grid-cols-6 gap-2.5 pt-1">
        {slots.map((idx) => {
          const wave = getWaveForSlot(idx);
          const active = isWaveActive(idx);
          const isSelected = selectedWaveIndex === idx;

          let totalMobs = 0;
          let coldMult = 1.0;
          if (wave) {
            totalMobs = wave.spawns.reduce((sum, s) => sum + s.count, 0);
            coldMult = wave.wave_modifiers?.cold_drain_mult ?? 1.0;
          }

          return (
            <div
              key={idx}
              className={`relative rounded-xl p-3 border transition-all flex flex-col justify-between gap-2.5 select-none cursor-pointer ${
                isSelected
                  ? 'ring-2 ring-sky-400 border-sky-400 shadow-lg shadow-sky-500/10'
                  : ''
              } ${
                active
                  ? 'bg-slate-950/90 border-slate-700 hover:border-slate-600 hover:bg-slate-900'
                  : 'bg-slate-950/40 border-slate-800/60 opacity-60 hover:opacity-90 hover:border-slate-700'
              }`}
              onClick={() => onSelectWave(idx)}
            >
              {/* Шапка плитки: Номер и Тумблер */}
              <div className="flex items-center justify-between">
                <span className={`text-xs font-black tracking-wide ${active ? 'text-slate-100' : 'text-slate-500'}`}>
                  Волна {idx}
                </span>

                <button
                  type="button"
                  onClick={(e) => {
                    e.stopPropagation();
                    onToggleWaveActive(idx, !active);
                  }}
                  className={`px-2 py-0.5 rounded-full text-[10px] font-black tracking-wider flex items-center gap-1 transition-all cursor-pointer ${
                    active
                      ? 'bg-emerald-500 text-slate-950 hover:bg-emerald-400 shadow-sm shadow-emerald-500/30'
                      : 'bg-slate-800 text-slate-400 hover:bg-slate-700 hover:text-slate-200 border border-slate-700'
                  }`}
                  title={active ? `Отключить волну ${idx}` : `Включить волну ${idx} в бой`}
                >
                  <Power className="w-2.5 h-2.5" />
                  {active ? 'ВКЛ' : 'ВЫКЛ'}
                </button>
              </div>

              {/* Инфо-блок */}
              <div className="flex flex-col gap-1">
                {active ? (
                  <div className="flex items-center justify-between text-[11px] text-slate-400">
                    <span className="flex items-center gap-1 text-slate-300">
                      <Users className="w-3 h-3 text-sky-400" />
                      {totalMobs} моб.
                    </span>
                    <span className={`flex items-center gap-0.5 text-[10px] font-semibold ${
                      coldMult <= 0.001 ? 'text-amber-400' : coldMult >= 1.5 ? 'text-cyan-300' : 'text-slate-400'
                    }`}>
                      <Snowflake className="w-2.5 h-2.5" />
                      {coldMult.toFixed(1)}x
                    </span>
                  </div>
                ) : (
                  <div className="text-[10px] text-slate-600 italic py-0.5">
                    Не участвует в бою
                  </div>
                )}
              </div>

              {/* Бейдж выбранной волны */}
              {isSelected && (
                <div className="w-full text-center text-[9px] font-bold uppercase tracking-wider text-sky-400 bg-sky-950/80 rounded py-0.5 border border-sky-800/60">
                  В инспекторе
                </div>
              )}
            </div>
          );
        })}
      </div>
    </div>
  );
};
