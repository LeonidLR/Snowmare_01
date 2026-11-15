# Tactical pause economy - studio review of "the pause is earned by hits / kills"

**Status:** PROPOSAL for the game designer, 2026-11-15. Read-only review: no code, data or asset was changed, nothing was built or run.
**Author:** Claude (model router level 4). **Format:** the studio-brief format of `docs/demo/studio_briefs/` (creative director, level
designer, UX, QA), then a synthesis and one recommendation.
**The proposal under review (designer, 2026-11-15):** the tactical pause should be ACCUMULATED depending on how many enemies the squad
has hit / killed; a low-intensity fight is no reason to give the player concessions.

Sources read: `CLAUDE.md`; `Source/CodexTactics/{Public,Private}/GameFlow/*`, `Combat/CombatTimeModeRules.*`,
`Core/CodexTacticsPlayerControllerTime.cpp`, `Core/LevelFlowRules.cpp`, `UI/PhaseBannersWidget.cpp`, `UI/CodexTacticsHUD.cpp`,
`Combat/KnockdownRules.h`, `Combat/CombatPressureRules.h`, `Characters/CriticalWoundRules.h`, `Bot/PlaytestBotSubsystem.cpp`;
`docs/systems/{waves-horde-encounters,interactables-traps-loot,pmc-squad-ai,save-load,stealth-lure-and-scent}.md`;
`docs/design/{FIRST_AID.draft,SUPPRESSION,AI_DIRECTOR,FORMATION_SECTORS}.md`; `docs/port/HANDOFF.md`; Godot archive
`Scenes/movements/main.gd`, `docs/GAME_DESIGN_SPEC.md`, `design/gdd/*.md`.

## 0. Verdict

1. **The diagnosis is right.** Today the pause is close to free exactly where it is least deserved: every fight, even one lone zombie,
   starts with 3 pauses of 30 s (section 1).
2. **The cure as worded ("earned by kills") is a weak proxy.** Kills measure how well the player is doing, not how hard the fight is. A
   kill-only pause arrives late, peaks when the fight is already won, and is absent when the squad is pinned, ambushed or has a man down.
