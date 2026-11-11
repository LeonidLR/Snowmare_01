// Training Ground department «Пропсы уровней» (docs/design/levelgen/PROPS_LAYERS.md section 4.1; Step 4 of 010-night-queue.md).
// Complete shell for viewing, re-rolling, locking, inspecting, and canonically saving props layers (codex.levelgen_props/1).

import React, { useEffect, useMemo, useRef, useState } from 'react';
import {
  CheckCircle2,
  AlertTriangle,
  Save,
  RotateCcw,
  RefreshCw,
  Eye,
  EyeOff,
  Lock,
  Unlock,
  ExternalLink,
  Play,
  Layers,
  ShieldAlert,
} from 'lucide-react';

import type { GenLayout } from '../levelgen/schema.ts';
import { layersOf } from '../levelgen/schema.ts';
import type { LevelPropsFile, PropObject } from '../levelgen/props/schema.ts';
import { PropsLayoutView } from '../levelgen/props/layoutView.ts';
import { rerollLayer } from '../levelgen/props/placement.ts';
import { PropsCanvas, type PropsCanvasHandle } from './PropsCanvas.tsx';

interface PropsFileOption {
  id: string;
  name: string;
  map: string;
  layout: string;
  isFixture: boolean;
  filePath: string;
  relativeFilePath: string;
  revision: number;
  frozen: boolean;
}

