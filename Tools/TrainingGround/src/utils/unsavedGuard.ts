// Training Ground: one place that knows which pages hold edits not written to disk yet, so a switch of the department (App-level tabs)
// can ask before the page unmounts and the edits are gone («Пропсы уровней» registers its dirty flag here, T-023). Pure state, no DOM
// except window.confirm in confirmLeave.

const sources = new Map<string, string>();

/** The page `key` has unsaved edits (`what` = a short human text), or none (null). */
export function setUnsaved(key: string, what: string | null): void {
  if (what) sources.set(key, what);
  else sources.delete(key);
}

/** The texts of the pages with unsaved edits. */
export function unsavedList(): string[] {
  return [...sources.values()];
}

/** true = leave (nothing unsaved, or the user agreed). */
export function confirmLeave(ask: (text: string) => boolean = (t) => window.confirm(t)): boolean {
  const list = unsavedList();
  if (list.length === 0) return true;
  return ask(`Есть несохранённые правки: ${list.join('; ')}. Уйти без сохранения? Они пропадут.`);
}
