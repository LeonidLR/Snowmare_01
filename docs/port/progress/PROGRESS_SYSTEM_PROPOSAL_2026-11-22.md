# Progress tracking system «Прогресс разработки» - proposal (design only)

2026-11-22 · author: Fable (architect, router level 4) · status: PROPOSAL for the Lead and the designer · no code written, nothing committed.

Designer (translated): "Add to the Training Ground a separate page 'Прогресс разработки': the stages of the global goal, tasks by
months, weeks and days. Every morning a report: what is ready, WHERE I can check it, what is developed today and this week, and
future tasks." Then: "Let Fable plan a system in which progress can be tracked in REAL TIME."

Off-plan note (rule "hold to the week plan"): this is not in the week 11-22..28 of `ROADMAP_THREE_TRACKS_2026-11-21.md`. It is
sized below so that it never touches the two hot sessions (19c, enemy-ai): scripts + one tools worker + ~1 h of Lead time.

## 0. The proposal in six lines

1. **Nobody types a status.** Status is derived from evidence the workers already produce: commits, FINISH lines, lane verdicts,
   smoke / digest JSON, merges. A worker's FINISH is a claim, not "done".
2. **Intent has one writer.** Stages, weeks, tasks, "where to check" and gates live in one small file in git,
   `docs/port/progress/plan.json`, written only by the Lead. Day rows and worker tasks are parsed from files the Lead writes anyway
   (`DAY_PLAN_<date>.md`, `docs/port/<agent>/inbox/NNN-*.md`).
3. **One collector** (Python, standard library, zero tokens) folds the sources into an event log and a state snapshot every 30 s.
4. **Views only read the snapshot:** TG department «Прогресс разработки» (live), the same page on the phone over Tailscale
   (read-only), an Artifact snapshot at each merge and each morning.
5. **"Готово" for the designer = merged + checked on main + a place to check it.** Everything less is shown as less.
6. **LLM cost:** one Haiku call per morning (~0.01-0.02 USD), one Fable batch per week (~1 % of the weekly Fable cap).

### Facts checked in the repo today (V = verified by reading / running, A = assumption)

