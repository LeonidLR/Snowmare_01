// Training Ground / level generator: pure geometry view of one codex.levelgen_layout/1 layout.
// Docs: docs/design/levelgen/PROPS_LAYERS.md section 2.1, task C2/T2.
// C++ reference: Source/CodexTactics/Private/LevelGen/PropsLayoutView.cpp

export const MAX_SEGMENT_CM = 30000;
export const MAX_RADIUS_CM = 10000;
export const MAX_WALL_THICKNESS_CM = 2000;

export interface PropsLayoutConfig {
  gateMinGapCm: number;
  gateMaxGapCm: number;
  doorThroughSlackCm: number;
  doorTurnReachCm: number;
  doorFallbackWidthCm: number;
  roofInsetCm: number;
  holeHalfSideCm: number;
  stairHalfWidthCm: number;
  levelHeightCm: number;
  floorPerimeterThicknessCm: number;
  parapetThicknessCm: number;
  cutEdgeThicknessCm: number;
  doorGapMatchCm: number;
  tallCoverMinCm: number;
  eyeHeightCm: number;
  roofEdgeInsetCm: number;
}

export const DEFAULT_LAYOUT_CONFIG: PropsLayoutConfig = {
  gateMinGapCm: 80,
  gateMaxGapCm: 600,
  doorThroughSlackCm: 60,
  doorTurnReachCm: 250,
  doorFallbackWidthCm: 180,
  roofInsetCm: 50,
  holeHalfSideCm: 120,
  stairHalfWidthCm: 75,
  levelHeightCm: 300,
  floorPerimeterThicknessCm: 30,
  parapetThicknessCm: 20,
  cutEdgeThicknessCm: 20,
  doorGapMatchCm: 2,
  tallCoverMinCm: 140,
  eyeHeightCm: 160,
  roofEdgeInsetCm: 100,
};

export type PropsWalkLayerKind = 'ground' | 'cut' | 'floor' | 'roof';

export interface PropsWalkLayer {
  name: string;
  kind: PropsWalkLayerKind;
  building?: string;
  level: number;
  zCm: number;
  buildingIndex: number;
}

export type PropsDoorwayKind = 'building_door' | 'gate';

export interface PropsDoorway {
  id: string;
  kind: PropsDoorwayKind;
  building?: string;
  wallGroup?: string;
  pos: [number, number];
  outward: [number, number];
  along: [number, number];
  widthCm: number;
  floor: number;
  walkLayer: string;
  mandatoryRouteDistCm: number;
  bRouteThrough: boolean;
  bRouteTurnsAt: boolean;
  bOnMandatoryRoute: boolean;
}

export interface PropsWallGap {
  group: string;
  wallKind: string;
  building?: string;
  center: [number, number];
  widthCm: number;
  alongY: boolean;
}

export interface PropsZone {
  id: string;
  kind: string;
  setPiece?: string;
  intent?: string;
  poly: [number, number][];
}

export interface PropsAnchorPoint {
  id: string;
  pos: [number, number];
}

export interface PropsRoute {
  id: string;
  points: [number, number][];
  bMandatory: boolean;
  layers: string[];
}

export interface PropsCover {
  id: string;
  pos: [number, number];
  yaw: number;
  sizeX: number;
  sizeY: number;
  heightCm: number;
}

export interface PropsBuildingSummary {
  id: string;
  min: [number, number];
  max: [number, number];
  floors: number;
  bHollow: boolean;
  bEnterable: boolean;
  bRoofWalkable: boolean;
}

export interface PropsSightSegment {
  ax: bigint;
  ay: bigint;
  bx: bigint;
  by: bigint;
}

export interface PropsExclusionParams {
  spawnCm?: number;
  endCm?: number;
  ladderCm?: number;
  stairCm?: number;
  doorCm?: number;
  routeMandatoryCm?: number;
  routeVariantsCm?: number;
  holeRimCm?: number;
  crossing?: boolean;
}

export type PropsExclusionKind =
  | 'spawn'
  | 'end'
  | 'ladder_foot'
  | 'ladder_top'
  | 'stair'
  | 'door'
  | 'route'
  | 'crossing'
  | 'hole_rim';

export interface PropsExclusion {
  kind: PropsExclusionKind;
  sourceId: string;
  walkLayer: string; // empty string means all walk layers
  points: [number, number][];
  radiusCm: number;
  bArea: boolean;
}

// ---------------- Integer Math & Tables ----------------

export const G_SIN_SCALE = 16384;
export const G_SIN_TABLE: readonly number[] = [
  0, 286, 572, 857, 1143, 1428, 1713, 1997, 2280, 2563, 2845, 3126, 3406,
  3686, 3964, 4240, 4516, 4790, 5063, 5334, 5604, 5872, 6138, 6402, 6664, 6924,
  7182, 7438, 7692, 7943, 8192, 8438, 8682, 8923, 9162, 9397, 9630, 9860, 10087,
  10311, 10531, 10749, 10963, 11174, 11381, 11585, 11786, 11982, 12176, 12365, 12551, 12733,
  12911, 13085, 13255, 13421, 13583, 13741, 13894, 14044, 14189, 14330, 14466, 14598, 14726,
  14849, 14968, 15082, 15191, 15296, 15396, 15491, 15582, 15668, 15749, 15826, 15897, 15964,
  16026, 16083, 16135, 16182, 16225, 16262, 16294, 16322, 16344, 16362, 16374, 16382, 16384,
];

export function sinQ(deg: number): bigint {
  deg = ((deg % 360) + 360) % 360;
  if (deg <= 90) return BigInt(G_SIN_TABLE[deg]);
  if (deg <= 180) return BigInt(G_SIN_TABLE[180 - deg]);
  if (deg <= 270) return -BigInt(G_SIN_TABLE[deg - 180]);
  return -BigInt(G_SIN_TABLE[360 - deg]);
}

export function cosQ(deg: number): bigint {
  return sinQ((deg % 360) + 90);
}

export const SIGHT_SCALE = 32768; // 2 * G_SIN_SCALE

export function isqrt(v: bigint | number): bigint {
  const vBig = BigInt(v);
  if (vBig <= 0n) return 0n;
  let r = BigInt(Math.floor(Math.sqrt(Number(vBig))));
  while (r > 0n && r * r > vBig) {
    r--;
  }
  while ((r + 1n) * (r + 1n) <= vBig) {
    r++;
  }
  return r;
}

export function floorDiv(a: bigint | number, b: bigint | number): number {
  if (typeof a === 'bigint' || typeof b === 'bigint') {
    const aBig = BigInt(a);
    const bBig = BigInt(b);
    const q = aBig / bBig;
    return Number(aBig % bBig !== 0n && ((aBig < 0n) !== (bBig < 0n)) ? q - 1n : q);
  }
  return Math.floor(a / b);
}

