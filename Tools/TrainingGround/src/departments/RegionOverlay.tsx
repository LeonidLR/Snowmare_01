// Training Ground «Генератор уровней» / level generator v2 stage 1 (task 070): the region-select overlay drawn over LayoutView.
// The designer draws the re-roll zone as a RECTANGLE (drag), a POLYGON (click the corners, double-click / Enter to close, Esc to
// cancel) or a LASSO (drag a free outline); the locked districts are drawn green, the zone amber, the centres of the objects the
// re-roll would replace cyan and the locked ones green. Same size and transform as the canvas under it (levelgen/planView.ts).
// UX round (task 072): ZOOM (mouse wheel, around the cursor) and PAN (middle button or Space + drag in every tool; plain left drag
// in «Просмотр»), all in one PlanView shared with the canvas; the START (green S) and the GOALS (gold) are drawn and edited here -
// drag to move (in «Просмотр» and «+ цель»), «+ цель» click = add, right click or the small × = delete; red = an invalid spot
// (the reason next to it and in the tooltip). The squad playback dots + the leader's trail are drawn here too.
import React, { useEffect, useRef, useState } from 'react';
import type { Vec2 } from '../movement/geometry';
import type { PlanView } from '../levelgen/planView';
import { panBy, planTransform, toScreen, toWorld, viewTransform, wheelFactor, zoomAt } from '../levelgen/planView';
import { isUsableZone, shapePolygon, simplifyLasso } from '../levelgen/regionRegen';
import type { LockDistrict, ObjectiveSet } from '../levelgen/schema';

export type RegionMode = 'off' | 'rect' | 'poly' | 'lasso';

/** One edit of the start / goals on the plan (the page applies it with objectives.ts). */
export type ObjectiveAction = { kind: 'move'; id: string; pos: Vec2 } | { kind: 'add'; pos: Vec2 } | { kind: 'remove'; id: string };

interface Props {
  bounds: { min: Vec2; max: Vec2 } | null;
  height: number;
  mode: RegionMode;
  zone: Vec2[] | null;
  locks: LockDistrict[];
  /** Centres of the groups the re-roll would replace / that a lock keeps. */
  inside?: Vec2[];
  locked?: Vec2[];
  /** The blocks the re-roll gives another type (stage 2), [min, max] UE cm: drawn cyan dashed. */
  blocks?: [Vec2, Vec2][];
  onZone: (zone: Vec2[] | null) => void;
  /** Zoom + pan (shared with LayoutView). */
  view: PlanView;
  onView: (v: PlanView) => void;
  /** The start + goals to draw and edit (null = none). */
  objectives?: ObjectiveSet | null;
  /** Errors by id ('START' / 'GOAL_n'): drawn red with the reason. */
  objErrors?: Record<string, string>;
  /** Warnings by id: an orange ring. */
  objWarnings?: Record<string, string>;
  /** The shortest visiting order's nav path: a gold dashed line. */
  bestPath?: Vec2[];
  /** «+ цель»: a click on the plan adds a goal. */
  addGoal?: boolean;
  /** Start / goal edits (absent = read-only markers). */
  onObjective?: (a: ObjectiveAction) => void;
  /** The squad playback: member positions (the leader first) and the leader's recent trail. */
  squad?: Vec2[] | null;
  trail?: Vec2[] | null;
  /** Every member's recent trail (the leader first): the followers' are drawn faint. */
  trails?: Vec2[][] | null;
  children: React.ReactNode;
}

/** The lasso keeps a point every this many cm of stroke. */
const LASSO_STEP_CM = 150;
const BORDER_PX = 1;
/** A press that moves less than this is a click (adds a goal), px. */
const CLICK_PX = 4;
const START_ID = 'START';

