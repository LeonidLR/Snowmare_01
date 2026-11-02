// Training Ground «Боевая лаборатория»: the top-down map of the chosen map - L_FormationLabCombat (walls of formation_lab_01, the towers of the
// combat overlay) or a generated map (its accepted levelgen layout: walls, building footprints, ladders), the bot's planned route (numbered, wall / climb waypoints marked), the authored ambush points and - in a report - where the
// bot got stuck and where the contacts happened. X right, Y up on screen - the lab's compass (its towers T_NE at +X +Y, T_SW at -X -Y).
import React, { useEffect, useRef } from 'react';

export interface LabWall { id: string; a: [number, number]; b: [number, number]; thickness?: number }
export interface LabTower { id: string; center: [number, number]; size: [number, number] }
export interface LabBuilding { id: string; min: [number, number]; max: [number, number]; floors?: number }
export interface LabLadder { id: string; pos: [number, number] }
export interface LabLayout {
  bounds: { min: [number, number]; max: [number, number] } | null; walls: LabWall[]; towers: LabTower[]; spawn?: { pos: [number, number] } | null;
  /** Generated maps only: building footprints (outlined) and ladder markers. */
  buildings?: LabBuilding[]; ladders?: LabLadder[];
}
export interface LabRoutePoint { at: [number, number]; label?: string; action?: string; wall?: [number, number] }
export interface LabMark { at: [number, number]; kind: 'stuck' | 'skipped' | 'patrol' | 'ambush' | 'coordinated' | 'end'; title?: string }

const MARK_COLOR: Record<LabMark['kind'], string> = {
  stuck: '#fbbf24', skipped: '#f43f5e', patrol: '#38bdf8', ambush: '#f97316', coordinated: '#e879f9', end: '#a3e635',
};

interface Props {
  layout: LabLayout | null;
  route?: LabRoutePoint[];
  loop?: boolean;
  ambushPoints?: [number, number][];
  marks?: LabMark[];
  height?: number;
}

