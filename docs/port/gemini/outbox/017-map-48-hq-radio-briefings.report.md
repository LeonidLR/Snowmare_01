# Implementation Map #48 — HQ Radio, Mission Briefings & Squad Narrative Dialogue

**Author:** Gemini (Architecture & Systems Specialist)  
**Date:** 2026-11-11  
**Target:** Task #48 (`pl-narrative-radio`) — Радиопереговоры со штабом, брифинги и реплики бойцов  
**Target Assignee:** Codex / Claude / Training Ground Team  
**Governing Documents:** `docs/design/NARRATIVE_BIBLE.md` §4 & §5, `docs/port/TASK_BOARD.md` §48, `docs/systems/squad-and-orders.md`

---

## 1. Tactical Concept & Narrative Premise

### 1.1. Core Narrative Voice (*Gorky 17* Realism)
In homage to *Gorky 17* (*Odium*), the tone is dry, tense, bleak, cynical, and grounded in military realism:
1. **The Dramatis Personae:**
   - **"The Handler" (ГБР / State Rapid-Response Directorate):** Crisp, unsympathetic bureaucratic voice delivering mission parameters, satellite intel, and deniable ultimatums over encrypted radio.
   - **Sally (Commander, Age 51):** Low, gravelly baritone; stoic, focused, delivers tactical directives and coordinates with HQ.
   - **Roger (Marksman/Sapper, Age 48):** Rapid, clipped, paranoid; reports concealed traps, ammunition counts, and casualty odds.
   - **Babai (Heavy/Engineer, Age 49):** Deep, resonant bass; sparse words, comments on heavy machinery, barricades, and mechanical repairs.
2. **Three Dialogue Channels:**
   - **Pre-Mission Briefing:** Full-screen tactical satellite overlay with mission parameters, ROE (Rules of Engagement), and atmospheric transmission audio before insertion.
   - **HQ Tactical Radio (Comms):** Mid-mission encrypted broadcasts triggered by objectives, entering key sectors (e.g. Depot Cut), or critical emergencies (operative KIA, cold alarm).
   - **Squad Walk-and-Talk (Banter):** Contextual atmospheric banter between Sally, Roger, and Babai during quiet exploration (stiff joints, sub-zero cold, bitter memories of Ronin).

---

## 2. Fast-Track: What Can Be Built in TypeScript (Training Ground) FIRST

> [!IMPORTANT]
> **TypeScript First-Class Strategy:** To iterate instantly on writing, pacing, sound queues, and state triggers without slow Unreal Engine recompilation or editor cooks, **the complete narrative engine can be prototyped and validated in TypeScript inside `Tools/TrainingGround/` first**, exporting identical data schemas to Unreal C++.

### 2.1. TypeScript Components for Training Ground
1. **Dialogue Catalog & Schema (`Tools/TrainingGround/src/narrative/dialogueCatalog.ts`):**
   - Strictly-typed catalog containing all briefings, HQ calls, and walk-and-talk banter:
     ```typescript
     export interface IDialogueLine {
       id: string;
       speaker: "HANDLER" | "SALLY" | "ROGER" | "BABAI";
       text_ru: string;
       text_en: string;
       audio_cue_id?: string;
       duration_s: number;
       priority: "CRITICAL" | "TACTICAL" | "AMBIENT";
       trigger: IDialogueTrigger;
     }
     ```
2. **Dialogue Trigger Evaluator (`Tools/TrainingGround/src/narrative/triggerRules.ts`):**
   - Pure state evaluation functions:
     - `onAreaEnter(zoneId)` $\to$ checks if briefing line should fire.
     - `onWaveCleared(waveNum)` $\to$ HQ status check.
     - `onOperativeWounded(role)` $\to$ squad reaction chatter.
     - `onColdDrop(tempC)` $\to$ shivering warning.
   - Manages cool-downs, deduplication, and priority overrides (e.g. Critical HQ order interrupts ambient banter).
3. **Tactical Radio & Briefing UI Widget (`Tools/TrainingGround/src/ui/radioWidget.ts`):**
   - Renders a military green-phosphor CRT radio panel in the browser with:
     - Speaker call-sign and unit badge.
     - Animated audio waveform oscilloscope.
     - Typewriter text effect with adjustable playback speed.
4. **Interactive Briefing Screen (`Tools/TrainingGround/src/ui/briefingScreen.ts`):**
   - Interactive tactical satellite map of Depot x2 with objective overlays, recon pins, and audio playback test harness.

---

## 3. Existing Engine Classes vs. New Unreal C++ Code

### 3.1. Reusable Unreal Components (~75% Reuse)

| Component | File & Line | Reusable Function / Capability |
| :--- | :--- | :--- |
| **Game Message Subsystem** | `Source/CodexTactics/Private/UI/GameMessageSubsystem.cpp:1-150` | `UGameMessageSubsystem::PostMessage` currently handles toasts and message history. |
| **Tutorial Subsystem** | `Source/CodexTactics/Private/Tutorial/TutorialSubsystem.cpp:1-350` | Step-by-step trigger gating, control lockouts, and prompt display. |
| **Narrative Smoke Tests** | `Source/CodexTacticsDev/Private/Debug/NarrativeSmokeCommand.cpp:1-120` | Automated verification of message queues and history. |
| **Audio & SFX Subsystem** | `Source/CodexTactics/Public/Audio/CodexAudioSubsystem.h` | 2D radio audio playback and radio static ducking. |

### 3.2. New Unreal C++ Code Required (~25%)

