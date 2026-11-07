// Training Ground / level generator: Chokepoint and Kill Zone Analyzer (docs/design/KILL_ZONE_AMBUSH.md, #49).
// Finds corridors 1-3 m wide between blocks >= 3 m tall, identifies and scores ambush positions on both sides
// (height, cover from corridor entrance, line of sight along the corridor), calculates kill zone ratings,
// and produces a heat-map data model for visualization in the Level Generator department.

import type { Vec2 } from '../movement/geometry.ts';
import { dist } from '../movement/geometry.ts';
import type { GenLayout } from './schema.ts';

export interface Block3D {
  id: string;
  min: Vec2; // [x, y] in UE cm
  max: Vec2; // [x, y] in UE cm
  height: number;
  roofWalkable: boolean;
  source: 'building' | 'wall';
}

export interface AmbushPosition {
  id: string;
  side: 'left' | 'right';
  pos: Vec2;
  heightCm: number;
  level: number;
  isRoof: boolean;
  buildingId?: string;
  heightScore: number; // 0..100
  coverScore: number;  // 0..100
  losScore: number;    // 0..100
  totalScore: number;  // 0..100
}

export type ChokepointCategory = 'fatal_funnel' | 'crossfire_alley' | 'elevated_ambush' | 'standard';

export interface ChokepointCorridor {
  id: string;
  axis: 'X' | 'Y'; // 'X' runs north-south, 'Y' runs east-west
  start: Vec2;
  end: Vec2;
  center: Vec2;
  widthCm: number;
  widthM: number;
  lengthCm: number;
  lengthM: number;
  leftBlockId: string;
  rightBlockId: string;
  leftHeightCm: number;
  rightHeightCm: number;
  leftBestScore: number;
  rightBestScore: number;
  killZoneRating: number; // 0..100
  category: ChokepointCategory;
  ambushPositions: AmbushPosition[];
}

export interface HeatmapCell {
  x: number;
  y: number;
  intensity: number; // 0..1
  chokepointId?: string;
}

export interface ChokepointSummary {
  totalCorridors: number;
  narrowCount: number; // width <= 200 cm (2m)
  crossfireCount: number; // killZoneRating >= 75
  highestKillZoneRating: number;
  meanWidthM: number;
  meanLengthM: number;
}

export interface ChokepointAnalysisResult {
  layoutName: string;
  corridors: ChokepointCorridor[];
  summary: ChokepointSummary;
  heatmap: {
    cellSizeCm: number;
    cells: HeatmapCell[];
  };
}

/** Extracts solid blocks >= 3 m tall (buildings and standalone tall walls). */
export function extractTallBlocks(layout: GenLayout): Block3D[] {
  const blocks: Block3D[] = [];

  // 1. Buildings >= 3 m
  for (const b of layout.buildings || []) {
    if ((b.height || 0) >= 300) {
      blocks.push({
        id: b.id,
        min: [b.footprint[0][0], b.footprint[0][1]],
        max: [b.footprint[1][0], b.footprint[1][1]],
        height: b.height,
        roofWalkable: !!b.roof_walkable,
        source: 'building',
      });
    }
  }

  // 2. Standalone tall walls (>= 3 m and not interior, not belonging to a building)
  for (const w of layout.walls || []) {
    const h = w.height ?? 220;
    if (h >= 300 && w.kind !== 'interior' && !w.building) {
      const halfThick = (w.thickness ?? 20) / 2;
      const minX = Math.min(w.a[0], w.b[0]) - halfThick;
      const maxX = Math.max(w.a[0], w.b[0]) + halfThick;
      const minY = Math.min(w.a[1], w.b[1]) - halfThick;
      const maxY = Math.max(w.a[1], w.b[1]) + halfThick;
      blocks.push({
        id: w.id || `wall_${Math.round(minX)}_${Math.round(minY)}`,
        min: [minX, minY],
        max: [maxX, maxY],
        height: h,
        roofWalkable: false,
        source: 'wall',
      });
    }
  }

  return blocks;
}

