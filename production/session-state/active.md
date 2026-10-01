# Session State — CodexTactics (UE 5.8 port)

<!-- STATUS -->
Epic: Godot → UE port (vertical slice per docs/port/REFERENCE_PLAYTHROUGH_01.md)
Feature: user art wired (operative / enemy AnimBPs), turn-based attack mode and walk animation
Task: prone animation from the user's Crawl_MocapAnimPack done; next = more animation packs from the user (standing hit / death / grenade / cold / vault)
<!-- /STATUS -->

**The full handoff (state, system map, traps, next steps, change log) is `docs/port/HANDOFF.md`. Read it first.**

## Mode (user, 2026-09-28)
- Full port with identical behaviour; work autonomously through PORT_MATRIX / HANDOFF §8, commit each verified
  system, ask only on real design forks. Godot project is read-only.
- Gemini may take over at any time: keep HANDOFF.md, PORT_MATRIX.md and TANDEM.md current with every commit.
- The user may close / reopen the Unreal Editor for builds (graceful close only).

## Last verified
2026-10-01: 145 automation tests, 57 smokes ALL GREEN (`verify_all.ps1 -SkipBuild`, parallel x3) after the combat movement fixes (CombatMoveSmoke added).

## Open questions
- none (L_MovementTest re-laid by the user is kept; smokes are layout-relative)
