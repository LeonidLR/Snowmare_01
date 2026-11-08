// Level listing, grouping, and classification for Training Ground.
// Groups: Campaign (from campaign.json), Generated (L_Gen_*), Labs & tests (L_*), Legacy stage_XX (stage_* / level_01_outpost).
// Two files that share a level_name (stage_01.json and level_01_outpost.json are both "Battle 1: Checkpoint Skirmish") get the file
// name appended in brackets in display_name, so the selector never shows the same entry twice.
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
  /** What the level selector shows: level_name, plus " (<file>)" when another listed file has the same level_name. */
  display_name: string;
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

/**
 * Display names for the selector: level_name as is, or "level_name (file)" for every item whose level_name another item
 * shares (compared trimmed, case-insensitive), so duplicates stay distinguishable without editing the level files.
 */
export function disambiguateNames<T extends { file: string; level_name: string }>(items: T[]): string[] {
  const key = (name: string) => name.trim().toLowerCase();
  const counts = new Map<string, number>();
  for (const item of items) {
    counts.set(key(item.level_name), (counts.get(key(item.level_name)) ?? 0) + 1);
  }
  return items.map((item) => ((counts.get(key(item.level_name)) ?? 0) > 1 ? `${item.level_name} (${item.file})` : item.level_name));
}

/** Builds the sorted, grouped level list from campaign config and level metadata. */
export function buildLevelList(campaign: CampaignConfig | null | undefined, rawLevels: RawLevelMeta[]): LevelItem[] {
  const campaignMaps = extractCampaignMapLeaves(campaign);

  const items: Omit<LevelItem, 'display_name'>[] = rawLevels
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

  const sorted = items.sort((a, b) => {
    if (groupPriority[a.group] !== groupPriority[b.group]) {
      return groupPriority[a.group] - groupPriority[b.group];
    }
    if (a.order !== b.order) {
      return a.order - b.order;
    }
    const byName = a.level_name.localeCompare(b.level_name);
    return byName !== 0 ? byName : a.file.localeCompare(b.file);
  });
  const names = disambiguateNames(sorted);
  return sorted.map((item, index) => ({ ...item, display_name: names[index] }));
}
