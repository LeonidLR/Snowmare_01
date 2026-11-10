// Training Ground / movement: formation shapes schema (codex.formation_shapes/1).
// Docs: docs/port/gemini/inbox/008-formation-editor.md.
//
// Allows designers to customize formation member positions and look sectors in the Training Ground,
// saving overrides into Content/Data/FormationLab/formation_shapes.json.
// Formations not present in the file fall back to standard procedural rules from formation_config.json.

import fs from 'node:fs';
import path from 'node:path';
import type { Role } from './rules.ts';
import { CUSTOM_MAX_RADIUS } from './custom.ts';

export const FORMATION_SHAPES_SCHEMA = 'codex.formation_shapes/1';

/** Standard formations supported by the formation editor. */
export const EDITABLE_FORMATIONS = [
  'diamond',
  'wedge',
  'line',
  'echelon_left',
  'echelon_right',
  'column',
  'pair',
] as const;

export type EditableFormation = (typeof EDITABLE_FORMATIONS)[number];

export const FORMATION_ROLES = ['commander', 'right', 'left', 'rear'] as const;
export type FormationRole = (typeof FORMATION_ROLES)[number];

export const DEFAULT_ARC_DEG = 90;

export const ROLE_LETTER_TO_NAME: Record<Role, FormationRole> = {
  K: 'commander',
  P: 'right',
  L: 'left',
  Z: 'rear',
};

export const ROLE_NAME_TO_LETTER: Record<FormationRole, Role> = {
  commander: 'K',
  right: 'P',
  left: 'L',
  rear: 'Z',
};

/** A single member slot in a formation shape. */
export interface FormationMemberShape {
  role: FormationRole;
  /** Forward along heading, integer cm (+ is forward, - is backward). Commander is always 0. */
  fwd: number;
  /** Right of heading, integer cm (+ is right, - is left). Commander is always 0. */
  right: number;
  /** Look direction relative to heading, integer degrees in (-180, 180]. */
  look: number;
  /** Optional look sector width in degrees (e.g. 90). Absent = default rules. */
  arc?: number;
  [key: string]: unknown;
}

/** A formation shape definition for a specific member count (e.g. 2, 3, 4). */
export interface FormationShape {
  members: FormationMemberShape[];
  [key: string]: unknown;
}

/** A custom formation slot (squad index 0..3). */
export interface CustomMemberShape {
  squad: number;
  fwd: number;
  right: number;
  look: number;
  arc?: number;
  [key: string]: unknown;
}

export interface CustomShapeDef {
  members: CustomMemberShape[];
  [key: string]: unknown;
}

/** Master schema document for codex.formation_shapes/1. */
export interface FormationShapesFile {
  schema: typeof FORMATION_SHAPES_SCHEMA | string;
  formations: Partial<Record<string, Record<string, FormationShape>>>;
  custom?: Partial<Record<'custom_1' | 'custom_2' | 'custom_3', CustomShapeDef>>;
  [key: string]: unknown;
}

function isInteger(val: unknown): val is number {
  return typeof val === 'number' && Number.isInteger(val);
}

