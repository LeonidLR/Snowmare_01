# Operative character: stances and movement

> `AOperativeCharacter` is ONE class in ONE header with its members defined in 16 .cpp files grouped by cluster (Movement, Stance, Vault, Overlays,
> Inventory, Progression, Health, Weapons, Combat, Aim, TargetedShots, Cover, CoverEntry, CornerAim, Sniper). New code goes into the file of its cluster;
> a helper two files need goes into `OperativeCharacterInternal.h` (never two anonymous namespaces - unity builds).
>
> Key files:
> - `Source/CodexTactics/Private/Characters/OperativeCharacter*.cpp`, `Public/Characters/OperativeCharacter.h`, `OperativeCharacterInternal.h`
> - Docs: `docs/port/REFACTOR_OPERATIVE_2026-10.md` (§3 cluster map); tests `CodexTactics.Characters.OperativeUnits.*`
> - Related: [cover-and-peek](cover-and-peek.md), [combat-and-fire](combat-and-fire.md), [animation-setup](animation-setup.md)
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Squad / characters (old §5): AOperativeCharacter and its source layout

**Squad / characters**
- `Characters/OperativeCharacter` — orders, stances (capsule per stance, placeholder body until a skeletal mesh),
  sprint rules, shooting (Gemini) with cold misfire / aim penalty, inventory (matches 3, grenades 2/4, turrets /
  barricades / mines with limits 2/4/5), role (Commander / Engineer / MedicSapper) + luck (25/30/35),
  carrying (`SetCarrying`), lift limits (80 % cold, 50 % HP), placement radius 15 m. Godot `player.gd`.
  **Source layout (cleanup day 2026-10-14, `docs/port/REFACTOR_OPERATIVE_2026-10.md`):** one header, members in
  `Private/Characters/OperativeCharacter.cpp` (ctor, BeginPlay / EndPlay, Tick, identity) + `OperativeCharacter<Cluster>.cpp`:
  `Movement` (orders, formation moves, speed, traffic hold), `Stance` (stances, prone dive, capsule, placeholder), `Vault`,
  `Overlays` (selection ring, silhouette, vision cone), `Inventory` (items, deployables, field aid), `Progression`,
  `Health` (hits, knockdown hooks, death, corpse), `Weapons` (arsenal, ammo, reload, AI grenades), `Combat` (target
  selection, lines of fire, posture, `ShootAtTarget`), `Aim` (muzzle, aim point / cone, combat facing), `TargetedShots`,
  `Cover` (orders, enter / leave, shimmy, facing, events), `CoverEntry` (run-in, mesh blend, wall clearance),
  `CornerAim` (corner hold / aim / peek / snap, open shot), `Sniper`; shared helpers `OperativeCharacterInternal.h`.
  Net: `CodexTactics.Characters.OperativeUnits.*` + the per-file rules `operative-*-units` in `Scripts/test_map.json`.

### Moved barricades are vaulted both ways (game designer play-test 2026-10-22)

"I moved the barricade and set it in one place; the operative vaulted over it, but cannot vault back and walks around." Cause:
`URelocationSubsystem::SetObjectCarried(false)` made the barricade's box affect the navmesh again when it was put down, although
`VaultNavigation::MakeVaultable` keeps every shape of a vaultable object off the navmesh (only its `UNavArea_Vault` modifier marks it): the moved
barricade cut a hole into the navmesh, paths across it vanished. Fix: a vault-made object (`VaultNavigation::FindVaultNavModifier`) keeps its box
off the navmesh; its vault area is switched back on and refreshed at the new spot. Check: `BarricadeVaultBackSmoke` (two 6 m walls with a 3.2 m
gap; a barricade pushed into it by a mate with the real push task, then a freshly placed one: box off the navmesh, the path straight through both
ways ~7 m, the leader vaults over and back).
