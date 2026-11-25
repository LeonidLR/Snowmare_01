# Gemini 130b — Fix `Weapons.Schema.NoLegacyReads` in `OperativeCharacterHealth.cpp`

Model: `Hydrogen`
Branch: `gemini/130b-no-legacy-reads` (from `origin/main` `3c3159e6`)

## Root Cause & Fix

- `CodexTactics.Weapons.Schema.NoLegacyReads` (`WeaponSchemaTest.cpp`) failed on two legacy `MaxClipSize` reads added in `OperativeCharacterHealth.cpp` (`SpawnRemainsLoot`, lines 448 & 455) by Task 130.
- Updated both reads in `Source/CodexTactics/Private/Characters/OperativeCharacterHealth.cpp` to read the schema accessor `->Stats.Ammo.MagazineSize` (`CurrentWeapon->Stats.Ammo.MagazineSize` and `Weapon->Stats.Ammo.MagazineSize`).

## Verification

| Check | Scope / Command | Result |
|---|---|---|
| Dispatched build | `machine_dispatch.py build --lane CodexTactics-130b` (`worker`) | `Result: Succeeded` (`25.06 s`) |
| Impact map | `Scripts/test.ps1 -CheckMap` | `Impact map OK` |
| Unit tests | `Scripts/test.ps1 -Filter "CodexTactics.Weapons+CodexTactics.Items" -SkipSmokes` | `43 tests, 0 failed` (`CodexTactics.Weapons.Schema.NoLegacyReads` PASS) |
| Smoke | `machine_dispatch.py smoke --lane CodexTactics-130b --smokes CorpseSearchSmoke --jobs 1` (`worker`) | `PASS` (`41.7 s`) |
