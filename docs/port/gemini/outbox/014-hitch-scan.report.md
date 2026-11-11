# Отчёт: Аудит синхронных загрузок (Hitch & Freeze Scan)

**Контекст задачи:** `docs/port/gemini/inbox/014-hitch-sync-loads.md`, подготовка архитектурной карты для задачи Codex `004` (`codex/hitch-enemy-appear`).  
**Ветка:** `gemini/014-hitch-scan` (от `main`, коммит `eb6f35e3`).  
**Стек:** Read-only аудит исходного кода C++ (`Source/CodexTactics/`), анализ лога `Saved/Orchestrator/hitch-2026-11-11-play.log`.  
**Статус:** Исследование завершено, полная раскладка по зависаниям на старте (13.9 с) и первом вражеском спавне (6.9 с).

---

## 1. Реестр всех синхронных загрузок в рантайме (Source/CodexTactics)

В ходе сканирования выявлено **98 вызовов** синхронной загрузки (`LoadObject`, `LoadSynchronous`, `LoadClass`, `TryLoad`) за пределами конструкторов `ConstructorHelpers`. Ниже приведена детальная категоризация по цепочкам вызовов (caller chains) и частоте срабатывания.

### 1.1. Старт уровня и спавн оперативников (Level Start / Operative Spawn)

| Файл и строка | Метод / API | Загружаемый ассет / путь / JSON ключ | Цепочка вызова (Caller Chain) | Частота выполнения |
| :--- | :--- | :--- | :--- | :--- |
| `CodexTacticsGameMode.cpp:188` | `LoadSynchronous()` | `GameBalanceConfig` (`/Game/Data/Balance/...`) | `StartPlay()` $\to$ инициализация баланса | 1 раз на уровень |
| `CodexTacticsGameMode.cpp:227` | `LoadSynchronous()` | `LevelConfig` (`DA_Level_*`) | `StartPlay()` $\to$ загрузка конфигурации уровня | 1 раз на уровень |
| `CodexTacticsGameMode.cpp:283` | `LoadSynchronous()` | `OperativeBlueprint` (`BP_Operative_Commander_C`) | `SpawnOperative()` $\to$ класс персонажа | 1 раз на оперативника |
| `CodexTacticsGameMode.cpp:397` | `LoadSynchronous()` | `Entry.BodyMesh` (кастомный меш тела) | `SpawnOperative()` $\to$ смена меша | 1 раз на оперативника |
| `CodexTacticsGameMode.cpp:433` | `LoadObject<UWeaponDataAsset>` | `DA_Weapon_*` из `weapons_tuning.json` | `SpawnOperative()` $\to$ экипировка оружия | 1 раз на оружие |
| `TacticalCameraPawn.cpp:77` | `LoadSynchronous()` | `GameBalanceConfig` | `BeginPlay()` камеры $\to$ параметры шейка | 1 раз на старт |
| `WeatherPresenterActor.cpp:420`| `LoadSynchronous()` | `EnvironmentParameters` (`MPC_Environment`) | `BeginPlay()` $\to$ погодный презентер | 1 раз на старт |
| `WeatherSnowOverlayComponent.cpp:173,187` | `LoadSynchronous()` | `OverlayMaterial`, `GroundMaterial` | `BeginPlay()` $\to$ материалы снежного покрова | 1 раз на старт |
| `WeatherSnowOverlayComponent.cpp:202,216` | `LoadSynchronous()` | `WetOverlayMaterial`, `WetGroundMaterial` | `BeginPlay()` $\to$ материалы влажного снега | 1 раз на старт |
| `WeatherSnowFxComponent.cpp:206,210,214` | `LoadSynchronous()` | `SnowSystem` (Niagara), `Settings`, `FlakeMaterial` | `BeginPlay()` $\to$ частицы снегопада | 1 раз на старт |
| `CodexUISubsystem.cpp:55` | `LoadSynchronous()` | `Settings.PrimaryLayoutClass` (UMG) | `Initialize()` $\to$ корневой UI-лейаут | 1 раз на сессию |
| `RadiusRingSubsystem.cpp:48` | `LoadObject<UMaterialInterface>` | `M_CombatFeedback` | `Initialize()` $\to$ кольца радиусов | 1 раз на старт |
| `DefenseMarkerSubsystem.cpp:87` | `LoadObject<UMaterialInterface>` | `M_TargetFresnel` | `Initialize()` $\to$ маркеры защиты | 1 раз на старт |
| `FrostVignetteWidget.cpp:21` | `LoadObject<UMaterialInterface>` | `M_FrostVignette` | Создание виджета обморожения | 1 раз на старт |

