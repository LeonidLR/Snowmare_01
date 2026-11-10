// Training Ground / level generator: props layers schema (codex.levelgen_props/1).
// Docs: docs/design/levelgen/PROPS_LAYERS.md sections 1.2, 1.4, 2.5, 6.1.
//
// Pure TypeScript types, validator, and stable read / write functions.
// Preserves all unknown root / layer / object keys across round-trips.

import fs from 'node:fs';

export const PROPS_SCHEMA = 'codex.levelgen_props/1';

/** Supported layer kinds in codex.levelgen_props/1. */
export const PROP_LAYER_KINDS = ['barrels', 'crates', 'turrets', 'mines', 'loot'] as const;
export type PropLayerKind = (typeof PROP_LAYER_KINDS)[number];

/** Supported hand override operations. */
export const PROP_OVERRIDE_OPS = ['move', 'delete', 'add', 'set'] as const;
export type PropOverrideOp = (typeof PROP_OVERRIDE_OPS)[number];

/** A single placed or hand-edited prop object. */
export interface PropObject {
  id: string;
  variant: string;
  pos: [number, number]; // integer cm [x, y]
  walk_layer: string; // ground, roof:B_x, floor:B_x:n, cut
  yaw?: number; // integer degrees
  locked?: boolean;
  origin?: 'hand' | 'generated' | string;
  attach?: string; // id of container / crate for mined crates or container loot
  door?: string; // doorway id (e.g. B_shed#0) for doorway mines
  trigger?: string; // tripwire, crate_open, proximity
  loot_table?: string;
  arc?: number; // arc degrees for turrets
  z_offset?: number;
  [key: string]: unknown; // unknown keys preserved
}

/** Override operation: moving an existing object (also pins it). */
export interface MoveOverride {
  op: 'move';
  id: string;
  pos: [number, number];
  walk_layer: string;
  z_offset?: number;
  yaw?: number;
  [key: string]: unknown;
}

/** Override operation: deleting an existing object (hides it and creates keep-out). */
export interface DeleteOverride {
  op: 'delete';
  id: string;
  pos: [number, number];
  [key: string]: unknown;
}

/** Override operation: adding a hand-placed object. */
export interface AddOverride {
  op: 'add';
  object: PropObject;
  [key: string]: unknown;
}

/** Override operation: setting fields on an existing object (also pins it). */
export interface SetOverride {
  op: 'set';
  id: string;
  fields: Record<string, unknown>;
  [key: string]: unknown;
}

export type PropOverride = MoveOverride | DeleteOverride | AddOverride | SetOverride;

/** Placement report for a layer. */
export interface PropLayerReport {
  requested?: number;
  placed?: number;
  kept?: number;
  short_by?: number;
  gate?: 'pass' | 'fail' | string;
  gate_leg?: string;
  rejected?: Record<string, number>;
  warnings?: string[];
  [key: string]: unknown;
}

