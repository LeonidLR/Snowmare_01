// Training Ground / level generator: top-down canvas with layout view and props overlay (schema codex.levelgen_props/1).
// Docs: docs/design/levelgen/PROPS_LAYERS.md sections 2.1-2.4, 4.1; Step 4 of inbox/010-night-queue.md.
// Visual vocabulary:
//   Crates: blue squares (or red if dangerous/mined).
//   Barrels: red circles (explosive with inner danger core).
//   Turrets: purple diamonds with yaw indicator.
//   Mines: yellow hazard discs with crosshairs.
//   Loot: emerald star/diamond placeholders.
//   Locked objects: prominent amber / gold outline (#fbbf24).
//   Overrides: added (green), moved (blue), deleted (gray X).

import React, { useEffect, useImperativeHandle, useRef } from 'react';
import type { GenLayout } from '../levelgen/schema.ts';
import type { LevelPropsFile, PropObject } from '../levelgen/props/schema.ts';

const SET_TINT: Record<string, string> = {
  street: '#787882',
  marksman_lane: '#8a7070',
  t_junction: '#8c8c5a',
  alley: '#969664',
  facade: '#5a463c',
  one_entry_compound: '#966e3c',
  industrial_hall: '#6e5032',
  warehouse: '#786450',
  junkyard_field: '#aa5a28',
  cut_crossing: '#283c6e',
  tank_farm: '#825a46',
  open_square: '#3c8246',
  ruined_two_level: '#963c32',
  dead_end: '#aa5050',
  ladder_overwatch: '#82643c',
  tower: '#646e82',
  chimney: '#46464e',
};

export interface PropsCanvasHandle {
  toPngBase64: () => string | null;
}

export interface PropsCanvasProps {
  layout: GenLayout | null;
  propsFile: LevelPropsFile | null;
  layerFilter?: string; // 'all' or specific walk layer
  visibleLayers?: Record<string, boolean>; // layerId -> boolean
  selectedPropId?: string | null;
  onSelectProp?: (prop: PropObject | null, layerId: string | null) => void;
  showExclusions?: boolean;
  height?: number;
}

