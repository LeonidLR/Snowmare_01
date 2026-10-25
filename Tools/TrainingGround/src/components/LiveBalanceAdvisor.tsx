import React from 'react';
import { WaveConfig, ENEMY_DB, SquadLoadoutConfig } from '../types';
import { ShieldAlert, HeartPulse, Zap, Snowflake, CheckCircle2, AlertTriangle, Sparkles } from 'lucide-react';

interface Props {
  wave: WaveConfig;
  allWaves: WaveConfig[];
  loadout?: SquadLoadoutConfig;
}

export const LiveBalanceAdvisor: React.FC<Props> = ({
  wave,
  allWaves,
  loadout,
}) => {
  // 1. Расчет характеристик активной волны на лету
  let totalHp = 0;
  let totalDps = 0;
  let totalEnemies = 0;

  const hpMult = wave.wave_modifiers?.enemy_hp_mult ?? 1.0;
  const dmgMult = wave.wave_modifiers?.enemy_damage_mult ?? 1.0;
  const coldMult = wave.wave_modifiers?.cold_drain_mult ?? 1.0;

  wave.spawns.forEach((s) => {
    const meta = ENEMY_DB[s.enemy_type];
    if (meta) {
      const hp = s.custom_stats?.health ?? meta.baseHp;
      const dps = s.custom_stats?.damage ?? meta.baseDps;
      totalHp += s.count * hp * hpMult;
      totalDps += s.count * dps * dmgMult;
      totalEnemies += s.count;
    }
  });

  const threatIndex = Math.round((totalHp * 0.15 + totalDps * 1.8) * coldMult);

  // Боевая мощь отряда: базовые 3 бойца + турели + баррикады + мины
  const turretBonus = (loadout?.turrets_count ?? 1) * 220;
  const mineBonus = (loadout?.mines_count ?? 2) * 90;
  const barricadeBonus = (loadout?.barricades_count ?? 2) * 80;
  const squadPower = Math.round(1100 + (wave.wave_index - 1) * 320 + turretBonus + mineBonus + barricadeBonus);

  const ratio = threatIndex / Math.max(1, squadPower);
  const estimatedWinrate = Math.max(5, Math.min(99, Math.round(100 / (1 + Math.exp((ratio - 1.0) * 3.5)))));

  // Оценка бурана
  const isColdCritical = coldMult >= 1.5;

  // Определение статуса баланса
  let verdictType: 'CRITICAL' | 'HARD' | 'BALANCED' | 'EASY' = 'BALANCED';
  if (estimatedWinrate < 40 || ratio > 1.35) {
    verdictType = 'CRITICAL';
  } else if (estimatedWinrate < 65 || ratio > 1.1) {
    verdictType = 'HARD';
  } else if (estimatedWinrate > 92 && ratio < 0.6) {
    verdictType = 'EASY';
  }

  return (
    <div className={`p-4 rounded-xl border transition-all duration-300 shadow-lg ${
      verdictType === 'CRITICAL'
        ? 'bg-rose-950/40 border-rose-500/60 shadow-rose-950/30'
        : verdictType === 'HARD'
        ? 'bg-amber-950/40 border-amber-500/60 shadow-amber-950/30'
        : verdictType === 'EASY'
        ? 'bg-indigo-950/40 border-indigo-500/60 shadow-indigo-950/30'
        : 'bg-emerald-950/40 border-emerald-500/60 shadow-emerald-950/30'
    }`}>
      <div className="flex flex-col md:flex-row items-start md:items-center justify-between gap-4">
        {/* Заголовок и вердикт */}
        <div className="flex items-start gap-3">
          <div className="mt-0.5 flex-shrink-0">
            {verdictType === 'CRITICAL' ? (
              <ShieldAlert className="w-5 h-5 text-rose-400 animate-pulse" />
            ) : verdictType === 'HARD' ? (
              <AlertTriangle className="w-5 h-5 text-amber-400" />
            ) : verdictType === 'EASY' ? (
              <Sparkles className="w-5 h-5 text-indigo-400" />
            ) : (
              <CheckCircle2 className="w-5 h-5 text-emerald-400" />
            )}
          </div>

          <div>
            <div className="flex items-center gap-2">
              <span className="text-xs font-semibold uppercase tracking-wider text-slate-400">
                Живой математический анализ (Волна {wave.wave_index} из {allWaves.length})
              </span>
              <span className={`text-[11px] font-bold px-2 py-0.5 rounded-full ${
                verdictType === 'CRITICAL'
                  ? 'bg-rose-500/20 text-rose-300 border border-rose-500/40'
                  : verdictType === 'HARD'
                  ? 'bg-amber-500/20 text-amber-300 border border-amber-500/40'
                  : verdictType === 'EASY'
                  ? 'bg-indigo-500/20 text-indigo-300 border border-indigo-500/40'
                  : 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/40'
              }`}>
                {verdictType === 'CRITICAL' && '⚠️ Критическая перегрузка'}
                {verdictType === 'HARD' && '⚡ Высокая сложность'}
                {verdictType === 'EASY' && '☕ Низкая плотность'}
                {verdictType === 'BALANCED' && '✅ Оптимальный баланс'}
              </span>
              {isColdCritical && (
                <span className="text-[11px] font-bold px-2 py-0.5 rounded-full bg-cyan-500/20 text-cyan-300 border border-cyan-500/40 flex items-center gap-1">
                  <Snowflake className="w-3 h-3" /> Буран {coldMult}x
                </span>
              )}
            </div>

            {/* Динамическое описание и рекомендации в реальном времени */}
            <div className="text-xs text-slate-300 mt-1 leading-relaxed">
              {verdictType === 'CRITICAL' && (
                <>
                  Напор орды превышает живучесть отряда (прогнозируемый винрейт: <strong className="text-rose-300">{estimatedWinrate}%</strong>).
                  Орда генерирует <strong className="text-rose-300">{Math.round(totalDps)} DPS</strong> и <strong className="text-rose-300">{Math.round(totalHp)} HP</strong>.
                  <br />
                  <span className="text-rose-200 font-semibold">💡 Рекомендация на лету:</span> снизьте количество врагов (сейчас {totalEnemies}), либо уменьшите множитель урона (сейчас {dmgMult}x).
                </>
              )}
              {verdictType === 'HARD' && (
                <>
                  Напряжённая волна на пределе возможностей отряда (прогнозируемый винрейт: <strong className="text-amber-300">{estimatedWinrate}%</strong>).
                  <br />
                  <span className="text-amber-200 font-semibold">💡 Совет:</span> требуется активное использование турелей или баррикад, чтобы сдержать натиск.
                </>
              )}
              {verdictType === 'EASY' && (
                <>
                  Волна проходится без особого сопротивления (прогнозируемый винрейт: <strong className="text-indigo-300">{estimatedWinrate}%</strong>).
                  <br />
                  <span className="text-indigo-200 font-semibold">💡 Совет:</span> можно увеличить число врагов или добавить стрелков/громилу для динамики.
                </>
              )}
              {verdictType === 'BALANCED' && (
                <>
                  Отличная тактическая калибровка (прогнозируемый винрейт: <strong className="text-emerald-300">{estimatedWinrate}%</strong>).
                  Отряд имеет достаточно огневой мощи и ресурсов, чтобы отразить волну при грамотной расстановке.
                </>
              )}
            </div>
          </div>
        </div>

        {/* Метрики в реальном времени */}
        <div className="flex items-center gap-3 bg-slate-900/80 p-2.5 rounded-xl border border-slate-800/80 flex-shrink-0">
          <div className="text-center px-2">
            <div className="text-[10px] text-slate-500 uppercase font-semibold">Враги / HP</div>
            <div className="text-xs font-bold text-slate-200 flex items-center justify-center gap-1 mt-0.5">
              <HeartPulse className="w-3.5 h-3.5 text-emerald-400" />
              {totalEnemies} шт / {Math.round(totalHp)}
            </div>
          </div>

          <div className="w-[1px] h-6 bg-slate-800" />

          <div className="text-center px-2">
            <div className="text-[10px] text-slate-500 uppercase font-semibold">DPS орды</div>
            <div className="text-xs font-bold text-slate-200 flex items-center justify-center gap-1 mt-0.5">
              <Zap className="w-3.5 h-3.5 text-sky-400" />
              {Math.round(totalDps)} ед/с
            </div>
          </div>

          <div className="w-[1px] h-6 bg-slate-800" />

          <div className="text-center px-2">
            <div className="text-[10px] text-slate-500 uppercase font-semibold">Винрейт</div>
            <div className={`text-sm font-black mt-0.5 ${
              estimatedWinrate >= 70 ? 'text-emerald-400' : estimatedWinrate >= 50 ? 'text-amber-400' : 'text-rose-400'
            }`}>
              {estimatedWinrate}%
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};
