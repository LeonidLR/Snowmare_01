// Training Ground «Генератор уровней» - the LAYERS panel and the layers' SVG overlay (designer spec 2026-11-16;
// docs/port/LEVELGEN_LAYERS_REPORT.md). The panel: every layer of Content/Data/LevelGen/layers.json with its colour, count, a
// show / hide checkbox, «только этот» (solo) and the pencil that makes it the ACTIVE layer (the tools then act on it alone); the
// active layer's tools (select / move, add by click, delete, rotate, re-roll the layer inside the zone); the obstacle size-class
// rule; the props file's state; the chokepoint list sorted by score (click = zoom to it) with the reasons and the threat model's
// numbers. The overlay (drawn inside RegionOverlay's SVG): chokepoints as heat-coloured outlines with a hover tooltip, the threat
// sub-layers (drop spots, windows, roof positions, hiding spots), the props markers and the selection. Logic: levelgen/layers.ts,
// levelgen/chokepoints.ts.
import React, { useState } from 'react';
import { Crosshair, Layers, MousePointer2, Pencil, Plus, RefreshCw, RotateCcw, RotateCw, Save, Shuffle, Trash2 } from 'lucide-react';
import { dirOf } from '../movement/geometry';
import type { Vec2 } from '../movement/geometry';
import type { ChokeConfig, Chokepoint, Conditions, LayoutAnalysis, ThreatNumbers } from '../levelgen/chokepoints';
import type { Entity, LayerDef, LayerVisibility, ResolvedProp } from '../levelgen/layers';
import { isSolo, layerMovable, LAYERS, MAP_PANEL_EXCLUDED, MAP_PLAN_HIDDEN, movableTypes, sizeClassRuleText, soloVisibility } from '../levelgen/layers';
import { NUDGE_CM } from '../levelgen/mapEdit';
import type { OverlayHelpers } from './RegionOverlay';

/** The heat colour of a score 0..1: yellow -> orange -> red. */
export function heatColor(score: number): string {
  const t = Math.max(0, Math.min(1, (score - 0.25) / 0.6));
  const a = t < 0.5 ? [253, 224, 71] : [251, 146, 60], b = t < 0.5 ? [251, 146, 60] : [239, 68, 68], k = t < 0.5 ? t * 2 : (t - 0.5) * 2;
  return `rgb(${a.map((v, i) => Math.round(v + (b[i] - v) * k)).join(',')})`;
}

const WEATHER: [string, string][] = [['clear', 'ясно'], ['overcast', 'пасмурно'], ['snowfall', 'снегопад'], ['blizzard', 'буран']];
const TIMES: [string, string][] = [['day', 'день'], ['twilight', 'сумерки'], ['night', 'ночь']];
const ROUTE_RU: Record<string, string> = { mandatory: 'основной маршрут', variant: 'вариант маршрута', objectives: 'путь к целям', street: 'улица' };
const TAG_RU: Record<string, string> = { narrow: 'узко', clutter: 'завал', canyon: 'каньон', only_way: 'единственный путь', drop_ambush: 'спрыгивание', window_fire: 'окна', sniper: 'снайпер', hiding: 'скрытый противник' };
const GROUP_RU: Record<string, string> = { main: 'Слои карты', analysis: 'Анализ', service: 'Служебные' };

const MOVE_TYPE_RU: Record<string, string> = { building: 'здание целиком', ladder: 'лестница', crossing: 'мост / пандус', wall: 'стена / забор', cover: 'преграда' };

export interface PropsInfo { map: string; path: string; revision: number; frozen: boolean; dirty: boolean }

interface PanelProps {
  counts: Record<string, number>;
  vis: LayerVisibility;
  onVis: (v: LayerVisibility) => void;
  /** The layer the tools act on (null = none: the plan behaves as before the layers). */
  active: string | null;
  onActive: (id: string | null) => void;
  tool: 'select' | 'add';
  onTool: (t: 'select' | 'add') => void;
  addIdx: number;
  onAddIdx: (i: number) => void;
  tintOn: boolean;
  onTint: (v: boolean) => void;
  selected: Entity | null;
  onDelete: () => void;
  onRotate: (deg: number) => void;
  /** «другой план» for a selected building (S2.7): absent = no plans on the map. `replanState`: null = not a planned building; 'locked' / 'no_base' = refused. */
  onReplan?: (id: string) => void;
  replanState?: (id: string) => 'ok' | 'locked' | 'no_base' | null;
  /** Re-roll the active (obstacle) layer inside the drawn zone; null = no zone / no reference profile. */
  onRerollLayer: (() => void) | null;
  message: { ok: boolean; text: string } | null;
  propsInfo: PropsInfo | null;
  propsNote: string;
  onSaveProps: () => void;
  busy: boolean;
  analysis: LayoutAnalysis | null;
  analysisBusy: boolean;
  conditions: Conditions;
  onConditions: (c: Conditions) => void;
  onAnalyze: () => void;
  selChoke: string | null;
  onZoomChoke: (id: string) => void;
  threat: ThreatNumbers;
  config: ChokeConfig;
}