| # | Fact | |
|---|---|---|
| F1 | `Scripts/Tools/task_board.py` (663 lines) already derives `Saved/AgentChat/task_board.json` from inbox files, outbox reports, git branches, chat topics and the register; `agent_chat_server.py` refreshes it every 60 s and serves `/api/task-board`; Agent Chat has a tab «Задачи». It covers inbox tasks only: no stages, weeks, gates, and not the Claude sessions' own work | V |
| F2 | Hand-kept plans decay within a week. Register `plan.weeks`: w1 131 items over 8 days; w2 16 items on 1 day; w3 1 item; w4 / w5 empty, titles («транспорт») no longer match the roadmap. `TASK_BOARD.md` header says «Обновлено: 2026-11-16». Gemini's reconcile 11-11: 52 rows = 27 done, 14 partial, 10 open, 1 stale + 17 works missing from the board | V |
| F3 | `LANES.json` is stale right now: lane `bgen` points at `worker/bgen-firing-gaps`, state `waiting`; `Saved/Lanes/status.json` calls it stalled for 2910 min, while the real work is on `claude/bgen-test` (f20a2bef, 08:51 today). `ai/arena-determinism` (035a71e4) has no lane at all | V |
| F4 | FINISH lines are structured: `097 FINISH \| done \| 86636934 \| done: ... \| NOT done: ... \| gate: ... \| report: <path>` (template in `TASK_TEMPLATE.md`); chat has 868 messages, ~60 lines mention FINISH; message ids are monotonic integers | V |
| F5 | All 5 worktrees share one git object store: `git for-each-ref` in main sees every local branch at once (Gemini's unpushed commits too) and takes 0.12 s for 467 refs | V |
| F6 | Event rate: 2300 commits on main since 2026-09-28 (106 / 661 / 1533 for Sep / Oct / Nov; 184 + 312 merges); all branches peak at 230 commits per day (11-17) = one per ~6 min; night digests arrive every ~7 min | V |
| F7 | Smoke results carry no commit: `Saved/Logs/SmokeParallel-last.json` = `results[{Name, State, Seconds, Log}], wallSeconds, jobs, rerunPassed`. `buildings_walk_*.json` (schema `codex.buildings_walk/1`) has `pass, share, minpass, entry_legs_passed / entry_legs, max_squad_ratio / squad_ratio_gate` (BgenPlans final: 5 of 9 houses, 81 / 100 entry legs, ratio 2.73 vs gate 2) | V |
| F8 | TG dev server (vite, port 5173) sets `Access-Control-Allow-Origin: *`, has POST endpoints that write level JSON and spawn bot runs; it is started by hand (`Scripts/run_training_ground.bat`). `agent_chat_server.py` binds 127.0.0.1 with a same-origin guard on purpose | V |
| F9 | `tailscale status` lists three devices: the main PC, `codex-worker` and an iOS phone. The phone is already in the tailnet | V |
| F10 | `.git/hooks` holds only the LFS hooks and the pre-push guard; hooks are untracked, shared by all worktrees, protected by `branch_guard.py`, and will be lost at the main-PC format (`MAIN_PC_FORMAT.md`; `Saved/Logs` is not mirrored either) | V |
| F11 | `api_task.py` prices: haiku 1 / 5, sonnet 2 / 10, opus 4 / 20 USD per M tokens; ledger `Saved/Orchestrator/api_spend.jsonl` sums to ~92 USD over 90 entries. Fable is not in that price table | V |
| F12 | An Artifact `db` document can be written only by a Claude session (`ArtifactData`, payload may come from a local file) or by a signed-in viewer of the page; there is no headless HTTP write for a script | V (skill text) |
| F13 | Artifact `db` size limits, and whether a page's `files` capability can read a local project's files on the phone | A - 30 min spike |

## 1. Goal and non-goals

**Goal.** At any moment the designer can answer, in Russian and without reading code: where are we against the global goal; what is
being done right now and by whom; what became ready, and where exactly he can check it; what waits for him; what comes next.

**What "real time" honestly means here.** Four freshness classes, each tied to how fast its source really changes:

| Signal | Source cadence (measured) | Freshness target | Where |
|---|---|---|---|
| Who works on what, liveness | a commit per ~6 min at peak, chat ~80 posts / day, watchdog every 10 min | <= 60 s | TG page, phone over Tailscale |
| Task transitions (started / FINISH / review / merged) | per commit, FINISH line, merge | <= 60 s after it lands on disk | same |
| Gate metrics | per smoke run (minutes) or A/B night (150 rounds per arm) | on each new result file; shown with its age and sample size | same |
| Plan structure (stages, weeks) | Lead decision, a few times a week | on the Lead's commit | all views |
| Phone snapshot (Artifact) | - | at each merge, each morning, on request | Artifact |

A 30 s tick is >= 10x faster than the fastest source, so faster buys nothing. Between commits an agent is a black box by nature; the
existing rule "push WIP every 30-45 min" (`LANES.md` rule 4) bounds that blind spot, and the watchdog flags anything longer.

**Non-goals.** Streaming agents' tool calls or tokens; replacing the agent chat, `LANES.json`, the mechanics register or HANDOFF;
letting agents report their own status; a general project-management tool; forecasting dates the roadmap refuses to promise
(breakable walls before S-NAV); any LLM in the live path.

## 2. Data model

### 2.1 Three stores, three single writers

| Store | Path | In git | Single writer | Holds |
|---|---|---|---|---|
| Plan (intent) | `docs/port/progress/plan.json` | yes | the Lead (main only) | goal, stages, months' themes, weeks, metrics, tasks with check / gate / dependencies |
| Manual ledger | `docs/port/progress/ledger.jsonl` | yes | the Lead, via one CLI | the few facts no source can show: designer verdicts (with the chat message id), drops, reopen |
| Derived events + state | `Saved/Progress/events.jsonl`, `state.json`, `cursors.json` | no | the collector process | everything observed; rebuildable from the sources at any time |
| Daily closing snapshot | `docs/port/progress/history/<date>.json` | yes | morning job writes, Lead commits | the day's final state, compact: durable history that survives the PC format |

JSON in git **and** local files, no database. Volume is ~100-250 events / day (~75 KB / day): JSONL plus an in-memory fold is
enough for years. SQLite would add a native module to the TG (node) side and a second source of truth; the event log is a cache,
not a record (delete `Saved/Progress`, run `--rebuild`, get the same state). Revisit only if history queries exceed ~100k events.

Workers never write any of these. `branch_guard.py` gets `docs/port/progress/plan.json` and `ledger.jsonl` in its PROTECTED list,
so a worker branch that edits them fails the pre-merge guard.

### 2.2 Hierarchy

`goal -> stage -> task` is the only real parent chain. **Month, week and day are calendar indexes computed from dates**, plus a
small record per week (goal + gates) and per month (one theme line). A task hangs under exactly one stage and is placed on the
calendar by its planned week / day and by the dates of its events. This avoids moving tasks between containers when a day slips.

- **Stages**: history stages H0-H6 = `PORT_MATRIX.md` phases 0-6; H7 = UE-only sprints; then roadmap stages 1-4
  (`ROADMAP_THREE_TRACKS_2026-11-21.md`), each with its gate list; then an unscheduled «Позже» lane (see section 6).
- **Weeks**: `{id: "2026-11-22..28", stage, goal_ru, gates: [metric ids], source: "<roadmap section>"}`.
- **Days**: not stored in plan.json. Parsed from `docs/port/DAY_PLAN_<date>.md` (table `# | Who | Task | Done when`); the Lead adds
  one `ID` column that names a plan task or an inbox number; a row without an ID becomes an ad-hoc day task `d1122-07`.
- **Worker tasks**: not typed either. Every `docs/port/<agent>/inbox/NNN-*.md` becomes a task stub through the existing
  `task_board.py` scan; title from the H1, acceptance and "Proved by" from its "Design gate" block, `needs_designer` from the line
  "Designer decision needed?".

### 2.3 Task record (plan.json part - written by the Lead)

```json
{
  "id": "bgen-confined-formation",
  "title_ru": "Отряд сжимается в тесных комнатах и коридорах",
  "stage": "S1", "week": "2026-11-22..28", "track": "buildings", "kind": "game", "owner": "lead-19c",
  "match": { "branches": ["claude/bgen-test"], "topics": ["claude-S022"], "inbox": null },
  "acceptance_ru": "Весь отряд заходит в квартиру и не застревает в дверях",
  "check": {
    "map": "/Game/Maps/Generated/L_Gen_BgenTest_X6",
    "how_ru": "Строем войти в любую квартиру витрины",
    "console": null,
    "switch": { "file": "Content/Data/AI/squad_confined.json", "key": "hold_places", "default": true },
    "tg_tab": null, "smokes": ["<walk smoke name>"], "register": ["<register item id>"]
  },
  "gates": [{ "metric": "bgen.entry_legs.showcase", "target": ">= 0.95" }],
  "depends_on": [], "needs_designer": null
}
```

(Illustrative, except the switch file and key, which are real; map, smoke and register ids are filled by the Lead.) `check` is **mandatory for `kind: game`** - the plan lint
refuses a game task with no map or `how_ru`, because "where can I check it" is the designer's explicit ask. Keys / cvar text is not
duplicated: `check.register` points at the mechanics-register item, whose `how` / `off` / `smoke` fields already hold it.

`needs_designer`: `{question_ru, doc: "<file#section>", asked: "<date>"}`; answered = a ledger entry quoting his chat message.

### 2.4 Metric record (gate)

```json
{ "id": "defence.crossroads.win_rate", "title_ru": "Перекрёсток: победы обороны",
  "target": ">= 0.60", "min_n": 150, "noise_from": "A/A run", "source": "paired_ab.py --json",
  "also": ["defence.crossroads.captures <= 0.10", "defence.crossroads.kills_per_round >= 2.0"] }
```

Current values are never stored in plan.json: they come from `metric` events (value, n, commit A / B, seeds, p-value, file hash).
Display (example numbers): «51 % -> цель 60 % (n = 150, шум +-4, main 8c6ba393, 6 ч назад)». Below `min_n` the gauge says «мало данных», never green.

### 2.5 Derived status (state.json part - never hand-written)

| Status | Designer label | Evidence required |
|---|---|---|
| planned | в плане | exists in plan / day plan / inbox |
| in_progress | в работе | commit on a matched branch or a status post within 2 h (same window as `task_board.py`) |
| claimed | исполнитель: готово | FINISH line **and** its commit hash exists in some ref |
| in_review / rework | на проверке / возвращено | claimed and not merged / a review line "NNN review: REWORK" |
| merged | влито | commit is an ancestor of `origin/main`, or the same patch-id is in main (cherry-picks) |
| built | собрано | a successful build of main at or after that commit (clears "merged-not-built") |
| verified | проверено | a check result PASS, or the gate metric meets its target, at a commit that contains the merge; not a flaky smoke |
| accepted | принято вами | a ledger entry quoting the designer's message |

Flags on top: `blocked_designer`, `blocked_dependency`, `stalled` (watchdog), `switch_off` (in the game but off by default),
`reopened`, `evidence_stale`, `unmatched`. The designer sees four colours: grey (plan), blue (в работе .. на проверке), amber
(влито, не проверено / выключено), green (проверено, принято); flags are red badges.

## 3. Event ingestion

One collector, `Scripts/Tools/progress_collect.py` (proposed name), pure reader of everything, single writer of `Saved/Progress`.
It runs as a 30 s loop inside the progress server process and as a 5-min scheduled task (`pythonw`, no window - the pattern of
`CodexLaneWatchdog`); an exclusive lock file (the `file_lock` of `agent_chat.py`) makes the second runner exit at once.

**Why polling and not git hooks:** F10 (untracked, protected, lost at the format, blind to the worker's pushes) against F5 (a full
ref scan costs 0.12 s). A watcher needs nobody's cooperation.

| Source | How it is read | Events (deterministic id) |
|---|---|---|
| Git, all worktrees | `git for-each-ref` diffed against the last ref map; `git log old..new` per moved ref; `git fetch -q origin` every 5 min for the worker's branches. Only plumbing, `GIT_OPTIONAL_LOCKS=0`: never touches the index or the designer's files | `commit:<sha>`, `branch_new:<ref>` |
| Merges | first-parent walk of `origin/main`; ancestor check per task; patch-id comparison for cherry-picks (today's `ec20d40e..196c7f62` came in that way) | `merge:<sha>`, `picked:<sha>` |
| Chat | tail `Saved/AgentChat/chat.jsonl` from the last id. FINISH regex (F4); "OK NNN"; "NNN review: REWORK / OK"; posts by `designer` in a task topic are attached verbatim, never interpreted | `chat:<id>` -> finish_claimed, plan_approved, review, designer_said |
| Lanes / watchdog | `docs/port/LANES.json` = the join table task <-> branch <-> topic; `Saved/Lanes/status.json` = verdicts. Topic `lanes` chat posts are ignored (same fact twice) | `lane:<id>:<verdict>:<last_activity>` |
| Inbox / outbox | import `task_board.py` functions (`scan_inbox_tasks`, `scan_outbox_reports`, `match_git_for_task`); do not re-implement | `task_stub:<owner>-<NNN>`, `report:<path>` |
| Day plan | parse `docs/port/DAY_PLAN_<date>.md` on change | `day_row:<date>:<#>:<hash of row>` |
| Smoke / test JSON | mtime + size watch, then sha256, in the main checkout and every path of `git worktree list`: `Saved/Logs/SmokeParallel-last.json`, `Saved/LevelGen/buildings_walk_*.json`, `route_smoke_*.json`, test summary | `check:<sha256-16>` with the worktree HEAD + dirty flag stamped at detection (`head_inferred: true`) |
| Worker | one ssh call per 10 min (as the watchdog's `ssh_heartbeat`): `Saved\Worker\loop.status.json` and new lines of `Saved/Worker/nightloop/<date>/digest.log` on the worker (written by `worker_night_loop.ps1`); local copies under `Saved/Logs/Worker/**`; `paired_ab.py --json` outputs | `digest:<run>:<scenario>:<sha>`, `metric:<file hash>`, `worker_phase`, `worker_offline` after 2 misses |
| Build | `Saved/Logs/Build.log` summary per checkout, `Saved/Logs/RemoteBuilds.jsonl` for worker builds | `build:<head>:<result>` |
| Ledger | `docs/port/progress/ledger.jsonl` | `accept`, `reopen`, `drop`, `answer` |

**Matching an event to a task**, in this order: trailer `Task: <id>` in the commit body (optional help, added to `TASK_TEMPLATE.md`
and the session prompts) -> NNN in the branch name (`gemini/099-...`) -> branch or topic listed in the task's `match` -> the lane's
branch -> otherwise the bucket **«без задачи»**, visible to the Lead. Unmatched activity is itself a signal: today it would have
shown `claude/bgen-test` and `ai/arena-determinism` as live work with no lane (F3) - the system reports "LANES.json is out of date",
it does not edit it.

**Idempotency.** Every id is derived from the source's own identity (sha, chat id, file hash). The collector keeps the set of seen
ids, appends under the lock, and every adapter can restart from cursor 0 without duplicates. `state = fold(plan, ledger, events)` is
a pure function: `--rebuild` twice must give a byte-identical `state.json` (this is also the main unit test, with fixture folders in
the style of `test_lane_watchdog.py`).

**Several agents at once.** They cannot conflict: agents write only what they write today (their branch, the chat, result files).
One process writes events; one person writes plan and ledger, on main. When sources disagree, precedence is
**git > check results > chat claims > hand-written docs**, and the disagreement becomes a `discrepancy` row for the Lead
(e.g. "FINISH names commit 1234abc, not found in any ref"; "register says main, git says not merged").

### 3.1 Existing signals: what is reused, and how (nothing is duplicated)

| Existing piece | Decision |
|---|---|
| `agent_chat.py` / `chat.jsonl` | **Source** (FINISH, OK, review lines). Its `file_lock` pattern is reused. The server on 8765 stays local and untouched |
| `task_board.py` + Agent Chat tab «Задачи» | **Code reused as an adapter** for inbox tasks; the tab stays the per-task chat view, the new page is the plan view |
| `LANES.json` + `lane_watchdog.py` + `Saved/Lanes/status.json` | **Source** for stalls and the task <-> branch join; the collector also registers itself as a lane |
| Git commits (Conventional Commits) on main and branches | **Primary evidence** |
| `DAY_PLAN_<date>.md`, `ROADMAP_THREE_TRACKS_2026-11-21.md` | Day rows **parsed**; roadmap stages and week copied once into plan.json with a `source` link |
| `docs/port/<agent>/inbox`, `outbox`, `reviews` | **Parsed** (task stubs, reports); never edited |
| `mechanics_register.json` + its Artifact page | Items' `how` / `off` / `smoke` are the where-to-check text, **by reference**; `planned` feeds the backlog tab; its hand-kept `plan` block is generated from the state in P5 |
| `TASK_BOARD.md` | «Сделано» tables = backfill source and stay the Lead's merge log; «Идёт сейчас» retired in P5 |
| `HANDOFF.md`, `PORT_MATRIX.md` | Not live sources. PORT_MATRIX gives the history stages once; HANDOFF is a link target |
| Smoke / walk / route JSON, `dayloop_digest.py`, `paired_ab.py`, `jev_digest.py` | **Evidence and metric sources**; the digest scripts are called, not rewritten |
| `morning_qa_report.py` (clusters reds from Saved/Automation, Orchestrator, Trainer, Telemetry) | **Called** for the morning report's risk section |
| `project-health.html` + `docs/port/audit/audit_*.json` (weekly, zero tokens) | Linked from the stage tab; the audit JSON is an input of the weekly analysis |
| `typesafe_triage.py --telemetry` (Jev) | Only inside the weekly input packer, to compress bot-run telemetry; never in the live path |
| `api_task.py` + its ledger | The morning Haiku call goes through it, so the spend is capped and logged |

## 4. Views

### 4.1 TG department «Прогресс разработки»

New `Tools/TrainingGround/src/departments/ProgressDepartment.tsx`, registered in `TrainingGround.tsx` (fifth entry of `DEPARTMENTS`),
with `Tools/TrainingGround/server/progressApi.ts` wired in `vite.config.ts` like the five existing `setup*Api` modules. Read-only
endpoints: `GET /api/progress/state`, `/history?date=`, `/report?date=`, and `/stream` (SSE: one `state` event when `state.json`
changes; the page falls back to a 15 s poll). The collector does **not** live in the vite process (F8: TG is not always running).

Every tab starts with the line «данные на 10:42:07 · сборщик жив» and one badge per source (git, чат, линии, worker, проверки); a
source silent past its budget turns grey «нет данных с 09:10» instead of leaving old green on screen.

| Tab | Shows |
|---|---|
| **Сегодня** | «Ждёт вас» (blocked-on-designer items with document link and waiting time; today that is the 5 items of the day plan's "Designer" line). «Сейчас работают»: one row per worker (lead-19c, enemy-ai, Gemini, worker, Lead) - task, last commit / post age, lane verdict. «План дня»: the DAY_PLAN rows with derived status. «Готово - где проверить»: per item the map, the copyable console command, keys, the switch and its default, TG tab; green tick only when verified. «Что-то не так»: stalls, red checks on main, discrepancies |
| **Неделя** | Week goal; gates as gauges (current vs target, sample size, noise, age, sparkline); seven day columns by track (buildings HOT / enemies / destruction / tools); planned vs merged vs verified; carry-overs |
| **Этапы и месяцы** | Timeline: history stages, roadmap stages 1-4 as bars with their gate checklists and a «мы здесь» marker; months Sep / Oct / Nov with a theme line, headline mechanics and counts. No dates where the roadmap forbids them |
| **Будущее** | The register's `planned` list (58 items) by group, TASK_BOARD queues, stage 2-4 epics, the «Позже» lane; each with "why not now" (dependency / designer / stage). Order is the Lead's |
| **История** | Day picker -> that day's closing snapshot and morning report; raw event feed with filters (worker, task, type) for the Lead |

### 4.2 The same view on the phone - options

| Option | Freshness | Token cost | Other cost / risk | Verdict |
|---|---|---|---|---|
| A. Artifact with `db` capability, page subscribes with `onSnapshot` | at each push: merge, morning, on request | one `ArtifactData update ... file_path=<trimmed state>` per push, < 1k tokens (payload read from the file, not through context) | only a Claude session can push (F12): gaps at night and when the Lead is asleep; one more published page | **snapshot + fallback** |
| B. Tailscale -> a separate read-only progress server on the main PC | 30-60 s, SSE | 0 | main PC must be on; Tailscale switched on in the phone (F9: already in the tailnet); one new listening port | **the live view** |
| C. SendUserFile of the morning report | once | one session message | static, stale at once; designer 10-22: no pictures unless asked | morning ping only (text + link) |
| D. Tailscale straight to the TG dev server (5173) or chat server (8765) | live | 0 | unsafe (F8): open CORS, write endpoints, process spawning; the chat server's same-origin guard exists to stop message injection | **rejected** |
| E. GitHub Pages branch of the (public) repo | 1-10 min | 0 | internal plans become public; a bot pushes branches | rejected unless the designer asks |
| F. Ask the Lead from the phone: «статус?» -> a 15-line script brief | on demand | ~1-2k tokens per ask | needs a live session | keep as the pull channel |

**Recommended: B for live + A as snapshot + C as the 08:15 ping.** B's server is a new small `progress_server.py`: GET only (any
other verb 405), serves the built page + `state.json` + SSE, binds the machine's Tailscale address only (never 0.0.0.0), Host-header
allowlist as in `same_origin_only`. A is trimmed to today + week + stage summary (target <= 50 KB) with `db` read rules = owner
only; its big timestamp says how old it is. When the main PC is off (format week) only A works - stated on the page.

## 5. Morning report and weekly analysis

**Morning, 08:10 scheduled task** (night loop ends 07:45, the farm starts 08:00). Script parts, zero tokens:

1. «Готово за сутки»: tasks that reached merged / verified since the last report, each with its where-to-check block.
2. «Сегодня»: rows of today's `DAY_PLAN_<date>.md`; if the file is missing the report says so in red.
3. «Неделя»: gate table (current, target, delta since yesterday), days left, carry-overs.
4. «Ждёт вас»: open designer questions with their age.
5. «Ночь на worker»: per scenario PASS n / N and sweep fails (reusing `dayloop_digest.py`, `jev_digest.py`), A/B results.
6. «Риски»: stalls, reds on main, discrepancies, budget line (API ledger total, Gemini usage).
7. «Дальше»: next three backlog items per track.

Outputs: `Saved/Progress/reports/<date>.md` + `.html`, the closing snapshot `docs/port/progress/history/<yesterday>.json`.

**The only LLM step:** a 5-8 sentence plain-Russian lead paragraph for a non-coder (commit subjects and task titles are engineer
English). Tier: **Haiku** through `api_task.py` (single-shot, hard `--budget-usd`, runs unattended): ~8k tokens in, ~1k out =
~0.013 USD per day, ~0.10 USD per week (0.2 % of the 50 USD weekly API budget). Haiku, not Sonnet: it rewrites given facts and
judges nothing. Guard: a script checks that every number, hash and map name in the paragraph exists in the facts JSON; on any
mismatch the report ships without the paragraph. The Lead does not write the report; he reads the 40-line md (~2k tokens), may add
two lines, pushes the Artifact snapshot (one tool call) and pings the phone.

**Weekly deep analysis (Fable), Sunday ~08:30** - after the zero-token `CodexTactics Weekly Audit` (Sunday 02:00, writes
`docs/port/audit/audit_<date>.json`) and after the Saturday limit reset; it is that day's single Fable batch.

- Inputs, pre-packed by a script to <= ~80k tokens: seven closing snapshots, the metric series, diffstat by system (no diffs),
  plan.json (week + next stages), the roadmap, lists of stalls / reworks / discrepancies / flaky checks, the audit JSON, the budget
  ledgers, open designer questions with ages, handover sections of 3-5 key reports.
- Outputs, `docs/port/progress/weekly/<week>.md`: verdict per gate with evidence; velocity and a range (not a date) for the stage
  gate; where time was lost (rework rate per worker, stalls, waiting on the designer); "green but not playable" suspects; a proposed
  plan.json patch for next week (Fable proposes, the Lead writes); a 5-line Russian summary for the designer.
- Cost per run: ~80k in + ~8k out. On the subscription, by the measured 270k tokens = 3 % of the weekly Fable cap (11-15), that is
  ~1 %. Fable has no price in `api_task.py` (F11), so no API figure is claimed; the Opus fallback through the API would be
  80k x 4 + 8k x 20 USD per M = ~0.48 USD.

## 6. Initial backfill (once)

| Step | Source | Who | Result |
|---|---|---|---|
| 1. Skeleton | PORT_MATRIX phases, roadmap stages, CLAUDE.md goal | Lead / Fable by hand, ~30 min | goal + stages H0-H7, S1-S4, months 2026-09 / 10 / 11 |
| 2. Past, by script (0 tokens) | first-parent `git log` of main (2300 commits) bucketed by ISO week and Conventional-Commit scope; TASK_BOARD «Сделано (дата)» tables (merge hash + what changed, already in the designer's Russian); register items' `since` dates (209 mechanics); register `plan.weeks` w1 (131 dated items); `CHANGELOG.md` | script | `docs/port/progress/history/backfill.json`: ~150-200 historical tasks as `verified_legacy`, each with a hash confirmed by `git merge-base --is-ancestor` |
| 3. Month / week themes | fact lists from step 2 | Gemini Flash (volume carrier), one task; Lead reads once | one theme line + five headline bullets per month and per week, Russian |
| 4. Present and future | roadmap, DAY_PLAN, LANES, inbox 095-099, register `planned` (58), TASK_BOARD queues, `OPEN_QUESTIONS.md` | one Opus session (judgment: where-to-check and gates), or that day's Fable batch | plan.json: stage 1 in task detail (~25-35 tasks), stages 2-4 as epics with gates |
| 5. Designer review | the «Этапы и месяцы» tab | designer | stage names and order confirmed |

Past work is imported, not re-verified. Everything beyond roadmap stage 4 (campaign and narrative, squad of six, vehicles, weapons
roster) exists only as register `planned` items and recorded decisions: it goes into the unscheduled «Позже» lane, and the designer
orders it - the backfill must not invent a schedule (open question Q1).

## 7. Alternatives considered

| # | Alternative | For | Against |
|---|---|---|---|
| 1 | Static JSON + a script that renders one HTML page (the `build_health_dashboard.py` / register pattern) | simplest, phone-readable, zero infrastructure | not live; on the phone only as fresh as the last republish; no drill-down |
| 2 | Live TG server (SSE / websocket) to which agents POST their status | truly instant, simple model | needs every agent to remember - the failure seen three times (F2, F3); TG is not always up and has write endpoints (F8); self-reported status is exactly what must not be trusted |
| 3 | Artifact + DB as the primary store | phone-native, live for viewers, private | only Claude sessions can write (F12): Gemini, the worker and scripts cannot; truth outside git (no diff, no review, lost with an account problem); tokens per write |
| 4 | Existing tool: GitHub Projects / Issues | mobile app, notifications, ~0.5 day to set up | gates, evidence and "where to check" do not fit flat fields; workers would need tokens with write access; ~15 tasks a day of issue noise on a public repo; the designer does not use GitHub; Russian player-terms view still needs custom rendering |
| 5 | Extend only what exists (Agent Chat «Задачи» + the register's plan block) | no new surface | F1: inbox tasks only, localhost only; F2: the plan block is the thing that decayed |

**Recommendation: a derived-state hybrid.** Alternative 1 as the base and first slice (plan in git, collector, snapshot, static
page), then live readers on top (TG department with SSE; read-only Tailscale server), the Artifact only as a snapshot, and
`task_board.py` reused as an adapter. The point that decides it: every status change the designer cares about already leaves a
trace on disk, so the reliable system is the one that asks nothing new from the workers.

**Against my own pick.** (a) It is a fourth status surface (Agent Chat board, register, TASK_BOARD, now this); unless the register's
plan block and TASK_BOARD «Идёт сейчас» are retired into it, it adds to the upkeep it claims to remove. (b) `plan.json` still needs
the Lead every morning; if he skips it for three days the page shows busy workers and no meaning - the same decay as F2, one level
up. (c) Matching commits to tasks is heuristic; the «без задачи» bucket needs someone to empty it. (d) It costs ~3-4 agent-days
against half a day for GitHub Projects. If the designer mostly wants the morning report, phases 0-1 are enough: stop there, watch
for a week whether he opens the live page, and only then build phases 2-3.

## 8. Implementation plan

Sizes: S <= half an agent-day, M ~ one day. Every delegated piece is classified with `model_router.py --classify` first; the tiers
below are expectations. No new full worktree (disk): a sparse worktree with `Scripts/`, `docs/port/`, `Tools/TrainingGround/` only.

| Phase | What | Size | Who | Acceptance |
|---|---|---|---|---|
| **P0 - today** | plan.json for stage 1 + this week (from the roadmap and today's day plan); collector v0 with three sources (git refs, chat FINISH, lanes status) + `task_board.json` import; static self-contained page `docs/port/progress/progress.html` (Russian, phone width, dark mode); Lead publishes it as an Artifact like the register | S + S | plan.json: Lead / Fable ~45 min; scripts: one Sonnet subagent (or Gemini task 100 after 098 if its queue allows) | `--rebuild` twice -> identical state; 20 real FINISH lines parse in a fixture test; all 15 rows of DAY_PLAN_2026-11-22 appear with a status; row 9 shows Gemini 089 in progress at its branch head (dec2c87d when this was written); the confined-formation merge shows `ec20d40e..196c7f62` with its check; `claude/bgen-test` and `ai/arena-determinism` are flagged as work without a lane |
| P1 | Remaining adapters (check JSON, worker ssh, A/B metrics, build, day plan, ledger), evidence ladder, plan lint, unit tests, scheduled tasks (collector 5 min, morning 08:10), Haiku paragraph + number validator | M | Sonnet: fold + evidence rules; Gemini Flash: adapters with fixtures (well-specified parsing); Opus API review per merge (~0.15-0.30 USD each) | a merged-but-unchecked task is amber, never green; a FINISH with a fake hash raises a discrepancy; morning report exists at 08:15 with no session running; validator rejects an injected wrong number |
| P2 | TG department: five tabs, `progressApi.ts`, SSE | M | Gemini Flash (TS / TG is its lane), layout brief by the Lead | `npm test` + typecheck green; a new commit shows on the page within 60 s; no POST route; source badges turn grey when a source file is removed |
| P3 | Phone: read-only server on the Tailscale address; Artifact with `db` + `onSnapshot`; morning ping | S | Sonnet (binding / security), Lead publishes | page opens on the phone over cellular; POST returns 405; a foreign Host header returns 403; `netstat` shows no 0.0.0.0 listener; Artifact shows its timestamp |
| P4 | Backfill (section 6) | S | script + Gemini + one Opus session; designer review | every historical task has an ancestor-verified hash; designer confirms stage names |
| P5 | Honesty upgrades: `smoke_parallel.ps1` writes `head` + tree token into its result JSON (`tree_token.ps1` exists); `branch_guard.py` protects plan / ledger; the register's plan block and TASK_BOARD «Идёт сейчас» are generated from the state; weekly input packer + Fable prompt | S | Sonnet; Lead for the two doc retirements | `head_inferred` disappears from new check events; one week runs with no hand-kept day list anywhere else |
| P6 (optional) | Designer verdict buttons («работает» / «не работает») on the local TG page; a mirror of the read-only server on `codex-worker` | S | later, if he uses the page | - |

Running cost after P3: collector 0 tokens; morning ~0.013 USD; Lead ~2-3k tokens per morning + < 1k per snapshot push; weekly ~1 %
of the Fable cap. Build cost: three Sonnet tasks on the subscription, two or three Gemini tasks, ~1 USD of API reviews.

## 9. Risks and how statuses stay honest

| Risk | Control |
|---|---|
| Stale data shown as current | `generated_at` on every view; per-source `last_ok` with grey badges; the morning job checks the collector's heartbeat and says so in the report; the Artifact prints its age in large type |
| Token creep | hard rule: the collector never calls a model; LLM only in the morning paragraph (budget-capped Haiku) and the weekly batch; the Lead reads the 40-line report, never `state.json` |
| Agents writing wrong statuses | they cannot write any; FINISH yields `claimed` at most, and only if its hash exists; gate PASS from the worker's own branch never counts as verified |
| "Green but not playable" | (1) `verified` needs a check at a commit that contains the merge, and a built main - "merged, main not built" (TASK_TEMPLATE checklist item 6) is its own amber state; (2) a feature behind a switch that is off shows «в игре, выключено»; (3) smokes in `knownFlaky` / `knownFail` of `Scripts/test_map.json` are tagged and do not turn a task green; (4) a metric below `min_n`, or inside the measured A/A noise of its target, is «мало данных»; (5) game tasks cannot exist without a where-to-check block; (6) the designer's verdict is the top state and works downwards too: his "не работает" reopens a green task (as on 11-20, door breach on the new map) |
| Wrong task matching | visible «без задачи» bucket; optional `Task:` trailer; branch names already carry NNN for Gemini / Grok |
| Plan decay (F2 one level up) | day rows come from the DAY_PLAN file the Lead already writes; "no day plan today" is printed in red in the designer's report; P5 retires the duplicate lists |
| Evidence loss at the PC format | plan, ledger and daily snapshots are in git; `Saved/Progress` is rebuildable; check events store the facts and the file hash, not a path into `Saved/Logs` (not mirrored); one install script re-creates the scheduled tasks (add to `MAIN_PC_FORMAT.md` restore list) |
| Touching the designer's files / fighting his editor | collector uses git plumbing only with `GIT_OPTIONAL_LOCKS=0`, writes only under `Saved/Progress`; never builds, never runs the editor |
| New network surface | separate GET-only server, tailnet address only, Host allowlist; 5173 and 8765 stay local; state holds FINISH fields, not chat bodies; Artifact private, `db` readable by the owner only |
| Pressure to make gates green | gates and targets live in plan.json (Lead only, protected by the branch guard); the TASK_TEMPLATE "Never" list already forbids loosening a check |
| Collector dies silently | it gets its own lane in `LANES.json` with a heartbeat file, so the existing watchdog posts the STALL |

## 10. Questions before P1

- **Q1 (designer):** what are the stages of the global goal after roadmap stage 4? The repo has decisions (narrative prologue, squad
  of six, vehicles, weapons order) but no agreed sequence. Until answered they sit in «Позже», unordered.
- **Q2 (designer):** phone access - Tailscale switched on when he wants the live page (recommended), or snapshot only?
- **Q3 (Lead):** who builds P0 today - a Sonnet subagent, or Gemini as task 100 after 098? P0 must not take time from 19c or enemy-ai.
- **Q4 (Lead):** agree the `ID` column in DAY_PLAN files and the optional `Task:` commit trailer.
- **Q5 (spike, 30 min):** F13 - Artifact `db` document size limit and the `files` capability on a local project.
