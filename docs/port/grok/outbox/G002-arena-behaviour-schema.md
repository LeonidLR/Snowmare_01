# G002 — схема `behaviour` в результате арены

Контракт для C++ (TACTICAL_MAP P3, `CodexTactics.PmcArena`). Тренер уже читает его (`Scripts/Trainer/nighttrainer/adapter.py`, каталог `BEHAVIOUR_METRICS`). Скаляр фитнеса не меняется: направление метрики — подпись для архива и утреннего отчёта, не вес в `fitness.py`.

## Куда класть

В каждый элемент `rounds[]` результата `codex.pmc_arena/1`, ключ `behaviour`. Это поведение стороны кандидата (side A). Если ключа на раунде нет, тренер смотрит `sides.A.behaviour` (или элемент списка сторон с `side`/`id` = A). Оба сразу — побеждает объект раунда.

Нет поля, поле `null`, или не объект — все метрики 0. Раунд не бракуется. Старая арена (pmc-v5 на второй машине) продолжает кормить текущий тренер. Ноль значит «не прислали или реально ноль»; тренер сам не выводит stalemate из `contact`.

## Два вида, оба законны

Богатый (предпочтительный, его пишет и мок):

```json
"behaviour": {
  "schema": "codex.pmc_arena_behaviour/1",
  "metrics": {
    "time_in_contact_s": {
      "name": "Time in contact",
      "unit": "s",
      "range": [0, 240],
      "direction": "higher",
      "value": 42.0
    }
  }
}
```

Плоский: `"behaviour": {"time_in_contact_s": 42.0, "flanks_completed": 1}`. Неназванные ключи каталога становятся 0. Лишние ключи тренер не требует. `value` не число и не bool — как отсутствие (bool не считается единицей).

`range` в объекте — паспорт датчика. Корзины MAP-Elites берутся из `trainer_config.json` → `qd.axes`, не из этого диапазона, чтобы сетка архива не прыгала между сборками арены.

`direction`: `higher` (больше — больше этого поведения), `lower` (больше — хуже, в §4 так помечены choke и stalemate), `context` (само по себе не хорошо и не плохо).

## Метрики (§4 + `moves`)

Считаются за раунд, по стороне кандидата. `moves` добавлен, потому что ось подвижности в задаче — фланги плюс перемещения.

| key | name | unit | range | direction | что мерить |
|---|---|---|---|---|---|
| time_in_contact_s | Time in contact | s | 0..240 | higher | секунды, пока стороны в огневом контакте |
| flanks_completed | Flanks completed | count | 0..8 | higher | фланги, которые дошли (не попытки) |
| moves | Position changes | count | 0..40 | higher | приказы сменить позицию, после которых отряд реально ушёл с места |
| suppress_then_move | Suppress-then-move | count | 0..8 | higher | suppress, за которым было перемещение, пока подавление ещё шло |
| ambush_success | Ambush success at chokepoint exits | count | 0..4 | higher | засада, которая нанесла урон на выходе из узкости |
| overwatch_kills | Overwatch kills | count | 0..8 | higher | убийства бойцом в Overwatch |
| time_in_chokepoint_s | Time inside a chokepoint | s | 0..240 | lower | стояли внутри самой узкости (это плохо) |
| window_positions_used | Window positions used | count | 0..6 | higher | разные оконные точки, которые заняли |
| quiet_spells | Quiet spells | count | 0..8 | context | промежутки без выстрела |
| order_churn | Order churn | count | 0..40 | lower | смены приказа за раунд |
| stalemate_share | Stalemate share | fraction | 0..1 | lower | 1, если раунд кончился без боя / в патовой ничьей по времени, иначе 0 |

Имена, единицы, диапазоны и направления в коде — тот же список, что в таблице. Оси архива по умолчанию (не часть JSON арены): aggression = `time_in_contact_s` (4 корзины, 0..180), mobility = `flanks_completed` + `moves` (4, 0..12), patience = `ambush_success` (3, 0..3).

В клетку архива попадает среднее по живым раундам кандидата. Упавший раунд в среднее не входит. Все раунды упали — кандидата в архив не кладут.
