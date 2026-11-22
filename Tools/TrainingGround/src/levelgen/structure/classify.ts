// Standalone classification lookup table for ADR-0004: Destructible Buildings data contract (P0)

import type { StructureClass, StructureMaterial } from './types.ts';

export type ConstructionSystem =
  | 'block'
  | 'hall'
  | 'warehouse'
  | 'office'
  | 'shed'
  | 'ruin';

export const CONSTRUCTION_SYSTEMS: readonly ConstructionSystem[] = [
  'block',
  'hall',
  'warehouse',
  'office',
  'shed',
  'ruin',
] as const;

export type ConstructionRole =
  | 'core_wall' // stair-core walls, spine, elevator cores
  | 'column_wall' // cross-walls between column lines
  | 'perimeter_wall' // exterior envelope / facade wall
  | 'room_partition' // internal dividing room wall
  | 'annex_volume' // attached lean-to, shed, annex enclosure
  | 'cladding' // exterior skin / sheet infill
  | 'door_leaf' // door opening
  | 'window_pane' // window opening
  | 'roof_slab' // roof slab / deck
  | 'floor_slab'; // intermediate floor slab

export const CONSTRUCTION_ROLES: readonly ConstructionRole[] = [
  'core_wall',
  'column_wall',
  'perimeter_wall',
  'room_partition',
  'annex_volume',
  'cladding',
  'door_leaf',
  'window_pane',
  'roof_slab',
  'floor_slab',
] as const;

export interface ClassifiedStructure {
  structure: StructureClass;
  material: StructureMaterial;
}

/**
 * Pure lookup table implementing the ADR-0004 proposal "Classification by construction system":
 * - block: panel cross-wall, core walls and column walls are load-bearing concrete, room walls are plaster partitions
 * - hall: frame lines load-bearing, cladding is facade sheet metal (never a collapse)
 * - warehouse: frame lines load-bearing concrete, cladding is facade brick infill
 * - office: brick perimeter + spine are load-bearing brick, room partitions are plaster
 * - shed: light attached volume, annex wood/sheet_metal
 * - ruin: already failed, partitions/facade, no load-bearing elements (no further collapse)
 */
const CLASSIFICATION_TABLE: Record<ConstructionSystem, Record<ConstructionRole, ClassifiedStructure>> = {
  block: {
    core_wall: { structure: 'load_bearing', material: 'concrete_panel' },
    column_wall: { structure: 'load_bearing', material: 'concrete_panel' },
    perimeter_wall: { structure: 'facade', material: 'concrete_panel' },
    room_partition: { structure: 'partition', material: 'plaster' },
    annex_volume: { structure: 'annex', material: 'brick' },
    cladding: { structure: 'facade', material: 'concrete_panel' },
    door_leaf: { structure: 'partition', material: 'wood' },
    window_pane: { structure: 'facade', material: 'sheet_metal' },
    roof_slab: { structure: 'load_bearing', material: 'concrete_panel' },
    floor_slab: { structure: 'load_bearing', material: 'concrete_panel' },
  },
  hall: {
    core_wall: { structure: 'load_bearing', material: 'concrete_panel' },
    column_wall: { structure: 'load_bearing', material: 'sheet_metal' },
    perimeter_wall: { structure: 'facade', material: 'sheet_metal' },
    room_partition: { structure: 'partition', material: 'sheet_metal' },
    annex_volume: { structure: 'annex', material: 'sheet_metal' },
    cladding: { structure: 'facade', material: 'sheet_metal' },
    door_leaf: { structure: 'facade', material: 'sheet_metal' },
    window_pane: { structure: 'facade', material: 'sheet_metal' },
    roof_slab: { structure: 'load_bearing', material: 'sheet_metal' },
    floor_slab: { structure: 'load_bearing', material: 'concrete_panel' },
  },
  warehouse: {
    core_wall: { structure: 'load_bearing', material: 'concrete_panel' },
    column_wall: { structure: 'load_bearing', material: 'concrete_panel' },
    perimeter_wall: { structure: 'facade', material: 'brick' },
    room_partition: { structure: 'partition', material: 'brick' },
    annex_volume: { structure: 'annex', material: 'sheet_metal' },
    cladding: { structure: 'facade', material: 'brick' },
    door_leaf: { structure: 'facade', material: 'sheet_metal' },
    window_pane: { structure: 'facade', material: 'sheet_metal' },
    roof_slab: { structure: 'load_bearing', material: 'sheet_metal' },
    floor_slab: { structure: 'load_bearing', material: 'concrete_panel' },
  },
  office: {
    core_wall: { structure: 'load_bearing', material: 'brick' },
    column_wall: { structure: 'load_bearing', material: 'brick' },
    perimeter_wall: { structure: 'load_bearing', material: 'brick' },
    room_partition: { structure: 'partition', material: 'plaster' },
    annex_volume: { structure: 'annex', material: 'brick' },
    cladding: { structure: 'facade', material: 'brick' },
    door_leaf: { structure: 'partition', material: 'wood' },
    window_pane: { structure: 'facade', material: 'wood' },
    roof_slab: { structure: 'load_bearing', material: 'concrete_panel' },
    floor_slab: { structure: 'load_bearing', material: 'concrete_panel' },
  },
  shed: {
    core_wall: { structure: 'annex', material: 'wood' },
    column_wall: { structure: 'annex', material: 'wood' },
    perimeter_wall: { structure: 'annex', material: 'wood' },
    room_partition: { structure: 'partition', material: 'wood' },
    annex_volume: { structure: 'annex', material: 'wood' },
    cladding: { structure: 'annex', material: 'sheet_metal' },
    door_leaf: { structure: 'annex', material: 'wood' },
    window_pane: { structure: 'annex', material: 'wood' },
    roof_slab: { structure: 'annex', material: 'wood' },
    floor_slab: { structure: 'annex', material: 'wood' },
  },
  ruin: {
    core_wall: { structure: 'partition', material: 'brick' },
    column_wall: { structure: 'partition', material: 'brick' },
    perimeter_wall: { structure: 'facade', material: 'brick' },
    room_partition: { structure: 'partition', material: 'plaster' },
    annex_volume: { structure: 'annex', material: 'brick' },
    cladding: { structure: 'facade', material: 'brick' },
    door_leaf: { structure: 'partition', material: 'wood' },
    window_pane: { structure: 'facade', material: 'wood' },
    roof_slab: { structure: 'partition', material: 'concrete_panel' },
    floor_slab: { structure: 'partition', material: 'concrete_panel' },
  },
};

/**
 * Pure lookup function: maps a construction system archetype and element role to structure class and material.
 */
export function classifyElement(system: ConstructionSystem, role: ConstructionRole): ClassifiedStructure {
  const sysTable = CLASSIFICATION_TABLE[system];
  if (!sysTable) {
    throw new Error(`Unknown construction system: '${system}'`);
  }
  const entry = sysTable[role];
  if (!entry) {
    throw new Error(`Unknown construction role '${role}' for system '${system}'`);
  }
  return { ...entry };
}

export function isConstructionSystem(val: unknown): val is ConstructionSystem {
  return typeof val === 'string' && (CONSTRUCTION_SYSTEMS as readonly string[]).includes(val);
}

export function isConstructionRole(val: unknown): val is ConstructionRole {
  return typeof val === 'string' && (CONSTRUCTION_ROLES as readonly string[]).includes(val);
}
