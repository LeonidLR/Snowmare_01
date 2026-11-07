// Tests for the Chokepoint & Kill Zone Analyzer (node --test; `npm test`).

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import type { Block3D } from './chokepoints.ts';
import {
  analyzeChokepoints,
  extractTallBlocks,
  findChokepointCorridors,
  generateChokepointHeatmap,
  scoreAmbushPosition,
} from './chokepoints.ts';
import type { GenLayout } from './schema.ts';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const ROOT = path.resolve(__dirname, '../../../../');
const DEPOT_LAYOUT_PATH = path.join(ROOT, 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json');

test('extractTallBlocks: filters obstacles below 3 m and includes tall buildings and walls', () => {
  const mockLayout: Partial<GenLayout> = {
    name: 'test_layout',
    buildings: [
      {
        id: 'B_tall_1',
        footprint: [[0, 0], [1000, 1000]],
        height: 600,
        floors: 2,
        roof_walkable: true,
        enterable: true,
        doors: [],
        stairs: [],
        roof_holes: [],
        floor_holes: [],
        set_piece: 'facade',
      },
      {
        id: 'B_low',
        footprint: [[2000, 0], [3000, 1000]],
        height: 250, // Below 3m
        floors: 1,
        roof_walkable: false,
        enterable: false,
        doors: [],
        stairs: [],
        roof_holes: [],
        floor_holes: [],
        set_piece: 'facade',
      },
    ],
    walls: [
      {
        id: 'W_tall',
        a: [0, 2000],
        b: [1000, 2000],
        height: 350,
        thickness: 50,
        kind: 'wall',
      },
      {
        id: 'W_standard',
        a: [0, 3000],
        b: [1000, 3000],
        height: 220, // Standard low wall
        thickness: 20,
        kind: 'wall',
      },
    ],
  };

  const blocks = extractTallBlocks(mockLayout as GenLayout);
  assert.equal(blocks.length, 2, 'only B_tall_1 and W_tall should be extracted');
  assert.equal(blocks[0].id, 'B_tall_1');
  assert.equal(blocks[1].id, 'W_tall');
});

test('findChokepointCorridors: detects corridors 1-3 m wide between 3m+ blocks', () => {
  const blocks: Block3D[] = [
    {
      id: 'Block_A',
      min: [1000, 0],
      max: [3000, 1000],
      height: 900,
      roofWalkable: true,
      source: 'building',
    },
    {
      id: 'Block_B',
      min: [1000, 1200], // Gap = 200 cm (2.0 m)
      max: [3000, 2200],
      height: 900,
      roofWalkable: true,
      source: 'building',
    },
    {
      id: 'Block_Far',
      min: [1000, 3000], // Gap = 800 cm (too wide)
      max: [3000, 4000],
      height: 900,
      roofWalkable: true,
      source: 'building',
    },
  ];

  const corridors = findChokepointCorridors(blocks, 100, 300, 150);
  assert.equal(corridors.length, 1, 'only Block_A and Block_B form a 1-3 m corridor');
  const c = corridors[0];
  assert.equal(c.widthCm, 200);
  assert.equal(c.widthM, 2);
  assert.equal(c.lengthCm, 2000); // Overlap X = 3000 - 1000 = 2000 cm (20 m)
  assert.equal(c.lengthM, 20);
  assert.equal(c.category, 'fatal_funnel', 'dual 9m buildings form a fatal funnel');
  assert.ok(c.killZoneRating >= 85, 'high kill zone rating for two 9m buildings with crossfire');
});

test('scoreAmbushPosition: awards high scores for elevated roof perches', () => {
  const corridor = {
    start: [1000, 1100] as [number, number],
    end: [3000, 1100] as [number, number],
    axis: 'X' as const,
  };

  // High roof perch (9m)
  const roofPerch = scoreAmbushPosition([2000, 1000], 900, corridor, true);
  assert.equal(roofPerch.heightScore, 100);
  assert.ok(roofPerch.coverScore >= 80);
  assert.ok(roofPerch.losScore >= 90);
  assert.ok(roofPerch.totalScore >= 85);

  // Ground level perch (0m)
  const groundPerch = scoreAmbushPosition([2000, 1000], 0, corridor, false);
  assert.equal(groundPerch.heightScore, 25);
  assert.ok(groundPerch.totalScore < roofPerch.totalScore);
});

test('generateChokepointHeatmap: produces grid cells covering corridors with valid intensities', () => {
  const blocks: Block3D[] = [
    { id: 'B1', min: [0, 0], max: [1000, 1000], height: 600, roofWalkable: true, source: 'building' },
    { id: 'B2', min: [0, 1200], max: [1000, 2200], height: 600, roofWalkable: true, source: 'building' },
  ];
  const corridors = findChokepointCorridors(blocks, 100, 300, 150);
  const bounds = { min: [-500, -500] as [number, number], max: [2500, 2500] as [number, number] };
  const heatmap = generateChokepointHeatmap(corridors, bounds, 200);

  assert.ok(heatmap.cells.length > 0, 'heatmap should have generated cells');
  for (const cell of heatmap.cells) {
    assert.ok(cell.intensity >= 0 && cell.intensity <= 1.0, 'intensity must be in 0..1');
  }
  assert.ok(heatmap.cells.some((cell) => cell.intensity === 1.0), 'ambush positions should have peak intensity 1.0');
});

test('analyzeChokepoints: correctly analyzes the accepted Depot reference layout', () => {
  assert.ok(fs.existsSync(DEPOT_LAYOUT_PATH), `Depot layout must exist at ${DEPOT_LAYOUT_PATH}`);
  const layout = JSON.parse(fs.readFileSync(DEPOT_LAYOUT_PATH, 'utf8')) as GenLayout;

  const result = analyzeChokepoints(layout);
  assert.equal(result.layoutName, 'gen_ref_01_02_depot_1');
  assert.ok(result.corridors.length >= 2, 'Depot must contain at least 2 distinct corridors');

  // Verify the landmark Hall-Warehouse corridor
  const hallWarehouse = result.corridors.find(
    (c) =>
      (c.leftBlockId === 'B_hall_n' && c.rightBlockId === 'B_warehouse') ||
      (c.leftBlockId === 'B_warehouse' && c.rightBlockId === 'B_hall_n')
  );
  assert.ok(hallWarehouse, 'Corridor between B_hall_n and B_warehouse must be detected');
  assert.equal(hallWarehouse.widthM, 3, 'Corridor width is 3.0 m');
  assert.equal(hallWarehouse.lengthM, 18, 'Corridor length is 18.0 m');
  assert.equal(hallWarehouse.leftHeightCm, 900);
  assert.equal(hallWarehouse.rightHeightCm, 900);
  assert.equal(hallWarehouse.category, 'fatal_funnel');
  assert.ok(hallWarehouse.killZoneRating >= 90);

  // Verify the Ruin-Annex corridor
  const ruinAnnex = result.corridors.find(
    (c) =>
      (c.leftBlockId === 'B_ruin_annex' && c.rightBlockId === 'B_ruin') ||
      (c.leftBlockId === 'B_ruin' && c.rightBlockId === 'B_ruin_annex')
  );
  assert.ok(ruinAnnex, 'Corridor between B_ruin and B_ruin_annex must be detected');
  assert.equal(ruinAnnex.widthM, 2.5, 'Corridor width is 2.5 m');
  assert.equal(ruinAnnex.lengthM, 8, 'Corridor length is 8.0 m');

  // Verify summary metrics
  assert.ok(result.summary.totalCorridors >= 2);
  assert.ok(result.summary.highestKillZoneRating >= 90);
  assert.ok(result.summary.meanWidthM >= 2.0 && result.summary.meanWidthM <= 3.0);
  assert.ok(result.heatmap.cells.length >= 25);
});
