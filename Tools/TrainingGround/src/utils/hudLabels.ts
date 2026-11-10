// Training Ground: HUD labels schema, groupings, and helpers for codex.hud_labels/1.
// Docs: Step 3 of docs/port/gemini/inbox/010-night-queue.md.
// File: Content/Data/UI/hud_labels.json

import { patchJsonText } from './jsonTextPatch.ts';

export const HUD_LABELS_SCHEMA = 'codex.hud_labels/1';

export interface HudLabelItem {
  show: boolean;
  label: string;
  doc: string;
  [key: string]: unknown;
}

export interface HudLabelsFile {
  schema: string;
  comment?: string;
  labels: Record<string, HudLabelItem>;
  [key: string]: unknown;
}

export type HudGroupId = 'operatives' | 'enemies' | 'world' | 'hud';

export interface HudGroupDef {
  id: HudGroupId;
  title: string;
  description: string;
  keys: readonly string[];
}

export const HUD_GROUPS: readonly HudGroupDef[] = [
  {
    id: 'operatives',
    title: 'Отряд и оперативники',
    description: 'Имена, стойки, метки ранений, баджи стресса и буквы огня над бойцами',
    keys: [
      'operative_name',
      'operative_stance',
      'frostbite_tag',
      'wound_tag',
      'critical_tag',
      'panic_badge',
      'rage_badge',
      'posture_letters',
      'knockdown_badges',
      'suppression_badges',
      'recruit_prompt',
    ],
  },
  {
    id: 'enemies',
    title: 'Враги и мутанты',
    description: 'Имена, здоровье, броня, статусы и метки поиска над противниками',
    keys: [
      'enemy_names',
      'enemy_status',
      'enemy_health',
      'enemy_armor_marker',
      'enemy_alert_markers',
    ],
  },
  {
    id: 'world',
    title: 'Окружение и мир',
    description: 'Подписи объектов, ворот, техники, всплывающий урон и щиты приказов',
    keys: [
      'interactable_labels',
      'gate_labels',
      'vehicle_hull_labels',
      'floating_texts',
      'cover_texts',
      'defense_markers',
    ],
  },
  {
    id: 'hud',
    title: 'Интерфейс и экраны',
    description: 'Шансы попадания, предупреждение об орде, режим боя и панель отряда',
    keys: [
      'hit_chance',
      'horde_warning',
      'combat_mode_badge',
      'squad_status_panel',
    ],
  },
] as const;

/** Validates that data conforms to codex.hud_labels/1. Returns a list of error messages. */
export function validateHudLabels(data: unknown): string[] {
  const errors: string[] = [];
  if (!data || typeof data !== 'object') {
    return ['hud_labels: root must be an object'];
  }
  const root = data as Record<string, unknown>;
  if (root.schema !== HUD_LABELS_SCHEMA) {
    errors.push(`hud_labels: schema must be "${HUD_LABELS_SCHEMA}", got "${String(root.schema)}"`);
  }
  if (!root.labels || typeof root.labels !== 'object' || Array.isArray(root.labels)) {
    errors.push('hud_labels: labels must be an object');
    return errors;
  }
  const labels = root.labels as Record<string, unknown>;
  for (const [key, val] of Object.entries(labels)) {
    if (!val || typeof val !== 'object' || Array.isArray(val)) {
      errors.push(`label "${key}": must be an object`);
      continue;
    }
    const item = val as Record<string, unknown>;
    if (typeof item.show !== 'boolean') {
      errors.push(`label "${key}": show must be a boolean`);
    }
    if (typeof item.label !== 'string' || item.label.length === 0) {
      errors.push(`label "${key}": label must be a non-empty string`);
    }
    if (typeof item.doc !== 'string' || item.doc.length === 0) {
      errors.push(`label "${key}": doc must be a non-empty string`);
    }
  }
  return errors;
}

/** Toggles all switches in a specified group to show (true/false). */
export function setGroupShow(file: HudLabelsFile, groupId: HudGroupId, show: boolean): HudLabelsFile {
  const group = HUD_GROUPS.find((g) => g.id === groupId);
  if (!group) return file;
  const updatedLabels: Record<string, HudLabelItem> = { ...file.labels };
  for (const k of group.keys) {
    if (updatedLabels[k]) {
      updatedLabels[k] = { ...updatedLabels[k], show };
    }
  }
  return { ...file, labels: updatedLabels };
}

/** Toggles all 26 switches to show (true/false). */
export function setAllShow(file: HudLabelsFile, show: boolean): HudLabelsFile {
  const updatedLabels: Record<string, HudLabelItem> = {};
  for (const [k, v] of Object.entries(file.labels)) {
    updatedLabels[k] = { ...v, show };
  }
  return { ...file, labels: updatedLabels };
}

/** Counts on/off states within a group. */
export function getGroupCounts(file: HudLabelsFile, groupId: HudGroupId): { total: number; on: number; off: number } {
  const group = HUD_GROUPS.find((g) => g.id === groupId);
  if (!group || !file.labels) return { total: 0, on: 0, off: 0 };
  let on = 0;
  let total = 0;
  for (const k of group.keys) {
    const item = file.labels[k];
    if (item) {
      total++;
      if (item.show) on++;
    }
  }
  return { total, on, off: total - on };
}

/** Counts on/off states across all labels. */
export function getTotalCounts(file: HudLabelsFile): { total: number; on: number; off: number } {
  if (!file.labels) return { total: 0, on: 0, off: 0 };
  let on = 0;
  let total = 0;
  for (const item of Object.values(file.labels)) {
    total++;
    if (item.show) on++;
  }
  return { total, on, off: total - on };
}

/** Applies updated labels data to the original text, preserving all comments, indentation and unknown keys. */
export function patchHudLabelsText(originalText: string, updated: HudLabelsFile): string {
  return patchJsonText(originalText, updated as unknown as Parameters<typeof patchJsonText>[1]);
}