export const LayersPanel: React.FC<PanelProps> = ({ counts, vis, onVis, active, onActive, tool, onTool, addIdx, onAddIdx, tintOn, onTint, selected, onDelete, onRotate, onReplan, replanState, onRerollLayer, message, propsInfo, propsNote, onSaveProps, busy,
  analysis, analysisBusy, conditions, onConditions, onAnalyze, selChoke, onZoomChoke, threat, config }) => {
  const def = active ? LAYERS.layers.find((l) => l.id === active) ?? null : null;
  const row = (l: LayerDef, indent = false) => {
    const on = vis[l.id] !== false, solo = isSolo(vis, l.id);
    return (
      <li key={l.id} className={`flex flex-col gap-0.5 rounded px-1 py-0.5 ${active === l.id ? 'bg-cyan-950/70 ring-1 ring-cyan-700' : ''} ${indent ? 'ml-4' : ''}`} data-layer={l.id}>
        <div className="flex items-center gap-1.5">
          <input type="checkbox" checked={on} onChange={(e) => onVis({ ...vis, [l.id]: e.target.checked })} className="accent-cyan-500" title="Показать / скрыть слой" data-layer-check={l.id} />
          <span className="inline-block w-3 h-3 rounded-sm border border-black/40 shrink-0" style={{ background: l.color }} />
          <span className={`flex-1 min-w-0 truncate ${on ? 'text-slate-200' : 'text-slate-500 line-through'}`} title={l.hint ?? l.name}>{l.name}</span>
          <span className="font-mono text-slate-400 tabular-nums" data-layer-count={l.id}>{counts[l.id] ?? 0}</span>
          <button type="button" onClick={() => onVis(solo ? Object.fromEntries(LAYERS.layers.map((x) => [x.id, true])) : soloVisibility(l.id))} title={solo ? 'Показать все слои' : 'Только этот слой'}
            className={`px-1 rounded text-[10px] font-bold border ${solo ? 'bg-amber-500 border-amber-400 text-slate-950' : 'bg-slate-800 border-slate-700 text-slate-400 hover:text-white'}`} data-layer-solo={l.id}>S</button>
          {l.edit.length > 0
            ? <button type="button" onClick={() => onActive(active === l.id ? null : l.id)} title={active === l.id ? 'Снять активный слой' : 'Сделать активным: инструменты действуют только на этот слой'}
                className={`p-0.5 rounded border ${active === l.id ? 'bg-cyan-500 border-cyan-400 text-slate-950' : 'bg-slate-800 border-slate-700 text-slate-400 hover:text-white'}`} data-layer-edit={l.id}><Pencil size={11} /></button>
            : <span className="w-[19px]" />}
        </div>
        {l.size_class && <p className="text-[10px] leading-snug text-slate-500 pl-6" data-size-rule={l.size_class}>{sizeClassRuleText(l.size_class)}</p>}
        {l.props && !propsInfo && <p className="text-[10px] leading-snug text-amber-300/80 pl-6">{propsNote}</p>}
      </li>
    );
  };
  const group = (g: 'main' | 'analysis') => LAYERS.layers.filter((l) => l.group === g && !MAP_PANEL_EXCLUDED.includes(l.id));
  const threats = LAYERS.layers.filter((l) => l.group === 'threats');
  const chokes = analysis?.chokepoints ?? [];
  return (
    <section className="bg-slate-900 border border-slate-800 rounded-2xl p-4 flex flex-col gap-2 min-w-0 text-xs" data-layers-panel>
      <div className="flex items-center gap-2">
        <h3 className="text-xs font-bold tracking-widest uppercase text-slate-400 inline-flex items-center gap-1.5"><Layers size={13} />Слои</h3>
        <button type="button" onClick={() => onVis(Object.fromEntries(LAYERS.layers.map((x) => [x.id, !MAP_PLAN_HIDDEN.includes(x.id)])))} className="px-1.5 py-0.5 rounded bg-slate-800 border border-slate-700 text-slate-300 hover:bg-slate-700">все</button>
        <button type="button" onClick={() => onVis(Object.fromEntries(LAYERS.layers.map((x) => [x.id, false])))} className="px-1.5 py-0.5 rounded bg-slate-800 border border-slate-700 text-slate-300 hover:bg-slate-700">ничего</button>
        <label className="ml-auto inline-flex items-center gap-1 text-slate-400" title="Преграды на плане красятся цветом своего класса (крупные / средние / мелкие)"><input type="checkbox" checked={tintOn} onChange={(e) => onTint(e.target.checked)} className="accent-cyan-500" />цвета классов</label>
      </div>
      <p className="text-[10px] text-slate-500 leading-snug">Галочка — показать слой, <b>S</b> — только он, карандаш — активный слой: выбор, перенос, добавление и удаление действуют только на него. Состав слоёв — <span className="font-mono">Content/Data/LevelGen/layers.json</span>.</p>
      <p className="text-[10px] font-bold tracking-widest uppercase text-slate-500">{GROUP_RU.main}</p>
      <ul className="flex flex-col gap-0.5">{group('main').map((l) => row(l))}</ul>
      <p className="text-[10px] font-bold tracking-widest uppercase text-slate-500 mt-1">{GROUP_RU.analysis}</p>
      <ul className="flex flex-col gap-0.5">{group('analysis').map((l) => row(l))}{threats.map((l) => row(l, true))}</ul>
      <details><summary className="cursor-pointer text-[10px] font-bold tracking-widest uppercase text-slate-500 select-none">{GROUP_RU.service}</summary>
        <ul className="flex flex-col gap-0.5 mt-1">{LAYERS.layers.filter((l) => l.group === 'service').map((l) => row(l))}</ul>
      </details>

      {def && (
        <div className="flex flex-col gap-1.5 bg-cyan-950/40 border border-cyan-900 rounded-xl p-2" data-active-layer={def.id}>
          <p className="text-cyan-200">Активный слой: <b>{def.name}</b>{def.hint ? <span className="text-slate-400"> — {def.hint}</span> : null}</p>
          {def.id === 'objectives' ? <p className="text-slate-400">Старт и цели: «+ цель» над планом и щелчок, перетаскивание мышью, × или правая кнопка — удалить.</p> : (
            <div className="flex flex-wrap items-center gap-1.5">
              <button type="button" onClick={() => onTool('select')} className={`inline-flex items-center gap-1 px-2 py-1 rounded border ${tool === 'select' ? 'bg-cyan-500 border-cyan-400 text-slate-950 font-semibold' : 'bg-slate-800 border-slate-700 text-slate-200'}`} data-tool="select"><MousePointer2 size={12} />{layerMovable(def) ? 'Выбрать / двигать' : 'Выбрать'}</button>
              {def.edit.includes('add') && def.add && def.add.length > 0 && (<>
                <button type="button" onClick={() => onTool('add')} className={`inline-flex items-center gap-1 px-2 py-1 rounded border ${tool === 'add' ? 'bg-cyan-500 border-cyan-400 text-slate-950 font-semibold' : 'bg-slate-800 border-slate-700 text-slate-200'}`} data-tool="add"><Plus size={12} />Добавить щелчком</button>
                <select value={addIdx} onChange={(e) => { onAddIdx(Number(e.target.value)); onTool('add'); }} className="bg-slate-950 border border-slate-700 rounded px-1.5 py-0.5 text-slate-100" data-add-template>
                  {def.add.map((t, i) => <option key={t.label} value={i}>{t.label}</option>)}
                </select>
              </>)}
              {def.edit.includes('delete') && <button type="button" onClick={onDelete} disabled={!selected || busy} title="Удалить выбранное (Delete)" className="inline-flex items-center gap-1 px-2 py-1 rounded bg-rose-900 border border-rose-700 text-rose-100 disabled:opacity-40" data-tool="delete"><Trash2 size={12} />Удалить</button>}
              {def.edit.includes('move') && !def.props && (<>
                <button type="button" onClick={() => onRotate(15)} disabled={!selected || busy} title="Повернуть на 15° против часовой" className="p-1 rounded bg-slate-800 border border-slate-700 text-slate-200 disabled:opacity-40"><RotateCcw size={12} /></button>
                <button type="button" onClick={() => onRotate(-15)} disabled={!selected || busy} title="Повернуть на 15° по часовой" className="p-1 rounded bg-slate-800 border border-slate-700 text-slate-200 disabled:opacity-40"><RotateCw size={12} /></button>
              </>)}
              {def.edit.includes('reroll') && (def.size_class
                ? <button type="button" onClick={() => onRerollLayer?.()} disabled={!onRerollLayer || busy} title="Нарисуйте зону (рамка / многоугольник / лассо): внутри неё заново расставляются только укрытия этого слоя, остальное не меняется" className="inline-flex items-center gap-1 px-2 py-1 rounded bg-amber-500 text-slate-950 font-semibold disabled:opacity-40" data-tool="reroll"><Shuffle size={12} />Перегенерировать слой в зоне</button>
                : <span className="text-slate-400">перегенерация зданий — «Перегенерировать зону» (режимы «Полностью» / «Тип квартала»)</span>)}
            </div>
          )}
          {layerMovable(def) && def.id !== 'objectives' && (
            <p className="text-[11px] text-slate-400" data-move-hint>
              Двигать: перетащите на плане ({movableTypes(def).filter((t) => t !== 'prop').map((t) => MOVE_TYPE_RU[t] ?? t).join(', ')}); стрелки сдвигают выбранное на {NUDGE_CM.fine} см, Shift — {NUDGE_CM.coarse / 100} м, Ctrl — {NUDGE_CM.huge / 100} м; Ctrl+Z / Ctrl+Y — отменить / вернуть.
            </p>
          )}
          {def.props && (propsInfo
            ? <p className="text-slate-400 flex flex-wrap items-center gap-1.5">Файл пропсов: <span className="font-mono text-slate-300">{propsInfo.path}</span> (ревизия {propsInfo.revision}{propsInfo.frozen ? ', заморожен' : ''})
                <button type="button" onClick={onSaveProps} disabled={!propsInfo.dirty || busy || propsInfo.frozen} className="inline-flex items-center gap-1 px-2 py-0.5 rounded bg-emerald-700 text-white disabled:opacity-40" data-save-props><Save size={11} />Сохранить пропсы{propsInfo.dirty ? ' *' : ''}</button></p>
            : <p className="text-amber-300">{propsNote}</p>)}
          {selected && <p className="text-slate-300" data-selected={selected.key}>Выбрано: <span className="font-mono text-cyan-200">{selected.id}</span>{selected.kind ? ` (${selected.kind}${selected.variant ? ` / ${selected.variant}` : ''})` : ''}{selected.height_cm !== undefined ? `, высота ${selected.height_cm} см` : ''}{selected.footprint_m2 !== undefined ? `, площадь ${Math.round(selected.footprint_m2 * 100) / 100} м²` : ''}{selected.size_class ? `, класс: ${selected.size_class === 'large' ? 'крупная' : selected.size_class === 'medium' ? 'средняя' : 'мелкая'}` : ''}</p>}
          {selected?.type === 'building' && onReplan && replanState?.(selected.id) && (
            <button type="button" onClick={() => onReplan(selected.id)} disabled={busy || replanState(selected.id) !== 'ok'} data-replan-selected={selected.id}
              title={replanState(selected.id) === 'locked' ? 'здание закреплено: план и история сохраняются' : replanState(selected.id) === 'no_base' ? 'карта загружена из файла: нет исходного здания, перегенерируйте' : 'Перепланировать только это здание (следующий счётчик seed плана), его истории пересчитать, остальную карту не трогать; отмена — «Отменить»'}
              className="self-start inline-flex items-center gap-1 px-2 py-1 rounded bg-sky-700 border border-sky-500 text-white font-semibold disabled:opacity-40"><Shuffle size={12} />Другой план для {selected.id}</button>
          )}
          {message && <p className={message.ok ? 'text-emerald-300' : 'text-rose-300'} data-layer-msg>{message.text}</p>}
        </div>
      )}

      <div className="flex flex-col gap-1.5 border-t border-slate-800 pt-2" data-choke-panel>
        <div className="flex flex-wrap items-center gap-1.5">
          <h4 className="text-[11px] font-bold tracking-widest uppercase text-slate-400 inline-flex items-center gap-1"><Crosshair size={12} />Узкие места и угрозы</h4>
          <button type="button" onClick={onAnalyze} disabled={busy || analysisBusy} className="inline-flex items-center gap-1 px-2 py-0.5 rounded bg-slate-800 border border-slate-700 text-slate-200 hover:bg-slate-700 disabled:opacity-40" data-analyze><RefreshCw size={11} className={analysisBusy ? 'animate-spin' : ''} />Пересчитать</button>
          <select value={conditions.weather} onChange={(e) => onConditions({ ...conditions, weather: e.target.value })} title="Погода: множитель обзора из enemy_perception.json" className="bg-slate-950 border border-slate-700 rounded px-1 py-0.5 text-slate-100" data-weather>{WEATHER.map(([v, n]) => <option key={v} value={v}>{n}</option>)}</select>
          <select value={conditions.time_of_day} onChange={(e) => onConditions({ ...conditions, time_of_day: e.target.value })} title="Время суток: множитель обзора из enemy_perception.json" className="bg-slate-950 border border-slate-700 rounded px-1 py-0.5 text-slate-100" data-time>{TIMES.map(([v, n]) => <option key={v} value={v}>{n}</option>)}</select>
        </div>
        {analysisBusy && <p className="text-cyan-300">Считаю узкие места…</p>}
        {!analysis && !analysisBusy && <p className="text-slate-500">Анализ считается, когда включён слой «Узкие места» или любой слой угроз; сохраняется в JSON карты кнопкой «Принять» (блок <span className="font-mono">analysis</span>).</p>}
        {analysis && (<>
          <p className="text-slate-400">Мест: <b className="text-slate-200">{chokes.length}</b> (оценка ≥ {config.places.min_score}); точек спрыгивания {analysis.dropAmbushSpots.length}, окон {analysis.windowsCovering.length}, крыш снайпера {analysis.overwatchRoofs.length}, укрытий {analysis.hidingSpots.length}. Обзор отряда {Math.round(threat.hiding.squad_sight_m)} м, в скрытности противник виден с {Math.round(threat.hiding.stealth_sight_m * 10) / 10} м. Линий {analysis.stats.lines}, {analysis.stats.length_m} м.</p>
          <ul className="flex flex-col gap-0.5 max-h-72 overflow-auto pr-1" data-choke-list>
            {chokes.map((c) => (
              <li key={c.id}>
                <button type="button" onClick={() => onZoomChoke(c.id)} className={`w-full text-left rounded px-1.5 py-1 border ${selChoke === c.id ? 'border-cyan-500 bg-slate-800' : 'border-transparent hover:bg-slate-800/70'}`} data-choke={c.id}>
                  <span className="flex items-center gap-1.5">
                    <span className="inline-block w-9 text-center rounded font-mono font-bold text-slate-950" style={{ background: heatColor(c.score) }}>{c.score.toFixed(2)}</span>
                    <span className="font-mono text-slate-400">{c.id.replace('CP_', '№')}</span>
                    <span className="text-slate-100 flex-1 min-w-0 truncate">{c.kind_name}</span>
                    <span className="font-mono text-slate-400">{c.width_m !== null ? `${c.width_m.toFixed(1)} м` : c.lane_m !== null ? `≈${c.lane_m.toFixed(1)} м` : '—'}</span>
                  </span>
                  <span className="block text-[10px] text-slate-500 truncate">{ROUTE_RU[c.route]}; {c.tags.map((t) => TAG_RU[t]).join(' + ')}</span>
                  {selChoke === c.id && <span className="block mt-1 text-[11px] text-slate-300 whitespace-normal">{c.reasons.map((r, i) => <span key={i} className="block">— {r}</span>)}</span>}
                </button>
              </li>
            ))}
          </ul>
        </>)}
        <details className="text-[11px] text-slate-400" data-threat-model><summary className="cursor-pointer select-none">Правила и числа: пороги (chokepoints.json) и модель угроз (конфиги игры)</summary>
          <ul className="mt-1 flex flex-col gap-0.5">
            <li>узкий проход: ширина между стенами / зданиями / крупными преградами &lt; {config.narrow.width_m} м (полный балл при ≤ {config.narrow.full_width_m} м)</li>
            <li>завал: на {config.clutter.window_m} м улицы ≥ {config.clutter.min_obstacles} преград и ≥ {Math.round(config.clutter.blocked_fraction * 100)} % полосы ±{config.clutter.half_band_m} м под ними, или свободный проход &lt; {config.clutter.lane_width_m} м на {config.clutter.lane_min_length_m} м и больше</li>
            <li>каньон: с обеих сторон объекты ≥ {config.canyon.tall_min_height_cm} см не дальше {config.canyon.side_max_m} м на ≥ {Math.round(config.canyon.min_share * 100)} % длины</li>
            <li>единственный путь: участок перекрыт — обхода нет в пределах {config.redundancy.search_margin_m} м или обход длиннее на {Math.round(config.redundancy.detour_critical_m * config.redundancy.tag_min_value)} м и больше (критично {config.redundancy.detour_critical_m} м)</li>
            <li>оценка = min(1, сумма вес × значение): {Object.entries(config.weights).filter(([k]) => k !== 'comment').map(([k, v]) => `${TAG_RU[k] ?? 'на маршруте'} ${v}`).join(', ')}</li>
          </ul>
          <table className="mt-1 w-full"><tbody>{threat.sources.map((s) => <tr key={s.name} className="align-top"><td className="pr-2 text-slate-300">{s.name}</td><td className="pr-2 font-mono text-cyan-200 whitespace-nowrap">{String(s.value)}</td><td className={`text-[10px] ${s.source.includes('по умолчанию') ? 'text-amber-300/80' : 'text-slate-500'}`}>{s.source}</td></tr>)}</tbody></table>
        </details>
      </div>
    </section>
  );
};

