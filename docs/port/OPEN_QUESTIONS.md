# Open user decisions / questions (moved from the old HANDOFF.md)

> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19). Index: [HANDOFF](HANDOFF.md) | [system docs](../systems/README.md) | [CHANGELOG](CHANGELOG.md)

## 7. Open user decisions / questions

- **Melee close-in: SHIPPED (user approval 2026-10-18)** - `Content/Data/AI/ai_tuning.json` "cvars" `"Codex.Enemy.MeleeCloseIn": "1"`
  (code default 0 kept for A/B: console / `-dpcvars=Codex.Enemy.MeleeCloseIn=0` / `-NoAITuning`). Open follow-ups from its A/B
  (2026-10-18): (1) ~~pack flanking lost its side attacks~~ - DONE (user decision 2026-10-18, §5 «Pack flanking»: a real detour to
  a new flank point beside the target on his outer side). (2) ~~return to cover after a knockdown~~ - DONE (user rule 2026-10-18, §5 «Return to cover
  after a knockdown»: a blast with nobody close -> back to the slot; surrounded -> stays out). (3) Balance: see §10
  for the numbers (bites x20 across the enemy smokes).

- **Demo 01 rev 5 (2026-10-13) - defaults chosen by the orchestrator, to confirm (beat sheet §12):** (1) the shaky bridge REFUSES a
  second operative at the edge (no fall / damage) - keep, or sway and dump both? (2) the low fence is MANUAL prone (a standing order
  stops with a hint) - keep, or auto-prone through it? (3) moves are refused while the HQ intro plays ("Hold on - HQ is talking.") -
  keep? (4) vision cones on `O`, a session setting (not saved) - OK / save it in the profile? (5) the pre-horde patrol (beat sheet
  §1.3 encounter groups): 2 frozen + 2 hounds, turn-based allowed, Panic off? (6) stances / vault / cover volumes move to the approach
  (`VOL_A3_*`), guard moves to the generator beat - OK? (7) precredit on by default for command cards except regroup / sprint / loot
  open / tripwire - OK?

- **Placed-enemy archetype (2026-10-12, user-approved fix):** an enemy placed in a level (not made by the wave spawner / a save
  load) whose `Archetype` field equals its class default now takes the archetype the game mode's `EnemyClasses` maps to its
  Blueprint (or a parent of it); a value changed on the instance wins; an unmapped class keeps its class default (C++
  `AMarksmanEnemyCharacter` = Marksman). One log line per placed enemy (`[Enemy] placed ... archetype ... from the ...`). To
  confirm: a designer who wants a different type on a BP_Enemy_X sets the field on the instance (as before).

- **APC P4 hull (2026-10-12) - choices made by Claude, to confirm:** (1) numbers: hull 1200 HP; small claws x0.1, Brute x1.0,
  spitter acid x0.5 (the spitter / cryo drone shoot the hull only when nobody is outside), marksman x0.25, blasts x1.0 (the
  blast's enemy damage at the hull side's distance), a mine / tripwire the APC sets off itself = flat 250, a fast Brute ram costs
  the hull 30; (2) enemies go for the APC only while it is **manned and running**, when it is the closest threat (weights: small
  1.0 like an operative, Brute 0.85 vs his 0.7 for operatives) and within 40 m (a horde always knows); an empty / disabled APC is
  ignored; (3) mines: level mines and squad mines with friendly fire; tripwires: hostile (PMC) ones like an operative; (4) the
  crew is ejected at once (also in the tactical pause; a fade in real time) on the side away from known threats, straight into
  hull cover, no damage, no knockdown; (5) repair: **Engineer only** (others refused, `OtherRepairSeconds` 0), no parts, 5 s at
  the hull, crouched, back to 50 %; Y with the Commander selected says "only the Engineer", Alt+Y picks him; refused in the
  pause / turn-based like other vehicle orders; (6) Ctrl+click object shot = **cannon only** (one shell, any posture), barrels /
  mines / trapped objects / trapped crates; an untrapped crate is ignored; (7) no "wheel damage -50 % speed" and no smoke at 25 %
  (design extras, not asked); (8) the damaged model is assumed to share the skeletal mesh's pivot (move `DamagedMesh` in the BP
  if it does not); headless the Cascade wreck smoke does not run (checked when rendering); (9) turn-based blasts do not reach the
  hull yet (the grid has its own blast code; P3).

