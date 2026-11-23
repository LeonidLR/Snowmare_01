// Unit tests for ADR-0004: Destructible Buildings data contract (P0) standalone module
// Validates types, V3 sub-rules (pass + fail for each), V5 structureHash, and classify lookup.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  isSlabPanel,
  isStructureClass,
  isStructureInfo,
  isStructureMaterial,
  STRUCTURE_CLASSES,
  STRUCTURE_MATERIALS,
  type SlabPanel,
  type StructureClassesConfig,
  type StructuredBuilding,
  type StructureInfo,
} from './types.ts';
import { canonicalizeJson, structureHash, validateBuildingStructure } from './validate.ts';
import {
  classifyElement,
  CONSTRUCTION_ROLES,
  CONSTRUCTION_SYSTEMS,
  type ConstructionRole,
  type ConstructionSystem,
} from './classify.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../../..');
const CLASSES_JSON_PATH = path.join(ROOT, 'Content/Data/LevelGen/structure_classes.json');

function loadClassesConfig(): StructureClassesConfig {
  assert.ok(fs.existsSync(CLASSES_JSON_PATH), `structure_classes.json must exist at ${CLASSES_JSON_PATH}`);
  return JSON.parse(fs.readFileSync(CLASSES_JSON_PATH, 'utf8')) as StructureClassesConfig;
}

// ---------------------------------------------------------------------------
// 1. Types and Type Guards
// ---------------------------------------------------------------------------

test('structure/types: type guards identify valid and invalid values', () => {
  for (const c of STRUCTURE_CLASSES) {
    assert.equal(isStructureClass(c), true);
  }
  assert.equal(isStructureClass('invalid_class'), false);
  assert.equal(isStructureClass(null), false);

  for (const m of STRUCTURE_MATERIALS) {
    assert.equal(isStructureMaterial(m), true);
  }
  assert.equal(isStructureMaterial('titanium'), false);
  assert.equal(isStructureMaterial(123), false);

  const validInfo: StructureInfo = {
    structure: 'load_bearing',
    material: 'concrete_panel',
    hp_class: 'hp_heavy_concrete',
    supports: ['slab_1'],
    protected: false,
  };
  assert.equal(isStructureInfo(validInfo), true);
  assert.equal(isStructureInfo({ structure: 'unknown', material: 'wood', hp_class: 'foo' }), false);
  assert.equal(isStructureInfo({ structure: 'load_bearing', material: 'wood', hp_class: '' }), false);

  const validSlab: SlabPanel = {
    id: 'slab_1',
    floor: 1,
    rect: [[0, 0], [1000, 1000]],
    supported_by: ['wall_1', 'wall_2'],
    needs: 2,
    on_fail: 'floor_hole',
    protected: true,
  };
  assert.equal(isSlabPanel(validSlab), true);
  assert.equal(isSlabPanel({ id: 'slab_1', floor: -1 }), false);
  assert.equal(isSlabPanel({ ...validSlab, on_fail: 'explosion' }), false);
  assert.equal(isSlabPanel({ ...validSlab, rect: [[NaN, 0], [1000, 1000]] }), false);
  assert.equal(isSlabPanel({ ...validSlab, rect: [[0, Infinity], [1000, 1000]] }), false);
  assert.equal(isSlabPanel({ ...validSlab, rect: [[-Infinity, 0], [1000, 1000]] }), false);
  assert.equal(isSlabPanel({ ...validSlab, needs: NaN }), false);
  assert.equal(isSlabPanel({ ...validSlab, needs: Infinity }), false);
});

// ---------------------------------------------------------------------------
// 2. V3 Sub-rules: Acyclic Support Graph
// ---------------------------------------------------------------------------

test('structure/validate: V3.1 Acyclic rule (PASS case)', () => {
  const building: StructuredBuilding = {
    id: 'B_valid_dag',
    walls: [
      {
        id: 'wall_ground',
        struct: {
          structure: 'load_bearing',
          material: 'concrete_panel',
          hp_class: 'hp_heavy_concrete',
          supports: ['slab_f1'],
        },
      },
      {
        id: 'wall_f1',
        struct: {
          structure: 'load_bearing',
          material: 'concrete_panel',
          hp_class: 'hp_heavy_concrete',
          supports: ['slab_f2'],
        },
      },
    ],
    slab_panels: [
      {
        id: 'slab_f1',
        floor: 1,
        rect: [[0, 0], [600, 600]],
        supported_by: ['wall_ground'],
        needs: 1,
        on_fail: 'floor_hole',
      },
      {
        id: 'slab_f2',
        floor: 2,
        rect: [[0, 0], [600, 600]],
        supported_by: ['wall_f1'],
        needs: 1,
        on_fail: 'floor_hole',
      },
    ],
  };

  const failures = validateBuildingStructure(building);
  const acyclicFails = failures.filter((f) => f.rule === 'V3_acyclic');
  assert.equal(acyclicFails.length, 0, 'DAG should have 0 acyclic failures');
});

