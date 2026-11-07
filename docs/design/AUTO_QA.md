# Systems Specification: Autonomous Unattended QA & Incident Capture Pipeline (#41)

**Status:** DRAFT / PROPOSAL  
**Author:** Gemini (Advanced Agentic Pair-Programmer)  
**Governing Subsystems:** `PlaytestBotSubsystem`, `TelemetrySubsystem`, `CrashReporter`, `TrainingGround`  
**Reference Document:** `Scripts/bot_run.ps1`, `docs/port/TASK_BOARD.md` §41

---

## 1. Goals & Operational Overview

As development accelerates, human playtesting alone cannot catch rare procedural bugs, geometry traps, or corner-case desynchronizations.
The **Autonomous QA System** runs headless or offscreen overnight on dedicated hardware:
1. **Passive Runtime Watchdog Sensors:** Constant background anomaly detectors embedded directly in runtime subsystems.
2. **Deterministic Incident Capture:** On detecting an anomaly, automatically captures a timestamped bundle: high-resolution screenshot, 5-second replay buffer clip, level seed, position, and structured log snapshot.
3. **Scenario-Driven Playtest Bot:** A comprehensive AI bot capable of traversing routes, vaulting, clearing rooms, using stances, throwing grenades, and testing combat encounters.
4. **Intelligent Morning Report & Deduplication:** Nightly telemetry ingestion script that clusters identical bugs, filters known traps, and generates an actionable triage digest for the team.

---

## 2. Runtime Watchdog Sensors

The watchdog monitors live simulation state every frame or tick interval:

