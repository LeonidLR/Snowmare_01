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
  if (typeof doc.revision !== 'number' || !Number.isInteger(doc.revision) || doc.revision < 0) {
    errs.push('revision: expected a non-negative integer');
  }
  if (typeof doc.frozen !== 'boolean') {
    errs.push('frozen: expected a boolean');
  }

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
      if (typeof l.seed !== 'number' || !Number.isInteger(l.seed) || l.seed < 0 || l.seed > 0xffffffff) {
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
          }
          if (typeof o.variant !== 'string' || !o.variant.trim()) {
            errs.push(`layer "${String(l.id ?? i)}" object "${String(o.id ?? j)}": variant must be a non-empty string`);
          }
          if (!Array.isArray(o.pos) || o.pos.length !== 2 || typeof o.pos[0] !== 'number' || typeof o.pos[1] !== 'number') {
            errs.push(`layer "${String(l.id ?? i)}" object "${String(o.id ?? j)}": pos must be [x, y] in cm`);
          }
          if (typeof o.walk_layer !== 'string' || !o.walk_layer.trim()) {
            errs.push(`layer "${String(l.id ?? i)}" object "${String(o.id ?? j)}": walk_layer must be a non-empty string`);
          }
          if (o.yaw !== undefined && typeof o.yaw !== 'number') {
            errs.push(`layer "${String(l.id ?? i)}" object "${String(o.id ?? j)}": yaw must be a number`);
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
          if (!Array.isArray(o.pos) || o.pos.length !== 2 || typeof o.pos[0] !== 'number' || typeof o.pos[1] !== 'number') {
            errs.push(`override move[${i}]: pos must be [x, y]`);
          }
          if (typeof o.walk_layer !== 'string' || !o.walk_layer.trim()) errs.push(`override move[${i}]: walk_layer must be a string`);
        } else if (o.op === 'delete') {
          if (typeof o.id !== 'string' || !o.id.trim()) errs.push(`override delete[${i}]: id must be a string`);
          if (!Array.isArray(o.pos) || o.pos.length !== 2 || typeof o.pos[0] !== 'number' || typeof o.pos[1] !== 'number') {
            errs.push(`override delete[${i}]: pos must be [x, y]`);
          }
        } else if (o.op === 'add') {
          if (!o.object || typeof o.object !== 'object' || Array.isArray(o.object)) {
            errs.push(`override add[${i}]: object must be an object`);
          } else {
            const addObj = o.object as Record<string, unknown>;
            if (typeof addObj.id !== 'string' || !addObj.id.trim()) errs.push(`override add[${i}]: object.id must be a string`);
            if (typeof addObj.variant !== 'string' || !addObj.variant.trim()) errs.push(`override add[${i}]: object.variant must be a string`);
            if (!Array.isArray(addObj.pos) || addObj.pos.length !== 2 || typeof addObj.pos[0] !== 'number' || typeof addObj.pos[1] !== 'number') {
              errs.push(`override add[${i}]: object.pos must be [x, y]`);
            }
            if (typeof addObj.walk_layer !== 'string' || !addObj.walk_layer.trim()) errs.push(`override add[${i}]: object.walk_layer must be a string`);
          }
        } else if (o.op === 'set') {
          if (typeof o.id !== 'string' || !o.id.trim()) errs.push(`override set[${i}]: id must be a string`);
          if (!o.fields || typeof o.fields !== 'object' || Array.isArray(o.fields)) {
            errs.push(`override set[${i}]: fields must be an object`);
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
}

function inlineJson(val: unknown): string {
  if (Array.isArray(val)) {
    return '[' + val.map(inlineJson).join(', ') + ']';
  }
  if (val !== null && typeof val === 'object') {
    const obj = val as Record<string, unknown>;
    const keys = Object.keys(obj).sort();
    return '{ ' + keys.map((k) => `"${k}": ${inlineJson(obj[k])}`).join(', ') + ' }';
  }
  return JSON.stringify(val);
}

const OBJ_KNOWN_ORDER: readonly string[] = [
  'id', 'origin', 'variant', 'door', 'attach', 'pos', 'walk_layer', 'yaw', 'arc', 'trigger', 'loot_table', 'locked'
];

function inlineObjectJson(obj: PropObject): string {
  const o = obj as Record<string, unknown>;
  const known = OBJ_KNOWN_ORDER.filter((k) => k in o);
  const unknown = Object.keys(o).filter((k) => !OBJ_KNOWN_ORDER.includes(k)).sort();
  const keys = [...known, ...unknown];
  const items = keys.map((k) => `"${k}": ${inlineJson(o[k])}`);
  return '{ ' + items.join(', ') + ' }';
}

const OV_KNOWN_ORDER: readonly string[] = [
  'op', 'id', 'object', 'fields', 'pos', 'walk_layer', 'z_offset', 'yaw'
];

function inlineOverrideJson(ov: PropOverride): string {
  const o = ov as Record<string, unknown>;
  const known = OV_KNOWN_ORDER.filter((k) => k in o);
  const unknown = Object.keys(o).filter((k) => !OV_KNOWN_ORDER.includes(k)).sort();
  const keys = [...known, ...unknown];
  const items = keys.map((k) => {
    if (k === 'object') {
      return `"object": ${inlineObjectJson(o.object as PropObject)}`;
    }
    if (k === 'fields') {
      return `"fields": ${inlineJson(o.fields)}`;
    }
    return `"${k}": ${inlineJson(o[k])}`;
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
 * Keeps all unknown root / layer / object keys.
 */
export function formatPropsJson(doc: LevelPropsFile, options: FormatPropsOptions = {}): string {
  const crlf = options.crlf ?? false;
  const nl = crlf ? '\r\n' : '\n';
  const tab = options.indent ?? '\t';
  const d = doc as Record<string, unknown>;

  const lines: string[] = ['{'];

  const rootKnown = ROOT_KNOWN_ORDER.filter((k) => k in d);
  const rootCustom = Object.keys(d).filter((k) => !ROOT_KNOWN_ORDER.includes(k) && k !== 'layers' && k !== 'overrides').sort();
  const topKeys = [...rootKnown, ...rootCustom];

  for (const k of topKeys) {
    lines.push(`${tab}"${k}": ${inlineJson(d[k])},`);
  }

  const layers = Array.isArray(doc.layers) ? doc.layers : [];
  const overrides = Array.isArray(doc.overrides) ? doc.overrides : [];
  const hasOverrides = overrides.length > 0;

  lines.push(`${tab}"layers": [`);
  for (let li = 0; li < layers.length; li++) {
    const layer = layers[li];
    const l = layer as Record<string, unknown>;
    lines.push(`${tab}${tab}{`);

    const layerKnown = LAYER_KNOWN_ORDER.filter((k) => k in l);
    const layerCustom = Object.keys(l).filter((k) => !LAYER_KNOWN_ORDER.includes(k) && k !== 'objects' && k !== 'report').sort();

    for (const k of [...layerKnown, ...layerCustom]) {
      if (k === 'rules') {
        lines.push(`${tab}${tab}${tab}"rules": {`);
        const rk = Object.keys(layer.rules ?? {}).sort();
        for (let ri = 0; ri < rk.length; ri++) {
          const r = rk[ri];
          const comma = ri < rk.length - 1 ? ',' : '';
          const ruleVal = (layer.rules as Record<string, unknown>)[r];
          lines.push(`${tab}${tab}${tab}${tab}"${r}": ${inlineJson(ruleVal)}${comma}`);
        }
        const hasMoreAfterRules = ('rules_hash' in l) || layerCustom.length > 0 || ('objects' in l) || ('report' in l);
        const comma = hasMoreAfterRules ? ',' : '';
        lines.push(`${tab}${tab}${tab}}${comma}`);
      } else {
        lines.push(`${tab}${tab}${tab}"${k}": ${inlineJson(l[k])},`);
      }
    }

    if (Array.isArray(layer.objects)) {
      lines.push(`${tab}${tab}${tab}"objects": [`);
      for (let oi = 0; oi < layer.objects.length; oi++) {
        const obj = layer.objects[oi];
        const comma = oi < layer.objects.length - 1 ? ',' : '';
        lines.push(`${tab}${tab}${tab}${tab}${inlineObjectJson(obj)}${comma}`);
      }
      const hasReport = 'report' in l && l.report !== undefined;
      const comma = hasReport ? ',' : '';
      lines.push(`${tab}${tab}${tab}]${comma}`);
    }

    if ('report' in l && l.report !== undefined) {
      lines.push(`${tab}${tab}${tab}"report": ${inlineJson(l.report)}`);
    }

    const comma = li < layers.length - 1 ? ',' : '';
    lines.push(`${tab}${tab}}${comma}`);
  }
  const layersComma = hasOverrides ? ',' : '';
  lines.push(`${tab}]${layersComma}`);

  if (hasOverrides) {
    lines.push(`${tab}"overrides": [`);
    for (let oi = 0; oi < overrides.length; oi++) {
      const ov = overrides[oi];
      const comma = oi < overrides.length - 1 ? ',' : '';
      lines.push(`${tab}${tab}${inlineOverrideJson(ov)}${comma}`);
    }
    lines.push(`${tab}]`);
  }

  lines.push('}');
  return lines.join(nl) + nl;
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

/** Reads a props file from disk, detecting CRLF and validating content. */
export function readPropsFile(filePath: string): LevelPropsFile {
  const text = fs.readFileSync(filePath, 'utf8');
  return parsePropsJson(text);
}

/** Writes a props file to disk with stable key order, preserving CRLF if present or requested. */
export function writePropsFile(filePath: string, props: LevelPropsFile, options: FormatPropsOptions = {}): void {
  const text = formatPropsJson(props, options);
  fs.writeFileSync(filePath, text, 'utf8');
}
