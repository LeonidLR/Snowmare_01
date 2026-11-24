# 054 - Door hover: a thin frame + an action icon (follow-up of 053)

Designer 2026-11-23 after testing 053 in main: "highlight doors more neatly - a thin FRAME (outline) round the leaf, not a filled box; and a
small ICON next to it: double ARROWS = you can go through (closed, unlocked door); a KICK / boot icon = locked, kick it open."
Reference he showed: a thin teal rectangle drawn on the door leaf plane, a small white double-arrow icon just beside the leaf's lower half.

## Branch / worktree
- Base: local `main` (053 is merged there, df6842b9):
  `powershell -ExecutionPolicy Bypass -File Scripts/Tools/new_agent_worktree.ps1 -Name cx054 -Branch codex/054-door-frame-icons -From main`
- `$env:CODEX_AGENT = "codex"`. Small commits, explicit paths. No push, no merge.
- Claude has a parallel branch `claude/showcase-fixes` touching `CodexTacticsPlayerController.cpp` (one line: the stair hover call) - keep
  your edits there minimal so the merge stays trivial.

## What to build
1. Frame instead of a filled box for DOORS (windows keep 053's box for now): extend `AOpeningHighlightActor`
   (`Source/CodexTactics/{Public,Private}/Interactables/OpeningHighlightActor.*`) with `ShowFrame(const FVector& Center, const FQuat& Rotation,
   float WidthCm, float HeightCm, float BarCm, const FLinearColor& Color)`: four thin bars (cube components, same glow material, ~4 cm bars)
   forming a rectangle in the leaf plane, slightly proud of the leaf (~2 cm on the viewer's side or on both faces). `Hide()` hides all.
   The door leaf plane / size: use `ADoorActor` (`GetWidthCm()`, `GetHeightCm()`, `GetNormal()`, `GetActorLocation()` = the doorway at floor
   level, `GetSideLeafSegments` / `GetOpenLeafSegments` if useful) - a closed / locked door's leaf lies in the doorway plane.
   Colours as 053: closed blue, locked red (keep them as constants).
2. Icon: drawn on the HUD canvas (`Source/CodexTactics/Private/UI/CodexTacticsHUD.cpp`, the existing draw pass) next to the hovered door:
   project the leaf's lower-side point to the screen (`Project`), draw a small icon (~28 px) offset to the side:
   - closed door -> a double horizontal arrow (draw it with canvas lines / triangles: a shaft + two arrow heads), white;
   - locked door -> a boot / kick icon. First search the project for an existing icon texture (`Content/UI/**`, `Content/**/Icons/**`:
     grep asset names for Kick / Boot / Foot / Breach) and use it by path if one exists (reading an existing asset is fine, creating
     or editing a `.uasset` is NOT); otherwise draw a simple boot silhouette with canvas lines/triangles (a sole + an upright shaft) in red-white.
   The HUD reads the state from the controller (add a small public getter, e.g. `GetDoorHoverIcon(FVector& OutWorldPoint, bool& bOutLocked)`).
3. Keep the hover rules from 053 (no highlight when open / broken, in turn-based, with Ctrl, with an armed click preview).
4. Doc line in `docs/systems/interactables-traps-loot.md` next to 053's line.

## Checks that must pass
- `Scripts/build.ps1`; `Scripts/test.ps1 -Filter CodexTactics.Interactables` all pass.
- `WindowStairHoverSmoke` D stage (053): still passes; add checks that the DOOR hover is a frame (4 visible bars) and that the icon getter
  reports locked = true for the forced-locked door and false for the closed one.
- `Scripts/smoke_parallel.ps1 -Commands WindowStairHoverSmoke,DoorsSmoke -Jobs 2` PASS.
- `python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex` PASS.
- One screenshot if you can render (optional): a hovered closed and locked door.

## Do not
- No `.uasset` / `Content/Maps/**` / `Content/VFX/**` edits, no click behaviour changes.

Report: `docs/port/codex/outbox/054-door-frame-icons.report.md` + one chat line `--topic codex-054`.

## Addendum (designer, same day): the SAME frame + icon style for windows and stairs
5. Windows: replace 053's filled box by the same thin frame round the window opening (the opening box 053 computes, in the sill's
   plane / rotation), colours as 053 (amber whole / green broken), and a small icon beside it: a "climb through" icon (draw a simple
   hurdle: a short horizontal bar with an arc arrow over it, or an existing project icon if one exists - search first).
6. Stairs: replace the stair-up hover silhouette (`UpdateStairHover`, CodexTacticsPlayerControllerOrders.cpp; keep the old ghost behind
   a CVar `Codex.Stair.HoverStyle` 1, default 0 = frame) by a thin frame round the hovered stair flight piece (the hit component's bounds,
   oriented by the component) in a neutral white/teal, plus an ARROW icon: UP when the flight leads up to the storey above his
   (today's `URoofCutawaySubsystem::FindStairUpTarget` succeeds), DOWN when the flight is below his feet in his building (hit Z below
   his feet; FindStairUpTarget now rejects it with "a flight below his feet" on `claude/showcase-fixes` - on main, test hit Z < feet - 40
   yourself). No frame on a stair of another building.
   NOTE: the stair hover call in `CodexTacticsPlayerController.cpp` is gated by `bClickPreview` on main (so it never showed in play);
   `claude/showcase-fixes` changes that line to `UpdateStairHover(HoverOperative, IsClickPreviewArmed() ? nullptr : HoverHit)` - make the
   same change so the merge is trivial.
7. Smoke: WindowStairHoverSmoke W and S stages check the frame (4 bars visible) and the icon kind (window / stair up; add a stair-down
   probe if cheap).
8. Window icon detail (designer, same day): a WHOLE window's icon shows the BREAK-IN - a breaking pane (a cracked glass: a small square with
   jagged crack lines / shards) next to the climb arrow - combined with a SOUND hint (2-3 small arcs / sound waves beside it, like a
   speaker's waves), because breaking it is loud (15 m). A BROKEN window: the climb arrow alone, no glass, no sound waves (it is quiet).
9. PRIORITY (designer, same day): STAIRS first - "it is still very hard to get up to the second floor when I cannot see it". Do item 6
   before doors / windows, commit it on its own so Claude can merge it early. Besides the flight frame + UP arrow, mark the TARGET: a small
   flat frame (a square ~80 cm, same white/teal) on the hidden storey's landing where FindStairUpTarget sends him, so the player sees where the
   click goes even though that storey is cut away.
