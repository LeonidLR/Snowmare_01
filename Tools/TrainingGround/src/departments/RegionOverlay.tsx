// Training Ground «Генератор уровней» / level generator v2 stage 1 (task 070): the region-select overlay drawn over LayoutView.
// The designer draws the re-roll zone as a RECTANGLE (drag), a POLYGON (click the corners, double-click / Enter to close, Esc to
// cancel) or a LASSO (drag a free outline); the locked districts are drawn green, the zone amber, the centres of the objects the
// re-roll would replace cyan and the locked ones green. Same size and transform as the canvas under it (levelgen/planView.ts).
import React, { useEffect, useRef, useState } from 'react';
import type { Vec2 } from '../movement/geometry';
import { planTransform, toScreen, toWorld } from '../levelgen/planView';
import { isUsableZone, shapePolygon, simplifyLasso } from '../levelgen/regionRegen';
import type { LockDistrict } from '../levelgen/schema';

export type RegionMode = 'off' | 'rect' | 'poly' | 'lasso';

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
  children: React.ReactNode;
}

/** The lasso keeps a point every this many cm of stroke. */
const LASSO_STEP_CM = 150;
const BORDER_PX = 1;

export const RegionOverlay: React.FC<Props> = ({ bounds, height, mode, zone, locks, inside = [], locked = [], blocks = [], onZone, children }) => {
  const box = useRef<HTMLDivElement>(null);
  const [size, setSize] = useState<[number, number]>([0, height]);
  const [draft, setDraft] = useState<Vec2[]>([]);
  const [hover, setHover] = useState<Vec2 | null>(null);
  const dragging = useRef(false);

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
    if (mode !== 'poly') return;
    const key = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setDraft([]);
      if (e.key === 'Enter') setDraft((d) => { if (isUsableZone(d)) onZone(d); return []; });
    };
    window.addEventListener('keydown', key);
    return () => window.removeEventListener('keydown', key);
  }, [mode, onZone]);

  // LayoutView's canvas has a 1 px border: its drawing area is the box minus 2 px, offset by 1 px
  const t = bounds && size[0] > 2 ? planTransform(bounds, size[0] - 2 * BORDER_PX, size[1] - 2 * BORDER_PX) : null;
  const scr = (p: Vec2): [number, number] => { const q = toScreen(t!, p); return [q[0] + BORDER_PX, q[1] + BORDER_PX]; };
  const world = (e: React.PointerEvent | React.MouseEvent): Vec2 | null => {
    if (!t || !box.current) return null;
    const r = box.current.getBoundingClientRect();
    return toWorld(t, [e.clientX - r.left - BORDER_PX, e.clientY - r.top - BORDER_PX]);
  };
  const pts = (poly: Vec2[]) => (t ? poly.map((p) => scr(p).map((v) => v.toFixed(1)).join(',')).join(' ') : '');

  const down = (e: React.PointerEvent) => {
    const p = world(e);
    if (!p || e.button !== 0) return;
    if (mode === 'rect' || mode === 'lasso') {
      dragging.current = true;
      (e.target as Element).setPointerCapture?.(e.pointerId);
      setDraft(mode === 'rect' ? [p, p] : [p]);
    } else if (mode === 'poly') setDraft((d) => [...d, p]);
  };
  const move = (e: React.PointerEvent) => {
    const p = world(e);
    if (!p) return;
    setHover(p);
    if (!dragging.current) return;
    if (mode === 'rect') setDraft((d) => [d[0], p]);
    else if (mode === 'lasso') setDraft((d) => [...d, p]);
  };
  const up = () => {
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

  return (
    <div ref={box} className="relative" style={{ height }}>
      {children}
      <svg
        className="absolute inset-0 w-full h-full rounded-xl"
        style={{ pointerEvents: mode === 'off' ? 'none' : 'auto', cursor: mode === 'off' ? 'default' : 'crosshair' }}
        onPointerDown={down} onPointerMove={move} onPointerUp={up} onPointerLeave={() => setHover(null)} onDoubleClick={dbl}
        onContextMenu={(e) => { if (mode === 'poly') { e.preventDefault(); setDraft([]); } }}
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
      </svg>
    </div>
  );
};
