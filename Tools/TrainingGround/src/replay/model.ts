/** Session v1: legacy snapshots/orders and task121 additive evidence. World cm, UE +X north/+Y east. */
export type Point = [number, number, number?];
export interface Unit { id: string; p: Point; yaw?: number; look_yaw?: number; alive?: number; hp?: number; cov?: boolean; in_cover?: boolean; stealth?: boolean; height?: number; arch?: string; lo?: string; sel?: boolean; lead?: boolean }
export interface SquadMember { id: string; role?: string; name?: string; display_name?: string; [key: string]: any }
export interface RecordRow { type: string; t?: number; ops?: Unit[]; threats?: Unit[]; [key: string]: any }
export interface Replay { header: RecordRow; footer: RecordRow; rows: RecordRow[]; frames: RecordRow[]; events: RecordRow[]; duration: number; warnings: string[] }
export const NOTE_TAGS = ['хорошо', 'ошибка', 'как надо', 'баг', 'прочее'] as const;
export type NoteTag = (typeof NOTE_TAGS)[number];
export interface Annotation { id: string; kind: 'comment' | 'route' | 'polyline' | 'arrow' | 'zone'; t: number; author: string; unit: string; label: string; color: string; points: Point[]; round?: number; height_cm?: number; tag?: NoteTag }
export interface Notes { schema: 'codex.demo_notes/1'; recording: string; map: string; coordinates: 'UE_cm_X_north_Y_east'; annotations: Annotation[] }
export function parseReplay(text: string): Replay {
  const warnings: string[] = [], rows: RecordRow[] = [];
  text.replace(/^\uFEFF/, '').split(/\r?\n/).forEach((line, i) => {
    if (!line.trim()) return;
    try { const row = JSON.parse(line); if (!row || typeof row.type !== 'string') throw Error(); rows.push(row); }
    catch { warnings.push(`Line ${i + 1}: invalid JSON record skipped`); }
  });
  const header = rows.find(r => r.type === 'header');
  if (!header) throw Error('Recording has no header');
  const footer = rows.find(r => r.type === 'footer') ?? { type: 'footer' };
  const timed = rows.filter(r => typeof r.t === 'number' && Number.isFinite(r.t) && r.t >= 0)
    .sort((a, b) => a.t! - b.t! || (a.type === 'round_start' ? -1 : a.type === 'round_end' ? 1 : 0) - (b.type === 'round_start' ? -1 : b.type === 'round_end' ? 1 : 0));
  // Footer duration can be wall time (legacy), so playback follows game time.
  const duration = Math.max(0, ...timed.map(r => r.t!));
  // Display a late mark on its target fight; preserve its actual call time separately.
  const marks = timed.filter(r => r.type === 'round_mark');
  const events = timed.filter(r => r.type !== 'snap' && r.type !== 'round_mark');
  for (const start of timed.filter(r => r.type === 'round_start')) {
    const own = marks.filter(m => m.round === start.round);
    if (own.length) {
      for (const mark of own) events.push({ ...mark, recorded_t: mark.t, t: start.t });
    } else if (!marks.length && footer.mark) {
      events.push({ type: 'round_mark', t: start.t, round: start.round, value: footer.mark });
    }
  }
  if (!timed.some(r => r.type === 'round_start')) events.push(...marks);
  events.sort((a, b) => a.t! - b.t!);
  return { header, footer, rows: timed, frames: timed.filter(r => Array.isArray(r.ops)), events, duration, warnings };
}
export function roundAt(replay: Replay, t: number): RecordRow | undefined {
  return replay.rows.filter(r => r.type === 'round_start' && r.t! <= t).at(-1);
}
export function frameAt(replay: Replay, t: number): RecordRow | undefined {
  const round = roundAt(replay, t);
  return replay.frames.filter(r => r.t! <= t && (!round || r.t! >= round.t!)).at(-1);
}
export function unitsAt(replay: Replay, t: number): Unit[] {
  const frame = frameAt(replay, t);
  if (!frame) return [];
  const units = [...(frame.ops ?? []), ...(frame.threats ?? []).map(u => ({ ...u, arch: u.arch ?? 'PMC' }))].map(u => ({ ...u }));
  // Never interpolate across rounds, deaths, respawns or absent visible threats.
  const nextRound = replay.rows.find(r => r.type === 'round_start' && r.t! > frame.t!);
  const next = replay.frames.find(r => r.t! > frame.t! && (!nextRound || r.t! < nextRound.t!));
  if (next && next.t! - frame.t! <= 2.5) {
    const alpha = Math.max(0, Math.min(1, (t - frame.t!) / (next.t! - frame.t!)));
    for (const u of units) {
      const v = [...(next.ops ?? []), ...(next.threats ?? [])].find(v => v.id === u.id);
      if (v && u.alive !== 0 && v.alive !== 0) u.p = [u.p[0] + (v.p[0] - u.p[0]) * alpha, u.p[1] + (v.p[1] - u.p[1]) * alpha, u.p[2]];
    }
  }
  for (const e of replay.events.filter(e => e.t! >= frame.t! && e.t! <= t)) {
    const u = units.find(u => u.id === (e.op ?? e.victim));
    if (!u) continue;
    if (e.type === 'kill') u.alive = 0;
    if (e.type.startsWith('stealth_')) u.stealth = e.type === 'stealth_on';
    if (e.type.startsWith('cover_')) u.in_cover = e.type === 'cover_enter';
    if (e.type === 'climb') u.height = e.height_cm;
  }
  return units;
}
export function eventUnits(e: RecordRow): [string | undefined, string | undefined] {
  return [e.who ?? e.killer ?? e.op ?? e.actor ?? undefined, e.whom ?? e.victim ?? undefined];
}
export const clockText = (t: number) => `${Math.floor(t / 60).toString().padStart(2, '0')}:${Math.floor(t % 60).toString().padStart(2, '0')}`;
/** Catmull-Rom sampled to cm: these exact displayed points are also decoded, no hidden curve. */
export function smoothRoute(points: Point[]): Point[] {
  if (points.length < 3) return points;
  const out: Point[] = [];
  for (let i = 0; i < points.length - 1; i++) {
    const a = points[Math.max(0, i - 1)], b = points[i], c = points[i + 1], d = points[Math.min(points.length - 1, i + 2)];
    for (let j = 0; j < 5; j++) {
      const t = j / 5;
      out.push([0, 1].map(k => Math.round(.5 * ((2 * b[k]!) + (-a[k]! + c[k]!) * t + (2 * a[k]! - 5 * b[k]! + 4 * c[k]! - d[k]!) * t * t + (-a[k]! + 3 * b[k]! - 3 * c[k]! + d[k]!) * t * t * t))) as Point);
    }
  }
  return [...out, points[points.length - 1]];
}
export function validNotes(value: unknown, recording: string, map: string): value is Notes {
  const n = value as Notes;
  return !!n && n.schema === 'codex.demo_notes/1' && n.recording === recording && n.map === map && n.coordinates === 'UE_cm_X_north_Y_east'
    && Array.isArray(n.annotations) && n.annotations.length <= 2000 && n.annotations.every(a => a && typeof a.id === 'string'
      && ['comment', 'route', 'polyline', 'arrow', 'zone'].includes(a.kind) && Number.isFinite(a.t) && a.t >= 0
      && ['author', 'unit', 'label', 'color'].every(k => typeof (a as any)[k] === 'string') && /^#[0-9a-f]{6}$/i.test(a.color)
      && (a.round === undefined || Number.isInteger(a.round)) && (a.height_cm === undefined || Number.isFinite(a.height_cm))
      && (a.tag === undefined || (NOTE_TAGS as readonly string[]).includes(a.tag))
      && Array.isArray(a.points) && a.points.length <= 20000 && a.points.every(p => Array.isArray(p) && p.length >= 2 && p.length <= 3 && p.every(Number.isFinite))
      && a.points.length >= (a.kind === 'comment' ? 0 : a.kind === 'zone' ? 2 : 2));
}

