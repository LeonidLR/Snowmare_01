# 064 · Автоматический worker gate

Ветка `codex/064-worker-auto-gate`. Вердикт PASS для реализации и локальных тестов.

## Диагноз

- `merge_gate.ps1:296` уже определяет необходимость сборки, а `merge_gate.ps1:255` проверяет свежесть и результаты смоуков. Существующий gate запускается явно с ветками, поэтому входы READY и периодический запуск вынесены в отдельный скрипт.
- `agent_chat.py` хранит очередь в файловом каталоге AgentChat. Для обмена с worker нужен доступ к существующему общему каталогу.
- В первой версии тестов дочерний процесс записывал `fixture build` и завершался успешно, но gate выдавал `FAIL: Build failed.`. Удержание дескриптора процесса до завершения позволяет PowerShell получить ExitCode.

## Изменения

- `Scripts/Tools/worker_auto_gate.ps1:16` принимает READY из темы gate или удалённые refs/heads/ready/*, сверяет SHA и обрабатывает один новый коммит за запуск.
- `Scripts/Tools/worker_auto_gate.ps1:45` проверяет окно 08:00–16:30, заморозку, синхронизацию и активный редактор. Атомарно занимает Saved/agent.lock для выделенной копии и существующих worker-слотов. После 16:30 останавливает запущенный им процесс сборки, тестов или смоука.
- `Scripts/Tools/worker_auto_gate.ps1:173` собирает пробное слияние в detached HEAD поверх полученного origin/main. Переиспользует правила merge gate для сборки и проверку результатов затронутых смоуков. Запускает test.ps1 -Smart и публикует один GATE с SHA и путём к журналу. Сохраняет результат для повторной доставки при ошибке чата.
- `Scripts/Tools/tests/test_worker_auto_gate.py:97` проверяет dry run, прохождение всего пути для кода, пропуск сборки для документации, дубли, занятый слот и временные окна.

## Проверка

`python -m unittest discover -s Scripts/Tools/tests -p test_worker_auto_gate.py -v`

    Ran 5 tests in 19.114s
    OK

Тесты выполняли реальные выбор SHA и слияние в временных Git-репозиториях. Cборка, UE-тесты и смоуки использовали имитации. Проверка индекса TypeSafe выполнена перед коммитом.

## Установка после согласования Lead

На worker нужны отдельный клон C:/Work/CodexTactics-auto-gate, одобренный экземпляр скрипта вне клона и доступ на чтение и запись к общему каталогу AgentChat. Учётная запись задачи должна иметь доступ к origin и к каталогу AgentChat. Проверить доступ и READY через -DryRun. Пример регистрации под интерактивной учётной записью worker:

```powershell
$runner = 'C:\Work\AutoGate\worker_auto_gate.ps1'
$chat = '\\approved-share\CodexTactics\Saved\AgentChat'
$args = '-NoProfile -WindowStyle Hidden -File "{0}" -Repo "C:\Work\CodexTactics-auto-gate" -ChatRoot "{1}"' -f $runner, $chat
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument $args
$trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(1) -RepetitionInterval (New-TimeSpan -Minutes 5)
$settings = New-ScheduledTaskSettingsSet -MultipleInstances IgnoreNew
$principal = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive
Register-ScheduledTask -TaskName 'CodexTactics Worker Auto Gate' -Action $action -Trigger $trigger -Settings $settings -Principal $principal
```

Установка, доступ worker к общему чату и запуск с реальными DLL остаются на проверку. Для первого прогона без пересборки в выделенной копии нужны подходящие DLL. Отдельно учесть ранее зафиксированное событие Windows Code Integrity 4551 при загрузке worker DLL.