export function floorHalf(v: bigint): number {
  return Number(v >= 0n ? v / 2n : -((-v + 1n) / 2n));
}

export function clampRadius(r: number): number {
  return Math.max(0, Math.min(MAX_RADIUS_CM, Math.floor(r)));
}

export function segmentWithin(
  p: [number, number],
  a: [number, number],
  b: [number, number],
  num: bigint,
  den: bigint
): boolean {
  if (num < 0n) return false;
  const reach = (num + den - 1n) / den;
  const px = BigInt(p[0]);
  const py = BigInt(p[1]);
  const minX = BigInt(Math.min(a[0], b[0]));
  const maxX = BigInt(Math.max(a[0], b[0]));
  const minY = BigInt(Math.min(a[1], b[1]));
  const maxY = BigInt(Math.max(a[1], b[1]));

  if (px < minX - reach || px > maxX + reach || py < minY - reach || py > maxY + reach) {
    return false;
  }
  const abx = BigInt(b[0] - a[0]);
  const aby = BigInt(b[1] - a[1]);
  const apx = px - BigInt(a[0]);
  const apy = py - BigInt(a[1]);
  const num2 = num * num;
  const den2 = den * den;
  const dot = apx * abx + apy * aby;
  const len2 = abx * abx + aby * aby;
  if (len2 === 0n || dot <= 0n) {
    return den2 * (apx * apx + apy * apy) <= num2;
  }
  if (dot >= len2) {
    const bpx = px - BigInt(b[0]);
    const bpy = py - BigInt(b[1]);
    return den2 * (bpx * bpx + bpy * bpy) <= num2;
  }
  const cross = abx * apy - aby * apx;
  return den2 * cross * cross <= num2 * len2;
}

export function distSqEstimate(
  p: [number, number],
  a: [number, number],
  b: [number, number]
): number {
  const dx = b[0] - a[0];
  const dy = b[1] - a[1];
  const len2 = dx * dx + dy * dy;
  let t = 0;
  if (len2 > 0) {
    t = Math.max(0, Math.min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / len2));
  }
  const ex = p[0] - (a[0] + t * dx);
  const ey = p[1] - (a[1] + t * dy);
  return ex * ex + ey * ey;
}

export function ceilDistance(
  p: [number, number],
  a: [number, number],
  b: [number, number]
): number {
  const estimate = Math.sqrt(distSqEstimate(p, a, b));
  if (estimate > MAX_RADIUS_CM) {
    return Math.min(Math.ceil(estimate), 2147483647 - 1);
  }
  let d = BigInt(Math.ceil(estimate));
  while (d > 0n && segmentWithin(p, a, b, d - 1n, 1n)) {
    d--;
  }
  while (!segmentWithin(p, a, b, d, 1n)) {
    d++;
  }
  return Number(d);
}

export function pointInPolygon(p: [number, number], poly: [number, number][]): boolean {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const a = poly[j];
    const b = poly[i];
    if ((a[1] > p[1]) === (b[1] > p[1])) {
      continue;
    }
    const lhs = BigInt(p[0] - a[0]) * BigInt(b[1] - a[1]);
    const rhs = BigInt(b[0] - a[0]) * BigInt(p[1] - a[1]);
    if (b[1] > a[1] ? lhs < rhs : lhs > rhs) {
      inside = !inside;
    }
  }
  return inside;
}

export function inRect(p: [number, number], min: [number, number], max: [number, number]): boolean {
  return p[0] >= min[0] && p[0] <= max[0] && p[1] >= min[1] && p[1] <= max[1];
}

export function rectWithin(
  p: [number, number],
  min: [number, number],
  max: [number, number],
  r: number
): boolean {
  const dx = BigInt(Math.max(min[0] - p[0], 0, p[0] - max[0]));
  const dy = BigInt(Math.max(min[1] - p[1], 0, p[1] - max[1]));
  const rBig = BigInt(r);
  if (dx > rBig || dy > rBig) return false;
  return dx * dx + dy * dy <= rBig * rBig;
}

export function coverWithin(
  p: [number, number],
  center: [number, number],
  yaw: number,
  sizeX: number,
  sizeY: number,
  clearance: number
): boolean {
  const dx = BigInt(p[0] - center[0]);
  const dy = BigInt(p[1] - center[1]);
  const reach = BigInt(Math.floor((sizeX + sizeY) / 2) + clearance + 2);
  const abs = (v: bigint) => (v < 0n ? -v : v);
  if (abs(dx) > reach || abs(dy) > reach) {
    return false;
  }
  const s = sinQ(yaw);
  const c = cosQ(yaw);
  const lx = dx * c + dy * s;
  const ly = -dx * s + dy * c;
  const scale = BigInt(G_SIN_SCALE);
  const sx = (BigInt(sizeX) + 2n * BigInt(clearance)) * scale;
  const sy = (BigInt(sizeY) + 2n * BigInt(clearance)) * scale;
  return 2n * abs(lx) <= sx && 2n * abs(ly) <= sy;
}

export function stripIndex(id: string): string {
  const idx = id.lastIndexOf('_');
  if (idx === -1 || idx + 1 >= id.length) return id;
  for (let i = idx + 1; i < id.length; i++) {
    if (id[i] < '0' || id[i] > '9') return id;
  }
  return id.substring(0, idx);
}

export function exclusionContains(
  ex: PropsExclusion,
  p: [number, number],
  layer: string
): boolean {
  if (ex.walkLayer !== '' && ex.walkLayer !== layer) return false;
  if (ex.points.length === 0) return false;
  const r = clampRadius(ex.radiusCm);
  if (ex.bArea && ex.points.length >= 3 && pointInPolygon(p, ex.points)) {
    return true;
  }
  if (ex.points.length === 1) {
    return segmentWithin(p, ex.points[0], ex.points[0], BigInt(r), 1n);
  }
  const edges = ex.bArea && ex.points.length >= 3 ? ex.points.length : ex.points.length - 1;
  for (let i = 0; i < edges; i++) {
    if (segmentWithin(p, ex.points[i], ex.points[(i + 1) % ex.points.length], BigInt(r), 1n)) {
      return true;
    }
  }
  return false;
}

// ---------------- PropsLayoutView ----------------

interface InternalWall {
  id: string;
  kind: string;
  building?: string;
  a: [number, number];
  b: [number, number];
  thickness: number;
  floor: number;
  height: number;
}

interface InternalLayerWall {
  a: [number, number];
  b: [number, number];
  thickness: number;
}

interface InternalBuilding {
  id: string;
  min: [number, number];
  max: [number, number];
  floors: number;
  bHollow: boolean;
  bEnterable: boolean;
  bRoofWalkable: boolean;
  doors: Array<{ pos: [number, number]; floor: number }>;
  stairs: Array<{ id: string; pos: [number, number]; top: [number, number]; mid?: [number, number]; bHasMid: boolean }>;
  roofHoles: [number, number][];
  floorHoles: Array<{ pos: [number, number]; floor: number }>;
}

