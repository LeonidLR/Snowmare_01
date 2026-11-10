# Отчёт по задаче 005: Исследование замедления оперативников и интеграции анимаций CON_Injured

- **Дата**: 10 ноября 2026
- **Агент**: Gemini (`$env:CODEX_AGENT = "gemini"`)
- **Статус**: Read-Only исследование (файлы исходного кода и ассеты не изменялись)
- **Целевой отчёт**: `docs/port/gemini/outbox/005-injured-scout.report.md`

---

## ЧАСТЬ A: Почему оперативники иногда передвигаются очень медленно?

В кодовой базе `Source/CodexTactics` скорость передвижения оперативника рассчитывается и модулируется несколькими независимыми системами. В базовом состоянии скорость прямого шага стоя составляет **220 см/с** (2.2 м/с), а спринта — **725 см/с** (7.25 м/с).

Ниже приведен исчерпывающий каталог всех модификаторов, условий и порогов, способных замедлить бойца вплоть до 20–30 см/с (или полностью остановить).

---

### 1. Состояние здоровья и ранения (`IsWounded`)

- **Файлы и строки**:
  - `Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp:571-576`:
    ```cpp
    bool AOperativeCharacter::IsWounded() const {
        return bWounded || (HealthComponent && HealthComponent->GetMaxHealth() > 0.f
            && HealthComponent->GetCurrentHealth() / HealthComponent->GetMaxHealth() < MovementConfig.WoundedHealthThreshold);
    }
    ```
  - `Source/CodexTactics/Private/Characters/OperativeMovementRules.cpp:30-33`:
    ```cpp
    if (bWounded) {
        Speed *= Config.WoundedSpeedMultiplier;
    }
    ```
  - `Source/CodexTactics/Public/Characters/OperativeMovementRules.h:71-78`
- **Конфигурационные ключи и значения**:
  - `MovementConfig.WoundedHealthThreshold` = **0.50** (50% от максимального запаса HP).
  - `MovementConfig.WoundedSpeedMultiplier` = **0.65** (65% от нормальной скорости).
- **Порог активации медленного шага**:
  - **Ровно 50% HP**. Как только текущее здоровье бойца падает ниже половины (`HP < 0.50 * MaxHP`), боец получает статус `IsWounded()`.
  - Его скорость ходьбы снижается с 220 см/с до **143 см/с**.
  - **Спринт полностью блокируется**: `CanSprint()` возвращает `false` при `bWounded == true`. Переход на бег 725 см/с становится невозможен.

---

### 2. Воздействие холода и гипотермия (`ColdSpeedMultiplier`)

- **Файлы и строки**:
  - `Source/CodexTactics/Private/Survival/ColdRules.cpp:56-86`:
    ```cpp
    EColdTier GetTier(float Cold) {
        if (Cold < 40.f) return EColdTier::Normal;
        if (Cold < 70.f) return EColdTier::Chills;
        if (Cold < 90.f) return EColdTier::Freezing;
        return Cold >= 100.f ? EColdTier::Frostbite : EColdTier::Hypothermia;
    }
    float GetSpeedMultiplier(EColdTier Tier) {
        switch (Tier) {
            case EColdTier::Normal: return 1.0f;
            case EColdTier::Chills: return 0.70f;
            case EColdTier::Freezing: return 0.45f;
            default: return 0.25f; // Hypothermia / Frostbite
        }
    }
    ```
  - `Source/CodexTactics/Private/Survival/ColdSurvivalComponent.cpp:299`:
    `Owner->SetColdSpeedMultiplier(ColdRules::GetSpeedMultiplier(NewTier));`
  - `Source/CodexTactics/Public/Characters/OperativeMovementRules.h:83-85`:
    `MovementConfig.MaxColdToSprint` = **60.0%**.
