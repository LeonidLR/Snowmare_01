1. **Endpoint: unguarded body parse (`movementApi.ts`, POST branch).** `const b = await readBody(req);` sits outside any try/catch. A malformed JSON body rejects the async handler, so the request hangs or raises an unhandled rejection instead of returning 400. Fix: wrap it as `let b; try { b = await readBody(req); } catch { return send(res, 400, { error: 'invalid JSON' }); }`.

2. **Writer drops unknown keys (`formatFormationShapesJson`).** Only `members` is emitted for each count shape and each custom slot shape. Extra keys on a `FormationShape` or `CustomShapeDef` (allowed by `[key: string]: unknown`) are silently lost on save, and the roundtrip test only checks member-level and root-level keys. Fix: after `"members"`, emit every other key of `shape` via `inlineJson`, for both formations and custom.

3. **Validator doesn't check roles against the member count (`validateFormationShapes`).** Only duplicate roles and a commander at (0,0) are checked. A count-3 shape with `rear` and `left` but no `right`, or with no commander, validates fine, and the simulator then maps roles by letter in file order. Fix: require `new Set(roles)` to equal `new Set(rolesFor(count).map(r => ROLE_LETTER_TO_NAME[r]))`, and require the commander to be at index 0.

4. **Simulator uses the `pair` shape for the wrong formation (`getSavedFormationShape`).** For `memberCount === 2` it always returns `formations.pair['2']` when present. A 2-member wedge, line or diamond then gets the pair shape. Fix: use the pair key only when `formation === 'pair'`, or when the formation is the one the rules map pair to (`column`), and otherwise use `formations[formation]`.

5. **Custom-shape validation is incomplete.**
   - `m` is not checked for null or non-object, so `m.squad` throws a TypeError and gives a 500 instead of a 400.
   - `arc` is never validated, so a non-integer arc is accepted.
   - Radius is not checked against `CUSTOM_MAX_RADIUS`, and duplicate squad indices are accepted.
   - Fix: add the `typeof m !== 'object'` guard, the same `arc` integer 10..360 check, the radius check and a squad uniqueness check.

6. **Atomic write is fragile (`writeFormationShapesFile`).** The temp name is `${filePath}.tmp.${Date.now()}`, so two writes in the same millisecond collide. A failed write or rename also leaves the temp file behind, and there is no fsync before the rename. Fix: use a unique name (`${process.pid}.${randomUUID()}`), fsync the file, and wrap the write and rename in try/catch with `fs.rmSync(temp, { force: true })` on failure.