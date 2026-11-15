# Отчёт 046: Предложения по закрытию BLOCKING пробелов в docs/design/audio/EVENTS.md

**Дата:** 2026-11-14  
**Ветка:** `gemini/046-audio-events`  
**Базовый коммит:** `664d78b2` (`main`)  
**Автор:** Gemini (Google Antigravity)  

---

## 1. Постановка задачи
В документе `docs/design/audio/EVENTS.md` раздел `0. Accessibility Audit` зафиксировал 4 критических `BLOCKING` пробела (`SOUND_BIBLE.md §7`: каждый критичный звук ОБЯЗАН иметь визуальный аналог).
Задача 046:
- Для каждого пробела составить детальное предложение: событие, аудио кью, точное место вызова в C++ (`файл:строка`) для звука и для спавна/отрисовки визуального индикатора.
- Оформить в виде отдельного раздела в `docs/design/audio/EVENTS.md`.
- **Строго без правок C++ кода проекта.**

---

## 2. Разработанные предложения (Раздел 5 в `EVENTS.md`)

### 2.1 A11Y-BLK-01: Свист взрывателя мины (`sfx_trap_mine_whistle`) и круг опасности
- **Событие / Кью:** `sfx_trap_mine_whistle` (`USoundCue` / MetaSound: резкий щелчок ударника 0.08 с + нарастающий металлический свист пружины 0.37 с, `2.2 -> 3.8 kHz`, шина `SFX`, приоритет 1, дакинг музыки `-10 dB`).
- **Вызов звука в C++:** `Source/CodexTactics/Private/Interactables/ProximityMineActor.cpp:208-212` в методе `AProximityMineActor::Tick()`. При входе бойца в `TriggerRadius` включается задержка детонации `PreDetonationFuseSeconds = 0.45f` и запускается звук через `UGameplayStatics::PlaySoundAtLocation`.
- **Визуальный аналог:** Пульсирующий красный декаль-круг на земле (`UDecalComponent` / `M_DangerRadius_Decal`) с радиусом взрыва `ExplosionRadius`, дающий возможность среагировать и отбежать.
- **Вызов визуала в C++:** `Source/CodexTactics/Private/Interactables/ProximityMineActor.cpp:209-211` через `UCombatFeedbackSubsystem::SpawnDangerRadiusRing(...)`.

### 2.2 A11Y-BLK-02: Огонь из-за экрана (`sfx_wpn_incoming_fire`) и направленный индикатор
- **Событие / Кью:** `sfx_wpn_incoming_fire` (сверхзвуковой хлыстовой свист / щелчок пролетающей пули `sfx_wpn_bullet_whiz_01..05`, шина `SFX`, приоритет 1, дакинг эмбиента `-3 dB`).
- **Вызов звука в C++:** `Source/CodexTactics/Private/Combat/SuppressionSubsystem.cpp:386-390` в `USuppressionSubsystem::ReportRound()` при `!Round.bAtEnemies` и `Impact == ESuppressionImpact::NearMiss` или `CoverStrike` около бойца.
- **Визуальный аналог:** Направленный дуговой шеврон на краю экрана (Screen-Edge Fire Indicator), указывающий 2D-угол на атакующего (жёлтый при подавлении/пролёте, красный при попадании).
- **Вызов визуала в C++:**
  - При пролёте мимо / подавлении: `Source/CodexTactics/Private/Combat/SuppressionSubsystem.cpp:389`.
  - При прямом попадании: `Source/CodexTactics/Private/Characters/OperativeCharacterHealth.cpp:474` (по `LastHitSource`).

### 2.3 A11Y-BLK-03: Свист миномётной мины (`sfx_wpn_mortar_whistle`) и зона поражения
- **Событие / Кью:** `sfx_wpn_mortar_whistle` (падающий по эффекту Доплера свист 1.5 с, `3.5 kHz -> 800 Hz`, шина `SFX`, приоритет 1, дакинг музыки и эмбиента `-4 dB`).
- **Вызов звука в C++:** `Source/CodexTactics/Private/Combat/ProjectileActor.cpp:145-160` в `AProjectileActor::Fly()` при `Flight.Trajectory == EProjectileTrajectory::HighAngle` и оставшемся времени полёта `<= 1.5f`.
- **Визуальный аналог:** Пульсирующий красный декаль-маркер зоны поражения на земле в точке `Shot.Aim` (радиус `Config.Round.Explosion.RadiusCm`) + краевой шеврон на экране, если точка за пределами видимости камеры.
- **Вызов визуала в C++:** `Source/CodexTactics/Private/Combat/MortarNestActor.cpp:366` в `AMortarNestActor::FireRound()` при отправке `OnRoundFired(this, Shot)` через `UCombatFeedbackSubsystem::SpawnMortarWarningZone(...)`.

### 2.4 A11Y-BLK-04: Стук сердца при критическом ранении (`sfx_ui_low_hp_heartbeat`) и индикация на портрете
- **Событие / Кью:** `sfx_ui_low_hp_heartbeat` (низкочастотный глухой ритмичный пульс 60-80 BPM, шина `UI`, приоритет 1, мягкий фильтр верхних частот для звуков окружения).
- **Вызов звука в C++:**
  - Старт: `Source/CodexTactics/Private/Characters/OperativeCharacterStance.cpp:387-397` при переходе в `ECriticalWoundAction::Down`.
  - Стоп: `Source/CodexTactics/Private/Characters/OperativeCharacterStance.cpp:402-411` при `ECriticalWoundAction::MayStand` (после лечения) или смерти.
- **Визуальный аналог:** Непрерывная пульсирующая красная рамка слота портрета раненого бойца в панели отряда (`CodexTacticsHUD`) и красный значок перевязки/кровотечения на полоске HP.
- **Вызов визуала в C++:** `Source/CodexTactics/Private/UI/CodexTacticsHUD.cpp:1607-1620` в цикле отрисовки состояния бойцов `ACodexTacticsHUD::DrawSquadStatus` по условию `Member->IsCriticallyWounded()`.

---

## 3. Статус
Документ `docs/design/audio/EVENTS.md` дополнен разделом 5 со всеми требованиями. Исходный код C++ не изменялся. Готово к фиксации.
