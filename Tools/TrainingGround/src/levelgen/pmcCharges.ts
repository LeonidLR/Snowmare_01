// Training Ground / level generator - the CHARGES of the PMC defend posts (docs/design/PMC_DEFENCE.md D2, designer clip notes
// 2026-11-18 notes 1, 1b, 2; task 087). Per defend post (a hunt squad leaves its post - no charges), from the squad's supply
// (sappers_per_squad x per_sapper: designer Q1, the PMC sapper carries 2 mines + 1 tripwire):
//   1. doorway mine - the door of the post's building the attack enters (the building door nearest the attack anchor; a roof post with an
//      inside access / a window post), door_inside_cm inside the door, tagged with the door id (the props validator lets a door's own
//      charge stand in its corridor);
//   2. approach mines - on the straight line from the attack target (the post on the ground / its door / its outside ladder's foot) to the
//      attack anchor (the mandatory-route point nearest the post), first_mine_m out, then every mine_spacing_m, until the supply is spent
//      or the line reaches route_clear_m of the mandatory route;
//   3. the tripwire - roof post with an inside access (stair top / hatch): access_offset_m from the access onto the roof towards the post;
//      outside ladder: none (the access guard covers it, note 1b); window post: across the building's other door; ground post: across the
//      flank, flank_m to the side of the post (the side with cover near it - the covered way in - first).
// Every charge: never within retreat_clear_m of the post's retreat line (retreat_m straight away from the anchor), never within
// route_clear_m of the mandatory route on its layer, start_clear_m of the start or mine_spacing_m of another PMC charge, and it passes the
// props static validator (validator.ts = the game's FPropsValidator: walkable, not blocked, no foreign doorway corridor, no stair / ladder
// disk) - so the game drops none at load. A blocked spot slides sideways (0, +-0.5, +-1 m) before it is given up.
// Output: PmcPlan.charges + the props layer `charges.layer_id` (kind mines, LOCKED so «перебросить» / rerollAll never touch it; objects
// owner "pmc", faction "hostile", post = the post id, role). Pure and deterministic.

import type { Vec2 } from '../movement/geometry.ts';
import { layerZ } from './missionGoals.ts';
import { PropsLayoutView, type PropsDoorway } from './props/layoutView.ts';
import { PROPS_SCHEMA, type LevelPropsFile, type PropLayer, type PropObject } from './props/schema.ts';
import { validateStatic } from './props/validator.ts';
import type { PmcCharge, PmcChargeRole, PmcPlan, PmcPost, PmcPostsConfig, RouteSeg } from './pmcPosts.ts';
import { mandatorySegments, PMC_POSTS, planPmcPosts, routeDistOnLayer, segDist, startOf } from './pmcPosts.ts';
import type { GenLayout } from './schema.ts';
import type { TacticalMapFile } from './tacticalMap.ts';
import { roofAccess } from './tacticalMap.ts';

const dist2 = (a: readonly number[], b: readonly number[]): number => Math.hypot(a[0] - b[0], a[1] - b[1]);
const unit = (a: readonly number[], b: readonly number[]): Vec2 | null => {
  const dx = b[0] - a[0], dy = b[1] - a[1], L = Math.hypot(dx, dy);
  return L < 1e-6 ? null : [dx / L, dy / L];
};
const yawOf = (d: Vec2): number => (Math.atan2(d[1], d[0]) * 180) / Math.PI;
const dirOfYaw = (deg: number): Vec2 => [Math.cos((deg * Math.PI) / 180), Math.sin((deg * Math.PI) / 180)];
const rnd = (p: readonly number[]): Vec2 => [Math.round(p[0]), Math.round(p[1])];
/** A tripwire / doorway charge's yaw: the crossing direction folded into [0, 180) (placement.ts doorMineYaw). */
const wireYaw = (crossing: Vec2): number => ((Math.round(yawOf(crossing)) % 180) + 180) % 180;

function doorMinePos(w: PropsDoorway, insideCm: number): Vec2 {
  return [Math.round(w.pos[0] - w.outward[0] * insideCm), Math.round(w.pos[1] - w.outward[1] * insideCm)];
}
function doorMineYaw(w: PropsDoorway): number {
  if (w.along[0] !== 0 && w.along[1] !== 0) return wireYaw([w.outward[0], w.outward[1]]);
  return w.along[0] !== 0 ? 90 : 0;
}

