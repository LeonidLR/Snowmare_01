import React, { useEffect, useState, useMemo } from 'react';
import {
  Tag,
  CheckCircle2,
  AlertTriangle,
  RotateCcw,
  Save,
  Loader2,
  Power,
  PowerOff,
  Search,
  Eye,
  EyeOff,
  Users,
  Skull,
  Compass,
  Monitor,
} from 'lucide-react';
import {
  HUD_GROUPS,
  type HudLabelsFile,
  type HudGroupId,
  setGroupShow,
  setAllShow,
  getGroupCounts,
  getTotalCounts,
} from '../utils/hudLabels.ts';

const GROUP_ICONS: Record<HudGroupId, React.ElementType> = {
  operatives: Users,
  enemies: Skull,
  world: Compass,
  hud: Monitor,
};

export const HudLabelsPanel: React.FC = () => {
  const [data, setData] = useState<HudLabelsFile | null>(null);
  const [initialData, setInitialData] = useState<HudLabelsFile | null>(null);
  const [loading, setLoading] = useState<boolean>(true);
  const [saving, setSaving] = useState<boolean>(false);
  const [statusMessage, setStatusMessage] = useState<{ kind: 'success' | 'error'; text: string } | null>(null);
  const [search, setSearch] = useState<string>('');
  const [filterMode, setFilterMode] = useState<'all' | 'on' | 'off'>('all');

  // Load data from dev-server
  const loadData = async () => {
    setLoading(true);
    setStatusMessage(null);
    try {
      const res = await fetch('/api/hud-labels');
      if (!res.ok) {
        throw new Error(`HTTP ${res.status}: ${res.statusText}`);
      }
      const json: HudLabelsFile = await res.json();
      setData(json);
      setInitialData(json);
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      setStatusMessage({ kind: 'error', text: `Не удалось загрузить hud_labels.json: ${msg}` });
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadData();
  }, []);

  // Check dirty state
  const isDirty = useMemo(() => {
    if (!data || !initialData) return false;
    return JSON.stringify(data.labels) !== JSON.stringify(initialData.labels);
  }, [data, initialData]);

  // Save changes through dev-server
  const handleSave = async () => {
    if (!data || saving) return;
    setSaving(true);
    setStatusMessage(null);
    try {
      const res = await fetch('/api/hud-labels', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data),
      });
      if (!res.ok) {
        const errJson = await res.json().catch(() => ({}));
        throw new Error(errJson.error || `HTTP ${res.status}`);
      }
      setInitialData(data);
      setStatusMessage({
        kind: 'success',
        text: 'Файл Content/Data/UI/hud_labels.json успешно сохранён! Игра обновит метки на лету.',
      });
      setTimeout(() => {
        setStatusMessage((curr) => (curr?.kind === 'success' ? null : curr));
      }, 5000);
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      setStatusMessage({ kind: 'error', text: `Ошибка сохранения: ${msg}` });
    } finally {
      setSaving(false);
    }
  };

  // Reset to initial
  const handleReset = () => {
    if (initialData) {
      setData(initialData);
      setStatusMessage(null);
    }
  };

  // Toggle single label
  const toggleLabel = (key: string) => {
    if (!data || !data.labels[key]) return;
    setData({
      ...data,
      labels: {
        ...data.labels,
        [key]: {
          ...data.labels[key],
          show: !data.labels[key].show,
        },
      },
    });
  };

  // Group toggles
  const handleGroupToggle = (groupId: HudGroupId, show: boolean) => {
    if (!data) return;
    setData(setGroupShow(data, groupId, show));
  };

  // Global toggles
  const handleGlobalToggle = (show: boolean) => {
    if (!data) return;
    setData(setAllShow(data, show));
  };

  const totalCounts = data ? getTotalCounts(data) : { total: 0, on: 0, off: 0 };

  if (loading) {
    return (
      <div className="flex flex-col items-center justify-center py-20 text-slate-400 gap-3">
        <Loader2 className="w-8 h-8 animate-spin text-amber-400" />
        <span className="text-sm font-medium">Загрузка Content/Data/UI/hud_labels.json...</span>
      </div>
    );
  }

  if (!data) {
    return (
      <div className="bg-slate-900 border border-rose-900/50 rounded-xl p-6 text-center text-rose-300">
        <AlertTriangle className="w-8 h-8 mx-auto mb-2 text-rose-400" />
        <h3 className="font-bold text-base mb-1">Файл hud_labels.json не найден</h3>
        <p className="text-xs text-rose-400/80 mb-4">{statusMessage?.text || 'Проверьте dev-сервер'}</p>
        <button
          onClick={loadData}
          className="px-4 py-2 bg-slate-800 hover:bg-slate-700 text-xs font-semibold rounded-lg transition-colors"
        >
          Повторить попытку
        </button>
      </div>
    );
  }

  const query = search.toLowerCase().trim();

  return (
    <div className="flex flex-col gap-6 max-w-7xl mx-auto w-full">
      {/* Top Banner & Action Controls */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-lg flex flex-col md:flex-row items-start md:items-center justify-between gap-4">
        <div className="flex items-center gap-3.5">
          <div className="w-10 h-10 rounded-xl bg-amber-500/20 text-amber-400 flex items-center justify-center border border-amber-500/30 shrink-0">
            <Tag className="w-5 h-5" />
          </div>
          <div>
            <div className="flex items-center gap-2.5">
              <h2 className="text-base font-bold text-slate-100">Метки интерфейса (HUD Labels)</h2>
              <span className="text-[11px] font-mono px-2 py-0.5 rounded bg-slate-800 text-slate-400 border border-slate-700">
                codex.hud_labels/1
              </span>
              {isDirty && (
                <span className="text-[11px] font-bold px-2 py-0.5 rounded bg-amber-500/20 text-amber-300 border border-amber-500/40 animate-pulse">
                  Есть несохранённые изменения
                </span>
              )}
            </div>
            <p className="text-xs text-slate-400 mt-0.5">
              26 переключателей плашек, маркеров и надписей. Игра перечитывает файл на лету в dev-сборках.
            </p>
          </div>
        </div>

        {/* Global Action Buttons */}
        <div className="flex flex-wrap items-center gap-2 self-stretch md:self-auto justify-end">
          <button
            onClick={() => handleGlobalToggle(true)}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-semibold bg-slate-800 hover:bg-slate-700 text-slate-200 border border-slate-700 transition-colors"
            title="Включить все 26 меток"
          >
            <Power className="w-3.5 h-3.5 text-emerald-400" />
            Все вкл
          </button>
          <button
            onClick={() => handleGlobalToggle(false)}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-semibold bg-slate-800 hover:bg-slate-700 text-slate-200 border border-slate-700 transition-colors"
            title="Выключить все 26 меток"
          >
            <PowerOff className="w-3.5 h-3.5 text-rose-400" />
            Все выкл
          </button>
          <button
            onClick={handleReset}
            disabled={!isDirty}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-semibold bg-slate-800 hover:bg-slate-700 disabled:opacity-40 disabled:hover:bg-slate-800 text-slate-300 border border-slate-700 transition-colors"
            title="Сбросить изменения к загруженному файлу"
          >
            <RotateCcw className="w-3.5 h-3.5" />
            Сброс
          </button>
          <button
            onClick={handleSave}
            disabled={!isDirty || saving}
            className={`flex items-center gap-1.5 px-4 py-1.5 rounded-lg text-xs font-bold transition-all shadow-md ${
              isDirty
                ? 'bg-emerald-500 hover:bg-emerald-400 text-slate-950 shadow-emerald-500/20'
                : 'bg-slate-800 text-slate-500 border border-slate-700 cursor-not-allowed'
            }`}
            title="Сохранить в Content/Data/UI/hud_labels.json через dev-сервер"
          >
            {saving ? (
              <Loader2 className="w-4 h-4 animate-spin text-slate-950" />
            ) : (
              <Save className="w-4 h-4" />
            )}
            Сохранить
          </button>
        </div>
      </div>

      {/* Status Alert */}
      {statusMessage && (
        <div
          className={`flex items-center gap-2 p-3 rounded-xl border text-xs font-medium ${
            statusMessage.kind === 'success'
              ? 'bg-emerald-950/60 border-emerald-800 text-emerald-300'
              : 'bg-rose-950/60 border-rose-800 text-rose-300'
          }`}
        >
          {statusMessage.kind === 'success' ? (
            <CheckCircle2 className="w-4 h-4 text-emerald-400 shrink-0" />
          ) : (
            <AlertTriangle className="w-4 h-4 text-rose-400 shrink-0" />
          )}
          <span>{statusMessage.text}</span>
        </div>
      )}

      {/* Filter and Stats Bar */}
      <div className="flex flex-col sm:flex-row items-stretch sm:items-center justify-between gap-3 bg-slate-950/60 p-3 rounded-xl border border-slate-800">
        <div className="relative flex-1 max-w-md">
          <Search className="w-4 h-4 absolute left-3 top-1/2 -translate-y-1/2 text-slate-500" />
          <input
            type="text"
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            placeholder="Поиск по названию, ключу или описанию..."
            className="w-full bg-slate-900 border border-slate-800 rounded-lg pl-9 pr-3 py-1.5 text-xs text-slate-200 placeholder-slate-500 focus:outline-none focus:border-amber-500"
          />
        </div>

        <div className="flex items-center gap-3 text-xs">
          <div className="flex items-center gap-1 bg-slate-900 border border-slate-800 rounded-lg p-1">
            <button
              onClick={() => setFilterMode('all')}
              className={`px-2.5 py-1 rounded text-[11px] font-semibold transition-all ${
                filterMode === 'all'
                  ? 'bg-slate-700 text-slate-100 shadow'
                  : 'text-slate-400 hover:text-slate-200'
              }`}
            >
              Все ({totalCounts.total})
            </button>
            <button
              onClick={() => setFilterMode('on')}
              className={`flex items-center gap-1 px-2.5 py-1 rounded text-[11px] font-semibold transition-all ${
                filterMode === 'on'
                  ? 'bg-emerald-600/30 text-emerald-300 border border-emerald-500/40 shadow'
                  : 'text-slate-400 hover:text-slate-200'
              }`}
            >
              <Eye className="w-3 h-3 text-emerald-400" />
              Вкл ({totalCounts.on})
            </button>
            <button
              onClick={() => setFilterMode('off')}
              className={`flex items-center gap-1 px-2.5 py-1 rounded text-[11px] font-semibold transition-all ${
                filterMode === 'off'
                  ? 'bg-rose-600/30 text-rose-300 border border-rose-500/40 shadow'
                  : 'text-slate-400 hover:text-slate-200'
              }`}
            >
              <EyeOff className="w-3 h-3 text-rose-400" />
              Выкл ({totalCounts.off})
            </button>
          </div>
        </div>
      </div>

      {/* The 4 Group Sections */}
      <div className="flex flex-col gap-6">
        {HUD_GROUPS.map((group) => {
          const counts = getGroupCounts(data, group.id);
          const Icon = GROUP_ICONS[group.id];

          // Filter group keys based on search and on/off filter
          const matchingKeys = group.keys.filter((k) => {
            const item = data.labels[k];
            if (!item) return false;
            if (filterMode === 'on' && !item.show) return false;
            if (filterMode === 'off' && item.show) return false;
            if (query) {
              const inKey = k.toLowerCase().includes(query);
              const inLabel = item.label.toLowerCase().includes(query);
              const inDoc = item.doc.toLowerCase().includes(query);
              return inKey || inLabel || inDoc;
            }
            return true;
          });

          if (matchingKeys.length === 0 && (query || filterMode !== 'all')) {
            return null;
          }

          return (
            <div
              key={group.id}
              className="bg-slate-900/90 border border-slate-800 rounded-xl overflow-hidden shadow"
            >
              {/* Group Header */}
              <div className="bg-slate-950/80 px-4 py-3 border-b border-slate-800 flex flex-wrap items-center justify-between gap-3">
                <div className="flex items-center gap-2.5">
                  <div className="w-7 h-7 rounded-lg bg-slate-800 text-amber-400 flex items-center justify-center border border-slate-700 shrink-0">
                    <Icon className="w-4 h-4" />
                  </div>
                  <div>
                    <div className="flex items-center gap-2">
                      <h3 className="text-sm font-bold text-slate-100">{group.title}</h3>
                      <span className="text-[11px] font-mono px-2 py-0.5 rounded-full bg-slate-800 text-slate-300 border border-slate-700">
                        {counts.on} / {counts.total} вкл
                      </span>
                    </div>
                    <p className="text-[11px] text-slate-400">{group.description}</p>
                  </div>
                </div>

                {/* Group All On / All Off */}
                <div className="flex items-center gap-1.5">
                  <button
                    onClick={() => handleGroupToggle(group.id, true)}
                    className="flex items-center gap-1 px-2.5 py-1 rounded text-[11px] font-semibold bg-slate-800 hover:bg-slate-700 text-emerald-300 border border-slate-700 transition-colors"
                    title={`Включить все метки группы «${group.title}»`}
                  >
                    <Power className="w-3 h-3 text-emerald-400" />
                    Все вкл
                  </button>
                  <button
                    onClick={() => handleGroupToggle(group.id, false)}
                    className="flex items-center gap-1 px-2.5 py-1 rounded text-[11px] font-semibold bg-slate-800 hover:bg-slate-700 text-rose-300 border border-slate-700 transition-colors"
                    title={`Выключить все метки группы «${group.title}»`}
                  >
                    <PowerOff className="w-3 h-3 text-rose-400" />
                    Все выкл
                  </button>
                </div>
              </div>

              {/* Switches Grid */}
              <div className="p-4 grid grid-cols-1 md:grid-cols-2 xl:grid-cols-3 gap-3">
                {matchingKeys.map((key) => {
                  const item = data.labels[key];
                  if (!item) return null;
                  const isChecked = item.show;

                  return (
                    <div
                      key={key}
                      onClick={() => toggleLabel(key)}
                      className={`p-3 rounded-xl border transition-all cursor-pointer select-none flex flex-col justify-between gap-2.5 ${
                        isChecked
                          ? 'bg-slate-900 border-slate-700/80 hover:border-emerald-500/60 shadow-sm'
                          : 'bg-slate-950/60 border-slate-800/80 opacity-60 hover:opacity-100 hover:border-slate-700'
                      }`}
                    >
                      <div className="flex items-start justify-between gap-3">
                        <div className="flex-1 min-w-0">
                          <div className="flex items-center gap-2 flex-wrap">
                            <span className="text-xs font-bold text-slate-100 truncate">
                              {item.label}
                            </span>
                          </div>
                          <span className="text-[10px] font-mono text-slate-500 block truncate">
                            {key}
                          </span>
                        </div>

                        {/* Switch toggle control */}
                        <div
                          className={`w-9 h-5 rounded-full transition-colors relative shrink-0 p-0.5 ${
                            isChecked ? 'bg-emerald-500' : 'bg-slate-800 border border-slate-700'
                          }`}
                        >
                          <div
                            className={`w-4 h-4 rounded-full bg-slate-950 shadow-md transition-transform transform ${
                              isChecked ? 'translate-x-4' : 'translate-x-0'
                            }`}
                          />
                        </div>
                      </div>

                      <p className="text-[11px] text-slate-400 line-clamp-2 leading-relaxed" title={item.doc}>
                        {item.doc}
                      </p>
                    </div>
                  );
                })}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
};