| Sensor ID | Monitored Condition | Trigger Threshold | Category / Severity |
|---|---|---|---|
| **`MON_STUCK`** | Pawn has move order but delta position < 5 cm. | Duration > 4.0 s on NavMesh. | Movement / Blocker |
| **`MON_OFF_NAVMESH`** | Pawn center of mass outside valid NavMesh bounds. | Distance to nearest nav surface > 60 cm. | Physics / Major |
| **`MON_FIRE_ARC`** | Weapon fired with angle between barrel and target > limit (#33). | Residual angle > 30.0° (prone > 15.0°). | Combat / Major |
| **`MON_WEAPON_REACH`** | Shot attempted at target beyond weapon max reach (#39). | Distance > `attack_range_m` * 100 cm. | Combat / Major |
| **`MON_COVER_EXPOSURE`** | Operative marked `bInCover = true` taking direct unmitigated hits. | Shot line hits center without wall trace hit. | Cover / Critical |
| **`MON_PERF_SPIKE`** | Single frame CPU/GPU time or Vision Cone trace duration. | Frame time > 33.3 ms (Vision Cone > 12.0 ms). | Performance / Minor |
| **`MON_FALL_THROUGH`** | Pawn Z coordinate drops below lowest map floor geometry. | Pawn Z < (Map Floor Min Z - 200 cm). | Collision / Critical |
| **`MON_T_POSE`** | Skeletal mesh animation evaluation fails or produces zero root velocity. | AnimInstance in default pose > 1.5 s while moving. | Visual / Major |
| **`MON_ORDER_STALL`** | Move/Action order issued with no state progression. | Elapsed > 12.0 s without arrival or refusal. | AI / Major |
| **`MON_LOG_ERROR`** | `UE_LOG(..., Error)` or `Warning` emitted in log. | Immediate trigger on regex error match. | Engine / Variable |

---

## 3. Incident Capture Protocol: The Crash/Bug Black Box

When any sensor fires:
1. **Snapshot Data Bundle:**
   ```
   Saved/QA/Incidents/<Date>_<Time>_<SensorID>_<Seed>/
   ├── incident.json       # Metadata: Level, Seed, Coordinates, Pawns, Orders
   ├── screenshot.png      # Offscreen rendered frame at incident instant
   ├── replay.clip         # 5-second input / state ring buffer
   └── slice.log           # Last 200 lines of engine log preceding the event
   ```
2. **Metadata Schema (`incident.json`):**
   ```json
   {
     "incident_id": "INC_20261108_031415_MON_STUCK_01",
     "sensor": "MON_STUCK",
     "severity": "CRITICAL",
     "level_id": "L_Gen_Depot_1",
     "seed": 42,
     "location": [ -2356.0, -6084.0, 0.0 ],
     "actor": "BP_Operative_Medic_C_1",
     "order": "MoveTo",
     "target_location": [ -1200.0, -6084.0, 0.0 ],
     "active_subsystems": [ "SquadSubsystem", "TacticalSightSubsystem" ],
     "engine_version": "5.8.0-Codex"
   }
   ```

---

## 4. The Scenario Bot: Autonomous Night Driver

Driven by an extended `UPlaytestBotSubsystem` running headless (`-nullrhi -nosound -benchmark -CodexBot`):
- **Scenario 1 (Exploration & Navigation):** Full route traversal on `L_Gen_Depot_1`, ascending all ladders, crossing ramps, vaulting jersey barriers, and collecting loot.
- **Scenario 2 (Stance & Formation Stress):** Cycles through formations (Wedge, Column, Line, Diamond) and stances (Stand, Crouch, Prone, Sprint) across uneven terrain.
- **Scenario 3 (Combat Stress):** Engages hostile PMC squads in both real-time aggressive and defensive postures, throwing grenades, deploying turrets, and seeking cover under fire.
- **Scenario 4 (Survival & Weather):** Survives extended 20-minute day cycles, warming at barrels during Blizzards and consuming rations.

---

## 5. Deduplication & Morning Digest Generation

To prevent developers from drowning in hundreds of duplicate logs:
1. **Fingerprint Hashing:**
   $$\text{Hash} = \text{MD5}(\text{SensorID} + \text{CallStackFrame}[0..2] + \text{NearestActorClass})$$
2. **Spatial Clustering:** Incidents occurring within 2.0 meters of each other on the same map layout are grouped into a single spatial issue.
3. **Automated Morning Report (`Saved/QA/MorningReport.md`):**
   - Summary statistics: Total runs, hours simulated, total incidents.
   - New Unique Issues: High-priority bugs discovered for the first time.
   - Regressions: Re-emergence of previously closed bug IDs.
   - Visual Carousel: Embedded screenshots of top 5 critical visual anomalies.

---

## 6. Numerical Parameters (Proposals)

| Parameter | Proposal Value | Description |
|---|---|---|
| **Ring Buffer Duration (`ReplayBufferSec`)** | `5.0` s | Historical window preserved in memory for incident capture. |
| **Incident Cooldown (`SameSensorCooldown`)** | `10.0` s | Prevents one stuck actor from generating 600 incidents per minute. |
| **Max Incidents Per Run (`MaxIncidentsPerRun`)** | `25` | Terminates run if game enters degenerate cascading failure. |
| **Nightly Bot Runs (`NightlyRunQuota`)** | `100` seeds | Target sample size per night across parallel headless workers. |

---

## 7. Smokes & Automated Verification

- **`EnemyStuckProbe`**: Validates stuck-agent detection and auto-recovery triggers.
- **`RepathProbe`**: Validates path recalculation when navigation goals are obstructed.
- **`IncidentRecorderSmoke` (Proposed)**:
  - Artificially triggers a `MON_FIRE_ARC` condition in a controlled test map.
  - Verifies that `incident.json`, screenshot, and log slice are cleanly written to disk.

---

## 8. Open Questions for Engineering Lead

1. *Video Recording Overhead:* Can the 5-second replay buffer be saved as a lightweight compressed MP4 video via NVENC offscreen, or should it record purely deterministic telemetry replay frames (`.demo.json`)?
2. *Visual LLM Triage ("Jev with Eyes"):* Should the morning report script optionally send anomalous screenshots to Gemini Flash via API to generate natural language bug summaries ("Soldier legs clipping through ramp mesh")?
3. *Automated Git Bisect:* Can the auto-QA runner trigger an automated git bisect when a nightly run fails a previously green seed?
