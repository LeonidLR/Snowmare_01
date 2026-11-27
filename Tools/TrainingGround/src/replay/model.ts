/** Session v1: legacy snapshots/orders and task121 additive evidence. World cm, UE +X north/+Y east. */
export type Point = [number, number, number?];
export interface Unit { id: string; p: Point; yaw?: number; look_yaw?: number; alive?: number; hp?: number; cov?: boolean; in_cover?: boolean; stealth?: boolean; height?: number; arch?: string; lo?: string }
export interface RecordRow { type: string; t?: number; ops?: Unit[]; threats?: Unit[]; [key: string]: any }
export interface Replay { header: RecordRow; footer: RecordRow; rows: RecordRow[]; frames: RecordRow[]; events: RecordRow[]; duration: number; warnings: string[] }
export interface Annotation { id: string; kind: 'comment' | 'route' | 'polyline' | 'arrow' | 'zone'; t: number; author: string; unit: string; label: string; color: string; points: Point[]; round?: number; height_cm?: number }
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
  return { header, footer, rows: timed, frames: timed.filter(r => Array.isArray(r.ops)), events: timed.filter(r => r.type !== 'snap'), duration, warnings };
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
      && Array.isArray(a.points) && a.points.length <= 20000 && a.points.every(p => Array.isArray(p) && p.length >= 2 && p.length <= 3 && p.every(Number.isFinite))
      && a.points.length >= (a.kind === 'comment' ? 0 : a.kind === 'zone' ? 2 : 2));
}