interface InternalCrossing {
  id: string;
  poly: [number, number][];
}

interface InternalLadder {
  id: string;
  pos: [number, number];
  foot: [number, number];
  top: [number, number];
  from: string;
  to: string;
}

interface InternalCover {
  id: string;
  pos: [number, number];
  yaw: number;
  sizeX: number;
  sizeY: number;
  heightCm: number;
}

interface InternalRoute {
  id: string;
  points: [number, number][];
  bMandatory: boolean;
  layers: string[];
}

export class PropsLayoutView {
  name = '';
  config: PropsLayoutConfig = { ...DEFAULT_LAYOUT_CONFIG };
  boundsMin: [number, number] = [0, 0];
  boundsMax: [number, number] = [0, 0];
  groundZ = 0;
  spawnPos: [number, number] = [0, 0];
  endPos: [number, number] = [0, 0];

  layers: PropsWalkLayer[] = [];
  layerWalls: InternalLayerWall[][] = [];
  doorways: PropsDoorway[] = [];
  wallGaps: PropsWallGap[] = [];
  zones: PropsZone[] = [];
  ambushPoints: PropsAnchorPoint[] = [];
  mandatoryRouteIds: string[] = [];

  private walls: InternalWall[] = [];
  private buildings: InternalBuilding[] = [];
  private crossings: InternalCrossing[] = [];
  private ladders: InternalLadder[] = [];
  private covers: InternalCover[] = [];
  private routes: InternalRoute[] = [];

  bHasCut = false;
  cutPoly: [number, number][] = [];
  cutLevel = -1;
  cutFloorZ = 0;
  bHasCutFloorZ = false;

  static readConfig(balance: Record<string, unknown>, config?: PropsLayoutConfig): PropsLayoutConfig {
    const cfg: PropsLayoutConfig = { ...(config ?? DEFAULT_LAYOUT_CONFIG) };
    if (!balance || typeof balance !== 'object') return cfg;
    const lv = (balance as Record<string, unknown>).layout_view;
    if (lv && typeof lv === 'object') {
      const o = lv as Record<string, unknown>;
      if (typeof o.gate_min_gap_cm === 'number' && o.gate_min_gap_cm >= 0) cfg.gateMinGapCm = Math.floor(o.gate_min_gap_cm);
      if (typeof o.gate_max_gap_cm === 'number' && o.gate_max_gap_cm >= 0) cfg.gateMaxGapCm = Math.floor(o.gate_max_gap_cm);
      if (typeof o.door_through_slack_cm === 'number' && o.door_through_slack_cm >= 0) cfg.doorThroughSlackCm = Math.floor(o.door_through_slack_cm);
      if (typeof o.door_turn_reach_cm === 'number' && o.door_turn_reach_cm >= 0) cfg.doorTurnReachCm = Math.floor(o.door_turn_reach_cm);
      if (typeof o.door_fallback_width_cm === 'number' && o.door_fallback_width_cm >= 0) cfg.doorFallbackWidthCm = Math.floor(o.door_fallback_width_cm);
      if (typeof o.tall_cover_min_cm === 'number' && o.tall_cover_min_cm >= 0) cfg.tallCoverMinCm = Math.floor(o.tall_cover_min_cm);
      if (typeof o.eye_height_cm === 'number' && o.eye_height_cm >= 0) cfg.eyeHeightCm = Math.floor(o.eye_height_cm);
      if (typeof o.roof_edge_inset_cm === 'number' && o.roof_edge_inset_cm >= 0) cfg.roofEdgeInsetCm = Math.floor(o.roof_edge_inset_cm);
    }
    return cfg;
  }

