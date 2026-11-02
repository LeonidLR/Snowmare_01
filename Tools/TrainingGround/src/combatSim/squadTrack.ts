// Training Ground / combat simulator: the SQUAD TRACK (docs/design/levelgen/COMBAT_SIM.md "The squad"). The squad's walk along the
// layout's mandatory route is independent of the enemies (the bot's route mode walks it in formation and only PAUSES for fights), so it is
// computed ONCE per layout by the real formation simulator (src/movement/sim.ts: the lab's rules, the sector looks, the wall layouts) on
// the ground legs, with a simple column walker on the roof / floor legs, the links (stairs, drops) and the return to the start, and sampled
// at 10 Hz: every member's position, body yaw and the look the rules asked of him (the AmbushWatchRules count both), the formation label,
// the layer / height level. A combat run then replays the track frame by frame - fast and deterministic. Pure.

import type { FormationConfig } from '../movement/config.ts';
import { cfg } from '../movement/config.ts';
import type { Vec2 } from '../movement/geometry.ts';
import { add, dist, normYaw, scale, sub, yawOf } from '../movement/geometry.ts';
import type { LayoutRoute } from '../movement/layout.ts';
import { simulate } from '../movement/sim.ts';
import { layerLevel, mandatoryRoute } from '../levelgen/schema.ts';
import type { SimWorld } from './world.ts';

export const TRACK_SCHEMA = 'codex.combat_sim_track/1';
export const TRACK_HZ = 10;

export interface TrackMember { pos: Vec2; bodyYaw: number; lookYaw: number }

export interface TrackFrame {
  /** Time since the track's start, s. */
  t: number;
  members: TrackMember[];
  /** The formation label as the combat lab report names it (wall_layout, column, echelon_left, wedge, diamond, ...). */
  formation: string;
  terrain: string;
  layer: string;
  level: number;
  heading: number;
  /** The mandatory route point the leader last reached (0 = the start; the return leg counts as the last + 1). */
  waypoint: number;
  /** The squad stands in the wall layout. */
  atWall: boolean;
}

export interface SquadTrack {
  schema: typeof TRACK_SCHEMA;
  layout: string;
  members: number;
  hz: number;
  frames: TrackFrame[];
  /** The lap: the route walked once + the return to its start, s / m. */
  lapSeconds: number;
  lapMeters: number;
  routeMeters: number;
  /** How the track was made per leg (for the report / debugging). */
  legs: { from: number; to: number; kind: 'sim' | 'walk' | 'link' | 'drop' | 'return'; layer: string; seconds: number }[];
}

export interface TrackOptions {
  members?: number;
  simSeed?: number;
  /** Followers' spacing in the simple column walker, cm. */
  columnSpacingCm?: number;
  /** Seconds a hole drop takes. */
  dropSeconds?: number;
  /** The hold at the route's end (the bot's wall action), s. */
  endHoldSeconds?: number;
}

const FORMATION_LABEL: Record<string, string> = { wall: 'wall_layout' };
const labelOf = (f: string, members: number): string => FORMATION_LABEL[f] ?? (f === 'diamond' && members === 3 ? 'wedge' : f);

