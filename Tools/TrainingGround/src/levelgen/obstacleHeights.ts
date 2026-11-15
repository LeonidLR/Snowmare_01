// Provisional animation targets: edit the JSON after measuring the CaV clips.
import config from '../../../../Content/Data/LevelGen/obstacle_heights.json' with { type: 'json' };

export const OBSTACLE_HEIGHTS = config.classes;
export type ObstacleHeightClass = keyof typeof OBSTACLE_HEIGHTS;
const traversable = ['step', 'low_vault', 'low_mantle', 'high_mantle'] as const;

export function obstacleHeight(kind: ObstacleHeightClass): number {
  return OBSTACLE_HEIGHTS[kind].nominal_cm;
}

/** Tall architecture remains untraversable at any height above the mantle ceiling. */
export function classifyObstacleHeight(height: number): ObstacleHeightClass | undefined {
  if (!Number.isFinite(height) || height <= 0) return undefined;
  if (height > OBSTACLE_HEIGHTS.wall_no_traverse.min_cm) return 'wall_no_traverse';
  return traversable.find((key) => {
    const c = OBSTACLE_HEIGHTS[key];
    return height >= c.min_cm && height <= c.max_cm && Math.abs(height - c.nominal_cm) <= c.tolerance_cm;
  });
}

/** Used only when emitting new geometry; validators never repair hand-edited data. */
export function standardObstacleHeight(height: number): number {
  if (!Number.isFinite(height) || height <= 0) throw new Error(`Invalid obstacle height: ${height}`);
  if (height > OBSTACLE_HEIGHTS.wall_no_traverse.min_cm) return height;
  const nearest = traversable.reduce((a, b) =>
    Math.abs(height - obstacleHeight(a)) <= Math.abs(height - obstacleHeight(b)) ? a : b);
  return obstacleHeight(nearest);
}