// ---- the overlay ----------------------------------------------------------------------------------------------------------------

interface OverlayProps {
  h: OverlayHelpers;
  vis: LayerVisibility;
  analysis: LayoutAnalysis | null;
  selChoke: string | null;
  onSelChoke: (id: string | null) => void;
  /** The props on the map by layer id (layers.ts), with the layer colours. */
  props: { layer: string; prop: ResolvedProp }[];
  selected: Entity | null;
  /** The entities of the active layer are outlined faintly when it is a small layer (roads, buildings, bridges). */
  activeEntities: Entity[];
}

const colorOf = (id: string): string => LAYERS.layers.find((l) => l.id === id)?.color ?? '#ffffff';

function shapeOutline(e: Entity, h: OverlayHelpers, stroke: string, width: number, dash?: string, key?: string): React.ReactNode {
  const s = e.shape;
  if (s.kind === 'poly') return <polygon key={key} points={h.pts(s.poly)} fill="none" stroke={stroke} strokeWidth={width} strokeDasharray={dash} style={{ pointerEvents: 'none' }} />;
  if (s.kind === 'seg') { const a = h.scr(s.a), b = h.scr(s.b); return <line key={key} x1={a[0]} y1={a[1]} x2={b[0]} y2={b[1]} stroke={stroke} strokeWidth={Math.max(width + 2, s.thickness * h.scale + 4)} strokeOpacity={0.55} strokeLinecap="round" style={{ pointerEvents: 'none' }} />; }
  const q = h.scr(e.pos);
  return <circle key={key} cx={q[0]} cy={q[1]} r={Math.max(7, s.r * h.scale + 4)} fill="none" stroke={stroke} strokeWidth={width} strokeDasharray={dash} style={{ pointerEvents: 'none' }} />;
}

