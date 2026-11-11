# Implementation Map #27+ — Remaining PMC Mechanics (Full Combat Suite)

**Author:** Gemini (Architecture & Systems Specialist)  
**Date:** 2026-11-12  
**Target:** Task #27+ (`pl-pmc-remaining`) — Завершение механик ЧВК: силуэты, слух, полоски здоровья, автономность/бот, лут с тел, гранаты, многораундовый бой  
**Target Assignee:** Codex / Claude  
**Governing Documents:** `docs/design/PMC_TRAINING_ANALYSIS.md`, `docs/systems/pmc-squad-ai.md`, `docs/port/TASK_BOARD.md` §27+

---

## 1. Tactical Concept & Operational Goal

The PMC (Private Military Company) squad serves as the intelligent, humanoid adversary in *CodexTactics*. While base movement, weapon fire, and arena training (`PmcCommanderRules`, `feat/pmc-squad-ai`) are established, **Task #27+** delivers the missing gameplay and presentation layers required for full production parity:

1. **Silhouettes & Occlusion Highlighting:** When PMC operatives are behind cover or geometry, their silhouettes are rendered via Custom Depth Stencil (amber/red outline when spotted or heard, faded when lost in Fog of War).
2. **Acoustic Perception & Hearing Propagation:** Footsteps, weapon handling, and reloads emit sound stimuli through walls. Occlusion per wall ($0.5\times$ attenuation per barrier, capped at 3 walls) allows tactical stealth vs PMC sentries.
3. **Overhead UMG Status Displays:** Tactical floating widgets displaying HP bars, body armor integrity pips, and suppression meters.
4. **Bot Autonomy vs PMC:** Enhancing `PlaytestBotSubsystem` so automated bots intelligently identify PMC cover positions, lay down suppressive fire, and execute flanking maneuvers rather than charging into killzones.
5. **Corpse Looting:** Integration with `UCorpseSubsystem`: downed PMC fighters drop loot containers (5.45x39/7.62x39 ammunition, armor plates, bandages, grenades) with a cursor/interaction prompt.
6. **Tactical Grenade Throws:** Full implementation of throwable fragmentation/flash grenades with trajectory calculation and the existing vendor animations (`CrouchThrowGrenade`, `ProneThrowGrenade`, `RunAndThrowGrenade`).
7. **Multi-Round Arena / Level Endurance:** Extending `PmcArenaRoundRules` into continuous multi-round engagements where PMC squads adapt their tactics, memorize player attack routes, and replenish attrition between waves.

---

## 2. Technical Decomposition (7 Systems)

### 2.1. System 1: Silhouettes & Outline Shader
- **Engine Seam:** `OperativeCharacter.cpp` (hostile PMC instances), Post-Process Material `M_PostProcess_TacticalSilhouettes`.
- **Implementation:**
  - Set `Mesh->SetRenderCustomDepth(true)` and `Mesh->SetCustomDepthStencilValue(STENCIL_PMC_HOSTILE = 250)`.
  - When in direct line-of-sight: regular rendering.
  - When occluded by geometry (`SceneDepth > CustomDepth`): render a 2-pixel pulsing outline (Amber: Alerted, Red: Engaged, Gray: Last Known Acoustic Position).
  - When unspotted and silent: stencil rendering is disabled (pure stealth).

### 2.2. System 2: Acoustic Perception & Sound Propagation
- **Engine Seam:** `Source/CodexTactics/Public/AI/PerceptionRules.h`, `Private/Characters/EnemyPerceptionRules.cpp`.
- **Implementation:**
  - Sound categories with noise radii:
    * `ProneCrawl`: $3.0\text{ m}$
    * `CrouchWalk`: $5.5\text{ m}$
    * `Sprint`: $18.0\text{ m}$
    * `SuppressedGunshot`: $12.0\text{ m}$
    * `UnsuppressedGunshot`: $65.0\text{ m}$
  - Wall Attenuation: Each intervening collision wall reduces loudness by `HearingOcclusionPerWall = 0.5`. 3 walls reduce loudness to $12.5\%$.
  - When noise threshold $> 1.0$, PMC transitions from `Idle` $\to$ `Suspicious` $\to$ rotates towards sound vector $\to$ alerts squad via radio bark.

### 2.3. System 3: Overhead UMG HP & Status Bars
- **Engine Seam:** `Source/CodexTactics/Public/UI/PmcStatusWidget.h`, `Content/UI/WBP_PmcStatusBar.uasset`.
- **Implementation:**
  - Attached via `UWidgetComponent` to PMC pawn socket `socket_overhead` (Screen Space mode).
  - Components:
    * Health ProgressBar ($0..100\%$, green $\to$ yellow $\to$ red).
    * Armor Pips (1 to 3 armor plates; absorbs direct damage before HP loss).
    * Suppression Meter (yellow filling gauge; when full, PMC is pinned and crouches).
  - Visibility Rule: Hidden by default; made visible upon first sight/hit; fades out $4.0\text{ s}$ after losing line of sight.

### 2.4. System 4: Autonomous Playtest Bot vs PMC
- **Engine Seam:** `Source/CodexTactics/Private/Bot/PlaytestBotSubsystem.cpp`, `Source/CodexTactics/Public/AI/PmcCommanderRules.h`.
- **Implementation:**
  - Playtest bot checks `FPerceivedThreat::bIsInCover`.
  - If target PMC is in full cover: Bot does NOT execute a direct charge across open ground.
  - Instead, Bot picks order `EPmcOrder::Flank` or designates an overwatch pin while a secondary operative bounds forward.

