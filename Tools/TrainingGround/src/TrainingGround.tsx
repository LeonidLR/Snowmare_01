// Training Ground (the former Wave Editor, user decision 2026-10-25): departments for the bots. «Волны» is the wave / level
// editor as before; «Перемещение» the squad movement simulator, bot and tuner; «Левел-дизайн» the layout audit.
import React, { useState } from 'react';
import { Footprints, Map, Swords } from 'lucide-react';
import { App as WavesDepartment } from './App';
import { MovementDepartment } from './departments/MovementDepartment';
import { LevelDesignDepartment } from './departments/LevelDesignDepartment';

type DepartmentId = 'waves' | 'movement' | 'level';

const DEPARTMENTS: { id: DepartmentId; name: string; icon: React.ReactNode; hint: string }[] = [
  { id: 'waves', name: 'Волны', icon: <Swords size={15} />, hint: 'Волны, оружие, скрытность, бот боя (Unreal)' },
  { id: 'movement', name: 'Перемещение', icon: <Footprints size={15} />, hint: 'Построения, сектора взгляда, бот и обучение' },
  { id: 'level', name: 'Левел-дизайн', icon: <Map size={15} />, hint: 'Проверка раскладок тестовых уровней' },
];

const STORAGE_KEY = 'training_ground_department';

function initialDepartment(): DepartmentId {
  try {
    const v = localStorage.getItem(STORAGE_KEY);
    if (v === 'waves' || v === 'movement' || v === 'level') return v;
  } catch { /* storage blocked: default */ }
  return 'waves';
}

export const TrainingGround: React.FC = () => {
  const [department, setDepartment] = useState<DepartmentId>(initialDepartment);
  const choose = (id: DepartmentId) => {
    setDepartment(id);
    try { localStorage.setItem(STORAGE_KEY, id); } catch { /* ignore */ }
  };
  return (
    <div className="min-h-screen bg-slate-950 flex flex-col">
      <nav className="bg-slate-950 border-b border-slate-800 px-6 py-2 flex flex-wrap items-center gap-3" aria-label="Отделы Training Ground">
        <span className="text-xs font-black tracking-[0.2em] uppercase text-slate-300 mr-2">Training Ground</span>
        {DEPARTMENTS.map((d) => (
          <button key={d.id} title={d.hint} onClick={() => choose(d.id)} aria-pressed={department === d.id}
            className={`inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-sm font-semibold border transition-colors ${
              department === d.id ? 'bg-cyan-600 border-cyan-500 text-slate-950' : 'bg-slate-900 border-slate-800 text-slate-300 hover:bg-slate-800'}`}>
            {d.icon}{d.name}
          </button>
        ))}
      </nav>
      {department === 'waves' && <WavesDepartment />}
      {department === 'movement' && <MovementDepartment />}
      {department === 'level' && <LevelDesignDepartment />}
    </div>
  );
};
