// Text-preserving JSON writer for the Wave Editor dev-server API (pure, no DOM / Node APIs; tests: jsonTextPatch.test.ts).
//
// patchJsonText(originalText, newData) returns the original file text with only the values that changed rewritten:
// unchanged numbers keep their spelling ("45.0" stays "45.0"), hand-made layouts (one-line objects in
// enemy_perception.json) stay, unknown keys stay where they are, the BOM / CRLF / trailing newline of the file are
// kept. A no-change round trip is byte-identical. Removed keys are cut out with their comma, added keys go after the
// last member of their object. When the patched text would not parse back to newData the function falls back to a
// canonical JSON.stringify(newData, null, 2) with the file's BOM / line endings.

type JsonValue = null | boolean | number | string | JsonValue[] | { [key: string]: JsonValue };

interface PrimNode { kind: 'prim'; start: number; end: number }
interface ArrayNode { kind: 'array'; start: number; end: number; items: Node[] }
interface Member { key: string; keyStart: number; node: Node }
interface ObjectNode { kind: 'object'; start: number; end: number; members: Member[] }
type Node = PrimNode | ArrayNode | ObjectNode;

interface Edit { start: number; end: number; text: string }

class Scanner {
  pos = 0;
  readonly text: string;
  constructor(text: string) {
    this.text = text;
  }

  skipWs(): void {
    while (this.pos < this.text.length && /\s/.test(this.text[this.pos])) this.pos++;
  }

  fail(what: string): never {
    throw new Error(`JSON scan: ${what} at ${this.pos}`);
  }

  readString(): string {
    const start = this.pos;
    if (this.text[this.pos] !== '"') this.fail('string expected');
    this.pos++;
    while (this.pos < this.text.length && this.text[this.pos] !== '"') {
      this.pos += this.text[this.pos] === '\\' ? 2 : 1;
    }
    this.pos++;
    return JSON.parse(this.text.slice(start, this.pos)) as string;
  }

  value(): Node {
    this.skipWs();
    const start = this.pos;
    const c = this.text[this.pos];
    if (c === '{') {
      this.pos++;
      const members: Member[] = [];
      this.skipWs();
      if (this.text[this.pos] === '}') {
        this.pos++;
        return { kind: 'object', start, end: this.pos, members };
      }
      for (;;) {
        this.skipWs();
        const keyStart = this.pos;
        const key = this.readString();
        this.skipWs();
        if (this.text[this.pos] !== ':') this.fail("':' expected");
        this.pos++;
        members.push({ key, keyStart, node: this.value() });
        this.skipWs();
        if (this.text[this.pos] === ',') { this.pos++; continue; }
        if (this.text[this.pos] === '}') { this.pos++; break; }
        this.fail("',' or '}' expected");
      }
      return { kind: 'object', start, end: this.pos, members };
    }
    if (c === '[') {
      this.pos++;
      const items: Node[] = [];
      this.skipWs();
      if (this.text[this.pos] === ']') {
        this.pos++;
        return { kind: 'array', start, end: this.pos, items };
      }
      for (;;) {
        items.push(this.value());
        this.skipWs();
        if (this.text[this.pos] === ',') { this.pos++; continue; }
        if (this.text[this.pos] === ']') { this.pos++; break; }
        this.fail("',' or ']' expected");
      }
      return { kind: 'array', start, end: this.pos, items };
    }
    if (c === '"') {
      this.readString();
      return { kind: 'prim', start, end: this.pos };
    }
    const m = /^(-?\d+(\.\d+)?([eE][+-]?\d+)?|true|false|null)/.exec(this.text.slice(this.pos, this.pos + 64));
    if (!m) this.fail('value expected');
    this.pos += m[0].length;
    return { kind: 'prim', start, end: this.pos };
  }
}

const isObject = (v: unknown): v is { [key: string]: JsonValue } => typeof v === 'object' && v !== null && !Array.isArray(v);

/** Deep equality of JSON values; object key order is ignored. */
export function jsonEqual(a: unknown, b: unknown): boolean {
  if (a === b) return true;
  if (Array.isArray(a) || Array.isArray(b)) {
    if (!Array.isArray(a) || !Array.isArray(b) || a.length !== b.length) return false;
    return a.every((v, i) => jsonEqual(v, b[i]));
  }
  if (isObject(a) && isObject(b)) {
    const ka = Object.keys(a);
    const kb = Object.keys(b);
    if (ka.length !== kb.length) return false;
    return ka.every((k) => Object.prototype.hasOwnProperty.call(b, k) && jsonEqual(a[k], b[k]));
  }
  return false;
}

function lineIndent(text: string, pos: number): string {
  const lineStart = text.lastIndexOf('\n', pos - 1) + 1;
  const m = /^[ \t]*/.exec(text.slice(lineStart, pos));
  return m ? m[0] : '';
}

function render(value: unknown, indent: string, inline: boolean): string {
  return inline ? JSON.stringify(value) : JSON.stringify(value, null, 2).replace(/\n/g, '\n' + indent);
}

