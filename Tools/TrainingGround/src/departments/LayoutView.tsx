// Training Ground / level generator: the top-down canvas of a generated layout (schema codex.levelgen_layout/1) - the same
// vocabulary as Scripts/Tools/levelgen_render.py (the designer compares like with like): zones tinted by set-piece, buildings
// (green inner outline = walkable roof, orange squares = roof holes, green squares = stairs, white dots = doors), the cut with
// its crossings, ladders, covers, the mandatory route (blue) and the variants (light blue), spawns / ambush points / patrols.
// UE axes: X forward = up on screen, Y right = right. `layer` filters the walls / buildings to one floor layer.
import React, { useEffect, useImperativeHandle, useRef } from 'react';
import type { Vec2 } from '../movement/geometry';
import type { GenLayout } from '../levelgen/schema';

const SET_TINT: Record<string, string> = {
  street: '#787882', marksman_lane: '#8a7070', t_junction: '#8c8c5a', alley: '#969664', facade: '#5a463c', one_entry_compound: '#966e3c',
  industrial_hall: '#6e5032', warehouse: '#786450', junkyard_field: '#aa5a28', cut_crossing: '#283c6e', tank_farm: '#825a46', open_square: '#3c8246',
  ruined_two_level: '#963c32', dead_end: '#aa5050', ladder_overwatch: '#82643c', tower: '#646e82', chimney: '#46464e',
};

export interface LayoutViewHandle { toPngBase64: () => string | null }

interface Props {
  layout: GenLayout | null;
  /** 'all' or a layer name ('ground', 'cut', 'roof:<b>', 'floor:<b>:<n>'). */
  layer?: string;
  height?: number;
  /** Highlighted points (failing route legs). */
  marks?: Vec2[];
}

