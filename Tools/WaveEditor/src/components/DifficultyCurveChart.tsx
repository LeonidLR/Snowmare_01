import React, { useState } from 'react';
import {
  Area, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer,
  Legend, Line, ComposedChart, ReferenceLine
} from 'recharts';
import { WaveConfig, ENEMY_DB } from '../types';
import { Activity, ShieldAlert, HeartPulse, Zap } from 'lucide-react';

interface Props {
  waves: WaveConfig[];
  selectedWaveIndex: number;
  onSelectWave: (index: number) => void;
}

export const DifficultyCurveChart: React.FC<Props> = ({
  waves,
  selectedWaveIndex,
  onSelectWave,
}) => {
  const [metricMode, setMetricMode] = useState<'COMBINED' | 'THREAT' | 'HP' | 'WINRATE'>('COMBINED');

  const chartData = waves.map((w) => {
    let totalHp = 0;
    let totalDps = 0;
    let totalEnemies = 0;

    w.spawns.forEach((s) => {
      const meta = ENEMY_DB[s.enemy_type];
      if (meta) {
        const hp = s.custom_stats?.health ?? meta.baseHp;
        const dps = s.custom_stats?.damage ?? meta.baseDps;
        totalHp += s.count * hp * (w.wave_modifiers?.enemy_hp_mult || 1.0);
        totalDps += s.count * dps * (w.wave_modifiers?.enemy_damage_mult || 1.0);
        totalEnemies += s.count;
      }
    });

    const threatIndex = Math.round((totalHp * 0.15 + totalDps * 1.8) * (w.wave_modifiers?.cold_drain_mult || 1.0));
    const squadPower = 1100 + (w.wave_index - 1) * 350;
    const ratio = threatIndex / Math.max(1, squadPower);
    const estimatedWinrate = Math.max(5, Math.min(99, Math.round(100 / (1 + Math.exp((ratio - 1.0) * 3.5)))));

    return {
      waveName: `Волна ${w.wave_index}`,
      waveIndex: w.wave_index,
      totalHp: Math.round(totalHp),
      totalDps: Math.round(totalDps),
      totalEnemies,
      threatIndex,
      estimatedWinrate,
      hpMult: w.wave_modifiers?.enemy_hp_mult || 1.0,
      dmgMult: w.wave_modifiers?.enemy_damage_mult || 1.0
    };
  });

  return (
    <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-xl flex flex-col gap-4">
      <div className="flex flex-wrap items-center justify-between gap-3 border-b border-slate-800 pb-3">
        <div className="flex items-center gap-2">
          <Activity className="w-5 h-5 text-sky-400" />
          <h2 className="text-base font-semibold text-slate-100">Интерактивная кривая сложности и баланса</h2>
        </div>
        
        <div className="flex items-center bg-slate-950 p-1 rounded-lg border border-slate-800 text-xs">
          <button
            onClick={() => setMetricMode('COMBINED')}
            className={`px-3 py-1.5 rounded-md font-medium transition-all ${
              metricMode === 'COMBINED' ? 'bg-sky-500 text-slate-950 shadow-md' : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            Сводный баланс
          </button>
          <button
            onClick={() => setMetricMode('THREAT')}
            className={`px-3 py-1.5 rounded-md font-medium transition-all ${
              metricMode === 'THREAT' ? 'bg-purple-500 text-white shadow-md' : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            Индекс угрозы (DPS)
          </button>
          <button
            onClick={() => setMetricMode('HP')}
            className={`px-3 py-1.5 rounded-md font-medium transition-all ${
              metricMode === 'HP' ? 'bg-emerald-500 text-slate-950 shadow-md' : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            Здоровье орды (HP)
          </button>
          <button
            onClick={() => setMetricMode('WINRATE')}
            className={`px-3 py-1.5 rounded-md font-medium transition-all ${
              metricMode === 'WINRATE' ? 'bg-amber-500 text-slate-950 shadow-md' : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            Винрейт (%)
          </button>
        </div>
      </div>

      <div className="grid grid-cols-2 sm:grid-cols-4 gap-3 text-xs">
        <div className="bg-slate-950/60 border border-slate-800/80 rounded-lg p-2.5 flex items-center gap-2.5">
          <div className="w-8 h-8 rounded-lg bg-sky-500/10 flex items-center justify-center text-sky-400">
            <Zap className="w-4 h-4" />
          </div>
          <div>
            <div className="text-slate-400 text-[11px]">Пиковый DPS орды</div>
            <div className="text-sm font-bold text-sky-300">
              {Math.max(...chartData.map(d => d.totalDps))} ед/с
            </div>
          </div>
        </div>

        <div className="bg-slate-950/60 border border-slate-800/80 rounded-lg p-2.5 flex items-center gap-2.5">
          <div className="w-8 h-8 rounded-lg bg-emerald-500/10 flex items-center justify-center text-emerald-400">
            <HeartPulse className="w-4 h-4" />
          </div>
          <div>
            <div className="text-slate-400 text-[11px]">Пиковое HP волны</div>
            <div className="text-sm font-bold text-emerald-300">
              {Math.max(...chartData.map(d => d.totalHp))} HP
            </div>
          </div>
        </div>

        <div className="bg-slate-950/60 border border-slate-800/80 rounded-lg p-2.5 flex items-center gap-2.5">
          <div className="w-8 h-8 rounded-lg bg-purple-500/10 flex items-center justify-center text-purple-400">
            <ShieldAlert className="w-4 h-4" />
          </div>
          <div>
            <div className="text-slate-400 text-[11px]">Макс. индекс угрозы</div>
            <div className="text-sm font-bold text-purple-300">
              {Math.max(...chartData.map(d => d.threatIndex))}
            </div>
          </div>
        </div>

        <div className="bg-slate-950/60 border border-slate-800/80 rounded-lg p-2.5 flex items-center gap-2.5">
          <div className="w-8 h-8 rounded-lg bg-amber-500/10 flex items-center justify-center text-amber-400">
            <Activity className="w-4 h-4" />
          </div>
          <div>
            <div className="text-slate-400 text-[11px]">Винрейт финала</div>
            <div className="text-sm font-bold text-amber-300">
              {chartData[chartData.length - 1]?.estimatedWinrate || 0}%
            </div>
          </div>
        </div>
      </div>

      <div className="h-64 w-full pt-2">
        <ResponsiveContainer width="100%" height="100%">
          <ComposedChart
            data={chartData}
            onClick={(e) => {
              if (e && e.activePayload && e.activePayload.length > 0) {
                const waveIdx = e.activePayload[0].payload.waveIndex;
                onSelectWave(waveIdx);
              }
            }}
          >
            <defs>
              <linearGradient id="threatGradient" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%" stopColor="#a855f7" stopOpacity={0.4} />
                <stop offset="95%" stopColor="#a855f7" stopOpacity={0.0} />
              </linearGradient>
            </defs>
            <CartesianGrid strokeDasharray="3 3" stroke="#1e293b" />
            <XAxis dataKey="waveName" stroke="#64748b" tick={{ fontSize: 11 }} />
            <YAxis yAxisId="left" stroke="#64748b" tick={{ fontSize: 11 }} />
            {metricMode === 'COMBINED' && (
              <YAxis yAxisId="right" orientation="right" stroke="#f59e0b" domain={[0, 100]} unit="%" tick={{ fontSize: 11 }} />
            )}
            <Tooltip
              contentStyle={{
                backgroundColor: '#0f172a',
                borderColor: '#334155',
                borderRadius: '8px',
                fontSize: '12px',
                boxShadow: '0 10px 15px -3px rgba(0, 0, 0, 0.5)'
              }}
              formatter={(value: any, name: string) => {
                if (name === 'threatIndex') return [`${value} очков`, 'Индекс угрозы'];
                if (name === 'totalHp') return [`${value} HP`, 'Суммарное HP'];
                if (name === 'totalDps') return [`${value} DPS`, 'Суммарный урон/сек'];
                if (name === 'estimatedWinrate') return [`${value}%`, 'Оценка шанса победы'];
                return [value, name];
              }}
            />
            <Legend wrapperStyle={{ fontSize: '11px', paddingTop: '8px' }} />

            {(metricMode === 'COMBINED' || metricMode === 'THREAT') && (
              <Area
                yAxisId="left"
                type="monotone"
                dataKey="threatIndex"
                stroke="#a855f7"
                strokeWidth={2}
                fillOpacity={1}
                fill="url(#threatGradient)"
                name="Индекс угрозы"
              />
            )}

            {(metricMode === 'COMBINED' || metricMode === 'HP') && (
              <Line
                yAxisId="left"
                type="monotone"
                dataKey="totalHp"
                stroke="#10b981"
                strokeWidth={2}
                dot={{ r: 3, fill: '#10b981' }}
                name="Суммарное HP"
              />
            )}

            {(metricMode === 'COMBINED' || metricMode === 'WINRATE') && (
              <Line
                yAxisId={metricMode === 'COMBINED' ? 'right' : 'left'}
                type="monotone"
                dataKey="estimatedWinrate"
                stroke="#f59e0b"
                strokeWidth={2.5}
                strokeDasharray="4 4"
                dot={{ r: 4, fill: '#f59e0b' }}
                name="Винрейт (%)"
              />
            )}

            <ReferenceLine yAxisId="left" x={`Волна ${selectedWaveIndex}`} stroke="#38bdf8" strokeDasharray="3 3" label={{ value: "Активная волна", fill: "#38bdf8", fontSize: 10, position: "top" }} />
          </ComposedChart>
        </ResponsiveContainer>
      </div>
      <div className="text-[11px] text-slate-500 text-center italic">
        💡 Кликните по любой волне на графике, чтобы мгновенно перейти к её детальной настройке в инспекторе.
      </div>
    </div>
  );
};