3. **Keep the idea, change the currency.** Recommended: **command charges** = a small grant scaled by the THREAT that engaged the squad
   (nothing for a trivial contact) + accumulation by damage dealt and kills (the designer's idea) + one emergency charge when a fighter
   goes critical. Cap 2, 20 s each, no free refill, nothing carried out of the fight (section 5).
4. **One decision blocks everything:** turn-based mode (hold Space) is unlimited today. If it stays free, a scarce pause changes nothing
   wherever the ground is flat (section 6, question 1).
5. Size: **medium** (2-3 agent-days + review). A "shadow meter" that only logs what the player would have had is **small** and should
   come first, so the numbers are tuned on real fights before the rule flips.

## 1. How the tactical pause works today (verified in code)

| Fact | Where |
|---|---|
| Three time modes in a fight: real time (default), tactical pause (Space tap), turn-based grid (Space hold 1.5 s). | `Public/Combat/CombatTimeModeRules.h:36-46` |
| The pause exists ONLY in a fight (`WaveCombat`). Exploration / stealth has no tactical pause. | `Private/Combat/CombatTimeModeRules.cpp:6`, `Private/GameFlow/GameFlowStateMachine.cpp:118-121` |
| **3 charges per wave**, each pause lasts up to **30 real seconds** then releases itself; world time x0.02. | `Public/GameFlow/GameFlowTypes.h:95-109`, `GameFlowStateMachine.cpp:133-145, 334-345` |
| When all 3 are spent, a **20 s cooldown** returns ALL 3. | `GameFlowStateMachine.cpp:324-332, 461-471` |
| Every wave start refills to 3, and so does the rest between waves. Every skirmish with a patrol is a new fight (`StartAmbushCombat` -> `StartWave`), so it also starts with 3. | `GameFlowStateMachine.cpp:75-91, 234-242, 380-393, 445-459` |
| Orders given in the pause are queued, clamped to 12 m from where each operative stood, and run together on release. | `GameFlowTypes.h:147-149`, `Private/Characters/SquadSubsystem.cpp:1325-1430` |
| Turn-based: entries per wave unlimited (`TurnBasedUsesPerWave = -1`); the grid freezes pause charges and cooldown. Needs flat open ground and an enemy within 15 m. | `GameFlowTypes.h:143-153`, `GameFlowStateMachine.cpp:148-184, 322-332`, `Private/Core/CodexTacticsPlayerControllerTime.cpp:152-166` |
| No free pause any more (designer 2026-10-26: "the tactical pause only by Space", "the first pause of a wave free: NO"); the old horde auto-pause survives only behind `Codex.HordeIntro.AutoPause 1`. | `Public/GameFlow/GameFlowStateMachine.h:66-71, 105-109`, `docs/port/HANDOFF.md:348` |
| Designer's stated intent 2026-10-26: "The tactical pause is an auxiliary pause that gives the player a breather; it must not cancel everything else." | `docs/systems/interactables-traps-loot.md:29-30` |
| The numbers are C++ defaults copied from Godot constants (`TACTICAL_PAUSE_DURATION 30`, `MAX_CHARGES 3`, `COOLDOWN_TIME 20`). They are in no JSON and not in `DA_GameBalanceConfig`; the level config overrides only preparation / rest / wave count. | Godot `Scenes/movements/main.gd:201-207, 4675-4730`; `Private/Core/LevelFlowRules.cpp:5-37` |
| HUD: banner "ORDER MODE - Charges per wave: N/M - Planning time", header "pauses N/M", feed lines. One feed line has "3" and "20s" hard-coded. | `Private/UI/PhaseBannersWidget.cpp:166-176`, `Private/UI/CodexTacticsHUD.cpp:1574-1575`, `CodexTacticsPlayerControllerTime.cpp:92-102` |
| Knockdown timers are frozen in the pause; cold, weather clock and the horde countdown stop too. | `Public/Combat/KnockdownRules.h:218-219`, `docs/systems/waves-horde-encounters.md:26` |
| The playtest bot fights in real time and never pauses. | `Private/Bot/PlaytestBotSubsystem.cpp:186, 214` |
| Godot design docs define no pause economy (only the constants above; `GAME_DESIGN_SPEC.md:42` speaks of 5-10 s micro-pauses between waves). | Godot `docs/GAME_DESIGN_SPEC.md`, `design/gdd/*.md` (grep) |

**What this means.** The generosity is inverted. A 20-second skirmish with one sentry gets 3 x 30 s of stopped time, far more than it can
use. A long horde or PMC fight is the only place the limit bites, and there the 20 s refill makes it a rate limit, not a budget. Nothing
ties the pause to how dangerous the fight is. The designer's complaint is correct.

## 2. What is good in the proposal

- It turns the pause from a wall-clock allowance into a resource with a source inside the fight, so spending it becomes a decision.
- It is easy to explain ("hit them and you earn the right to stop the clock") and one event already exists (`EnemyKilled`,
  `Public/Core/PlayerCommands.h:431-437`).
- It rewards decisive action and scales by itself with the size of a horde.
- It removes the arbitrary "3 per wave".

## 3. Studio discussion

### 3.1 Creative director / game design

The demo pillar says: "Stop time, think, command. When it goes wrong you tap Space" (`docs/demo/studio_briefs/01_creative_director.md`,
pillar 2). In this game the pause is a DEFENSIVE and cognitive tool: it is the only way to give three operatives synchronised orders, and
it is the answer to surprise. A kill-fed pause treats it as an offensive reward. That mismatch produces the real risks:

| # | Risk of "earned by kills only" | Why it matters here |
|---|---|---|
| 1 | **Wrong measure.** Kills count success, not intensity. Twenty hounds at the gate with none dead yet is the most intense moment and pays nothing; shooting the last two runners pays. | The stated goal is "no concessions in a LOW-INTENSITY fight". Intensity is the threat to the squad, known at contact. |
| 2 | **Death spiral.** Pinned, flanked, a man down, killing nothing: zero pause exactly when it is needed. The winner gets more control, the loser less. | Survival game, permanent losses, and from #063 a 120 s bleed-out clock. |
| 3 | **No pause at contact.** The most valuable pause of any fight is the first one. The pressure system's ambushes (single spot, coordinated four-party plans) and the PMC's bait-and-ambush are built to surprise. With zero at contact the best AI trick becomes a reflex test. | Contradicts "planning beats reflexes". |
| 4 | **PMC fights starve.** A fireteam in cover under mutual suppression yields few kills for minutes (the night trainer was paused for PMC stalemates). That is the most order-heavy fight in the game, and the suppression design wants "suppress, then flank" (`docs/design/SUPPRESSION.md:11-12`), which needs synchronised orders. | Kill-only gives hordes a flood and PMC fights a drought: the opposite of tactical value. |
| 5 | **It pulls against the horde rule.** Ten firefights in a row summon the horde; slipping past patrols resets the streak (`docs/systems/waves-horde-encounters.md:17-27`). If charges are banked between fights, killing weak patrols to stock up becomes the best play and feeds the horde counter. | Two systems with opposite incentives. |
| 6 | **Stealth is punished twice.** Stealth happens in exploration, where no pause exists. When stealth breaks, a stealth player has killed nothing and starts the worst moment with zero. | Stealth v2 was merged today; the game tells the player to stay quiet. |
| 7 | **Hoarding.** A scarce earned resource is saved "for later" and never used. The feature then disappears from play. | Needs a low cap and use-it-or-lose-it. |
| 8 | **Accessibility.** In a real-time game with about 50 bindings the pause is the main aid for slower players. Gating it behind performance excludes those who need it. | Needs an "unlimited" option. There is no difficulty setting in code yet (only bot profiles and wave modifiers). |
| 9 | **Loophole: turn-based.** Hold Space is a stronger, unlimited time stop. | Section 6, question 1. |

Precedent: Max Payne refills bullet time by kills, and it works there because bullet time is an attack tool in a power fantasy. FF7 Remake
fills ATB both by attacking and by guarding. Fighting games fill the super meter when you are hit as well as when you hit. Door
Kickers leaves the pause free and rewards NOT pausing. The models that survive are the ones that pay on both sides of the exchange.

**View:** keep "earned", reject "by kills only". Pay for threat faced and for damage done.

### 3.2 Level designer: T-map and encounter pacing

T-map here = the tactical map of an encounter (approach lanes, cover, choke points, sight lines, flank routes, fallback points) plus its
tension curve over time. The pause is a rest beat inside the peak of that curve. How each model places the beats:

| Encounter on the T-map | Today | Kills only | Recommended (section 5) |
|---|---|---|---|
| Lone sentry / pair of hounds (low intensity) | 3 pauses | 0 until it is over | 0 (meter fills 25-50 %): **no concession** |
| Patrol of 3 military zombies | 3 | 0 at contact | 1 at contact, second partly earned |
| Ambush sprung on the squad (flanks, then rear and front) | 3 | 0 at the spring: reflex test | 1 at the spring, more by damage: "fix, plan, counter" |
| PMC fireteam across a yard (fix and flank) | 3 + refill | starved for minutes | 1 at contact, ~1.5 more by wounds and kills, +1 if a man goes down |
| Horde at a choke point (30-45 melee) | 3 + refill every 20 s | flood, cap always full | 1 at contact, ~1 per 10 kills, cap 2 |
| Brute + escort | 3 | one big payout at the very end | 1 at contact; the Brute pays half while it is being worn down |
| Multi-wave defence | 3 per wave | carries, floods | meter kept across the rest; each wave is a new contact |

Level-design consequences:

1. **The 12 m order radius already defines what one charge buys:** one local manoeuvre, not a redeployment. With scarce charges, every
   contact point with real threat needs cover or a flank corner within 12 m. Otherwise the charge is a consolation prize.
2. **Set pieces must declare their grant** (level JSON, section 5.6) instead of trusting the formula: the gate finale, the tutorial's
   pause lesson (`Content/Data/Tutorial/demo_01_steps.json:792-798` teaches "N pauses per wave, S seconds, C s recharge").
3. **Hand-authored budgets per encounter cannot be the whole answer.** The Depot's fights come from the pressure system (random patrols
   and ambushers every 120 s, `Public/Combat/CombatPressureRules.h:56-101`) and from generated levels. A formula from threat is required.
