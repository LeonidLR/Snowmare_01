// Training Ground / level generator: canonical writer for LevelPropsFile matching C++ LevelPropsFile::Write byte-for-byte.
// Docs: docs/design/levelgen/PROPS_LAYERS.md sections 5 (task C1/T2) and 7.
// C++ reference: Source/CodexTactics/Private/LevelGen/LevelPropsFile.cpp

import type { LevelPropsFile, PropObject } from './schema.ts';

const G_ROOT_KEYS: readonly string[] = [
  'schema',
  'map',
  'layout',
  'layout_hash',
  'revision',
  'frozen',
  'layers',
  'overrides',
];

const G_LAYER_KEYS: readonly string[] = [
  'id',
  'kind',
  'enabled',
  'seed',
  'locked',
  'preset',
  'rules',
  'rules_hash',
  'objects',
  'report',
];

const G_OBJECT_KEYS: readonly string[] = [
  'id',
  'variant',
  'pos',
  'walk_layer',
  'yaw',
  'locked',
  'origin',
  'attach',
  'door',
  'trigger',
  'loot_table',
  'arc',
];

const G_OVERRIDE_KEYS: readonly string[] = [
  'op',
  'id',
  'pos',
  'walk_layer',
  'z_offset',
  'yaw',
  'object',
  'fields',
];

class JsonOut {
  s = '';
  counts: number[] = [];
  bAfterKey = false;

  prep(): void {
    if (this.bAfterKey) {
      this.bAfterKey = false;
      return;
    }
    if (this.counts.length > 0) {
      if (this.counts[this.counts.length - 1]++ > 0) {
        this.s += ',';
      }
      this.s += '\n';
      this.s += '\t'.repeat(this.counts.length);
    }
  }

  open(c: '{' | '['): void {
    this.prep();
    this.s += c;
    this.counts.push(0);
  }

  close(c: '}' | ']'): void {
    const n = this.counts.pop() ?? 0;
    if (n > 0) {
      this.s += '\n';
      this.s += '\t'.repeat(this.counts.length);
    }
    this.s += c;
  }

  key(k: string): void {
    this.prep();
    this.s += JSON.stringify(k) + ': ';
    this.bAfterKey = true;
  }

  raw(text: string): void {
    this.prep();
    this.s += text;
  }

  fieldStr(k: string, v: string): void {
    this.key(k);
    this.raw(JSON.stringify(v));
  }

  fieldInt(k: string, v: number): void {
    this.key(k);
    this.raw(String(Math.floor(v)));
  }

  fieldBool(k: string, v: boolean): void {
    this.key(k);
    this.raw(v ? 'true' : 'false');
  }

  fieldPos(k: string, p: [number, number]): void {
    this.key(k);
    this.raw(`[${Math.floor(p[0])}, ${Math.floor(p[1])}]`);
  }
}

function isScalar(val: unknown): boolean {
  return (
    val === null ||
    val === undefined ||
    typeof val === 'boolean' ||
    typeof val === 'number' ||
    typeof val === 'string'
  );
}

function scalarText(val: unknown): string {
  if (val === null || val === undefined) return 'null';
  if (typeof val === 'boolean') return val ? 'true' : 'false';
  if (typeof val === 'number') {
    if (!Number.isFinite(val)) return 'null';
    return String(Math.floor(val) === val ? Math.floor(val) : val);
  }
  return JSON.stringify(val);
}

function writeValue(out: JsonOut, val: unknown): void {
  if (isScalar(val)) {
    out.raw(scalarText(val));
    return;
  }
  if (Array.isArray(val)) {
    const bAllScalar = val.every(isScalar);
    if (bAllScalar) {
      out.raw('[' + val.map(scalarText).join(', ') + ']');
      return;
    }
    out.open('[');
    for (const item of val) {
      writeValue(out, item);
    }
    out.close(']');
    return;
  }
  if (val && typeof val === 'object') {
    writeObjectSorted(out, val as Record<string, unknown>);
    return;
  }
  out.raw('null');
}

function writeObjectSorted(out: JsonOut, obj: Record<string, unknown>): void {
  out.open('{');
  const keys = Object.keys(obj).sort();
  for (const k of keys) {
    if (obj[k] !== undefined) {
      out.key(k);
      writeValue(out, obj[k]);
    }
  }
  out.close('}');
}

