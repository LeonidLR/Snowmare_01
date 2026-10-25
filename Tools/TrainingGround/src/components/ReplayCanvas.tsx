import React, { useRef, useEffect } from 'react';
import { SpatialTelemetryRun, TelemetryFrame } from '../types/spatialTelemetry';

interface ReplayCanvasProps {
  telemetry: SpatialTelemetryRun;
  currentTime: number;
  showCoversOverlay: boolean;
  showChokeOverlay: boolean;
  showElevationOverlay: boolean;
  showEventsOverlay: boolean;
  canvasRef?: React.RefObject<HTMLCanvasElement>;
}

export function getEnemyStyle(type?: string, id?: string): { fill: string; stroke: string; label: string; radius: number; maxHp: number } {
  const t = (type || '').toLowerCase();
  const name = (id || '').toLowerCase();

  // 1. Гончая (белая)
  if (t.includes('hound') || name.includes('hound')) {
    return { fill: '#ffffff', stroke: '#94a3b8', label: 'Гончая', radius: 5.5, maxHp: 40 };
  }
  // 2. Механическая гончая (синяя)
  if (t.includes('cutter') || t.includes('mech') || name.includes('cutter')) {
    return { fill: '#3b82f6', stroke: '#1d4ed8', label: 'Мех-гончая', radius: 6.0, maxHp: 60 };
  }
  // 3. Брут (зелёный)
  if (t.includes('brute') || name.includes('brute')) {
    return { fill: '#22c55e', stroke: '#15803d', label: 'Брут', radius: 9.0, maxHp: 200 };
  }
  // 4. Промёрзший (бирюзовый)
  if (t.includes('frost') || t.includes('walker') || name.includes('frost') || name.includes('walker')) {
    return { fill: '#06b6d4', stroke: '#0891b2', label: 'Промёрзший', radius: 6.0, maxHp: 80 };
  }

  // По умолчанию: Промёрзший (бирюзовый)
  return { fill: '#06b6d4', stroke: '#0891b2', label: 'Промёрзший', radius: 6.0, maxHp: 80 };
}

