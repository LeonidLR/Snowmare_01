// Training Ground «Боевая лаборатория», part C: bot runs in Unreal - N runs x M minutes (2 at a time), seeds varied, the page's current
// settings / preset; live status while they go (bot_status.json + each game's live snapshot); the batches and their reports.
import React, { useCallback, useEffect, useRef, useState } from 'react';
import { Bot, RefreshCw } from 'lucide-react';
import type { CombatLabSettings } from '../../combatLab/pressureSettings';
import { validateSettings } from '../../combatLab/pressureSettings';
import type { LiveRun, RunRequest } from '../../combatLab/report';
import { RUN_DEFAULTS, RUN_LIMITS, batchProgress, formationName, validateRunRequest } from '../../combatLab/report';
import type { LabLayout, LabRoutePoint } from './CombatLabMap';
import type { BatchReport } from './ReportView';
import { ReportView } from './ReportView';
import { Card, btnGhost, btnPrimary, fmtNum, inputCls } from './ui';

interface Status {
  isRunning: boolean; level?: string; stale?: boolean; mode?: string; batch?: string; totalRuns?: number; currentRun?: number; activeRuns?: number; completed?: number;
  aborted?: number; errors?: number; elapsedSec?: number; live?: LiveRun[];
}
interface BatchItem { id: string; runs?: number; minutes?: number; preset?: string; started_utc?: string; finished_runs: number; has_report: boolean }

interface Props {
  /** The chosen map (src/combatLab/maps.ts): batches, reports and the run itself are per map. */
  mapId: string;
  hasRoute: boolean;
  settings: CombatLabSettings | null;
  presetName?: string;
  unsaved: boolean;
  layout: LabLayout | null;
  route: LabRoutePoint[];
}

