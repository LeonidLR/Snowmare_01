import React from 'react';
import { WaveConfig, EnemyType, ENEMY_DB, SpawnLane } from '../types';
import { Plus, Trash2, Shield, Flame, Wind, Snowflake, Users, ChevronLeft, ChevronRight, Terminal, Loader2, Power, Settings2, ChevronDown, ChevronUp, RotateCcw } from 'lucide-react';

interface Props {
  wave: WaveConfig;
  totalWaves: number;
  onUpdateWave: (updatedWave: WaveConfig) => void;
  onSelectWave: (index: number) => void;
  onDeleteWave: (index: number) => void;
  onDuplicateWave: (index: number) => void;
  onToggleActive?: (active: boolean) => void;
  onRunBot?: () => void;
  isBotRunning?: boolean;
  onApplyColdToAllWaves?: (cold: number) => void;
}

export const WaveEditorCard: React.FC<Props> = ({
  wave,
  totalWaves,
  onUpdateWave,
  onSelectWave,
  onDeleteWave,
  onDuplicateWave,
  onToggleActive,
  onRunBot,
  isBotRunning = false,
  onApplyColdToAllWaves,
}) => {
  const [expandedEnemy, setExpandedEnemy] = React.useState<EnemyType | null>(null);

  const handleUpdateEnemyStat = (enemyType: EnemyType, statName: string, value: number) => {
    const existingSpawns = [...wave.spawns];
    const spawnIdx = existingSpawns.findIndex(s => s.enemy_type === enemyType);
    if (spawnIdx >= 0) {
      const sp = { ...existingSpawns[spawnIdx] };
      sp.custom_stats = {
        ...(sp.custom_stats || {}),
        [statName]: value
      };
      existingSpawns[spawnIdx] = sp;
      onUpdateWave({ ...wave, spawns: existingSpawns });
    }
  };

  const handleResetEnemyStats = (enemyType: EnemyType) => {
    const existingSpawns = [...wave.spawns];
    const spawnIdx = existingSpawns.findIndex(s => s.enemy_type === enemyType);
    if (spawnIdx >= 0) {
      const sp = { ...existingSpawns[spawnIdx] };
      delete sp.custom_stats;
      existingSpawns[spawnIdx] = sp;
      onUpdateWave({ ...wave, spawns: existingSpawns });
    }
  };
  const handleUpdateSpawnCount = (type: EnemyType, count: number) => {
    let newSpawns = [...wave.spawns];
    const existingIdx = newSpawns.findIndex((s) => s.enemy_type === type);
    if (count <= 0) {
      if (existingIdx !== -1) {
        newSpawns.splice(existingIdx, 1);
      }
    } else {
      if (existingIdx !== -1) {
        newSpawns[existingIdx] = { ...newSpawns[existingIdx], count };
      } else {
        newSpawns.push({
          enemy_type: type,
          count,
          spawn_lane: 'ANY',
          spawn_delay_sec: 1.0,
          initial_delay_sec: 0.0
        });
      }
    }
    onUpdateWave({ ...wave, spawns: newSpawns });
  };

  const handleUpdateSpawnLane = (type: EnemyType, lane: SpawnLane) => {
    const newSpawns = wave.spawns.map((s) => (s.enemy_type === type ? { ...s, spawn_lane: lane } : s));
    onUpdateWave({ ...wave, spawns: newSpawns });
  };

  const handleUpdateModifier = (key: keyof typeof wave.wave_modifiers, val: number) => {
    onUpdateWave({
      ...wave,
      wave_modifiers: {
        ...wave.wave_modifiers,
        [key]: val
      }
    });
  };

  const getSpawnCount = (type: EnemyType) => {
    const s = wave.spawns.find((item) => item.enemy_type === type);
    return s ? s.count : 0;
  };

  const getSpawnLane = (type: EnemyType): SpawnLane => {
    const s = wave.spawns.find((item) => item.enemy_type === type);
    return s ? s.spawn_lane : 'ANY';
  };

  const totalEnemiesInWave = wave.spawns.reduce((sum, s) => sum + s.count, 0);

  return (
    <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-xl flex flex-col gap-4">
      <div className="flex flex-wrap items-center justify-between gap-3 border-b border-slate-800 pb-3">
        <div className="flex items-center gap-3">
          <span className="w-8 h-8 rounded-lg bg-sky-500/20 text-sky-400 font-black text-sm flex items-center justify-center border border-sky-500/30">
            {wave.wave_index}
          </span>
          <div>
            <div className="flex items-center gap-3">
              <h3 className="text-base font-bold text-slate-100 flex items-center gap-2">
                Настройка Волны {wave.wave_index}
                <span className="text-xs font-normal text-slate-400 bg-slate-800 px-2 py-0.5 rounded-full">
                  Всего врагов: {totalEnemiesInWave}
                </span>
              </h3>

              {onToggleActive && (
                <button
                  type="button"
                  onClick={() => onToggleActive(wave.is_active === false)}
                  className={`px-2.5 py-0.5 rounded-full text-xs font-black tracking-wider flex items-center gap-1 transition-all cursor-pointer ${
                    wave.is_active !== false
                      ? 'bg-emerald-500 text-slate-950 hover:bg-emerald-400 shadow-sm shadow-emerald-500/30'
                      : 'bg-slate-800 text-slate-400 hover:bg-slate-700 hover:text-slate-200 border border-slate-700'
                  }`}
                  title={wave.is_active !== false ? 'Волна включена в бой. Нажмите, чтобы выключить.' : 'Волна отключена. Нажмите, чтобы включить в бой.'}
                >
                  <Power className="w-3 h-3" />
                  {wave.is_active !== false ? 'ВКЛЮЧЕНА В БОЙ' : 'ОТКЛЮЧЕНА (ВЫКЛ)'}
                </button>
              )}
            </div>
          </div>
        </div>

        <div className="flex items-center gap-2">
          <button
            disabled={wave.wave_index <= 1}
            onClick={() => onSelectWave(wave.wave_index - 1)}
            className="p-1.5 rounded-lg bg-slate-800 hover:bg-slate-700 disabled:opacity-30 disabled:hover:bg-slate-800 text-slate-300 transition-all"
            title="Предыдущая волна"
          >
            <ChevronLeft className="w-4 h-4" />
          </button>
          <span className="text-xs text-slate-400 font-mono">
            {wave.wave_index} / {totalWaves}
          </span>
          <button
            disabled={wave.wave_index >= totalWaves}
            onClick={() => onSelectWave(wave.wave_index + 1)}
            className="p-1.5 rounded-lg bg-slate-800 hover:bg-slate-700 disabled:opacity-30 disabled:hover:bg-slate-800 text-slate-300 transition-all"
            title="Следующая волна"
          >
            <ChevronRight className="w-4 h-4" />
          </button>

          <button
            onClick={() => onDuplicateWave(wave.wave_index)}
            className="ml-2 px-2.5 py-1.5 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-300 text-xs flex items-center gap-1 transition-all"
            title="Дублировать волну"
          >
            <Plus className="w-3.5 h-3.5" />
            Дублировать
          </button>

          {onRunBot && (
            <button
              disabled={isBotRunning}
              onClick={onRunBot}
              className="ml-2 px-3 py-1.5 rounded-lg bg-gradient-to-r from-cyan-500 to-blue-600 hover:from-cyan-400 hover:to-blue-500 disabled:opacity-50 text-slate-950 text-xs font-black flex items-center gap-1.5 transition-all shadow-md shadow-cyan-900/30 active:scale-95 cursor-pointer"
              title="Применить текущие параметры и запустить автономный тест волн на ботах"
            >
              {isBotRunning ? (
                <>
                  <Loader2 className="w-3.5 h-3.5 animate-spin text-slate-950" />
                  <span>Запуск консоли...</span>
                </>
              ) : (
                <>
                  <Terminal className="w-3.5 h-3.5 text-slate-950" />
                  <span>Перезапустить бота (.bat)</span>
                </>
              )}
            </button>
          )}

          {totalWaves > 1 && (
            <button
              onClick={() => onDeleteWave(wave.wave_index)}
              className="p-1.5 rounded-lg bg-red-950/40 border border-red-800/40 hover:bg-red-900/60 text-red-400 text-xs transition-all"
              title="Удалить волну"
            >
              <Trash2 className="w-4 h-4" />
            </button>
          )}
        </div>
      </div>

      <div className="flex flex-col gap-3">
        <label className="text-xs text-slate-400 font-medium flex items-center gap-1.5">
          <Users className="w-3.5 h-3.5 text-sky-400" />
          Количество и направления спавна врагов:
        </label>

        <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
          {(['HOUND', 'CUTTER', 'SPITTER', 'BRUTE', 'FROSTBITTEN', 'MARKSMAN'] as EnemyType[]).map((type) => {
            const meta = ENEMY_DB[type];
            const count = getSpawnCount(type);
            const lane = getSpawnLane(type);
            const sp = wave.spawns.find((s) => s.enemy_type === type);
            const activeHp = sp?.custom_stats?.health ?? meta.baseHp;
            const activeDmg = sp?.custom_stats?.damage ?? meta.baseDps;
            const activeSpd = sp?.custom_stats?.speed ?? meta.speed;
            const isModified = Boolean(sp?.custom_stats && Object.keys(sp.custom_stats).length > 0);

            return (
              <div
                key={type}
                className="bg-slate-950/70 border border-slate-800/80 rounded-lg p-3 flex flex-col gap-2 transition-all hover:border-slate-700"
              >
                <div className="flex items-center justify-between">
                  <div className="flex items-center gap-2">
                    <span className="text-lg">{meta.icon}</span>
                    <div>
                      <div className="flex items-center gap-1.5">
                        <span className="text-xs font-bold text-slate-200">{meta.name}</span>
                        {isModified && (
                          <span className="text-[9px] bg-sky-500/20 text-sky-300 font-bold px-1.5 py-0.5 rounded border border-sky-500/30">
                            модиф.
                          </span>
                        )}
                      </div>
                      <div className={`text-[10px] ${isModified ? 'text-sky-400 font-medium' : 'text-slate-500'}`}>
                        {activeHp} HP | {activeDmg} DPS | Скор: {activeSpd}
                      </div>
                    </div>
                  </div>

                  <div className="flex items-center gap-2">
                    <select
                      value={lane}
                      onChange={(e) => handleUpdateSpawnLane(type, e.target.value as SpawnLane)}
                      className="bg-slate-900 border border-slate-800 rounded text-[11px] text-slate-300 py-1 px-1.5 focus:outline-none focus:border-sky-500"
                    >
                      <option value="ANY">Любой фланг</option>
                      <option value="NORTH_GATE">Северные ворота</option>
                      <option value="WEST_FLANK">Западный фланг</option>
                      <option value="EAST_FLANK">Восточный фланг</option>
                    </select>

                    <span
                      className="text-xs font-black px-2 py-0.5 rounded min-w-8 text-center"
                      style={{ backgroundColor: `${meta.color}25`, color: meta.color }}
                    >
                      {count} шт
                    </span>
                  </div>
                </div>

                <input
                  type="range"
                  min={0}
                  max={100}
                  step={1}
                  value={count}
                  onChange={(e) => handleUpdateSpawnCount(type, Number(e.target.value))}
                  className="w-full h-1.5 bg-slate-800 rounded-lg cursor-pointer"
                  style={{ accentColor: meta.color }}
                />

                {/* Кнопка раскрытия персональных характеристик врага */}
                <div className="border-t border-slate-800/80 pt-2 flex items-center justify-between">
                  <button
                    type="button"
                    onClick={() => setExpandedEnemy(expandedEnemy === type ? null : type)}
                    className="text-[11px] font-bold text-slate-400 hover:text-sky-300 flex items-center gap-1 transition-colors cursor-pointer"
                  >
                    <Settings2 className="w-3 h-3 text-sky-400" />
                    <span>⚙️ Параметры {meta.name}</span>
                    {expandedEnemy === type ? <ChevronUp className="w-3 h-3" /> : <ChevronDown className="w-3 h-3" />}
                  </button>

                  {wave.spawns.find(s => s.enemy_type === type)?.custom_stats && (
                    <button
                      type="button"
                      onClick={() => handleResetEnemyStats(type)}
                      className="text-[10px] text-amber-400/80 hover:text-amber-300 flex items-center gap-1 cursor-pointer"
                      title="Сбросить к базовым параметрам"
                    >
                      <RotateCcw className="w-2.5 h-2.5" />
                      Сброс
                    </button>
                  )}
                </div>

                {/* Раскрывающийся блок точной настройки параметров врага */}
                {expandedEnemy === type && (() => {
                  const sp = wave.spawns.find(s => s.enemy_type === type);
                  const cs = sp?.custom_stats || {};
                  const currentHp = cs.health ?? meta.baseHp;
                  const currentDmg = cs.damage ?? meta.baseDps;
                  const currentSpd = cs.speed ?? meta.speed;
                  const currentRng = cs.attack_range ?? (type === 'SPITTER' ? 15.0 : (type === 'BRUTE' ? 2.4 : 1.8));
                  const currentCd = cs.attack_cooldown ?? (type === 'SPITTER' ? 2.2 : (type === 'BRUTE' ? 2.0 : 1.0));

                  return (
                    <div className="bg-slate-900/90 border border-slate-700/60 rounded-lg p-2.5 flex flex-col gap-2 mt-1 animate-fade-in text-[11px]">
                      {/* HP */}
                      <div className="flex flex-col gap-1">
                        <div className="flex justify-between items-center text-slate-300">
                          <span className="flex items-center gap-1"><Shield className="w-3 h-3 text-emerald-400" /> Здоровье (HP):</span>
                          <span className="font-bold text-emerald-400 font-mono">{currentHp} HP</span>
                        </div>
                        <input
                          type="range"
                          min={10}
                          max={500}
                          step={5}
                          value={currentHp}
                          onChange={(e) => handleUpdateEnemyStat(type, 'health', Number(e.target.value))}
                          className="w-full h-1 bg-slate-800 rounded cursor-pointer accent-emerald-400"
                        />
                      </div>

                      {/* Damage */}
                      <div className="flex flex-col gap-1">
                        <div className="flex justify-between items-center text-slate-300">
                          <span className="flex items-center gap-1"><Flame className="w-3 h-3 text-orange-400" /> Урон (Damage):</span>
                          <span className="font-bold text-orange-400 font-mono">{currentDmg}</span>
                        </div>
                        <input
                          type="range"
                          min={2}
                          max={100}
                          step={1}
                          value={currentDmg}
                          onChange={(e) => handleUpdateEnemyStat(type, 'damage', Number(e.target.value))}
                          className="w-full h-1 bg-slate-800 rounded cursor-pointer accent-orange-400"
                        />
                      </div>

                      {/* Speed */}
                      <div className="flex flex-col gap-1">
                        <div className="flex justify-between items-center text-slate-300">
                          <span className="flex items-center gap-1"><Wind className="w-3 h-3 text-sky-400" /> Скорость бега:</span>
                          <span className="font-bold text-sky-400 font-mono">{currentSpd.toFixed(1)} м/с</span>
                        </div>
                        <input
                          type="range"
                          min={1.0}
                          max={15.0}
                          step={0.2}
                          value={currentSpd}
                          onChange={(e) => handleUpdateEnemyStat(type, 'speed', Number(e.target.value))}
                          className="w-full h-1 bg-slate-800 rounded cursor-pointer accent-sky-400"
                        />
                      </div>

                      {/* Attack Range */}
                      <div className="flex flex-col gap-1">
                        <div className="flex justify-between items-center text-slate-300">
                          <span>🎯 Дистанция атаки:</span>
                          <span className="font-bold text-indigo-400 font-mono">{currentRng.toFixed(1)} м</span>
                        </div>
                        <input
                          type="range"
                          min={1.0}
                          max={25.0}
                          step={0.5}
                          value={currentRng}
                          onChange={(e) => handleUpdateEnemyStat(type, 'attack_range', Number(e.target.value))}
                          className="w-full h-1 bg-slate-800 rounded cursor-pointer accent-indigo-400"
                        />
                      </div>

                      {/* Cooldown */}
                      <div className="flex flex-col gap-1">
                        <div className="flex justify-between items-center text-slate-300">
                          <span>⏱️ Кулдаун удара:</span>
                          <span className="font-bold text-purple-400 font-mono">{currentCd.toFixed(1)} с</span>
                        </div>
                        <input
                          type="range"
                          min={0.2}
                          max={4.0}
                          step={0.1}
                          value={currentCd}
                          onChange={(e) => handleUpdateEnemyStat(type, 'attack_cooldown', Number(e.target.value))}
                          className="w-full h-1 bg-slate-800 rounded cursor-pointer accent-purple-400"
                        />
                      </div>
                    </div>
                  );
                })()}
              </div>
            );
          })}
        </div>
      </div>

      <div className="border-t border-slate-800/80 pt-3 flex flex-col gap-2">
        <label className="text-xs text-slate-400 font-medium">Множители волны (Локальные модификаторы):</label>
        
        <div className="grid grid-cols-1 sm:grid-cols-3 gap-3 text-xs">
          <div className="bg-slate-950/70 border border-slate-800 rounded-lg p-2.5 flex flex-col gap-1.5">
            <div className="flex justify-between items-center text-slate-400 text-[11px]">
              <span className="flex items-center gap-1">
                <Shield className="w-3 h-3 text-emerald-400" />
                Множитель HP орды
              </span>
              <span className="font-bold text-emerald-400">{(wave.wave_modifiers?.enemy_hp_mult ?? 1.0).toFixed(2)}x</span>
            </div>
            <input
              type="range"
              min={0.8}
              max={2.5}
              step={0.05}
              value={wave.wave_modifiers?.enemy_hp_mult || 1.0}
              onChange={(e) => handleUpdateModifier('enemy_hp_mult', Number(e.target.value))}
              className="w-full h-1 bg-slate-800 rounded cursor-pointer accent-emerald-400"
            />
          </div>

          <div className="bg-slate-950/70 border border-slate-800 rounded-lg p-2.5 flex flex-col gap-1.5">
            <div className="flex justify-between items-center text-slate-400 text-[11px]">
              <span className="flex items-center gap-1">
                <Flame className="w-3 h-3 text-orange-400" />
                Множитель урона
              </span>
              <span className="font-bold text-orange-400">{(wave.wave_modifiers?.enemy_damage_mult ?? 1.0).toFixed(2)}x</span>
            </div>
            <input
              type="range"
              min={0.8}
              max={2.2}
              step={0.05}
              value={wave.wave_modifiers?.enemy_damage_mult || 1.0}
              onChange={(e) => handleUpdateModifier('enemy_damage_mult', Number(e.target.value))}
              className="w-full h-1 bg-slate-800 rounded cursor-pointer accent-orange-400"
            />
          </div>

          <div className="bg-slate-950/70 border border-slate-800 rounded-lg p-2.5 flex flex-col gap-1.5">
            <div className="flex justify-between items-center text-slate-400 text-[11px]">
              <span className="flex items-center gap-1">
                <Wind className="w-3 h-3 text-sky-400" />
                Скорость бега
              </span>
              <span className="font-bold text-sky-400">{(wave.wave_modifiers?.enemy_speed_mult ?? 1.0).toFixed(2)}x</span>
            </div>
            <input
              type="range"
              min={0.8}
              max={1.6}
              step={0.05}
              value={wave.wave_modifiers?.enemy_speed_mult || 1.0}
              onChange={(e) => handleUpdateModifier('enemy_speed_mult', Number(e.target.value))}
              className="w-full h-1 bg-slate-800 rounded cursor-pointer accent-sky-400"
            />
          </div>
        </div>
      </div>

      {/* Полноценный блок управления окружением и силой холода */}
      <div className="border-t border-slate-800/80 pt-3 flex flex-col gap-2.5">
        <div className="flex items-center justify-between">
          <label className="text-xs text-slate-300 font-medium flex items-center gap-1.5">
            <Snowflake className="w-3.5 h-3.5 text-cyan-400" />
            Окружение арены и сила холода:
          </label>
          <span className={`text-xs font-bold px-2.5 py-0.5 rounded-full border transition-all ${
            (wave.wave_modifiers?.cold_drain_mult ?? 1.0) <= 0.001
              ? 'bg-amber-950/60 text-amber-400 border-amber-800/60 shadow-sm'
              : (wave.wave_modifiers?.cold_drain_mult ?? 1.0) < 1.0
              ? 'bg-emerald-950/60 text-emerald-400 border-emerald-800/60'
              : (wave.wave_modifiers?.cold_drain_mult ?? 1.0) < 1.8
              ? 'bg-sky-950/60 text-sky-400 border-sky-800/60'
              : 'bg-cyan-950/80 text-cyan-300 border-cyan-500/60 shadow-md shadow-cyan-500/20'
          }`}>
            {(wave.wave_modifiers?.cold_drain_mult ?? 1.0) <= 0.001
              ? '🏠 Закрытое помещение (Отапливаемый бункер, 0.0x)'
              : (wave.wave_modifiers?.cold_drain_mult ?? 1.0) < 1.0
              ? `🛡️ Полуоткрытое укрытие (${(wave.wave_modifiers?.cold_drain_mult ?? 1.0).toFixed(1)}x)`
              : (wave.wave_modifiers?.cold_drain_mult ?? 1.0) < 1.8
              ? `❄️ Открытая местность (${(wave.wave_modifiers?.cold_drain_mult ?? 1.0).toFixed(1)}x)`
              : `🌨️ Экстремальный буран (${(wave.wave_modifiers?.cold_drain_mult ?? 1.0).toFixed(1)}x)`
            }
          </span>
        </div>

        {/* Пресеты окружения */}
        <div className="grid grid-cols-2 sm:grid-cols-4 gap-2 text-xs">
          <button
            onClick={() => handleUpdateModifier('cold_drain_mult', 0.0)}
            className={`p-2.5 rounded-lg border text-left flex flex-col gap-0.5 transition-all cursor-pointer ${
              (wave.wave_modifiers?.cold_drain_mult ?? 1.0) <= 0.001
                ? 'bg-amber-950/50 border-amber-500 text-amber-300 font-bold shadow-md shadow-amber-950/40'
                : 'bg-slate-900/60 border-slate-800 text-slate-400 hover:text-slate-200 hover:bg-slate-800/50'
            }`}
          >
            <span className="flex items-center gap-1 text-[11px]">🏠 Закрытое</span>
            <span className="text-[10px] text-slate-400">Бункер (0.0x холода)</span>
          </button>
          <button
            onClick={() => handleUpdateModifier('cold_drain_mult', 0.5)}
            className={`p-2.5 rounded-lg border text-left flex flex-col gap-0.5 transition-all cursor-pointer ${
              Math.abs((wave.wave_modifiers?.cold_drain_mult ?? 1.0) - 0.5) < 0.1
                ? 'bg-emerald-950/50 border-emerald-500 text-emerald-300 font-bold shadow-md shadow-emerald-950/40'
                : 'bg-slate-900/60 border-slate-800 text-slate-400 hover:text-slate-200 hover:bg-slate-800/50'
            }`}
          >
            <span className="flex items-center gap-1 text-[11px]">🛡️ Укрытие</span>
            <span className="text-[10px] text-slate-400">Умеренный мороз (0.5x)</span>
          </button>
          <button
            onClick={() => handleUpdateModifier('cold_drain_mult', 1.0)}
            className={`p-2.5 rounded-lg border text-left flex flex-col gap-0.5 transition-all cursor-pointer ${
              Math.abs((wave.wave_modifiers?.cold_drain_mult ?? 1.0) - 1.0) < 0.1
                ? 'bg-sky-950/50 border-sky-500 text-sky-300 font-bold shadow-md shadow-sky-950/40'
                : 'bg-slate-900/60 border-slate-800 text-slate-400 hover:text-slate-200 hover:bg-slate-800/50'
            }`}
          >
            <span className="flex items-center gap-1 text-[11px]">❄️ Стандарт</span>
            <span className="text-[10px] text-slate-400">Открытый воздух (1.0x)</span>
          </button>
          <button
            onClick={() => handleUpdateModifier('cold_drain_mult', 2.5)}
            className={`p-2.5 rounded-lg border text-left flex flex-col gap-0.5 transition-all cursor-pointer ${
              (wave.wave_modifiers?.cold_drain_mult ?? 1.0) >= 2.0
                ? 'bg-cyan-950/60 border-cyan-400 text-cyan-200 font-bold shadow-md shadow-cyan-950/50'
                : 'bg-slate-900/60 border-slate-800 text-slate-400 hover:text-slate-200 hover:bg-slate-800/50'
            }`}
          >
            <span className="flex items-center gap-1 text-[11px]">🌨️ Буран</span>
            <span className="text-[10px] text-slate-400">Экстремальный (2.5x)</span>
          </button>
        </div>

        {/* Точный слайдер силы холода */}
        <div className="flex items-center gap-3 bg-slate-950/70 border border-slate-800 rounded-lg px-3 py-2">
          <input
            type="range"
            min={0.0}
            max={5.0}
            step={0.1}
            value={wave.wave_modifiers?.cold_drain_mult ?? 1.0}
            onChange={(e) => handleUpdateModifier('cold_drain_mult', Number(e.target.value))}
            className="w-full h-1.5 bg-slate-800 rounded cursor-pointer accent-cyan-400"
          />
          <span className="text-xs font-mono font-bold text-cyan-400 w-12 text-right">
            {(wave.wave_modifiers?.cold_drain_mult ?? 1.0).toFixed(1)}x
          </span>

          {onApplyColdToAllWaves && (
            <button
              type="button"
              onClick={() => onApplyColdToAllWaves(wave.wave_modifiers?.cold_drain_mult ?? 1.0)}
              className="px-2.5 py-1 bg-slate-800 hover:bg-slate-700 text-cyan-300 text-[11px] font-semibold rounded border border-slate-700 whitespace-nowrap transition-colors cursor-pointer"
              title="Применить это значение холода ко ВСЕМ волнам уровня"
            >
              Ко всем волнам
            </button>
          )}
        </div>
      </div>
    </div>
  );
};
