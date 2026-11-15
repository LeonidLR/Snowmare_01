// Measured source-root proxies; geometry/action eligibility remains separate from height matching.
import config from '../../../../Content/Data/LevelGen/obstacle_heights.json' with { type: 'json' };

export const OBSTACLE_HEIGHTS = config.classes;
export type ObstacleHeightClass = keyof typeof OBSTACLE_HEIGHTS;
type TraversableClass = Exclude<ObstacleHeightClass, 'wall_no_traverse'>;
const traversable = (Object.keys(OBSTACLE_HEIGHTS) as ObstacleHeightClass[])
  .filter((key): key is TraversableClass => key !== 'wall_no_traverse');
const traversalCeiling = Math.max(...traversable.map(key => OBSTACLE_HEIGHTS[key].nominal_cm + OBSTACLE_HEIGHTS[key].tolerance_cm));
const wallThreshold = Math.max(OBSTACLE_HEIGHTS.wall_no_traverse.min_cm, traversalCeiling);

/** Height bands overlap: depth/landing intent chooses the action. An explicit class disambiguates it. */
export function matchesObstacleHeight(height: number, kind: ObstacleHeightClass): boolean {
  if (!Number.isFinite(height) || height <= 0) return false;
  if (kind === 'wall_no_traverse') return height > wallThreshold;
  const c = OBSTACLE_HEIGHTS[kind];
  return height >= c.min_cm && height <= c.max_cm && Math.abs(height - c.nominal_cm) <= c.tolerance_cm;
}

export function obstacleHeight(kind: ObstacleHeightClass): number {
  return OBSTACLE_HEIGHTS[kind].nominal_cm;
}

export function classifyObstacleHeight(height: number, preferredClass?: ObstacleHeightClass): ObstacleHeightClass | undefined {
  if (preferredClass) return matchesObstacleHeight(height, preferredClass) ? preferredClass : undefined;
  const candidates = traversable.filter(key => matchesObstacleHeight(height, key));
  if (candidates.length) return candidates.reduce((a, b) =>
    Math.abs(height - obstacleHeight(a)) <= Math.abs(height - obstacleHeight(b)) ? a : b);
  return matchesObstacleHeight(height, 'wall_no_traverse') ? 'wall_no_traverse' : undefined;
}

/** Preserve already-valid heights (including 300 cm storeys); normalize only new invalid draft geometry. */
export function standardObstacleHeight(height: number): number {
  if (!Number.isFinite(height) || height <= 0) throw new Error(`Invalid obstacle height: ${height}`);
  if (classifyObstacleHeight(height)) return height;
  const nearest = traversable.reduce((a, b) =>
    Math.abs(height - obstacleHeight(a)) <= Math.abs(height - obstacleHeight(b)) ? a : b);
  return obstacleHeight(nearest);
}
