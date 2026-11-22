// Standalone types and type guards for ADR-0004: Destructible Buildings data contract (P0)

export type StructureClass = 'load_bearing' | 'partition' | 'annex' | 'facade';
export const STRUCTURE_CLASSES: readonly StructureClass[] = [
  'load_bearing',
  'partition',
  'annex',
  'facade',
] as const;

export type StructureMaterial =
  | 'concrete_panel'
  | 'brick'
  | 'plaster'
  | 'wood'
  | 'sheet_metal';
export const STRUCTURE_MATERIALS: readonly StructureMaterial[] = [
  'concrete_panel',
  'brick',
  'plaster',
  'wood',
  'sheet_metal',
] as const;

export interface StructureInfo {
  structure: StructureClass;
  material: StructureMaterial;
  hp_class: string; // key of structure_classes.json
  supports?: string[]; // element ids this one carries
  protected?: boolean; // never fails (validator-set, rule V2)
}

export interface SlabPanel {
  id: string;
  floor: number;
  rect: [[number, number], [number, number]]; // [Vec2, Vec2]
  supported_by: string[];
  needs: number;
  on_fail: 'floor_hole' | 'none';
  protected?: boolean;
}

export interface StructuredWall {
  id?: string;
  struct?: StructureInfo;
  [key: string]: unknown;
}

export interface StructuredOpening {
  id?: string;
  struct?: StructureInfo;
  [key: string]: unknown;
}

export interface StructuredStair {
  id: string;
  supported_by?: string[];
  on_fail?: 'blocked_rubble' | 'none';
  [key: string]: unknown;
}

export interface StructuredBuilding {
  id: string;
  walls?: StructuredWall[];
  partitions?: (StructuredWall & { id: string })[];
  openings?: StructuredOpening[];
  stairs?: StructuredStair[];
  slab_panels?: SlabPanel[];
  structure_hash?: string;
  [key: string]: unknown;
}

export interface HpClassConfig {
  _placeholder?: boolean;
  hp_per_cell: number;
  min_breach_class: string;
  noise_m: number;
  rubble: string;
  [key: string]: unknown;
}

export interface StructureClassesConfig {
  _comment?: string;
  classes: Record<string, string>; // "<structure>.<material>" -> "<hp_class>"
  hp_classes: Record<string, HpClassConfig>;
  [key: string]: unknown;
}

// ---------------------------------------------------------------------------
// Type guards
// ---------------------------------------------------------------------------

export function isStructureClass(val: unknown): val is StructureClass {
  return typeof val === 'string' && (STRUCTURE_CLASSES as readonly string[]).includes(val);
}

export function isStructureMaterial(val: unknown): val is StructureMaterial {
  return typeof val === 'string' && (STRUCTURE_MATERIALS as readonly string[]).includes(val);
}

export function isStructureInfo(val: unknown): val is StructureInfo {
  if (!val || typeof val !== 'object') return false;
  const o = val as Partial<StructureInfo>;
  if (!isStructureClass(o.structure)) return false;
  if (!isStructureMaterial(o.material)) return false;
  if (typeof o.hp_class !== 'string' || !o.hp_class.trim()) return false;
  if (o.supports !== undefined && (!Array.isArray(o.supports) || !o.supports.every((s) => typeof s === 'string'))) {
    return false;
  }
  if (o.protected !== undefined && typeof o.protected !== 'boolean') {
    return false;
  }
  return true;
}

export function isSlabPanel(val: unknown): val is SlabPanel {
  if (!val || typeof val !== 'object') return false;
  const o = val as Partial<SlabPanel>;
  if (typeof o.id !== 'string' || !o.id.trim()) return false;
  if (typeof o.floor !== 'number' || !Number.isInteger(o.floor) || o.floor < 0) return false;
  if (
    !Array.isArray(o.rect) ||
    o.rect.length !== 2 ||
    !Array.isArray(o.rect[0]) ||
    !Array.isArray(o.rect[1]) ||
    o.rect[0].length !== 2 ||
    o.rect[1].length !== 2 ||
    typeof o.rect[0][0] !== 'number' ||
    typeof o.rect[0][1] !== 'number' ||
    typeof o.rect[1][0] !== 'number' ||
    typeof o.rect[1][1] !== 'number'
  ) {
    return false;
  }
  if (!Array.isArray(o.supported_by) || !o.supported_by.every((s) => typeof s === 'string')) {
    return false;
  }
  if (typeof o.needs !== 'number' || o.needs < 0) return false;
  if (o.on_fail !== 'floor_hole' && o.on_fail !== 'none') return false;
  if (o.protected !== undefined && typeof o.protected !== 'boolean') return false;
  return true;
}