export interface PostChargeReport { post: string; mines: number; tripwires: number; unused_mines: number; unused_tripwires: number; target: string; notes: string[] }
export interface PlanChargesOptions { config?: PmcPostsConfig; balance?: Record<string, unknown>; view?: PropsLayoutView }

/** The retreat line of a post: from the post straight away from its attack anchor, retreat_m long (xy). */
export function retreatLine(p: PmcPost, retreatCm: number): [Vec2, Vec2] {
  const away = unit(p.anchor, p.pos) ?? dirOfYaw(p.yaw + 180);
  return [[p.pos[0], p.pos[1]], [p.pos[0] + away[0] * retreatCm, p.pos[1] + away[1] * retreatCm]];
}

/** The mandatory-route point `backCm` before (towards the start) the route point nearest `p` (the route's first point at most). */
export function upstreamPoint(segs: readonly RouteSeg[], p: readonly number[], backCm: number): Vec2 {
  if (!segs.length) return [p[0], p[1]];
  let bi = 0, bt = 0, bd = Infinity;
  segs.forEach((s, i) => { const h = segDist(p, s.a, s.b); if (h.d < bd) { bd = h.d; bi = i; bt = h.t; } });
  let left = backCm;
  let at: Vec2 = [segs[bi].a[0] + (segs[bi].b[0] - segs[bi].a[0]) * bt, segs[bi].a[1] + (segs[bi].b[1] - segs[bi].a[1]) * bt];
  for (let i = bi; i >= 0 && left > 0; i--) {
    const a = segs[i].a, L = dist2(at, a);
    if (L >= left) { const d = unit(at, a)!; return [at[0] + d[0] * left, at[1] + d[1] * left]; }
    left -= L;
    at = [a[0], a[1]];
  }
  return at;
}