/** The layers' SVG, drawn inside RegionOverlay (same transform as the canvas): props, chokepoints + tooltip, threat spots, the selection. */
export const LayersOverlay: React.FC<OverlayProps> = ({ h, vis, analysis, selChoke, onSelChoke, props, selected, activeEntities }) => {
  const [hover, setHover] = useState<string | null>(null);
  const on = (id: string) => vis[id] !== false;
  const tick = (p: Vec2, yaw: number, len: number): [number, number] => { const d = dirOf(yaw); return h.scr([p[0] + d[0] * len, p[1] + d[1] * len]); };
  const tipOf: Chokepoint | undefined = analysis?.chokepoints.find((c) => c.id === (hover ?? selChoke));
  const tip = on('chokepoints') ? tipOf : undefined;
  return (
    <g data-layers-overlay>
      {activeEntities.map((e) => shapeOutline(e, h, 'rgba(34, 211, 238, 0.75)', 1.2, '3 3', `a:${e.key}`))}
      {/* props of the map's props file */}
      {props.filter((x) => on(x.layer)).map(({ layer, prop }) => {
        const q = h.scr([prop.object.pos[0], prop.object.pos[1]]), c = colorOf(layer), r = 5;
        const title = `${prop.object.id} (${prop.object.variant}${prop.object.trigger ? `, ${String(prop.object.trigger)}` : ''})${prop.hand ? ' — вручную' : ''}`;
        return (
          <g key={prop.object.id} data-prop={prop.object.id} data-prop-layer={layer} style={{ pointerEvents: 'none' }}>
            <title>{title}</title>
            {layer === 'mines' ? (<><circle cx={q[0]} cy={q[1]} r={r} fill="#1f2937" stroke={c} strokeWidth={1.8} /><path d={`M${q[0] - 3},${q[1] - 3} L${q[0] + 3},${q[1] + 3} M${q[0] + 3},${q[1] - 3} L${q[0] - 3},${q[1] + 3}`} stroke={c} strokeWidth={1.5} /></>)
              : layer === 'loot' ? <rect x={q[0] - r} y={q[1] - r} width={2 * r} height={2 * r} rx={1.5} fill={c} stroke="#022c22" strokeWidth={1.2} />
              : layer === 'special' ? <path d={`M${q[0]},${q[1] - r - 1} L${q[0] + r + 1},${q[1]} L${q[0]},${q[1] + r + 1} L${q[0] - r - 1},${q[1]} Z`} fill={c} stroke="#2e1065" strokeWidth={1.2} />
              : <rect x={q[0] - 3.5} y={q[1] - 3.5} width={7} height={7} fill="none" stroke={c} strokeWidth={1.6} />}
          </g>
        );
      })}
      {/* threat sub-layers */}
      {analysis && on('threat_windows') && analysis.windowsCovering.map((w) => { const q = h.scr(w.pos), t = tick(w.pos, w.yaw, 260); return (
        <g key={`w:${w.id}`} data-window={w.id}><title>{`Окно ${w.id} (${w.building}, этаж ${w.floor}): простреливает ${w.covered_m} м линий, ближайшая точка ${w.min_dist_m} м; места: ${w.places.join(', ')}`}</title>
          <line x1={q[0]} y1={q[1]} x2={t[0]} y2={t[1]} stroke={colorOf('threat_windows')} strokeWidth={1.5} /><rect x={q[0] - 3.5} y={q[1] - 3.5} width={7} height={7} fill={colorOf('threat_windows')} stroke="#082f49" strokeWidth={1} /></g>); })}
      {analysis && on('threat_overwatch') && analysis.overwatchRoofs.flatMap((r) => r.spots.map((s, i) => { const q = h.scr(s.pos), t = tick(s.pos, s.facing_yaw, 500); return (
        <g key={`o:${r.building}:${i}`} data-overwatch={r.building}><title>{`Снайперская позиция на крыше ${r.building} (+${r.level}, ${r.height_cm / 100} м): видит ${s.covered_m} м линий, дальний луч ${s.longest_los_m} м, дальность ${r.range_m} м; доступ: ${r.access.join(', ')}`}</title>
          <line x1={q[0]} y1={q[1]} x2={t[0]} y2={t[1]} stroke={colorOf('threat_overwatch')} strokeWidth={1.5} strokeDasharray="4 2" />
          <path d={`M${q[0]},${q[1] - 7} L${q[0] + 7},${q[1]} L${q[0]},${q[1] + 7} L${q[0] - 7},${q[1]} Z`} fill={colorOf('threat_overwatch')} stroke="#4a044e" strokeWidth={1.2} /></g>); }))}
      {analysis && on('threat_drop') && analysis.dropAmbushSpots.map((d) => { const a = h.scr(d.edge), b = h.scr(d.land); return (
        <g key={d.id} data-drop={d.id}><title>{`Спрыгивание ${d.id}: с ${d.kind === 'roof' ? 'крыши' : 'верха стены'} ${d.surface.split(':')[1]} (${d.drop_cm / 100} м), до точки ${Math.round(d.gap_cm) / 100} м; доступ: ${d.access === 'stairs' ? 'лестничный марш' : d.access === 'ladder' ? 'только приставная лестница' : d.access === 'wall' ? 'верх стены' : 'нет'}; ${d.jumpers.join(', ')}`}</title>
          <line x1={a[0]} y1={a[1]} x2={b[0]} y2={b[1]} stroke={colorOf('threat_drop')} strokeWidth={2.2} /><circle cx={a[0]} cy={a[1]} r={3} fill={colorOf('threat_drop')} />
          <path d={`M${b[0]},${b[1] - 6} L${b[0] + 5.5},${b[1] + 4} L${b[0] - 5.5},${b[1] + 4} Z`} fill={colorOf('threat_drop')} stroke="#431407" strokeWidth={1} transform={`rotate(180 ${b[0]} ${b[1]})`} /></g>); })}
      {analysis && on('threat_hiding') && analysis.hidingSpots.map((s) => { const q = h.scr(s.pos), t = tick(s.pos, s.facing_yaw, 220), c = colorOf('threat_hiding'); return (
        <g key={s.id} data-hiding={s.id}><title>{`Укрытие ${s.id}: противник виден с ${s.reveal_normal_m} м (в скрытности с ${s.reveal_stealth_m} м)${s.stealth_only ? ' — только для бойца в режиме скрытности' : ''}${s.blind ? '; за углом, без прямой видимости' : ''}; бросок ${s.rush_m} м; за: ${s.cover} (${s.cover_height_cm} см); угрожает: ${s.threatens.line}`}</title>
          <line x1={q[0]} y1={q[1]} x2={t[0]} y2={t[1]} stroke={c} strokeWidth={1.4} /><circle cx={q[0]} cy={q[1]} r={4} fill={s.stealth_only ? 'rgba(15,23,42,0.85)' : c} stroke={c} strokeWidth={1.6} strokeDasharray={s.stealth_only ? '2 1.5' : undefined} /></g>); })}
      {/* chokepoints: heat by score */}
      {analysis && on('chokepoints') && analysis.chokepoints.map((c) => {
        const col = heatColor(c.score), q = h.scr(c.pos), sel = selChoke === c.id || hover === c.id;
        return (
          <g key={c.id} data-chokepoint={c.id} onPointerEnter={() => setHover(c.id)} onPointerLeave={() => setHover((x) => (x === c.id ? null : x))} onClick={() => onSelChoke(c.id)} style={{ cursor: 'pointer' }}>
            <polygon points={h.pts(c.poly)} fill={col} fillOpacity={sel ? 0.5 : 0.3} stroke={col} strokeWidth={sel ? 2.6 : 1.6} strokeLinejoin="round" />
            <circle cx={q[0]} cy={q[1]} r={8.5} fill="#0f172a" stroke={col} strokeWidth={2} />
            <text x={q[0]} y={q[1] + 3.5} textAnchor="middle" fill={col} fontSize={9.5} fontWeight="bold" fontFamily="ui-monospace, monospace" style={{ pointerEvents: 'none' }}>{Number(c.id.slice(3))}</text>
          </g>
        );
      })}
      {selected && shapeOutline(selected, h, '#f8fafc', 2.2, '5 3', 'sel')}
      {tip && (() => {
        const q = h.scr(tip.pos), w = 300, left = q[0] + w + 24 > h.size[0], top = Math.max(4, Math.min(h.size[1] - 190, q[1] - 30));
        return (
          <foreignObject x={left ? q[0] - w - 14 : q[0] + 14} y={top} width={w} height={400} style={{ pointerEvents: 'none', overflow: 'visible' }} data-choke-tip={tip.id}>
            <div style={{ background: 'rgba(2, 6, 23, 0.94)', border: `1.5px solid ${heatColor(tip.score)}`, borderRadius: 8, padding: '6px 8px', color: '#e2e8f0', font: '11px/1.35 ui-sans-serif, system-ui, sans-serif' }}>
              <div style={{ fontWeight: 700, color: heatColor(tip.score) }}>№{Number(tip.id.slice(3))} · {tip.kind_name} · {tip.score.toFixed(2)}</div>
              <div style={{ color: '#94a3b8', marginBottom: 2 }}>{tip.width_m !== null ? `ширина ${tip.width_m} м` : 'открытая улица'}{tip.lane_m !== null && tip.lane_m !== tip.width_m ? `, свободный проход ${tip.lane_m} м` : ''}, длина {tip.length_m} м</div>
              {tip.reasons.map((r, i) => <div key={i}>— {r}</div>)}
              <div style={{ color: '#64748b', marginTop: 2 }}>угрозы: {tip.tags.filter((t) => ['drop_ambush', 'window_fire', 'sniper', 'hiding'].includes(t)).map((t) => TAG_RU[t]).join(', ') || 'нет'}</div>
            </div>
          </foreignObject>
        );
      })()}
    </g>
  );
};
