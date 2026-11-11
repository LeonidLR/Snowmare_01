# Auto-QA #41 — Morning Report Generator Report

**Author:** Gemini (Architecture & Systems Specialist)  
**Date:** 2026-11-11  
**Branch:** `gemini/016-2-auto-qa-morning` (from `main`)  
**Target:** Task #41 (`auto-qa`, Part 1) — Autonomous Unattended QA Morning Report Pipeline  
**Companion Artifacts:**  
- Tool: `Scripts/Tools/morning_qa_report.py`
- Test Suite: `Scripts/Tools/tests/test_morning_qa_report.py`
- Sample Output: `Saved/Orchestrator/qa/morning-2026-11-11.md`

---

## 1. Executive Summary

As specified in `docs/design/AUTO_QA.md` and queue 016, this task implements the **Morning Report Generator** (Part 1 of the Autonomous QA pipeline). 

The tool runs unattended every morning, ingesting automation logs, orchestrator hitch logs, trainer outputs, and telemetry traces from `Saved/Automation`, `Saved/Orchestrator`, `Saved/Trainer`, and `Saved/Telemetry`. It normalizes error signatures, deduplicates recurring hitches and crashes into distinct clusters, and generates an actionable triage digest at `Saved/Orchestrator/qa/morning-YYYY-MM-DD.md` proposing prioritized "QA" rows for the project task board.

---

## 2. Tool Architecture & Implementation

### 2.1. `Scripts/Tools/morning_qa_report.py`
1. **Intelligent Ingestion & Log Parsers:**
   - **Fatal Errors & Assertions:** Extracts assertion failure text, file, and line from `Assertion failed:` or `Fatal error:`.
   - **Performance Hitches:** Parses `FlushAsyncLoading(...)` stalls ($\ge 0.5\text{ s}$) and skinned asset compiler locks (`Waiting for skinned assets to be ready 0/1 (<Asset>)`).
   - **Automated Test Regressions:** Catches automation test failures (`[FAILED]`, `TestFalse failed`, `TestTrue failed`).
   - **Bot Traversal Stalls:** Detects `[Bot] stuck` and navigation abandonment events.
   - **Runtime Sensors:** Parses alarms emitted by passive watchdog sensors (`MON_*`).
   - **Telemetry Traces:** Reads `runs.jsonl` failure/timeout records.
2. **Fingerprint Hashing & Deduplication:**
   - Normalizes variable components (timestamps, frame indices, line numbers, memory pointers) into a canonical signature:
     $$\text{Fingerprint} = \text{MD5}(\text{Category} + \text{NormalizedMessage})[0..10]$$
   - Aggregates repeat occurrences, timestamps, source logs, and extracts the top 3 distinct log snippets per cluster.
3. **Directory Optimization:**
   - Filters out non-essential directories (e.g. `pmc-main/candidates`, `Intermediate`, `SourceArt`, `Cooked`, `backup*`) to enable rapid execution across multi-gigabyte project trees ($< 2\text{ seconds}$).
4. **Markdown Digest Output:**
   - Outputs `Saved/Orchestrator/qa/morning-YYYY-MM-DD.md`.
   - Structures findings into:
     1. Proposed QA Tasks for `TASK_BOARD.md` (`QA-MMDD-NN` format with severity and proposed assignee).
     2. Clustered Defect Details with log excerpts.
     3. Backlog of C++ passive runtime sensors for Codex.

---

## 3. Verification & Real Workspace Validation

### 3.1. Unit Test Suite
Implemented comprehensive unit test suite in `Scripts/Tools/tests/test_morning_qa_report.py`:
- `test_fingerprint_normalization`: Verifies dynamic pointers/line numbers cluster to identical fingerprints.
- `test_parse_unreal_log_incidents`: Verifies extraction of assertions, hitches, bot stalls, and sensor alarms.
- `test_deduplication`: Verifies multiple identical occurrences collapse into a single cluster.
- `test_parse_telemetry_jsonl`: Verifies parsing of JSONL telemetry run results.
- `test_generate_report_markdown`: Verifies full markdown generation against specification.

**Result:**
```text
Ran 5 tests in 0.041s
OK (5/5 PASS)
```

### 3.2. Execution on Actual Project Logs
Executed `python Scripts/Tools/morning_qa_report.py --root .`:
- Ingested 45 active logs (15,677 lines).
- Successfully identified and clustered the two real performance stalls logged during today's playtest sessions:
  * **`QA-1111-01` (HIGH):** `Skinned asset compilation stall: SKM_PmcOperator` (frame 614 in `hitch-2026-11-11-play.log`).
  * **`QA-1111-02` (HIGH):** `Skinned asset compilation stall: SK_Cutter_1slot` (frame 835 in `hitch-2026-11-11-play2.log`).
- Generated complete digest: `Saved/Orchestrator/qa/morning-2026-11-11.md`.

---

## 4. Codex Follow-Ups: C++ Passive Runtime Sensors (Part 2)

Per `docs/design/AUTO_QA.md` §2, the next phase requires Codex to embed passive watchdog sensors in engine code:

| Sensor ID | Monitored Condition | Trigger Threshold | Target C++ Class |
| :--- | :--- | :--- | :--- |
| **`MON_STUCK`** | Pawn ordered to move but $\Delta P < 5\text{ cm}$ | Duration $> 4.0\text{ s}$ on NavMesh | `AOperativeAIController` / `EnemyAIController` |
| **`MON_OFF_NAVMESH`** | Pawn center of mass outside valid NavMesh bounds | Distance to nav $> 60\text{ cm}$ | `AOperativeCharacter` |
| **`MON_FIRE_ARC`** | Weapon fired outside valid barrel angle (#33) | Residual angle $> 30.0^\circ$ (prone $> 15.0^\circ$) | `AOperativeCharacterCombat` |
| **`MON_WEAPON_REACH`**| Shot attempted past max weapon distance (#39) | Distance $> \text{attack\_range\_m} \times 100$ | `WeaponRangeRules` |
| **`MON_COVER_EXPOSURE`**| Operative in cover taking unmitigated hits | Shot ray hits center without wall trace hit | `AOperativeCharacterCover` |
| **`MON_FALL_THROUGH`**| Pawn drops below lowest floor geometry | Pawn $Z < (\text{Map Floor Min } Z - 200\text{ cm})$ | `LedgeFallComponent` |
| **`MON_T_POSE`** | AnimInstance in default pose while moving | Velocity $> 50\text{ cm/s}$ and AnimSpeed $= 0$ for $> 1.5\text{ s}$ | `UOperativeAnimInstance` |
