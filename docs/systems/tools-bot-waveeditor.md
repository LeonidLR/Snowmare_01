# Tools: editor scripts, playtest bot, Wave Editor, Jev

> Editor scripts (`Scripts/Editor/`) are idempotent commandlets. The Wave Editor is `Tools/WaveEditor` (run via `Scripts/run_wave_editor.bat`);
> the playtest bot is `UPlaytestBotSubsystem` (development builds only) driven by `Scripts/bot_run.ps1`; Jev tools are in `Scripts/Tools/`.
> Most history of these tools is in `docs/port/CHANGELOG.md` (search for «Wave Editor», «playtest bot», «Jev»).
>
> Key files:
> - `Scripts/Editor/*.py`, `Tools/WaveEditor`, `Scripts/bot_run.ps1`, `Scripts/run_simulations.bat`, `Scripts/Tools/typesafe_triage.py`, `jev_digest.py`, `find_unity_clashes.py`
> - `Source/CodexTactics/Private/Bot/`, `Private/Telemetry/`; data `Content/Data/LevelJson/*.json` (edited with the Wave Editor)
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Editor scripts (old §5)

**Editor scripts** (`Scripts/Editor/`, run with `UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path>`):
`create_movement_test_map.py` (regenerates the test map — **overwrites manual edits, avoid**),
`add_level_objects_to_movement_test.py` (adds barrel / barricades / mines / supply crates by label, keeps edits;
Python names drop the `b` prefix of bools: `bTrapped` → `trapped`),
`create_operative_blueprint.py`, `import_operative_assets.py` (Explorer glb merged + root bone baked, M16, 60 FBX
clips), `setup_operative_animation.py` (ABP + BP wiring, M16 offset from Godot). All are idempotent.

## Where the history is (entry titles in docs/port/CHANGELOG.md)

- Playtest bot + run telemetry (2026-10-04)
- Jev AI coach + Gemini's marksman / bot work finished; Marksman kiting limit + bot storms marksmen + Jev AI coach
- Jev tooling made real (user request: use Jev maximally to save agent tokens)
- Wave Editor tab «Скрытность и бой» (Stealth & Combat), editor-only, no C++ change; User reports 2026-10-04 + Wave Editor weapons
- Stealth playtest bot + coach proposals (user plan 2026-10-07, old §8 item 0)
- Wave Editor: level settings panels (user request 2026-10-11)
- Cleanup task 4, batch 4 - bot gate (the playtest bot only where it belongs, 2026-10-16)

Old HANDOFF §8 item 0 «Bot training» (approve / reject `Saved/Coach/stealth_proposal.md`) is in [NEXT_STEPS](../port/NEXT_STEPS.md).