export const LayoutView = React.forwardRef<LayoutViewHandle, Props>(({ layout, layer = 'all', height = 560, marks }, ref) => {
  const canvas = useRef<HTMLCanvasElement>(null);
  useImperativeHandle(ref, () => ({ toPngBase64: () => canvas.current ? canvas.current.toDataURL('image/png').replace(/^data:image\/png;base64,/, '') : null }), []);

  useEffect(() => {
    const c = canvas.current;
    if (!c) return;
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    const w = c.clientWidth, h = c.clientHeight;
    c.width = Math.round(w * dpr); c.height = Math.round(h * dpr);
    const g = c.getContext('2d');
    if (!g) return;
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.fillStyle = '#191c22'; g.fillRect(0, 0, w, h);
    if (!layout) return;
    const [minX, minY] = layout.bounds.min, [maxX, maxY] = layout.bounds.max;
    const pad = 10;
    const s = Math.min((w - 2 * pad) / (maxY - minY), (h - 2 * pad) / (maxX - minX));
    const ox = (w - (maxY - minY) * s) / 2, oy = (h - (maxX - minX) * s) / 2;
    const P = (p: Vec2): [number, number] => [ox + (p[1] - minY) * s, oy + (maxX - p[0]) * s];
    const poly = (pts: Vec2[]) => { g.beginPath(); pts.forEach((p, i) => { const q = P(p); if (i) g.lineTo(q[0], q[1]); else g.moveTo(q[0], q[1]); }); g.closePath(); };
    const rectPts = ([a, b]: [Vec2, Vec2]): Vec2[] => [[a[0], a[1]], [b[0], a[1]], [b[0], b[1]], [a[0], b[1]]];
    const onLayer = (floor: number, building?: string) => layer === 'all' || (layer === 'ground' && floor === 0) || (building && layer === `floor:${building}:${floor}`);
    const showBuilding = (bid: string, floors: number) => layer === 'all' || layer === 'ground' || layer === 'cut' || (layer.startsWith('floor:') && layer.split(':')[1] === bid && Number(layer.split(':')[2]) < floors) || layer === `roof:${bid}`;

    // 10 m grid
    g.lineWidth = 1; g.strokeStyle = '#242a36';
    for (let x = Math.ceil(minX / 1000) * 1000; x <= maxX; x += 1000) { const a = P([x, minY]), b = P([x, maxY]); g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke(); }
    for (let y = Math.ceil(minY / 1000) * 1000; y <= maxY; y += 1000) { const a = P([minX, y]), b = P([maxX, y]); g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke(); }
    // zones
    for (const z of layout.zones) {
      const tint = SET_TINT[z.set_piece ?? ''] ?? '#808080';
      g.fillStyle = tint + '40'; g.strokeStyle = tint + 'b0'; g.lineWidth = 1;
      poly(z.poly); g.fill(); g.stroke();
    }
    // the cut
    if (layout.cut && (layer === 'all' || layer === 'ground' || layer === 'cut')) {
      g.fillStyle = '#1e2850'; g.strokeStyle = '#465aa0'; poly(layout.cut.poly); g.fill(); g.stroke();
      for (const x of layout.cut.crossings) { g.fillStyle = '#c8c8be'; poly(x.poly); g.fill(); }
    }
    // buildings
    for (const b of layout.buildings) {
      if (!showBuilding(b.id, b.floors)) continue;
      const pts = rectPts(b.footprint);
      g.fillStyle = b.set_piece === 'chimney' ? '#46464e' : b.set_piece === 'tower' ? '#646e82' : b.height >= 600 ? '#967864' : '#aa9678'; g.strokeStyle = '#ffffff'; g.lineWidth = 1.2;
      poly(pts); g.fill(); g.stroke();
      if (b.roof_walkable) { g.strokeStyle = '#50dc78'; g.lineWidth = 1.5; const q = P(b.footprint[0]), r = P(b.footprint[1]); g.strokeRect(Math.min(q[0], r[0]) + 3, Math.min(q[1], r[1]) + 3, Math.abs(r[0] - q[0]) - 6, Math.abs(r[1] - q[1]) - 6); }
      for (const d of b.doors) { const q = P(d.pos); g.fillStyle = '#fff'; g.beginPath(); g.arc(q[0], q[1], 2.5, 0, Math.PI * 2); g.fill(); }
      for (const hpos of b.roof_holes) { const q = P(hpos); g.strokeStyle = '#ff783c'; g.lineWidth = 2; g.strokeRect(q[0] - 4, q[1] - 4, 8, 8); }
      for (const st of b.stairs) { const q = P(st.pos); g.fillStyle = '#50dc78'; g.fillRect(q[0] - 3.5, q[1] - 3.5, 7, 7); const t = P(st.top); g.strokeStyle = '#50dc78'; g.lineWidth = 1; g.beginPath(); g.moveTo(q[0], q[1]); g.lineTo(t[0], t[1]); g.stroke(); }
      const q = P(b.footprint[1]);
      g.fillStyle = '#fff'; g.font = '9px ui-monospace, monospace'; g.textAlign = 'left'; g.fillText(b.roof_walkable ? `${b.id} +${b.floors}` : b.id, Math.min(P(b.footprint[0])[0], q[0]) + 3, Math.min(P(b.footprint[0])[1], q[1]) + 10);
    }
    // walls
    for (const wl of layout.walls) {
      if (!onLayer(wl.floor ?? 0, wl.building)) continue;
      const a = P(wl.a), b = P(wl.b);
      g.strokeStyle = wl.kind === 'fence' ? '#e6e678' : wl.kind === 'cut_wall' ? '#465aa0' : '#c8c8d2'; g.lineCap = 'butt';
      g.lineWidth = Math.max(1, wl.thickness * s);
      g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke();
    }
    // covers
    if (layer === 'all' || layer === 'ground') for (const cv of layout.covers) {
      const q = P(cv.pos), rx = Math.max(1.5, cv.size[0] / 2 * s), ry = Math.max(1.5, cv.size[1] / 2 * s);
      g.save(); g.translate(q[0], q[1]); g.rotate(-cv.yaw * Math.PI / 180);
      g.fillStyle = cv.size[2] >= 140 ? '#8c7c62' : '#6e645a'; g.strokeStyle = '#c8be a0'.replace(' ', ''); g.lineWidth = 0.8;
      if (cv.kind === 'silo') { g.beginPath(); g.arc(0, 0, rx, 0, Math.PI * 2); g.fill(); g.stroke(); } else { g.fillRect(-ry, -rx, ry * 2, rx * 2); g.strokeRect(-ry, -rx, ry * 2, rx * 2); }
      g.restore();
    }
    // ladders
    for (const l of layout.ladders) { const q = P(l.pos); g.fillStyle = '#ffe600'; g.strokeStyle = '#000'; g.lineWidth = 1; g.fillRect(q[0] - 3.5, q[1] - 6, 7, 12); g.strokeRect(q[0] - 3.5, q[1] - 6, 7, 12); }
    // routes
    for (const r of layout.routes) {
      const main = !!r.mandatory;
      g.strokeStyle = main ? '#008cff' : '#5ac8ff'; g.lineWidth = main ? 4 : 1.5; g.setLineDash(main ? [] : [5, 4]);
      g.beginPath(); r.points.forEach((p, i) => { const q = P(p); if (i) g.lineTo(q[0], q[1]); else g.moveTo(q[0], q[1]); }); g.stroke(); g.setLineDash([]);
      if (main) r.points.forEach((p, i) => { const q = P(p); g.fillStyle = '#008cff'; g.beginPath(); g.arc(q[0], q[1], 3.5, 0, Math.PI * 2); g.fill(); g.fillStyle = '#c8e6ff'; g.font = '9px ui-monospace, monospace'; g.fillText(String(i), q[0] + 5, q[1] - 4); });
      if (!main) { const p = r.points[Math.floor(r.points.length / 2)]; const q = P(p); g.fillStyle = r.intent === 'kill_zone' ? '#ff6464' : '#5ac8ff'; g.font = '9px ui-monospace, monospace'; g.fillText(r.intent === 'kill_zone' ? `${r.id} (kill zone, intended)` : r.id, q[0] + 4, q[1] + 10); }
    }
    for (const sp of layout.spawn_points) { const q = P(sp.pos); g.fillStyle = '#ff5050'; g.beginPath(); g.moveTo(q[0], q[1] - 5); g.lineTo(q[0] + 5, q[1] + 4); g.lineTo(q[0] - 5, q[1] + 4); g.closePath(); g.fill(); }
    for (const ap of layout.ambush_points) { const q = P(ap.pos); g.strokeStyle = '#ffa03c'; g.lineWidth = 1.5; g.beginPath(); g.arc(q[0], q[1], 3.5, 0, Math.PI * 2); g.stroke(); }
    for (const pr of layout.patrol_routes) { g.strokeStyle = '#ff6464'; g.lineWidth = 1; g.beginPath(); pr.points.forEach((p, i) => { const q = P(p); if (i) g.lineTo(q[0], q[1]); else g.moveTo(q[0], q[1]); }); g.closePath(); g.stroke(); }
    const sq = P(layout.spawn.pos); g.strokeStyle = '#00e65a'; g.lineWidth = 3; g.beginPath(); g.arc(sq[0], sq[1], 9, 0, Math.PI * 2); g.stroke();
    const eq = P(layout.end); g.strokeStyle = '#ff5050'; g.lineWidth = 3; g.strokeRect(eq[0] - 8, eq[1] - 8, 16, 16);
    for (const m of marks ?? []) { const q = P(m); g.strokeStyle = '#f97316'; g.lineWidth = 2.5; g.beginPath(); g.arc(q[0], q[1], 10, 0, Math.PI * 2); g.stroke(); }
    g.fillStyle = '#ffe600'; g.font = '11px ui-monospace, monospace'; g.textAlign = 'left';
    g.fillText(`${layout.name}  ${Math.round((maxY - minY) / 100)} x ${Math.round((maxX - minX) / 100)} m  seed ${layout.seed}  layer ${layer}`, 8, h - 8);
  }, [layout, layer, height, marks]);

  return <canvas ref={canvas} style={{ width: '100%', height }} className="rounded-xl border border-slate-800 block" />;
});
LayoutView.displayName = 'LayoutView';
