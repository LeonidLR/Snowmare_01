// Tactical checks for generated / authored combat buildings (inbox 027 item 3):
//   - Per-building room & roof reachability graph (doors, partition doors, stairs, hatches, ladders, holes -> street)
//   - Window / loophole clearance check (no opening firing straight into an adjacent wall or building)
//   - Firing lines from windows & loopholes onto the mandatory route (count + min/mean/max length + segments for UI)
//   - Cover share along routes, multi-level route verification, mantle edge validation, per-building and per-level scores

import type { Segment, Vec2 } from '../movement/geometry.ts';
import { dirOf, dist, distToWall, segmentBlocked, sub } from '../movement/geometry.ts';
import { wallSegments } from '../movement/layout.ts';
import type { GenBuilding, GenLayout, GenRoom, MantleEdge, WallOpening } from './schema.ts';
import {
  isMantleable,
  layerLevel,
  layersOf,
  levelZ,
  mandatoryRoute,
  NEVER_CLIMBABLE,
  OPENING_DEFAULTS,
  toFormationLayout,
  unifyOpenings,
} from './schema.ts';

export interface FiringLine {
  openingId: string;
  buildingId: string;
  kind: 'window' | 'loophole';
  floor: number;
  from: Vec2;
  to: Vec2;
  length_m: number;
}

export interface BlockedOpening {
  id: string;
  building: string;
  pos: Vec2;
  floor: number;
  kind: 'window' | 'loophole';
  reason: string;
}

export interface BuildingTacticalReport {
  id: string;
  set_piece: string;
  enterable: boolean;
  roof_walkable: boolean;
  isCombatBuilding: boolean;
  roomsTotal: number;
  roomsReachable: number;
  unreachableRooms: string[];
  roofReachable: boolean;
  doorsCount: number;
  windowsCount: number;
  loopholesCount: number;
  blockedWindows: BlockedOpening[];
  firingLinesCount: number;
  maxFiringLine_m: number;
  partitionsCount: number;
  hatchesCount: number;
  parapetsCount: number;
  mantleEdgesCount: number;
  score: number;
}

export interface TacticalReport {
  score: number;
  buildings: BuildingTacticalReport[];
  roomsTotal: number;
  roomsReachable: number;
  unreachableRooms: string[];
  blockedWindows: BlockedOpening[];
  firingLines: {
    openingsTotal: number;
    openingsWithLos: number;
    count: number;
    minLength_m: number;
    meanLength_m: number;
    maxLength_m: number;
    lines: FiringLine[];
  };
  routeCover: {
    share: number;
    samplesWithCover: number;
    samplesTotal: number;
    coversNearRoute: number;
  };
  multiLevelRoutes: {
    exists: boolean;
    count: number;
    levelsUsed: number[];
  };
  mantle: {
    count: number;
    valid: number;
    invalid: number;
    byKind: Record<string, number>;
    potentialCount: number;
  };
}

const EYE_CM = 160;
const WALL_CLEAR_CM = 150;
const MAX_FIRING_CM = 15000;
const COVER_NEAR_CM = 400;
const COVER_COUNT_CM = 600;
const ROOM_TOUCH_TOL_CM = 60;

const round = (v: number, digits = 0) => {
  const k = 10 ** digits;
  const r = Math.round(v * k) / k;
  return Object.is(r, -0) ? 0 : r;
};

/** Outward unit normal for an opening along a building's perimeter. */
export function openingOutwardDir(b: GenBuilding, op: WallOpening): Vec2 {
  if (typeof op.yaw === 'number' && Number.isFinite(op.yaw)) return dirOf(op.yaw);
  const [[x0, y0], [x1, y1]] = b.footprint;
  const dW = Math.abs(op.pos[0] - x0);
  const dE = Math.abs(op.pos[0] - x1);
  const dN = Math.abs(op.pos[1] - y0);
  const dS = Math.abs(op.pos[1] - y1);
  const m = Math.min(dW, dE, dN, dS);
  if (m === dW) return [-1, 0];
  if (m === dE) return [1, 0];
  if (m === dN) return [0, -1];
  return [0, 1];
}

