import React, { useState, useEffect } from 'react';
import { BarChart, Bar, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer } from 'recharts';
import { ShieldAlert, Trophy, Skull, Activity, FileText, Upload, Users, RefreshCw, Trash2, Snowflake, Crosshair, Bomb, Shield, Wrench, BarChart2 } from 'lucide-react';

import { TelemetryRun, WeaponMemberTelemetry, MemberColdTelemetry } from '../types';
import { TacticalReplayPlayer } from './TacticalReplayPlayer';

const DEFAULT_RUNS_DATA: TelemetryRun[] = [
  {
    session_id: "init_01",
    timestamp_utc: "2026-09-03T11:32:57",
    tester_profile: "VETERAN",
    level_id: "outpost_gate_01",
    result: "DEFEAT",
    waves_cleared: 2,
    total_waves: 10,
    run_duration_sec: 3.85,
    death_context: { failed_wave: 3, cause: "HP_DEPLETED" }
  },
  {
    session_id: "init_02",
    timestamp_utc: "2026-09-03T11:33:16",
    tester_profile: "CASUAL",
    level_id: "outpost_gate_01",
    result: "DEFEAT",
    waves_cleared: 1,
    total_waves: 10,
    run_duration_sec: 6.47,
    death_context: { failed_wave: 2, cause: "HP_DEPLETED" }
  }
];

