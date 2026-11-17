# G004 план — стоимость замороженных тел выше 0.5 мс

Кода в этом коммите нет. Ветка `grok/G004-corpse-cost` от main `0f352ba2`. Роутер: level 3 | grok-4.7 (основная сессия; confidence 0.47, порог 0.60, route false) | план записан, поведение не менялось.

Бюджет 0.5 мс не поднимаю. Смоук `CorpseCostSmoke` не ослабляю. `EnemyAnimInstance`, ABP и `.uasset` не трогаю.

## Что уже измерено (не этот прогон)

`CorpseCostSmoke` (`Source/CodexTacticsDev/Private/Debug/CorpseCostSmokeCommand.cpp:91-97`) считает игровой поток как `GGameThreadTime` и валит прогон, если 150 замороженных минус 50 замороженных >= 0.5 мс. Это не время `UCorpseSubsystem::Tick`, а весь кадр игрового потока (QPC, включая ожидание физики и вытеснение).

Codex 037 (`docs/port/codex/outbox/037-red-smokes-triage.report.md`): 0.92 мс и повтор 1.70 мс. До рэгдолла (`docs/systems/animation-setup.md:68-69`) те же 0 / 50 / 150 давали игровой поток 4.3 / 5.6 / 5.7 мс, то есть +0.1 мс на сотню тел. Реестр: `Scripts/test_map.json`, карта `L_Gen_Depot_1`, rendered, timeout 600.

## Гипотеза по коду (ещё не доказана прогоном)

При активации (`CorpseSubsystem.cpp:238-243`) меш получает профиль `Ragdoll`, `QueryAndPhysics`, `SetAllBodiesSimulatePhysics(true)` и `bBlendPhysics = true`.

При заморозке (`CorpseSubsystem.cpp:527-533`) порядок такой:

1. `SetComponentTickEnabled(false)` — в UE 5.8 это снимает только cloth tick (`SkeletalMeshComponent.cpp:749-757`), не EndPhysics.
2. `PutAllRigidBodiesToSleep`, затем `SetAllBodiesSimulatePhysics(false)`. Регистрация EndPhysics смотрит `PrimaryComponentTick.IsTickFunctionRegistered()` (тик выключен, но функция всё ещё зарегистрирована) и `ShouldRunEndPhysicsTick` (`SkeletalMeshComponent.cpp:837-846`). Симуляция уже выключена, но коллизия ещё с физикой и `bBlendPhysics` ещё true, поэтому `ShouldBlendPhysicsBones` (`PhysAnim.cpp:398-402`) истинно и EndPhysics остаётся.
3. `SetCollisionEnabled(NoCollision)` фильтр меняет и состояние физики не уничтожает (`PrimitiveComponentPhysics.cpp:1376-1385`). Повторный вызов из `AEnemyCharacter::FreezeCorpse` уже no-op. `UpdateEndPhysicsTickRegisteredState` больше никто не зовёт.

Итог на каждое замороженное тело: тик EndPhysics в кадре и тела с ограничениями остаются в солвере. `SetAllBodiesPhysicsBlendWeight(0)` как раз снимает `bBlendPhysics` и зовёт `UpdateEndPhysicsTickRegisteredState` (`SkeletalMeshComponentPhysics.cpp:1783-1811`). Сейчас его нет. `OnDestroyPhysicsState` / `TermArticulated` (`SkeletalMeshComponentPhysics.cpp:1999-2020`) кости не переписывает. `GetPelvisBodyBottomZ` считается по формам PhysicsAsset и живому телу не нужен (`CorpseSubsystem.h:64-68`).

`UCorpseSubsystem::Tick` (`CorpseSubsystem.cpp:648-666`) по замороженным делает ранний `continue`. Счётчик `STAT_UCorpseSubsystem` этот хвост рэгдолла не видит.

Поза: последний blend уже в component space, `bPauseAnims` её держит. `SetRenderStatic` в этом движке рисует reference pose (`SkinnedMeshComponent.h:2191-2197`, `SkeletalRenderStatic.cpp` берёт статический буфер). Это сменит вид смерти. Не использую.

## Как докажу, до правки поведения

Все прогоны стоимости — на этой машине: `CODEX_SMOKE_REMOTE=off`. Сборка: `Scripts/build.ps1 -Remote worker` (exit 2 → локальный `Scripts/build.ps1`). `-Multiprocess` обязателен.

1. Три прогона одного `CorpseCostSmoke`, `-Jobs 1 -NoRetry`. В отчёт — четыре строки `[CorpseCost]` и строка `100 more frozen bodies`.
2. Один прогон под нагрузкой: `-Jobs 4` вместе с `DeathRagdollSmoke`, `KnockdownSmoke`, `MilitaryZombieSmoke`. Та же дельта.
3. Если в одиночку дельта стабильно < 0.5 мс, а под нагрузкой нет — это шум. В отчёте предложу честный замер (не применяю, бюджет не поднимаю).
4. Если в одиночку >= 0.5 мс: один диагностический коммит в `CorpseSubsystem.cpp`, cvar `Codex.Corpse.CostTrace` по умолчанию 0. При 1 раз в секунду лог: мс собственного `Tick`, сколько замороженных ещё `IsSimulatingPhysics`, `bBlendPhysics`, `IsPhysicsStateCreated`. Один смоук с `-PreCmds "Codex.Corpse.CostTrace 1"`. Поведение то же.

## Правка, только если пункт 4 подтвердится

Только `Source/CodexTactics/Private/Characters/CorpseSubsystem.cpp` (и объявление cvar в том же файле, если A/B нужен в одном бинаре). После замера таза и подъёма, не вместо них:

- `SetAllBodiesSimulatePhysics(false)` оставить (поза уже пишется так);
- сразу `SetAllBodiesPhysicsBlendWeight(0)` — снимает EndPhysics, пока коллизия ещё не мешает солверу отдать последний кадр;
- затем `NoCollision`, `bPauseAnims`, `SkipAllBones`, тик компонента выкл;
- `RecreatePhysicsState()` — при `NoCollision` состояние только уничтожается, кости не трогает.

Cvar `Codex.Corpse.ReleaseFrozenPhysics`, по умолчанию 1. `0` — сегодняшний порядок, один контрольный смоук. Новых чисел нет, JSON не меняю. Снимок в статическую позу и лимит «живых» рэгдоллов не трогаю, пока этот сброс не уложится в 0.5 мс: `max_simultaneous_ragdolls` уже 4 в `death_ragdoll.json`, на замороженные он не действует.

Гейт после фикса: `CorpsePersistSmoke` x3, `CorpseCostSmoke` x3 в одиночку (`CODEX_SMOKE_REMOTE=off`), `DeathRagdollSmoke`, `KnockdownSmoke`, `MilitaryZombieSmoke`, `OptimizedCharacterSmoke`, `python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok`. Юниты `CodexTactics.Characters.Corpse`, если дифф заденет правила.