  static build(jsonOrObj: string | Record<string, unknown>, inConfig?: PropsLayoutConfig): PropsLayoutView {
    const root = (typeof jsonOrObj === 'string' ? JSON.parse(jsonOrObj) : jsonOrObj) as Record<string, unknown>;
    const view = new PropsLayoutView();
    view.config = inConfig ? { ...inConfig } : { ...DEFAULT_LAYOUT_CONFIG };

    if (!root || typeof root !== 'object') {
      throw new Error('layout: root must be an object');
    }
    if (root.schema !== 'codex.levelgen_layout/1') {
      throw new Error(`layout: schema '${String(root.schema)}' is not codex.levelgen_layout/1`);
    }

    view.name = String(root.name ?? '');
    view.groundZ = typeof root.ground_z === 'number' ? Math.floor(root.ground_z) : 0;

    const bounds = root.bounds as Record<string, unknown> | undefined;
    if (bounds && Array.isArray(bounds.min) && Array.isArray(bounds.max)) {
      view.boundsMin = [Math.floor(bounds.min[0]), Math.floor(bounds.min[1])];
      view.boundsMax = [Math.floor(bounds.max[0]), Math.floor(bounds.max[1])];
    }
    const spawn = root.spawn as Record<string, unknown> | undefined;
    if (spawn && Array.isArray(spawn.pos)) {
      view.spawnPos = [Math.floor(spawn.pos[0]), Math.floor(spawn.pos[1])];
    }
    if (Array.isArray(root.end)) {
      view.endPos = [Math.floor(root.end[0]), Math.floor(root.end[1])];
    }

    if (Array.isArray(root.walls)) {
      for (const w of root.walls) {
        if (w && typeof w === 'object' && Array.isArray(w.a) && Array.isArray(w.b)) {
          view.walls.push({
            id: String(w.id ?? ''),
            kind: String(w.kind ?? 'wall'),
            building: w.building ? String(w.building) : undefined,
            a: [Math.floor(w.a[0]), Math.floor(w.a[1])],
            b: [Math.floor(w.b[0]), Math.floor(w.b[1])],
            thickness: typeof w.thickness === 'number' ? Math.floor(w.thickness) : 0,
            floor: typeof w.floor === 'number' ? Math.floor(w.floor) : 0,
            height: typeof w.height === 'number' ? Math.floor(w.height) : 0,
          });
        }
      }
    }

    if (Array.isArray(root.buildings)) {
      for (const b of root.buildings) {
        if (b && typeof b === 'object' && Array.isArray(b.footprint) && b.footprint.length === 2) {
          const f0 = b.footprint[0];
          const f1 = b.footprint[1];
          const min: [number, number] = [Math.min(f0[0], f1[0]), Math.min(f0[1], f1[1])];
          const max: [number, number] = [Math.max(f0[0], f1[0]), Math.max(f0[1], f1[1])];
          const doors: Array<{ pos: [number, number]; floor: number }> = [];
          if (Array.isArray(b.doors)) {
            for (const d of b.doors) {
              if (d && typeof d === 'object' && Array.isArray(d.pos)) {
                doors.push({ pos: [Math.floor(d.pos[0]), Math.floor(d.pos[1])], floor: typeof d.floor === 'number' ? Math.floor(d.floor) : 0 });
              }
            }
          }
          const stairs: Array<{ id: string; pos: [number, number]; top: [number, number]; mid?: [number, number]; bHasMid: boolean }> = [];
          if (Array.isArray(b.stairs)) {
            for (const st of b.stairs) {
              if (st && typeof st === 'object' && Array.isArray(st.pos) && Array.isArray(st.top)) {
                const hasMid = Array.isArray(st.mid);
                stairs.push({
                  id: String(st.id ?? ''),
                  pos: [Math.floor(st.pos[0]), Math.floor(st.pos[1])],
                  top: [Math.floor(st.top[0]), Math.floor(st.top[1])],
                  mid: hasMid ? [Math.floor(st.mid[0]), Math.floor(st.mid[1])] : undefined,
                  bHasMid: hasMid,
                });
              }
            }
          }
          const roofHoles: [number, number][] = [];
          if (Array.isArray(b.roof_holes)) {
            for (const rh of b.roof_holes) {
              if (Array.isArray(rh) && rh.length === 2) roofHoles.push([Math.floor(rh[0]), Math.floor(rh[1])]);
            }
          }
          const floorHoles: Array<{ pos: [number, number]; floor: number }> = [];
          if (Array.isArray(b.floor_holes)) {
            for (const fh of b.floor_holes) {
              if (fh && typeof fh === 'object' && Array.isArray(fh.pos)) {
                floorHoles.push({ pos: [Math.floor(fh.pos[0]), Math.floor(fh.pos[1])], floor: typeof fh.floor === 'number' ? Math.floor(fh.floor) : 0 });
              }
            }
          }

          view.buildings.push({
            id: String(b.id ?? ''),
            min,
            max,
            floors: Math.max(1, typeof b.floors === 'number' ? Math.floor(b.floors) : 1),
            bHollow: Boolean(b.hollow),
            bEnterable: Boolean(b.enterable),
            bRoofWalkable: Boolean(b.roof_walkable),
            doors,
            stairs,
            roofHoles,
            floorHoles,
          });
        }
      }
    }

    const cut = root.cut as Record<string, unknown> | undefined;
    if (cut && typeof cut === 'object') {
      view.bHasCut = true;
      if (Array.isArray(cut.poly)) {
        for (const p of cut.poly) {
          if (Array.isArray(p) && p.length === 2) view.cutPoly.push([Math.floor(p[0]), Math.floor(p[1])]);
        }
      }
      view.cutLevel = typeof cut.level === 'number' ? Math.floor(cut.level) : -1;
      if (typeof cut.floor_z === 'number') {
        view.bHasCutFloorZ = true;
        view.cutFloorZ = Math.floor(cut.floor_z);
      }
      if (Array.isArray(cut.crossings)) {
        for (const cr of cut.crossings) {
          if (cr && typeof cr === 'object' && Array.isArray(cr.poly)) {
            const poly: [number, number][] = [];
            for (const p of cr.poly) {
              if (Array.isArray(p) && p.length === 2) poly.push([Math.floor(p[0]), Math.floor(p[1])]);
            }
            view.crossings.push({ id: String(cr.id ?? ''), poly });
          }
        }
      }
    }

    if (Array.isArray(root.ladders)) {
      for (const ld of root.ladders) {
        if (ld && typeof ld === 'object' && Array.isArray(ld.foot) && Array.isArray(ld.top)) {
          const pos = Array.isArray(ld.pos) ? [Math.floor(ld.pos[0]), Math.floor(ld.pos[1])] : [Math.floor(ld.foot[0]), Math.floor(ld.foot[1])];
          view.ladders.push({
            id: String(ld.id ?? ''),
            pos: pos as [number, number],
            foot: [Math.floor(ld.foot[0]), Math.floor(ld.foot[1])],
            top: [Math.floor(ld.top[0]), Math.floor(ld.top[1])],
            from: String(ld.from ?? ''),
            to: String(ld.to ?? ''),
          });
        }
      }
    }

    if (Array.isArray(root.covers)) {
      for (const c of root.covers) {
        if (c && typeof c === 'object' && Array.isArray(c.pos) && Array.isArray(c.size) && c.size.length >= 2) {
          view.covers.push({
            id: String(c.id ?? ''),
            pos: [Math.floor(c.pos[0]), Math.floor(c.pos[1])],
            yaw: typeof c.yaw === 'number' ? Math.floor(c.yaw) : 0,
            sizeX: Math.floor(c.size[0]),
            sizeY: Math.floor(c.size[1]),
            heightCm: c.size.length >= 3 && typeof c.size[2] === 'number' ? Math.floor(c.size[2]) : 0,
          });
        }
      }
    }

    if (Array.isArray(root.zones)) {
      for (const z of root.zones) {
        if (z && typeof z === 'object' && Array.isArray(z.poly)) {
          const poly: [number, number][] = [];
          for (const p of z.poly) {
            if (Array.isArray(p) && p.length === 2) poly.push([Math.floor(p[0]), Math.floor(p[1])]);
          }
          view.zones.push({
            id: String(z.id ?? ''),
            kind: String(z.kind ?? ''),
            setPiece: z.set_piece ? String(z.set_piece) : undefined,
            intent: z.intent ? String(z.intent) : undefined,
            poly,
          });
        }
      }
    }

    if (Array.isArray(root.ambush_points)) {
      for (const a of root.ambush_points) {
        if (a && typeof a === 'object' && Array.isArray(a.pos)) {
          view.ambushPoints.push({ id: String(a.id ?? ''), pos: [Math.floor(a.pos[0]), Math.floor(a.pos[1])] });
        }
      }
    }

    if (Array.isArray(root.routes)) {
      for (const rt of root.routes) {
        if (rt && typeof rt === 'object' && Array.isArray(rt.points)) {
          const points: [number, number][] = [];
          for (const p of rt.points) {
            if (Array.isArray(p) && p.length === 2) points.push([Math.floor(p[0]), Math.floor(p[1])]);
          }
          const isMandatory = Boolean(rt.mandatory);
          let layers: string[];
          if (Array.isArray(rt.layers)) {
            layers = rt.layers.map(String);
          } else {
            layers = new Array(points.length).fill('ground');
          }
          const id = String(rt.id ?? '');
          if (isMandatory) {
            view.mandatoryRouteIds.push(id);
          }
          view.routes.push({ id, points, bMandatory: isMandatory, layers });
        }
      }
    }

    view.buildLayers();
    view.findGaps();
    view.buildDoorways();

    return view;
  }

  private static layerNameFor(building: string, floor: number): string {
    return floor <= 0 ? 'ground' : `floor:${building}:${floor}`;
  }

