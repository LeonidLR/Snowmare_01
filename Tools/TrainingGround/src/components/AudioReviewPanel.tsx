import React, { useEffect, useMemo, useState } from 'react';
import {
  Volume2,
  Mic,
  Music,
  CheckCircle2,
  XCircle,
  Clock,
  RefreshCw,
  Loader2,
  AlertTriangle,
  DollarSign,
} from 'lucide-react';
import {
  type AudioApprovalDecision,
  type AudioApprovalsFile,
  type AudioItemEntry,
  type AudioListResponse,
  summarizeAudioCounts,
} from '../utils/audioReview.ts';

export const AudioReviewPanel: React.FC = () => {
  const [data, setData] = useState<AudioListResponse | null>(null);
  const [loading, setLoading] = useState<boolean>(true);
  const [savingKey, setSavingKey] = useState<string | null>(null);
  const [statusMessage, setStatusMessage] = useState<{ kind: 'success' | 'error'; text: string } | null>(null);
  const [kindFilter, setKindFilter] = useState<'all' | 'voice' | 'music'>('all');
  const [stateFilter, setStateFilter] = useState<'all' | 'generated' | 'approved' | 'rejected' | 'pending'>('all');
  const [noteDrafts, setNoteDrafts] = useState<Record<string, string>>({});

  const loadData = async () => {
    setLoading(true);
    setStatusMessage(null);
    try {
      const res = await fetch('/api/audio/list');
      if (!res.ok) {
        throw new Error(`HTTP ${res.status}: ${res.statusText}`);
      }
      const json: AudioListResponse = await res.json();
      setData(json);
      const nextNotes: Record<string, string> = {};
      for (const item of json.items) {
        nextNotes[item.key] = json.approvals?.approvals?.[item.key]?.notes ?? '';
      }
      setNoteDrafts(nextNotes);
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      setStatusMessage({ kind: 'error', text: `Не удалось загрузить /api/audio/list: ${msg}` });
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadData();
  }, []);

  const submitReview = async (item: AudioItemEntry, status: AudioApprovalDecision) => {
    setSavingKey(item.key);
    setStatusMessage(null);
    try {
      const notes = noteDrafts[item.key] ?? '';
      const res = await fetch('/api/audio/review', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          kind: item.kind,
          id: item.id,
          status,
          notes,
        }),
      });
      const json = await res.json();
      if (!res.ok) {
        throw new Error(json?.error || `HTTP ${res.status}`);
      }
      const nextApprovals = json.approvals as AudioApprovalsFile;
      setData((prev) => (prev ? { ...prev, approvals: nextApprovals } : prev));
      setStatusMessage({
        kind: 'success',
        text: `Сохранено для ${item.key}: ${status}`,
      });
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      setStatusMessage({ kind: 'error', text: `Ошибка сохранения ${item.key}: ${msg}` });
    } finally {
      setSavingKey(null);
    }
  };

  const counts = useMemo(
    () => (data ? summarizeAudioCounts(data.items, data.approvals) : null),
    [data]
  );

  const visibleItems = useMemo(() => {
    if (!data) return [];
    const map = data.approvals?.approvals ?? {};
    return data.items.filter((item) => {
      if (kindFilter !== 'all' && item.kind !== kindFilter) return false;
      const decision = map[item.key]?.status ?? 'pending';
      if (stateFilter === 'generated') return item.wavExists;
      if (stateFilter === 'approved' || stateFilter === 'rejected' || stateFilter === 'pending') {
        return decision === stateFilter;
      }
      return true;
    });
  }, [data, kindFilter, stateFilter]);

  if (loading && !data) {
    return (
      <div className="bg-slate-900 border border-slate-800 rounded-2xl p-8 flex items-center justify-center gap-3 text-slate-400">
        <Loader2 className="w-5 h-5 animate-spin text-sky-400" />
        <span className="text-sm font-semibold">Загрузка аудио-манифестов и сгенерированных WAV...</span>
      </div>
    );
  }

  return (
    <div className="space-y-4">
      {/* Header & Summary */}
      <div className="bg-slate-900 border border-slate-800 rounded-2xl p-5 flex flex-wrap items-center justify-between gap-4">
        <div className="flex items-center gap-3">
          <div className="p-2.5 rounded-xl bg-sky-500/10 border border-sky-500/20">
            <Volume2 className="w-6 h-6 text-sky-400" />
          </div>
          <div>
            <h2 className="text-base font-black text-slate-100">Аудио v1 — Озвучка и Музыка</h2>
            <p className="text-xs text-slate-400">
              Прослушивание <code className="text-slate-300">Saved/AudioGen/</code> и ревью перед импортом в UE (
              <code className="text-slate-300">Content/Data/Audio/audio_approvals.json</code>)
            </p>
          </div>
        </div>

        <div className="flex flex-wrap items-center gap-2">
          {counts && (
            <>
              <span className="px-2.5 py-1 rounded-lg bg-slate-950 border border-slate-800 text-xs text-slate-300">
                Всего: <b>{counts.total}</b>
              </span>
              <span className="px-2.5 py-1 rounded-lg bg-slate-950 border border-sky-800/60 text-xs text-sky-300">
                WAV готово: <b>{counts.generated}</b>
              </span>
              <span className="px-2.5 py-1 rounded-lg bg-slate-950 border border-emerald-800/60 text-xs text-emerald-300">
                Одобрено: <b>{counts.approved}</b>
              </span>
              <span className="px-2.5 py-1 rounded-lg bg-slate-950 border border-rose-800/60 text-xs text-rose-300">
                Отклонено: <b>{counts.rejected}</b>
              </span>
            </>
          )}
          {data?.spendSummary && (
            <span className="px-2.5 py-1 rounded-lg bg-amber-950/30 border border-amber-700/40 text-xs text-amber-300 flex items-center gap-1">
              <DollarSign className="w-3.5 h-3.5" />
              Потрачено: <b>${data.spendSummary.total_usd.toFixed(4)}</b>
            </span>
          )}
          <button
            onClick={loadData}
            disabled={loading}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-200 text-xs font-bold border border-slate-700 transition-colors"
          >
            <RefreshCw className={`w-3.5 h-3.5 ${loading ? 'animate-spin' : ''}`} />
            Обновить
          </button>
        </div>
      </div>

      {/* Status Banner */}
      {statusMessage && (
        <div
          className={`rounded-xl px-4 py-2.5 border text-xs font-semibold flex items-center gap-2 ${
            statusMessage.kind === 'success'
              ? 'bg-emerald-950/40 border-emerald-700/50 text-emerald-300'
              : 'bg-rose-950/40 border-rose-700/50 text-rose-300'
          }`}
        >
          {statusMessage.kind === 'success' ? (
            <CheckCircle2 className="w-4 h-4 shrink-0" />
          ) : (
            <AlertTriangle className="w-4 h-4 shrink-0" />
          )}
          <span>{statusMessage.text}</span>
        </div>
      )}

      {/* Filters */}
      <div className="flex flex-wrap items-center justify-between gap-3 bg-slate-900/70 border border-slate-800 rounded-xl px-4 py-3">
        <div className="flex items-center gap-1.5">
          {(
            [
              ['all', 'Все'],
              ['voice', 'Реплики (TTS)'],
              ['music', 'Музыка (Lyria)'],
            ] as const
          ).map(([val, label]) => (
            <button
              key={val}
              onClick={() => setKindFilter(val)}
              className={`px-3 py-1 rounded-lg text-xs font-bold transition-colors ${
                kindFilter === val
                  ? 'bg-sky-500 text-slate-950'
                  : 'bg-slate-800/80 text-slate-300 hover:bg-slate-800'
              }`}
            >
              {label}
            </button>
          ))}
        </div>

        <div className="flex items-center gap-1.5">
          {(
            [
              ['all', 'Любой статус'],
              ['generated', 'Только с WAV'],
              ['pending', 'Ожидают'],
              ['approved', 'Одобрены'],
              ['rejected', 'Отклонены'],
            ] as const
          ).map(([val, label]) => (
            <button
              key={val}
              onClick={() => setStateFilter(val)}
              className={`px-2.5 py-1 rounded-lg text-xs font-semibold transition-colors ${
                stateFilter === val
                  ? 'bg-amber-400 text-slate-950'
                  : 'bg-slate-950 text-slate-400 border border-slate-800 hover:text-slate-200'
              }`}
            >
              {label}
            </button>
          ))}
        </div>
      </div>

      {/* Items List */}
      <div className="space-y-3">
        {visibleItems.map((item) => {
          const rec = data?.approvals?.approvals?.[item.key];
          const decision: AudioApprovalDecision = rec?.status ?? 'pending';
          const isSaving = savingKey === item.key;

          return (
            <div
              key={item.key}
              className={`bg-slate-900 border rounded-xl p-4 space-y-3 transition-colors ${
                decision === 'approved'
                  ? 'border-emerald-700/60'
                  : decision === 'rejected'
                    ? 'border-rose-700/60'
                    : 'border-slate-800'
              }`}
            >
              <div className="flex flex-wrap items-start justify-between gap-3">
                <div className="space-y-1">
                  <div className="flex flex-wrap items-center gap-2">
                    {item.kind === 'voice' ? (
                      <Mic className="w-4 h-4 text-cyan-400" />
                    ) : (
                      <Music className="w-4 h-4 text-violet-400" />
                    )}
                    <span className="text-sm font-black font-mono text-slate-100">{item.id}</span>
                    {item.speaker && (
                      <span className="px-2 py-0.5 rounded bg-cyan-950/60 border border-cyan-800/50 text-[11px] font-bold text-cyan-300">
                        {item.speaker} ({item.voice})
                      </span>
                    )}
                    {item.state && (
                      <span className="px-2 py-0.5 rounded bg-violet-950/60 border border-violet-800/50 text-[11px] font-bold text-violet-300">
                        state: {item.state}
                      </span>
                    )}
                    <span className="px-2 py-0.5 rounded bg-slate-950 border border-slate-800 text-[11px] font-mono text-slate-400">
                      {item.model}
                    </span>
                    {item.wavExists ? (
                      <span className="px-2 py-0.5 rounded bg-emerald-950/60 border border-emerald-700/50 text-[11px] font-bold text-emerald-300">
                        WAV готов {item.duration_sec !== null ? `· ${item.duration_sec.toFixed(2)}s` : ''}
                        {item.cost_usd !== null ? ` · $${item.cost_usd.toFixed(4)}` : ''}
                      </span>
                    ) : (
                      <span className="px-2 py-0.5 rounded bg-slate-800 text-[11px] text-slate-400">
                        Не сгенерирован
                      </span>
                    )}
                  </div>

                  {item.text && <p className="text-sm font-semibold text-slate-100">«{item.text}»</p>}
                  <p className="text-xs text-slate-400">{item.prompt}</p>
                  {item.source && <p className="text-[11px] font-mono text-slate-500">src: {item.source}</p>}
                </div>

                <div className="flex items-center gap-1.5">
                  {decision === 'approved' && (
                    <span className="inline-flex items-center gap-1 px-2.5 py-1 rounded-lg bg-emerald-500/15 border border-emerald-500/40 text-xs font-bold text-emerald-300">
                      <CheckCircle2 className="w-3.5 h-3.5" />
                      Одобрено
                    </span>
                  )}
                  {decision === 'rejected' && (
                    <span className="inline-flex items-center gap-1 px-2.5 py-1 rounded-lg bg-rose-500/15 border border-rose-500/40 text-xs font-bold text-rose-300">
                      <XCircle className="w-3.5 h-3.5" />
                      Отклонено
                    </span>
                  )}
                  {decision === 'pending' && (
                    <span className="inline-flex items-center gap-1 px-2.5 py-1 rounded-lg bg-slate-800 border border-slate-700 text-xs font-semibold text-slate-300">
                      <Clock className="w-3.5 h-3.5" />
                      Ожидает
                    </span>
                  )}
                </div>
              </div>

              {/* Player or CLI hint */}
              {item.wavExists ? (
                <audio controls preload="none" src={item.streamUrl} className="w-full h-9" />
              ) : (
                <div className="text-[11px] font-mono text-slate-500 bg-slate-950 border border-slate-800 rounded-lg px-3 py-1.5">
                  python Scripts/Tools/audio_gen.py {item.kind} --id {item.id}
                </div>
              )}

              {/* Notes + Actions */}
              <div className="flex flex-wrap items-center gap-2 pt-1">
                <input
                  type="text"
                  value={noteDrafts[item.key] ?? ''}
                  onChange={(e) => setNoteDrafts((prev) => ({ ...prev, [item.key]: e.target.value }))}
                  placeholder="Заметки дизайнера (интонация, темп, артефакты)..."
                  className="flex-1 min-w-[240px] bg-slate-950 border border-slate-800 rounded-lg px-3 py-1.5 text-xs text-slate-200 placeholder:text-slate-600 focus:outline-none focus:border-sky-500"
                />
                <button
                  disabled={isSaving}
                  onClick={() => submitReview(item, 'approved')}
                  className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-emerald-600 hover:bg-emerald-500 disabled:opacity-50 text-white text-xs font-bold transition-colors"
                >
                  {isSaving ? <Loader2 className="w-3.5 h-3.5 animate-spin" /> : <CheckCircle2 className="w-3.5 h-3.5" />}
                  Approve
                </button>
                <button
                  disabled={isSaving}
                  onClick={() => submitReview(item, 'rejected')}
                  className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-rose-600 hover:bg-rose-500 disabled:opacity-50 text-white text-xs font-bold transition-colors"
                >
                  <XCircle className="w-3.5 h-3.5" />
                  Reject
                </button>
                <button
                  disabled={isSaving}
                  onClick={() => submitReview(item, 'pending')}
                  className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-slate-800 hover:bg-slate-700 disabled:opacity-50 text-slate-300 text-xs font-semibold border border-slate-700 transition-colors"
                >
                  Pending
                </button>
              </div>
            </div>
          );
        })}

        {visibleItems.length === 0 && (
          <div className="bg-slate-900 border border-slate-800 rounded-xl p-6 text-center text-xs text-slate-500">
            Нет элементов под выбранные фильтры.
          </div>
        )}
      </div>
    </div>
  );
};
