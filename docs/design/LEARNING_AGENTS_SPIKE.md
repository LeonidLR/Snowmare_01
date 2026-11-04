# Learning Agents Feasibility Spike: Autonomous PMC Squad Combat in UE 5.8

**Document Version:** 1.0  
**Date:** 2026-11-04  
**Author:** Gemini (Autonomous Systems & AI Lead)  
**Status:** Architecture Proposal & Feasibility Spike  
**Governing Context:** `docs/port/GEMINI_WEEKEND_2026-11.md` (Task T8), `docs/design/PMC_TRAINING_ANALYSIS.md` (Task T9), `docs/systems/pmc-squad-ai.md`

---

## 1. Executive Summary & Strategic Context

### 1.1 Why Investigate Learning Agents?
The analysis of the overnight genetic trainer (`docs/design/PMC_TRAINING_ANALYSIS.md`) revealed a fundamental ceiling in utility-based AI:
1. **The Policy Expressiveness Ceiling:** The current PMC commander (`PmcCommanderRules`) evaluates utility scores via linear weights multiplied by fixed 0..1 considerations. Evolution tuned these weights, but the structure remains stateless (with rudimentary 2-second commit timers).
2. **Convergence on Passive Stalemates:** Because utility scoring lacks sequential foresight (temporal credit assignment), self-play converged on risk-averse camping: **95% of rounds ended in 240-second time-limit draws**, with only 0.63 kills per round.
3. **Flapping and Flanking Blindness:** Operatives frequently fall into infinite "yo-yo" loops (`Advance` $\leftrightarrow$ `FallBack`), traversing hundreds of meters with zero net displacement, and flank attempts suffer a **98.5% failure rate** because the utility function cannot plan multi-step spatial routes under dynamic crossfire.

To build "very smart, human-like PMC fighters nearly as good as the player," the squad AI requires **reinforcement learning (RL)** and **imitation learning (IL)** capable of learning non-linear, temporal policies with memory. 

Unreal Engine 5.8 provides the native **Learning Agents** plugin suite (`Plugins/Experimental/LearningAgents/` and `LearningCore/`), offering an integrated, high-performance bridge between Unreal's gameplay simulation and PyTorch.

---

## 2. Unreal Engine 5.8 Learning Agents API Architecture

A comprehensive inspection of the engine source code at `C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Experimental/LearningAgents/` confirms that UE 5.8 features a mature, production-oriented machine learning runtime:

```
                      +-------------------------------------------------------+
                      |                 Unreal Engine 5.8                     |
                      |                                                       |
                      |   +-----------------------------------------------+   |
                      |   |           ULearningAgentsManager              |   |
                      |   |    (Tracks Agent IDs, Ticks Listeners)         |   |
                      |   +-----------------------+-----------------------+   |
                      |                           |                           |
                      |         +-----------------+-----------------+         |
                      |         |                                   |         |
                      |         v                                   v         |
                      |   +--------------------+             +------------+   |
                      |   | ULearningAgents-   |             | ULearning- |   |
                      |   | Interactor         |             | Agents-    |   |
                      |   | - Observations     |             | Policy     |   |
                      |   | - Actions          |             | (MLP+GRU)  |   |
                      |   +---------+----------+             +-----+------+   |
                      |             |                              |          |
                      +-------------|------------------------------|----------+
                                    |                              |
                             Shared Memory / Socket Communicator   |
                                    |                              |
                      +-------------v------------------------------v----------+
                      |                Python ML Subsystem                    |
                      |        (PyTorch 2.5 / CUDA 12.4 / PPO / BC)           |
                      |                                                       |
                      |  - learning_core/train_ppo.py                         |
                      |  - learning_core/train_behavior_cloning.py            |
                      |  - learning_core/communicators/shared_memory.py       |
                      +-------------------------------------------------------+
```