  private buildLayers(): void {
    this.layers = [];
    this.layerWalls = [];

    const addRect = (out: InternalLayerWall[], min: [number, number], max: [number, number], thickness: number) => {
      const c: [number, number][] = [min, [max[0], min[1]], max, [min[0], max[1]]];
      for (let i = 0; i < 4; i++) {
        out.push({ a: c[i], b: c[(i + 1) % 4], thickness });
      }
    };

    // Ground layer
    this.layers.push({
      name: 'ground',
      kind: 'ground',
      level: 0,
      zCm: this.groundZ,
      buildingIndex: -1,
    });
    const groundWalls: InternalLayerWall[] = [];
    for (const w of this.walls) {
      if (w.floor === 0) {
        groundWalls.push({ a: w.a, b: w.b, thickness: w.thickness });
      }
    }
    this.layerWalls.push(groundWalls);

    // Cut layer
    if (this.bHasCut) {
      this.layers.push({
        name: 'cut',
        kind: 'cut',
        level: this.cutLevel,
        zCm: this.groundZ + (this.bHasCutFloorZ ? this.cutFloorZ : this.cutLevel * this.config.levelHeightCm),
        buildingIndex: -1,
      });
      const cutWalls: InternalLayerWall[] = [];
      if (this.cutPoly.length >= 3) {
        for (let i = 0; i < this.cutPoly.length; i++) {
          cutWalls.push({ a: this.cutPoly[i], b: this.cutPoly[(i + 1) % this.cutPoly.length], thickness: this.config.cutEdgeThicknessCm });
        }
      }
      this.layerWalls.push(cutWalls);
    }

    // Building floors and roofs
    for (let index = 0; index < this.buildings.length; index++) {
      const b = this.buildings[index];
      if (!b.bHollow) {
        for (let n = 1; n < b.floors; n++) {
          this.layers.push({
            name: PropsLayoutView.layerNameFor(b.id, n),
            kind: 'floor',
            building: b.id,
            level: n,
            buildingIndex: index,
            zCm: this.groundZ + n * this.config.levelHeightCm,
          });
          const floorWalls: InternalLayerWall[] = [];
          addRect(floorWalls, b.min, b.max, this.config.floorPerimeterThicknessCm);
          for (const w of this.walls) {
            if (w.building === b.id && w.floor === n) {
              floorWalls.push({ a: w.a, b: w.b, thickness: w.thickness });
            }
          }
          this.layerWalls.push(floorWalls);
        }
      }
      if (b.bRoofWalkable) {
        this.layers.push({
          name: `roof:${b.id}`,
          kind: 'roof',
          building: b.id,
          level: b.floors,
          buildingIndex: index,
          zCm: this.groundZ + b.floors * this.config.levelHeightCm,
        });
        const roofWalls: InternalLayerWall[] = [];
        addRect(roofWalls, b.min, b.max, this.config.parapetThicknessCm);
        this.layerWalls.push(roofWalls);
      }
    }
  }

  private findGaps(): void {
    interface Interval {
      lo: number;
      hi: number;
      fixed: number;
      alongY: boolean;
      wallIndex: number;
    }
    const groups = new Map<string, Interval[]>();
    for (let index = 0; index < this.walls.length; index++) {
      const w = this.walls[index];
      const alongY = w.a[0] === w.b[0];
      if (alongY === (w.a[1] === w.b[1])) {
        continue; // diagonal or zero length
      }
      const fixed = alongY ? w.a[0] : w.a[1];
      const lo = alongY ? Math.min(w.a[1], w.b[1]) : Math.min(w.a[0], w.b[0]);
      const hi = alongY ? Math.max(w.a[1], w.b[1]) : Math.max(w.a[0], w.b[0]);
      const key = `${stripIndex(w.id)}|${alongY ? 'Y' : 'X'}|${fixed}`;
      let arr = groups.get(key);
      if (!arr) {
        arr = [];
        groups.set(key, arr);
      }
      arr.push({ lo, hi, fixed, alongY, wallIndex: index });
    }

    const sortedKeys = [...groups.keys()].sort();
    this.wallGaps = [];
    for (const key of sortedKeys) {
      const items = groups.get(key)!;
      items.sort((a, b) => (a.lo !== b.lo ? a.lo - b.lo : a.hi - b.hi));
      let maxHi = items[0].hi;
      for (let i = 1; i < items.length; i++) {
        const item = items[i];
        if (item.lo > maxHi) {
          const w = this.walls[item.wallIndex];
          const mid = floorHalf(BigInt(maxHi) + BigInt(item.lo));
          this.wallGaps.push({
            group: stripIndex(w.id),
            wallKind: w.kind,
            building: w.building,
            widthCm: item.lo - maxHi,
            alongY: item.alongY,
            center: item.alongY ? [item.fixed, mid] : [mid, item.fixed],
          });
        }
        maxHi = Math.max(maxHi, item.hi);
      }
    }
  }