/** The charges of every defend post of `plan` (returns a new plan; posts unchanged). */
export function planPmcCharges(l: GenLayout, plan: PmcPlan, o: PlanChargesOptions = {}): PmcPlan {
  const c = (o.config ?? PMC_POSTS).charges;
  const view = o.view ?? PropsLayoutView.build(l as unknown as Record<string, unknown>, o.balance ? PropsLayoutView.readConfig(o.balance) : undefined);
  const segs: RouteSeg[] = mandatorySegments(l);
  const start = startOf(l);
  const all: PmcCharge[] = [];
  const reports: PostChargeReport[] = [];

  for (const post of plan.posts) {
    if (post.posture !== 'defend') continue;
    let mines = Math.max(0, c.sappers_per_squad * c.per_sapper.mines);
    let wires = Math.max(0, c.sappers_per_squad * c.per_sapper.tripwires);
    const rep: PostChargeReport = { post: post.id, mines: 0, tripwires: 0, unused_mines: 0, unused_tripwires: 0, target: 'post', notes: [] };
    const retreat = retreatLine(post, c.retreat_m * 100);
    const anchor: Vec2 = [post.anchor[0], post.anchor[1]];
    let n = 0;

    /** A charge at p on `layer` passes every rule (the static validator with the object as the game sees it). */
    const fits = (obj: PropObject): string | null => {
      const p = obj.pos;
      if (segDist(p, retreat[0], retreat[1]).d < c.retreat_clear_m * 100) return 'retreat';
      if (routeDistOnLayer(p, segs, obj.walk_layer) < c.route_clear_m * 100) return 'route';
      if (dist2(p, start) < c.start_clear_m * 100) return 'start';
      if (all.some((x) => dist2(x.pos, p) < c.mine_spacing_m * 100)) return 'spacing';
      const v = validateStatic(view, [{ layerId: c.layer_id, object: obj, bHand: false, bPinned: true }], [], o.balance);
      return v.dropped.length ? 'validator' : null;
    };
    const add = (role: PmcChargeRole, type: 'mine' | 'tripwire', p: Vec2, layer: string, yaw: number, extra: Partial<PmcCharge> = {}, slide?: Vec2): boolean => {
      const kind = type === 'mine' ? c.mine : c.tripwire;
      const offsets = slide ? [0, 50, -50, 100, -100] : [0];
      let last = '';
      for (const off of offsets) {
        const q = rnd(slide ? [p[0] + slide[0] * off, p[1] + slide[1] * off] : p);
        const obj: PropObject = { id: 'probe', variant: kind.variant, pos: q, walk_layer: layer, yaw: Math.round(yaw), trigger: kind.trigger,
          ...(extra.door ? { door: extra.door } : {}), ...(extra.width_cm ? { width_cm: extra.width_cm } : {}) };
        const why = fits(obj);
        if (why) { last = why; continue; }
        all.push({ id: `${c.layer_id}_${post.id}_${n++}`, post: post.id, role, type, variant: kind.variant, trigger: kind.trigger, pos: q, z: layerZ(l, layer),
          walk_layer: layer, yaw: Math.round(yaw), ...extra });
        if (type === 'mine') rep.mines++; else rep.tripwires++;
        return true;
      }
      rep.notes.push(`${role} ${type} at [${rnd(p).join(', ')}] refused: ${last}`);
      return false;
    };

    // the building's doors (nearest the anchor first) and the attack target
    const doors = post.building
      ? view.doorways.filter((w) => w.kind === 'building_door' && w.building === post.building).sort((a, b) => dist2(a.pos, anchor) - dist2(b.pos, anchor) || (a.id < b.id ? -1 : 1))
      : [];
    const b = post.building ? l.buildings.find((x) => x.id === post.building) : undefined;
    const roof = post.kind === 'overwatch_roof' && b ? b : undefined;
    // inside roof access = a stairwell up to the roof / a hatch; outside = a ladder / an external stair up the wall (note 1b)
    const insideTops: { id: string; p: Vec2 }[] = roof
      ? [
          ...(roof.stairs ?? []).filter((s) => s.to >= roof.floors && s.kind !== 'external' && Array.isArray(s.top)).map((s) => ({ id: `stair:${s.id}`, p: [s.top[0], s.top[1]] as Vec2 })),
          ...(roof.hatches ?? []).map((h) => ({ id: `hatch:${h.id}`, p: [h.pos[0], h.pos[1]] as Vec2 })),
        ].sort((x, y) => dist2(x.p, post.pos) - dist2(y.p, post.pos) || (x.id < y.id ? -1 : 1))
      : [];
    const outsideFeet: { id: string; p: Vec2 }[] = roof
      ? [
          ...l.ladders.filter((k) => k.to === post.layer).map((k) => ({ id: `ladder:${k.id}`, p: [k.foot[0], k.foot[1]] as Vec2 })),
          ...(roof.stairs ?? []).filter((s) => s.to >= roof.floors && s.kind === 'external').map((s) => ({ id: `stair:${s.id}`, p: [s.pos[0], s.pos[1]] as Vec2 })),
        ].sort((x, y) => dist2(x.p, anchor) - dist2(y.p, anchor) || (x.id < y.id ? -1 : 1))
      : [];
    const outsideOnly = !!roof && insideTops.length === 0 && roofAccess(l, roof).length > 0;
    let target: Vec2 = [post.pos[0], post.pos[1]];
    let entry: PropsDoorway | undefined;
    if (outsideOnly) {
      if (outsideFeet.length) { target = outsideFeet[0].p; rep.target = outsideFeet[0].id; }
    } else if (post.building) {
      entry = doors.find((w) => !w.bOnMandatoryRoute);
      if (entry) { target = [entry.pos[0], entry.pos[1]]; rep.target = `door:${entry.id}`; }
      else rep.notes.push('no door off the mandatory route');
    }

    // 1. doorway mine
    if (entry && mines > 0 && add('doorway', 'mine', doorMinePos(entry, c.door_inside_cm), entry.walkLayer, doorMineYaw(entry), { door: entry.id })) mines--;

    // 2. approach mines: target -> anchor, then target -> the route point second_line_back_m upstream (the way the attackers come)
    for (const from of [anchor, upstreamPoint(segs, anchor, c.second_line_back_m * 100)]) {
      const toFrom = unit(target, from);
      if (!toFrom || mines <= 0) continue;
      const len = dist2(target, from), side: Vec2 = [-toFrom[1], toFrom[0]];
      for (let s = c.first_mine_m * 100; mines > 0 && s <= len; s += c.mine_spacing_m * 100) {
        const p: Vec2 = [target[0] + toFrom[0] * s, target[1] + toFrom[1] * s];
        if (routeDistOnLayer(p, segs, 'ground') < c.route_clear_m * 100) break;
        if (add('approach', 'mine', p, 'ground', yawOf(toFrom), {}, side)) mines--;
      }
    }

    // 3. the tripwire: the inside roof access / the window building's other door, else (and on a ground post) across the flank
    /** A tripwire flank_m to either side of `center` across the way in (the side with cover near it - the covered way - first). */
    const flankAt = (center: Vec2, layer: string): boolean => {
      const away = unit(anchor, center) ?? dirOfYaw(post.yaw + 180);
      const sides: Vec2[] = [[-away[1], away[0]], [away[1], -away[0]]];
      const pts = sides.map((s) => ({ s, p: rnd([center[0] + s[0] * c.flank_m * 100, center[1] + s[1] * c.flank_m * 100]) }));
      const covered = (p: Vec2) => (view.isWalkable(p, layer) && view.isNearCover(p, layer, 300) ? 1 : 0);
      pts.sort((x, y) => covered(y.p) - covered(x.p));
      for (const { s, p } of pts) if (add('flank', 'tripwire', p, layer, wireYaw(s), { width_cm: c.tripwire_width_cm }, away)) return true;
      return false;
    };
    if (wires > 0) {
      let done = false;
      if (roof) {
        if (outsideOnly) rep.notes.push('outside access (ladder / external stair): no access trap, the guard covers it');
        for (const t of insideTops) {
          const toPost = unit(t.p, post.pos) ?? dirOfYaw(post.yaw);
          for (const turn of [0, 45, -45, 90, -90, 135, -135, 180]) {
            const d = dirOfYaw(yawOf(toPost) + turn);
            const p: Vec2 = [t.p[0] + d[0] * c.access_offset_m * 100, t.p[1] + d[1] * c.access_offset_m * 100];
            if (add('roof_access', 'tripwire', p, post.layer, wireYaw(d), { width_cm: c.tripwire_width_cm })) { done = true; break; }
          }
          if (done) break;
        }
      } else if (post.kind === 'window') {
        const other = doors.find((w) => w !== entry && !w.bOnMandatoryRoute);
        if (other) done = add('flank', 'tripwire', doorMinePos(other, c.door_inside_cm), other.walkLayer, doorMineYaw(other),
          { door: other.id, width_cm: other.widthCm > 0 ? other.widthCm : c.tripwire_width_cm });
      }
      if (!done) done = post.building ? flankAt(target, 'ground') : flankAt([post.pos[0], post.pos[1]], post.layer);
      if (done) wires--;
    }
    rep.unused_mines = mines;
    rep.unused_tripwires = wires;
    reports.push(rep);
  }
  return { ...plan, charges: all, report: { ...plan.report, charges: { posts: reports, total: all.length } } };
}

