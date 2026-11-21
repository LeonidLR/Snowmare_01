// Training Ground / level generator v2 - ROOM ANCHORS (BUILDINGS_TECH_SPEC.md section 2 GenRoom.anchor, S2_ROOMS_PLAN.md section 4.1).
// A room's `bounds` centre is not always a place a body can stand: the stair strip of a hollow hall is mostly floor gap + flight ramp, a
// room with a flight starting on its floor has the ramp across its middle. `anchor` is one walkable representative point of the room on
// its own floor, in the BUILDING frame (the frame of `rooms[].bounds`: unrotated, absolute cm; building_coord_fields.json rows
// `rooms[].anchor` / `floors_detail[].rooms[].anchor`). The engine smoke (LevelGenBuildingsSmoke) probes it instead of the centre.
// Blocked on the room's floor F: every floors_detail[F] gap (bounding box), every stair flight touching F (a ramp body from `pos` to
// `top`, one lane wide), the hatches and floor holes of F, the partitions of F (thin boxes). The anchor is the point of a 25 cm grid
// (inside the bounds inset by 50 cm, at least 50 cm clear of everything blocked) nearest the bounds centre; the centre itself when it
// qualifies; ties by the smaller x, then the smaller y. No such point -> no anchor.
// REACHABILITY: a pocket the entry cannot walk to is no anchor (B_hall_n: the north landing of floor 2 is cut off by the stair gap). With an
// entry seed - the cell nearest 60 cm past the top of a flight arriving in the room, else the room's first door by id - only cells of the
// 4-connected component (25 cm cells >= 35 cm = agent radius from every blocked box, partitions open at their doors) of that seed count;
// none at 50 cm clear -> the component's cells at 35 cm; a room without a seed is not restricted. Pure TS, deterministic.
import type { Vec2 } from '../movement/geometry.ts';
import { toBuildingFrame } from './rotation.ts';
import type { GenBuilding, GenLayout, GenRoom } from './schema.ts';
import { isSwitchback, stairFlights } from './schema.ts';

/** The grid step of the candidate points, cm. */
export const ANCHOR_GRID_CM = 25;
/** The candidate points keep this far inside the room's bounds and from every blocked box, cm. */
export const ANCHOR_CLEARANCE_CM = 50;
/** Half the width of a flight's ramp, cm: generate.ts STAIR_FLIGHT_W (150) / 2 + 5 cm of lane wall (the roomAnchors test pins the equality). */
export const ANCHOR_FLIGHT_HALF_W_CM = 80;
/** Half the side of the square a `floor_holes` entry opens in its slab, cm (build_levelgen_map.py slabs_with_holes default). */
export const ANCHOR_FLOOR_HOLE_HALF_CM = 120;
/** A hatch opening when it names no size, cm (build_levelgen_map.py). */
const HATCH_DEFAULT_SIZE: Vec2 = [100, 100];
/** A partition wall's thickness when it names none, cm (schema GenPartition). */
const PARTITION_DEFAULT_T_CM = 20;

/** An oriented box: from `o` along the unit vector `u` for `len`, `hw` to each side. */
interface OBox { o: Vec2; u: Vec2; len: number; hw: number }

const aabb = (x0: number, y0: number, x1: number, y1: number): OBox => ({ o: [x0, (y0 + y1) / 2], u: [1, 0], len: x1 - x0, hw: (y1 - y0) / 2 });

/** The box of the segment a -> b, `hw` to each side (a degenerate segment: a square of side 2 x hw). */
function segBox(a: Vec2, b: Vec2, hw: number): OBox {
  const dx = b[0] - a[0], dy = b[1] - a[1], L = Math.hypot(dx, dy);
  if (L < 1e-9) return aabb(a[0] - hw, a[1] - hw, a[0] + hw, a[1] + hw);
  return { o: a, u: [dx / L, dy / L], len: L, hw };
}

/** The distance from p to the box (0 inside). */
function distToBox(p: Vec2, bx: OBox): number {
  const dx = p[0] - bx.o[0], dy = p[1] - bx.o[1];
  const s = dx * bx.u[0] + dy * bx.u[1], t = -dx * bx.u[1] + dy * bx.u[0];
  const ds = s < 0 ? -s : s > bx.len ? s - bx.len : 0;
  const dt = Math.max(0, Math.abs(t) - bx.hw);
  return Math.hypot(ds, dt);
}

