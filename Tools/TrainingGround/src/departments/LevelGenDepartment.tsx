// Training Ground department «Генератор уровней» (docs/design/levelgen/LEVEL_GENERATOR.md): from a reference profile (the game
// designer's annotated map, Content/Data/LevelGen/refs), a seed and parameters the generator makes original greybox layouts IN
// THE BROWSER (0 tokens), the evaluator checks each (the mandatory route must be completable; mechanics, sightlines, ambush
// quality, profile similarity), the page shows the candidate's top-down render next to the reference image, the metrics and the
// critics' verdicts (Jev - typed judgments; the Claude API - sees both images, USD-capped; the server holds the keys). The
// designer accepts (-> Content/Data/LevelGen/accepted, what the UE builder reads) or rejects.
import React, { useEffect, useMemo, useRef, useState } from 'react';
import { CheckCircle2, AlertTriangle, XCircle, Play, Footprints, ThumbsUp, ThumbsDown, Bot, Boxes, Layers } from 'lucide-react';
import { LayoutView } from './LayoutView';
import type { FloorViewMode, LayoutViewHandle } from './LayoutView';
import { PropsPanel } from './PropsPanel';
import type { FormationConfigFile } from '../movement/config';
import { configValues } from '../movement/config';
import type { Vec2 } from '../movement/geometry';
import type { CriticVerdict } from '../levelgen/critic';
import type { Evaluation } from '../levelgen/evaluate';
import { evaluateLayout } from '../levelgen/evaluate';
import { generateLayout } from '../levelgen/generate';
import type { Profile } from '../levelgen/profile';
import type { GenLayout, GenParams } from '../levelgen/schema';
import { layersOf, mandatoryRoute } from '../levelgen/schema';

interface Candidate { layout: GenLayout; ev: Evaluation; simmed: boolean; verdicts: CriticVerdict[] }
type ProfileRow = { id: string; title: string; size_m: [number, number] };

const pct = (v?: number) => `${Math.round((v ?? 0) * 100)} %`;

