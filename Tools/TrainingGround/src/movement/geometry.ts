// Training Ground / movement: 2D geometry in UE conventions (docs/design/FORMATION_CONTRACT.md "Units").
// Centimetres; X forward, Y right; yaw = atan2(dy, dx) in degrees, growing from +X towards +Y; clock c -> heading + 30 * c.

export type Vec2 = [number, number];

export interface Segment {
  a: Vec2;
  b: Vec2;
  /** Wall thickness, cm (0 = a thin line). */
  thickness?: number;
}

export const add = (p: Vec2, q: Vec2): Vec2 => [p[0] + q[0], p[1] + q[1]];
export const sub = (p: Vec2, q: Vec2): Vec2 => [p[0] - q[0], p[1] - q[1]];
export const scale = (p: Vec2, k: number): Vec2 => [p[0] * k, p[1] * k];
export const dot = (p: Vec2, q: Vec2): number => p[0] * q[0] + p[1] * q[1];
export const cross = (p: Vec2, q: Vec2): number => p[0] * q[1] - p[1] * q[0];
export const len = (p: Vec2): number => Math.hypot(p[0], p[1]);
export const dist = (p: Vec2, q: Vec2): number => Math.hypot(p[0] - q[0], p[1] - q[1]);
export const lerp = (a: number, b: number, k: number): number => a + (b - a) * k;
export const clamp = (v: number, lo: number, hi: number): number => Math.max(lo, Math.min(hi, v));

export const DEG = Math.PI / 180;

/** Unit vector of a yaw (degrees). */
export const dirOf = (yawDeg: number): Vec2 => [Math.cos(yawDeg * DEG), Math.sin(yawDeg * DEG)];

/** Yaw (degrees, -180..180] of a vector; 0 for a zero vector. */
export const yawOf = (v: Vec2): number => (v[0] === 0 && v[1] === 0 ? 0 : Math.atan2(v[1], v[0]) / DEG);

/** Normalises an angle to (-180, 180]. */
export function normYaw(a: number): number {
  let r = ((a + 180) % 360 + 360) % 360 - 180;
  if (r === -180) r = 180;
  return r;
}

/** Signed shortest delta from -> to, degrees (-180, 180]. */
export const deltaYaw = (from: number, to: number): number => normYaw(to - from);

/** Absolute angular difference, degrees [0, 180]. */
export const angleDiff = (a: number, b: number): number => Math.abs(deltaYaw(a, b));

/** Clock position (12 = heading, 3 = right) to a world yaw. */
export const clockToYaw = (headingYaw: number, clock: number): number => normYaw(headingYaw + 30 * clock);

/** A local offset (forward x, right y) around an origin with a heading, to world. */
export function localToWorld(origin: Vec2, headingYaw: number, local: Vec2): Vec2 {
  const f = dirOf(headingYaw);
  const r: Vec2 = [-f[1], f[0]]; // right of forward (yaw + 90)
  return [origin[0] + f[0] * local[0] + r[0] * local[1], origin[1] + f[1] * local[0] + r[1] * local[1]];
}

/** Closest point on segment ab to p and its parameter t in [0, 1]. */
export function closestOnSegment(p: Vec2, a: Vec2, b: Vec2): { point: Vec2; t: number } {
  const ab = sub(b, a);
  const l2 = dot(ab, ab);
  const t = l2 === 0 ? 0 : clamp(dot(sub(p, a), ab) / l2, 0, 1);
  return { point: add(a, scale(ab, t)), t };
}

/** Distance from p to the wall's surface (centre line distance minus half the thickness, never below 0). */
export function distToWall(p: Vec2, w: Segment): number {
  const c = closestOnSegment(p, w.a, w.b).point;
  return Math.max(0, dist(p, c) - (w.thickness ?? 0) / 2);
}

/**
 * Distance along a ray (origin, unit dir) to the first wall surface within maxDist, or Infinity.
 * A wall is treated as its centre line widened by half the thickness (the ray stops that much earlier, measured along the
 * wall normal), which is what the probes and the "wall stare" check need.
 */
export function raycastWalls(origin: Vec2, dir: Vec2, walls: readonly Segment[], maxDist: number): number {
  let best = Infinity;
  for (const w of walls) {
    const d = raySegment(origin, dir, w);
    if (d < best) best = d;
  }
  return best <= maxDist ? best : Infinity;
}

function raySegment(o: Vec2, d: Vec2, w: Segment): number {
  const half = (w.thickness ?? 0) / 2;
  const ab = sub(w.b, w.a);
  const L = len(ab);
  if (L === 0) return Infinity;
  const n: Vec2 = [-ab[1] / L, ab[0] / L];
  // Two faces of the wall (offset lines) and the centre line when thin.
  let best = Infinity;
  const offsets = half > 0 ? [half, -half] : [0];
  for (const off of offsets) {
    const a = add(w.a, scale(n, off));
    const denom = cross(d, ab);
    if (Math.abs(denom) < 1e-9) continue;
    const ao = sub(a, o);
    const t = cross(ao, ab) / denom; // along the ray
    const u = cross(ao, d) / denom; // along the wall
    if (t >= 0 && u >= 0 && u <= 1 && t < best) best = t;
  }
  // Started inside the wall body: distance 0.
  if (half > 0) {
    const c = closestOnSegment(o, w.a, w.b);
    if (c.t > 0 && c.t < 1 && dist(o, c.point) < half) return 0;
  }
  return best;
}

/** True when the straight move p -> q crosses a wall (widened by the agent radius). */
export function segmentBlocked(p: Vec2, q: Vec2, walls: readonly Segment[], radius: number): boolean {
  const d = sub(q, p);
  const L = len(d);
  if (L < 1e-6) return pointInWall(p, walls, radius);
  for (const w of walls) {
    if (segSegDist(p, q, w.a, w.b) < (w.thickness ?? 0) / 2 + radius) return true;
  }
  return false;
}

/** True when p lies inside a wall body widened by radius. */
export function pointInWall(p: Vec2, walls: readonly Segment[], radius: number): boolean {
  for (const w of walls) if (distToWall(p, w) < radius) return true;
  return false;
}

/** Minimum distance between segments pq and ab. */
export function segSegDist(p: Vec2, q: Vec2, a: Vec2, b: Vec2): number {
  if (segmentsIntersect(p, q, a, b)) return 0;
  return Math.min(
    dist(p, closestOnSegment(p, a, b).point),
    dist(q, closestOnSegment(q, a, b).point),
    dist(a, closestOnSegment(a, p, q).point),
    dist(b, closestOnSegment(b, p, q).point),
  );
}

function segmentsIntersect(p: Vec2, q: Vec2, a: Vec2, b: Vec2): boolean {
  const d1 = cross(sub(q, p), sub(a, p));
  const d2 = cross(sub(q, p), sub(b, p));
  const d3 = cross(sub(b, a), sub(p, a));
  const d4 = cross(sub(b, a), sub(q, a));
  return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

/** Point in polygon (even-odd). */
export function pointInPoly(p: Vec2, poly: readonly Vec2[]): boolean {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [xi, yi] = poly[i];
    const [xj, yj] = poly[j];
    if (yi > p[1] !== yj > p[1] && p[0] < ((xj - xi) * (p[1] - yi)) / (yj - yi) + xi) inside = !inside;
  }
  return inside;
}

/** Deterministic PRNG (mulberry32) for seeded runs. */
export function rng(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/** Rounds for stable JSON output (golden vectors, reports). */
export const round = (v: number, digits = 3): number => {
  const k = 10 ** digits;
  const r = Math.round(v * k) / k;
  return Object.is(r, -0) ? 0 : r;
};