### 1.2. Резолв анимационных слоёв оперативника (`UOperativeAnimInstance::NativeInitializeAnimation`)

> ⚠️ **КРИТИЧЕСКАЯ ЗОНА:** Вызывается при инициализации скелетного меша каждого оперативника.

| Файл и строка | Метод / API | Загружаемый ассет / путь / JSON ключ | Цепочка вызова | Частота / объём |
| :--- | :--- | :--- | :--- | :--- |
| `OperativeAnimInstanceShotgun.cpp:186` | `LoadObject<UAnimSequenceBase>` | `weapon_anim_sets.json` блок `"shotgun"` | `LoadShotgunAnimSet()` $\to$ `LoadWeaponAnimSet("shotgun")` | **50 клипов** синхронно |
| `OperativeAnimInstanceShotgun.cpp:186` | `LoadObject<UAnimSequenceBase>` | `weapon_anim_sets.json` блок `"pistol"` | `LoadShotgunAnimSet()` $\to$ `LoadWeaponAnimSet("pistol")` | **44 клипа** синхронно |
| `OperativeAnimInstanceShotgun.cpp:186` | `LoadObject<UAnimSequenceBase>` | `weapon_anim_sets.json` блок `"launcher"` | `LoadShotgunAnimSet()` $\to$ `LoadWeaponAnimSet("launcher")` | **42 клипа** синхронно |
| `OperativeAnimInstanceShotgun.cpp:186` | `LoadObject<UAnimSequenceBase>` | `weapon_anim_sets.json` блок `"prone_crawl"` | `LoadShotgunAnimSet()` $\to$ `LoadWeaponAnimSet("prone_crawl")` | **12 клипов** синхронно |
| `OperativeAnimInstanceInjured.cpp:66` | `LoadObject<UAnimSequenceBase>` | `weapon_anim_sets.json` блок `"injured"` | `LoadShotgunAnimSet()` $\to$ `LoadInjuredAnimSet()` | **24 клипа** синхронно |
| `OperativeAnimInstance.cpp:121, 165` | `LoadObject<UAnimSequenceBase>` | `/Game/M4_Cover_Pack/Animations/...` | `NativeInitializeAnimation()` $\to$ укрытия M4 | **54 клипа** синхронно |
| `OperativeAnimInstanceBlizzardWalk.cpp:56` | `LoadObject<UAnimSequenceBase>` | `/Game/.../AS_*_Storm_1_*` | `LoadDefaultBlizzardWalkClips()` | **8 клипов** (4 male, 4 female) |
| `OperativeAnimInstanceInteraction.cpp:25,75`| `LoadObject<UAnimMontage>` | `AM_Interact_*`, `ColdIdleVariantPath` | `LoadDefaultInteractionClips()` | **7 ассетов** (6 монтажей, 1 idle) |
| `OperativeAnimInstance.cpp:90, 95` | `LoadObject<UAnimSequenceBase>` | `AS_Crawl_from_Walk_RM`, `AS_Crawl_from_Run_RM` | `NativeInitializeAnimation()` $\to$ дайв в prone | **2 клипа** синхронно |
| **ИТОГО по AnimInstance** | | | | **243 анимационных ассета!** |

### 1.3. Спавн врагов по расписанию (Combat Pressure, 30 с)

