# Mortars Preparation & System Integration Report

**Author:** Gemini (Architecture & Systems Specialist)  
**Date:** 2026-11-11  
**Target Branch:** `main` (Authored on `gemini/015-4-mortars-prep`)  
**Scope:** Comprehensive census of mortar specifications across design, narrative, systems, and code, synthesis of agreed mechanics, identified open designer questions (with Russian recommendations), and mapping of reusable engine systems. Read-only report.

---

## 1. Executive Summary

Mortars in *CodexTactics* bridge tactical squad command and indirect artillery support. Following the Lead Designer's definitive ruling on 2026-11-08 (`docs/design/SQUAD_SIX_FIRE_TEAMS.md` Section 7.3), mortars are **stationary emplacements (fixed nests)** rather than man-portable carried weapons. 

A preliminary C++ implementation (`MortarRules`, `MortarNestActor`, `MortarSubsystem`, `MortarSmokeCommand`) already exists in `Source/CodexTactics/` and was verified in the combat lab (`L_FormationLabCombat.json`).

This report consolidates all existing design commitments, details open questions for the Lead Designer, and maps the existing engine systems that will drive the production rollout of player-facing and enemy mortar warfare.

---

## 2. Comprehensive Census of Mortar Mentions

| Document / Area | Key Content & Citations | Context / Architectural Impact |
| :--- | :--- | :--- |
| **`design/NARRATIVE_BIBLE.md:63, 176`** | Sally's physical trauma (high-frequency hearing loss / tinnitus from close mortar blast during bridge ambush). Mortar concussions frame the prologue ambush. | Establishes lore grounded in heavy ordnance concussions and deafness/suppression effects. |
| **`design/SQUAD_SIX_FIRE_TEAMS.md:18, 97, 142, 307, 403-435`** | **Section 7.3 (Designer Ruling 2026-11-08):**<br>- Mortar is a **fixed nest**, not carried.<br>- Crew of **three** needed at the tube to fire.<br>- High-angle plunging fire ($45^\circ - 85^\circ$); walls/side cover do not protect, only roofs.<br>- Both sides: PMC nests can shell squad; squad can eliminate crew and capture tube.<br>- **Spotter Rule:** fires only at points currently seen by a living teammate. | Core governing document for mortar gameplay. Motivates the 6-man squad expansion (3 at mortar, 3 maneuvering). |
| **`design/WEAPONS_BALLISTICS.md:79, 112-115, 155`** | Ballistic arc solver ($45^\circ - 85^\circ$ elevation), peak altitude 35–60 m, standard gravity scale ($4.0$), roof interception, plunging blast wave. | Ballistic and collision specifications. |
| **`docs/systems/weapons.md:259-289`** | Complete specification of `AMortarNestActor`, `UMortarSubsystem`, `FMortarNestConfig`, `FMortarBracketing`: 110 m/s, 5–6 s flight, 30–250 m range, 110 damage / 6 m blast, 3.5 s interval, 6 rds/order, 24 ammo. Bracketing walk-in: 15 m $\to$ 8.2 m $\to$ 4.5 m $\to$ 2.5 m. PMC cluster targeting. | Shipped runtime C++ reference. |
| **`docs/port/TASK_BOARD.md:145`** | **Task #37:** Preparation for mortar crew: 2 additional squad cells (total 6), HUD slots, selection keys 1–6, 6-man formations, savegame schema. Mortar nest itself is a distinct task. | Squad roster dependency. |
| **`Source/CodexTactics/Public/Combat/`** | `MortarRules.h`, `MortarNestActor.h`, `MortarSubsystem.h`, `MortarSmokeCommand.cpp`, `MortarRulesTest.cpp`. | Working pure rules, actor, subsystem, automated tests, and smoke harness. |

---

## 3. The Agreed Design Specification

From `SQUAD_SIX_FIRE_TEAMS.md` and `docs/systems/weapons.md`:

1. **Fixed Nests, Not Carried:**
   - Instantiated as `AMortarNestActor` spawned by `UMortarSubsystem` from level JSON (`"mortar_nests": [...]`).
   - Placed at pre-designated fortified sandbag/trench pits.
2. **Crew Requirement:**
   - Requires exactly **3 living operatives** positioned at designated places around the tube (`CrewRadiusCm = 160 cm`).
   - If any crew member moves farther than $2.5\text{ m}$ (`CrewLeaveCm`), gets knocked down, or dies, firing immediately halts.
