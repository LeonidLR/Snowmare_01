# Task Report: T8 — Learning Agents Feasibility Spike

**Task:** T8 (optional). Learning Agents — an isolated feasibility spike  
**Branch:** `gemini/t8-learning-agents-spike`  
**Date:** 2026-11-04  
**Author:** Gemini (Autonomous Systems & AI Lead)  
**Status:** ✅ Completed (Feasibility Spike Documented)

---

## 1. Overview & Deliverables
The user's vision calls for PMC fighters to become formidable, human-like opponents. Following the empirical findings in T9 (`PMC_TRAINING_ANALYSIS.md`), which proved that linear utility weights have plateaued in self-play (95% time-limit draws), this spike investigates Epic's native **Learning Agents** plugin suite in Unreal Engine 5.8 as the next evolutionary leap.

Deliverable document authored:
[`docs/design/LEARNING_AGENTS_SPIKE.md`](file:///C:/Users/Zephyrus15Duo/Documents/Unreal%20Projects/CodexTactics-gemini/docs/design/LEARNING_AGENTS_SPIKE.md)

---

## 2. Engine API Deep Dive (UE 5.8)
Inspection of the engine source (`C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Experimental/LearningAgents/` and `LearningCore/`) verified:
- **`ULearningAgentsManager`:** Central actor component managing agent collections and batched dispatch.
- **`ULearningAgentsInteractor`:** Observation and action specification and gathering primitives (`SpecifyFloatObservation`, `SpecifyVectorObservation`, `SpecifyDiscreteAction`, `SpecifyContinuousAction`).
- **`ULearningAgentsPolicy`:** Neural network policy with MLP and Gated Recurrent Unit (`LearningAgentsGRU`, 64 memory cells) memory, running via NNE CPU or GPU parallel evaluation.
- **`ULearningAgentsPPOTrainer`:** C++ and Python implementation of Proximal Policy Optimization with GAE, entropy regularization, and gradient norm clipping.
- **`ULearningAgentsImitationTrainer` & `ULearningAgentsRecorder`:** Behavior Cloning framework recording gameplay transitions into `ULearningAgentsRecording` assets.
- **`ULearningAgentsCommunicator`:** Windows Shared Memory (`SharedMemoryCommunicator`) zero-copy IPC and TCP Socket communicators to an external Python/PyTorch process.

---

## 3. Hardware & Machine Audit
- **Local Rig:** ASUS ROG Zephyrus Duo running Windows 11 with multi-core CPU and NVIDIA RTX GPU.
- **Python / PyTorch:**
  - System Python 3.10.6 has `torch 2.10.0+cpu`.
  - Bundled Unreal Python 3.11.8 is available in `Engine/Binaries/ThirdParty/Python3/Win64/python.exe`.
  - `PythonMLPackages.uplugin` in UE 5.8 manages wheels for `torch==2.5.1+cu124` on Win64, enabling GPU-accelerated PPO training.

---

## 4. Integration with CodexTactics Architecture
- **Action Space:** Maps cleanly onto `SquadOrderCatalog` (`ESquadOrder` / `EPmcOrder`) as a hierarchical discrete/continuous action space.
- **Observation Space:** Direct normalized tensor input of the ~36 PMC commander considerations (health, threat vectors, exposure, weapon bands, squad states).
- **Training Environment:** `CodexTactics.PmcArena` drives episodes with reward shaping that explicitly penalizes time-limit draws (eliminating the camping failure mode).
- **First Narrow PoC Skill:** 1v1 Tactical Cover Selection and Relocation under fire (solving flapping and flank death traps).
- **Estimated Roadmap:** 13 working days across 5 structured phases.

---

## 5. Verification
- Inspected all relevant UE 5.8 engine headers and Python training scripts.
- Verified machine Python and PyTorch environments.
- Ensured zero modifications were made to `CodexTactics.uproject` or existing game code.