- **Пороги холода и множители скорости**:
  - **Cold 0%..39% (`Normal`)**: множитель **1.00** (100% скорости).
  - **Cold 40%..69% (`Chills`)**: множитель **0.70** (70% скорости -> 154 см/с).
  - **Cold 70%..89% (`Freezing`)**: множитель **0.45** (45% скорости -> **99 см/с**).
  - **Cold 90%..100% (`Hypothermia / Frostbite`)**: множитель **0.25** (25% скорости -> **55 см/с**!).
  - **Блокировка спринта**: при `Cold >= 60%` спринт запрещён правилом `OperativeMovementRules::CanSprint()`.
- **Мультипликативный эффект (Ранен + Замерзает)**:
  Если боец ранен (0.65) и находится в стадии `Freezing` (0.45), его результирующая скорость равна:
  `220 * 0.65 * 0.45 =` **64.3 см/с** (замедление в 3.4 раза!).
  При `Frostbite` (0.25): `220 * 0.65 * 0.25 =` **35.7 см/с** (замедление более чем в 6 раз!).

---

### 3. Стойка персонажа (`Stance`)

- **Файлы и строки**:
  - `Source/CodexTactics/Private/Characters/OperativeMovementRules.cpp:5-16, 25-29`
  - `Source/CodexTactics/Public/Characters/OperativeMovementRules.h:55-70`
- **Конфигурационные параметры**:
  - `Standing`: базовая скорость `WalkSpeed` = **220 см/с**.
  - `Crouching`: `CrouchSpeed` = **125 см/с** (множитель ~**0.568**, т.е. 56.8% от шага).
  - `Prone`: `ProneSpeedMultiplier` = **0.28** (множитель **28%**, скорость ползка = **61.6 см/с**).
- **Особенности**:
  При получении приказа лечь или автоматическом уходе в укрытие скорость падает более чем в 3.5 раза.

---

### 4. Переноска раненых, тел и тяжелых грузов (`bCarrying`)

- **Файлы и строки**:
  - `Source/CodexTactics/Private/Characters/OperativeMovementRules.cpp:34-37`:
    ```cpp
    if (bCarrying) {
        Speed *= Config.CarryingSpeedMultiplier;
    }
    ```
  - `Source/CodexTactics/Public/Characters/OperativeMovementRules.h:79-82`
- **Конфигурационные параметры**:
  - `CarryingSpeedMultiplier` = **0.30** (30% скорости -> **66 см/с**).
  - `CanSprint`: жестко возвращает `false` при `bCarrying == true`.

---

### 5. Темп патрулирования лидера строя (`PacePatrol`)

- **Файлы и строки**:
  - `Source/CodexTactics/Private/Characters/SquadSubsystemFormation.cpp:974-978, 153`:
    ```cpp
    if (Sectors.Mode == EFormationMode::Walk || Sectors.Mode == EFormationMode::Forward) {
        FramePaceLeader = &LeaderRef;
        FramePace = Config.PacePatrol;
    }
    ```
  - `Source/CodexTactics/Public/Formation/FormationTypes.h:191-194`
  - `Source/CodexTactics/Private/Formation/FormationTuning.cpp:54` (ключ `"pace.patrol"`)
- **Конфигурационный параметр**:
  - `PacePatrol` = **0.80** (80% от `WalkSpeed` -> **176 см/с**).
- **Причина в коде**:
  При обычном перемещении строем (режимы `Walk` и `Forward`) лидер отряда сознательно замедляется на 20%, чтобы замыкающий боец (`Rear guard`), идущий спиной или полубоком для контроля тыла, не отставал от группы.

---

### 6. Автоматическое торможение при приближении к слоту строя

- **Файлы и строки**:
  - `Source/CodexTactics/Private/Characters/SquadFormation.cpp:85-113`:
    ```cpp
    if (DistanceToSlot < Config.SlowRadius) {
        Speed = FMath::Max(Speed * (DistanceToSlot / Config.SlowRadius), Config.MinApproachSpeed);
    }
    ```
  - `Source/CodexTactics/Public/Characters/SquadFormation.h:86-97`
- **Конфигурационные параметры**:
  - `Config.SlowRadius` = **250 см** (2.5 метра).
  - `Config.MinApproachSpeed` = **30 см/с** (0.3 м/с!).
  - `Config.StopRadius` = **25 см** (остановка в ноль).
