# 009 - Карта реализации аудио-подсистемы `UCodexAudioSubsystem` (Draft, Gemini 2026-11-12)

> Статус: read-only спецификация для Codex (C++ + Editor Python импортер). Источники: `docs/design/AUDIO_PIPELINE.md` (раздел 5), `Content/Data/Audio/voice_lines.json`, `Content/Data/Audio/music_cues.json`, `Saved/AudioGen/catalog.json`.

---

## 1. Общая архитектура (без тикера в горячем цикле, только события + редкий таймер состояния)

- **`UCodexAudioSubsystem : public UWorldSubsystem`** (или `UGameInstanceSubsystem` для сохранения `menu`-музыки и громкостей между уровнями; рекомендуется `UGameInstanceSubsystem` + подписка на `FWorldDelegates::OnPostWorldInitialization` / `OnWorldCleanup`, либо `UWorldSubsystem` для мировых источников + `UGameInstanceSubsystem` для микшера).
  - **Почему `UGameInstanceSubsystem` для музыки и настроек микшера предпочтительнее:** `menu` играет на фронтенд-карте (`UCodexFrontendSubsystem::IsFrontendWorld(World)` — `Source/CodexTactics/Public/UI/Frontend/CodexFrontendSubsystem.h:57`) и может кроссфейдиться при загрузке уровня; пользовательские громкости (`Music / VO / SFX / UI`) также живут на уровне `GameInstance` / `SaveGame`.
  - Без `UTickableWorldSubsystem::Tick` на 120 Гц: достаточно:
    1) прямой подписки на делегаты `UGameMessageSubsystem`, `UGameFlowSubsystem`, `UEnvironmentSubsystem`, `UCodexEventBus`;
    2) лёгкого `FTimerHandle` раз в `0.25–0.5 c` только для проверки эвристик напряжения (`tension`: видимость врагов / pressure / патрули).
- **Побочные правила данных (Data-driven):**
  - Чистый табличный/правильный модуль `Source/CodexTactics/Public/Audio/AudioRules.h` (+ `Private/Audio/AudioRules.cpp`) для чистых функций (без `UWorld`) и юнит-тестов: выбор музыкального состояния по приоритету, проверка cooldown/min-hold, подбор клипа по тексту HUD-сообщения и говорящему.
  - Конфиг громкостей и кулдаунов в `Content/Data/Audio/audio_config.json` (создать при реализации).

---

## 2. Существующие точки входа для реплик (Barks & HUD Message Events)

### Главный единый перехватчик
Весь текст реплик бойцов и HQ проходит через **`UGameMessageSubsystem::PostMessage`** (`Source/CodexTactics/Private/UI/GameMessageSubsystem.cpp:6-22`):
- Делегат 1: **`UGameMessageSubsystem::OnMessagePosted`** (`Public/UI/GameMessageSubsystem.h:46`, `FOnGameMessagePosted(const FGameMessage& Message)` — где `Message.Speaker`, `Message.Text`, `Message.PostedAt`).
- Делегат 2: **`UCodexEventBus::OnDialogueLineDisplayed`** (`Public/Subsystems/CodexEventBus.h:66`, `FCodexLineEvent(const FString& Speaker, const FString& Text)` — транслируется прямо из `UGameMessageSubsystem::PostMessage` на строке `GameMessageSubsystem.cpp:10`).

Подписка на **`UGameMessageSubsystem::OnMessagePosted`** (per-world) или **`UCodexEventBus::OnDialogueLineDisplayed`** (per-GameInstance) сразу ловит **все 49 озвученных реплик v1** без правки мест вызова! Для 3D-позиционирования (`SpawnSoundAtLocation` / `SpawnSoundAttached`) нужно лишь сопоставить `Speaker` (локализованный/текстовый `DisplayName`) с живым `AOperativeCharacter` из `USquadSubsystem::GetMembers()` / `UPmcSquadSubsystem` (или играть 2D для радио `Commander` / `HQ` на квестах).

