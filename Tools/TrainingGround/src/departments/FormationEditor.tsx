// Training Ground: interactive Formation Editor (Movement tab).
// Docs: docs/port/gemini/inbox/008-formation-editor.md.
//
// Allows game designers to customize formation member positions and look sectors:
// - 10 cm grid snap, leader (K) locked at (0, 0), heading arrow pointing forward (0 deg, 12 o'clock).
// - Draggable member discs (right/fwd in cm).
// - Draggable look direction arrows (-180..180 deg).
// - Draggable arc edges (sector width).
// - "Зеркало" button (reflects shape and look angles, swaps left/right roles).
// - "Сброс к дефолту" button (removes override, restores rules default).
// - Live 360-degree coverage ring (blind zones, duplicated looks, spacing and radius checks).
// - Save button writes Content/Data/FormationLab/formation_shapes.json via dev-server API.

import React, { useState, useEffect, useMemo, useRef, useCallback } from 'react';
import {
  Save,
  RotateCcw,
  FlipHorizontal,
  AlertTriangle,
  CheckCircle2,
  Shield,
  ZoomIn,
  ZoomOut,
} from 'lucide-react';
import type { FormationConfig } from '../movement/config';
import {
  type FormationShapesFile,
  type FormationShape,
  type FormationMemberShape,
  type EditableFormation,
  EDITABLE_FORMATIONS,
  FORMATION_SHAPES_SCHEMA,
  DEFAULT_ARC_DEG,
} from '../movement/shapesSchema';
import {
  validateLiveShape,
  mirrorFormationShape,
  computeDefaultFormationShape,
  resetFormationShape,
  MIN_MEMBER_SPACING_CM,
} from '../movement/shapesCoverage';
import { CUSTOM_MAX_RADIUS } from '../movement/custom';
import { normYaw } from '../movement/geometry';

export interface FormationEditorProps {
  config: FormationConfig;
  savedShapes: FormationShapesFile | null;
  onShapesSaved: (newShapes: FormationShapesFile) => void;
  initialFormation?: EditableFormation;
  initialMemberCount?: number;
}

const FORMATION_NAMES_RU: Record<EditableFormation, string> = {
  diamond: 'Ромб',
  wedge: 'Клин',
  line: 'Цепь',
  echelon_left: 'Эшелон влево',
  echelon_right: 'Эшелон вправо',
  column: 'Колонна-ёлочка',
  pair: 'Пара',
};

const ROLE_LETTERS_RU: Record<string, string> = {
  commander: 'К',
  right: 'П',
  left: 'Л',
  rear: 'З',
};

const ROLE_NAMES_RU: Record<string, string> = {
  commander: 'Командир',
  right: 'Правый',
  left: 'Левый',
  rear: 'Замыкающий',
};

const ROLE_COLORS: Record<string, { fill: string; stroke: string; sector: string }> = {
  commander: { fill: '#fbbf24', stroke: '#f59e0b', sector: 'rgba(251, 191, 36, 0.25)' }, // amber
  right: { fill: '#38bdf8', stroke: '#0284c7', sector: 'rgba(56, 189, 248, 0.25)' },     // sky
  left: { fill: '#a855f7', stroke: '#7e22ce', sector: 'rgba(168, 85, 247, 0.25)' },     // purple
  rear: { fill: '#34d399', stroke: '#059669', sector: 'rgba(52, 211, 153, 0.25)' },     // emerald
};

type DragKind = 'pos' | 'look' | 'arc';

interface DragState {
  memberIndex: number;
  kind: DragKind;
  startPos: { x: number; y: number };
}

