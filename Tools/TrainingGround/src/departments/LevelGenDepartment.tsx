// Training Ground department «Генератор уровней» (docs/design/levelgen/LEVEL_GENERATOR.md): from a reference profile (the game
// designer's annotated map, Content/Data/LevelGen/refs), a seed and parameters the generator makes original greybox layouts IN
// THE BROWSER (0 tokens), the evaluator checks each (the mandatory route must be completable; mechanics, sightlines, ambush
// quality, profile similarity), the page shows the candidate's top-down render next to the reference image, the metrics and the
// critics' verdicts (Jev - typed judgments; the Claude API - sees both images, USD-capped; the server holds the keys). The
// designer accepts (-> Content/Data/LevelGen/accepted, what the UE builder reads) or rejects.
import React, { useEffect, useMemo, useRef, useState } from 'react';
import { CheckCircle2, AlertTriangle, XCircle, Play, Footprints, ThumbsUp, ThumbsDown, Bot, Boxes, Layers, Shuffle, Undo2, Redo2, Lock, FolderOpen, Eraser, Save } from 'lucide-react';
import { LayoutView } from './LayoutView';
import type { FloorViewMode, LayoutViewHandle } from './LayoutView';
import { PropsPanel } from './PropsPanel';
import { RegionOverlay } from './RegionOverlay';
import type { RegionMode } from './RegionOverlay';
import type { History } from '../levelgen/regenHistory';
import { canRedo, canUndo, historyInit, historyPush, historyRedo, historyUndo } from '../levelgen/regenHistory';
import type { BlockMode, RegionRegenResult } from '../levelgen/regionRegen';
import type { ExpandResult, SeamCheck } from '../levelgen/expand';
import { checkSeams, expandLayout, expansionSize } from '../levelgen/expand';
import type { PlaceObjectivesResult } from '../levelgen/objectives';
import { checkObjectives, defaultGoalCount, placeObjectives } from '../levelgen/objectives';
import { V2 } from '../levelgen/blockTemplates';
import { addLock, DEFAULT_MAX_RETRIES, regenerateRegion, regenStateOf, removeLock, selectRegion } from '../levelgen/regionRegen';
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

/** `key` = the slot's history key (the first layout name: an expansion renames the map to <name>_x<N>). */
interface Candidate { layout: GenLayout; ev: Evaluation; simmed: boolean; verdicts: CriticVerdict[]; key?: string }
type ProfileRow = { id: string; title: string; size_m: [number, number] };