- **Поведение**:
  Когда ведомый боец подходит к своей точке строя ближе чем на 2.5 метра, его скорость плавно интерполируется вниз от нормальной до минимальной скорости подруливания **30 см/с**. Это выглядит со стороны как "замедленный шаг на цыпочках" перед фиксацией на месте.

---

### 7. Шаг назад и развороты тылового дозора (`Move.bBackward`)

- **Файлы и строки**:
  - `Source/CodexTactics/Private/Formation/FormationRuntimeRules.cpp:168-177`:
    ```cpp
    Move.bBackward = FormationMath::AngleBetween(FormationMath::YawOf(Velocity), FacingYaw) > BackwardAngleDeg;
    if (!bRun && Move.bBackward) {
        Cap = FMath::Min(Cap, static_cast<double>(WalkSpeed) * Config.MoveBackwardMult);
    }
    ```
  - `Source/CodexTactics/Public/Formation/FormationTypes.h:197-207` (ключ `"move.backward_mult"`, `"move.rear_turn_lag"`)
- **Конфигурационные параметры**:
  - `MoveBackwardMult` = **0.90** (90% от скорости шага).
  - `MoveRearTurnLag` = **150 см**: если замыкающий боец отстает от своего слота более чем на 1.5 м, он разворачивается вперед и ускоряется (`MoveCatchupMult = 1.25`), но внутри 1.5 м он пятится назад с ограничением скорости.

---

### 8. Доводка позиции у стены / в укрытии ("Shuffle in place")

- **Файлы и строки**:
  - `Source/CodexTactics/Private/Characters/SquadSubsystemFormation.cpp:1290-1293`
  - `SquadWallTuning::Get().WallPathBrakingCm`
- **Поведение**:
  При встраивании бойца в линию укрытия вдоль стены (`bWallOrder`) для предотвращения проскока и раскачки включается закон прибытия `FollowerMove`, который на последнем метре искусственно сдерживает скорость до диапазона **2–40 см/с**.

---

### 9. Движение по пандусам и лестницам "гуськом" (`Codex.Squad.RampFile`)

- **Файлы и строки**:
  - `Source/CodexTactics/Private/Characters/SquadSubsystemRampFile.cpp:32-37, 71-75`
  - `Source/CodexTactics/Public/Formation/RampFileRules.h:27-28` (`MinSlopeDeg = 12.0°`)
- **Консольная переменная**:
  - `Codex.Squad.RampFile` (по умолчанию `1`).
- **Поведение**:
  Если под ногами оперативника наклонный меш с углом >= 12°, отряд выстраивается в цепочку. Идущий следом боец принудительно останавливается (`Speed = 0` / пауза движения), пока впереди идущий не отойдет на дистанцию зазора `FileGapCm`. Это часто воспринимается как застревание или резкое замедление при входе на лестницу/пандус.

---

### 10. Подавление противником (`Suppression` / `Pinned`)

- **Файлы и строки**:
  - `Source/CodexTactics/Public/Combat/SuppressionRules.h:36-44, 68-74`
  - `Source/CodexTactics/Private/Characters/MarksmanEnemyCharacter.cpp:694`
  - `Content/Data/Combat/suppression.json`
- **Конфигурационные параметры**:
  - `tier.suppressed_enter` = **0.50** (50% шкалы подавления): запрещен пик из-за угла, стрельба только наугад.
  - `tier.pinned_enter` = **0.85** (85% шкалы подавления): боец прижат к земле, огонь прекращается, боец принудительно укладывается в `Prone` (или `Crouch`), снижая скорость до **61.6 см/с** (ползок) без возможности встать и побежать.

---

### 11. Задержки анимации смены стойки (`ComputeMoveHold`)

- **Файлы и строки**:
  - `Source/CodexTactics/Private/Characters/OperativeMovementRules.cpp:54-62`
  - `Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp:135, 242`