const ROLE_DISPLAY_NAMES: Record<string, string> = {
  Commander: 'Командир',
  Engineer: 'Инженер',
  MedicSapper: 'Медик-сапёр',
  Recruit: 'Новобранец',
};

const squadCache = new WeakMap<Replay, Map<string, SquadMember>>();

function squadMemberMap(replay: Replay | undefined): Map<string, SquadMember> {
  if (!replay) return new Map();
  const cached = squadCache.get(replay);
  if (cached) return cached;
  const map = new Map<string, SquadMember>();
  const squad: SquadMember[] = Array.isArray(replay.header.squad) ? replay.header.squad : [];
  for (const m of squad) if (m?.id) map.set(m.id, m);
  if (squad.length > 0) {
    const starts = replay.rows.filter(r => r.type === 'round_start').map(r => r.t!);
    const segments: [number, number][] = starts.length === 0
      ? [[0, Infinity]]
      : [[0, starts[0]], ...starts.map((s, i): [number, number] => [s, starts[i + 1] ?? Infinity])];
    for (const [t0, t1] of segments) {
      const first = replay.frames.find(f => f.t! >= t0 && f.t! < t1 && (f.ops?.length ?? 0) > 0);
      if (!first?.ops) continue;
      for (let i = 0; i < Math.min(first.ops.length, squad.length); i++) {
        const id = first.ops[i]?.id;
        if (id && !map.has(id)) map.set(id, squad[i]);
      }
    }
  }
  squadCache.set(replay, map);
  return map;
}