/** Scores an ambush perch based on height, entrance concealment, and corridor line of sight. */
export function scoreAmbushPosition(
  pos: Vec2,
  heightCm: number,
  corridor: { start: Vec2; end: Vec2; axis: 'X' | 'Y' },
  isRoof: boolean
): { heightScore: number; coverScore: number; losScore: number; totalScore: number } {
  // 1. Height Score (0..100): elevation grants tactical superiority
  const hM = heightCm / 100;
  let heightScore = 25;
  if (hM >= 9) heightScore = 100;
  else if (hM >= 6) heightScore = 85;
  else if (hM >= 3) heightScore = 70;
  else if (hM >= 2) heightScore = 40;

  // 2. Cover from Corridor Entrance (0..100)
  // Approaching squad enters from start or end looking down the corridor.
  // Elevated perches exploit vertical blind spots (units look horizontal).
  let coverScore = 50;
  if (heightCm >= 600) coverScore += 35;
  else if (heightCm >= 300) coverScore += 25;
  if (isRoof) coverScore += 15; // parapet / roof setback cover
  coverScore = Math.min(100, Math.max(0, coverScore));

  // 3. Line of Sight along corridor length (0..100)
  // Perches along the edge overlooking the corridor have near 100% field of view.
  const corrLen = dist(corridor.start, corridor.end);
  const distToStart = dist(pos, corridor.start);
  const distToEnd = dist(pos, corridor.end);
  let losScore = 90;
  // If positioned near the center with high elevation, full enfilade coverage
  if (corrLen > 0 && Math.abs(distToStart - distToEnd) < corrLen * 0.4) {
    losScore = 100;
  } else if (distToStart < 300 || distToEnd < 300) {
    // Corner overlooking entrance/exit
    losScore = 85;
  }

  const totalScore = Math.round(0.35 * heightScore + 0.30 * coverScore + 0.35 * losScore);

  return { heightScore, coverScore, losScore, totalScore };
}

/** Generates candidate ambush positions for a corridor flank. */
function generateFlankPerches(
  block: Block3D,
  side: 'left' | 'right',
  corridor: { start: Vec2; end: Vec2; axis: 'X' | 'Y'; widthCm: number }
): AmbushPosition[] {
  const perches: AmbushPosition[] = [];
  const axis = corridor.axis;

  // Determine facing edge coordinate of the block
  let facingCoord: number;
  if (axis === 'X') {
    // Corridor runs along X; blocks are to the left (smaller Y) or right (greater Y)
    facingCoord = side === 'left' ? block.max[1] : block.min[1];
  } else {
    // Corridor runs along Y; blocks are to the left (smaller X) or right (greater X)
    facingCoord = side === 'left' ? block.max[0] : block.min[0];
  }

  // 1. Center Roof Perch overlooking the corridor
  const centerCoord = axis === 'X'
    ? [(corridor.start[0] + corridor.end[0]) / 2, facingCoord] as Vec2
    : [facingCoord, (corridor.start[1] + corridor.end[1]) / 2] as Vec2;

  const centerScores = scoreAmbushPosition(centerCoord, block.height, corridor, block.roofWalkable);
  perches.push({
    id: `${block.id}_roof_center`,
    side,
    pos: centerCoord,
    heightCm: block.height,
    level: Math.round(block.height / 300),
    isRoof: true,
    buildingId: block.id,
    ...centerScores,
  });

  // 2. Entrance Corner Perch
  const entranceCoord = axis === 'X'
    ? [corridor.start[0], facingCoord] as Vec2
    : [facingCoord, corridor.start[1]] as Vec2;
  const entranceScores = scoreAmbushPosition(entranceCoord, block.height, corridor, block.roofWalkable);
  perches.push({
    id: `${block.id}_roof_entrance`,
    side,
    pos: entranceCoord,
    heightCm: block.height,
    level: Math.round(block.height / 300),
    isRoof: true,
    buildingId: block.id,
    ...entranceScores,
  });

  // 3. Exit Corner Perch
  const exitCoord = axis === 'X'
    ? [corridor.end[0], facingCoord] as Vec2
    : [facingCoord, corridor.end[1]] as Vec2;
  const exitScores = scoreAmbushPosition(exitCoord, block.height, corridor, block.roofWalkable);
  perches.push({
    id: `${block.id}_roof_exit`,
    side,
    pos: exitCoord,
    heightCm: block.height,
    level: Math.round(block.height / 300),
    isRoof: true,
    buildingId: block.id,
    ...exitScores,
  });

  return perches;
}