function diff(text: string, node: Node, oldVal: unknown, newVal: unknown, edits: Edit[]): void {
  if (jsonEqual(oldVal, newVal)) return;
  const replaceWhole = () => {
    const inline = !text.slice(node.start, node.end).includes('\n') && node.kind !== 'prim' && JSON.stringify(newVal).length <= 120;
    edits.push({ start: node.start, end: node.end, text: render(newVal, lineIndent(text, node.start), inline) });
  };

  if (node.kind === 'array' && Array.isArray(oldVal) && Array.isArray(newVal) && oldVal.length === newVal.length) {
    node.items.forEach((item, i) => diff(text, item, oldVal[i], newVal[i], edits));
    return;
  }
  if (node.kind !== 'object' || !isObject(oldVal) || !isObject(newVal)) {
    replaceWhole();
    return;
  }

  const members = node.members;
  const n = members.length;
  const deleted = members.map((m) => !Object.prototype.hasOwnProperty.call(newVal, m.key));
  const known = new Set(members.map((m) => m.key));
  const added = Object.keys(newVal).filter((k) => !known.has(k));
  const singleLine = !text.slice(node.start, node.end).includes('\n');
  const parentIndent = lineIndent(text, node.start);
  const memberIndent = n > 0 && !singleLine ? lineIndent(text, members[0].keyStart) : parentIndent + '  ';
  const sep = singleLine ? ', ' : ',\n' + memberIndent;
  const addedText = added
    .map((k) => `${JSON.stringify(k)}: ${render(newVal[k], memberIndent, singleLine)}`)
    .join(sep);

  members.forEach((m, i) => {
    if (!deleted[i]) diff(text, m.node, oldVal[m.key], newVal[m.key], edits);
  });

  // Trailing run of deleted members: cut from the end of the last kept value (its comma included).
  let t = n;
  while (t > 0 && deleted[t - 1]) t--;
  for (let i = 0; i < t; i++) {
    if (deleted[i]) edits.push({ start: members[i].keyStart, end: members[i + 1].keyStart, text: '' });
  }
  const keptCount = deleted.filter((d) => !d).length;
  if (keptCount === 0) {
    if (added.length === 0) {
      edits.push({ start: node.start + 1, end: node.end - 1, text: '' });
    } else if (singleLine) {
      edits.push({ start: node.start + 1, end: node.end - 1, text: ' ' + addedText + ' ' });
    } else {
      edits.push({ start: node.start + 1, end: node.end - 1, text: '\n' + memberIndent + addedText + '\n' + parentIndent });
    }
    return;
  }
  const lastKeptEnd = members[t - 1].node.end;
  const tail = added.length > 0 ? sep + addedText : '';
  if (t < n) {
    edits.push({ start: lastKeptEnd, end: members[n - 1].node.end, text: tail });
  } else if (tail) {
    edits.push({ start: lastKeptEnd, end: lastKeptEnd, text: tail });
  }
}

/** The original text's BOM / line ending / trailing newline, to write a canonical file the same way. */
function fileStyle(original: string) {
  return {
    bom: original.charCodeAt(0) === 0xfeff,
    crlf: original.includes('\r\n'),
    trailingNewline: /\n\s*$/.test(original) || original.endsWith('\n'),
  };
}

function applyStyle(body: string, style: ReturnType<typeof fileStyle>): string {
  let out = body.replace(/\s+$/, '');
  if (style.trailingNewline) out += '\n';
  if (style.crlf) out = out.replace(/\n/g, '\r\n');
  return (style.bom ? '﻿' : '') + out;
}

/** JSON.stringify(data, null, 2) written in the style (BOM / CRLF / trailing newline) of original. */
export function formatJsonLike(original: string, data: unknown): string {
  return applyStyle(JSON.stringify(data, null, 2), fileStyle(original));
}

/** Parses a JSON file text (a leading BOM is ignored). */
export function parseJsonText(text: string): unknown {
  return JSON.parse(text.replace(/^﻿/, ''));
}

/** The original file text with only the changed values rewritten (see the header). */
export function patchJsonText(original: string, newData: unknown): string {
  const style = fileStyle(original);
  const body = original.replace(/^﻿/, '').replace(/\r\n/g, '\n');
  try {
    const oldData = JSON.parse(body);
    if (jsonEqual(oldData, newData)) return original;
    const root = new Scanner(body).value();
    const edits: Edit[] = [];
    diff(body, root, oldData, newData, edits);
    edits.sort((a, b) => b.start - a.start || b.end - a.end);
    let out = body;
    for (const e of edits) out = out.slice(0, e.start) + e.text + out.slice(e.end);
    if (jsonEqual(JSON.parse(out), newData)) {
      if (style.crlf) out = out.replace(/\n/g, '\r\n');
      return (style.bom ? '﻿' : '') + out;
    }
  } catch {
    // fall through to the canonical layout
  }
  return applyStyle(JSON.stringify(newData, null, 2), style);
}