export const FormationEditor: React.FC<FormationEditorProps> = ({
  config,
  savedShapes,
  onShapesSaved,
  initialFormation = 'diamond',
  initialMemberCount = 4,
}) => {
  const [formation, setFormation] = useState<EditableFormation>(initialFormation);
  const [memberCount, setMemberCount] = useState<number>(initialMemberCount);
  const [zoom, setZoom] = useState<number>(0.6); // px per cm
  const [shapesDoc, setShapesDoc] = useState<FormationShapesFile>(
    savedShapes ?? { schema: FORMATION_SHAPES_SCHEMA, formations: {}, custom: {} }
  );

  // Active editable members for the selected formation & member count
  const [members, setMembers] = useState<FormationMemberShape[]>([]);
  const [dragState, setDragState] = useState<DragState | null>(null);
  const [saveStatus, setSaveStatus] = useState<string>('');
  const [saving, setSaving] = useState<boolean>(false);

  const svgRef = useRef<SVGSVGElement | null>(null);

  // Keep local shapesDoc in sync if parent passes updated savedShapes
  useEffect(() => {
    if (savedShapes) {
      setShapesDoc(savedShapes);
    }
  }, [savedShapes]);

  // Load shape when formation or count changes
  useEffect(() => {
    const countKey = String(memberCount);
    const existing = shapesDoc.formations?.[formation]?.[countKey];
    if (existing && existing.members && existing.members.length > 0) {
      setMembers(JSON.parse(JSON.stringify(existing.members)));
    } else {
      const def = computeDefaultFormationShape(formation, memberCount, config);
      setMembers(def.members);
    }
    setSaveStatus('');
  }, [formation, memberCount, shapesDoc, config]);

  // Check if current shape has saved override in shapesDoc
  const isSavedInFile = useMemo(() => {
    const countKey = String(memberCount);
    return !!shapesDoc.formations?.[formation]?.[countKey];
  }, [shapesDoc, formation, memberCount]);

  // Live validation
  const validation = useMemo(() => {
    return validateLiveShape(members, MIN_MEMBER_SPACING_CM, CUSTOM_MAX_RADIUS);
  }, [members]);

  // SVG dimensions
  const viewWidth = 600;
  const viewHeight = 600;
  const originX = viewWidth / 2;
  const originY = viewHeight / 2 + 80; // slightly down so forward space is ample

  // Coordinate transforms:
  // Forward (+fwd) is UP in SVG (-Y)
  // Right (+right) is RIGHT in SVG (+X)
  const toSvgX = useCallback((rightCm: number) => originX + rightCm * zoom, [originX, zoom]);
  const toSvgY = useCallback((fwdCm: number) => originY - fwdCm * zoom, [originY, zoom]);
  const fromSvgX = useCallback((svgX: number) => Math.round((svgX - originX) / zoom), [originX, zoom]);
  const fromSvgY = useCallback((svgY: number) => Math.round((originY - svgY) / zoom), [originY, zoom]);

  // Grid snap (10 cm)
  const snap10 = (v: number) => Math.round(v / 10) * 10;

  // Handle pointer down on member position, look arrow, or arc edge
  const handlePointerDown = (index: number, kind: DragKind, e: React.PointerEvent) => {
    e.stopPropagation();
    (e.target as Element).setPointerCapture(e.pointerId);
    setDragState({
      memberIndex: index,
      kind,
      startPos: { x: e.clientX, y: e.clientY },
    });
  };

  const handlePointerMove = (e: React.PointerEvent) => {
    if (!dragState || !svgRef.current) return;
    const rect = svgRef.current.getBoundingClientRect();
    const cursorSvgX = e.clientX - rect.left;
    const cursorSvgY = e.clientY - rect.top;

    const cursorRight = fromSvgX(cursorSvgX);
    const cursorFwd = fromSvgY(cursorSvgY);

    setMembers((prev) => {
      const next = [...prev];
      const m = { ...next[dragState.memberIndex] };

      if (dragState.kind === 'pos') {
        // Leader (index 0 / commander) is locked at (0, 0)
        if (dragState.memberIndex === 0 || m.role === 'commander') {
          m.fwd = 0;
          m.right = 0;
        } else {
          m.fwd = snap10(cursorFwd);
          m.right = snap10(cursorRight);
        }
      } else if (dragState.kind === 'look') {
        const dRight = cursorRight - m.right;
        const dFwd = cursorFwd - m.fwd;
        if (Math.hypot(dRight, dFwd) > 10) {
          const rawAngle = Math.round((Math.atan2(dRight, dFwd) * 180) / Math.PI);
          const normed = normYaw(rawAngle);
          m.look = normed === -180 ? 180 : normed;
        }
      } else if (dragState.kind === 'arc') {
        const dRight = cursorRight - m.right;
        const dFwd = cursorFwd - m.fwd;
        if (Math.hypot(dRight, dFwd) > 10) {
          const cursorAngle = (Math.atan2(dRight, dFwd) * 180) / Math.PI;
          const diff = Math.abs(normYaw(cursorAngle - m.look));
          const newArc = Math.max(30, Math.min(180, Math.round(diff * 2 / 5) * 5));
          m.arc = newArc;
        }
      }

      next[dragState.memberIndex] = m;
      return next;
    });
  };

  const handlePointerUp = (e: React.PointerEvent) => {
    if (dragState) {
      try {
        (e.target as Element).releasePointerCapture(e.pointerId);
      } catch {
        // ignore if not captured
      }
      setDragState(null);
    }
  };

  // Mirror current formation
  const handleMirror = () => {
    const currentShape: FormationShape = { members };
    const mirrored = mirrorFormationShape(formation, currentShape);
    if (mirrored.formation !== formation) {
      setFormation(mirrored.formation);
    }
    setMembers(mirrored.shape.members);
    setSaveStatus('Форма зеркалирована (не сохранено)');
  };

  // Reset to default
  const handleResetToDefault = async () => {
    const newDoc: FormationShapesFile = JSON.parse(JSON.stringify(shapesDoc));
    resetFormationShape(newDoc, formation, memberCount);
    setShapesDoc(newDoc);

    const def = computeDefaultFormationShape(formation, memberCount, config);
    setMembers(def.members);

    // Save removal to server
    setSaving(true);
    try {
      const res = await fetch('/api/movement/formation-shapes', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(newDoc),
      });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      onShapesSaved(newDoc);
      setSaveStatus('Сброшено к дефолту из formation_config.json');
    } catch (err) {
      setSaveStatus(`Ошибка сброса: ${String(err)}`);
    } finally {
      setSaving(false);
    }
  };

  // Save current shape to file
  const handleSave = async () => {
    if (!validation.valid) {
      setSaveStatus('Нельзя сохранить форму с нарушениями дистанции или радиуса!');
      return;
    }

    setSaving(true);
    const newDoc: FormationShapesFile = JSON.parse(JSON.stringify(shapesDoc));
    if (!newDoc.formations) newDoc.formations = {};
    if (!newDoc.formations[formation]) newDoc.formations[formation] = {};

    const countKey = String(memberCount);
    newDoc.formations[formation]![countKey] = {
      members: members.map((m) => ({
        role: m.role,
        fwd: m.fwd,
        right: m.right,
        look: m.look,
        ...(m.arc !== undefined ? { arc: m.arc } : {}),
      })),
    };

    try {
      const res = await fetch('/api/movement/formation-shapes', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(newDoc),
      });
      if (!res.ok) {
        const body = await res.json().catch(() => ({}));
        throw new Error(body.error || `HTTP ${res.status}`);
      }
      setShapesDoc(newDoc);
      onShapesSaved(newDoc);
      setSaveStatus('Сохранено в formation_shapes.json!');
    } catch (err) {
      setSaveStatus(`Ошибка сохранения: ${String(err)}`);
    } finally {
      setSaving(false);
    }
  };

  // Helper to generate SVG arc path for sector wedge
  const makeSectorPath = (
    cx: number,
    cy: number,
    radius: number,
    lookDeg: number,
    arcDeg: number
  ): string => {
    const half = arcDeg / 2;
    const startAngle = lookDeg - half;
    const endAngle = lookDeg + half;

    const toRad = (d: number) => (d * Math.PI) / 180;
    // In our SVG: 0 deg is UP (-Y), 90 deg is RIGHT (+X)
    const x1 = cx + radius * Math.sin(toRad(startAngle));
    const y1 = cy - radius * Math.cos(toRad(startAngle));
    const x2 = cx + radius * Math.sin(toRad(endAngle));
    const y2 = cy - radius * Math.cos(toRad(endAngle));

    const largeArcFlag = arcDeg > 180 ? 1 : 0;
    return `M ${cx} ${cy} L ${x1} ${y1} A ${radius} ${radius} 0 ${largeArcFlag} 1 ${x2} ${y2} Z`;
  };

  return (
    <div className="flex flex-col gap-4">
      {/* Top Toolbar */}
      <div className="flex flex-wrap items-center justify-between gap-3 bg-slate-900 border border-slate-800 rounded-xl p-3">
        <div className="flex flex-wrap items-center gap-3">
          {/* Formation Picker */}
          <div className="flex items-center gap-1.5 text-xs text-slate-400">
            <span>Строй:</span>
            <select
              className="bg-slate-950 border border-slate-700 rounded-lg px-2.5 py-1.5 text-slate-100 font-medium text-sm focus:border-cyan-500"
              value={formation}
              onChange={(e) => setFormation(e.target.value as EditableFormation)}
            >
              {EDITABLE_FORMATIONS.map((f) => (
                <option key={f} value={f}>
                  {FORMATION_NAMES_RU[f]}
                </option>
              ))}
            </select>
          </div>

          {/* Member Count Picker */}
          <div className="flex items-center gap-1.5 text-xs text-slate-400">
            <span>Бойцов:</span>
            <div className="inline-flex rounded-lg border border-slate-800 p-0.5 bg-slate-950">
              {(formation === 'pair' ? [2] : [2, 3, 4]).map((n) => (
                <button
                  key={n}
                  onClick={() => setMemberCount(n)}
                  className={`px-2.5 py-1 rounded-md text-xs font-semibold transition-colors ${
                    memberCount === n
                      ? 'bg-cyan-600 text-white'
                      : 'text-slate-400 hover:text-slate-200'
                  }`}
                >
                  {n}
                </button>
              ))}
            </div>
          </div>

          {/* Status Badge */}
          <span
            className={`px-2 py-0.5 rounded text-xs font-medium border ${
              isSavedInFile
                ? 'bg-emerald-950/60 border-emerald-800 text-emerald-300'
                : 'bg-slate-800/60 border-slate-700 text-slate-400'
            }`}
          >
            {isSavedInFile ? 'Сохранено в файл' : 'По умолчанию (правила)'}
          </span>
        </div>

        {/* Action Buttons */}
        <div className="flex items-center gap-2">
          <button
            onClick={handleMirror}
            className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-semibold border border-slate-700 bg-slate-800 text-slate-200 hover:bg-slate-700 transition-colors"
            title="Отразить координаты по горизонтали и инвертировать углы обзора"
          >
            <FlipHorizontal size={14} />
            Зеркало
          </button>
          <button
            onClick={handleResetToDefault}
            disabled={saving}
            className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-semibold border border-slate-700 bg-slate-800 text-slate-200 hover:bg-slate-700 transition-colors disabled:opacity-50"
            title="Удалить сохраненную форму из файла и вернуть правила"
          >
            <RotateCcw size={14} />
            Сброс к дефолту
          </button>
          <button
            onClick={handleSave}
            disabled={saving || !validation.valid}
            className={`inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-semibold border transition-colors ${
              validation.valid
                ? 'border-cyan-500 bg-cyan-600 text-white hover:bg-cyan-500'
                : 'border-slate-800 bg-slate-800 text-slate-500 cursor-not-allowed'
            }`}
          >
            <Save size={14} />
            {saving ? 'Сохранение...' : 'Сохранить'}
          </button>
        </div>
      </div>

      {/* Save / Status Toast */}
      {saveStatus && (
        <div
          className={`text-xs px-3 py-2 rounded-lg border flex items-center justify-between ${
            saveStatus.includes('Ошибка') || saveStatus.includes('Нельзя')
              ? 'bg-rose-950/40 border-rose-800 text-rose-300'
              : 'bg-emerald-950/40 border-emerald-800 text-emerald-300'
          }`}
        >
          <span>{saveStatus}</span>
          <button
            onClick={() => setSaveStatus('')}
            className="text-xs opacity-70 hover:opacity-100 ml-2"
          >
            ×
          </button>
        </div>
      )}

      {/* Main Content: Canvas & Coverage HUD */}
      <div className="grid grid-cols-1 lg:grid-cols-[1fr_300px] gap-4">
        {/* Interactive SVG Diagram */}
        <div className="relative bg-slate-950 border border-slate-800 rounded-2xl overflow-hidden flex flex-col items-center justify-center p-2 select-none">
          {/* Zoom controls */}
          <div className="absolute top-3 right-3 flex items-center gap-1 bg-slate-900/80 border border-slate-800 rounded-lg p-1 z-10">
            <button
              onClick={() => setZoom((z) => Math.max(0.3, z - 0.1))}
              className="p-1 text-slate-400 hover:text-slate-100 rounded"
              title="Уменьшить масштаб"
            >
              <ZoomOut size={14} />
            </button>
            <span className="text-xs font-mono text-slate-300 px-1">
              {Math.round(zoom * 100)}%
            </span>
            <button
              onClick={() => setZoom((z) => Math.min(1.2, z + 0.1))}
              className="p-1 text-slate-400 hover:text-slate-100 rounded"
              title="Увеличить масштаб"
            >
              <ZoomIn size={14} />
            </button>
          </div>

          {/* Heading guide label */}
          <div className="absolute top-3 left-3 text-xs font-mono text-slate-400 bg-slate-900/80 border border-slate-800 rounded-lg px-2.5 py-1 z-10 flex items-center gap-2">
            <span className="w-2 h-2 rounded-full bg-cyan-400 animate-pulse"></span>
            <span>Сетка: 10 см (тяните бойца или стрелку взгляда)</span>
          </div>

          <svg
            ref={svgRef}
            viewBox={`0 0 ${viewWidth} ${viewHeight}`}
            className="w-full max-w-[620px] aspect-square cursor-crosshair"
            onPointerMove={handlePointerMove}
            onPointerUp={handlePointerUp}
          >
            <defs>
              {/* Arrow marker for look vector */}
              <marker
                id="look-arrow"
                viewBox="0 0 10 10"
                refX="6"
                refY="5"
                markerWidth="6"
                markerHeight="6"
                orient="auto-start-reverse"
              >
                <path d="M 0 1 L 10 5 L 0 9 z" fill="#38bdf8" />
              </marker>
              {/* Heading arrow marker */}
              <marker
                id="heading-arrow"
                viewBox="0 0 10 10"
                refX="7"
                refY="5"
                markerWidth="7"
                markerHeight="7"
                orient="auto"
              >
                <path d="M 0 1 L 10 5 L 0 9 z" fill="#06b6d4" />
              </marker>
            </defs>

            {/* Background Grid: 100 cm major, 50 cm minor */}
            {[-400, -300, -200, -100, 0, 100, 200, 300, 400].map((d) => (
              <g key={`grid-x-${d}`}>
                <line
                  x1={toSvgX(d)}
                  y1={0}
                  x2={toSvgX(d)}
                  y2={viewHeight}
                  stroke={d === 0 ? '#334155' : '#1e293b'}
                  strokeWidth={d === 0 ? 1.5 : 0.75}
                  strokeDasharray={d === 0 ? undefined : '4 4'}
                />
                {d !== 0 && (
                  <text
                    x={toSvgX(d) + 4}
                    y={originY + 14}
                    fill="#475569"
                    fontSize="9"
                    fontFamily="monospace"
                  >
                    {d > 0 ? `+${d}` : d}
                  </text>
                )}
              </g>
            ))}
            {[-400, -300, -200, -100, 0, 100, 200, 300, 400].map((d) => (
              <g key={`grid-y-${d}`}>
                <line
                  x1={0}
                  y1={toSvgY(d)}
                  x2={viewWidth}
                  y2={toSvgY(d)}
                  stroke={d === 0 ? '#334155' : '#1e293b'}
                  strokeWidth={d === 0 ? 1.5 : 0.75}
                  strokeDasharray={d === 0 ? undefined : '4 4'}
                />
                {d !== 0 && (
                  <text
                    x={originX + 4}
                    y={toSvgY(d) - 4}
                    fill="#475569"
                    fontSize="9"
                    fontFamily="monospace"
                  >
                    {d > 0 ? `+${d}` : d}
                  </text>
                )}
              </g>
            ))}

            {/* Heading arrow pointing forward from leader */}
            <line
              x1={originX}
              y1={originY}
              x2={originX}
              y2={originY - 90}
              stroke="#06b6d4"
              strokeWidth="2.5"
              strokeDasharray="4 3"
              markerEnd="url(#heading-arrow)"
            />
            <text
              x={originX}
              y={originY - 100}
              fill="#06b6d4"
              fontSize="10"
              fontWeight="bold"
              textAnchor="middle"
            >
              12 ч. (Вперед)
            </text>

            {/* Maximum radius circle (CUSTOM_MAX_RADIUS) */}
            <circle
              cx={originX}
              cy={originY}
              r={CUSTOM_MAX_RADIUS * zoom}
              fill="none"
              stroke="#ef4444"
              strokeWidth="1"
              strokeDasharray="6 6"
              opacity="0.3"
            />

            {/* Spacing violation lines between close members */}
            {validation.spacingViolations.map((v, i) => {
              const mA = members[v.memberA];
              const mB = members[v.memberB];
              if (!mA || !mB) return null;
              return (
                <g key={`violation-${i}`}>
                  <line
                    x1={toSvgX(mA.right)}
                    y1={toSvgY(mA.fwd)}
                    x2={toSvgX(mB.right)}
                    y2={toSvgY(mB.fwd)}
                    stroke="#ef4444"
                    strokeWidth="2.5"
                    strokeDasharray="4 3"
                  />
                  <text
                    x={(toSvgX(mA.right) + toSvgX(mB.right)) / 2}
                    y={(toSvgY(mA.fwd) + toSvgY(mB.fwd)) / 2 - 6}
                    fill="#ef4444"
                    fontSize="10"
                    fontWeight="bold"
                    textAnchor="middle"
                    className="bg-slate-950"
                  >
                    {v.distCm} см (&lt; 120)
                  </text>
                </g>
              );
            })}

            {/* Render look sectors for all members */}
            {members.map((m, i) => {
              const cx = toSvgX(m.right);
              const cy = toSvgY(m.fwd);
              const arc = m.arc ?? DEFAULT_ARC_DEG;
              const sectorRadius = 110 * zoom;
              const colors = ROLE_COLORS[m.role] ?? ROLE_COLORS.right;

              return (
                <path
                  key={`sector-${i}`}
                  d={makeSectorPath(cx, cy, sectorRadius, m.look, arc)}
                  fill={colors.sector}
                  stroke={colors.stroke}
                  strokeWidth="1"
                  strokeOpacity="0.4"
                />
              );
            })}

            {/* Render members, look arrows, and interactive drag handles */}
            {members.map((m, i) => {
              const cx = toSvgX(m.right);
              const cy = toSvgY(m.fwd);
              const colors = ROLE_COLORS[m.role] ?? ROLE_COLORS.right;
              const isCommander = m.role === 'commander' || i === 0;
              const arc = m.arc ?? DEFAULT_ARC_DEG;

              // Look arrow end coordinates
              const arrowLen = 55 * zoom;
              const lookRad = (m.look * Math.PI) / 180;
              const arrowX = cx + arrowLen * Math.sin(lookRad);
              const arrowY = cy - arrowLen * Math.cos(lookRad);

              // Arc edge handles (for resizing sector width)
              const halfArcRad = ((m.look + arc / 2) * Math.PI) / 180;
              const arcHandleX = cx + 80 * zoom * Math.sin(halfArcRad);
              const arcHandleY = cy - 80 * zoom * Math.cos(halfArcRad);

              return (
                <g key={`member-${i}`}>
                  {/* Look Arrow line */}
                  <line
                    x1={cx}
                    y1={cy}
                    x2={arrowX}
                    y2={arrowY}
                    stroke={colors.fill}
                    strokeWidth="2.5"
                  />

                  {/* Look Arrow Head Draggable Handle */}
                  <circle
                    cx={arrowX}
                    cy={arrowY}
                    r={6}
                    fill={colors.fill}
                    stroke="#0f172a"
                    strokeWidth="1.5"
                    className="cursor-grab active:cursor-grabbing hover:scale-125 transition-transform"
                    onPointerDown={(e) => handlePointerDown(i, 'look', e)}
                  />

                  {/* Arc Width Handle */}
                  <circle
                    cx={arcHandleX}
                    cy={arcHandleY}
                    r={4}
                    fill="#94a3b8"
                    stroke="#0f172a"
                    strokeWidth="1"
                    className="cursor-ew-resize hover:scale-125 transition-transform opacity-70 hover:opacity-100"
                    onPointerDown={(e) => handlePointerDown(i, 'arc', e)}
                  />

                  {/* Member Node Disc */}
                  <circle
                    cx={cx}
                    cy={cy}
                    r={18}
                    fill={colors.fill}
                    stroke={isCommander ? '#f59e0b' : '#0f172a'}
                    strokeWidth={isCommander ? 3 : 2}
                    className={
                      isCommander
                        ? 'cursor-not-allowed'
                        : 'cursor-grab active:cursor-grabbing hover:brightness-110'
                    }
                    onPointerDown={(e) => {
                      if (!isCommander) {
                        handlePointerDown(i, 'pos', e);
                      }
                    }}
                  />

                  {/* Role Letter in Disc */}
                  <text
                    x={cx}
                    y={cy + 5}
                    fill="#0f172a"
                    fontSize="13"
                    fontWeight="bold"
                    textAnchor="middle"
                    pointerEvents="none"
                  >
                    {ROLE_LETTERS_RU[m.role] ?? 'Б'}
                  </text>

                  {/* Coordinates label below disc */}
                  <text
                    x={cx}
                    y={cy + 30}
                    fill="#94a3b8"
                    fontSize="9"
                    fontFamily="monospace"
                    textAnchor="middle"
                    pointerEvents="none"
                  >
                    [{m.fwd}, {m.right}] {m.look}°
                  </text>
                </g>
              );
            })}
          </svg>
        </div>

        {/* Coverage HUD & Live Diagnostics */}
        <div className="flex flex-col gap-4">
          {/* 360° Coverage Ring Panel */}
          <div className="bg-slate-900 border border-slate-800 rounded-2xl p-4 flex flex-col gap-3">
            <div className="flex items-center justify-between">
              <h4 className="text-xs font-bold tracking-widest uppercase text-slate-300 flex items-center gap-1.5">
                <Shield size={14} className="text-cyan-400" />
                Круговой обзор 360°
              </h4>
              <span
                className={`text-xs font-mono font-bold px-2 py-0.5 rounded ${
                  validation.coverage.coveragePercent >= 95
                    ? 'bg-emerald-950 text-emerald-300 border border-emerald-800'
                    : 'bg-amber-950 text-amber-300 border border-amber-800'
                }`}
              >
                {validation.coverage.coveragePercent}%
              </span>
            </div>

            {/* Circular Mini-Radar */}
            <div className="flex justify-center p-2">
              <svg viewBox="0 0 160 160" className="w-36 h-36">
                {/* Background Ring */}
                <circle
                  cx={80}
                  cy={80}
                  r={60}
                  fill="#020617"
                  stroke="#1e293b"
                  strokeWidth="10"
                />

                {/* 360 colored sectors */}
                {validation.coverage.degreeCounts.map((count, idx) => {
                  const deg = idx - 179;
                  const rad = ((deg - 90) * Math.PI) / 180;
                  const rInner = 55;
                  const rOuter = 65;
                  const x1 = 80 + rInner * Math.cos(rad);
                  const y1 = 80 + rInner * Math.sin(rad);
                  const x2 = 80 + rOuter * Math.cos(rad);
                  const y2 = 80 + rOuter * Math.sin(rad);

                  let color = '#ef4444'; // blind / unwatched
                  if (count === 1) color = '#06b6d4'; // covered
                  else if (count >= 2) color = '#f59e0b'; // overlap

                  return (
                    <line
                      key={idx}
                      x1={x1}
                      y1={y1}
                      x2={x2}
                      y2={y2}
                      stroke={color}
                      strokeWidth="1.2"
                    />
                  );
                })}

                {/* Squad center dot */}
                <circle cx={80} cy={80} r={4} fill="#06b6d4" />
                <line x1={80} y1={80} x2={80} y2={45} stroke="#06b6d4" strokeWidth="1.5" />
                <text x={80} y={40} fill="#94a3b8" fontSize="8" textAnchor="middle">
                  12 ч.
                </text>
              </svg>
            </div>

            {/* Legend */}
            <div className="flex items-center justify-between text-[11px] text-slate-400 border-t border-slate-800 pt-2">
              <span className="flex items-center gap-1">
                <span className="w-2.5 h-2.5 rounded-full bg-cyan-400"></span> Под контролем
              </span>
              <span className="flex items-center gap-1">
                <span className="w-2.5 h-2.5 rounded-full bg-amber-400"></span> Дублирование
              </span>
              <span className="flex items-center gap-1">
                <span className="w-2.5 h-2.5 rounded-full bg-rose-500"></span> Слепая зона
              </span>
            </div>

            {/* Blind Spans Details */}
            {validation.coverage.uncoveredSpans.length > 0 ? (
              <div className="bg-rose-950/20 border border-rose-900/40 rounded-lg p-2 flex flex-col gap-1 text-xs">
                <span className="font-semibold text-rose-300 flex items-center gap-1">
                  <AlertTriangle size={12} />
                  Слепые секторы ({validation.coverage.uncoveredPercent}%):
                </span>
                {validation.coverage.uncoveredSpans.map((s, idx) => (
                  <span key={idx} className="font-mono text-slate-300 ml-4">
                    {s.from}° .. {s.to}° ({Math.abs(s.to - s.from)}°)
                  </span>
                ))}
              </div>
            ) : (
              <div className="bg-emerald-950/20 border border-emerald-900/40 rounded-lg p-2 text-xs text-emerald-300 flex items-center gap-1.5">
                <CheckCircle2 size={13} />
                Полный круговой обзор 360° обеспечен!
              </div>
            )}
          </div>

          {/* Members Table */}
          <div className="bg-slate-900 border border-slate-800 rounded-2xl p-4 flex flex-col gap-2.5">
            <h4 className="text-xs font-bold tracking-widest uppercase text-slate-300">
              Позиции бойцов (вперед / вправо)
            </h4>
            <div className="flex flex-col gap-1.5 text-xs font-mono">
              {members.map((m, idx) => {
                const colors = ROLE_COLORS[m.role] ?? ROLE_COLORS.right;
                return (
                  <div
                    key={idx}
                    className="flex items-center justify-between p-2 rounded-lg bg-slate-950 border border-slate-800/80"
                  >
                    <div className="flex items-center gap-2">
                      <span
                        className="w-4 h-4 rounded-full flex items-center justify-center font-bold text-[10px] text-slate-950"
                        style={{ backgroundColor: colors.fill }}
                      >
                        {ROLE_LETTERS_RU[m.role]}
                      </span>
                      <span className="text-slate-200">{ROLE_NAMES_RU[m.role]}:</span>
                    </div>
                    <span className="text-slate-400">
                      fwd: <b className="text-cyan-300">{m.fwd}</b>, right:{' '}
                      <b className="text-cyan-300">{m.right}</b>, look:{' '}
                      <b className="text-amber-300">{m.look}°</b>, arc:{' '}
                      <b className="text-slate-300">{m.arc ?? DEFAULT_ARC_DEG}°</b>
                    </span>
                  </div>
                );
              })}
            </div>
          </div>

          {/* Validation Warnings */}
          {(!validation.valid || validation.spacingViolations.length > 0) && (
            <div className="bg-rose-950/30 border border-rose-900 rounded-2xl p-3 flex flex-col gap-1.5 text-xs text-rose-300">
              <span className="font-bold flex items-center gap-1">
                <AlertTriangle size={14} /> Нарушения требований:
              </span>
              {validation.spacingViolations.map((v, i) => (
                <span key={i} className="ml-3">
                  • Дистанция между бойцами {v.roleA} и {v.roleB}: {v.distCm} см (минимум{' '}
                  {v.minSpacingCm} см).
                </span>
              ))}
              {validation.radiusViolations.map((v, i) => (
                <span key={i} className="ml-3">
                  • Боец {v.role} удален от командира на {v.radiusCm} см (максимум {v.maxRadiusCm} см).
                </span>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