/** Placement rules structure preserving unknown keys. */
export interface PropLayerRules {
  count?: {
    mode: 'count' | 'density' | 'per_zone' | string;
    value?: number;
    per_100m2?: number;
    max?: number;
    [key: string]: unknown;
  };
  where?: {
    zones?: string[];
    walk_layers?: string[];
    anchors?: string[];
    near?: Array<{ what: string; max_cm: number; weight: number; [key: string]: unknown }>;
    [key: string]: unknown;
  };
  variants?: Record<string, number>;
  min_spacing_cm?: number;
  min_gap_any_cm?: number;
  exclude?: Record<string, unknown>;
  cluster?: {
    min: number;
    max: number;
    ring_cm: [number, number];
    [key: string]: unknown;
  };
  explosive?: {
    share?: number;
    min_gap_cm?: number;
    never_within_cm_of?: Record<string, number>;
    [key: string]: unknown;
  };
  cover_route?: {
    route: string;
    range_cm: [number, number];
    arc_deg: number;
    min_covered_m: number;
    [key: string]: unknown;
  };
  doorways?: {
    mode?: 'percent' | 'count' | string;
    value?: number;
    kinds?: string[];
    mandatory_route?: 'forbid' | 'allow' | string;
    inside_cm?: number;
    trigger?: string;
    [key: string]: unknown;
  };
  crates?: {
    source_layer?: string;
    percent?: number;
    trigger?: string;
    [key: string]: unknown;
  };
  field?: {
    count?: { mode: string; value: number; [key: string]: unknown };
    where?: { zones?: string[]; walk_layers?: string[]; [key: string]: unknown };
    route_cm?: { mandatory?: number; variants?: number; [key: string]: unknown };
    trigger?: string;
    min_spacing_cm?: number;
    [key: string]: unknown;
  };
  budget_value?: number;
  tables?: string[];
  containers?: {
    crates_share?: number;
    source_layer?: string;
    [key: string]: unknown;
  };
  loose?: {
    where?: { anchors?: string[]; [key: string]: unknown };
    prefer_far_from_route_cm?: [number, number];
    roof_table?: string;
    [key: string]: unknown;
  };
  [key: string]: unknown;
}

/** Layer definition. */
export interface PropLayer {
  id: string;
  kind: PropLayerKind;
  enabled: boolean;
  seed: number; // uint32
  locked: boolean;
  preset?: string;
  rules: PropLayerRules;
  rules_hash?: string;
  objects: PropObject[];
  report?: PropLayerReport;
  [key: string]: unknown;
}

/** Master file schema: codex.levelgen_props/1. */
export interface LevelPropsFile {
  schema: typeof PROPS_SCHEMA | string;
  map: string;
  layout: string;
  layout_hash: string;
  revision: number;
  frozen: boolean;
  layers: PropLayer[];
  overrides?: PropOverride[];
  [key: string]: unknown;
}

function isInteger(val: unknown): val is number {
  return typeof val === 'number' && Number.isInteger(val);
}

function isValidPos(pos: unknown): pos is [number, number] {
  return Array.isArray(pos) && pos.length === 2 && isInteger(pos[0]) && isInteger(pos[1]);
}

