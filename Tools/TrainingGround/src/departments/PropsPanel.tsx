// Training Ground department «Пропсы уровней» (docs/design/levelgen/PROPS_LAYERS.md section 4.1; Step 4 of 010-night-queue.md).
// Complete shell for viewing, re-rolling, locking, inspecting, and canonically saving props layers (codex.levelgen_props/1).
// T-023: zoom / pan / F11 full window on the plan (planView.ts + the T-022 FullWindowFrame), a re-roll INSIDE a drawn zone (the map page's
// RegionOverlay box / polygon / lasso -> levelgen/props/zoneReroll.ts), drag + arrow-key move of a prop with auto-lock (propsEdit.ts), the
// validator + fast gate after every edit, undo / redo, and the unsaved-edits guards. The file is still written by «Сохранить» only
// (PUT /api/levelgen/props with the revision check): the game reads that JSON at OnWorldBeginPlay, so a saved move is what Unreal spawns.

import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
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
  ZoomIn,
  ZoomOut,
  Maximize2,
  Expand,
  Undo2,
  Redo2,
  Eraser,
  Grid3x3,
} from 'lucide-react';

import type { GenLayout } from '../levelgen/schema.ts';
import { layersOf } from '../levelgen/schema.ts';
import type { LevelPropsFile } from '../levelgen/props/schema.ts';
import { PropsLayoutView } from '../levelgen/props/layoutView.ts';
import { rerollPropsLayers, rerollSupport } from '../levelgen/props/rerollKinds.ts';
import type { PropsAutogenConfig } from '../levelgen/props/autogenConfig.ts';
import { DEFAULT_AUTOGEN_CONFIG } from '../levelgen/props/autogenConfig.ts';
import { zonePreview } from '../levelgen/props/zoneReroll.ts';
import { checkProps, DEFAULT_PROP_SNAP_CM, findProp, moveProp, moveRefusal, newIssues, pickProp, propRadius, PROP_SNAP_CM, setPropLocked, withGate, type PropsCheck } from '../levelgen/props/propsEdit.ts';
import type { PropsValidationIssue } from '../levelgen/props/validator.ts';
import { nudgeDelta, NUDGE_CM } from '../levelgen/mapEdit.ts';
import { canRedo, canUndo, historyInit, historyPush, historyPushCoalesced, historyRedo, historyUndo, type History } from '../levelgen/regenHistory.ts';
import { FIT_VIEW, zoomAt, type PlanView } from '../levelgen/planView.ts';
import { PLAN_VIEW } from '../levelgen/planView.ts';
import type { Vec2 } from '../movement/geometry.ts';
import { isKeyConsumer, planHeightFor } from '../utils/fullWindow.ts';
import { setUnsaved } from '../utils/unsavedGuard.ts';
import { FullWindowFrame, useFullWindow } from '../components/FullWindowFrame.tsx';
import { PropsCanvas, type PropsCanvasHandle } from './PropsCanvas.tsx';
import { RegionOverlay, type LayerAction, type LayerHit, type RegionMode } from './RegionOverlay.tsx';
import {
  acceptPropsLoad,
  applyPropsLoad,
  missingPropsHint,
  propsLoadId,
  propsMapMode,
  propsSaveRequest,
} from './propsPanelMap.ts';

/** Props file the panel shows: the CURRENT MAP of the department (T-020; no file picker, no Depot default). */
export interface PropsPanelProps {
  /** The current map's name (for the headline). */
  mapName: string | null;
  /** The current map's props file id (`<id>.props.json`), '' / null when the map has none. */
  propsId: string | null;
  /** Accepted layout id. Used only when «Сохранить» creates the map's first props file. */
  layoutId?: string | null;
  /** The panel has unsaved props edits (the page asks before switching the map). */
  onDirtyChange?: (dirty: boolean) => void;
}

const NO_LOCKS: never[] = [];
const fmtM = (cm: number) => `${cm >= 0 ? '+' : ''}${(cm / 100).toFixed(2)}`;