export function squadUnitIds(replay: Replay | undefined, t?: number): string[] {
  if (!replay) return [];
  if (t !== undefined) {
    const round = roundAt(replay, t);
    const nextRound = replay.rows.find(r => r.type === 'round_start' && r.t! > (round?.t ?? -1));
    const t0 = round?.t ?? 0;
    const t1 = nextRound?.t ?? Infinity;
    const first = replay.frames.find(f => f.t! >= t0 && f.t! < t1 && (f.ops?.length ?? 0) > 0);
    if (first?.ops?.length) return first.ops.map(u => u.id);
  }
  const squad: SquadMember[] = Array.isArray(replay.header.squad) ? replay.header.squad : [];
  const ids: string[] = [];
  for (const m of squad) if (m?.id && !ids.includes(m.id)) ids.push(m.id);
  return ids;
}

export function unitDisplayName(replay: Replay | undefined, id: string, unit?: Unit): string {
  if (!id) return 'Any / unassigned';
  const map = squadMemberMap(replay);
  const member = map.get(id);
  if (member) {
    const explicit = member.display_name || member.name;
    if (explicit) return explicit;
    const shortRole = member.role?.split('::').pop() || '';
    if (shortRole) {
      const base = ROLE_DISPLAY_NAMES[shortRole] ?? shortRole;
      const squad: SquadMember[] = Array.isArray(replay?.header.squad) ? replay!.header.squad : [];
      const sameRole = squad.filter(m => (m?.role?.split('::').pop() || '') === shortRole).length > 1;
      return sameRole ? `${base} (${id.split('_').pop()})` : base;
    }
  }
  const isThreat = unit?.arch !== undefined || (!member && replay?.frames.some(f => f.threats?.some(th => th.id === id)));
  if (isThreat) return `${unit?.arch ?? 'PMC'} ${id.split('_').pop()}`;
  return id;
}

export function selectedReplayUnit(replay: Replay | undefined, t: number): string {
  if (!replay) return '';
  const frame = frameAt(replay, t) ?? (!roundAt(replay, t) ? replay.frames[0] : undefined);
  const ops: Unit[] = frame?.ops ?? [];
  const selected = ops.filter(u => u.sel);
  if (selected.length > 0) return (selected.find(u => u.lead) ?? selected[0]).id;
  return '';
}

export function translatePoints(points: Point[], dx: number, dy: number): Point[] {
  const rdx = Math.round(dx), rdy = Math.round(dy);
  return points.map(p => (p.length > 2 && p[2] !== undefined ? [p[0] + rdx, p[1] + rdy, p[2]] : [p[0] + rdx, p[1] + rdy]));
}

export function deduplicateAnnotations(annotations: Annotation[]): { annotations: Annotation[]; removed: number } {
  const samePoints = (a: Point[], b: Point[]) =>
    a.length === b.length && a.every((p, i) => Math.hypot(p[0] - b[i][0], p[1] - b[i][1]) <= 5);
  const sameKey = (a: Annotation, b: Annotation) =>
    a.kind === b.kind && a.label.trim() === b.label.trim() && Math.abs(a.t - b.t) <= 2;

  const keptIndices = new Set<number>();
  const indexed = annotations.map((a, idx) => ({ a, idx }));

  const keptPointed: { a: Annotation; idx: number }[] = [];
  for (const item of indexed) {
    if (item.a.points.length === 0) continue;
    if (keptPointed.some(k => sameKey(k.a, item.a) && samePoints(k.a.points, item.a.points))) continue;
    keptPointed.push(item);
    keptIndices.add(item.idx);
  }

  const keptAbsent: { a: Annotation; idx: number }[] = [];
  for (const item of indexed) {
    if (item.a.points.length > 0) continue;
    if (keptPointed.some(k => sameKey(k.a, item.a))) continue;
    if (keptAbsent.some(k => sameKey(k.a, item.a))) continue;
    keptAbsent.push(item);
    keptIndices.add(item.idx);
  }

  const result = annotations.filter((_, idx) => keptIndices.has(idx));
  return { annotations: result, removed: annotations.length - result.length };
}