### Полная таблица существующих реплик в коде (для маппинга в `AudioRules` / `voice_lines.json`)

| Категория | Спикер в HUD | Текст (нормализация без эмодзи-префиксов) | Источник `file:line` | ID в `voice_lines.json` |
|---|---|---|---|---|
| Тревога / контакт | `Commander` | `"Contact!"` | `Private/Combat/WaveSubsystem.cpp:333` | `cmd_contact` |
| Засада | `Commander` | `"Ambush! Hostiles on the level - eliminate them!"` | `Private/Combat/WaveSubsystem.cpp:302` | `cmd_ambush` |
| Волна | `Commander` | `"Wave %d: enemies advancing..."` | `Private/Combat/WaveSubsystem.cpp:375` | `cmd_wave_advancing` |
| Автономия вкл/выкл | `Commander` | `"Autonomy ON..."` / `"Autonomy OFF..."` | `Private/Characters/SquadSubsystem.cpp:638-639` | `cmd_autonomy_on`, `cmd_autonomy_off` |
| Старт/приказ | `SQUAD` / `Commander` | `"Orders received! Moving out!"` | `Private/Core/CodexTacticsPlayerControllerTime.cpp:87` | `cmd_orders_received` |
| Подавление | `SQUAD` / Leader | `"Suppressive fire! Keep their heads down!"` | `Private/Core/CodexTacticsPlayerControllerOrders.cpp:1066` | `cmd_suppressive_fire` |
| Выход открыт | `Commander` | `"The blast door is wide open - that's our way out. Everybody through!"` | `Private/GameFlow/LevelExitSubsystem.cpp:222` (`MissionRules.cpp:139`) | `cmd_gate_exit_open` |
| Поводок соло | Leader | `"Solo range exceeded..."` | `Private/Characters/SquadSubsystem.cpp:1105` | `cmd_solo_leash` |
| Квест: дизель/генератор | `Commander` / `Engineer` / `Medic-Sapper` | Строки генератора и канистры | `Private/Quests/QuestChain.cpp:40,50,58,69,74`, `Private/World/InteractableActor.cpp:602` | `cmd_generator_starts`, `eng_generator_*`, `eng_draining_diesel`, `eng_found_canister`, `med_apc_tank_needs_canister` |
| Разбор укреплений | `Engineer` | `"Surviving fortifications dismantled..."` | `Private/Combat/WaveVictorySubsystem.cpp:288` | `eng_fortifications_dismantled` |
| Перезарядка | `Operative->DisplayName` (Наши + PMC) | `"Reloading! Cover me!"`, `"Mag empty! Cover the sector!"`, `"Changing mags, hold them off!"`, `"Reloading! Watch my back!"` | `Private/Characters/OperativeCharacterWeapons.cpp:276-279` | `med_reloading`, `eng_changing_mags`, `med_watch_back`, `pmc1_reloading`, `pmc2_changing_mags` |
| Граната | `Operative->DisplayName` (Наши + PMC) | `"Grenade out! Get down!"`, `"Grenade away! Heads down!"` | `Private/Characters/OperativeCharacterWeapons.cpp:394-395` | `med_grenade_out`, `med_grenade_away`, `pmc3_grenade_out` |
| Укрытие / стена / угол | `Operative->DisplayName` (Наши + PMC) | `"At the corner - covering it!"`, `"Hugging the wall, holding the corner!"`, `"Behind cover!"`, `"In cover, holding the sector!"`, `"Behind the barricade..."` | `Private/Characters/OperativeCharacterCover.cpp:326-327`, `Private/Characters/OperativeCharacterMovement.cpp:546-547` | `eng_corner_peek`, `eng_hugging_wall`, `eng_behind_cover`, `med_in_cover`, `med_barricade_cover`, `pmc1_*`, `pmc2_*`, `pmc3_*` |
| Приказ укрытия / сдвиг | `Operative->DisplayName` | `"Take cover!"`, `"Dash to the other cover!"`, `"Along the wall, round the corner."`, `"Covering the corner - the squad stacks up behind me."` | `Private/Core/CodexTacticsPlayerControllerCover.cpp:275,958,1501` | `eng_take_cover`, `eng_dash_other_cover`, `eng_along_wall_corner`, `eng_cover_group_stack` |
| Охрана точки | `Operative->DisplayName` | `"Guard point set! Holding the sector."`, `"Off guard, regrouping!"` | `Private/Characters/SquadSubsystem.cpp:1444-1445` | `eng_guard_on`, `eng_guard_off` |
| Растяжка | `Medic-Sapper` | `"Tripwire! Wire at shin height - everyone hold position!"` | `Private/World/TripwireActor.cpp:355` | `med_tripwire` |
| Ранение / лечение / холод | `Operative->DisplayName` | Критическое ранение / перевязка / отогрев | `Private/Characters/OperativeCharacterStance.cpp:390,404`, `Private/Survival/TacticalHeatSubsystem.cpp:183` | `med_critically_down`, `med_patched_up`, `med_thawed` |
| Ящик лута | `Operative->DisplayName` | `"Took ALL supplies from the supply crate!"` | `Private/Interactables/InteractionSubsystem.cpp:552` | `med_looted_crate` |
| Паника / ярость | `Operative->DisplayName` | Паника / выход из паники / ярость | `Private/Characters/PanicComponent.cpp:222,272`, `Private/Characters/RageComponent.cpp:126` | `cmd_panic_flee`, `med_panic_grip`, `eng_rage_shout` |

