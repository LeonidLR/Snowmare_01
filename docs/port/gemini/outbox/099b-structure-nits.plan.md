# Plan: 099b Structure Nits

1. V3 Dangling check: in `validate.ts`, report `V3_dangling_reference` for unknown IDs in `supports` or `supported_by` against `elements`.
2. Stairs needs: add `needs?: number` to `StructuredStair` in `types.ts`; remove `(s as any).needs` cast in `validate.ts:105`.
3. isSlabPanel validation: reject `NaN` and `Infinity` using `Number.isFinite()` on `rect` coordinates and `needs` in `types.ts:116`.
4. Unit tests: add tests in `structure.test.ts` for dangling `supports`, dangling `supported_by`, stair `needs` type safety, and `NaN`/`Infinity` rejection in `isSlabPanel`.
5. Run tests: execute `node --test src/levelgen/structure/structure.test.ts` to confirm 100% pass; run `gemini_gate.ps1 -Task 099b`.