/** What a floor of the building offers the anchor search (building frame). */
interface FloorCtx {
  /** Clearance boxes: gaps, flights touching the floor, hatches, floor holes, whole partitions. */
  solid: OBox[];
  /** Connectivity boxes: the same, but a partition is cut open at its doors. */
  walk: OBox[];
  /** Flights that bring a man up onto this floor: their top and the unit direction pos -> top. */
  arrivals: { top: Vec2; u: Vec2 }[];
}
interface BuildingCtx { floor(f: number): FloorCtx; doors: Map<string, { pos: Vec2; floor: number }> }

/** Walking radius of an operative, cm: the connectivity grid keeps this far from every blocked box. */
export const ANCHOR_AGENT_RADIUS_CM = 35;
/** The room entry seed lies this far past a flight top, cm. */
const ARRIVAL_STEP_CM = 60;
/** A flight top within this of the room bounds belongs to the room, cm. */
const ARRIVAL_TOUCH_CM = 60;
/** A door width when it names none, cm. */
const DOOR_DEFAULT_W_CM = 120;

/** A partition as boxes: one, or cut open at its door openings (`cut`). */
function partitionBoxes(b: GenBuilding, pt: NonNullable<GenBuilding['partitions']>[number], cut: boolean): OBox[] {
  const F = (p: readonly number[]): Vec2 => toBuildingFrame(b, p);
  const a = F(pt.start), e = F(pt.end), hw = (pt.thickness ?? PARTITION_DEFAULT_T_CM) / 2;
  const full = segBox(a, e, hw);
  if (!cut || full.len < 1e-9) return [full];
  const doors = (pt.openings ?? []).filter((o) => o.kind === 'door').map((o) => {
    const p = F(o.pos), t = (p[0] - a[0]) * full.u[0] + (p[1] - a[1]) * full.u[1], w = (o.width ?? DOOR_DEFAULT_W_CM) / 2;
    return [Math.max(0, t - w), Math.min(full.len, t + w)] as [number, number];
  }).filter((iv) => iv[1] > iv[0]).sort((x, y) => x[0] - y[0]);
  const out: OBox[] = [];
  let at = 0;
  for (const [s, f] of doors) {
    if (s > at) out.push({ ...full, o: [a[0] + full.u[0] * at, a[1] + full.u[1] * at], len: s - at });
    at = Math.max(at, f);
  }
  if (at < full.len) out.push({ ...full, o: [a[0] + full.u[0] * at, a[1] + full.u[1] * at], len: full.len - at });
  return out;
}

function buildingCtx(b: GenBuilding): BuildingCtx {
  const F = (p: readonly number[]): Vec2 => toBuildingFrame(b, p);
  const floors = new Map<number, FloorCtx>();
  const doors = new Map<string, { pos: Vec2; floor: number }>();
  for (const op of [...(b.openings ?? []), ...(b.partitions ?? []).flatMap((pt) => pt.openings ?? [])]) if (op.kind === 'door' && op.id) doors.set(op.id, { pos: F(op.pos), floor: op.floor });
  const build = (floor: number): FloorCtx => {
    const common: OBox[] = [];
    for (const fd of b.floors_detail ?? []) {
      if (fd.floor !== floor) continue;
      for (const g of fd.gaps ?? []) {
        if (!g.poly.length) continue;
        const q = g.poly.map(F);
        common.push(aabb(Math.min(...q.map((p) => p[0])), Math.min(...q.map((p) => p[1])), Math.max(...q.map((p) => p[0])), Math.max(...q.map((p) => p[1]))));
      }
    }
    // a flight starting on the floor is a ramp across it; one arriving on it runs below it (under the gap), blocked all the same
    const arrivals: FloorCtx['arrivals'] = [];
    for (const st of b.stairs ?? []) {
      if (st.from !== floor && st.to !== floor) continue;
      if (isSwitchback(st)) {
        // a half-landing switchback: both flight bodies, and its mid landing over the floor it starts on (130 cm under it: no standing room)
        const fl = stairFlights(st).map((x) => segBox(F(x.a), F(x.b), st.width ? st.width / 2 + 5 : ANCHOR_FLIGHT_HALF_W_CM));
        common.push(...fl);
        if (st.from === floor && st.mid_rect) { const [[x0, y0], [x1, y1]] = st.mid_rect; common.push(aabb(Math.min(x0, x1), Math.min(y0, y1), Math.max(x0, x1), Math.max(y0, y1))); }
        if (st.to === floor && !st.blocked && !st.down_only && fl[1].len > 1e-9) arrivals.push({ top: F(st.top), u: fl[1].u });
        continue;
      }
      const pos = F(st.pos), top = F(st.top), box = segBox(pos, top, ANCHOR_FLIGHT_HALF_W_CM);
      common.push(box);
      if (st.to === floor && !st.blocked && !st.down_only && box.len > 1e-9) arrivals.push({ top, u: box.u });
    }
    for (const h of b.hatches ?? []) {
      if (h.floor !== floor) continue;
      const [sx, sy] = h.size ?? HATCH_DEFAULT_SIZE, c = F(h.pos);
      common.push(aabb(c[0] - sx / 2, c[1] - sy / 2, c[0] + sx / 2, c[1] + sy / 2));
    }
    for (const fh of b.floor_holes ?? []) {
      if (fh.floor !== floor) continue;
      const c = F(fh.pos), r = ANCHOR_FLOOR_HOLE_HALF_CM;
      common.push(aabb(c[0] - r, c[1] - r, c[0] + r, c[1] + r));
    }
    const parts = (b.partitions ?? []).filter((pt) => pt.floor === floor);
    return { solid: [...common, ...parts.flatMap((pt) => partitionBoxes(b, pt, false))], walk: [...common, ...parts.flatMap((pt) => partitionBoxes(b, pt, true))], arrivals };
  };
  return { floor: (f) => { let c = floors.get(f); if (!c) { c = build(f); floors.set(f, c); } return c; }, doors };
}