/** Validates arbitrary JSON against codex.formation_shapes/1. Returns array of error strings (empty if valid). */
export function validateFormationShapes(data: unknown): string[] {
  if (!data || typeof data !== 'object' || Array.isArray(data)) {
    return ['expected a JSON object'];
  }
  const doc = data as Record<string, unknown>;
  const errs: string[] = [];

  if (doc.schema !== FORMATION_SHAPES_SCHEMA) {
    errs.push(`schema must be "${FORMATION_SHAPES_SCHEMA}" (got "${String(doc.schema)}")`);
  }

  if (doc.formations !== undefined) {
    if (!doc.formations || typeof doc.formations !== 'object' || Array.isArray(doc.formations)) {
      errs.push('formations must be an object');
    } else {
      const formations = doc.formations as Record<string, unknown>;
      for (const [formName, countMap] of Object.entries(formations)) {
        if (!countMap || typeof countMap !== 'object' || Array.isArray(countMap)) {
          errs.push(`formations.${formName}: expected an object mapping member counts to shapes`);
          continue;
        }
        for (const [countStr, shape] of Object.entries(countMap as Record<string, unknown>)) {
          const count = Number(countStr);
          if (!isInteger(count) || count < 2 || count > 4) {
            errs.push(`formations.${formName}: member count "${countStr}" must be 2, 3, or 4`);
          }
          if (!shape || typeof shape !== 'object' || Array.isArray(shape)) {
            errs.push(`formations.${formName}["${countStr}"]: expected an object`);
            continue;
          }
          const s = shape as Record<string, unknown>;
          if (!Array.isArray(s.members)) {
            errs.push(`formations.${formName}["${countStr}"].members: expected an array`);
          } else {
            if (s.members.length !== count) {
              errs.push(`formations.${formName}["${countStr}"]: members array length ${s.members.length} does not match count ${count}`);
            }
            const rolesSeen = new Set<string>();
            for (let i = 0; i < s.members.length; i++) {
              const m = s.members[i] as Record<string, unknown>;
              if (!m || typeof m !== 'object') {
                errs.push(`formations.${formName}["${countStr}"].members[${i}]: expected an object`);
                continue;
              }
              if (!FORMATION_ROLES.includes(m.role as FormationRole)) {
                errs.push(`formations.${formName}["${countStr}"].members[${i}]: bad role "${String(m.role)}"`);
              } else if (rolesSeen.has(String(m.role))) {
                errs.push(`formations.${formName}["${countStr}"]: duplicate role "${String(m.role)}"`);
              } else {
                rolesSeen.add(String(m.role));
              }

              if (!isInteger(m.fwd) || !isInteger(m.right)) {
                errs.push(`formations.${formName}["${countStr}"].members[${i}]: fwd and right must be integer cm`);
              }
              if (m.role === 'commander' && (m.fwd !== 0 || m.right !== 0)) {
                errs.push(`formations.${formName}["${countStr}"]: commander must be at fwd 0, right 0`);
              }
              if (isInteger(m.fwd) && isInteger(m.right)) {
                const r = Math.hypot(m.fwd, m.right);
                if (r > CUSTOM_MAX_RADIUS) {
                  errs.push(`formations.${formName}["${countStr}"].members[${i}]: distance ${Math.round(r)} cm exceeds CUSTOM_MAX_RADIUS ${CUSTOM_MAX_RADIUS}`);
                }
              }

              if (!isInteger(m.look) || m.look <= -180 || m.look > 180) {
                errs.push(`formations.${formName}["${countStr}"].members[${i}]: look must be integer degrees in (-180, 180]`);
              }
              if (m.arc !== undefined && (!isInteger(m.arc) || m.arc < 10 || m.arc > 360)) {
                errs.push(`formations.${formName}["${countStr}"].members[${i}]: arc must be integer degrees between 10 and 360`);
              }
            }
          }
        }
      }
    }
  }

  if (doc.custom !== undefined) {
    if (!doc.custom || typeof doc.custom !== 'object' || Array.isArray(doc.custom)) {
      errs.push('custom must be an object');
    } else {
      const custom = doc.custom as Record<string, unknown>;
      for (const [slotKey, shape] of Object.entries(custom)) {
        if (!['custom_1', 'custom_2', 'custom_3'].includes(slotKey)) {
          errs.push(`custom: unexpected slot key "${slotKey}"`);
        }
        if (!shape || typeof shape !== 'object' || Array.isArray(shape)) {
          errs.push(`custom.${slotKey}: expected an object`);
          continue;
        }
        const s = shape as Record<string, unknown>;
        if (!Array.isArray(s.members)) {
          errs.push(`custom.${slotKey}.members: expected an array`);
        } else {
          for (let i = 0; i < s.members.length; i++) {
            const m = s.members[i] as Record<string, unknown>;
            if (!isInteger(m.squad) || m.squad < 0 || m.squad > 3) {
              errs.push(`custom.${slotKey}.members[${i}]: squad must be 0..3`);
            }
            if (!isInteger(m.fwd) || !isInteger(m.right)) {
              errs.push(`custom.${slotKey}.members[${i}]: fwd and right must be integer cm`);
            }
            if (m.squad === 0 && (m.fwd !== 0 || m.right !== 0)) {
              errs.push(`custom.${slotKey}: squad 0 (anchor) must be at fwd 0, right 0`);
            }
            if (!isInteger(m.look) || m.look <= -180 || m.look > 180) {
              errs.push(`custom.${slotKey}.members[${i}]: look must be integer degrees in (-180, 180]`);
            }
          }
        }
      }
    }
  }

  return errs;
}

export interface FormatShapesOptions {
  crlf?: boolean;
  indent?: string;
  skipValidation?: boolean;
}

