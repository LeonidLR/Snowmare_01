// Training Ground: the keyboard rules of the «full window» mode shared by the map page (T-022, «Управление картой») and the props canvas
// (T-023, «Пропсы уровней»): F11 toggles a plan to fill the whole browser window (the browser's own fullscreen is NOT used: F11 is
// preventDefault-ed), Esc / F11 / «Свернуть» go back. Pure decisions, no DOM: the hook + the frame are in components/FullWindowFrame.tsx.

/** The part of a KeyboardEvent the rules read. */
export interface KeyLike {
  key: string;
  ctrlKey?: boolean;
  altKey?: boolean;
  metaKey?: boolean;
  shiftKey?: boolean;
  /** The event target: a text field swallows F11 (the browser keeps it). */
  target?: EventTarget | { tagName?: string; isContentEditable?: boolean; type?: string } | null;
}

export type FullWindowAction = 'toggle' | 'exit' | null;

const TEXT_INPUT_TYPES = ['', 'text', 'number', 'search', 'email', 'url', 'tel', 'password'];
/** A field the user types in (a text / number input, a textarea, a contenteditable): F11 / Esc / arrows / Delete must not be taken from it. */
export function isTypingTarget(target: KeyLike['target']): boolean {
  if (!target) return false;
  const t = target as { tagName?: string; isContentEditable?: boolean; type?: string };
  const tag = (t.tagName ?? '').toUpperCase();
  return tag === 'TEXTAREA' || !!t.isContentEditable || (tag === 'INPUT' && TEXT_INPUT_TYPES.includes((t.type ?? '').toLowerCase()));
}

/**
 * A control that uses the arrow keys / Delete itself (a typing field, a <select>, a range slider): the page's own key shortcuts (nudge,
 * Delete, Ctrl+Z) leave it alone. A checkbox / button holding the focus after a click does NOT count (the shortcuts must still work).
 */
export function isKeyConsumer(target: KeyLike['target']): boolean {
  if (!target) return false;
  const t = target as { tagName?: string; type?: string };
  const tag = (t.tagName ?? '').toUpperCase();
  return isTypingTarget(target) || tag === 'SELECT' || (tag === 'INPUT' && (t.type ?? '').toLowerCase() === 'range');
}

/**
 * What a key press does to the full-window mode. F11 (no modifiers) toggles - only while `enabled` (the page is the active one) and the
 * focus is not in a text field (a <select> or a checkbox keeps F11 working); Esc leaves the mode (only while it is on and `escExits`: a page with its own Esc, like the polygon
 * tool's cancel, turns it off). null = not ours (the page must not preventDefault).
 */
export function fullWindowAction(e: KeyLike, st: { enabled: boolean; full: boolean; escExits?: boolean }): FullWindowAction {
  if (!st.enabled || e.ctrlKey || e.altKey || e.metaKey || e.shiftKey) return null;
  if (isTypingTarget(e.target)) return null;
  if (e.key === 'F11') return 'toggle';
  if (e.key === 'Escape' && st.full && st.escExits !== false) return 'exit';
  return null;
}

/** The default plan height (px) of the normal (not full) mode: the viewport minus the toolbars above it, at least `min`. */
export function planHeightFor(viewportH: number, reserved = 260, min = 600): number {
  return Math.max(min, Math.round(viewportH - reserved));
}
