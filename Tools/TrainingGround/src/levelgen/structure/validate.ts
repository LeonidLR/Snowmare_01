// Standalone V3 and V5 validation helpers for ADR-0004: Destructible Buildings data contract (P0)

import crypto from 'node:crypto';
import type {
  StructureClassesConfig,
  StructuredBuilding,
  StructureInfo,
} from './types.ts';

export interface ValidationFailure {
  rule:
    | 'V3_acyclic'
    | 'V3_supports_count'
    | 'V3_partition_supports_nothing'
    | 'V3_annex_supports_annex_only'
    | 'V3_hp_class_resolves'
    | 'V3_dangling_reference';
  element_id: string;
  message: string;
}

// ---------------------------------------------------------------------------
// V5 Helpers: Deterministic Canonical JSON Stringification & Hashing
// ---------------------------------------------------------------------------

/**
 * Serializes any JavaScript object or primitive into a canonical, deterministic JSON string
 * with sorted object keys. Byte-identical across runs regardless of key insertion order.
 */
export function canonicalizeJson(obj: unknown): string {
  if (obj === null || typeof obj !== 'object') {
    return JSON.stringify(obj);
  }
  if (Array.isArray(obj)) {
    return `[${obj.map((item) => canonicalizeJson(item)).join(',')}]`;
  }
  const rec = obj as Record<string, unknown>;
  const keys = Object.keys(rec).sort();
  const pairs = keys.map((k) => `${JSON.stringify(k)}:${canonicalizeJson(rec[k])}`);
  return `{${pairs.join(',')}}`;
}

/**
 * Computes a deterministic SHA-256 hex digest of a structure classes configuration object.
 * Stable on repeated calls with unordered keys; changes if any value or key changes.
 */
export function structureHash(classesJson: object): string {
  const canonical = canonicalizeJson(classesJson);
  return crypto.createHash('sha256').update(canonical, 'utf8').digest('hex');
}

// ---------------------------------------------------------------------------
// V3 Rules Validator
// ---------------------------------------------------------------------------

interface ElementEntry {
  id: string;
  kind: 'wall' | 'partition' | 'opening' | 'stair' | 'slab';
  struct?: StructureInfo;
  supported_by?: string[];
  needs?: number;
  is_annex?: boolean;
}

function collectElements(
  building: StructuredBuilding,
  failures?: ValidationFailure[],
): Map<string, ElementEntry> {
  const map = new Map<string, ElementEntry>();

  if (building.walls) {
    for (let i = 0; i < building.walls.length; i++) {
      const w = building.walls[i];
      const id = w.id ?? `${building.id}_wall_${i}`;
      map.set(id, {
        id,
        kind: 'wall',
        struct: w.struct,
        is_annex: w.struct?.structure === 'annex',
      });
    }
  }

  if (building.partitions) {
    for (let i = 0; i < building.partitions.length; i++) {
      const p = building.partitions[i];
      const id = p.id ?? `${building.id}_part_${i}`;
      map.set(id, {
        id,
        kind: 'partition',
        struct: p.struct,
        is_annex: p.struct?.structure === 'annex',
      });
    }
  }

  if (building.openings) {
    for (let i = 0; i < building.openings.length; i++) {
      const o = building.openings[i];
      const id = o.id ?? `${building.id}_opening_${i}`;
      map.set(id, {
        id,
        kind: 'opening',
        struct: o.struct,
        is_annex: o.struct?.structure === 'annex',
      });
    }
  }

  if (building.stairs) {
    for (let i = 0; i < building.stairs.length; i++) {
      const s = building.stairs[i];
      const id = s.id ?? `${building.id}_stair_${i}`;
      let needs: number | undefined;
      if (s.needs !== undefined) {
        if (typeof s.needs === 'number' && Number.isFinite(s.needs)) {
          needs = s.needs;
        } else {
          needs = s.needs as number;
          if (failures) {
            failures.push({
              rule: 'V3_supports_count',
              element_id: id,
              message: `Stair '${id}' has invalid needs value '${s.needs}' (must be a finite number)`,
            });
          }
        }
      } else {
        needs = s.supported_by?.length ? 1 : 0;
      }
      map.set(id, {
        id,
        kind: 'stair',
        supported_by: s.supported_by,
        needs,
        is_annex: (s as any).is_annex === true,
      });
    }
  }

  if (building.slab_panels) {
    for (const slab of building.slab_panels) {
      map.set(slab.id, {
        id: slab.id,
        kind: 'slab',
        supported_by: slab.supported_by,
        needs: slab.needs,
        is_annex: (slab as any).is_annex === true,
      });
    }
  }

  return map;
}

/**
 * Validates V3 sub-rules on a single building object against an optional structure classes configuration.
 * Pure function: never throws; returns an array of failures.
 *
 * Rules:
 * - V3_acyclic: support graph must be acyclic (no circular load paths)
 * - V3_supports_count: every slab panel and stair flight must have supported_by.length >= needs
 * - V3_partition_supports_nothing: a partition carries nothing
 * - V3_annex_supports_annex_only: an annex carries only annex elements
 * - V3_hp_class_resolves: every referenced hp_class exists in classesJson
 */
