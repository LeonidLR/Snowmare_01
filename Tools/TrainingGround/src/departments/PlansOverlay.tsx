// «Планировки» layer of the level generator page (S2_ROOMS_PLAN.md step S2.7): the room plan of the planned buildings, per storey, drawn in the plan's own transform
// (RegionOverlay's `overlay` slot). Paints the pure data of levelgen/buildings/planOverlay.ts and nothing else: rooms filled by unit / zone, door markers by role, loops,
// dead-end flats, validator issues. Room ids and what a room is show on hover; the legend (same glyphs as docs/design/levelgen/S2_3_PLANS.md) is PlansLegend.
import React, { useState } from 'react';
import type { Vec2 } from '../movement/geometry';
import type { PlanOverlay, OverlayRoom } from '../levelgen/buildings/planOverlay';
import { DEAD_END_COLOUR, LOOP_COLOUR, PLAN_LEGEND } from '../levelgen/buildings/planOverlay';
import type { OverlayHelpers } from './RegionOverlay';

const KIND_RU: Record<string, string> = {
  flat: 'квартира', suite: 'тупиковый кабинет', office: 'комната', lobby: 'вестибюль', service: 'служебная', core: 'лестничное ядро', corridor: 'коридор / площадка',
};

/** The hover text of a room: id, what it is, unit / zone, size, its openings. */
export function roomTitle(r: OverlayRoom): string {
  const what = KIND_RU[r.fill] ?? r.fill;
  const unit = r.unit && r.fill === 'flat' ? `, ${r.unit}${r.dead_end ? ' (тупик)' : ''}` : r.unit === 'suite' ? '' : r.unit ? `, ${r.unit}` : '';
  return `${r.id}\n${what}${unit}${r.zone ? `, зона ${r.zone}` : ''}\n${(r.size_cm[0] / 100).toFixed(1)} x ${(r.size_cm[1] / 100).toFixed(1)} м, ${r.area_m2.toFixed(1)} м2${r.loop ? '\nна кольце дверей' : ''}${r.openings.length ? `\nпроёмы: ${r.openings.length}` : ''}`;
}

interface Props {
  h: OverlayHelpers;
  overlays: PlanOverlay[];
}