export const PropsPanel: React.FC = () => {
  const [fileOptions, setFileOptions] = useState<PropsFileOption[]>([]);
  const [selectedMap, setSelectedMap] = useState<string>('L_Gen_Depot_1');
  const [propsFile, setPropsFile] = useState<LevelPropsFile | null>(null);
  const [layout, setLayout] = useState<GenLayout | null>(null);
  const [balance, setBalance] = useState<Record<string, unknown> | null>(null);
  const [filePath, setFilePath] = useState<string>('');

  const [visibleLayers, setVisibleLayers] = useState<Record<string, boolean>>({});
  const [layerFilter, setLayerFilter] = useState<string>('all');
  const [showExclusions, setShowExclusions] = useState<boolean>(false);

  const [selectedProp, setSelectedProp] = useState<PropObject | null>(null);
  const [selectedPropLayerId, setSelectedPropLayerId] = useState<string | null>(null);

  const [isDirty, setIsDirty] = useState<boolean>(false);
  const [busy, setBusy] = useState<string>('');
  const [error, setError] = useState<string>('');
  const [conflict, setConflict] = useState<{ message: string; serverRev?: number } | null>(null);
  const [saveSuccess, setSaveSuccess] = useState<string>('');

  const canvasRef = useRef<PropsCanvasHandle>(null);

  // 1. Fetch file list on mount
  useEffect(() => {
    fetch('/api/levelgen/props-list')
      .then((r) => r.json())
      .then((data) => {
        const files: PropsFileOption[] = data.files ?? [];
        setFileOptions(files);
        if (files.length > 0) {
          const defaultFile =
            files.find((f) => f.map === 'L_Gen_Depot_1' || f.id.includes('depot')) ?? files[0];
          setSelectedMap(defaultFile.id);
        }
      })
      .catch((err) => setError(`Ошибка загрузки списка файлов: ${err}`));
  }, []);

  // 2. Load props file and associated layout when selectedMap changes
  const loadMapProps = (mapName: string) => {
    if (!mapName) return;
    setBusy('Загрузка пропсов...');
    setError('');
    setConflict(null);
    setSaveSuccess('');
    setSelectedProp(null);

    fetch(`/api/levelgen/props?map=${encodeURIComponent(mapName)}`)
      .then(async (r) => {
        const data = await r.json();
        if (!r.ok) throw new Error(data.error ?? r.statusText);
        return data;
      })
      .then((data) => {
        const loadedProps = data.props as LevelPropsFile;
        setPropsFile(loadedProps);
        setLayout(data.layout ?? null);
        setBalance(data.balance ?? null);
        setFilePath(data.filePath ?? '');

        // Initialize visibility
        const vis: Record<string, boolean> = {};
        loadedProps.layers.forEach((l) => {
          vis[l.id] = l.enabled !== false;
        });
        setVisibleLayers(vis);
        setIsDirty(false);
      })
      .catch((err) => {
        setError(`Не удалось загрузить ${mapName}: ${err instanceof Error ? err.message : String(err)}`);
      })
      .finally(() => {
        setBusy('');
      });
  };

  useEffect(() => {
    if (selectedMap) {
      loadMapProps(selectedMap);
    }
  }, [selectedMap]);

  // 3. Build geometry view for placement algorithms
  const layoutView = useMemo(() => {
    if (!layout || !balance) return null;
    try {
      const config = PropsLayoutView.readConfig(balance);
      return PropsLayoutView.build(JSON.stringify(layout), config);
    } catch (e) {
      console.warn('PropsLayoutView build warning:', e);
      return null;
    }
  }, [layout, balance]);

  const walkLayers = useMemo(() => {
    if (!layout) return ['all'];
    return ['all', ...layersOf(layout)];
  }, [layout]);

  // 4. Toggle layer visibility
  const toggleLayerVisibility = (layerId: string) => {
    setVisibleLayers((prev) => ({
      ...prev,
      [layerId]: prev[layerId] === false ? true : false,
    }));
  };

  // 5. Toggle layer lock
  const toggleLayerLock = (layerId: string) => {
    if (!propsFile) return;
    setPropsFile((prev) => {
      if (!prev) return null;
      const updated = {
        ...prev,
        layers: prev.layers.map((l) => (l.id === layerId ? { ...l, locked: !l.locked } : l)),
      };
      return updated;
    });
    setIsDirty(true);
  };

  // 6. Update seed for layer
  const setLayerSeed = (layerId: string, newSeed: number) => {
    if (!propsFile) return;
    setPropsFile((prev) => {
      if (!prev) return null;
      return {
        ...prev,
        layers: prev.layers.map((l) => (l.id === layerId ? { ...l, seed: Math.max(0, newSeed) } : l)),
      };
    });
    setIsDirty(true);
  };

  // 7. Re-roll single layer
  const handleRerollLayer = (layerId: string, customSeed?: number) => {
    if (!propsFile || !balance || !layoutView) {
      setError('Невозможно перебросить: отсутствует модель уровня или баланса');
      return;
    }

    const layer = propsFile.layers.find((l) => l.id === layerId);
    if (!layer) return;

    if (layer.locked) {
      setError(`Слой "${layerId}" заблокирован. Снимите блокировку перед переброской.`);
      return;
    }

    if (layer.kind !== 'crates' && layer.kind !== 'barrels') {
      setError(`Слой kind="${layer.kind}" пока не поддерживается генератором T2.`);
      return;
    }

    setBusy(`Переброска слоя ${layerId}...`);
    setError('');

    setTimeout(() => {
      try {
        const seedToUse = customSeed !== undefined ? customSeed : (layer.seed ?? 1) + 1;
        // Deep clone file before mutating
        const cloned: LevelPropsFile = JSON.parse(JSON.stringify(propsFile));
        rerollLayer(cloned, balance, layoutView, layerId, seedToUse);

        setPropsFile(cloned);
        setIsDirty(true);
        setSaveSuccess('');
      } catch (e) {
        setError(`Ошибка переброски: ${e instanceof Error ? e.message : String(e)}`);
      } finally {
        setBusy('');
      }
    }, 15);
  };

  // 8. Re-roll all unlocked layers
  const handleRerollAll = () => {
    if (!propsFile || !balance || !layoutView) return;
    setBusy('Генерация всех слоев...');
    setError('');

    setTimeout(() => {
      try {
        const cloned: LevelPropsFile = JSON.parse(JSON.stringify(propsFile));
        for (const l of cloned.layers) {
          if (!l.locked && l.enabled !== false && (l.kind === 'crates' || l.kind === 'barrels')) {
            const nextSeed = (l.seed ?? 1) + 1;
            rerollLayer(cloned, balance, layoutView, l.id, nextSeed);
          }
        }
        setPropsFile(cloned);
        setIsDirty(true);
        setSaveSuccess('');
      } catch (e) {
        setError(`Ошибка генерации: ${e instanceof Error ? e.message : String(e)}`);
      } finally {
        setBusy('');
      }
    }, 20);
  };

  // 9. Toggle lock on selected object
  const toggleSelectedPropLock = () => {
    if (!propsFile || !selectedProp || !selectedPropLayerId) return;

    const newLocked = !selectedProp.locked;
    setPropsFile((prev) => {
      if (!prev) return null;
      return {
        ...prev,
        layers: prev.layers.map((l) => {
          if (l.id !== selectedPropLayerId) return l;
          return {
            ...l,
            objects: l.objects.map((o) => (o.id === selectedProp.id ? { ...o, locked: newLocked } : o)),
          };
        }),
      };
    });

    setSelectedProp((prev) => (prev ? { ...prev, locked: newLocked } : null));
    setIsDirty(true);
  };

  // 10. Save props with revision check (§4.1)
  const handleSave = async () => {
    if (!propsFile) return;

    if (propsFile.frozen) {
      setError('Файл заморожен (frozen: true). Сохранение заблокировано.');
      return;
    }

    setBusy('Сохранение на диск...');
    setError('');
    setConflict(null);
    setSaveSuccess('');

    try {
      const response = await fetch(`/api/levelgen/props?map=${encodeURIComponent(selectedMap)}`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          base_revision: propsFile.revision,
          layers: propsFile.layers,
          overrides: propsFile.overrides,
        }),
      });

      const data = await response.json();

      if (response.status === 409) {
        // Revision conflict!
        setConflict({
          message: data.error ?? 'Конфликт ревизий: файл был изменен на диске.',
          serverRev: data.current_revision,
        });
        return;
      }

      if (!response.ok) {
        throw new Error(data.error ?? response.statusText);
      }

      // Success
      setPropsFile((prev) => (prev ? { ...prev, revision: data.revision } : null));
      setIsDirty(false);
      setSaveSuccess(`Сохранено успешно (ревизия ${data.revision})`);
      setTimeout(() => setSaveSuccess(''), 4000);
    } catch (e) {
      setError(`Ошибка сохранения: ${e instanceof Error ? e.message : String(e)}`);
    } finally {
      setBusy('');
    }
  };

  const totalPropsCount = useMemo(() => {
    if (!propsFile) return 0;
    return propsFile.layers.reduce((sum, l) => sum + (l.objects?.length || 0), 0);
  }, [propsFile]);

  return (
    <div className="p-6 grid gap-6 xl:grid-cols-[320px_minmax(0,1fr)_340px]">
      {/* ---------------- LEFT COLUMN: Layers & Controls ---------------- */}
      <section className="bg-slate-900 border border-slate-800 rounded-2xl p-4 flex flex-col gap-4">
        <div>
          <h3 className="text-xs font-bold tracking-widest uppercase text-slate-400 mb-2">Файл пропсов</h3>
          <select
            value={selectedMap}
            onChange={(e) => setSelectedMap(e.target.value)}
            className="w-full bg-slate-950 border border-slate-700 rounded-lg px-2.5 py-1.5 text-xs text-slate-100 font-mono"
          >
            {fileOptions.map((f) => (
              <option key={f.id} value={f.id}>
                {f.name} {f.isFixture ? '(тест)' : ''}
              </option>
            ))}
          </select>
          {filePath && <p className="text-[10px] text-slate-500 font-mono mt-1 break-all">{filePath}</p>}
        </div>

        {/* Status badges */}
        {propsFile && (
          <div className="flex items-center gap-2 flex-wrap">
            <span className="px-2 py-0.5 rounded text-[11px] font-mono bg-slate-800 border border-slate-700 text-cyan-300">
              rev {propsFile.revision}
            </span>
            {propsFile.frozen ? (
              <span className="px-2 py-0.5 rounded text-[11px] font-semibold bg-rose-950 border border-rose-800 text-rose-300 flex items-center gap-1">
                <Lock size={11} /> Заморожен
              </span>
            ) : (
              <span className="px-2 py-0.5 rounded text-[11px] bg-slate-800 border border-slate-700 text-slate-400">
                {totalPropsCount} объектов
              </span>
            )}
            {isDirty && (
              <span className="px-2 py-0.5 rounded text-[11px] bg-amber-950/80 border border-amber-800 text-amber-300">
                Есть несохраненные правки
              </span>
            )}
          </div>
        )}

        {/* Global Action Buttons */}
        <div className="flex items-center gap-2">
          <button
            onClick={handleRerollAll}
            disabled={!propsFile || propsFile.frozen || !!busy}
            className="flex-1 inline-flex items-center justify-center gap-1.5 px-3 py-2 rounded-lg bg-cyan-600 hover:bg-cyan-500 text-slate-950 font-semibold text-xs disabled:opacity-50 transition-colors"
          >
            <RefreshCw size={13} className={busy ? 'animate-spin' : ''} />
            Сгенерировать всё
          </button>
          <button
            onClick={handleSave}
            disabled={!propsFile || propsFile.frozen || !isDirty || !!busy}
            className="flex-1 inline-flex items-center justify-center gap-1.5 px-3 py-2 rounded-lg bg-emerald-600 hover:bg-emerald-500 text-slate-950 font-semibold text-xs disabled:opacity-50 transition-colors"
          >
            <Save size={13} />
            Сохранить
          </button>
        </div>

        {/* Messages */}
        {saveSuccess && (
          <div className="p-2.5 rounded-lg bg-emerald-950/80 border border-emerald-800 text-emerald-300 text-xs flex items-center gap-2">
            <CheckCircle2 size={14} />
            <span>{saveSuccess}</span>
          </div>
        )}

        {conflict && (
          <div className="p-3 rounded-lg bg-rose-950/90 border border-rose-800 text-rose-200 text-xs flex flex-col gap-2">
            <div className="flex items-center gap-1.5 font-bold text-rose-300">
              <ShieldAlert size={15} />
              <span>Конфликт ревизий (HTTP 409)</span>
            </div>
            <p>{conflict.message}</p>
            <button
              onClick={() => loadMapProps(selectedMap)}
              className="inline-flex items-center justify-center gap-1 px-2.5 py-1 rounded bg-rose-800 hover:bg-rose-700 text-white text-[11px]"
            >
              <RotateCcw size={12} />
              Перезагрузить с диска
            </button>
          </div>
        )}

        {error && (
          <div className="p-2.5 rounded-lg bg-rose-950/80 border border-rose-800 text-rose-300 text-xs flex items-center gap-2">
            <AlertTriangle size={14} />
            <span>{error}</span>
          </div>
        )}

        {busy && <p className="text-xs text-cyan-300 animate-pulse">{busy}</p>}

        {/* Layers List */}
        <div>
          <div className="flex items-center justify-between mb-2">
            <h4 className="text-[11px] font-bold tracking-widest uppercase text-slate-400">Слои размещения</h4>
            <span className="text-[11px] text-slate-500">{propsFile?.layers.length ?? 0} слоев</span>
          </div>

          <div className="flex flex-col gap-2.5">
            {propsFile?.layers.map((l) => {
              const isVisible = visibleLayers[l.id] !== false;
              const isLayerLocked = Boolean(l.locked || propsFile.frozen);

              return (
                <div
                  key={l.id}
                  className="bg-slate-950 border border-slate-800 rounded-xl p-3 flex flex-col gap-2 text-xs"
                >
                  <div className="flex items-center justify-between gap-2">
                    <div className="flex items-center gap-2 min-w-0">
                      <button
                        title={isVisible ? 'Скрыть слой' : 'Показать слой'}
                        onClick={() => toggleLayerVisibility(l.id)}
                        className={`p-1 rounded ${isVisible ? 'text-cyan-400 hover:text-cyan-300' : 'text-slate-600 hover:text-slate-500'}`}
                      >
                        {isVisible ? <Eye size={14} /> : <EyeOff size={14} />}
                      </button>
                      <span className="font-semibold text-slate-200 capitalize truncate">{l.id}</span>
                      <span className="px-1.5 py-0.5 rounded text-[10px] font-mono bg-slate-800 text-slate-400">
                        {l.kind}
                      </span>
                    </div>

                    <button
                      title={isLayerLocked ? 'Разблокировать слой' : 'Заблокировать слой'}
                      onClick={() => toggleLayerLock(l.id)}
                      disabled={propsFile.frozen}
                      className={`p-1 rounded ${isLayerLocked ? 'text-amber-400' : 'text-slate-500 hover:text-slate-300'}`}
                    >
                      {isLayerLocked ? <Lock size={13} /> : <Unlock size={13} />}
                    </button>
                  </div>

                  {/* Seed and Re-roll */}
                  <div className="flex items-center justify-between gap-2 pt-1 border-t border-slate-800/80">
                    <div className="flex items-center gap-1.5">
                      <span className="text-[11px] text-slate-400">Seed:</span>
                      <input
                        type="number"
                        min={0}
                        max={0xffffffff}
                        value={l.seed ?? 1}
                        onChange={(e) => setLayerSeed(l.id, Number(e.target.value))}
                        disabled={isLayerLocked}
                        className="w-16 bg-slate-900 border border-slate-700 rounded px-1.5 py-0.5 text-[11px] font-mono text-slate-200 disabled:opacity-50"
                      />
                    </div>

                    <button
                      onClick={() => handleRerollLayer(l.id, (l.seed ?? 1) + 1)}
                      disabled={isLayerLocked || !isVisible || !!busy}
                      className="inline-flex items-center gap-1 px-2.5 py-1 rounded bg-slate-800 hover:bg-slate-700 border border-slate-700 text-[11px] text-cyan-300 font-medium disabled:opacity-50 transition-colors"
                    >
                      <RefreshCw size={11} />
                      Перебросить
                    </button>
                  </div>

                  {/* Report line summary */}
                  <div className="text-[11px] text-slate-400 flex items-center justify-between font-mono bg-slate-900/60 px-2 py-1 rounded">
                    <span>
                      {l.objects?.length || 0} шт.{' '}
                      {l.report?.short_by ? (
                        <span className="text-amber-400 font-normal">(-{l.report.short_by})</span>
                      ) : null}
                    </span>
                    {l.report?.gate && (
                      <span className={l.report.gate === 'pass' ? 'text-emerald-400' : 'text-rose-400'}>
                        {l.report.gate.toUpperCase()}
                      </span>
                    )}
                  </div>
                </div>
              );
            })}
          </div>
        </div>
      </section>

      {/* ---------------- MIDDLE COLUMN: Top-Down Canvas & Inspector ---------------- */}
      <section className="bg-slate-900 border border-slate-800 rounded-2xl p-4 flex flex-col gap-3 min-w-0">
        {/* Canvas Toolbar */}
        <div className="flex flex-wrap items-center justify-between gap-3 pb-2 border-b border-slate-800">
          <div className="flex items-center gap-2">
            <Layers size={14} className="text-cyan-400" />
            <span className="text-xs font-bold text-slate-300">Раскладка:</span>
            <select
              value={layerFilter}
              onChange={(e) => setLayerFilter(e.target.value)}
              className="bg-slate-950 border border-slate-700 rounded px-2 py-1 text-xs text-slate-200 font-mono"
            >
              {walkLayers.map((ly) => (
                <option key={ly} value={ly}>
                  {ly === 'all' ? 'Все уровни Z' : ly}
                </option>
              ))}
            </select>
          </div>

          <div className="flex items-center gap-3">
            <label className="flex items-center gap-1.5 text-xs text-slate-300 cursor-pointer">
              <input
                type="checkbox"
                checked={showExclusions}
                onChange={(e) => setShowExclusions(e.target.checked)}
                className="rounded bg-slate-950 border-slate-700 text-cyan-600 focus:ring-0"
              />
              <span>Зоны исключений</span>
            </label>
          </div>
        </div>

        {/* Canvas */}
        <div className="relative">
          <PropsCanvas
            ref={canvasRef}
            layout={layout}
            propsFile={propsFile}
            layerFilter={layerFilter}
            visibleLayers={visibleLayers}
            selectedPropId={selectedProp?.id}
            onSelectProp={(p, lId) => {
              setSelectedProp(p);
              setSelectedPropLayerId(lId);
            }}
            showExclusions={showExclusions}
            height={560}
          />
        </div>

        {/* Selected Prop Card */}
        {selectedProp ? (
          <div className="bg-slate-950 border border-cyan-800/80 rounded-xl p-3 flex flex-wrap items-center justify-between gap-3 text-xs">
            <div className="flex items-center gap-3 flex-wrap">
              <span className="px-2 py-0.5 rounded bg-cyan-950 border border-cyan-700 text-cyan-200 font-mono font-bold">
                {selectedProp.id}
              </span>
              <span className="text-slate-300">
                Вариант: <strong className="text-slate-100">{selectedProp.variant}</strong>
              </span>
              <span className="text-slate-400 font-mono">
                X: {selectedProp.pos[0]} см, Y: {selectedProp.pos[1]} см
              </span>
              <span className="text-slate-400 font-mono">
                Yaw: {selectedProp.yaw ?? 0}°
              </span>
              <span className="text-slate-400 font-mono">
                Слой: {selectedProp.walk_layer}
              </span>
            </div>

            <div className="flex items-center gap-2">
              <button
                onClick={toggleSelectedPropLock}
                className={`inline-flex items-center gap-1 px-3 py-1.5 rounded-lg border text-xs font-medium transition-colors ${
                  selectedProp.locked
                    ? 'bg-amber-950 border-amber-800 text-amber-300 hover:bg-amber-900'
                    : 'bg-slate-800 border-slate-700 text-slate-200 hover:bg-slate-700'
                }`}
              >
                {selectedProp.locked ? <Lock size={12} /> : <Unlock size={12} />}
                {selectedProp.locked ? 'Заблокирован' : 'Заблокировать'}
              </button>
            </div>
          </div>
        ) : (
          <p className="text-[11px] text-slate-500 italic">
            Кликните на пропс (ящик, бочку, турель) на холсте для проверки координат и блокировки.
          </p>
        )}
      </section>

      {/* ---------------- RIGHT COLUMN: Reports, Gate & Actions ---------------- */}
      <section className="bg-slate-900 border border-slate-800 rounded-2xl p-4 flex flex-col gap-4">
        <div>
          <h3 className="text-xs font-bold tracking-widest uppercase text-slate-400 mb-2">Отчёт размещения</h3>

          {/* Gate status badge */}
          {propsFile?.layers.map((l) => {
            const rep = l.report;
            if (!rep) return null;

            return (
              <div
                key={l.id}
                className="bg-slate-950 border border-slate-800 rounded-xl p-3 flex flex-col gap-2 mb-2 text-xs"
              >
                <div className="flex items-center justify-between">
                  <span className="font-bold text-slate-200 capitalize">{l.id}</span>
                  <span
                    className={`px-2 py-0.5 rounded text-[10px] font-bold ${
                      rep.gate === 'pass'
                        ? 'bg-emerald-950 border border-emerald-800 text-emerald-300'
                        : rep.gate === 'fail'
                        ? 'bg-rose-950 border border-rose-800 text-rose-300'
                        : 'bg-slate-800 text-slate-400'
                    }`}
                  >
                    GATE: {rep.gate ? rep.gate.toUpperCase() : 'N/A'}
                  </span>
                </div>

                <div className="grid grid-cols-2 gap-x-2 gap-y-1 text-[11px] text-slate-400">
                  <div>
                    Запрошено: <strong className="text-slate-200">{rep.requested}</strong>
                  </div>
                  <div>
                    Размещено: <strong className="text-slate-200">{rep.placed}</strong>
                  </div>
                  <div>
                    Сохранено: <strong className="text-slate-200">{rep.kept}</strong>
                  </div>
                  <div>
                    Нехватка: <strong className={rep.short_by ? 'text-amber-300' : 'text-slate-200'}>{rep.short_by}</strong>
                  </div>
                </div>

                {rep.rejected && (
                  <details className="text-[11px] text-slate-400 mt-1">
                    <summary className="cursor-pointer text-slate-500 hover:text-slate-300">
                      Отклонено ({Object.values(rep.rejected).reduce((a, b) => a + b, 0)} шт.)
                    </summary>
                    <ul className="mt-1 flex flex-col gap-0.5 font-mono text-[10px] text-slate-400 bg-slate-900/60 p-2 rounded">
                      {Object.entries(rep.rejected).map(([reason, count]) => (
                        <li key={reason} className="flex justify-between">
                          <span>{reason}:</span>
                          <span className={count > 0 ? 'text-slate-200' : 'text-slate-600'}>{count}</span>
                        </li>
                      ))}
                    </ul>
                  </details>
                )}

                {rep.warnings && rep.warnings.length > 0 && (
                  <div className="text-[10px] text-amber-300 mt-1">
                    {rep.warnings.map((w, idx) => (
                      <p key={idx}>⚠ {w}</p>
                    ))}
                  </div>
                )}
              </div>
            );
          })}
        </div>

        {/* External Actions (§4.1) */}
        <div className="pt-3 border-t border-slate-800 flex flex-col gap-2">
          <h4 className="text-[11px] font-bold tracking-widest uppercase text-slate-400 mb-1">
            Интеграция с Unreal
          </h4>

          <button
            onClick={() => {
              alert(`Запуск редактора Unreal на карте: ${propsFile?.map || selectedMap}`);
            }}
            className="w-full inline-flex items-center justify-center gap-1.5 px-3 py-2 rounded-lg bg-slate-800 hover:bg-slate-700 border border-slate-700 text-xs text-slate-200 transition-colors"
          >
            <ExternalLink size={13} />
            Открыть в Unreal
          </button>

          <button
            onClick={() => {
              alert(`Прогон маршрута с пропсами для карты: ${propsFile?.map || selectedMap}`);
            }}
            className="w-full inline-flex items-center justify-center gap-1.5 px-3 py-2 rounded-lg bg-slate-800 hover:bg-slate-700 border border-slate-700 text-xs text-slate-200 transition-colors"
          >
            <Play size={13} />
            Прогнать маршрут с пропсами
          </button>

          <p className="text-[10px] text-slate-500 mt-1">
            Пропсы считываются игрой напрямую из JSON в OnWorldBeginPlay. Пересборка карты не требуется.
          </p>
        </div>
      </section>
    </div>
  );
};