- **Поведение**:
  Пока проигрывается клип вставания с земли или перехода в укрытие, действует блокировка `ComputeMoveHold`. Приказ на перемещение откладывается (скорость 0) вплоть до завершения анимационного транзишена (обычно 0.8–1.5 с).

---

### 12. Погода и метель (`BlizzardWalk`)

- **Файлы и строки**:
  - `Source/CodexTactics/Public/Characters/BlizzardWalkRules.h:65-77`
  - `Source/CodexTactics/Private/Characters/OperativeAnimInstanceBlizzardWalk.cpp:5-47`
- **Поведение**:
  При открытой метели (`BlizzardAmount >= 0.5`) на открытом воздухе скорость набора холода возрастает в **2.0 раза** (`WeatherMultiplier = 2.0`). Это форсирует стремительный вход персонажей в тиры `Freezing` (0.45) и `Frostbite` (0.25). Кроме того, верхняя часть тела переключается в анимационную позу сопротивления ветру (`BlizzardWalk`), скрывая готовность оружия к мгновенному бою.

---

### 📊 Сводка порогов: что именно запускает "медленный шаг"?

| Причина | Порог / Условие | Результирующая скорость шага | Блокировка спринта? |
|---|---|---|---|
| **Ранение** | **HP < 50%** (`WoundedHealthThreshold = 0.5`) | **143 см/с** (x0.65) | **Да** (жестко) |
| **Озноб (Chills)** | Cold 40..69% | **154 см/с** (x0.70) | Нет (до 60% холода) |
| **Замерзание (Freezing)** | Cold 70..89% | **99 см/с** (x0.45) | **Да** (с 60% холода) |
| **Обморожение (Frostbite)** | Cold >= 90% | **55 см/с** (x0.25) | **Да** |
| **Ранен + Замерзает** | HP < 50% И Cold >= 70% | **64 см/с** (x0.29) | **Да** |
| **Ранен + Обморожен** | HP < 50% И Cold >= 90% | **35 см/с** (x0.16) | **Да** |
| **Приближение к строю** | Дистанция до слота < 2.5 м | Плавный спад до **30 см/с** | Лидер бежит / ведомый догоняет |
| **Узкий пандус/лестница** | Идущий впереди на лестнице | **0 см/с** (пауза до дистанции) | - |
| **Стойка Crouching** | Клавиша C / укрытие | **125 см/с** (x0.57) | Встает при беге |
| **Стойка Prone** | Клавиша Z / подавлен Pinned | **61.6 см/с** (x0.28) | **Да** |
| **Переноска груза** | Поднят предмет / боец | **66 см/с** (x0.30) | **Да** |

---

## ЧАСТЬ B: Анализ ассет-пака `Content/CON_Injured` и план анимационного слоя ранений

Пак `Content/CON_Injured` является неприкосновенным контентом дизайнера (junction-папка, доступ только на чтение).

---

### 1. Содержимое пака и скелетная привязка

Анализ бинарных манифестов `.uasset` показал, что пак содержит две параллельные реализации анимаций:

1. **UE4-версия (`Content/CON_Injured/Animations/In-Place/` и `RootMotion/`)**:
   - Скелет: `/Game/CON_Injured/EpicContent/Mannequin/Character/Mesh/UE4_Mannequin_Skeleton`.
   - **Идеально совместим со скелетами игровых оперативников**:
     - Мужской боец: `/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a` (базируется на `UE4_Mannequin_Skeleton`).
     - Женский боец: `/Game/Female_Soldier/Mesh/SK_Female_soldier` (базируется на `Female_Soldier_Skeleton`).
2. **UE5-версия (`Content/CON_Injured/Animations/UE5/`)**:
   - Скелет: `/Game/CON_Injured/EpicContent/Mannequins/Meshes/SK_Mannequin` (UE5 Manny/Quinn).
   - Используется для PMC-оперативников арены на UE5 скелете.

#### Полный перечень анимаций в паке:

- **Ходьба раненого (8 направлений, BlendSpace)**:
  - `Walk_Injured_BS.uasset` (Blend Space направления/скорости)
  - `In-Place/Walk_Injured_BS/Walk_Injured_Front.uasset`
  - `In-Place/Walk_Injured_BS/Walk_Injured_Front_Left.uasset`
  - `In-Place/Walk_Injured_BS/Walk_Injured_Front_Right.uasset`
  - `In-Place/Walk_Injured_BS/Walk_Injured_Left.uasset`
  - `In-Place/Walk_Injured_BS/Walk_Injured_Right.uasset`
  - `In-Place/Walk_Injured_BS/Walk_Injured_Back.uasset`
  - `In-Place/Walk_Injured_BS/Walk_Injured_Back_Left.uasset`
  - `In-Place/Walk_Injured_BS/Walk_Injured_Back_Right.uasset`
- **Прямолинейные циклы ходьбы и бега**:
  - `In-Place/Walk_Injured.uasset` (обычный раненый шаг)
  - `In-Place/Walk_Injured_Slow.uasset` (тяжелый медленный раненый шаг с волочением ноги)
  - `In-Place/Run_Injured.uasset` (раненая перебежка / бег в панике)
  - `In-Place/Crawl_Injured.uasset` (раненое ползание на животе/локте)
- **Айдлы раненого (состояния покоя)**:
  - `In-Place/Idle_Walk_Injured1.uasset`, `Idle_Walk_Injured2.uasset` (стойка раненого, держится за бок/рану)
  - `In-Place/Idle_Walk_Injured_Slow1.uasset`, `Idle_Walk_Injured_Slow2.uasset` (стойка тяжелораненого, согнувшись)
  - `In-Place/Idle_Run_Injured1.uasset`, `Idle_Run_Injured2.uasset` (одышка после раненого бега)
  - `In-Place/Idle_Crawl_Injured1.uasset`, `Idle_Crawl_Injured2.uasset` (лежание раненым на земле)
- **Root-Motion версии**:
  - `RootMotion/Walk_Injured_RM.uasset`, `Walk_Injured_Slow_RM.uasset`, `Run_Injured_RM.uasset`, `Crawl_Injured_RM.uasset`.

---

### 2. Сравнение с существующей реализацией слоя `prone_crawl`

В проекте уже реализована эталонная архитектура нативного подмешивания локомоции без модификации дерева ассетов `ABP_Operative`:

1. **Конфигурация данных**:
   - `Content/Data/Weapons/weapon_anim_sets.json` (блок `"prone_crawl"`).
   - Описывает пути к исходным клипам, параметры сглаживания направления (`direction_smooth_s`), время блендинга (`blend_seconds`), масштабирование шага (`stride_scale`).
2. **Пайплайн ретаргетинга (Editor Python)**:
   - `Scripts/Editor/setup_crawl_animation.py`.
   - Запускается автономно в редакторе (`-run=pythonscript`).
   - Читает клипы из внешнего пака, через встроенный транзиентный ретаргетер `M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/IK_UE4_Mannequin` копирует анимации в:
     - `/Game/Characters/Animations/Crawl/Male/`
     - `/Game/Characters/Animations/Crawl/Female/`
   - Измеряет фактическую скорость шага и фазы опоры конечностей, не оставляя мусора и не трогая исходный пак.
3. **C++ анимационный слой (Game Runtime)**:
   - `Source/CodexTactics/Private/Characters/OperativeAnimInstanceProneCrawl.cpp`.
   - `ProneCrawlRules.cpp` (чистая математика весов и направлений).
   - В `NativeUpdateAnimation` проверяется пригодность (`IsProneCrawlEligible()`), вычисляются угол движения относительно взгляда, скорость и интерполируется вес слоя `CrawlWeight`.
   - Слой плавно подмешивается на уровне полного тела через слот или нативную интерполяцию поз.
   - Управляется консольной переменной `Codex.Anim.ProneCrawlLayer`.

---

### 3. План реализации анимационного слоя ранений (`injured_locomotion`)