/** Validates arbitrary data against codex.levelgen_props/1. Returns an array of error messages (empty if valid). */
export function validateProps(data: unknown): string[] {
  if (!data || typeof data !== 'object' || Array.isArray(data)) {
    return ['expected a JSON object'];
  }
  const doc = data as Record<string, unknown>;
  const errs: string[] = [];

  if (doc.schema !== PROPS_SCHEMA) {
    errs.push(`schema: must be "${PROPS_SCHEMA}" (got "${String(doc.schema)}")`);
  }
  if (typeof doc.map !== 'string' || !doc.map.trim()) {
    errs.push('map: expected a non-empty string');
  }
  if (typeof doc.layout !== 'string' || !doc.layout.trim()) {
    errs.push('layout: expected a non-empty string');
  }
  if (typeof doc.layout_hash !== 'string') {
    errs.push('layout_hash: expected a string');
  }
  if (!isInteger(doc.revision) || (doc.revision as number) < 0) {
    errs.push('revision: expected a non-negative integer');
  }
  if (typeof doc.frozen !== 'boolean') {
    errs.push('frozen: expected a boolean');
  }

  const allObjectIds = new Set<string>();

  if (!Array.isArray(doc.layers)) {
    errs.push('layers: expected an array');
  } else {
    const layerIds = new Set<string>();
    for (let i = 0; i < doc.layers.length; i++) {
      const layer = doc.layers[i];
      if (!layer || typeof layer !== 'object' || Array.isArray(layer)) {
        errs.push(`layers[${i}]: expected an object`);
        continue;
      }
      const l = layer as Record<string, unknown>;
      if (typeof l.id !== 'string' || !l.id.trim()) {
        errs.push(`layers[${i}]: id must be a non-empty string`);
      } else if (layerIds.has(l.id)) {
        errs.push(`layers: duplicate layer id "${l.id}"`);
      } else {
        layerIds.add(l.id);
      }

      if (!PROP_LAYER_KINDS.includes(l.kind as PropLayerKind)) {
        errs.push(`layer "${String(l.id ?? i)}": bad kind "${String(l.kind)}", expected one of: ${PROP_LAYER_KINDS.join(', ')}`);
      }
      if (typeof l.enabled !== 'boolean') {
        errs.push(`layer "${String(l.id ?? i)}": enabled must be a boolean`);
      }
      if (!isInteger(l.seed) || (l.seed as number) < 0 || (l.seed as number) > 0xffffffff) {
        errs.push(`layer "${String(l.id ?? i)}": missing or invalid seed (expected uint32)`);
      }
      if (typeof l.locked !== 'boolean') {
        errs.push(`layer "${String(l.id ?? i)}": locked must be a boolean`);
      }
      if (!l.rules || typeof l.rules !== 'object' || Array.isArray(l.rules)) {
        errs.push(`layer "${String(l.id ?? i)}": rules must be an object`);
      }

      if (!Array.isArray(l.objects)) {
        errs.push(`layer "${String(l.id ?? i)}": objects must be an array`);
      } else {
        for (let j = 0; j < l.objects.length; j++) {
          const obj = l.objects[j];
          if (!obj || typeof obj !== 'object' || Array.isArray(obj)) {
            errs.push(`layer "${String(l.id ?? i)}" object[${j}]: expected an object`);
            continue;
          }
          const o = obj as Record<string, unknown>;
          if (typeof o.id !== 'string' || !o.id.trim()) {
            errs.push(`layer "${String(l.id ?? i)}" object[${j}]: id must be a non-empty string`);
          } else if (allObjectIds.has(o.id)) {
            errs.push(`duplicate object id "${o.id}"`);
          } else {
            allObjectIds.add(o.id);
          }

          if (typeof o.variant !== 'string' || !o.variant.trim()) {
            errs.push(`layer "${String(l.id ?? i)}" object "${String(o.id ?? j)}": variant must be a non-empty string`);
          }
          if (!isValidPos(o.pos)) {
            errs.push(`layer "${String(l.id ?? i)}" object "${String(o.id ?? j)}": pos must be [x, y] in integer cm`);
          }
          if (typeof o.walk_layer !== 'string' || !o.walk_layer.trim()) {
            errs.push(`layer "${String(l.id ?? i)}" object "${String(o.id ?? j)}": walk_layer must be a non-empty string`);
          }
          if (o.yaw !== undefined && !isInteger(o.yaw)) {
            errs.push(`layer "${String(l.id ?? i)}" object "${String(o.id ?? j)}": yaw must be an integer degree`);
          }
          if (o.locked !== undefined && typeof o.locked !== 'boolean') {
            errs.push(`layer "${String(l.id ?? i)}" object "${String(o.id ?? j)}": locked must be a boolean`);
          }
        }
      }
    }
  }

  if (doc.overrides !== undefined) {
    if (!Array.isArray(doc.overrides)) {
      errs.push('overrides: expected an array');
    } else {
      for (let i = 0; i < doc.overrides.length; i++) {
        const ov = doc.overrides[i];
        if (!ov || typeof ov !== 'object' || Array.isArray(ov)) {
          errs.push(`overrides[${i}]: expected an object`);
          continue;
        }
        const o = ov as Record<string, unknown>;
        if (!PROP_OVERRIDE_OPS.includes(o.op as PropOverrideOp)) {
          errs.push(`override[${i}]: bad op "${String(o.op)}", expected one of: ${PROP_OVERRIDE_OPS.join(', ')}`);
          continue;
        }
        if (o.op === 'move') {
          if (typeof o.id !== 'string' || !o.id.trim()) errs.push(`override move[${i}]: id must be a string`);
          if (!isValidPos(o.pos)) {
            errs.push(`override move[${i}]: pos must be [x, y] in integer cm`);
          }
          if (typeof o.walk_layer !== 'string' || !o.walk_layer.trim()) errs.push(`override move[${i}]: walk_layer must be a string`);
          if (o.yaw !== undefined && !isInteger(o.yaw)) errs.push(`override move[${i}]: yaw must be an integer degree`);
        } else if (o.op === 'delete') {
          if (typeof o.id !== 'string' || !o.id.trim()) errs.push(`override delete[${i}]: id must be a string`);
          if (!isValidPos(o.pos)) {
            errs.push(`override delete[${i}]: pos must be [x, y] in integer cm`);
          }
        } else if (o.op === 'add') {
          if (!o.object || typeof o.object !== 'object' || Array.isArray(o.object)) {
            errs.push(`override add[${i}]: object must be an object`);
          } else {
            const addObj = o.object as Record<string, unknown>;
            if (typeof addObj.id !== 'string' || !addObj.id.trim()) {
              errs.push(`override add[${i}]: object.id must be a string`);
            } else if (allObjectIds.has(addObj.id)) {
              errs.push(`duplicate object id "${addObj.id}"`);
            } else {
              allObjectIds.add(addObj.id);
            }
            if (typeof addObj.variant !== 'string' || !addObj.variant.trim()) errs.push(`override add[${i}]: object.variant must be a string`);
            if (!isValidPos(addObj.pos)) {
              errs.push(`override add[${i}]: object.pos must be [x, y] in integer cm`);
            }
            if (typeof addObj.walk_layer !== 'string' || !addObj.walk_layer.trim()) errs.push(`override add[${i}]: object.walk_layer must be a string`);
            if (addObj.yaw !== undefined && !isInteger(addObj.yaw)) errs.push(`override add[${i}]: object.yaw must be an integer degree`);
          }
        } else if (o.op === 'set') {
          if (typeof o.id !== 'string' || !o.id.trim()) errs.push(`override set[${i}]: id must be a string`);
          if (!o.fields || typeof o.fields !== 'object' || Array.isArray(o.fields)) {
            errs.push(`override set[${i}]: fields must be an object`);
          } else {
            const f = o.fields as Record<string, unknown>;
            if ('id' in f) errs.push(`override set[${i}]: fields must not modify id`);
            if ('pos' in f) errs.push(`override set[${i}]: fields must not modify pos (use move)`);
            if ('walk_layer' in f) errs.push(`override set[${i}]: fields must not modify walk_layer (use move)`);
          }
        }
      }
    }
  }

  return errs;
}

