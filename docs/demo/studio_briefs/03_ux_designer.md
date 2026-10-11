# 03 UX Designer: onboarding + tutorial for Demo #1 (testers)

Scope: read-only review of `docs/port/HANDOFF.md`, `Source/CodexTactics/{Private,Public}/UI/**`, `CodexTacticsPlayerController.cpp`, `MissionRules.cpp`, `Content/Data/Narrative/narrative_manifest.json`, the Godot `design/` folder. Nothing was edited or built. Facts below are from code; items marked (?) are inferred and need a check.

## 1. Current onboarding state

**There is no tutorial.** A grep of the repo found no tutorial/onboarding code or data. The Godot `design/` folder has none either. It only has a Stage-01 LDD that says zone 1 "teaches 8-direction movement, sprint, formation", and that was never implemented as UI.

**What the player is told today, and where**

| Channel | Content | Problem for a first-time tester |
|---|---|---|
| Intro dialogue (`DA_DialogueIntro`, 11 lines, `DialogueWidget`) | Lore only (Sector-17, rumours). Space = next, Esc = skip. | About 1 minute of lore, zero mechanics. |
| Objective banner (top-left, canvas, `MissionRules`) | "Explore the checkpoint and find a way to open the blast gates", then quest-chain texts, "DEFENSE PREPARATION: place turrets, barricades and mines ([Space] for orders)", "DEFENSE: Repel wave N! Enemies: M". | Says what, never how. Only one line mentions a key. |
| Message feed (top-right, canvas, `GameMessageSubsystem`) | About 110 `PostMessage` calls. Mostly reactive: "[PLAN] ...", "Weapon: ...", "Pick an enemy ... for aimed fire [Ctrl+Click]!", "During combat, moving is only possible in tactical pause [SPACE]!". | Hints appear after a failed action, vanish after a timeout, and are not repeatable. |
| Combat badge (top-centre, canvas) | `REAL TIME [SPACE - pause · hold 1.5s - turn-based]` and `FIRE: PASSIVE [,] pas [.] def [/] agg · Alt: squad`. | The only always-on key hints. Cryptic: P/D/A letters over heads are never explained. |
| Squad panel (top-left, canvas) | `PHASE: ...`, `Mode: squad [B]`, `AUTONOMY: OFF [Ctrl+T]`, one long line per operative (stance, fire, HP, cold %, tier, ammo, matches, grenades, deployables). | This is a debug dump. It is also where the player learns B and Ctrl+T. |
| Action bar (bottom, UMG `ActionBarWidget`) | Inventory, weapon slot `M16 [30/90] \| [G] Grenade`, relocate, stance, guard, squad slots 1-4 with HP/cold bars. Keys sit in tooltips only. | Tooltips need hover and are not discovered by accident. |
| Banners (`PhaseBannersWidget`), `HordeAlertWidget` | Prep countdown, wave counter, free-pause banner, "HORDE INCOMING / TACTICAL PAUSE - GIVE YOUR ORDERS \| [SPACE] TO ENGAGE", "[Esc / click] skip". | Good in tone. One-shot, no explanation of what a pause allows. |
| Turn-based HUD buttons | `NEXT OPERATIVE [Tab]`, `End turn [Enter]`, `STANCE [C]`, `Turn [R]`, `GET UP (2 AP) [Z]`, `Barrel [F]`. | Keys are visible here, but only after the player is already in TB. |
| Pause menu, Options | Resume/Save/Load/Options/Quit. **Options is a placeholder ("Graphics, audio and control settings arrive in a later update").** | No controls reference anywhere. |

**Undiscoverable today (no in-game explanation, or only an after-failure line)**

