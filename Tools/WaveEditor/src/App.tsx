import React, { useState, useEffect } from 'react';
import { LevelConfig, WaveConfig, BotPreset, MemberColdTelemetry } from './types';
import { DifficultyCurveChart } from './components/DifficultyCurveChart';
import { EnemyCompositionBarChart } from './components/EnemyCompositionBarChart';
import { GlobalCurveControls } from './components/GlobalCurveControls';
import { WaveEditorCard } from './components/WaveEditorCard';
import { WaveToggleGrid } from './components/WaveToggleGrid';
import { SquadLoadoutControls } from './components/SquadLoadoutControls';
import { TelemetryAnalytics } from './components/TelemetryAnalytics';
import { BotStatusBar } from './components/BotStatusBar';
import { LiveBalanceAdvisor } from './components/LiveBalanceAdvisor';
import { Activity, Terminal, Loader2, Swords, Plus, Snowflake, X, Wifi, WifiOff } from 'lucide-react';
import { Save, Upload, RotateCcw, Layers, CheckCircle2, FileCode2 } from 'lucide-react';

const DEFAULT_LEVEL: LevelConfig = {
  $schema: "../../schemas/level_config.schema.json",
  level_id: "outpost_gate_01",
  level_name: "Карантинный КПП: Рубеж ворот",
  description: "Первый рубеж обороны перед комплексом с генератором. 3 волны ледяных тварей штурмуют гермоворота.",
  prep_phase_duration: 45.0,
  wave_rest_duration: 15.0,
  squad_loadout: {
    simulation_mode: 'EXPLORE_AND_COLLECT',
    preset_tier: 'STANDARD',
    turrets_count: 1,
    barricades_count: 2,
    mines_count: 2,
    medkits_count: 2,
    m16_ammo: 120,
    pistol_ammo: 48,
    canned_food: 4,
    matches: 3,
  },
  waves: [
    {
      wave_index: 1,
      max_simultaneous_enemies: 6,
      spawns: [
        { enemy_type: "HOUND", count: 3, spawn_lane: "WEST_FLANK", spawn_delay_sec: 1.2, initial_delay_sec: 0.0 },
        { enemy_type: "SPITTER", count: 1, spawn_lane: "EAST_FLANK", spawn_delay_sec: 1.5, initial_delay_sec: 2.0 }
      ],
      wave_modifiers: { enemy_hp_mult: 1.0, enemy_damage_mult: 1.0, enemy_speed_mult: 1.0, cold_drain_mult: 1.0 }
    },
    {
      wave_index: 2,
      max_simultaneous_enemies: 8,
      spawns: [
        { enemy_type: "HOUND", count: 4, spawn_lane: "WEST_FLANK", spawn_delay_sec: 1.0, initial_delay_sec: 0.0 },
        { enemy_type: "SPITTER", count: 2, spawn_lane: "EAST_FLANK", spawn_delay_sec: 1.2, initial_delay_sec: 1.5 }
      ],
      wave_modifiers: { enemy_hp_mult: 1.05, enemy_damage_mult: 1.0, enemy_speed_mult: 1.0, cold_drain_mult: 1.0 }
    },
    {
      wave_index: 3,
      max_simultaneous_enemies: 10,
      spawns: [
        { enemy_type: "HOUND", count: 4, spawn_lane: "ANY", spawn_delay_sec: 0.9, initial_delay_sec: 0.0 },
        { enemy_type: "SPITTER", count: 2, spawn_lane: "EAST_FLANK", spawn_delay_sec: 1.2, initial_delay_sec: 2.0 },
        { enemy_type: "BRUTE", count: 1, spawn_lane: "NORTH_GATE", spawn_delay_sec: 2.0, initial_delay_sec: 3.0 }
      ],
      wave_modifiers: { enemy_hp_mult: 1.1, enemy_damage_mult: 1.05, enemy_speed_mult: 1.0, cold_drain_mult: 1.05 }
    }
  ]
};

