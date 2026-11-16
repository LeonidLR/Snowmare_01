# 026 - First-start black screen -> a loading overlay with progress (Claude, 2026-11-16, board 073) - MEDIUM
Why: your 025 diagnosis: the preload takes 76 s cold / 43 s warm and WeatherPresenterActor.cpp:468 keeps the screen black through
"Preparing" (even with the pause off). The designer sees a black screen with only the HUD and thinks the game hung.
Branch codex/026-loading-overlay (from main), worktree CodexTactics-agents.
Do (smallest fix, no gameplay change):
1. While UGameplayPreloadSubsystem is Preparing, show a full-screen overlay drawn by C++ (the existing HUD canvas / Slate - NO new .uasset):
   "Загрузка уровня… NN %" (+ the current phase name if the subsystem knows it) on a dark background, hidden the moment the preload
   is released (Ready / Failed). Progress = done / total paths from the subsystem (add a small getter if missing).
2. Keep WeatherPresenterActor's black fade only if it is needed for the weather start; otherwise let the overlay replace it - explain
   which in the report with file:line. Do not change weather logic beyond that one hold.
3. The overlay must not appear in smokes that run with -CodexPreload=0, and must not block input after release.
4. A unit test for the progress math / state -> visible rule (Source/CodexTacticsTests), and run HitchProbeSmoke + MainMenuSmoke +
   2 level smokes of your choice (paste result lines). Optional: a screenshot of the overlay from a -game run into
   docs/port/screenshots/026-loading-overlay.png.
Allowed paths: GameplayPreloadSubsystem.*, WeatherPresenterActor.cpp (the hold only), the HUD class that draws the overlay, the new test,
report docs/port/codex/outbox/026-loading-overlay.report.md. No .uasset, no Config/*.ini, no Content/Maps/**. 10 % limit rule.
