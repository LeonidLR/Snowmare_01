// Training Ground department «Генератор уровней» (docs/design/levelgen/LEVEL_GENERATOR.md): from a reference profile (the game
// designer's annotated map, Content/Data/LevelGen/refs), a seed and parameters the generator makes original greybox layouts IN
// THE BROWSER (0 tokens), the evaluator checks each (the mandatory route must be completable; mechanics, sightlines, ambush
// quality, profile similarity), the page shows the candidate's top-down render next to the reference image, the metrics and the
// critics' verdicts (Jev - typed judgments; the Claude API - sees both images, USD-capped; the server holds the keys). The
// designer accepts (-> Content/Data/LevelGen/accepted, what the UE builder reads) or rejects.
import React, { useEffect, useMemo, useRef, useState } from 'react';
import { CheckCircle2, AlertTriangle, XCircle, Play, Pause, Hammer, Footprints, ThumbsUp, ThumbsDown, Bot, Boxes, Layers, Shuffle, Undo2, Redo2, Lock, FolderOpen, Eraser, Save, ZoomIn, ZoomOut, Maximize2, MapPinPlus } from 'lucide-react';
import { LayoutView } from './LayoutView';
import type { FloorViewMode, LayoutViewHandle } from './LayoutView';
import { PropsPanel } from './PropsPanel';
import { RegionOverlay } from './RegionOverlay';
import type { LayerAction, ObjectiveAction, RegionMode } from './RegionOverlay';
import { LayersOverlay, LayersPanel } from './LayersPanel';
import type { PlanView } from '../levelgen/planView';
import { FIT_VIEW, PLAN_VIEW, planTransform, viewOfRect, zoomAt } from '../levelgen/planView';
import type { Conditions, LayoutAnalysis } from '../levelgen/chokepoints';
import { analyzeLayout, CHOKE, geometryHash, threatNumbers, withoutStaleAnalysis } from '../levelgen/chokepoints';
import type { LayerVisibility } from '../levelgen/layers';
import { assignMap, defaultVisibility, editLayout, editProps, hiddenKeys, layerById, layerCounts, LAYERS, layerTints, mergeLayerReroll, pickEntity, resolveProps } from '../levelgen/layers';
import type { LevelPropsFile } from '../levelgen/props/schema';
import type { History } from '../levelgen/regenHistory';
import { canRedo, canUndo, historyInit, historyPush, historyRedo, historyUndo } from '../levelgen/regenHistory';
import type { BlockMode, RegionRegenResult } from '../levelgen/regionRegen';
import type { ExpandResult, SeamCheck } from '../levelgen/expand';
import { checkSeams, expandLayout, expansionSize } from '../levelgen/expand';
import type { PlaceObjectivesResult } from '../levelgen/objectives';
import { addGoal, checkObjectives, defaultGoalCount, groundNav, moveObjective, placeObjectives, removeGoal, START_ID } from '../levelgen/objectives';
import { V2 } from '../levelgen/blockTemplates';
import { FULL } from '../levelgen/fullReroll';
import { ROTATION } from '../levelgen/rotation';
import type { SquadTrack } from '../levelgen/playback';
import { advanceClock, clockLabel, PLAYBACK, positionsAt, summarizeTrack, trailAt } from '../levelgen/playback';
import { addLock, DEFAULT_MAX_RETRIES, regenerateRegion, regenStateOf, removeLock, selectRegion } from '../levelgen/regionRegen';
import type { FormationConfigFile } from '../movement/config';
import { configValues } from '../movement/config';
import type { Vec2 } from '../movement/geometry';
import type { CriticVerdict } from '../levelgen/critic';
import type { Evaluation } from '../levelgen/evaluate';
import { evaluateLayout } from '../levelgen/evaluate';
import type { Profile } from '../levelgen/profile';
import type { StoryState } from '../levelgen/storyPass';
import { generateCandidate, hasStories, lockedBuildingIds, rerollBuildingStory, restoryForGoals, storyMapSummary, withBuildingStories } from '../levelgen/storyPass';
import type { GenLayout, GenParams } from '../levelgen/schema';
import { layersOf, mandatoryRoute } from '../levelgen/schema';
import { BuildingsPanel } from './BuildingsPanel';

/** `key` = the slot's history key (the first layout name: an expansion renames the map to <name>_x<N>). */
interface Candidate { layout: GenLayout; ev: Evaluation; simmed: boolean; verdicts: CriticVerdict[]; key?: string; /** The squad playback of the last simulator walk (task 072; never sent to the server). */ track?: SquadTrack;
  /** The chokepoint / threat analysis of `layout` (chokepoints.ts); «Принять» writes it into the layout JSON. */ analysis?: LayoutAnalysis }