  private buildDoorways(): void {
    this.doorways = [];

    // Building doors
    for (const b of this.buildings) {
      for (let index = 0; index < b.doors.length; index++) {
        const d = b.doors[index];
        const id = `${b.id}#${index}`;
        const walkLayer = PropsLayoutView.layerNameFor(b.id, d.floor);

        // side read from footprint
        const dist = [
          Math.abs(d.pos[0] - b.min[0]),
          Math.abs(d.pos[0] - b.max[0]),
          Math.abs(d.pos[1] - b.min[1]),
          Math.abs(d.pos[1] - b.max[1]),
        ];
        const dir: [number, number][] = [[-1, 0], [1, 0], [0, -1], [0, 1]];
        let best = 0;
        for (let i = 1; i < 4; i++) {
          if (dist[i] < dist[best]) best = i;
        }
        const outward = dir[best];
        const along: [number, number] = outward[0] !== 0 ? [0, 1] : [1, 0];

        let widthCm = 0;
        let wallGroup: string | undefined;
        for (const gap of this.wallGaps) {
          if (
            gap.wallKind === 'building' &&
            gap.building === b.id &&
            Math.abs(gap.center[0] - d.pos[0]) <= this.config.doorGapMatchCm &&
            Math.abs(gap.center[1] - d.pos[1]) <= this.config.doorGapMatchCm
          ) {
            widthCm = gap.widthCm;
            wallGroup = gap.group;
            break;
          }
        }

        this.doorways.push({
          id,
          kind: 'building_door',
          building: b.id,
          wallGroup,
          pos: d.pos,
          outward,
          along,
          widthCm,
          floor: d.floor,
          walkLayer,
          mandatoryRouteDistCm: 0,
          bRouteThrough: false,
          bRouteTurnsAt: false,
          bOnMandatoryRoute: false,
        });
      }
    }

    // Compound gates
    const gateCounter = new Map<string, number>();
    for (const gap of this.wallGaps) {
      if (
        (gap.wallKind !== 'fence' && gap.wallKind !== 'wall') ||
        gap.widthCm < this.config.gateMinGapCm ||
        gap.widthCm > this.config.gateMaxGapCm
      ) {
        continue;
      }
      const count = gateCounter.get(gap.group) ?? 0;
      gateCounter.set(gap.group, count + 1);
      this.doorways.push({
        id: `${gap.group}#g${count}`,
        kind: 'gate',
        wallGroup: gap.group,
        pos: gap.center,
        outward: [0, 0],
        along: gap.alongY ? [0, 1] : [1, 0],
        widthCm: gap.widthCm,
        floor: 0,
        walkLayer: 'ground',
        mandatoryRouteDistCm: 0,
        bRouteThrough: false,
        bRouteTurnsAt: false,
        bOnMandatoryRoute: false,
      });
    }

    for (const w of this.doorways) {
      w.mandatoryRouteDistCm = this.distanceToMandatoryRouteCm(w.pos, w.walkLayer);
      const reach =
        Math.floor((w.widthCm > 0 ? w.widthCm : this.config.doorFallbackWidthCm) / 2) +
        this.config.doorThroughSlackCm;
      const bLineAlongY = w.along[1] !== 0;

      for (const route of this.routes) {
        if (!route.bMandatory) continue;
        const n = route.points.length;
        for (let i = 0; i < n; i++) {
          const v = route.points[i];
          // Through check
          if (i + 1 < n && route.layers[i] === w.walkLayer && route.layers[i + 1] === w.walkLayer) {
            const b = route.points[i + 1];
            const s0 = bLineAlongY ? v[0] - w.pos[0] : v[1] - w.pos[1];
            const s1 = bLineAlongY ? b[0] - w.pos[0] : b[1] - w.pos[1];
            const bCrosses = !((s0 > 0 && s1 > 0) || (s0 < 0 && s1 < 0) || (s0 === 0 && s1 === 0));
            if (bCrosses) {
              const den = BigInt(s1 - s0);
              const l0 = BigInt(bLineAlongY ? v[1] - w.pos[1] : v[0] - w.pos[0]);
              const l1 = BigInt(bLineAlongY ? b[1] - w.pos[1] : b[0] - w.pos[0]);
              const s0Big = BigInt(s0);
              const num = l0 * den - s0Big * (l1 - l0);
              const abs = (val: bigint) => (val < 0n ? -val : val);
              if (abs(num) <= BigInt(reach) * abs(den)) {
                w.bRouteThrough = true;
              }
            }
          }

          // Turns at check
          if (route.layers[i] === w.walkLayer) {
            const rx = v[0] - w.pos[0];
            const ry = v[1] - w.pos[1];
            const lateral = Math.abs(rx * w.along[0] + ry * w.along[1]);
            let bInFront = false;
            if (w.outward[0] === 0 && w.outward[1] === 0) {
              bInFront = Math.abs(bLineAlongY ? rx : ry) <= this.config.doorTurnReachCm;
            } else {
              const depth = rx * w.outward[0] + ry * w.outward[1];
              bInFront = depth >= 0 && depth <= this.config.doorTurnReachCm;
            }
            if (bInFront && lateral <= reach) {
              let bTurn = i === 0 || i === n - 1;
              if (!bTurn) {
                const p = route.points[i - 1];
                const q = route.points[i + 1];
                const dot = (v[0] - p[0]) * (q[0] - v[0]) + (v[1] - p[1]) * (q[1] - v[1]);
                bTurn = dot < 0;
              }
              w.bRouteTurnsAt ||= bTurn;
            }
          }
        }
      }
      w.bOnMandatoryRoute = w.bRouteThrough || w.bRouteTurnsAt;
    }

    this.doorways.sort((a, b) => (a.id < b.id ? -1 : a.id > b.id ? 1 : 0));
  }

  findLayer(name: string): PropsWalkLayer | undefined {
    return this.layers.find((l) => l.name === name);
  }

  findDoorway(id: string): PropsDoorway | undefined {
    return this.doorways.find((d) => d.id === id);
  }

  findZone(id: string): PropsZone | undefined {
    return this.zones.find((z) => z.id === id);
  }

  isInBounds(p: [number, number]): boolean {
    return inRect(p, this.boundsMin, this.boundsMax);
  }

  isInZone(p: [number, number], zoneId: string): boolean {
    const zone = this.findZone(zoneId);
    return Boolean(zone && pointInPolygon(p, zone.poly));
  }

  isWalkable(p: [number, number], walkLayer: string): boolean {
    const l = this.findLayer(walkLayer);
    if (!l) return false;
    switch (l.kind) {
      case 'ground': {
        if (!this.isInBounds(p)) return false;
        if (this.cutPoly.length >= 3 && pointInPolygon(p, this.cutPoly)) {
          for (const x of this.crossings) {
            if (pointInPolygon(p, x.poly)) return true;
          }
          return false;
        }
        return true;
      }
      case 'cut':
        return this.cutPoly.length >= 3 && pointInPolygon(p, this.cutPoly);
      case 'roof': {
        const b = this.buildings[l.buildingIndex];
        const inset = this.config.roofInsetCm;
        const min: [number, number] = [b.min[0] + inset, b.min[1] + inset];
        const max: [number, number] = [b.max[0] - inset, b.max[1] - inset];
        if (!inRect(p, min, max)) return false;
        for (const h of b.roofHoles) {
          if (Math.abs(p[0] - h[0]) <= this.config.holeHalfSideCm && Math.abs(p[1] - h[1]) <= this.config.holeHalfSideCm) {
            return false;
          }
        }
        return true;
      }
      case 'floor': {
        const b = this.buildings[l.buildingIndex];
        if (!inRect(p, b.min, b.max)) return false;
        for (const h of b.floorHoles) {
          if (
            h.floor === l.level &&
            Math.abs(p[0] - h.pos[0]) <= this.config.holeHalfSideCm &&
            Math.abs(p[1] - h.pos[1]) <= this.config.holeHalfSideCm
          ) {
            return false;
          }
        }
        return true;
      }
    }
  }

  isBlocked(p: [number, number], walkLayer: string, clearanceCm: number): boolean {
    const layerIndex = this.layers.findIndex((l) => l.name === walkLayer);
    if (layerIndex === -1) return false;
    const clearance = BigInt(clampRadius(clearanceCm));

    for (const w of this.layerWalls[layerIndex]) {
      // 2 * dist <= thickness + 2 * clearance
      if (segmentWithin(p, w.a, w.b, BigInt(w.thickness) + 2n * clearance, 2n)) {
        return true;
      }
    }

    if (this.layers[layerIndex].kind !== 'ground') {
      return false;
    }

    for (const b of this.buildings) {
      if (!b.bEnterable && rectWithin(p, b.min, b.max, Number(clearance))) {
        return true;
      }
    }

    for (const c of this.covers) {
      if (coverWithin(p, c.pos, c.yaw, c.sizeX, c.sizeY, Number(clearance))) {
        return true;
      }
    }

    return false;
  }