function inlineJson(val: unknown): string {
  if (val === undefined) return 'null';
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

const MEMBER_KNOWN_ORDER: readonly string[] = ['role', 'squad', 'fwd', 'right', 'look', 'arc'];

function inlineMemberJson(m: FormationMemberShape | CustomMemberShape): string {
  const o = m as Record<string, unknown>;
  const known = MEMBER_KNOWN_ORDER.filter((k) => k in o && o[k] !== undefined);
  const unknown = Object.keys(o).filter((k) => !MEMBER_KNOWN_ORDER.includes(k) && o[k] !== undefined);
  const keys = [...known, ...unknown];
  const items = keys.map((k) => `${JSON.stringify(k)}: ${inlineJson(o[k])}`);
  return '{ ' + items.join(', ') + ' }';
}

/** Formats FormationShapesFile to stable JSON string preserving insertion order and unknown keys. */
export function formatFormationShapesJson(doc: FormationShapesFile, options: FormatShapesOptions = {}): string {
  if (!options.skipValidation) {
    const errs = validateFormationShapes(doc);
    if (errs.length > 0) {
      throw new Error(`Cannot format invalid formation shapes: ${errs.join('; ')}`);
    }
  }

  const crlf = options.crlf ?? false;
  const nl = crlf ? '\r\n' : '\n';
  const tab = options.indent ?? '  ';
  const d = doc as Record<string, unknown>;

  const rootItems: string[] = [];

  rootItems.push(`${tab}"schema": ${inlineJson(doc.schema)}`);

  // Formations block
  if (doc.formations !== undefined) {
    const formKeys = Object.keys(doc.formations);
    if (formKeys.length === 0) {
      rootItems.push(`${tab}"formations": {}`);
    } else {
      const fLines: string[] = [`${tab}"formations": {`];
      const formItems: string[] = [];
      for (const formKey of formKeys) {
        const countMap = doc.formations[formKey];
        if (!countMap || Object.keys(countMap).length === 0) {
          formItems.push(`${tab}${tab}${JSON.stringify(formKey)}: {}`);
          continue;
        }
        const countKeys = Object.keys(countMap).sort((a, b) => Number(a) - Number(b));
        const cLines: string[] = [`${tab}${tab}${JSON.stringify(formKey)}: {`];
        const countItems: string[] = [];
        for (const countKey of countKeys) {
          const shape = countMap[countKey];
          const members = Array.isArray(shape?.members) ? shape.members : [];
          const mItems = members.map((m) => `${tab}${tab}${tab}${tab}${inlineMemberJson(m)}`);
          const sLines: string[] = [`${tab}${tab}${tab}${JSON.stringify(countKey)}: {`];
          sLines.push(`${tab}${tab}${tab}${tab}"members": [`);
          sLines.push(mItems.join(',' + nl));
          sLines.push(`${tab}${tab}${tab}${tab}]`);
          sLines.push(`${tab}${tab}${tab}}`);
          countItems.push(sLines.join(nl));
        }
        cLines.push(countItems.join(',' + nl));
        cLines.push(`${tab}${tab}}`);
        formItems.push(cLines.join(nl));
      }
      fLines.push(formItems.join(',' + nl));
      fLines.push(`${tab}}`);
      rootItems.push(fLines.join(nl));
    }
  }

  // Custom block
  if (doc.custom !== undefined) {
    const customKeys = Object.keys(doc.custom);
    if (customKeys.length === 0) {
      rootItems.push(`${tab}"custom": {}`);
    } else {
      const cLines: string[] = [`${tab}"custom": {`];
      const customItems: string[] = [];
      for (const slotKey of customKeys) {
        const shape = doc.custom[slotKey as 'custom_1' | 'custom_2' | 'custom_3'];
        const members = Array.isArray(shape?.members) ? shape.members : [];
        const mItems = members.map((m) => `${tab}${tab}${tab}${inlineMemberJson(m)}`);
        const sLines: string[] = [`${tab}${tab}${JSON.stringify(slotKey)}: {`];
        sLines.push(`${tab}${tab}${tab}"members": [`);
        sLines.push(mItems.join(',' + nl));
        sLines.push(`${tab}${tab}${tab}]`);
        sLines.push(`${tab}${tab}}`);
        customItems.push(sLines.join(nl));
      }
      cLines.push(customItems.join(',' + nl));
      cLines.push(`${tab}}`);
      rootItems.push(cLines.join(nl));
    }
  }

  // Unknown root keys
  for (const k of Object.keys(d)) {
    if (k !== 'schema' && k !== 'formations' && k !== 'custom' && d[k] !== undefined) {
      rootItems.push(`${tab}${JSON.stringify(k)}: ${inlineJson(d[k])}`);
    }
  }

  return `{${nl}${rootItems.join(',' + nl)}${nl}}${nl}`;
}

/** Parses JSON text into a validated FormationShapesFile document. */
export function parseFormationShapesJson(text: string): FormationShapesFile {
  const data = JSON.parse(text);
  const errors = validateFormationShapes(data);
  if (errors.length > 0) {
    throw new Error(`Invalid codex.formation_shapes/1: ${errors.join('; ')}`);
  }
  return data as FormationShapesFile;
}

export const readFormationShapesJson = parseFormationShapesJson;
export const writeFormationShapesJson = formatFormationShapesJson;

/** Reads formation shapes from file, returning empty default document if file does not exist. */
export function readFormationShapesFile(filePath: string): FormationShapesFile {
  if (!fs.existsSync(filePath)) {
    return {
      schema: FORMATION_SHAPES_SCHEMA,
      formations: {},
      custom: {},
    };
  }
  const text = fs.readFileSync(filePath, 'utf8');
  return parseFormationShapesJson(text);
}

/** Writes formation shapes to file with atomic write (temp file + rename). */
export function writeFormationShapesFile(filePath: string, shapes: FormationShapesFile, options: FormatShapesOptions = {}): void {
  const text = formatFormationShapesJson(shapes, options);
  const dir = path.dirname(filePath);
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }
  const tempPath = `${filePath}.tmp.${Date.now()}`;
  fs.writeFileSync(tempPath, text, 'utf8');
  fs.renameSync(tempPath, filePath);
}