const round2 = (v: number): number => { const r = Math.round(v * 100) / 100; return Object.is(r, -0) ? 0 : r; };

/** The walkable representative point of `room` on its floor, in the building frame (see the file header); undefined when none. */
export function roomAnchor(b: GenBuilding, room: GenRoom): Vec2 | undefined {
  return anchorAmong(room, buildingCtx(b));
}

function anchorAmong(room: GenRoom, bc: BuildingCtx): Vec2 | undefined {
  const fc = bc.floor(room.floor), { min, max } = room.bounds, g = ANCHOR_GRID_CM, R = ANCHOR_AGENT_RADIUS_CM, C = ANCHOR_CLEARANCE_CM;
  const centre: Vec2 = [round2((min[0] + max[0]) / 2), round2((min[1] + max[1]) / 2)];
  const clear = (p: Vec2, boxes: readonly OBox[], d: number): boolean => boxes.every((bx) => distToBox(p, bx) >= d - 1e-6);
  // the connectivity grid: 25 cm cells at least R from the bounds and from every blocked box (x ascends in the outer loop, y in the inner)
  const xs: number[] = [], ys: number[] = [];
  for (let x = Math.ceil((min[0] + R) / g) * g; x <= max[0] - R + 1e-9; x += g) xs.push(x);
  for (let y = Math.ceil((min[1] + R) / g) * g; y <= max[1] - R + 1e-9; y += g) ys.push(y);
  const ok = xs.map((x) => ys.map((y) => clear([x, y], fc.walk, R)));
  // the entry seed: past the top of a flight that arrives in the room, else at the first door of the room (by id)
  let seeds: Vec2[] = fc.arrivals
    .filter((a) => a.top[0] >= min[0] - ARRIVAL_TOUCH_CM && a.top[0] <= max[0] + ARRIVAL_TOUCH_CM && a.top[1] >= min[1] - ARRIVAL_TOUCH_CM && a.top[1] <= max[1] + ARRIVAL_TOUCH_CM)
    .map((a) => [a.top[0] + a.u[0] * ARRIVAL_STEP_CM, a.top[1] + a.u[1] * ARRIVAL_STEP_CM] as Vec2);
  if (!seeds.length) {
    const door = [...(room.door_ids ?? [])].sort().map((id) => bc.doors.get(id)).find((d) => d && d.floor === room.floor);
    if (door) seeds = [door.pos];
  }
  const restricted = seeds.length > 0;
  let inComp: boolean[][] | undefined;
  if (restricted) {
    const comp = xs.map(() => ys.map(() => false));
    inComp = comp;
    const stack: [number, number][] = [];
    for (const s of seeds) {
      let best: [number, number] | undefined, bestD = Infinity;
      for (let i = 0; i < xs.length; i++) for (let j = 0; j < ys.length; j++) {
        const d = (xs[i] - s[0]) ** 2 + (ys[j] - s[1]) ** 2;
        if (ok[i][j] && d < bestD - 1e-9) { best = [i, j]; bestD = d; }
      }
      if (best && !comp[best[0]][best[1]]) { comp[best[0]][best[1]] = true; stack.push(best); }
    }
    while (stack.length) {
      const [i, j] = stack.pop()!;
      for (const [di, dj] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
        const a = i + di, c = j + dj;
        if (a >= 0 && c >= 0 && a < xs.length && c < ys.length && ok[a][c] && !comp[a][c]) { comp[a][c] = true; stack.push([a, c]); }
      }
    }
  }
  const nearest = (clearance: number): Vec2 | undefined => {
    let best: Vec2 | undefined, bestD = Infinity;
    for (let i = 0; i < xs.length; i++) for (let j = 0; j < ys.length; j++) {
      if (!ok[i][j] || (inComp && !inComp[i][j])) continue;
      const p: Vec2 = [xs[i], ys[j]];
      if (p[0] < min[0] + C || p[0] > max[0] - C || p[1] < min[1] + C || p[1] > max[1] - C) continue;
      if (!clear(p, fc.solid, clearance)) continue;
      const d = (p[0] - centre[0]) ** 2 + (p[1] - centre[1]) ** 2;
      // an equal distance keeps the first found = the smaller x, then the smaller y
      if (d < bestD - 1e-9) { best = p; bestD = d; }
    }
    return best;
  };
  // the centre itself when it is clear (and, with an entry seed, beside a cell the entry reaches)
  const inset = centre[0] >= min[0] + C && centre[0] <= max[0] - C && centre[1] >= min[1] + C && centre[1] <= max[1] - C;
  if (inset && clear(centre, fc.solid, C)) {
    if (!inComp) return centre;
    const reach = inComp;
    if (xs.some((x, i) => Math.abs(x - centre[0]) <= g && ys.some((y, j) => Math.abs(y - centre[1]) <= g && reach[i][j]))) return centre;
  }
  const best = nearest(C);
  if (best || !restricted) return best;
  return nearest(R); // an entry seed but no cell at 50 cm: the cells at the agent radius
}