export const PropsCanvas = React.forwardRef<PropsCanvasHandle, PropsCanvasProps>(
  (
    {
      layout,
      propsFile,
      layerFilter = 'all',
      visibleLayers = {},
      selectedPropId = null,
      onSelectProp,
      showExclusions = false,
      height = 560,
    },
    ref
  ) => {
    const canvas = useRef<HTMLCanvasElement>(null);
    const transformRef = useRef<{
      s: number;
      ox: number;
      oy: number;
      minX: number;
      minY: number;
      maxX: number;
      maxY: number;
    }>({ s: 1, ox: 0, oy: 0, minX: 0, minY: 0, maxX: 0, maxY: 0 });

    useImperativeHandle(
      ref,
      () => ({
        toPngBase64: () =>
          canvas.current ? canvas.current.toDataURL('image/png').replace(/^data:image\/png;base64,/, '') : null,
      }),
      []
    );

    // Canvas click handling: find nearest prop object
    const handleCanvasClick = (e: React.MouseEvent<HTMLCanvasElement>) => {
      const c = canvas.current;
      if (!c || !propsFile || !onSelectProp) return;
      const rect = c.getBoundingClientRect();
      const clickX = e.clientX - rect.left;
      const clickY = e.clientY - rect.top;

      const { s, ox, oy, minY, maxX } = transformRef.current;
      if (s <= 0) return;

      // Invert transform:
      // Screen X = ox + (worldY - minY) * s  =>  worldY = minY + (clickX - ox) / s
      // Screen Y = oy + (maxX - worldX) * s  =>  worldX = maxX - (clickY - oy) / s
      const worldY = minY + (clickX - ox) / s;
      const worldX = maxX - (clickY - oy) / s;

      let closestProp: PropObject | null = null;
      let closestLayerId: string | null = null;
      let minPixelDist = 18; // maximum click distance in screen pixels

      for (const layer of propsFile.layers) {
        if (visibleLayers[layer.id] === false) continue;
        for (const obj of layer.objects) {
          if (layerFilter !== 'all' && obj.walk_layer && obj.walk_layer !== layerFilter) {
            continue;
          }
          const dxWorld = obj.pos[0] - worldX;
          const dyWorld = obj.pos[1] - worldY;
          const pixelDist = Math.sqrt(dxWorld * dxWorld + dyWorld * dyWorld) * s;
          if (pixelDist < minPixelDist) {
            minPixelDist = pixelDist;
            closestProp = obj;
            closestLayerId = layer.id;
          }
        }
      }

      onSelectProp(closestProp, closestLayerId);
    };

    useEffect(() => {
      const c = canvas.current;
      if (!c) return;
      const dpr = Math.min(2, window.devicePixelRatio || 1);
      const w = c.clientWidth;
      const h = c.clientHeight;
      c.width = Math.round(w * dpr);
      c.height = Math.round(h * dpr);
      const g = c.getContext('2d');
      if (!g) return;

      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.fillStyle = '#191c22';
      g.fillRect(0, 0, w, h);

      if (!layout) {
        g.fillStyle = '#64748b';
        g.font = '14px ui-sans-serif, system-ui';
        g.textAlign = 'center';
        g.fillText('Выберите уровень или эталон для отображения раскладки', w / 2, h / 2);
        return;
      }

      const [minX, minY] = layout.bounds.min;
      const [maxX, maxY] = layout.bounds.max;
      const pad = 16;
      const s = Math.min((w - 2 * pad) / (maxY - minY), (h - 2 * pad) / (maxX - minX));
      const ox = (w - (maxY - minY) * s) / 2;
      const oy = (h - (maxX - minX) * s) / 2;

      transformRef.current = { s, ox, oy, minX, minY, maxX, maxY };

      const P = (p: [number, number]): [number, number] => [
        ox + (p[1] - minY) * s,
        oy + (maxX - p[0]) * s,
      ];

      const poly = (pts: [number, number][]) => {
        g.beginPath();
        pts.forEach((p, i) => {
          const q = P(p);
          if (i) g.lineTo(q[0], q[1]);
          else g.moveTo(q[0], q[1]);
        });
        g.closePath();
      };

      const rectPts = ([a, b]: [[number, number], [number, number]]): [number, number][] => [
        [a[0], a[1]],
        [b[0], a[1]],
        [b[0], b[1]],
        [a[0], b[1]],
      ];

      const onLayer = (floor: number, building?: string) =>
        layerFilter === 'all' ||
        (layerFilter === 'ground' && floor === 0) ||
        (building && layerFilter === `floor:${building}:${floor}`);

      const showBuilding = (bid: string, floors: number) =>
        layerFilter === 'all' ||
        layerFilter === 'ground' ||
        layerFilter === 'cut' ||
        (layerFilter.startsWith('floor:') &&
          layerFilter.split(':')[1] === bid &&
          Number(layerFilter.split(':')[2]) < floors) ||
        layerFilter === `roof:${bid}`;

      // 1. Grid (10 m)
      g.lineWidth = 1;
      g.strokeStyle = '#242a36';
      for (let x = Math.ceil(minX / 1000) * 1000; x <= maxX; x += 1000) {
        const a = P([x, minY]);
        const b = P([x, maxY]);
        g.beginPath();
        g.moveTo(a[0], a[1]);
        g.lineTo(b[0], b[1]);
        g.stroke();
      }
      for (let y = Math.ceil(minY / 1000) * 1000; y <= maxY; y += 1000) {
        const a = P([minX, y]);
        const b = P([maxX, y]);
        g.beginPath();
        g.moveTo(a[0], a[1]);
        g.lineTo(b[0], b[1]);
        g.stroke();
      }

      // 2. Zones
      for (const z of layout.zones) {
        const tint = SET_TINT[z.set_piece ?? ''] ?? '#808080';
        g.fillStyle = tint + '30';
        g.strokeStyle = tint + '90';
        g.lineWidth = 1;
        poly(z.poly);
        g.fill();
        g.stroke();
      }

      // 3. The Cut
      if (layout.cut && (layerFilter === 'all' || layerFilter === 'ground' || layerFilter === 'cut')) {
        g.fillStyle = '#1e2850';
        g.strokeStyle = '#465aa0';
        poly(layout.cut.poly);
        g.fill();
        g.stroke();
        for (const x of layout.cut.crossings) {
          g.fillStyle = '#c8c8be';
          poly(x.poly);
          g.fill();
        }
      }

      // 4. Buildings
      for (const b of layout.buildings) {
        if (!showBuilding(b.id, b.floors)) continue;
        const pts = rectPts(b.footprint);
        g.fillStyle =
          b.set_piece === 'chimney'
            ? '#46464e'
            : b.set_piece === 'tower'
            ? '#646e82'
            : b.height >= 600
            ? '#967864'
            : '#aa9678';
        g.strokeStyle = '#ffffff';
        g.lineWidth = 1.2;
        poly(pts);
        g.fill();
        g.stroke();

        if (b.roof_walkable) {
          g.strokeStyle = '#50dc78';
          g.lineWidth = 1.5;
          const q = P(b.footprint[0]);
          const r = P(b.footprint[1]);
          g.strokeRect(
            Math.min(q[0], r[0]) + 3,
            Math.min(q[1], r[1]) + 3,
            Math.abs(r[0] - q[0]) - 6,
            Math.abs(r[1] - q[1]) - 6
          );
        }
        for (const d of b.doors) {
          const q = P(d.pos);
          g.fillStyle = '#fff';
          g.beginPath();
          g.arc(q[0], q[1], 2.5, 0, Math.PI * 2);
          g.fill();
        }
        for (const hpos of b.roof_holes) {
          const q = P(hpos);
          g.strokeStyle = '#ff783c';
          g.lineWidth = 2;
          g.strokeRect(q[0] - 4, q[1] - 4, 8, 8);
        }
        for (const st of b.stairs) {
          const q = P(st.pos);
          g.fillStyle = '#50dc78';
          g.fillRect(q[0] - 3.5, q[1] - 3.5, 7, 7);
          const t = P(st.top);
          g.strokeStyle = '#50dc78';
          g.lineWidth = 1;
          g.beginPath();
          g.moveTo(q[0], q[1]);
          g.lineTo(t[0], t[1]);
          g.stroke();
        }
        const q = P(b.footprint[1]);
        g.fillStyle = '#e2e8f0';
        g.font = '9px ui-monospace, monospace';
        g.textAlign = 'left';
        g.fillText(
          b.roof_walkable ? `${b.id} +${b.floors}` : b.id,
          Math.min(P(b.footprint[0])[0], q[0]) + 3,
          Math.min(P(b.footprint[0])[1], q[1]) + 10
        );
      }

      // 5. Walls
      for (const wl of layout.walls) {
        if (!onLayer(wl.floor ?? 0, wl.building)) continue;
        const a = P(wl.a);
        const b = P(wl.b);
        g.strokeStyle = wl.kind === 'fence' ? '#e6e678' : wl.kind === 'cut_wall' ? '#465aa0' : '#c8c8d2';
        g.lineCap = 'butt';
        g.lineWidth = Math.max(1, wl.thickness * s);
        g.beginPath();
        g.moveTo(a[0], a[1]);
        g.lineTo(b[0], b[1]);
        g.stroke();
      }

      // 6. Covers
      if (layerFilter === 'all' || layerFilter === 'ground') {
        for (const cv of layout.covers) {
          const q = P(cv.pos);
          const rx = Math.max(1.5, (cv.size[0] / 2) * s);
          const ry = Math.max(1.5, (cv.size[1] / 2) * s);
          g.save();
          g.translate(q[0], q[1]);
          g.rotate((-cv.yaw * Math.PI) / 180);
          g.fillStyle = cv.size[2] >= 140 ? '#8c7c62' : '#6e645a';
          g.strokeStyle = '#c8bea0';
          g.lineWidth = 0.8;
          if (cv.kind === 'silo') {
            g.beginPath();
            g.arc(0, 0, rx, 0, Math.PI * 2);
            g.fill();
            g.stroke();
          } else {
            g.fillRect(-ry, -rx, ry * 2, rx * 2);
            g.strokeRect(-ry, -rx, ry * 2, rx * 2);
          }
          g.restore();
        }
      }

      // 7. Ladders
      for (const l of layout.ladders) {
        const q = P(l.pos);
        g.fillStyle = '#ffe600';
        g.strokeStyle = '#000';
        g.lineWidth = 1;
        g.fillRect(q[0] - 3.5, q[1] - 6, 7, 12);
        g.strokeRect(q[0] - 3.5, q[1] - 6, 7, 12);
      }

      // 8. Routes
      for (const r of layout.routes) {
        const main = !!r.mandatory;
        g.strokeStyle = main ? '#008cff' : '#5ac8ff';
        g.lineWidth = main ? 3 : 1.2;
        g.setLineDash(main ? [] : [5, 4]);
        g.beginPath();
        r.points.forEach((p, i) => {
          const q = P(p);
          if (i) g.lineTo(q[0], q[1]);
          else g.moveTo(q[0], q[1]);
        });
        g.stroke();
        g.setLineDash([]);
        if (main) {
          r.points.forEach((p, i) => {
            const q = P(p);
            g.fillStyle = '#008cff';
            g.beginPath();
            g.arc(q[0], q[1], 2.5, 0, Math.PI * 2);
            g.fill();
            g.fillStyle = '#93c5fd';
            g.font = '8px ui-monospace, monospace';
            g.fillText(String(i), q[0] + 4, q[1] - 3);
          });
        }
      }

      // 9. Fixed Points (Spawn and End)
      const sq = P(layout.spawn.pos);
      g.strokeStyle = '#00e65a';
      g.lineWidth = 2.5;
      g.beginPath();
      g.arc(sq[0], sq[1], 8, 0, Math.PI * 2);
      g.stroke();

      const eq = P(layout.end);
      g.strokeStyle = '#ff5050';
      g.lineWidth = 2.5;
      g.strokeRect(eq[0] - 7, eq[1] - 7, 14, 14);

      // 10. Exclusions Overlay (Optional toggle)
      if (showExclusions) {
        // Spawn keepout (e.g. 500 cm)
        g.strokeStyle = '#f97316';
        g.lineWidth = 1.2;
        g.setLineDash([4, 4]);
        g.beginPath();
        g.arc(sq[0], sq[1], 500 * s, 0, Math.PI * 2);
        g.stroke();

        // Ladder keepouts (100 cm)
        for (const l of layout.ladders) {
          const lq = P(l.pos);
          g.strokeStyle = '#eab308';
          g.beginPath();
          g.arc(lq[0], lq[1], 100 * s, 0, Math.PI * 2);
          g.stroke();
        }

        // Door keepouts (150 cm)
        for (const b of layout.buildings) {
          for (const d of b.doors) {
            const dq = P(d.pos);
            g.strokeStyle = '#fbbf24';
            g.beginPath();
            g.arc(dq[0], dq[1], 150 * s, 0, Math.PI * 2);
            g.stroke();
          }
        }
        g.setLineDash([]);
      }

      // 11. Overrides from Props File (Deleted objects marker)
      if (propsFile?.overrides) {
        for (const op of propsFile.overrides) {
          if (op.op === 'delete' && op.pos) {
            const dq = P(op.pos);
            g.strokeStyle = '#9ca3af';
            g.lineWidth = 2;
            g.beginPath();
            g.moveTo(dq[0] - 4, dq[1] - 4);
            g.lineTo(dq[0] + 4, dq[1] + 4);
            g.moveTo(dq[0] + 4, dq[1] - 4);
            g.lineTo(dq[0] - 4, dq[1] + 4);
            g.stroke();
          }
        }
      }

      // 12. Props Overlay
      if (propsFile?.layers) {
        for (const l of propsFile.layers) {
          if (visibleLayers[l.id] === false) continue;

          for (const o of l.objects) {
            if (layerFilter !== 'all' && o.walk_layer && o.walk_layer !== layerFilter) {
              continue;
            }

            const q = P(o.pos);
            const isSelected = selectedPropId === o.id;
            const isLocked = Boolean(o.locked || l.locked);

            // Check if object is from override
            const isHandAdded = o.origin === 'hand';

            if (l.kind === 'crates') {
              const isLarge = o.variant?.includes('_l') || o.variant?.includes('large');
              const isExplosive = o.variant?.includes('explosive') || o.variant?.includes('mined');
              const halfX = Math.max(3.5, (isLarge ? 60 : 40) * s);
              const halfY = Math.max(3.5, 40 * s);

              g.save();
              g.translate(q[0], q[1]);
              g.rotate(-((o.yaw ?? 0) * Math.PI) / 180);

              // Fill
              g.fillStyle = isExplosive ? '#ef4444' : '#0284c7';
              g.fillRect(-halfY, -halfX, halfY * 2, halfX * 2);

              // Border
              if (isSelected) {
                g.strokeStyle = '#ffffff';
                g.lineWidth = 2.5;
              } else if (isLocked) {
                g.strokeStyle = '#fbbf24';
                g.lineWidth = 2;
              } else if (isHandAdded) {
                g.strokeStyle = '#22c55e';
                g.lineWidth = 1.8;
              } else {
                g.strokeStyle = isExplosive ? '#fca5a5' : '#7dd3fc';
                g.lineWidth = 1.2;
              }
              g.strokeRect(-halfY, -halfX, halfY * 2, halfX * 2);

              // Internal cross lines for crate texture
              g.strokeStyle = isExplosive ? '#b91c1c' : '#0369a1';
              g.lineWidth = 0.8;
              g.beginPath();
              g.moveTo(-halfY, -halfX);
              g.lineTo(halfY, halfX);
              g.moveTo(halfY, -halfX);
              g.lineTo(-halfY, halfX);
              g.stroke();

              g.restore();
            } else if (l.kind === 'barrels') {
              const isExplosive = o.variant?.includes('explosive') ?? true;
              const rad = Math.max(3.5, 30 * s);

              g.beginPath();
              g.arc(q[0], q[1], rad, 0, Math.PI * 2);
              g.fillStyle = '#ef4444';
              g.fill();

              if (isSelected) {
                g.strokeStyle = '#ffffff';
                g.lineWidth = 2.5;
              } else if (isLocked) {
                g.strokeStyle = '#fbbf24';
                g.lineWidth = 2;
              } else if (isHandAdded) {
                g.strokeStyle = '#22c55e';
                g.lineWidth = 1.8;
              } else {
                g.strokeStyle = '#fca5a5';
                g.lineWidth = 1.2;
              }
              g.stroke();

              // Inner yellow danger dot for explosive barrels
              if (isExplosive) {
                g.beginPath();
                g.arc(q[0], q[1], Math.max(1.5, rad * 0.45), 0, Math.PI * 2);
                g.fillStyle = '#fbbf24';
                g.fill();
              }
            } else if (l.kind === 'turrets') {
              const rad = Math.max(4.5, 45 * s);
              g.save();
              g.translate(q[0], q[1]);
              g.rotate(-((o.yaw ?? 0) * Math.PI) / 180);

              // Turret diamond base
              g.beginPath();
              g.moveTo(0, -rad);
              g.lineTo(rad * 0.8, 0);
              g.lineTo(0, rad * 0.8);
              g.lineTo(-rad * 0.8, 0);
              g.closePath();
              g.fillStyle = '#a855f7';
              g.fill();

              // Gun barrel line
              g.strokeStyle = '#f3e8ff';
              g.lineWidth = 2;
              g.beginPath();
              g.moveTo(0, 0);
              g.lineTo(0, -rad * 1.5);
              g.stroke();

              if (isSelected) {
                g.strokeStyle = '#ffffff';
                g.lineWidth = 2;
                g.stroke();
              } else if (isLocked) {
                g.strokeStyle = '#fbbf24';
                g.lineWidth = 2;
                g.stroke();
              }
              g.restore();
            } else if (l.kind === 'mines') {
              const rad = Math.max(3, 25 * s);
              g.beginPath();
              g.arc(q[0], q[1], rad, 0, Math.PI * 2);
              g.fillStyle = '#eab308';
              g.fill();
              g.strokeStyle = isSelected ? '#ffffff' : isLocked ? '#fbbf24' : '#713f12';
              g.lineWidth = isSelected || isLocked ? 2 : 1;
              g.stroke();

              // Crosshair
              g.strokeStyle = '#000000';
              g.lineWidth = 0.8;
              g.beginPath();
              g.moveTo(q[0] - rad, q[1]);
              g.lineTo(q[0] + rad, q[1]);
              g.moveTo(q[0], q[1] - rad);
              g.lineTo(q[0], q[1] + rad);
              g.stroke();
            } else if (l.kind === 'loot') {
              const rad = Math.max(3.5, 30 * s);
              g.beginPath();
              g.arc(q[0], q[1], rad, 0, Math.PI * 2);
              g.fillStyle = '#10b981';
              g.fill();
              g.strokeStyle = isSelected ? '#ffffff' : isLocked ? '#fbbf24' : '#064e3b';
              g.lineWidth = isSelected || isLocked ? 2 : 1;
              g.stroke();
            }

            // Lock badge / dot
            if (isLocked) {
              g.fillStyle = '#fbbf24';
              g.beginPath();
              g.arc(q[0] + 5, q[1] - 5, 2.5, 0, Math.PI * 2);
              g.fill();
            }

            // Selection ring
            if (isSelected) {
              g.strokeStyle = '#38bdf8';
              g.lineWidth = 1.5;
              g.setLineDash([2, 2]);
              g.beginPath();
              g.arc(q[0], q[1], 12, 0, Math.PI * 2);
              g.stroke();
              g.setLineDash([]);
            }
          }
        }
      }

      // 13. Legend overlay in top-left
      g.fillStyle = '#0f172a';
      g.strokeStyle = '#334155';
      g.lineWidth = 1;
      g.beginPath();
      g.roundRect(8, 8, 140, 72, 6);
      g.fill();
      g.stroke();

      g.font = '10px ui-sans-serif, system-ui';
      g.textAlign = 'left';

      // Crates legend
      g.fillStyle = '#0284c7';
      g.fillRect(14, 15, 8, 8);
      g.strokeStyle = '#7dd3fc';
      g.strokeRect(14, 15, 8, 8);
      g.fillStyle = '#cbd5e1';
      g.fillText('Ящики', 28, 23);

      // Barrels legend
      g.fillStyle = '#ef4444';
      g.beginPath();
      g.arc(18, 35, 4, 0, Math.PI * 2);
      g.fill();
      g.fillStyle = '#fbbf24';
      g.beginPath();
      g.arc(18, 35, 1.8, 0, Math.PI * 2);
      g.fill();
      g.fillStyle = '#cbd5e1';
      g.fillText('Бочки (взрыв)', 28, 38);

      // Locked legend
      g.strokeStyle = '#fbbf24';
      g.lineWidth = 1.8;
      g.strokeRect(14, 46, 8, 8);
      g.fillStyle = '#cbd5e1';
      g.fillText('Заблокировано', 28, 54);

      // Bottom info bar
      g.fillStyle = '#fbbf24';
      g.font = '11px ui-monospace, monospace';
      g.textAlign = 'left';
      const mapTitle = propsFile?.map ? `Map: ${propsFile.map}` : layout.name;
      const revTitle = propsFile?.revision !== undefined ? `rev ${propsFile.revision}` : '';
      g.fillText(
        `${mapTitle} (${Math.round((maxY - minY) / 100)} × ${Math.round((maxX - minX) / 100)} м)  ${revTitle}  слой: ${layerFilter}`,
        8,
        h - 8
      );
    }, [layout, propsFile, layerFilter, visibleLayers, selectedPropId, showExclusions, height]);

    return (
      <canvas
        ref={canvas}
        onClick={handleCanvasClick}
        style={{ width: '100%', height, cursor: 'crosshair' }}
        className="rounded-xl border border-slate-800 block select-none"
      />
    );
  }
);

PropsCanvas.displayName = 'PropsCanvas';