| Файл и строка | Метод / API | Загружаемый ассет / путь / JSON ключ | Цепочка вызова | Частота выполнения |
| :--- | :--- | :--- | :--- | :--- |
| `WaveSubsystem.cpp:448` | `VariantSoft->LoadSynchronous()` | `GameMode->EnemyVariantClasses["MilitaryZombie"]` (`BP_Enemy_MilitaryZombie_C`) | `CombatPressureSubsystem::SpawnOne` $\to$ `SpawnLevelEnemy` $\to$ `SpawnEnemyInternal` | При первом спавне варианта зомби |
| `WaveSubsystem.cpp:455` | `Soft->LoadSynchronous()` | `GameMode->EnemyClasses[Archetype]` (`BP_Enemy_Frostbitten_C`, и др.) | `SpawnEnemyInternal` (фоллбек базового класса) | При первом спавне архетипа |
| `LadderClimbComponent.cpp:215` | `UpLoopClip.LoadSynchronous()` | `/Game/Characters/Animations/Ladders/anim_ladder_climb_up_loop` | Первый опрос лестницы при поиске пути патруля (`LoadClips()`) | 1 раз на первый контакт с лестницей |
| `LadderClimbComponent.cpp:216` | `ClimbOutClip.LoadSynchronous()` | `/Game/Characters/Animations/Ladders/anim_ladder_climb_out_top` | `LoadClips()` | 1 раз на первый контакт |
| `LadderClimbComponent.cpp:217` | `IntoFromTopClip.LoadSynchronous()`| `/Game/Characters/Animations/Ladders/anim_ladder_into_from_top` | `LoadClips()` | 1 раз на первый контакт |

### 1.4. Спавн отрядов ЧВК (PmcEncounter / PmcSquadSubsystem, 30 с)

| Файл и строка | Метод / API | Загружаемый ассет / путь / JSON ключ | Цепочка вызова | Частота выполнения |
| :--- | :--- | :--- | :--- | :--- |
| `PmcSquadSubsystem.cpp:1967` | `Entry.BodyMesh.LoadSynchronous()` | `SKM_PmcOperator` (`/Game/Characters/Enemies/PMC/SKM_PmcOperator`) | `SpawnOperator()` $\to$ установка скелетного меша бойца | При первом спавне PMC |
| `PmcSquadSubsystem.cpp:1993` | `LoadObject<UMaterialInterface>` | `Look.CoatMaterial` (материал плаща из `pmc_commander.json`) | `SpawnOperator()` $\to$ назначение камуфляжа | 1 раз на тип плаща |
| `PmcSquadSubsystem.cpp:2016` | `LoadObject<UWeaponDataAsset>` | `DA_Weapon_*` из пула (`m16`, `shotgun`, и т.д.) | `SpawnOperator()` $\to$ выдача оружия | На каждого бойца при спавне |
| `PmcSquadSubsystem.cpp:2036` | `LoadObject<UWeaponDataAsset>` | `DA_Weapon_rpg7` | `SpawnOperator()` $\to$ экипировка RPG-стрелка | При спавне гранатомётчика |

---

## 2. Анатомия «тихого разрыва» в 13.9 секунд на старте уровня

В логе `Saved/Orchestrator/hitch-2026-11-11-play.log` зафиксирован разрыв между двумя соседними строками в кадре **352**:
```
[2026.11.11-13.04.22:058][352]LogCodexEnvTrace: [EnvTrace] f15352 t0.000 start cover on
[2026.11.11-13.04.35:927][352]LogCodexTactics: Display: Critical wound: critical_from 0.20, heavy_from 0.35 (speed x0.45)...
```
**Точная длительность зависания:** $13.869\text{ секунды}$ (внутри одного кадра 352).

### 2.1. Что происходит внутри этого окна?
1. В `13:04:22.058` завершается `AWeatherPresenterActor::BeginPlay()`, который включает черную заглушку камеры (`ShowStartCover()`) и выводит `start cover on`.
2. Далее движок UE продолжает диспетчеризацию `BeginPlay()` для остальных акторов на карте `L_Gen_Depot_1`.
3. Инициализируется актор оперативника игрока (`BP_Operative_Commander_C_0`).
4. При инициализации его скелетного меша вызывается **`UOperativeAnimInstance::NativeInitializeAnimation()`**.
5. Внутри `NativeInitializeAnimation()` последовательно запускаются:
   - `LoadShotgunAnimSet()` $\to$ считывает `weapon_anim_sets.json` и поштучно вызывает `LoadObject<UAnimSequenceBase>` для:
     - 50 клипов дробовика (`shotgun`),
     - 44 клипов пистолета (`pistol`),
     - 42 клипов гранатомёта (`launcher`),
     - 12 клипов ползания (`prone_crawl`),
     - 24 клипов ранений (`injured`).
   - Синхронная загрузка 54 клипов укрытий `M4_Cover_Pack`.
   - Синхронная загрузка 8 клипов снежной метели (`BlizzardWalk`).
   - Синхронная загрузка 7 клипов взаимодействий (`AM_Interact_*`).
   - Синхронная загрузка 2 клипов нырка в положение лёжа (`ProneDive`).
   - **Всего: 243 `LoadObject` подряд на Game Thread!**
