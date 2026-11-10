# Diagnostic Report: Bug #36 (Operatives Do Not Vault Over Low Obstacles on Depot)

**Issue Summary:**
While the vaulting mechanic (`VaultNavigation`, `OperativeCharacterVault`, `VaultRules`) functions reliably on handcrafted test maps (such as `L_Demo01_A_FrozenLake` and `L_MovementTest` as verified by `VaultSmoke`), operatives completely fail to vault or climb over low obstacles, concrete barriers, and crates on the procedurally generated Depot map (`L_Gen_Depot_1`).

---

## 1. Exact Files, Functions, and Line Numbers

1. **`Source/CodexTactics/Private/Interactables/VaultNavigation.cpp`**
   - **Function:** `VaultNavigation::IsVaultable(const AActor* Actor)`
   - **Lines:** `69–80`
   - *Detail:* Requires the actor to have the actor tag `Vault` or have a `UVaultableBarricadeComponent` with `bVaultable == true`:
     ```cpp
     if (Actor->ActorHasTag(TEXT("Vault"))) return true;
     if (const UVaultableBarricadeComponent* Comp = Actor->FindComponentByClass<UVaultableBarricadeComponent>())
     {
         return Comp->bVaultable;
     }
     return false;
     ```
   - **Function:** `VaultNavigation::MakeVaultable(AActor* Actor, ...)`
   - **Lines:** `161–180`
   - *Detail:* Attaches `UNavArea_Vault` to obstacle collision components to create navigable navmesh links across the obstacle.

2. **`Source/CodexTactics/Private/Characters/OperativeCharacterVault.cpp`**
   - **Function:** `AOperativeCharacter::UpdateVaultTrigger(float DeltaTime)`
   - **Lines:** `25–53`
   - **Function:** `AOperativeCharacter::TryVault()`
   - **Lines:** `85–110` (specifically line `92`)
   - *Detail:* Explicitly gates vault attempts:
     ```cpp
     if (!VaultNavigation::IsVaultable(Front.GetActor()))
     {
         return false;
     }
     ```
     If `IsVaultable` returns false, vault evaluation aborts immediately.

3. **`Source/CodexTactics/Private/Core/CodexTacticsGameMode.cpp`**
   - **Lines:** `116–122`
   - *Detail:* In `InitGame` / `BeginPlay`, `MakeVaultable` is called only for actors pre-tagged with `Vault`:
     ```cpp
     for (TActorIterator<AActor> It(GetWorld()); It; ++It)
     {
         if (It->ActorHasTag(TEXT("Vault")))
         {
             VaultNavigation::MakeVaultable(*It);
         }
     }
     ```

4. **`Source/CodexTactics/Public/Characters/VaultRules.h`** & **`VaultRules.cpp`**
   - **Lines:** `VaultRules.h:35–65`
   - *Detail:* Defines strict geometric constraints:
     - `ProbeHeight = 45.f;` (knee height forward raycast)
     - `MinHeight = 40.f;`
     - `MaxHeight = 130.f;` (obstacle top must be <= 130 cm)
     - `ApproachDistance = 120.f;`
     - `LandingDistance = 140.f;` (obstacle thickness/depth <= 140 cm)

5. **Procedural Depot Map Generation (`Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json` & `Scripts/Editor/build_levelgen_map.py`)**
   - *Detail:* Spawns modular greybox meshes (concrete blocks, low walls, crates). These actors are spawned as generic `AStaticMeshActor` instances without the `Vault` tag and without attaching `UVaultableBarricadeComponent`.

---

## 2. Ranked Hypotheses with Code Evidence

### Hypothesis 1: Procedurally Generated Obstacles on Depot Lack the `Vault` Tag (Rank 1 - Primary Cause)
- **Evidence:** In `OperativeCharacterVault.cpp:92`:
  ```cpp
  if (!VaultNavigation::IsVaultable(Front.GetActor())) return false;
  ```
  And in `VaultNavigation.cpp:73`:
  ```cpp
  if (Actor->ActorHasTag(TEXT("Vault"))) return true;
  ```
  On handcrafted levels, designers manually added the actor tag `Vault` or placed `BP_Barricade` actors containing `UVaultableBarricadeComponent`.
  On Depot (`L_Gen_Depot_1`), low obstacles are generated procedurally by `build_levelgen_map.py` from `gen_ref_01_02_depot_1.json`. The generator assigns collision but does not add the tag `Vault` to low obstacle meshes. When an operative runs up to a low barrier on Depot, `IsVaultable` returns `false`, aborting `TryVault()` before geometric traces are even conducted.