4. **Chaining.** If the meter resets when a fight ends, the designer controls accumulation by spacing: chain groups into one fight to let
   charges build, or leave a gap to reset them.
5. **Stealth lanes are unaffected** (no pause there today either). When stealth breaks, the contact grant depends on what noticed the
   squad, so a blown stealth run is not punished twice.
6. **Pacing curve.** Kill-only puts every rest beat at the end of the curve. A contact grant puts one at the start of the peak, and
   accumulation puts the second after first blood: contact, fix, first blood, manoeuvre, finish. That matches the build-up / peak / relax
   cycle of `docs/design/AI_DIRECTOR.md` (draft) without depending on it.

### 3.3 UX

- **One glance must answer "can I pause, and how close is the next one".** Two pips and a thin fill bar next to the time-mode badge at
  the top centre. No numbers on the HUD.
- **States:** empty (Space key cap greyed), filling, ready (pip lit, short radio click), full (one pulse: overflow is being wasted, use it).
- **Gain feedback** from the source: a small tick from the enemy that was hit; "CONTACT" when the opening grant lands; "MAN DOWN -
  emergency pause [SPACE]" when the valve opens. Never pause automatically (designer rule 2026-10-26).
- **Refusal must teach:** Space at zero says "No command charge - 60 % to the next (hit and kill to earn)". Today's refusal text has "3"
  and "20s" hard-coded (`CodexTacticsPlayerControllerTime.cpp:102`).
