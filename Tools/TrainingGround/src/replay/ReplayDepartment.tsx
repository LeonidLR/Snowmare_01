import React, { useEffect, useMemo, useRef, useState } from 'react';
import { clockText, eventUnits, frameAt, parseReplay, roundAt, smoothRoute, unitsAt, validNotes } from './model';
import type { Annotation, Notes, Point, Replay } from './model';
import { confirmLeave, setUnsaved } from '../utils/unsavedGuard';
import './replay.css';

const api = async (url: string, options?: RequestInit) => {
  const response = await fetch('/api/replay/' + url, options);
  if (!response.ok) throw Error((await response.json()).error || response.statusText);
  return response.json();
};
const xy = (p: Point) => `${p[1]},${-p[0]}`;
const emptyNotes = (file: string, map: string): Notes => ({ schema: 'codex.demo_notes/1', recording: file, map, coordinates: 'UE_cm_X_north_Y_east', annotations: [] });
type Tool = 'select' | Annotation['kind'];
export function ReplayDepartment() {
  const [listing, setListing] = useState<any>({ recordings: [] });
  const [file, setFile] = useState(''), [replay, setReplay] = useState<Replay>(), [geometry, setGeometry] = useState<any>();
  const [notes, setNotes] = useState<Notes>(), [history, setHistory] = useState<Notes[]>([]), [dirty, setDirty] = useState(false);
  const [status, setStatus] = useState('Loading recordings…'), [t, setT] = useState(0), [playing, setPlaying] = useState(false), [speed, setSpeed] = useState(1);
  const [tool, setTool] = useState<Tool>('select'), [draft, setDraft] = useState<Point[]>([]), [selected, setSelected] = useState('');
  const [label, setLabel] = useState(''), [author, setAuthor] = useState('Designer'), [unit, setUnit] = useState(''), [color, setColor] = useState('#facc15'), [height, setHeight] = useState('');
  const [sectorView, setSectorView] = useState(true), [cones, setCones] = useState(false), [zoom, setZoom] = useState(1), [pan, setPan] = useState<[number, number]>([0, 0]);
  const svg = useRef<SVGSVGElement>(null), dragging = useRef(false), handle = useRef<number | null>(null), gesture = useRef<Point[]>([]);
  const loadVersion = useRef(0);
  useEffect(() => { api('list').then(value => { setListing(value); const first = value.recordings.find((r: any) => !r.error && r.map === 'L_Gen_Depot_1_X6'); if (first) setFile(first.file); else setStatus(value.warning || 'No supported recordings'); }).catch(e => setStatus(String(e))); }, []);
  useEffect(() => {
    if (!file) return;
    const version = ++loadVersion.current;
    setPlaying(false); setReplay(undefined); setGeometry(undefined); setNotes(undefined); setT(0); setDraft([]); setSelected(''); setStatus('Loading recording…');
    (async () => {
      const response = await fetch('/api/replay/recording?file=' + encodeURIComponent(file));
      if (!response.ok) throw Error('Cannot read recording');
      const data = parseReplay(await response.text());
      const [map, stored] = await Promise.all([api('map?map=' + encodeURIComponent(data.header.map)), api('notes?file=' + encodeURIComponent(file))]);
      if (version !== loadVersion.current) return;
      if (stored && !validNotes(stored, file, data.header.map)) throw Error('Saved notes are invalid; file has been preserved');
      setReplay(data); setGeometry(map); setNotes(stored || emptyNotes(file, data.header.map)); setHistory([]); setDirty(false); setZoom(1); setPan([0, 0]);
      setStatus(data.warnings.length ? data.warnings.join('; ') : 'Recording loaded. Draw a route or pin a comment.');
    })().catch(e => { if (version === loadVersion.current) setStatus(String(e)); });
  }, [file]);
  useEffect(() => {
    setUnsaved('fight-replay', dirty ? 'Fight replay notes' : null);
    const unload = (e: BeforeUnloadEvent) => { if (dirty) { e.preventDefault(); e.returnValue = ''; } };
    window.addEventListener('beforeunload', unload);
    return () => { setUnsaved('fight-replay', null); window.removeEventListener('beforeunload', unload); };
  }, [dirty]);
  useEffect(() => {
    if (!playing || !replay) return;
    let previous = performance.now();
    const timer = window.setInterval(() => { const now = performance.now(); const dt = (now - previous) / 1000 * speed; previous = now; setT(old => { const next = Math.min(replay.duration, old + dt); if (next >= replay.duration) setPlaying(false); return next; }); }, 40);
    return () => clearInterval(timer);
  }, [playing, speed, replay]);
  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      if ((e.target as HTMLElement)?.closest('input,textarea,select,button')) return;
      if (e.code === 'Space') { e.preventDefault(); setPlaying(v => !v); }
      if (e.code === 'ArrowLeft' || e.code === 'ArrowRight') { e.preventDefault(); setPlaying(false); setT(v => Math.max(0, Math.min(replay?.duration || 0, v + (e.code === 'ArrowLeft' ? -1 : 1) * (e.shiftKey ? 10 : 1)))); }
      if (e.code === 'Escape') setDraft([]);
    };
    window.addEventListener('keydown', key); return () => window.removeEventListener('keydown', key);
  }, [replay]);
  const frame = replay && frameAt(replay, t), round = replay && roundAt(replay, t);
  const units = replay ? unitsAt(replay, t) : [];
  const events = useMemo(() => replay?.events.filter(e => e.t! <= t && e.t! >= (round?.t ?? 0)) || [], [replay, t, round]);
  const layout = geometry?.layout;
  const box = sectorView && replay?.header.sector_box_cm || layout?.bounds || { min: [-16000, -23000], max: [16000, 24000] };
  const w = (box.max[1] - box.min[1] + 800) / zoom, h = (box.max[0] - box.min[0] + 800) / zoom;
  const cx = (box.min[1] + box.max[1]) / 2 + pan[0], cy = -(box.min[0] + box.max[0]) / 2 + pan[1];
  const scale = Math.max(w, h) / 900, r = 6 * scale;
  const commit = (annotations: Annotation[]) => { if (!notes) return; setHistory(h => [...h.slice(-49), notes]); setNotes({ ...notes, annotations }); setDirty(true); };
  const chooseAnnotation = (a: Annotation) => { setSelected(a.id); setLabel(a.label); setUnit(a.unit); setColor(a.color); setAuthor(a.author); setHeight(a.height_cm === undefined ? '' : String(a.height_cm)); };
  const add = (points: Point[]) => {
    if (!notes || tool === 'select') return;
    if (tool !== 'comment' && points.length < 2) return;
    const a: Annotation = { id: crypto.randomUUID(), kind: tool, t, author, unit, label, color, points: tool === 'route' ? smoothRoute(points) : points, round: round?.round,
      ...(height !== '' && Number.isFinite(Number(height)) ? { height_cm: Number(height) } : {}) };
    commit([...notes.annotations, a]); setSelected(a.id); setDraft([]); setStatus('Annotation added — save notes to keep it.');
  };
  const point = (e: React.PointerEvent): Point => {
    const pt = new DOMPoint(e.clientX, e.clientY).matrixTransform(svg.current!.getScreenCTM()!.inverse());
    return [Math.round(-pt.y), Math.round(pt.x)];
  };
  const down = (e: React.PointerEvent<SVGSVGElement>) => {
    if (!notes) return;
    const p = point(e); setPlaying(false);
    if (tool === 'select') return;
    if (tool === 'comment') { add([p]); return; }
    if (tool === 'polyline') { setDraft(d => [...d, p]); return; }
    dragging.current = true; gesture.current = [p]; setDraft([p]); e.currentTarget.setPointerCapture(e.pointerId);
  };
  const move = (e: React.PointerEvent<SVGSVGElement>) => {
    if (!dragging.current) return;
    const p = point(e);
    if (handle.current !== null) {
      const a = notes?.annotations.find(a => a.id === selected);
      if (a) setDraft(a.points.map((v, i) => i === handle.current ? p : v));
    } else {
      const old = gesture.current;
      gesture.current = tool === 'route' ? [...old, p] : [old[0], p]; setDraft(gesture.current);
    }
  };
  const up = () => {
    if (!dragging.current) return;
    dragging.current = false;
    if (handle.current !== null) { commit(notes!.annotations.map(a => a.id === selected ? { ...a, points: draft } : a)); handle.current = null; setDraft([]); }
    else add(gesture.current);
  };
  const shape = (a: Pick<Annotation, 'kind' | 'points' | 'color'>, width = 3) => {
    const p = a.points;
    if (!p.length) return null;
    if (a.kind === 'zone' && p.length >= 2) return <circle cx={p[0][1]} cy={-p[0][0]} r={Math.hypot(p[1][0] - p[0][0], p[1][1] - p[0][1])} fill={a.color} fillOpacity=".10" stroke={a.color} strokeWidth={width * scale} />;
    if (a.kind === 'comment') return <circle cx={p[0][1]} cy={-p[0][0]} r={r} fill={a.color} />;
    return <><polyline points={p.map(xy).join(' ')} fill="none" stroke={a.color} strokeWidth={width * scale} strokeLinecap="round" strokeLinejoin="round" />{p.length > 1 && (() => { const end = p[p.length - 1], prev = p[p.length - 2], angle = Math.atan2(-(end[0] - prev[0]), end[1] - prev[1]) * 180 / Math.PI; return <path d={`M ${-r * 2} ${-r} L 0 0 L ${-r * 2} ${r}`} fill="none" stroke={a.color} strokeWidth={width * scale} transform={`translate(${end[1]},${-end[0]}) rotate(${angle})`} />; })()}</>;
  };
  const save = async () => {
    if (!notes) return;
    const version = loadVersion.current, saved = notes;
    try { const result = await api('notes?file=' + encodeURIComponent(file), { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(saved) });
      if (version === loadVersion.current) { setNotes(current => { if (current === saved) setDirty(false); return current; }); setStatus('Saved: ' + result.saved); }
    } catch (e) { setStatus(String(e)); }
  };
  return <main className="fight-replay">
    <header><div><h1>Хроника боя <span>Fight replay</span></h1><p>Recorded evidence · world centimetres · N = +X / E = +Y</p></div><button disabled={!notes || !dirty} onClick={save}>Save notes{dirty ? ' •' : ''}</button></header>
    <div className="replay-source"><label>Recording <select aria-label="Recording" value={file} onChange={e => { if (confirmLeave()) { setDirty(false); setFile(e.target.value); } }}>
      {listing.recordings.map((r: any) => <option key={r.file} value={r.file}>{r.file} · S{r.sector ?? '?'} · {r.rounds ?? 0} rounds · {r.result ?? r.error} · {clockText(r.duration || 0)}</option>)}</select></label><small title={listing.demos}>Source: {listing.demos}</small></div>
    <div className="replay-workspace"><section className="replay-stage">
      <div className="replay-map-tools"><button onClick={() => { setSectorView(v => !v); setZoom(1); setPan([0, 0]); }}>{sectorView ? 'Whole map' : 'Sector view'}</button><button onClick={() => setZoom(v => Math.min(8, v * 1.4))}>Zoom +</button><button onClick={() => setZoom(v => Math.max(.5, v / 1.4))}>Zoom −</button><button onClick={() => setPan(v => [v[0] - w / 4, v[1]])}>←</button><button onClick={() => setPan(v => [v[0], v[1] - h / 4])}>↑</button><button onClick={() => setPan(v => [v[0], v[1] + h / 4])}>↓</button><button onClick={() => setPan(v => [v[0] + w / 4, v[1]])}>→</button><label><input type="checkbox" checked={cones} onChange={e => setCones(e.target.checked)} /> Facing cones</label></div>
      <svg ref={svg} aria-label="Fight replay map" className="replay-map" viewBox={`${cx - w / 2} ${cy - h / 2} ${w} ${h}`} onPointerDown={down} onPointerMove={move} onPointerUp={up} onPointerCancel={() => { dragging.current = false; handle.current = null; setDraft([]); }}>
        <rect x={cx - w / 2} y={cy - h / 2} width={w} height={h} fill="#101f2c" />
        {geometry?.sectors.map((s: any) => <g key={s.id}><rect x={s.box_cm.min[1]} y={-s.box_cm.max[0]} width={s.box_cm.max[1] - s.box_cm.min[1]} height={s.box_cm.max[0] - s.box_cm.min[0]} fill="none" stroke="#38bdf8" strokeOpacity=".5" strokeDasharray={`${r * 2} ${r}`} strokeWidth={scale} /><text x={s.box_cm.min[1] + r} y={-s.box_cm.max[0] + r * 3} fill="#7dd3fc" fontSize={16 * scale}>SECTOR {s.id}</text></g>)}
        {layout?.buildings.map((b: any) => { const [a, z] = b.footprint; const n = Object.entries(geometry.index).find(([, id]) => id === b.id)?.[0]; return <g key={b.id}><rect x={a[1]} y={-z[0]} width={z[1] - a[1]} height={z[0] - a[0]} fill="#293e4f" stroke="#7890a3" strokeWidth={scale * 1.4} transform={`rotate(${b.rotation || 0},${(a[1] + z[1]) / 2},${-(a[0] + z[0]) / 2})`} /><text x={(a[1] + z[1]) / 2} y={-(a[0] + z[0]) / 2} fill="#dce9f3" fontSize={14 * scale} textAnchor="middle" paintOrder="stroke" stroke="#101f2c" strokeWidth={3 * scale}>{n}<title>{b.id} · {b.height} cm</title></text>{b.stairs?.map((s: any) => <line key={s.id} x1={s.pos[1]} y1={-s.pos[0]} x2={s.top?.[1] ?? s.pos[1]} y2={-(s.top?.[0] ?? s.pos[0] + 150)} stroke="#c084fc" strokeWidth={4 * scale} />)}</g>; })}
        {layout?.walls.map((wall: any) => <line key={wall.id} x1={wall.a[1]} y1={-wall.a[0]} x2={wall.b[1]} y2={-wall.b[0]} stroke={wall.kind?.includes('fence') ? '#9ca3af' : '#61788b'} strokeWidth={Math.max(scale, Math.min(35, wall.thickness || 15))} strokeDasharray={wall.kind?.includes('fence') ? `${r} ${r / 2}` : undefined} />)}
        {layout?.covers.map((c: any) => <rect key={c.id} x={c.pos[1] - (c.size?.[1] || 100) / 2} y={-c.pos[0] - (c.size?.[0] || 100) / 2} width={c.size?.[1] || 100} height={c.size?.[0] || 100} fill={c.size?.[2] <= 150 ? '#8b8153' : '#56635a'} opacity=".85" transform={`rotate(${c.yaw || 0},${c.pos[1]},${-c.pos[0]})`}><title>{c.id} · {c.kind}</title></rect>)}
        {layout?.ladders.map((l: any) => <line key={l.id} x1={(l.foot || l.pos)[1]} y1={-(l.foot || l.pos)[0]} x2={(l.top || l.pos)[1]} y2={-(l.top || l.pos)[0]} stroke="#c084fc" strokeWidth={4 * scale} />)}
        {events.filter(e => e.type === 'order' && e.ok && e.dest && t - e.t! < 12).map((e, i) => { const u = e.ops?.find(u => u.id === e.actor); return u && <line key={'o' + i} x1={u.p[1]} y1={-u.p[0]} x2={e.dest[1]} y2={-e.dest[0]} stroke="#67e8f9" opacity=".25" strokeWidth={2 * scale} />; })}
        {units.map((u, i) => { const yaw = (u.look_yaw ?? u.yaw ?? 0) * Math.PI / 180, friendly = frame?.ops?.some(v => v.id === u.id), name = friendly ? (replay?.header.squad?.[i]?.role?.split('::').pop() || u.id) : 'PMC ' + u.id.split('_').pop(); return <g key={u.id} opacity={u.alive === 0 ? .4 : 1}>
          {cones && (u.look_yaw !== undefined || u.yaw !== undefined) && <path d={`M ${u.p[1]} ${-u.p[0]} l ${Math.sin(yaw - .6) * r * 14} ${-Math.cos(yaw - .6) * r * 14} A ${r * 14} ${r * 14} 0 0 1 ${u.p[1] + Math.sin(yaw + .6) * r * 14} ${-u.p[0] - Math.cos(yaw + .6) * r * 14} Z`} fill={friendly ? '#22d3ee' : '#fb7185'} opacity=".08" />}
          <circle cx={u.p[1]} cy={-u.p[0]} r={r} fill={friendly ? '#22d3ee' : '#fb7185'} stroke={u.in_cover ?? u.cov ? '#facc15' : '#0f172a'} strokeWidth={2 * scale} />
          {(u.look_yaw !== undefined || u.yaw !== undefined) && <line x1={u.p[1]} y1={-u.p[0]} x2={u.p[1] + Math.sin(yaw) * r * 2.5} y2={-u.p[0] - Math.cos(yaw) * r * 2.5} stroke="white" strokeWidth={2 * scale} />}
          <text x={u.p[1] + r * 1.5} y={-u.p[0] - r} fill={friendly ? '#a5f3fc' : '#fda4af'} fontSize={11 * scale} paintOrder="stroke" stroke="#101f2c" strokeWidth={3 * scale}>{name} {u.alive === 0 ? '✕' : ''} {u.stealth ? 'S' : ''} {u.height === undefined ? `z=${u.p[2] ?? '?'}cm` : `↑${Math.round(u.height)}cm`}</text>
        </g>; })}
        {events.filter(e => ['shot_hit', 'damage', 'kill', 'down'].includes(e.type) && t - e.t! < 1.5).map((e, i) => { const [a, b] = eventUnits(e), from = units.find(u => u.id === a), to = units.find(u => u.id === b); return <g key={'hit' + i}>{from && to && <line x1={from.p[1]} y1={-from.p[0]} x2={to.p[1]} y2={-to.p[0]} stroke="#fb923c" strokeWidth={3 * scale} />}{to && <circle cx={to.p[1]} cy={-to.p[0]} r={r * 2} fill="none" stroke="#f87171" strokeWidth={3 * scale} />}</g>; })}
        {notes?.annotations.map(a => <g key={a.id} opacity={a.id === selected ? 1 : .65} onPointerDown={e => { if (tool === 'select') { e.stopPropagation(); chooseAnnotation(a); } }} style={{ cursor: tool === 'select' ? 'pointer' : undefined }}>{shape(a, a.id === selected ? 4 : 3)}{a.points[0] && <text x={a.points[0][1] + r} y={-a.points[0][0] - r * 2} fontSize={12 * scale} fill={a.color} paintOrder="stroke" stroke="#101f2c" strokeWidth={3 * scale}>{clockText(a.t)} {a.label || a.kind}</text>}
          {tool === 'select' && a.id === selected && a.points.map((p, i) => (i % Math.max(1, Math.floor(a.points.length / 25)) === 0 || i === a.points.length - 1) && <circle key={i} cx={p[1]} cy={-p[0]} r={r * .7} fill="white" stroke={a.color} strokeWidth={scale} onPointerDown={e => { e.stopPropagation(); handle.current = i; dragging.current = true; setDraft(a.points); svg.current!.setPointerCapture(e.pointerId); }} />)}</g>)}
        {draft.length > 0 && shape({ kind: tool === 'select' ? notes?.annotations.find(a => a.id === selected)?.kind || 'route' : tool, points: draft, color })}
      </svg>
      <div className="replay-legend">↑ N · → E &nbsp; <b>● Squad</b> <em>● PMC (visible only)</em> · gold = low cover / cover state · purple = ladder / stairs · numbered buildings</div>
      <div className="replay-transport"><button disabled={!replay} onClick={() => { if (t >= (replay?.duration || 0)) setT(0); setPlaying(v => !v); }}>{playing ? 'Pause' : 'Play'}</button>{[1, 2, 4].map(v => <button key={v} aria-pressed={speed === v} onClick={() => setSpeed(v)}>×{v}</button>)}<strong>{clockText(t)} / {clockText(replay?.duration || 0)}</strong><label>Time (s)<input aria-label="Go to second" style={{ maxWidth: 90 }} type="number" min="0" max={replay?.duration || 0} value={Math.round(t * 10) / 10} onChange={e => { setT(Math.max(0, Math.min(replay?.duration || 0, Number(e.target.value)))); setPlaying(false); }} /></label><span>Round {round?.round ?? '—'} · {frame?.mode || '—'} · {replay?.footer.result || '—'}</span></div>
      <div className="replay-timeline"><svg viewBox="0 0 1000 15" preserveAspectRatio="none" aria-label="Event ticks">{replay?.events.map((e, i) => <rect key={i} x={e.t! / (replay.duration || 1) * 998} width={e.type.startsWith('round_') ? 3 : 1} height={e.type === 'order' ? 5 : 15} fill={e.type === 'kill' ? '#f87171' : e.type.startsWith('round_') ? '#c084fc' : '#38bdf8'}><title>{clockText(e.t!)} {e.type} {e.type === "round_mark" ? `R${e.round}: ${e.value}` : ""}</title></rect>)}</svg><input aria-label="Replay time" type="range" min="0" max={replay?.duration || 1} step=".1" value={t} onChange={e => { setT(Number(e.target.value)); setPlaying(false); }} /></div>
      <p className="replay-help">Space: play/pause · ←/→: 1 second · Shift: 10 seconds. Orange hit lines use snapshot positions; events without both actors stay in the feed. Legacy recordings have no confirmed shot/kill/stealth events.</p>
    </section><aside>
      <h2>Designer annotations</h2><div className="replay-tools">{(['select', 'comment', 'route', 'polyline', 'arrow', 'zone'] as Tool[]).map(k => <button key={k} aria-pressed={tool === k} onClick={() => { setTool(k); setDraft([]); }}>{k}</button>)}</div>
      <p className="replay-help">Route: drag a smooth path. Polyline: click vertices, then Finish. Arrow / zone: drag. Select: click a drawing, drag white handles to edit. Zones use centre + radius.</p>
      <label>Label / comment<textarea aria-label="Annotation label" value={label} onChange={e => setLabel(e.target.value)} /></label>
      <div className="replay-fields"><label>Author<input value={author} onChange={e => setAuthor(e.target.value)} /></label><label>Colour<input aria-label="Annotation colour" type="color" value={color} onChange={e => setColor(e.target.value)} /></label></div>
      <label>Who<select aria-label="Annotation unit" value={unit} onChange={e => setUnit(e.target.value)}><option value="">Any / unassigned</option>{[...new Set([...units.map(u => u.id), ...notes?.annotations.map(a => a.unit).filter(Boolean) || []])].map(id => <option key={id} value={id}>{id}</option>)}</select></label>
      <label>Height above ground (cm, optional)<input type="number" value={height} onChange={e => setHeight(e.target.value)} placeholder="Explicit roof evidence for decoder" /></label>
      <div className="replay-tools">{tool === 'polyline' && <button disabled={draft.length < 2} onClick={() => add(draft)}>Finish polyline</button>}{tool === 'comment' && <button onClick={() => add([])}>Pin to time only</button>}<button disabled={!history.length} onClick={() => { setNotes(history[history.length - 1]); setHistory(v => v.slice(0, -1)); setDirty(true); setSelected(''); }}>Undo</button>
        <button disabled={!selected} onClick={() => commit(notes!.annotations.map(a => a.id === selected ? { ...a, label, author, unit, color, height_cm: height === '' ? undefined : Number(height) } : a))}>Apply edits</button><button disabled={!selected} onClick={() => { commit(notes!.annotations.filter(a => a.id !== selected)); setSelected(''); }}>Delete</button></div>
      <div className="replay-annotations">{notes?.annotations.map(a => <button key={a.id} aria-pressed={selected === a.id} onClick={() => { chooseAnnotation(a); setT(a.t); setPlaying(false); setTool('select'); }}>{clockText(a.t)} · R{a.round ?? '—'} · {a.kind} · {a.label || '(no label)'}</button>)}</div>
      <h2>Recorded events</h2><div className="replay-events">{events.slice(-35).reverse().map((e, i) => <button key={i} onClick={() => { setT(e.t!); setPlaying(false); }}><time>{clockText(e.t!)}</time> {e.type} {e.type === 'round_mark' ? `R${e.round}: ${e.value}` : e.kind || e.result || ''}<small>{eventUnits(e).filter(Boolean).join(' → ')} {e.dmg === undefined ? '' : ` · ${e.dmg} damage`}</small></button>)}</div>
    </aside></div><footer role="status">{status} {replay && ` · ${replay.frames.length} frames · ${replay.events.length} events`}</footer>
  </main>;
}