/** Finds corridors 1-3 m wide between blocks >= 3 m tall. */
export function findChokepointCorridors(
  blocks: Block3D[],
  minWidthCm = 100,
  maxWidthCm = 300,
  minLengthCm = 150
): ChokepointCorridor[] {
  const corridors: ChokepointCorridor[] = [];

  for (let i = 0; i < blocks.length; i++) {
    const b1 = blocks[i];
    for (let j = i + 1; j < blocks.length; j++) {
      const b2 = blocks[j];

      // Case A: Corridors running along X (north-south), separated along Y (east-west)
      const gapY1 = b2.min[1] - b1.max[1];
      const gapY2 = b1.min[1] - b2.max[1];
      let gapY = -1;
      let leftY: Block3D | null = null;
      let rightY: Block3D | null = null;

      if (gapY1 >= minWidthCm && gapY1 <= maxWidthCm) {
        gapY = gapY1;
        leftY = b1;
        rightY = b2;
      } else if (gapY2 >= minWidthCm && gapY2 <= maxWidthCm) {
        gapY = gapY2;
        leftY = b2;
        rightY = b1;
      }

      if (gapY > 0 && leftY && rightY) {
        const overlapMinX = Math.max(leftY.min[0], rightY.min[0]);
        const overlapMaxX = Math.min(leftY.max[0], rightY.max[0]);
        const lenX = overlapMaxX - overlapMinX;

        if (lenX >= minLengthCm) {
          const centerY = (leftY.max[1] + rightY.min[1]) / 2;
          const start: Vec2 = [overlapMinX, centerY];
          const end: Vec2 = [overlapMaxX, centerY];
          const center: Vec2 = [(overlapMinX + overlapMaxX) / 2, centerY];

          const corridorData = { start, end, axis: 'X' as const, widthCm: gapY };
          const leftPerches = generateFlankPerches(leftY, 'left', corridorData);
          const rightPerches = generateFlankPerches(rightY, 'right', corridorData);
          const allPerches = [...leftPerches, ...rightPerches];

          const leftBest = Math.max(...leftPerches.map((p) => p.totalScore), 0);
          const rightBest = Math.max(...rightPerches.map((p) => p.totalScore), 0);

          let killZoneRating: number;
          let category: ChokepointCategory;

          if (leftBest >= 60 && rightBest >= 60) {
            // High crossfire from both flanks
            killZoneRating = Math.min(100, Math.round((leftBest + rightBest) / 2 + 15));
            category = Math.min(leftY.height, rightY.height) >= 600 ? 'fatal_funnel' : 'crossfire_alley';
          } else if (Math.max(leftBest, rightBest) >= 60) {
            killZoneRating = Math.round((leftBest + rightBest) / 2);
            category = 'elevated_ambush';
          } else {
            killZoneRating = Math.round((leftBest + rightBest) / 2);
            category = 'standard';
          }

          corridors.push({
            id: `CHOKE_${leftY.id}_${rightY.id}`,
            axis: 'X',
            start,
            end,
            center,
            widthCm: gapY,
            widthM: Math.round((gapY / 100) * 10) / 10,
            lengthCm: lenX,
            lengthM: Math.round((lenX / 100) * 10) / 10,
            leftBlockId: leftY.id,
            rightBlockId: rightY.id,
            leftHeightCm: leftY.height,
            rightHeightCm: rightY.height,
            leftBestScore: leftBest,
            rightBestScore: rightBest,
            killZoneRating,
            category,
            ambushPositions: allPerches,
          });
        }
      }

      // Case B: Corridors running along Y (east-west), separated along X (north-south)
      const gapX1 = b2.min[0] - b1.max[0];
      const gapX2 = b1.min[0] - b2.max[0];
      let gapX = -1;
      let leftX: Block3D | null = null;
      let rightX: Block3D | null = null;

      if (gapX1 >= minWidthCm && gapX1 <= maxWidthCm) {
        gapX = gapX1;
        leftX = b1;
        rightX = b2;
      } else if (gapX2 >= minWidthCm && gapX2 <= maxWidthCm) {
        gapX = gapX2;
        leftX = b2;
        rightX = b1;
      }

      if (gapX > 0 && leftX && rightX) {
        const overlapMinY = Math.max(leftX.min[1], rightX.min[1]);
        const overlapMaxY = Math.min(leftX.max[1], rightX.max[1]);
        const lenY = overlapMaxY - overlapMinY;

        if (lenY >= minLengthCm) {
          const centerX = (leftX.max[0] + rightX.min[0]) / 2;
          const start: Vec2 = [centerX, overlapMinY];
          const end: Vec2 = [centerX, overlapMaxY];
          const center: Vec2 = [centerX, (overlapMinY + overlapMaxY) / 2];

          const corridorData = { start, end, axis: 'Y' as const, widthCm: gapX };
          const leftPerches = generateFlankPerches(leftX, 'left', corridorData);
          const rightPerches = generateFlankPerches(rightX, 'right', corridorData);
          const allPerches = [...leftPerches, ...rightPerches];

          const leftBest = Math.max(...leftPerches.map((p) => p.totalScore), 0);
          const rightBest = Math.max(...rightPerches.map((p) => p.totalScore), 0);

          let killZoneRating: number;
          let category: ChokepointCategory;

          if (leftBest >= 60 && rightBest >= 60) {
            killZoneRating = Math.min(100, Math.round((leftBest + rightBest) / 2 + 15));
            category = Math.min(leftX.height, rightX.height) >= 600 ? 'fatal_funnel' : 'crossfire_alley';
          } else if (Math.max(leftBest, rightBest) >= 60) {
            killZoneRating = Math.round((leftBest + rightBest) / 2);
            category = 'elevated_ambush';
          } else {
            killZoneRating = Math.round((leftBest + rightBest) / 2);
            category = 'standard';
          }

          corridors.push({
            id: `CHOKE_${leftX.id}_${rightX.id}`,
            axis: 'Y',
            start,
            end,
            center,
            widthCm: gapX,
            widthM: Math.round((gapX / 100) * 10) / 10,
            lengthCm: lenY,
            lengthM: Math.round((lenY / 100) * 10) / 10,
            leftBlockId: leftX.id,
            rightBlockId: rightX.id,
            leftHeightCm: leftX.height,
            rightHeightCm: rightX.height,
            leftBestScore: leftBest,
            rightBestScore: rightBest,
            killZoneRating,
            category,
            ambushPositions: allPerches,
          });
        }
      }
    }
  }

  // Sort descending by kill zone rating
  return corridors.sort((a, b) => b.killZoneRating - a.killZoneRating);
}