> **Примечание по PMC:** `UPmcSquadSubsystem::SpawnFighter` (`Private/AI/PmcSquadSubsystem.cpp:1956`) создаёт вражеских оперативников как `AOperativeCharacter` с именами из `Content/Data/AI/pmc_operators.json` (`"Operator Kessler"`, `"Operator Vance"`, ...). Подписка на реплики должна фильтровать: PMC-операторы не спамят в общий VO-канал игрока (или играют только при слышимости/видимости отряда `UTacticalSightSubsystem::IsHostileOperativeConcealed(Operative) == false` с 3D attenuation и 3 детерминированными мужскими голосами `pmc_op_1/2/3` по хешу `Index % 3`).

### Правила воспроизведения реплик (VO Arbiter)
- **Приоритеты:** `Critical/Hazard (0)` (падение, растяжка, граната, засада) > `CombatOrder (1)` (контакт, подавление, перезарядка) > `Cover/Movement (2)` ("Behind cover!", "Hugging the wall") > `Loot/IdleChatter (3)`.
- **Без наложений (No Overlap):** одна активная VO-линия одновременно (или 1 для отряда + 1 приглушённая 3D для PMC на дистанции). Более высокий приоритет может вытеснять низкий; равный/низкий отбрасывается.
- **Per-speaker cooldown + phrase cooldown:** например `min 6–8 c` на одного спикера (чтобы отряд в стене из 3 человек не кричал хором `"Hugging the wall"` — первый берёт слово, остальные молчат) и `min 15 c` на повтор одинакового клипа; радио от `Commander` / `HQ` всегда допускается с `2 c` анти-спамом.
- **Пространственность:**
  - `Commander` (глобальные радио/квесты, `WaveSubsystem`) и `HQ` — **2D** (чтобы слышать из любой точки камеры).
  - Боевые/покрытие/перезарядка (`AOperativeCharacter`) — **3D SoundAttenuation** (`SA_CodexVoice3D`, слышимость вокруг камеры/бойца, умеренный спад, без полного исчезновения при высоком зуме RTS-камеры).

---

## 3. Машина состояний музыки (8 тем из `AUDIO_PIPELINE.md` раздел 2)

Матрица приоритетов (сверху вниз — первый истинный предикат определяет целевое состояние `ECodexMusicState`):

