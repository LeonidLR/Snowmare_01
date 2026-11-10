/**
 * Bot simulator parameter sanitization and validation.
 * Protects against command injection when spawning runner scripts.
 */

export interface BotRunPayload {
  runs?: number | string;
  profile?: string;
  speed?: number | string;
  loadout?: string;
  mapPath?: string;
  levelJsonPath?: string;
  file?: string;
  config?: unknown;
}

export interface SanitizedBotRunArgs {
  runs?: string;
  profile: string;
  speed: string;
  loadout: string;
  mapPath: string;
  levelJsonPath: string;
}

export const FORBIDDEN_SHELL_PATTERN = /["&|<>^%!;\r\n\t`'$]/;

export function sanitizeBotRunArgs(payload: BotRunPayload): { ok: true; args: SanitizedBotRunArgs } | { ok: false; error: string } {
  // 1. Reject any dangerous shell metacharacters in string fields
  for (const [key, val] of Object.entries(payload)) {
    if (typeof val === 'string' && FORBIDDEN_SHELL_PATTERN.test(val)) {
      return { ok: false, error: `Параметр ${key} содержит недопустимые спецсимволы командной строки` };
    }
  }

  // 2. Validate runs (/^\d+$/, 1..1000)
  let sanitizedRuns: string | undefined = undefined;
  if (payload.runs !== undefined && payload.runs !== null && payload.runs !== '') {
    const runsStr = String(payload.runs).trim();
    if (!/^\d+$/.test(runsStr)) {
      return { ok: false, error: 'Параметр runs должен быть целым числом' };
    }
    const num = parseInt(runsStr, 10);
    if (num < 1 || num > 1000) {
      return { ok: false, error: 'Количество забегов (runs) должно быть от 1 до 1000' };
    }
    sanitizedRuns = String(num);
  }

  // 3. Validate profile (/^[A-Z0-9_]+$/)
  const profile = (payload.profile || 'NORMAL').trim();
  if (!/^[A-Z0-9_]+$/.test(profile)) {
    return { ok: false, error: 'Параметр profile должен содержать только заглавные латинские буквы, цифры и подчёркивание' };
  }

  // 4. Validate speed (/^\d+(\.\d+)?$/, 0.1..100.0)
  const speedStr = String(payload.speed ?? '5.0').trim();
  if (!/^\d+(\.\d+)?$/.test(speedStr)) {
    return { ok: false, error: 'Параметр speed должен быть положительным числом' };
  }
  const speedNum = parseFloat(speedStr);
  if (speedNum < 0.1 || speedNum > 100.0) {
    return { ok: false, error: 'Скорость симуляции (speed) должна быть от 0.1 до 100.0' };
  }

  // 5. Validate loadout (/^[A-Z0-9_]+$/)
  const loadout = (payload.loadout || 'COLLECT').trim();
  if (!/^[A-Z0-9_]+$/.test(loadout)) {
    return { ok: false, error: 'Параметр loadout должен содержать только заглавные латинские буквы, цифры и подчёркивание' };
  }

  // 6. Validate mapPath (must start with /Game/ and contain only safe path chars, no ..)
  const mapPath = (payload.mapPath || '/Game/Maps/L_MovementTest').trim();
  if (!/^\/Game\/[A-Za-z0-9_\-\/]+$/.test(mapPath) || mapPath.includes('..')) {
    return { ok: false, error: 'Недопустимый путь к карте Unreal (должен начинаться с /Game/)' };
  }

  // 7. Validate levelJsonPath (if provided, must be safe path ending in .json, no ..)
  const levelJsonPath = (payload.levelJsonPath || '').trim();
  if (levelJsonPath) {
    if (!/^[A-Za-z0-9_:\\\/\.\-]+$/.test(levelJsonPath) || levelJsonPath.includes('..') || !levelJsonPath.toLowerCase().endsWith('.json')) {
      return { ok: false, error: 'Недопустимый путь к levelJsonPath (должен быть путем к .json файлу без обхода каталогов)' };
    }
  }

  return {
    ok: true,
    args: {
      runs: sanitizedRuns,
      profile,
      speed: speedStr,
      loadout,
      mapPath,
      levelJsonPath,
    },
  };
}
