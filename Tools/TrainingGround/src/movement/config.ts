// Training Ground / movement: formation_config.json (schema codex.formation_config/1, FORMATION_CONTRACT.md "Units").
// Scalars only; the tuner changes `value` inside min..max. The C++ rules read the same file.

export const CONFIG_SCHEMA = 'codex.formation_config/1';

export interface ConfigParam {
  value: number;
  min: number;
  max: number;
  unit: string;
  doc: string;
}

export interface FormationConfigFile {
  schema: string;
  comment?: string;
  params: Record<string, ConfigParam>;
}

/** The values the rules read: key -> number. */
export type FormationConfig = Readonly<Record<string, number>>;

/** Value lookup that fails loudly on a missing key (a typo must not silently become NaN). */
export function cfg(config: FormationConfig, key: string): number {
  const v = config[key];
  if (typeof v !== 'number' || !Number.isFinite(v)) throw new Error(`formation_config: missing or bad key "${key}"`);
  return v;
}

/** Problems of a config file (empty = valid). */
export function validateConfigFile(data: unknown): string[] {
  const errors: string[] = [];
  if (!data || typeof data !== 'object') return ['expected a JSON object'];
  const file = data as Partial<FormationConfigFile>;
  if (file.schema !== CONFIG_SCHEMA) errors.push(`schema must be "${CONFIG_SCHEMA}"`);
  if (!file.params || typeof file.params !== 'object') return [...errors, 'params must be an object'];
  for (const [key, p] of Object.entries(file.params)) {
    if (!p || typeof p !== 'object') { errors.push(`${key}: expected an object`); continue; }
    for (const field of ['value', 'min', 'max'] as const) {
      if (typeof p[field] !== 'number' || !Number.isFinite(p[field])) errors.push(`${key}.${field}: expected a number`);
    }
    if (typeof p.min === 'number' && typeof p.max === 'number' && p.min > p.max) errors.push(`${key}: min > max`);
    if (typeof p.value === 'number' && (p.value < p.min || p.value > p.max)) errors.push(`${key}: value ${p.value} outside ${p.min}..${p.max}`);
  }
  return errors;
}

/** The values of a (valid) config file. */
export function configValues(file: FormationConfigFile): FormationConfig {
  const out: Record<string, number> = {};
  for (const [key, p] of Object.entries(file.params)) out[key] = p.value;
  return out;
}

/** A copy of the file with some values replaced (clamped into range); unknown keys are reported, not added. */
export function withValues(file: FormationConfigFile, values: Record<string, number>): { file: FormationConfigFile; errors: string[] } {
  const errors: string[] = [];
  const params: Record<string, ConfigParam> = {};
  for (const [key, p] of Object.entries(file.params)) params[key] = { ...p };
  for (const [key, v] of Object.entries(values)) {
    const p = params[key];
    if (!p) { errors.push(`unknown key "${key}"`); continue; }
    if (typeof v !== 'number' || !Number.isFinite(v)) { errors.push(`${key}: not a number`); continue; }
    if (v < p.min || v > p.max) errors.push(`${key}: ${v} clamped into ${p.min}..${p.max}`);
    p.value = Math.max(p.min, Math.min(p.max, v));
  }
  return { file: { ...file, params }, errors };
}
