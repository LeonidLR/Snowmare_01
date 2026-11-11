# Interim hand-back report — 2026-11-11 through Saturday 2026-11-14

Integration worktree: `CodexTactics-interim`; branch: `interim/2026-11-12`.
Frozen base: tag `before-interim-2026-11-11`, commit `8b0bb579`.
Handover documentation on integration before the first own commit: `56e1d1f2`, `11065194`, `a1100f29` (authorized API tools).
Main is frozen. No push, main build, scheduled-task change or main asset save is authorized.

## Completed / integrated work

Tasks 001 and 002 were merged by Claude before the frozen base; their reports remain in the outbox.

### Queue 2 — TS formation pair rule

Own task commit on `interim/2026-11-12`: `fix(formation): align saved pair lookup with runtime` (its hash will be listed in the next report update). The designer requested own work only on this integration branch; no other worktree/branch was checked out.
- Exact saved shapes now win. `pair/2` is a fallback only for 2-member wedge/diamond; column/line/echelons never consume it.
- Updated `shapes.test.ts` for exact column/wedge/diamond precedence, missing pair-ineligible shapes, mixed counts and absent file; removed the obsolete TS/C++ mismatch note in FORMATION_CONTRACT 19.2.
- `node --test src/movement/shapes.test.ts src/movement/shapesGolden.test.ts`: 18/18 PASS. Saved-shape golden unchanged and current.
- `npm run typecheck`: PASS. Used the existing main node_modules through a read-only-use junction; no dependency installation or main code changes.
- Interim baseline build Succeeded (491.39 s); Animation + Formation.Shapes + Formation.Parity.SavedShapes: 25/25 PASS.
- Coordination: Gemini assigned in 113/117, accepted 119 then asked whether to proceed in 120 after crossed standby messages. Clarified in 121/122 and supplied the exact tested patch. Asked for an actual branch/worktree/commit in 124; none was present in the shared Git refs at the time of this commit. Preserve any later Gemini contribution for review; do not duplicate or blindly merge it.

## Queue and current status

1. Props C4 TypeScript: already merged before takeover; no repeat work.
2. Formation pair lookup: own verified patch committed after coordination above; awaiting any additional Gemini review/contribution. No Gemini merge claimed.
3. Priority stealth mode, inbox 003: independent pure rules/data prepared and built; four new StealthMode tests PASS. Actor state, gray runtime visuals, fire/sprint breaks, patrol/combat perception and STEALTH/7 input are being compiled (not yet verified or committed). Guarded source-inspection/retarget script prepared; no assets generated yet. Source pack accessible through a read-only-use junction. Designer defaults proposed in chat 123.
4. Props C5 static validator: pending after stealth.
5. Props C6 runtime spawner: pending after C5.
6. Night trainer review: read-only morning review to Gemini when results are available; main binaries, outputs and scheduler remain untouched.

## Operating decisions / limits

- The designer explicitly requested own work only on `interim/2026-11-12` in `CodexTactics-interim`; own changes stay there. Gemini task branches are reviewed before any authorized `--no-ff` integration into this branch.
- Build/test/play-test only in the interim worktree. Run the Codex TypeSafe audit before each merge and before commits as required by the mailbox.
- No protected maps, configs, VFX, Depot designer JSON, CLAUDE.md or source content packs will be changed.
- Stealth task 003 has a narrowly described exception for newly retargeted `/Game/Characters/Animations/Stealth/{Male,Female}` copies, only with the designer editor closed. No asset writes have occurred; guard that exact destination and the editor process state before any generation.
- The original source pack remains read-only; no editor UI use.

## Verification / failures / open questions

Verification is listed per task above. No implementation failures recorded so far; runtime stealth is still incomplete and must not be described as complete. Anthropic API spend by Codex: $0 (not used); TypeSafe pre-commit audits are recorded separately. Chat shells now set PYTHONIOENCODING=utf-8 to avoid the older agent-chat reader failing on Russian text.
This is a living hand-back report and will be updated before Saturday.
