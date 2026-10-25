import React from 'react';
import {
  BarChart, Bar, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, Legend
} from 'recharts';
import { WaveConfig, ENEMY_DB, EnemyType } from '../types';
import { Layers } from 'lucide-react';

interface Props {
  waves: WaveConfig[];
  selectedWaveIndex: number;
  onSelectWave: (index: number) => void;
}

export const EnemyCompositionBarChart: React.FC<Props> = ({
  waves,
  selectedWaveIndex,
  onSelectWave,
}) => {
  const chartData = waves.map((w) => {
    const row: any = {
      waveName: `В${w.wave_index}`,
      waveIndex: w.wave_index,
      HOUND: 0,
      CUTTER: 0,
      MARKSMAN: 0,
      SPITTER: 0,
      BRUTE: 0,
      FROSTBITTEN: 0,
      total: 0
    };

    w.spawns.forEach((s) => {
      if (row[s.enemy_type] !== undefined) {
        row[s.enemy_type] += s.count;
        row.total += s.count;
      }
    });

    return row;
  });

  return (
    <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-xl flex flex-col gap-3">
      <div className="flex items-center justify-between border-b border-slate-800 pb-3">
        <div className="flex items-center gap-2">
          <Layers className="w-5 h-5 text-sky-400" />
          <h2 className="text-base font-semibold text-slate-100">Состав орды по типам врагов (Штук)</h2>
        </div>
        <div className="text-xs text-slate-400">
          Выбрана: <span className="text-sky-400 font-bold">Волна {selectedWaveIndex}</span>
        </div>
      </div>

      <div className="h-56 w-full pt-2">
        <ResponsiveContainer width="100%" height="100%">
          <BarChart
            data={chartData}
            onClick={(e) => {
              if (e && e.activePayload && e.activePayload.length > 0) {
                const waveIdx = e.activePayload[0].payload.waveIndex;
                onSelectWave(waveIdx);
              }
            }}
          >
            <CartesianGrid strokeDasharray="3 3" stroke="#1e293b" />
            <XAxis dataKey="waveName" stroke="#64748b" tick={{ fontSize: 11 }} />
            <YAxis stroke="#64748b" tick={{ fontSize: 11 }} allowDecimals={false} />
            <Tooltip
              contentStyle={{
                backgroundColor: '#0f172a',
                borderColor: '#334155',
                borderRadius: '8px',
                fontSize: '12px',
              }}
              formatter={(val: any, name: string) => {
                const enemyType = name as EnemyType;
                const meta = ENEMY_DB[enemyType];
                return [`${val} шт`, meta ? `${meta.icon} ${meta.name}` : name];
              }}
            />
            <Legend
              formatter={(value) => {
                const meta = ENEMY_DB[value as EnemyType];
                return <span className="text-xs text-slate-300">{meta ? `${meta.icon} ${meta.name}` : value}</span>;
              }}
              wrapperStyle={{ fontSize: '11px', paddingTop: '6px' }}
            />

            <Bar dataKey="HOUND" stackId="a" fill={ENEMY_DB.HOUND.color} name="HOUND" radius={[0, 0, 0, 0]} />
            <Bar dataKey="CUTTER" stackId="a" fill={ENEMY_DB.CUTTER.color} name="CUTTER" radius={[0, 0, 0, 0]} />
            <Bar dataKey="MARKSMAN" stackId="a" fill={ENEMY_DB.MARKSMAN.color} name="MARKSMAN" radius={[0, 0, 0, 0]} />
            <Bar dataKey="SPITTER" stackId="a" fill={ENEMY_DB.SPITTER.color} name="SPITTER" radius={[0, 0, 0, 0]} />
            <Bar dataKey="BRUTE" stackId="a" fill={ENEMY_DB.BRUTE.color} name="BRUTE" radius={[0, 0, 0, 0]} />
            <Bar dataKey="FROSTBITTEN" stackId="a" fill={ENEMY_DB.FROSTBITTEN.color} name="FROSTBITTEN" radius={[2, 2, 0, 0]} />
          </BarChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
};
