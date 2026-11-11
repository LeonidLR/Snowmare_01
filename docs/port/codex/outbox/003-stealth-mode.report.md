# 003 - Stealth mode: interim handover checkpoint

Worktree: CodexTactics-interim. Branch: interim/2026-11-12. Main frozen; no push or main asset edits.

## Implemented

- STEALTH on the bottom action bar and hotkey **7** (no pre-existing EKeys::Seven binding found). Selected eligible operatives toggle together: mixed selection enables all, all-on disables. Exit leaves crouched; prone/critical/knockdown/panic/rage/carry/vault/ladder/mount cancels or refuses entry.
- Runtime grey translucent MID of existing M_FormationGhost on body/weapon materials, with original slots restored on exit. Markers / vision cones retain their visuals; temporary feedback overlays keep ownership until their flash ends. No material asset was saved.
- Config Content/Data/AI/stealth_mode.json: vision range x0.4, suspicion buildup x0.5, movement hearing x0.5; data controls fire break, opacity and grey. Patrol vision/buildup and combat perception consume the per-operative state. Hound scent and existing demask behavior unchanged. A real fired round and accepted sprint break only that operative; re-entry is explicit.
- Native full-body stealth pose over the existing AnimBP: eight Slow/Fast directional loops, shared phase, measured per-body foot-strike offsets and nominal speeds, starts/stops, turns, wall idle/movement/lean actions. Existing cover geometry and corner orders remain the source of wall/peek state. Pack crouch entry is used by both the stance montage and move hold. Fire has immediate pose priority; reload/actions fade out the stealth pose.
- Peek uses existing PeekSight / TacticalSight. Visible enemies are shown and appear as seen-now in the shared minimap feed while line of sight holds. After loss: existing last-known ghost (12 seconds in exploration); combat memory unchanged. It does not reveal enemies through other occluders or outside the peek cone/range.

## Files

- Characters: StealthModeRules.{h,cpp}, OperativeCharacterStealth.cpp, OperativeCharacter.{h,cpp}, OperativeCharacterMovement.cpp, OperativeCharacterStance.cpp, EnemyCharacterPerception.cpp.
- Animation: OperativeAnimInstanceStealth.cpp, OperativeAnimInstance.{h,cpp}, OperativeAnimInstanceShotgun.cpp; Content/Data/Weapons/weapon_anim_sets.json stealth block.
- Input/UI: CodexTacticsPlayerController.{h,cpp}, CodexTacticsPlayerControllerItems.cpp, PlayerCommands.{h,cpp}, ActionBarWidget.{h,cpp}.
- Verification: StealthModeRulesTest.cpp, StealthModeSmokeCommand.cpp, Scripts/test_map.json.
- Retarget: Scripts/Editor/setup_stealth_animation.py and ONLY Content/Characters/Animations/Stealth/{Male,Female} (124 files).

## Asset authorization / evidence

Initial automatic-review rejection was resolved by explicit primary-chat user approval: ONLY 124 new copies, 62 male + 62 female, exact Stealth/{Male,Female} destinations; user then confirmed editor closed. Generation checked zero CodexTactics editor processes, interim worktree/branch/output paths, SCC disabled to prevent temporary retargeter auto-save, target skeletons and rebaked pose error <=0.5 cm. Source pack SHA-256 verification PASS. All 62 source paths and 124 destination paths resolve. No temporary retargeter saved.

- 8ff5d194: configurable pure rules/data (4 tests).
- 1843f002: 124 approved LFS copies + reproducible script.
- Runtime commit: to be recorded after final reviewed smoke below.
- Asset evidence: Saved/Logs/setup_stealth_animation.json, StealthRetarget.log. Source pack is a read-only-use junction; do not delete through it.

## Verification so far

- Build PASS (native integration 173.85 s; later smoke-only build 19.09 s).
- 44/44 tests PASS: Characters.StealthMode, Animation.*, AI.Perception, Combat.Sight. Log Saved/Logs/StealthNative-unit.log.
- Impact map check PASS: every smoke registered/mapped.
- Six required smokes PASS with no re-run: StealthModeSmoke 57.4 s, InjuredLocomotionSmoke 76.6 s, ProneCrawlSmoke 67.6 s, CoverSwitchSmoke 59.9 s, FormationLabSmoke 201.3 s, SoloStartSmoke 56.6 s. Saved/Logs/StealthRegression-results.json.
- Stealth smoke asserts single/group toggle, actual data grey/opacity, all eight Slow clips, actual enemy perception outside/inside the scaled range, wall idle, corner lean/reveal and shared minimap row, actual ammo expenditure on fire break, material restore and sprint break.
- First rendered smoke PASS and three game-camera frames generated. Inspection found capture timing advanced to the next action in the same frame; final capture hold stages were added so wall/peek evidence is recorded before movement/fire. Updated capture pending.
- Gemini read-only review: gemini/interim-c5-prep e3c05788 (includes C5 prep 2f50e91a). Confirmed wall-phase sign bug (actor faces wall normal) fixed with existing IsShimmyForward; real wall shimmy added to smoke. Reload snap softened; post-shot aim retains priority. Final build/smoke after these fixes pending at this checkpoint.