- **APC P2 (2026-10-12) - choices made by Claude, to confirm:** (1) **run-over decision changed by the user**: small enemies
  (hound, frostbitten, cutter, spitter, marksman - the marksman treated as small as asked) die under the wheels at >= 1.5 m/s
  (credit = the driver); slower they are shoved aside without braking; (2) a **downed Brute is not run over** (the APC stops
  against him; `bCrushDownedRammed` / JSON `crush_downed_brute` switches it); (3) the ram deals 40 to the Brute and **no damage to
  the hull** yet (no hull health until P4; `HullRamDamage` 0 is kept for P4); (4) the Brute's knockdown clips play at an absolute
  0.65x (fall 1.4 s, get-up ~2.5 s) and he stays down 0.9 s (1.5 x 0.6 s); (5) the turret holds fire in **Passive** even when
  attacked (as operatives), Ctrl+click always fires; the cannon is used for a Brute / marksman / a group of 3 / a priority target,
  an overheated MG makes small targets wait (shells are kept for big threats); (6) ~~MG suppression stagger~~ **removed (user
  decision 2026-10-12): the MG just deals damage** (damage / rate / heat unchanged); (7) the turret also fires in exploration (no
  phase gate; a turret hit starts an ambush like any hit); (8) a Ctrl+click on a barrel / object is ignored (enemies only for
  now); (9) no sounds: the project has no weapon sounds yet (BP event On Turret Fired; TANDEM request 20); muzzle flash = the
  vendor Cascade emitter, tracers = the operatives' tracers; (10) enemy AI does not dodge the moving APC (small enemies are run over
  by design); while parked the hull is carved out of the infantry navmesh as before.

