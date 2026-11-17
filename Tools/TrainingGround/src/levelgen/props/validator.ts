// Training Ground / level generator: the static props validator - a port of the C++ FPropsValidator
// (Source/CodexTactics/Private/LevelGen/PropsValidator.cpp, docs/design/levelgen/PROPS_LAYERS.md 2.4).
// G003b (2026-11-17): the autogen kept objects the game then dropped at load (7 per expanded map: Depot crates / a barrel kept from
// L_Gen_Depot_1.props.json that now overlap the Depot covers, mines inside a stair keep-out disk or a doorway corridor). The autogen
// now runs this validator and leaves those out, so what the TG shows is what the game spawns. Same checks, same order, same
// integer maths as the C++ (the layout view is the shared exact port).

import { PropsLayoutView } from './layoutView.ts';
import type { PropsKeepOut, PropsResolvedObject } from './placement.ts';
import type { PropObject } from './schema.ts';

export interface PropsValidationIssue {
  objectId: string;
  layerId: string;
  reason: string;
}

export interface PropsValidationResult {
  valid: PropsResolvedObject[];
  dropped: PropsResolvedObject[];
  issues: PropsValidationIssue[];
}

/** FPropsValidator::GetVariantHalfSize: kinds.<variant>.radius_cm, else (max(size x, size y) + 1) / 2, else the prefix defaults. */
export function variantHalfSize(variant: string, balance?: Record<string, unknown>): number {
  const kinds = balance?.kinds as Record<string, Record<string, unknown>> | undefined;
  const k = kinds && typeof kinds === 'object' ? kinds[variant] : undefined;
  if (k && typeof k === 'object') {
    if (typeof k.radius_cm === 'number') return Math.max(0, Math.round(k.radius_cm));
    const size = k.size_cm;
    if (Array.isArray(size) && size.length >= 2 && typeof size[0] === 'number' && typeof size[1] === 'number') {
      return Math.trunc((Math.max(Math.round(size[0]), Math.round(size[1]), 0) + 1) / 2);
    }
  }
  const v = variant.toLowerCase();
  if (v.startsWith('barrel')) return 30;
  if (v === 'crate_l') return 80;
  if (v.startsWith('crate')) return 60;
  if (v.startsWith('turret')) return 50;
  if (v.startsWith('loot_pile')) return 30;
  return 0;
}

/** FPropsValidator::IsInDoorwayCorridor: 90 cm wide (45 + half size), 150 cm deep (+ half size) on both sides; a doorway's own mine passes. */
export function doorwayCorridorBlocking(view: PropsLayoutView, obj: PropObject, halfSize: number): string | null {
  for (const door of view.doorways) {
    if (door.walkLayer !== (obj.walk_layer ?? 'ground')) continue;
    if (obj.door && obj.door === door.id) continue;
    const dx = obj.pos[0] - door.pos[0];
    const dy = obj.pos[1] - door.pos[1];
    const nx = -door.along[1];
    const ny = door.along[0];
    const lateral = Math.abs(dx * door.along[0] + dy * door.along[1]);
    const normal = Math.abs(dx * nx + dy * ny);
    if (lateral <= 45 + Math.max(0, halfSize) && normal <= 150 + Math.max(0, halfSize)) return door.id;
  }
  return null;
}

/** FPropsValidator::IsInLadderOrStairDisk: the 100 cm (+ half size) disk at ladder feet / tops and stair ends. */
export function ladderOrStairBlocking(view: PropsLayoutView, obj: PropObject, halfSize: number): string | null {
  const radius = 100 + Math.max(0, halfSize);
  const shapes = view.buildExclusions({ ladderCm: radius, stairCm: radius });
  const layer = obj.walk_layer ?? 'ground';
  for (const ex of shapes) {
    if (ex.kind !== 'ladder_foot' && ex.kind !== 'ladder_top' && ex.kind !== 'stair') continue;
    if (PropsLayoutView.isExcluded([ex], obj.pos, layer)) return ex.sourceId;
  }
  return null;
}

/** FPropsValidator::ValidateObjects: pass 1 spatial checks, pass 2 attach targets. Never throws. */
export function validateStatic(
  view: PropsLayoutView,
  objects: PropsResolvedObject[],
  keepOuts: PropsKeepOut[],
  balance?: Record<string, unknown>,
): PropsValidationResult {
  const result: PropsValidationResult = { valid: [], dropped: [], issues: [] };
  const provisional: PropsResolvedObject[] = [];
  const provisionalIds = new Set<string>();
  for (const r of objects) {
    const obj = r.object;
    const layer = obj.walk_layer ?? 'ground';
    const half = variantHalfSize(obj.variant, balance);
    let reason = '';
    if (!view.findLayer(layer)) {
      reason = `unknown walk layer '${layer}'`;
    } else if (!view.isWalkable(obj.pos, layer)) {
      reason = `not on its walk layer (unwalkable at [${obj.pos[0]}, ${obj.pos[1]}])`;
    } else if (!obj.attach && view.isBlocked(obj.pos, layer, half)) {
      reason = `blocked by layout geometry at [${obj.pos[0]}, ${obj.pos[1]}]`;
    } else {
      const door = doorwayCorridorBlocking(view, obj, half);
      if (door) {
        reason = `inside 90 cm clear corridor of doorway '${door}'`;
      } else {
        const ladder = ladderOrStairBlocking(view, obj, half);
        if (ladder) {
          reason = `inside 100 cm keep-out disk of ladder/stair '${ladder}'`;
        } else {
          for (const ko of keepOuts) {
            if (ko.layerId && ko.layerId !== r.layerId) continue;
            const radius = ko.radiusCm > 0 ? ko.radiusCm : 120;
            const dx = obj.pos[0] - ko.pos[0];
            const dy = obj.pos[1] - ko.pos[1];
            if (dx * dx + dy * dy < radius * radius) {
              reason = `inside keep-out of deleted object '${ko.id}'`;
              break;
            }
          }
        }
      }
    }
    if (reason) {
      result.dropped.push(r);
      result.issues.push({ objectId: obj.id, layerId: r.layerId, reason });
    } else {
      provisional.push(r);
      provisionalIds.add(obj.id);
    }
  }
  for (const r of provisional) {
    if (r.object.attach && !provisionalIds.has(r.object.attach)) {
      result.dropped.push(r);
      result.issues.push({ objectId: r.object.id, layerId: r.layerId, reason: `attach target '${r.object.attach}' not found or dropped` });
    } else {
      result.valid.push(r);
    }
  }
  return result;
}
