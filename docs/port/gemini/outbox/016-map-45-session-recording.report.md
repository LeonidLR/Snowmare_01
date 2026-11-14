# Implementation Map #45 — Session Recording Subsystem (`Codex.Demo.Record`)

**Author:** Gemini (Architecture & Systems Specialist)  
**Date:** 2026-11-11  
**Target:** Task #45 (`pl-session-record`) — Автозапись игровых сессий пользователя  
**Target Assignee:** Codex / Claude / Sonnet  
**Governing Documents:** `docs/port/TASK_BOARD.md` §45, `docs/design/AUTO_QA.md`, `Source/CodexTactics/Public/Telemetry/RunTelemetrySubsystem.h`

---

## 1. Tactical Concept & Operational Goal

### 1.1. Purpose & Requirements
To enable future imitation learning, behavioral cloning, and automated QA triage, every gameplay session played in Unreal Editor (PIE) or Standalone should be automatically recorded without human friction:
1. **Automatic Activation:** Starts recording on `Play` start; stops and serializes on `Play` stop.
2. **Lightweight Decision Log:** Captures high-level commander decisions rather than raw low-level input spam:
   - **Orders:** Chosen operative, destination, mode (Solo vs Formation / Box Group), target cover slot, corner peek angle, targeted fire, quick fire, suppress, grenade toss, stance change.
   - **Tactical Context (Snapshots):** Squad positions and health, visible enemies, ranges, blizzard weather intensity, active combat phase.
3. **Storage & Discard Rules:**
   - Saved to: `Saved/Demos/<YYYY-MM-DD>_<Map>_<Time>.json`.
   - **Quality Filter:** Short or trivial runs ($< 5.0\text{ s}$ duration OR $0$ user orders issued) are discarded immediately without writing to disk.
4. **Console Variable & Zero Overhead:**
   - Controlled by `Codex.Demo.Record 0/1` (default `1`).
   - Zero impact on game thread FPS: captured as lightweight struct ring buffer in memory; JSON formatting and disk I/O occur only at session end (or via background thread task).

---

## 2. Existing Classes to Extend vs. New Code

### 2.1. Reusable Engine Components (~80% Reuse)

| Component | File & Line | Reusable Function / Capability |
| :--- | :--- | :--- |
| **Player Controller Orders** | `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerOrders.cpp:1-400` | Intercept point for `OrderGroupMove`, `IssueTargetedShot`, `IssueQuickFire`, `IssueSuppress`. |
| **Player Controller Cover** | `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerCover.cpp:1-350` | Intercept point for cover slot assignment and corner peek commands. |
| **Grenade Dispatch** | `Source/CodexTactics/Private/Combat/GrenadeSubsystem.cpp:150-240` | `UGrenadeSubsystem::ThrowGrenade()` captures grenade orders and target coordinates. |
| **Squad State & Selection** | `Source/CodexTactics/Private/Characters/SquadSubsystem.cpp:1-300` | `USquadSubsystem` provides members, stances, roles, and solo/formation toggles. |
| **Telemetry Patterns** | `Source/CodexTactics/Private/Telemetry/RunTelemetrySubsystem.cpp:1-250` | Established pattern for `UWorldSubsystem` telemetry hooks and session paths. |
| **Spatial Telemetry Schema** | `Source/CodexTactics/Private/Bot/SpatialTelemetryRecorder.cpp:1-200` | Existing Godot coordinate conversion (`ToGodot`) and event JSON formatting. |

### 2.2. New Code Required (~20%)

1. **`USessionRecordingSubsystem` (`UWorldSubsystem`):**
   - Header: `Source/CodexTactics/Public/Telemetry/SessionRecordingSubsystem.h`
   - Source: `Source/CodexTactics/Private/Telemetry/SessionRecordingSubsystem.cpp`
   - Governs the session recording lifecycle (`OnWorldBeginPlay`, `Deinitialize`).
   - Maintains memory buffer of user orders and periodic environment snapshots.
