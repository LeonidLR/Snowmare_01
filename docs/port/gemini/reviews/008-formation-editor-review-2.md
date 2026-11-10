**(1) readBody / 400 on bad JSON: FIXED.** readBody now rejects on a JSON parse error, and the POST handler wraps it in try/catch and returns `send(res, 400, {error:'invalid JSON'})`.

**(2) Writer drops unknown keys: PARTLY.** The extra keys on shapes (both the formations and custom branches) are now emitted after `members`. Evidence: the new loops over `Object.keys(shape)`. The loop that appends extra keys to custom slots is on the shape object only, and the custom slot's own `name` and `extra_custom_metadata` handling is not visible in the diff. The test expects `extra_custom_metadata` at the slot level, but the diff only adds keys at the shape level. Individual member objects with unknown keys are also not shown as preserved, and `inlineJson` is assumed to exist. The slot-level claim is unverified.

**(3) Roles must equal rolesFor(count), commander at index 0: FIXED.** Evidence: the `i === 0 && m.role !== 'commander'` check, plus the set comparison against `expectedRoles` built from `rolesFor`. Minor weakness: the set comparison is skipped when `rolesSeen.size === 0`, but the bad-role errors already cover that case.

**(4) Pair shape applied to any 2-member formation: FIXED.** The pair shape is used only when the formation is `'pair'`, or when it is `'column'` with a saved pair shape. Evidence: the rewritten condition in `getSavedFormationShape`, and the test showing wedge/2 returns undefined.

**(5) Custom validation: FIXED.** Evidence in the custom loop:
- A null member is handled by the `!m || typeof m !== 'object'` guard.
- Arc is checked as an integer between 10 and 360.
- The `CUSTOM_MAX_RADIUS` check is added.
- Duplicate squads are caught with `squadSeen`.

**(6) Atomic write: FIXED.** The temp name uses `pid` plus `randomUUID`. A try/catch removes the temp file on failure, and fsync is added before the rename. Parent-directory fsync is not done, which is acceptable.

**New defects introduced:**
1. **Pair/column fallback is inconsistent.** If `formation === 'pair'` and no pair/2 shape exists, the code falls through to `formations['pair'][...]`, which is the same lookup and gives undefined. That is harmless, but the pair-to-column fallback now only works in one direction. A saved column/2 shape is also ignored for column when a pair shape exists, because pair takes precedence. That is the old behavior, but it now silently overrides an explicitly saved column/2 shape.
2. **Extra keys can corrupt the output or leak into validation.** Extra keys are written verbatim, and a shape key named, for example, `members` is skipped. The writer does not escape or validate unknown keys, and `undefined` values are skipped while `null` values are written. The validator does not check these keys either.
3. **Exporting `readBody` and the test's dynamic import of `server/movementApi.ts` pulls the server module into unit tests.** Importing it may have side effects or dependencies, which makes the test fragile. The mock request also fires `data` and `end` synchronously at registration, so the test does not model the real event flow.