export const CombatLabMap: React.FC<Props> = ({ layout, route = [], loop = true, ambushPoints = [], marks = [], height = 460 }) => {
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
    if (!layout) {
      g.fillStyle = '#64748b';
      g.font = '13px sans-serif';
      g.fillText('Карта загружается…', 16, 24);
      return;
    }
    const min = layout.bounds?.min ?? [-6000, -6000];
    const max = layout.bounds?.max ?? [6000, 6000];
    // Screen: x = X, y = -Y (north = +Y up).
    const spanX = max[0] - min[0], spanY = max[1] - min[1];
    const scale = Math.min((w - 24) / spanX, (h - 24) / spanY);
    const ox = (w - spanX * scale) / 2, oy = (h - spanY * scale) / 2;
    const sx = (p: [number, number]) => ox + (p[0] - min[0]) * scale;
    const sy = (p: [number, number]) => oy + (max[1] - p[1]) * scale;

    g.strokeStyle = '#1e293b';
    g.lineWidth = 1;
    for (let x = Math.ceil(min[0] / 1000) * 1000; x <= max[0]; x += 1000) { g.beginPath(); g.moveTo(sx([x, min[1]]), sy([x, min[1]])); g.lineTo(sx([x, max[1]]), sy([x, max[1]])); g.stroke(); }
    for (let y = Math.ceil(min[1] / 1000) * 1000; y <= max[1]; y += 1000) { g.beginPath(); g.moveTo(sx([min[0], y]), sy([min[0], y])); g.lineTo(sx([max[0], y]), sy([max[0], y])); g.stroke(); }

    g.lineCap = 'butt';
    g.strokeStyle = '#94a3b8';
    for (const wall of layout.walls) {
      g.lineWidth = Math.max(2, (wall.thickness ?? 40) * scale);
      g.beginPath(); g.moveTo(sx(wall.a), sy(wall.a)); g.lineTo(sx(wall.b), sy(wall.b)); g.stroke();
    }
    g.fillStyle = 'rgba(71,85,105,0.35)';
    g.strokeStyle = '#64748b';
    g.lineWidth = 1;
    g.font = '10px sans-serif';
    for (const b of layout.buildings ?? []) {
      const x0 = sx([b.min[0], b.max[1]]), y0 = sy([b.min[0], b.max[1]]);
      g.fillRect(x0, y0, (b.max[0] - b.min[0]) * scale, (b.max[1] - b.min[1]) * scale);
      g.strokeRect(x0, y0, (b.max[0] - b.min[0]) * scale, (b.max[1] - b.min[1]) * scale);
    }
    g.fillStyle = '#475569';
    g.strokeStyle = '#cbd5e1';
    for (const t of layout.towers) {
      const [cx, cy] = t.center, [sizeX, sizeY] = t.size;
      const x0 = sx([cx - sizeX / 2, cy + sizeY / 2]), y0 = sy([cx - sizeX / 2, cy + sizeY / 2]);
      g.fillRect(x0, y0, sizeX * scale, sizeY * scale);
      g.strokeRect(x0, y0, sizeX * scale, sizeY * scale);
      g.fillStyle = '#cbd5e1';
      g.fillText(t.id, x0 + sizeX * scale + 3, y0 + 8);
      g.fillStyle = '#475569';
    }
    g.fillStyle = '#a78bfa';
    for (const l of layout.ladders ?? []) g.fillRect(sx(l.pos) - 2, sy(l.pos) - 2, 4, 4);
    if (layout.spawn) {
      g.fillStyle = '#22c55e';
      g.beginPath(); g.arc(sx(layout.spawn.pos), sy(layout.spawn.pos), 5, 0, Math.PI * 2); g.fill();
    }
    // Authored ambush points.
    g.strokeStyle = '#f97316';
    g.lineWidth = 1.5;
    for (const p of ambushPoints) {
      const x = sx(p), y = sy(p);
      g.beginPath(); g.moveTo(x - 5, y); g.lineTo(x, y - 5); g.lineTo(x + 5, y); g.lineTo(x, y + 5); g.closePath(); g.stroke();
    }
    // The route.
    if (route.length >= 2) {
      g.strokeStyle = 'rgba(34,211,238,0.75)';
      g.lineWidth = 2;
      g.setLineDash([6, 4]);
      g.beginPath();
      route.forEach((p, i) => (i === 0 ? g.moveTo(sx(p.at), sy(p.at)) : g.lineTo(sx(p.at), sy(p.at))));
      if (loop) g.lineTo(sx(route[0].at), sy(route[0].at));
      g.stroke();
      g.setLineDash([]);
    }
    g.font = 'bold 10px sans-serif';
    route.forEach((p, i) => {
      const x = sx(p.at), y = sy(p.at);
      const color = p.action === 'climb' ? '#a78bfa' : p.action === 'wall' ? '#34d399' : '#22d3ee';
      if (p.action === 'wall' && p.wall) {
        g.strokeStyle = '#34d399';
        g.lineWidth = 1.5;
        g.beginPath(); g.moveTo(x, y); g.lineTo(sx(p.wall), sy(p.wall)); g.stroke();
      }
      g.fillStyle = color;
      g.beginPath(); g.arc(x, y, 8, 0, Math.PI * 2); g.fill();
      g.fillStyle = '#020617';
      const text = String(i);
      g.fillText(text, x - g.measureText(text).width / 2, y + 3.5);
    });
    // The axes.
    g.fillStyle = '#64748b';
    g.font = '10px sans-serif';
    g.fillText('X →   Y ↑ (север)', 8, h - 8);
    // Report marks.
    for (const m of marks) {
      const x = sx(m.at), y = sy(m.at);
      g.strokeStyle = MARK_COLOR[m.kind];
      g.fillStyle = MARK_COLOR[m.kind];
      g.lineWidth = 2.5;
      if (m.kind === 'stuck' || m.kind === 'skipped') {
        g.beginPath(); g.moveTo(x - 6, y - 6); g.lineTo(x + 6, y + 6); g.moveTo(x + 6, y - 6); g.lineTo(x - 6, y + 6); g.stroke();
      } else if (m.kind === 'end') {
        g.beginPath(); g.rect(x - 5, y - 5, 10, 10); g.stroke();
      } else {
        g.globalAlpha = 0.8;
        g.beginPath(); g.arc(x, y, 4, 0, Math.PI * 2); g.fill();
        g.globalAlpha = 1;
      }
    }
  }, [layout, route, loop, ambushPoints, marks]);

  return <canvas ref={ref} className="w-full rounded-xl border border-slate-800" style={{ height }} />;
};

export const MapLegend: React.FC<{ report?: boolean }> = ({ report }) => (
  <div className="flex flex-wrap gap-x-4 gap-y-1 text-[11px] text-slate-400">
    <span><span className="inline-block w-2.5 h-2.5 rounded-full bg-cyan-400 mr-1 align-middle" />точка маршрута</span>
    <span><span className="inline-block w-2.5 h-2.5 rounded-full bg-emerald-400 mr-1 align-middle" />к стене</span>
    <span><span className="inline-block w-2.5 h-2.5 rounded-full bg-violet-400 mr-1 align-middle" />подъём на вышку</span>
    <span><span className="text-orange-400 mr-1">◇</span>точка засады дизайнера</span>
    {report && <>
      <span><span className="text-amber-300 mr-1">✕</span>застрял</span>
      <span><span className="text-rose-400 mr-1">✕</span>точка пропущена</span>
      <span><span className="inline-block w-2 h-2 rounded-full bg-sky-400 mr-1 align-middle" />патруль</span>
      <span><span className="inline-block w-2 h-2 rounded-full bg-orange-500 mr-1 align-middle" />засада</span>
      <span><span className="inline-block w-2 h-2 rounded-full bg-fuchsia-400 mr-1 align-middle" />скоординированная</span>
      <span><span className="text-lime-400 mr-1">□</span>где закончил</span>
    </>}
  </div>
);