  isFree(p: [number, number], walkLayer: string, clearanceCm: number): boolean {
    return this.isWalkable(p, walkLayer) && !this.isBlocked(p, walkLayer, clearanceCm);
  }

  getLayerExtent(walkLayer: string): { min: [number, number]; max: [number, number] } | null {
    const l = this.findLayer(walkLayer);
    if (!l) return null;
    switch (l.kind) {
      case 'ground':
        return { min: this.boundsMin, max: this.boundsMax };
      case 'cut': {
        if (this.cutPoly.length < 3) return null;
        let minX = this.cutPoly[0][0];
        let minY = this.cutPoly[0][1];
        let maxX = minX;
        let maxY = minY;
        for (const pt of this.cutPoly) {
          minX = Math.min(minX, pt[0]);
          minY = Math.min(minY, pt[1]);
          maxX = Math.max(maxX, pt[0]);
          maxY = Math.max(maxY, pt[1]);
        }
        return { min: [minX, minY], max: [maxX, maxY] };
      }
      default: {
        const b = this.buildings[l.buildingIndex];
        return { min: b.min, max: b.max };
      }
    }
  }

  distanceToMandatoryRouteCm(p: [number, number], walkLayer: string): number {
    let best = 2147483647;
    for (const route of this.routes) {
      if (!route.bMandatory) continue;
      for (let i = 0; i + 1 < route.points.length; i++) {
        if (route.layers[i] === walkLayer && route.layers[i + 1] === walkLayer) {
          best = Math.min(best, ceilDistance(p, route.points[i], route.points[i + 1]));
        }
      }
    }
    return best;
  }

  isNearMandatoryRoute(p: [number, number], walkLayer: string, radiusCm: number): boolean {
    const r = BigInt(clampRadius(radiusCm));
    for (const route of this.routes) {
      if (!route.bMandatory) continue;
      for (let i = 0; i + 1 < route.points.length; i++) {
        if (
          route.layers[i] === walkLayer &&
          route.layers[i + 1] === walkLayer &&
          segmentWithin(p, route.points[i], route.points[i + 1], r, 1n)
        ) {
          return true;
        }
      }
    }
    return false;
  }

  isNearWall(p: [number, number], walkLayer: string, clearanceCm: number): boolean {
    return this.findNearestWallAxis(p, walkLayer, clearanceCm) !== null;
  }

  isNearCover(p: [number, number], walkLayer: string, clearanceCm: number): boolean {
    const l = this.findLayer(walkLayer);
    if (!l || l.kind !== 'ground') return false;
    const clearance = clampRadius(clearanceCm);
    for (const c of this.covers) {
      if (coverWithin(p, c.pos, c.yaw, c.sizeX, c.sizeY, clearance)) {
        return true;
      }
    }
    return false;
  }

  findNearestWallAxis(
    p: [number, number],
    walkLayer: string,
    clearanceCm: number
  ): { alongX: boolean; dist: number } | null {
    const layerIndex = this.layers.findIndex((l) => l.name === walkLayer);
    if (layerIndex === -1) return null;
    const clearance = BigInt(clampRadius(clearanceCm));
    let bestDist = 2147483647;
    let bestWall: InternalLayerWall | null = null;

    for (const w of this.layerWalls[layerIndex]) {
      if (!segmentWithin(p, w.a, w.b, BigInt(w.thickness) + 2n * clearance, 2n)) {
        continue;
      }
      const d = ceilDistance(p, w.a, w.b);
      if (d < bestDist) {
        bestDist = d;
        bestWall = w;
      }
    }
    if (!bestWall) return null;
    const alongX = Math.abs(bestWall.b[0] - bestWall.a[0]) >= Math.abs(bestWall.b[1] - bestWall.a[1]);
    return { alongX, dist: bestDist };
  }

  buildExclusions(params: PropsExclusionParams): PropsExclusion[] {
    const out: PropsExclusion[] = [];
    const shape = (
      kind: PropsExclusionKind,
      id: string,
      layer: string,
      points: [number, number][],
      radius: number,
      bArea: boolean
    ) => {
      out.push({
        kind,
        sourceId: id,
        walkLayer: layer,
        points,
        radiusCm: clampRadius(radius),
        bArea,
      });
    };
    const square = (c: [number, number], half: number): [number, number][] => [
      [c[0] - half, c[1] - half],
      [c[0] + half, c[1] - half],
      [c[0] + half, c[1] + half],
      [c[0] - half, c[1] + half],
    ];

    if ((params.spawnCm ?? 0) > 0) {
      shape('spawn', 'spawn', '', [this.spawnPos], params.spawnCm!, false);
    }
    if ((params.endCm ?? 0) > 0) {
      shape('end', 'end', '', [this.endPos], params.endCm!, false);
    }
    if ((params.ladderCm ?? 0) > 0) {
      for (const l of this.ladders) {
        shape('ladder_foot', l.id, l.from, [l.pos, l.foot], params.ladderCm!, false);
        shape('ladder_top', l.id, l.to, [l.top], params.ladderCm!, false);
      }
    }
    if ((params.stairCm ?? 0) > 0) {
      for (const b of this.buildings) {
        for (const st of b.stairs) {
          const pts: [number, number][] = [st.pos];
          if (st.bHasMid && st.mid) pts.push(st.mid);
          pts.push(st.top);
          shape('stair', st.id, '', pts, this.config.stairHalfWidthCm + params.stairCm!, false);
        }
      }
    }
    if ((params.doorCm ?? 0) > 0) {
      for (const d of this.doorways) {
        shape('door', d.id, d.walkLayer, [d.pos], params.doorCm!, false);
      }
    }
    for (const route of this.routes) {
      const radius = route.bMandatory ? (params.routeMandatoryCm ?? 0) : (params.routeVariantsCm ?? 0);
      if (radius <= 0) continue;
      let start = 0;
      for (let i = 1; i <= route.points.length; i++) {
        if (i === route.points.length || route.layers[i] !== route.layers[start]) {
          shape('route', route.id, route.layers[start], route.points.slice(start, i), radius, false);
          start = i;
        }
      }
    }
    if (params.crossing) {
      for (const x of this.crossings) {
        shape('crossing', x.id, '', x.poly, 0, true);
      }
    }
    if ((params.holeRimCm ?? 0) > 0) {
      const half = this.config.holeHalfSideCm + params.holeRimCm!;
      for (const b of this.buildings) {
        for (let i = 0; i < b.roofHoles.length; i++) {
          shape('hole_rim', `${b.id}:roof_hole:${i}`, `roof:${b.id}`, square(b.roofHoles[i], half), 0, true);
        }
        for (let i = 0; i < b.floorHoles.length; i++) {
          shape(
            'hole_rim',
            `${b.id}:floor_hole:${i}`,
            PropsLayoutView.layerNameFor(b.id, b.floorHoles[i].floor),
            square(b.floorHoles[i].pos, half),
            0,
            true
          );
        }
      }
    }

    return out;
  }

