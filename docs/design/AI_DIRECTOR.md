# Systems Specification: The Dynamic AI Director & HQ Radio Dispatch (#47 + #48)

**Status:** DRAFT / PROPOSAL  
**Author:** Gemini (Advanced Agentic Pair-Programmer)  
**Governing Subsystems:** `GameFlowSubsystem`, `CombatPressureSubsystem`, `EnvironmentSubsystem`, `PmcEncounterSubsystem`, `NarrativeSubsystem`  
**Reference Document:** `docs/design/NARRATIVE_BIBLE.md` (origin/gemini/t4-narrative-bible), `docs/systems/waves-horde-encounters.md`

---

## 1. Goals & Architectural Vision

The AI Director is not a rigid wave timer; it is the **Director of the Moment** that dynamically modulates game intensity:
1. **Mathematical Tension Model:** Computes a continuous `TensionScore` (0.0 to 1.0) derived from physical squad danger: HP depletion, ammo scarcity, hypothermia, gunfire noise, unobserved perimeter sectors, and elapsed calm time.
2. **Pacing Cycle (Build-up → Peak → Forced Relax):** Governs the rhythm of exploration and survival, ensuring high-intensity firefights are followed by mandatory breathing room (to heal, loot, thaw at fires, and reload).
3. **Action Budgeting:** Spends director tokens on contextual encounters (roaming monster packs, PMC ambushes, flanking waves, weather escalations, environmental sound cues).
4. **HQ Radio Operator as In-Game Companion (#48):** The state bureau (ГБР) dispatch operator provides dynamic briefings, tactical advice based on real-world map states (detected vehicles, mined sectors, loot points), and character banter.
5. **Atmospheric Blizzard Blackout:** During intense arctic storms and whiteouts, all radio communications with HQ are severed by radio interference, leaving the squad completely isolated.

---

## 2. The Dynamic Tension Model

The Director calculates squad tension every `UpdateInterval` (1.0 s) using a weighted normalized sum:

$$\text{Tension} = \sum w_i \cdot S_i \in [0.0, 1.0]$$

| Factor ($S_i$) | Weight ($w_i$) | Source Metric & Evaluation |
|---|---|---|
| **Health Depletion ($S_{\text{HP}}$)** | `0.30` | Average squad missing health: $1.0 - \frac{\sum \text{HP}}{\sum \text{MaxHP}}$. |
| **Ammunition Scarcity ($S_{\text{Ammo}}$)** | `0.15` | Total rounds in magazines & reserves / Baseline mission allotment. |
| **Cold & Hypothermia ($S_{\text{Cold}}$)** | `0.20` | Mean squad `ColdLevel` (0–100%). At frostbite threshold, scales sharply. |
| **Noise & Ballistic Heat ($S_{\text{Noise}}$)** | `0.15` | Recent decibels from unsuppressed gunfire and explosions within 45 m. |
| **Vulnerable Perimeter ($S_{\text{Sectors}}$)** | `0.10` | Uncovered vision sectors (e.g. rear guard blind arc). |
| **Calm Quota ($S_{\text{Dwell}}$)** | `0.10` | Time since last combat concluded / Level target rest interval. |

```
Tension
  1.0 ──┐             ┌─── Peak (Firefight / Ambush)
        │            /│\
        │           / │ \
  0.6 ──┼─ Build-up / │  \
        │          /  │   └── Forced Relax (Loot / Warmth / Dialogue)
  0.2 ──┴─────────/───┴────────────────────────
        0 min     2 min   4 min   6 min   8 min
```

### The Three Director Phases
1. **Build-up Phase ($T < 0.7$):** Squad explores; director seeds ambient noises, minor scavenging opportunities, or solitary scouts.
2. **Peak Phase ($T \ge 0.75$ or Ambush Event):** The director unleashes a combat event (PMC squad contact, monster swarm, or flanking horde). Action budget is locked; no secondary random waves may spawn during a peak.
3. **Forced Relax Phase ($T$ dropped after fight):** Upon elimination of active threats, the director enforces a rest window (`ForcedRelaxSeconds = 45–90 s`). Hostile spawns and wandering patrols are strictly suppressed, allowing operatives to kindle stoves, bandage wounds, and talk.

---

## 3. Budgeted Actions & Level Pacing Profiles

The Director generates encounters by expending an **Action Point Budget** accrued over time:

| Action | Cost (AP) | Conditions & Cooldown |
|---|---|---|
| **Lure Sound / False Alarm** | `10` | Calm phase; rustle in snow or distant barking. |
| **Solitary Patrol Probe** | `25` | Spawns 1–2 Frostbitten scouts investigating squad noise. |
| **Coordinated Flank Wave** | `50` | During combat peak; spawns fast runners along flank lanes. |
| **Hostile PMC Squad Ambush** | `75` | Minimum 120 s cooldown; places a 3-man fireteam in overwatch. |
| **Weather Shift Acceleration** | `30` | Pushes weather ladder from Overcast to Snowfall/Blizzard. |
| **Horde Swarm** | `100` | Only if squad remains stagnant in one zone exceeding safe threshold. |

### Per-Level Profiles (`level_config.json`)
- **Depot Profile (`Depot_TacticalPressure`):** High PMC budget, low monster swarm budget, frequent 60-second relax periods.
- **Lake Profile (`FrozenLake_Survival`):** Heavy cold drain weighting, periodic scripted wave triggers, long atmospheric silences.

---

## 4. HQ Dispatch & Radio Comms System (#48)

### HQ Persona: Senior Operator Voronov (ГБР Directorate)
- **Role:** The squad's external lifeline—a cynical, battle-weary state logistics officer speaking through military encrypted VHF.
- **Dynamic Context-Aware Voice Cues:** Voronov's radio lines are triggered by real world state:
  1. *Unclaimed Heavy Asset:* *"Sally, drone feed shows an intact BTR-80 parked near the eastern fueling rack. Grab it if you have the fuel."*
  2. *Loot Cache Intel:* *"Scouts flagged an abandoned medical container in grid Delta-2. Watch for tripwires."*
  3. *PMC Movement Warning:* *"Intercepts show Ronin's advance patrol moving towards the rail yard. Get into cover."*
  4. *Fallibility / Fog of War:* HQ's satellite intel is not omniscient. Occasionally Voronov reports: *"Grid looks clear,"* only for the squad to encounter dead-end barricades or radio silence.

### The Blizzard Blackout Mechanic
- When `EnvironmentSubsystem` transitions to `EWeatherState::Blizzard` or `Whiteout`:
  - Radio audio is overwhelmed by violent static and howling wind.
  - Subtitles flicker with corrupted characters (`[COMM_LOST - STATIC]`).
  - Minimap grid markers and HQ recon pings are disabled.
  - The squad is entirely on their own until the weather descends to Snowfall or Overcast.
  - When the storm clears, Voronov returns with relief: *"Sally, do you read me? Static cleared. Report status!"*

---

## 5. Numerical Parameters (Proposals)

| Variable | Proposal Value | Description |
|---|---|---|
| **Director Tick Rate (`DirectorTickInterval`)** | `1.0` s | Update frequency of the tension evaluator. |
| **Tension Peak Threshold (`TensionPeakMax`)** | `0.85` | Score at which director suspends additional hostile pressure. |
| **Forced Relax Minimum Duration (`RelaxMinSec`)** | `45.0` s | Guaranteed peace window after surviving a combat encounter. |
| **HQ Line Delivery Cooldown (`RadioLineCooldown`)** | `75.0` s | Minimum interval between non-urgent narrative voice lines. |
| **Blizzard Radio Threshold (`BlizzardBlackoutWind`)** | `WindSpeed >= 18.0 m/s` | Triggers total comms severance. |

---

## 6. Fairness, Telemetry & Pacing Metrics

To ensure the game feels challenging rather than cheap:
1. **No Spawn in Vision Cones:** Enemies never spawn inside any active operative vision cone or within 30 m of the camera center.
2. **Resource-Aware Throttling:** If all squad members have ammo below 10%, the director will never spawn heavy armored enemies, favoring solitary scouts carrying ammunition loot.
3. **Telemetry & Pacing Metric:** Logged to `Saved/Demos/<Date>_Pacing.json`:
   - Average tension over mission time (Target: 0.45 – 0.65).
   - Peak duration vs Relax duration ratio (Target: 1 : 2.5).

---

## 7. Smokes & Automated Verification

- **`CombatPressureSmoke`**: Verifies enemy group interval spawning based on pressure config.
- **`WeatherLakeSmoke`**: Validates weather transitions triggering environmental states.
- **`NarrativeSmoke`**: Validates radio line queues, audio ducking, and priority interrupts.
- **New Proposed Smoke:** `DirectorPacingSmoke`
  - Runs a 10-minute simulated playtest on Depot.
  - Verifies that post-combat forced relax is respected and tension graphs exhibit distinct wave cycles.

---

## 8. Open Questions for Director & Writer

1. *Voice Acting vs. Synthesized Radio Filter:* Will Voronov's lines be fully voiced by an actor with military radio DSP, or use text-to-speech with radio distortion for rapid prototyping?
2. *Intel Accuracy Slider:* Should higher difficulty settings introduce deliberate false intel from HQ due to hostile EW (electronic warfare) jamming?
3. *Player Radio Requests:* Can the player initiate contact with HQ via a keybind (e.g. **Y** to "Report SitRep") to request compass orientation or weather forecasts?
