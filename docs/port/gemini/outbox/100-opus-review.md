# Review: Gemini C++ Task 100 (`PmcTraversalSmokeCommand.cpp`)

**No HIGH findings.**

The diff only adds diagnostic logging. It does not change smoke-test control flow, pass/fail accounting, or leg sequencing. All pointer chains are null-guarded except one noted below. Shared-pointer and UObject lifetimes are safe within each synchronous scope.

## Findings

- **[MEDIUM] Possible missed requirement: diagnostics only, no fix.**
  - The patch logs stair-leg path state at two points: when the order is issued and when the leg fails.
  - It does not change how stair legs are routed or validated.
  - If task 100 asked for stair traversal failures to be *fixed*, not just instrumented, the requirement is unmet.
  - The smoke run will still report the same stair FAILs.
  - Please confirm against the task statement.

- **[MEDIUM] Build and link dependency risk.**
  - The new code uses `FNavMeshPath`, `NavNodeRef`, `UPathFollowingComponent`, `AAIController`, and `FNavPathSharedPtr`.
  - `Path->CastPath<FNavMeshPath>()` needs two things:
    - the full definition from `NavMesh/RecastNavMesh.h` / `NavMesh/NavMeshPath.h`;
    - a link against `FNavMeshPath::Type`, a `NAVIGATIONSYSTEM_API` static.
  - The diff adds no includes.
  - Unless `CodexTacticsDev.Build.cs` already lists both `NavigationSystem` and `AIModule`, and the needed headers are already included in this TU, this fails to compile or link.
  - It may compile today only through transitive or unity-build includes, and break in non-unity builds.

- **[LOW] Order-time diagnostic may report a stale or empty path.**
  - The second block reads `Follow->GetPath()` immediately after the order call.
  - The path may not reflect the new stair leg if either:
    - the operative's order is deferred or queued; or
    - pathfinding is async (`bUseAsyncPathfinding`).
  - In that case the log shows the *previous* leg's path, or none, labelled as this stair leg. This would mislead the investigation.
  - Consider logging only when `Result == Accepted`, or also logging the path's goal and request ID.

- **[LOW] Fail-time diagnostic may run after path following was reset.**
  - Depending on what the failure path does before this block (abort, `StopMovement`, timeout handling), `GetPath()` may already be null or reset.
  - The result would be `numPts=0` and `endPoly=0`, which hides the real failure state.
  - Capture the path snapshot before any abort or stop call.

- **[LOW] Unguarded `Walker` dereference in the order-time block.**
  - `Cast<AAIController>(Walker->GetController())` assumes `Walker` is non-null.
  - The surrounding code (`Walker->IsMantlePathAllowed()`) suggests it is valid here.
  - However, the fail-time block defensively checks `Walker ? … : nullptr`.
  - If this code path can be reached with a destroyed or null walker, it crashes. Add a guard for consistency.

- **[LOW] Inconsistent validity gating for `CastPath`.**
  - `CastPath`/`PathCorridor` is gated on `Path.IsValid()`.
  - The point and length data are gated on `Path->IsValid()`.
  - On an invalidated path, `numPolys` and `endPoly` may report stale corridor data while `numPts=0`. Use `bValidPath` for both.

- **[LOW] Duplicated logic and log noise.**
  - The path-inspection code is copy-pasted across two sites.
  - It logs at `Display` verbosity for *every* stair leg on every pass, not only on failure.
  - Factor it into a helper, and consider `Log`/`Verbose` level or a CVar gate so smoke output stays readable.
  - `Feet(*Walker)` is also recomputed three times in one statement; compute it once.

## Checked and found fine

- **Type conversions:**
  - The ternary `FNavPathSharedPtr` / `nullptr` compiles.
  - `%llu` with a `uint64` cast is correct.
  - `GetLength()` as double/float into `%.0f` is fine.
- **Bounds:** `[PtIdx]` is bounded by `Min(NumPts, 5)`, and `.Last()` is guarded by `NumPts > 0` and `Num() > 0`.
- **Lifetimes:**
  - `FString` temporaries from `Vec()` live through `UE_LOG`.
  - The path is held by a thread-safe shared pointer copy.
- **Stealth:** no gameplay or stealth behavior is touched.