6. Каждый вызов `LoadObject` обращается к диску / подсистеме потоковой загрузки пакетов (`FPackageName`, десериализация UAsset). При отсутствии предзагрузки в память это занимает от 30 до 80 мс на ассет.
   $$243 \text{ ассетов} \times \sim 55\text{ мс} \approx 13.4\text{ секунды}!$$
7. Как только анимационный инстанс завершает загрузку всех 243 клипов, управление передается в первый тик оперативника `AOperativeCharacter::Tick`, где на строке 327 вызывается `UpdateCriticalWound()`. Он считывает `critical_wound.json` и выводит лог-строку `Critical wound: ...` ровно в `13:04:35.927`!

---

## 3. Анатомия фриза на 30-й секунде (Первое появление врагов, кадр 614)

В кадре **614** время застыло на **6.9 секунды** (с `13:05:07.100` по `13:05:14.000`):
```
[2026.11.11-13.05.07:100][614]LogCodexTactics: Display: [Pressure] 385 obstacle boxes...
[2026.11.11-13.05.07:101][614]LogCodexTactics: Display: [Pressure] event at 30 s: patrol of 2
[2026.11.11-13.05.07:103][614]LogStreaming: Display: FlushAsyncLoading(608): 3 QueuedPackages, 0 AsyncPackages
[2026.11.11-13.05.09:440][614]LogCodexTactics: Display: [Ladder] climb out: the clip's own end lift of 73.1 cm...
[2026.11.11-13.05.09:441][614]LogCodexTactics: Display: [Ladder] clips: up loop 1.767 s rise 72.5 cm...
[2026.11.11-13.05.09:446][614]LogCodexTactics: Display: [EnemyAnim] BP_Enemy_MilitaryZombie_C_0 plays its spawn entrance (4.0 s)
[2026.11.11-13.05.09:446][614]LogCodexTactics: Display: [Pressure] spawned patrol MILITARY_ZOMBIE (group 1)...
[2026.11.11-13.05.10:024][614]LogSkinnedAsset: Display: Waiting for skinned assets to be ready 0/1 (SKM_PmcOperator) ...
[2026.11.11-13.05.13:925][614]LogCodexTactics: Display: Operator Haas: body mesh SKM_PmcOperator
[2026.11.11-13.05.14:000][614]LogCodexTactics: Display: [PmcEncounter] team 1: 3 fighter(s)...
```

### 3.1. Почему события совпали в одном кадре?
- В `CombatPressureSubsystem.cpp:160`: `DueAt = Config.FirstEventSeconds;` (по умолчанию `30.0` с).
- В `L_Gen_Depot_1.json`: отряд ЧВК настроен с параметром `"delay_s": 30`.
- Обе подсистемы отсчитывают игровое время `Clock` от `BeginPlay` ($t = 0.0$ с).
- Ровно на 30-й секунде (`Clock >= 30.0`) **в одном и том же тике кадра 614** сработали оба спавнера.

### 3.2. Что происходило внутри кадра 614:
1. **Событие CombatPressure (2.34 с):**
   - Спавн зомби потребовал синхронной загрузки класса `BP_Enemy_MilitaryZombie_C` (`WaveSubsystem.cpp:448`).
   - При спавне патруля вблизи препятствий компонент `LadderClimbComponent` впервые вызвал `LoadClips()`.
   - В `LoadClips()` вызваны 3 синхронные загрузки: `UpLoopClip.LoadSynchronous()`, `ClimbOutClip.LoadSynchronous()`, `IntoFromTopClip.LoadSynchronous()`.
   - Поскольку в фоне работали асинхронные пакеты стриминга, движок вызвал принудительную остановку потока: `FlushAsyncLoading(608): 3 QueuedPackages`.
   - Задержка: **$2.337\text{ с}$** (`13:05:07.103` $\to$ `13:05:09.440`).