export const LevelGenDepartment: React.FC = () => {
  const [activeTab, setActiveTab] = useState<'generator' | 'props'>('props');
  const [profiles, setProfiles] = useState<ProfileRow[]>([]);
  const [profileId, setProfileId] = useState('');
  const [profile, setProfile] = useState<Profile | null>(null);
  const [configFile, setConfigFile] = useState<FormationConfigFile | null>(null);
  const [seed, setSeed] = useState(1);
  const [count, setCount] = useState(3);
  const [params, setParams] = useState<GenParams>({ size_m: [177, 124], density: 1, variants: 3, verticality: 3 });
  const [candidates, setCandidates] = useState<Candidate[]>([]);
  const [sel, setSel] = useState(0);
  const [layer, setLayer] = useState('all');
  const [floorMode, setFloorMode] = useState<FloorViewMode>('all');
  const [showFiringLines, setShowFiringLines] = useState(true);
  const [busy, setBusy] = useState('');
  const [error, setError] = useState('');
  const [budgetUsd, setBudgetUsd] = useState(2);
  const [model, setModel] = useState('claude-opus-5-5');
  const [ledger, setLedger] = useState<{ spentUsd: number; calls: unknown[] }>({ spentUsd: 0, calls: [] });
  const [accepted, setAccepted] = useState<{ name: string; seed: number; profile: string; at: string }[]>([]);
  const [refOk, setRefOk] = useState(true);
  const view = useRef<LayoutViewHandle>(null);

  useEffect(() => {
    fetch('/api/movement/config').then((r) => r.json()).then(setConfigFile).catch(() => setError('Нет связи с сервером Training Ground'));
    fetch('/api/levelgen/profiles').then((r) => r.json()).then((d) => { const list: ProfileRow[] = d.profiles ?? []; setProfiles(list); setProfileId((s) => s || list[0]?.id || ''); }).catch(() => {});
    fetch('/api/levelgen/ledger').then((r) => r.json()).then(setLedger).catch(() => {});
    fetch('/api/levelgen/accepted').then((r) => r.json()).then((d) => setAccepted(d.accepted ?? [])).catch(() => {});
  }, []);
  useEffect(() => {
    if (!profileId) return;
    fetch(`/api/levelgen/profile?id=${encodeURIComponent(profileId)}`).then((r) => r.json()).then((p: Profile) => { setProfile(p); setParams((q) => ({ ...q, size_m: [p.bounds.max[0], p.bounds.max[1]] })); }).catch(() => {});
  }, [profileId]);

  const config = useMemo(() => (configFile ? configValues(configFile) : null), [configFile]);
  const cur = candidates[sel] ?? null;
  const layers = useMemo(() => (cur ? ['all', ...layersOf(cur.layout)] : ['all']), [cur]);
  const marks = useMemo<Vec2[]>(() => {
    if (!cur) return [];
    const r = mandatoryRoute(cur.layout);
    return cur.ev.route.legs.filter((x) => !x.ok).flatMap((x) => [r.points[x.index - 1], r.points[x.index]]);
  }, [cur]);

  const generate = () => {
    if (!profile || !config) return;
    setBusy('Генерация...'); setError('');
    setTimeout(() => {
      try {
        const out: Candidate[] = [];
        for (let i = 0; i < count; i++) {
          const layout = generateLayout({ profile, seed: seed + i, params });
          out.push({ layout, ev: evaluateLayout({ layout, profile, config, noSim: true }), simmed: false, verdicts: [] });
        }
        setCandidates(out); setSel(0); setLayer('all');
      } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
      setBusy('');
    }, 20);
  };

  const walk = () => {
    if (!cur || !profile || !config) return;
    setBusy('Отряд идёт по маршруту (симулятор)...');
    setTimeout(() => {
      try {
        const ev = evaluateLayout({ layout: cur.layout, profile, config });
        setCandidates((cs) => cs.map((c, i) => (i === sel ? { ...c, ev, simmed: true } : c)));
      } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
      setBusy('');
    }, 20);
  };

  const critic = async (client: 'claude' | 'jev' | 'mock') => {
    if (!cur) return;
    setBusy(client === 'claude' ? `Критик Claude (${model}, бюджет $${budgetUsd})...` : client === 'jev' ? 'Jev оценивает...' : 'Эвристика...'); setError('');
    try {
      const candidatePng = client === 'claude' ? view.current?.toPngBase64() ?? '' : '';
      const r = await fetch('/api/levelgen/critic', { method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name: cur.layout.name, profile: cur.layout.profile, layout: cur.layout, evaluation: cur.ev, candidatePng, client, budgetUsd, model }) });
      const d = await r.json();
      if (!r.ok) throw new Error(d.error ?? r.statusText);
      setCandidates((cs) => cs.map((c, i) => (i === sel ? { ...c, verdicts: [...c.verdicts, d.verdict] } : c)));
      if (d.ledger) setLedger(d.ledger);
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    setBusy('');
  };

  const accept = async () => {
    if (!cur) return;
    setBusy('Сохраняю...');
    try {
      const r = await fetch('/api/levelgen/accept', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ layout: cur.layout, evaluation: cur.ev, verdicts: cur.verdicts }) });
      const d = await r.json();
      if (!r.ok) throw new Error(d.error ?? r.statusText);
      setAccepted((a) => [...a.filter((x) => x.name !== cur.layout.name), { name: cur.layout.name, seed: cur.layout.seed, profile: cur.layout.profile, at: new Date().toISOString() }]);
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    setBusy('');
  };

  const reject = async () => {
    if (!cur) return;
    fetch('/api/levelgen/reject', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ name: cur.layout.name, seed: cur.layout.seed, profile: cur.layout.profile }) }).catch(() => {});
    setCandidates((cs) => cs.filter((_, i) => i !== sel)); setSel(0);
  };

  const num = (label: string, value: number, set: (v: number) => void, step = 1, min = 0, max = 1000) => (
    <label className="flex items-center justify-between gap-2 text-xs text-slate-300">{label}
      <input type="number" value={value} step={step} min={min} max={max} onChange={(e) => set(Number(e.target.value))} className="w-24 bg-slate-950 border border-slate-700 rounded px-2 py-1 text-slate-100" />
    </label>
  );
  const verdictIcon = (v: CriticVerdict) => (v.fits === null ? <AlertTriangle size={14} className="text-amber-300" /> : v.fits ? <CheckCircle2 size={14} className="text-emerald-400" /> : <XCircle size={14} className="text-rose-400" />);

  return (
    <div className="flex flex-col min-h-full">
      <div className="bg-slate-950/80 border-b border-slate-800 px-6 py-2.5 flex flex-wrap items-center justify-between gap-3">
        <div className="flex items-center gap-2">
          <button
            onClick={() => setActiveTab('props')}
            className={`inline-flex items-center gap-2 px-3 py-1.5 rounded-lg text-xs font-semibold transition-colors ${
              activeTab === 'props'
                ? 'bg-cyan-600 text-slate-950 font-bold'
                : 'bg-slate-900 border border-slate-800 text-slate-300 hover:bg-slate-800'
            }`}
          >
            <Layers size={14} />
            Пропсы уровней (T5)
          </button>
          <button
            onClick={() => setActiveTab('generator')}
            className={`inline-flex items-center gap-2 px-3 py-1.5 rounded-lg text-xs font-semibold transition-colors ${
              activeTab === 'generator'
                ? 'bg-cyan-600 text-slate-950 font-bold'
                : 'bg-slate-900 border border-slate-800 text-slate-300 hover:bg-slate-800'
            }`}
          >
            <Boxes size={14} />
            Генератор раскладок
          </button>
        </div>
        <span className="text-[11px] text-slate-500 font-mono">
          codex.levelgen_props/1 &bull; docs/design/levelgen/PROPS_LAYERS.md
        </span>
      </div>

      {activeTab === 'props' ? (
        <PropsPanel />
      ) : (
        <div className="p-6 grid gap-6 xl:grid-cols-[300px_minmax(0,1fr)_380px]">
          <section className="bg-slate-900 border border-slate-800 rounded-2xl p-4 flex flex-col gap-3">
            <h3 className="text-xs font-bold tracking-widest uppercase text-slate-400">Генерация</h3>
            {error && <p className="text-sm text-rose-300">{error}</p>}
            <label className="text-xs text-slate-300 flex flex-col gap-1">Эталон (профиль)
              <select value={profileId} onChange={(e) => setProfileId(e.target.value)} className="bg-slate-950 border border-slate-700 rounded px-2 py-1 text-slate-100">
                {profiles.map((p) => <option key={p.id} value={p.id}>{p.id} ({p.size_m[0]} x {p.size_m[1]} м)</option>)}
              </select>
            </label>
            {num('Seed', seed, setSeed)}
            {num('Кандидатов', count, (v) => setCount(Math.max(1, Math.min(12, v))), 1, 1, 12)}
            {num('Ширина, м', params.size_m[0], (v) => setParams((p) => ({ ...p, size_m: [v, p.size_m[1]] })), 1, 60, 400)}
            {num('Глубина, м', params.size_m[1], (v) => setParams((p) => ({ ...p, size_m: [p.size_m[0], v] })), 1, 60, 400)}
            {num('Плотность укрытий', params.density, (v) => setParams((p) => ({ ...p, density: v })), 0.1, 0, 3)}
            {num('Вариантов маршрута', params.variants, (v) => setParams((p) => ({ ...p, variants: v })), 1, 0, 5)}
            <label className="flex items-center justify-between gap-2 text-xs text-slate-300">Вертикальность
              <select value={params.verticality} onChange={(e) => setParams((p) => ({ ...p, verticality: Number(e.target.value) as GenParams['verticality'] }))} className="bg-slate-950 border border-slate-700 rounded px-2 py-1 text-slate-100">
                <option value={0}>0 — плоско</option><option value={1}>1 — лестницы и крыши</option><option value={2}>2 — + лестничные клетки, этажи</option><option value={3}>3 — + проломы в руине</option>
              </select>
            </label>
            <button onClick={generate} disabled={!profile || !config || !!busy} className="inline-flex items-center justify-center gap-2 px-3 py-2 rounded-lg bg-cyan-600 hover:bg-cyan-500 text-slate-950 font-semibold text-sm disabled:opacity-50"><Play size={15} />Сгенерировать</button>
            {busy && <p className="text-xs text-cyan-300">{busy}</p>}
            <h4 className="text-[11px] font-bold tracking-widest uppercase text-slate-400 mt-2">Кандидаты</h4>
            {candidates.map((c, i) => (
              <button key={c.layout.name} onClick={() => { setSel(i); setLayer('all'); }} className={`text-left px-3 py-2 rounded-lg border text-xs flex items-center justify-between gap-2 ${sel === i ? 'border-cyan-600 bg-slate-800' : 'border-slate-800 hover:bg-slate-800/60'}`}>
                <span className="font-mono text-slate-200">seed {c.layout.seed}</span>
                <span className="flex items-center gap-2">
                  <span className="text-slate-400">{c.ev.score}</span>
                  {c.ev.route.completable ? <CheckCircle2 size={13} className="text-emerald-400" /> : <XCircle size={13} className="text-rose-400" />}
                  {c.verdicts.map((v, k) => <span key={k}>{verdictIcon(v)}</span>)}
                </span>
              </button>
            ))}
            <h4 className="text-[11px] font-bold tracking-widest uppercase text-slate-400 mt-2">Принятые (Content/Data/LevelGen/accepted)</h4>
            {accepted.length === 0 && <p className="text-xs text-slate-500">Пока нет.</p>}
            {accepted.map((a) => <p key={a.name} className="text-xs font-mono text-emerald-300">{a.name}</p>)}
            <p className="text-[11px] text-slate-500 mt-1">Сборка в UE: <span className="font-mono">Scripts/Editor/build_levelgen_map.py</span> (см. LEVEL_GENERATOR.md §7).</p>
          </section>

          <section className="bg-slate-900 border border-slate-800 rounded-2xl p-4 flex flex-col gap-3 min-w-0">
            <div className="flex flex-wrap items-center gap-2">
              <h3 className="text-xs font-bold tracking-widest uppercase text-slate-400 mr-1">{cur ? cur.layout.name : 'Кандидат'}</h3>
              <div className="inline-flex rounded-lg bg-slate-950 border border-slate-700 p-0.5 text-[11px]">
                {([['all', 'Все'], ['ground', 'Земля'], ['1', 'Этаж 1'], ['2', 'Этаж 2'], ['roof', 'Крыша']] as const).map(([mode, label]) => (
                  <button
                    key={mode}
                    type="button"
                    onClick={() => { setFloorMode(mode); setLayer('all'); }}
                    className={`px-2 py-0.5 rounded ${layer === 'all' && floorMode === mode ? 'bg-cyan-700 text-white' : 'text-slate-300 hover:text-white'}`}
                  >
                    {label}
                  </button>
                ))}
              </div>
              <select value={layer} onChange={(e) => { setLayer(e.target.value); if (e.target.value !== 'all') setFloorMode('all'); }} className="bg-slate-950 border border-slate-700 rounded px-2 py-1 text-xs text-slate-100">
                {layers.map((l) => <option key={l} value={l}>{l === 'all' ? 'все слои' : l}</option>)}
              </select>
              <button
                type="button"
                onClick={() => setShowFiringLines((v) => !v)}
                className={`px-2.5 py-1 rounded-lg border text-xs ${showFiringLines ? 'bg-rose-900/60 border-rose-500 text-rose-200' : 'bg-slate-800 border-slate-700 text-slate-400 hover:text-slate-200'}`}
              >
                Линии огня
              </button>
              <button onClick={walk} disabled={!cur || !!busy} className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-slate-800 border border-slate-700 text-xs text-slate-200 hover:bg-slate-700 disabled:opacity-50"><Footprints size={13} />Прогнать отряд (симулятор)</button>
              <button onClick={accept} disabled={!cur || !!busy || !cur.ev.route.completable} className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-emerald-700 text-xs text-white hover:bg-emerald-600 disabled:opacity-50"><ThumbsUp size={13} />Принять</button>
              <button onClick={reject} disabled={!cur || !!busy} className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-rose-800 text-xs text-white hover:bg-rose-700 disabled:opacity-50"><ThumbsDown size={13} />Отклонить</button>
            </div>
            <div className="grid gap-3 lg:grid-cols-2">
              <div>
                <p className="text-[11px] text-slate-500 mb-1">Кандидат (сверху; синий — маршрут, жёлтый — лестницы, голубой/янтарный — окна/бойницы, зелёный — крыша/парапет, фиол. — перелаз, пунктир — линии огня)</p>
                <LayoutView ref={view} layout={cur?.layout ?? null} layer={layer} floorMode={floorMode} firingLines={cur?.ev.tactical?.firingLines.lines} showFiringLines={showFiringLines} height={520} marks={marks} />
              </div>
              <div>
                <p className="text-[11px] text-slate-500 mb-1">Эталон (карта дизайнера, развёрнутая сверху; синее — его маршрут, жёлтое — лестницы, зелёный круг — старт)</p>
                {refOk ? <img src="/api/levelgen/ref-image" alt="reference" onError={() => setRefOk(false)} className="rounded-xl border border-slate-800 w-full" style={{ maxHeight: 520, objectFit: 'contain', background: '#191c22' }} />
                  : <p className="text-xs text-amber-300">Эталонная картинка недоступна (Saved/MapRefs или python/PIL).</p>}
              </div>
            </div>
          </section>

          <section className="bg-slate-900 border border-slate-800 rounded-2xl p-4 flex flex-col gap-3 min-w-0">
            <h3 className="text-xs font-bold tracking-widest uppercase text-slate-400">Оценка</h3>
            {!cur && <p className="text-sm text-slate-500">Сгенерируйте кандидатов.</p>}
            {cur && (<>
              <p className={`text-sm flex items-center gap-2 ${cur.ev.route.completable ? 'text-emerald-400' : 'text-rose-400'}`}>
                {cur.ev.route.completable ? <CheckCircle2 size={16} /> : <XCircle size={16} />}
                Маршрут {cur.ev.route.completable ? 'проходим' : `НЕ проходим: ${cur.ev.route.fail}`} — {cur.ev.route.length_m} м, слои {cur.ev.route.layers.join(' / ')}
              </p>
              <ul className="text-xs text-slate-300 flex flex-col gap-1">
                {cur.ev.variants.map((v) => <li key={v.id} className="flex items-center gap-2">{v.completable ? <CheckCircle2 size={12} className="text-emerald-400" /> : <XCircle size={12} className="text-rose-400" />}{v.id}: {v.length_m} м{v.fail ? ` — ${v.fail}` : ''}{v.intent === 'kill_zone' ? <span className="text-rose-300"> — зона убийства по замыслу (не дефект)</span> : null}</li>)}
              </ul>
              <table className="text-xs text-slate-300">
                <tbody>
                  <tr><td className="pr-2 text-slate-500">Итог</td><td className="font-mono">{cur.ev.score} (сходство с эталоном {cur.ev.similarity.score})</td></tr>
                  <tr><td className="pr-2 text-slate-500">Рельеф (сим)</td><td className="font-mono">{cur.simmed ? Object.entries(cur.ev.mechanics.terrain).map(([k, v]) => `${k} ${pct(v)}`).join(', ') : 'прогоните отряд'}</td></tr>
                  <tr><td className="pr-2 text-slate-500">Строй (сим)</td><td className="font-mono">{cur.simmed ? Object.entries(cur.ev.mechanics.formation).map(([k, v]) => `${k} ${pct(v)}`).join(', ') : '—'}</td></tr>
                  <tr><td className="pr-2 text-slate-500">Механики</td><td className="font-mono">углы {cur.ev.mechanics.cornerWatches}, двери {cur.ev.mechanics.doorways}, лестницы {cur.ev.mechanics.ladderUses}, лестн. клетки {cur.ev.mechanics.stairUses}, проломы {cur.ev.mechanics.holeUses}, крыши {cur.ev.mechanics.roofs}, этажи {cur.ev.mechanics.floors}</td></tr>
                  <tr><td className="pr-2 text-slate-500">Обзор</td><td className="font-mono">p50 {cur.ev.sightlines.p50_m} м, p90 {cur.ev.sightlines.p90_m} м, макс {cur.ev.sightlines.max_m} м, короткие {pct(cur.ev.sightlines.short_share)}, коридоры ≥ 80 м: {cur.ev.sightlines.lanes_over_80m}</td></tr>
                  <tr><td className="pr-2 text-slate-500">Вид с крыш</td><td className="font-mono">{(cur.ev.roofViews ?? []).map((r) => `${r.id} +${r.level}: ср. ${r.mean_m} м, макс ${r.longest_m} м`).join('; ') || '—'}</td></tr>
                  <tr><td className="pr-2 text-slate-500">Засады</td><td className="font-mono">{cur.ev.ambush.concealed} / {cur.ev.ambush.points} скрытых точек ({cur.ev.ambush.per100m} на 100 м), скоординированная возможна на {pct(cur.ev.ambush.coordinatedShare)} маршрута</td></tr>
                  {cur.ev.tactical && (
                    <tr><td className="pr-2 text-slate-500">Тактика зданий</td><td className="font-mono">оценка {cur.ev.tactical.score}, комнаты {cur.ev.tactical.roomsReachable}/{cur.ev.tactical.roomsTotal}, заблок. окна {cur.ev.tactical.blockedWindows.length}, линии огня {cur.ev.tactical.firingLines.count} ({cur.ev.tactical.firingLines.minLength_m}–{cur.ev.tactical.firingLines.maxLength_m} м), укрытия {pct(cur.ev.tactical.routeCover.share)}, перелазы {cur.ev.tactical.mantle.valid}</td></tr>
                  )}
                </tbody>
              </table>
              <details className="text-xs text-slate-300"><summary className="cursor-pointer text-slate-400">Сходство с эталоном по метрикам</summary>
                <ul className="mt-1 flex flex-col gap-0.5">{cur.ev.similarity.metrics.map((m) => <li key={m.key} className={m.score < 1 ? 'text-amber-300' : ''}>{m.key}: {m.value}{m.target ? ` [${m.target[0]}..${m.target[1]}]` : ''}{m.score < 1 ? ` ×${m.score}` : ''}</li>)}</ul>
              </details>
              <h4 className="text-[11px] font-bold tracking-widest uppercase text-slate-400 mt-1">Вердикты</h4>
              <div className="flex flex-wrap items-center gap-2">
                <button onClick={() => critic('jev')} disabled={!!busy} className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-slate-800 border border-slate-700 text-xs text-slate-200 hover:bg-slate-700 disabled:opacity-50"><Bot size={13} />Jev</button>
                <button onClick={() => critic('claude')} disabled={!!busy} className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-slate-800 border border-slate-700 text-xs text-slate-200 hover:bg-slate-700 disabled:opacity-50"><Bot size={13} />Критик Claude</button>
                <button onClick={() => critic('mock')} disabled={!!busy} className="px-3 py-1.5 rounded-lg bg-slate-800 border border-slate-700 text-xs text-slate-400 hover:bg-slate-700 disabled:opacity-50">эвристика</button>
                <label className="text-xs text-slate-400 flex items-center gap-1">бюджет $<input type="number" value={budgetUsd} step={0.5} min={0} max={50} onChange={(e) => setBudgetUsd(Number(e.target.value))} className="w-16 bg-slate-950 border border-slate-700 rounded px-1 py-0.5 text-slate-100" /></label>
                <select value={model} onChange={(e) => setModel(e.target.value)} className="bg-slate-950 border border-slate-700 rounded px-2 py-1 text-xs text-slate-100">
                  <option value="claude-opus-5-5">claude-opus-5-5</option><option value="claude-fable-5-1">claude-fable-5-1</option><option value="claude-sonnet-5-5">claude-sonnet-5-5</option>
                </select>
                <span className="text-[11px] text-slate-500">потрачено ${ledger.spentUsd.toFixed(4)}</span>
              </div>
              {cur.verdicts.length === 0 && <p className="text-xs text-slate-500">Вердиктов нет. Jev — типизированные суждения по метрикам (бесплатно); критик Claude видит оба изображения и метрики (платно, в пределах бюджета).</p>}
              <ul className="text-xs flex flex-col gap-2">
                {cur.verdicts.map((v, k) => (
                  <li key={k} className="bg-slate-950 border border-slate-800 rounded-lg p-2 flex flex-col gap-1">
                    <span className="flex items-center gap-2 font-semibold">{verdictIcon(v)}{v.client} ({v.model}{v.offline ? ', офлайн' : ''}): {v.fits === null ? 'без вердикта' : v.fits ? 'ПОДХОДИТ' : 'НЕ ПОДХОДИТ'}{v.confidence !== null ? ` ${Math.round(v.confidence * 100)} %` : ''}{v.quality !== null ? `, качество ${v.quality}/5` : ''} <span className="text-slate-500 font-normal">${v.costUsd.toFixed(4)}</span></span>
                    {v.note && <span className="text-amber-300">{v.note}</span>}
                    {v.reasons.map((r, i) => <span key={i} className="text-slate-300">— {r}</span>)}
                    {v.fixes.length > 0 && <span className="text-cyan-300">исправить: {v.fixes.join('; ')}</span>}
                  </li>
                ))}
              </ul>
              <details className="text-[11px] text-slate-400"><summary className="cursor-pointer">Отчёт (≤ 30 строк)</summary><pre className="whitespace-pre-wrap bg-slate-950 border border-slate-800 rounded-lg p-2 mt-1 text-slate-300">{cur.ev.summary.join('\n')}</pre></details>
            </>)}
          </section>
        </div>
      )}
    </div>
  );
};