2. **`SessionRecordingRules` (Pure Logic):**
   - Header: `Source/CodexTactics/Public/Telemetry/SessionRecordingRules.h`
   - Source: `Source/CodexTactics/Private/Telemetry/SessionRecordingRules.cpp`
   - Pure function `bool ShouldDiscardSession(float DurationSec, int32 OrderCount)`.
   - File path generator: `FormatDemoFilePath(const FString& SavedDir, const FString& MapName, FDateTime Timestamp)`.
   - JSON serializer for demo sessions.

---

## 3. Data Schema (`Saved/Demos/<Date>_<Map>.json`)

```json
{
  "version": 1,
  "session_id": "demo-20261112-143022-depot1",
  "map_name": "L_Gen_Depot_1",
  "timestamp": "2026-11-12T14:30:22Z",
  "duration_s": 48.6,
  "orders_count": 14,
  "result": "VICTORY",
  "weather": {
    "profile": "Blizzard",
    "intensity": 0.80,
    "temperature_c": -18.5
  },
  "initial_squad": [
    { "id": "Commander", "role": "Commander", "max_hp": 120.0, "weapon": "Rifle" },
    { "id": "Sapper", "role": "Sapper", "max_hp": 110.0, "weapon": "Shotgun" },
    { "id": "Medic", "role": "Medic", "max_hp": 100.0, "weapon": "Pistol" }
  ],
  "orders": [
    {
      "t": 1.42,
      "order_type": "MOVE",
      "operative_id": "Commander",
      "mode": "Formation",
      "target_location": [1250.0, 480.0, 20.0],
      "b_sprint": false,
      "cover_slot_id": "none"
    },
    {
      "t": 8.15,
      "order_type": "COVER",
      "operative_id": "Commander",
      "mode": "Formation",
      "target_location": [1840.0, 520.0, 20.0],
      "b_sprint": true,
      "cover_slot_id": "wall_south_slot_2",
      "stance": "Crouching"
    },
    {
      "t": 14.80,
      "order_type": "TARGETED_SHOT",
      "operative_id": "Sapper",
      "mode": "Solo",
      "target_enemy_id": "BP_Enemy_Hound_C_1",
      "target_enemy_archetype": "FrostHound",
      "distance_cm": 680.0
    },
    {
      "t": 22.30,
      "order_type": "GRENADE",
      "operative_id": "Commander",
      "mode": "Solo",
      "grenade_type": "Frag",
      "target_location": [2450.0, 610.0, 30.0]
    }
  ],
  "snapshots": [
    {
      "t": 10.0,
      "squad": [
        { "id": "Commander", "hp": 120.0, "pos": [1840.0, 520.0, 20.0], "in_cover": true },
        { "id": "Sapper", "hp": 110.0, "pos": [1720.0, 490.0, 20.0], "in_cover": true },
        { "id": "Medic", "hp": 100.0, "pos": [1600.0, 460.0, 20.0], "in_cover": true }
      ],
      "visible_enemies": [
        { "id": "BP_Enemy_Hound_C_1", "archetype": "FrostHound", "hp": 45.0, "pos": [2320.0, 580.0, 20.0], "range_cm": 520.0 }
      ]
    }
  ]
}
```

---

## 4. Insertion Points in Existing Code

### 4.1. Console Variable Registration
In `Source/CodexTactics/Private/Telemetry/SessionRecordingSubsystem.cpp`:
```cpp
namespace SessionRecordTuning
{
    static TAutoConsoleVariable<int32> CVarDemoRecord(
        TEXT("Codex.Demo.Record"),
        1,
        TEXT("1 = Auto-record player gameplay decisions to Saved/Demos/ on Play stop. 0 = Disabled."),
        ECVF_Default
    );
}
```

### 4.2. World Lifecycle Hooks
```cpp
void USessionRecordingSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    
    // Only record in Game and PIE worlds
    if (!InWorld.IsGameWorld() || SessionRecordTuning::CVarDemoRecord.GetValueOnGameThread() == 0)
    {
        bIsRecording = false;
        return;
    }
    
    StartSession();
}

void USessionRecordingSubsystem::Deinitialize()
{
    if (bIsRecording)
    {
        StopAndFlushSession();
    }
    Super::Deinitialize();
}
```

