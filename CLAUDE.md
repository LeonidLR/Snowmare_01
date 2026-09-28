# CodexTactics — Operation: Cold Silence (Unreal Engine 5.8 port)

Tactical arctic survival RPG, Gorky 17 tribute. This repo is the **UE 5.8.2 C++ port** of the
Godot 4.7 project. The Godot version keeps evolving in parallel (Gemini) and stays the reference.

## Source of truth (read, never copy)

Godot repo root: `C:/Users/Zephyrus15Duo/Documents/Codex/godot-test-01/`

| What | Where (Godot repo) |
|---|---|
| Port blueprint | `docs/CLAUDE_OPUS_UE58_HANDOFF.md` |
| Architecture / data contracts / combat spec | `docs/ARCHITECTURE.md`, `docs/DATA_CONTRACTS.md`, `docs/GAME_DESIGN_SPEC.md` |
| ADRs | `docs/architecture/adr-*.md` |
| GDDs | `design/gdd/*.md` |
| Level spec / art bible | `design/levels/`, `design/art/art-bible.md` |
| Balance data | `data/configs/**/*.json`, `resources/**/*.tres` |
| Reference behaviour + tests | `Scripts/`, `Scenes/movements/`, `tests/` |

Balance values are imported from the Godot JSON by script (Phase 2) — never hand-typed into C++.
If Godot behaviour and a GDD disagree, stop and ask the user.

UE skills: `C:/Users/Zephyrus15Duo/Documents/Codex/godot-test-01/.agents/skills/ue-*` and `tactical-turn-based-combat`.
Source art: `C:/Users/Zephyrus15Duo/Documents/Codex/ASSETS/` (import FBX/glTF, not Godot `.res`).

## Tech decisions

- Engine: UE 5.8.2 (`C:/Program Files/Epic Games/UE_5.8`), C++ first, Blueprints only for content/tuning.
- **No GAS.** Plain C++ components (`UTacticalUnitComponent`, `UColdSurvivalComponent`, ...).
- Event bus: `UGameInstanceSubsystem` + dynamic multicast delegates (Godot `EventBus.gd`).
- Plugins: EnhancedInput, Niagara, ModularGameplay, StateTree, ProceduralMeshComponent. Ultra Dynamic Sky/Weather in Phase 6.
- Epic coding standard (A/U/F/E/I prefixes, IWYU, `TObjectPtr`), doc comments on public API,
  each class header names its Godot reference file.

## Modules

- `CodexTactics` (Runtime) — all gameplay code. `Public/` + `Private/`, grouped by system folder
  (`Core/`, `Combat/`, `Survival/`, `Characters/`, `Data/`, `Subsystems/`, `Camera/`).
- `CodexTacticsTests` (DeveloperTool, editor only) — Automation tests, `CodexTactics.<System>.<Feature>.<Case>`.

## Build & test (no editor needed)

```
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1
powershell -ExecutionPolicy Bypass -File Scripts/test.ps1            # all CodexTactics.* tests
powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Filter CodexTactics.Grid
```

Pure-logic systems get **parity tests** mirroring the Godot tests (same inputs → same outputs).

## Workflow

- Phase plan and status: `docs/port/PORT_MATRIX.md`. Session checkpoint: `production/session-state/active.md` — read it first.
- Ask before writing files; show the changeset first. No commits without explicit user instruction.
- Commits: Conventional Commits, body references the phase / Godot file.
- A phase is done only when build + tests pass.