1. **`UNarrativeSubsystem` (`UWorldSubsystem`):**
   - Header: `Source/CodexTactics/Public/Narrative/NarrativeSubsystem.h`
   - Source: `Source/CodexTactics/Private/Narrative/NarrativeSubsystem.cpp`
   - Ingests `Content/Data/Narrative/dialogue_catalog.json` (validated by TypeScript).
   - Manages playback queues: `QueueLine()`, `InterruptWithCritical()`, `PlayBriefing()`.
   - Fires gameplay delegates on line completion (`OnNarrativeSequenceFinished`).
2. **`NarrativeTriggerRules` (Pure State & Priority Logic):**
   - Header: `Source/CodexTactics/Public/Narrative/NarrativeTriggerRules.h`
   - Source: `Source/CodexTactics/Private/Narrative/NarrativeTriggerRules.cpp`
   - Evaluates queue precedence: `CRITICAL` (HQ alerts) interrupts `TACTICAL` (orders), which suppresses `AMBIENT` (banter).
   - Enforces minimum quiet gap between banter lines ($12.0\text{ s}$).

---

## 4. Data Keys & Tuning

Configured in `Content/Data/Narrative/dialogue_catalog.json`:

```json
{
  "settings": {
    "min_gap_between_lines_s": 4.0,
    "ambient_banter_cooldown_s": 25.0,
    "radio_static_lead_in_s": 0.35,
    "typing_speed_chars_per_s": 35.0
  },
  "briefings": {
    "DEPOT_INSERTION": {
      "id": "briefing_depot_01",
      "map": "L_Gen_Depot_1",
      "lines": [
        {
          "speaker": "HANDLER",
          "text": "Север-1, приём. Вы вышли к внешнему периметру базы ГСМ. Спутник подтверждает: здесь развёрнут передовой взвод ЧВК «Ронин».",
          "duration_s": 4.5
        },
        {
          "speaker": "SALLY",
          "text": "Вас понял, Центр. Бабай, держи ящики под прицелом. Роджер, проверь подходы на растяжки.",
          "duration_s": 3.8
        },
        {
          "speaker": "HANDLER",
          "text": "Ваша задача — зачистить узлы связи и вскрыть ворота ангара. Если попадёте в плен — государство о вас не слышало. Конец связи.",
          "duration_s": 4.2
        }
      ]
    }
  },
  "tactical_triggers": [
    {
      "trigger_id": "APPROACH_DEPOT_CUT",
      "condition": "ZoneEnter:Z_cut",
      "speaker": "ROGER",
      "text": "Внимание... Слишком тихо в узком проходе. Крыши идеальны для засады.",
      "priority": "TACTICAL"
    }
  ]
}
```

---

## 5. Insertion Points in Existing Code

### 5.1. Trigger Volumes in Level & Spatial Triggers
In `Source/CodexTactics/Private/Narrative/NarrativeSubsystem.cpp`:
```cpp
void UNarrativeSubsystem::NotifyZoneEntered(FName ZoneId)
{
    const FDialogueLine* Line = NarrativeRules::FindTriggerLine(ActiveCatalog, ZoneId, PlayHistory);
    if (Line)
    {
        QueueDialogueLine(*Line);
    }
}
```

### 5.2. HUD Presentation Hook in `UGameMessageSubsystem`
```cpp
void UGameMessageSubsystem::DisplayRadioTransmission(const FDialogueLine& Line)
{
    // 1. Play radio static burst SFX
    AudioSubsystem->PlayRadioStaticBurst();

    // 2. Broadcast to UI HUD Slate widget
    OnRadioTransmissionStarted.Broadcast(Line.Speaker, Line.Text, Line.DurationSeconds);
}
```

---

## 6. Automated Verification & Smokes

* **TypeScript Unit Test:** `Tools/TrainingGround/src/narrative/trigger.test.ts`
  - Validates priority sorting: `CRITICAL` preempts running `AMBIENT` chatter.
  - Asserts cool-downs prevent repeated trigger loops.
* **Unreal C++ Unit Test:** `Source/CodexTacticsTests/Private/Narrative/NarrativeTriggerRulesTest.cpp`
  - Validates JSON ingestion and speaker tag parsing.
* **Smoke Command:** `HQRadioSmokeCommand` (`Codex.Test.HQRadio`):
  1. Triggers briefing sequence $\to$ asserts 3 sequential lines delivered in correct order.
  2. Spawns ambient squad banter line.
  3. Fires emergency operative wounded event $\to$ asserts ambient line is immediately interrupted by emergency transmission.
  4. Asserts message history correctly logged in `UGameMessageSubsystem`.

---

## 7. Open Design Questions (with Recommended Defaults)

1. **Voiceover Scope:**
   - *Question:* In Milestone 1, are radio transmissions full voiceover audio, radio-filtered clips, or text subtitles with military static sound effects?
   - *Recommended Default:* In Milestone 1, use encrypted radio static / click sound effects + green-phosphor text display. Full voice actor recordings can be integrated in Milestone 2.
2. **Tactical Pause Interaction:**
   - *Question:* When a narrative briefing or HQ radio transmission plays, should player controls freeze?
   - *Recommended Default:* Pre-mission briefings freeze controls during the opening 10 seconds. In-mission tactical comms do **NOT** pause the game, allowing the player to maneuver while listening.
3. **Dialogue Skip Hotkey:**
   - *Question:* Can the player skip dialogue lines?
   - *Recommended Default:* Spacebar or Left Click skips to the next line during briefings; in-mission radio cannot be skipped but does not impede controls.