export interface FormatPropsOptions {
  crlf?: boolean;
  indent?: string;
  skipValidation?: boolean;
}

/**
 * Formats a value as compact JSON string.
 * Keeps insertion order of object keys (never sorts nested keys, as variants order drives rolls).
 * Skips keys with undefined values. Escapes keys via JSON.stringify.
 */
function inlineJson(val: unknown): string {
  if (val === undefined) {
    return 'null';
  }
  if (Array.isArray(val)) {
    return '[' + val.map((x) => (x === undefined ? 'null' : inlineJson(x))).join(', ') + ']';
  }
  if (val !== null && typeof val === 'object') {
    const obj = val as Record<string, unknown>;
    const items: string[] = [];
    for (const k of Object.keys(obj)) {
      if (obj[k] !== undefined) {
        items.push(`${JSON.stringify(k)}: ${inlineJson(obj[k])}`);
      }
    }
    return '{ ' + items.join(', ') + ' }';
  }
  return JSON.stringify(val);
}

const OBJ_KNOWN_ORDER: readonly string[] = [
  'id', 'origin', 'variant', 'door', 'attach', 'pos', 'walk_layer', 'yaw', 'arc', 'trigger', 'loot_table', 'locked'
];

function inlineObjectJson(obj: PropObject): string {
  if (!obj || typeof obj !== 'object' || Array.isArray(obj)) {
    return inlineJson(obj);
  }
  const o = obj as Record<string, unknown>;
  const known = OBJ_KNOWN_ORDER.filter((k) => k in o && o[k] !== undefined);
  const unknown = Object.keys(o).filter((k) => !OBJ_KNOWN_ORDER.includes(k) && o[k] !== undefined);
  const keys = [...known, ...unknown];
  const items = keys.map((k) => `${JSON.stringify(k)}: ${inlineJson(o[k])}`);
  return '{ ' + items.join(', ') + ' }';
}

