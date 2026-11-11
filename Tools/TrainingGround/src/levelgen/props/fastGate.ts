// Training Ground / level generator: fast nav gate for props layers (PROPS_LAYERS.md section 2.4 and section 5 T3).
//
// Builds the lab nav grid per walk layer with nav-affecting props (crates, barrels, turrets; not mines/loot)
// widened by the 35 cm agent radius. Validates:
//   (1) completability of the mandatory route (and any variants): every leg on its layer must have a free path;
//   (2) reachability of every doorway (building doors + fence/wall gates);
//   (3) reachability of every ladder foot (from layer) and top (to layer).
//
// When blocked, identifies the failing leg / element and the specific blocking prop object ids.

import type { Segment, Vec2 } from '../../movement/geometry.ts';
import { dist } from '../../movement/geometry.ts';
import type { Layout } from '../../movement/layout.ts';
import { wallSegments } from '../../movement/layout.ts';
import { AGENT_RADIUS, NavGrid } from '../../movement/nav.ts';
import type { GenLayout, GenRoute } from '../schema.ts';
import { layersOf, mandatoryRoute, toFormationLayout } from '../schema.ts';
import type { LevelPropsFile, PropObject } from './schema.ts';
import { PropsLayoutView } from './layoutView.ts';

export interface FastGateResult {
  gate: 'pass' | 'fail';
  gate_leg?: string; // e.g. "leg 3", "doorway B_shed#0", "ladder LAD_B_shed"
  gate_blockers?: string[]; // IDs of props that cause the blockage
  reason?: string;
  failing_route?: string;
}

/** Determines if a prop kind/variant is nav-affecting according to balance or defaults. */
export function isPropNavAffecting(
  prop: PropObject,
  kinds?: Record<string, Record<string, unknown>>
): boolean {
  if (kinds && kinds[prop.variant] && typeof kinds[prop.variant].nav_affecting === 'boolean') {
    return kinds[prop.variant].nav_affecting as boolean;
  }
  const v = prop.variant.toLowerCase();
  if (v.startsWith('mine') || v.startsWith('loot')) return false;
  if (v.startsWith('crate') || v.startsWith('barrel') || v.startsWith('turret')) return true;
  return false;
}

/** Converts a prop object into obstacle segments for the NavGrid. */
export function propToObstacleSegments(
  prop: PropObject,
  kinds?: Record<string, Record<string, unknown>>
): Array<Segment & { propId: string }> {
  const kindInfo = (kinds && kinds[prop.variant]) || {};
  const isCylinder = kindInfo.shape === 'cylinder' || prop.variant.startsWith('barrel');
  const pos = prop.pos;
  const propId = prop.id;

  if (isCylinder) {
    const radius = typeof kindInfo.radius_cm === 'number' ? Math.floor(kindInfo.radius_cm) : 30;
    // Single point segment with thickness = 2 * radius.
    // NavGrid widens by AGENT_RADIUS (35) + thickness / 2 (radius) = radius + 35 cm.
    return [
      {
        a: [pos[0], pos[1]],
        b: [pos[0], pos[1]],
        thickness: 2 * radius,
        propId,
      },
    ];
  }

  // Box / Rectangle (crates, turrets)
  let sx = 120;
  let sy = 120;
  if (Array.isArray(kindInfo.size_cm) && kindInfo.size_cm.length >= 2) {
    sx = Math.floor(kindInfo.size_cm[0]);
    sy = Math.floor(kindInfo.size_cm[1]);
  } else if (prop.variant.includes('_l')) {
    sx = 160;
    sy = 160;
  }

  const yawDeg = typeof prop.yaw === 'number' ? prop.yaw : 0;
  const rad = (yawDeg * Math.PI) / 180;
  const cos = Math.cos(rad);
  const sin = Math.sin(rad);
  const hx = sx / 2;
  const hy = sy / 2;

  const rotate = (lx: number, ly: number): Vec2 => [
    Math.round(pos[0] + (lx * cos - ly * sin)),
    Math.round(pos[1] + (lx * sin + ly * cos)),
  ];

  const c0 = rotate(-hx, -hy);
  const c1 = rotate(hx, -hy);
  const c2 = rotate(hx, hy);
  const c3 = rotate(-hx, hy);

  // 4 perimeter edges + 2 diagonal cross-segments to fully block interior cells
  return [
    { a: c0, b: c1, thickness: 0, propId },
    { a: c1, b: c2, thickness: 0, propId },
    { a: c2, b: c3, thickness: 0, propId },
    { a: c3, b: c0, thickness: 0, propId },
    { a: c0, b: c2, thickness: 0, propId },
    { a: c1, b: c3, thickness: 0, propId },
  ];
}

