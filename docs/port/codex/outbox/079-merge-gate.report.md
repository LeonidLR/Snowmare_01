# 079 — исправления merge gate

Ветка `codex/079-merge-gate`. Пункты 1–3 — `3ac58389`, пункты 4–8 — `a986dd16`. Проверка Python — **PASS**.

## Диагноз

На прежнем gate `merge_gate.ps1:229` проверял код worker раньше строки с JSON, поэтому при `WalkSmoke=FAIL` оставалось только `Worker smoke command failed (exit 1)`. Фикстура повторного прогона после красного теста показала `Already up to date`, пропуск сборки, `Worker smokes none mapped` и `ready to push`. При конфликте второй ветки SHA HEAD уже отличался от исходного. Все три случая воспроизведены в временных репозиториях.

## Сделано

- `Scripts/Tools/merge_gate.ps1:228,280` читает и показывает свежий JSON перед оценкой кода worker. Проверяет полноту списка и финальный вердикт для FAIL и KNOWN-FAIL. Сбой загрузки UE или сбор результата остаётся красным.
- `merge_gate.ps1:210,399–459` хранит pre-merge SHA, точные tips, объединённый HEAD, профиль тестов и путь бэкапа в `Saved/Logs/merge-gate-state.json`. `-Resume` выполняет проверку этого HEAD с исходной базой. Уже влитая ветка и пустой diff останавливают обычный запуск.
- `merge_gate.ps1:122–207,445–457` до слияния симулирует весь пакет через `git merge-tree --write-tree` с временными Git-объектами. Проверяет пересечения с dirty, untracked и игнорируемыми локальными путями. При неожиданном конфликте вызывает `git merge --abort` и печатает точный `git reset --keep <SHA>` для ручного восстановления.
- `merge_gate.ps1:520–555` повторяет только упавшие смоуки на фиксированном pre-merge SHA. Подтверждённый PRE-EXISTING даёт exit 2. `-Resume -AcceptPreExisting` принимает его после повторной проверки. Worker возвращается на кандидат. `Scripts/Tools/worker_sync.ps1:75,250` поддерживает `-ShipRef` и передаёт unpushed SHA именованным bundle с удалением временного ref.
- `merge_gate.ps1:113,446,482` останавливается при нечитаемом CommandLine редактора, повторяет проверку перед слиянием и перед сборкой. `merge_gate.ps1:79,301` пишет tracked SHA256 и список untracked в Manifest.json, проверяет оригиналы и копии после прогона.
- `merge_gate.ps1:246–279,335,482–598` запускает smoke worker параллельно с локальными тестами Lead, выбирает Jobs 2 в 08:00–16:30 и 1 вне этого окна, поддерживает явный `-Jobs`. Итог печатает отдельные полные строки с SHA, путями к логам, временем и объяснением выбора smoke из `Scripts/verify_lib.ps1:89`.
- `Scripts/Tools/tests/test_merge_gate.py:240–520` покрывает обычный и аварийный ход фикстурами Git, CIM и worker.

## Проверка

`python -m unittest discover -s Scripts/Tools/tests -p test_merge_gate.py -q` → `Ran 25 tests in 88.222s`, `OK`. В отдельных фикстурах проверены exit 2 и его явное принятие, свежесть JSON, возможность KNOWN-FAIL, запуск до готовности локального теста, совпадение SHA bundle и отсутствие временного ref. PowerShell parse, `git diff --check` и TypeSafe — PASS.

Проверка проводилась в временных репозиториях и с имитаторами Unreal и SSH. Боевой merge gate на main и тяжёлые прогоны 079 остаются у Lead. При Windows Application Control 4551 на worker baseline остаётся инфраструктурной ошибкой, пока загрузка проектных DLL не проходит политику. Конфигурацию worker в 079 не менял.
