# 097 — вкладка «Пропсы» держит пропсы чужой карты

Ветка `grok/097-props-canvas-stale`. Код: `f689b905` (`fix(tg): drop stale props when the current map has no props file`), поверх `origin/main` `b2d93029` (за время работы main ушёл на 6 коммитов, ветка перебазирована, конфликтов не было).

## Claim

Смена карты сбрасывает документ пропсов до прихода ответа сети. Карта без файла пропсов показывает пустой холст и фразу «У карты <имя> нет файла пропсов - добавьте пропс, чтобы создать его». «Сохранить» пишет в эту карту и создаёт `Content/Data/LevelGen/props/<map>.props.json`. Запрос с `map=""` не уходит. Поздний GET предыдущей карты холст не возвращает.

## Evidence

| Утверждение | Доказательство |
|---|---|
| Причина: ответ GET применялся без проверки, что карта всё ещё та | До правки `loadMapProps` в `PropsPanel.tsx` делал `setHist(historyInit(loadedProps))` на любой успешный ответ. Эффект зависел только от `propsId`. Пока GET карты A был в полёте, переход на карту B без файла обнулял `hist`, а затем ответ A записывал его снова. `catch` старый документ не сбрасывал. Сохранение с `map=""` уже закрыто в `4163d7f2`; холст — нет. |
| Смена карты обнуляет файл и увеличивает поколение загрузки | `Tools/TrainingGround/src/departments/propsPanelMap.ts:50-62` `dropPropsForMap`. Панель: `PropsPanel.tsx:235-238` — `++loadGen`, `clearLoadedProps()`, дальше GET только если `propsLoadId` не пустой. |
| Чужой или опоздавший документ не применяется | `propsPanelMap.ts:69-78` `applyPropsLoad`: чужое поколение или `file.map !== mapName` — снимок без изменений. Панель вызывает это в `PropsPanel.tsx:197-201`. |
| Подсказка на пустой карте | `propsPanelMap.ts:8-9`, на экране `PropsPanel.tsx:593` (`data-testid="props-missing-hint"`). |
| Сохранение целится в текущую карту и не собирает запрос с пустым именем | `propsPanelMap.ts:82-84` `saveMapTarget` (имя карты, иначе propsId, не `file.map` чужой карты). `propsPanelMap.ts:113-148` `propsSaveRequest`. `PropsPanel.tsx:453-470` шлёт PUT только с `plan.map`. |
| Файл создаётся, если его не было | `server/propsApi.ts:339-357`: нет файла и `base_revision === 0` → атомарная запись `<map>.props.json`, ревизия 1. Ревизия ≠ 0 по-прежнему 404 и ничего не пишет. `LevelGenDepartment.tsx` передаёт `layoutId` текущей карты в новый файл. |
| Тест A → B (нет файла) → пусто + подсказка; поздний A не возвращается; save B целится в B; обратно на A грузится A | `src/departments/propsPanelMap.test.ts`, первый тест. Прогнан в полном `npm test`. |
| Создание файла — временное имя, не боевая карта | Тот же файл, третий тест: `grok097_tmp_nostale.props.json`, `validateProps` пустой, файл удалён в `finally`. В `git status` его нет. |
| `npm test` | `npm --prefix Tools/TrainingGround test` → `tests 590`, `pass 590`, `fail 0`, `duration_ms 251571.5673`. Три новых теста в этом прогоне зелёные (в том числе create, 456 ms). |
| typecheck | `npm --prefix Tools/TrainingGround run typecheck` (`tsc && tsc -p tsconfig.node.json`) — exit 0. |
| `git diff --check` | exit 0 по файлам задачи до коммита. |
| Аудит границ | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok` по индексу (5 файлов): `Boundary Breach : PASS`, `Risk Rating : 0.1 / 4 (Jev)`. |

## Что сделано

- Холст привязан к `mapName` + `propsId`, а не только к id файла. Смена карты сразу чистит историю, раскладку, проверку и ошибки.
- GET без id не отправляется: сервер на пустой `map` отдаёт список всех файлов, это не документ.
- 404 на карте без файла — пустой холст, не ошибка и не предыдущие пропсы.
- «Сохранить» на такой карте включено и создаёт пустой `codex.levelgen_props/1` (слои `[]`, ревизия станет 1). Отдельной кнопки «добавить пропс» на панели не было и нет: создание файла — это само сохранение.
- Документ другой карты в сохранение не попадает: `propsSaveRequest` возвращает ошибку, PUT нет.

## NOT done

- Ручной прогон в браузере и скриншот. В этой сессии нет браузера; `npm run dev` не запускался. Поведение проверено node-тестами, не кликами.
- Список карт отдела после создания файла сам не обновляется. Пока родитель не перечитает `/api/levelgen/maps`, у карты `propsId` пустой. Повторный заход на неё делает GET по имени карты и подхватывает уже созданный файл (`propsLoadId`).
- `server/propsApi.ts` не в списке Allowed paths задачи (там только `src/**`). Без него PUT на отсутствующий файл оставался 404 (`propsApi.ts`, прежний бросок до строки создания). Формат файла не менялся: тот же `codex.levelgen_props/1`.
- `docs/port/HANDOFF.md` не трогал: его нет в Allowed paths.

## Gate

`npm test` 590/590 | typecheck exit 0 | `typesafe_triage.py --audit-diff --agent grok` PASS 0.1/4 | `git diff --check` exit 0. Скриншота нет.