function writeExtra(out: JsonOut, extra: Record<string, unknown>, known: readonly string[]): void {
  const keys = Object.keys(extra)
    .filter((k) => !known.includes(k) && extra[k] !== undefined)
    .sort();
  for (const k of keys) {
    out.key(k);
    writeValue(out, extra[k]);
  }
}

function writeObject(out: JsonOut, o: PropObject): void {
  out.open('{');
  out.fieldStr('id', o.id);
  out.fieldStr('variant', o.variant);
  out.fieldPos('pos', o.pos);
  out.fieldStr('walk_layer', o.walk_layer);
  out.fieldInt('yaw', o.yaw ?? 0);
  out.fieldBool('locked', Boolean(o.locked));
  if (o.origin) out.fieldStr('origin', o.origin);
  if (o.attach) out.fieldStr('attach', o.attach);
  if (o.door) out.fieldStr('door', o.door);
  if (o.trigger) out.fieldStr('trigger', o.trigger);
  if (o.loot_table) out.fieldStr('loot_table', o.loot_table);
  if (typeof o.arc === 'number') out.fieldInt('arc', o.arc);
  writeExtra(out, o as Record<string, unknown>, G_OBJECT_KEYS);
  out.close('}');
}

/** Formats a LevelPropsFile with exact byte-for-byte fidelity to C++ LevelPropsFile::Write. */
export function formatPropsCanonical(file: LevelPropsFile): string {
  const out = new JsonOut();
  out.open('{');
  out.fieldStr('schema', file.schema);
  out.fieldStr('map', file.map);
  out.fieldStr('layout', file.layout);
  out.fieldStr('layout_hash', file.layout_hash);
  out.fieldInt('revision', file.revision);
  out.fieldBool('frozen', Boolean(file.frozen));
  writeExtra(out, file as Record<string, unknown>, G_ROOT_KEYS);

  out.key('layers');
  out.open('[');
  for (const l of file.layers) {
    out.open('{');
    out.fieldStr('id', l.id);
    out.fieldStr('kind', l.kind);
    out.fieldBool('enabled', Boolean(l.enabled));
    out.fieldInt('seed', l.seed >>> 0);
    out.fieldBool('locked', Boolean(l.locked));
    if (l.preset) out.fieldStr('preset', l.preset);
    if (l.rules) {
      out.key('rules');
      writeObjectSorted(out, l.rules as unknown as Record<string, unknown>);
    }
    if (l.rules_hash) out.fieldStr('rules_hash', l.rules_hash);
    out.key('objects');
    out.open('[');
    for (const o of l.objects) {
      writeObject(out, o);
    }
    out.close(']');
    if (l.report) {
      out.key('report');
      writeObjectSorted(out, l.report as unknown as Record<string, unknown>);
    }
    writeExtra(out, l as Record<string, unknown>, G_LAYER_KEYS);
    out.close('}');
  }
  out.close(']');

  out.key('overrides');
  out.open('[');
  if (Array.isArray(file.overrides)) {
    for (const ov of file.overrides) {
      out.open('{');
      out.fieldStr('op', ov.op);
      if (ov.op === 'add') {
        out.key('object');
        writeObject(out, (ov as { object: PropObject }).object);
      } else {
        out.fieldStr('id', (ov as { id: string }).id);
      }
      const o = ov as Record<string, unknown>;
      if (o.pos) out.fieldPos('pos', o.pos as [number, number]);
      if (o.walk_layer) out.fieldStr('walk_layer', String(o.walk_layer));
      if (typeof o.z_offset === 'number') out.fieldInt('z_offset', o.z_offset);
      if (typeof o.yaw === 'number') out.fieldInt('yaw', o.yaw);
      if (o.fields && typeof o.fields === 'object') {
        out.key('fields');
        writeObjectSorted(out, o.fields as Record<string, unknown>);
      }
      writeExtra(out, ov as Record<string, unknown>, G_OVERRIDE_KEYS);
      out.close('}');
    }
  }
  out.close(']');
  out.close('}');
  return out.s + '\n';
}
