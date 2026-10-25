// Training Ground: top-down canvas of a formation layout (walls, zones, routes) and, optionally, one simulator frame
// (members with their vision cones, slots, heading). UE axes: X forward = up on screen, Y right = right on screen.
import React, { useEffect, useRef } from 'react';
import type { Layout } from '../movement/layout';
import type { Frame } from '../movement/sim';
import type { Vec2 } from '../movement/geometry';

const ROLE_LABEL: Record<string, string> = { K: 'К', P: 'П', L: 'Л', Z: 'З' };
const ROLE_COLOR: Record<string, string> = { K: '#eab308', P: '#2dd4bf', L: '#818cf8', Z: '#e879f9' };
const ZONE_COLOR: Record<string, string> = {
  open: 'rgba(56,189,248,0.07)', corridor: 'rgba(250,204,21,0.10)', wall_run: 'rgba(129,140,248,0.10)',
  gap_run: 'rgba(244,114,182,0.10)', corner: 'rgba(52,211,153,0.10)', wall_end: 'rgba(251,146,60,0.12)',
};

interface Props {
  layout: Layout | null;
  frame?: Frame | null;
  routeId?: string;
  fov?: number;
  coneLength?: number;
  height?: number;
  /** Highlighted points (audit issues). */
  marks?: Vec2[];
  /** Camera follow: view this point with `span` cm across instead of the whole layout. */
  focus?: Vec2 | null;
  span?: number;
}

