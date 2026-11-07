# Diagnostic Report: #36 (No Vaulting Over Low Obstacles on L_Gen_Depot_1)

**Issue Summary:**
Operatives vault reliably over low obstacles on benchmark maps (`L_Demo01_A_FrozenLake`, `L_MovementTest`, verified by `VaultSmoke` and `BarricadeVaultBackSmoke`).
On the procedurally generated Depot map (`L_Gen_Depot_1`), operatives never vault over sandbags, concrete jersey barriers, or wooden crates, even though their heights (60–100 cm) are well within the admissible vault range (40–130 cm).

---

## 1. Exact Files, Functions, and Line Numbers (Copied from Source)

1. **`Source/CodexTactics/Private/Interactables/VaultNavigation.cpp:69-80`**
   ```cpp
bool VaultNavigation::IsVaultable(const AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}
	if (Actor->ActorHasTag(TEXT("Vault")))
	{
		return true;
	}
	if (const UVaultableBarricadeComponent* Barricade = Actor->FindComponentByClass<UVaultableBarricadeComponent>())
	{
		return Barricade->bVaultable;
	}
	return false;
}
   ```

2. **`Source/CodexTactics/Private/Characters/OperativeCharacterVault.cpp:90-95`**
   ```cpp
	if (!VaultNavigation::IsVaultable(Front.GetActor()))
	{
		return false;
	}
   ```

3. **`Source/CodexTactics/Private/Core/CodexTacticsGameMode.cpp:118-125`**
   ```cpp
	// Level objects tagged "Vault" (Godot "vault" group): the squad paths across and vaults them (barricades do it themselves).
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(VaultNavigation::VaultTag))
		{
			VaultNavigation::MakeVaultable(*It);
		}
	}
   ```

4. **`Scripts/Editor/build_levelgen_map.py:290-295`**
   ```python
    # ---- covers
    for c in layout["covers"]:
        size = c["size"]
        kind = "cylinder" if c.get("kind") == "silo" else "cube"
        specs.append({"kind": kind, "label": c["id"], "folder": "Covers", "location": [c["pos"][0], c["pos"][1], gz + size[2] / 2.0], "yaw": float(c.get("yaw", 0.0)), "pitch": 0.0,
                      "scale": [size[0] / 100.0, size[1] / 100.0, size[2] / 100.0], "tags": [c.get("kind", "cover")]})
   ```

5. **`Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json` (Covers Data)**
   ```json
    {
     "id": "COV_261",
     "pos": [ ... ],
     "size": [ 80, 80, 100 ],
     "kind": "sandbag"
    },
    {
     "id": "COV_269",
     "pos": [ ... ],
     "size": [ 60, 60, 90 ],
     "kind": "jersey"
    }
   ```

---

## 2. Ranked Hypotheses with Code Evidence

### Hypothesis 1: Generated Depot Obstacles Missing `Vault` Actor Tag (Rank 1 - Proven Root Cause)
- **Evidence:**
  In `build_levelgen_map.py:295`, cover meshes are spawned with `"tags": [c.get("kind", "cover")]`. A sandbag receives tag `"sandbag"`, a jersey barrier receives tag `"jersey"`.
  Neither actor receives the tag `"Vault"`.
  When `OperativeCharacterVault::TryVault()` executes:
  ```cpp
  if (!VaultNavigation::IsVaultable(Front.GetActor())) return false;
  ```
  `VaultNavigation::IsVaultable` checks `Actor->ActorHasTag(TEXT("Vault"))`. Because the tag is absent and no `UVaultableBarricadeComponent` is attached, it returns `false`, aborting vault execution immediately.

### Hypothesis 2: NavMesh Modifier `UNavArea_Vault` Never Created on Depot Obstacles (Rank 2 - Secondary Blocking Cause)
- **Evidence:**
  In `CodexTacticsGameMode.cpp:120`:
  ```cpp
  if (It->ActorHasTag(VaultNavigation::VaultTag))
  {
      VaultNavigation::MakeVaultable(*It);
  }
  ```
  `VaultNavigation::MakeVaultable` adds a `UNavModifierComponent` with `UNavArea_Vault` to allow pathfinding across the obstacle.
  Because the tag is missing at map startup, `MakeVaultable` is never called. The Recast NavMesh carves solid obstacles out of the navigation graph, forcing pathfinders (both squad movement and AI) to steer around low obstacles rather than approaching them along their normal.

---

## 3. How to Verify

- **Automated Smoke Tests:**
  - `powershell Scripts/test.ps1 -Smoke VaultSmoke`
  - `powershell Scripts/test.ps1 -Smoke BarricadeVaultBackSmoke`
  - `powershell Scripts/test.ps1 -Smoke LevelGenRouteSmoke`
- **Console Variables:**
  - `Codex.Debug.Vault 1`: Displays vault trace spheres and logs rejection reason.
  - `ShowFlag.Navigation 1`: Visualizes NavMesh areas in viewport (confirms whether obstacles have vault nav links or solid cutouts).
- **Log Line to Watch:**
  - `LogCodexTactics: Verbose: TryVault: Actor StaticMeshActor_%s is not vaultable (no Vault tag or component)`

---

## 4. Smallest Proposed Fix (Description Only)

Two minimal, non-conflicting options:

1. **Option A (Level Generator - Data/Builder side):**
   In `Scripts/Editor/build_levelgen_map.py:295`:
   For any cover where height (`size[2]`) is between 40 and 130 cm and depth is <= 140 cm, append `"Vault"` to its tags:
   ```python
   tags = [c.get("kind", "cover")]
   if 40 <= size[2] <= 130:
       tags.append("Vault")
   specs.append({..., "tags": tags})
   ```
2. **Option B (Engine/Runtime Fallback in `VaultNavigation.cpp`):**
   In `VaultNavigation::IsVaultable(const AActor* Actor)`:
   If tag is missing, add an automated dimension fallback: if the actor is a `AStaticMeshActor` tagged `"cover"`, `"sandbag"`, or `"jersey"` with bounding box Z between 40 cm and 130 cm, treat it as vaultable.
   And in `CodexTacticsGameMode.cpp:120`, also call `MakeVaultable` on actors with tag `"cover"` that fit the height criteria.
