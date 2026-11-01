// Training Ground department «Боевая лаборатория» (game designer 2026-11-01, docs/design/COMBAT_LAB.md section 7): only the map
// L_FormationLabCombat. A - the level's enemy pressure / ambush / cheat settings and named presets (combatLab/SettingsPanel); C - bot runs
// in Unreal along the level's planned route and their report (combatLab/RunPanel). B (the map editor) and D (the simulator) come later:
// their tabs are reserved here.
import React, { useEffect, useMemo, useState } from 'react';
import { Bot, Cpu, Map as MapIcon, SlidersHorizontal } from 'lucide-react';
import type { CombatLabPresetFile, CombatLabSettings } from '../combatLab/pressureSettings';
import { diffSettings, getAt, matchingPreset } from '../combatLab/pressureSettings';
import type { LabLayout, LabRoutePoint } from './combatLab/CombatLabMap';
import { CombatLabMap, MapLegend } from './combatLab/CombatLabMap';
import { RunPanel } from './combatLab/RunPanel';
import { SettingsPanel } from './combatLab/SettingsPanel';
import { Card } from './combatLab/ui';

type TabId = 'settings' | 'map' | 'bots' | 'sim';
const TABS: { id: TabId; name: string; icon: React.ReactNode; ready: boolean; hint: string }[] = [
  { id: 'settings', name: 'Настройки уровня', icon: <SlidersHorizontal size={14} />, ready: true, hint: 'Давление, засады, читы, пресеты' },
  { id: 'map', name: 'Редактор карты', icon: <MapIcon size={14} />, ready: false, hint: 'Следующий этап: стены, точки засад, маршрут бота' },
  { id: 'bots', name: 'Прогоны ботов', icon: <Bot size={14} />, ready: true, hint: 'Боты в Unreal по маршруту и отчёт' },
  { id: 'sim', name: 'Симулятор', icon: <Cpu size={14} />, ready: false, hint: 'Следующий этап: прогон в браузере без Unreal' },
];
const TAB_KEY = 'training_ground_combat_lab_tab';

export const CombatLabDepartment: React.FC = () => {
  const [tab, setTab] = useState<TabId>(() => {
    try { const v = localStorage.getItem(TAB_KEY); return v === 'bots' ? 'bots' : 'settings'; } catch { return 'settings'; }
  });
  const [base, setBase] = useState<CombatLabSettings | null>(null);
  const [settings, setSettings] = useState<CombatLabSettings | null>(null);
  const [presets, setPresets] = useState<CombatLabPresetFile | null>(null);
  const [layout, setLayout] = useState<LabLayout | null>(null);
  const [route, setRoute] = useState<{ name: string; loop: boolean; points: LabRoutePoint[] } | null>(null);
  const [file, setFile] = useState('Content/Data/LevelJson/L_FormationLabCombat.json');
  const [error, setError] = useState('');

  useEffect(() => {
    fetch('/api/combat-lab/settings').then((r) => r.json()).then((d) => {
      if (d.error) return setError(d.error);
      setBase(d.settings);
      setSettings(d.settings);
      setRoute(d.route);
      if (d.file) setFile(d.file);
    }).catch(() => setError('Нет связи с сервером Training Ground'));
    fetch('/api/combat-lab/presets').then((r) => r.json()).then(setPresets).catch(() => {});
    fetch('/api/combat-lab/layout').then((r) => r.json()).then((d) => !d.error && setLayout(d)).catch(() => {});
  }, []);

  const choose = (id: TabId) => {
    setTab(id);
    try { localStorage.setItem(TAB_KEY, id); } catch { /* ignore */ }
  };
  const unsaved = useMemo(() => (base && settings ? diffSettings(base, settings).length > 0 : false), [base, settings]);
  const presetId = settings ? matchingPreset(presets, settings) : undefined;
  const presetName = presets?.presets.find((p) => p.id === presetId)?.name;
  const ambushPoints = useMemo(() => {
    const points = settings ? getAt(settings.pressure, 'ambush.points') : undefined;
    return Array.isArray(points) ? (points.filter((p) => Array.isArray(p) && p.length === 2) as [number, number][]) : [];
  }, [settings]);

  return (
    <div className="p-6 flex flex-col gap-4">
      <div className="flex flex-wrap items-center gap-3">
        <div className="mr-2">
          <h2 className="text-lg font-bold text-slate-100">Боевая лаборатория · L_FormationLabCombat</h2>
          <p className="text-xs text-slate-500">Бесконечное давление врагов: патрули, засады, скоординированные засады; отряд с читами. Файл: {file}</p>
        </div>
        <nav className="flex flex-wrap gap-1.5" aria-label="Разделы боевой лаборатории">
          {TABS.map((t) => (
            <button key={t.id} disabled={!t.ready} title={t.hint} onClick={() => choose(t.id)} aria-pressed={tab === t.id}
              className={`inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-sm border ${tab === t.id ? 'bg-slate-100 border-slate-100 text-slate-950 font-semibold'
                : t.ready ? 'bg-slate-900 border-slate-700 text-slate-300 hover:bg-slate-800' : 'bg-slate-950 border-slate-800 text-slate-600 cursor-not-allowed'}`}>
              {t.icon}{t.name}{!t.ready && <span className="text-[10px] uppercase tracking-wider">скоро</span>}
            </button>
          ))}
        </nav>
      </div>
      {error && <p className="text-sm text-amber-300">{error}</p>}
      {tab === 'settings' && (
        <div className="grid gap-4 2xl:grid-cols-[minmax(0,1fr)_420px]">
          <SettingsPanel base={base} settings={settings} presets={presets} file={file} onChange={setSettings}
            onSaved={(saved) => { setBase(saved); setSettings(saved); }} onPresetsChanged={setPresets} />
          <Card title={`Маршрут бота${route?.name ? ` · ${route.name}` : ''}`} hint={route ? `${route.points.length} точек${route.loop ? ', петля' : ''}. Хранится в уровне («bot_route»); бот проходит его в «Прогонах ботов».` : 'У уровня нет маршрута бота («bot_route»).'} className="2xl:sticky 2xl:top-4 self-start">
            <CombatLabMap layout={layout} route={route?.points ?? []} loop={route?.loop ?? true} ambushPoints={ambushPoints} height={420} />
            <MapLegend />
          </Card>
        </div>
      )}
      {tab === 'bots' && <RunPanel settings={settings} presetName={presetName} unsaved={unsaved} layout={layout} route={route?.points ?? []} />}
    </div>
  );
};