export const ReplayCanvas: React.FC<ReplayCanvasProps> = ({
  telemetry,
  currentTime,
  showCoversOverlay,
  showChokeOverlay,
  showElevationOverlay,
  showEventsOverlay,
  canvasRef: externalRef
}) => {
  const internalRef = useRef<HTMLCanvasElement>(null);
  const canvasRef = externalRef || internalRef;

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const width = canvas.width;
    const height = canvas.height;

    // Очистка фона
    ctx.fillStyle = '#0f172a'; // slate-900
    ctx.fillRect(0, 0, width, height);

    const bounds = telemetry.level_layout.bounds || { min_x: -25, max_x: 25, min_z: -35, max_z: 20 };
    const pad = 40;
    const worldW = (bounds.max_x - bounds.min_x) || 50;
    const worldH = (bounds.max_z - bounds.min_z) || 55;

    const scaleX = (width - pad * 2) / worldW;
    const scaleZ = (height - pad * 2) / worldH;
    const scale = Math.min(scaleX, scaleZ);

    const offsetX = width / 2 - ((bounds.min_x + bounds.max_x) / 2) * scale;
    const offsetZ = height / 2 - ((bounds.min_z + bounds.max_z) / 2) * scale;

    const toScreen = (x: number, z: number): [number, number] => {
      return [
        offsetX + x * scale,
        offsetZ + z * scale
      ];
    };

    // 1. Сетка координат (метры)
    ctx.strokeStyle = '#1e293b'; // slate-800
    ctx.lineWidth = 1;
    ctx.fillStyle = '#475569';
    ctx.font = '10px monospace';

    const minXGrid = Math.floor(bounds.min_x / 5) * 5;
    const maxXGrid = Math.ceil(bounds.max_x / 5) * 5;
    for (let gx = minXGrid; gx <= maxXGrid; gx += 5) {
      const [sx] = toScreen(gx, 0);
      ctx.beginPath();
      ctx.moveTo(sx, 0);
      ctx.lineTo(sx, height);
      ctx.stroke();
      ctx.fillText(`${gx}m`, sx + 2, height - 8);
    }

    const minZGrid = Math.floor(bounds.min_z / 5) * 5;
    const maxZGrid = Math.ceil(bounds.max_z / 5) * 5;
    for (let gz = minZGrid; gz <= maxZGrid; gz += 5) {
      const [, sy] = toScreen(0, gz);
      ctx.beginPath();
      ctx.moveTo(0, sy);
      ctx.lineTo(width, sy);
      ctx.stroke();
      ctx.fillText(`${gz}m`, 6, sy - 2);
    }

    // 1.5. Статические препятствия, здания, коллайдеры и бочки (Obstacles & Colliders)
    if (telemetry.level_layout.obstacles) {
      for (const obs of telemetry.level_layout.obstacles) {
        const [ox, oy] = toScreen(obs.position[0], obs.position[2]);
        const ow = (obs.size ? obs.size[0] : 4.0) * scale;
        const od = (obs.size ? obs.size[2] : 4.0) * scale;
        const rot = obs.rotation_y || 0;
        const lowerId = (obs.id || '').toLowerCase();
        const isCylinder = obs.shape === 'cylinder' || obs.type === 'barrel' || lowerId.includes('barrel');

        ctx.save();
        ctx.translate(ox, oy);
        ctx.rotate(rot);

        if (isCylinder) {
          // --- Отрисовка цилиндрического коллайдера (бочки, круглые колонны) ---
          const radius = Math.max(3, Math.max(ow, od) / 2);
          ctx.beginPath();
          ctx.arc(0, 0, radius, 0, Math.PI * 2);

          if (obs.type === 'barrel' || lowerId.includes('barrel')) {
            ctx.fillStyle = '#7c2d12'; // rust/amber
            ctx.fill();
            ctx.strokeStyle = '#ea580c'; // bright orange outline
            ctx.lineWidth = 1.5;
            ctx.stroke();

            // Внутреннее ребро бочки
            ctx.beginPath();
            ctx.arc(0, 0, Math.max(1.5, radius * 0.45), 0, Math.PI * 2);
            ctx.strokeStyle = 'rgba(251, 146, 60, 0.7)';
            ctx.lineWidth = 1;
            ctx.stroke();
          } else {
            ctx.fillStyle = '#1e293b';
            ctx.fill();
            ctx.strokeStyle = '#64748b';
            ctx.lineWidth = 2;
            ctx.stroke();
          }

          if (radius >= 10) {
            ctx.fillStyle = '#fed7aa';
            ctx.font = 'bold 8px monospace';
            ctx.fillText(obs.id, radius + 3, 3);
          }
        } else {
          // --- Отрисовка прямоугольного коллайдера (стены, здания, контейнеры, техника) ---
          let fillColor = '#1e293b';
          let strokeColor = '#64748b';
          let labelColor = '#cbd5e1';
          let icon = '🏢';

          if (obs.type === 'vehicle' || lowerId.includes('vehicle') || lowerId.includes('suv') || lowerId.includes('truck')) {
            fillColor = 'rgba(4, 47, 46, 0.9)'; // teal-950
            strokeColor = '#0d9488'; // teal-600
            labelColor = '#5eead4';
            icon = '🚙';
          } else if (obs.type === 'container' || lowerId.includes('container')) {
            fillColor = 'rgba(30, 27, 75, 0.9)'; // indigo-950
            strokeColor = '#6366f1'; // indigo-500
            labelColor = '#c7d2fe';
            icon = '📦';
          } else if (obs.type === 'barrier' || lowerId.includes('barrier') || lowerId.includes('fence') || lowerId.includes('perimeter')) {
            fillColor = 'rgba(51, 65, 85, 0.85)'; // slate-700
            strokeColor = '#94a3b8'; // slate-400
            labelColor = '#f1f5f9';
            icon = '🚧';
          } else if (obs.type === 'building' || lowerId.includes('wall') || lowerId.includes('bunker') || lowerId.includes('gate') || lowerId.includes('lintel')) {
            fillColor = '#1e293b';
            strokeColor = '#64748b';
            labelColor = '#e2e8f0';
            icon = '🏢';
          } else {
            fillColor = '#1e293b';
            strokeColor = '#475569';
            labelColor = '#94a3b8';
            icon = '⬛';
          }

          ctx.fillStyle = fillColor;
          ctx.fillRect(-ow / 2, -od / 2, ow, od);

          // Внутренняя диагональная штриховка для крупных построек и контейнеров
          if (Math.min(ow, od) >= 12 && Math.max(ow, od) >= 18) {
            ctx.strokeStyle = 'rgba(148, 163, 184, 0.2)';
            ctx.lineWidth = 1;
            const step = 8;
            for (let x = -ow / 2; x < ow / 2; x += step) {
              ctx.beginPath();
              ctx.moveTo(x, -od / 2);
              ctx.lineTo(Math.min(ow / 2, x + od), od / 2);
              ctx.stroke();
            }
          }

          // Внешний контур
          ctx.strokeStyle = strokeColor;
          ctx.lineWidth = 2;
          ctx.strokeRect(-ow / 2, -od / 2, ow, od);

          // Подпись объекта и точных габаритов коллайдера
          if (Math.max(ow, od) >= 22 && Math.min(ow, od) >= 8) {
            ctx.fillStyle = labelColor;
            ctx.font = 'bold 9px sans-serif';
            ctx.fillText(`${icon} ${obs.id}`, -ow / 2 + 4, Math.min(od / 2 - 4, -2));
            if (ow >= 28 && od >= 16 && obs.size) {
              ctx.fillStyle = '#94a3b8';
              ctx.font = '8px monospace';
              ctx.fillText(`${obs.size[0].toFixed(1)}x${obs.size[2].toFixed(1)}м`, -ow / 2 + 4, 9);
            }
          }
        }

        ctx.restore();
      }
    }

    // 2. Возвышенности и платформы (Overlay #3: Высоты)
    if (showElevationOverlay && telemetry.level_layout.elevations) {
      for (const el of telemetry.level_layout.elevations) {
        if (el.polygon && el.polygon.length >= 3) {
          ctx.beginPath();
          const [startSx, startSy] = toScreen(el.polygon[0][0], el.polygon[0][1]);
          ctx.moveTo(startSx, startSy);
          for (let i = 1; i < el.polygon.length; ++i) {
            const [psx, psy] = toScreen(el.polygon[i][0], el.polygon[i][1]);
            ctx.lineTo(psx, psy);
          }
          ctx.closePath();

          ctx.fillStyle = 'rgba(56, 189, 248, 0.12)'; // sky-400 tint
          ctx.fill();
          ctx.strokeStyle = '#0284c7';
          ctx.lineWidth = 1.5;
          ctx.setLineDash([4, 3]);
          ctx.stroke();
          ctx.setLineDash([]);

          ctx.fillStyle = '#38bdf8';
          ctx.font = 'bold 11px sans-serif';
          ctx.fillText(`▲ +${el.height.toFixed(1)}м`, startSx + 5, startSy + 14);
        }
      }
    }

    // 3. Зоны скопления, узкие места и застрявшие враги (Overlay #2: Choke Points)
    if (showChokeOverlay && telemetry.summary.choke_points_detected) {
      for (const cp of telemetry.summary.choke_points_detected) {
        const [cx, cy] = toScreen(cp.position[0], cp.position[1]);
        const isSingle = cp.enemy_count_peak === 1 || cp.stuck_type === 'SINGLE';
        const r = Math.max(16, (isSingle ? 18 : 24) * (scale / 12));

        const grad = ctx.createRadialGradient(cx, cy, 2, cx, cy, r);
        if (isSingle) {
          grad.addColorStop(0, 'rgba(236, 72, 153, 0.55)'); // pink-500 для одиночного застрявшего врага
          grad.addColorStop(1, 'rgba(236, 72, 153, 0.0)');
          ctx.strokeStyle = '#ec4899';
        } else {
          grad.addColorStop(0, 'rgba(168, 85, 247, 0.5)'); // purple-500 для массового затора
          grad.addColorStop(1, 'rgba(168, 85, 247, 0.0)');
          ctx.strokeStyle = '#a855f7';
        }
        ctx.fillStyle = grad;
        ctx.beginPath();
        ctx.arc(cx, cy, r, 0, Math.PI * 2);
        ctx.fill();

        ctx.lineWidth = 1.5;
        ctx.stroke();

        // Центральная метка затора
        ctx.beginPath();
        ctx.arc(cx, cy, 3.5, 0, Math.PI * 2);
        ctx.fillStyle = isSingle ? '#f472b6' : '#c084fc';
        ctx.fill();

        ctx.fillStyle = isSingle ? '#fbcfe8' : '#e9d5ff';
        ctx.font = 'bold 10px sans-serif';
        const durText = cp.duration_sec ? ` (${cp.duration_sec.toFixed(1)}с)` : '';
        const label = isSingle
          ? `⚠️ Застрял [1 вр.]${durText}`
          : `⚠️ Затор [${cp.enemy_count_peak} вр.]${durText}`;
        ctx.fillText(label, cx - 38, cy - r - 4);
      }
    }

    // 4. Точки спавна врагов и база
    if (telemetry.level_layout.spawn_points) {
      for (const sp of telemetry.level_layout.spawn_points) {
        const [sx, sy] = toScreen(sp.position[0], sp.position[2]);
        ctx.fillStyle = 'rgba(244, 63, 94, 0.2)';
        ctx.beginPath();
        ctx.arc(sx, sy, 14, 0, Math.PI * 2);
        ctx.fill();

        ctx.strokeStyle = '#f43f5e';
        ctx.lineWidth = 1.5;
        ctx.stroke();

        ctx.fillStyle = '#fda4af';
        ctx.font = '9px sans-serif';
        ctx.fillText(`SPAWN [${sp.lane || 'C'}]`, sx - 22, sy + 18);
      }
    }

    if (telemetry.level_layout.defend_points) {
      for (const dp of telemetry.level_layout.defend_points) {
        const [dx, dy] = toScreen(dp.position[0], dp.position[2]);
        ctx.strokeStyle = '#22c55e';
        ctx.lineWidth = 1.5;
        ctx.setLineDash([5, 4]);
        ctx.beginPath();
        ctx.arc(dx, dy, (dp.radius || 5) * scale, 0, Math.PI * 2);
        ctx.stroke();
        ctx.setLineDash([]);

        ctx.fillStyle = '#86efac';
        ctx.font = '10px sans-serif';
        ctx.fillText('Рубеж обороны', dx - 35, dy);
      }
    }

    // 5. Укрытия и баррикады (Overlay #1: Плотность и эффективность укрытий)
    if (telemetry.level_layout.covers) {
      const coverMetrics = telemetry.summary.cover_metrics || {};

      for (const cov of telemetry.level_layout.covers) {
        const [cx, cy] = toScreen(cov.position[0], cov.position[2]);
        const w = (cov.size ? cov.size[0] : 2.5) * scale;
        const h = (cov.size ? cov.size[2] : 0.8) * scale;
        const rot = cov.rotation_y || 0;

        ctx.save();
        ctx.translate(cx, cy);
        ctx.rotate(rot);

        const summary = coverMetrics[cov.id] || cov.usage_summary;
        let fillColor = '#334155'; // default slate-700
        let strokeColor = '#64748b';
        let statusTag = '';

        if (showCoversOverlay) {
          if (!summary || summary.time_sec <= 0.05) {
            // МЕРТВАЯ ЗОНА: укрытие ни разу не использовалось ботами!
            fillColor = 'rgba(71, 85, 105, 0.4)';
            strokeColor = '#94a3b8';
            statusTag = '⚪ Забыто (0с)';
          } else if (summary.destroyed) {
            fillColor = 'rgba(239, 68, 68, 0.8)';
            strokeColor = '#b91c1c';
            statusTag = '💥 Разрушено';
          } else if (summary.time_sec >= 3.0) {
            // ВЫСОКАЯ ЭФФЕКТИВНОСТЬ
            fillColor = 'rgba(34, 197, 94, 0.8)';
            strokeColor = '#15803d';
            statusTag = `🟢 ${summary.time_sec.toFixed(1)}с`;
          } else {
            // УМЕРЕННОЕ ИСПОЛЬЗОВАНИЕ
            fillColor = 'rgba(234, 179, 8, 0.8)';
            strokeColor = '#a16207';
            statusTag = `🟡 ${summary.time_sec.toFixed(1)}с`;
          }
        }

        ctx.fillStyle = fillColor;
        ctx.fillRect(-w / 2, -h / 2, w, h);
        ctx.strokeStyle = strokeColor;
        ctx.lineWidth = 2;
        ctx.strokeRect(-w / 2, -h / 2, w, h);

        ctx.restore();

        if (showCoversOverlay && statusTag) {
          ctx.fillStyle = '#f8fafc';
          ctx.font = 'bold 9px sans-serif';
          ctx.fillText(statusTag, cx - 18, cy - h / 2 - 3);
        }
      }
    }

    // 6. Динамический кадр: поиск ближайшего кадра по времени
    const frames = telemetry.frames || [];
    let currentFrame: TelemetryFrame | null = null;
    if (frames.length > 0) {
      // Ищем ближайший кадр
      let minDiff = 9999;
      for (const f of frames) {
        const diff = Math.abs(f.t - currentTime);
        if (diff < minDiff) {
          minDiff = diff;
          currentFrame = f;
        }
      }
    }

    if (currentFrame) {
      // А. Враги (Дифференциация по типам: Гончая = Белая, Мех-гончая = Синяя, Промёрзший = Бирюзовый, Брут = Зелёный)
      for (const en of currentFrame.enemies) {
        const [ex, ey] = toScreen(en.x, en.z);
        const style = getEnemyStyle(en.type, en.id);

        // Тело врага
        ctx.fillStyle = style.fill;
        ctx.beginPath();
        ctx.arc(ex, ey, style.radius, 0, Math.PI * 2);
        ctx.fill();
        ctx.strokeStyle = style.stroke;
        ctx.lineWidth = 1.8;
        ctx.stroke();

        // Вектор скорости перемещения
        if (en.vx || en.vz) {
          ctx.strokeStyle = style.fill;
          ctx.lineWidth = 1.5;
          ctx.beginPath();
          ctx.moveTo(ex, ey);
          ctx.lineTo(ex + en.vx * (scale * 0.4), ey + en.vz * (scale * 0.4));
          ctx.stroke();
        }

        // Полоса здоровья врага
        const hpBarW = 16;
        const hpPct = Math.max(0, Math.min(1.0, en.hp / (style.maxHp || 80)));
        ctx.fillStyle = '#0f172a';
        ctx.fillRect(ex - hpBarW / 2, ey - style.radius - 6, hpBarW, 3);
        ctx.fillStyle = style.fill === '#ffffff' ? '#e2e8f0' : style.fill;
        ctx.fillRect(ex - hpBarW / 2, ey - style.radius - 6, hpBarW * hpPct, 3);
      }

      // Б. Бойцы отряда
      for (const sm of currentFrame.squad) {
        const [px, py] = toScreen(sm.x, sm.z);

        // Индикатор укрытия (аура)
        if (sm.in_cover) {
          ctx.fillStyle = 'rgba(52, 211, 153, 0.3)';
          ctx.beginPath();
          ctx.arc(px, py, 14, 0, Math.PI * 2);
          ctx.fill();
          ctx.strokeStyle = '#10b981';
          ctx.lineWidth = 1.5;
          ctx.stroke();
        }

        // Тело бойца (голубой круг)
        ctx.fillStyle = '#38bdf8';
        ctx.beginPath();
        ctx.arc(px, py, 8, 0, Math.PI * 2);
        ctx.fill();
        ctx.strokeStyle = '#0369a1';
        ctx.lineWidth = 2;
        ctx.stroke();

        // Полоса здоровья бойца
        const hpBarW = 24;
        const hpPct = Math.max(0, Math.min(1.0, sm.hp / 100));
        ctx.fillStyle = '#082f49';
        ctx.fillRect(px - hpBarW / 2, py - 14, hpBarW, 4);
        ctx.fillStyle = sm.hp > 35 ? '#38bdf8' : '#f87171';
        ctx.fillRect(px - hpBarW / 2, py - 14, hpBarW * hpPct, 4);

        // Стойка и имя
        const stanceStr = sm.stance === 1 ? ' [СИДЯ]' : (sm.stance === 2 ? ' [ЛЕЖА]' : '');
        ctx.fillStyle = '#e0f2fe';
        ctx.font = 'bold 9px sans-serif';
        ctx.fillText(`${sm.id}${stanceStr}`, px - 20, py + 16);
      }
    }

    // 7. Боевые события (Overlay #4: Взрывы, выстрелы, гибель)
    if (showEventsOverlay && telemetry.events) {
      for (const ev of telemetry.events) {
        const dt = currentTime - ev.t;
        if (dt >= 0 && dt <= 0.6) { // Событие активно 0.6 сек
          const [evX, evY] = ev.position ? toScreen(ev.position[0], ev.position[2]) : [0, 0];
          const fade = 1.0 - (dt / 0.6);

          if (ev.type === 'GRENADE_THROWN' || ev.type === 'EXPLOSION') {
            const blastRadius = 28 * (scale / 10) * (1.0 + (1.0 - fade) * 0.5);
            ctx.fillStyle = `rgba(251, 146, 60, ${fade * 0.5})`;
            ctx.beginPath();
            ctx.arc(evX, evY, blastRadius, 0, Math.PI * 2);
            ctx.fill();

            ctx.strokeStyle = `rgba(234, 88, 12, ${fade})`;
            ctx.lineWidth = 2.5;
            ctx.stroke();

            ctx.fillStyle = '#fed7aa';
            ctx.font = 'bold 11px sans-serif';
            ctx.fillText('💥 ВЗРЫВ', evX - 22, evY - blastRadius - 4);
          } else if (ev.type === 'ENEMY_DEATH') {
            ctx.fillStyle = `rgba(239, 68, 68, ${fade})`;
            ctx.font = 'bold 13px sans-serif';
            ctx.fillText('✖ ВРАГ', evX - 12, evY - 6);
          } else if (ev.type === 'SQUAD_DEATH') {
            ctx.fillStyle = `rgba(244, 63, 94, ${fade})`;
            ctx.font = 'bold 14px sans-serif';
            ctx.fillText('☠ ПАЛ', evX - 12, evY - 6);
          }
        }
      }
    }

  }, [telemetry, currentTime, showCoversOverlay, showChokeOverlay, showElevationOverlay, showEventsOverlay]);

  return (
    <div className="relative border border-slate-800 rounded-lg overflow-hidden shadow-2xl bg-slate-950 flex items-center justify-center">
      <canvas
        ref={canvasRef}
        width={740}
        height={540}
        className="w-full h-auto max-h-[640px] object-contain block"
      />
    </div>
  );
};