/** Generates a 2D heat-map model representing corridor danger zones and ambush hotspots. */
export function generateChokepointHeatmap(
  corridors: ChokepointCorridor[],
  _bounds?: { min: Vec2; max: Vec2 },
  cellSizeCm = 200
): { cellSizeCm: number; cells: HeatmapCell[] } {
  const cellMap = new Map<string, HeatmapCell>();

  for (const c of corridors) {
    const baseIntensity = c.killZoneRating / 100;
    const halfWidth = c.widthCm / 2;

    if (c.axis === 'X') {
      const minX = Math.min(c.start[0], c.end[0]);
      const maxX = Math.max(c.start[0], c.end[0]);
      const y = c.center[1];

      for (let x = minX; x <= maxX; x += cellSizeCm) {
        for (let dy = -halfWidth; dy <= halfWidth; dy += cellSizeCm) {
          const cy = y + dy;
          const kx = Math.round(x / cellSizeCm) * cellSizeCm;
          const ky = Math.round(cy / cellSizeCm) * cellSizeCm;
          const key = `${kx},${ky}`;
          const current = cellMap.get(key)?.intensity ?? 0;
          const distFromCenter = Math.abs(dy) / (halfWidth || 1);
          const intensity = Math.max(current, baseIntensity * (1 - distFromCenter * 0.3));
          cellMap.set(key, { x: kx, y: ky, intensity: Math.round(intensity * 100) / 100, chokepointId: c.id });
        }
      }
    } else {
      const minY = Math.min(c.start[1], c.end[1]);
      const maxY = Math.max(c.start[1], c.end[1]);
      const x = c.center[0];

      for (let y = minY; y <= maxY; y += cellSizeCm) {
        for (let dx = -halfWidth; dx <= halfWidth; dx += cellSizeCm) {
          const cx = x + dx;
          const kx = Math.round(cx / cellSizeCm) * cellSizeCm;
          const ky = Math.round(y / cellSizeCm) * cellSizeCm;
          const key = `${kx},${ky}`;
          const current = cellMap.get(key)?.intensity ?? 0;
          const distFromCenter = Math.abs(dx) / (halfWidth || 1);
          const intensity = Math.max(current, baseIntensity * (1 - distFromCenter * 0.3));
          cellMap.set(key, { x: kx, y: ky, intensity: Math.round(intensity * 100) / 100, chokepointId: c.id });
        }
      }
    }

    // Add high-danger hot spots at ambush positions
    for (const amb of c.ambushPositions) {
      const kx = Math.round(amb.pos[0] / cellSizeCm) * cellSizeCm;
      const ky = Math.round(amb.pos[1] / cellSizeCm) * cellSizeCm;
      const key = `${kx},${ky}`;
      cellMap.set(key, { x: kx, y: ky, intensity: 1.0, chokepointId: c.id });
    }
  }

  return {
    cellSizeCm,
    cells: Array.from(cellMap.values()),
  };
}