- **Colour:** the cyan of the plan markers. Amber stays reserved for heat and safety (art bible).
- **Hoarding counter-measures are UX, not maths:** low cap, visible waste at full, reset at fight end.
- **Accessibility:** a pause mode option (Earned / Unlimited). Until an options screen holds it: level JSON and a console variable.
- **Tutorial debt:** the Lake's two pause cards and the tokens `{pause_charges}` / `{pause_seconds}` / `{pause_cooldown}` describe the old
  rule (`docs/systems/tutorial-engine.md:554`). The Lake should stay on the old rule until the cards are rewritten.

### 3.4 QA / balance

| Topic | Finding |
|---|---|
| Exploits to close | (a) turn-based as a free pause; (b) wound, let it heal, wound again (hostile PMC fighters can heal): cap the damage credit per enemy; (c) drop aggro and re-engage to farm the contact grant: use the fight's peak threat, never re-grant below it; (d) third-party kills (monsters vs PMC, enemy-lit barrels) must pay nothing; (e) Esc menu as a thinking pause: no orders possible, accept. |
| Regression surface | `FGameFlowStateMachine` is central. `HordeIntroSmoke` asserts "charges 3/3" and "max - 1" (`Source/CodexTacticsDev/Private/Debug/HordeIntroSmokeCommand.cpp:328, 448, 501, 542`); `CombatFlowSmoke`, `PauseBarrelOrderSmoke`, `RadiusRingSmoke`, `CombatFormationSmoke`, `GroupCoverSmoke`, the Lake pause-lesson smokes all open pauses. Keep the old rule as a selectable mode so these stay valid; the new rule gets its own tests and smoke. |
| Save / load | Saving is allowed only outside combat, and pause charges are already "derived, not saved" (`docs/systems/save-load.md:93`). A meter that is zero outside a fight needs no save-format change. Carrying charges between fights would need one. |
| Bot | The bot never pauses, so its win rates do not move, and it cannot validate spending. It CAN log the meter (what was available, when) on every run for free. |
| Night trainer | AI against AI, no player pause: unaffected. Its PMC stalemates are evidence for risk 4. The hostile PMC has no pause at all; the asymmetry is acceptable. |
| Hostile PMC hook | A PMC fighter is an `AOperativeCharacter` on a hostile team (`docs/systems/pmc-squad-ai.md`, section 1). `EnemyKilled` is raised only for `AEnemyCharacter` (`Private/Characters/EnemyCharacterHealth.cpp:201-204`). PMC damage and deaths need their own hook. Main integration risk. |
| Telemetry | Per fight: charges earned by source, spent, wasted at the cap, Space refusals, seconds at zero charges while any operative is under 50 % HP, duration, outcome. The schema already has `tactical_pauses_used` (`Content/Data/Schemas/telemetry_run.schema.json:51`); no writer was found in `Source/` (grep). |
| First targets (guesses) | Trivial skirmish: 0 pauses. PMC fireteam: 2-3. Horde wave: 3-5. Space refused while a man is down: never. |

