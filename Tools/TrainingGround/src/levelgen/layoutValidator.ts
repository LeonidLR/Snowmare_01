// Training Ground / level generator: layout validation for wall-wall and wall-building crossings (task 085).
// Checks that no wall segment of one family crosses another family's wall or a building footprint (rotated).
// Returns conflict markers for the UI (LayoutView / LevelGenDepartment) and an automated cleanup helper.

import type { Vec2 } from '../movement/geometry.ts';
import { segmentsIntersect, segmentSegmentIntersection } from '../movement/geometry.ts';
import { footprintPoly } from './rotation.ts';
import { wallFamily } from './regionRegen.ts';
import type { GenBuilding, GenLayout, GenWall } from './schema.ts';

export interface WallCrossingConflict {
  id: string;
  kind: 'wall_wall' | 'wall_building';
  wallA: GenWall;
  wallB?: GenWall;
  building?: GenBuilding;
  familyA?: string;
  familyB?: string;
  point: Vec2;
  reason: string;
}

/**
 * Validates the layout for wall segments crossing another family's wall or a building footprint.
 * Returns a list of conflicts (empty = valid layout).
 */
export function validateWallCrossings(layout: GenLayout): WallCrossingConflict[] {
  const conflicts: WallCrossingConflict[] = [];
  const walls = layout.walls ?? [];
  const buildings = layout.buildings ?? [];

  // 1. Wall vs Wall crossings
  for (let i = 0; i < walls.length; i++) {
    const w1 = walls[i];
    const fl1 = w1.floor ?? 0;
    const fam1 = wallFamily(w1);

    for (let j = i + 1; j < walls.length; j++) {
      const w2 = walls[j];
      const fl2 = w2.floor ?? 0;
      if (fl1 !== fl2) continue; // walls on different floors do not cross in 3D

      // Ignore segments of the same building or same wall family
      if (w1.building && w1.building === w2.building) continue;
      const fam2 = wallFamily(w2);
      if (fam1 && fam2 && fam1 === fam2) continue;

      // Cut walls are retaining ground structures/trench borders; ignore if either is a cut wall
      if (w1.kind === 'cut_wall' || w2.kind === 'cut_wall') continue;

      if (segmentsIntersect(w1.a, w1.b, w2.a, w2.b)) {
        const pt = segmentSegmentIntersection(w1.a, w1.b, w2.a, w2.b) ?? [
          (w1.a[0] + w1.b[0]) / 2,
          (w1.a[1] + w1.b[1]) / 2,
        ];
        const name1 = fam1 ?? w1.id;
        const name2 = fam2 ?? w2.id;
        conflicts.push({
          id: `conflict_${w1.id}_${w2.id}`,
          kind: 'wall_wall',
          wallA: w1,
          wallB: w2,
          familyA: fam1,
          familyB: fam2,
          point: pt,
          reason: `Стена ${w1.id} (${w1.kind ?? 'wall'}, ${name1}) пересекает ${w2.id} (${w2.kind ?? 'wall'}, ${name2})`,
        });
      }
    }
  }

  // 2. Wall vs Building footprint crossings
  for (const w of walls) {
    if (w.floor && w.floor > 0) continue; // upper floor walls are inside/on the building
    if (w.kind === 'cut_wall') continue;
    const fam = wallFamily(w);

    for (const b of buildings) {
      if (w.building === b.id) continue; // walls belonging to this building

      const poly = footprintPoly(b);
      for (let k = 0; k < poly.length; k++) {
        const edgeA = poly[k], edgeB = poly[(k + 1) % poly.length];
        if (segmentsIntersect(w.a, w.b, edgeA, edgeB)) {
          const pt = segmentSegmentIntersection(w.a, w.b, edgeA, edgeB) ?? [
            (w.a[0] + w.b[0]) / 2,
            (w.a[1] + w.b[1]) / 2,
          ];
          conflicts.push({
            id: `conflict_${w.id}_${b.id}`,
            kind: 'wall_building',
            wallA: w,
            building: b,
            familyA: fam,
            point: pt,
            reason: `Стена ${w.id} (${w.kind ?? 'wall'}) пересекает контур здания ${b.id}`,
          });
          break; // report once per building
        }
      }
    }
  }

  return conflicts;
}

/**
 * Removes fence families that conflict with other fences or buildings.
 * When an outer/orphan fence crosses newer parcel fences or buildings, this removes the obsolete fence family.
 */
export function cleanConflictingFences(layout: GenLayout): {
  layout: GenLayout;
  removedFamilies: string[];
  removedWalls: string[];
} {
  const conflicts = validateWallCrossings(layout);
  if (!conflicts.length) {
    return { layout, removedFamilies: [], removedWalls: [] };
  }

  // Collect fence families involved in conflicts
  const toRemoveFamilies = new Set<string>();
  const toRemoveWallIds = new Set<string>();

  for (const c of conflicts) {
    // If one is a fence and crosses another fence or building:
    // When a fence family crosses another fence family, prioritize removing the one with the larger bounding box (parent macro-fence)
    if (c.familyA && c.familyB) {
      const wallsA = layout.walls.filter((w) => wallFamily(w) === c.familyA);
      const wallsB = layout.walls.filter((w) => wallFamily(w) === c.familyB);
      const lenA = wallsA.reduce((s, w) => s + Math.hypot(w.b[0] - w.a[0], w.b[1] - w.a[1]), 0);
      const lenB = wallsB.reduce((s, w) => s + Math.hypot(w.b[0] - w.a[0], w.b[1] - w.a[1]), 0);
      // The larger fence ring that encloses/crosses the smaller parcels is the orphan outer fence
      const victim = lenA >= lenB ? c.familyA : c.familyB;
      toRemoveFamilies.add(victim);
    } else if (c.familyA && c.wallA.kind === 'fence') {
      toRemoveFamilies.add(c.familyA);
    } else if (c.familyB && c.wallB?.kind === 'fence') {
      toRemoveFamilies.add(c.familyB);
    } else {
      if (c.wallA.kind === 'fence') toRemoveWallIds.add(c.wallA.id);
      if (c.wallB?.kind === 'fence') toRemoveWallIds.add(c.wallB.id);
    }
  }

  for (const fam of toRemoveFamilies) {
    for (const w of layout.walls) {
      if (wallFamily(w) === fam) toRemoveWallIds.add(w.id);
    }
  }

  const nextWalls = layout.walls.filter((w) => !toRemoveWallIds.has(w.id));
  return {
    layout: { ...layout, walls: nextWalls },
    removedFamilies: [...toRemoveFamilies],
    removedWalls: [...toRemoveWallIds],
  };
}