/** Posts + charges in one go (the generator stage: the layout's `pmc` block). */
export function planPmc(l: GenLayout, t: TacticalMapFile, seed: number, o: PlanChargesOptions & { count?: number } = {}): PmcPlan {
  return planPmcCharges(l, planPmcPosts(l, t, seed, { config: o.config, count: o.count }), o);
}

/** The props layer of the plan's charges (kind mines, locked: rerolls never touch it; objects tagged owner / faction / post / role). */
export function pmcChargesLayer(plan: PmcPlan, layerId = PMC_POSTS.charges.layer_id): PropLayer {
  return {
    id: layerId, kind: 'mines', enabled: true, seed: plan.seed >>> 0, locked: true, preset: 'pmc_charges',
    rules: { pmc: { source: 'pmcCharges.ts (PMC_DEFENCE.md D2)', posts: plan.posts.filter((p) => p.posture === 'defend').map((p) => p.id) } },
    objects: plan.charges.map((ch): PropObject => ({
      id: ch.id, origin: 'generated', variant: ch.variant, ...(ch.door ? { door: ch.door } : {}), pos: [ch.pos[0], ch.pos[1]], walk_layer: ch.walk_layer, yaw: ch.yaw,
      trigger: ch.trigger, locked: true, ...(ch.width_cm ? { width_cm: ch.width_cm } : {}), owner: 'pmc', faction: 'hostile', post: ch.post, role: ch.role,
    })),
  };
}

/** The props file with the plan's PMC layer in place of the old one (added at the end when the file had none). */
export function withPmcCharges(file: LevelPropsFile, plan: PmcPlan, layerId = PMC_POSTS.charges.layer_id): LevelPropsFile {
  const layer = pmcChargesLayer(plan, layerId);
  const i = file.layers.findIndex((x) => x.id === layerId);
  const layers = i >= 0 ? file.layers.map((x, j) => (j === i ? layer : x)) : [...file.layers, layer];
  return { ...file, schema: file.schema ?? PROPS_SCHEMA, layers };
}