### 4.3. Order Intercept in `ACodexTacticsPlayerController`
Hook cleanly into existing order entry points:
```cpp
void ACodexTacticsPlayerController::OrderGroupMove(const FVector& Destination, bool bSprint, bool bPlan)
{
    // Existing move logic...
    
    // Telemetry capture:
    if (USessionRecordingSubsystem* Rec = GetWorld() ? GetWorld()->GetSubsystem<USessionRecordingSubsystem>() : nullptr)
    {
        Rec->RecordMoveOrder(GetSelectedOperative(), Destination, bSprint, IsSoloModeActive());
    }
}

void ACodexTacticsPlayerController::IssueTargetedShot(AActor* HitActor)
{
    // Existing shot logic...
    
    if (USessionRecordingSubsystem* Rec = GetWorld() ? GetWorld()->GetSubsystem<USessionRecordingSubsystem>() : nullptr)
    {
        Rec->RecordShotOrder(GetSelectedOperative(), HitActor);
    }
}
```

### 4.4. Discard Filter on Flush
In `USessionRecordingSubsystem::StopAndFlushSession()`:
```cpp
void USessionRecordingSubsystem::StopAndFlushSession()
{
    bIsRecording = false;
    const float DurationSec = GetWorld()->GetTimeSeconds() - SessionStartTime;
    const int32 OrdersCount = RecordedOrders.Num();

    if (SessionRecordingRules::ShouldDiscardSession(DurationSec, OrdersCount))
    {
        UE_LOG(LogCodexTactics, Display, TEXT("[DemoRecord] Discarded short/empty session (duration: %.1f s, orders: %d)"),
            DurationSec, OrdersCount);
        return;
    }

    const FString FilePath = SessionRecordingRules::FormatDemoFilePath(
        FPaths::ProjectSavedDir() / TEXT("Demos"),
        GetWorld()->GetMapName(),
        FDateTime::UtcNow()
    );

    const FString JsonString = SessionRecordingRules::SerializeSessionToJson(SessionData);
    FFileHelper::SaveStringToFile(JsonString, *FilePath);
    UE_LOG(LogCodexTactics, Display, TEXT("[DemoRecord] Saved session demo to %s (%d orders, %.1f s)"),
        *FilePath, OrdersCount, DurationSec);
}
```

---

## 5. Automated Verification & Smokes

* **Unit Test:** `Source/CodexTacticsTests/Private/Telemetry/SessionRecordingRulesTest.cpp`
  - Asserts `ShouldDiscardSession(3.0f, 5)` returns `true` (too short $< 5\text{ s}$).
  - Asserts `ShouldDiscardSession(25.0f, 0)` returns `true` (no orders issued).
  - Asserts `ShouldDiscardSession(12.0f, 2)` returns `false` (valid session).
  - Validates JSON output against schema.
* **Smoke Command:** `SessionRecordSmokeCommand` (`Codex.Test.SessionRecord`):
  1. Sets `Codex.Demo.Record 1`.
  2. Initializes subsystem with mock game world.
  3. Dispatches mock move order and targeted shot order.
  4. Advances time to $10.0\text{ s}$.
  5. Flushes subsystem.
  6. Asserts target JSON file was created in `Saved/Demos/` and verifies field integrity.
  7. Cleans up test demo file.

---

## 6. Open Design Questions (with Recommended Defaults)

1. **Environment Snapshot Frequency:**
   - *Question:* How often should full tactical environment snapshots (pawn positions, health, visible targets) be recorded during a match?
   - *Recommended Default:* Snapshot dynamically upon every user order, plus a periodic heartbeat every $5.0\text{ s}$ during active combat. This keeps log sizes well under $150\text{ KB}$ per match while capturing complete tactical context.
2. **Tactical Pause Handling:**
   - *Question:* During tactical pause (Spacebar), should planning markers be recorded when placed or when unpaused?
   - *Recommended Default:* Record when planned with a flag `"b_planned_during_pause": true` and record the execution timestamp when unpaused. This reveals user pause-thinking time.
3. **Log Retention and Disk Cap:**
   - *Question:* Should `Saved/Demos/` enforce a disk space cap or file count limit?
   - *Recommended Default:* Cap `Saved/Demos/` at $500\text{ MB}$ or $200$ files (FIFO: oldest unflagged files deleted when cap is exceeded).