/** Builds the track of a layout's mandatory route (the ground legs by the formation simulator). */
export function buildSquadTrack(world: SimWorld, config: FormationConfig, o: TrackOptions = {}): SquadTrack {
  const layout = world.layout;
  const members = o.members ?? 3;
  const route = mandatoryRoute(layout);
  const layers = route.layers ?? route.points.map(() => 'ground');
  const spacing = o.columnSpacingCm ?? cfg(config, 'slot.back_offset');
  const pace = cfg(config, 'pace.walk_speed') * cfg(config, 'pace.patrol');
  const frames: TrackFrame[] = [];
  const legs: SquadTrack['legs'] = [];
  const dt = 1 / TRACK_HZ;
  let t = 0;
  const push = (f: Omit<TrackFrame, 't'>) => { frames.push({ t, ...f }); t += dt; };

  // The simple column walker along a polyline on one layer (roof / floor legs, links, the return): the leader at the pace, the followers
  // on his trail, the rear man looking back, the middle men to the sides.
  const walk = (pts: Vec2[], layer: string, waypointAt: (s: number) => number, kind: SquadTrack['legs'][number]['kind'], from: number, to: number) => {
    const level = layerLevel(layout, layer);
    const cum: number[] = [0];
    for (let i = 1; i < pts.length; i++) cum.push(cum[i - 1] + dist(pts[i - 1], pts[i]));
    const total = cum[cum.length - 1];
    const at = (s: number): { pos: Vec2; yaw: number } => {
      const sc = Math.min(total, Math.max(0, s));
      let i = 1;
      while (i < cum.length - 1 && cum[i] < sc) i++;
      const a = pts[i - 1], b = pts[i], L = cum[i] - cum[i - 1];
      const k = L > 1e-6 ? (sc - cum[i - 1]) / L : 0;
      return { pos: add(a, scale(sub(b, a), k)), yaw: L > 1e-6 ? yawOf(sub(b, a)) : 0 };
    };
    const t0 = t;
    const steps = Math.max(1, Math.ceil(total / pace / dt));
    for (let k = 0; k <= steps; k++) {
      const s = Math.min(total, (k * dt) * pace);
      const lead = at(s);
      const ms: TrackMember[] = [];
      for (let m = 0; m < members; m++) {
        const p = at(s - m * spacing);
        const heading = lead.yaw;
        const look = m === 0 ? heading : m === members - 1 ? normYaw(heading + 180) : normYaw(heading + (m % 2 ? 90 : -90));
        ms.push({ pos: p.pos, bodyYaw: m === members - 1 && members > 1 ? look : (m === 0 ? heading : p.yaw), lookYaw: look });
      }
      push({ members: ms, formation: 'column', terrain: layer === 'ground' ? 'open' : layer, layer, level, heading: lead.yaw, waypoint: waypointAt(s), atWall: false });
    }
    legs.push({ from, to, kind, layer, seconds: Math.round((t - t0) * 10) / 10 });
  };

  // The standing squad (a link's wait: a drop, a ladder queue): the last frame held.
  const hold = (seconds: number, waypoint: number, layer: string, kind: SquadTrack['legs'][number]['kind'], from: number, to: number) => {
    const last = frames[frames.length - 1];
    const t0 = t;
    const n = Math.max(1, Math.round(seconds / dt));
    for (let k = 0; k < n; k++) push({ ...last, members: last.members.map((m) => ({ ...m })), waypoint, layer, level: layerLevel(layout, layer) });
    legs.push({ from, to, kind, layer, seconds: Math.round((t - t0) * 10) / 10 });
  };

  // The formation simulator over a run of consecutive ground points.
  const simRun = (idx: number[]) => {
    const pts = idx.map((i) => route.points[i]);
    const leg: LayoutRoute = { id: `${route.id}_${idx[0]}_${idx[idx.length - 1]}`, pace: 'walk', points: pts };
    const cum: number[] = [0];
    for (let i = 1; i < pts.length; i++) cum.push(cum[i - 1] + dist(pts[i - 1], pts[i]));
    const t0 = t;
    let every = 0;
    const simDt = 1 / 30;
    const keep = Math.max(1, Math.round(1 / simDt / TRACK_HZ));
    let wp = idx[0];
    simulate({
      layout: world.ground, route: leg, memberCount: members, config, seed: o.simSeed ?? 1, nav: world.groundNav, haltSeconds: idx[idx.length - 1] === route.points.length - 1 ? (o.endHoldSeconds ?? 3) : 0.5,
      onFrame: (f) => {
        if (every++ % keep !== 0) return;
        // the route point reached: the leader within 2.5 m of the next point in order (the bot's reach radius)
        const leaderPos = f.members[0].pos;
        for (let k = wp - idx[0] + 1; k < pts.length; k++) if (dist(leaderPos, pts[k]) <= 250) wp = idx[k];
        push({
          members: f.members.map((m) => ({ pos: m.pos, bodyYaw: m.yaw, lookYaw: m.lookYaw ?? m.yaw })),
          formation: labelOf(f.formation, members), terrain: f.terrain, layer: 'ground', level: 0, heading: f.heading, waypoint: wp, atWall: f.formation === 'wall',
        });
      },
    });
    legs.push({ from: idx[0], to: idx[idx.length - 1], kind: 'sim', layer: 'ground', seconds: Math.round((t - t0) * 10) / 10 });
  };

  // ---- the route, leg by leg --------------------------------------------------------------------------------------------------------
  const linkAt = new Map((route.links ?? []).map((k) => [k.at, k.via]));
  let i = 0;
  while (i < route.points.length - 1) {
    const layer = layers[i];
    // a run of same-layer points
    let j = i + 1;
    while (j < route.points.length && layers[j] === layer && !linkAt.has(j)) j++;
    const run = Array.from({ length: j - i }, (_, k) => i + k);
    if (run.length >= 2) {
      if (layer === 'ground') simRun(run);
      else walk(run.map((k) => route.points[k]), layer, (s) => { let w = run[0]; let acc = 0; for (let k = 1; k < run.length; k++) { acc += dist(route.points[run[k - 1]], route.points[run[k]]); if (s >= acc - 250) w = run[k]; } return w; }, 'walk', run[0], run[run.length - 1]);
    } else if (frames.length === 0) {
      // a route that starts with a link: stand at the first point
      const level = layerLevel(layout, layer);
      push({ members: Array.from({ length: members }, (_, m) => ({ pos: add(route.points[i], [-(m * spacing), 0]), bodyYaw: 0, lookYaw: m === members - 1 ? 180 : 0 })), formation: 'column', terrain: 'open', layer, level, heading: 0, waypoint: i, atWall: false });
    }
    i = j - 1;
    if (i >= route.points.length - 1) break;
    // the link leg i -> i + 1 (a layer change)
    const via = linkAt.get(i + 1) ?? '';
    const to = layers[i + 1];
    if (/^hole_/.test(via)) hold(o.dropSeconds ?? 2.8, i + 1, to, 'drop', i, i + 1);
    else {
      // a stair / ladder / crossing: walked at the pace along its run (the leader first; the followers queue behind)
      const pts: Vec2[] = [route.points[i], route.points[i + 1]];
      const ladder = layout.ladders.find((l) => l.id === via);
      const seconds = ladder ? ladder.rungs * 0.5 * members + 2 : 0;
      if (seconds > 0) hold(seconds, i + 1, to, 'link', i, i + 1);
      walk(pts, to, () => i + 1, 'link', i, i + 1);
    }
    i = i + 1;
  }
  const routeMeters = route.points.slice(1).reduce((n, p, k) => n + dist(route.points[k], p), 0) / 100;
  // ---- the return to the start (the loop): the ground path from the end --------------------------------------------------------------
  const end = route.points[route.points.length - 1];
  const start = route.points[0];
  const back = world.groundNav.path(end, start) ?? [end, start];
  const lastIndex = route.points.length;
  walk(back, 'ground', () => lastIndex, 'return', route.points.length - 1, 0);
  const lapMeters = routeMeters + back.slice(1).reduce((n, p, k) => n + dist(back[k], p), 0) / 100;
  return { schema: TRACK_SCHEMA, layout: layout.name, members, hz: TRACK_HZ, frames, lapSeconds: Math.round(t * 10) / 10, lapMeters: Math.round(lapMeters), routeMeters: Math.round(routeMeters), legs };
}

/** The leader's heading at a frame (the pressure's SquadHeading: his move direction when he walks, else his body yaw). */
export function frameHeading(track: SquadTrack, index: number): number {
  const f = track.frames[index];
  const prev = track.frames[Math.max(0, index - 1)];
  const d = sub(f.members[0].pos, prev.members[0].pos);
  return Math.hypot(d[0], d[1]) > 5 ? yawOf(d) : f.members[0].bodyYaw;
}