/** The threat sub-layers start hidden (hundreds of marks on a big map); the designer turns them on in the panel. */
const HIDDEN_AT_START = ['threat_drop', 'threat_windows', 'threat_overwatch', 'threat_hiding'];
const sameConditions = (a: Conditions, b: Conditions) => a.weather === b.weather && a.time_of_day === b.time_of_day;
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
/** 'a + b + c' (the parcels of a 'full' re-roll) -> the labels. */
const blockLabels = (t: string) => t.split(' + ').filter(Boolean).map(blockLabel).join(' + ');

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
  /** «Истории зданий (S1)»: the story pass as the last generation step (storyPass.ts); off = the generator's /1 output unchanged. */
  const [storiesOn, setStoriesOn] = useState(true);
  /** The story pass state per layout object (pristine buildings for «другая история», rollback log); history steps share it. */
  const storyStates = useRef(new WeakMap<GenLayout, StoryState>());
  const [storyMsg, setStoryMsg] = useState('');
  const [sel, setSel] = useState(0);
  const [layer, setLayer] = useState('all');
  const [floorMode, setFloorMode] = useState<FloorViewMode>('all');
  // the LAYER system (levelgen/layers.ts, Content/Data/LevelGen/layers.json): visibility, the active layer and its tools
  const [vis, setVis] = useState<LayerVisibility>(() => defaultVisibility(LAYERS, HIDDEN_AT_START));
  const [activeLayer, setActiveLayer] = useState<string | null>(null);
  const [layerTool, setLayerTool] = useState<'select' | 'add'>('select');
  const [addIdx, setAddIdx] = useState(0);
  const [selKey, setSelKey] = useState<string | null>(null);
  const [tintOn, setTintOn] = useState(true);
  const [layerMsg, setLayerMsg] = useState<{ ok: boolean; text: string } | null>(null);
  // the map's props file (mines, loot, special items): read through the props API, hand edits are written as its overrides
  const [propsState, setPropsState] = useState<{ map: string; path: string; file: LevelPropsFile; dirty: boolean } | null>(null);
  const NO_PROPS = 'У этой карты ещё нет файла пропсов — пропсы появятся после задачи 076 (автоматические пропсы для сгенерированных карт).';
  // the chokepoint / threat analysis (levelgen/chokepoints.ts)
  const [conditions, setConditions] = useState<Conditions>(CHOKE.threats.conditions);
  const [analysisBusy, setAnalysisBusy] = useState(false);
  const [selChoke, setSelChoke] = useState<string | null>(null);
  // the reference image beside the plan can be folded away: the plan then takes the whole width (big maps, the layers)
  const [showRef, setShowRef] = useState(true);
  const showFiringLines = vis.fire_lines !== false;
  const [busy, setBusy] = useState('');
  const [error, setError] = useState('');
  const [budgetUsd, setBudgetUsd] = useState(2);
  const [model, setModel] = useState('claude-opus-5-5');
  const [ledger, setLedger] = useState<{ spentUsd: number; calls: unknown[] }>({ spentUsd: 0, calls: [] });
  const [accepted, setAccepted] = useState<{ name: string; seed: number; profile: string; at: string }[]>([]);
  const [refOk, setRefOk] = useState(true);
  const view0 = useRef<LayoutViewHandle>(null);
  // region re-roll + locked districts (levelgen v2 stage 1, task 070; docs/port/LEVELGEN_V2_STAGE1_REPORT.md)
  const [regionMode, setRegionMode] = useState<RegionMode>('off');
  const [zone, setZone] = useState<Vec2[] | null>(null);
  const [rerollSeed, setRerollSeed] = useState(1001);
  const [maxRetries, setMaxRetries] = useState(DEFAULT_MAX_RETRIES);
  const [lockName, setLockName] = useState('');
  const [regenReport, setRegenReport] = useState<RegionRegenResult | null>(null);
  const [histories, setHistories] = useState<Record<string, History<GenLayout>>>({});
  // task 072: the default is the full re-plan (the designer expected the buildings to change, not only the points)
  const [blockMode, setBlockMode] = useState<BlockMode>('full');
  const [chaos, setChaos] = useState<number>(FULL.default_chaos);
  // «Макс. угол поворота зданий» (rotation.ts): 0 = axis-aligned; follows the shown map's stored value
  const [maxRot, setMaxRot] = useState<number>(ROTATION.max_deg_default);
  // zoom + pan on the plan (UX round, task 072): reset to "fit" when another map (other bounds) is shown
  const [view, setView] = useState<PlanView>(FIT_VIEW);
  const [goalTool, setGoalTool] = useState(false);
  const [objMsg, setObjMsg] = useState<{ ok: boolean; text: string } | null>(null);

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
          const res = generateCandidate({ profile, seed: seed + i, params, stories: storiesOn });
          const layout = res.layout;
          if ('base' in res) storyStates.current.set(layout, res);
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
        const { track, ...ev } = evaluateLayout({ layout: cur.layout, profile, config, track: true });
        setCandidates((cs) => cs.map((c, i) => (i === sel ? { ...c, ev, simmed: true, track } : c)));
        setPlayT(0); setPlaying(!!track);
      } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
      setBusy('');
    }, 20);
  };

  const critic = async (client: 'claude' | 'jev' | 'mock') => {
    if (!cur) return;
    setBusy(client === 'claude' ? `Критик Claude (${model}, бюджет $${budgetUsd})...` : client === 'jev' ? 'Jev оценивает...' : 'Эвристика...'); setError('');
    try {
      const candidatePng = client === 'claude' ? view0.current?.toPngBase64() ?? '' : '';
      const r = await fetch('/api/levelgen/critic', { method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name: cur.layout.name, profile: cur.layout.profile, layout: cur.layout, evaluation: cur.ev, candidatePng, client, budgetUsd, model }) });
      const d = await r.json();
      if (!r.ok) throw new Error(d.error ?? r.statusText);
      setCandidates((cs) => cs.map((c, i) => (i === sel ? { ...c, verdicts: [...c.verdicts, d.verdict] } : c)));
      if (d.ledger) setLedger(d.ledger);
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    setBusy('');
  };

  /** BUILDING_GENERATOR.md section 5: «Принять» waits while a building fails traversal, unless the designer forces it. */
  const [forceBroken, setForceBroken] = useState(false);
  const brokenBuildings = cur?.ev.buildings?.failed ?? 0;
  // the override is per candidate: a new candidate / seed / slot starts unforced again (API review 2026-11-18)
  useEffect(() => { setForceBroken(false); }, [sel, cur?.layout.name, cur?.layout.seed]);
  const accept = async () => {
    if (!cur) return;
    if (brokenBuildings > 0 && !forceBroken) return;
    setBusy('Сохраняю...');
    try {
      // the chokepoint / threat analysis is saved with the map (block `analysis`): the shown one, else computed now
      const an = analysis ?? analyzeLayout(cur.layout, { conditions });
      // a forced accept is recorded in the map JSON (analysis.forced_buildings = how many buildings failed traversal)
      const layout: GenLayout = { ...cur.layout, analysis: brokenBuildings > 0 ? { ...an, forced_buildings: brokenBuildings } : an };
      const r = await fetch('/api/levelgen/accept', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ layout, evaluation: cur.ev, verdicts: cur.verdicts }) });
      const d = await r.json();
      if (!r.ok) throw new Error(d.error ?? r.statusText);
      setAccepted((a) => [...a.filter((x) => x.name !== cur.layout.name), { name: cur.layout.name, seed: cur.layout.seed, profile: cur.layout.profile, at: new Date().toISOString() }]);
      setCandidates((cs) => cs.map((c, i) => (i === sel ? { ...c, layout, analysis: an } : c)));
      setAcceptedSnap((m) => ({ ...m, [cur.layout.name]: JSON.stringify(layout) }));
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    setBusy('');
  };

  // «Собрать в Unreal» (task 072): the server builds the ACCEPTED file (accepted/<name>.json) + runs the route smoke, one at a time
  type BuildTarget = { name: string; leaf: string; map: string; file: string; exists: boolean; builtFrom: string; canBuild: boolean; needsConfirm: boolean; reason: string };
  type BuildState = { stage: 'idle' | 'build' | 'smoke' | 'done' | 'failed'; name: string; map: string; file: string; elapsed_s: number; stage_elapsed_s: number; message: string;
    result: { built: boolean; actors?: number; nav_tiles?: string; route?: string; route_summary?: string; reason?: string };
    /** The scripts' latest output lines (they run hidden, no window: bug 2026-11-16 evening). */ log?: string[] };
  const [acceptedSnap, setAcceptedSnap] = useState<Record<string, string>>({});
  const [buildAsk, setBuildAsk] = useState<BuildTarget | null>(null);
  const [buildTarget, setBuildTarget] = useState<BuildTarget | null>(null);
  const [buildState, setBuildState] = useState<BuildState | null>(null);
  const [buildErr, setBuildErr] = useState('');
  const isAccepted = !!cur && accepted.some((a) => a.name === cur.layout.name);
  /** 'same' = the page shows the accepted file; 'changed' = edited after «Принять»; 'unknown' = a candidate with an accepted name. */
  const acceptedSync = useMemo<'same' | 'changed' | 'unknown'>(() => {
    if (!cur) return 'unknown';
    const snap = acceptedSnap[cur.layout.name];
    return snap === undefined ? 'unknown' : snap === JSON.stringify(cur.layout) ? 'same' : 'changed';
  }, [cur, acceptedSnap]);
  const building = buildState?.stage === 'build' || buildState?.stage === 'smoke';
  // the build log panel follows the newest line
  const buildLogRef = useRef<HTMLPreElement>(null);
  const buildLogLast = buildState?.log?.at(-1) ?? '';
  useEffect(() => { const el = buildLogRef.current; if (el) el.scrollTop = el.scrollHeight; }, [buildLogLast, buildState?.log?.length]);
  const pollBuild = async () => { try { const r = await fetch('/api/levelgen/build-status'); if (r.ok) setBuildState(await r.json()); } catch { /* the dev server restarts */ } };
  useEffect(() => { pollBuild(); }, []);
  useEffect(() => {
    if (!building) return;
    const id = setInterval(pollBuild, V2.build.poll_s * 1000);
    return () => clearInterval(id);
  }, [building]);
  const curName = cur?.layout.name;
  useEffect(() => {
    setBuildAsk(null); setBuildErr('');
    if (!isAccepted || !curName) { setBuildTarget(null); return; }
    fetch(`/api/levelgen/build-target?name=${encodeURIComponent(curName)}`).then((r) => r.json()).then(setBuildTarget).catch(() => setBuildTarget(null));
  }, [isAccepted, curName, buildState?.stage]);
  const buildNow = async (confirm: boolean) => {
    if (!cur) return;
    setBuildErr(''); setBuildAsk(null);
    try {
      const r = await fetch('/api/levelgen/build-map', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ name: cur.layout.name, confirm }) });
      const d = await r.json();
      if (r.status === 409 && d.needsConfirm) { setBuildAsk(d.target); return; }
      if (!r.ok) throw new Error(d.error ?? r.statusText);
      await pollBuild();
    } catch (e) { setBuildErr(e instanceof Error ? e.message : String(e)); }
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
  const showLayout = (shown: GenLayout) => {
    if (!profile || !config) return;
    const layout = withoutStaleAnalysis(shown); // an edited map: the stored analysis no longer describes it
    const st = storyStates.current.get(shown);
    if (st) storyStates.current.set(layout, st);
    const ev = evaluateLayout({ layout, profile, config, noSim: true });
    setCandidates((cs) => cs.map((c, i) => (i === sel ? { ...c, key: c.key ?? c.layout.name, layout, ev, simmed: false, verdicts: [], track: undefined, analysis: undefined } : c)));
  };
  /** `stories` = the story state of `layout`; absent = the current one carries over (locks, goals: the same buildings). */
  const pushStep = (layout: GenLayout, label: string, stories?: StoryState) => {
    if (!cur) return;
    const st = stories ?? storyStates.current.get(cur.layout);
    if (st) storyStates.current.set(layout, st);
    setHistories((hs) => ({ ...hs, [curKey]: historyPush(hs[curKey] ?? historyInit(cur.layout, 'исходная'), layout, label) }));
    showLayout(layout);
  };
  /**
   * A step that wrote `layout.objectives` (place / clear / add / move / remove a goal): with the story toggle on and a storied map,
   * the story pass re-runs for the buildings whose role changed (restoryForGoals; locked districts keep theirs), else as it is.
   */
  const pushGoalStep = (layout: GenLayout, label: string) => {
    if (!cur) return;
    if (!storiesOn || !hasStories(layout)) { pushStep(layout, label); return; }
    const r = restoryForGoals(layout, layout.seed, storyStates.current.get(cur.layout));
    pushStep(r.layout, r.repicked.length ? `${label}; истории заново: ${r.repicked.length}` : label, r);
  };
  const reroll = () => {
    if (!cur || !zone || !profileOk || !profile) return;
    setBusy('Перегенерация зоны...'); setError('');
    setTimeout(() => {
      try {
        const r = regenerateRegion({ layout: cur.layout, zone, seed: rerollSeed, profile, maxRetries, blocks: blockMode, chaos, maxRotationDeg: maxRot });
        setRegenReport(r);
        // kept / locked buildings keep their stories; the regenerated ones get new stories from the re-roll seed's sub-seeds
        const sp = storiesOn && r.ok && r.layout !== cur.layout ? withBuildingStories(r.layout, r.seed, storyStates.current.get(cur.layout)) : null;
        if (r.ok && r.layout !== cur.layout) pushStep(sp?.layout ?? r.layout, `зона (${blockMode === 'full' ? `полностью, хаос ${chaos}` : blockMode === 'types' ? 'типы' : 'наполнение'}), seed ${r.seed}${r.retries ? ` (повторов ${r.retries})` : ''}`, sp ?? undefined);
        setRerollSeed((s) => s + 1);
      } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
      setBusy('');
    }, 20);
  };
  const stepHistory = (dir: 'undo' | 'redo') => {
    if (!history || (dir === 'undo' ? !canUndo(history) : !canRedo(history))) return;
    const h = dir === 'undo' ? historyUndo(history) : historyRedo(history);
    setHistories((hs) => ({ ...hs, [curKey]: h }));
    setRegenReport(null); setObjMsg(null); setLayerMsg(null);
    showLayout(h.present.state);
  };
  const lockZone = () => {
    if (!cur || !zone) return;
    try { pushStep(addLock(cur.layout, zone, lockName), `закреплён район ${lockName || ''}`.trim()); setLockName(''); setZone(null); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
  };
  const unlock = (id: string) => { if (cur) pushStep(removeLock(cur.layout, id), `снято закрепление ${id}`); };
  // ---- building stories (S1 step 4) -------------------------------------------------------------------------------------
  const storyState = cur ? storyStates.current.get(cur.layout) : undefined;
  const lockedBuildings = useMemo(() => (cur ? lockedBuildingIds(cur.layout) : new Set<string>()), [cur]);
  const storySummary = useMemo(() => (cur && cur.layout.buildings.some((b) => b.stories?.length) ? storyMapSummary(cur.layout, storyStates.current.get(cur.layout)?.log) : null), [cur]);
  /** «другая история»: one building's story re-rolled with the next sub-seed (refused inside a lock). */
  const rerollStory = (id: string) => {
    if (!cur) return;
    const st = storyStates.current.get(cur.layout);
    if (!st) { setStoryMsg(`${id}: нет состояния историй (карта загружена или сгенерирована без историй)`); return; }
    try {
      const r = rerollBuildingStory(cur.layout, id, st);
      setStoryMsg(r.message);
      if (r.ok && r.layout) pushStep(r.layout, `другая история ${id}: ${r.from} -> ${r.to}`, { base: r.base!, log: r.log! });
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
  };
  const [locksSaved, setLocksSaved] = useState('');
  // map expansion x2 / x4 / x6 + start and goals (levelgen v2 stage 2)
  const [expandFactor, setExpandFactor] = useState(4);
  const [expandSeed, setExpandSeed] = useState(7);
  const [expandReport, setExpandReport] = useState<(ExpandResult & { seamChecks: SeamCheck[] }) | null>(null);
  const [goalCount, setGoalCount] = useState(0);
  const [goalStart, setGoalStart] = useState<'spawn' | 'edge'>('spawn');
  const [goalSeed, setGoalSeed] = useState(1);
  const [goalReport, setGoalReport] = useState<PlaceObjectivesResult | null>(null);
  // the ground nav of the shown map: built once per geometry (an objectives edit keeps the arrays, so it is reused)
  const curLayout = cur?.layout;
  const nav = useMemo(() => { try { return curLayout ? groundNav(curLayout) : null; } catch { return null; } },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [curLayout?.walls, curLayout?.buildings, curLayout?.covers, curLayout?.zones, curLayout?.cut, curLayout?.bounds]);
  const objCheck = useMemo(() => { try { return cur?.layout.objectives ? checkObjectives(cur.layout, nav ?? undefined) : null; } catch { return null; } }, [cur, nav]);
  const objErrors = useMemo(() => {
    const e: Record<string, string> = {};
    if (objCheck?.start.error) e[START_ID] = objCheck.start.error;
    for (const g of objCheck?.goals ?? []) if (g.error) e[g.id] = g.error;
    return e;
  }, [objCheck]);
  const objWarnings = useMemo(() => Object.fromEntries((objCheck?.goals ?? []).filter((g) => g.warnings.length).map((g) => [g.id, g.warnings.join(', ')])), [objCheck]);
  // another map in the plan (another slot, an opened accepted map, an expansion): back to "fit", the last edit message cleared
  const viewKey = cur ? `${sel}:${cur.key ?? cur.layout.name}:${JSON.stringify(cur.layout.bounds)}` : '';
  useEffect(() => { setView(FIT_VIEW); setObjMsg(null); }, [viewKey]);
  const storedRot = cur?.layout.params.max_rotation_deg;
  useEffect(() => { setMaxRot(storedRot ?? ROTATION.max_deg_default); }, [viewKey, storedRot]);
  /** A start / goal edit on the plan: applied, re-walked, one undo step (an invalid spot stays where it was dropped, marked red). */
  const onObjective = (a: ObjectiveAction) => {
    if (!cur || busy) return;
    const r = a.kind === 'add' ? addGoal(cur.layout, a.pos, nav ?? undefined) : a.kind === 'move' ? moveObjective(cur.layout, a.id, a.pos, nav ?? undefined) : removeGoal(cur.layout, a.id, nav ?? undefined);
    setObjMsg({ ok: r.ok && !!r.check?.ok, text: r.message });
    if (r.ok) { pushGoalStep(r.layout, r.message.split(';')[0]); setGoalReport(null); }
  };
  const zoomBy = (f: number) => setView((v) => zoomAt(v, [planBox.current ? planBox.current.clientWidth / 2 : 0, 260], f));
  const planBox = useRef<HTMLDivElement>(null);
  // the squad playback (task 072): a clock driven by requestAnimationFrame at x1..x8, a scrubber; dots + the leader's trail
  const [playT, setPlayT] = useState(0);
  const [playing, setPlaying] = useState(false);
  const playRef = useRef(0);
  playRef.current = playT;
  const [speed, setSpeed] = useState<number>(PLAYBACK.speeds[1] ?? 2);
  const track = cur?.track ?? null;
  useEffect(() => {
    if (!playing || !track) return;
    let raf = 0, last = performance.now();
    const tick = (now: number) => {
      const dt = (now - last) / 1000; last = now;
      const r = advanceClock(playRef.current, dt, speed, track.duration_s);
      playRef.current = r.t; setPlayT(r.t);
      if (r.ended) { setPlaying(false); return; }
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [playing, speed, track]);
  const squadNow = useMemo(() => {
    if (!track) return null;
    const p = positionsAt(track, playT);
    return p ? { members: p.members, trail: trailAt(track, playT), trails: p.members.map((_, m) => trailAt(track, playT, PLAYBACK.trail_s, m)), leg: p.leg, formation: p.formation } : null;
  }, [track, playT]);
  const trackSummary = useMemo(() => (track ? summarizeTrack(track) : null), [track]);
  const expandNow = () => {
    if (!cur || !profileOk || !profile) return;
    setBusy(`Расширяю карту x${expandFactor}...`); setError('');
    setTimeout(() => {
      try {
        // the rotation setting rides in the map's params (absent / unchanged = the map as it is: byte-identical expansions)
        const src = maxRot === (cur.layout.params.max_rotation_deg ?? 0) ? cur.layout : { ...cur.layout, params: { ...cur.layout.params, max_rotation_deg: maxRot } };
        const r = expandLayout({ layout: src, factor: expandFactor, seed: expandSeed, profile });
        setExpandReport({ ...r, seamChecks: r.ok ? checkSeams(r.layout, r.roads) : [] });
        // the old map's buildings keep their stories; the new blocks' buildings get stories from the expansion seed's sub-seeds
        const sp = storiesOn && r.ok ? withBuildingStories(r.layout, r.seed, storyStates.current.get(cur.layout)) : null;
        if (r.ok) { pushStep(sp?.layout ?? r.layout, `расширение x${expandFactor}, seed ${r.seed}`, sp ?? undefined); setZone(null); setRegenReport(null); setExpandSeed((s) => s + 1); }
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
        if (r.ok) pushGoalStep(r.layout, `старт + цели, seed ${goalSeed}`);
        setGoalSeed((s) => s + 1);
      } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
      setBusy('');
    }, 20);
  };
  const clearGoals = () => {
    if (!cur?.layout.objectives) return;
    const next = { ...cur.layout };
    delete next.objectives;
    pushGoalStep(next, 'цели убраны'); setGoalReport(null);
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
      setAcceptedSnap((m) => ({ ...m, [layout.name]: JSON.stringify(layout) }));
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    setBusy('');
  };
  // ---- layers: assignment, visibility, the active layer's tools, the props file, the chokepoint analysis -------------------------
  const propsFile = propsState?.file ?? null;
  const assignment = useMemo(() => (curLayout ? assignMap(curLayout, propsFile) : null), [curLayout, propsFile]);
  const geoHash = useMemo(() => (curLayout ? geometryHash(curLayout) : ''), [curLayout]);
  const analysis = useMemo<LayoutAnalysis | null>(() => {
    if (!cur) return null;
    for (const a of [cur.analysis, cur.layout.analysis]) if (a && a.geometry_hash === geoHash && sameConditions(a.conditions, conditions)) return a;
    return null;
  }, [cur, geoHash, conditions]);
  const analysisWanted = ['chokepoints', ...HIDDEN_AT_START].some((id) => vis[id] !== false);
  const runAnalysis = (layout: GenLayout, slot: number) => {
    setAnalysisBusy(true);
    return window.setTimeout(() => {
      try { const a = analyzeLayout(layout, { conditions }); setCandidates((cs) => cs.map((c, i) => (i === slot && c.layout === layout ? { ...c, analysis: a } : c))); }
      catch (e) { setError(e instanceof Error ? e.message : String(e)); }
      setAnalysisBusy(false);
    }, 200);
  };
  const hasAnalysis = !!analysis;
  useEffect(() => {
    if (!curLayout || hasAnalysis || !analysisWanted) return;
    const id = runAnalysis(curLayout, sel);
    return () => { window.clearTimeout(id); setAnalysisBusy(false); };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [curLayout, hasAnalysis, analysisWanted, conditions, sel]);
  const threat = useMemo(() => threatNumbers(CHOKE, conditions), [conditions]);
  const hidden = useMemo(() => (assignment ? hiddenKeys(assignment, vis) : undefined), [assignment, vis]);
  const tints = useMemo(() => (assignment && tintOn ? layerTints(assignment) : undefined), [assignment, tintOn]);
  const layerCountsNow = useMemo(() => (assignment ? layerCounts(assignment, { fire_line: cur?.ev.tactical?.firingLines.lines.length ?? 0, chokepoint: analysis?.chokepoints.length ?? 0, drop_spot: analysis?.dropAmbushSpots.length ?? 0,
    window: analysis?.windowsCovering.length ?? 0, overwatch: analysis?.overwatchRoofs.length ?? 0, hiding: analysis?.hidingSpots.length ?? 0 }) : {}), [assignment, cur, analysis]);
  const selectedEntity = useMemo(() => (assignment && selKey ? Object.values(assignment.byLayer).flat().find((e) => e.key === selKey) ?? null : null), [assignment, selKey]);
  const propsOnMap = useMemo(() => (assignment ? resolveProps(propsFile).map((prop) => ({ layer: assignment.layerOf[`prop:${prop.object.id}`] ?? 'special', prop })) : []), [assignment, propsFile]);
  const activeDef = activeLayer ? layerById(activeLayer) ?? null : null;
  // the active layer's entities are outlined when the layer is small enough to read (roads, buildings, bridges)
  const activeEntities = useMemo(() => { const list = assignment && activeLayer ? assignment.byLayer[activeLayer] ?? [] : []; return list.length <= 160 ? list.filter((e) => e.shape.kind !== 'point' || e.shape.r > 0) : []; }, [assignment, activeLayer]);
  useEffect(() => { setSelKey(null); setLayerMsg(null); setLayerTool('select'); setAddIdx(0); }, [activeLayer]);
  // the props file of the shown map (by its `layout` name); none yet for the generated / expanded maps (task 076 adds them)
  const curLayoutName = cur?.layout.name;
  useEffect(() => {
    setPropsState(null);
    if (!curLayoutName) return;
    let dead = false;
    fetch('/api/levelgen/props-list').then((r) => r.json()).then(async (d: { files?: { id: string; layout: string; isFixture: boolean }[] }) => {
      const f = (d.files ?? []).find((x) => !x.isFixture && x.layout === curLayoutName);
      if (!f || dead) return;
      const r = await fetch(`/api/levelgen/props?map=${encodeURIComponent(f.id)}`);
      const p = await r.json() as { props?: LevelPropsFile; filePath?: string };
      if (r.ok && p.props && !dead) setPropsState({ map: f.id, path: p.filePath ?? f.id, file: p.props, dirty: false });
    }).catch(() => {});
    return () => { dead = true; };
  }, [curLayoutName]);
  const saveProps = async () => {
    if (!propsState) return;
    setBusy('Сохраняю пропсы...'); setError('');
    try {
      const r = await fetch(`/api/levelgen/props?map=${encodeURIComponent(propsState.map)}`, { method: 'PUT', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ base_revision: propsState.file.revision, layers: propsState.file.layers, overrides: propsState.file.overrides ?? [] }) });
      const d = await r.json();
      if (!r.ok) throw new Error(d.error ?? r.statusText);
      setPropsState({ ...propsState, file: d.props as LevelPropsFile, dirty: false });
      setLayerMsg({ ok: true, text: `пропсы сохранены: ${propsState.path}, ревизия ${d.revision}` });
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    setBusy('');
  };
  /** Whether the last layer edit was applied (deleteSelected reads it right after the call). */
  const layerMsgOk = useRef(false);
  /** A props edit of the active layer (an override in the props file; saved by «Сохранить пропсы»). */
  const applyProps = (edit: Parameters<typeof editProps>[2]): string | undefined => {
    if (!propsState || !activeLayer) { setLayerMsg({ ok: false, text: NO_PROPS }); return undefined; }
    const r = editProps(propsState.file, activeLayer, edit);
    layerMsgOk.current = r.ok;
    setLayerMsg({ ok: r.ok, text: r.message });
    if (r.ok) setPropsState({ ...propsState, file: r.props, dirty: true });
    return r.ok ? r.id : undefined;
  };
  /** A layout edit of the active layer: one undo step. */
  const applyLayout = (edit: Parameters<typeof editLayout>[2]): string | undefined => {
    if (!cur || !activeLayer) return undefined;
    const r = editLayout(cur.layout, activeLayer, edit);
    layerMsgOk.current = r.ok;
    setLayerMsg({ ok: r.ok, text: r.message });
    if (r.ok) pushStep(r.layout, `${activeDef?.name ?? activeLayer}: ${r.message}`);
    return r.ok ? r.key : undefined;
  };
  const pickLayer = (p: Vec2, tol: number) => {
    if (!assignment || !activeDef) return null;
    const e = pickEntity(assignment, activeDef.id, p, tol);
    return e ? { key: e.key, movable: activeDef.edit.includes('move') && (e.type === 'cover' || e.type === 'wall' || e.type === 'prop') } : null;
  };
  const onLayerAction = (a: LayerAction) => {
    if (!cur || !activeDef || busy) return;
    if (a.kind === 'select') { setSelKey(a.key); return; }
    if (a.kind === 'move') {
      if (a.key.startsWith('prop:')) {
        const id = a.key.slice(5), o = resolveProps(propsFile).find((x) => x.object.id === id)?.object;
        if (o) applyProps({ op: 'move', id, pos: [o.pos[0] + a.delta[0], o.pos[1] + a.delta[1]] });
      } else applyLayout({ op: 'move', key: a.key, delta: a.delta });
      return;
    }
    const t = activeDef.add?.[addIdx];
    if (!t) return;
    if (t.prop) { const id = applyProps({ op: 'add', template: t, pos: a.pos }); if (id) setSelKey(`prop:${id}`); }
    else { const key = applyLayout({ op: 'add', template: t, pos: a.pos }); if (key) setSelKey(key); }
  };
  const deleteSelected = () => {
    if (!selKey || !activeDef || busy) return;
    const was = layerMsgOk.current;
    layerMsgOk.current = false;
    if (selKey.startsWith('prop:')) applyProps({ op: 'delete', id: selKey.slice(5) }); else applyLayout({ op: 'delete', key: selKey });
    if (layerMsgOk.current) setSelKey(null);
    layerMsgOk.current = was;
  };
  const deleteRef = useRef(deleteSelected);
  deleteRef.current = deleteSelected;
  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      const el = e.target;
      if (e.key !== 'Delete' || (el instanceof HTMLElement && ['INPUT', 'SELECT', 'TEXTAREA'].includes(el.tagName))) return;
      deleteRef.current();
    };
    window.addEventListener('keydown', key);
    return () => window.removeEventListener('keydown', key);
  }, []);
  /** Re-roll ONLY the active obstacle layer inside the zone: the zone is re-dealt (jitter) and only that layer's covers are taken. */
  const rerollLayer = () => {
    if (!cur || !zone || !profileOk || !profile || !activeDef) return;
    setBusy(`Перегенерация слоя «${activeDef.name}»...`); setError('');
    setTimeout(() => {
      try {
        const fresh = regenerateRegion({ layout: cur.layout, zone, seed: rerollSeed, profile, maxRetries, blocks: 'jitter' });
        const r = mergeLayerReroll(cur.layout, fresh.layout, zone, [activeDef.id]);
        setLayerMsg({ ok: r.ok, text: `${r.message}; seed ${rerollSeed}` });
        if (r.ok) pushStep(r.layout, `слой «${activeDef.name}» в зоне, seed ${rerollSeed}`);
        setRerollSeed((x) => x + 1);
      } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
      setBusy('');
    }, 20);
  };
  /** Click in the list: select the place and zoom the plan to it. */
  const zoomToChoke = (id: string) => {
    setSelChoke(id);
    const cp = analysis?.chokepoints.find((c) => c.id === id), el = planBox.current;
    if (!cp || !cur || !el) return;
    setVis((v) => (v.chokepoints === false ? { ...v, chokepoints: true } : v));
    const xs = cp.poly.map((q) => q[0]), ys = cp.poly.map((q) => q[1]), pad = 1500, w = el.clientWidth - 2, h = 518;
    setView(viewOfRect(planTransform(cur.layout.bounds, w, h), [Math.min(...xs) - pad, Math.min(...ys) - pad], [Math.max(...xs) + pad, Math.max(...ys) + pad], w, h));
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
            <label className="flex items-center justify-between gap-2 text-xs text-slate-300" title="Последний шаг генерации: роли зданий, истории (запертая дверь, завал + пожарная лестница, крыша снаружи, сугроб), ключевые маршруты; схема /2. Действует на генерацию, расширение и перегенерацию зоны.">Истории зданий (S1)
              <input type="checkbox" checked={storiesOn} onChange={(e) => setStoriesOn(e.target.checked)} />
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
                onClick={() => setVis((v) => ({ ...v, fire_lines: v.fire_lines === false }))}
                title="Слой «Линия огня» (то же, что галочка в панели слоёв)"
                className={`px-2.5 py-1 rounded-lg border text-xs ${showFiringLines ? 'bg-rose-900/60 border-rose-500 text-rose-200' : 'bg-slate-800 border-slate-700 text-slate-400 hover:text-slate-200'}`}
              >
                Линии огня
              </button>
              <button onClick={walk} disabled={!cur || !!busy} className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-slate-800 border border-slate-700 text-xs text-slate-200 hover:bg-slate-700 disabled:opacity-50"><Footprints size={13} />Прогнать отряд (симулятор)</button>
              {brokenBuildings > 0 && (
                <label className="inline-flex items-center gap-1 text-[11px] text-amber-300" title="Принять карту, хотя часть зданий не проходит проверку проходимости (этажи / крыши недостижимы)">
                  <input type="checkbox" checked={forceBroken} onChange={(e) => setForceBroken(e.target.checked)} />
                  принять с {brokenBuildings} сломанными зданиями
                </label>
              )}
              <button onClick={accept} disabled={!cur || !!busy || !cur.ev.route.completable || (!!cur.layout.objectives && !!objCheck && objCheck.errors.length > 0) || (brokenBuildings > 0 && !forceBroken)} title={cur?.layout.objectives && objCheck?.errors.length ? `Цели с ошибками: ${objCheck.errors.join('; ')}` : brokenBuildings > 0 && !forceBroken ? `${brokenBuildings} зданий не проходят проверку проходимости (панель «Здания»)` : ''} className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-emerald-700 text-xs text-white hover:bg-emerald-600 disabled:opacity-50"><ThumbsUp size={13} />Принять</button>
              <button onClick={() => buildNow(false)} disabled={!cur || !!busy || !isAccepted || building || (buildTarget !== null && !buildTarget.canBuild)} data-build
                title={!isAccepted ? 'Сначала «Принять»: собирается принятая карта (Content/Data/LevelGen/accepted)' : buildTarget && !buildTarget.canBuild ? buildTarget.reason : `Собрать ${buildTarget?.map ?? ''} и проверить маршрут ботом (фоном, ~5-15 мин)`}
                className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-orange-700 text-xs text-white hover:bg-orange-600 disabled:opacity-50"><Hammer size={13} />Собрать в Unreal</button>
              <button onClick={reject} disabled={!cur || !!busy} className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-rose-800 text-xs text-white hover:bg-rose-700 disabled:opacity-50"><ThumbsDown size={13} />Отклонить</button>
            </div>
            {cur && (isAccepted || (buildState && buildState.stage !== 'idle')) && (
              <div className="flex flex-col gap-1 bg-orange-950/30 border border-orange-900/70 rounded-xl p-2.5 text-xs" data-build-panel>
                <div className="flex flex-wrap items-center gap-2">
                  <span className="text-[11px] font-bold tracking-widest uppercase text-orange-300">Сборка в Unreal</span>
                  {buildTarget && <span className="text-slate-300">карта <span className="font-mono text-orange-200">{buildTarget.map}</span>{buildTarget.exists ? ' (уже есть)' : ' (новая)'}</span>}
                  <span className="text-[11px] text-slate-500" title={`levelgen_v2.json build.strip_prefix = ${V2.build.strip_prefix}`}>имя: L_Gen_ + имя раскладки без «gen_ref_NN_NN_», слова с заглавной (gen_ref_01_02_depot_1_x4 → L_Gen_Depot_1_X4)</span>
                </div>
                {isAccepted && acceptedSync === 'changed' && <p className="text-amber-300">Карта на странице изменена после «Принять»: собирается ПРИНЯТАЯ версия (accepted/{cur.layout.name}.json) — нажмите «Принять», чтобы собрать текущую.</p>}
                {isAccepted && acceptedSync === 'unknown' && <p className="text-amber-300">Собирается принятый файл accepted/{cur.layout.name}.json — на странице может быть другая версия с тем же именем (откройте её в «Принятые» или нажмите «Принять»).</p>}
                {buildTarget && !buildTarget.canBuild && <p className="text-rose-300"><XCircle size={12} className="inline mr-1" />{buildTarget.reason}</p>}
                {buildAsk && (
                  <div className="flex flex-wrap items-center gap-2 text-amber-200" data-build-confirm>
                    <AlertTriangle size={13} />Карта {buildAsk.map} уже собрана из этой раскладки ({buildAsk.file}). Заменить её?
                    <button type="button" onClick={() => buildNow(true)} className="px-2 py-0.5 rounded bg-orange-600 hover:bg-orange-500 text-white font-semibold">Заменить</button>
                    <button type="button" onClick={() => setBuildAsk(null)} className="px-2 py-0.5 rounded bg-slate-800 border border-slate-700 text-slate-200">Отмена</button>
                  </div>
                )}
                {buildErr && <p className="text-rose-300">{buildErr}</p>}
                {buildState && buildState.stage !== 'idle' && (
                  <div className="flex flex-col gap-0.5" data-build-state={buildState.stage}>
                    <p className={buildState.stage === 'failed' ? 'text-rose-300' : buildState.stage === 'done' ? 'text-emerald-300' : 'text-orange-200'}>
                      {buildState.stage === 'done' ? <CheckCircle2 size={12} className="inline mr-1" /> : buildState.stage === 'failed' ? <XCircle size={12} className="inline mr-1" /> : <Hammer size={12} className="inline mr-1 animate-pulse" />}
                      {buildState.name} → {buildState.map}: {buildState.stage === 'build' ? `1/2 сборка карты (${buildState.stage_elapsed_s} с)` : buildState.stage === 'smoke' ? `2/2 проверка маршрута ботом (${buildState.stage_elapsed_s} с)` : buildState.stage === 'done' ? 'готово' : 'ошибка'}; всего {Math.floor(buildState.elapsed_s / 60)} мин {buildState.elapsed_s % 60} с — {buildState.message}
                    </p>
                    {(buildState.result.built || buildState.stage === 'done' || buildState.stage === 'failed') && (
                      <p className="text-slate-300">Собрана: {buildState.result.built ? `да${buildState.result.actors ? ` (${buildState.result.actors} акторов)` : ''}` : 'нет'}; навигация: {buildState.result.nav_tiles || '—'}; маршрут: {buildState.result.route ? <span className={buildState.result.route === 'PASS' ? 'text-emerald-300 font-semibold' : 'text-rose-300 font-semibold'}>{buildState.result.route}</span> : '—'}{buildState.result.route_summary ? ` — ${buildState.result.route_summary}` : ''}{buildState.result.reason ? `; причина: ${buildState.result.reason}` : ''}</p>
                    )}
                    {buildState.result.built && <p className="text-slate-400">Открыть: в Unreal Content Browser → <span className="font-mono">{buildState.map}</span> (файл <span className="font-mono">{buildState.file}</span>); бой на ней — «Боевая лаборатория», карта {buildState.map.split('/').pop()}.</p>}
                    {!!buildState.log?.length && (
                      <details open={building} className="mt-1" data-build-log>
                        <summary className="cursor-pointer text-slate-400 select-none">Журнал сборки ({buildState.log.length} последних строк; скрипты идут без окон, полный журнал — Saved/LevelGen/build_{buildState.map.split('/').pop()}.log и route_….log)</summary>
                        <pre ref={buildLogRef} className="mt-1 max-h-48 overflow-auto whitespace-pre-wrap break-all bg-black/50 border border-slate-800 rounded p-1.5 text-[10px] leading-snug text-slate-300 font-mono">{buildState.log.join('\n')}</pre>
                      </details>
                    )}
                  </div>
                )}
                <p className="text-[11px] text-slate-500">Фоном, без окон: Scripts/levelgen_build_map.ps1 (карта + навигация, ~4-6 мин) → Scripts/levelgen_route_smoke.ps1 (бот проходит маршрут, ~6-8 мин для x4). Редактор этого checkout должен быть закрыт; одна сборка за раз; чужие карты не перезаписываются.</p>
              </div>
            )}
            {cur && (
              <div className="flex flex-col gap-1.5 bg-slate-950/60 border border-slate-800 rounded-xl p-2.5 text-xs">
                <div className="flex flex-wrap items-center gap-2">
                  <span className="text-[11px] font-bold tracking-widest uppercase text-slate-400">Зона</span>
                  <div className="inline-flex rounded-lg bg-slate-950 border border-slate-700 p-0.5 text-[11px]">
                    {([['off', 'Просмотр'], ['rect', 'Рамка'], ['poly', 'Многоугольник'], ['lasso', 'Лассо']] as const).map(([m, label]) => (
                      <button key={m} type="button" onClick={() => { setRegionMode(m); setGoalTool(false); }} className={`px-2 py-0.5 rounded ${regionMode === m ? 'bg-amber-600 text-slate-950 font-semibold' : 'text-slate-300 hover:text-white'}`}>{label}</button>
                    ))}
                  </div>
                  <button type="button" onClick={() => setZone(null)} disabled={!zone} className="inline-flex items-center gap-1 px-2 py-1 rounded-lg bg-slate-800 border border-slate-700 text-slate-300 hover:bg-slate-700 disabled:opacity-40"><Eraser size={12} />Сбросить</button>
                  <label className="flex items-center gap-1 text-slate-400">seed
                    <input type="number" value={rerollSeed} onChange={(e) => setRerollSeed(Math.max(0, Math.floor(Number(e.target.value) || 0)))} className="w-20 bg-slate-950 border border-slate-700 rounded px-1.5 py-0.5 text-slate-100" />
                  </label>
                  <label className="flex items-center gap-1 text-slate-400" title="Сколько раз повторить с новым seed, если маршрут ломается">повторов до
                    <input type="number" value={maxRetries} min={0} max={20} onChange={(e) => setMaxRetries(Math.max(0, Math.min(20, Math.floor(Number(e.target.value) || 0))))} className="w-12 bg-slate-950 border border-slate-700 rounded px-1.5 py-0.5 text-slate-100" />
                  </label>
                  <label className="flex items-center gap-1 text-slate-400" title="Полностью: квартал заново делится на участки, у каждого свой тип, здания другой формы, размера, отступа и ориентации (по хаосу). Тип квартала: квартал получает другой тип, но здания стоят на тех же местах. Только наполнение: те же здания, новые укрытия, точки, проёмы.">Что менять
                    <select value={blockMode} onChange={(e) => setBlockMode(e.target.value as BlockMode)} className="bg-slate-950 border border-slate-700 rounded px-1.5 py-0.5 text-slate-100" data-blockmode>
                      <option value="full">Полностью (форма и расстановка)</option>
                      <option value="types">Тип квартала (здания на тех же местах)</option>
                      <option value="jitter">Только наполнение (укрытия, точки)</option>
                    </select>
                  </label>
                  {blockMode === 'full' && (
                    <label className="flex items-center gap-1 text-slate-400" title="0 — ровная сетка: участки поровну, здания вровень с улицей; 1 — неровно: разные участки, отступы, промежутки, угловые участки могут смотреть на другую улицу">хаос
                      <span className="text-[10px] text-slate-500">ровно</span>
                      <input type="range" min={0} max={1} step={0.1} value={chaos} onChange={(e) => setChaos(Number(e.target.value))} className="w-24 accent-amber-500" data-chaos />
                      <span className="text-[10px] text-slate-500">хаотично</span>
                      <span className="font-mono text-slate-200 w-6">{chaos.toFixed(1)}</span>
                    </label>
                  )}
                  {blockMode !== 'jitter' && (
                    <label className="flex items-center gap-1 text-slate-400" title={`Макс. угол поворота зданий: каждый новый квартал (его здания, заборы, укрытия, лестницы) встаёт повёрнутым на случайный угол в пределах ±этого числа (в «Полностью» — ещё × хаос). 0 — всё по осям, как сейчас (промзона). Хранится в карте и действует и на «Расширить карту».`}>Макс. угол поворота зданий
                      <input type="number" min={0} max={ROTATION.max_deg_limit} step={5} value={maxRot} onChange={(e) => setMaxRot(Math.max(0, Math.min(ROTATION.max_deg_limit, Number(e.target.value) || 0)))} className="w-14 bg-slate-950 border border-slate-700 rounded px-1 py-0.5 text-slate-100 font-mono" data-maxrot />°
                    </label>
                  )}
                  <button type="button" onClick={reroll} disabled={!zone || !profileOk || !!busy} className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-amber-500 hover:bg-amber-400 text-slate-950 font-semibold disabled:opacity-40"><Shuffle size={13} />Перегенерировать зону</button>
                  <button type="button" onClick={() => stepHistory('undo')} disabled={!history || !canUndo(history) || !!busy} title={history && canUndo(history) ? `Отменить: ${history.present.label}` : ''} className="inline-flex items-center gap-1 px-2 py-1 rounded-lg bg-slate-800 border border-slate-700 text-slate-200 hover:bg-slate-700 disabled:opacity-40"><Undo2 size={13} />Отменить ({history?.past.length ?? 0})</button>
                  <button type="button" onClick={() => stepHistory('redo')} disabled={!history || !canRedo(history) || !!busy} title={history && canRedo(history) ? `Вернуть: ${history.future[0].label}` : ''} className="inline-flex items-center gap-1 px-2 py-1 rounded-lg bg-slate-800 border border-slate-700 text-slate-200 hover:bg-slate-700 disabled:opacity-40"><Redo2 size={13} />Вернуть ({history?.future.length ?? 0})</button>
                  <input value={lockName} onChange={(e) => setLockName(e.target.value)} placeholder="имя района" className="w-28 bg-slate-950 border border-slate-700 rounded px-1.5 py-0.5 text-slate-100" />
                  <button type="button" onClick={lockZone} disabled={!zone || !!busy} className="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-lg bg-emerald-800 hover:bg-emerald-700 text-white disabled:opacity-40"><Lock size={12} />Закрепить район</button>
                  <button type="button" onClick={saveLocksNow} disabled={!!busy} title="Записать закреплённые районы (многоугольники) в JSON карты: в принятую карту (только блок regen.locks), иначе — черновик Saved/TrainingGround/levelgen" className="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-lg bg-slate-800 border border-emerald-700 text-emerald-200 hover:bg-slate-700 disabled:opacity-40"><Save size={12} />Сохранить замки</button>
                </div>
                <p className="text-[11px] text-slate-500">{regionHint[regionMode]} Кварталы выбираются по ЦЕНТРУ (голубой пунктир — что изменится); «Полностью» делит квартал заново на участки. Перегенерируются объекты, чей центр внутри зоны (здания целиком); дороги, канал и закреплённые районы не трогаются. Закрепления хранятся в JSON карты (блок regen): кнопка «Сохранить замки» пишет только их, «Принять» — всю карту.</p>
                {locksSaved && <p className="text-[11px] text-emerald-300">{locksSaved}</p>}
                {!profileOk && <p className="text-amber-300">Для перегенерации нужен эталон {cur.layout.profile} — выберите его слева.</p>}
                {selection && (
                  <p className="text-slate-300">В зоне: перегенерируется <span className="text-cyan-300 font-mono">{selection.inside.length}</span> объектов (зданий {selection.inside.filter((g) => g.category === 'building').length}{region && region.plan.selLots.length ? (blockMode === 'full' ? `, кварталов переразбить ${region.plan.groups.length}` : `, кварталов с новым типом ${region.plan.selLots.length}`) : ''}), закреплено <span className="text-emerald-300 font-mono">{selection.locked.length}</span>, дороги/канал <span className="font-mono">{selection.fixed.length}</span>{region?.plan.anchored.length ? `; квартал старта/финиша сохраняет тип: ${region.plan.anchored.join(', ')}` : ''}.</p>
                )}
                {regenReport && (
                  <p className={regenReport.ok ? 'text-emerald-300' : 'text-rose-300'}>
                    {regenReport.ok ? <CheckCircle2 size={12} className="inline mr-1" /> : <XCircle size={12} className="inline mr-1" />}
                    {regenReport.message}; seed {regenReport.seed}; основной маршрут {regenReport.routes.find((r) => r.mandatory)?.completable ? 'проходим' : 'НЕ проходим'}; высоты подогнаны к классам: {regenReport.snappedHeights}{regenReport.skeletonMatch ? '' : '; скелет карты отличается от генератора (правила наложения сохраняют целостность)'}
                  </p>
                )}
                {regenReport && regenReport.retemplated.length > 0 && (
                  <p className="text-[11px] text-cyan-200">{regenReport.retemplated.some((x) => x.to.includes(' + ')) ? 'Кварталы заново (участки по типам): ' : 'Новые типы кварталов: '}{regenReport.retemplated.map((x) => `${x.id.replace(/^LOT_/, '')}: ${blockLabels(x.from)} → ${blockLabels(x.to) || 'пусто'}`).join('; ')}{regenReport.templateFailed.length ? `; без нового типа (не помещается): ${regenReport.templateFailed.map((x) => x.replace(/^LOT_/, '')).join(', ')}` : ''}{regenReport.removedRoutes.length ? `; убраны варианты маршрута: ${regenReport.removedRoutes.join(', ')}` : ''}</p>
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
                    Старт ({cur.layout.objectives.start.source === 'spawn' ? 'точка появления' : cur.layout.objectives.start.source === 'edge' ? 'край карты' : 'вручную'}) → цели в любом порядке: {objCheck.goals.map((g) => `${g.id.replace('GOAL_', '№')} ${g.error ? `ОШИБКА (${g.error})` : g.reachable ? `${g.length_m} м` : 'НЕДОСТИЖИМА'}`).join(', ')}; кратчайший обход {objCheck.bestOrder.order.length ? `${objCheck.bestOrder.order.map((x) => x.replace('GOAL_', '№')).join(' → ')} = ${objCheck.bestOrder.length_m} м` : '—'}{objCheck.goals.length > 1 ? `; мин. расстояние между целями ${objCheck.minSpacing_m} м` : ''}{objCheck.ok ? '' : ` — ${objCheck.errors.join('; ')}`}
                  </p>
                )}
                {objCheck && Object.keys(objWarnings).length > 0 && <p className="text-orange-300 text-[11px]">Внимание (правила расстановки): {Object.entries(objWarnings).map(([id, w]) => `${id.replace('GOAL_', '№')}: ${w}`).join('; ')}</p>}
                {objMsg && <p className={`text-[11px] ${objMsg.ok ? 'text-yellow-200' : 'text-rose-300'}`}>{objMsg.text}</p>}
                <p className="text-[11px] text-slate-500">Цели вручную: «+ цель» над планом, затем щелчок по плану — новая цель; старт (зелёный S) и цели перетаскиваются в режиме «Просмотр»; правая кнопка или × — удалить цель (от {V2.objectives.manual_count_min} до {V2.objectives.count_max}). Красное — недопустимое место (причина рядом), точку никто не двигает за вас; «Принять» ждёт, пока ошибок не будет.</p>
                <p className="text-[11px] text-slate-500">Расширение: старая карта остаётся в центре без изменений (кроме проёма в заборе, если он перегораживает продолжение дороги), кварталы вокруг — из каталога levelgen_v2.json; их тоже можно перегенерировать зоной. Цели — на земле, достижимы со старта, разнесены, не в закреплённых зданиях; хранятся в JSON карты (блок objectives).</p>
              </div>
            )}
            <div className={`grid gap-3 ${showRef ? 'lg:grid-cols-2' : ''}`}>
              <div>
                <div className="flex flex-wrap items-center gap-1.5 mb-1 text-[11px]">
                  <button type="button" onClick={() => zoomBy(1 / PLAN_VIEW.button_step)} disabled={!cur} title="Отдалить" className="p-1 rounded bg-slate-800 border border-slate-700 text-slate-200 hover:bg-slate-700 disabled:opacity-40"><ZoomOut size={13} /></button>
                  <span className="font-mono text-slate-300 w-12 text-center" data-zoom>{Math.round(view.zoom * 100)} %</span>
                  <button type="button" onClick={() => zoomBy(PLAN_VIEW.button_step)} disabled={!cur} title="Приблизить" className="p-1 rounded bg-slate-800 border border-slate-700 text-slate-200 hover:bg-slate-700 disabled:opacity-40"><ZoomIn size={13} /></button>
                  <button type="button" onClick={() => setView(FIT_VIEW)} disabled={!cur} title="Вписать карту целиком" className="inline-flex items-center gap-1 px-2 py-1 rounded bg-slate-800 border border-slate-700 text-slate-200 hover:bg-slate-700 disabled:opacity-40"><Maximize2 size={12} />Вписать</button>
                  <button type="button" onClick={() => { setGoalTool((v) => !v); setRegionMode('off'); }} disabled={!cur} title="Щелчок по плану добавляет цель" className={`inline-flex items-center gap-1 px-2 py-1 rounded border disabled:opacity-40 ${goalTool ? 'bg-yellow-500 border-yellow-400 text-slate-950 font-semibold' : 'bg-slate-800 border-slate-700 text-yellow-200 hover:bg-slate-700'}`}><MapPinPlus size={12} />+ цель</button>
                  <button type="button" onClick={() => setShowRef((v) => !v)} title="Показать / скрыть эталонную картинку справа от плана (без неё план занимает всю ширину)" className="px-2 py-1 rounded bg-slate-800 border border-slate-700 text-slate-300 hover:bg-slate-700" data-ref-toggle>{showRef ? 'скрыть эталон' : 'показать эталон'}</button>
                  <span className="text-slate-500">колесо — масштаб у курсора; сдвиг — средняя кнопка или пробел + перетаскивание (в «Просмотре» — левой)</span>
                </div>
                <div ref={planBox}>
                <RegionOverlay bounds={cur?.layout.bounds ?? null} height={520} mode={goalTool ? 'off' : regionMode} zone={zone} locks={locks} blocks={region?.plan.selLots.map((k) => k.rect)}
                  inside={selection?.inside.map((g) => g.centre)} locked={selection?.locked.map((g) => g.centre)} onZone={setZone}
                  view={view} onView={setView} objectives={cur?.layout.objectives ?? null} objErrors={objErrors} objWarnings={objWarnings} bestPath={objCheck?.bestPath}
                  addGoal={goalTool} onObjective={cur ? onObjective : undefined} squad={squadNow?.members ?? null} trail={squadNow?.trail ?? null} trails={squadNow?.trails ?? null}
                  objectivesEditable={!activeLayer || activeLayer === 'objectives'} showObjectives={vis.objectives !== false}
                  layerTool={activeDef && activeDef.id !== 'objectives' && !goalTool ? layerTool : 'off'} pickLayer={pickLayer} onLayerAction={onLayerAction}
                  overlay={(h) => <LayersOverlay h={h} vis={vis} analysis={analysis} selChoke={selChoke} onSelChoke={setSelChoke} props={propsOnMap} selected={selectedEntity} activeEntities={activeEntities} />}>
                  <LayoutView ref={view0} layout={cur?.layout ?? null} layer={layer} floorMode={floorMode} firingLines={cur?.ev.tactical?.firingLines.lines} showFiringLines={showFiringLines} height={520} marks={marks} view={view} hideObjectives hidden={hidden} tints={tints} />
                </RegionOverlay>
                </div>
                {cur && (track ? (
                  <div className="mt-1.5 flex flex-col gap-1 bg-slate-950/60 border border-pink-900/60 rounded-lg p-2 text-[11px]" data-player>
                    <div className="flex flex-wrap items-center gap-2">
                      <button type="button" onClick={() => { if (playT >= track.duration_s) setPlayT(0); setPlaying((v) => !v); }} className="inline-flex items-center gap-1 px-2 py-1 rounded bg-pink-600 hover:bg-pink-500 text-white font-semibold" data-play>{playing ? <Pause size={12} /> : <Play size={12} />}{playing ? 'Пауза' : 'Играть'}</button>
                      <div className="inline-flex rounded bg-slate-950 border border-slate-700 p-0.5">
                        {PLAYBACK.speeds.map((x) => <button key={x} type="button" onClick={() => setSpeed(x)} className={`px-1.5 rounded ${speed === x ? 'bg-pink-700 text-white' : 'text-slate-300 hover:text-white'}`}>x{x}</button>)}
                      </div>
                      <input type="range" min={0} max={track.duration_s} step={0.1} value={playT} onChange={(e) => { setPlayT(Number(e.target.value)); }} className="flex-1 min-w-[120px] accent-pink-500" data-scrub />
                      <span className="font-mono text-slate-200">{clockLabel(playT)} / {clockLabel(track.duration_s)}</span>
                      {squadNow && <span className="text-slate-400">участок {squadNow.leg + 1}/{track.legs.length}, строй {squadNow.formation}</span>}
                    </div>
                    {trackSummary && <p className="text-slate-400">Прогон: {trackSummary.members} бойца, {trackSummary.duration_s} с, лидер прошёл {trackSummary.leader_m} м по {trackSummary.legs} наземным участкам (крыши / лестницы симулятор пропускает — точки перескакивают); строи: {Object.entries(trackSummary.formations).map(([k, v]) => `${k} ${pct(v)}`).join(', ')}; углы {cur.ev.mechanics.cornerWatches}, раскладок у стены {cur.ev.mechanics.wallLayouts}.</p>}
                  </div>
                ) : cur.simmed ? <p className="mt-1 text-[11px] text-slate-500">Симулятор не прошёл маршрут (основной маршрут не проходим) — воспроизводить нечего.</p> : <p className="mt-1 text-[11px] text-slate-500">«Прогнать отряд (симулятор)» — после прогона здесь появится проигрыватель: отряд идёт по маршруту на плане.</p>)}
              </div>
              {showRef && (<div>
                <p className="text-[11px] text-slate-500 mb-1">Эталон (карта дизайнера, развёрнутая сверху; синее — его маршрут, жёлтое — лестницы, зелёный круг — старт)</p>
                {refOk ? <img src="/api/levelgen/ref-image" alt="reference" onError={() => setRefOk(false)} className="rounded-xl border border-slate-800 w-full" style={{ maxHeight: 520, objectFit: 'contain', background: '#191c22' }} />
                  : <p className="text-xs text-amber-300">Эталонная картинка недоступна (Saved/MapRefs или python/PIL).</p>}
              </div>)}
            </div>
          </section>

          <div className="flex flex-col gap-6 min-w-0">
          {cur && assignment && (
            <LayersPanel counts={layerCountsNow} vis={vis} onVis={setVis} active={activeLayer} onActive={(id) => { setActiveLayer(id); if (id) { setRegionMode('off'); setGoalTool(false); } }}
              tool={layerTool} onTool={setLayerTool} addIdx={addIdx} onAddIdx={setAddIdx} tintOn={tintOn} onTint={setTintOn} selected={selectedEntity} onDelete={deleteSelected}
              onRotate={(deg) => { if (selKey && !selKey.startsWith('prop:')) applyLayout({ op: 'rotate', key: selKey, deg }); }}
              onRerollLayer={zone && profileOk && activeDef?.size_class ? rerollLayer : null} message={layerMsg}
              propsInfo={propsState ? { map: propsState.map, path: propsState.path, revision: propsState.file.revision, frozen: propsState.file.frozen, dirty: propsState.dirty } : null} propsNote={NO_PROPS} onSaveProps={saveProps} busy={!!busy}
              analysis={analysis} analysisBusy={analysisBusy} conditions={conditions} onConditions={setConditions}
              onAnalyze={() => { if (cur) { setCandidates((cs) => cs.map((c, i) => (i === sel ? { ...c, analysis: undefined } : c))); runAnalysis(cur.layout, sel); } }}
              selChoke={selChoke} onZoomChoke={zoomToChoke} threat={threat} config={CHOKE} />
          )}
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
              {cur.ev.buildings && <BuildingsPanel report={cur.ev.buildings} summary={storySummary} message={storyMsg}
                onReroll={storyState ? rerollStory : undefined} isLocked={(id) => lockedBuildings.has(id)} canReroll={(id) => !!storyState?.base[id]} />}
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
        </div>
      )}
    </div>
  );
};