test('structure/validate: V3.1 Acyclic rule (FAIL case: circular loop A -> B -> A)', () => {
  const building: StructuredBuilding = {
    id: 'B_cycle',
    walls: [
      {
        id: 'wall_A',
        struct: {
          structure: 'load_bearing',
          material: 'concrete_panel',
          hp_class: 'hp_heavy_concrete',
          supports: ['wall_B'],
        },
      },
      {
        id: 'wall_B',
        struct: {
          structure: 'load_bearing',
          material: 'concrete_panel',
          hp_class: 'hp_heavy_concrete',
          supports: ['wall_A'], // cycle
        },
      },
    ],
  };

  const failures = validateBuildingStructure(building);
  const acyclicFails = failures.filter((f) => f.rule === 'V3_acyclic');
  assert.ok(acyclicFails.length >= 1, 'Cycle should trigger V3_acyclic failure');
  assert.ok(acyclicFails[0].message.includes('Circular support dependency'));
});

// ---------------------------------------------------------------------------
// 3. V3 Sub-rules: Supports Count (supported_by.length >= needs)
// ---------------------------------------------------------------------------

test('structure/validate: V3.2 Supports Count (PASS case)', () => {
  const building: StructuredBuilding = {
    id: 'B_good_supports',
    slab_panels: [
      {
        id: 'slab_1',
        floor: 1,
        rect: [[0, 0], [500, 500]],
        supported_by: ['col_1', 'col_2', 'col_3'],
        needs: 2,
        on_fail: 'floor_hole',
      },
    ],
  };

  const failures = validateBuildingStructure(building);
  const countFails = failures.filter((f) => f.rule === 'V3_supports_count');
  assert.equal(countFails.length, 0, 'Slab with 3 supports for needs 2 should PASS');
});

test('structure/validate: V3.2 Supports Count (FAIL case)', () => {
  const building: StructuredBuilding = {
    id: 'B_insufficient_supports',
    slab_panels: [
      {
        id: 'slab_weak',
        floor: 1,
        rect: [[0, 0], [500, 500]],
        supported_by: ['col_1'],
        needs: 2, // only 1 provided!
        on_fail: 'floor_hole',
      },
    ],
  };

  const failures = validateBuildingStructure(building);
  const countFails = failures.filter((f) => f.rule === 'V3_supports_count');
  assert.equal(countFails.length, 1, 'Slab with 1 support for needs 2 should FAIL');
  assert.equal(countFails[0].element_id, 'slab_weak');
  assert.ok(countFails[0].message.includes('requires at least 2 support(s), but only has 1'));
});

// ---------------------------------------------------------------------------
// 4. V3 Sub-rules: Partition Supports Nothing
// ---------------------------------------------------------------------------

test('structure/validate: V3.3 Partition Supports Nothing (PASS case)', () => {
  const building: StructuredBuilding = {
    id: 'B_clean_partition',
    partitions: [
      {
        id: 'part_interior_1',
        struct: {
          structure: 'partition',
          material: 'plaster',
          hp_class: 'hp_partition_plaster',
          supports: [], // supports nothing
        },
      },
    ],
  };

  const failures = validateBuildingStructure(building);
  const partFails = failures.filter((f) => f.rule === 'V3_partition_supports_nothing');
  assert.equal(partFails.length, 0, 'Partition with empty supports should PASS');
});

test('structure/validate: V3.3 Partition Supports Nothing (FAIL case)', () => {
  const building: StructuredBuilding = {
    id: 'B_bad_partition',
    partitions: [
      {
        id: 'part_interior_bad',
        struct: {
          structure: 'partition',
          material: 'plaster',
          hp_class: 'hp_partition_plaster',
          supports: ['slab_upper'], // illegal!
        },
      },
    ],
  };

  const failures = validateBuildingStructure(building);
  const partFails = failures.filter((f) => f.rule === 'V3_partition_supports_nothing');
  assert.equal(partFails.length, 1, 'Partition with supports should FAIL');
  assert.equal(partFails[0].element_id, 'part_interior_bad');
  assert.ok(partFails[0].message.includes('cannot support other elements'));
});

