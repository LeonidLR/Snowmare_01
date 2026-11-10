// Training Ground / level generator: props schema migration stub (codex.levelgen_props).
// Docs: docs/design/levelgen/PROPS_LAYERS.md section 1.4:
// Readers (C++ and TS) accept /1 and refuse an unknown major version with a clear log line.
// New optional fields are allowed in /1. A breaking change means /2 plus a TS migration here.

import { PROPS_SCHEMA, type LevelPropsFile, validateProps } from './schema.ts';

/** Returns true if data carries a known older or alternate codex.levelgen_props schema version requiring migration. */
export function needsMigration(data: unknown): boolean {
  if (!data || typeof data !== 'object') return false;
  const s = (data as { schema?: unknown }).schema;
  return typeof s === 'string' && s.startsWith('codex.levelgen_props/') && s !== PROPS_SCHEMA;
}

/** Migrates a raw props document to the current codex.levelgen_props/1 format. */
export function migrateProps(data: unknown): LevelPropsFile {
  if (!data || typeof data !== 'object') {
    throw new Error('migrateProps: expected a JSON object');
  }
  const s = (data as { schema?: unknown }).schema;
  if (typeof s !== 'string' || !s.startsWith('codex.levelgen_props/')) {
    throw new Error(`migrateProps: unknown props schema "${String(s)}", expected "${PROPS_SCHEMA}"`);
  }

  if (s === PROPS_SCHEMA) {
    const errs = validateProps(data);
    if (errs.length > 0) {
      throw new Error(`migrateProps: invalid ${PROPS_SCHEMA}: ${errs.join('; ')}`);
    }
    return data as LevelPropsFile;
  }

  // Future schema versions (/2, /3) will be handled here.
  throw new Error(`migrateProps: unsupported schema version "${s}" (only "${PROPS_SCHEMA}" is currently supported)`);
}