export const TelemetryAnalytics: React.FC = () => {
  const [runs, setRuns] = useState<TelemetryRun[]>(() => {
    const saved = localStorage.getItem('codex_telemetry_runs_v1');
    if (saved) {
      try { return JSON.parse(saved); } catch (e) { /* ignore */ }
    }
    return DEFAULT_RUNS_DATA;
  });

  const [rawJsonlInput, setRawJsonlInput] = useState('');
  const [showInputModal, setShowInputModal] = useState(false);
  const [subTab, setSubTab] = useState<'replay' | 'charts'>('replay');

  const handleImportJsonl = (text: string) => {
    const lines = text.trim().split('\n');
    const parsed: TelemetryRun[] = [];
    for (const line of lines) {
      if (!line.trim()) continue;
      try {
        const item = JSON.parse(line.trim());
        if (item.session_id && item.result) {
          parsed.push(item);
        }
      } catch (e) {
        console.error("Failed to parse line:", line);
      }
    }
    if (parsed.length > 0) {
      setRuns(parsed);
      localStorage.setItem('codex_telemetry_runs_v1', JSON.stringify(parsed));
      setShowInputModal(false);
    }
  };

  const handleClearTelemetry = async () => {
    if (!confirm('Вы действительно хотите очистить всю историю забегов и начать тестирование с чистого листа?')) {
      return;
    }
    try {
      await fetch('/api/clear-telemetry', { method: 'POST' });
      setRuns([]);
      localStorage.removeItem('codex_telemetry_runs_v1');
    } catch (e) {
      console.error('Failed to clear telemetry', e);
    }
  };

  const loadLatestTelemetry = async () => {
    try {
      const res = await fetch('/api/get-telemetry');
      if (res.ok) {
        const text = await res.text();
        if (text && text.trim().length > 0) {
          handleImportJsonl(text);
        } else {
          setRuns([]);
          localStorage.removeItem('codex_telemetry_runs_v1');
        }
      }
    } catch (e) {
      console.warn('Could not auto-fetch telemetry:', e);
    }
  };

  useEffect(() => {
    loadLatestTelemetry();
    const interval = setInterval(() => {
      loadLatestTelemetry();
    }, 3500);
    return () => clearInterval(interval);
  }, []);

  const handleFileUpload = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (event) => {
      const content = event.target?.result as string;
      if (content) handleImportJsonl(content);
    };
    reader.readAsText(file);
  };

  // KPIs
  const totalRuns = runs.length;
  const victories = runs.filter(r => r.result === 'VICTORY').length;
  const winrate = totalRuns > 0 ? ((victories / totalRuns) * 100).toFixed(1) : '0';

  const veteranRuns = runs.filter(r => r.tester_profile.includes('VETERAN'));
  const normalRuns = runs.filter(r => r.tester_profile.includes('NORMAL') || r.tester_profile.includes('REGULAR'));
  const casualRuns = runs.filter(r => r.tester_profile.includes('CASUAL'));

  const avgClearedVet = veteranRuns.length > 0
    ? (veteranRuns.reduce((acc, r) => acc + r.waves_cleared, 0) / veteranRuns.length).toFixed(1)
    : '0';

  const avgClearedNorm = normalRuns.length > 0
    ? (normalRuns.reduce((acc, r) => acc + r.waves_cleared, 0) / normalRuns.length).toFixed(1)
    : '0';

  const avgClearedCas = casualRuns.length > 0
    ? (casualRuns.reduce((acc, r) => acc + r.waves_cleared, 0) / casualRuns.length).toFixed(1)
    : '0';

  // Death distribution per wave (для 12 волн)
  const deathByWave: { [wave: number]: { casual: number; normal: number; veteran: number; total: number } } = {};
  for (let w = 1; w <= 12; w++) {
    deathByWave[w] = { casual: 0, normal: 0, veteran: 0, total: 0 };
  }

  runs.forEach(r => {
    if (r.death_context && r.death_context.failed_wave) {
      const w = r.death_context.failed_wave;
      if (!deathByWave[w]) deathByWave[w] = { casual: 0, normal: 0, veteran: 0, total: 0 };
      if (r.tester_profile.includes('VETERAN')) {
        deathByWave[w].veteran += 1;
      } else if (r.tester_profile.includes('NORMAL') || r.tester_profile.includes('REGULAR')) {
        deathByWave[w].normal += 1;
      } else {
        deathByWave[w].casual += 1;
      }
      deathByWave[w].total += 1;
    }
  });

  const lethalityData = Object.keys(deathByWave).map(wStr => {
    const w = parseInt(wStr);
    return {
      wave: `Волна ${w}`,
      'Казуал (CASUAL)': deathByWave[w].casual,
      'Обычный (NORMAL)': deathByWave[w].normal,
      'Ветеран (VETERAN)': deathByWave[w].veteran,
      total: deathByWave[w].total
    };
  });

  // Расчёт средних остатков ресурсов и эффективности укреплений
  let totalM16Rem = 0, totalPistolRem = 0, totalMedRem = 0, totalCansRem = 0, totalChocoRem = 0;
  let turretKillsTotal = 0, turretDmgTotal = 0, turretSurvTotal = 0, turretHpTotal = 0;
  let mineKillsTotal = 0, mineDmgTotal = 0, mineDetTotal = 0;
  let barKillsTotal = 0, barDmgTotal = 0, barSurvTotal = 0, barHpTotal = 0;
  let countWithResources = 0;

  runs.forEach(r => {
    if (r.remaining_resources) {
      countWithResources += 1;
      totalM16Rem += r.remaining_resources.ammo_m16 || 0;
      totalPistolRem += r.remaining_resources.ammo_pistol || 0;
      totalMedRem += r.remaining_resources.medkits || 0;
      totalCansRem += r.remaining_resources.canned_food || 0;
      totalChocoRem += r.remaining_resources.chocolate || 0;
    }
    if (r.deployables_summary) {
      const d = r.deployables_summary;
      if (d.turrets) {
        turretKillsTotal += d.turrets.kills || 0;
        turretDmgTotal += d.turrets.damage || 0;
        turretSurvTotal += d.turrets.survived || 0;
        turretHpTotal += d.turrets.avg_hp_pct || 0;
      }
      if (d.mines) {
        mineKillsTotal += d.mines.kills || 0;
        mineDmgTotal += d.mines.damage || 0;
        mineDetTotal += d.mines.detonated || 0;
      }
      if (d.barricades) {
        barKillsTotal += d.barricades.kills || 0;
        barDmgTotal += d.barricades.damage || 0;
        barSurvTotal += d.barricades.survived || 0;
        barHpTotal += d.barricades.avg_hp_pct || 0;
      }
    }
  });

  const avgM16Rem = countWithResources > 0 ? Math.round(totalM16Rem / countWithResources) : 0;
  const avgPistolRem = countWithResources > 0 ? Math.round(totalPistolRem / countWithResources) : 0;
  const avgMedRem = countWithResources > 0 ? (totalMedRem / countWithResources).toFixed(1) : '0';
  const avgCansRem = countWithResources > 0 ? (totalCansRem / countWithResources).toFixed(1) : '0';
  const avgChocoRem = countWithResources > 0 ? (totalChocoRem / countWithResources).toFixed(1) : '0';

  const lastRunWithWeapons = [...runs].reverse().find(r => r.weapon_analytics && r.weapon_analytics.length > 0);
  const latestWeaponAnalytics = lastRunWithWeapons?.weapon_analytics || [];

  const lastRunWithCold = [...runs].reverse().find(r => r.squad_cold_stats && r.squad_cold_stats.length > 0);
  const latestColdStats: MemberColdTelemetry[] = lastRunWithCold?.squad_cold_stats || [];

  let runsWithColdCount = 0;
  let totalColdDmgTaken = 0;
  let totalExtremeColdSec = 0;
  runs.forEach(r => {
    if (r.squad_cold_stats && r.squad_cold_stats.length > 0) {
      runsWithColdCount++;
      r.squad_cold_stats.forEach(sc => {
        totalColdDmgTaken += sc.cold_damage_taken || 0;
        totalExtremeColdSec += sc.extreme_cold_time_sec || 0;
      });
    }
  });

  // Calculate recent runs and latest status
  const recentRuns = runs.slice(-5);
  const recentVictories = recentRuns.filter(r => r.result === 'VICTORY').length;
  const recentWinrate = recentRuns.length > 0 ? Math.round((recentVictories / recentRuns.length) * 100) : 0;
  const latestRun = runs.length > 0 ? runs[runs.length - 1] : null;
  const totalDeaths = runs.filter(r => r.result === 'DEFEAT').length;

  // Find most lethal wave
  let maxDeaths = 0;
  let mostLethalWave = 1;
  Object.entries(deathByWave).forEach(([w, data]) => {
    if (data.total > maxDeaths) {
      maxDeaths = data.total;
      mostLethalWave = parseInt(w);
    }
  });

  return (
    <div className="space-y-6">
      {/* Top Banner & Actions */}
      <div className="flex flex-col md:flex-row items-start md:items-center justify-between gap-4 bg-slate-800/80 p-5 rounded-2xl border border-slate-700/60 shadow-xl backdrop-blur-md">
        <div>
          <h2 className="text-xl font-bold text-slate-100 flex items-center gap-2">
            <Activity className="w-5 h-5 text-cyan-400" />
            Аналитика телеметрии автономных симуляций
          </h2>
          <p className="text-sm text-slate-400 mt-1">
            Анализ реальных логов забегов ботов (<code>data/telemetry/raw_runs/runs.jsonl</code>)
          </p>
        </div>

        <div className="flex items-center gap-3">
          <button
            onClick={handleClearTelemetry}
            className="flex items-center gap-2 px-3 py-2 bg-red-950/50 hover:bg-red-900/70 text-red-300 text-sm font-semibold rounded-xl transition-all border border-red-800/60 shadow-md cursor-pointer"
            title="Очистить все старые забеги и начать с чистого листа"
          >
            <Trash2 className="w-4 h-4 text-red-400" />
            Очистить историю
          </button>
          <button
            onClick={loadLatestTelemetry}
            className="flex items-center gap-2 px-4 py-2 bg-emerald-600 hover:bg-emerald-500 text-white text-sm font-semibold rounded-xl transition-all shadow-md hover:shadow-emerald-600/30"
            title="Автоматически прочитать свежие логи забегов из data/telemetry/raw_runs/runs.jsonl"
          >
            <RefreshCw className="w-4 h-4" />
            Обновить из игры
          </button>
          <label className="flex items-center gap-2 px-4 py-2 bg-slate-700 hover:bg-slate-600 text-slate-200 text-sm font-medium rounded-xl cursor-pointer transition-colors border border-slate-600">
            <Upload className="w-4 h-4 text-cyan-400" />
            Загрузить runs.jsonl
            <input type="file" accept=".jsonl,.json,.txt" className="hidden" onChange={handleFileUpload} />
          </label>

          <button
            onClick={() => setShowInputModal(true)}
            className="flex items-center gap-2 px-4 py-2 bg-cyan-600 hover:bg-cyan-500 text-white text-sm font-medium rounded-xl transition-colors shadow-lg shadow-cyan-900/30"
          >
            <FileText className="w-4 h-4" />
            Вставить JSONL текст
          </button>
        </div>
      </div>

      {/* Sub-tab Navigation */}
      <div className="flex items-center gap-3 border-b border-slate-700/80 pb-3">
        <button
          onClick={() => setSubTab('replay')}
          className={`px-4 py-2.5 rounded-xl text-sm font-semibold flex items-center gap-2 transition-all ${
            subTab === 'replay'
              ? 'bg-emerald-600 text-white shadow-lg shadow-emerald-600/30'
              : 'bg-slate-800/80 text-slate-400 hover:text-slate-200 hover:bg-slate-800 border border-slate-700'
          }`}
        >
          <Activity className="w-4 h-4" />
          2D Тактический Плеер (Левел-Дизайн)
        </button>
        <button
          onClick={() => setSubTab('charts')}
          className={`px-4 py-2.5 rounded-xl text-sm font-semibold flex items-center gap-2 transition-all ${
            subTab === 'charts'
              ? 'bg-emerald-600 text-white shadow-lg shadow-emerald-600/30'
              : 'bg-slate-800/80 text-slate-400 hover:text-slate-200 hover:bg-slate-800 border border-slate-700'
          }`}
        >
          <BarChart2 className="w-4 h-4" />
          Сводная Статистика и Графики
        </button>
      </div>

      {subTab === 'replay' ? (
        <TacticalReplayPlayer />
      ) : (
        <>
          {/* KPI Cards */}
          <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
        <div className="bg-slate-800/60 p-5 rounded-xl border border-slate-700/50 shadow-md">
          <div className="flex items-center justify-between">
            <span className="text-xs font-semibold text-slate-400 uppercase tracking-wider">Всего забегов</span>
            <Activity className="w-4 h-4 text-blue-400" />
          </div>
          <div className="text-3xl font-black text-slate-100 mt-2">{totalRuns}</div>
          <div className="text-xs text-slate-500 mt-1">VET: {veteranRuns.length} | NORM: {normalRuns.length} | CAS: {casualRuns.length}</div>
        </div>

        <div className="bg-slate-800/60 p-5 rounded-xl border border-slate-700/50 shadow-md">
          <div className="flex items-center justify-between">
            <span className="text-xs font-semibold text-slate-400 uppercase tracking-wider">Общий Винрейт</span>
            <Trophy className="w-4 h-4 text-emerald-400" />
          </div>
          <div className="text-3xl font-black text-emerald-400 mt-2">{winrate}%</div>
          <div className="text-xs text-slate-500 mt-1">{victories} побед из {totalRuns} симуляций</div>
        </div>

        <div className="bg-slate-800/60 p-5 rounded-xl border border-slate-700/50 shadow-md">
          <div className="flex items-center justify-between">
            <span className="text-xs font-semibold text-slate-400 uppercase tracking-wider">Ср. волн: Вет / Норм / Каз</span>
            <Users className="w-4 h-4 text-indigo-400" />
          </div>
          <div className="text-2xl font-black text-slate-100 mt-2 flex items-baseline">
            <span className="text-indigo-400" title="Ветеран">{avgClearedVet}</span>
            <span className="text-slate-600 text-base mx-1.5">/</span>
            <span className="text-sky-400" title="Обычный">{avgClearedNorm}</span>
            <span className="text-slate-600 text-base mx-1.5">/</span>
            <span className="text-rose-400" title="Казуал">{avgClearedCas}</span>
          </div>
          <div className="text-xs text-slate-500 mt-1">Ветеран (VET) / Обычный (NORM) / Казуал (CAS)</div>
        </div>

        <div className="bg-slate-800/60 p-5 rounded-xl border border-slate-700/50 shadow-md">
          <div className="flex items-center justify-between">
            <span className="text-xs font-semibold text-slate-400 uppercase tracking-wider">Смертельный пик</span>
            <Skull className="w-4 h-4 text-rose-400" />
          </div>
          <div className="text-3xl font-black text-rose-400 mt-2">
            {totalDeaths === 0 ? 'Нет' : `Волна ${mostLethalWave}`}
          </div>
          <div className="text-xs text-slate-400 mt-1">
            {totalDeaths === 0 ? '100% симуляций победные' : `Узкое горлышко сложности (${maxDeaths} поражений)`}
          </div>
        </div>
      </div>

      {/* Динамический умный вывод рекомендаций по балансу и холоду */}
      {totalRuns === 0 ? (
        <div className="p-4 rounded-xl border border-slate-700 bg-slate-800/60 flex items-center gap-3">
          <Activity className="w-5 h-5 text-cyan-400 flex-shrink-0" />
          <div className="text-xs text-slate-300">
            Пока нет записанных симуляций. Запустите бота через кнопку <strong>«Перезапустить бота (.bat)»</strong> или сыграйте в игру в Unreal, чтобы сформировать актуальные логи.
          </div>
        </div>
      ) : latestRun?.result === 'VICTORY' && recentWinrate >= 60 ? (
        <div className="p-4 rounded-xl border flex items-start justify-between gap-3 bg-emerald-950/40 border-emerald-500/60 shadow-lg shadow-emerald-950/30">
          <div className="flex items-start gap-3">
            <Trophy className="w-5 h-5 text-emerald-400 flex-shrink-0 mt-0.5" />
            <div>
              <div className="text-sm font-bold text-emerald-200">
                🏆 Текущая калибровка стабильна: последние симуляции завершились ПОБЕДОЙ!
              </div>
              <div className="text-xs mt-1 text-emerald-300/90 leading-relaxed">
                Отряд успешно проходит все волны (последний забег: {latestRun.waves_cleared} из {latestRun.total_waves} волн отражено, винрейт серии: {recentWinrate}%).
                {maxDeaths > 0 && (
                  <span className="text-slate-400 block mt-1">
                    (В ранней истории тестов зафиксировано {maxDeaths} поражение на Волне {mostLethalWave}, которое на текущих настройках уже успешно преодолено).
                  </span>
                )}
              </div>
            </div>
          </div>
          {maxDeaths > 0 && (
            <button
              onClick={handleClearTelemetry}
              className="px-3 py-1.5 bg-slate-800 hover:bg-slate-700 text-slate-300 text-xs font-semibold rounded-lg border border-slate-700 whitespace-nowrap transition-colors cursor-pointer flex-shrink-0"
              title="Очистить старые поражения прошлых версий"
            >
              Сбросить старые логи
            </button>
          )}
        </div>
      ) : maxDeaths > 0 ? (() => {
        const lethalRuns = runs.filter(r => r.result === 'DEFEAT' && r.death_context?.failed_wave === mostLethalWave);
        const freezingDeaths = lethalRuns.filter(r =>
          r.death_context?.cause === 'FREEZING_FATIGUE' ||
          ((r.death_context as any)?.squad_cold_at_death ?? 0) >= 70
        ).length;
        const isFreezingCritical = lethalRuns.length > 0 && (freezingDeaths / lethalRuns.length) >= 0.4;

        return (
          <div className={`p-4 rounded-xl border flex items-start gap-3 transition-all ${
            isFreezingCritical
              ? 'bg-cyan-950/50 border-cyan-500/70 shadow-lg shadow-cyan-950/50'
              : 'bg-amber-950/50 border-amber-500/70 shadow-lg shadow-amber-950/50'
          }`}>
            {isFreezingCritical ? (
              <Snowflake className="w-5 h-5 text-cyan-400 flex-shrink-0 mt-0.5 animate-pulse" />
            ) : (
              <ShieldAlert className="w-5 h-5 text-amber-400 flex-shrink-0 mt-0.5" />
            )}
            <div>
              <div className={`text-sm font-bold ${isFreezingCritical ? 'text-cyan-200' : 'text-amber-200'}`}>
                {isFreezingCritical
                  ? '❄️ Критический фактор: Экстремальный мороз и переохлаждение!'
                  : `⚠️ Анализ боевых потерь: Узкое горлышко на Волне ${mostLethalWave}`}
              </div>
              <div className={`text-xs mt-1.5 leading-relaxed ${isFreezingCritical ? 'text-cyan-300/90' : 'text-amber-300/90'}`}>
                {isFreezingCritical ? (
                  <>
                    На <strong>Волне {mostLethalWave}</strong> отряд погибает от переохлаждения ({freezingDeaths} из {lethalRuns.length} смертей).
                    Бойцы замерзают свыше 70%, теряют скорость и очки действий.
                    <br />
                    <span className="font-bold text-cyan-200">💡 Рекомендация:</span> уменьшите силу холода на Волне {mostLethalWave} (слайдер 0.5x–1.0x) или нажмите <code>🏠 Закрытое (Бункер)</code> для отключения мороза.
                  </>
                ) : (
                  <>
                    На <strong>Волне {mostLethalWave}</strong> отряд погибает от физического урона монстров (температура и холод в безопасных пределах).
                    <br />
                    <span className="font-bold text-amber-200">💡 Рекомендация:</span> в карточке Волны {mostLethalWave} уменьшите число нападающих или раскройте <code>⚙️ Параметры врагов</code> и снизьте их урон или запас здоровья (HP). Также можно включить найденные ящики снабжения.
                  </>
                )}
              </div>
            </div>
          </div>
        );
      })() : null}

      {/* Lethality Chart */}
      <div className="bg-slate-800/70 p-6 rounded-2xl border border-slate-700/60 shadow-xl">
        <h3 className="text-base font-bold text-slate-200 mb-2 flex items-center gap-2">
          <Skull className="w-4 h-4 text-rose-400" />
          Гистограмма смертности по волнам (Lethality Distribution)
        </h3>
        <p className="text-xs text-slate-400 mb-5">
          Показывает, на каких волнах чаще всего погибает отряд для каждого архетипа тестера.
        </p>
        <div className="h-72 w-full">
          <ResponsiveContainer width="100%" height="100%">
            <BarChart data={lethalityData} margin={{ top: 10, right: 20, left: 0, bottom: 20 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="#334155" vertical={false} />
              <XAxis dataKey="wave" stroke="#94a3b8" fontSize={12} tickLine={false} />
              <YAxis stroke="#94a3b8" fontSize={12} allowDecimals={false} />
              <Tooltip
                contentStyle={{ backgroundColor: '#1e293b', borderColor: '#475569', borderRadius: '0.75rem' }}
                itemStyle={{ fontSize: '13px' }}
              />
              <Legend wrapperStyle={{ paddingTop: '15px' }} />
              <Bar dataKey="Казуал (CASUAL)" fill="#f43f5e" radius={[4, 4, 0, 0]} />
              <Bar dataKey="Обычный (NORMAL)" fill="#38bdf8" radius={[4, 4, 0, 0]} />
              <Bar dataKey="Ветеран (VETERAN)" fill="#818cf8" radius={[4, 4, 0, 0]} />
            </BarChart>
          </ResponsiveContainer>
        </div>
      </div>

      {/* Анализ боевой эффективности инженерных укреплений */}
      <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
        {/* Карточка Турелей */}
        <div className="bg-slate-800/80 p-5 rounded-2xl border border-sky-500/30 shadow-lg flex flex-col justify-between">
          <div>
            <div className="flex items-center justify-between">
              <span className="text-xs font-bold text-sky-400 uppercase tracking-wider flex items-center gap-1.5">
                <Crosshair className="w-4 h-4 text-sky-400" />
                Автотурели
              </span>
              <span className="text-[11px] px-2 py-0.5 rounded bg-sky-950/80 text-sky-300 border border-sky-800/60">
                🛡️ Уцелело: {runs.length > 0 ? (turretSurvTotal / runs.length).toFixed(1) : 0} шт ({runs.length > 0 ? Math.round(turretHpTotal / runs.length) : 0}% HP)
              </span>
            </div>
            <div className="mt-3 grid grid-cols-2 gap-2">
              <div className="bg-slate-900/60 p-2.5 rounded-xl border border-slate-700/50">
                <div className="text-[10px] text-slate-400">Уничтожено врагов:</div>
                <div className="text-xl font-black text-sky-300 mt-0.5">{turretKillsTotal}</div>
              </div>
              <div className="bg-slate-900/60 p-2.5 rounded-xl border border-slate-700/50">
                <div className="text-[10px] text-slate-400">Нанесено урона:</div>
                <div className="text-xl font-black text-cyan-400 mt-0.5">{turretDmgTotal}</div>
              </div>
            </div>
          </div>
          <div className="text-[11px] text-slate-400 mt-3 pt-2 border-t border-slate-700/60 flex items-center justify-between">
            <span>Эффективность огня:</span>
            <span className="text-sky-300 font-semibold">{turretKillsTotal > 0 ? Math.round(turretDmgTotal / turretKillsTotal) : 0} урона / фраг</span>
          </div>
        </div>

        {/* Карточка Мин */}
        <div className="bg-slate-800/80 p-5 rounded-2xl border border-rose-500/30 shadow-lg flex flex-col justify-between">
          <div>
            <div className="flex items-center justify-between">
              <span className="text-xs font-bold text-rose-400 uppercase tracking-wider flex items-center gap-1.5">
                <Bomb className="w-4 h-4 text-rose-400" />
                Противопехотные мины
              </span>
              <span className="text-[11px] px-2 py-0.5 rounded bg-rose-950/80 text-rose-300 border border-rose-800/60">
                💥 Сдетонировало: {mineDetTotal} шт
              </span>
            </div>
            <div className="mt-3 grid grid-cols-2 gap-2">
              <div className="bg-slate-900/60 p-2.5 rounded-xl border border-slate-700/50">
                <div className="text-[10px] text-slate-400">Уничтожено врагов:</div>
                <div className="text-xl font-black text-rose-300 mt-0.5">{mineKillsTotal}</div>
              </div>
              <div className="bg-slate-900/60 p-2.5 rounded-xl border border-slate-700/50">
                <div className="text-[10px] text-slate-400">Взрывной урон:</div>
                <div className="text-xl font-black text-orange-400 mt-0.5">{mineDmgTotal}</div>
              </div>
            </div>
          </div>
          <div className="text-[11px] text-slate-400 mt-3 pt-2 border-t border-slate-700/60 flex items-center justify-between">
            <span>Плотность подрыва:</span>
            <span className="text-rose-300 font-semibold">{mineDetTotal > 0 ? (mineKillsTotal / mineDetTotal).toFixed(1) : 0} врагов / мину</span>
          </div>
        </div>

        {/* Карточка Баррикад */}
        <div className="bg-slate-800/80 p-5 rounded-2xl border border-amber-500/30 shadow-lg flex flex-col justify-between">
          <div>
            <div className="flex items-center justify-between">
              <span className="text-xs font-bold text-amber-400 uppercase tracking-wider flex items-center gap-1.5">
                <Shield className="w-4 h-4 text-amber-400" />
                Тактические баррикады
              </span>
              <span className="text-[11px] px-2 py-0.5 rounded bg-amber-950/80 text-amber-300 border border-amber-800/60">
                🛡️ Уцелело: {runs.length > 0 ? (barSurvTotal / runs.length).toFixed(1) : 0} шт ({runs.length > 0 ? Math.round(barHpTotal / runs.length) : 0}% HP)
              </span>
            </div>
            <div className="mt-3 grid grid-cols-2 gap-2">
              <div className="bg-slate-900/60 p-2.5 rounded-xl border border-slate-700/50">
                <div className="text-[10px] text-slate-400">Урон шипов/электро:</div>
                <div className="text-xl font-black text-amber-300 mt-0.5">{barDmgTotal}</div>
              </div>
              <div className="bg-slate-900/60 p-2.5 rounded-xl border border-slate-700/50">
                <div className="text-[10px] text-slate-400">Уничтожено врагов:</div>
                <div className="text-xl font-black text-amber-400 mt-0.5">{barKillsTotal}</div>
              </div>
            </div>
          </div>
          <div className="text-[11px] text-slate-400 mt-3 pt-2 border-t border-slate-700/60 flex items-center justify-between">
            <span>Статус укрытий:</span>
            <span className="text-amber-300 font-semibold">{barSurvTotal > 0 ? 'Выдержали напор' : 'Разрушены волной'}</span>
          </div>
        </div>
      </div>

      {/* Экономика боеприпасов и остатки ресурсов */}
      <div className="bg-slate-800/70 p-6 rounded-2xl border border-slate-700/60 shadow-xl">
        <h3 className="text-base font-bold text-slate-200 mb-2 flex items-center gap-2">
          <Wrench className="w-4 h-4 text-emerald-400" />
          Экономика и остаток припасов отряда после боя
        </h3>
        <p className="text-xs text-slate-400 mb-4">
          Показывает, сколько боеприпасов и медикаментов в среднем сохраняется у отряда к концу сражения.
        </p>
        <div className="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-5 gap-3">
          <div className="bg-slate-900/70 p-3 rounded-xl border border-slate-700">
            <span className="text-[11px] text-slate-400 font-medium">🔫 Патроны M16 (5.56 мм):</span>
            <div className="text-xl font-black text-sky-400 mt-1">{avgM16Rem} <span className="text-xs text-slate-500 font-normal">шт</span></div>
            <div className="text-[10px] text-slate-500 mt-0.5">{avgM16Rem > 30 ? '🟢 Достаточный запас' : '🔴 Опасный дефицит'}</div>
          </div>
          <div className="bg-slate-900/70 p-3 rounded-xl border border-slate-700">
            <span className="text-[11px] text-slate-400 font-medium">🔫 Патроны 9мм (Пистолет):</span>
            <div className="text-xl font-black text-indigo-400 mt-1">{avgPistolRem} <span className="text-xs text-slate-500 font-normal">шт</span></div>
            <div className="text-[10px] text-slate-500 mt-0.5">{avgPistolRem > 15 ? '🟢 Резерв сохранён' : '🟡 Израсходован'}</div>
          </div>
          <div className="bg-slate-900/70 p-3 rounded-xl border border-slate-700">
            <span className="text-[11px] text-slate-400 font-medium">🩹 Аптечки отряда:</span>
            <div className="text-xl font-black text-emerald-400 mt-1">{avgMedRem} <span className="text-xs text-slate-500 font-normal">шт</span></div>
            <div className="text-[10px] text-slate-500 mt-0.5">{Number(avgMedRem) >= 1.0 ? '🟢 Потерь нет' : '🔴 Критично'}</div>
          </div>
          <div className="bg-slate-900/70 p-3 rounded-xl border border-slate-700">
            <span className="text-[11px] text-slate-400 font-medium">🥫 Банки консервов:</span>
            <div className="text-xl font-black text-amber-400 mt-1">{avgCansRem} <span className="text-xs text-slate-500 font-normal">банок</span></div>
            <div className="text-[10px] text-slate-500 mt-0.5">Резерв калорий</div>
          </div>
          <div className="bg-slate-900/70 p-3 rounded-xl border border-slate-700">
            <span className="text-[11px] text-slate-400 font-medium">🍫 Шоколад и спички:</span>
            <div className="text-xl font-black text-purple-400 mt-1">{avgChocoRem} <span className="text-xs text-slate-500 font-normal">шт</span></div>
            <div className="text-[10px] text-slate-500 mt-0.5">Для быстрого согревания</div>
          </div>
        </div>
      </div>

      {/* ❄️ Анализ экстремального холода (100%+) и урон бойцам */}
      <div className="bg-slate-800/70 p-6 rounded-2xl border border-slate-700/60 shadow-xl">
        <div className="flex flex-wrap items-center justify-between gap-3 mb-3">
          <div>
            <h3 className="text-base font-bold text-slate-200 flex items-center gap-2">
              <Snowflake className="w-4 h-4 text-sky-400" />
              Воздействие экстремального холода (100%+) и урон бойцам
            </h3>
            <p className="text-xs text-slate-400 mt-0.5">
              Мониторинг времени нахождения бойцов за порогом 100% обморожения и полученного прямого урона здоровью.
            </p>
          </div>
          {runsWithColdCount > 0 && (
            <div className="flex items-center gap-2 text-xs bg-slate-900/80 px-3 py-1.5 rounded-lg border border-slate-700 text-slate-300">
              <span>Суммарный урон от мороза: <strong className="text-rose-400">{totalColdDmgTaken.toFixed(1)} HP</strong></span>
              <span className="text-slate-600">•</span>
              <span>Время в экстриме (100%+): <strong className="text-sky-300">{totalExtremeColdSec.toFixed(1)} с</strong></span>
            </div>
          )}
        </div>

        {latestColdStats.length > 0 ? (
          <div className="grid grid-cols-1 md:grid-cols-3 gap-4 mt-4">
            {latestColdStats.map((member, mIdx) => {
              const hasExtremeExposure = member.extreme_cold_time_sec > 0;
              const hasColdDamage = member.cold_damage_taken > 0;

              let statusColor = "bg-emerald-950 text-emerald-400 border-emerald-800/50";
              let statusLabel = "🟢 В безопасности";
              if (hasColdDamage || hasExtremeExposure) {
                statusColor = "bg-rose-950 text-rose-300 border-rose-800/50";
                statusLabel = "💀 Переохлаждение / Урон";
              } else if (member.final_cold_pct >= 70) {
                statusColor = "bg-amber-950 text-amber-300 border-amber-800/50";
                statusLabel = "❄️ Сильное замерзание";
              } else if (member.final_cold_pct >= 40) {
                statusColor = "bg-sky-950 text-sky-300 border-sky-800/50";
                statusLabel = "🥶 Озноб";
              }

              return (
                <div key={mIdx} className="bg-slate-900/80 p-4 rounded-xl border border-slate-700/60 flex flex-col justify-between">
                  <div>
                    <div className="flex items-center justify-between mb-2">
                      <span className="text-sm font-bold text-slate-200">{member.name}</span>
                      <span className={`text-[10px] font-semibold px-2 py-0.5 rounded border ${statusColor}`}>
                        {statusLabel}
                      </span>
                    </div>

                    {/* Прогресс замерзания */}
                    <div className="mt-2">
                      <div className="flex justify-between text-[11px] text-slate-400 mb-1">
                        <span>Итоговый холод:</span>
                        <span className="font-bold text-sky-300">{member.final_cold_pct}%</span>
                      </div>
                      <div className="h-2 w-full bg-slate-800 rounded-full overflow-hidden">
                        <div
                          style={{ width: `${Math.min(100, member.final_cold_pct)}%` }}
                          className={`h-full transition-all ${
                            member.final_cold_pct >= 100 ? 'bg-rose-500 animate-pulse' :
                            member.final_cold_pct >= 70 ? 'bg-amber-500' :
                            member.final_cold_pct >= 40 ? 'bg-sky-400' : 'bg-emerald-400'
                          }`}
                        />
                      </div>
                    </div>

                    {/* Ключевые метрики запрошенные пользователем */}
                    <div className="mt-4 grid grid-cols-2 gap-2 text-center">
                      <div className="bg-slate-950/70 p-2.5 rounded-lg border border-slate-800">
                        <div className="text-[10px] text-slate-400">Урон от холода:</div>
                        <div className={`text-base font-black mt-0.5 ${hasColdDamage ? 'text-rose-400' : 'text-slate-300'}`}>
                          {member.cold_damage_taken.toFixed(1)} <span className="text-[10px] font-normal text-slate-500">HP</span>
                        </div>
                      </div>
                      <div className="bg-slate-950/70 p-2.5 rounded-lg border border-slate-800">
                        <div className="text-[10px] text-slate-400">В экстриме (100%+):</div>
                        <div className={`text-base font-black mt-0.5 ${hasExtremeExposure ? 'text-sky-300' : 'text-slate-300'}`}>
                          {member.extreme_cold_time_sec.toFixed(1)} <span className="text-[10px] font-normal text-slate-500">сек</span>
                        </div>
                      </div>
                    </div>
                  </div>

                  <div className="mt-3 pt-2 border-t border-slate-800/80 text-[11px] text-slate-400 flex items-center justify-between">
                    <span>Порог 100% переохлаждения:</span>
                    <span className={hasExtremeExposure ? "text-rose-400 font-bold" : "text-emerald-400 font-medium"}>
                      {hasExtremeExposure ? `Превышен на ${member.extreme_cold_time_sec.toFixed(1)} с` : 'Не достигался'}
                    </span>
                  </div>
                </div>
              );
            })}
          </div>
        ) : (
          <div className="text-center py-6 text-slate-500 text-xs bg-slate-900/40 rounded-xl border border-slate-800/50">
            Для отображения детальной телеметрии обморожения запустите симуляцию (данные записываются автоматически после каждого боя).
          </div>
        )}
      </div>

      {/* Анализ использования оружия бойцами и этапы переключения */}
      {latestWeaponAnalytics.length > 0 && (
        <div className="bg-slate-800/70 p-6 rounded-2xl border border-slate-700/60 shadow-xl">
          <h3 className="text-base font-bold text-slate-200 mb-2 flex items-center gap-2">
            <BarChart2 className="w-4 h-4 text-cyan-400" />
            Анализ вооружения отряда и этапы переключения (M16 vs Пистолет vs Нож)
          </h3>
          <p className="text-xs text-slate-400 mb-4">
            Показывает, из какого оружия стрелял каждый боец и на какой волне произошёл переход на вторичное оружие.
          </p>

          <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
            {latestWeaponAnalytics.map((member: WeaponMemberTelemetry, mIdx: number) => {
              const totalShots = member.total_shots_m16 + member.total_shots_pistol + member.total_strikes_knife;
              const m16Pct = totalShots > 0 ? Math.round((member.total_shots_m16 / totalShots) * 100) : 0;
              const pistolPct = totalShots > 0 ? Math.round((member.total_shots_pistol / totalShots) * 100) : 0;
              const knifePct = totalShots > 0 ? Math.round((member.total_strikes_knife / totalShots) * 100) : 0;

              return (
                <div key={mIdx} className="bg-slate-900/80 p-4 rounded-xl border border-slate-700/60 flex flex-col justify-between">
                  <div>
                    <div className="flex items-center justify-between">
                      <span className="text-sm font-bold text-slate-200">{member.character_name}</span>
                      <span className="text-[10px] font-semibold px-2 py-0.5 rounded bg-sky-950 text-sky-400 border border-sky-800/40">
                        {member.dominant_weapon === 'm16' ? '🔫 M16 Основное' : (member.dominant_weapon === 'pistol' ? '🔫 Пистолет' : '🗡️ Нож')}
                      </span>
                    </div>

                    {/* Шкала распределения выстрелов */}
                    <div className="mt-3">
                      <div className="flex justify-between text-[11px] text-slate-400 mb-1">
                        <span>Доли выстрелов:</span>
                        <span>M16: {m16Pct}% | 9мм: {pistolPct}% {knifePct > 0 ? `| Нож: ${knifePct}%` : ''}</span>
                      </div>
                      <div className="h-2 w-full bg-slate-800 rounded-full overflow-hidden flex">
                        <div style={{ width: `${m16Pct}%` }} className="bg-sky-400" title={`M16: ${member.total_shots_m16} выстр.`} />
                        <div style={{ width: `${pistolPct}%` }} className="bg-indigo-400" title={`Пистолет: ${member.total_shots_pistol} выстр.`} />
                        <div style={{ width: `${knifePct}%` }} className="bg-rose-400" title={`Нож: ${member.total_strikes_knife} ударов`} />
                      </div>
                    </div>

                    {/* Детальная статистика выстрелов и урона */}
                    <div className="mt-3 space-y-1.5 text-[11px]">
                      <div className="flex justify-between text-slate-300">
                        <span className="text-slate-400">Винтовка M16:</span>
                        <span className="font-semibold text-sky-300">{member.total_shots_m16} выстр. ({member.total_damage_m16} урона)</span>
                      </div>
                      <div className="flex justify-between text-slate-300">
                        <span className="text-slate-400">Пистолет 9мм:</span>
                        <span className="font-semibold text-indigo-300">{member.total_shots_pistol} выстр. ({member.total_damage_pistol} урона)</span>
                      </div>
                      {member.total_strikes_knife > 0 && (
                        <div className="flex justify-between text-slate-300">
                          <span className="text-slate-400">Боевой нож:</span>
                          <span className="font-semibold text-rose-300">{member.total_strikes_knife} ударов ({member.total_damage_knife} урона)</span>
                        </div>
                      )}
                    </div>
                  </div>

                  {/* Индикатор момента переключения на пистолет */}
                  <div className="mt-3 pt-2.5 border-t border-slate-800 text-[11px] flex items-center justify-between">
                    <span className="text-slate-400">Переход на пистолет:</span>
                    <span className={`font-bold ${member.first_pistol_wave > 0 ? 'text-amber-400' : 'text-emerald-400'}`}>
                      {member.first_pistol_wave > 0 ? `Волна ${member.first_pistol_wave}` : 'Не потребовался (хватило M16)'}
                    </span>
                  </div>
                </div>
              );
            })}
          </div>
        </div>
      )}

      {/* Raw Runs Table */}
      <div className="bg-slate-800/70 p-6 rounded-2xl border border-slate-700/60 shadow-xl">
        <h3 className="text-base font-bold text-slate-200 mb-4 flex items-center gap-2">
          <FileText className="w-4 h-4 text-cyan-400" />
          Журнал последних симуляций ({runs.length})
        </h3>
        <div className="overflow-x-auto">
          <table className="w-full text-left text-xs text-slate-300">
            <thead className="bg-slate-900/60 text-slate-400 uppercase tracking-wider border-b border-slate-700">
              <tr>
                <th className="py-3 px-4">Время UTC</th>
                <th className="py-3 px-4">Профиль</th>
                <th className="py-3 px-4">Результат</th>
                <th className="py-3 px-4">Пройдено волн</th>
                <th className="py-3 px-4">Длительность</th>
                <th className="py-3 px-4">Причина поражения</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-700/50">
              {runs.slice().reverse().map((r, idx) => (
                <tr key={r.session_id + idx} className="hover:bg-slate-700/30 transition-colors">
                  <td className="py-3 px-4 text-slate-400 font-mono">{r.timestamp_utc}</td>
                  <td className="py-3 px-4">
                    <span className={`px-2 py-0.5 rounded font-bold ${r.tester_profile.includes('VETERAN') ? 'bg-indigo-500/20 text-indigo-300' : 'bg-rose-500/20 text-rose-300'}`}>
                      {r.tester_profile}
                    </span>
                  </td>
                  <td className="py-3 px-4">
                    <span className={`font-bold ${r.result === 'VICTORY' ? 'text-emerald-400' : 'text-rose-400'}`}>
                      {r.result === 'VICTORY' ? '🏆 ПОБЕДА' : '💀 ПОРАЖЕНИЕ'}
                    </span>
                  </td>
                  <td className="py-3 px-4 font-semibold">{r.waves_cleared} / {r.total_waves}</td>
                  <td className="py-3 px-4 font-mono">{r.run_duration_sec.toFixed(2)} с</td>
                  <td className="py-3 px-4 text-slate-400">
                    {r.death_context ? `Волна ${r.death_context.failed_wave} (${r.death_context.cause})` : '—'}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>
      </>
      )}

      {/* Input Modal */}
      {showInputModal && (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 backdrop-blur-sm p-4">
          <div className="bg-slate-800 border border-slate-700 rounded-2xl max-w-2xl w-full p-6 space-y-4 shadow-2xl">
            <h3 className="text-lg font-bold text-slate-100">Вставить сырые строки JSONL</h3>
            <p className="text-xs text-slate-400">
              Скопируйте содержимое файла <code>data/telemetry/raw_runs/runs.jsonl</code> и вставьте сюда:
            </p>
            <textarea
              rows={8}
              value={rawJsonlInput}
              onChange={(e) => setRawJsonlInput(e.target.value)}
              placeholder='{"session_id": "...", "result": "DEFEAT", ...}'
              className="w-full bg-slate-900 border border-slate-700 rounded-xl p-3 text-xs font-mono text-cyan-300 focus:outline-none focus:border-cyan-500"
            />
            <div className="flex justify-end gap-3 pt-2">
              <button
                onClick={() => setShowInputModal(false)}
                className="px-4 py-2 bg-slate-700 hover:bg-slate-600 text-slate-200 text-xs font-semibold rounded-xl"
              >
                Отмена
              </button>
              <button
                onClick={() => handleImportJsonl(rawJsonlInput)}
                className="px-4 py-2 bg-cyan-600 hover:bg-cyan-500 text-white text-xs font-semibold rounded-xl shadow-lg shadow-cyan-900/30"
              >
                Применить данные
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};