function pointInBounds(p: Vec2, bounds: { min: Vec2; max: Vec2 }, tol = ROOM_TOUCH_TOL_CM): boolean {
  return p[0] >= bounds.min[0] - tol && p[0] <= bounds.max[0] + tol && p[1] >= bounds.min[1] - tol && p[1] <= bounds.max[1] + tol;
}

/** Build the room & roof reachability graph for one building and BFS from 'street'. */
function checkBuildingReachability(l: GenLayout, b: GenBuilding): {
  roomsTotal: number;
  roomsReachable: number;
  unreachableRooms: string[];
  roofReachable: boolean;
} {
  const roofNode = 'roof';
  const streetNode = 'street';
  const adj = new Map<string, Set<string>>();
  const addNode = (u: string) => {
    if (!adj.has(u)) adj.set(u, new Set());
  };
  const addEdge = (u: string, v: string) => {
    addNode(u);
    addNode(v);
    adj.get(u)!.add(v);
    adj.get(v)!.add(u);
  };

  addNode(streetNode);
  if (b.roof_walkable) addNode(roofNode);

  // Build room list (or fallback virtual per-floor rooms when legacy building omits `rooms`)
  const walkableFloors = b.hollow ? 1 : b.floors;
  const rooms: GenRoom[] = [];
  if (b.enterable) {
    if (b.rooms && b.rooms.length > 0) {
      rooms.push(...b.rooms);
    } else {
      for (let f = 0; f < walkableFloors; f++) {
        rooms.push({
          id: `${b.id}_floor_${f}`,
          floor: f,
          bounds: { min: b.footprint[0], max: b.footprint[1] },
          kind: b.hollow ? 'hall' : 'room',
        });
      }
    }
    for (const rm of rooms) addNode(rm.id);
  }

  const roomsAt = (floor: number, pos: Vec2, fallbackSingle = false): GenRoom[] => {
    const onFloor = rooms.filter((rm) => rm.floor === floor);
    const hits = onFloor.filter((rm) => pointInBounds(pos, rm.bounds, ROOM_TOUCH_TOL_CM));
    if (hits.length > 0) return hits;
    if (fallbackSingle && onFloor.length === 1) return onFloor;
    return [];
  };

  if (b.enterable) {
    // 1. Exterior doors (floor 0 -> street) and any upper-floor openings acting as doors
    const openings = unifyOpenings(b);
    for (const op of openings) {
      if (op.kind !== 'door') continue;
      const hits = rooms.filter(
        (rm) => rm.floor === op.floor && ((op.id && rm.door_ids?.includes(op.id)) || pointInBounds(op.pos, rm.bounds, ROOM_TOUCH_TOL_CM)),
      );
      if (op.floor === 0) {
        for (const rm of hits) addEdge(streetNode, rm.id);
      } else {
        for (let i = 1; i < hits.length; i++) addEdge(hits[0].id, hits[i].id);
      }
    }

    // 2. Interior partition doors connecting rooms on same floor
    for (const pt of b.partitions ?? []) {
      for (const op of pt.openings ?? []) {
        if (op.kind !== 'door') continue;
        const hits = rooms.filter(
          (rm) => rm.floor === pt.floor && ((op.id && rm.door_ids?.includes(op.id)) || pointInBounds(op.pos, rm.bounds, ROOM_TOUCH_TOL_CM)),
        );
        for (let i = 1; i < hits.length; i++) addEdge(hits[0].id, hits[i].id);
      }
    }

    // Also connect rooms sharing a common door_id (for hand-crafted room graphs without partition records)
    const byDoor = new Map<string, GenRoom[]>();
    for (const rm of rooms) {
      for (const did of rm.door_ids ?? []) {
        const list = byDoor.get(did) ?? [];
        list.push(rm);
        byDoor.set(did, list);
      }
    }
    for (const group of byDoor.values()) {
      for (let i = 1; i < group.length; i++) addEdge(group[0].id, group[i].id);
    }

    // 3. Stairs
    for (const st of b.stairs ?? []) {
      const fromNodes = st.kind === 'external'
        ? [streetNode]
        : roomsAt(st.from, st.pos, true).map((rm) => rm.id);
      const toNodes = st.to >= b.floors && b.roof_walkable
        ? [roofNode]
        : roomsAt(st.to, st.top, true).map((rm) => rm.id);
      for (const fn of fromNodes) for (const tn of toNodes) addEdge(fn, tn);
    }

    // 4. Roof hatches (link floor -> roof)
    for (const ht of b.hatches ?? []) {
      if (!b.roof_walkable) continue;
      for (const rm of roomsAt(ht.floor, ht.pos, true)) addEdge(rm.id, roofNode);
    }

    // 5. Roof holes and floor holes (ruined two-level)
    for (const rh of b.roof_holes ?? []) {
      if (!b.roof_walkable || b.floors < 1) continue;
      const topFloor = b.hollow ? 0 : b.floors - 1;
      for (const rm of roomsAt(topFloor, rh, false)) addEdge(roofNode, rm.id);
    }
    for (const fh of b.floor_holes ?? []) {
      const upper = roomsAt(fh.floor, fh.pos, false);
      const lower = roomsAt(Math.max(0, fh.floor - 1), fh.pos, false);
      for (const u of upper) for (const lo of lower) addEdge(u.id, lo.id);
    }
  }

  // 6. Ladders attached to this building (street/ground or interior floor -> roof or upper floor)
  for (const ld of l.ladders ?? []) {
    if (ld.to === `roof:${b.id}` && b.roof_walkable) {
      if (ld.from === 'ground' || ld.from === 'cut') {
        addEdge(streetNode, roofNode);
      } else if (ld.from.startsWith(`floor:${b.id}:`) && b.enterable) {
        const fl = Number(ld.from.split(':')[2]);
        for (const rm of roomsAt(fl, ld.foot, true)) addEdge(rm.id, roofNode);
      }
    } else if (ld.to.startsWith(`floor:${b.id}:`) && b.enterable) {
      const fl = Number(ld.to.split(':')[2]);
      const srcNodes = ld.from === 'ground'
        ? [streetNode]
        : ld.from.startsWith(`floor:${b.id}:`)
          ? roomsAt(Number(ld.from.split(':')[2]), ld.foot, true).map((r) => r.id)
          : [];
      for (const sn of srcNodes) for (const rm of roomsAt(fl, ld.top, true)) addEdge(sn, rm.id);
    }
  }

  // BFS from street
  const visited = new Set<string>([streetNode]);
  const queue = [streetNode];
  while (queue.length > 0) {
    const u = queue.shift()!;
    for (const v of adj.get(u) ?? []) {
      if (!visited.has(v)) {
        visited.add(v);
        queue.push(v);
      }
    }
  }

  const unreachableRooms = rooms.filter((rm) => !visited.has(rm.id)).map((rm) => rm.id);
  const roofReachable = !b.roof_walkable || visited.has(roofNode);
  return {
    roomsTotal: rooms.length,
    roomsReachable: rooms.length - unreachableRooms.length,
    unreachableRooms,
    roofReachable,
  };
}

