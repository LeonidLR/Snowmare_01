# Agent entry point — CodexTactics (Gemini)

**Your role (user decision 2026-10-01): optimization and performance, shaders and materials, VFX, rendering / scalability
settings, profiling.** Claude writes gameplay code, logic, AI, UI and animation setup. The ownership table, the request flow
between you and the shared build lock are in `docs/port/TANDEM.md` — they are binding.

Before doing anything in this repository read, in order:

1. `docs/port/TANDEM.md` — roles, who owns which files / assets, «In progress» claims, «Requests».
2. `docs/port/HANDOFF.md` — current state, system map, environment traps, how to verify, change log.
3. `CLAUDE.md` — project rules (they apply to every agent, not only Claude).

Key rules:

- In every shell: `$env:CODEX_AGENT = "gemini"`. `Scripts/build.ps1`, `test.ps1`, `smoke.ps1`, `verify_all.ps1` take the shared
  lock `Saved/agent.lock` and wait while Claude holds it; wrap other engine work (editor Python, profiling runs) in
  `Scripts/agent_lock.ps1 -Acquire -Purpose "…"` / `-Release`.
- Gameplay C++ (`Source/CodexTactics/**`), characters / AnimBPs and data assets are Claude's: propose changes there in TANDEM
  «Requests» with the measurement (stat unit / Insights numbers), do not edit them.
- Claim every `.uasset` you edit in TANDEM «In progress» (they cannot be merged).
- Your perf smokes live in `Source/CodexTactics/Private/Debug/Perf*SmokeCommand.cpp` (yours) and are listed in `Scripts/verify_all.ps1`
  `$Smokes` (the only line of that file you edit); it must be ALL GREEN before you commit; commit only your own
  files, prefixes `perf|shader|vfx(...)`.
- The Godot project `Documents/Codex/godot-test-01` is read-only. Never commit the user's map, packs, DefaultEditor.ini or
  DefaultInput.ini.
- Update HANDOFF.md §10 and TANDEM.md Log with every commit.
