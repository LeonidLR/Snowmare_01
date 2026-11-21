// Task 097: the props tab follows the department's current map.
// A map with no props file must show an empty canvas. A late GET for the previous map must not put that map's props back.
// «Сохранить» always names the current map (never map="") and, when the file does not exist yet, creates it (base_revision 0).

import type { LevelPropsFile, PropLayer, PropOverride } from '../levelgen/props/schema.ts';

/** The sentence the props tab shows when the current map has no props file (task 097). */
export function missingPropsHint(mapName: string): string {
  return `У карты ${mapName} нет файла пропсов - добавьте пропс, чтобы создать его`;
}

export type PropsMapMode =
  | { kind: 'load'; propsId: string }
  | { kind: 'empty'; hint: string }
  | { kind: 'unselected' };

/** What the panel should do for this map. `propsId` empty => the map has no props file. */
export function propsMapMode(mapName: string | null | undefined, propsId: string | null | undefined): PropsMapMode {
  const map = (mapName ?? '').trim();
  const id = (propsId ?? '').trim();
  if (!map && !id) return { kind: 'unselected' };
  if (!id) return { kind: 'empty', hint: missingPropsHint(map) };
  return { kind: 'load', propsId: id };
}

/**
 * Id for GET /api/levelgen/props. Empty means "do not request":
 * the server treats `map=` as "list every file", which is not a props document.
 */
export function propsLoadId(mapName: string | null | undefined, propsId: string | null | undefined): string {
  const id = (propsId ?? '').trim();
  if (id) return id;
  return (mapName ?? '').trim();
}

/** A GET that started at `requestGen` is stale once the panel has moved on. */
export function acceptPropsLoad(requestGen: number, currentGen: number): boolean {
  return requestGen === currentGen;
}

export interface PropsCanvasSnapshot {
  mapName: string | null;
  propsId: string | null;
  file: LevelPropsFile | null;
  loadGen: number;
  hint: string | null;
}

/** Map switch: drop the previous file immediately and bump the generation so an in-flight GET cannot restore it. */
export function dropPropsForMap(
  prev: PropsCanvasSnapshot,
  mapName: string | null,
  propsId: string | null,
): PropsCanvasSnapshot {
  const mode = propsMapMode(mapName, propsId);
  return {
    mapName,
    propsId: (propsId ?? '').trim() || null,
    file: null,
    loadGen: prev.loadGen + 1,
    hint: mode.kind === 'empty' ? mode.hint : null,
  };
}

/**
 * Apply a GET only if it is still the active generation and the document belongs to the current map.
 * A file whose `map` is a different map is refused (the stale-canvas bug).
 */
export function applyPropsLoad(
  state: PropsCanvasSnapshot,
  requestGen: number,
  file: LevelPropsFile | null | undefined,
): PropsCanvasSnapshot {
  if (!acceptPropsLoad(requestGen, state.loadGen)) return state;
  if (!file || !Array.isArray(file.layers)) return state;
  if (state.mapName && file.map && file.map !== state.mapName) return state;
  if (propsMapMode(state.mapName, state.propsId).kind === 'unselected') return state;
  return { ...state, file, hint: null };
}

/** Save target. The current map name wins. Never the previous file's map, never "". */
export function saveMapTarget(mapName: string | null | undefined, propsId: string | null | undefined): string {
  const map = (mapName ?? '').trim();
  if (map) return map;
  return (propsId ?? '').trim();
}

/** A new props document for a map that does not have one yet (revision 0 until the server assigns 1). */
export function emptyPropsFile(map: string, layout: string): LevelPropsFile {
  return {
    schema: 'codex.levelgen_props/1',
    map,
    layout: layout.trim() || map,
    layout_hash: '',
    revision: 0,
    frozen: false,
    layers: [],
  };
}

export interface PropsSaveBody {
  base_revision: number;
  layers: PropLayer[];
  overrides?: PropOverride[];
  layout?: string;
  layout_hash?: string;
}

/**
 * The PUT body for «Сохранить». A map with no file sends base_revision 0 (the server creates the file).
 * A document that still belongs to another map is refused: no request is built.
 */
export function propsSaveRequest(args: {
  mapName: string | null;
  propsId: string | null;
  file: LevelPropsFile | null;
  layoutId: string | null;
}): { map: string; body: PropsSaveBody } | { error: string } {
  const map = saveMapTarget(args.mapName, args.propsId);
  if (!map) return { error: 'Не понятно, в какую карту сохранять: карта не выбрана.' };
  if (args.file && args.file.map && args.file.map !== map) {
    return {
      error: `На экране пропсы карты ${args.file.map}. Они не записываются в карту ${map}: холст должен быть пустым, пока у карты нет своего файла.`,
    };
  }
  const mode = propsMapMode(args.mapName, args.propsId);
  if (!args.file) {
    if (mode.kind === 'load') return { error: 'Файл пропсов ещё не загружен.' };
    const created = emptyPropsFile(map, args.layoutId ?? '');
    return {
      map,
      body: {
        base_revision: 0,
        layers: created.layers,
        overrides: [],
        layout: created.layout,
        layout_hash: created.layout_hash,
      },
    };
  }
  return {
    map,
    body: {
      base_revision: args.file.revision,
      layers: args.file.layers,
      overrides: args.file.overrides,
      ...(mode.kind === 'empty' ? { layout: args.file.layout, layout_hash: args.file.layout_hash } : {}),
    },
  };
}