/** Analyzes a GenLayout for chokepoints, scoring ambush perches and producing full analysis report. */
export function analyzeChokepoints(layout: GenLayout): ChokepointAnalysisResult {
  const blocks = extractTallBlocks(layout);
  const corridors = findChokepointCorridors(blocks, 100, 300, 150);

  const totalCorridors = corridors.length;
  const narrowCount = corridors.filter((c) => c.widthCm <= 200).length;
  const crossfireCount = corridors.filter((c) => c.killZoneRating >= 75).length;
  const highestKillZoneRating = corridors.length ? Math.max(...corridors.map((c) => c.killZoneRating)) : 0;
  const meanWidthM = corridors.length
    ? Math.round((corridors.reduce((s, c) => s + c.widthM, 0) / corridors.length) * 10) / 10
    : 0;
  const meanLengthM = corridors.length
    ? Math.round((corridors.reduce((s, c) => s + c.lengthM, 0) / corridors.length) * 10) / 10
    : 0;

  const heatmap = generateChokepointHeatmap(corridors, layout.bounds, 200);

  return {
    layoutName: layout.name,
    corridors,
    summary: {
      totalCorridors,
      narrowCount,
      crossfireCount,
      highestKillZoneRating,
      meanWidthM,
      meanLengthM,
    },
    heatmap,
  };
}