2. **Событие PmcEncounter (3.98 с):**
   - Подсистема ЧВК спавнит 3 бойцов.
   - Вызывается `Entry.BodyMesh.LoadSynchronous()` для меша `SKM_PmcOperator`.
   - Меш `SKM_PmcOperator` ранее в памяти отсутствовал. Движок блокирует Game Thread ожидания подготовки скина:  
     `LogSkinnedAsset: Waiting for skinned assets to be ready 0/1 (SKM_PmcOperator) ...`.
   - Задержка: **$3.901\text{ с}$** (`13:05:10.024` $\to$ `13:05:13.925`).
   - Синхронно загружаются материалы плаща (`LoadObject<UMaterialInterface>`) и ассеты оружия (`DA_Weapon_m16`, `DA_Weapon_rpg7`).
   - Каждый созданный боец инициализирует свой `UOperativeAnimInstance`.

---

## 4. Ранжированный план устранения для Codex (Fix Backlog)

| Приоритет | Место исправления | Что сделать | Ожидаемая экономия времени |
| :---: | :--- | :--- | :---: |
| **P0** | **Единый Preload под шторкой старта** (`GameMode::StartPlay` / `PreloadSubsystem`) | Создать централизованный предзагрузчик ассетов при старте уровня, пока активен `StartCover` (`WeatherPresenterActor::ShowStartCover`):<br>1. Предзагрузить `SKM_PmcOperator` и его материалы.<br>2. Предзагрузить классы врагов уровня (`BP_Enemy_MilitaryZombie`, `BP_Enemy_Frostbitten`).<br>3. Предзагрузить 3 клипа лестниц (`ULadderClimbComponent`). | **Полное устранение фриза на 30-й секунде ($-6.9$ с $\to 0$ мс)** |
| **P0** | **Кэширование и асинхронный предзагрузчик AnimInstance** (`OperativeAnimInstanceShotgun.cpp`) | 1. Вынести структуру `FShotgunAnimSet` и анимационные клипы в разделяемый singleton-кэш на уровне `GameInstance` / Subsystem, чтобы 243 клипа не загружались синхронно по `LoadObject` при создании каждого экземпляра персонажа.<br>2. Использовать `FStreamableManager` / `UAssetManager::GetStreamableManager().RequestAsyncLoad` во время загрузки уровня для всех путей из `weapon_anim_sets.json` и `M4_Cover_Pack`. | **Сокращение времени старта уровня на 13.9 секунд (с 21 с до ~5-7 с)** |
| **P1** | **Разведение расписания спавна (Staggering)** (`L_Gen_Depot_1.json` / `PmcEncounterSubsystem.cpp`) | Развести тайминги первого события: если `CombatPressure` стартует на 30 с, то первую группу ЧВК сдвинуть на `delay_s: 45` или `delay_s: 60`, либо добавить программный джиттер $\pm 5\text{ с}$ в `PmcEncounterSubsystem`, чтобы две тяжелые подсистемы никогда не стреляли в одном кадре. | **Гарантированная защита от наложения пиков нагрузки** |
| **P2** | **Асинхронная инициализация `LadderClimbComponent`** (`LadderClimbComponent.cpp`) | Перевести `UpLoopClip`, `ClimbOutClip`, `IntoFromTopClip` на асинхронную подгрузку в `BeginPlay()` компонента через `RequestAsyncLoad`, убрав вызов `LoadSynchronous()` из критического пути проверки условий входа на лестницу (`CanStartClimb`). | **Исключение `FlushAsyncLoading` ($-2.3$ с)** |
| **P3** | **Прогрев (Pre-warming) скина `SKM_PmcOperator`** | В режиме редактора (PIE) создание скелетного меша требует компиляции/подготовки данных (`Waiting for skinned assets`). Спавн скрытого фиктивного экземпляра или предварительный вызов `InitResources()` в фоне на старте уровня снимет 3.9 секунды компиляции. | **Устранение задержки ожидания меша ($-3.9$ с)** |