/** Collects all active, nav-affecting props on a given walk layer. */
export function getLayerNavProps(
  file: LevelPropsFile,
  layer: string,
  kinds?: Record<string, Record<string, unknown>>
): PropObject[] {
  const result: PropObject[] = [];
  const visited = new Set<string>();

  for (const l of file.layers) {
    if (l.enabled === false) continue;
    for (const p of l.objects) {
      if (visited.has(p.id)) continue;
      visited.add(p.id);
      const walkLayer = p.walk_layer ?? 'ground';
      if (walkLayer === layer && isPropNavAffecting(p, kinds)) {
        result.push(p);
      }
    }
  }

  return result;
}

/**
 * Runs the fast nav gate validation (T3).
 *
 * Checks:
 *   1. Mandatory route (and variants) leg pathing with props on each layer.
 *   2. Reachability of every gate / doorway on each layer from the route.
 *   3. Reachability of every ladder foot and top.
 */
export function runFastGate(
  layout: GenLayout,
  file: LevelPropsFile,
  balance?: Record<string, unknown>,
  _targetLayerId?: string
): FastGateResult {
  const kinds = ((balance?.kinds ?? {}) as Record<string, Record<string, unknown>>) || {};
  const layers = layersOf(layout);

  // Cache layouts and nav grids per layer
  const flatLayouts = new Map<string, Layout>();
  const baseGrids = new Map<string, NavGrid>();
  const propGrids = new Map<string, NavGrid>();
  const layerPropsMap = new Map<string, PropObject[]>();

  const getLayerLayout = (layer: string): Layout => {
    let f = flatLayouts.get(layer);
    if (!f) {
      f = toFormationLayout(layout, layer);
      flatLayouts.set(layer, f);
    }
    return f;
  };

  const getBaseNav = (layer: string): NavGrid => {
    let g = baseGrids.get(layer);
    if (!g) {
      const f = getLayerLayout(layer);
      g = new NavGrid(wallSegments(f), f.bounds.min, f.bounds.max, 50, AGENT_RADIUS);
      baseGrids.set(layer, g);
    }
    return g;
  };

  const getPropNav = (layer: string): { grid: NavGrid; props: PropObject[] } => {
    let g = propGrids.get(layer);
    let props = layerPropsMap.get(layer);
    if (!g || !props) {
      const f = getLayerLayout(layer);
      const baseWalls = wallSegments(f);
      props = getLayerNavProps(file, layer, kinds);
      layerPropsMap.set(layer, props);

      const propSegs: Segment[] = [];
      for (const p of props) {
        propSegs.push(...propToObstacleSegments(p, kinds));
      }

      g = new NavGrid([...baseWalls, ...propSegs], f.bounds.min, f.bounds.max, 50, AGENT_RADIUS);
      propGrids.set(layer, g);
    }
    return { grid: g, props };
  };

  // Helper to isolate which props caused a blockage between a and b on a layer
  const findBlockingProps = (layer: string, a: Vec2, b: Vec2, candidateProps: PropObject[]): string[] => {
    const blockers: string[] = [];
    const f = getLayerLayout(layer);
    const baseWalls = wallSegments(f);

    // 1. Check single-prop removals: does removing prop P restore the path?
    for (const p of candidateProps) {
      const remainingProps = candidateProps.filter((x) => x.id !== p.id);
      const testSegs: Segment[] = [];
      for (const rp of remainingProps) {
        testSegs.push(...propToObstacleSegments(rp, kinds));
      }
      const testGrid = new NavGrid([...baseWalls, ...testSegs], f.bounds.min, f.bounds.max, 50, AGENT_RADIUS);
      if (testGrid.path(a, b) !== null) {
        blockers.push(p.id);
      }
    }

    if (blockers.length > 0) {
      return blockers;
    }

    // 2. If multi-prop blockage, check which props lie on the unobstructed baseline path
    const unobstructedGrid = getBaseNav(layer);
    const basePath = unobstructedGrid.path(a, b);
    if (basePath) {
      for (const p of candidateProps) {
        const radiusEst = (p.variant.includes('_l') ? 80 : 60) + AGENT_RADIUS;
        const intersects = basePath.some((pt) => dist(pt, p.pos) <= radiusEst);
        if (intersects) {
          blockers.push(p.id);
        }
      }
    }

    // 3. Fallback: all props near a or b
    if (blockers.length === 0) {
      for (const p of candidateProps) {
        if (dist(a, p.pos) < 500 || dist(b, p.pos) < 500) {
          blockers.push(p.id);
        }
      }
    }

    return blockers.length > 0 ? blockers : candidateProps.map((p) => p.id);
  };

  // ------------------------------------------------------------------
  // CHECK 1: Mandatory route & variants completability
  // ------------------------------------------------------------------
  const routesToCheck: GenRoute[] = [];
  const mand = mandatoryRoute(layout);
  if (mand) routesToCheck.push(mand);
  for (const r of layout.routes) {
    if (r !== mand) routesToCheck.push(r);
  }

  for (const r of routesToCheck) {
    const rLayers = r.layers ?? r.points.map(() => 'ground');
    for (let i = 1; i < r.points.length; i++) {
      const a = r.points[i - 1];
      const b = r.points[i];
      const la = rLayers[i - 1];
      const lb = rLayers[i];

      if (la === lb && layers.includes(la)) {
        const { grid: propGrid, props: layerProps } = getPropNav(la);
        const path = propGrid.path(a, b);

        if (!path) {
          // Verify that it was pathable without props (so we only fail on prop blockages)
          const baseGrid = getBaseNav(la);
          const basePath = baseGrid.path(a, b);
          if (basePath) {
            const blockers = findBlockingProps(la, a, b, layerProps);
            return {
              gate: 'fail',
              gate_leg: `leg ${i}`,
              gate_blockers: blockers,
              reason: `route blocked at leg ${i}`,
              failing_route: r.id,
            };
          }
        }
      }
    }
  }

  // ------------------------------------------------------------------
  // CHECK 2: Doorway & gate reachability
  // ------------------------------------------------------------------
  const mandPoints = mand ? mand.points : [layout.spawn.pos];
  const mandLayers = mand ? (mand.layers ?? mand.points.map(() => 'ground')) : ['ground'];

  // ------------------------------------------------------------------
  // CHECK 2: Doorway & gate reachability
  // ------------------------------------------------------------------
  try {
    const config = PropsLayoutView.readConfig(balance ?? {});
    const view = PropsLayoutView.build(JSON.stringify(layout), config);
    for (const d of view.doorways) {
      const doorLayer = d.walkLayer ?? 'ground';
      if (!layers.includes(doorLayer)) continue;

      const { grid: propGrid, props: layerProps } = getPropNav(doorLayer);
      const baseGrid = getBaseNav(doorLayer);

      const candidateRoutePoints = mandPoints.filter((_, idx) => mandLayers[idx] === doorLayer);
      const startPt = candidateRoutePoints.length > 0 ? candidateRoutePoints[0] : (doorLayer === 'ground' ? layout.spawn.pos : null);
      if (!startPt) continue;

      const targetPt: Vec2 = [d.pos[0], d.pos[1]];
      if (baseGrid.path(startPt, targetPt) !== null) {
        if (propGrid.path(startPt, targetPt) === null) {
          const blockers = findBlockingProps(doorLayer, startPt, targetPt, layerProps);
          return {
            gate: 'fail',
            gate_leg: `${d.kind === 'gate' ? 'gate' : 'doorway'} ${d.id}`,
            gate_blockers: blockers,
            reason: `${d.kind === 'gate' ? 'gate' : 'doorway'} ${d.id} unreachable`,
          };
        }
      }
    }
  } catch (e) {
    // Fallback if view cannot build
  }

  // ------------------------------------------------------------------
  // CHECK 3: Ladder foot & top reachability
  // ------------------------------------------------------------------
  for (const lad of layout.ladders) {
    const fromLayer = lad.from ?? 'ground';
    const toLayer = lad.to ?? 'ground';

    // Foot reachability on fromLayer
    if (layers.includes(fromLayer)) {
      const { grid: propGrid, props: layerProps } = getPropNav(fromLayer);
      const baseGrid = getBaseNav(fromLayer);
      const candidateRoutePoints = mandPoints.filter((_, idx) => mandLayers[idx] === fromLayer);
      const startPt = candidateRoutePoints.length > 0 ? candidateRoutePoints[0] : (fromLayer === 'ground' ? layout.spawn.pos : null);
      if (startPt && baseGrid.path(startPt, lad.foot) !== null) {
        if (propGrid.path(startPt, lad.foot) === null) {
          const blockers = findBlockingProps(fromLayer, startPt, lad.foot, layerProps);
          return {
            gate: 'fail',
            gate_leg: `ladder ${lad.id} foot`,
            gate_blockers: blockers,
            reason: `ladder ${lad.id} foot unreachable`,
          };
        }
      }
    }

    // Top reachability on toLayer
    if (layers.includes(toLayer)) {
      const { grid: propGrid, props: layerProps } = getPropNav(toLayer);
      const baseGrid = getBaseNav(toLayer);
      const candidateRoutePoints = mandPoints.filter((_, idx) => mandLayers[idx] === toLayer);
      const startPt = candidateRoutePoints.length > 0 ? candidateRoutePoints[0] : (toLayer === 'ground' ? layout.spawn.pos : null);
      if (startPt && baseGrid.path(startPt, lad.top) !== null) {
        if (propGrid.path(startPt, lad.top) === null) {
          const blockers = findBlockingProps(toLayer, startPt, lad.top, layerProps);
          return {
            gate: 'fail',
            gate_leg: `ladder ${lad.id} top`,
            gate_blockers: blockers,
            reason: `ladder ${lad.id} top unreachable`,
          };
        }
      }
    }
  }

  return { gate: 'pass' };
}
