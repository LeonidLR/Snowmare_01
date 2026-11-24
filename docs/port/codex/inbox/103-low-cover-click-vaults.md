# Codex 103-low-cover-click-vaults (URGENT, before 070)

Designer bug 2026-11-24 ~15:30, playing main (origin/main + local 72b1bbc5) on /Game/Maps/Generated/L_Gen_Depot_1_X6:
every low VAULTABLE cover can no longer be used as cover. Ordering the squad to crouch behind it: the cover ghosts show them
crouched in the cover slot, but on the click they run to it and climb over it instead of taking cover.
Expected (designer decision 2026-11-17, memory designer-decisions-2026-11-17): click on the cover FACE = take cover,
click on the TOP = walk-climb, double-click = run-climb.

Find: which commit changed it (suspects: today's 074 / 076 / 077 merges, mantling / vault click routing, "[Vault] 73 generated
low covers made vaultable" in UVault*, cursor pick of cover vs vault). Reproduce headless (an existing smoke such as
MantleClickSmoke / CoverSmoke / LowCoverMoveLakeSmoke on X6, or a new small check). Fix so a cover order on a vaultable low cover
takes cover; the vault stays on the top click / double click.
Done = root cause + fix on codex/103-low-cover-click, build, -Smart, the cover + mantle-click smokes on the worker PASS.
Rules: docs/port/TANDEM.md 'WORK PROCESS'. Report in outbox + chat FINISH.

Designer answer 2026-11-24 15:40: he clicks the FRONT / END FACE of the cover, never the top - and they still climb over.
