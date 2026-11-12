// Training Ground / level generator: the top-down canvas of a generated layout (schema codex.levelgen_layout/1) - the same
// vocabulary as Scripts/Tools/levelgen_render.py (the designer compares like with like): zones tinted by set-piece, buildings
// (green inner outline = walkable roof, orange squares = roof holes, green squares = stairs, white dots = doors), the cut with
// its crossings, ladders, covers, the mandatory route (blue) and the variants (light blue), spawns / ambush points / patrols.
// UE axes: X forward = up on screen, Y right = right. `layer` filters the walls / buildings to one floor layer.
import React, { useEffect, useImperativeHandle, useRef } from 'react';
import { dirOf, type Vec2 } from '../movement/geometry';
import type { GenBuilding, GenLayout, MantleEdge } from '../levelgen/schema';
import { unifyOpenings } from '../levelgen/schema';
import type { FiringLine } from '../levelgen/tacticalEvaluate';

const SET_TINT: Record<string, string> = {
  street: '#787882', marksman_lane: '#8a7070', t_junction: '#8c8c5a', alley: '#969664', facade: '#5a463c', one_entry_compound: '#966e3c',
  industrial_hall: '#6e5032', warehouse: '#786450', junkyard_field: '#aa5a28', cut_crossing: '#283c6e', tank_farm: '#825a46', open_square: '#3c8246',
  ruined_two_level: '#963c32', dead_end: '#aa5050', ladder_overwatch: '#82643c', tower: '#646e82', chimney: '#46464e',
};

export interface LayoutViewHandle { toPngBase64: () => string | null }

export type FloorViewMode = 'all' | 'ground' | '1' | '2' | 'roof';

interface Props {
  layout: GenLayout | null;
  /** 'all' or a layer name ('ground', 'cut', 'roof:<b>', 'floor:<b>:<n>'). */
  layer?: string;
  /** Quick floor switcher filter ('all' | 'ground' | '1' | '2' | 'roof'). */
  floorMode?: FloorViewMode;
  /** Firing lines from windows & loopholes onto the mandatory route. */
  firingLines?: FiringLine[];
  /** Toggle display of firing line rays. */
  showFiringLines?: boolean;
  height?: number;
  /** Highlighted points (failing route legs). */
  marks?: Vec2[];
}