3. **Plunging Ballistics:**
   - High-angle trajectory ($v_0 = 110\text{ m/s}$, gravity multiplier $4.0$). Flight time $\approx 5.0 - 5.6\text{ s}$.
   - Arrives from near-vertical angles. Solid walls, sandbags, and waist-high barricades offer zero protection. Only solid ceilings/roofs intercept the shells.
4. **The Strict Spotter Rule:**
   - Neither squad nor PMC mortar can blind-fire into the Fog of War.
   - Target coordinate must be actively visible to at least one living operative/PMC (`UTacticalSightSubsystem::CanOperativeSee` tested at target $+ 60\text{ cm}$ height).
5. **Simplified Bracketing Walk-In:**
   - 1st shell lands up to $15\text{ m}$ off target in random direction.
   - Each consecutive shell shrinks error by $\times 0.55$ ($15\text{ m} \to 8.2\text{ m} \to 4.5\text{ m} \to 2.5\text{ m} \to 1.5\text{ m}$).
   - Retargeting $> 10\text{ m}$ away resets bracketing to $15\text{ m}$.
6. **PMC AI Commander Integration:**
   - `MortarRules::BestCluster`: Evaluates clusters of seen squad members within $8\text{ m}$ radius. If cluster score $\ge 0.3$ (2+ operatives), PMC orders mortar fire.

---

## 4. Open Questions for the Lead Designer (с рекомендуемыми ответами)

Ниже сформулированы 5 ключевых геймдизайнерских вопросов для окончательного утверждения механики:

### Вопрос 1: Боевой радиус стрельбы (Min / Max Range) на компактных картах
* **Контекст:** В текущих тестовых данных заложен диапазон $30\text{ м} - 250\text{ м}$. Однако реальные размеры боевых локаций: полигон $120 \times 120\text{ м}$, склад Депо $\approx 124 \times 177\text{ м}$. При дальности $250\text{ м}$ миномет из угла базы простреливает 100% площади карты, не оставляя игроку «мертвых зон» для тактического маневра под открытым небом.
* **Рекомендуемый дефолт (Recommended):** **Установить рабочий диапазон $35\text{ м} - 140\text{ м}$**. 
  * *Обоснование:* $35\text{ м}$ минимальной дистанции защищает от самоподрыва вблизи гнезда; $140\text{ м}$ максимальной дистанции покрывает ключевой сектор/перекресток, но оставляет противоположный фланг вне досягаемости, вынуждая игрока либо менять позицию, либо подавлять вражеский миномет с дальней дистанции.

### Вопрос 2: Интерфейс и способ целеуказания игроком (Targeting UX)
* **Контекст:** Сейчас приказ отдается через консоль `CodexTactics.MortarFire [nest] <x y>`. Как игрок должен наводить миномет в реальном бою?
* **Рекомендуемый дефолт (Recommended):** **Двухкликовый маркер с визуализацией зоны досягаемости («Бублик»)**.
  * При клике на свое укомплектованное гнездо (или хоткеем на панели приказов) на земле вокруг гнезда отображается полупрозрачное кольцо допустимой дальности ($35 - 140\text{ м}$).
  * Курсор превращается в круг прицеливания (диаметр $15\text{ м}$, соответствующий радиусу первого разлета).
  * Кликом в видимую зону подтверждается приказ на 6 выстрелов. Кликом по кнопке «Прекратить огонь» залп отменяется.

### Вопрос 3: Захват и выведение из строя вражеского миномета (Capture & Sabotage)
* **Контекст:** Если расчет ЧВК уничтожен снайпером или фланговым обходом, как squad взаимодействует с пустой трубой?
* **Рекомендуемый дефолт (Recommended):** **Захват любыми тремя бойцами + опциональный подрыв**.
  * Любые 3 бойца отряда игрока могут подойти к гнезду и занять места расчета (миномет переходит на сторону игрока).
  * Если игрок не хочет оставлять миномет за спиной, любой боец (или быстрее Саппер) через контекстное действие [E] «Подорвать казенник» (время: 3.0 сек) может уничтожить орудие, превратив его в неактивный реквизит.

