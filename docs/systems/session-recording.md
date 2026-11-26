# Запись игровых сессий

В Game и PIE запись по умолчанию включена для человека. `Codex.Demo.Record 0` отключает её,
`1` возвращает автоматический режим, `2` принудительно включает в игровом мире.
В автоматическом режиме unattended, nullrhi, NoTelemetry, commandlets и automation отключают запись.
Арена и профили автоматического тестировщика не сохраняются. Для записи нужны 60 секунд и принятый приказ.
`Codex.Demo.Mark good` или `Codex.Demo.Mark bad` помечает итог без изменения условия сохранения.
При 300 МБ появляется предупреждение. Файлы сохраняются только локально, старые записи не удаляются.

## JSONL v1

`Saved/Demos/YYYY-MM-DD_Map_HHMMSS.jsonl` записывается при закрытии игрового мира. Каждая строка содержит один
UTF-8 JSON объект. После заголовка события идут по игровому времени `t`, `rt` измеряет реальное время.
Координаты в сантиметрах, углы в градусах, время в секундах. Дистанции — 2D.

- `header` содержит версию, сессию, карту, время UTC, тип мира, единицы, исходные параметры бойцов,
  активную цель и флаги `immortal` / `infinite_ammo`. При любом из флагов сессия относится к лабораторным.
- `order` содержит исходный приказ, метку PMC или причину её отсутствия, результат, актёра, выделение,
  цели, укрытие, контекст отряда и угрозы на момент приказа. `plan` обозначает приказ, записанный при планировании.
- `snap` содержит режим, формацию, паузу, автономию, оперативников и до 12 ближайших видимых угроз, включая PMC.
  Период — секунда игрового времени, снимки прекращаются после четырёх часов.
- `mode` регистрирует смену solo, autonomy, cease_fire и pause.
- `footer` содержит длительность, счётчики, результат, число живых, флаг усечения и метку.

`ops` хранит стабильный внутри сессии ID, роль в формации, позицию, направление, стойку,
движение, укрытие, здоровье, патроны, выделение, дистанцию до цели и ближайшую видимую угрозу.
`lo` — последний приказ, `Follow` для ведомого, `lo_t` — время приказа.

## Сопоставление с каталогом PMC

| Ввод | Метка |
| --- | --- |
| Укрытие / выглядывание | Ambush без угрозы, FallBack при удалении ≥300 см, Relocate из укрытия, иначе TakeCover |
| Движение с угрозой | FallBack при удалении ≥300 см, Flank при угле ≥45°, Advance при сближении ≥300 см, иначе reposition |
| Движение без угрозы | Objective при сближении с целью ≥300 см, иначе explore_move |
| Подавление | Suppress, отмена — suppress_cease |
| Стойка | Crouch при приседании без движения и укрытия с видимой угрозой, иначе stance |
| Аптечка | Heal |
| Выстрел, быстрый огонь, граната, штурм | Сырые команды с причинами fire_target, grenade и breach |

Hold выводится из состояния. Схема сохраняет пространственный контекст для будущего конвертера.
Приказы про окна, перелезание, наблюдение за сектором и смену формации не записываются в v1.


## 121 additive tactics evidence (2026-11-27)

Version remains 1. Additive events: shot_hit, damage, kill, down, stealth_on/off, cover_enter/leave and climb (ladder/roof/mantle, height in cm). Down means a critical-health threshold crossing; zero-health deaths are kills for either side. Kill context is tied to a matching synchronous hit, and unresolved awareness/support is null. Support means a living ally has optical sight of the attacker, not mere proximity.

Snapshots add stealth, in_cover, height (feet above the sight system's ground column), seen_by_enemy (observer-specific optical count), under_fire (seconds since incoming fire, -1 when absent), scan_pitch, and look_yaw (the live sight facing). Threats add yaw and optional in_cover; unavailable cover evidence stays absent.

`python Scripts/Tools/demo_tactics.py <recording.jsonl> [--json]` emits a short tactic table and recorded kill/down list. Rules and thresholds live in that script. Each round is analyzed independently; old cov/clip evidence can recover cover and firing, but missing stealth/visibility/kills never become confirmed tactical success. Pattern evidence and round are included in JSON/table output. The tagger covers fallback, relative high ground, distract/kill flank, rear flank, prone crawl to cover, cover-order overwatch, approach_unseen under elevated overwatch, continuous unwatched sectors, scan pitch range and same-enemy lure/follow/open-ground/distant kill.

A synthetic fixture verifies the offline rules only. A short new PIE recording showing the three core tactics is a separate acceptance gate; it is not replaced by synthetic evidence. See the task121 report for validation status.

- 121 offline edge correction (2026-11-27): round_end sorts after same-timestamp kills, preserving the final kill and completed flank; Python19/19 PASS. Final runtime/PIE validation remains pending (task121 report).

- 121 direct-loss completeness checkpoint (2026-11-27): health recording now observes actual common health decreases (normal/direct/cold/burning/bleeding) exactly once, removing actor callback duplication. Known TakeHit attackers arm a synchronous context; critical down is recorded across the same paths. Added C++ AllHealthLossPaths coverage; final worker checks pending.

**121 BLOCKER (2026-11-27): real PIE acceptance evidence still needed.** C++8de50957: worker build PASS410.31s; Smart848/848 PASS including HitAttribution/AllHealthLossPaths; all16 mapped smokes PASS first try; Python20/20 PASS including actual null-target schema and round-end ties. Recording facts/tagger finished and reviewed, but no real new PIE fight showing all3 core tactics was captured in this headless run. Synthetic fixture is not acceptance evidence. See `docs/port/codex/outbox/121-recording-tactics.report.md`. Stop here; Lead owns the next task.
