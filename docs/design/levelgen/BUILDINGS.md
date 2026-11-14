# Combat-Ready Buildings Specification (codex.levelgen_layout/1 extension)

**Author:** Gemini (Designer directive 2026-11-12, Task 027)  
**Status:** Approved Schema Extension  
**Scope:** TypeScript Training Ground (`Tools/TrainingGround/src/levelgen`) & C++ Builder Spec (`Source/CodexTactics/Private/LevelGen`)

---

## 1. Overview and Motivation

For PMC combat tactical simulation (Learning Agents) and realistic tactical infantry training, generated buildings require realistic combat architecture:
- Windows on facades providing tactical line of sight and vaulting/mantling access.
- Fortified loopholes (narrow firing slits) in checkpoints, bunkers, and guard towers.
- Interior partition walls dividing floor footprints into rooms and corridors.
- Multi-storey floor slabs with gap cutouts (stairwells, atriums, catwalks, balconies).
- Hatches providing vertical roof access from top floors.
- Roof parapets serving as chest-high cover.
- Mantle edge markers (50–150 cm) allowing operatives to climb and vault over low obstacles and windowsills.

---

## 2. Schema Specification

### 2.1 Wall Openings (`WallOpening`)
Replaces and unifies legacy doorways with a generalized opening structure:
```typescript
export const OPENING_KINDS = ['door', 'window', 'loophole'] as const;
export type OpeningKind = (typeof OPENING_KINDS)[number];

export interface WallOpening {
  id?: string;
  kind: OpeningKind;
  pos: Vec2;           // Center position in UE cm [X, Y]
  floor: number;       // Storey floor index (0 = ground, 1 = 1st floor...)
  wall?: string;       // Associated perimeter or partition wall ID
  width?: number;      // Width along wall face (cm)
  sill_height?: number;// Height of sill above slab (cm)
  top_height?: number; // Height of opening top above slab (cm)
  yaw?: number;        // Outward facing sight/fire direction (deg)
}
```

#### Dimensions and Standards:
- **`door`**: `width: 120 cm`, `sill_height: 0 cm`, `top_height: 210 cm`. Walk-through access.
- **`window`**: `width: 120 cm`, `sill_height: 90 cm`, `top_height: 210 cm`. Firing and observation port; vaultable via mantling.
- **`loophole`**: `width: 20–40 cm` (default `30 cm`), `sill_height: 110–140 cm` (default `120 cm`), `top_height: 150 cm`. Fortified slit; blocks grenades and bodies, permits rifle/MG fire with minimal exposure.

### 2.2 Interior Partitions (`GenPartition`)
Divides building floors into rooms, corridors, and compartments:
```typescript
export interface GenPartition {
  id: string;
  start: Vec2;         // [X, Y] cm
  end: Vec2;           // [X, Y] cm
  floor: number;       // Floor index
  height?: number;     // Default: LEVEL_HEIGHT (300 cm)
  thickness?: number;  // Default: 20 cm
  openings?: WallOpening[]; // Interior doors or observation slits
}
```

### 2.3 Storey Floor Slabs and Gaps (`BuildingFloor`, `FloorGap`)
Defines floor slabs and voids:
```typescript
export interface FloorGap {
  id?: string;
  poly: Vec2[];        // Polygon bounding the void/cutout
  kind?: 'stairwell' | 'landing' | 'atrium' | 'balcony' | 'catwalk';
}

export interface BuildingFloor {
  floor: number;
  height_z?: number;   // Absolute elevation cm = floor * 300 cm
  gaps?: FloorGap[];   // Cutouts through the slab
  rooms?: GenRoom[];   // Sub-room zoning
}
```

### 2.4 Roof Hatches (`RoofHatch`)
Direct vertical access to building roofs:
```typescript
export interface RoofHatch {
  id: string;
  pos: Vec2;           // [X, Y] cm
  floor: number;       // Upper floor originating the hatch
  size?: [number, number]; // Default [100, 100] cm
  ladder_id?: string;  // Associated ladder id
}
```

### 2.5 Roof Parapets (`RoofParapet`)
Low protective walls around roof perimeters providing chest-high cover:
```typescript
export interface RoofParapet {
  wall_id?: string;
  start: Vec2;
  end: Vec2;
  height_cm: number;   // Typically 100 cm (cover height)
  thickness_cm?: number;// Typically 20 cm
}
```

### 2.6 Mantle Edge Markers (`MantleEdge`)
Explicit traversal markers for the future mantle/vault system:
```typescript
export const MIN_MANTLE_HEIGHT_CM = 50;
export const MAX_MANTLE_HEIGHT_CM = 150;

export interface MantleEdge {
  id: string;
  start: Vec2;
  end: Vec2;
  floor: number;
  base_z: number;      // Base elevation in UE cm
  height_cm: number;   // Must be within [50..150] cm
  normal: Vec2;        // Vault direction normal [nx, ny]
  kind?: 'window' | 'fence' | 'parapet' | 'obstacle' | 'crate' | 'partition' | 'ledge';
  building?: string;
  wall?: string;
}
```

---

## 3. Backward Compatibility
- Existing `doors: Door[]` on `GenBuilding` remains fully supported. Legacy layouts without `openings` continue to work unchanged.
- Helper `unifyOpenings(building)` transforms legacy doors into `WallOpening` instances dynamically.
- `toFormationLayout(layout, layer)` handles both legacy and extended layouts, ensuring all simulator, training, and Unreal export workflows remain 100% operational.