// ---------------------------------------------------------------------------
// 5. V3 Sub-rules: Annex Supports Annex Only
// ---------------------------------------------------------------------------

test('structure/validate: V3.4 Annex Supports Annex Only (PASS case)', () => {
  const building: StructuredBuilding = {
    id: 'B_clean_annex',
    walls: [
      {
        id: 'annex_wall_1',
        struct: {
          structure: 'annex',
          material: 'wood',
          hp_class: 'hp_annex_wood',
          supports: ['annex_roof_slab'],
        },
      },
    ],
    slab_panels: [
      {
        id: 'annex_roof_slab',
        floor: 1,
        rect: [[0, 0], [300, 300]],
        supported_by: ['annex_wall_1'],
        needs: 1,
        on_fail: 'floor_hole',
        is_annex: true, // annex element
      } as SlabPanel & { is_annex?: boolean },
    ],
  };

  const failures = validateBuildingStructure(building);
  const annexFails = failures.filter((f) => f.rule === 'V3_annex_supports_annex_only');
  assert.equal(annexFails.length, 0, 'Annex supporting annex slab should PASS');
});

test('structure/validate: V3.4 Annex Supports Annex Only (FAIL case)', () => {
  const building: StructuredBuilding = {
    id: 'B_bad_annex',
    walls: [
      {
        id: 'annex_wall_illegal',
        struct: {
          structure: 'annex',
          material: 'wood',
          hp_class: 'hp_annex_wood',
          supports: ['main_load_bearing_wall'], // illegal: annex carrying load-bearing element
        },
      },
      {
        id: 'main_load_bearing_wall',
        struct: {
          structure: 'load_bearing',
          material: 'concrete_panel',
          hp_class: 'hp_heavy_concrete',
        },
      },
    ],
  };

  const failures = validateBuildingStructure(building);
  const annexFails = failures.filter((f) => f.rule === 'V3_annex_supports_annex_only');
  assert.equal(annexFails.length, 1, 'Annex supporting load_bearing wall should FAIL');
  assert.equal(annexFails[0].element_id, 'annex_wall_illegal');
  assert.ok(annexFails[0].message.includes('cannot support non-annex element'));
});

// ---------------------------------------------------------------------------
// 6. V3 Sub-rules: HP Class Resolves
// ---------------------------------------------------------------------------

test('structure/validate: V3.5 HP Class Resolves (PASS and FAIL cases)', () => {
  const config = loadClassesConfig();

  const goodBuilding: StructuredBuilding = {
    id: 'B_good_hp',
    walls: [
      {
        id: 'w1',
        struct: {
          structure: 'load_bearing',
          material: 'concrete_panel',
          hp_class: 'hp_heavy_concrete', // valid in config
        },
      },
    ],
  };
  const goodFails = validateBuildingStructure(goodBuilding, config);
  assert.equal(goodFails.filter((f) => f.rule === 'V3_hp_class_resolves').length, 0);

  const badBuilding: StructuredBuilding = {
    id: 'B_bad_hp',
    walls: [
      {
        id: 'w_unobtanium',
        struct: {
          structure: 'load_bearing',
          material: 'concrete_panel',
          hp_class: 'hp_unobtainium_fantasy_material', // does not exist!
        },
      },
    ],
  };
  const badFails = validateBuildingStructure(badBuilding, config);
  const hpFails = badFails.filter((f) => f.rule === 'V3_hp_class_resolves');
  assert.equal(hpFails.length, 1);
  assert.equal(hpFails[0].element_id, 'w_unobtanium');
  assert.ok(hpFails[0].message.includes('does not exist in structure_classes.json'));
});

// ---------------------------------------------------------------------------
// 7. V5 Helper: structureHash Determinism & Sensitivity
// ---------------------------------------------------------------------------

