# 053 - Hover highlight for windows and doors (replaces the window silhouette)

Designer decision 2026-11-23 (he is testing the showcase right now - speed matters, keep it small):
"No soldier silhouette. Just highlight the window I want to climb through. Highlight the door I want to open: a door that is only
CLOSED - blue; a LOCKED door - red."

## Branch / worktree
- Base: `claude/bgen-test` (head 7071c941), NOT main:
  `powershell -ExecutionPolicy Bypass -File Scripts/Tools/new_agent_worktree.ps1 -Name cx053 -Branch codex/053-hover-highlight -From claude/bgen-test`
- `$env:CODEX_AGENT = "codex"`. Small commits, explicit paths. No push, no merge (Claude merges).

## What to build
1. `AOpeningHighlightActor` (new, `Source/CodexTactics/Public/Interactables/OpeningHighlightActor.h` + `Private/Interactables/OpeningHighlightActor.cpp`):
   one `UStaticMeshComponent` with `/Engine/BasicShapes/Cube.Cube`, no collision, no shadows, not affecting navigation, hidden by default.
   Material: a dynamic instance of `/Game/VFX/Materials/M_CombatFeedback.M_CombatFeedback` with params `Color` (vector) and
   `Intensity` (scalar) - copy the way `ACombatFeedbackActor::MakeGlow` (Private/Combat/CombatFeedbackActor.cpp) creates it.
   API: `void ShowBox(const FBox& WorldBox, const FQuat& Rotation, const FLinearColor& Color)` (cube scaled to the box; the cube mesh
   is 100 cm), `void Hide()`, `bool IsShown() const`. Low intensity (it must read as a glow, not a solid block; ~0.6, tune by eye in
   code constants at the top of the .cpp).
2. Window hover (`ACodexTacticsPlayerController::UpdateWindowHover`, Private/Core/CodexTacticsPlayerControllerOrders.cpp ~line 932):
   keep the exact same "wanted" test (sill found, target found), but show the highlight box over the WINDOW OPENING instead of the
   climbing silhouette: the opening = the sill's local box (as already computed there: `CalculateComponentsBoundingBoxInLocalSpace`,
   `GetActorTransform`) extended up from the sill top by ~140 cm (same numbers as the `Opening` box in
   `WindowBreakage::FindWindowSillAt`, Private/Interactables/WindowBreakage.cpp), rotated with the sill actor. Colour: warm amber
   (whole window) / pale green (window tagged `window_broken`). The old silhouette stays behind a new console variable
   `Codex.Window.HoverStyle` (0 = highlight, DEFAULT; 1 = the old climbing silhouette, unchanged code path). Hide the highlight
   wherever the code hides `WindowGhost` today.
3. Door hover (new `bool UpdateDoorHover(AOperativeCharacter* Operative, const FHitResult* Hit)` next to UpdateWindowHover; call it
   where UpdateWindowHover is called in `CodexTacticsPlayerController.cpp` ~line 420 with the same arguments):
   `UDoorSubsystem::FindClickedDoor(*Hit)` (doors active only: `Doors->IsActive()`); state from `ADoorActor::GetDoorState()`:
   `EDoorLeafState::Closed` -> BLUE, `EDoorLeafState::Locked` -> RED, anything else (open / broken) -> no highlight. Box = the door
   actor's leaf bounds (`GetComponentsBoundingBox(true)` of the ADoorActor, inflated ~5 cm), axis-aligned is fine. Hide on no hit,
   in turn-based (`GetActiveTurnBased()`), with the attack modifier down, and when the click preview is showing (same guards as the
   window hover).
4. A header comment on each new piece naming the designer decision; doc line in `docs/systems/interactables-traps-loot.md`
   (windows / doors section): "hover highlight: window amber / broken green; door closed blue, locked red; Codex.Window.HoverStyle 1 =
   old silhouette".

## Checks that must pass
- `Scripts/build.ps1`
- `Scripts/test.ps1 -Filter CodexTactics.Interactables` (all pass)
- Extend `Source/CodexTacticsDev/Private/Debug/WindowStairHoverSmokeCommand.cpp`: in the W (window) stage, instead of requiring the
  silhouette, require the window highlight shown (add a public getter on the controller, e.g. `IsWindowHighlightShown()`), and add a
  D stage: put the leader in front of a closed inner door (any `ADoorActor` in state Closed), a cursor ray onto its leaf -> the door
  highlight shows BLUE; `ForceState(EDoorLeafState::Locked)` on it -> RED; restore its state. Then
  `Scripts/smoke_parallel.ps1 -Commands WindowStairHoverSmoke,DoorsSmoke -Jobs 2` -> both PASS (map from Scripts/test_map.json).
- `python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex` PASS.

## Do not
- Do not touch any `.uasset` / `Content/Maps/**` / `Content/VFX/**` (use the existing M_CombatFeedback as it is).
- Do not change the click behaviour (window order, door clicks) - hover visuals only.

Report: `docs/port/codex/outbox/053-hover-highlight.report.md` + one chat line `--topic codex-053`.
