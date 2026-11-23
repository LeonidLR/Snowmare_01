# 053 — подсветка окон и дверей под курсором

Ветка `codex/053-hover-highlight`, база `claude/bgen-test` (`7071c941`). Реализация `d7420130`, тест переключения стиля `2eabc97d`.

## Диагноз

В исходном `CodexTacticsPlayerController.cpp:420` включённая настройка `Codex.Click.Preview` (по умолчанию 1) гасила оконный силуэт даже при отсутствии активного предпросмотра. Исходный `CodexTacticsPlayerControllerOrders.cpp:932–993` уже определял подходящее окно и точку на другой стороне. У двери исходный `CodexTacticsPlayerController.cpp:450` менял только курсор; состояние створки на подсветку не влияло.

## Что сделано

- `Source/CodexTactics/Public/Interactables/OpeningHighlightActor.h:14` и `Source/CodexTactics/Private/Interactables/OpeningHighlightActor.cpp:10–70` — куб 100 см без коллизии, теней и влияния на навигацию. Существующий аддитивный материал `M_CombatFeedback` получает цвет и интенсивность `0.6`.
- `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerOrders.cpp:857,942–1039` — прежний тест подходящего подоконника и проходимости сохранён. Подсветка поворачивается вместе с проёмом; целое окно янтарное, разбитое светло-зелёное. `Codex.Window.HoverStyle 1` возвращает прежнюю позу восхождения без смены настройки предпросмотра клика.
- `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerOrders.cpp:1045–1076` и `Source/CodexTactics/Private/Core/CodexTacticsPlayerController.cpp:426–435` — закрытая створка синяя, запертая красная. В режиме атаки, в пошаговом бою, при активном предпросмотре клика и при уходе курсора подсветка скрывается. Открытые и сломанные створки не подсвечиваются.
- `Source/CodexTacticsDev/Private/Debug/WindowStairHoverSmokeCommand.cpp:267–292,408–499` — этап W проверяет смену стиля и исчезновение подсветки. Этап D находит закрытую внутреннюю дверь на карте, трассирует луч до створки, читает цвет материала в обоих состояниях, восстанавливает состояние и проверяет очистку подсветки. Сохранены проверки прохождения окна и подъёма по лестнице.
- `docs/systems/interactables-traps-loot.md:14` — описаны цвета и переключатель окна.

## Проверки

**PASS.** Код `2eabc97d` собран и проверен на `C:/Work/CodexTactics-slot1`.

- `Scripts/build.ps1` → `Result: Succeeded`, UBT 603,93 с, код 0. Журнал `Saved/Logs/Worker/20261123-100423/Build.log`. Компилятор вывел предупреждения C4701 о `Feet` в двух путях hover (лестница и старый силуэт окна). Сборка продолжилась и завершилась.
- `Scripts/test.ps1 -Filter CodexTactics.Interactables` → `26 tests, 0 failed`, код 0. Первый запуск не дошёл до тестов и остановился на строке загрузки `TurnkeySupport`. Сохранён `Saved/Logs/AutomationTests-codex053-sdk-stalled.log` на worker. Повтор той же команды прошёл с процессным `UE_SKIP_UBT_SDK_SETUP=1` по решению из отчёта 029.
- `worker_sync.ps1 -RemoteRoot C:/Work/CodexTactics-slot1 -ShareMachine -ShipHead -Build -Jobs 2 -Commands CodexTactics.WindowStairHoverSmoke,CodexTactics.DoorsSmoke` → `WindowStairHoverSmoke PASS 51.5s`, `DoorsSmoke PASS 56.1s`, без повторов.
- `Saved/Logs/Worker/20261123-100423/Smoke-WindowStairHoverSmoke.log:1920–1957` — W: подсветка видна, оба стиля переключаются, после клика лидер прошёл на 45 см за окно и разбил его. S: подъём на этаж прошёл. D: луч попал в `DoorActor_5`; материал вернул синий `(0.08, 0.36, 1.0)` и красный `(1.0, 0.08, 0.06)`. Исходное состояние двери восстановлено.
- `git diff --cached --check` и `python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex` → PASS.

## Чего не сделал

Визуальную оценку яркости в рендере не проводил. Смоук проверяет видимость компонента и параметры материала в headless-режиме. Локальных UE-прогонов, пуша и слияния не было.
