1. **Nested key order is rewritten.** `inlineJson` does `Object.keys(obj).sort()`, and the `rules` block does `Object.keys(layer.rules).sort()`. That reorders `rules.variants` (`crate_s, crate_l` becomes `crate_l, crate_s`), `where`, `exclude` and any unknown nested keys. The design says the weighted draw consumes candidates and variants in order, so a reorder can change the roll in the game and the TS port. Fix: keep insertion order (`Object.keys(obj)` with no `.sort()`) for nested values and `rules`; only order the known top-level keys.

2. **Invalid JSON for `undefined` values and special keys.** `inlineJson(undefined)` returns `JSON.stringify(undefined)`, which is `undefined`, so the output contains the literal text `undefined`. Keys are written as `` `"${k}"` `` without escaping, so a key containing `"` or `\` produces an unparseable file. Fix: skip keys whose value is `undefined`, and emit keys with `JSON.stringify(k)`.

3. **Trailing comma when `objects` or `report` is missing.** In `formatPropsJson`, every non-`rules` key is written with `,` and the `objects` block is written only `if (Array.isArray(layer.objects))`. A layer with no `objects` array and no `report` ends on `"k": v,` followed by `}`. The `rules` branch also assumes `objects` follows when it computes its comma. `writePropsFile` never validates, so it writes this file. Fix: build the layer's item strings into an array and `join(',' + nl)`, and call `validateProps` in `formatPropsJson` or `writePropsFile` before writing.

4. **`overrides: []` is dropped on write.** `hasOverrides = overrides.length > 0` means an empty array the editor wrote disappears and the key is lost. Fix: use `doc.overrides !== undefined` (and `Array.isArray`) to decide whether to emit it.

5. **Crash on a null `object` in an `add` override.** `inlineOverrideJson` calls `inlineObjectJson(o.object)`, which runs `k in o` on `null` or a non-object and throws. The write path skips the validator, so this is reachable. Fix: guard with `o.object && typeof o.object === 'object'`, otherwise fall back to `inlineJson`. Validating before write also fixes it.

6. **Validator gaps for invalid files.**
   - Duplicate object ids across layers and `add` overrides are not rejected, but override semantics depend on unique ids.
   - `pos` and `yaw` are only checked as `number`, so `pos: [1.5, 2]` passes although the format is integer cm and integer degrees.
   - Move and add `pos` are not checked for `Number.isInteger`.
   - A `set` override's `fields` may overwrite `id`, `pos` or `walk_layer`.
   - Fix: collect object and override ids into a `Set` and report duplicates; require `Number.isInteger` on `pos[0]`, `pos[1]` and `yaw`; reject `id` in `set.fields`.