- *Camera*: W/A/S/D and screen-edge scroll, Q/E or Left/Right rotate, RMB-drag rotate, MMB-drag pan, mouse wheel zoom, 1-4 recentres the camera.
- *Selection/orders*: box-select drag; Shift+click = face/watch sector; Shift+RMB = hold-line (defense line) assignment; **Ctrl+click = aimed fire** (only hinted after a miss-click); **Alt+click = cover**; clicking a live enemy during a wave = priority target; double meaning of click (ground / object / wall).
- *Posture and stance*: Z/C/V stances, **Alt+Z/C/V = whole squad (Alt+V)**; `,` `.` `/` fire posture, Alt = squad; **N** cover-fire mode; **T** guard; **Ctrl+T** Commander Mode (autonomy); **B** solo.
- *Survival/items*: H/J/K/L items, **U heat pack**, drawer drag-and-drop to hand over items; cold tiers and warm zones; P profile dialog.
- *Combat*: **G** grenade, **Shift+G flare**, X cycle weapon, **F deploy / set up from supply**, R rotate placement, wheel to rotate ghost, Esc/RMB cancel.
- *Time*: **Space tap = pause, hold = turn-based, hold again = back to real time**; free pause at the horde; Tab/Enter in TB; **Z get up** after knockdown.
- *System*: **F5/F9** quicksave/quickload, **Ctrl+X** quick restart, Esc.

That is about 50 bindings. The first five minutes needs maybe 8.

**Other findings**

- `OrdersLocked` HQ line ("orders only in tactical pause ... squad acts autonomously") describes the old 2026-10-05 lock. The default is now `Codex.RealTimeOrders=1`, so the line is stale where it still fires. Check it.
- Input actions are created in C++ at runtime (`MakeAction`), so there is no table to read for "what key does X". Key icons, a controls screen and remapping all need that table first.
- The HUD is mixed: canvas (`DrawHUD`: objective, feed, badge, squad panel, labels), `AddToViewport` widgets at z 1-20, and the CommonUI layout (menus only).

## 2. Tutorial system proposal (minimal, data-driven)

**Goal:** a writer or the user can add or reorder tutorial steps without touching C++. Logic lives in JSON, text lives in the narrative pipeline, look lives in WBPs.

### 2.1 Prompt types (all non-modal except the optional "Welcome" card)

1. **Hint card.** Title, 1-2 lines of body, key caps inline, progress dots (e.g. 2/5), `[X] Skip tutorial`. Bottom-left or near the relevant widget. One card at a time.
2. **Objective tracker.** The existing objective banner grows into a checklist of at most 3 lines (current goal plus sub-steps). It reuses `UMissionSubsystem` and `UQuestSubsystem::OnObjectiveChanged`.
3. **HQ radio line.** In-fiction voice (Commander/HQ), one sentence, through the existing feed or the dialogue strip (speaker "HQ"). Never carries the exact key, only the fiction ("Squad, get eyes on that sentry"). The card carries the key.
4. **Highlight.** (a) A world ring/arrow marker at an actor or point (reuse `RadiusRingSubsystem` visuals). (b) A pulse on a named widget (action-bar slot, drawer button). Looked up by a stable widget id.

### 2.2 Step data (`Content/Data/Tutorial/tutorial_steps.json`, staged like `Data/LevelJson`)

```json
{ "id": "T04_ctrl_click", "tier": 2, "once": true,
  "trigger":  { "type": "first_enemy_seen" },
  "prompt":   { "kind": "card", "text_id": "TUT_T04", "keys": ["Order.AimedFire"],
                "radio_id": "TUTR_T04", "highlight": { "actor_tag": "TutorialTarget1" } },
  "complete": { "type": "command", "command": "Order.AimedFire", "count": 1 },
  "on_fail":  { "after_blocked": "Order.Move@Pause", "times": 2, "show": "T04_remedy" },
  "next": "T05_pause", "timeout_sec": 90, "skippable": true }
```

