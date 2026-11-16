// Training Ground / level generator v2 - designer UX round (task 072, item 5): the SQUAD PLAYBACK after «Прогнать отряд
// (симулятор)». The evaluator's simulator walk (evaluate.ts, the formation sim on the mandatory route's ground legs) records the
// members' positions at levelgen_v2.json playback.sample_hz (createTrackRecorder); the page plays the track on the 2D plan
// (positionsAt + trailAt; play / pause / speed x1-x8 / a time scrubber) and shows its summary (summarizeTrack). The legs are
// played back to back on one clock: a leg the simulator does not walk (a roof, a ladder, a floor) is a jump between two legs.
// Pure TS (no DOM).
import cfg from '../../../../Content/Data/LevelGen/levelgen_v2.json' with { type: 'json' };
import type { Vec2 } from '../movement/geometry.ts';

export const PLAYBACK = cfg.playback;

/** One sample: the time on the track clock (s), the leg index, every member's position (UE cm, the leader first), the formation. */
export interface TrackFrame { t: number; leg: number; pos: Vec2[]; formation: string }
export interface TrackLeg { id: string; start_s: number; end_s: number }
/** The sampled squad trajectory of one simulator walk (codex.levelgen_track/1, kept in the page, not saved with the map). */
export interface SquadTrack { schema: 'codex.levelgen_track/1'; hz: number; members: number; frames: TrackFrame[]; legs: TrackLeg[]; duration_s: number }

/** The simulator's frame as the recorder needs it (movement/sim.ts Frame). */
export interface SimFrameLike { t: number; formation: string; members: { pos: Vec2 }[] }

/**
 * A recorder for consecutive simulator runs (one per route leg): beginLeg(id) -> frame(f) for every sim frame -> endLeg(seconds).
 * A frame is kept when it starts a new 1/hz slot of the leg (and the leg's last frame), with the legs laid on one clock.
 */
export function createTrackRecorder(hz = PLAYBACK.sample_hz) {
  const frames: TrackFrame[] = [];
  const legs: TrackLeg[] = [];
  let offset = 0, slot = -1, last: TrackFrame | null = null, members = 0;
  const round = (v: number) => Math.round(v);
  return {
    beginLeg(id: string): void { legs.push({ id, start_s: offset, end_s: offset }); slot = -1; last = null; },
    frame(f: SimFrameLike): void {
      const leg = legs.length - 1;
      if (leg < 0) throw new Error('beginLeg first');
      const s = Math.floor(f.t * hz + 1e-9);
      const fr: TrackFrame = { t: Math.round((offset + f.t) * 1000) / 1000, leg, pos: f.members.map((m) => [round(m.pos[0]), round(m.pos[1])] as Vec2), formation: f.formation };
      members = Math.max(members, fr.pos.length);
      if (s !== slot) { frames.push(fr); slot = s; last = null; } else last = fr;
    },
    endLeg(seconds: number): void {
      if (last) frames.push(last);
      last = null;
      offset += seconds;
      const l = legs[legs.length - 1];
      if (l) l.end_s = Math.round(offset * 1000) / 1000;
    },
    track(): SquadTrack { return { schema: 'codex.levelgen_track/1', hz, members, frames: [...frames], legs: legs.map((l) => ({ ...l })), duration_s: Math.round(offset * 1000) / 1000 }; },
  };
}

/** The index of the last frame with t <= time (binary search); -1 before the first. */
function frameIndex(track: SquadTrack, time: number): number {
  let lo = 0, hi = track.frames.length - 1, ans = -1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (track.frames[mid].t <= time) { ans = mid; lo = mid + 1; } else hi = mid - 1;
  }
  return ans;
}

/** The squad at `time` (s, clamped to the track): positions interpolated between two samples of the same leg. */
export function positionsAt(track: SquadTrack, time: number): { members: Vec2[]; leg: number; formation: string; t: number } | null {
  if (!track.frames.length) return null;
  const t = Math.max(0, Math.min(track.duration_s, time));
  let i = frameIndex(track, t);
  if (i < 0) i = 0;
  const a = track.frames[i], b = track.frames[i + 1];
  if (!b || b.leg !== a.leg || b.t <= a.t || t <= a.t) return { members: a.pos.map((p) => [p[0], p[1]] as Vec2), leg: a.leg, formation: a.formation, t };
  const k = Math.min(1, (t - a.t) / (b.t - a.t));
  return { members: a.pos.map((p, m) => { const q = b.pos[m] ?? p; return [p[0] + (q[0] - p[0]) * k, p[1] + (q[1] - p[1]) * k] as Vec2; }), leg: a.leg, formation: a.formation, t };
}

/** The leader's positions over the last `seconds` before `time`, within the current leg (the trail on the plan). */
export function trailAt(track: SquadTrack, time: number, seconds = PLAYBACK.trail_s): Vec2[] {
  const now = positionsAt(track, time);
  if (!now) return [];
  const i = frameIndex(track, now.t);
  const out: Vec2[] = [];
  for (let k = Math.max(0, i); k >= 0; k--) {
    const f = track.frames[k];
    if (f.leg !== now.leg || f.t < now.t - seconds) break;
    out.unshift(f.pos[0]);
  }
  out.push(now.members[0]);
  return out;
}

/** The playback clock: `t` advanced by `dtReal` seconds of wall time at `speed`, clamped to the track; playing stops at the end. */
export function advanceClock(t: number, dtReal: number, speed: number, duration: number): { t: number; ended: boolean } {
  const next = Math.min(duration, Math.max(0, t + Math.max(0, dtReal) * speed));
  return { t: next, ended: next >= duration };
}

/** The summary the page shows next to the player: duration, the leader's distance, legs, formation shares. */
export function summarizeTrack(track: SquadTrack): { duration_s: number; leader_m: number; legs: number; members: number; formations: Record<string, number> } {
  let d = 0;
  const count = new Map<string, number>();
  for (let i = 0; i < track.frames.length; i++) {
    const f = track.frames[i], p = track.frames[i - 1];
    if (p && p.leg === f.leg) d += Math.hypot(f.pos[0][0] - p.pos[0][0], f.pos[0][1] - p.pos[0][1]);
    count.set(f.formation, (count.get(f.formation) ?? 0) + 1);
  }
  const n = Math.max(1, track.frames.length);
  return { duration_s: Math.round(track.duration_s * 10) / 10, leader_m: Math.round(d / 10) / 10, legs: track.legs.length, members: track.members,
    formations: Object.fromEntries([...count].sort((a, b) => b[1] - a[1]).map(([k, v]) => [k, Math.round((v / n) * 1000) / 1000])) };
}

/** "m:ss" for the scrubber. */
export const clockLabel = (s: number): string => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, '0')}`;