- **APC P1 (2026-10-12) - choices made by Claude inside the approved design, to confirm:** (1) the tactical pause refuses vehicle
  orders (board / get out / drive) with a feed line instead of planning them (the task allowed "planned if simple"); (2) turn-based
  entry is refused while anyone is aboard (the grid has no vehicle seats until P3); (3) Alt + Y with the squad partly aboard boards
  the rest ("all aboard"); only with everyone aboard does it empty the vehicle; (4) the vehicle navmesh is opt-in per bounds volume
  (tag `VehicleNav`); (5) the engine runs while anyone is aboard (no 10 s auto-off yet) and then heats a 3.5 m zone round the hull
  centre, which also warms operatives hugging the hull; (6) the path preview ribbon (red where it won't fit) is not built: a drive
  that can't reach the click goes as close as it can, the driver says so and `VehiclePathBlocked` is raised; (7) the vehicle agent's
  slope limit is the infantry's 44 deg (a per-agent 25 deg needs a RecastNavMesh subclass - say if wanted).

- **Real time vs turn-based damage numbers (2026-10-11, table `docs/port/tb_parity_2026-10-11.md`).** The double cuts are fixed;
  still different per mode (all inherited from Godot): crouch defence RT 0.75 vs TB 0.70; barrel on enemies RT 120 x falloff vs
  TB 80; mine RT 120 (squad 90) x falloff vs TB 50; TB blasts keep Godot's luck dodge, RT blasts never dodge. Choose per row:
  keep / real-time value / turn-based value.

- **Blast gate tied to the generator (2026-10-11), to confirm with the user:** (1) speed: 0.4 %/s of wave fight with the generator
  up, capped per wave at 30 / 60 / 90 % (a wave's share fills in ~75 s), full only after wave 3; (2) «Start Battle» keeps opening
  the gate at once because it places the squad behind the gate (`CombatStart`) while the waves spawn near the test start — say if
  «Start Battle» should place the squad in front of the shut gate instead; (3) the Wave Editor has no UI for the `gate_*` keys yet
  (only the TS types / schema).
- **Sprint 15 heat - design gaps decided by Claude (2026-10-11), waiting for the user's confirmation** (values in tactical_heat.json):
  (1) heat pack "-35 %" = 35 points of the 0..100 cold scale (like the food's "25 % cold off"), frostbite cleared -> the operative gets
  up to a crouch; a pack dragged onto a COLD mate is used on him, onto a warm one it is handed over; (2) items: starting loadout (above) +
  every crate 1 heat pack + 1 firewood; flares only per crate / loadout; the stove only in the loadout (no gas canister item yet);
  (3) keys: U = heat pack, Shift + G = flare (and the selector's 5th line), F / drawer = stove and campfire (F keeps the Godot priority of
  turrets / barricades / mines; heat gear when the squad has none, or by cycling F while placing); (4) crafting = the campfire's set-up
  spends 1 match + 1 firewood (else 10 flamethrower fuel; no crafting UI); new item `Firewood`; the "Fuel" of the spec = flamethrower
  fuel; (5) campfire noise = patrols within 15 m start a search (no ambush / fight start); (6) wind = the cold system's elevation rule
  (fire base Z >= 140); (7) the beasts' fear of the flare = the existing fire fear of every active heat source (hounds / cutters /
  frostbitten flee while inside 6 m) instead of a 2 s hesitation — it also applies to the campfire, stove and puddle; (8) not in
  turn-based: throwing / lighting a flare; deploying the stove / campfire costs 2 / 3 AP there; (9) an exploded barrel leaves a puddle
  (its core damages operatives too); (10) no night / darkness system exists: the flare's "night accuracy" part has no hook.

- **Sniper rifle vs move orders (user decision 2026-10-09, confirmed):** the fire posture's automatic fire never interrupts a
  player's move order — the Medic-Sapper does not shoot while walking and fires only once she has stopped (kneeling first);
  direct fire orders (priority target, Ctrl + click, planned / turn-based shot) do stop her (`SniperRules::NextStep`).

- **Enemy AI direction (user decisions 2026-10-04):** bot VETERAN target win rate 40-75 % (the Jev coach's band);
  turn-based ranged enemies (spitter, marksman) shoot at range with line of fire and a hit roll (deliberate departure
  from Godot, where every turn-based enemy only bites); per-archetype morale (fast ones fall back to the pack and come
  again, brutes / frostbitten never fall back, marksmen relocate); real-time enemy tactics first, then turn-based on
  the same shared rules. Enemy *power* knobs (damage, accuracy, ...) stay proposals for the user; enemy *intelligence*
  knobs may be tuned by the coach. Marksman kiting limit (cooldown 10 s, one 3.5 s dash) = user decision 2026-10-04.
- Jev coach finding 2026-10-04 (user's current waves, VETERAN, 8 runs per batch): 0-1 / 8 victories whatever the bot
  knobs; Jev fairness ~1 / 4 every iteration, enemy issue «marksman too accurate / too evasive» — wave 3 (20 hounds,
  brute, 6 cutters, 4 marksmen) is the user's to rebalance; the coach only proposes enemy power changes.
- 2026-10-04 bot balance batch (25 runs, level_01_outpost / stage_01 data): NORMAL 2/10, VETERAN 4/10, CASUAL 1/5 victories; 16 of 19 defeats are FREEZING_FATIGUE, almost all in wave 3 (squad cold at the end 72-97 %), the medic freezes most often. Open for the user: tune the cold (e.g. wave 3 cold_drain_mult in the Wave Editor / cold rates in DA_GameBalanceConfig) or keep it.
- Spawn lanes: resolved 2026-10-04 (Sprint 05-C aliases).

- L_MovementTest re-laid by the user (rotated 180°, moved): kept (user decision 2026-09-29); smokes are layout-relative.
- `Config/DefaultEditor.ini` has local editor changes — never commit it unless asked.
- M16 clip (user decision 2026-09-29): 30 for every operative like the reference video. The current Godot scene sets
  reload_after_shots 10 (commander) / 15 (engineer) — deliberately not ported.
- Level waves (user decision 2026-09-29): like the Godot code — `main.gd _spawn_custom_json_wave` spawns the whole wave
  at once; `spawn_delay_sec`, `initial_delay_sec`, `max_simultaneous_enemies` (described in DATA_CONTRACTS.md) are
  ignored. Wave modifiers apply (hp / damage / speed per enemy, `cold_drain_mult` → operatives' cold outside camera
  zones, kept after the wave like Godot's cold_rate_modifier), `custom_stats.health` × hp_mult.
- L_MovementTest (user, 2026-09-30): PlayerStart moved ~170 m south of the gate; the four EnemySpawn_* points were moved
  next to it on the user's request (same 20-28 m spread, north of the start — the south wall is 9 m behind it). Smokes
  anchor the layout on the checkpoint gate (`SmokeUtils::LayoutTransform`) since the floor was stretched.
- Monster models: the user imports them personally; the enemy BPs / AnimBPs are generated from them by
  setup_enemy_animation.py (user request 2026-09-30). Spitter / cryo drone have no art yet (C++ placeholder body).
- Start menu (user decision 2026-09-29): only «Начать игру» (exploration → combat) and «Начать бой» (preparation);
  Godot's third mode «Начать исследование» is removed from UE (menu button, `EMissionStartMode::Exploration`, texts).
- «Начать бой» position: Godot hard-codes the yard behind the gate; UE uses the `CombatStart` tag (test map: (0, −2150)).
- Turn-based deviations: an operative that survives a mine ends its turn once (Godot schedules end_current_unit_turn
  twice — from the move and from the blast — which skips the next operative; treated as a bug). Squad starts facing the
  enemies' centroid (Godot hard-codes SOUTH = towards the enemies on its map). The attack line names the arc
  (фронт / фланг / тыл; Godot prints the enum number). Trapped-barricade retaliation: Godot _damage_barricade checks `is_mined`, which
  nothing ever sets — dead code, not ported. Enemy idle variations: with the AnimBP.
- Stage loadout (Godot _apply_stage_exploration_resources, `LoadoutRules`): when the cutscene ends, «Начать игру» keeps
  what the squad collected (turrets to the commander ≤ 2, barricades to the engineer ≤ 4, mines to the medic-sapper ≤ 5);
  «Начать бой» turns EXPLORE_AND_COLLECT into STARTING_UNIQUE (at least 1 / 2 / 2; level_01_outpost already says
  STARTING_UNIQUE); EDITOR_PRESET tiers from the level JSON. Godot quirk kept: the specialist gets the squad total
  while the others keep theirs (items double up) — kept as in Godot (user decision 2026-09-30). The --bot-loadout CLI override is not ported.
- Turn-based entry on flat ground only (user decision 2026-09-30; Godot allows platforms): the controller samples the floor
  under the future grid (7 x 7 over 21 m, `CombatQueries::SampleGroundHeights`) and refuses when a living operative's feet
  are more than 50 cm from the median («⚠️ Пошаговый бой можно начать только на ровной поверхности…»). A plateau wider
  than the grid counts as flat. Smokes call `RequestEnterTurnBased` directly and are not affected.
- `FString::ToLower` / `Contains(IgnoreCase)` do not fold Cyrillic: use `FText::ToLower` (see DialogueRules).
- The world is paused while the start menu is open (Godot keeps processing behind its menu) — cosmetic difference.

