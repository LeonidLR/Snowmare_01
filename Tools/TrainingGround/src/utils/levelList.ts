// Level listing, grouping, and classification for Training Ground.
// Groups: Campaign (from campaign.json), Generated (L_Gen_*), Labs & tests (L_*), Legacy stage_XX (stage_* / level_01_outpost).
// Tests in levelList.test.ts (`npm test`).

export type LevelGroup = 'Campaign' | 'Generated' | 'Labs & tests' | 'Legacy stage_XX';

export interface CampaignEntry {
  id?: string;
  map?: string;
  title?: string;
}

export interface CampaignConfig {
  campaign_id?: string;
  levels?: CampaignEntry[];
}

export interface RawLevelMeta {
  file: string;
  level_id?: string;
  level_name?: string;
  stage_number?: number;
  act?: number;
  biome_id?: string;
  waves_count?: number;
  description?: string;
  horde_after_firefights?: number;
}

export interface LevelItem {
  id: string;
  file: string;
  level_id: string;
  level_name: string;
  group: LevelGroup;
  order: number;
  waves_count: number;
  description: string;
}

export const LEVEL_GROUPS: LevelGroup[] = [
  'Campaign',
  'Generated',
  'Labs & tests',
  'Legacy stage_XX',
];

export const LEVEL_GROUP_LABELS: Record<LevelGroup, string> = {
  'Campaign': 'Кампания (Campaign)',
  'Generated': 'Сгенерированные (Generated)',
  'Labs & tests': 'Лаборатории и тесты (Labs & tests)',
  'Legacy stage_XX': 'Устаревшие бои (Legacy stage_XX)',
};

/** Classifies a level file into one of the four groups. */
export function classifyLevelGroup(file: string, campaignMapNames: string[] = []): LevelGroup {
  const baseName = file.replace(/\.json$/i, '');
  if (campaignMapNames.includes(baseName)) {
    return 'Campaign';
  }
  if (baseName.startsWith('L_Gen_')) {
    return 'Generated';
  }
  if (baseName.startsWith('L_')) {
    return 'Labs & tests';
  }
  return 'Legacy stage_XX';
}

/** Extracts the map leaf names from a campaign.json configuration. */
export function extractCampaignMapLeaves(campaign: CampaignConfig | null | undefined): string[] {
  const leaves: string[] = [];
  if (campaign && Array.isArray(campaign.levels)) {
    for (const lvl of campaign.levels) {
      if (lvl.map) {
        const leaf = lvl.map.split('/').pop() || '';
        if (leaf) leaves.push(leaf);
      }
    }
  }
  return leaves;
}

/** Builds the sorted, grouped level list from campaign config and level metadata. */
export function buildLevelList(campaign: CampaignConfig | null | undefined, rawLevels: RawLevelMeta[]): LevelItem[] {
  const campaignMaps = extractCampaignMapLeaves(campaign);

  const items: LevelItem[] = rawLevels
    .filter((l) => l.file !== 'stage_template.json')
    .map((raw) => {
      const baseName = raw.file.replace(/\.json$/i, '');
      const group = classifyLevelGroup(raw.file, campaignMaps);
      let order = 0;
      if (group === 'Campaign') {
        const idx = campaignMaps.indexOf(baseName);
        order = idx >= 0 ? idx : 999;
      } else if (group === 'Legacy stage_XX') {
        if (typeof raw.stage_number === 'number') {
          order = raw.stage_number;
        } else if (baseName.startsWith('stage_')) {
          const num = parseInt(baseName.replace('stage_', ''), 10);
          order = Number.isFinite(num) ? num : 999;
        } else {
          order = 1000;
        }
      } else {
        order = 0;
      }

      return {
        id: baseName,
        file: raw.file,
        level_id: raw.level_id || baseName,
        level_name: raw.level_name || baseName,
        group,
        order,
        waves_count: raw.waves_count ?? 0,
        description: raw.description || '',
      };
    });

  const groupPriority: Record<LevelGroup, number> = {
    'Campaign': 1,
    'Generated': 2,
    'Labs & tests': 3,
    'Legacy stage_XX': 4,
  };

  return items.sort((a, b) => {
    if (groupPriority[a.group] !== groupPriority[b.group]) {
      return groupPriority[a.group] - groupPriority[b.group];
    }
    if (a.order !== b.order) {
      return a.order - b.order;
    }
    return a.level_name.localeCompare(b.level_name);
  });
}