/** Main entry: compute comprehensive tactical checks for a layout and its sampled mandatory route. */
export function evaluateTactical(
  l: GenLayout,
  routeSamples: { pos: Vec2; layer: string; heading: number }[],
  coverSegsFn: (c: GenLayout['covers'][number]) => Segment[],
  tops: { seg: Segment; top: number }[],
): TacticalReport {
  const layers = layersOf(l);
  const mandatory = mandatoryRoute(l);

  // Layer wall cache for cover-near-route checks
  const layerSegsCache = new Map<string, Segment[]>();
  const getLayerSegs = (layer: string): Segment[] => {
    let segs = layerSegsCache.get(layer);
    if (!segs) {
      try {
        segs = wallSegments(toFormationLayout(l, layer));
      } catch {
        segs = [];
      }
      layerSegsCache.set(layer, segs);
    }
    return segs;
  };

  // Pre-build obstacle segments with metadata for aperture blockage checks
  const wallObstacles = l.walls.map((w) => ({
    id: w.id,
    building: w.building,
    floor: w.floor ?? 0,
    botZ: levelZ(w.floor ?? 0),
    topZ: levelZ(w.floor ?? 0) + w.height,
    seg: { a: w.a, b: w.b, thickness: w.thickness } as Segment,
  }));

  const blockedWindows: BlockedOpening[] = [];
  const firingLines: FiringLine[] = [];
  const buildingReports: BuildingTacticalReport[] = [];

  let openingsTotal = 0;
  let openingsWithLos = 0;
  let totalRooms = 0;
  let totalRoomsReachable = 0;
  const allUnreachableRooms: string[] = [];

  for (const b of l.buildings) {
    const isCombatBuilding = b.enterable || (b.roof_walkable && !NEVER_CLIMBABLE.includes(b.set_piece));
    const reach = checkBuildingReachability(l, b);
    totalRooms += reach.roomsTotal;
    totalRoomsReachable += reach.roomsReachable;
    allUnreachableRooms.push(...reach.unreachableRooms);

    const openings = unifyOpenings(b);
    const doorsCount = openings.filter((o) => o.kind === 'door').length;
    const apertures = openings.filter((o) => o.kind === 'window' || o.kind === 'loophole');
    const windowsCount = apertures.filter((o) => o.kind === 'window').length;
    const loopholesCount = apertures.filter((o) => o.kind === 'loophole').length;

    const bBlocked: BlockedOpening[] = [];
    const bLines: FiringLine[] = [];

    for (const op of apertures) {
      const kind = op.kind as 'window' | 'loophole';
      const opId = op.id ?? `${b.id}_${kind}_${Math.round(op.pos[0])}_${Math.round(op.pos[1])}`;
      openingsTotal++;

      const dir = openingOutwardDir(b, op);
      const sill = op.sill_height ?? OPENING_DEFAULTS[kind].sill;
      const topH = op.top_height ?? OPENING_DEFAULTS[kind].top;
      const baseZ = levelZ(op.floor);
      const opBotZ = baseZ + sill;
      const opTopZ = baseZ + topH;

      // 1. Clearance check outside the aperture (no wall/building directly blocking within 150 cm)
      const probeStart: Vec2 = [op.pos[0] + dir[0] * 35, op.pos[1] + dir[1] * 35];
      const probeEnd: Vec2 = [op.pos[0] + dir[0] * WALL_CLEAR_CM, op.pos[1] + dir[1] * WALL_CLEAR_CM];

      let blockReason: string | null = null;
      for (const wo of wallObstacles) {
        if (wo.topZ <= opBotZ || wo.botZ >= opTopZ) continue;
        // Skip host perimeter wall right at the opening
        if (wo.building === b.id && wo.floor === 0 && distToWall(op.pos, wo.seg) < 40) continue;
        if (segmentBlocked(probeStart, probeEnd, [wo.seg], 0)) {
          blockReason = `blocked by wall ${wo.id} within ${WALL_CLEAR_CM} cm`;
          break;
        }
      }
      if (!blockReason) {
        for (const other of l.buildings) {
          if (other.id === b.id) continue;
          if (other.height <= opBotZ) continue;
          const [[ox0, oy0], [ox1, oy1]] = other.footprint;
          if (
            (probeStart[0] >= ox0 - 20 && probeStart[0] <= ox1 + 20 && probeStart[1] >= oy0 - 20 && probeStart[1] <= oy1 + 20) ||
            (probeEnd[0] >= ox0 && probeEnd[0] <= ox1 && probeEnd[1] >= oy0 && probeEnd[1] <= oy1)
          ) {
            blockReason = `opens directly into building ${other.id}`;
            break;
          }
        }
      }

      if (blockReason) {
        const blk: BlockedOpening = { id: opId, building: b.id, pos: op.pos, floor: op.floor, kind, reason: blockReason };
        bBlocked.push(blk);
        blockedWindows.push(blk);
        continue;
      }

      // 2. Firing lines from unblocked aperture onto the mandatory route samples within +-75 deg field of fire
      const origin: Vec2 = [op.pos[0] + dir[0] * 40, op.pos[1] + dir[1] * 40];
      const eyeZ = baseZ + Math.min(EYE_CM, (sill + topH) / 2);

      // Obstacles strictly excluding the aperture's host facade wall segment
      const losObstaclesCache = new Map<number, Segment[]>();
      const blockersAt = (zThreshold: number): Segment[] => {
        let segs = losObstaclesCache.get(zThreshold);
        if (!segs) {
          segs = tops
            .filter((t) => t.top >= zThreshold && distToWall(op.pos, t.seg) >= 35)
            .map((t) => t.seg);
          losObstaclesCache.set(zThreshold, segs);
        }
        return segs;
      };

      // Group consecutive visible route samples into contiguous firing-arc runs, keeping the closest line per run
      let activeRun: { pos: Vec2; distCm: number }[] = [];
      const runs: { pos: Vec2; distCm: number }[][] = [];
      const flushRun = () => {
        if (activeRun.length > 0) {
          runs.push(activeRun);
          activeRun = [];
        }
      };

      for (const smp of routeSamples) {
        const dVec = sub(smp.pos, origin);
        const dCm = Math.hypot(dVec[0], dVec[1]);
        if (dCm < 80 || dCm > MAX_FIRING_CM) {
          flushRun();
          continue;
        }
        const dotFwd = (dVec[0] * dir[0] + dVec[1] * dir[1]) / dCm;
        if (dotFwd < 0.25) {
          flushRun();
          continue;
        }
        const smpLevel = layers.includes(smp.layer) ? layerLevel(l, smp.layer) : 0;
        const smpEyeZ = levelZ(smpLevel) + EYE_CM;
        const obs = blockersAt(Math.min(eyeZ, smpEyeZ));
        if (!segmentBlocked(origin, smp.pos, obs, 0)) {
          activeRun.push({ pos: smp.pos, distCm: dCm });
        } else {
          flushRun();
        }
      }
      flushRun();

      if (runs.length > 0) {
        openingsWithLos++;
        for (const run of runs.slice(0, 2)) {
          // Pick best (closest) point in the visible run as representative firing line
          const best = run.reduce((a, c) => (c.distCm < a.distCm ? c : a), run[0]);
          const line: FiringLine = {
            openingId: opId,
            buildingId: b.id,
            kind,
            floor: op.floor,
            from: origin,
            to: best.pos,
            length_m: round(best.distCm / 100, 1),
          };
          bLines.push(line);
          firingLines.push(line);
        }
      }
    }

    const partitionsCount = (b.partitions?.length ?? 0) || Math.floor(l.walls.filter((w) => w.building === b.id && w.kind === 'interior').length / 2);
    const hatchesCount = b.hatches?.length ?? 0;
    const parapetsCount = b.parapets?.length ?? (b.roof_walkable ? 4 : 0);
    const mantleEdgesCount = b.mantle_edges?.length ?? 0;

    let bScore = 1;
    if (!b.enterable && b.roof_walkable && !NEVER_CLIMBABLE.includes(b.set_piece)) {
      bScore = round(0.6 * (reach.roofReachable ? 1 : 0) + 0.4 * (parapetsCount > 0 ? 1 : 0), 3);
    } else if (b.enterable) {
      const reachScore = 0.7 * (reach.roomsTotal > 0 ? reach.roomsReachable / reach.roomsTotal : 1) + 0.3 * (reach.roofReachable ? 1 : 0);
      const apertureClearScore = apertures.length > 0 ? 1 - bBlocked.length / apertures.length : 1;
      const combatUtility = (
        (apertures.length > 0 ? Math.min(1, apertures.length / 2) : 0.6) +
        (b.roof_walkable ? (parapetsCount > 0 ? 1 : 0.4) : 1) +
        (b.floors > 1 ? (partitionsCount > 0 || (b.stairs?.length ?? 0) > 0 || hatchesCount > 0 ? 1 : 0.5) : 1)
      ) / 3;
      bScore = round(0.45 * reachScore + 0.30 * apertureClearScore + 0.25 * combatUtility, 3);
      if (reach.unreachableRooms.length > 0 || !reach.roofReachable) bScore = Math.min(bScore, 0.49);
      if (bBlocked.length > 0) bScore = Math.min(bScore, 0.59);
    }

    buildingReports.push({
      id: b.id,
      set_piece: b.set_piece,
      enterable: b.enterable,
      roof_walkable: b.roof_walkable,
      isCombatBuilding,
      roomsTotal: reach.roomsTotal,
      roomsReachable: reach.roomsReachable,
      unreachableRooms: reach.unreachableRooms,
      roofReachable: reach.roofReachable,
      doorsCount,
      windowsCount,
      loopholesCount,
      blockedWindows: bBlocked,
      firingLinesCount: bLines.length,
      maxFiringLine_m: bLines.length ? Math.max(...bLines.map((x) => x.length_m)) : 0,
      partitionsCount,
      hatchesCount,
      parapetsCount,
      mantleEdgesCount,
      score: bScore,
    });
  }

  // Firing lines summary stats
  const flLens = firingLines.map((x) => x.length_m);
  const flSummary = {
    openingsTotal,
    openingsWithLos,
    count: firingLines.length,
    minLength_m: flLens.length ? round(Math.min(...flLens), 1) : 0,
    meanLength_m: flLens.length ? round(flLens.reduce((a, v) => a + v, 0) / flLens.length, 1) : 0,
    maxLength_m: flLens.length ? round(Math.max(...flLens), 1) : 0,
    lines: firingLines,
  };

  // Cover along mandatory route
  const coverBoxes = l.covers.map((c) => ({
    pos: c.pos,
    half: Math.max(c.size[0], c.size[1]) / 2,
    segs: coverSegsFn(c),
  }));
  let samplesWithCover = 0;
  for (const smp of routeSamples) {
    const segs = getLayerSegs(smp.layer);
    let hasCov = segs.some((s) => distToWall(smp.pos, s) <= COVER_NEAR_CM);
    if (!hasCov && smp.layer === 'ground') {
      hasCov = coverBoxes.some((cb) => Math.max(0, dist(smp.pos, cb.pos) - cb.half) <= COVER_NEAR_CM);
    }
    if (hasCov) samplesWithCover++;
  }
  const routePts = mandatory.points;
  const nearRouteSegment = (p: Vec2, threshold: number): boolean => {
    for (let i = 1; i < routePts.length; i++) {
      const a = routePts[i - 1], b = routePts[i];
      const abx = b[0] - a[0], aby = b[1] - a[1];
      const l2 = abx * abx + aby * aby;
      const t = l2 === 0 ? 0 : Math.max(0, Math.min(1, ((p[0] - a[0]) * abx + (p[1] - a[1]) * aby) / l2));
      if (dist(p, [a[0] + abx * t, a[1] + aby * t]) <= threshold) return true;
    }
    return false;
  };
  const coversNearRoute = coverBoxes.filter((cb) => nearRouteSegment(cb.pos, COVER_COUNT_CM + cb.half)).length;
  const routeCover = {
    share: routeSamples.length ? round(samplesWithCover / routeSamples.length, 3) : 0,
    samplesWithCover,
    samplesTotal: routeSamples.length,
    coversNearRoute,
  };

  // Multi-level routes
  const allLevelsUsed = new Set<number>();
  let multiCount = 0;
  for (const r of l.routes) {
    const rLevels = new Set(
      (r.layers ?? r.points.map(() => 'ground')).map((lay) => (layers.includes(lay) ? layerLevel(l, lay) : 0)),
    );
    for (const lv of rLevels) allLevelsUsed.add(lv);
    if (rLevels.size >= 2) multiCount++;
  }
  const multiLevelRoutes = {
    exists: multiCount >= 1,
    count: multiCount,
    levelsUsed: [...allLevelsUsed].sort((a, b) => a - b),
  };

  // Mantle edges validation & census
  const edgeMap = new Map<string, MantleEdge>();
  for (const me of l.mantle_edges ?? []) edgeMap.set(me.id, me);
  for (const b of l.buildings) for (const me of b.mantle_edges ?? []) edgeMap.set(me.id, me);
  let validMantle = 0;
  let invalidMantle = 0;
  const byKind: Record<string, number> = {};
  for (const me of edgeMap.values()) {
    if (isMantleable(me.height_cm)) {
      validMantle++;
      const k = me.kind ?? 'ledge';
      byKind[k] = (byKind[k] ?? 0) + 1;
    } else {
      invalidMantle++;
    }
  }
  // Potential mantleables across buildings/covers (windows/parapets/covers in 50..150 cm)
  const potentialCount =
    l.buildings.reduce((acc, b) => {
      const winCount = unifyOpenings(b).filter((o) => o.kind === 'window' && isMantleable(o.sill_height ?? OPENING_DEFAULTS.window.sill)).length;
      const paraCount = b.roof_walkable ? (b.parapets?.filter((p) => isMantleable(p.height_cm)).length ?? 4) : 0;
      return acc + winCount + paraCount;
    }, 0) + l.covers.filter((c) => isMantleable(c.size[2])).length;

  const mantle = {
    count: edgeMap.size,
    valid: validMantle,
    invalid: invalidMantle,
    byKind,
    potentialCount,
  };

  // Overall level tactical score
  const combatBuildings = buildingReports.filter((b) => b.isCombatBuilding);
  const combatMeanScore = combatBuildings.length
    ? combatBuildings.reduce((s, b) => s + b.score, 0) / combatBuildings.length
    : 1;
  const reachRatio = totalRooms > 0 ? totalRoomsReachable / totalRooms : 1;
  const windowClearRatio = openingsTotal > 0 ? 1 - blockedWindows.length / openingsTotal : 1;
  const losRatio = openingsTotal > 0 ? Math.min(1, openingsWithLos / Math.max(1, Math.min(4, openingsTotal))) : 1;
  const coverRatio = Math.min(1, routeCover.share / 0.5);
  const verticalityRatio = 0.5 * (multiLevelRoutes.exists ? 1 : 0) + 0.5 * Math.min(1, mantle.valid / 4);

  const score = round(
    0.30 * combatMeanScore +
    0.20 * reachRatio +
    0.15 * windowClearRatio +
    0.15 * losRatio +
    0.10 * coverRatio +
    0.10 * verticalityRatio,
    3,
  );

  return {
    score,
    buildings: buildingReports,
    roomsTotal: totalRooms,
    roomsReachable: totalRoomsReachable,
    unreachableRooms: allUnreachableRooms,
    blockedWindows,
    firingLines: flSummary,
    routeCover,
    multiLevelRoutes,
    mantle,
  };
}
