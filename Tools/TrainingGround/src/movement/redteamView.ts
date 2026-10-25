// Training Ground / movement: what the UI shows of a red-team regression layout (FORMATION_CONTRACT.md 11) - the layout picker's
// groups and the failure info next to the selected layout. Pure and dependency-free (the browser bundles it).

import type { MenuChoice } from './menu.ts';

/** Layout names the dev server lists for the red team's files (Content/Data/FormationLab/redteam/<round>_<n>.json). */
export const REDTEAM_PREFIX = 'redteam/';

export interface LayoutGroup { label: string; names: string[] }

/** The picker's groups: the lab layouts first, then "Red team" (numeric order: 2_1 before 10_1). */
export function groupLayoutNames(names: readonly string[]): LayoutGroup[] {
  const lab = names.filter((n) => !n.startsWith(REDTEAM_PREFIX));
  const red = names.filter((n) => n.startsWith(REDTEAM_PREFIX)).sort((a, b) => a.localeCompare(b, 'en', { numeric: true }));
  return [{ label: 'Лаборатория', names: lab }, { label: 'Red team', names: red }].filter((g) => g.names.length);
}

export interface RedteamFailureView { check: number; choice: MenuChoice; members: number; route: string; seed: number; t: number | null; what: string }

export interface RedteamView { comment: string; family: string; failures: RedteamFailureView[] }

/** The red-team info of a loaded layout JSON (null for a plain layout): its comment and expected_fail entries. */
export function redteamView(data: unknown): RedteamView | null {
  if (!data || typeof data !== 'object') return null;
  const d = data as { comment?: unknown; redteam?: { family?: unknown }; expected_fail?: unknown };
  if (!Array.isArray(d.expected_fail)) return null;
  const failures = d.expected_fail.filter((e): e is RedteamFailureView =>
    !!e && typeof e === 'object' && typeof e.check === 'number' && typeof e.route === 'string' && typeof e.members === 'number' &&
    typeof e.choice === 'string' && typeof e.seed === 'number');
  return {
    comment: typeof d.comment === 'string' ? d.comment : '',
    family: typeof d.redteam?.family === 'string' ? d.redteam.family : '',
    failures,
  };
}

/** Where the replay should start for a failure: 2 s before it (the start for an untimed one). */
export const seekFor = (f: Pick<RedteamFailureView, 't'>): number => (f.t === null ? 0 : Math.max(0, f.t - 2));
