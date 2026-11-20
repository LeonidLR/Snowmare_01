// Training Ground «Пропсы уровней» (T-023): the re-roll of ONE props layer inside a drawn zone (box / polygon / lasso - the same
// ZoneShape the map page's region tools produce, levelgen/regionRegen.ts). Pure (no fs): runs in the browser and in the tests.
//
// How: the layer is re-rolled in a CLONE of the file with the new seed (the placement works on the whole map, so the density and every
// rule - spacing, exclusions, route clearance inputs - stay the generator's own), then only the fresh objects whose position is inside
// the zone are taken; every object outside the zone, every locked / hand / pinned object (locked flag, hand origin, a move / set
// override) and every unlocked object of the layer that stands outside the zone is kept EXACTLY as it was. The unlocked old objects
// inside the zone are replaced. A fresh object closer than the layer's min spacing to a kept one is dropped (no spacing break at the border).

import type { Vec2 } from '../../movement/geometry.ts';
import { pointInPoly } from '../../movement/geometry.ts';
import type { LevelPropsFile, PropObject } from './schema.ts';
import type { PropsLayoutView } from './layoutView.ts';
import { applyOverrides, buildRules, rerollLayer } from './placement.ts';

export interface ZoneLayerResult {
  layerId: string;
  /** Unlocked objects inside the zone that were replaced (removed). */
  removed: number;
  /** Fresh objects now in the layer (inside the zone). */
  added: string[];
  /** Fresh objects the min spacing (vs a kept object) refused. */
  droppedSpacing: number;
  /** Objects of the layer outside the zone (untouched). */
  keptOutside: number;
  /** Locked / hand / pinned objects inside the zone (untouched). */
  keptPinned: number;
  /** Objects of OTHER layers attached to a removed object of this layer (their crate is gone): reported, the validator drops them at load. */
  orphaned: string[];
}

const clone = <T>(v: T): T => JSON.parse(JSON.stringify(v)) as T;

/** The resolved (override-applied) position, pin state of every object of `layerId` by id. */
function resolvedOf(file: LevelPropsFile, layerId: string): Map<string, { pos: Vec2; pinned: boolean }> {
  const m = new Map<string, { pos: Vec2; pinned: boolean }>();
  for (const r of applyOverrides(file).objects) {
    if (r.layerId === layerId) m.set(r.object.id, { pos: r.object.pos, pinned: r.bPinned || r.bHand || r.object.locked === true });
  }
  return m;
}

/** What the re-roll of `layerId` in `zone` would replace / keep (the plan overlay's cyan / green marks): centres in UE cm. */
export function zonePreview(file: LevelPropsFile, layerIds: readonly string[], zone: readonly Vec2[]): { inside: Vec2[]; locked: Vec2[] } {
  const inside: Vec2[] = [];
  const locked: Vec2[] = [];
  for (const layer of file.layers) {
    if (!layerIds.includes(layer.id)) continue;
    const res = resolvedOf(file, layer.id);
    for (const o of layer.objects) {
      const r = res.get(o.id);
      const pos = r?.pos ?? o.pos;
      if (!pointInPoly(pos, zone)) continue;
      if (layer.locked || r?.pinned) locked.push(pos);
      else inside.push(pos);
    }
  }
  return { inside, locked };
}

/**
 * Re-rolls `layerId` of `file` IN PLACE (the caller passes a clone) inside `zone` with `seed`. The layer must pass rerollSupport (the caller
 * checks). Returns what happened; `file` is untouched outside the zone.
 */
export function rerollLayerInZone(
  file: LevelPropsFile,
  balance: Record<string, unknown>,
  view: PropsLayoutView,
  layerId: string,
  seed: number,
  zone: readonly Vec2[],
): ZoneLayerResult {
  const layer = file.layers.find((l) => l.id === layerId);
  if (!layer) throw new Error(`props: no layer '${layerId}'`);
  const before = resolvedOf(file, layerId);
  const originals = layer.objects;

  // kept = everything the zone re-roll must not touch
  const isKept = (o: PropObject): boolean => {
    const r = before.get(o.id);
    if (!r) return true; // an object the overrides hide / delete: never touch
    return r.pinned || !pointInPoly(r.pos, zone);
  };
  const keep = originals.filter(isKept);
  const keepIds = new Set(keep.map((o) => o.id));
  const pinnedIds = new Set(originals.filter((o) => before.get(o.id)?.pinned).map((o) => o.id));

  // the placement on a clone: the whole map with the new seed (kept = locked / pinned / hand objects, like a normal re-roll)
  const tmp = clone(file);
  rerollLayer(tmp, balance, view, layerId, seed); // no gate here: rerollPropsLayers runs it once on the merged file
  const tmpLayer = tmp.layers.find((l) => l.id === layerId)!;

  let spacing = 0;
  try { spacing = buildRules(balance, layer).minSpacingCm; } catch { spacing = 0; }
  const spacingSq = spacing * spacing;
  const posOf = (o: PropObject): Vec2 => before.get(o.id)?.pos ?? o.pos;

  const fresh: PropObject[] = [];
  let droppedSpacing = 0;
  const taken = new Set<string>(keepIds);
  for (const o of tmpLayer.objects) {
    if (pinnedIds.has(o.id)) continue; // the placement's own kept copy of a pinned object
    if (!pointInPoly(o.pos, zone)) continue;
    // spacing against the kept objects of this layer on the same walk layer (an attached object stands on its crate: exempt)
    if (!o.attach && spacingSq > 0) {
      const near = keep.some((k) => {
        if (k.attach || (k.walk_layer ?? 'ground') !== (o.walk_layer ?? 'ground')) return false;
        const kp = posOf(k);
        return (kp[0] - o.pos[0]) ** 2 + (kp[1] - o.pos[1]) ** 2 < spacingSq;
      });
      if (near) { droppedSpacing++; continue; }
    }
    let id = o.id;
    while (taken.has(id)) id = `${id}z`; // a re-used seed could repeat an id that a kept object holds
    taken.add(id);
    fresh.push(id === o.id ? o : { ...o, id });
  }

  const removedObjs = originals.filter((o) => !keepIds.has(o.id));
  layer.objects = [...keep, ...fresh];
  layer.seed = seed >>> 0;
  const warnings: string[] = [];
  if (droppedSpacing > 0) warnings.push(`zone_spacing_dropped ${droppedSpacing}`);
  layer.report = {
    ...(tmpLayer.report ?? {}),
    requested: layer.objects.length,
    placed: fresh.length,
    kept: keep.length,
    short_by: 0,
    warnings,
  };

  // objects of other layers attached to a removed object: reported (their crate is gone)
  const removedIds = new Set(removedObjs.map((o) => o.id).filter((id) => !fresh.some((f) => f.id === id)));
  const orphaned: string[] = [];
  for (const l of file.layers) {
    if (l.id === layerId) continue;
    for (const o of l.objects) if (o.attach && removedIds.has(o.attach)) orphaned.push(o.id);
  }
  return {
    layerId,
    removed: removedObjs.length,
    added: fresh.map((o) => o.id),
    droppedSpacing,
    keptOutside: keep.filter((o) => !pinnedIds.has(o.id)).length,
    keptPinned: keep.filter((o) => pinnedIds.has(o.id) && pointInPoly(posOf(o), zone)).length,
    orphaned,
  };
}