### 2.5. System 5: Corpse Looting & Gear Retrieval
- **Engine Seam:** `Source/CodexTactics/Private/Characters/CorpseSubsystem.cpp:80-160`.
- **Implementation:**
  - Implements `ICorpseBody` interface on PMC pawns.
  - Upon death, `UCorpseSubsystem::RegisterBody` adds an interactable loot component `UCorpseLootComponent`.
  - Loot Table (`Content/Data/Balance/loot_tables.json` under `"pmc_corpse"`):
    * 5.45x39 ammo: 30–60 rounds ($100\%$ probability).
    * Medkit / Bandage: 1 unit ($40\%$).
    * Frag Grenade: 1 unit ($25\%$).
  - Cursor interaction: hovering over body displays gear search icon; pressing `E` transfers items to squad inventory drawer (`UInventoryDrawerWidget`).

### 2.6. System 6: Tactical Grenade Throws
- **Engine Seam:** `Source/CodexTactics/Public/Combat/GrenadeProjectile.h`, `Private/Combat/GrenadeProjectile.cpp`.
- **Asset Integration:**
  - Stand / Run: `Content/Animations_Grenade/Grenade_01root_root_RunAndThrowGrenade_UE.uasset`
  - Crouch: `Content/Animations_Grenade/Grenade_01root_root_CrouchThrowGrenade_UE.uasset`
  - Prone: `Content/Animations_Grenade/Grenade_01root_root_ProneThrowGrenade_UE.uasset`
- **Ballistics & Explosion:**
  - Launch velocity computed by `UGameplayStatics::SuggestProjectileVelocity_CustomArc(TossVelocity, Start, TargetPos, 0.5f)`.
  - Fuse time: $3.0\text{ s}$ from release.
  - Blast Radius: $450\text{ cm}$; Damage: $150\text{ HP}$ at epicenter down to $25\text{ HP}$ at edge; fully blocked by heavy cover walls.
  - Red circular decal projected onto ground during fuse, prompting operatives/bots to perform an emergency dive.

### 2.7. System 7: Multi-Round Arena Endurance
- **Engine Seam:** `Source/CodexTactics/Public/AI/PmcArenaRoundRules.h`, `Private/AI/PmcArenaRoundRules.cpp`.
- **Implementation:**
  - Expands `FPmcRoundOutcome` with round counter `CurrentRound / MaxRounds (e.g. 3 of 5)`.
  - Tactical Memory between rounds: PMC commander stores a heatmap of coordinates where squad casualties occurred in previous rounds and biases defensive overwatch toward those directions.

---

## 3. Data Keys & Tuning

In `Content/Data/AI/pmc_combat_config.json`:

```json
{
  "pmc_perception": {
    "hearing_occlusion_per_wall": 0.5,
    "max_occlusion_walls": 3,
    "sound_radii_cm": {
      "crawl": 300.0,
      "crouch_walk": 550.0,
      "sprint": 1800.0,
      "unsuppressed_shot": 6500.0
    }
  },
  "pmc_grenades": {
    "max_throw_range_cm": 2500.0,
    "fuse_duration_s": 3.0,
    "explosion_radius_cm": 450.0,
    "epicenter_damage": 150.0,
    "outer_damage": 25.0,
    "cooldown_per_fighter_s": 18.0
  },
  "pmc_loot": {
    "ammo_rounds_min": 30,
    "ammo_rounds_max": 60,
    "medkit_drop_chance": 0.40,
    "grenade_drop_chance": 0.25
  },
  "pmc_ui": {
    "hp_bar_fade_delay_s": 4.0,
    "show_armor_pips": true,
    "suppression_alert_threshold": 0.5
  }
}
```

---

## 4. Failure Modes & Edge Cases

1. **Grenade Trajectory Clipping:**
   - *Risk:* AI attempts to throw a grenade while in low cover, and the ballistic parabola collides with the cover wall directly in front of the thrower, blowing up the PMC.
   - *Mitigation:* `GrenadeRules::ValidateArcTrace` runs a simulated trace along the trajectory. If a collision occurs within $1.5\text{ m}$ of the thrower, the throw order is cancelled and the fighter falls back to rifle fire.
2. **Corpse Loot Bloat:**
   - *Risk:* 50 dead PMC bodies on a long mission overwhelm memory and clutter interaction traces.
   - *Mitigation:* `UCorpseSubsystem` enforces a hard cap of 20 active lootable bodies; oldest non-searched bodies decay into non-interactive frozen skeletal remains with items consolidated into a single supply box.
3. **Custom Depth Stencil Overflow:**
   - *Risk:* Multiple outline shaders fighting for stencil bitmasks.
   - *Mitigation:* Strict bit allocation: Bit 1 (Friendlies = 1), Bit 2 (Hostiles = 2), Bit 7 (Selected = 128).

---

## 5. Automated Tests & Acceptance Criteria

### 5.1. Unit & Rules Tests
- `FPmcHearingOcclusionTest`: Verifies sound decay across 0, 1, 2, 3 walls according to exponential formula.
- `FPmcGrenadeArcValidationTest`: Verifies valid target coordinates within $[500\text{ cm}, 2500\text{ cm}]$ and rejections of blocked arcs.
- `FPmcLootGenerationTest`: Deterministic loot generation seeded by `RoundSeed` + `FighterID`.
- `FPmcMultiRoundMemoryTest`: Verifies threat heatmap accumulation across round transitions.

### 5.2. Smoke Tests
- `CodexTactics.PmcFullCombatSmoke`:
  1. Spawns 3v3 PMC encounter in `L_VehicleTest` or `L_Gen_Depot_1`.
  2. Verifies CustomDepth outline renders when behind cover.
  3. Fires unsuppressed shot, verifies PMC turns towards acoustic vector.
  4. Triggers PMC grenade throw, verifies ballistic arc and avoidance dive.
  5. Eliminates PMC, asserts `UCorpseSubsystem` registers body and loot drawer displays dropped ammo.