export const LayoutView = React.forwardRef<LayoutViewHandle, Props>(({ layout, layer = 'all', floorMode = 'all', firingLines, showFiringLines = false, height = 560, marks }, ref) => {
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
    const floorNum = floorMode === 'ground' ? 0 : floorMode === '1' ? 1 : floorMode === '2' ? 2 : null;
    const floorActive = (fl: number, bid?: string): boolean => {
      if (layer !== 'all') {
        if (layer === 'ground' || layer === 'cut') return fl === 0;
        if (layer.startsWith('floor:') && bid) return layer === `floor:${bid}:${fl}`;
        return false;
      }
      if (floorMode === 'roof') return false;
      return floorNum === null || fl === floorNum;
    };
    const roofActive = (bid: string): boolean => {
      if (layer !== 'all') return layer === `roof:${bid}`;
      return floorMode === 'all' || floorMode === 'roof';
    };
    const onLayer = (floor: number, building?: string) => floorActive(floor, building);
    const showBuilding = (b: GenBuilding) => {
      if (layer !== 'all') return layer === 'ground' || layer === 'cut' || (layer.startsWith('floor:') && layer.split(':')[1] === b.id && Number(layer.split(':')[2]) < b.floors) || layer === `roof:${b.id}`;
      if (floorMode === 'roof') return b.roof_walkable;
      if (floorNum !== null) return floorNum === 0 || (!b.hollow && floorNum < b.floors);
      return true;
    };

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
      if (!showBuilding(b)) continue;
      const pts = rectPts(b.footprint);
      g.fillStyle = b.set_piece === 'chimney' ? '#46464e' : b.set_piece === 'tower' ? '#646e82' : b.height >= 600 ? '#967864' : '#aa9678'; g.strokeStyle = '#ffffff'; g.lineWidth = 1.2;
      poly(pts); g.fill(); g.stroke();
      if (b.roof_walkable && roofActive(b.id)) {
        g.strokeStyle = '#50dc78'; g.lineWidth = 1.5; const q = P(b.footprint[0]), r = P(b.footprint[1]);
        g.strokeRect(Math.min(q[0], r[0]) + 3, Math.min(q[1], r[1]) + 3, Math.abs(r[0] - q[0]) - 6, Math.abs(r[1] - q[1]) - 6);
        for (const par of b.parapets ?? []) {
          const pA = P(par.start), pB = P(par.end);
          g.strokeStyle = '#34d399'; g.lineWidth = 2.2; g.beginPath(); g.moveTo(pA[0], pA[1]); g.lineTo(pB[0], pB[1]); g.stroke();
        }
      }
      // Interior rooms (subtle boundary & id)
      for (const rm of b.rooms ?? []) {
        if (!floorActive(rm.floor, b.id)) continue;
        const rA = P(rm.bounds.min), rB = P(rm.bounds.max);
        const rx = Math.min(rA[0], rB[0]), ry = Math.min(rA[1], rB[1]), rw = Math.abs(rB[0] - rA[0]), rh = Math.abs(rB[1] - rA[1]);
        g.save(); g.strokeStyle = 'rgba(226, 232, 240, 0.35)'; g.lineWidth = 0.8; g.setLineDash([2, 2]); g.strokeRect(rx + 1, ry + 1, Math.max(2, rw - 2), Math.max(2, rh - 2)); g.restore();
        if (floorMode !== 'all' || layer !== 'all') {
          g.fillStyle = 'rgba(226, 232, 240, 0.7)'; g.font = '8px ui-monospace, monospace'; g.textAlign = 'center';
          g.fillText(rm.id, rx + rw / 2, ry + rh / 2 + 3);
        }
      }
      // Upper floor slab details & floor gaps (stairwell / hole voids)
      for (const fd of b.floors_detail ?? []) {
        if (!floorActive(fd.floor, b.id)) continue;
        for (const gp of fd.gaps ?? []) {
          if (gp.poly.length < 3) continue;
          g.save(); g.fillStyle = 'rgba(15, 23, 42, 0.65)'; g.strokeStyle = '#fb923c'; g.lineWidth = 1.4; g.setLineDash([3, 2]);
          poly(gp.poly); g.fill(); g.stroke();
          const xs = gp.poly.map((pt) => P(pt)[0]), ys = gp.poly.map((pt) => P(pt)[1]);
          const gx = Math.min(...xs), gy = Math.min(...ys), gx2 = Math.max(...xs), gy2 = Math.max(...ys);
          g.beginPath(); g.moveTo(gx, gy); g.lineTo(gx2, gy2); g.moveTo(gx2, gy); g.lineTo(gx, gy2); g.stroke();
          g.restore();
        }
      }
      // Interior partition doors
      for (const pt of b.partitions ?? []) {
        if (!floorActive(pt.floor, b.id)) continue;
        for (const op of pt.openings ?? []) {
          const q = P(op.pos); g.fillStyle = '#fde047'; g.strokeStyle = '#1e293b'; g.lineWidth = 1;
          g.beginPath(); g.arc(q[0], q[1], 2.5, 0, Math.PI * 2); g.fill(); g.stroke();
        }
      }
      // Unified exterior doors, windows (cyan), and loopholes (amber)
      for (const op of unifyOpenings(b)) {
        if (!floorActive(op.floor, b.id)) continue;
        if (op.kind === 'door') {
          const q = P(op.pos); g.fillStyle = '#fff'; g.beginPath(); g.arc(q[0], q[1], 2.5, 0, Math.PI * 2); g.fill();
        } else {
          const dir = typeof op.yaw === 'number' ? dirOf(op.yaw) : (() => {
            const [[x0, y0], [x1, y1]] = b.footprint;
            const dW = Math.abs(op.pos[0] - x0), dE = Math.abs(op.pos[0] - x1), dN = Math.abs(op.pos[1] - y0), dS = Math.abs(op.pos[1] - y1);
            const m = Math.min(dW, dE, dN, dS);
            return m === dW ? [-1, 0] as Vec2 : m === dE ? [1, 0] as Vec2 : m === dN ? [0, -1] as Vec2 : [0, 1] as Vec2;
          })();
          const tan: Vec2 = [-dir[1], dir[0]];
          const half = Math.max(40, (op.width ?? (op.kind === 'window' ? 120 : 60)) / 2);
          const aPt = P([op.pos[0] - tan[0] * half, op.pos[1] - tan[1] * half]);
          const bPt = P([op.pos[0] + tan[0] * half, op.pos[1] + tan[1] * half]);
          const cPt = P(op.pos);
          const tip = P([op.pos[0] + dir[0] * 45, op.pos[1] + dir[1] * 45]);
          g.strokeStyle = op.kind === 'window' ? '#38bdf8' : '#fbbf24';
          g.lineWidth = op.kind === 'window' ? 2.6 : 2.2;
          g.beginPath(); g.moveTo(aPt[0], aPt[1]); g.lineTo(bPt[0], bPt[1]); g.moveTo(cPt[0], cPt[1]); g.lineTo(tip[0], tip[1]); g.stroke();
        }
      }
      for (const hpos of b.roof_holes) { const q = P(hpos); g.strokeStyle = '#ff783c'; g.lineWidth = 2; g.strokeRect(q[0] - 4, q[1] - 4, 8, 8); }
      for (const ht of b.hatches ?? []) {
        if (!(roofActive(b.id) || floorActive(ht.floor, b.id))) continue;
        const q = P(ht.pos); g.fillStyle = '#7c2d12'; g.strokeStyle = '#fb923c'; g.lineWidth = 1.6;
        g.fillRect(q[0] - 4.5, q[1] - 4.5, 9, 9); g.strokeRect(q[0] - 4.5, q[1] - 4.5, 9, 9);
        g.strokeStyle = '#fed7aa'; g.lineWidth = 1; g.beginPath(); g.moveTo(q[0] - 3, q[1] - 3); g.lineTo(q[0] + 3, q[1] + 3); g.moveTo(q[0] + 3, q[1] - 3); g.lineTo(q[0] - 3, q[1] + 3); g.stroke();
      }
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
    // subtle mantleable edges (window sills, parapets, low covers)
    const mantleMap = new Map<string, MantleEdge>();
    for (const me of layout.mantle_edges ?? []) mantleMap.set(me.id, me);
    for (const b of layout.buildings) for (const me of b.mantle_edges ?? []) mantleMap.set(me.id, me);
    g.save();
    g.strokeStyle = 'rgba(192, 132, 252, 0.85)';
    g.lineWidth = 1.6;
    g.setLineDash([3, 3]);
    for (const me of mantleMap.values()) {
      const mFloor = me.floor ?? 0;
      const showM = me.kind === 'parapet'
        ? (floorMode === 'all' || floorMode === 'roof' || layer.startsWith('roof:'))
        : (floorNum === null ? layer === 'all' || layer === 'ground' : mFloor === floorNum);
      if (!showM) continue;
      const pA = P(me.start), pB = P(me.end);
      g.beginPath(); g.moveTo(pA[0], pA[1]); g.lineTo(pB[0], pB[1]); g.stroke();
    }
    g.restore();

    // dashed firing lines from windows/loopholes to mandatory route
    if (showFiringLines && firingLines && firingLines.length > 0) {
      g.save();
      g.strokeStyle = 'rgba(248, 113, 113, 0.78)';
      g.fillStyle = '#fca5a5';
      g.lineWidth = 1.4;
      g.setLineDash([5, 4]);
      for (const fl of firingLines) {
        if (floorMode === 'roof') continue;
        if (floorNum !== null && fl.floor !== floorNum) continue;
        const pA = P(fl.from), pB = P(fl.to);
        g.beginPath(); g.moveTo(pA[0], pA[1]); g.lineTo(pB[0], pB[1]); g.stroke();
        g.beginPath(); g.arc(pB[0], pB[1], 2.2, 0, Math.PI * 2); g.fill();
      }
      g.restore();
    }

    for (const m of marks ?? []) { const q = P(m); g.strokeStyle = '#f97316'; g.lineWidth = 2.5; g.beginPath(); g.arc(q[0], q[1], 10, 0, Math.PI * 2); g.stroke(); }
    g.fillStyle = '#ffe600'; g.font = '11px ui-monospace, monospace'; g.textAlign = 'left';
    g.fillText(`${layout.name}  ${Math.round((maxY - minY) / 100)} x ${Math.round((maxX - minX) / 100)} m  seed ${layout.seed}  layer ${layer !== 'all' ? layer : floorMode}`, 8, h - 8);
  }, [layout, layer, floorMode, firingLines, showFiringLines, height, marks]);

  return <canvas ref={canvas} style={{ width: '100%', height }} className="rounded-xl border border-slate-800 block" />;
});
LayoutView.displayName = 'LayoutView';