### Hypothesis 2: NavMesh Lacks `UNavArea_Vault` Nav Links (Rank 2)
- **Evidence:** In `CodexTacticsGameMode.cpp:116–122`, `VaultNavigation::MakeVaultable` is only executed on actors that already possess the `Vault` tag. Because procedural obstacles lack the tag, `MakeVaultable` is never called for them.
  As a result, low obstacles carve empty collision holes in the Recast NavMesh instead of generating traversable `UNavArea_Vault` navmesh zones. Both the AI pathfinder and squad movement components route paths around the obstacles rather than across them. An operative will never willingly move directly into the barrier normal to reach the 120 cm `ApproachDistance` trigger.

### Hypothesis 3: Obstacle Geometry Violates `VaultRules` Dimensions (Rank 3)
- **Evidence:** `VaultRules.h` defines `MinHeight = 40.f`, `MaxHeight = 130.f`, and `LandingDistance = 140.f`.
  Certain procedural greybox assets on Depot (such as doubled industrial crates, stacked cargo boxes, or industrial pipes) have heights of 140–160 cm or widths exceeding 150 cm. Even if an actor were tagged, `VaultRules::EvaluateVault` raycasts would fail to locate a valid clear landing surface within `LandingDistance` (140 cm).

---

## 3. How to Verify Each Hypothesis

### Automated Smoke Tests (`Scripts/test_map.json`)
- **`VaultSmoke`**:
  - Command: `powershell Scripts/test.ps1 -Smoke VaultSmoke`
  - Validates baseline vault navigation, approach detection, and vault animation transitions on benchmark barricades.
- **`BarricadeVaultBackSmoke`**:
  - Command: `powershell Scripts/test.ps1 -Smoke BarricadeVaultBackSmoke`
  - Validates vaulting across barricades in both directions and verifies `UNavArea_Vault` nav links.
- **`LevelGenRouteSmoke`**:
  - Command: `powershell Scripts/test.ps1 -Smoke LevelGenRouteSmoke`
  - Executes bot traversal along `gen_ref_01_02_depot_1_R_main` on Depot.

### Console Variables and Debug Logging
- **Console Variables:**
  - `Codex.Debug.Vault 1`: Visualizes vault probes (knee probe, top probe, landing probe) in green/red and logs rejection reasons.
  - `ShowFlag.Navigation 1`: Visualizes NavMesh in viewport. Checks whether obstacles have purple/custom vault nav areas or solid navmesh holes.
- **Log Signatures to Watch:**
  - `LogCodexTactics: Verbose: TryVault: Actor StaticMeshActor_... has no Vault tag / component`
  - `LogCodexTactics: Verbose: VaultRules: Height %.1f outside [40, 130] range`
  - `LogCodexTactics: Verbose: VaultRules: No landing found within 140 cm`

---

## 4. Smallest Proposed Fix (Description Only)

1. **Tag Low Obstacles in Level Generator (`build_levelgen_map.py`):**
   In `Scripts/Editor/build_levelgen_map.py`, when instantiating low barriers, concrete blocks, or low cover meshes (height <= 130 cm), add `Tags.Add(TEXT("Vault"))` to the generated `AStaticMeshActor`.
2. **Runtime Geometric Fallback in `VaultNavigation::IsVaultable`:**
   In `Source/CodexTactics/Private/Interactables/VaultNavigation.cpp`, add a geometric fallback: if an actor has collision, is labeled/named as a cover or obstacle, and its bounding box height is between 40 cm and 130 cm with depth <= 140 cm, treat it as vaultable even if the explicit tag is missing.
3. **Register Vault Nav Areas on Startup:**
   In `CodexTacticsGameMode.cpp`, during level setup, scan all static meshes with low-cover tags or matching vault height bounds and execute `VaultNavigation::MakeVaultable(Actor)`.
