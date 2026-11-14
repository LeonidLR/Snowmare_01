import React, { useEffect, useMemo, useState } from 'react';
import { CheckCircle2, XCircle, Clock, Volume2, Music, Mic, RefreshCw, Loader2, DollarSign } from 'lucide-react';
import {
  type AudioApprovalDecision,
  type AudioApprovalsFile,
  type AudioItemEntry,
  type AudioListResponse,
  extractFilterOptions,
  filterAudioItems,
  itemCueName,
  summarizeAudioCounts,
} from '../utils/audioReview';

export const AudioReviewPanel: React.FC = () => {
  const [data, setData] = useState<AudioListResponse | null>(null);
  const [loading, setLoading] = useState<boolean>(true);
  const [error, setError] = useState<string | null>(null);
  const [kindFilter, setKindFilter] = useState<'all' | 'voice' | 'music'>('all');
  const [statusFilter, setStatusFilter] = useState<'all' | AudioApprovalDecision>('all');
  const [speakerFilter, setSpeakerFilter] = useState<string>('all');
  const [cueFilter, setCueFilter] = useState<string>('all');
  const [search, setSearch] = useState<string>('');
  const [noteDrafts, setNoteDrafts] = useState<Record<string, string>>({});
  const [savingKey, setSavingKey] = useState<string | null>(null);
  const [toast, setToast] = useState<string | null>(null);

  const loadAudioList = async () => {
    setLoading(true);
    setError(null);
    try {
      const res = await fetch('/api/audio/list');
      if (!res.ok) {
        throw new Error(`HTTP ${res.status}`);
      }
      const payload: AudioListResponse = await res.json();
      setData(payload);
      const drafts: Record<string, string> = {};
      for (const item of payload.items) {
        drafts[item.key] = payload.approvals.approvals[item.key]?.notes ?? '';
      }
      setNoteDrafts(drafts);
    } catch (err: unknown) {
      setError(`Не удалось загрузить каталог аудио: ${String(err)}`);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadAudioList();
  }, []);

  const items = data?.items ?? [];
  const approvals: AudioApprovalsFile = data?.approvals ?? { version: 1, approvals: {} };
  const summary = useMemo(() => summarizeAudioCounts(items, approvals), [items, approvals]);
  const filterOpts = useMemo(() => extractFilterOptions(items), [items]);

  const visibleItems = useMemo(
    () =>
      filterAudioItems(items, approvals, {
        kind: kindFilter,
        status: statusFilter,
        speaker: speakerFilter,
        cue: cueFilter,
        query: search,
      }),
    [items, approvals, kindFilter, statusFilter, speakerFilter, cueFilter, search]
  );

  const submitReview = async (item: AudioItemEntry, status: AudioApprovalDecision) => {
    setSavingKey(item.key);
    setToast(null);
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
      const body = await res.json();
      if (!res.ok) {
        throw new Error(body.error || `HTTP ${res.status}`);
      }
      setData((prev) => (prev ? { ...prev, approvals: body.approvals } : prev));
      setToast(`${item.id}: ${status}`);
      setTimeout(() => setToast(null), 2500);
    } catch (err: unknown) {
      setError(`Ошибка сохранения ревью (${item.id}): ${String(err)}`);
    } finally {
      setSavingKey(null);
    }
  };

  if (loading && !data) {
    return (
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-8 flex items-center justify-center gap-3 text-slate-400">
        <Loader2 className="w-5 h-5 animate-spin text-sky-400" />
        <span className="text-sm font-mono">Загрузка аудио-каталога...</span>
      </div>
    );
  }

  return (
    <div className="flex flex-col gap-4">
      {/* Header & Summary */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 flex flex-wrap items-center justify-between gap-4">
        <div className="flex items-center gap-3">
          <Volume2 className="w-6 h-6 text-sky-400" />
          <div>
            <h2 className="text-sm font-bold text-slate-100">Аудио-ревью (Gemini TTS + Lyria)</h2>
            <p className="text-xs text-slate-400">
              Каталог: <code className="text-slate-300">{data?.catalogDir ?? 'Saved/Orchestrator/audio_v1'}/catalog.json</code>
              {' · '}Одобрения: <code className="text-slate-300">{data?.approvalsFile ?? 'audio_approvals.json'}</code>
            </p>
          </div>
        </div>

        <div className="flex flex-wrap items-center gap-3 text-xs font-mono">
          <span className="px-2.5 py-1 rounded bg-slate-800 border border-slate-700 text-slate-200">
            WAV: {summary.generated}/{summary.total}
          </span>
          <span className="px-2.5 py-1 rounded bg-emerald-950/70 border border-emerald-700/50 text-emerald-300">
            Одобрено: {summary.approved}
          </span>
          <span className="px-2.5 py-1 rounded bg-rose-950/70 border border-rose-700/50 text-rose-300">
            Отклонено: {summary.rejected}
          </span>
          <span className="px-2.5 py-1 rounded bg-amber-950/60 border border-amber-700/40 text-amber-300">
            Ожидают: {summary.pending}
          </span>
          <span className="px-2.5 py-1 rounded bg-slate-800 border border-slate-700 text-cyan-300 inline-flex items-center gap-1">
            <DollarSign className="w-3.5 h-3.5" />
            {(data?.spendSummary.total_usd ?? 0).toFixed(4)}
          </span>
          <button
            onClick={loadAudioList}
            className="flex items-center gap-1.5 px-3 py-1 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-200 border border-slate-700 transition-colors"
          >
            <RefreshCw className={`w-3.5 h-3.5 ${loading ? 'animate-spin' : ''}`} />
            Обновить
          </button>
        </div>
      </div>

      {error && (
        <div className="bg-rose-950/60 border border-rose-700/60 text-rose-200 text-xs rounded-xl px-4 py-3">
          {error}
        </div>
      )}
      {toast && (
        <div className="bg-emerald-950/70 border border-emerald-700/60 text-emerald-200 text-xs rounded-xl px-4 py-2 font-mono">
          Сохранено: {toast}
        </div>
      )}

      {/* Filters */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-3 flex flex-wrap items-center gap-2">
        <div className="flex items-center gap-1 bg-slate-950 p-1 rounded-lg border border-slate-800 text-xs">
          {(['all', 'voice', 'music'] as const).map((k) => (
            <button
              key={k}
              onClick={() => setKindFilter(k)}
              className={`px-2.5 py-1 rounded font-semibold transition-colors ${
                kindFilter === k ? 'bg-sky-500 text-slate-950' : 'text-slate-400 hover:text-slate-200'
              }`}
            >
              {k === 'all' ? 'Все' : k === 'voice' ? 'Речь (VO)' : 'Музыка'}
            </button>
          ))}
        </div>

        <div className="flex items-center gap-1 bg-slate-950 p-1 rounded-lg border border-slate-800 text-xs">
          {(['all', 'pending', 'approved', 'rejected'] as const).map((st) => (
            <button
              key={st}
              onClick={() => setStatusFilter(st)}
              className={`px-2.5 py-1 rounded font-semibold transition-colors ${
                statusFilter === st ? 'bg-amber-400 text-slate-950' : 'text-slate-400 hover:text-slate-200'
              }`}
            >
              {st === 'all'
                ? 'Все статусы'
                : st === 'pending'
                  ? 'Ожидают'
                  : st === 'approved'
                    ? 'Одобрено'
                    : 'Отклонено'}
            </button>
          ))}
        </div>

        <select
          aria-label="Фильтр по спикеру"
          value={speakerFilter}
          onChange={(e) => {
            setSpeakerFilter(e.target.value);
            if (e.target.value !== 'all') setCueFilter('all');
          }}
          className="bg-slate-950 border border-slate-800 rounded-lg px-2.5 py-1.5 text-xs text-slate-200 focus:outline-none focus:border-sky-500"
        >
          <option value="all">Все спикеры ({filterOpts.speakers.length})</option>
          {filterOpts.speakers.map((sp) => (
            <option key={sp} value={sp}>
              Спикер: {sp}
            </option>
          ))}
        </select>

        <select
          aria-label="Фильтр по cue"
          value={cueFilter}
          onChange={(e) => {
            setCueFilter(e.target.value);
            if (e.target.value !== 'all') setSpeakerFilter('all');
          }}
          className="bg-slate-950 border border-slate-800 rounded-lg px-2.5 py-1.5 text-xs text-slate-200 focus:outline-none focus:border-sky-500"
        >
          <option value="all">Все cues ({filterOpts.cues.length})</option>
          {filterOpts.cues.map((cue) => (
            <option key={cue} value={cue}>
              Cue: {cue}
            </option>
          ))}
        </select>

        <input
          type="text"
          value={search}
          onChange={(e) => setSearch(e.target.value)}
          placeholder="Поиск (id, текст, спикер, cue, промпт)..."
          className="flex-1 min-w-[200px] bg-slate-950 border border-slate-800 rounded-lg px-3 py-1.5 text-xs text-slate-200 placeholder:text-slate-500 focus:outline-none focus:border-sky-500"
        />
      </div>

      {/* Items List */}
      <div className="flex flex-col gap-3">
        {visibleItems.map((item) => {
          const rec = approvals.approvals[item.key];
          const decision: AudioApprovalDecision = rec?.status ?? 'pending';
          const isSaving = savingKey === item.key;
          const cueLabel = itemCueName(item);

          return (
            <div
              key={item.key}
              className="bg-slate-900 border border-slate-800 rounded-xl p-4 flex flex-col gap-3"
            >
              <div className="flex flex-wrap items-start justify-between gap-3">
                <div className="flex flex-col gap-1">
                  <div className="flex flex-wrap items-center gap-2">
                    {item.kind === 'voice' ? (
                      <Mic className="w-4 h-4 text-sky-400" />
                    ) : (
                      <Music className="w-4 h-4 text-violet-400" />
                    )}
                    <span className="font-mono text-sm font-bold text-slate-100">{item.id}</span>
                    <span className="px-2 py-0.5 rounded bg-slate-800 text-[11px] font-mono text-slate-300">
                      {item.model}
                    </span>
                    {item.speaker && (
                      <span className="px-2 py-0.5 rounded bg-sky-950/70 border border-sky-800/60 text-[11px] font-mono text-sky-300">
                        {item.speaker} ({item.voice})
                      </span>
                    )}
                    {cueLabel && (
                      <span className="px-2 py-0.5 rounded bg-violet-950/70 border border-violet-800/60 text-[11px] font-mono text-violet-300">
                        cue: {cueLabel}{item.variant ? ` · ${item.variant}` : ''}
                      </span>
                    )}
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

              {item.wavExists ? (
                <audio controls preload="none" src={item.streamUrl} className="w-full h-9" />
              ) : (
                <div className="text-[11px] font-mono text-slate-500 bg-slate-950 border border-slate-800 rounded-lg px-3 py-1.5">
                  python Scripts/Tools/audio_gen.py {item.kind} --id {item.id}
                </div>
              )}

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
