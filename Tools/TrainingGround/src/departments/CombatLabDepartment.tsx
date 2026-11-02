// Training Ground department «Боевая лаборатория» (game designer 2026-11-01, docs/design/COMBAT_LAB.md section 7). No longer lab-only: it works per
// MAP, chosen in the header - L_FormationLabCombat (the default) or a generated map (Content/Maps/Generated) whose level JSON has a "bot_route"
// (src/combatLab/maps.ts, GET /api/combat-lab/maps). A - the level's enemy pressure / ambush / cheat settings and named presets (combatLab/SettingsPanel);
// C - bot runs in Unreal along the level's planned route and their report (combatLab/RunPanel). B (the map editor) and D (the simulator) come later:
// their tabs are reserved here.
import React, { useEffect, useMemo, useState } from 'react';
import { Bot, Cpu, Map as MapIcon, SlidersHorizontal } from 'lucide-react';
import type { CombatLabPresetFile, CombatLabSettings } from '../combatLab/pressureSettings';
import { diffSettings, getAt, matchingPreset } from '../combatLab/pressureSettings';
import type { CombatLabMapInfo } from '../combatLab/maps';
import { LAB_MAP_ID } from '../combatLab/maps';
import type { LabLayout, LabRoutePoint } from './combatLab/CombatLabMap';
import { CombatLabMap, MapLegend } from './combatLab/CombatLabMap';
import { RunPanel } from './combatLab/RunPanel';
import { SettingsPanel } from './combatLab/SettingsPanel';
import { Card, inputCls } from './combatLab/ui';

type TabId = 'settings' | 'map' | 'bots' | 'sim';
const TABS: { id: TabId; name: string; icon: React.ReactNode; ready: boolean; hint: string }[] = [
  { id: 'settings', name: 'Настройки уровня', icon: <SlidersHorizontal size={14} />, ready: true, hint: 'Давление, засады, читы, пресеты' },
  { id: 'map', name: 'Редактор карты', icon: <MapIcon size={14} />, ready: false, hint: 'Следующий этап: стены, точки засад, маршрут бота' },
  { id: 'bots', name: 'Прогоны ботов', icon: <Bot size={14} />, ready: true, hint: 'Боты в Unreal по маршруту и отчёт' },
  { id: 'sim', name: 'Симулятор', icon: <Cpu size={14} />, ready: false, hint: 'Следующий этап: прогон в браузере без Unreal' },
];
const TAB_KEY = 'training_ground_combat_lab_tab';
const MAP_KEY = 'training_ground_combat_lab_map';