| Состояние | Кью-группа (2 варианта `_v1`/`_v2`) | Loop | Источник состояния в C++ (`file:line`) |
|---|---|---|---|
| `Menu` | `menu` | `true` | `UCodexFrontendSubsystem::IsFrontendWorld(World)` (`Public/UI/Frontend/CodexFrontendSubsystem.h:57`) |
| `Defeat` | `defeat_sting` | `false` | `UGameFlowSubsystem::GetPhase() == ECodexGamePhase::GameOver` (`Public/GameFlow/GameFlowTypes.h:22`) |
| `Victory` | `victory_sting` | `false` | `ECodexGamePhase::WaveCleared` или `ECodexGamePhase::PostCombat` (`Public/GameFlow/GameFlowTypes.h:19-20`) |
| `CombatHeavy` | `combat_heavy` | `true` | Бой с ордой: `Flow->GetPhase() == ECodexGamePhase::WaveCombat` и (`Flow->IsFreePause()` при внезапной орде / `UHordeSubsystem::GetHordesReleased() > 0` при живых врагах орды из `GetLastHorde()` `Public/Combat/HordeSubsystem.h:53,69` или `GetActiveQueued() > 0`) |
| `Combat` | `combat` | `true` | `UGameFlowSubsystem::GetPhase() == ECodexGamePhase::WaveCombat` (`Public/GameFlow/GameFlowTypes.h:17`), или активная стычка (`UWaveSubsystem::GetAliveEnemyCount() > 0`) |
| `Tension` | `tension` | `true` | Вне `WaveCombat` (`Exploration`/`Preparation`): отряд видит хотя бы одного живого врага через `UTacticalSightSubsystem::IsVisibleToSquad` (`Public/Combat/TacticalSightSubsystem.h:92`), ИЛИ враг ищет отряд (`!Enemy->IsOnPatrol()`), ИЛИ активно давление `UCombatPressureSubsystem::IsActive() && GetAliveCount() > 0` (`Public/Combat/CombatPressureSubsystem.h:147,186`) |
| `ExploreCold` | `explore_cold` | `true` | Вне боя: `UEnvironmentSubsystem::Get(World)->GetWeather() == EWeatherState::Blizzard` (`Public/Environment/EnvironmentSubsystem.h:130`, `Public/Environment/EnvironmentTypes.h:32`) |
| `ExploreCalm` | `explore_calm` | `true` | По умолчанию в `Exploration` / `Preparation` (включая `Clear`/`Overcast`/`Snowfall` без обнаруженной угрозы) |

### Тайминги и переходы
- **Min hold time:** минимум `6.0–10.0 c` в одном луп-состоянии, чтобы микро-мелькания sight-линий не дёргали `tension <-> explore_calm`. Исключения (перебивают без ожидания): `Combat`, `CombatHeavy`, `Victory`, `Defeat`, `Menu`.
- **Cross-fade:** `UAudioComponent` (A/B два компонента 2D `bIsUISound = true`, `bAllowSpatialization = false`): старый трек `FadeOut(CrossfadeSec, 0.f)` (рекомендуется `2.0–3.0 c` для ambient/tension и `0.8–1.2 c` для входа в `combat`/`stings`), новый `FadeIn(CrossfadeSec, 1.f)`.
- **Тактическая пауза / Пошаговый режим (`ECodexCombatMode::TacticalPause`, `TurnBased`):** музыку не ставить на game-pause (`UAudioComponent` с `bTickWhenPaused = true` / не гасить при dilation `0.01`); при желании в `TacticalPause` можно слегка приглушать фильтр/громкость (`0.7x`) через SoundMix.
- **Стинги (`victory_sting`, `defeat_sting`):** `bLooping = false`; после проигрывания `victory_sting` при возврате в `Exploration`/`Preparation` переходить на `explore_calm`/`explore_cold`.

---

## 4. Микшер (Sound Classes & Submix) + `CodexTactics.Build.cs`

