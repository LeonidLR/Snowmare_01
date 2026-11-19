// Training Ground / level generator (task T-020): the ONE "current map" of the whole «Генератор уровней» department.
// Pure rules shared by the dev server (which lists the maps and persists the choice) and the page (the selector).
// A map is a generated level: its level JSON (Content/Data/LevelJson/<Map>.json, key `levelgen_layout`) and / or its props file
// (Content/Data/LevelGen/props/<Map>.props.json); its layout is the accepted layout (Content/Data/LevelGen/accepted/<layout>.json).

export interface MapEntry {
  /** The map name (the level JSON leaf / props `map`): L_Gen_Depot_1_X6. */
  map: string;
  /** The accepted layout name the map was built from ('' when unknown): gen_ref_01_02_depot_1_x6. */
  layout: string;
  /** The level JSON exists (the weather section writes it). */
  levelJson: boolean;
  /** The props file id (`<id>.props.json`) or '' when the map has no props file. */
  propsId: string;
  /** ISO time of the layout's last «Принять» (accepted/<layout>.meta.json `at`), '' when never accepted. */
  acceptedAt: string;
}

/**
 * The current map: the saved choice when it still exists, else the most recently accepted layout's map, else the first map by name.
 * `null` only when there is no map at all.
 */
export function pickCurrentMap(entries: readonly MapEntry[], saved: string | null | undefined): string | null {
  if (entries.length === 0) return null;
  if (saved && entries.some((e) => e.map === saved)) return saved;
  const recent = [...entries]
    .filter((e) => e.acceptedAt !== '')
    .sort((a, b) => (a.acceptedAt === b.acceptedAt ? a.map.localeCompare(b.map) : a.acceptedAt < b.acceptedAt ? 1 : -1))[0];
  if (recent) return recent.map;
  return [...entries].sort((a, b) => a.map.localeCompare(b.map))[0].map;
}

/** The selector's label: the map name and its layout name. */
export function mapLabel(e: MapEntry): string {
  return e.layout ? `${e.map}  ←  ${e.layout}` : e.map;
}