/**
 * A copy of the building whose rooms (`rooms` and `floors_detail[].rooms`) carry their `anchor` (recomputed; a room without a walkable
 * point loses a stale one). A room object shared by both lists stays one object, and the per-floor blocked boxes are built once.
 * Nothing but `anchor` changes. A building that is not enterable is returned as it is.
 */
export function withRoomAnchors<T extends GenBuilding>(b: T): T {
  if (!b.enterable) return b;
  const bc = buildingCtx(b);
  const done = new Map<GenRoom, GenRoom>();
  const mapRoom = (room: GenRoom): GenRoom => {
    const seen = done.get(room);
    if (seen) return seen;
    const a = anchorAmong(room, bc);
    // a room that already carried its anchor keeps its key order (idempotent bytes); a new one is appended
    let out: GenRoom;
    if (a) out = { ...room, anchor: a };
    else { const { anchor: _stale, ...rest } = room; out = rest; }
    done.set(room, out);
    return out;
  };
  const out: T = { ...b };
  if (b.rooms) out.rooms = b.rooms.map(mapRoom);
  if (b.floors_detail) out.floors_detail = b.floors_detail.map((fd) => (fd.rooms ? { ...fd, rooms: fd.rooms.map(mapRoom) } : fd));
  return out;
}

/** The layout with every enterable building's rooms anchored (withRoomAnchors); other fields untouched. */
export function addRoomAnchors<T extends GenLayout>(layout: T): T {
  return { ...layout, buildings: layout.buildings.map((b) => withRoomAnchors(b)) };
}

/** The layout file's text style: BOM, line ending, indent, final newline - so a rewrite changes nothing but the anchors. */
export function textStyle(text: string): { bom: string; eol: string; indent: string | number | undefined; tail: string } {
  const bom = text.startsWith('\uFEFF') ? '\uFEFF' : '';
  const body = text.slice(bom.length);
  const eol = body.includes('\r\n') ? '\r\n' : '\n';
  const second = body.split(eol)[1];
  const ws = second === undefined ? '' : /^[ \t]*/.exec(second)![0];
  return { bom, eol, indent: ws === '' ? undefined : ws.startsWith('\t') ? '\t' : ws.length, tail: body.endsWith(eol) ? eol : '' };
}

/**
 * The text of a layout JSON file with the room anchors added / refreshed (addRoomAnchors), in the file's own style. Idempotent. Throws
 * when the file's formatting is not what JSON.stringify reproduces (the rewrite would change more than the anchors) unless `force`.
 */
export function anchorLayoutText(text: string, force = false): string {
  const st = textStyle(text);
  const render = (v: unknown): string => st.bom + JSON.stringify(v, null, st.indent).replace(/\n/g, st.eol) + st.tail;
  const layout = JSON.parse(text.slice(st.bom.length)) as GenLayout;
  if (!force && render(layout) !== text) throw new Error('the file is not in JSON.stringify style: a rewrite would change more than the anchors (use --force to rewrite it normalised)');
  return render(addRoomAnchors(layout));
}