test('structure/validate: V5 structureHash is byte-identical across 5 repeated calls with re-ordered keys', () => {
  const config = loadClassesConfig();

  // Create 5 copies with different key insertion orders
  const hashes: string[] = [];
  for (let i = 0; i < 5; i++) {
    // Reverse or shuffle keys
    const shuffled: Record<string, unknown> = {};
    const keys = Object.keys(config).sort(() => (i % 2 === 0 ? 1 : -1));
    for (const k of keys) {
      shuffled[k] = (config as any)[k];
    }
    hashes.push(structureHash(shuffled));
  }

  // All 5 hashes must be strictly identical
  const firstHash = hashes[0];
  assert.ok(firstHash && firstHash.length === 64, 'SHA-256 hash must be 64 hex characters');
  for (let i = 1; i < 5; i++) {
    assert.equal(hashes[i], firstHash, `Hash run ${i} must match first run`);
  }

  // Mutate one value: hash must change
  const mutated = JSON.parse(JSON.stringify(config)) as StructureClassesConfig;
  mutated.hp_classes.hp_heavy_concrete.hp_per_cell = 5001; // changed from 5000
  const mutatedHash = structureHash(mutated);
  assert.notEqual(mutatedHash, firstHash, 'Mutating hp_per_cell must change structureHash');
});

// ---------------------------------------------------------------------------
// 8. Classify: All 6 Construction Systems and Roles Covered
// ---------------------------------------------------------------------------

test('structure/classify: covers all 6 construction systems across all 10 roles', () => {
  for (const sys of CONSTRUCTION_SYSTEMS) {
    for (const role of CONSTRUCTION_ROLES) {
      const result = classifyElement(sys, role);
      assert.ok(result, `classifyElement(${sys}, ${role}) must return a result`);
      assert.ok(isStructureClass(result.structure), `result.structure must be valid StructureClass: ${result.structure}`);
      assert.ok(isStructureMaterial(result.material), `result.material must be valid StructureMaterial: ${result.material}`);
    }
  }

  // Specific ADR-0004 proposals verified
  assert.deepEqual(classifyElement('block', 'core_wall'), { structure: 'load_bearing', material: 'concrete_panel' });
  assert.deepEqual(classifyElement('block', 'room_partition'), { structure: 'partition', material: 'plaster' });
  assert.deepEqual(classifyElement('office', 'perimeter_wall'), { structure: 'load_bearing', material: 'brick' });
  assert.deepEqual(classifyElement('office', 'core_wall'), { structure: 'load_bearing', material: 'brick' });
  assert.deepEqual(classifyElement('shed', 'perimeter_wall'), { structure: 'annex', material: 'wood' });
  assert.deepEqual(classifyElement('hall', 'cladding'), { structure: 'facade', material: 'sheet_metal' });
  assert.deepEqual(classifyElement('warehouse', 'cladding'), { structure: 'facade', material: 'brick' });
  assert.deepEqual(classifyElement('ruin', 'core_wall'), { structure: 'partition', material: 'brick' });
});

// ---------------------------------------------------------------------------
// 9. structure_classes.json Data Contract Integrity
// ---------------------------------------------------------------------------

test('structure_classes.json: all 20 structure.material pairs mapped and HP ordering holds', () => {
  const config = loadClassesConfig();

  // All 4 x 5 = 20 pairs exist
  for (const s of STRUCTURE_CLASSES) {
    for (const m of STRUCTURE_MATERIALS) {
      const key = `${s}.${m}`;
      assert.ok(key in config.classes, `Key '${key}' must exist in structure_classes.json classes`);
      const hpKey = config.classes[key];
      assert.ok(hpKey in config.hp_classes, `hp_class '${hpKey}' must exist in hp_classes`);
      const hpDef = config.hp_classes[hpKey];
      assert.equal(hpDef._placeholder, true, `hp_class '${hpKey}' must be marked _placeholder: true`);
      assert.ok(hpDef.hp_per_cell > 0, `hp_class '${hpKey}' hp_per_cell must be > 0`);
      assert.ok(typeof hpDef.min_breach_class === 'string', 'min_breach_class must be string');
      assert.ok(typeof hpDef.noise_m === 'number', 'noise_m must be number');
      assert.ok(typeof hpDef.rubble === 'string', 'rubble must be string');
    }
  }

  // Order rule: load_bearing.concrete_panel > facade.brick > annex.* > partition.plaster > *.wood
  const hpOf = (s: string, m: string) => config.hp_classes[config.classes[`${s}.${m}`]].hp_per_cell;

  const lbConcrete = hpOf('load_bearing', 'concrete_panel');
  const facBrick = hpOf('facade', 'brick');
  const partPlaster = hpOf('partition', 'plaster');

  assert.ok(lbConcrete > facBrick, `load_bearing.concrete_panel (${lbConcrete}) > facade.brick (${facBrick})`);
  assert.ok(facBrick > hpOf('annex', 'concrete_panel'), `facade.brick (${facBrick}) > annex.concrete_panel (${hpOf('annex', 'concrete_panel')})`);

  // Annex materials (non-wood) > partition.plaster
  for (const m of ['concrete_panel', 'brick', 'sheet_metal', 'plaster'] as const) {
    const annexHp = hpOf('annex', m);
    assert.ok(annexHp > partPlaster, `annex.${m} (${annexHp}) > partition.plaster (${partPlaster})`);
  }

  // partition.plaster > *.wood
  for (const s of STRUCTURE_CLASSES) {
    const woodHp = hpOf(s, 'wood');
    assert.ok(partPlaster > woodHp, `partition.plaster (${partPlaster}) > ${s}.wood (${woodHp})`);
  }
});