const pct = (v?: number) => `${Math.round((v ?? 0) * 100)} %`;
/** Block types (levelgen_v2.json templates.catalog + the classic-only ones) as the designer reads them. */
const BLOCK_LABEL: Record<string, string> = {
  open_square: 'площадь', junkyard_field: 'свалка', one_entry_compound: 'двор с одним входом', industrial_hall: 'цех', warehouse: 'склад с двором',
  small_office: 'контора', facade: 'глухой фасад', ladder_overwatch: 'вышка с лестницей', tank_farm: 'резервуары', fenced_lot: 'огороженная площадка',
  two_storey: 'двухэтажка', boiler_yard: 'котельный двор', dead_end_square: 'площадь с тупиком', ruined_two_level: 'руина', ruin_annex: 'пристройка руины',
  steel_works: 'сталелитейный цех', racks: 'стеллажи',
};
const blockLabel = (t: string) => BLOCK_LABEL[t] ?? t;

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
  // region re-roll + locked districts (levelgen v2 stage 1, task 070; docs/port/LEVELGEN_V2_STAGE1_REPORT.md)
  const [regionMode, setRegionMode] = useState<RegionMode>('off');
  const [zone, setZone] = useState<Vec2[] | null>(null);
  const [rerollSeed, setRerollSeed] = useState(1001);
  const [maxRetries, setMaxRetries] = useState(DEFAULT_MAX_RETRIES);
  const [lockName, setLockName] = useState('');
  const [regenReport, setRegenReport] = useState<RegionRegenResult | null>(null);
  const [histories, setHistories] = useState<Record<string, History<GenLayout>>>({});
  const [blockMode, setBlockMode] = useState<BlockMode>('types');

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
        setHistories({}); setZone(null); setRegenReport(null);
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

  // ---- region re-roll + locks (task 070) -------------------------------------------------------------------------------
  const curKey = cur?.key ?? cur?.layout.name ?? '';
  const history = cur ? histories[curKey] ?? historyInit(cur.layout, 'исходная') : null;
  const profileOk = !!cur && !!profile && profile.id === cur.layout.profile;
  const locks = useMemo(() => { try { return cur ? regenStateOf(cur.layout).locks : []; } catch { return []; } }, [cur]);
  const region = useMemo(() => { try { return cur && zone ? selectRegion(cur.layout, zone, profileOk ? profile : null, blockMode) : null; } catch { return null; } }, [cur, zone, profile, profileOk, blockMode]);
  const selection = region?.selection ?? null;

  /** Show a layout in the current candidate slot (re-evaluated without the simulator; old verdicts no longer apply). */
  const showLayout = (layout: GenLayout) => {
    if (!profile || !config) return;
    const ev = evaluateLayout({ layout, profile, config, noSim: true });
    setCandidates((cs) => cs.map((c, i) => (i === sel ? { ...c, key: c.key ?? c.layout.name, layout, ev, simmed: false, verdicts: [] } : c)));
  };
  const pushStep = (layout: GenLayout, label: string) => {
    if (!cur) return;
    setHistories((hs) => ({ ...hs, [curKey]: historyPush(hs[curKey] ?? historyInit(cur.layout, 'исходная'), layout, label) }));
    showLayout(layout);
  };
  const reroll = () => {
    if (!cur || !zone || !profileOk || !profile) return;
    setBusy('Перегенерация зоны...'); setError('');
    setTimeout(() => {
      try {
        const r = regenerateRegion({ layout: cur.layout, zone, seed: rerollSeed, profile, maxRetries, blocks: blockMode });
        setRegenReport(r);
        if (r.ok && r.layout !== cur.layout) pushStep(r.layout, `зона, seed ${r.seed}${r.retries ? ` (повторов ${r.retries})` : ''}`);
        setRerollSeed((s) => s + 1);
      } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
      setBusy('');
    }, 20);
  };
  const stepHistory = (dir: 'undo' | 'redo') => {
    if (!history || (dir === 'undo' ? !canUndo(history) : !canRedo(history))) return;
    const h = dir === 'undo' ? historyUndo(history) : historyRedo(history);
    setHistories((hs) => ({ ...hs, [curKey]: h }));
    setRegenReport(null);
    showLayout(h.present.state);
  };
  const lockZone = () => {
    if (!cur || !zone) return;
    try { pushStep(addLock(cur.layout, zone, lockName), `закреплён район ${lockName || ''}`.trim()); setLockName(''); setZone(null); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
  };
  const unlock = (id: string) => { if (cur) pushStep(removeLock(cur.layout, id), `снято закрепление ${id}`); };
  const [locksSaved, setLocksSaved] = useState('');
  // map expansion x2 / x4 / x6 + start and goals (levelgen v2 stage 2)
  const [expandFactor, setExpandFactor] = useState(4);
  const [expandSeed, setExpandSeed] = useState(7);
  const [expandReport, setExpandReport] = useState<(ExpandResult & { seamChecks: SeamCheck[] }) | null>(null);
  const [goalCount, setGoalCount] = useState(0);
  const [goalStart, setGoalStart] = useState<'spawn' | 'edge'>('spawn');
  const [goalSeed, setGoalSeed] = useState(1);
  const [goalReport, setGoalReport] = useState<PlaceObjectivesResult | null>(null);
  const objCheck = useMemo(() => { try { return cur?.layout.objectives ? checkObjectives(cur.layout) : null; } catch { return null; } }, [cur]);
  const expandNow = () => {
    if (!cur || !profileOk || !profile) return;
    setBusy(`Расширяю карту x${expandFactor}...`); setError('');
    setTimeout(() => {
      try {
        const r = expandLayout({ layout: cur.layout, factor: expandFactor, seed: expandSeed, profile });
        setExpandReport({ ...r, seamChecks: r.ok ? checkSeams(r.layout, r.roads) : [] });
        if (r.ok) { pushStep(r.layout, `расширение x${expandFactor}, seed ${r.seed}`); setZone(null); setRegenReport(null); setExpandSeed((s) => s + 1); }
      } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
      setBusy('');
    }, 20);
  };
  const placeGoals = () => {
    if (!cur) return;
    setBusy('Расставляю старт и цели...'); setError('');
    setTimeout(() => {
      try {
        const r = placeObjectives({ layout: cur.layout, seed: goalSeed, count: goalCount || undefined, start: goalStart });
        setGoalReport(r);
        if (r.ok) pushStep(r.layout, `старт + цели, seed ${goalSeed}`);
        setGoalSeed((s) => s + 1);
      } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
      setBusy('');
    }, 20);
  };
  const clearGoals = () => {
    if (!cur?.layout.objectives) return;
    const next = { ...cur.layout };
    delete next.objectives;
    pushStep(next, 'цели убраны'); setGoalReport(null);
  };
  const saveLocksNow = async () => {
    if (!cur) return;
    setBusy('Сохраняю закрепления...'); setError(''); setLocksSaved('');
    try {
      const r = await fetch('/api/levelgen/save-locks', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ layout: cur.layout }) });
      const d = await r.json();
      if (!r.ok) throw new Error(d.error ?? r.statusText);
      setLocksSaved(`закреплений ${d.locks} сохранено в ${d.target === 'accepted' ? 'принятую карту' : 'черновик'}: ${d.file}`);
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    setBusy('');
  };
  const openAccepted = async (name: string) => {
    if (!config) return;
    setBusy('Открываю принятую карту...'); setError('');
    try {
      const r = await fetch(`/api/levelgen/open-accepted?name=${encodeURIComponent(name)}`);
      const d = await r.json();
      if (!r.ok) throw new Error(d.error ?? r.statusText);
      const layout = d.layout as GenLayout;
      const pr = await fetch(`/api/levelgen/profile?id=${encodeURIComponent(layout.profile)}`);
      const p = await pr.json() as Profile;
      if (!pr.ok) throw new Error(`эталон ${layout.profile} не найден`);
      setProfileId(layout.profile); setProfile(p);
      const ev = evaluateLayout({ layout, profile: p, config, noSim: true });
      const rest = candidates.filter((c) => c.layout.name !== layout.name);
      setCandidates([...rest, { layout, ev, simmed: false, verdicts: [] }]); setSel(rest.length); setLayer('all');
      setHistories((hs) => { const keep = { ...hs }; delete keep[layout.name]; return keep; });
      setZone(null); setRegenReport(null);
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    setBusy('');
  };
  const regionHint: Record<RegionMode, string> = {
    off: 'Выберите способ выделения зоны: рамка, многоугольник или лассо.',
    rect: 'Рамка: зажмите левую кнопку и протяните.',
    poly: 'Многоугольник: щёлкайте углы; двойной щелчок или Enter — замкнуть; Esc или правая кнопка — отмена.',
    lasso: 'Лассо: обведите зону, удерживая левую кнопку.',
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
            {accepted.map((a) => (
              <button key={a.name} onClick={() => openAccepted(a.name)} disabled={!config || !!busy} title="Открыть для перегенерации зон и закрепления районов (сохранение — кнопкой «Принять»)"
                className="text-left text-xs font-mono text-emerald-300 hover:text-emerald-200 inline-flex items-center gap-1.5 disabled:opacity-50"><FolderOpen size={12} />{a.name}</button>
            ))}
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
            {cur && (
              <div className="flex flex-col gap-1.5 bg-slate-950/60 border border-slate-800 rounded-xl p-2.5 text-xs">
                <div className="flex flex-wrap items-center gap-2">
                  <span className="text-[11px] font-bold tracking-widest uppercase text-slate-400">Зона</span>
                  <div className="inline-flex rounded-lg bg-slate-950 border border-slate-700 p-0.5 text-[11px]">
                    {([['off', 'Просмотр'], ['rect', 'Рамка'], ['poly', 'Многоугольник'], ['lasso', 'Лассо']] as const).map(([m, label]) => (
                      <button key={m} type="button" onClick={() => setRegionMode(m)} className={`px-2 py-0.5 rounded ${regionMode === m ? 'bg-amber-600 text-slate-950 font-semibold' : 'text-slate-300 hover:text-white'}`}>{label}</button>
                    ))}
                  </div>
                  <button type="button" onClick={() => setZone(null)} disabled={!zone} className="inline-flex items-center gap-1 px-2 py-1 rounded-lg bg-slate-800 border border-slate-700 text-slate-300 hover:bg-slate-700 disabled:opacity-40"><Eraser size={12} />Сбросить</button>
                  <label className="flex items-center gap-1 text-slate-400">seed
                    <input type="number" value={rerollSeed} onChange={(e) => setRerollSeed(Math.max(0, Math.floor(Number(e.target.value) || 0)))} className="w-20 bg-slate-950 border border-slate-700 rounded px-1.5 py-0.5 text-slate-100" />
                  </label>
                  <label className="flex items-center gap-1 text-slate-400" title="Сколько раз повторить с новым seed, если маршрут ломается">повторов до
                    <input type="number" value={maxRetries} min={0} max={20} onChange={(e) => setMaxRetries(Math.max(0, Math.min(20, Math.floor(Number(e.target.value) || 0))))} className="w-12 bg-slate-950 border border-slate-700 rounded px-1.5 py-0.5 text-slate-100" />
                  </label>
                  <label className="flex items-center gap-1 text-slate-400" title="Кварталы, чей центр внутри зоны, получают ДРУГОЙ тип (цех -> двор, свалка -> двухэтажка...) и меняются целиком; иначе — только перестановка внутри тех же типов (этап 1)">
                    <input type="checkbox" checked={blockMode === 'types'} onChange={(e) => setBlockMode(e.target.checked ? 'types' : 'jitter')} />новые типы кварталов
                  </label>
                  <button type="button" onClick={reroll} disabled={!zone || !profileOk || !!busy} className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-amber-500 hover:bg-amber-400 text-slate-950 font-semibold disabled:opacity-40"><Shuffle size={13} />Перегенерировать зону</button>
                  <button type="button" onClick={() => stepHistory('undo')} disabled={!history || !canUndo(history) || !!busy} title={history && canUndo(history) ? `Отменить: ${history.present.label}` : ''} className="inline-flex items-center gap-1 px-2 py-1 rounded-lg bg-slate-800 border border-slate-700 text-slate-200 hover:bg-slate-700 disabled:opacity-40"><Undo2 size={13} />Отменить ({history?.past.length ?? 0})</button>
                  <button type="button" onClick={() => stepHistory('redo')} disabled={!history || !canRedo(history) || !!busy} title={history && canRedo(history) ? `Вернуть: ${history.future[0].label}` : ''} className="inline-flex items-center gap-1 px-2 py-1 rounded-lg bg-slate-800 border border-slate-700 text-slate-200 hover:bg-slate-700 disabled:opacity-40"><Redo2 size={13} />Вернуть ({history?.future.length ?? 0})</button>
                  <input value={lockName} onChange={(e) => setLockName(e.target.value)} placeholder="имя района" className="w-28 bg-slate-950 border border-slate-700 rounded px-1.5 py-0.5 text-slate-100" />
                  <button type="button" onClick={lockZone} disabled={!zone || !!busy} className="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-lg bg-emerald-800 hover:bg-emerald-700 text-white disabled:opacity-40"><Lock size={12} />Закрепить район</button>
                  <button type="button" onClick={saveLocksNow} disabled={!!busy} title="Записать закреплённые районы (многоугольники) в JSON карты: в принятую карту (только блок regen.locks), иначе — черновик Saved/TrainingGround/levelgen" className="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-lg bg-slate-800 border border-emerald-700 text-emerald-200 hover:bg-slate-700 disabled:opacity-40"><Save size={12} />Сохранить замки</button>
                </div>
                <p className="text-[11px] text-slate-500">{regionHint[regionMode]} Перегенерируются объекты, чей центр внутри зоны (здания целиком); дороги, канал и закреплённые районы не трогаются. Закрепления хранятся в JSON карты (блок regen): кнопка «Сохранить замки» пишет только их, «Принять» — всю карту.</p>
                {locksSaved && <p className="text-[11px] text-emerald-300">{locksSaved}</p>}
                {!profileOk && <p className="text-amber-300">Для перегенерации нужен эталон {cur.layout.profile} — выберите его слева.</p>}
                {selection && (
                  <p className="text-slate-300">В зоне: перегенерируется <span className="text-cyan-300 font-mono">{selection.inside.length}</span> объектов (зданий {selection.inside.filter((g) => g.category === 'building').length}{region && region.plan.selLots.length ? `, кварталов с новым типом ${region.plan.selLots.length}` : ''}), закреплено <span className="text-emerald-300 font-mono">{selection.locked.length}</span>, дороги/канал <span className="font-mono">{selection.fixed.length}</span>{region?.plan.anchored.length ? `; квартал старта/финиша сохраняет тип: ${region.plan.anchored.join(', ')}` : ''}.</p>
                )}
                {regenReport && (
                  <p className={regenReport.ok ? 'text-emerald-300' : 'text-rose-300'}>
                    {regenReport.ok ? <CheckCircle2 size={12} className="inline mr-1" /> : <XCircle size={12} className="inline mr-1" />}
                    {regenReport.message}; seed {regenReport.seed}; основной маршрут {regenReport.routes.find((r) => r.mandatory)?.completable ? 'проходим' : 'НЕ проходим'}; высоты подогнаны к классам: {regenReport.snappedHeights}{regenReport.skeletonMatch ? '' : '; скелет карты отличается от генератора (правила наложения сохраняют целостность)'}
                  </p>
                )}
                {regenReport && regenReport.retemplated.length > 0 && (
                  <p className="text-[11px] text-cyan-200">Новые типы кварталов: {regenReport.retemplated.map((x) => `${x.id.replace(/^LOT_/, '')}: ${blockLabel(x.from)} → ${blockLabel(x.to)}`).join('; ')}{regenReport.templateFailed.length ? `; без нового типа (не помещается): ${regenReport.templateFailed.map((x) => x.replace(/^LOT_/, '')).join(', ')}` : ''}{regenReport.removedRoutes.length ? `; убраны варианты маршрута: ${regenReport.removedRoutes.join(', ')}` : ''}</p>
                )}
                {regenReport && (regenReport.dropped.length > 0 || regenReport.attempts.some((a) => !a.ok)) && (
                  <details className="text-[11px] text-slate-400"><summary className="cursor-pointer">Подробности: отброшено {regenReport.dropped.length}, неудачных попыток {regenReport.attempts.filter((a) => !a.ok).length}</summary>
                    <ul className="mt-1 flex flex-col gap-0.5 font-mono">
                      {regenReport.attempts.filter((a) => !a.ok).map((a) => <li key={a.seed} className="text-rose-300">seed {a.seed}: {a.reason}</li>)}
                      {regenReport.dropped.map((d) => <li key={`${d.category}:${d.id}`}>{d.id} ({d.category}): {d.reason === 'overlaps_kept' ? 'пересекает сохранённый объект' : d.reason === 'overlaps_lock' ? 'залезает в закреплённый район' : d.reason === 'blocks_road' ? 'перекрывает дорогу/канал' : 'id занят сохранённым объектом'}</li>)}
                    </ul>
                  </details>
                )}
                {locks.length > 0 && (
                  <div className="flex flex-wrap items-center gap-1.5">
                    <span className="text-slate-400">Закреплённые районы:</span>
                    {locks.map((k) => (
                      <span key={k.id} className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full bg-emerald-950 border border-emerald-700 text-emerald-200">
                        <Lock size={10} />{k.name}
                        <button type="button" onClick={() => unlock(k.id)} disabled={!!busy} className="ml-1 text-emerald-400 hover:text-rose-300" title="Снять закрепление">×</button>
                      </span>
                    ))}
                  </div>
                )}
              </div>
            )}
            {cur && (
              <div className="flex flex-col gap-1.5 bg-slate-950/60 border border-slate-800 rounded-xl p-2.5 text-xs">
                <div className="flex flex-wrap items-center gap-2">
                  <span className="text-[11px] font-bold tracking-widest uppercase text-slate-400">Расширение</span>
                  <select value={expandFactor} onChange={(e) => setExpandFactor(Number(e.target.value))} className="bg-slate-950 border border-slate-700 rounded px-1.5 py-0.5 text-slate-100">
                    {V2.expansion.factors.map((f) => { const sz = expansionSize(cur.layout.expansion?.base_size_m ?? cur.layout.params.size_m, f); return <option key={f} value={f}>x{f} по площади ({sz[0]} x {sz[1]} м)</option>; })}
                  </select>
                  <label className="flex items-center gap-1 text-slate-400">seed
                    <input type="number" value={expandSeed} onChange={(e) => setExpandSeed(Math.max(0, Math.floor(Number(e.target.value) || 0)))} className="w-16 bg-slate-950 border border-slate-700 rounded px-1.5 py-0.5 text-slate-100" />
                  </label>
                  <button type="button" onClick={expandNow} disabled={!profileOk || !!busy} title="Старая карта и закреплённые районы остаются на месте; вокруг — кольцевая улица, продолжения дорог и канала, новые кварталы из каталога типов" className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-indigo-600 hover:bg-indigo-500 text-white font-semibold disabled:opacity-40"><Boxes size={13} />Расширить карту</button>
                  <span className="text-slate-600">|</span>
                  <span className="text-[11px] font-bold tracking-widest uppercase text-slate-400">Старт и цели</span>
                  <select value={goalCount} onChange={(e) => setGoalCount(Number(e.target.value))} className="bg-slate-950 border border-slate-700 rounded px-1.5 py-0.5 text-slate-100">
                    <option value={0}>авто ({defaultGoalCount(cur.layout)})</option><option value={2}>2 цели</option><option value={3}>3 цели</option><option value={4}>4 цели</option>
                  </select>
                  <select value={goalStart} onChange={(e) => setGoalStart(e.target.value as 'spawn' | 'edge')} className="bg-slate-950 border border-slate-700 rounded px-1.5 py-0.5 text-slate-100">
                    <option value="spawn">старт: точка появления</option><option value="edge">старт: край карты</option>
                  </select>
                  <label className="flex items-center gap-1 text-slate-400">seed
                    <input type="number" value={goalSeed} onChange={(e) => setGoalSeed(Math.max(0, Math.floor(Number(e.target.value) || 0)))} className="w-14 bg-slate-950 border border-slate-700 rounded px-1.5 py-0.5 text-slate-100" />
                  </label>
                  <button type="button" onClick={placeGoals} disabled={!!busy} className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-yellow-500 hover:bg-yellow-400 text-slate-950 font-semibold disabled:opacity-40"><Footprints size={13} />Расставить старт и цели</button>
                  <button type="button" onClick={clearGoals} disabled={!cur.layout.objectives || !!busy} className="inline-flex items-center gap-1 px-2 py-1 rounded-lg bg-slate-800 border border-slate-700 text-slate-300 hover:bg-slate-700 disabled:opacity-40"><Eraser size={12} />Убрать цели</button>
                </div>
                {expandReport && (
                  <p className={expandReport.ok ? 'text-indigo-200' : 'text-rose-300'}>{expandReport.ok ? <CheckCircle2 size={12} className="inline mr-1" /> : <XCircle size={12} className="inline mr-1" />}{expandReport.message}{expandReport.ok ? `; швы: ${expandReport.seamChecks.map((x) => `${x.kind === 'canal' ? 'канал' : x.id.replace(/^Z_/, '')} (${x.side}) ${x.connected ? 'продолжается' : 'ОБРЫВ'}`).join(', ')}; маршруты ${expandReport.routes.every((x) => x.completable) ? 'проходимы' : 'НЕ проходимы: ' + expandReport.routes.filter((x) => !x.completable).map((x) => x.id).join(', ')}` : ''}</p>
                )}
                {goalReport && !goalReport.ok && <p className="text-rose-300"><XCircle size={12} className="inline mr-1" />{goalReport.message}</p>}
                {cur.layout.objectives && objCheck && (
                  <p className={objCheck.ok ? 'text-yellow-200' : 'text-rose-300'}>
                    {objCheck.ok ? <CheckCircle2 size={12} className="inline mr-1" /> : <XCircle size={12} className="inline mr-1" />}
                    Старт ({cur.layout.objectives.start.source === 'spawn' ? 'точка появления' : 'край карты'}) → цели в любом порядке: {objCheck.goals.map((g) => `${g.id.replace('GOAL_', '№')} ${g.reachable ? `${g.length_m} м` : 'НЕДОСТИЖИМА'}`).join(', ')}; кратчайший обход {objCheck.bestOrder.order.map((x) => x.replace('GOAL_', '№')).join(' → ')} = {objCheck.bestOrder.length_m} м; мин. расстояние между целями {objCheck.minSpacing_m} м{objCheck.ok ? '' : ' — расставьте цели заново'}
                  </p>
                )}
                <p className="text-[11px] text-slate-500">Расширение: старая карта остаётся в центре без изменений (кроме проёма в заборе, если он перегораживает продолжение дороги), кварталы вокруг — из каталога levelgen_v2.json; их тоже можно перегенерировать зоной. Цели — на земле, достижимы со старта, разнесены, не в закреплённых зданиях; хранятся в JSON карты (блок objectives).</p>
              </div>
            )}
            <div className="grid gap-3 lg:grid-cols-2">
              <div>
                <p className="text-[11px] text-slate-500 mb-1">Кандидат (сверху; синий — маршрут, жёлтый — лестницы, голубой/янтарный — окна/бойницы, зелёный — крыша/парапет, фиол. — перелаз, пунктир — линии огня)</p>
                <RegionOverlay bounds={cur?.layout.bounds ?? null} height={520} mode={regionMode} zone={zone} locks={locks} blocks={region?.plan.selLots.map((k) => k.rect)}
                  inside={selection?.inside.map((g) => g.centre)} locked={selection?.locked.map((g) => g.centre)} onZone={setZone}>
                  <LayoutView ref={view} layout={cur?.layout ?? null} layer={layer} floorMode={floorMode} firingLines={cur?.ev.tactical?.firingLines.lines} showFiringLines={showFiringLines} height={520} marks={marks} />
                </RegionOverlay>
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