export const CombatLabDepartment: React.FC = () => {
  const [tab, setTab] = useState<TabId>(() => {
    try { const v = localStorage.getItem(TAB_KEY); return v === 'bots' ? 'bots' : 'settings'; } catch { return 'settings'; }
  });
  const [maps, setMaps] = useState<CombatLabMapInfo[] | null>(null);
  const [mapId, setMapId] = useState<string>(() => {
    try { return localStorage.getItem(MAP_KEY) || LAB_MAP_ID; } catch { return LAB_MAP_ID; }
  });
  const [base, setBase] = useState<CombatLabSettings | null>(null);
  const [settings, setSettings] = useState<CombatLabSettings | null>(null);
  const [presets, setPresets] = useState<CombatLabPresetFile | null>(null);
  const [layout, setLayout] = useState<LabLayout | null>(null);
  const [route, setRoute] = useState<{ name: string; loop: boolean; points: LabRoutePoint[] } | null>(null);
  const [file, setFile] = useState(`Content/Data/LevelJson/${LAB_MAP_ID}.json`);
  const [error, setError] = useState('');

  // The maps list (once) and the presets (one shared file).
  useEffect(() => {
    fetch('/api/combat-lab/maps').then((r) => r.json()).then((d) => {
      const list: CombatLabMapInfo[] = d.maps ?? [];
      setMaps(list);
      // A remembered map that is gone (or was never valid) falls back to the default.
      setMapId((id) => (list.some((m) => m.id === id) ? id : (d.default ?? LAB_MAP_ID)));
    }).catch(() => { setMaps([]); setError('Нет связи с сервером Training Ground'); });
    fetch('/api/combat-lab/presets').then((r) => r.json()).then(setPresets).catch(() => {});
  }, []);

  // Switching the map reloads its settings / route / layout (the batches and the report reload in RunPanel).
  useEffect(() => {
    if (maps === null || !maps.some((m) => m.id === mapId)) return;
    let alive = true;
    const q = `?map=${encodeURIComponent(mapId)}`;
    setBase(null); setSettings(null); setRoute(null); setLayout(null); setError('');
    fetch(`/api/combat-lab/settings${q}`).then((r) => r.json()).then((d) => {
      if (!alive) return;
      if (d.error) return setError(d.error);
      setBase(d.settings);
      setSettings(d.settings);
      setRoute(d.route);
      if (d.file) setFile(d.file);
    }).catch(() => alive && setError('Нет связи с сервером Training Ground'));
    fetch(`/api/combat-lab/layout${q}`).then((r) => r.json()).then((d) => { if (alive && !d.error) setLayout(d); }).catch(() => {});
    return () => { alive = false; };
  }, [maps, mapId]);

  const choose = (id: TabId) => {
    setTab(id);
    try { localStorage.setItem(TAB_KEY, id); } catch { /* ignore */ }
  };
  const chooseMap = (id: string) => {
    setMapId(id);
    try { localStorage.setItem(MAP_KEY, id); } catch { /* ignore */ }
  };
  const mapInfo = maps?.find((m) => m.id === mapId);
  const unsaved = useMemo(() => (base && settings ? diffSettings(base, settings).length > 0 : false), [base, settings]);
  const presetId = settings ? matchingPreset(presets, settings, mapId) : undefined;
  const presetName = presets?.presets.find((p) => p.id === presetId)?.name;
  const ambushPoints = useMemo(() => {
    const points = settings ? getAt(settings.pressure, 'ambush.points') : undefined;
    return Array.isArray(points) ? (points.filter((p) => Array.isArray(p) && p.length === 2) as [number, number][]) : [];
  }, [settings]);

  return (
    <div className="p-6 flex flex-col gap-4">
      <div className="flex flex-wrap items-center gap-3">
        <div className="mr-2">
          <h2 className="text-lg font-bold text-slate-100">Боевая лаборатория · {mapId}</h2>
          <p className="text-xs text-slate-500">Бесконечное давление врагов: патрули, засады, скоординированные засады; отряд с читами. Файл: {file}</p>
        </div>
        <label className="flex flex-col gap-0.5 text-[11px] text-slate-500">
          Карта
          <select className={`${inputCls} min-w-[14rem]`} value={mapId} onChange={(e) => chooseMap(e.target.value)} aria-label="Карта боевой лаборатории"
            title="Лаборатория или сгенерированная карта с маршрутом бота (bot_route в её файле уровня)">
            {(maps ?? [{ id: mapId, kind: 'lab' } as CombatLabMapInfo]).map((m) => (
              <option key={m.id} value={m.id}>{m.kind === 'lab' ? `${m.id} (лаборатория)` : `${m.id} (сгенерированная${m.routePoints ? `, ${m.routePoints} т.` : ''})`}</option>
            ))}
          </select>
        </label>
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
          <SettingsPanel base={base} settings={settings} presets={presets} file={file} mapId={mapId} onChange={setSettings}
            onSaved={(saved) => { setBase(saved); setSettings(saved); }} onPresetsChanged={setPresets} />
          <Card title={`Маршрут бота${route?.name ? ` · ${route.name}` : ''}`} hint={route ? `${route.points.length} точек${route.loop ? ', петля' : ''}. Хранится в уровне («bot_route»); бот проходит его в «Прогонах ботов».` : 'У уровня нет маршрута бота («bot_route»).'} className="2xl:sticky 2xl:top-4 self-start">
            <CombatLabMap layout={layout} route={route?.points ?? []} loop={route?.loop ?? true} ambushPoints={ambushPoints} height={420} />
            <MapLegend />
          </Card>
        </div>
      )}
      {tab === 'bots' && (
        <RunPanel mapId={mapId} hasRoute={mapInfo?.hasRoute ?? false} settings={settings} presetName={presetName} unsaved={unsaved} layout={layout} route={route?.points ?? []} />
      )}
    </div>
  );
};
