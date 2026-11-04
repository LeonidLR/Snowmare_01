// Level list building and classification tests (`npm test`).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  buildLevelList, classifyLevelGroup, extractCampaignMapLeaves,
  type CampaignConfig, type RawLevelMeta,
} from './levelList.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const CAMPAIGN_FILE = path.join(ROOT, 'Content/Data/Campaign/campaign.json');
const LEVELS_DIR = path.join(ROOT, 'Content/Data/LevelJson');

test('classifyLevelGroup: correctly classifies all 4 categories', () => {
  const campaignMaps = ['L_Demo01_A_FrozenLake', 'L_Demo01_B_FrozenFactory'];
  
  assert.equal(classifyLevelGroup('L_Demo01_A_FrozenLake.json', campaignMaps), 'Campaign');
  assert.equal(classifyLevelGroup('L_Demo01_B_FrozenFactory.json', campaignMaps), 'Campaign');
  
  assert.equal(classifyLevelGroup('L_Gen_Depot_1.json', campaignMaps), 'Generated');
  assert.equal(classifyLevelGroup('L_Gen_Outpost_2.json', campaignMaps), 'Generated');
  
  assert.equal(classifyLevelGroup('L_FormationLab.json', campaignMaps), 'Labs & tests');
  assert.equal(classifyLevelGroup('L_FormationLabCombat.json', campaignMaps), 'Labs & tests');
  assert.equal(classifyLevelGroup('L_PmcArena.json', campaignMaps), 'Labs & tests');
  assert.equal(classifyLevelGroup('L_WeatherRainTest.json', campaignMaps), 'Labs & tests');
  
  assert.equal(classifyLevelGroup('stage_01.json', campaignMaps), 'Legacy stage_XX');
  assert.equal(classifyLevelGroup('stage_12.json', campaignMaps), 'Legacy stage_XX');
  assert.equal(classifyLevelGroup('level_01_outpost.json', campaignMaps), 'Legacy stage_XX');
});

test('extractCampaignMapLeaves: extracts map leaf names from campaign config', () => {
  const campaign: CampaignConfig = {
    campaign_id: 'demo01',
    levels: [
      { id: '0.0', map: '/Game/Maps/L_Demo01_A_FrozenLake', title: '0.0 Frozen Dam' },
      { id: '0.1', map: '/Game/Maps/L_Demo01_B_FrozenFactory', title: '0.1 Frozen Factory' },
    ],
  };
  const leaves = extractCampaignMapLeaves(campaign);
  assert.deepEqual(leaves, ['L_Demo01_A_FrozenLake', 'L_Demo01_B_FrozenFactory']);
});