  static isExcluded(shapes: PropsExclusion[], p: [number, number], walkLayer: string): boolean {
    for (const s of shapes) {
      if (exclusionContains(s, p, walkLayer)) return true;
    }
    return false;
  }

  getRoutes(): PropsRoute[] {
    return this.routes;
  }

  getMandatoryRouteIds(): string[] {
    return this.routes.filter((r) => r.bMandatory).map((r) => r.id);
  }

  findRoute(id: string): PropsRoute | undefined {
    return this.routes.find((r) => r.id === id);
  }

  getCovers(): PropsCover[] {
    return this.covers;
  }

  getZones(): PropsZone[] {
    return this.zones;
  }

  getBuildings(): PropsBuildingSummary[] {
    return this.buildings.map((b) => ({
      id: b.id,
      min: b.min,
      max: b.max,
      floors: b.floors,
      bHollow: b.bHollow,
      bEnterable: b.bEnterable,
      bRoofWalkable: b.bRoofWalkable,
    }));
  }

  distanceToRouteCm(p: [number, number], route: PropsRoute, layer?: string): number {
    let best = 2147483647;
    for (let i = 0; i + 1 < route.points.length; i++) {
      if (!layer || (route.layers[i] === layer && route.layers[i + 1] === layer)) {
        best = Math.min(best, ceilDistance(p, route.points[i], route.points[i + 1]));
      }
    }
    return best;
  }

  distanceToMandatoryRouteAnyLayerCm(p: [number, number]): number {
    let best = 2147483647;
    for (const route of this.routes) {
      if (route.bMandatory) {
        best = Math.min(best, this.distanceToRouteCm(p, route));
      }
    }
    return best;
  }

  getSightBlockers(eyeLayer: string): PropsSightSegment[] {
    const out: PropsSightSegment[] = [];
    const layerIndex = this.layers.findIndex((l) => l.name === eyeLayer);
    if (layerIndex === -1) {
      return out;
    }
    const K = BigInt(SIGHT_SCALE);
    const addWall = (a: [number, number], b: [number, number]) => {
      out.push({
        ax: BigInt(a[0]) * K,
        ay: BigInt(a[1]) * K,
        bx: BigInt(b[0]) * K,
        by: BigInt(b[1]) * K,
      });
    };
    const addCover = (c: InternalCover) => {
      const cs = cosQ(c.yaw);
      const sn = sinQ(c.yaw);
      const signs = [
        [1, 1],
        [-1, 1],
        [-1, -1],
        [1, -1],
      ];
      const x: bigint[] = [];
      const y: bigint[] = [];
      for (let i = 0; i < 4; i++) {
        const lx = BigInt(c.sizeX) * BigInt(signs[i][0]);
        const ly = BigInt(c.sizeY) * BigInt(signs[i][1]);
        x.push(BigInt(c.pos[0]) * K + cs * lx - sn * ly);
        y.push(BigInt(c.pos[1]) * K + sn * lx + cs * ly);
      }
      for (let i = 0; i < 4; i++) {
        out.push({
          ax: x[i],
          ay: y[i],
          bx: x[(i + 1) % 4],
          by: y[(i + 1) % 4],
        });
      }
    };

    const l = this.layers[layerIndex];
    if (l.kind === 'roof') {
      const eye = BigInt(l.level) * BigInt(this.config.levelHeightCm) + BigInt(this.config.eyeHeightCm);
      for (const w of this.walls) {
        if (BigInt(w.floor) * BigInt(this.config.levelHeightCm) + BigInt(w.height) >= eye) {
          addWall(w.a, w.b);
        }
      }
      for (const c of this.covers) {
        if (BigInt(c.heightCm) >= eye) {
          addCover(c);
        }
      }
      return out;
    }
    for (const w of this.layerWalls[layerIndex]) {
      addWall(w.a, w.b);
    }
    if (l.kind === 'ground') {
      for (const c of this.covers) {
        if (c.heightCm >= this.config.tallCoverMinCm) {
          addCover(c);
        }
      }
    }
    return out;
  }

  static isSightBlocked(
    from: [number, number],
    to: [number, number],
    blockers: PropsSightSegment[]
  ): boolean {
    const K = BigInt(SIGHT_SCALE);
    const px = BigInt(from[0]) * K;
    const py = BigInt(from[1]) * K;
    const dx = BigInt(to[0] - from[0]) * K;
    const dy = BigInt(to[1] - from[1]) * K;
    const minX = dx < 0n ? dx : 0n;
    const maxX = dx > 0n ? dx : 0n;
    const minY = dy < 0n ? dy : 0n;
    const maxY = dy > 0n ? dy : 0n;

    const orient = (ax: bigint, ay: bigint, bx: bigint, by: bigint, cx: bigint, cy: bigint): number => {
      const c = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
      return c > 0n ? 1 : c < 0n ? -1 : 0;
    };
    const onSeg = (ax: bigint, ay: bigint, bx: bigint, by: bigint, cx: bigint, cy: bigint): boolean => {
      const minAbX = ax < bx ? ax : bx;
      const maxAbX = ax > bx ? ax : bx;
      const minAbY = ay < by ? ay : by;
      const maxAbY = ay > by ? ay : by;
      return cx >= minAbX && cx <= maxAbX && cy >= minAbY && cy <= maxAbY;
    };

    for (const s of blockers) {
      const ax = s.ax - px;
      const ay = s.ay - py;
      const bx = s.bx - px;
      const by = s.by - py;
      const minSAbX = ax < bx ? ax : bx;
      const maxSAbX = ax > bx ? ax : bx;
      const minSAbY = ay < by ? ay : by;
      const maxSAbY = ay > by ? ay : by;

      if (maxSAbX < minX || minSAbX > maxX || maxSAbY < minY || minSAbY > maxY) {
        continue;
      }
      const o1 = orient(0n, 0n, dx, dy, ax, ay);
      const o2 = orient(0n, 0n, dx, dy, bx, by);
      const o3 = orient(ax, ay, bx, by, 0n, 0n);
      const o4 = orient(ax, ay, bx, by, dx, dy);
      if (o1 * o2 < 0 && o3 * o4 < 0) {
        return true;
      }
      if (
        (o1 === 0 && onSeg(0n, 0n, dx, dy, ax, ay)) ||
        (o2 === 0 && onSeg(0n, 0n, dx, dy, bx, by)) ||
        (o3 === 0 && onSeg(ax, ay, bx, by, 0n, 0n)) ||
        (o4 === 0 && onSeg(ax, ay, bx, by, dx, dy))
      ) {
        return true;
      }
    }
    return false;
  }
}