- **Triggers:** `volume` (a placed `ATutorialTriggerVolume`, patterned on `ADialogueTriggerVolume`), `event` (`UCodexEventBus`, `UMissionSubsystem`, `UQuestSubsystem`, `UGameFlowSubsystem` phase/mode), `first_use` (first time a system is touched: first item, first stance, first cold tier), `after_step`, `phase`, `failure_retry` (repeated blocked command, mission failed, a squad member downed).
- **Completion** is always a *player command id*, never a raw key. Each key handler in the player controller reports one logical command (`Camera.Rotate`, `Order.Move`, `Stance.Crouch`, `Time.Pause`, ...) through one choke point. The tutorial counts commands, so remapping or gamepad later changes nothing here.
- **Key tokens in text:** `{key:Stance.Crouch}` resolves via a small command registry (command id -> FKey, label id, section). The same registry feeds the controls screen. See work item W1.

### 2.3 Pacing rules

1. One new thing at a time: prompt, then the player does it, then a confirm (check mark plus soft tick, 0.6 s), then a 1.5 s breather, then the next. Never two cards at once; a queued trigger waits.
2. **Teach in tiers, only what the situation needs now**: T0 camera, move, select (min 0-3); T1 interact, cold basics, stance; T2 stealth/contact, cover, aimed fire; T3 pause/orders (Space) at the first fight; T4 preparation (deploy, items); T5 optional "field manual" tips on first use (autonomy, solo, guard, grenade, flare, posture, N, X). T5 is never forced.
3. No world pause for hints, except the existing free pause at the horde where Space is the lesson.
4. Never block progress: a step auto-hides on `timeout_sec`, on the player doing it earlier, or on a skip. Do not hard-gate inputs (exception: none for demo #1).
5. Cap: at most 7 *new keys* before the first fight; at most one remedial hint per 60 s.
6. A step already done (because the player did it earlier) is marked complete silently, with no card.

### 2.4 Skip, replay, controls reference

- **Skip:** `Skip tutorial` on every card, plus a "Welcome" card with *Guided* / *I know the game*. Choice is stored in a small profile file (separate from mission saves, which `SaveGameRules` treats as mission state).
- **Replay:** Pause menu gets `CONTROLS` and `TUTORIAL: ON/OFF / RESET`. Reset clears step progress; the next trigger re-fires.
- **Controls screen** (`F1` and pause menu): one page, five sections (Camera, Orders and selection, Combat, Survival and items, Time and system). Each row is key cap + label. Built from the same registry plus `controls.json` (section, order, label id). It is also the future home of remapping.

### 2.5 CommonUI layers and text pipeline

- The Controls screen is a `UCodexActivatableScreen` (new tag `Screen_Controls`) pushed on `Layer_GameMenu`, opened from the pause menu. A reset confirm uses `Screen_Confirm` on `Layer_Modal`.
- The hint card, tracker, radio strip and highlights live in **one persistent `UTutorialOverlayWidget`** (`AddToViewport`, z about 14: above banners 8-9, below the dialogue 15), `SelfHitTestInvisible` so it never steals clicks. Do not push cards as activatable screens: the layer containers are stacks, and `bEnableDefaultInputConfig=False` must stay (HANDOFF trap).
- Text: English only, same Google Sheet to `narrative_manifest.json` flow. Add a **Tutorial tab** (`StepID, Kind, Title_EN, Text_EN, Radio_EN`) and a `tutorial` section in the manifest; `FNarrativeManifest` gets `GetTutorialText(id)`; a missing id shows `[EN missing: TUT_xx]` like dialogue. Gemini owns the narrative content and `sync_narrative.py`, so the extension goes through TANDEM «Requests». Step *logic* stays in Claude's JSON and references text by id only.
- Input: keyboard and mouse now. Gamepad later: `UCommonInputSubsystem` already exists in the project; the registry returns an FKey per input type and the key cap widget swaps brushes. Not built for demo #1.
- Telemetry: log step shown, step done, time, and stalls to `Saved/Telemetry/tutorial.jsonl`, same style as the bot `runs.jsonl`. Testers' stalls then show in a Jev digest.

### 2.6 Suggested beat list for the demo level (outpost: explore, open the gate, waves)

| # | Beat | Prompt | Confirmed by |
|---|---|---|---|
| 0 | Mission start | Trim the intro to about 4 lines plus a one-line goal; Welcome card (Guided / I know the game) | choice |
| 1 | Camera | Card: look around (W/A/S/D or edge, Q/E, wheel) | any 2 camera commands or 20 s |
| 2 | Move | Ring marker on the ground 10 m away; "Click the ground" | leader reaches it |
| 3 | Select | Pulse on action-bar slot 2; "Press 2 or click a portrait"; mention drag to box-select | operative 2 selected |
| 4 | Interact | Marker on the quest object; objective tracker updates | action menu opened |
| 5 | Cold | First cold tier: card (warm zone, J/K/L, U) | item used or warm zone entered |
| 6 | Sentry ahead | Radio: "Sentry ahead, keep out of its sight"; explain vision cone and crouch (Z/C/V) | stance changed |
| 7 | Cover | Card: Alt+click a wall | cover taken |
| 8 | First contact | Card: click enemy = priority, Ctrl+click = aimed shot | command issued |
| 9 | Pause | At the horde free pause: card "Tap Space = pause and order; hold = turn-based" | Space tap |
| 10 | Prepare | Prep banner plus card: F deploy, R rotate | one deployable placed |
| 11 | Heal / survive | First wound or knockdown: card H medkit, Z get up | event |
| 12 | Checkpoint | After wave 1: "F5 quicksave" | F5 |
| T5 | Optional tips on first use | Autonomy Ctrl+T, solo B, guard T, G/Shift+G, posture `, . /`, N, X | none, auto-hide |

Check with the designer (?): that a sentry, a cover wall and the quest object exist in this order on the chosen map; I could not verify the layout without the editor.

## 3. HUD for testers

**Hide or demote for the demo** (one switch, e.g. `Codex.Demo=1` or a game-mode flag; `CodexTactics.HUD.ShowStatus` already gates the squad panel and operative labels in `DrawHUD`):

- Squad panel's `PHASE:` line and per-operative debug lines (stance, fire, "walking", cold %, ammo, matches, grenades, deployables). The action bar already shows HP and cold bars.
- Operative text labels over heads (`DrawOperativeLabels`).
- Console/dev output, bot/debug overlays, and anything under `Private/Debug`.
- Keep `[SPACE]`/`Ctrl+T` info, but move it into the combat badge and action bar.

**Keep and restyle:** objective banner (becomes the tracker), message feed (shorter lifetime, "HQ" style), combat mode badge (fire posture line becomes a small widget with the P/D/A legend), action bar, banners, horde alert, floating text, hit-chance label, vision cones (they are gameplay information in a stealth level, so keep on; confirm with the user).

**Add:**

- Always-visible footer: `F1 Controls   Esc Menu   F8 Report` (small, bottom-left).
- Objective tracker (see 2.1).
- **Feedback button (F8 + pause menu entry):** local report folder `Saved/Feedback/<timestamp>/` with a screenshot, the current quicksave, the last 60 s of the feed, log tail, position/phase/wave, build id, and an optional one-line comment box. The tester sends the folder. No network in demo #1.
- Build stamp (version/changelist/date) bottom-right, very small.
- Optional debug overlay on `F3` for testers who want it (F1, F3, F8 are unbound in the controller; confirm F3/F8 against the other agents' work).
- Options: either hide the placeholder Options button for the demo, or put the Controls page there.

## 4. Work items for Demo #1

S < 0.5 day, M 0.5-2 days, L > 2 days (Claude's estimate for the code part).

| ID | Item | Size | Owner |
|---|---|---|---|
| W1 | Command registry: replace the runtime `MakeAction` list with a table (command id, FKey, label id, section); keep behaviour identical | M | Claude |
| W2 | Report logical player commands from the controller's handlers through one choke point (`OnPlayerCommand`) | M | Claude |
| W3 | `UTutorialSubsystem` (world): JSON loader, state machine, trigger evaluators, completion, timeout, once/skip, profile persistence, telemetry | L | Claude |
| W4 | Pure rules + automation tests `CodexTactics.Tutorial.*` and `TutorialSmoke` | M | Claude |
| W5 | `UTutorialOverlayWidget` C++ base with named slots (HintCard, Tracker, RadioStrip) + key-cap rich-text decorator `{key:...}` | M | Claude, look by user |
| W6 | `ATutorialTriggerVolume` + `ATutorialMarkerActor` (world ring/arrow) + named-widget pulse | M | Claude |
| W7 | `UCodexControlsScreen` (`Screen_Controls`, F1, pause menu button, `controls.json`) | M | Claude |
| W8 | Pause menu: `TUTORIAL ON/OFF/RESET` and `CONTROLS`; hide or fill placeholder Options | S | Claude |
| W9 | Demo HUD preset: gate squad panel/labels/PHASE line; footer hints; move key hints into badge and action bar | S (gate) / M (rework) | Claude |
| W10 | Remedial hints: hook the existing "blocked" feed posts (orders locked, move only in pause, relocate in combat) to the tutorial; "Tip" line on the Mission Failed screen incl. Ctrl+X | S | Claude |
| W11 | Feedback report (F8): screenshot, save, feed tail, log tail, metadata, comment box; build stamp | M | Claude |
| W12 | Tutorial tab in the sheet, `sync_narrative.py` extension, `FNarrativeManifest::GetTutorialText`, `[EN missing]` fallback | S | Gemini (request) + Claude (reader) |
| W13 | Author `tutorial_steps.json` (about 15 steps), place volumes/markers on the demo map, trim the intro dialogue | M | Claude + writer + user (map) |
| W14 | Fix stale `OrdersLocked` text; check `FirstPreparation` text against current flow | S | Claude |

**What the user (artist) designs in WBPs** (names are the contract for BindWidget):

- `WBP_TutorialOverlay`: `HintCard` (Title, Body as rich text, ProgressDots, SkipButton, ConfirmCheck + show/confirm/hide animations), `Tracker` (up to 3 lines with check states), `RadioStrip` (portrait, speaker, line).
- `WBP_KeyCap` (+ rich-text style set): normal, wide (Space/Shift/Ctrl), mouse-button glyphs; gamepad brushes later.
- `WBP_ControlsScreen` + row widget (key caps + label, 5 section headers); `WBP_FeedbackDialog`.
- Highlight look: world ring/arrow material (Gemini owns materials per TANDEM, so request it), and a named `Pulse` animation on action-bar slots and the drawer button.
- Restyle of the combat badge, objective banner and message feed in UMG (optional for demo #1; canvas versions work).
- A confirm tick sound and a hint-appear sound (Gemini/audio side).

## 5. Questions for the user (max 5)

1. **Which map and how long is Demo #1?** Recommended: `L_MovementTest` (the standard map), flow explore, open the gate, 3 waves, about 20 minutes; tutorial covers beats 0-12.
2. **How forceful is the tutorial?** Recommended: guided but non-blocking; a Welcome card with *Guided* / *I know the game*, skip on every card, no hard input gating.
3. **Voice of instructions?** Recommended: both. One in-fiction HQ radio line for flavour plus a clean hint card with the exact keys (radio never carries the key).
4. **How do testers send feedback?** Recommended: local F8 report folder (screenshot, save, log tail, note) that the tester zips and sends; no network code in demo #1.
5. **Debug panels for testers?** Recommended: hidden by default in the demo preset, `F3` toggles a trimmed overlay; vision cones stay on.

Выполнено моделью: Sonnet 5.5