export const PropsPanel: React.FC<PropsPanelProps> = ({ mapName, propsId, layoutId = null, onDirtyChange }) => {
  const selectedMap = propsId ?? '';
  // the props file with its undo / redo history (regenHistory.ts): every hand edit and re-roll is a step
  const [hist, setHist] = useState<History<LevelPropsFile> | null>(null);
  const propsFile = hist?.present.state ?? null;
  const histRef = useRef(hist);
  histRef.current = hist;
  const [layout, setLayout] = useState<GenLayout | null>(null);
  const [balance, setBalance] = useState<Record<string, unknown> | null>(null);
  const [autogenCfg, setAutogenCfg] = useState<PropsAutogenConfig>(DEFAULT_AUTOGEN_CONFIG);
  const [filePath, setFilePath] = useState<string>('');

  const [visibleLayers, setVisibleLayers] = useState<Record<string, boolean>>({});
  const [layerFilter, setLayerFilter] = useState<string>('all');
  const [showExclusions, setShowExclusions] = useState<boolean>(false);

  const [selectedId, setSelectedId] = useState<string | null>(null);
  const selectedHit = propsFile && selectedId ? findProp(propsFile, selectedId) : null;
  const selectedProp = selectedHit?.obj ?? null;

  const [isDirty, setIsDirty] = useState<boolean>(false);
  const [busy, setBusy] = useState<string>('');
  const [error, setError] = useState<string>('');
  const [conflict, setConflict] = useState<{ message: string; serverRev?: number } | null>(null);
  const [saveSuccess, setSaveSuccess] = useState<string>('');
  /** What the last re-roll did (the 088 clearance, validator drops, skipped layers). */
  const [rerollNote, setRerollNote] = useState<{ ok: boolean; text: string } | null>(null);
  /** What the last hand move did (or why it was refused). */
  const [moveNote, setMoveNote] = useState<{ ok: boolean; text: string } | null>(null);

  // T-023: plan view (zoom / pan), the tool (move props / a zone shape), the zone, the layers a zone re-roll takes, the seed, the snap
  const [view, setView] = useState<PlanView>(FIT_VIEW);
  const [tool, setTool] = useState<'move' | RegionMode>('move');
  const [zone, setZone] = useState<Vec2[] | null>(null);
  const [zoneOff, setZoneOff] = useState<Record<string, boolean>>({});
  const [zoneSeed, setZoneSeed] = useState<number>(1001);
  const [snapOn, setSnapOn] = useState<boolean>(false);
  const [snapStep, setSnapStep] = useState<number>(DEFAULT_PROP_SNAP_CM);
  const [vh, setVh] = useState(() => (typeof window === 'undefined' ? 900 : window.innerHeight));
  useEffect(() => { const f = () => setVh(window.innerHeight); window.addEventListener('resize', f); return () => window.removeEventListener('resize', f); }, []);
  // F11 / «На весь экран»: the plan fills the browser window (Esc / F11 back; Esc stays the polygon tool's own while it draws)
  const { full, toggle: toggleFull, setFull } = useFullWindow(true, tool !== 'poly');
  const planBox = useRef<HTMLDivElement>(null);

  /** The check after the edits (validator + fast gate), the file's issues as loaded, and the layers whose gate verdict the edits changed. */
  const [check, setCheck] = useState<PropsCheck | null>(null);
  const [checkBusy, setCheckBusy] = useState(false);
  const baselineRef = useRef<PropsValidationIssue[] | null>(null);
  const editedLayersRef = useRef<Set<string>>(new Set());
  const gatePatchedRef = useRef<LevelPropsFile | null>(null);

  const canvasRef = useRef<PropsCanvasHandle>(null);

  useEffect(() => { onDirtyChange?.(isDirty); }, [isDirty, onDirtyChange]);
  // unsaved edits: the department switch (App level) and the browser's reload / close ask first
  useEffect(() => {
    setUnsaved('props', isDirty ? `пропсы карты ${mapName ?? ''}` : null);
    if (!isDirty) return;
    const warn = (e: BeforeUnloadEvent) => { e.preventDefault(); e.returnValue = ''; };
    window.addEventListener('beforeunload', warn);
    return () => window.removeEventListener('beforeunload', warn);
  }, [isDirty, mapName]);
  useEffect(() => () => setUnsaved('props', null), []);

  /** A new step in the history (the file after a hand edit / a re-roll); `coalesce` merges a run of nudges into one. */
  const commit = useCallback((next: LevelPropsFile, label: string, coalesce?: string) => {
    setHist((h) => (h ? (coalesce ? historyPushCoalesced(h, next, label, coalesce) : historyPush(h, next, label)) : h));
    setIsDirty(true);
    setSaveSuccess('');
  }, []);
  /** A change of the present file that is not an edit step (the saved revision, the gate verdict). */
  const patchPresent = useCallback((fn: (f: LevelPropsFile) => LevelPropsFile) => {
    setHist((h) => (h ? { ...h, present: { ...h.present, state: fn(h.present.state) } } : h));
  }, []);
  const stepHistory = (dir: 'undo' | 'redo') => {
    setHist((h) => (h ? (dir === 'undo' ? historyUndo(h) : historyRedo(h)) : h));
    setIsDirty(true);
    setMoveNote(null);
  };

  // 2. Load the CURRENT map's props. A map with no file stays empty. A late GET for the previous map is ignored (task 097).
  const loadGen = useRef(0);
  const clearLoadedProps = () => {
    setHist(null);
    setLayout(null);
    setBalance(null);
    setFilePath('');
    setIsDirty(false);
    setSelectedId(null);
    setRerollNote(null);
    setMoveNote(null);
    setZone(null);
    setView(FIT_VIEW);
    setCheck(null);
    setError('');
    setConflict(null);
    setSaveSuccess('');
    setVisibleLayers({});
    setBusy('');
    baselineRef.current = null;
    editedLayersRef.current = new Set();
  };
  const startLoad = useCallback((id: string, gen: number) => {
    if (!id) return;
    setBusy('Загрузка пропсов...');
    setError('');
    setConflict(null);
    setSaveSuccess('');
    setSelectedId(null);
    const modeMap = mapName;
    const modeProps = propsId;
    fetch(`/api/levelgen/props?map=${encodeURIComponent(id)}`)
      .then(async (r) => {
        const data = await r.json();
        // 404 is the empty-map case (no props file yet), not a failure that should keep the previous canvas.
        if (r.status === 404) return null;
        if (!r.ok) throw new Error(data.error ?? r.statusText);
        return data as { props?: LevelPropsFile; layout?: GenLayout | null; balance?: Record<string, unknown> | null; autogen?: PropsAutogenConfig | null; filePath?: string };
      })
      .then((data) => {
        if (!acceptPropsLoad(gen, loadGen.current)) return;
        if (!data?.props || !Array.isArray(data.props.layers)) return;
        const next = applyPropsLoad(
          { mapName: modeMap, propsId: modeProps, file: null, loadGen: loadGen.current, hint: null },
          gen,
          data.props,
        );
        if (!next.file) return;
        setHist(historyInit(next.file));
        setLayout(data.layout ?? null);
        setBalance(data.balance ?? null);
        setAutogenCfg(data.autogen ?? DEFAULT_AUTOGEN_CONFIG);
        setFilePath(data.filePath ?? '');
        setRerollNote(null);
        setMoveNote(null);
        setZone(null);
        setView(FIT_VIEW);
        setCheck(null);
        baselineRef.current = null;
        editedLayersRef.current = new Set();
        const vis: Record<string, boolean> = {};
        next.file.layers.forEach((l) => { vis[l.id] = l.enabled !== false; });
        setVisibleLayers(vis);
        setIsDirty(false);
      })
      .catch((err) => {
        if (!acceptPropsLoad(gen, loadGen.current)) return;
        setError(`Не удалось загрузить ${id}: ${err instanceof Error ? err.message : String(err)}`);
      })
      .finally(() => {
        if (acceptPropsLoad(gen, loadGen.current)) setBusy('');
      });
  }, [mapName, propsId]);
  const loadMapProps = (id: string) => {
    if (!id) return;
    startLoad(id, ++loadGen.current);
  };
  useEffect(() => {
    const gen = ++loadGen.current;
    clearLoadedProps();
    const id = propsLoadId(mapName, propsId);
    if (!id) return;
    startLoad(id, gen);
  }, [mapName, propsId, startLoad]);

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

  const failingLayer = useMemo(
    () => propsFile?.layers.find((l) => l.report?.gate === 'fail'),
    [propsFile]
  );

  // 3b. The validator + the fast gate after the edits (debounced: the gate costs seconds on a big map)
  useEffect(() => {
    if (!propsFile || !layoutView || !layout || !balance) return;
    if (gatePatchedRef.current === propsFile) { gatePatchedRef.current = null; return; }
    setCheckBusy(true);
    const snapshot = propsFile;
    const timer = setTimeout(() => {
      try {
        const c = checkProps(snapshot, layoutView, layout, balance);
        if (baselineRef.current === null) baselineRef.current = c.issues;
        setCheck(c);
        // the layers the hand edits touched show the verdict of the whole file now
        const ids = [...editedLayersRef.current];
        if (ids.length > 0 && withGate(snapshot, c.gate, ids) !== snapshot) {
          setHist((h) => {
            if (!h || h.present.state !== snapshot) return h;
            const patched = withGate(snapshot, c.gate, ids);
            gatePatchedRef.current = patched;
            return { ...h, present: { ...h.present, state: patched } };
          });
        }
      } catch (e) {
        console.warn('props check failed:', e);
      } finally {
        setCheckBusy(false);
      }
    }, 350);
    return () => { clearTimeout(timer); };
  }, [propsFile, layoutView, layout, balance]);

  const addedIssues = useMemo(() => (check && baselineRef.current ? newIssues(baselineRef.current, check.issues) : []), [check]);

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
    commit({ ...propsFile, layers: propsFile.layers.map((l) => (l.id === layerId ? { ...l, locked: !l.locked } : l)) }, `замок слоя ${layerId}`);
  };

  // 6. Update seed for layer
  const setLayerSeed = (layerId: string, newSeed: number) => {
    if (!propsFile) return;
    commit({ ...propsFile, layers: propsFile.layers.map((l) => (l.id === layerId ? { ...l, seed: Math.max(0, newSeed) } : l)) }, `seed ${layerId}`, `seed:${layerId}`);
  };

  // 7. Re-roll (T-020: every kind the autogen supports - crates, barrels, turrets, mines, loot, power - with the 088 route clearance)
  const describeOutcome = (out: ReturnType<typeof rerollPropsLayers>): { ok: boolean; text: string } => {
    const parts: string[] = [];
    if (out.rerolled.length > 0) parts.push(`Переброшено: ${out.rerolled.join(', ')}.`);
    if (out.zone) parts.push(`В зоне: убрано ${out.zone.removed}, поставлено ${out.zone.added}${out.zone.droppedSpacing ? ` (ещё ${out.zone.droppedSpacing} не встали: слишком близко к соседям у границы)` : ''}; вне зоны не тронуто ${out.zone.keptOutside}, закреплено в зоне ${out.zone.keptPinned}.`);
    const c = out.clearance;
    if (c.droppedTotal > 0) {
      parts.push(`Трасса (088) убрала ${c.droppedTotal}: ящиков ${c.droppedCrates}, бочек ${c.droppedBarrels}, мин ${c.droppedMines}, лута ${c.droppedLoot}.`);
    } else if (out.rerolled.some((id) => id !== 'power')) {
      parts.push('Трасса (088) свободна: ничего убирать не пришлось.');
    }
    if (out.droppedByValidator > 0) parts.push(`Валидатор игры убрал ${out.droppedByValidator}.`);
    if (out.powerRebuilt) parts.push('Сеть питания пересобрана.');
    if (c.pmcChargesInside.length > 0) parts.push(`Заряды ЧВК на трассе: ${c.pmcChargesInside.join(', ')}.`);
    for (const n of out.notes) parts.push(n + '.');
    for (const sk of out.skipped) parts.push(`${sk.id}: ${sk.reason}.`);
    return { ok: out.rerolled.length > 0, text: parts.join(' ') || 'Нечего перебрасывать.' };
  };

  const runReroll = (ids: string[], seedFor: (l: { seed?: number }) => number, label: string, zonePoly?: Vec2[]) => {
    if (!propsFile || !balance || !layoutView || !layout) {
      setError('Невозможно перебросить: отсутствует модель уровня или баланса');
      return;
    }
    setBusy(label);
    setError('');
    setRerollNote(null);
    setTimeout(() => {
      try {
        // Deep clone the file before mutating
        const cloned: LevelPropsFile = JSON.parse(JSON.stringify(propsFile));
        const out = rerollPropsLayers(cloned, balance, layoutView, layout, { ids, seedFor, zone: zonePoly }, autogenCfg);
        if (out.rerolled.length > 0) {
          for (const id of out.rerolled) editedLayersRef.current.add(id);
          commit(cloned, zonePoly ? `зона: ${out.rerolled.join(', ')}` : `переброска: ${out.rerolled.join(', ')}`);
          setVisibleLayers((prev) => {
            const next = { ...prev };
            for (const l of cloned.layers) if (next[l.id] === undefined) next[l.id] = l.enabled !== false;
            return next;
          });
        }
        setRerollNote(describeOutcome(out));
      } catch (e) {
        setError(`Ошибка переброски: ${e instanceof Error ? e.message : String(e)}`);
      } finally {
        setBusy('');
      }
    }, 15);
  };

  const handleRerollLayer = (layerId: string, customSeed?: number) => {
    const layer = propsFile?.layers.find((l) => l.id === layerId);
    if (!layer) return;
    const verdict = rerollSupport(layer, propsFile?.frozen);
    if (!verdict.ok) {
      setRerollNote({ ok: false, text: `${layerId}: ${verdict.reason}.` });
      return;
    }
    runReroll([layerId], (l) => customSeed ?? (l.seed ?? 1) + 1, `Переброска слоя ${layerId}...`);
  };

  // 8. Re-roll all unlocked layers (the locked ones and pmc_charges stay)
  const handleRerollAll = () => {
    if (!propsFile) return;
    const ids = propsFile.layers.filter((l) => l.enabled !== false && !l.locked && rerollSupport(l, propsFile.frozen).ok).map((l) => l.id);
    runReroll(ids, (l) => (l.seed ?? 1) + 1, 'Генерация всех слоев...');
  };

  // 8b. T-023: re-roll the chosen layers ONLY inside the drawn zone, with the zone seed
  const zoneLayerIds = useMemo(
    () => (propsFile ? propsFile.layers.filter((l) => l.enabled !== false && !l.locked && rerollSupport(l, propsFile.frozen).ok && l.kind !== 'power' && !zoneOff[l.id]).map((l) => l.id) : []),
    [propsFile, zoneOff]
  );
  const preview = useMemo(() => (propsFile && zone ? zonePreview(propsFile, zoneLayerIds, zone) : null), [propsFile, zone, zoneLayerIds]);
  const handleRerollZone = () => {
    if (!zone || zoneLayerIds.length === 0) return;
    runReroll(zoneLayerIds, () => zoneSeed, `Переброска в зоне (seed ${zoneSeed})...`, zone);
    setZoneSeed((x) => x + 1);
  };

  // 9. Lock / unlock the selected object (a hand move locks it by itself)
  const toggleSelectedPropLock = () => {
    if (!propsFile || !selectedProp) return;
    const f = findProp(propsFile, selectedProp.id);
    if (f) editedLayersRef.current.add(f.layer.id);
    commit(setPropLocked(propsFile, selectedProp.id, !selectedProp.locked), selectedProp.locked ? `снять замок ${selectedProp.id}` : `замок ${selectedProp.id}`);
  };

  // 9b. T-023: the hand move of a prop (a drag on the plan / the arrow keys): snaps to the walk surface, pins the prop, goes into the history
  const applyMove = useCallback((id: string, delta: Vec2, coalesce?: string) => {
    const file = histRef.current?.present.state;
    if (!file || !layoutView) return;
    const r = moveProp(file, layoutView, id, delta, balance ?? undefined);
    if (!r.ok) { setMoveNote({ ok: false, text: r.reason }); return; }
    const f = findProp(r.file, id);
    if (f) editedLayersRef.current.add(f.layer.id);
    for (const c of r.carried) { const cf = findProp(r.file, c); if (cf) editedLayersRef.current.add(cf.layer.id); }
    commit(r.file, `${id} ${fmtM(delta[0])} м, ${fmtM(delta[1])} м`, coalesce);
    setMoveNote({ ok: true, text: `${id}: на [${r.pos[0]}, ${r.pos[1]}] см, слой ${r.walkLayer}${r.changedLayer ? ' (сменился)' : ''}; закреплён замком${r.carried.length ? `; вместе с ним: ${r.carried.join(', ')}` : ''}. Замок можно снять.` });
  }, [layoutView, balance, commit]);

  const onLayerAction = (a: LayerAction) => {
    if (a.kind === 'select') setSelectedId(a.key);
    else if (a.kind === 'move') applyMove(a.key, a.delta);
  };
  const pickLayer = (p: Vec2, tolCm: number): LayerHit | null => {
    if (!propsFile) return null;
    const hit = pickProp(propsFile, p, tolCm, { visibleLayers, layerFilter, balance: balance ?? undefined });
    if (!hit) return null;
    return { key: hit.obj.id, movable: moveRefusal(propsFile, hit.obj.id) === null, shape: { kind: 'point', r: propRadius(hit.obj, balance ?? undefined) }, ref: hit.obj.pos };
  };

  // arrow keys nudge the selected prop (10 cm / Shift 1 m / Ctrl 10 m), Ctrl+Z / Ctrl+Y undo / redo; a field that types keeps its keys
  const keyRef = useRef<(e: KeyboardEvent) => boolean>(() => false);
  keyRef.current = (e) => {
    if (busy) return false;
    if ((e.ctrlKey || e.metaKey) && !e.altKey && ['z', 'Z', 'y', 'Y'].includes(e.key)) {
      const redo = e.key === 'y' || e.key === 'Y' || e.shiftKey;
      const h = histRef.current;
      if (!h || (redo ? !canRedo(h) : !canUndo(h))) return false;
      stepHistory(redo ? 'redo' : 'undo');
      return true;
    }
    const d = nudgeDelta(e.key, { shift: e.shiftKey, ctrl: e.ctrlKey || e.metaKey });
    if (!d || e.altKey || !selectedId || tool !== 'move') return false;
    applyMove(selectedId, d, `nudge:${selectedId}`);
    return true;
  };
  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      if (isKeyConsumer(e.target)) return;
      if (keyRef.current(e)) e.preventDefault();
    };
    window.addEventListener('keydown', key);
    return () => window.removeEventListener('keydown', key);
  }, []);

  // 10. Save props with revision check (§4.1). Task 097: the target is always the current map.
  // A map with no file sends base_revision 0 and the server creates props/<map>.props.json. Never map="".
  const handleSave = async () => {
    if (propsFile?.frozen) {
      setError('Файл заморожен (frozen: true). Сохранение заблокировано.');
      return;
    }
    const plan = propsSaveRequest({ mapName, propsId, file: propsFile, layoutId });
    if ('error' in plan || !plan.map) {
      setError('error' in plan ? plan.error : 'Не понятно, в какую карту сохранять.');
      return;
    }

    setBusy('Сохранение на диск...');
    setError('');
    setConflict(null);
    setSaveSuccess('');

    try {
      const response = await fetch(`/api/levelgen/props?map=${encodeURIComponent(plan.map)}`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(plan.body),
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

      // Success. A create has no history step yet: take the document the server wrote.
      if (data.props && typeof data.props === 'object') {
        setHist(historyInit(data.props as LevelPropsFile));
        if (!propsId) setFilePath(`Content/Data/LevelGen/props/${plan.map}.props.json`);
      } else {
        patchPresent((f) => ({ ...f, revision: data.revision }));
      }
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

  // ---- the pieces the normal page and the full-window header share --------------------------------------------------------------------
  const planHeight = full ? planHeightFor(vh, 215, 320) : planHeightFor(vh, 330, 560);
  const zoomBy = (f: number) => setView((v) => zoomAt(v, [planBox.current ? planBox.current.clientWidth / 2 : 0, planBox.current ? planBox.current.clientHeight / 2 : 260], f));
  const btn = 'inline-flex items-center gap-1 px-2 py-1 rounded bg-slate-800 border border-slate-700 text-slate-200 hover:bg-slate-700 disabled:opacity-40';
  const undoRedo = (
    <>
      <button type="button" onClick={() => stepHistory('undo')} disabled={!hist || !canUndo(hist) || !!busy} title={hist && canUndo(hist) ? `Отменить: ${hist.present.label} (Ctrl+Z)` : ''} className={btn} data-props-undo><Undo2 size={13} />Отменить ({hist?.past.length ?? 0})</button>
      <button type="button" onClick={() => stepHistory('redo')} disabled={!hist || !canRedo(hist) || !!busy} title={hist && canRedo(hist) ? `Вернуть: ${hist.future[0].label} (Ctrl+Y)` : ''} className={btn} data-props-redo><Redo2 size={13} />Вернуть ({hist?.future.length ?? 0})</button>
    </>
  );
  const creatingFile = propsMapMode(mapName, propsId).kind === 'empty' && !propsFile;
  const saveDisabled = !!propsFile?.frozen || !!busy || (!isDirty && !creatingFile) || !propsLoadId(mapName, propsId);
  const saveButton = (
    <button type="button" onClick={handleSave} disabled={saveDisabled} className="inline-flex items-center gap-1 px-2 py-1 rounded bg-emerald-600 hover:bg-emerald-500 text-slate-950 font-semibold disabled:opacity-50" data-props-save-header><Save size={12} />Сохранить{isDirty ? ' *' : ''}</button>
  );
  const issueMarks = useMemo(() => {
    if (!check || !propsFile) return [] as { id: string; pos: Vec2; fresh: boolean; reason: string }[];
    const fresh = new Set(addedIssues.map((i) => `${i.objectId}|${i.reason}`));
    const out: { id: string; pos: Vec2; fresh: boolean; reason: string }[] = [];
    for (const i of check.issues) {
      const f = findProp(propsFile, i.objectId);
      if (f) out.push({ id: i.objectId, pos: f.obj.pos, fresh: fresh.has(`${i.objectId}|${i.reason}`), reason: i.reason });
    }
    return out;
  }, [check, propsFile, addedIssues]);

  const checkCard = (
    <div className="bg-slate-950 border border-slate-800 rounded-xl p-3 flex flex-col gap-1.5 mb-2 text-xs" data-props-check>
      <div className="flex items-center justify-between">
        <span className="font-bold text-slate-200">Проверка (валидатор игры + проходимость)</span>
        <span
          data-props-gate
          className={`px-2 py-0.5 rounded text-[10px] font-bold ${checkBusy || !check ? 'bg-slate-800 text-slate-400' : check.gate.gate === 'pass' ? 'bg-emerald-950 border border-emerald-800 text-emerald-300' : 'bg-rose-950 border border-rose-800 text-rose-300'}`}
        >
          {checkBusy || !check ? 'ПРОВЕРКА…' : `GATE: ${check.gate.gate.toUpperCase()}`}
        </span>
      </div>
      {check && !checkBusy && (
        <>
          {check.gate.gate === 'fail' && (
            <p className="text-[11px] text-rose-300">Маршрут / проём / лестница заблокированы: {check.gate.gate_leg ?? 'route'}{check.gate.gate_blockers?.length ? ` (${check.gate.gate_blockers.join(', ')})` : ''}</p>
          )}
          <p className="text-[11px] text-slate-400" data-props-issue-count>
            Игра уберёт при загрузке: <strong className={check.issues.length ? 'text-amber-300' : 'text-emerald-300'}>{check.issues.length}</strong>
            {baselineRef.current ? <> (было при открытии {baselineRef.current.length}, <strong className={addedIssues.length ? 'text-rose-300' : 'text-slate-200'}>новых после правок {addedIssues.length}</strong>)</> : null}
          </p>
          {check.issues.length > 0 && (
            <details className="text-[11px] text-slate-400" open={addedIssues.length > 0}>
              <summary className="cursor-pointer text-slate-500 hover:text-slate-300">Список ({check.issues.length})</summary>
              <ul className="mt-1 flex flex-col gap-0.5 font-mono text-[10px] max-h-40 overflow-y-auto">
                {check.issues.map((i, k) => {
                  const fresh = addedIssues.some((a) => a.objectId === i.objectId && a.reason === i.reason);
                  return (
                    <li key={`${i.objectId}${k}`}>
                      <button type="button" onClick={() => setSelectedId(i.objectId)} className={`text-left hover:underline ${fresh ? 'text-rose-300' : 'text-amber-300'}`}>{i.objectId}</button>
                      <span className="text-slate-500">: {i.reason}</span>
                    </li>
                  );
                })}
              </ul>
            </details>
          )}
        </>
      )}
    </div>
  );

  // ---------------- LEFT COLUMN: Layers & Controls ----------------
  const leftCol = (
      <section className="bg-slate-900 border border-slate-800 rounded-2xl p-4 flex flex-col gap-4">
        <div>
          <h3 className="text-xs font-bold tracking-widest uppercase text-slate-400 mb-2">Пропсы текущей карты</h3>
          <div className="w-full bg-slate-950 border border-slate-700 rounded-lg px-2.5 py-1.5 text-xs text-cyan-200 font-mono" data-testid="props-map-name">
            {mapName ?? 'карта не выбрана'}
          </div>
          {propsId || propsFile ? (
            filePath && <p className="text-[10px] text-slate-500 font-mono mt-1 break-all">{filePath}</p>
          ) : mapName ? (
            <p className="text-[11px] text-amber-300 mt-1" data-testid="props-missing-hint">{missingPropsHint(mapName)}</p>
          ) : (
            <p className="text-[11px] text-amber-300 mt-1">Выберите карту в выпадающем списке сверху.</p>
          )}
          <p className="text-[10px] text-slate-500 mt-1">Карту переключают сверху, одну на весь отдел «Генератор уровней».</p>
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
            {failingLayer && (
              <span className="px-2 py-0.5 rounded text-[11px] font-semibold bg-rose-950/90 border border-rose-700 text-rose-300 flex items-center gap-1.5">
                <ShieldAlert size={12} />
                route blocked at {failingLayer.report?.gate_leg ?? 'route'}
                {failingLayer.report?.gate_blockers && failingLayer.report.gate_blockers.length > 0 && (
                  <span className="font-mono text-[10px] text-rose-200">
                    ({failingLayer.report.gate_blockers.join(', ')})
                  </span>
                )}
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
            disabled={saveDisabled}
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
              onClick={() => loadMapProps(propsLoadId(mapName, propsId))}
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

        {rerollNote && (
          <div
            className={`p-2.5 rounded-lg border text-[11px] leading-snug ${rerollNote.ok ? 'bg-slate-950 border-cyan-900 text-cyan-200' : 'bg-amber-950/60 border-amber-800 text-amber-200'}`}
            data-testid="reroll-note"
          >
            {rerollNote.text}
          </div>
        )}

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
              const support = rerollSupport(l, propsFile.frozen);

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
                      disabled={!support.ok || !isVisible || !!busy}
                      title={support.ok ? 'Перебросить слой (с очисткой трассы, задача 088)' : support.reason}
                      className="inline-flex items-center gap-1 px-2.5 py-1 rounded bg-slate-800 hover:bg-slate-700 border border-slate-700 text-[11px] text-cyan-300 font-medium disabled:opacity-50 transition-colors"
                    >
                      <RefreshCw size={11} />
                      Перебросить
                    </button>
                  </div>

                  {!support.ok && (
                    <p className="text-[10px] text-slate-500 leading-snug" data-testid="layer-reroll-reason">{support.reason}</p>
                  )}

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

                  {/* Gate failure badge */}
                  {l.report?.gate === 'fail' && (
                    <div className="flex items-center gap-1.5 px-2 py-1.5 rounded bg-rose-950/90 border border-rose-800 text-rose-300 text-xs font-semibold">
                      <ShieldAlert size={13} className="shrink-0 text-rose-400" />
                      <div className="flex flex-col min-w-0">
                        <span>route blocked at {l.report.gate_leg ?? 'route'}</span>
                        {l.report.gate_blockers && l.report.gate_blockers.length > 0 && (
                          <span className="text-[10px] font-mono text-rose-400 truncate">
                            blockers: {l.report.gate_blockers.join(', ')}
                          </span>
                        )}
                      </div>
                    </div>
                  )}
                </div>
              );
            })}
          </div>
        </div>
      </section>
  );

  // ---------------- RIGHT COLUMN: Reports, Gate & Actions ----------------
  const rightCol = (
      <section className="bg-slate-900 border border-slate-800 rounded-2xl p-4 flex flex-col gap-4">
        <div>
          <h3 className="text-xs font-bold tracking-widest uppercase text-slate-400 mb-2">Отчёт размещения</h3>

          {checkCard}

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
  );

  // ---------------- MIDDLE COLUMN: Top-Down Canvas & Inspector (T-023: zoom / pan / F11, zone tools, drag-move) ----------------
  const rerollable = propsFile ? propsFile.layers.filter((l) => l.enabled !== false && !l.locked && rerollSupport(l, propsFile.frozen).ok && l.kind !== 'power') : [];
  const refusal = selectedProp && propsFile ? moveRefusal(propsFile, selectedProp.id) : null;
  const middle = (
    <>
      {/* Canvas Toolbar */}
      <div className="flex flex-wrap items-center gap-2 pb-2 border-b border-slate-800 text-[11px]">
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
        <button type="button" onClick={() => zoomBy(1 / PLAN_VIEW.button_step)} disabled={!layout} title="Отдалить" className="p-1 rounded bg-slate-800 border border-slate-700 text-slate-200 hover:bg-slate-700 disabled:opacity-40" data-props-zoom-out><ZoomOut size={13} /></button>
        <span className="font-mono text-slate-300 w-12 text-center" data-props-zoom>{Math.round(view.zoom * 100)} %</span>
        <button type="button" onClick={() => zoomBy(PLAN_VIEW.button_step)} disabled={!layout} title="Приблизить" className="p-1 rounded bg-slate-800 border border-slate-700 text-slate-200 hover:bg-slate-700 disabled:opacity-40" data-props-zoom-in><ZoomIn size={13} /></button>
        <button type="button" onClick={() => setView(FIT_VIEW)} disabled={!layout} title="Вписать карту целиком" className={btn} data-props-fit><Maximize2 size={12} />Вписать</button>
        {!full && <button type="button" onClick={toggleFull} disabled={!layout} title="План на всё окно браузера (F11); обратно — F11, Esc или «Свернуть»" className="inline-flex items-center gap-1 px-2 py-1 rounded bg-slate-800 border border-cyan-700 text-cyan-200 hover:bg-slate-700 disabled:opacity-40" data-props-full-enter><Expand size={12} />На весь экран</button>}
        <label className="flex items-center gap-1.5 text-slate-300 cursor-pointer">
          <input type="checkbox" checked={showExclusions} onChange={(e) => setShowExclusions(e.target.checked)} className="rounded bg-slate-950 border-slate-700 text-cyan-600 focus:ring-0" />
          <span>Зоны исключений</span>
        </label>
        <span className="text-slate-500">колесо — масштаб у курсора; сдвиг — средняя кнопка, пробел + перетаскивание или левой по пустому месту</span>
      </div>

      {/* The tool row: move props / re-roll zone shape */}
      <div className="flex flex-wrap items-center gap-2 text-[11px]" data-props-tools>
        <div className="inline-flex rounded-lg bg-slate-950 border border-slate-700 p-0.5">
          {([['move', 'Двигать'], ['rect', 'Рамка'], ['poly', 'Многоугольник'], ['lasso', 'Лассо']] as const).map(([m, label]) => (
            <button key={m} type="button" onClick={() => setTool(m)} data-props-tool={m} className={`px-2 py-0.5 rounded ${tool === m ? (m === 'move' ? 'bg-cyan-700 text-white font-semibold' : 'bg-amber-600 text-slate-950 font-semibold') : 'text-slate-300 hover:text-white'}`}>{label}</button>
          ))}
        </div>
        {!full && undoRedo}
        {tool === 'move' ? (
          <span className="inline-flex flex-wrap items-center gap-1.5 px-2 py-0.5 rounded bg-slate-950 border border-cyan-900 text-slate-300" data-props-move-controls>
            <label className="inline-flex items-center gap-1" title="Сетка: при перетаскивании пропс встаёт на узел сетки"><input type="checkbox" checked={snapOn} onChange={(e) => setSnapOn(e.target.checked)} className="accent-cyan-500" data-props-snap-toggle /><Grid3x3 size={12} />сетка</label>
            <select value={snapStep} disabled={!snapOn} onChange={(e) => setSnapStep(Number(e.target.value))} className="bg-slate-900 border border-slate-700 rounded px-1 py-0.5 text-slate-100 disabled:opacity-40" data-props-snap-step>
              {PROP_SNAP_CM.map((c) => <option key={c} value={c}>{c} см</option>)}
            </select>
            <span className="text-slate-500">перетащите пропс; стрелки: {NUDGE_CM.fine} см · Shift {NUDGE_CM.coarse / 100} м · Ctrl {NUDGE_CM.huge / 100} м; сдвинутый пропс получает замок</span>
          </span>
        ) : (
          <>
            <button type="button" onClick={() => setZone(null)} disabled={!zone} className={btn} data-props-zone-clear><Eraser size={12} />Сбросить зону</button>
            <label className="flex items-center gap-1 text-slate-400">seed
              <input type="number" value={zoneSeed} min={0} onChange={(e) => setZoneSeed(Math.max(0, Math.floor(Number(e.target.value) || 0)))} className="w-20 bg-slate-950 border border-slate-700 rounded px-1.5 py-0.5 text-slate-100" data-props-zone-seed />
            </label>
            <span className="inline-flex flex-wrap items-center gap-1.5 text-slate-400" data-props-zone-layers>
              слои:
              {rerollable.map((l) => (
                <label key={l.id} className="inline-flex items-center gap-1"><input type="checkbox" checked={!zoneOff[l.id]} onChange={(e) => setZoneOff((z) => ({ ...z, [l.id]: !e.target.checked }))} className="accent-amber-500" />{l.id}</label>
              ))}
            </span>
            <button type="button" onClick={handleRerollZone} disabled={!zone || zoneLayerIds.length === 0 || !!busy || !propsFile || propsFile.frozen} className="inline-flex items-center gap-1 px-2.5 py-1 rounded bg-amber-600 hover:bg-amber-500 text-slate-950 font-semibold disabled:opacity-40" data-props-zone-reroll><RefreshCw size={12} />Перебросить в зоне</button>
            <span className="text-slate-500">{tool === 'rect' ? 'тяните рамку' : tool === 'lasso' ? 'обведите зону, не отпуская кнопку' : 'щёлкайте углы, двойной щелчок / Enter — замкнуть, Esc — отмена'}; вне зоны, закреплённое и слой pmc_charges не трогаются</span>
          </>
        )}
      </div>
      {/* Canvas: the drawing under the zone / pick / drag overlay (the same PlanView) */}
      <div ref={planBox}>
        <RegionOverlay
          bounds={layout?.bounds ?? null}
          height={planHeight}
          mode={tool === 'move' ? 'off' : tool}
          zone={zone}
          locks={NO_LOCKS}
          inside={preview?.inside}
          locked={preview?.locked}
          onZone={setZone}
          view={view}
          onView={setView}
          layerTool={tool === 'move' ? 'select' : 'off'}
          pickLayer={pickLayer}
          onLayerAction={onLayerAction}
          snapStep={snapOn ? snapStep : 0}
          overlay={(h) => (
            <g style={{ pointerEvents: 'none' }} data-props-issue-marks>
              {issueMarks.map((m, i) => {
                const q = h.scr(m.pos);
                return <circle key={`${m.id}${i}`} cx={q[0]} cy={q[1]} r={13} fill="none" stroke={m.fresh ? '#ef4444' : '#f59e0b'} strokeWidth={2.5} strokeDasharray={m.fresh ? undefined : '3 3'} />;
              })}
            </g>
          )}
        >
          <PropsCanvas
            ref={canvasRef}
            layout={layout}
            propsFile={propsFile}
            layerFilter={layerFilter}
            visibleLayers={visibleLayers}
            selectedPropId={selectedId}
            showExclusions={showExclusions}
            height={planHeight}
            view={view}
          />
        </RegionOverlay>
      </div>

      {zone && tool !== 'move' && preview && (
        <p className="text-[11px] text-slate-300" data-props-zone-summary>
          В зоне: заменится <span className="text-cyan-300 font-mono">{preview.inside.length}</span> пропсов (голубые точки), останется закреплённых <span className="text-emerald-300 font-mono">{preview.locked.length}</span> (зелёные квадраты); слои: {zoneLayerIds.join(', ') || 'не выбраны'}.
        </p>
      )}

      {moveNote && (
        <p className={`text-[11px] ${moveNote.ok ? 'text-cyan-200' : 'text-amber-300'}`} data-props-move-note>{moveNote.text}</p>
      )}

      {/* Selected Prop Card */}
      {selectedProp ? (
        <div className="bg-slate-950 border border-cyan-800/80 rounded-xl p-3 flex flex-wrap items-center justify-between gap-3 text-xs" data-props-selected>
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
            {refusal && <span className="text-amber-300">{refusal}</span>}
          </div>

          <div className="flex items-center gap-2">
            <button
              onClick={toggleSelectedPropLock}
              disabled={!!propsFile?.frozen}
              data-props-lock
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
          Кликните на пропс (ящик, бочку, турель, мину, лут, генератор), чтобы увидеть координаты и замок; перетащите его или сдвиньте стрелками — он останется там, где вы его поставили.
        </p>
      )}
    </>
  );

  const drawerContent = (
    <div className="flex flex-col gap-3">
      {leftCol}
      {rightCol}
    </div>
  );

  return (
    <div className={`p-6 grid gap-6 ${full ? '' : 'xl:grid-cols-[320px_minmax(0,1fr)_340px]'}`}>
      {!full && leftCol}

      <section className="bg-slate-900 border border-slate-800 rounded-2xl p-4 flex flex-col gap-3 min-w-0">
        <FullWindowFrame
          full={full}
          onExit={() => setFull(false)}
          drawer={drawerContent}
          drawerTitle="Слои и отчёт"
          header={
            <>
              <span className="font-mono text-cyan-200" data-props-full-title>{mapName ?? ''}</span>
              {undoRedo}
              {saveButton}
              {isDirty && <span className="text-amber-300" title="Правки не записаны в файл пропсов">● не сохранено</span>}
              {saveSuccess && <span className="text-emerald-300">{saveSuccess}</span>}
              {error && <span className="text-rose-300">{error}</span>}
              {busy && <span className="text-cyan-300 animate-pulse">{busy}</span>}
              <span className="text-slate-500">F11 / Esc — свернуть</span>
            </>
          }
        >
          <div className="flex flex-col gap-3">{middle}</div>
        </FullWindowFrame>
      </section>

      {!full && rightCol}
    </div>
  );
};