#### Этап 1: Добавление метаданных в `weapon_anim_sets.json`
Создать секцию `"injured_locomotion"`:
```json
"injured_locomotion": {
  "comment": "Injured walk, limp and crawl layers driven by HP thresholds and wound state (Content/CON_Injured).",
  "retargeted": {
    "UE4_Mannequin_Skeleton": "/Game/Characters/Animations/Injured/Male",
    "Female_Soldier_Skeleton": "/Game/Characters/Animations/Injured/Female"
  },
  "light_wound": {
    "idle": "/Game/CON_Injured/Animations/In-Place/Idle_Walk_Injured1",
    "walk_bs": "/Game/CON_Injured/Animations/In-Place/Walk_Injured_BS",
    "walk_fwd": "/Game/CON_Injured/Animations/In-Place/Walk_Injured"
  },
  "heavy_wound": {
    "idle": "/Game/CON_Injured/Animations/In-Place/Idle_Walk_Injured_Slow1",
    "walk": "/Game/CON_Injured/Animations/In-Place/Walk_Injured_Slow"
  },
  "run": {
    "idle": "/Game/CON_Injured/Animations/In-Place/Idle_Run_Injured1",
    "run": "/Game/CON_Injured/Animations/In-Place/Run_Injured"
  },
  "crawl": {
    "idle": "/Game/CON_Injured/Animations/In-Place/Idle_Crawl_Injured1",
    "crawl": "/Game/CON_Injured/Animations/In-Place/Crawl_Injured"
  },
  "blend_seconds": 0.35,
  "light_wound_hp_threshold": 0.50,
  "heavy_wound_hp_threshold": 0.25
}
```

#### Этап 2: Скрипт ретаргетинга `Scripts/Editor/setup_injured_animation.py`
- По аналогии с `setup_crawl_animation.py` создать Python-скрипт.
- Вход: `Content/CON_Injured/Animations/In-Place/*.uasset`.
- Выход:
  - Мужской скелет -> `Content/Characters/Animations/Injured/Male/`
  - Женский скелет -> `Content/Characters/Animations/Injured/Female/`
- Настройка: флаг `Root Motion = False`, ретаргет костей через `IK_UE4_Mannequin`.
- Исходный пак `Content/CON_Injured` остаётся строго нетронутым.

#### Этап 3: C++ логика в `OperativeAnimInstanceInjured.cpp` и `InjuredRules`
1. **Правила и пороги (`InjuredRules`)**:
   - **HP >= 50%**: Здоров, играет штатная локомоция.
   - **25% <= HP < 50% (Легкое/среднее ранение)**:
     - Стойка стоя: воспроизводится `Idle_Walk_Injured1` в покое и `Walk_Injured_BS` (8-направленная ходьба с хромотой) при движении.
     - Оружие удерживается, но с заметным наклоном корпуса в сторону раны.
   - **HP < 25% (Тяжелое ранение / агония)**:
     - Стойка стоя: воспроизводится `Idle_Walk_Injured_Slow1` и тяжелый шаг `Walk_Injured_Slow` (скорость падает, персонаж волочит ногу).
   - **Стойка лежа (Prone) + Ранение (HP < 50%)**:
     - Вместо спортивного `prone_crawl` включается тяжелое раненое ползание `Crawl_Injured` и покой `Idle_Crawl_Injured1`.
   - **Спринт / Паника при ранении**:
     - Если боец в панике (`PanicComponent`) бежит, воспроизводится `Run_Injured`.
2. **Блендинг поз (`UOperativeAnimInstance`)**:
   - Вес `InjuredWeight` интерполируется плавно (`FMath::FInterpTo`) за 0.35 секунды, исключая резкие скачки костей при получении урона.
   - Перебивается монтажами действий (бросок гранаты, перезарядка, нокдаун, смерть).
3. **Безопасное включение (Safety & Non-Regression)**:
   - Создание консольной переменной `Codex.Anim.InjuredLayer` (по умолчанию 1; 0 = возврат к текущей чистой локомоции).
   - Никаких правок пользовательского графа `ABP_Operative.uasset` — вся логика работает через нативный C++ интерфейс инстанса.