export const SquadView: React.FC<Props> = ({ layout, frame, routeId, fov = 140, coneLength = 450, height = 520, marks, focus, span = 2400 }) => {
  const ref = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = ref.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth, h = canvas.clientHeight;
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
    const g = canvas.getContext('2d');
    if (!g) return;
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.fillStyle = '#020617';
    g.fillRect(0, 0, w, h);
    if (!layout) return;

    // World X (forward) runs up the screen, world Y (right) runs right.
    const [minX, minY] = focus ? [focus[0] - span / 2, focus[1] - (span / 2) * (w / h)] : layout.bounds.min;
    const [maxX, maxY] = focus ? [focus[0] + span / 2, focus[1] + (span / 2) * (w / h)] : layout.bounds.max;
    const pad = 16;
    const s = Math.min((w - 2 * pad) / (maxY - minY), (h - 2 * pad) / (maxX - minX));
    const ox = (w - (maxY - minY) * s) / 2, oy = (h - (maxX - minX) * s) / 2;
    const P = (p: Vec2): [number, number] => [ox + (p[1] - minY) * s, oy + (maxX - p[0]) * s];
    const screenAngle = (yaw: number) => (yaw - 90) * (Math.PI / 180); // yaw 0 (+X) points up

    // grid every metre (5 m stronger)
    g.lineWidth = 1;
    for (let x = Math.ceil(minX / 100) * 100; x <= maxX; x += 100) {
      g.strokeStyle = x % 500 === 0 ? '#1e293b' : '#0f172a';
      const a = P([x, minY]), b = P([x, maxY]);
      g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke();
    }
    for (let y = Math.ceil(minY / 100) * 100; y <= maxY; y += 100) {
      g.strokeStyle = y % 500 === 0 ? '#1e293b' : '#0f172a';
      const a = P([minX, y]), b = P([maxX, y]);
      g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke();
    }

    for (const z of layout.zones) {
      g.fillStyle = ZONE_COLOR[z.kind] ?? 'rgba(148,163,184,0.08)';
      g.beginPath();
      z.poly.forEach((p, i) => { const q = P(p); if (i) g.lineTo(q[0], q[1]); else g.moveTo(q[0], q[1]); });
      g.closePath(); g.fill();
      const c = z.poly.reduce<[number, number]>((acc, p) => [acc[0] + p[0] / z.poly.length, acc[1] + p[1] / z.poly.length], [0, 0]);
      const q = P(c);
      g.fillStyle = '#64748b'; g.font = '10px ui-monospace, monospace'; g.textAlign = 'center';
      g.fillText(z.id, q[0], q[1]);
    }

    for (const r of layout.routes) {
      const on = !routeId || r.id === routeId;
      g.strokeStyle = on ? 'rgba(226,232,240,0.55)' : 'rgba(100,116,139,0.25)';
      g.setLineDash([6, 5]); g.lineWidth = 1.5;
      g.beginPath();
      r.points.forEach((p, i) => { const q = P(p); if (i) g.lineTo(q[0], q[1]); else g.moveTo(q[0], q[1]); });
      g.stroke(); g.setLineDash([]);
    }

    for (const wall of layout.walls) {
      const a = P(wall.a), b = P(wall.b);
      g.strokeStyle = '#94a3b8'; g.lineCap = 'butt';
      g.lineWidth = Math.max(2, wall.thickness * s);
      g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke();
    }

    // v2.1: the selected route's known threats (red diamonds).
    for (const r of layout.routes) if (routeId && r.id === routeId) for (const th of r.threats ?? []) {
      const q = P(th.pos), d = 8;
      g.fillStyle = 'rgba(239,68,68,0.25)'; g.strokeStyle = '#ef4444'; g.lineWidth = 2;
      g.beginPath(); g.moveTo(q[0], q[1] - d); g.lineTo(q[0] + d, q[1]); g.lineTo(q[0], q[1] + d); g.lineTo(q[0] - d, q[1]); g.closePath(); g.fill(); g.stroke();
      if (frame && frame.t >= th.since_s) { g.fillStyle = '#fca5a5'; g.font = '10px ui-monospace, monospace'; g.textAlign = 'center'; g.fillText('угроза', q[0], q[1] - d - 4); }
    }

    for (const m of marks ?? []) {
      const q = P(m);
      g.strokeStyle = '#f97316'; g.lineWidth = 2;
      g.beginPath(); g.arc(q[0], q[1], 9, 0, Math.PI * 2); g.stroke();
    }

    if (frame) {
      const r = coneLength * s;
      for (const m of frame.members) {
        const q = P(m.pos), a = screenAngle(m.yaw), half = (fov / 2) * (Math.PI / 180);
        g.fillStyle = ROLE_COLOR[m.role] + '26'; g.strokeStyle = ROLE_COLOR[m.role] + 'aa'; g.lineWidth = 1;
        g.beginPath(); g.moveTo(q[0], q[1]); g.arc(q[0], q[1], r, a - half, a + half); g.closePath(); g.fill(); g.stroke();
      }
      for (const m of frame.members) {
        const sl = P(m.slot);
        g.strokeStyle = m.slotInvalid ? '#ef4444' : m.slotFallback ? '#f97316' : '#475569'; g.lineWidth = 1.5;
        g.beginPath(); g.moveTo(sl[0] - 4, sl[1] - 4); g.lineTo(sl[0] + 4, sl[1] + 4); g.moveTo(sl[0] - 4, sl[1] + 4); g.lineTo(sl[0] + 4, sl[1] - 4); g.stroke();
        const q = P(m.pos), a = screenAngle(m.yaw), rad = Math.max(6, 35 * s);
        g.strokeStyle = ROLE_COLOR[m.role]; g.lineWidth = 3;
        g.beginPath(); g.moveTo(q[0], q[1]); g.lineTo(q[0] + Math.cos(a) * rad * 2.2, q[1] + Math.sin(a) * rad * 2.2); g.stroke();
        g.fillStyle = ROLE_COLOR[m.role];
        g.beginPath(); g.arc(q[0], q[1], rad, 0, Math.PI * 2); g.fill();
        if (m.backwards) { g.strokeStyle = '#f8fafc'; g.lineWidth = 1.5; g.beginPath(); g.arc(q[0], q[1], rad + 3, 0, Math.PI * 2); g.stroke(); }
        if (m.cornerWatch) { g.strokeStyle = '#facc15'; g.lineWidth = 2; g.beginPath(); g.arc(q[0], q[1], rad + 6, 0, Math.PI * 2); g.stroke(); }
        g.fillStyle = '#020617'; g.font = `bold ${Math.max(9, rad * 1.2)}px ui-monospace, monospace`; g.textAlign = 'center'; g.textBaseline = 'middle';
        g.fillText(ROLE_LABEL[m.role] ?? m.role, q[0], q[1] + 0.5);
        g.textBaseline = 'alphabetic';
      }
    }
  }, [layout, frame, routeId, fov, coneLength, height, marks, focus, span]);

  return <canvas ref={ref} style={{ width: '100%', height }} className="rounded-xl border border-slate-800 block" />;
};