test('buildLevelList: sorts by group priority, campaign order, and drops template', () => {
  const campaign: CampaignConfig = {
    campaign_id: 'demo01',
    levels: [
      { id: '0.0', map: '/Game/Maps/L_Demo01_A_FrozenLake', title: '0.0 Frozen Dam' },
      { id: '0.1', map: '/Game/Maps/L_Demo01_B_FrozenFactory', title: '0.1 Frozen Factory' },
    ],
  };

  const rawLevels: RawLevelMeta[] = [
    { file: 'stage_template.json', level_id: 'template', level_name: 'Template' },
    { file: 'stage_02.json', level_id: 'stage_02', level_name: 'Battle 2', stage_number: 2 },
    { file: 'stage_01.json', level_id: 'stage_01', level_name: 'Battle 1', stage_number: 1 },
    { file: 'L_FormationLab.json', level_id: 'L_FormationLab', level_name: 'Formation Lab' },
    { file: 'L_Gen_Depot_1.json', level_id: 'L_Gen_Depot_1', level_name: '0.1 Depot' },
    { file: 'L_Demo01_B_FrozenFactory.json', level_id: 'L_Demo01_B_FrozenFactory', level_name: '0.1 Frozen Factory' },
    { file: 'L_Demo01_A_FrozenLake.json', level_id: 'L_Demo01_A_FrozenLake', level_name: '0.0 Frozen Dam' },
  ];

  const items = buildLevelList(campaign, rawLevels);

  // stage_template.json should be dropped
  assert.equal(items.some((i) => i.file === 'stage_template.json'), false);
  assert.equal(items.length, 6);

  // Group 1: Campaign, sorted by campaign order
  assert.equal(items[0].group, 'Campaign');
  assert.equal(items[0].id, 'L_Demo01_A_FrozenLake');
  assert.equal(items[0].level_name, '0.0 Frozen Dam');

  assert.equal(items[1].group, 'Campaign');
  assert.equal(items[1].id, 'L_Demo01_B_FrozenFactory');
  assert.equal(items[1].level_name, '0.1 Frozen Factory');

  // Group 2: Generated
  assert.equal(items[2].group, 'Generated');
  assert.equal(items[2].id, 'L_Gen_Depot_1');
  assert.equal(items[2].level_name, '0.1 Depot');

  // Group 3: Labs & tests
  assert.equal(items[3].group, 'Labs & tests');
  assert.equal(items[3].id, 'L_FormationLab');
  assert.equal(items[3].level_name, 'Formation Lab');

  // Group 4: Legacy stage_XX, ordered by stage_number
  assert.equal(items[4].group, 'Legacy stage_XX');
  assert.equal(items[4].id, 'stage_01');
  assert.equal(items[5].group, 'Legacy stage_XX');
  assert.equal(items[5].id, 'stage_02');
});

test('Real repo data verification: campaign.json and LevelJson/*.json produce valid level list', () => {
  assert.ok(fs.existsSync(CAMPAIGN_FILE), 'campaign.json must exist');
  assert.ok(fs.existsSync(LEVELS_DIR), 'LevelJson dir must exist');

  const campaign = JSON.parse(fs.readFileSync(CAMPAIGN_FILE, 'utf-8'));
  const files = fs.readdirSync(LEVELS_DIR).filter((f) => f.endsWith('.json') && f !== 'stage_template.json');

  const rawLevels: RawLevelMeta[] = files.map((file) => {
    const content = JSON.parse(fs.readFileSync(path.join(LEVELS_DIR, file), 'utf-8'));
    return {
      file,
      level_id: content.level_id || file.replace('.json', ''),
      level_name: content.level_name || file,
      stage_number: content.stage_number,
      act: content.act,
      biome_id: content.biome_id,
      waves_count: content.waves ? content.waves.length : 0,
      description: content.description || '',
      horde_after_firefights: content.horde_after_firefights,
    };
  });

  const list = buildLevelList(campaign, rawLevels);
  assert.ok(list.length >= 20, `expected at least 20 levels, got ${list.length}`);

  // Check L_Gen_Depot_1.json has level_name: "0.1 Depot"
  const depot = list.find((i) => i.id === 'L_Gen_Depot_1');
  assert.ok(depot, 'L_Gen_Depot_1 must be present in level list');
  assert.equal(depot.level_name, '0.1 Depot');
  assert.equal(depot.group, 'Generated');

  // Check Campaign has Frozen Dam and Frozen Factory
  const campaignItems = list.filter((i) => i.group === 'Campaign');
  assert.equal(campaignItems.length, 2);
  assert.equal(campaignItems[0].id, 'L_Demo01_A_FrozenLake');
  assert.equal(campaignItems[0].level_name, '0.0 Frozen Dam');
  assert.equal(campaignItems[1].id, 'L_Demo01_B_FrozenFactory');
  assert.equal(campaignItems[1].level_name, '0.1 Frozen Factory');

  // Check Labs & tests contains Formation Lab
  const lab = list.find((i) => i.id === 'L_FormationLab');
  assert.ok(lab, 'L_FormationLab must be in Labs & tests');
  assert.equal(lab.group, 'Labs & tests');
});