### 3.5 Synthesis

All four roles agree on three points. First, today's rule is too generous in small fights and the designer is right to change it. Second,
"kills only" fails at the three moments the pause exists for: contact, being pinned, a man down. Third, the fix is not a different
reward for winning but a second source that pays for danger faced. The level designer adds that the formula must work on generated and
pressure-driven fights, QA adds that the old rule must stay selectable, UX adds that the cap must be low and the meter visible.

## 4. Alternatives compared

| | Rule | Meets "no concession when easy" | Death spiral | Stealth / solo | PMC fight | Hoard / farm | Legible | Cost |
|---|---|---|---|---|---|---|---|---|
| A | **Kills only** (designer's wording): charge per N kills | yes | **bad** | punishes a blown stealth run | **starved** | farm weak patrols if banked | very | small |
| B | **Combat contribution**: hits, kills, suppression, assists, damage taken, revives | yes | medium (damage taken helps) | neutral | good | low | **poor** if more than 3 sources | medium |
| C | **Base regen + kill bonus**: 1 charge per X s of fight, kills speed it up | **no**: waiting pays, easy fights pay | none | neutral | good | turtling | good | small |
| D | **Danger meter**: pause granted when HP is low / pinned / man down | yes | **none** (it is the cure) | neutral | good | none | poor: no proactive use, "why now?" | medium |
| E | **Budget per encounter set by level design** | yes | none | neutral | good | none | very | small code, **large authoring**; impossible for generated / pressure fights |
| F | **One bank of seconds that drains while paused**, refilled by any source | yes | depends on source | neutral | depends | low | good | medium (new HUD, new state); timer stress in the pause |
| G | **Threat-scaled grant at contact**, no refill | **yes, exactly the stated goal** | low (no recovery later) | good | first pause only | none | good | **small** |
| H | Keep it free, add a price: cold keeps draining in the pause | weak | none | neutral | neutral | none | poor (invisible cost) | small; punishes slow thinkers |

None is enough alone. **G** answers the stated complaint at the lowest cost but does not "accumulate". **A** accumulates but fails at
contact and under pressure. **D** fixes the spiral but cannot be planned around. The recommendation is **G + A (widened to damage) + one
valve from D**, with three visible sources so it stays legible. F is the best later refinement if testers find 20 s charges too coarse.

## 5. Recommendation: command charges

### 5.1 Rules

1. The pause costs one **command charge**. A charge is 100 points of a meter; at most `max_charges` are held; overflow is lost.
2. **Contact** (threat faced): the meter is owed `contact_points_per_threat` x the fight's PEAK engaged threat, at most 100 per fight.
   It is paid as the peak rises, so a trivial contact pays a fraction and a late, bigger group still pays. Each new scripted wave or
   horde release re-arms it.
3. **Damage and kills** (the designer's idea): every enemy is worth `kill_points_per_threat` x its threat weight. Half is paid in
   proportion to the damage the squad deals to it (credited once, never above its max health), half on the kill. Operatives, squad
   traps and squad turrets count; anything else pays nothing.
4. **Man down** (the valve): when an operative enters the critical state, +100, once per operative per fight.
5. No cooldown refill, no refill per wave. The meter is kept through the rest between waves of one fight and is **zero when the fight
   ends** (skirmish over, post-combat, exploration).
6. A pause lasts at most `pause_seconds` and releases itself as today. Releasing early refunds nothing (v1).
7. Entering turn-based costs `turn_based_cost` charges (question 1).
8. Modes: `legacy` (today's rule), `earned` (this), `unlimited` (no charges). Per level and per difficulty.

### 5.2 Starting numbers (to be tuned from the shadow meter)

| Key | Story | Normal | Veteran |
|---|---|---|---|
| `mode` | unlimited | earned | earned |
| `max_charges` | - | **2** | 1 |
| `pause_seconds` | 30 | **20** | 15 |
| `contact_points_per_threat` (max 100 per fight) | - | **25** | 15 |
| `kill_points_per_threat` (half by damage, half on the kill) | - | **10** | 8 |
| `man_down_points` | - | **100** | 100 |
| `turn_based_cost` (charges) | 0 | **2** | 1 (= the whole bar) |
| `pin_points` (enemy pinned by squad suppression, once per enemy per 20 s) | - | 0 for now, 10 if PMC fights starve | 0 |

Threat weights: Hound 1, Frostbitten 1, Military Zombie 2, Cutter 2, Marksman 4, hostile PMC fighter 5, Brute 6.
A fight "counts" from threat 4: one PMC, one marksman, two military zombies, four hounds.

### 5.3 Worked examples (Normal)

| Fight | At contact | Earned during | Pauses in total | Today |
|---|---|---|---|---|
| 1 frostbitten sentry | 25 % | +10 % | 0 | 3 |
| 2 hounds + 1 frostbitten (threat 3) | 75 % | +30 %: full as the last one falls | 0 usable | 3 |
| 3 military zombies (6) | 1 | +60 % | 1 | 3 |
| PMC fireteam of 3 (15) | 1 | +150 % (half as they are wounded) | 2-3, +1 per man down | 3 + refill |
| Coordinated ambush: 2 marksmen, 4 military zombies, 2 cutters (20) | 1 at the spring | +200 % | 3 | 3 + refill |
| Brute + 6 hounds (12) | 1 | +120 % | 2 | 3 + refill |
| Horde of 45 hounds | 1 | +450 %, one per 10 kills | 5, never more than 2 held | 3 + 3 every 20 s |
| Any fight, a fighter goes critical at zero charges | - | +1 at once | - | - |

### 5.4 Interactions

- **Stealth:** nothing is earned or spent in exploration; silent kills pay nothing, so there is nothing to farm. A blown stealth run gets
  the contact grant of whatever noticed the squad.
- **Horde rule:** no carry-over means no reason to pick fights for charges, so it does not push against "ten firefights summon the horde".
- **Critical wound / revive (#063, `docs/design/FIRST_AID.draft.md:59-65`):** the man-down charge gives one guaranteed pause to organise
  the rescue (who carries the medkit, who covers). The 120 s bar should run on game time, like the knockdown bar: the pause neither
  stalls it nor costs seconds of it. Until #063 exists the hook is the existing Critical wound tier (`CriticalWoundRules.h:27`, HP share
  under 20 %). A completed revive pays nothing extra (the valve already paid).
- **Turn-based:** costs the whole bar, so the grid becomes the big spend and the pause the small one. Free and unlimited would empty the
  economy.
- **Solo play:** real-time orders stay free and immediate. The charge is for the synchronised plan of the other two. Scarcity makes
  that plan a moment, which fits "solo first, formation for complex moves".
- **PMC vs monsters:** weights make one PMC worth five hounds, and half of it is paid for wounding. The hostile PMC itself has no pause.
- **Difficulty:** the table in 5.2; "unlimited" is also the accessibility option.
- **Bot / trainer:** the bot logs the meter and never spends; win rates are unchanged. The trainer is unaffected.
- **Save / load:** nothing to save (zero outside a fight).

### 5.5 HUD

Top centre, beside the mode badge: `[SPACE]` key cap, two pips, a thin bar towards the next pip. Feed and banner texts read the config
(no literals). In the pause: the 20 s ring and the pips left. Detailed numbers only in the controls / field-manual screen.

### 5.6 Data

| Where | What | Why there |
|---|---|---|
| `Content/Data/Combat/tactical_pause.json` (new) | every key of 5.2 per difficulty preset, the threat weights | The pattern of all UE-only systems (`suppression.json`, `under_fire.json`, `horde.json`); `Data/Combat` is already staged (`Config/DefaultGame.ini:22`). |
| Level JSON root block `"pause"` | `mode`, `max_charges`, `start_charges` (fixed grant at fight start for set pieces and lessons), `gain_multiplier`; per wave `"pause_grant"` | Level-design lever; parsed like `"pressure"` / `"horde"` in `LevelJsonRules`; editable in the Wave Editor / Training Ground. |
| Console | `Codex.Pause.Mode`, `Codex.Pause.Grant <n>`, `Codex.Pause.Shadow 1` | Smokes, tuning, shadow logging. |
| `DA_GameBalanceConfig` | not recommended for v1 | `GameBalanceConfig.h` is generated from the frozen Godot file (`Scripts/generate_balance_config.py`); today's pause numbers are not in it. If the designer wants them in the asset, the generator must first learn UE-only fields (question 9). |

### 5.7 Implementation sketch (project style: pure rules + subsystem, no GAS)

1. **`GameFlow/PauseEconomyRules.{h,cpp}`** (pure, no world): `EPauseEconomyMode`, `FPauseEconomyConfig`, `FPauseMeter` (points, peak
   threat, contact points paid, damage credited per enemy id, man-down set). Functions: `ThreatOf(archetype / PMC)`,
   `ContactPointsOwed(Config, PeakThreat)`, `PointsForDamage(Config, Threat, Damage, MaxHealth, InOutCreditedShare)`,
   `PointsForKill(Config, Threat, Source)`, `PointsForManDown(Config, bAlreadyPaid)`, `AddPoints(Meter, Config, Points, OutWasted)`,
   `TakeReadyCharge(Meter, Config)`, `ResetForFightEnd(Meter)`, `ParseConfig(Json)`.
2. **`FGameFlowStateMachine`** stays the owner of `PauseCharges` (HUD, tutorial tokens and smokes keep reading it). New
   `FGameFlowConfig::PauseChargePolicy { PerWaveRefill, Earned, Unlimited }`. With `Earned`: `StartWave` of a new fight, `FinishSkirmish`
   and the end of the fight set 0; the rest between waves keeps the charges; the cooldown never starts; new `GrantPauseCharge()` (clamped
   to the max); `RequestEnterTurnBased` checks and spends `TurnBasedEntryCharges`. `PerWaveRefill` is untouched, so existing tests and
   smokes hold.
3. **`UPauseEconomySubsystem`** (tickable world subsystem) owns the meter and turns it into `GrantPauseCharge()` calls. Inputs:
   - engaged threat at 2 Hz from `UEnemyRegistrySubsystem` (enemies hunting the squad) and `UPmcSquadSubsystem` (hostile fighters in
     contact);
   - `UHealthComponent::OnDamaged` / `OnDied` of enemies with a squad-side source (`Public/Combat/HealthComponent.h:119-122`), plus the
     same for hostile-team operatives (no `EnemyKilled` is raised for them);
   - the Critical tier edge of an operative (later the #063 bleed-out state);
   - `UGameFlowSubsystem` phase changes for the reset.
   In `Codex.Pause.Shadow 1` it only logs and feeds telemetry.
4. **HUD / texts:** `PhaseBannersWidget`, `CodexTacticsHUD.cpp:1574`, the feed lines in `CodexTacticsPlayerControllerTime.cpp`,
   tutorial tokens (add `{pause_max}`; `{pause_cooldown}` only in legacy).
5. **Data:** `tactical_pause.json`, `LevelJsonRules` block, schema entry, Wave Editor field (later).

### 5.8 Tests and smoke

- Automation, `CodexTactics.GameFlow.PauseEconomy.*`: `TrivialContactPaysAFraction`, `ContactScalesWithPeakThreat`,
  `NoRegrantBelowThePeak`, `ContactRearmedByNewWave`, `DamageShareCappedPerEnemy`, `KillWeights`, `ThirdPartyKillPaysNothing`,
  `ManDownOncePerOperative`, `CapWastesOverflow`, `ZeroAtFightEnd`, `KeptAcrossTheWaveRest`, `TurnBasedCost`, `LegacyUnchanged`,
  `UnlimitedMode`, `DataKeys`.
- Headless smoke `CodexTactics.PauseEconomySmoke` on its own clear arena: one hound engages -> Space refused, meter 25 %; it dies -> the
  fight ends -> meter 0; three military zombies engage -> 1 charge -> Space opens the pause -> 0 left; damage and a kill fill the bar as
  computed; an operative forced critical at 0 -> 1 charge; the Space hold for turn-based refused below the cost; `mode legacy` -> 3 / 3.
- Existing smokes pin `legacy` through one helper in `SmokeUtils.h` until each is reviewed.

### 5.9 Rollout and size

| Step | Content | Size |
|---|---|---|
| 1 | Pure rules + tests + shadow meter (log + telemetry, debug HUD line), bot runs and one designer session on the Depot | small |
| 2 | Tune numbers from step 1; state-machine policy, subsystem grants, HUD pips, texts, smoke; `earned` on the Depot and the combat lab only | medium |
| 3 | Lake tutorial cards rewritten, then `earned` there; difficulty presets in the options screen; Wave Editor field | small-medium |

Total: **medium**. Risks: the hostile-PMC hooks, the turn-based decision, the number of smokes that open pauses.

## 6. Questions for the designer (before any code)

1. **Turn-based (hold Space):** does entering the grid cost charges (recommended: the whole bar, 2), keep its own limit, or stay free?
   Free means the earned pause changes little on flat ground.
2. **Contact grant:** accept one charge when a fight of threat 4 or more starts (recommended), or strictly zero until first blood (then
   ambushes and PMC contacts are pure reflex; suggested only for Veteran)?
3. **Man-down charge:** +1 when a fighter enters the 120 s critical state - yes or no? And does the bleed-out bar stop during the pause
   (as the knockdown bar does today) or keep running?
4. **Carry-over:** zero at the end of each fight (recommended) or banked between fights (allows farming and hoarding, must be saved)?
5. **What pays:** all squad-caused damage and kills, traps and turrets included (recommended)? Damage without a kill?
6. **Size:** 2 charges of 20 s on Normal (today 3 x 30 s with refill) - agreed as the starting point?
7. **Difficulty / accessibility:** is an "unlimited pause" Story setting acceptable? Veteran = 1 charge?
8. **Rollout:** Depot and combat lab first, the Lake demo on the old rule until its pause lesson is rewritten?
9. **Tuning place:** `tactical_pause.json` + the level JSON block (recommended), or fields in `DA_GameBalanceConfig`?
10. **PMC weight:** one PMC fighter worth five hounds, half paid for wounding him - and a suppression "pin" bonus now or only if
    telemetry shows PMC fights starved?

## 7. Verified and not verified

- **Verified by reading code / docs:** every row of section 1; the event and hook locations in 3.4 and 5.7; staging of `Data/Combat`;
  the designer's decisions quoted with file references.
- **Not verified:** real fight lengths and kill rates (no telemetry was read; all numbers in 5.2-5.3 are first guesses for the shadow
  meter to correct); that nothing writes `tactical_pauses_used` outside `Source/` (grep of `Source/` only); that the custom-formation
  placement pause cannot be opened in a real-time fight (docs say exploration or inside a tactical pause only,
  `docs/design/FORMATION_SECTORS.md:384`); the exact shape of #063, which is not implemented yet; the attacker-side information available
  on `OnDamaged` for crediting squad damage.

## Decisions 2026-11-15 (designer)

- All 10 open questions: **as recommended** (TB entry costs the whole bar; contact charge for serious threats; emergency charge on a critical wound, the 120 s bar pauses; reset at combat end; traps/turrets/damage count; 2 x 20 s; unlimited on easy / 1 charge on Veteran; Depot + combat lab first, Lake later; JSON data; PMC = 5 hounds, suppression bonus after telemetry).
- **Scope now: ONLY the shadow counter.** It counts what the earned-charge rules would give, logs it next to the real pause use, changes nothing in play. Every game start, in every current build copy (main, gemc5, ...): all copies write to ONE shared folder (the main checkout's Saved/PauseShadow/) so the data collects in one place. The real rule switch comes later, after calibration.
