// Training Ground: отдел «Состояние проекта» (docs/port/DEBT.md, Saved/Health/*.json).
// Архитектурное правило: ТОЛЬКО чтение Saved/Health/*.json через GET /api/project-health.

import React, { useState, useEffect } from 'react';
import {
  Activity,
  AlertTriangle,
  CheckCircle2,
  Clock,
  FileCode2,
  GitCommit,
  GitPullRequest,
  HeartPulse,
  History,
  Layers,
  RefreshCw,
  ShieldAlert,
  ShieldCheck,
  XCircle,
} from 'lucide-react';

export interface KnownFailItem {
  name: string;
  type: string;
  reason: string;
  owner: string;
  commit?: string;
  first_seen: string;
  days_active: number;
  expired: boolean;
}

export interface DisabledSmokeItem {
  name: string;
  type: string;
  map?: string;
  status: string;
  recent_commit?: string;
}

export interface TodoSample {
  file: string;
  line: number;
  text: string;
}

export interface BuildTrendItem {
  timestamp: string;
  label: string;
  duration_s: number | null;
  actions: number | null;
  status: string;
  log_file: string;
}

export interface MergeGateItem {
  timestamp: string;
  branch: string;
  status: string;
  details: string;
}

export interface ProjectHealthData {
  $schema?: string;
  generated_at: string;
  date: string;
  head: string;
  summary: {
    status: 'OK' | 'WARNING' | 'CRITICAL';
    unit_tests: { total: number; passed: number; failed: number; duration_s?: number };
    smokes: { total: number; pass: number; known_fail: number; fail: number; duration_s?: number };
    known_fails_count: number;
    known_fails_expired: number;
    disabled_this_week_count: number;
    todo_markers_count: number;
    todo_markers_growth?: number;
    last_build_time_s: number | null;
    last_merge_gate_status: string;
  };
  known_fails: KnownFailItem[];
  disabled_this_week: DisabledSmokeItem[];
  todo_markers: {
    total: number;
    growth?: number;
    by_directory: Record<string, number>;
    by_tag?: Record<string, number>;
    sample: TodoSample[];
  };
  build_trend: BuildTrendItem[];
  merge_gate_runs: MergeGateItem[];
}