- В `Source/CodexTactics/CodexTactics.Build.cs`: при необходимости `USoundSubmix` / `FAudioDevice` проверить добавление `"AudioMixer"` (базовые `USoundBase`, `USoundWave`, `USoundClass`, `USoundMix`, `UGameplayStatics`, `UAudioComponent` уже входят в `"Engine"`).
- Иерархия **Sound Classes** (создаются редакторным Python-импортером в `/Game/Audio/Mix/`):
  - `SC_Master`
    - `SC_Music` (default vol `0.75`, подключается к `Submix_Music`)
    - `SC_VO` (default vol `1.0`, `Submix_VO`)
    - `SC_SFX` (default vol `0.9`, `Submix_SFX`)
    - `SC_UI` (default vol `0.85`, `Submix_UI`)
- Плюс базовый `SM_CodexMaster` (`USoundMix`) + `SetSoundMixClassOverride` для ручек громкости в меню/консоли (`Codex.Audio.MusicVol`, `VOVol`, `SFXVol`, `UIVol` + `SaveGame` ключи).

---

## 5. Редакторный Python-импортер (`Scripts/Editor/import_audio.py` — Editor CLOSED)

Создаётся **только** в ветке задачи, запускается строго при закрытом редакторе по образцу `Scripts/Editor/import_dialogues.py`:
```powershell
UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="Scripts/Editor/import_audio.py" -unattended -nullrhi
```
- **Источники входа:**
  - Approved (или v1 по умолчанию, если `Saved/AudioGen/approvals.json` не запретил `rejected`) файлы из `Saved/AudioGen/voice/*.wav` и `Saved/AudioGen/music/*.wav`, описанные в `Content/Data/Audio/voice_lines.json` и `Content/Data/Audio/music_cues.json`.
- **Выход в Content (только новые папки `/Game/Audio/...`):**
  - `/Game/Audio/VO/SW_<id>` (`USoundWave`, `bLooping = false`, SoundClass = `SC_VO`)
  - `/Game/Audio/Music/SW_<id>` (`USoundWave`, `bLooping = true` кроме `victory_sting*` и `defeat_sting*`, SoundClass = `SC_Music`)
  - `/Game/Audio/Mix/{SC_Master,SC_Music,SC_VO,SC_SFX,SC_UI,SM_CodexMaster}`
  - `/Game/Audio/SA_CodexVoice3D` (`USoundAttenuation` для 3D реплик бойцов)
  - При желании: `DA_AudioBank` DataAsset или чтение прямых `/Game/Audio/...` путей по таблицам `voice_lines.json` / `music_cues.json` + генерация `UEditorAssetLibrary::save_loaded_asset`.

---

## 6. Обязательные Smoke / Automated Tests (без рендеринга, headlessly)

1. **`AudioRules` Unit Test (C++ Automation / null-world):**
   - Проверка таблицы приоритетов музыки: `Menu > Defeat > Victory > CombatHeavy > Combat > Tension > ExploreCold (Blizzard) > ExploreCalm`.
   - Проверка нормализации текста реплик (срез эмодзи/иконок `"🔄 "`, `"🧱 "` и форматированных префиксов вроде `"Wave 2: enemies advancing..."` -> `cmd_wave_advancing`).
   - Проверка cooldown per-speaker и priority preemption (высокий приоритет вытесняет низкий, повтор за `<= CooldownSec` подавляется).
2. **`AudioSubsystemSmoke` (Runtime Integration Smoke):**
   - Переходы `GameFlow`: `Exploration` -> `Blizzard` (`explore_cold`) -> `Squad sees enemy` (`tension`) -> `WaveCombat` (`combat`) -> `Horde released` (`combat_heavy`) -> `WaveCleared` (`victory_sting`) -> `GameOver` (`defeat_sting`).
   - Вызов `UGameMessageSubsystem::PostMessage("Commander", "Contact!")` -> выбран верный cue id `cmd_contact`, повторный вызов в пределах cooldown не запускает второй поток.
   - Проверка наличия всех ассетов после импорта (`/Game/Audio/VO/SW_*` и `/Game/Audio/Music/SW_*`).
