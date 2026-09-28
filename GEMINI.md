# Agent entry point — CodexTactics

Before doing anything in this repository read, in order:

1. `docs/port/HANDOFF.md` — current state, system map, environment traps, how to verify, next steps, change log.
2. `CLAUDE.md` — project rules (they apply to every agent, not only Claude).
3. `docs/port/TANDEM.md` — claim files there before editing; one builder at a time.

Key rules: the Godot project `Documents/Codex/godot-test-01` is read-only; Godot code values win; verify with
`Scripts/verify_all.ps1` (Unreal Editor closed) before committing; update HANDOFF.md §5 / §8 / §10, PORT_MATRIX.md
and TANDEM.md with every committed system.