const OV_KNOWN_ORDER: readonly string[] = [
  'op', 'id', 'object', 'fields', 'pos', 'walk_layer', 'z_offset', 'yaw'
];

function inlineOverrideJson(ov: PropOverride): string {
  if (!ov || typeof ov !== 'object' || Array.isArray(ov)) {
    return inlineJson(ov);
  }
  const o = ov as Record<string, unknown>;
  const known = OV_KNOWN_ORDER.filter((k) => k in o && o[k] !== undefined);
  const unknown = Object.keys(o).filter((k) => !OV_KNOWN_ORDER.includes(k) && o[k] !== undefined);
  const keys = [...known, ...unknown];
  const items = keys.map((k) => {
    if (k === 'object') {
      return `${JSON.stringify(k)}: ${o.object && typeof o.object === 'object' && !Array.isArray(o.object) ? inlineObjectJson(o.object as PropObject) : inlineJson(o.object)}`;
    }
    if (k === 'fields') {
      return `${JSON.stringify(k)}: ${inlineJson(o.fields)}`;
    }
    return `${JSON.stringify(k)}: ${inlineJson(o[k])}`;
  });
  return '{ ' + items.join(', ') + ' }';
}

const ROOT_KNOWN_ORDER: readonly string[] = [
  'schema', 'map', 'layout', 'layout_hash', 'revision', 'frozen'
];

const LAYER_KNOWN_ORDER: readonly string[] = [
  'id', 'kind', 'enabled', 'seed', 'locked', 'preset', 'rules', 'rules_hash'
];

/**
 * Formats a LevelPropsFile into stable, canonically ordered JSON string.
 * - Orders known top-level and layer keys.
 * - Preserves exact insertion order of nested keys (rules, variants, etc.) without reordering.
 * - Preserves all unknown keys.
 * - Properly handles empty overrides array (`overrides: []`).
 * - Validates document before serialization unless skipValidation is true.
 */