### Вопрос 4: Сигнализация о приближающейся мине (Audio/VFX Incoming Warning)
* **Контекст:** Время полета мины составляет $\approx 5.0 - 5.6$ сек. Должен ли игрок получать заблаговременное предупреждение о падающем снаряде?
* **Рекомендуемый дефолт (Recommended):** **Звуковой свист + 2-секундный маркер опасности**.
  * За **2.0 секунды** до прилета проигрывается нарастающий звуковой свист падающей мины.
  * На земле в расчетной точке взрыва проецируется пульсирующий красный круг опасной зоны ($R = 6\text{ м}$).
  * Это награждает внимательного игрока возможностью прожать спринт или нырнуть под ближайший навес/крышу здания.

### Вопрос 5: Боекомплект и восполнение боеприпасов (Ammo Limits & Resupply)
* **Контекст:** В гнезде заложено 24 мины (4 полных залпа по 6 выстрелов). Возобновляется ли боезапас?
* **Рекомендуемый дефолт (Recommended):** **Конечный стартовый боезапас (24 снаряда) с возможностью пополнения из ящиков снабжения**.
  * Огневая мощь миномета колоссальна. 24 снаряда достаточно для решения 2-3 тяжелых тактических задач на карте.
  * На карте в тыловых складах могут быть размещены 1-2 контейнера с боеприпасами («Mortar Shell Crate»). Доставка ящика к гнезду восстанавливает $+12$ снарядов.

---

## 5. Reusable Engine Subsystems & Integration Seams

Внедрение миномета не требует написания низкоуровневых подсистем с нуля; оно опирается на зрелые существующие модули проекта:

```
+---------------------------------------------------------------------------------------+
|                                    MORTAR PIPELINE                                    |
+---------------------------------------------------------------------------------------+
| 1. Spotting & Fog of War:                                                             |
|    - UTacticalSightSubsystem::CanOperativeSee(Target + SpotHeight)                    |
|    - SightConeComponent (visibility sweeps)                                           |
+---------------------------------------------------------------------------------------+
| 2. Ballistics & Trajectory:                                                           |
|    - UGrenadeTrajectoryRules / GrenadeAimRules (parabolic high-angle arc calculation) |
|    - High-angle gravity multiplier (gravity_scale = 4.0, v0 = 110 m/s)                |
+---------------------------------------------------------------------------------------+
| 3. Roof & Ceiling Occlusion:                                                          |
|    - URigidBodyExplosionRules / LevelObstacleRules                                    |
|    - LineTraceSingleByChannel(ECC_WorldStatic) from sky down to target                |
|    - Intercepted by roof actors -> blast detonates on ceiling, ground floor is safe   |
+---------------------------------------------------------------------------------------+
| 4. Detonation, Damage & Suppression:                                                  |
|    - Explosions::Detonate(World, BlastOrigin, Damage = 110, OuterRadius = 600 cm)     |
|    - USuppressionSubsystem::ApplyAreaSuppression(BlastOrigin, Radius = 800 cm)        |
+---------------------------------------------------------------------------------------+
| 5. Squad Roster & Fire Teams:                                                         |
|    - SQUAD_SIX_FIRE_TEAMS (Slots 4-6, UI cells, selection keys 1-6)                   |
|    - SquadSubsystem: Assigns 3 men to AMortarNestActor crew places                    |
+---------------------------------------------------------------------------------------+
```

---

## 6. Implementation Readiness

* **Logic & Data:** `MortarRules` parses cleanly from `weapons_tuning.json` (`"mortar_nest"` block) and `L_Gen_Depot_1.json` (`"mortar_nests"` array).
* **Actor & Combat:** `AMortarNestActor` and `UMortarSubsystem` manage world instantiation, crew slots, and firing cycles.
* **Next Steps:**
  1. Receive Lead Designer sign-off on the 5 open questions above.
  2. Implement HUD targeting ring widget / two-click targeting flow.
  3. Hook up Niagara muzzle flash / mortar smoke trail and incoming audio whistle.
  4. Place test nests in `L_Gen_Depot_1.json` for gameplay balancing.


---
**Status (designer 2026-11-11): mortars POSTPONED.** Questions 1-5 stay open; Claude recommends 1, 2, 4, 5 as proposed and for 3 a crew of 1 (slow) / 2 (normal) instead of 3 (solo is the primary mode). Revisit when the designer reopens mortars.