export const PlansOverlay: React.FC<Props> = ({ h, overlays }) => {
  const [hover, setHover] = useState<string | null>(null);
  const k = Math.max(0.35, Math.min(1.6, h.scale * 28)); // marker scale: bigger when zoomed in, never invisible
  const wallW = Math.max(2.5, h.scale * 28);
  return (
    <g data-plans-overlay>
      {overlays.map((o) => (
        <g key={`${o.building}:${o.floor}`} data-plan-building={o.building} data-plan-floor={o.floor} data-plan-kind={o.kind}>
          {/* rooms: filled by unit / zone; a dead-end flat has a red frame, a room on a loop a green dash */}
          {o.rooms.map((r) => (
            <g key={r.id} data-plan-room={r.id} data-plan-fill={r.fill} data-plan-loop={r.loop ? 1 : 0} data-plan-dead={r.dead_end ? 1 : 0}
              onMouseEnter={() => setHover(r.id)} onMouseLeave={() => setHover((x) => (x === r.id ? null : x))}>
              <title>{roomTitle(r)}</title>
              <polygon points={h.pts(r.poly)} fill={r.colour} fillOpacity={hover === r.id ? 0.78 : r.fill === 'core' ? 0.5 : 0.52} stroke={r.dead_end ? DEAD_END_COLOUR : r.colour} strokeWidth={r.dead_end ? 2.4 : 0.8}
                strokeOpacity={0.95} style={{ pointerEvents: 'all' }} />
              {r.loop && <polygon points={h.pts(r.poly)} fill="none" stroke={LOOP_COLOUR} strokeWidth={1.4} strokeDasharray="5 3" strokeOpacity={0.9} style={{ pointerEvents: 'none' }} />}
              {(() => { // the glyph grows with the room on screen (a flat letter must be readable when zoomed, absent when the room is a speck)
                const minPx = Math.min(r.size_cm[0], r.size_cm[1]) * h.scale; if (minPx < 12) return null;
                const fs = Math.max(10, Math.min(26, minPx * 0.42)), q = h.scr(r.centre); return (
                <text x={q[0]} y={q[1] + fs * 0.35} textAnchor="middle" fontSize={fs} fontFamily="ui-monospace, monospace" fontWeight={800} fill="#0b1120" stroke="#f8fafc" strokeWidth={fs * 0.12} paintOrder="stroke" style={{ pointerEvents: 'none' }}>{r.glyph}</text>); })()}
            </g>
          ))}
          {hover && o.rooms.some((r) => r.id === hover) && (() => {
            const r = o.rooms.find((x) => x.id === hover)!, q = h.scr(r.centre);
            return <text x={q[0]} y={q[1] - 9} textAnchor="middle" fontSize={10} fontFamily="ui-monospace, monospace" fill="#f8fafc" stroke="#0b1120" strokeWidth={3} paintOrder="stroke" style={{ pointerEvents: 'none' }} data-plan-hover={r.id}>{r.id}</text>;
          })()}
          {/* doors and windows by role; a loop door gets a green ring */}
          {o.doors.map((d) => {
            const a = h.scr(d.a), b = h.scr(d.b), c = h.scr(d.pos);
            const thin = d.kind === 'window';
            return (
              <g key={d.id} data-plan-door={d.id} data-plan-role={d.role} data-plan-state={d.state} data-plan-loop={d.loop ? 1 : 0} style={{ pointerEvents: 'none' }}>
                <title>{`${d.id}\n${d.role}${d.slot ? ` (слот: ${d.slot})` : ''}, ${d.state}${d.links ? `\n${d.links[0]} - ${d.links[1]}` : ''}`}</title>
                <line x1={a[0]} y1={a[1]} x2={b[0]} y2={b[1]} stroke="#0b1120" strokeWidth={(thin ? wallW * 0.6 : wallW) + 2} strokeLinecap="butt" />
                <line x1={a[0]} y1={a[1]} x2={b[0]} y2={b[1]} stroke={d.colour} strokeWidth={thin ? wallW * 0.6 : wallW} strokeLinecap="butt" strokeDasharray={d.role === 'portal' ? '3 2' : undefined} />
                {d.loop && <circle cx={c[0]} cy={c[1]} r={4.5 * k + 1.5} fill="none" stroke={LOOP_COLOUR} strokeWidth={1.5} />}
                {d.role === 'unit_entry' && <rect x={c[0] - 2 * k} y={c[1] - 2 * k} width={4 * k} height={4 * k} fill="#7f1d1d" stroke="#fecaca" strokeWidth={0.8} />}
              </g>
            );
          })}
          {/* validator issues of the building that resolve to a room / an opening on this storey */}
          {o.issues.filter((i) => i.pos).map((i, n) => { const q = h.scr(i.pos as Vec2); return (
            <g key={`${i.code}:${i.ref ?? n}`} data-plan-issue={i.code} style={{ pointerEvents: 'none' }}>
              <title>{`${i.code}${i.ref ? ` (${i.ref})` : ''}\n${i.message}`}</title>
              <path d={`M${q[0]},${q[1] - 7} L${q[0] + 6.5},${q[1] + 5} L${q[0] - 6.5},${q[1] + 5} Z`} fill={i.severity === 'error' ? '#dc2626' : '#f59e0b'} stroke="#0b1120" strokeWidth={1} />
              <text x={q[0]} y={q[1] + 3.5} textAnchor="middle" fontSize={8} fontWeight={700} fill="#fff">!</text>
            </g>); })}
          {/* the building's name and storey, above its first room */}
          {o.rooms.length > 0 && (() => { const r0 = o.rooms[0], q = h.scr(r0.poly[0]); return (
            <text x={q[0]} y={q[1] - 4} fontSize={9} fontFamily="ui-monospace, monospace" fill="#e2e8f0" stroke="#0b1120" strokeWidth={2.5} paintOrder="stroke" style={{ pointerEvents: 'none' }}>
              {o.building} · этаж {o.floor + 1}{o.kind === 'bsp' ? ` · BSP (${o.fallback ?? 'fallback'})` : ''}
            </text>); })()}
        </g>
      ))}
    </g>
  );
};

/** The legend under the plan: the glyphs and colours of the overlay, the same as S2_3_PLANS.md. */
export const PlansLegend: React.FC = () => (
  <div className="flex flex-wrap items-center gap-x-3 gap-y-1 text-[10px] text-slate-400" data-plans-legend>
    {PLAN_LEGEND.map((e, i) => (
      <span key={`${e.glyph}${i}`} className="inline-flex items-center gap-1" data-legend-glyph={e.glyph}>
        <span className="inline-flex items-center justify-center w-4 h-4 rounded-sm font-mono font-bold text-slate-950" style={{ background: e.colour, outline: e.glyph === 'A' ? `2px solid ${DEAD_END_COLOUR}` : undefined }}>{e.glyph}</span>
        {e.name}
      </span>
    ))}
  </div>
);