export const ProjectHealthDepartment: React.FC = () => {
  const [data, setData] = useState<ProjectHealthData | null>(null);
  const [loading, setLoading] = useState<boolean>(true);
  const [error, setError] = useState<string | null>(null);
  const [activeTab, setActiveTab] = useState<'known_fails' | 'disabled' | 'todos' | 'builds' | 'merge_gate'>('known_fails');
  const [availableDates, setAvailableDates] = useState<string[]>([]);
  const [selectedDate, setSelectedDate] = useState<string>('latest');

  const fetchHealthData = async (dateStr: string = 'latest') => {
    setLoading(true);
    setError(null);
    try {
      const url = dateStr === 'latest' ? '/api/project-health' : `/api/project-health/by-date?date=${dateStr}`;
      const res = await fetch(url);
      if (!res.ok) {
        throw new Error(`HTTP ${res.status}: ${res.statusText}`);
      }
      const json = await res.json();
      setData(json);
    } catch (err: any) {
      setError(err.message || 'Ошибка загрузки данных здоровья проекта');
    } finally {
      setLoading(false);
    }
  };

  const fetchHistory = async () => {
    try {
      const res = await fetch('/api/project-health/history');
      if (res.ok) {
        const json = await res.json();
        if (Array.isArray(json.dates)) {
          setAvailableDates(json.dates);
        }
      }
    } catch {
      // Игнорируем ошибку истории если dev-сервер не запущен
    }
  };

  useEffect(() => {
    fetchHealthData(selectedDate);
    fetchHistory();
  }, [selectedDate]);

  if (loading && !data) {
    return (
      <div className="flex-1 flex items-center justify-center bg-slate-950 p-12 text-slate-400">
        <RefreshCw className="animate-spin mr-3" size={24} />
        <span>Загрузка данных состояния проекта (Saved/Health/*.json)...</span>
      </div>
    );
  }

  if (error && !data) {
    return (
      <div className="flex-1 p-8 bg-slate-950 text-slate-200">
        <div className="max-w-2xl mx-auto bg-rose-950/40 border border-rose-800 rounded-xl p-6">
          <div className="flex items-center gap-3 text-rose-400 font-bold text-lg mb-2">
            <XCircle size={22} />
            <span>Не удалось загрузить Saved/Health/health_latest.json</span>
          </div>
          <p className="text-sm text-rose-300 mb-4">{error}</p>
          <p className="text-xs text-slate-400 mb-4">
            Убедитесь, что запущен скрипт <code className="text-cyan-400">Scripts/Tools/project_health.py</code> для генерации снапшота здоровья.
          </p>
          <button
            onClick={() => fetchHealthData(selectedDate)}
            className="px-4 py-2 bg-slate-800 hover:bg-slate-700 rounded-lg text-sm font-semibold transition"
          >
            Повторить попытку
          </button>
        </div>
      </div>
    );
  }

  const summary = data?.summary || {
    status: 'OK',
    unit_tests: { total: 0, passed: 0, failed: 0 },
    smokes: { total: 41, pass: 40, known_fail: 1, fail: 0 },
    known_fails_count: 0,
    known_fails_expired: 0,
    disabled_this_week_count: 0,
    todo_markers_count: 0,
    last_build_time_s: null,
    last_merge_gate_status: 'PASS',
  };

  const statusColor =
    summary.status === 'CRITICAL'
      ? 'bg-rose-500/20 text-rose-400 border-rose-500/40'
      : summary.status === 'WARNING'
      ? 'bg-amber-500/20 text-amber-400 border-amber-500/40'
      : 'bg-emerald-500/20 text-emerald-400 border-emerald-500/40';

  const statusIcon =
    summary.status === 'CRITICAL' ? (
      <ShieldAlert className="text-rose-400" size={20} />
    ) : summary.status === 'WARNING' ? (
      <AlertTriangle className="text-amber-400" size={20} />
    ) : (
      <ShieldCheck className="text-emerald-400" size={20} />
    );

  return (
    <div className="flex-1 bg-slate-950 text-slate-100 p-6 overflow-y-auto">
      {/* Top Header */}
      <div className="flex flex-wrap items-center justify-between gap-4 pb-6 border-b border-slate-800 mb-6">
        <div>
          <div className="flex items-center gap-3">
            <HeartPulse className="text-cyan-400" size={26} />
            <h1 className="text-xl font-black tracking-wide uppercase text-slate-100">Состояние проекта</h1>
            <span className={`inline-flex items-center gap-1.5 px-3 py-1 rounded-full text-xs font-bold border ${statusColor}`}>
              {statusIcon}
              {summary.status}
            </span>
          </div>
          <div className="flex items-center gap-4 text-xs text-slate-400 mt-2">
            <span className="flex items-center gap-1">
              <Clock size={13} className="text-slate-500" />
              Обновлено: {data?.generated_at ? new Date(data.generated_at).toLocaleString() : '—'}
            </span>
            <span className="flex items-center gap-1">
              <GitCommit size={13} className="text-slate-500" />
              HEAD: <code className="text-cyan-400 font-mono">{data?.head || '—'}</code>
            </span>
          </div>
        </div>

        {/* Snapshot selector & Refresh */}
        <div className="flex items-center gap-3">
          <div className="flex items-center gap-2 bg-slate-900 border border-slate-800 rounded-lg px-3 py-1.5 text-xs">
            <History size={14} className="text-slate-400" />
            <span className="text-slate-400">Срез:</span>
            <select
              value={selectedDate}
              onChange={(e) => setSelectedDate(e.target.value)}
              className="bg-transparent text-slate-200 font-medium focus:outline-none cursor-pointer"
            >
              <option value="latest" className="bg-slate-900 text-slate-200">
                Актуальный (health_latest.json)
              </option>
              {availableDates.map((d) => (
                <option key={d} value={d} className="bg-slate-900 text-slate-200">
                  {d}
                </option>
              ))}
            </select>
          </div>

          <button
            onClick={() => fetchHealthData(selectedDate)}
            title="Обновить данные"
            className="p-2 bg-slate-900 hover:bg-slate-800 border border-slate-800 rounded-lg text-slate-300 transition"
          >
            <RefreshCw size={15} />
          </button>
        </div>
      </div>

      {/* Metric Cards Grid */}
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-6 gap-4 mb-6">
        {/* Known Fails */}
        <div className="bg-slate-900/90 border border-slate-800 rounded-xl p-4 flex flex-col justify-between">
          <div className="flex items-center justify-between text-xs text-slate-400 mb-2">
            <span className="font-semibold uppercase tracking-wider">Known Fails</span>
            <AlertTriangle size={15} className="text-amber-400" />
          </div>
          <div className="flex items-baseline gap-2">
            <span className="text-2xl font-black text-slate-100">{summary.known_fails_count}</span>
            {summary.known_fails_expired > 0 ? (
              <span className="text-xs font-bold text-rose-400 bg-rose-500/20 border border-rose-500/30 px-1.5 py-0.5 rounded">
                {summary.known_fails_expired} &gt;7дн!
              </span>
            ) : (
              <span className="text-xs font-semibold text-emerald-400">0 просрочено</span>
            )}
          </div>
          <div className="text-[11px] text-slate-500 mt-2">SLA 7 дней на исправление</div>
        </div>

        {/* Smokes (41 active) */}
        <div className="bg-slate-900/90 border border-slate-800 rounded-xl p-4 flex flex-col justify-between">
          <div className="flex items-center justify-between text-xs text-slate-400 mb-2">
            <span className="font-semibold uppercase tracking-wider">Смоуки (Реестр)</span>
            <Activity size={15} className="text-cyan-400" />
          </div>
          <div className="flex items-baseline gap-2">
            <span className="text-2xl font-black text-slate-100">{summary.smokes.pass}</span>
            <span className="text-xs text-slate-400">/ {summary.smokes.total} PASS</span>
          </div>
          <div className="flex items-center gap-2 text-[11px] mt-2">
            <span className="text-amber-400">{summary.smokes.known_fail} knownFail</span>
            {summary.smokes.fail > 0 && <span className="text-rose-400 font-bold">{summary.smokes.fail} FAIL</span>}
          </div>
        </div>

        {/* Unit Tests */}
        <div className="bg-slate-900/90 border border-slate-800 rounded-xl p-4 flex flex-col justify-between">
          <div className="flex items-center justify-between text-xs text-slate-400 mb-2">
            <span className="font-semibold uppercase tracking-wider">Юнит-тесты</span>
            <CheckCircle2 size={15} className="text-emerald-400" />
          </div>
          <div className="flex items-baseline gap-2">
            <span className="text-2xl font-black text-slate-100">{summary.unit_tests.passed}</span>
            <span className="text-xs text-slate-400">/ {summary.unit_tests.total}</span>
          </div>
          <div className="text-[11px] text-slate-500 mt-2">
            {summary.unit_tests.failed > 0 ? (
              <span className="text-rose-400 font-bold">{summary.unit_tests.failed} упало</span>
            ) : (
              <span className="text-emerald-400 font-semibold">100% чистый прогон</span>
            )}
          </div>
        </div>

        {/* Disabled / Archived this week */}
        <div className="bg-slate-900/90 border border-slate-800 rounded-xl p-4 flex flex-col justify-between">
          <div className="flex items-center justify-between text-xs text-slate-400 mb-2">
            <span className="font-semibold uppercase tracking-wider">Отключено</span>
            <Layers size={15} className="text-slate-400" />
          </div>
          <div className="flex items-baseline gap-2">
            <span className="text-2xl font-black text-slate-100">{summary.disabled_this_week_count}</span>
            <span className="text-xs text-slate-400">на неделе</span>
          </div>
          <div className="text-[11px] text-slate-500 mt-2">Архивировано с картами (061)</div>
        </div>

        {/* TODO Markers */}
        <div className="bg-slate-900/90 border border-slate-800 rounded-xl p-4 flex flex-col justify-between">
          <div className="flex items-center justify-between text-xs text-slate-400 mb-2">
            <span className="font-semibold uppercase tracking-wider">TODO / Маркеры</span>
            <FileCode2 size={15} className="text-purple-400" />
          </div>
          <div className="flex items-baseline gap-2">
            <span className="text-2xl font-black text-slate-100">{summary.todo_markers_count}</span>
            {summary.todo_markers_growth !== undefined && summary.todo_markers_growth !== 0 ? (
              <span
                className={`text-xs font-bold px-1.5 py-0.5 rounded ${
                  summary.todo_markers_growth > 0
                    ? 'text-amber-400 bg-amber-500/20'
                    : 'text-emerald-400 bg-emerald-500/20'
                }`}
              >
                {summary.todo_markers_growth > 0 ? `+${summary.todo_markers_growth}` : summary.todo_markers_growth}
              </span>
            ) : (
              <span className="text-xs text-slate-500">стабильно</span>
            )}
          </div>
          <div className="text-[11px] text-slate-500 mt-2">Source + Scripts</div>
        </div>

        {/* Last Merge Gate & Build */}
        <div className="bg-slate-900/90 border border-slate-800 rounded-xl p-4 flex flex-col justify-between">
          <div className="flex items-center justify-between text-xs text-slate-400 mb-2">
            <span className="font-semibold uppercase tracking-wider">Merge-Gate</span>
            <GitPullRequest size={15} className="text-cyan-400" />
          </div>
          <div className="flex items-baseline gap-2">
            <span
              className={`text-lg font-black px-2 py-0.5 rounded ${
                summary.last_merge_gate_status === 'PASS'
                  ? 'bg-emerald-500/20 text-emerald-400 border border-emerald-500/30'
                  : 'bg-rose-500/20 text-rose-400 border border-rose-500/30'
              }`}
            >
              {summary.last_merge_gate_status}
            </span>
          </div>
          <div className="text-[11px] text-slate-400 mt-2">
            Сборка:{' '}
            <span className="text-slate-200 font-mono">
              {summary.last_build_time_s ? `${summary.last_build_time_s.toFixed(1)}s` : '—'}
            </span>
          </div>
        </div>
      </div>

      {/* Sub-tab Navigation */}
      <div className="flex items-center gap-2 border-b border-slate-800 mb-6">
        <button
          onClick={() => setActiveTab('known_fails')}
          className={`px-4 py-2 text-sm font-semibold border-b-2 transition-colors ${
            activeTab === 'known_fails'
              ? 'border-cyan-400 text-cyan-300'
              : 'border-transparent text-slate-400 hover:text-slate-200'
          }`}
        >
          Реестр Known-Fails (SLA 7 дней)
          {summary.known_fails_expired > 0 && (
            <span className="ml-2 px-1.5 py-0.2 rounded-full text-xs bg-rose-500 text-slate-950 font-black">
              {summary.known_fails_expired}
            </span>
          )}
        </button>

        <button
          onClick={() => setActiveTab('todos')}
          className={`px-4 py-2 text-sm font-semibold border-b-2 transition-colors ${
            activeTab === 'todos'
              ? 'border-cyan-400 text-cyan-300'
              : 'border-transparent text-slate-400 hover:text-slate-200'
          }`}
        >
          Маркеры техдолга ({summary.todo_markers_count})
        </button>

        <button
          onClick={() => setActiveTab('disabled')}
          className={`px-4 py-2 text-sm font-semibold border-b-2 transition-colors ${
            activeTab === 'disabled'
              ? 'border-cyan-400 text-cyan-300'
              : 'border-transparent text-slate-400 hover:text-slate-200'
          }`}
        >
          Отключённые проверки ({summary.disabled_this_week_count})
        </button>

        <button
          onClick={() => setActiveTab('builds')}
          className={`px-4 py-2 text-sm font-semibold border-b-2 transition-colors ${
            activeTab === 'builds'
              ? 'border-cyan-400 text-cyan-300'
              : 'border-transparent text-slate-400 hover:text-slate-200'
          }`}
        >
          Тренд сборок ({data?.build_trend?.length || 0})
        </button>

        <button
          onClick={() => setActiveTab('merge_gate')}
          className={`px-4 py-2 text-sm font-semibold border-b-2 transition-colors ${
            activeTab === 'merge_gate'
              ? 'border-cyan-400 text-cyan-300'
              : 'border-transparent text-slate-400 hover:text-slate-200'
          }`}
        >
          История Merge-Gate ({data?.merge_gate_runs?.length || 0})
        </button>
      </div>

      {/* Tab 1: Known Fails */}
      {activeTab === 'known_fails' && (
        <div className="bg-slate-900 border border-slate-800 rounded-xl overflow-hidden">
          <div className="p-4 border-b border-slate-800 flex items-center justify-between">
            <h2 className="text-sm font-bold text-slate-200 uppercase tracking-wide">
              Активный реестр Known-Fails с контролем срока давности (7 дней)
            </h2>
            <span className="text-xs text-slate-400">Просроченные элементы подлежат немедленному устранению</span>
          </div>

          {!data?.known_fails || data.known_fails.length === 0 ? (
            <div className="p-8 text-center text-slate-400 text-sm">
              <CheckCircle2 size={32} className="mx-auto text-emerald-400 mb-2" />
              Все смоуки и тесты зелёные. Активных known-fails нет.
            </div>
          ) : (
            <div className="overflow-x-auto">
              <table className="w-full text-left text-xs border-collapse">
                <thead>
                  <tr className="bg-slate-950/70 border-b border-slate-800 text-slate-400 uppercase text-[11px] font-bold">
                    <th className="py-3 px-4">Имя проверки</th>
                    <th className="py-3 px-4">Тип</th>
                    <th className="py-3 px-4">Причина (_why)</th>
                    <th className="py-3 px-4">Владелец</th>
                    <th className="py-3 px-4">Появился</th>
                    <th className="py-3 px-4 text-center">Дней в работе</th>
                    <th className="py-3 px-4 text-right">Статус SLA</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-slate-800/60">
                  {data.known_fails.map((kf) => (
                    <tr
                      key={kf.name}
                      className={`hover:bg-slate-800/40 transition-colors ${
                        kf.expired ? 'bg-rose-950/20' : ''
                      }`}
                    >
                      <td className="py-3 px-4 font-mono font-bold text-cyan-300">{kf.name}</td>
                      <td className="py-3 px-4 text-slate-400">
                        <span className="px-2 py-0.5 rounded bg-slate-800 border border-slate-700 text-[10px]">
                          {kf.type}
                        </span>
                      </td>
                      <td className="py-3 px-4 text-slate-300 max-w-md">{kf.reason}</td>
                      <td className="py-3 px-4 font-medium text-slate-300">{kf.owner}</td>
                      <td className="py-3 px-4 text-slate-400 font-mono">{kf.first_seen}</td>
                      <td className="py-3 px-4 text-center font-bold">{kf.days_active} дн</td>
                      <td className="py-3 px-4 text-right">
                        {kf.expired ? (
                          <span className="inline-flex items-center gap-1 px-2 py-1 rounded text-xs font-black bg-rose-500/20 text-rose-400 border border-rose-500/40 animate-pulse">
                            🔴 ПРОСРОЧЕНО (&gt; 7 дн)
                          </span>
                        ) : (
                          <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded text-xs font-semibold bg-emerald-500/20 text-emerald-400 border border-emerald-500/30">
                            🟢 В работе ({Math.max(0, 7 - kf.days_active)}д осталось)
                          </span>
                        )}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </div>
      )}

      {/* Tab 2: TODO Debt Markers */}
      {activeTab === 'todos' && (
        <div className="space-y-6">
          <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-5 gap-4">
            {Object.entries(data?.todo_markers?.by_directory || {}).map(([dir, count]) => (
              <div key={dir} className="bg-slate-900 border border-slate-800 rounded-xl p-4">
                <div className="text-xs text-slate-400 font-medium truncate mb-1" title={dir}>
                  {dir}
                </div>
                <div className="text-xl font-black text-slate-100">{count}</div>
              </div>
            ))}
          </div>

          <div className="bg-slate-900 border border-slate-800 rounded-xl overflow-hidden">
            <div className="p-4 border-b border-slate-800 flex items-center justify-between">
              <h2 className="text-sm font-bold text-slate-200 uppercase tracking-wide">
                Образцы маркеров техдолга (TODO / FIXME / WORKAROUND / HACK)
              </h2>
              <span className="text-xs text-slate-400">
                Всего найдено: <strong className="text-cyan-400">{data?.todo_markers?.total || 0}</strong>
              </span>
            </div>

            <div className="overflow-x-auto">
              <table className="w-full text-left text-xs border-collapse">
                <thead>
                  <tr className="bg-slate-950/70 border-b border-slate-800 text-slate-400 uppercase text-[11px] font-bold">
                    <th className="py-3 px-4">Файл</th>
                    <th className="py-3 px-4 w-20 text-center">Строка</th>
                    <th className="py-3 px-4">Текст маркера</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-slate-800/60 font-mono text-[11px]">
                  {data?.todo_markers?.sample?.map((s, idx) => (
                    <tr key={idx} className="hover:bg-slate-800/40 transition-colors">
                      <td className="py-2.5 px-4 text-cyan-300 font-medium">{s.file}</td>
                      <td className="py-2.5 px-4 text-center text-slate-500">{s.line}</td>
                      <td className="py-2.5 px-4 text-slate-300 truncate max-w-xl">{s.text}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        </div>
      )}

      {/* Tab 3: Disabled / Archived Smokes */}
      {activeTab === 'disabled' && (
        <div className="bg-slate-900 border border-slate-800 rounded-xl overflow-hidden">
          <div className="p-4 border-b border-slate-800 flex items-center justify-between">
            <h2 className="text-sm font-bold text-slate-200 uppercase tracking-wide">
              Проверки, списанные или перемещённые в архив на этой неделе
            </h2>
            <span className="text-xs text-slate-400">Всего: {data?.disabled_this_week?.length || 0}</span>
          </div>

          <div className="overflow-x-auto">
            <table className="w-full text-left text-xs border-collapse">
              <thead>
                <tr className="bg-slate-950/70 border-b border-slate-800 text-slate-400 uppercase text-[11px] font-bold">
                  <th className="py-3 px-4">Имя смоука</th>
                  <th className="py-3 px-4">Контекст карты</th>
                  <th className="py-3 px-4">Статус</th>
                  <th className="py-3 px-4 text-right">Коммит списания</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-800/60">
                {data?.disabled_this_week?.slice(0, 30).map((d) => (
                  <tr key={d.name} className="hover:bg-slate-800/40 transition-colors">
                    <td className="py-2.5 px-4 font-mono font-semibold text-slate-300">{d.name}</td>
                    <td className="py-2.5 px-4 font-mono text-slate-400">{d.map || '—'}</td>
                    <td className="py-2.5 px-4 text-amber-400/90">{d.status}</td>
                    <td className="py-2.5 px-4 text-right font-mono text-cyan-400">{d.recent_commit || '—'}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      )}

      {/* Tab 4: Build Duration Trend */}
      {activeTab === 'builds' && (
        <div className="bg-slate-900 border border-slate-800 rounded-xl overflow-hidden">
          <div className="p-4 border-b border-slate-800 flex items-center justify-between">
            <h2 className="text-sm font-bold text-slate-200 uppercase tracking-wide">
              История и время последних сборок (UBT / Lead / Worker)
            </h2>
          </div>

          <div className="overflow-x-auto">
            <table className="w-full text-left text-xs border-collapse">
              <thead>
                <tr className="bg-slate-950/70 border-b border-slate-800 text-slate-400 uppercase text-[11px] font-bold">
                  <th className="py-3 px-4">Время</th>
                  <th className="py-3 px-4">Метка / Цель</th>
                  <th className="py-3 px-4">Длительность</th>
                  <th className="py-3 px-4 text-center">Действий</th>
                  <th className="py-3 px-4 text-center">Статус</th>
                  <th className="py-3 px-4 text-right">Файл лога</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-800/60 font-mono text-[11px]">
                {data?.build_trend?.map((b, idx) => (
                  <tr key={idx} className="hover:bg-slate-800/40 transition-colors">
                    <td className="py-2.5 px-4 text-slate-400">{b.timestamp}</td>
                    <td className="py-2.5 px-4 text-slate-200 font-semibold">{b.label}</td>
                    <td className="py-2.5 px-4 text-cyan-300 font-bold">
                      {b.duration_s ? `${b.duration_s.toFixed(1)} s` : '—'}
                    </td>
                    <td className="py-2.5 px-4 text-center text-slate-400">{b.actions || '—'}</td>
                    <td className="py-2.5 px-4 text-center">
                      <span
                        className={`px-2 py-0.5 rounded text-[10px] font-bold ${
                          b.status === 'SUCCESS' || b.status === 'SUCCEEDED'
                            ? 'bg-emerald-500/20 text-emerald-400'
                            : 'bg-rose-500/20 text-rose-400'
                        }`}
                      >
                        {b.status}
                      </span>
                    </td>
                    <td className="py-2.5 px-4 text-right text-slate-500">{b.log_file}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      )}

      {/* Tab 5: Merge Gate Runs */}
      {activeTab === 'merge_gate' && (
        <div className="bg-slate-900 border border-slate-800 rounded-xl overflow-hidden">
          <div className="p-4 border-b border-slate-800 flex items-center justify-between">
            <h2 className="text-sm font-bold text-slate-200 uppercase tracking-wide">
              История прогонов Lead Merge Gate (Scripts/Tools/merge_gate.ps1)
            </h2>
          </div>

          <div className="overflow-x-auto">
            <table className="w-full text-left text-xs border-collapse">
              <thead>
                <tr className="bg-slate-950/70 border-b border-slate-800 text-slate-400 uppercase text-[11px] font-bold">
                  <th className="py-3 px-4">Время</th>
                  <th className="py-3 px-4">Ветка / Ветки</th>
                  <th className="py-3 px-4 text-center">Статус</th>
                  <th className="py-3 px-4">Детали / Смоуки</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-800/60 font-mono text-[11px]">
                {data?.merge_gate_runs?.map((g, idx) => (
                  <tr key={idx} className="hover:bg-slate-800/40 transition-colors">
                    <td className="py-2.5 px-4 text-slate-400">{g.timestamp}</td>
                    <td className="py-2.5 px-4 text-cyan-300 font-semibold">{g.branch}</td>
                    <td className="py-2.5 px-4 text-center">
                      <span
                        className={`px-2.5 py-0.5 rounded text-[10px] font-black ${
                          g.status === 'PASS'
                            ? 'bg-emerald-500/20 text-emerald-400 border border-emerald-500/30'
                            : 'bg-rose-500/20 text-rose-400 border border-rose-500/30'
                        }`}
                      >
                        {g.status}
                      </span>
                    </td>
                    <td className="py-2.5 px-4 text-slate-300 font-sans max-w-lg truncate">{g.details}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      )}
    </div>
  );
};