export const RegionOverlay: React.FC<Props> = ({ bounds, height, mode, zone, locks, inside = [], locked = [], blocks = [], onZone, view, onView, objectives, objErrors = {}, objWarnings = {}, bestPath, addGoal = false, onObjective, squad, trail, trails, children }) => {
  const box = useRef<HTMLDivElement>(null);
  const [size, setSize] = useState<[number, number]>([0, height]);
  const [draft, setDraft] = useState<Vec2[]>([]);
  const [hover, setHover] = useState<Vec2 | null>(null);
  const dragging = useRef(false);
  const pan = useRef<{ x: number; y: number; view: PlanView; moved: boolean; click: boolean } | null>(null);
  const [panning, setPanning] = useState(false);
  const [drag, setDrag] = useState<{ id: string; pos: Vec2 } | null>(null);
  const space = useRef(false);
  const [spaceDown, setSpaceDown] = useState(false);
  const viewRef = useRef(view);
  viewRef.current = view;

  useEffect(() => {
    const el = box.current;
    if (!el) return;
    const ro = new ResizeObserver(() => setSize([el.clientWidth, el.clientHeight]));
    ro.observe(el);
    setSize([el.clientWidth, el.clientHeight]);
    return () => ro.disconnect();
  }, []);
  useEffect(() => { setDraft([]); setHover(null); dragging.current = false; }, [mode]);
  useEffect(() => {
    const typing = (e: KeyboardEvent) => e.target instanceof HTMLElement && (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA');
    const key = (e: KeyboardEvent) => {
      if (e.code === 'Space' && !typing(e)) { if (!space.current) { space.current = true; setSpaceDown(true); } e.preventDefault(); return; }
      if (mode !== 'poly') return;
      if (e.key === 'Escape') setDraft([]);
      if (e.key === 'Enter') setDraft((d) => { if (isUsableZone(d)) onZone(d); return []; });
    };
    const keyUp = (e: KeyboardEvent) => { if (e.code === 'Space') { space.current = false; setSpaceDown(false); } };
    window.addEventListener('keydown', key);
    window.addEventListener('keyup', keyUp);
    return () => { window.removeEventListener('keydown', key); window.removeEventListener('keyup', keyUp); };
  }, [mode, onZone]);
  // the wheel zooms around the cursor (a native non-passive listener: React's onWheel cannot preventDefault the page scroll)
  useEffect(() => {
    const el = box.current;
    if (!el) return;
    const wheel = (e: WheelEvent) => {
      if (!bounds) return;
      e.preventDefault();
      const r = el.getBoundingClientRect();
      onView(zoomAt(viewRef.current, [e.clientX - r.left - BORDER_PX, e.clientY - r.top - BORDER_PX], wheelFactor(e.deltaY)));
    };
    el.addEventListener('wheel', wheel, { passive: false });
    return () => el.removeEventListener('wheel', wheel);
  }, [bounds, onView]);

  // LayoutView's canvas has a 1 px border: its drawing area is the box minus 2 px, offset by 1 px; the view goes on top
  const t = bounds && size[0] > 2 ? viewTransform(planTransform(bounds, size[0] - 2 * BORDER_PX, size[1] - 2 * BORDER_PX), view) : null;
  const scr = (p: Vec2): [number, number] => { const q = toScreen(t!, p); return [q[0] + BORDER_PX, q[1] + BORDER_PX]; };
  const world = (e: React.PointerEvent | React.MouseEvent): Vec2 | null => {
    if (!t || !box.current) return null;
    const r = box.current.getBoundingClientRect();
    return toWorld(t, [e.clientX - r.left - BORDER_PX, e.clientY - r.top - BORDER_PX]);
  };
  const pts = (poly: Vec2[]) => (t ? poly.map((p) => scr(p).map((v) => v.toFixed(1)).join(',')).join(' ') : '');
  const editable = !!onObjective && (mode === 'off' || addGoal);

  const down = (e: React.PointerEvent) => {
    const p = world(e);
    if (!p) return;
    // pan: the middle button or Space + left in every tool; a plain left drag in «Просмотр» / «+ цель» (a short press = a click)
    const panTool = e.button === 1 || (e.button === 0 && (space.current || mode === 'off'));
    if (panTool) {
      e.preventDefault();
      (e.target as Element).setPointerCapture?.(e.pointerId);
      pan.current = { x: e.clientX, y: e.clientY, view, moved: false, click: e.button === 0 && !space.current };
      return;
    }
    if (e.button !== 0) return;
    if (mode === 'rect' || mode === 'lasso') {
      dragging.current = true;
      (e.target as Element).setPointerCapture?.(e.pointerId);
      setDraft(mode === 'rect' ? [p, p] : [p]);
    } else if (mode === 'poly') setDraft((d) => [...d, p]);
  };
  const move = (e: React.PointerEvent) => {
    const pn = pan.current;
    if (pn) {
      const dx = e.clientX - pn.x, dy = e.clientY - pn.y;
      if (!pn.moved && Math.hypot(dx, dy) >= CLICK_PX) { pn.moved = true; setPanning(true); }
      if (pn.moved) onView(panBy(pn.view, dx, dy));
      return;
    }
    const p = world(e);
    if (!p) return;
    if (drag) { setDrag({ id: drag.id, pos: p }); return; }
    setHover(p);
    if (!dragging.current) return;
    if (mode === 'rect') setDraft((d) => [d[0], p]);
    else if (mode === 'lasso') setDraft((d) => [...d, p]);
  };
  const up = (e: React.PointerEvent) => {
    const pn = pan.current;
    if (pn) {
      pan.current = null; setPanning(false);
      if (!pn.moved && pn.click && addGoal && onObjective) { const p = world(e); if (p) onObjective({ kind: 'add', pos: p }); }
      return;
    }
    if (drag) { onObjective?.({ kind: 'move', id: drag.id, pos: drag.pos }); setDrag(null); return; }
    if (!dragging.current) return;
    dragging.current = false;
    const poly = mode === 'rect' && draft.length === 2 ? shapePolygon({ kind: 'rect', a: draft[0], b: draft[1] }) : simplifyLasso(draft, LASSO_STEP_CM);
    setDraft([]);
    if (isUsableZone(poly)) onZone(poly);
  };
  const dbl = () => {
    if (mode !== 'poly') return;
    // the double-click's two clicks added the same corner twice: drop the duplicate
    const d = draft.filter((p, i) => i === 0 || Math.hypot(p[0] - draft[i - 1][0], p[1] - draft[i - 1][1]) > 1);
    setDraft([]);
    if (isUsableZone(d)) onZone(d);
  };
  const draftPoly = mode === 'rect' && draft.length === 2 ? shapePolygon({ kind: 'rect', a: draft[0], b: draft[1] }) : mode === 'poly' && hover && draft.length ? [...draft, hover] : draft;

  /** A marker press starts its drag (never a pan / a zone). */
  const markerDown = (id: string) => (e: React.PointerEvent) => {
    if (!editable || e.button !== 0 || space.current) return;
    e.stopPropagation();
    (e.currentTarget as Element).setPointerCapture?.(e.pointerId);
    const p = world(e);
    if (p) setDrag({ id, pos: p });
  };
  const markerMenu = (id: string) => (e: React.MouseEvent) => {
    if (!editable || id === START_ID) return;
    e.preventDefault(); e.stopPropagation();
    onObjective?.({ kind: 'remove', id });
  };
  const posOf = (id: string, p: Vec2): Vec2 => (drag && drag.id === id ? drag.pos : p);
  const cursor = panning || drag ? 'grabbing' : spaceDown ? 'grab' : addGoal ? 'copy' : mode === 'off' ? 'grab' : 'crosshair';

  return (
    <div ref={box} className="relative overflow-hidden rounded-xl" style={{ height }}>
      {children}
      <svg
        className="absolute inset-0 w-full h-full rounded-xl"
        style={{ cursor, touchAction: 'none' }}
        onPointerDown={down} onPointerMove={move} onPointerUp={up} onPointerLeave={() => setHover(null)} onDoubleClick={dbl}
        onAuxClick={(e) => e.preventDefault()}
        onContextMenu={(e) => { e.preventDefault(); if (mode === 'poly') setDraft([]); }}
      >
        {t && locks.map((k) => {
          const c = scr(k.poly[0]);
          return (
            <g key={k.id}>
              <polygon points={pts(k.poly)} fill="rgba(16, 185, 129, 0.16)" stroke="#10b981" strokeWidth={2} />
              <text x={c[0] + 4} y={c[1] + 13} fill="#6ee7b7" fontSize={11} fontFamily="ui-monospace, monospace">▣ {k.name}</text>
            </g>
          );
        })}
        {t && blocks.map((b, i) => <polygon key={`b${i}`} points={pts([[b[0][0], b[0][1]], [b[1][0], b[0][1]], [b[1][0], b[1][1]], [b[0][0], b[1][1]]])} fill="rgba(34, 211, 238, 0.07)" stroke="#22d3ee" strokeWidth={1.5} strokeDasharray="3 3" />)}
        {t && zone && <polygon points={pts(zone)} fill="rgba(250, 204, 21, 0.12)" stroke="#facc15" strokeWidth={2} strokeDasharray="6 4" />}
        {t && inside.map((p, i) => { const q = scr(p); return <circle key={`i${i}`} cx={q[0]} cy={q[1]} r={3} fill="#22d3ee" />; })}
        {t && locked.map((p, i) => { const q = scr(p); return <rect key={`l${i}`} x={q[0] - 3} y={q[1] - 3} width={6} height={6} fill="#10b981" />; })}
        {t && draftPoly.length > 1 && <polyline points={pts(mode === 'poly' ? draftPoly : [...draftPoly, draftPoly[0]])} fill="rgba(250, 204, 21, 0.08)" stroke="#fde047" strokeWidth={1.5} />}
        {t && mode === 'poly' && draft.map((p, i) => { const q = scr(p); return <circle key={`d${i}`} cx={q[0]} cy={q[1]} r={3.5} fill="#fde047" />; })}
        {/* the shortest visiting order (hidden while a point is dragged: it belongs to the old position) */}
        {t && bestPath && bestPath.length > 1 && !drag && <polyline points={pts(bestPath)} fill="none" stroke="#facc15" strokeOpacity={0.75} strokeWidth={2} strokeDasharray="7 5" style={{ pointerEvents: 'none' }} />}
        {/* the squad playback */}
        {t && trails && trails.slice(1).map((tr, i) => (tr.length > 1 ? <polyline key={`st${i}`} points={pts(tr)} fill="none" stroke="#fbcfe8" strokeWidth={1.5} strokeOpacity={0.55} style={{ pointerEvents: 'none' }} /> : null))}
        {t && trail && trail.length > 1 && <polyline points={pts(trail)} fill="none" stroke="#f472b6" strokeWidth={2.5} strokeOpacity={0.7} style={{ pointerEvents: 'none' }} />}
        {t && squad && squad.map((p, i) => { const q = scr(p); return <circle key={`sq${i}`} cx={q[0]} cy={q[1]} r={i === 0 ? 6 : 4.5} fill={i === 0 ? '#f472b6' : '#fbcfe8'} stroke="#111827" strokeWidth={1.5} style={{ pointerEvents: 'none' }} />; })}
        {/* the start + goals */}
        {t && objectives && (() => {
          const sp = scr(posOf(START_ID, objectives.start.pos));
          const sErr = objErrors[START_ID];
          return (
            <g>
              <g onPointerDown={markerDown(START_ID)} style={{ cursor: editable ? 'move' : 'default' }} data-marker="START">
                <title>{`Старт${sErr ? ` — ОШИБКА: ${sErr}` : ''}${editable ? ' (перетащите)' : ''}`}</title>
                <circle cx={sp[0]} cy={sp[1]} r={13} fill="rgba(0,0,0,0.001)" />
                <circle cx={sp[0]} cy={sp[1]} r={11} fill="none" stroke={sErr ? '#ef4444' : '#22c55e'} strokeWidth={3} />
                <text x={sp[0]} y={sp[1] + 4} textAnchor="middle" fill={sErr ? '#ef4444' : '#22c55e'} fontSize={11} fontWeight="bold" fontFamily="ui-monospace, monospace" style={{ pointerEvents: 'none' }}>S</text>
              </g>
              {objectives.goals.map((gl, i) => {
                const q = scr(posOf(gl.id, gl.pos));
                // the number the page's text uses (GOAL_3 = №3, also after №1 was deleted)
                const num = /^GOAL_\d+$/.test(gl.id) ? gl.id.slice(5) : String(i + 1);
                const err = objErrors[gl.id], warn = objWarnings[gl.id];
                return (
                  <g key={gl.id} data-marker={gl.id}>
                    <g onPointerDown={markerDown(gl.id)} onContextMenu={markerMenu(gl.id)} style={{ cursor: editable ? 'move' : 'default' }}>
                      <title>{`Цель №${num}${err ? ` — ОШИБКА: ${err}` : ''}${warn ? ` — внимание: ${warn}` : ''}${editable ? ' (перетащите; правая кнопка — удалить)' : ''}`}</title>
                      <circle cx={q[0]} cy={q[1]} r={10} fill={err ? '#ef4444' : '#facc15'} stroke={warn && !err ? '#f97316' : '#000'} strokeWidth={warn && !err ? 3 : 1.5} />
                      <text x={q[0]} y={q[1] + 4} textAnchor="middle" fill="#111" fontSize={11} fontWeight="bold" fontFamily="ui-monospace, monospace" style={{ pointerEvents: 'none' }}>{num}</text>
                    </g>
                    {err && !drag && <text x={q[0] + 14} y={q[1] + 15} fill="#fca5a5" fontSize={10} fontFamily="ui-monospace, monospace" style={{ pointerEvents: 'none', paintOrder: 'stroke', stroke: '#111827', strokeWidth: 3 }}>{err}</text>}
                    {editable && !drag && (
                      <g onPointerDown={(e) => { e.stopPropagation(); }} onClick={(e) => { e.stopPropagation(); onObjective?.({ kind: 'remove', id: gl.id }); }} style={{ cursor: 'pointer' }} data-remove={gl.id}>
                        <title>{`Удалить цель №${num}`}</title>
                        <circle cx={q[0] + 10} cy={q[1] - 10} r={5.5} fill="#1f2937" stroke="#f87171" strokeWidth={1} />
                        <text x={q[0] + 10} y={q[1] - 7} textAnchor="middle" fill="#fca5a5" fontSize={9} fontWeight="bold" style={{ pointerEvents: 'none' }}>×</text>
                      </g>
                    )}
                  </g>
                );
              })}
              {sErr && !drag && <text x={sp[0] + 15} y={sp[1] + 15} fill="#fca5a5" fontSize={10} fontFamily="ui-monospace, monospace" style={{ pointerEvents: 'none', paintOrder: 'stroke', stroke: '#111827', strokeWidth: 3 }}>{sErr}</text>}
            </g>
          );
        })()}
      </svg>
    </div>
  );
};
