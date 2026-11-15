// Training Ground / level generator v2, stage 1 (task 070): the undo / redo history of region re-rolls and lock edits. Immutable
// snapshots (a layout is ~100 KB; HISTORY_DEFAULT_LIMIT steps stay a few MB). Pure TS, no DOM.

/** The task's floor: at least this many undo steps. */
export const HISTORY_MIN_STEPS = 20;
export const HISTORY_DEFAULT_LIMIT = 50;

export interface HistoryEntry<T> { state: T; label: string }
export interface History<T> {
  past: HistoryEntry<T>[];
  present: HistoryEntry<T>;
  future: HistoryEntry<T>[];
  /** Max undo steps kept (>= HISTORY_MIN_STEPS). */
  limit: number;
}

export function historyInit<T>(state: T, label = 'start', limit = HISTORY_DEFAULT_LIMIT): History<T> {
  return { past: [], present: { state, label }, future: [], limit: Math.max(HISTORY_MIN_STEPS, Math.floor(limit)) };
}

/** A new step: the present goes to the past (the oldest beyond `limit` is dropped), the redo branch is cleared. */
export function historyPush<T>(h: History<T>, state: T, label: string): History<T> {
  const past = [...h.past, h.present];
  return { ...h, past: past.length > h.limit ? past.slice(past.length - h.limit) : past, present: { state, label }, future: [] };
}

export const canUndo = <T>(h: History<T>): boolean => h.past.length > 0;
export const canRedo = <T>(h: History<T>): boolean => h.future.length > 0;

export function historyUndo<T>(h: History<T>): History<T> {
  if (!canUndo(h)) return h;
  return { ...h, past: h.past.slice(0, -1), present: h.past[h.past.length - 1], future: [h.present, ...h.future] };
}

export function historyRedo<T>(h: History<T>): History<T> {
  if (!canRedo(h)) return h;
  return { ...h, past: [...h.past, h.present], present: h.future[0], future: h.future.slice(1) };
}