// ---------------------------------------------------------------------------
// 8. 099b Nits: Stairs explicit needs & V3 Dangling references
// ---------------------------------------------------------------------------

test('structure/validate: stairs explicit needs validation', () => {
  const stair: StructuredStair = {
    id: 'stair_0',
    supported_by: ['wall_1'],
    needs: 2,
    on_fail: 'blocked_rubble',
  };
  const building: StructuredBuilding = {
    id: 'B_stair_test',
    walls: [
      {
        id: 'wall_1',
        struct: { structure: 'load_bearing', material: 'concrete_panel', hp_class: 'hp_heavy_concrete' },
      },
    ],
    stairs: [stair],
  };
  const failures = validateBuildingStructure(building);
  assert.equal(failures.length, 1);
  assert.equal(failures[0].rule, 'V3_supports_count');
  assert.equal(failures[0].element_id, 'stair_0');
});

test('structure/validate: stairs reject NaN or Infinity needs as V3 failure', () => {
  for (const badNeeds of [NaN, Infinity, -Infinity]) {
    const stair: StructuredStair = {
      id: 'stair_nan',
      supported_by: ['wall_1'],
      needs: badNeeds,
      on_fail: 'blocked_rubble',
    };
    const building: StructuredBuilding = {
      id: 'B_stair_nan_test',
      walls: [
        {
          id: 'wall_1',
          struct: { structure: 'load_bearing', material: 'concrete_panel', hp_class: 'hp_heavy_concrete' },
        },
      ],
      stairs: [stair],
    };
    const failures = validateBuildingStructure(building);
    assert.ok(failures.length >= 1, `Expected failure for needs=${badNeeds}`);
    const countFail = failures.find((f) => f.rule === 'V3_supports_count' && f.element_id === 'stair_nan');
    assert.ok(countFail, `Expected V3_supports_count failure for stair with needs=${badNeeds}`);
    assert.ok(countFail.message.includes('invalid needs value'), `Message should indicate invalid needs: ${countFail.message}`);
  }
});

test('structure/validate: V3 Dangling Reference rule (supports non-existent element)', () => {
  const building: StructuredBuilding = {
    id: 'B_dangling_supports',
    walls: [
      {
        id: 'wall_1',
        struct: {
          structure: 'load_bearing',
          material: 'concrete_panel',
          hp_class: 'hp_heavy_concrete',
          supports: ['non_existent_slab'],
        },
      },
    ],
  };
  const failures = validateBuildingStructure(building);
  assert.equal(failures.length, 1);
  assert.equal(failures[0].rule, 'V3_dangling_reference');
  assert.equal(failures[0].element_id, 'wall_1');
  assert.match(failures[0].message, /non_existent_slab/);
});

test('structure/validate: V3 Dangling Reference rule (supported_by non-existent element)', () => {
  const building: StructuredBuilding = {
    id: 'B_dangling_supported_by',
    slab_panels: [
      {
        id: 'slab_1',
        floor: 1,
        rect: [[0, 0], [500, 500]],
        supported_by: ['ghost_wall'],
        needs: 1,
        on_fail: 'floor_hole',
      },
    ],
  };
  const failures = validateBuildingStructure(building);
  assert.equal(failures.length, 1);
  assert.equal(failures[0].rule, 'V3_dangling_reference');
  assert.equal(failures[0].element_id, 'slab_1');
  assert.match(failures[0].message, /ghost_wall/);
});