### 2.1 Core C++ Classes & Interfaces
1. **`ULearningAgentsManager` (`UActorComponent`):**
   - The central orchestrator. Maintains an array of active agents (`AgentId: int32`).
   - Handles agent registration (`AddAgent`, `RequestAddAgent` for safe deferred addition), deletion, and life-cycle resets.
   - Manages listener components and ensures batched observation gathering and action dispatch across all active entities.
2. **`ULearningAgentsInteractor` (`ULearningAgentsManagerListener`):**
   - Bridges gameplay semantics into tensor representations.
   - **Observations:** Defined via `SpecifyAgentObservation` (schema) and populated via `GatherAgentObservation`. Primitives in `LearningAgentsObservations.h` include:
     - `SpecifyFloatObservation`, `SpecifyVectorObservation`, `SpecifyRotatorObservation`.
     - `SpecifyEnumObservation`, `SpecifyArrayObservation`.
     - Integrated normalization (`ELearningAgentsNormalization::Manual` with mean/scale offsets).
   - **Actions:** Defined via `SpecifyAgentAction` and executed via `PerformAgentAction`. Primitives in `LearningAgentsActions.h` include:
     - `SpecifyDiscreteAction` (categorical decisions: e.g. which order to issue).
     - `SpecifyContinuousAction` (analog outputs: e.g. heading angle, standoff fraction).
     - `SpecifyExclusiveAction` (one-hot choice among exclusive branches).
3. **`ULearningAgentsPolicy`:**
   - Neural network policy mapping observations to actions.
   - Supports Multi-Layer Perceptrons (MLP) combined with **Gated Recurrent Units (GRU)**:
     - `MemoryCell = ELearningAgentsMemoryCell::LearningAgentsGRU` (default 64 hidden states).
     - Default architecture: 1–2 hidden layers, 128 neurons, Exponential Linear Unit (`ELU`) activations.
   - Inference runs locally inside Unreal via `NNERuntimeBasicCpu` or GPU acceleration (`bUseParallelEvaluation = true`).
4. **`ULearningAgentsCritic`:**
   - Neural value function ($V(s)$) estimating expected future returns.
   - Used exclusively during PPO training to compute Generalized Advantage Estimation (GAE).
5. **`ULearningAgentsPPOTrainer` (`LearningAgentsTraining` module):**
   - Implements Proximal Policy Optimization (PPO) with clipped surrogate objective:
     - Policy learning rate: `0.0001`, Critic learning rate: `0.001` (10x policy).
     - GAE parameter $\lambda = 0.95$, discount factor $\gamma = 0.99$.
     - Entropy regularization weight (`ActionEntropyWeight`) to encourage exploration.
     - Gradient norm clipping (`bUseGradNormMaxClipping = true`, max 0.5).
6. **`ULearningAgentsImitationTrainer` & `ULearningAgentsRecorder`:**
   - Enables **Behavior Cloning (BC)**.
   - `ULearningAgentsRecorder` records gameplay frames (observations + actions) into `ULearningAgentsRecording` assets.
   - `ULearningAgentsImitationTrainer` trains the policy to minimize cross-entropy loss against recorded human decisions.
7. **`ULearningAgentsCommunicator`:**
   - IPC transport layer between Unreal Engine and the external Python training process.
   - Supports **Windows Shared Memory** (`FLearningAgentsSharedMemoryCommunicatorSettings`) for zero-copy memory transfers, and **TCP Sockets** (`FLearningAgentsSocketCommunicatorSettings`) for remote/distributed rigs.

### 2.2 The Python / PyTorch Process Lifecycle
Unreal does not embed PyTorch in C++; it spawns an external Python training process:
1. Unreal exports the observation and action schemas as a JSON config into a temporary directory.
2. UE initializes shared memory regions (`UE::Learning::FSharedMemoryTrainerServerProcess`) identified by a GUID.
3. UE launches Python (`Engine/Plugins/Experimental/LearningCore/Content/Python/train.py`).
4. `train.py` initializes PyTorch tensors mapped directly to the shared memory buffers.
5. In game ticks, Unreal writes observations, rewards, and completions to shared memory.
6. PyTorch batches transitions, performs backpropagation on GPU, updates network weights, and streams serialized weights back into Unreal's `ULearningAgentsNeuralNetwork` assets.

