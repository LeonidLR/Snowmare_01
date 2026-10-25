import React, { useEffect, useRef, useState } from 'react';
import { Bot, CheckCircle2, Loader2, XCircle } from 'lucide-react';

/** /api/bot-status (Scripts/bot_run.ps1 -> Saved/Telemetry/bot_status.json). */
export interface BotStatus {
  isRunning: boolean;
  currentRun: number;
  totalRuns: number;
  activeRuns: number;
  victories: number;
  defeats: number;
  aborted: number;
  errors: number;
  elapsedSec: number;
  profile: string;
  parallel?: number;
  stale?: boolean;
}

interface Props {
  /** Called once when a running batch finishes (reload the analytics). */
  onFinished?: () => void;
  /** One-line badge for the header instead of the full panel. */
  compact?: boolean;
}

const formatTime = (sec: number) => `${Math.floor(sec / 60)}:${String(sec % 60).padStart(2, '0')}`;

/** Live progress of the Unreal playtest bot batch (Sprint 05-B): polled every 1.5 s. */
export const BotStatusBar: React.FC<Props> = ({ onFinished, compact }) => {
  const [status, setStatus] = useState<BotStatus | null>(null);
  const [justFinished, setJustFinished] = useState(false);
  const wasRunning = useRef(false);
  const finishedCallback = useRef(onFinished);
  finishedCallback.current = onFinished;

  useEffect(() => {
    let cancelled = false;
    const poll = async () => {
      try {
        const res = await fetch('/api/bot-status');
        if (!res.ok || cancelled) return;
        const next: BotStatus = await res.json();
        setStatus(next);
        if (wasRunning.current && !next.isRunning) {
          setJustFinished(true);
          finishedCallback.current?.();
          setTimeout(() => !cancelled && setJustFinished(false), 15000);
        }
        wasRunning.current = next.isRunning;
      } catch {
        /* dev server not reachable: keep the last state */
      }
    };
    poll();
    const timer = setInterval(poll, 1500);
    return () => {
      cancelled = true;
      clearInterval(timer);
    };
  }, []);

  if (!status || (!status.isRunning && !justFinished)) {
    return null;
  }
  const total = Math.max(1, status.totalRuns);
  const percent = Math.round((status.currentRun / total) * 100);
  const failed = status.aborted + status.errors;

  if (compact) {
    return (
      <div className="flex items-center gap-2 px-3 py-1.5 rounded-lg bg-slate-800/80 border border-cyan-700/60 text-xs text-slate-200"
        title="Прогон бота Unreal (Scripts/bot_run.ps1)">
        {status.isRunning ? <Loader2 className="w-3.5 h-3.5 animate-spin text-cyan-400" /> : <CheckCircle2 className="w-3.5 h-3.5 text-emerald-400" />}
        <span className="font-mono">{status.profile} {status.currentRun}/{status.totalRuns}</span>
        <span className="text-emerald-400">✔ {status.victories}</span>
        <span className="text-red-400">✖ {status.defeats}</span>
      </div>
    );
  }

  return (
    <div className="bg-slate-800/80 p-4 rounded-2xl border border-cyan-800/60 shadow-xl">
      <div className="flex flex-wrap items-center justify-between gap-3 mb-3">
        <div className="flex items-center gap-2 text-slate-100 font-semibold">
          {status.isRunning ? <Loader2 className="w-4 h-4 animate-spin text-cyan-400" /> : <Bot className="w-4 h-4 text-emerald-400" />}
          {status.isRunning
            ? `Идёт прогон бота: ${status.profile}, ${status.currentRun} из ${status.totalRuns} (одновременно ${status.activeRuns})`
            : `Прогон завершён: ${status.profile}, ${status.currentRun} из ${status.totalRuns}${status.stale ? ' (окно раннера закрыто)' : ''}`}
        </div>
        <div className="flex items-center gap-4 text-sm">
          <span className="flex items-center gap-1 text-emerald-400"><CheckCircle2 className="w-4 h-4" /> Победы: {status.victories}</span>
          <span className="flex items-center gap-1 text-red-400"><XCircle className="w-4 h-4" /> Поражения: {status.defeats}</span>
          {failed > 0 && <span className="text-amber-400">Сбои / таймауты: {failed}</span>}
          <span className="font-mono text-slate-400">{formatTime(status.elapsedSec)}</span>
        </div>
      </div>
      <div className="h-3 w-full rounded-full bg-slate-900 overflow-hidden border border-slate-700">
        <div className={`h-full transition-all duration-500 ${status.isRunning ? 'bg-cyan-500' : 'bg-emerald-500'}`} style={{ width: `${percent}%` }} />
      </div>
      <div className="mt-1 text-right text-xs text-slate-400 font-mono">{percent}%</div>
    </div>
  );
};