export function validateBuildingStructure(
  building: StructuredBuilding,
  classesConfig?: StructureClassesConfig,
): ValidationFailure[] {
  const failures: ValidationFailure[] = [];
  const elements = collectElements(building, failures);

  // Build directed support graph: u -> v means "u carries/supports v"
  const adj = new Map<string, Set<string>>();
  const getAdj = (u: string): Set<string> => {
    let s = adj.get(u);
    if (!s) {
      s = new Set<string>();
      adj.set(u, s);
    }
    return s;
  };

  for (const [id, entry] of elements.entries()) {
    // 1. From entry.struct.supports: id carries target
    if (entry.struct?.supports) {
      for (const target of entry.struct.supports) {
        getAdj(id).add(target);
        if (!elements.has(target)) {
          failures.push({
            rule: 'V3_dangling_reference',
            element_id: id,
            message: `Element '${id}' supports non-existent element '${target}'`,
          });
        }
      }
    }
    // 2. From entry.supported_by: carrier carries id
    if (entry.supported_by) {
      for (const carrier of entry.supported_by) {
        getAdj(carrier).add(id);
        if (!elements.has(carrier)) {
          failures.push({
            rule: 'V3_dangling_reference',
            element_id: id,
            message: `Element '${id}' is supported by non-existent element '${carrier}'`,
          });
        }
      }
    }
  }

  // -------------------------------------------------------------------------
  // Rule V3.1: Acyclic Support Graph
  // -------------------------------------------------------------------------
  const visited = new Set<string>();
  const inStack = new Set<string>();
  const cycleNodes = new Set<string>();

  function dfs(u: string, pathNodes: string[]): void {
    visited.add(u);
    inStack.add(u);
    pathNodes.push(u);

    const neighbors = adj.get(u);
    if (neighbors) {
      for (const v of neighbors) {
        if (!visited.has(v)) {
          dfs(v, pathNodes);
        } else if (inStack.has(v)) {
          // Cycle found
          const cycleStartIdx = pathNodes.indexOf(v);
          const cyclePath = pathNodes.slice(cycleStartIdx).concat(v);
          for (const node of cyclePath) {
            cycleNodes.add(node);
          }
          failures.push({
            rule: 'V3_acyclic',
            element_id: v,
            message: `Circular support dependency detected: ${cyclePath.join(' -> ')}`,
          });
        }
      }
    }

    pathNodes.pop();
    inStack.delete(u);
  }

  for (const node of adj.keys()) {
    if (!visited.has(node)) {
      dfs(node, []);
    }
  }

  // -------------------------------------------------------------------------
  // Rule V3.2: Supports Count (supported_by.length >= needs)
  // -------------------------------------------------------------------------
  for (const [id, entry] of elements.entries()) {
    if (entry.needs !== undefined) {
      if (!Number.isFinite(entry.needs)) {
        if (!failures.some((f) => f.rule === 'V3_supports_count' && f.element_id === id)) {
          failures.push({
            rule: 'V3_supports_count',
            element_id: id,
            message: `Element '${id}' has invalid needs value '${entry.needs}' (must be a finite number)`,
          });
        }
      } else if (entry.needs > 0) {
        const count = entry.supported_by?.length ?? 0;
        if (count < entry.needs) {
          failures.push({
            rule: 'V3_supports_count',
            element_id: id,
            message: `Element '${id}' requires at least ${entry.needs} support(s), but only has ${count}`,
          });
        }
      }
    }
  }

  // -------------------------------------------------------------------------
  // Rule V3.3: Partition Supports Nothing
  // -------------------------------------------------------------------------
  for (const [id, entry] of elements.entries()) {
    if (entry.struct?.structure === 'partition') {
      const carried = adj.get(id);
      if (carried && carried.size > 0) {
        failures.push({
          rule: 'V3_partition_supports_nothing',
          element_id: id,
          message: `Partition '${id}' cannot support other elements (supports: ${Array.from(carried).join(', ')})`,
        });
      }
    }
  }

  // -------------------------------------------------------------------------
  // Rule V3.4: Annex Supports Annex Only
  // -------------------------------------------------------------------------
  for (const [id, entry] of elements.entries()) {
    if (entry.struct?.structure === 'annex') {
      const carried = adj.get(id);
      if (carried) {
        for (const targetId of carried) {
          const target = elements.get(targetId);
          // Target must be an annex element
          if (target && !target.is_annex && target.struct?.structure !== 'annex') {
            failures.push({
              rule: 'V3_annex_supports_annex_only',
              element_id: id,
              message: `Annex element '${id}' cannot support non-annex element '${targetId}' (structure=${target.struct?.structure ?? target.kind})`,
            });
          }
        }
      }
    }
  }

  // -------------------------------------------------------------------------
  // Rule V3.5: HP Class Resolves
  // -------------------------------------------------------------------------
  if (classesConfig && classesConfig.hp_classes) {
    const validHpClasses = new Set(Object.keys(classesConfig.hp_classes));
    for (const [id, entry] of elements.entries()) {
      if (entry.struct) {
        const hp = entry.struct.hp_class;
        if (!validHpClasses.has(hp)) {
          failures.push({
            rule: 'V3_hp_class_resolves',
            element_id: id,
            message: `Element '${id}' specifies hp_class '${hp}' which does not exist in structure_classes.json`,
          });
        }
      }
    }
  }

  return failures;
}