export const App: React.FC = () => {
  const [config, setConfig] = useState<LevelConfig>(() => {
    const saved = localStorage.getItem('codex_level_config_v4');
    if (saved) {
      try {
        return JSON.parse(saved);
      } catch (e) {
        console.error('Failed to parse saved config', e);
      }
    }
    return DEFAULT_LEVEL;
  });

  const [activeStageId, setActiveStageId] = useState<string>('stage_01');

  const handleToggleWaveActive = (waveIndex: number, active: boolean) => {
    const existing = config.waves.find((w) => w.wave_index === waveIndex);
    let newWaves: WaveConfig[];

    if (existing) {
      newWaves = config.waves.map((w) =>
        w.wave_index === waveIndex ? { ...w, is_active: active } : w
      );
    } else {
      // Создаем новую запись для этого слота при включении
      const newWave: WaveConfig = {
        wave_index: waveIndex,
        is_active: active,
        max_simultaneous_enemies: Math.min(20, 6 + waveIndex * 2),
        spawns: [
          {
            enemy_type: 'HOUND',
            count: 2 + Math.floor(waveIndex * 0.7),
            spawn_lane: 'WEST_FLANK',
            spawn_delay_sec: 1.0,
            initial_delay_sec: 0.0,
          },
          {
            enemy_type: 'SPITTER',
            count: 1 + Math.floor(waveIndex * 0.4),
            spawn_lane: 'EAST_FLANK',
            spawn_delay_sec: 1.2,
            initial_delay_sec: 1.5,
          },
        ],
        wave_modifiers: {
          enemy_hp_mult: Number((1.0 + (waveIndex - 1) * 0.05).toFixed(2)),
          enemy_damage_mult: Number((1.0 + (waveIndex - 1) * 0.04).toFixed(2)),
          enemy_speed_mult: 1.0,
          cold_drain_mult: config.waves[0]?.wave_modifiers?.cold_drain_mult ?? 1.0,
        },
      };
      newWaves = [...config.waves, newWave].sort((a, b) => a.wave_index - b.wave_index);
    }

    setConfig({ ...config, waves: newWaves });
    if (active) {
      setSelectedWaveIndex(waveIndex);
    }
  };

  const handleSelectStage = async (stageId: string) => {
    setActiveStageId(stageId);
    try {
      const res = await fetch(`/api/get-stage?id=${stageId}`);
      if (res.ok) {
        const data = await res.json();
        setConfig(data);
        setSelectedWaveIndex(1);
        setSaveStatus(`Загружен ${data.level_name || stageId}`);
        setTimeout(() => setSaveStatus(null), 3500);
      }
    } catch (e) {
      console.error('Failed to load stage:', e);
    }
  };

  const [selectedWaveIndex, setSelectedWaveIndex] = useState<number>(1);
  const [activeTab, setActiveTab] = useState<'waves' | 'telemetry'>('waves');
  const [saveStatus, setSaveStatus] = useState<string | null>(null);
  const [isServerConnected, setIsServerConnected] = useState<boolean | null>(null);

  // Проверка связи с локальным dev-сервером игры
  const checkConnection = async () => {
    try {
      const res = await fetch('/api/health');
      if (res.ok) {
        const data = await res.json();
        if (data.status === 'ok') {
          setIsServerConnected(true);
          return true;
        }
      }
      setIsServerConnected(false);
      return false;
    } catch {
      setIsServerConnected(false);
      return false;
    }
  };

  useEffect(() => {
    checkConnection();
    const interval = setInterval(checkConnection, 4000);
    return () => clearInterval(interval);
  }, []);

  // При запуске всегда загружаем актуальный конфиг с диска из папки игры
  useEffect(() => {
    fetch('/api/get-config')
      .then(res => {
        if (res.ok) {
          setIsServerConnected(true);
          return res.json();
        }
        return null;
      })
      .then(data => {
        if (data && Array.isArray(data.waves) && data.waves.length > 0) {
          setConfig(data);
          setSelectedWaveIndex(1);
        }
      })
      .catch(() => {
        setIsServerConnected(false);
      });
  }, []);

  useEffect(() => {
    localStorage.setItem('codex_level_config_v4', JSON.stringify(config));
  }, [config]);

  let activeWave = config.waves.find((w) => w.wave_index === selectedWaveIndex);
  if (!activeWave) {
    activeWave = {
      wave_index: selectedWaveIndex,
      is_active: false,
      max_simultaneous_enemies: 8,
      spawns: [
        { enemy_type: 'HOUND', count: 3, spawn_lane: 'WEST_FLANK', spawn_delay_sec: 1.0, initial_delay_sec: 0.0 }
      ],
      wave_modifiers: {
        enemy_hp_mult: 1.0,
        enemy_damage_mult: 1.0,
        enemy_speed_mult: 1.0,
        cold_drain_mult: 1.0
      }
    };
  }

  const handleUpdateWave = (updatedWave: WaveConfig) => {
    const newWaves = config.waves.map((w) => (w.wave_index === updatedWave.wave_index ? updatedWave : w));
    setConfig({ ...config, waves: newWaves });
  };

  const handleDeleteWave = (waveIndex: number) => {
    if (config.waves.length <= 1) return;
    const remaining = config.waves.filter((w) => w.wave_index !== waveIndex);
    const reindexed = remaining.map((w, idx) => ({ ...w, wave_index: idx + 1 }));
    setConfig({ ...config, waves: reindexed });
    setSelectedWaveIndex(Math.max(1, Math.min(reindexed.length, waveIndex)));
  };

  const handleDuplicateWave = (waveIndex: number) => {
    const target = config.waves.find((w) => w.wave_index === waveIndex);
    if (!target) return;
    const newWave: WaveConfig = JSON.parse(JSON.stringify(target));
    const newWaves = [...config.waves];
    newWaves.splice(waveIndex, 0, newWave);
    const reindexed = newWaves.map((w, idx) => ({ ...w, wave_index: idx + 1 }));
    setConfig({ ...config, waves: reindexed });
    setSelectedWaveIndex(waveIndex + 1);
  };

  const handleApplyColdToAll = (cold: number) => {
    const newWaves = config.waves.map((w) => ({
      ...w,
      wave_modifiers: {
        ...w.wave_modifiers,
        cold_drain_mult: cold
      }
    }));
    setConfig({ ...config, waves: newWaves });
    setSaveStatus(`❄️ Сила холода ${cold.toFixed(1)}x применена ко всем ${newWaves.length} волнам!`);
    setTimeout(() => setSaveStatus(null), 3000);
  };


  const [isBotRunning, setIsBotRunning] = useState(false);
  const [botPresets, setBotPresets] = useState<BotPreset[]>([
    { id: 'short_check', name: 'Короткая проверка', description: '1 забег, NORMAL, 5.0x', runs: 1, profile: 'NORMAL', speed: 5.0 },
    { id: 'fast_test', name: 'Быстрый тест', description: '5 забегов, NORMAL, 8.0x', runs: 5, profile: 'NORMAL', speed: 8.0 },
    { id: 'standard_eval', name: 'Стандартный прогон', description: '20 забегов, NORMAL, 8.0x', runs: 20, profile: 'NORMAL', speed: 8.0 },
    { id: 'veteran_stress', name: 'Стресс-тест ветерана', description: '50 забегов, VETERAN, 10.0x', runs: 50, profile: 'VETERAN', speed: 10.0 }
  ]);
  const [selectedPresetId, setSelectedPresetId] = useState<string>('short_check');
  const [isCustomPresetModalOpen, setIsCustomPresetModalOpen] = useState(false);
  const [newPresetForm, setNewPresetForm] = useState<{
    name: string;
    runs: number;
    profile: 'CASUAL' | 'NORMAL' | 'VETERAN';
    speed: number;
  }>({
    name: 'Мой буран-тест',
    runs: 1,
    profile: 'NORMAL',
    speed: 5.0
  });

  // Метрики холода последнего боя для отображения прямо в конфигураторе волн
  const [latestColdTelemetry, setLatestColdTelemetry] = useState<MemberColdTelemetry[]>([]);

  useEffect(() => {
    fetch('/api/get-bot-presets')
      .then(res => res.ok ? res.json() : null)
      .then(data => {
        if (Array.isArray(data) && data.length > 0) {
          setBotPresets(data);
        }
      })
      .catch(() => {});
  }, []);

  useEffect(() => {
    const fetchLatestCold = () => {
      fetch('/api/get-telemetry')
        .then(res => res.ok ? res.text() : '')
        .then(text => {
          if (!text) return;
          const lines = text.trim().split('\n');
          for (let i = lines.length - 1; i >= 0; i--) {
            try {
              const run = JSON.parse(lines[i]);
              if (run.squad_cold_stats && Array.isArray(run.squad_cold_stats) && run.squad_cold_stats.length > 0) {
                setLatestColdTelemetry(run.squad_cold_stats);
                break;
              }
            } catch (e) {}
          }
        })
        .catch(() => {});
    };
    fetchLatestCold();
    const interval = setInterval(fetchLatestCold, 3500);
    return () => clearInterval(interval);
  }, []);

  const handleCreatePreset = async () => {
    const newPreset: BotPreset = {
      id: `preset_${Date.now()}`,
      name: newPresetForm.name || 'Пользовательский пресет',
      description: `${newPresetForm.runs} забегов, ${newPresetForm.profile}, ${newPresetForm.speed}x`,
      runs: Number(newPresetForm.runs) || 1,
      profile: newPresetForm.profile,
      speed: Number(newPresetForm.speed) || 5.0
    };

    try {
      const res = await fetch('/api/save-bot-preset', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(newPreset)
      });
      if (res.ok) {
        setBotPresets(prev => [...prev, newPreset]);
        setSelectedPresetId(newPreset.id);
        setIsCustomPresetModalOpen(false);
        setSaveStatus(`Пресет "${newPreset.name}" сохранён!`);
        setTimeout(() => setSaveStatus(null), 3500);
      }
    } catch (e) {
      setBotPresets(prev => [...prev, newPreset]);
      setSelectedPresetId(newPreset.id);
      setIsCustomPresetModalOpen(false);
    }
  };

  const handleRunBot = async (presetOverride?: BotPreset) => {
    const preset = presetOverride || botPresets.find(p => p.id === selectedPresetId) || botPresets[0];
    setIsBotRunning(true);
    setSaveStatus(`🚀 Запуск пресета: "${preset.name}" (${preset.runs} забег., ${preset.profile}, ${preset.speed}x)...`);
    try {
      const res = await fetch('/api/run-bot', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          config,
          runs: preset.runs,
          profile: preset.profile,
          speed: preset.speed
        }),
      });
      const data = await res.json();
      if (data.success) {
        setSaveStatus(`🖥️ ${data.message || 'Окно симулятора открыто!'}`);
        setTimeout(() => setSaveStatus(null), 6000);
      } else {
        setSaveStatus(`❌ Ошибка: ${data.error}`);
        setTimeout(() => setSaveStatus(null), 4000);
      }
    } catch (e: any) {
      setSaveStatus('❌ Не удалось связаться с dev-сервером');
      setTimeout(() => setSaveStatus(null), 4000);
    } finally {
      setTimeout(() => setIsBotRunning(false), 2500);
    }
  };

  const handleSaveConfig = async () => {
    try {
      const res = await fetch('/api/save-stage', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(config, null, 2),
      });
      if (res.ok) {
        setIsServerConnected(true);
        const stageName = config.level_name || config.level_id || 'stage_01';
        setSaveStatus(`✅ Сохранено в игру: ${stageName} (Content/Data/LevelJson/)!`);
        setTimeout(() => setSaveStatus(null), 4500);
        return;
      }
    } catch (e) {
      console.warn('Direct save unavailable, dev server offline:', e);
      setIsServerConnected(false);
    }

    // Если dev-сервер не запущен (например, открыт статический файл или окно Vite было закрыто):
    // 1. Предлагаем прямой выбор файла через File System Access API (работает в Chrome, Edge, Opera)
    if ('showSaveFilePicker' in window) {
      try {
        const handle = await (window as any).showSaveFilePicker({
          suggestedName: `${config.level_id || 'stage_01'}.json`,
          types: [{
            description: 'JSON Level Config (выберите файл в CodexTactics/Content/Data/LevelJson/)',
            accept: { 'application/json': ['.json'] },
          }],
        });
        const writable = await handle.createWritable();
        await writable.write(JSON.stringify(config, null, 2));
        await writable.close();
        setSaveStatus(`💾 Сохранено напрямую в выбранный файл!`);
        setTimeout(() => setSaveStatus(null), 4500);
        return;
      } catch (err: any) {
        if (err.name === 'AbortError') {
          return;
        }
      }
    }

    // 2. Если системный диалог недоступен или отклонен:
    alert(
      '⚠️ Dev-сервер редактора не подключен!\n\n' +
      'Чтобы изменения применялись напрямую в игру:\n' +
      '1. Запустите "run_wave_editor.bat" в корне проекта.\n' +
      '2. Страница автоматически подключится к файловой системе игры.\n\n' +
      'Сейчас файл будет скачан в "Загрузки" (Downloads). Чтобы игра его увидела, переместите его вручную в:\n' +
      'CodexTactics/Content/Data/LevelJson/'
    );
    handleDownloadJson();
  };

  const handleDownloadJson = () => {
    const jsonStr = JSON.stringify(config, null, 2);
    const blob = new Blob([jsonStr], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `${config.level_id || 'level_config'}.json`;
    a.click();
    URL.revokeObjectURL(url);
    setSaveStatus('Конфиг скачан в JSON!');
    setTimeout(() => setSaveStatus(null), 3500);
  };

  const handleUploadJson = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (event) => {
      try {
        const parsed = JSON.parse(event.target?.result as string);
        if (parsed.waves && Array.isArray(parsed.waves)) {
          setConfig(parsed);
          setSelectedWaveIndex(1);
          setSaveStatus('Конфиг успешно загружен!');
          setTimeout(() => setSaveStatus(null), 3500);
        } else {
          alert('Файл не соответствует формату LevelConfig (отсутствует массив waves)');
        }
      } catch (err) {
        alert('Ошибка при чтении JSON-файла');
      }
    };
    reader.readAsText(file);
    e.target.value = '';
  };

  return (
    <div className="min-h-screen bg-slate-950 flex flex-col">
      <header className="bg-slate-900 border-b border-slate-800 px-6 py-4 flex flex-wrap items-center justify-between gap-4 sticky top-0 z-50 shadow-md">
        <div className="flex items-center gap-3">
          <div className="w-10 h-10 rounded-xl bg-sky-500/20 border border-sky-500/30 flex items-center justify-center text-sky-400 font-black text-xl">
            ⚡
          </div>
          <div>
            <div className="flex items-center gap-2">
              <h1 className="text-lg font-black text-slate-100 tracking-wide">CODEX TACTICS</h1>
              <span className="text-[10px] font-mono uppercase bg-sky-500/20 text-sky-300 border border-sky-500/30 px-2 py-0.5 rounded">
                Wave Shaper Editor
              </span>
            </div>
            <div className="text-xs text-slate-400">
              Визуальный редактор волн, таймингов и математического баланса
            </div>
          </div>

          {/* 12 Боев Селектор */}
          <div className="flex items-center gap-2 bg-slate-950 px-3 py-1.5 rounded-xl border border-slate-800 shadow-inner ml-2">
            <Swords className="w-4 h-4 text-cyan-400" />
            <select
              value={activeStageId}
              onChange={(e) => handleSelectStage(e.target.value)}
              className="bg-transparent text-slate-200 text-xs font-bold outline-none cursor-pointer pr-2"
            >
              <optgroup label="АКТ 1: Бункер / Индустриальная зона">
                <option value="stage_01" className="bg-slate-900 text-slate-100">Бой 1: Бой у КПП (3 волны)</option>
                <option value="stage_02" className="bg-slate-900 text-slate-100">Бой 2: Складской терминал (2 волны)</option>
                <option value="stage_03" className="bg-slate-900 text-slate-100">Бой 3: Вентшахта бункера (3 волны)</option>
                <option value="stage_04" className="bg-slate-900 text-slate-100">Бой 4: Дизельный узел [Мини-босс] (3 волны)</option>
              </optgroup>
              <optgroup label="АКТ 2: Катакомбы / Заброшенный комплекс">
                <option value="stage_05" className="bg-slate-900 text-slate-100">Бой 5: Затопленный тоннель (3 волны)</option>
                <option value="stage_06" className="bg-slate-900 text-slate-100">Бой 6: Технический коллектор (3 волны)</option>
                <option value="stage_07" className="bg-slate-900 text-slate-100">Бой 7: Руины архива (4 волны)</option>
                <option value="stage_08" className="bg-slate-900 text-slate-100">Бой 8: Насосная станция (4 волны)</option>
                <option value="stage_09" className="bg-slate-900 text-slate-100">Бой 9: Гермошлюз Цитадели (4 волны)</option>
              </optgroup>
              <optgroup label="АКТ 3: Цитадель / Ядро">
                <option value="stage_10" className="bg-slate-900 text-slate-100">Бой 10: Верхний мост лаборатории (4 волны)</option>
                <option value="stage_11" className="bg-slate-900 text-slate-100">Бой 11: Криокамера реактора (4 волны)</option>
                <option value="stage_12" className="bg-slate-900 text-slate-100">Бой 12: Сердцевина Ядра [Финальный босс] (4 волны)</option>
              </optgroup>
            </select>
          </div>

          {/* Статус связи с локальным сервером проекта */}
          <div className="flex items-center">
            {isServerConnected === true && (
              <div 
                className="flex items-center gap-1.5 px-2.5 py-1 rounded-lg bg-emerald-950/60 border border-emerald-500/40 text-[11px] font-bold text-emerald-400 shadow-sm"
                title="Dev-сервер активен. Файлы сохраняются прямо в CodexTactics/Content/Data/LevelJson/"
              >
                <Wifi className="w-3.5 h-3.5 text-emerald-400 animate-pulse" />
                <span className="hidden sm:inline">Связь с игрой:</span> Прямой автосейв
              </div>
            )}
            {isServerConnected === false && (
              <div 
                className="flex items-center gap-1.5 px-2.5 py-1 rounded-lg bg-amber-950/60 border border-amber-500/40 text-[11px] font-bold text-amber-300 shadow-sm cursor-help"
                title="Dev-сервер не запущен. Для автоматической записи прямо в игру запустите run_wave_editor.bat в корне проекта"
              >
                <WifiOff className="w-3.5 h-3.5 text-amber-400" />
                <span className="hidden sm:inline">Офлайн:</span> Запустите run_wave_editor.bat
              </div>
            )}
            {isServerConnected === null && (
              <div className="flex items-center gap-1.5 px-2.5 py-1 rounded-lg bg-slate-800 text-[11px] text-slate-400">
                <Loader2 className="w-3.5 h-3.5 animate-spin" />
                Проверка связи...
              </div>
            )}
          </div>
        </div>

        <div className="flex items-center gap-2.5">
          {saveStatus && (
            <span className="text-xs text-emerald-400 bg-emerald-950/60 border border-emerald-800 px-2.5 py-1 rounded flex items-center gap-1.5 animate-fade-in">
              <CheckCircle2 className="w-3.5 h-3.5" />
              {saveStatus}
            </span>
          )}

          <label className="cursor-pointer px-3 py-2 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-200 text-xs font-semibold flex items-center gap-1.5 transition-all border border-slate-700 shadow-sm">
            <Upload className="w-3.5 h-3.5 text-slate-400" />
            Загрузить JSON
            <input type="file" accept=".json" onChange={handleUploadJson} className="hidden" />
          </label>

          <button
            onClick={handleDownloadJson}
            className="px-3 py-2 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-300 text-xs font-semibold flex items-center gap-1.5 transition-all border border-slate-700"
            title="Скачать локальную копию JSON файла"
          >
            <FileCode2 className="w-3.5 h-3.5 text-slate-400" />
            Скачать копию
          </button>

          <button
            onClick={handleSaveConfig}
            className="px-4 py-2 rounded-lg bg-emerald-500 hover:bg-emerald-400 text-slate-950 text-xs font-black flex items-center gap-1.5 transition-all shadow-md hover:shadow-emerald-500/20"
            title="Мгновенно сохранить и применить изменения в игру для запуска бота"
          >
            <Save className="w-4 h-4" />
            Применить в игру
          </button>

          {/* Селектор пресета симуляции */}
          <div className="flex items-center gap-1.5 bg-slate-950 px-2.5 py-1.5 rounded-lg border border-slate-800 shadow-inner">
            <span className="text-[11px] text-slate-400 font-semibold hidden lg:inline">Пресет:</span>
            <select
              value={selectedPresetId}
              onChange={(e) => setSelectedPresetId(e.target.value)}
              className="bg-transparent text-slate-200 text-xs font-bold outline-none cursor-pointer max-w-[200px]"
              title="Выберите пресет тестирования"
            >
              {botPresets.map((p) => (
                <option key={p.id} value={p.id} className="bg-slate-900 text-slate-100">
                  {p.name} ({p.runs} забег., {p.profile}, {p.speed}x)
                </option>
              ))}
            </select>
            <button
              type="button"
              onClick={() => setIsCustomPresetModalOpen(true)}
              className="p-1 text-slate-400 hover:text-cyan-300 hover:bg-slate-800 rounded transition-all"
              title="Создать собственный пресет симуляции"
            >
              <Plus className="w-3.5 h-3.5 text-cyan-400" />
            </button>
          </div>

          <button
            disabled={isBotRunning}
            onClick={() => handleRunBot()}
            className="px-4 py-2 rounded-lg bg-gradient-to-r from-cyan-500 to-blue-600 hover:from-cyan-400 hover:to-blue-500 disabled:opacity-50 text-slate-950 text-xs font-black flex items-center gap-1.5 transition-all shadow-md shadow-cyan-900/30 cursor-pointer active:scale-95"
            title="Запустить выбранный пресет симулятора ботов (run_simulations.bat)"
          >
            {isBotRunning ? (
              <>
                <Loader2 className="w-4 h-4 animate-spin text-slate-950" />
                <span>Запуск...</span>
              </>
            ) : (
              <>
                <Terminal className="w-4 h-4 text-slate-950" />
                <span>Запустить симулятор</span>
              </>
            )}
          </button>

          {/* Live progress of the Unreal bot batch (Sprint 05-B). */}
          <BotStatusBar compact />

          <button
            onClick={() => {
              if (confirm('Сбросить конфиг к исходному эталону КПП?')) {
                setConfig(DEFAULT_LEVEL);
                setSelectedWaveIndex(1);
              }
            }}
            className="p-2 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-400 hover:text-slate-200 text-xs transition-all border border-slate-700"
            title="Сбросить к эталону"
          >
            <RotateCcw className="w-4 h-4" />
          </button>
        </div>

        {/* Навигационные табы */}
        <div className="w-full flex items-center justify-between pt-2 border-t border-slate-800/80">
          <div className="flex items-center gap-2 bg-slate-950 p-1 rounded-xl border border-slate-800">
            <button
              onClick={() => setActiveTab('waves')}
              className={`flex items-center gap-2 px-4 py-1.5 rounded-lg text-xs font-bold transition-all ${
                activeTab === 'waves'
                  ? 'bg-sky-500 text-slate-950 shadow-md shadow-sky-500/20'
                  : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800/60'
              }`}
            >
              <Layers className="w-3.5 h-3.5" />
              Конфигуратор волн и графики
            </button>
            <button
              onClick={() => setActiveTab('telemetry')}
              className={`flex items-center gap-2 px-4 py-1.5 rounded-lg text-xs font-bold transition-all ${
                activeTab === 'telemetry'
                  ? 'bg-cyan-500 text-slate-950 shadow-md shadow-cyan-500/20'
                  : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800/60'
              }`}
            >
              <Activity className="w-3.5 h-3.5" />
              Телеметрия симуляций и боты
            </button>
          </div>

          <div className="hidden sm:flex items-center gap-2 text-xs text-slate-400 font-mono">
            <span>Автономное тестирование:</span>
            <span className="px-2 py-0.5 bg-slate-800 text-cyan-400 rounded border border-slate-700">run_simulations.bat</span>
          </div>
        </div>
      </header>

      <main className="flex-1 p-6 max-w-7xl mx-auto w-full flex flex-col gap-6">
        {activeTab === 'telemetry' ? (
          <TelemetryAnalytics />
        ) : (
          <>
        <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 flex flex-wrap items-center justify-between gap-4">
          <div className="flex items-center gap-3">
            <FileCode2 className="w-6 h-6 text-sky-400" />
            <div>
              <div className="flex items-center gap-2">
                <input
                  type="text"
                  value={config.level_name}
                  onChange={(e) => setConfig({ ...config, level_name: e.target.value })}
                  className="bg-transparent text-sm font-bold text-slate-100 focus:outline-none focus:bg-slate-800 px-1.5 py-0.5 rounded border border-transparent hover:border-slate-700"
                />
                <span className="text-xs text-slate-500 font-mono">({config.level_id})</span>
              </div>
              <input
                type="text"
                value={config.description}
                onChange={(e) => setConfig({ ...config, description: e.target.value })}
                className="bg-transparent text-xs text-slate-400 focus:outline-none focus:bg-slate-800 px-1.5 py-0.5 rounded border border-transparent hover:border-slate-700 w-full max-w-xl"
              />
            </div>
          </div>

          <div className="flex items-center gap-4 text-xs">
            <div className="bg-slate-950 px-3 py-1.5 rounded-lg border border-slate-800">
              <span className="text-slate-500">Подготовка:</span>{' '}
              <span className="font-bold text-sky-400">{config.prep_phase_duration}с</span>
            </div>
            <div className="bg-slate-950 px-3 py-1.5 rounded-lg border border-slate-800">
              <span className="text-slate-500">Отдых:</span>{' '}
              <span className="font-bold text-indigo-400">{config.wave_rest_duration}с</span>
            </div>
            <div className="bg-slate-950 px-3 py-1.5 rounded-lg border border-slate-800">
              <span className="text-slate-500">Волн:</span>{' '}
              <span className="font-bold text-emerald-400">{config.waves.length}</span>
            </div>
          </div>
        </div>

        {/* ❄️ Карточка мониторинга холода последнего боя прямо в конфигураторе волн */}
        {latestColdTelemetry.length > 0 && (
          <div className="bg-slate-900 border border-sky-800/40 rounded-xl p-4 shadow-lg flex flex-wrap items-center justify-between gap-4">
            <div className="flex items-center gap-3">
              <div className="w-9 h-9 rounded-xl bg-sky-500/20 text-sky-400 flex items-center justify-center border border-sky-500/30">
                <Snowflake className="w-5 h-5 text-sky-400" />
              </div>
              <div>
                <div className="text-sm font-bold text-slate-100 flex items-center gap-2">
                  Воздействие экстремального холода (последний бой)
                  <span className="text-[10px] font-mono uppercase bg-sky-950 text-sky-400 border border-sky-800 px-2 py-0.5 rounded">
                    порог 100% замерзания
                  </span>
                </div>
                <div className="text-xs text-slate-400">
                  Время пребывания бойцов в экстремальном холоде (100%+) и полученный урон здоровью
                </div>
              </div>
            </div>

            <div className="flex flex-wrap items-center gap-3">
              {latestColdTelemetry.map((m, idx) => {
                const tookDamage = m.cold_damage_taken > 0;
                const wasExtreme = m.extreme_cold_time_sec > 0;
                return (
                  <div key={idx} className="bg-slate-950 px-3.5 py-2 rounded-xl border border-slate-800 flex items-center gap-3 text-xs shadow-inner">
                    <span className="font-bold text-slate-200">{m.name}:</span>
                    <span className={`font-semibold ${tookDamage ? 'text-rose-400' : 'text-slate-400'}`}>
                      Урон: {m.cold_damage_taken.toFixed(1)} HP
                    </span>
                    <span className="text-slate-600">•</span>
                    <span className={`font-semibold ${wasExtreme ? 'text-sky-300' : 'text-slate-400'}`}>
                      100%+ экстрим: {m.extreme_cold_time_sec.toFixed(1)} с
                    </span>
                    <span className="text-slate-600">•</span>
                    <span className="text-slate-400">Холод: {m.final_cold_pct}%</span>
                  </div>
                );
              })}
            </div>
          </div>
        )}

        {/* Графики сложности по активным волнам */}
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
          <DifficultyCurveChart
            waves={config.waves.filter((w) => w.is_active !== false)}
            selectedWaveIndex={selectedWaveIndex}
            onSelectWave={setSelectedWaveIndex}
          />
          <EnemyCompositionBarChart
            waves={config.waves.filter((w) => w.is_active !== false)}
            selectedWaveIndex={selectedWaveIndex}
            onSelectWave={setSelectedWaveIndex}
          />
        </div>

        {/* Настройка снаряжения отряда и режима симуляции */}
        <SquadLoadoutControls
          loadout={config.squad_loadout}
          onChange={(updated) => setConfig({ ...config, squad_loadout: updated })}
        />

        <GlobalCurveControls
          config={config}
          onChangeConfig={setConfig}
        />

        {/* Живой советник математического баланса активной волны на лету */}
        {activeWave && (
          <LiveBalanceAdvisor
            wave={activeWave}
            allWaves={config.waves}
            loadout={config.squad_loadout}
          />
        )}

        {/* 12 Кнопок управления волнами */}
        <WaveToggleGrid
          waves={config.waves}
          selectedWaveIndex={selectedWaveIndex}
          onSelectWave={setSelectedWaveIndex}
          onToggleWaveActive={handleToggleWaveActive}
        />

        {activeWave && (
          <WaveEditorCard
            wave={activeWave}
            totalWaves={config.waves.length}
            onUpdateWave={handleUpdateWave}
            onSelectWave={setSelectedWaveIndex}
            onDeleteWave={handleDeleteWave}
            onDuplicateWave={handleDuplicateWave}
            onToggleActive={(active) => handleToggleWaveActive(activeWave.wave_index, active)}
            onRunBot={() => handleRunBot()}
            isBotRunning={isBotRunning}
            onApplyColdToAllWaves={handleApplyColdToAll}
          />
        )}
          </>
        )}
      </main>

      {/* Модальное окно создания собственного пресета симуляции */}
      {isCustomPresetModalOpen && (
        <div className="fixed inset-0 bg-black/70 backdrop-blur-sm z-50 flex items-center justify-center p-4">
          <div className="bg-slate-900 border border-slate-800 rounded-2xl max-w-md w-full p-6 shadow-2xl space-y-4">
            <div className="flex items-center justify-between border-b border-slate-800 pb-3">
              <h3 className="text-base font-bold text-slate-100 flex items-center gap-2">
                <Terminal className="w-4 h-4 text-cyan-400" />
                Создать собственный пресет бота
              </h3>
              <button
                onClick={() => setIsCustomPresetModalOpen(false)}
                className="text-slate-400 hover:text-slate-200 p-1"
              >
                <X className="w-4 h-4" />
              </button>
            </div>

            <div className="space-y-3 text-xs">
              <div>
                <label className="block text-slate-400 font-semibold mb-1">Название пресета:</label>
                <input
                  type="text"
                  value={newPresetForm.name}
                  onChange={(e) => setNewPresetForm({ ...newPresetForm, name: e.target.value })}
                  placeholder="напр. Экспресс Буран"
                  className="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-slate-100 focus:outline-none focus:border-cyan-500 font-medium"
                />
              </div>

              <div className="grid grid-cols-2 gap-3">
                <div>
                  <label className="block text-slate-400 font-semibold mb-1">Количество забегов:</label>
                  <input
                    type="number"
                    min={1}
                    max={100}
                    value={newPresetForm.runs}
                    onChange={(e) => setNewPresetForm({ ...newPresetForm, runs: Math.max(1, parseInt(e.target.value) || 1) })}
                    className="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-slate-100 focus:outline-none focus:border-cyan-500 font-medium"
                  />
                </div>
                <div>
                  <label className="block text-slate-400 font-semibold mb-1">Профиль бота:</label>
                  <select
                    value={newPresetForm.profile}
                    onChange={(e) => setNewPresetForm({ ...newPresetForm, profile: e.target.value as any })}
                    className="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-slate-100 focus:outline-none focus:border-cyan-500 font-medium"
                  >
                    <option value="NORMAL">NORMAL (Обычный)</option>
                    <option value="VETERAN">VETERAN (Тактик)</option>
                    <option value="CASUAL">CASUAL (Новичок)</option>
                  </select>
                </div>
              </div>

              <div>
                <div className="flex justify-between items-center mb-1">
                  <label className="text-slate-400 font-semibold">Скорость симуляции:</label>
                  <span className="font-mono text-cyan-400 font-bold">{newPresetForm.speed.toFixed(1)}x</span>
                </div>
                <input
                  type="range"
                  min={1.0}
                  max={10.0}
                  step={0.5}
                  value={newPresetForm.speed}
                  onChange={(e) => setNewPresetForm({ ...newPresetForm, speed: parseFloat(e.target.value) })}
                  className="w-full accent-cyan-500 cursor-pointer"
                />
                <div className="flex justify-between text-[10px] text-slate-500 mt-0.5 font-mono">
                  <span>1.0x (Норма)</span>
                  <span>5.0x (Быстро)</span>
                  <span>10.0x (Максимум)</span>
                </div>
              </div>
            </div>

            <div className="flex items-center justify-end gap-2.5 pt-3 border-t border-slate-800">
              <button
                type="button"
                onClick={() => setIsCustomPresetModalOpen(false)}
                className="px-3.5 py-2 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-300 text-xs font-semibold"
              >
                Отмена
              </button>
              <button
                type="button"
                onClick={handleCreatePreset}
                className="px-4 py-2 rounded-lg bg-cyan-500 hover:bg-cyan-400 text-slate-950 text-xs font-bold shadow-lg shadow-cyan-500/20"
              >
                Сохранить и выбрать
              </button>
            </div>
          </div>
        </div>
      )}

      <footer className="border-t border-slate-800 py-3 px-6 text-center text-xs text-slate-500 bg-slate-950">
        Codex Tactics Architecture • Data Contract: <code className="text-slate-400">data/schemas/level_config.schema.json</code> • Clean Core Isolation
      </footer>
    </div>
  );
};