## Review disposition / handover limits

- Gemini's proposed feedback slot-material corruption does not match the current feedback implementation: CombatFeedbackActor.cpp 131-163 changes/restores OverlayMaterial only. Slot materials and feedback overlays are handled separately; late restoration of our overlay is cleaned on Tick.
- New weapon mesh components created after stealth entry are not enrolled in the initial snapshot. A weapon switch introducing new modular parts may leave those new parts opaque. This remains a concrete follow-up for Claude; current smoke uses the initial weapon. Do not claim weapon-switch coverage.
- Female copies were generated, skeleton-validated and measured; the game smoke currently exercises the default male leader. Fast gait, acoustic end-to-end detection, reload/damage/death edge cases and direction action transitions have pure/reused-rule coverage or code inspection, not dedicated runtime assertions. Preserve these limitations in review.
- Stealth_Uncrouch_IP is retained as one of the explicitly authorized 62-copy sets. Default toggle exit intentionally stays crouched; it is not currently played for that toggle.
- Defaults were proposed to designer in chat #123. No contrary response was observed. Grenade throws are not treated as firearm shots by this feature.
- Designer requested hand-back to Claude for the next two hours (chat #141). C5/C6 implementation and the chat attention indicator were not started. This report is a handover checkpoint, not unconditional acceptance of all edge cases.

## Resumed working part 1 - late weapon visuals (checkpoint)
Runtime baseline is Claude's 90a6bf64. New weapon meshes are enrolled while stealth is active, including WeaponRoundMesh. Original material OVERRIDES (including null slots) are preserved, rather than baking the previous mesh's default material into a component override. ApplyWeaponVisual restores our overrides before replacing assets, then snapshots/reapplies stealth to the new meshes. Real pistol/magazine and RPG/round swaps plus restoration assertions added to StealthModeSmoke. Full 73-action rebuild is in progress after MSVC changed; no build or smoke PASS claimed for this checkpoint yet. Local checkpoint is saved to avoid long uncommitted work.
Router: 3 | Codex main session | checkpoint, verification pending. API lookup: 2 | gpt-6-luna | partial redo corrected (protected perception method was first called public); primary verified the corrected public Tick/StartPatrol route.

## Resumed working part 2 - verified integration coverage
Build PASS (fresh full-header incremental 384.38 s, final smoke-only build 28.15 s). Expanded male StealthModeSmoke PASS 113.1 s; female scenario PASS; 44/44 rules/animation/perception/sight tests PASS; impact map PASS. Expanded rendered smoke PASS. Three actual game-camera frames visually inspected and copied with matching SHA-256 to MAIN Saved/Orchestrator/stealth/{stealth,wall,peek_revealed_enemy}.png.
Coverage now includes all six Gemini cases: real pistol magazine/RPG round swaps/restoration; four real wall legs (left/right/forward/reverse, phase sign, wall-normal yaw, idle recovery); controlled movement-component velocities 120/300/480 through NativeUpdateAnimation (actual Slow/Fast weights and sprint contrast, not a 1200 cm navigation run); real enemy patrol Tick hearing brackets and wall occlusion; female copy paths/skeleton/measured speed and phase offsets/root drift; real knockdown/recovery and lethal health loss with state/weight/material cleanup.
Fixture corrections: direct female command needs -SquadStart=formation; the committed movement-map roster still gives all three active characters male bodies, so female scenario applies the existing optimized female body in memory and reinitializes animation. No asset or roster file saved. Initial extended-test compile errors (LeaveCover required reason, local Entry shadowing and stale header diagnostics) were corrected before successful builds. The first two female setup attempts failed before body substitution; final full female scenario PASS. Camera capture holds/real camera recentering and frozen parked movement resolve unusable sky/next-action captures.
Router: 3 | Codex main session | PASS after test-fixture corrections; 2 | gpt-6-luna | API lookup partial REDO, corrected and primary-verified. Gemini second review 1a05804e: PASS with minor recommendations. Source pack and main assets unchanged; only the three explicitly requested main Saved PNGs were written.