---

## 3. Machine Audit & Environment Prerequisites

### 3.1 Local Machine Audit
- **Operating System:** Windows 11 64-bit.
- **Hardware:** ASUS ROG Zephyrus Duo (Multi-core CPU + NVIDIA RTX Laptop GPU).
- **Engine Version:** Unreal Engine 5.8 (`C:/Program Files/Epic Games/UE_5.8`).
- **Python Installations:**
  1. *System Python:* Python 3.10.6 (`C:\Users\Zephyrus15Duo\...`). Currently has `torch 2.10.0+cpu` installed.
  2. *Bundled Unreal Python:* Python 3.11.8 (`C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe`). Clean environment without torch.

### 3.2 Prerequisites for GPU Training
To enable full PyTorch training in UE 5.8:
1. **Enable Plugins in Project / Plugin:**
   - `LearningAgents`
   - `LearningCore`
   - `PythonMLPackages` (Epic's auto-installer for PyTorch)
   - `NNERuntimeBasicCpu`
2. **PyTorch Wheel Requirements:**
   - `PythonMLPackages.uplugin` specifies exact pip requirements for Win64:
     - `torch==2.5.1+cu124`
     - `torchaudio==2.5.1+cu124`
     - `torchvision==0.20.1+cu124`
     - From index: `https://download.pytorch.org/whl/cu124/`
   - Enabling `PythonMLPackages` will trigger Unreal's internal pip manager to download and cache these wheels into `Engine/Intermediate/PipInstall/`.
3. **Memory Budget:**
   - Shared memory ring buffer: ~1–2 GB RAM for 10,000 steps buffer.
   - VRAM usage during batch training: ~2 GB VRAM on the NVIDIA RTX GPU.

---

## 4. Integration with CodexTactics Architecture & Seams

Learning Agents does not replace our codebase; it sits cleanly on top of our existing architectural seams:

```
+-----------------------------------------------------------------------------------+
| CodexTactics Core Seams                     Learning Agents Mapping               |
+-----------------------------------------------------------------------------------+
| SquadOrderCatalog (Shared Order Catalog) -> Discrete Action Space                 |
| PmcCommanderRules (Considerations)       -> Observation Space (Normalized Vectors)|
| CodexTactics.PmcArena (Arena Harness)    -> Training Environment (Rewards / Resets|
| Human Playtest Sessions                  -> Imitation Learning (Recorder / Tracks)|
+-----------------------------------------------------------------------------------+
```

### 4.1 Action Space Mapping (`SquadOrderCatalog`)
Our game already features a unified `SquadOrderCatalog` (`ESquadOrder` / `EPmcOrder`). This maps directly to a **Hierarchical Action Space**:
- **Action 0: Primary Order Type (Discrete, 12 branches):**
  `Hold`, `TakeCover`, `Advance`, `Flank`, `Suppress`, `FallBack`, `Heal`, `Relocate`, `Ambush`, `Reload`, `Sidearm`, `Crouch`.
- **Action 1: Spatial Target Direction (Continuous, 2 floats):**
  Unit vector $[\Delta x, \Delta y]$ specifying the desired movement bias or cover offset relative to threat facing.
- **Action 2: Target Entity Index (Discrete, 3 branches):**
  Which visible enemy to target/suppress (0: nearest, 1: most exposed, 2: commander priority).

### 4.2 Observation Space Mapping (PMC Perceptual Inputs)
Instead of hand-crafted utility multiplications, the raw state vector is fed directly to the neural policy:

| Feature Category | Inputs | Format & Normalization |
|:---|:---|:---|
| **Self State** | Health %, Ammo %, Stance (Stand/Crouch/Prone), Weapon Band Min/Max, IsInCover, CoverQualityAgainstThreat | Floats [0..1], 1-hot stances |
| **Exposure** | ExposedDurationSeconds, SecondsSinceDemasked, HighGroundLevel (0..3) | Floats normalized to [0..1] |
| **Threat Vector** | Relative 2D distance to nearest threat, angle to threat line-of-sight | Continuous: Distance / 50m, $\cos(\theta), \sin(\theta)$ |
| **Threat State** | Nearest threat health %, threat weapon band, is threat suppressing | Floats [0..1], bool |
| **Squad Coordination** | Alive squadmates count, MatesFlanking count, MatesSuppressing count, MatesAdvancing count | Normalized integers [0..1] |
| **Spatial Cover Raycast** | 8 radial rays measuring distance to nearest valid cover slot & cover angle | 8 floats normalized [0..20m] |

Total Observation Vector Size: **~36 scalar floats**. Extremely lightweight for MLPs, evaluating in $< 0.1\text{ ms}$ on CPU.

### 4.3 Training Environment & Rewards (`PmcArena`)
The arena console command `CodexTactics.PmcArena` becomes the episode driver:
- **Episode Completion (`ULearningAgentsCompletions`):**
  - Team wipe (Victory / Defeat).
  - Time limit reached (120 s).
- **Reward Function (`ULearningAgentsRewards`):**
  - $+10.0$ for Team Elimination Victory.
  - $+2.0$ per Enemy Down / Kill.
  - $+0.5 \times \text{DamageDealt} / 100$.
  - $-0.5 \times \text{DamageTaken} / 100$.
  - $+1.0$ for reaching an effective flank angle ($> 60^\circ$ off enemy cover normal).
  - $-5.0$ penalty for Time-Limit Stalemate (directly eliminating the draw wall).
  - $-1.0$ penalty for entering NavMesh stuck state.

### 4.4 Warm-Starting via Imitation Learning
A major weakness of pure RL is the "cold start" problem (random actions in early episodes look nonsensical). We can bypass this:
1. Run `ULearningAgentsRecorder` while a human developer or our rule-based bot plays against PMC squads on Depot.
2. Train a base policy using `ULearningAgentsImitationTrainer` (Behavior Cloning) for 100 epochs.
3. Switch to `ULearningAgentsPPOTrainer` to fine-tune and discover advanced squad tactics.

---

## 5. Narrow First Skill to Learn: Tactical Cover Selection Under Fire

To ensure rapid validation without risking project complexity, the first spike should tackle a single, tightly bounded skill:

### Skill Definition: "1v1 Tactical Cover & Relocation"
- **Agent:** One PMC Operative.
- **Opponent:** Static turret or scripted bot shooting from fixed positions across Depot streets.
- **Goal:** Advance from starting spawn to an objective marker while minimizing exposure time and damage taken.

```
Observation: [Self Health, Distance to Target, 8 Radial Cover Slot Distances & Qualities]
                          |
                          v
                 Neural Policy (PPO)
                          |
                          v
Action: [Pick Cover Slot (0..7) + Movement Style (Sprint vs Walk-Aim)]
```

### Why This Is the Ideal Spike:
1. It directly eliminates the **yo-yo flapping** and **flank death traps** identified in T9.
2. It tests the complete pipeline (Manager $\to$ Interactor $\to$ Policy $\to$ PPO $\to$ PyTorch IPC) in a deterministic, sub-minute test scenario.
3. If it succeeds, the trained neural asset (`.uasset`) can be dropped directly into `PmcCommanderRules::FindCoverNear` as an intelligent cover evaluator!

---

## 6. Technical Risks & Architectural Mitigations

| Risk | Description | Architectural Mitigation |
|:---|:---|:---|
| **Determinism & Async Tick Drift** | Unreal tick rate fluctuating while Python trains can cause sample desynchronization. | Run training with `-UseFixedTimeStep -FPS=60 -FixedSeed` (already standard in our arena scripts). |
| **Garbage Collection Hitches** | Dynamic spawning/destruction of actors during rapid episodes can cause GC stalls. | Pre-allocate squad actors in an object pool; reset actor positions and health on episode restart instead of re-spawning. |
| **Experimental Plugin Churn** | `LearningAgents` is located in `Engine/Plugins/Experimental/`. Epic may alter APIs in future minor releases. | Encapsulate all Learning Agents includes strictly within `Plugins/CodexLearningSpike/`. Game modules (`Source/CodexTactics/`) never touch Learning Agents directly. |
| **Inference CPU Cost on Client** | Running neural network inference every tick for dozens of characters could impact 60 FPS budget. | Run policy evaluation every $0.2 - 0.4\text{ s}$ (matching current `decision_interval`), use `NNERuntimeBasicCpu` with multi-threading (`bUseParallelEvaluation`), and leverage batching. |
| **Overfitting to Single Layout** | Agent memorizes specific cover walls on `L_Gen_Depot_1`. | Train across multiple procedural seeds from our level generator (`gen_ref_01_02_depot_x2_{1,2,3}`). |

---

## 7. Isolated Plugin Architecture: `Plugins/CodexLearningSpike`

Per the project guidelines, any prototype code must be completely self-contained:
- **Location:** `Plugins/CodexLearningSpike/`
- **Plugin Descriptor (`CodexLearningSpike.uplugin`):**
  ```json
  {
    "FileVersion": 3,
    "Version": 1,
    "VersionName": "0.1",
    "FriendlyName": "Codex Learning Spike",
    "Description": "Isolated feasibility spike for UE 5.8 Learning Agents integration.",
    "Category": "AI",
    "EnabledByDefault": false,
    "CanContainContent": true,
    "Modules": [
      {
        "Name": "CodexLearningSpike",
        "Type": "Runtime",
        "LoadingPhase": "Default"
      }
    ],
    "Plugins": [
      {
        "Name": "LearningAgents",
        "Enabled": true
      }
    ]
  }
  ```
- **Safety Guarantee:** `CodexTactics.uproject` remains untouched (`"EnabledByDefault": false`). Existing maps, game modules, and CI tests remain 100% unaffected.

---

## 8. Implementation Roadmap & Estimates

| Phase | Milestone | Deliverables | Effort Estimate |
|:---|:---|:---|:---:|
| **Phase 1** | **Tooling & Environment Setup** | Enable plugins in spike; verify PyTorch CUDA installation via `PythonMLPackages`. | 1 day |
| **Phase 2** | **Spike Plugin Skeleton** | Scaffold `Plugins/CodexLearningSpike`; implement Interactor schema for Cover Selection. | 2 days |
| **Phase 3** | **Single-Agent Cover PoC** | Train 1v1 cover selection agent using PPO; verify policy converges to safe cover paths. | 3 days |
| **Phase 4** | **Imitation Learning from Telemetry**| Convert night trainer 2D tracks into `LearningAgentsRecording` assets; run Behavior Cloning. | 2 days |
| **Phase 5** | **Squad PPO Arena Integration** | Multi-agent 3v3 self-play arena with Draw Timeout penalties. | 5 days |
| **Total** | **Full Production Ready Integration** | End-to-end intelligent PMC squad policy. | **~13 working days** |

---

## 9. Conclusion & Recommendation

The night trainer (`Scripts/Trainer/`) was an excellent Phase 1 evolutionary baseline. However, as demonstrated by the 95% draw rate in `pmc-main`, utility weights have reached the mathematical limit of what linear scoring can achieve.

**Recommendation:**
Proceed with Phase 1 & 2 of the **Learning Agents Spike** after the weekend merge. Build the isolated cover selection PoC inside `Plugins/CodexLearningSpike`. This represents the most promising path toward creating truly formidable, human-like tactical adversaries in Unreal Engine 5.8.