export const RunPanel: React.FC<Props> = ({ mapId, hasRoute, settings, presetName, unsaved, layout, route }) => {
  const [form, setForm] = useState<RunRequest>(RUN_DEFAULTS);
  const [status, setStatus] = useState<Status | null>(null);
  const [batches, setBatches] = useState<BatchItem[]>([]);
  const [selected, setSelected] = useState('');
  const [report, setReport] = useState<BatchReport | null>(null);
  const [message, setMessage] = useState('');
  const mapRef = useRef(mapId);
  mapRef.current = mapId;
  const formErrors = validateRunRequest(form);
  const settingsErrors = settings ? validateSettings(settings) : ['настройки не загружены'];

  const loadBatches = useCallback(async () => {
    try {
      const asked = mapId;
      const d = await fetch(`/api/combat-lab/batches?map=${encodeURIComponent(asked)}`).then((r) => r.json());
      if (asked !== mapRef.current) return;
      setBatches(d.batches ?? []);
      setSelected((s) => s || d.batches?.[0]?.id || '');
    } catch { /* offline */ }
  }, [mapId]);
  const loadReport = useCallback(async (id: string) => {
    if (!id) return setReport(null);
    try {
      const asked = mapId;
      const r = await fetch(`/api/combat-lab/report?batch=${encodeURIComponent(id)}&map=${encodeURIComponent(asked)}`);
      const d = r.ok ? await r.json() : null;
      if (asked === mapRef.current) setReport(d);
    } catch { setReport(null); }
  }, [mapId]);

  // Another map: forget the batch of the previous one, list this map's batches.
  useEffect(() => { setSelected(''); setReport(null); setBatches([]); loadBatches(); }, [loadBatches]);
  useEffect(() => { loadReport(selected); }, [selected, loadReport]);
  // Live status: every 2 s while a batch runs, every 10 s otherwise; the report refreshes when a run finishes.
  useEffect(() => {
    let alive = true;
    let lastFinished = -1;
    const poll = async () => {
      try {
        const s: Status = await fetch('/api/combat-lab/status').then((r) => r.json());
        if (!alive) return;
        setStatus(s);
        if (s.mode === 'combat_lab' && s.batch && (s.level ?? mapId) === mapId && s.currentRun !== lastFinished) {
          lastFinished = s.currentRun ?? 0;
          loadBatches();
          if (s.batch === selected) loadReport(s.batch);
        }
      } catch { /* offline */ }
    };
    poll();
    const timer = window.setInterval(poll, status?.isRunning ? 2000 : 10000);
    return () => { alive = false; window.clearInterval(timer); };
  }, [status?.isRunning, selected, mapId, loadBatches, loadReport]);

  const start = async () => {
    setMessage('');
    const r = await fetch('/api/combat-lab/run', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ ...form, map: mapId, settings, preset: presetName ?? '' }) });
    const d = await r.json();
    if (!r.ok) return setMessage(d.error ?? `ошибка ${r.status}`);
    setMessage(`Серия ${d.batch} запущена (${d.dir}).`);
    setSelected(d.batch);
    setStatus((s) => ({ ...(s ?? { isRunning: true }), isRunning: true }));
    loadBatches();
  };

  const running = Boolean(status?.isRunning);
  const labRunning = running && status?.mode === 'combat_lab';
  const progress = labRunning ? batchProgress(status?.totalRuns ?? 0, status?.currentRun ?? 0, status?.live ?? []) : 0;
  const num = (key: keyof RunRequest, label: string, step = 1) => (
    <label className="flex flex-col gap-1 text-xs text-slate-400">
      <span className="text-slate-300">{label}</span>
      <input type="number" className={inputCls} value={form[key]} step={step} min={RUN_LIMITS[key][0]} max={RUN_LIMITS[key][1]}
        onChange={(e) => setForm({ ...form, [key]: Number(e.target.value) })} />
      <span className="text-[11px] text-slate-500">по умолчанию {RUN_DEFAULTS[key]}; {RUN_LIMITS[key][0]}–{RUN_LIMITS[key][1]}</span>
    </label>
  );

  return (
    <div className="flex flex-col gap-4">
      <div className="grid gap-4 xl:grid-cols-[minmax(0,1fr)_minmax(0,1.3fr)]">
        <Card title={`Запуск ботов в Unreal · ${mapId}`} hint="Каждый забег: бот ведёт отряд по маршруту уровня строем, дерётся со встречными, бессмертие / патроны — как в настройках; конец — по времени. Сид давления = сид + номер забега.">
          <div className="grid grid-cols-3 gap-3">
            {num('runs', 'Забегов')}
            {num('minutes', 'Минут игры на забег', 0.5)}
            {num('parallel', 'Одновременно')}
          </div>
          <p className="text-xs text-slate-400">Настройки: <span className="text-slate-200">{presetName ? `пресет «${presetName}»` : 'свои (со страницы)'}</span>
            {unsaved && <span className="text-amber-300"> — не сохранены в уровень; бот всё равно играет их (копия уровня в папке серии).</span>}</p>
          {!hasRoute && <p className="text-xs text-rose-300">У уровня {mapId} нет маршрута бота («bot_route») — запускать нечего.</p>}
          {[...formErrors, ...settingsErrors].map((e) => <p key={e} className="text-xs text-rose-300">{e}</p>)}
          <div className="flex gap-2 items-center">
            <button className={btnPrimary} onClick={start} disabled={running || !hasRoute || formErrors.length > 0 || settingsErrors.length > 0}><Bot size={15} />Запустить ботов</button>
            {running && !labRunning && <span className="text-xs text-amber-300">Идёт другая серия ботов (волны) — дождитесь её конца.</span>}
          </div>
          {message && <p className="text-xs text-slate-300">{message}</p>}
          <p className="text-[11px] text-slate-500">Игры идут без рендера, фиксированным шагом (Scripts/bot_run.ps1 -ReportDir); сборку на это время блокирует общий замок агентов.</p>
        </Card>

        <Card title="Ход серии" right={<button className={btnGhost} onClick={loadBatches}><RefreshCw size={14} /></button>}>
          {labRunning ? (
            <>
              <div className="flex items-center justify-between text-sm text-slate-300">
                <span>Серия {status?.batch}{status?.level && status.level !== mapId ? ` (карта ${status.level})` : ''}: готово {status?.currentRun ?? 0} из {status?.totalRuns ?? 0}, идёт {status?.activeRuns ?? 0}</span>
                <span className="tabular-nums text-slate-400">{fmtNum((status?.elapsedSec ?? 0) / 60, 1)} мин</span>
              </div>
              <div className="h-2.5 bg-slate-800 rounded-full overflow-hidden"><div className="h-full bg-cyan-500 transition-all" style={{ width: `${progress * 100}%` }} /></div>
              <table className="text-xs w-full">
                <thead className="text-slate-500 text-left"><tr>{['№', 'Время', 'Точка маршрута', 'Кругов', 'Контакты', 'Бой', 'Построение', 'Застрял', 'Приказ'].map((h) => <th key={h} className="font-normal pr-2">{h}</th>)}</tr></thead>
                <tbody className="text-slate-200 tabular-nums">
                  {(status?.live ?? []).map((l) => (
                    <tr key={l.run} className="border-t border-slate-800">
                      <td className="pr-2 py-1">{l.run}</td>
                      <td className="pr-2">{fmtNum(l.time_sec / 60, 1)} / {fmtNum(l.planned_sec / 60, 1)} мин</td>
                      <td className="pr-2">#{l.waypoint} {l.label}</td>
                      <td className="pr-2">{l.laps} ({Math.round(l.progress * 100)} %)</td>
                      <td className="pr-2">{l.contacts}</td>
                      <td className="pr-2">{l.fight ? <span className="text-rose-300">да</span> : '—'}</td>
                      <td className="pr-2">{formationName(l.formation || '—')}</td>
                      <td className="pr-2">{l.stuck}</td>
                      <td className="pr-2 text-slate-400 truncate max-w-[14rem]" title={l.last_order}>{l.last_order}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
              {(status?.live ?? []).length === 0 && <p className="text-xs text-slate-500">Игры загружаются…</p>}
            </>
          ) : (
            <p className="text-sm text-slate-400">{status?.stale ? 'Прошлая серия оборвалась (окно закрыто).' : 'Сейчас боты не идут.'}</p>
          )}
          <div className="flex flex-col gap-1">
            <span className="text-xs text-slate-500">Серии карты {mapId}:</span>
            <div className="flex flex-wrap gap-1.5 max-h-28 overflow-y-auto">
              {batches.map((b) => (
                <button key={b.id} onClick={() => setSelected(b.id)} title={b.started_utc}
                  className={`px-2 py-1 rounded-md text-xs border ${selected === b.id ? 'bg-cyan-600 border-cyan-500 text-slate-950' : 'bg-slate-950 border-slate-700 text-slate-300 hover:bg-slate-800'}`}>
                  {b.id} · {b.finished_runs}/{b.runs ?? '?'}{b.preset ? ` · ${b.preset}` : ''}
                </button>
              ))}
              {batches.length === 0 && <span className="text-xs text-slate-500">ещё нет</span>}
            </div>
          </div>
        </Card>
      </div>
      {report && <ReportView report={report} mapId={mapId} layout={layout} route={route} />}
    </div>
  );
};
