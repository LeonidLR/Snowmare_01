import React, { useState, useEffect, useRef } from 'react';
import { Play, Pause, RotateCcw, Film, Shield, AlertTriangle, Mountain, Flame, Download, CheckCircle2, XCircle, HelpCircle, Trash2, RefreshCw, Info } from 'lucide-react';
import { SpatialTelemetryRun, SpatialRunHeader } from '../types/spatialTelemetry';
import { ReplayCanvas } from './ReplayCanvas';
import { SimpleGifWriter } from '../utils/gifExporter';

export const TacticalReplayPlayer: React.FC = () => {
  const [runsList, setRunsList] = useState<SpatialRunHeader[]>([]);
  const [selectedFile, setSelectedFile] = useState<string>('');
  const [telemetry, setTelemetry] = useState<SpatialTelemetryRun | null>(null);
  const [loading, setLoading] = useState<boolean>(false);

  // Контролы воспроизведения
  const [isPlaying, setIsPlaying] = useState<boolean>(false);
  const [currentTime, setCurrentTime] = useState<number>(0);
  const [playbackSpeed, setPlaybackSpeed] = useState<number>(1.0);

  // Переключатели слоев левел-дизайна и легенда
  const [showCoversOverlay, setShowCoversOverlay] = useState<boolean>(true);
  const [showChokeOverlay, setShowChokeOverlay] = useState<boolean>(true);
  const [showElevationOverlay, setShowElevationOverlay] = useState<boolean>(true);
  const [showEventsOverlay, setShowEventsOverlay] = useState<boolean>(true);
  const [showLegend, setShowLegend] = useState<boolean>(true);

  // Экспорт GIF
  const [isExportingGif, setIsExportingGif] = useState<boolean>(false);
  const [exportProgress, setExportProgress] = useState<number>(0);

  const canvasRef = useRef<HTMLCanvasElement>(null);
  const animFrameIdRef = useRef<number | null>(null);
  const lastTimeRef = useRef<number>(performance.now());

  // Загрузка списка забегов
  const loadRunsList = async () => {
    try {
      const res = await fetch('/api/get-spatial-telemetry-list');
      if (res.ok) {
        const list: SpatialRunHeader[] = await res.json();
        setRunsList(list);
        if (list.length > 0) {
          // Если текущий выбранный файл больше не существует в списке, выбираем первый
          if (!selectedFile || !list.some((r) => r.fileName === selectedFile)) {
            setSelectedFile(list[0].fileName);
          }
        } else {
          setSelectedFile('');
          setTelemetry(null);
        }
      }
    } catch (e) {
      console.warn('Не удалось загрузить список забегов пространственной телеметрии:', e);
    }
  };

  // Удаление отдельного забега и связанных с ним гифок
  const handleDeleteCurrentRun = async () => {
    if (!selectedFile) return;
    const ok = window.confirm(`Удалить выбранный забег "${selectedFile}" и связанную с ним GIF-анимацию?\nЭто действие нельзя отменить.`);
    if (!ok) return;

    try {
      const res = await fetch('/api/delete-spatial-telemetry', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ file: selectedFile })
      });
      const data = await res.json();
      if (data.success) {
        await loadRunsList();
      } else {
        alert('Ошибка при удалении: ' + (data.error || 'Неизвестная ошибка'));
      }
    } catch (err: any) {
      alert('Ошибка при удалении: ' + err.message);
    }
  };

  // Полное опустошение списка забегов и удаление всех GIF
  const handleClearAllRuns = async () => {
    if (runsList.length === 0) {
      alert('Список забегов уже пуст.');
      return;
    }
    const ok = window.confirm(
      `⚠️ ВНИМАНИЕ: Очистить ВСЕ (${runsList.length}) забегов и удалить все связанные GIF-анимации с диска?\nЭто освободит память и полностью опустошит список.`
    );
    if (!ok) return;

    try {
      const res = await fetch('/api/clear-spatial-telemetry', { method: 'POST' });
      const data = await res.json();
      if (data.success) {
        alert(`Список очищен. Удалено файлов забегов: ${data.deletedRuns}, удалено GIF-файлов: ${data.deletedGifs}.`);
        setTelemetry(null);
        setSelectedFile('');
        await loadRunsList();
      } else {
        alert('Ошибка при очистке: ' + (data.error || 'Неизвестная ошибка'));
      }
    } catch (err: any) {
      alert('Ошибка при очистке: ' + err.message);
    }
  };

  useEffect(() => {
    loadRunsList();
  }, []);

  // Загрузка конкретного забега при смене файла
  useEffect(() => {
    if (!selectedFile) return;
    const fetchRunData = async () => {
      setLoading(true);
      try {
        const res = await fetch(`/api/get-spatial-telemetry?file=${encodeURIComponent(selectedFile)}`);
        if (res.ok) {
          const data: SpatialTelemetryRun = await res.json();
          setTelemetry(data);
          setCurrentTime(0);
          setIsPlaying(false);
        }
      } catch (e) {
        console.error('Ошибка загрузки данных пространственной телеметрии:', e);
      } finally {
        setLoading(false);
      }
    };
    fetchRunData();
  }, [selectedFile]);

  // Игровой цикл таймлайна (Playback loop)
  useEffect(() => {
    if (!isPlaying || !telemetry) return;

    lastTimeRef.current = performance.now();
    const maxTime = telemetry.duration_sec || (telemetry.frames.length > 0 ? telemetry.frames[telemetry.frames.length - 1].t : 30);

    const step = (now: number) => {
      const deltaSec = (now - lastTimeRef.current) / 1000;
      lastTimeRef.current = now;

      setCurrentTime((prev) => {
        const next = prev + deltaSec * playbackSpeed;
        if (next >= maxTime) {
          setIsPlaying(false);
          return maxTime;
        }
        return next;
      });

      animFrameIdRef.current = requestAnimationFrame(step);
    };

    animFrameIdRef.current = requestAnimationFrame(step);
    return () => {
      if (animFrameIdRef.current) cancelAnimationFrame(animFrameIdRef.current);
    };
  }, [isPlaying, playbackSpeed, telemetry]);

  const togglePlay = () => {
    if (!telemetry) return;
    const maxTime = telemetry.duration_sec || 30;
    if (currentTime >= maxTime) {
      setCurrentTime(0);
    }
    setIsPlaying(!isPlaying);
  };

  const handleSeek = (e: React.ChangeEvent<HTMLInputElement>) => {
    const val = parseFloat(e.target.value);
    setCurrentTime(val);
  };

  // Экспорт текущей симуляции в анимированный GIF
  const handleExportGif = async () => {
    if (!telemetry || !canvasRef.current || isExportingGif) return;

    const originalTime = currentTime;
    const wasPlaying = isPlaying;
    setIsPlaying(false);
    setIsExportingGif(true);
    setExportProgress(0);

    try {
      const canvas = canvasRef.current;
      const ctx = canvas.getContext('2d');
      if (!ctx) throw new Error('No 2d context');

      const gifWriter = new SimpleGifWriter(canvas.width, canvas.height);
      const totalDuration = telemetry.duration_sec || 20;
      const fps = 8; // 8 кадров в секунду для компактного GIF
      const frameStep = 1 / fps;
      const totalSteps = Math.min(160, Math.floor(totalDuration * fps)); // Ограничение до 160 кадров (20 сек)

      for (let i = 0; i < totalSteps; ++i) {
        const simT = i * frameStep;
        setCurrentTime(simT);

        // Ждем отрисовку канваса
        await new Promise((resolve) => setTimeout(resolve, 20));

        gifWriter.addFrame(ctx, 125); // 125ms delay per frame = 8 FPS
        setExportProgress(Math.round(((i + 1) / totalSteps) * 100));
      }

      const blob = gifWriter.getBlob();
      const filename = `tactical_replay_${telemetry.session_id || 'run'}.gif`;

      // 1. Автоматически сохраняем на диск в data/telemetry/exports_gif/ для Google Drive и очистки
      try {
        const reader = new FileReader();
        reader.onloadend = async () => {
          const resStr = reader.result as string;
          if (resStr && resStr.includes(',')) {
            const base64Data = resStr.split(',')[1];
            await fetch('/api/save-gif', {
              method: 'POST',
              headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify({ filename, base64Data })
            });
          }
        };
        reader.readAsDataURL(blob);
      } catch (saveErr) {
        console.warn('Не удалось автоматически сохранить GIF на сервер:', saveErr);
      }

      // 2. Скачивание в браузере
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = filename;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
    } catch (err) {
      console.error('Ошибка экспорта GIF:', err);
      alert('Не удалось экспортировать GIF: ' + err);
    } finally {
      setIsExportingGif(false);
      setCurrentTime(originalTime);
      if (wasPlaying) setIsPlaying(true);
    }
  };

  const maxDuration = telemetry?.duration_sec || (telemetry?.frames && telemetry.frames.length > 0 ? telemetry.frames[telemetry.frames.length - 1].t : 30);

  // Метрики левел-дизайна
  const coversCount = telemetry?.level_layout.covers?.length || 0;
  const coverMetrics = telemetry?.summary.cover_metrics || {};
  let utilizedCovers = 0;
  let deadCovers = 0;
  for (const c of telemetry?.level_layout.covers || []) {
    const met = coverMetrics[c.id] || c.usage_summary;
    if (met && met.time_sec > 0.1) utilizedCovers++;
    else deadCovers++;
  }
  const coverEfficiencyPct = coversCount > 0 ? Math.round((utilizedCovers / coversCount) * 100) : 0;

  const chokePoints = telemetry?.summary.choke_points_detected || [];
  const heightMetrics = telemetry?.summary.height_metrics;

  return (
    <div className="space-y-6">
      {/* 1. Верхняя панель: выбор забега и сводка */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-lg flex flex-wrap items-center justify-between gap-4">
        <div className="flex items-center gap-3">
          <Film className="w-6 h-6 text-emerald-400" />
          <div>
            <div className="flex items-center gap-2">
              <h2 className="text-lg font-bold text-white tracking-wide">2D Тактический Плеер Левел-Дизайна</h2>
              <span className="text-[11px] text-emerald-400/90 bg-emerald-950/70 border border-emerald-800/60 px-2 py-0.5 rounded-full inline-flex items-center gap-1">
                <Info className="w-3 h-3" />
                Забег = записанный лог симуляции боя
              </span>
            </div>
            <p className="text-xs text-slate-400">Пространственный анализ плотности укрытий, заторов и высот</p>
          </div>
        </div>

        <div className="flex items-center flex-wrap gap-2">
          <label className="text-xs font-semibold text-slate-400 uppercase tracking-wider">Забег:</label>
          <select
            value={selectedFile}
            onChange={(e) => setSelectedFile(e.target.value)}
            className="bg-slate-800 border border-slate-700 text-slate-200 text-xs rounded-lg px-3 py-2 outline-none focus:border-emerald-500 max-w-xs"
          >
            {runsList.length === 0 ? (
              <option value="">Нет записанных забегов</option>
            ) : (
              runsList.map((r) => (
                <option key={r.fileName} value={r.fileName}>
                  {r.sessionId} — {r.result} (Волн: {r.wavesCleared}, {r.durationSec.toFixed(1)}с)
                </option>
              ))
            )}
          </select>
          <button
            onClick={loadRunsList}
            className="px-2.5 py-2 bg-slate-800 hover:bg-slate-700 text-slate-300 text-xs rounded-lg border border-slate-700 flex items-center gap-1"
            title="Обновить список забегов с диска"
          >
            <RefreshCw className="w-3.5 h-3.5" />
            <span>Обновить</span>
          </button>
          <button
            onClick={handleDeleteCurrentRun}
            disabled={!selectedFile}
            className="px-2.5 py-2 bg-rose-950/60 hover:bg-rose-900 text-rose-300 disabled:opacity-40 disabled:hover:bg-rose-950/60 text-xs rounded-lg border border-rose-800/70 flex items-center gap-1 transition-colors"
            title="Удалить выбранный забег и связанный с ним GIF"
          >
            <Trash2 className="w-3.5 h-3.5" />
            <span>Удалить</span>
          </button>
          <button
            onClick={handleClearAllRuns}
            disabled={runsList.length === 0}
            className="px-2.5 py-2 bg-rose-900/80 hover:bg-rose-800 text-white disabled:opacity-40 disabled:hover:bg-rose-900/80 text-xs rounded-lg border border-rose-700 flex items-center gap-1 transition-colors font-medium shadow-sm"
            title="Очистить все файлы забегов и все сохраненные GIF с диска"
          >
            <Trash2 className="w-3.5 h-3.5" />
            <span>Очистить все</span>
          </button>
        </div>
      </div>

      {loading ? (
        <div className="p-12 text-center text-slate-400 bg-slate-900/50 rounded-xl border border-slate-800">
          Загрузка пространственной телеметрии...
        </div>
      ) : !telemetry ? (
        <div className="p-12 text-center text-slate-400 bg-slate-900/50 rounded-xl border border-slate-800 space-y-3">
          <Film className="w-10 h-10 text-slate-600 mx-auto" />
          <p className="font-semibold text-slate-200">Список забегов пуст или ни один забег не выбран</p>
          <p className="text-xs text-slate-400 max-w-lg mx-auto">
            Каждый раз при запуске тестирования или автономных симуляций ботов генерируется файл телеметрии в{' '}
            <code className="text-emerald-400 bg-slate-800 px-1.5 py-0.5 rounded font-mono">data/telemetry/spatial_runs/</code>.
            Запустите <code className="text-emerald-400 bg-slate-800 px-1.5 py-0.5 rounded font-mono">run_simulations.bat</code> или модульный тест{' '}
            <code className="text-emerald-400 bg-slate-800 px-1.5 py-0.5 rounded font-mono">test_spatial_telemetry.gd</code>, чтобы записать новый забег.
          </p>
        </div>
      ) : (
        <div className="grid grid-cols-1 lg:grid-cols-4 gap-6">
          {/* Левая и центральная колонка: Канвас и контролы */}
          <div className="lg:col-span-3 space-y-4">
            <ReplayCanvas
              telemetry={telemetry}
              currentTime={currentTime}
              showCoversOverlay={showCoversOverlay}
              showChokeOverlay={showChokeOverlay}
              showElevationOverlay={showElevationOverlay}
              showEventsOverlay={showEventsOverlay}
              canvasRef={canvasRef}
            />

            {/* Панель управления воспроизведением */}
            <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 shadow-md space-y-3">
              <div className="flex items-center gap-4">
                <button
                  onClick={togglePlay}
                  className="p-2.5 rounded-lg bg-emerald-600 hover:bg-emerald-500 text-white font-semibold flex items-center justify-center shadow-md transition-colors"
                  title={isPlaying ? 'Пауза' : 'Воспроизведение'}
                >
                  {isPlaying ? <Pause className="w-5 h-5" /> : <Play className="w-5 h-5 ml-0.5" />}
                </button>

                <button
                  onClick={() => setCurrentTime(0)}
                  className="p-2 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-300 border border-slate-700"
                  title="В начало"
                >
                  <RotateCcw className="w-4 h-4" />
                </button>

                <div className="flex-1 flex items-center gap-3">
                  <input
                    type="range"
                    min={0}
                    max={maxDuration}
                    step={0.05}
                    value={currentTime}
                    onChange={handleSeek}
                    className="w-full accent-emerald-500 h-2 bg-slate-800 rounded-lg cursor-pointer"
                  />
                  <span className="text-xs font-mono text-slate-300 whitespace-nowrap min-w-[70px]">
                    {currentTime.toFixed(1)}s / {maxDuration.toFixed(1)}s
                  </span>
                </div>

                {/* Скорость */}
                <div className="flex items-center gap-1 bg-slate-800 rounded-lg p-1 border border-slate-700">
                  {[0.5, 1.0, 2.0, 5.0].map((s) => (
                    <button
                      key={s}
                      onClick={() => setPlaybackSpeed(s)}
                      className={`px-2 py-1 text-[11px] font-bold rounded ${
                        playbackSpeed === s ? 'bg-emerald-600 text-white' : 'text-slate-400 hover:text-white'
                      }`}
                    >
                      {s}x
                    </button>
                  ))}
                </div>

                {/* Экспорт GIF */}
                <button
                  onClick={handleExportGif}
                  disabled={isExportingGif}
                  className={`px-3 py-2 rounded-lg flex items-center gap-2 text-xs font-semibold shadow-md transition-colors ${
                    isExportingGif
                      ? 'bg-slate-800 text-slate-500 cursor-not-allowed'
                      : 'bg-indigo-600 hover:bg-indigo-500 text-white'
                  }`}
                  title="Создать анимированный GIF симуляции для демонстрации"
                >
                  <Download className="w-4 h-4" />
                  {isExportingGif ? `Экспорт... ${exportProgress}%` : 'Экспорт GIF'}
                </button>
              </div>

              {/* Переключатели слоев */}
              <div className="flex flex-wrap items-center gap-2 pt-2 border-t border-slate-800 text-xs">
                <span className="text-slate-400 font-semibold mr-1">Слои карты:</span>

                <button
                  onClick={() => setShowCoversOverlay(!showCoversOverlay)}
                  className={`px-2.5 py-1 rounded-md border flex items-center gap-1.5 transition-colors ${
                    showCoversOverlay
                      ? 'bg-emerald-950/60 border-emerald-500/80 text-emerald-300'
                      : 'bg-slate-800/60 border-slate-700 text-slate-400 hover:text-slate-200'
                  }`}
                >
                  <Shield className="w-3.5 h-3.5" />
                  #1 Плотность укрытий
                </button>

                <button
                  onClick={() => setShowChokeOverlay(!showChokeOverlay)}
                  className={`px-2.5 py-1 rounded-md border flex items-center gap-1.5 transition-colors ${
                    showChokeOverlay
                      ? 'bg-purple-950/60 border-purple-500/80 text-purple-300'
                      : 'bg-slate-800/60 border-slate-700 text-slate-400 hover:text-slate-200'
                  }`}
                >
                  <AlertTriangle className="w-3.5 h-3.5 text-purple-400" />
                  #2 Заторы (Choke)
                </button>

                <button
                  onClick={() => setShowElevationOverlay(!showElevationOverlay)}
                  className={`px-2.5 py-1 rounded-md border flex items-center gap-1.5 transition-colors ${
                    showElevationOverlay
                      ? 'bg-sky-950/60 border-sky-500/80 text-sky-300'
                      : 'bg-slate-800/60 border-slate-700 text-slate-400 hover:text-slate-200'
                  }`}
                >
                  <Mountain className="w-3.5 h-3.5" />
                  #3 Высоты (+Y)
                </button>

                <button
                  onClick={() => setShowEventsOverlay(!showEventsOverlay)}
                  className={`px-2.5 py-1 rounded-md border flex items-center gap-1.5 transition-colors ${
                    showEventsOverlay
                      ? 'bg-amber-950/60 border-amber-500/80 text-amber-300'
                      : 'bg-slate-800/60 border-slate-700 text-slate-400 hover:text-slate-200'
                  }`}
                >
                  <Flame className="w-3.5 h-3.5" />
                  События боя
                </button>

                <div className="flex-1" />

                <button
                  onClick={() => setShowLegend(!showLegend)}
                  className={`px-2.5 py-1 rounded-md border flex items-center gap-1.5 transition-colors ${
                    showLegend
                      ? 'bg-indigo-950/60 border-indigo-500/80 text-indigo-300'
                      : 'bg-slate-800/60 border-slate-700 text-slate-400 hover:text-slate-200'
                  }`}
                  title="Показать или скрыть обозначения на тактической карте"
                >
                  <HelpCircle className="w-3.5 h-3.5" />
                  {showLegend ? 'Скрыть легенду' : 'Обозначения карты'}
                </button>
              </div>
            </div>

            {/* Легенда карты с обозначениями всех символов */}
            {showLegend && (
              <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 shadow-md space-y-3">
                <div className="flex items-center justify-between border-b border-slate-800 pb-2">
                  <span className="font-bold text-slate-200 flex items-center gap-1.5 text-xs">
                    <HelpCircle className="w-4 h-4 text-emerald-400" />
                    Легенда тактической карты
                  </span>
                  <span className="text-[10px] text-slate-500 font-mono">Координаты пола в метрах (X, Z)</span>
                </div>
                <div className="grid grid-cols-2 sm:grid-cols-4 gap-3 text-slate-300 text-xs">
                  <div className="space-y-1">
                    <div className="font-semibold text-slate-100 flex items-center gap-1.5">
                      <span className="inline-block w-3 h-3 bg-slate-800 border border-slate-500 rounded-sm"></span>
                      🏢 Здания и кубы (BOX)
                    </div>
                    <p className="text-[11px] text-slate-400">
                      Монолитные препятствия и постройки, которые формируют проходы и направляют врагов.
                    </p>
                  </div>

                  <div className="space-y-1">
                    <div className="font-semibold text-slate-100 flex items-center gap-1.5">
                      <span className="inline-block w-3 h-3 bg-emerald-500 rounded-sm"></span>
                      🛡️ Укрытия (Баррикады)
                    </div>
                    <p className="text-[11px] text-slate-400">
                      <span className="text-emerald-400 font-bold">🟢 Активные</span>, <span className="text-amber-400 font-bold">🟡 Умеренные</span>, <span className="text-rose-400 font-bold">🔴 Разрушенные</span>, <span className="text-slate-400 font-bold">⚪ Забытые</span> (мертвые зоны).
                    </p>
                  </div>

                  <div className="space-y-1">
                    <div className="font-semibold text-slate-100 flex items-center gap-1.5">
                      <span className="inline-block w-3 h-3 bg-purple-500/40 border border-purple-500 rounded-full"></span>
                      🟣 Заторы и узкие места
                    </div>
                    <p className="text-[11px] text-slate-400">
                      Фиолетовые пульсирующие зоны опасных заторов, где скучиваются враги (цели для броска гранат).
                    </p>
                  </div>

                  <div className="space-y-1">
                    <div className="font-semibold text-slate-100 flex items-center gap-1.5">
                      <span className="inline-block w-3 h-3 bg-sky-500/20 border border-sky-400 border-dashed rounded-sm"></span>
                      ▲ Высоты (+Y.Yм)
                    </div>
                    <p className="text-[11px] text-slate-400">
                      Платформы $\ge 1.5$м: +25% к дальности, +15% к урону, прострел над нижними укрытиями.
                    </p>
                  </div>

                  <div className="space-y-1">
                    <div className="font-semibold text-slate-100 flex items-center gap-1.5">
                      <span className="inline-block w-2.5 h-2.5 bg-sky-400 rounded-full"></span>
                      🔵 Бойцы отряда
                    </div>
                    <p className="text-[11px] text-slate-400">
                      Командир и союзники. Стойки: [СИДЯ], [ЛЕЖА]. Зеленая аура — боец в укрытии (-35% урона).
                    </p>
                  </div>

                  <div className="space-y-1 col-span-2 sm:col-span-2">
                    <div className="font-semibold text-slate-100 flex items-center gap-1.5">
                      <span>👾 Противники (Цветовая дифференциация)</span>
                    </div>
                    <div className="grid grid-cols-2 gap-x-4 gap-y-1 text-[11px] text-slate-300 pt-1">
                      <div className="flex items-center gap-1.5">
                        <span className="inline-block w-2.5 h-2.5 bg-white border border-slate-400 rounded-full shrink-0"></span>
                        <span><b>Гончая</b> (белый)</span>
                      </div>
                      <div className="flex items-center gap-1.5">
                        <span className="inline-block w-2.5 h-2.5 bg-blue-500 rounded-full shrink-0"></span>
                        <span><b>Мех-гончая / Cutter</b> (синий)</span>
                      </div>
                      <div className="flex items-center gap-1.5">
                        <span className="inline-block w-2.5 h-2.5 bg-cyan-400 rounded-full shrink-0"></span>
                        <span><b>Промёрзший</b> (бирюзовый)</span>
                      </div>
                      <div className="flex items-center gap-1.5">
                        <span className="inline-block w-2.5 h-2.5 bg-emerald-500 rounded-full shrink-0"></span>
                        <span><b>Брут</b> (зелёный)</span>
                      </div>
                    </div>
                  </div>

                  <div className="space-y-1">
                    <div className="font-semibold text-slate-100 flex items-center gap-1.5">
                      <span className="text-amber-400 font-bold">💥</span>
                      События боя
                    </div>
                    <p className="text-[11px] text-slate-400">
                      Оранжевый круг — радиус поражения гранаты. ✖ / ☠ — гибель противника или бойца.
                    </p>
                  </div>

                  <div className="space-y-1">
                    <div className="font-semibold text-slate-100 flex items-center gap-1.5">
                      <span className="inline-block w-2.5 h-2.5 border border-dashed border-emerald-400 rounded-full"></span>
                      🎯 Рубежи и спавны
                    </div>
                    <p className="text-[11px] text-slate-400">
                      Красные круги — точки спавна волн (SPAWN). Зеленый пунктир — периметр защиты отряда.
                    </p>
                  </div>
                </div>
              </div>
            )}
          </div>

          {/* Правая колонка: Аналитическая панель Левел-Дизайна */}
          <div className="space-y-4">
            {/* Карточка 1: Эффективность расстановки укрытий */}
            <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 shadow-md space-y-3">
              <div className="flex items-center gap-2 text-emerald-400 font-bold text-sm">
                <Shield className="w-4 h-4" />
                Анализ расстановки укрытий
              </div>
              <div className="space-y-2 text-xs">
                <div className="flex justify-between items-center text-slate-300">
                  <span>Всего укрытий на карте:</span>
                  <span className="font-bold text-white">{coversCount}</span>
                </div>
                <div className="flex justify-between items-center text-slate-300">
                  <span>Активно использовано:</span>
                  <span className="font-bold text-emerald-400">{utilizedCovers}</span>
                </div>
                <div className="flex justify-between items-center text-slate-300">
                  <span>Мертвые зоны (0с использования):</span>
                  <span className="font-bold text-rose-400">{deadCovers}</span>
                </div>

                {/* Прогресс-бар эффективности */}
                <div className="pt-1">
                  <div className="flex justify-between text-[11px] mb-1">
                    <span className="text-slate-400">Коэффициент полезности:</span>
                    <span className="font-bold text-emerald-400">{coverEfficiencyPct}%</span>
                  </div>
                  <div className="w-full bg-slate-800 rounded-full h-2 overflow-hidden">
                    <div
                      className={`h-full ${coverEfficiencyPct > 60 ? 'bg-emerald-500' : 'bg-amber-500'}`}
                      style={{ width: `${coverEfficiencyPct}%` }}
                    />
                  </div>
                </div>
              </div>
            </div>

            {/* Карточка 2: Обнаруженные узкие места (Choke Points) */}
            <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 shadow-md space-y-3">
              <div className="flex items-center gap-2 text-rose-400 font-bold text-sm">
                <AlertTriangle className="w-4 h-4" />
                Узкие места и заторы
              </div>
              {chokePoints.length === 0 ? (
                <div className="text-xs text-slate-400 py-2">
                  Критических заторов не зафиксировано. Потоки врагов распределяются равномерно.
                </div>
              ) : (
                <div className="space-y-2 max-h-48 overflow-y-auto pr-1">
                  {chokePoints.map((cp, idx) => (
                    <div
                      key={idx}
                      className="p-2 bg-slate-800/80 rounded-lg border border-slate-700 text-xs flex items-center justify-between"
                    >
                      <div>
                        <div className="font-semibold text-rose-300">
                          Затор #{idx + 1} ({cp.enemy_count_peak} врагов в точке)
                        </div>
                        <div className="text-[10px] text-slate-400 font-mono">
                          X: {cp.position[0].toFixed(1)}м, Z: {cp.position[1].toFixed(1)}м
                        </div>
                      </div>
                      <span className="px-2 py-0.5 rounded bg-rose-950 text-rose-300 text-[10px] font-bold">
                        Score: {cp.congestion_score}
                      </span>
                    </div>
                  ))}
                </div>
              )}
            </div>

            {/* Карточка 3: Анализ высот */}
            <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 shadow-md space-y-3">
              <div className="flex items-center gap-2 text-sky-400 font-bold text-sm">
                <Mountain className="w-4 h-4" />
                Использование высот (+Y)
              </div>
              <div className="space-y-2 text-xs">
                <div className="flex justify-between items-center text-slate-300">
                  <span>Время удержания высоты:</span>
                  <span className="font-bold text-sky-300">
                    {heightMetrics?.elevation_usage_time_sec?.toFixed(1) || 0}s
                  </span>
                </div>
                <div className="flex justify-between items-center text-slate-300">
                  <span>Уничтожено врагов сверху:</span>
                  <span className="font-bold text-sky-300">
                    {heightMetrics?.kills_from_height || 0}
                  </span>
                </div>
                <p className="text-[11px] text-slate-400 pt-1 leading-relaxed">
                  Позиции выше 1.5м дают +25% к дальности, +15% к урону и игнорируют нижние укрытия цели.
                </p>
              </div>
            </div>

            {/* Карточка 4: Результат забега */}
            <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 shadow-md space-y-2 text-xs">
              <div className="flex items-center justify-between">
                <span className="text-slate-400 font-semibold">Итог симуляции:</span>
                <span
                  className={`font-bold flex items-center gap-1 ${
                    telemetry.summary.result === 'VICTORY' ? 'text-emerald-400' : 'text-rose-400'
                  }`}
                >
                  {telemetry.summary.result === 'VICTORY' ? (
                    <>
                      <CheckCircle2 className="w-3.5 h-3.5" /> ПОБЕДА
                    </>
                  ) : (
                    <>
                      <XCircle className="w-3.5 h-3.5" /> ПОРАЖЕНИЕ
                    </>
                  )}
                </span>
              </div>
              <div className="flex justify-between text-slate-300">
                <span>Волн пройдено:</span>
                <span className="font-bold text-white">
                  {telemetry.summary.waves_cleared} / {telemetry.summary.total_waves}
                </span>
              </div>
              <div className="flex justify-between text-slate-300">
                <span>Профиль тестировщика:</span>
                <span className="font-bold text-white">{telemetry.tester_profile}</span>
              </div>
              <div className="flex justify-between text-slate-300">
                <span>Записано кадров:</span>
                <span className="font-bold text-white">{telemetry.frames?.length || 0}</span>
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};