export function formatPropsJson(doc: LevelPropsFile, options: FormatPropsOptions = {}): string {
  if (!options.skipValidation) {
    const errs = validateProps(doc);
    if (errs.length > 0) {
      throw new Error(`Cannot format invalid props file: ${errs.join('; ')}`);
    }
  }

  const crlf = options.crlf ?? false;
  const nl = crlf ? '\r\n' : '\n';
  const tab = options.indent ?? '\t';
  const d = doc as Record<string, unknown>;

  const rootItems: string[] = [];

  const rootKnown = ROOT_KNOWN_ORDER.filter((k) => k in d && d[k] !== undefined);
  const rootCustom = Object.keys(d).filter((k) => !ROOT_KNOWN_ORDER.includes(k) && k !== 'layers' && k !== 'overrides' && d[k] !== undefined);
  const topKeys = [...rootKnown, ...rootCustom];

  for (const k of topKeys) {
    rootItems.push(`${tab}${JSON.stringify(k)}: ${inlineJson(d[k])}`);
  }

  if (Array.isArray(doc.layers)) {
    const layerBlockLines: string[] = [`${tab}"layers": [`];
    const layerBlocks: string[] = [];

    for (let li = 0; li < doc.layers.length; li++) {
      const layer = doc.layers[li];
      const l = layer as Record<string, unknown>;
      const layerFieldLines: string[] = [];

      const layerKnown = LAYER_KNOWN_ORDER.filter((k) => k in l && l[k] !== undefined);
      const layerCustom = Object.keys(l).filter((k) => !LAYER_KNOWN_ORDER.includes(k) && k !== 'objects' && k !== 'report' && l[k] !== undefined);

      for (const k of [...layerKnown, ...layerCustom]) {
        if (k === 'rules') {
          const rulesObj = (layer.rules ?? {}) as Record<string, unknown>;
          const ruleKeys = Object.keys(rulesObj).filter((rk) => rulesObj[rk] !== undefined);
          if (ruleKeys.length === 0) {
            layerFieldLines.push(`${tab}${tab}${tab}${JSON.stringify(k)}: {}`);
          } else {
            const ruleLines: string[] = [`${tab}${tab}${tab}${JSON.stringify(k)}: {`];
            const ruleItems = ruleKeys.map((rk) => `${tab}${tab}${tab}${tab}${JSON.stringify(rk)}: ${inlineJson(rulesObj[rk])}`);
            ruleLines.push(ruleItems.join(',' + nl));
            ruleLines.push(`${tab}${tab}${tab}}`);
            layerFieldLines.push(ruleLines.join(nl));
          }
        } else {
          layerFieldLines.push(`${tab}${tab}${tab}${JSON.stringify(k)}: ${inlineJson(l[k])}`);
        }
      }

      if (Array.isArray(layer.objects)) {
        if (layer.objects.length === 0) {
          layerFieldLines.push(`${tab}${tab}${tab}"objects": []`);
        } else {
          const objLines: string[] = [`${tab}${tab}${tab}"objects": [`];
          const objItems = layer.objects.map((obj) => `${tab}${tab}${tab}${tab}${inlineObjectJson(obj)}`);
          objLines.push(objItems.join(',' + nl));
          objLines.push(`${tab}${tab}${tab}]`);
          layerFieldLines.push(objLines.join(nl));
        }
      }

      if ('report' in l && l.report !== undefined) {
        layerFieldLines.push(`${tab}${tab}${tab}"report": ${inlineJson(l.report)}`);
      }

      layerBlocks.push(`${tab}${tab}{${nl}${layerFieldLines.join(',' + nl)}${nl}${tab}${tab}}`);
    }

    layerBlockLines.push(layerBlocks.join(',' + nl));
    layerBlockLines.push(`${tab}]`);
    rootItems.push(layerBlockLines.join(nl));
  }

  // Preserve overrides array even if empty (overrides: [])
  if (doc.overrides !== undefined && Array.isArray(doc.overrides)) {
    if (doc.overrides.length === 0) {
      rootItems.push(`${tab}"overrides": []`);
    } else {
      const ovLines: string[] = [`${tab}"overrides": [`];
      const ovItems = doc.overrides.map((ov) => `${tab}${tab}${inlineOverrideJson(ov)}`);
      ovLines.push(ovItems.join(',' + nl));
      ovLines.push(`${tab}]`);
      rootItems.push(ovLines.join(nl));
    }
  }

  return `{${nl}${rootItems.join(',' + nl)}${nl}}${nl}`;
}

/** Parses JSON text and validates against LevelPropsFile schema. Preserves all unknown keys. */
export function parsePropsJson(text: string): LevelPropsFile {
  const data = JSON.parse(text);
  const errors = validateProps(data);
  if (errors.length > 0) {
    throw new Error(`Invalid codex.levelgen_props/1: ${errors.join('; ')}`);
  }
  return data as LevelPropsFile;
}

// Aliases matching different naming preferences
export const readPropsJson = parsePropsJson;
export const writePropsJson = formatPropsJson;

/** Reads a props file from disk, validating content and schema. */
export function readPropsFile(filePath: string): LevelPropsFile {
  const text = fs.readFileSync(filePath, 'utf8');
  return parsePropsJson(text);
}

/** Writes a props file to disk with stable key order and validated data. */
export function writePropsFile(filePath: string, props: LevelPropsFile, options: FormatPropsOptions = {}): void {
  const text = formatPropsJson(props, options);
  fs.writeFileSync(filePath, text, 'utf8');
}
