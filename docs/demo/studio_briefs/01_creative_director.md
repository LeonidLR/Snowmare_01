# 01 — Creative Director: Tester Demo #1 «Sector-17 Perimeter»

Sources read: UE `CLAUDE.md`, `docs/port/HANDOFF.md` (§6–§8), `PORT_MATRIX.md`, `TANDEM.md`, `REFERENCE_PLAYTHROUGH_01.md`; Godot `design/gdd/game-concept.md`, `docs/GAME_DESIGN_SPEC.md`, `design/levels/stage-01-bunker-gate.md`, `design/art/art-bible.md`; `narrative_manifest.json` (5 sequences), `Content/Data/LevelJson/*`. Facts that shape this brief: no demo map exists yet (only `L_MainMenu`, `L_MovementTest`, `L_PatrolTest`); there is no tutorial / hint layer and no checkpoint autosave (Godot's autosave was never ported); losing the Engineer or the Medic-Sapper is permanent and only the Commander's death fails the mission; the 2026-10-04 bot batch lost 16 of 19 runs to freezing in wave 3.

## 1. Vision and pillars

**Vision.** One continuous level of about 30–35 minutes, built on the approved Stage 01 «Bunker Gate» layout: a three-person polar squad lands at the dead perimeter of Sector-17 in a rising storm. They have to get power to the gate and hold it against the horde that the siren wakes. The level is the course: each zone teaches **one new verb** through a small, low-risk situation, and the gate finale asks for all of them together. The tester should finish thinking "I understand how this squad survives the cold". They should not finish thinking "I sat through a tutorial". The game's signature is real time that opens into a Gorky 17 turn-based grid when you hold Space, and the demo has to make that its unforgettable moment.

**Demo pillars (what the tester must feel / learn).**
1. **Cold is the second enemy.** The cold bar is always draining, and heat (a barrel lit with matches, a heat pack, the generator's warm zone) is the resource you plan around. *Learn: read the cold bar and move between warm places.*
2. **Stop time, think, command.** When it goes wrong you tap Space for a tactical pause, or hold it for the turn-based grid. Planning beats reflexes. *Learn: tap = pause and orders, hold = Gorky grid with AP, hit chance and Tab.*
3. **Three specialists, one squad.** The Commander leads, and his death ends the mission. The Engineer builds (barricades, a turret). The Medic-Sapper defuses mines and shoots the sniper rifle from a kneel. *Learn: switch members (1–3) and use the right person for the job.*

## 2. What "educational" means here

- **Teach by doing, with a light explicit layer.** Every lesson is a small situation the player solves: a mine pops out of the snow, a patrol walks past, the group is too big for real time. Explicit help is a **one-line key hint** (e.g. `[Space] tap: tactical pause`). It appears when the verb first becomes useful, never blocks play, and disappears once the player does the action. No modal screens and no text walls. A "Field Manual" page in the pause menu lists every hint already seen.
- **Tone: in fiction first.** The *why* comes from the squad and HQ radio over the existing feed (speakers Commander / Engineer / Medic-Sapper / HQ). It should be dry and grim with sardonic banter, in the voice of the existing intro (Sokolova, the General's "no trophy hunting"). Only the *which key* sits in the UI hint. HQ never sounds like a teacher. It gives orders and warnings, for example: "Captain, the gate motor will be loud. Dig in before you power it."
- **Failure tolerance.** The first half (beats 1–4) is forgiving. Cold slows the squad and lowers accuracy but cannot kill there, and the first enemy contacts are small. Each beat ends at a **checkpoint**, and a defeat goes back to that checkpoint, not to the mission start (Ctrl+X stays as a full restart). The defeat screen keeps the "cause + tip" format, and its tip names the verb the player missed. Target: Act 1 win rate of 85–90% for the finale (spec), checked with the bot.
- **Gorky 17 tribute and mood.** The turn-based grid is the tribute, so it gets a staged reveal: time freezes, the grid draws in, the HQ line drops away to silence. Sector-17 is a Soviet-era secret enclave, and the demo should end with a mystery hook rather than a full explanation. Colour script per the art bible: cold steel and arctic blue everywhere, with hazard amber only where heat or safety is, so warm light also works as the navigation language.

## 3. Scope cut for demo #1

| IN (taught) | IN (present, not taught) | WAIT (demo #2+) and why |
|---|---|---|
| WASD/zoom/Q-E camera, click/double-click move, squad select 1–3, stances C/Z/X | Fire postures `, . /` (one hint only) | **Panic / Rage:** a third untaught meter muddies tester feedback |
| Cold bar plus barrel fire with matches, heat pack (U), generator warm zone | Loot crates plus the inventory drawer | **Campfire crafting, stove, flare, fire puddles:** five heat verbs at once is too many; keep one for demo #2 |
| Mine reveal plus Medic-Sapper defuse, one trapped crate | Drag-and-drop hand-over (works, no lesson) | **Marksman:** the bot finds him too accurate and evasive; one ranged threat too early |
| One stealth patrol: vision cones ON, guard post, ambush on contact | Knockdown (via the Brute) | **Cutter, Spitter, cryo drone:** the Spitter has no art; the Cutter adds noise |
| Cover against a wall, real-time fire, Ctrl+click target, grenade (G) | Engineer turret powered by the generator | **Susanin recruit, golden safe puzzle, stash:** side content; good for "secrets" later |
| **Tactical pause plus the turn-based grid (forced once)** | Save/load, CommonUI frontend | **Base and level-up phase (5):** perks and crafting do not exist; show the profile/EXP screen only |
| Generator quest chain (canister → APC → generator → terminal) | Death cinematic | **Mid-fight 4-minute horde (`UHordeSubsystem`)** and wave 3 of `level_01_outpost` (freezing defeats) |
| Barricade relocation plus a turret before the gate (prep is player-paced) | | **Stages 2–12 data, `L_PatrolTest`:** the demo is one level; other content waits for demo #2 |
| Sudden horde with flyover: 2 waves of Frostbitten + Hound, then the Brute | | |

**New work needed (small, all in Claude's area except the map):** the hint / objective-step layer, checkpoints, a demo end card with a feedback prompt, a per-beat telemetry stopwatch (where testers get stuck), and a demo map built by the user (greybox is acceptable).

## 4. Emotional arc (~32 min)

| # | Beat | Feeling | Teaches | ~min |
|---|---|---|---|---|
| 0 | **Cold open:** wrecked snowcat, a dead guard's radio, trimmed intro (≤5 lines) | isolation, unease | — | 2 |
| 1 | **The trench:** walk, camera, sprint; the cold bar starts falling; a match lights a barrel and brings relief | vulnerability, then first warmth | move, camera, cold, barrel fire | 3 |
| 2 | **Under the snow:** a mine surfaces; Sokolova: "Nobody move." Defuse it, open the trapped crate, loot | careful focus, role pride | select 1–3, defuse, loot, heat pack | 4 |
| 3 | **Silent patrol:** a lit yard, two Frostbitten on a route plus one guard post, vision cones visible | held breath | crouch/prone, cones, cover, ambush, Ctrl+click | 5 |
| 4 | **Stop the clock:** a pack bursts from a hangar; HQ: "Too many, Captain, take a breath." Tap Space, then hold Space for the grid | panic turning into control, the **tribute moment** | tactical pause, turn-based AP / Tab / hit chance / grenade | 6 |
| 5 | **Power:** canister → APC diesel → generator; an amber warm zone; the Engineer's barricades and turret come online | relief, then the calm before the storm (player-paced prep) | quest chain, barricade relocation, turret, warm zone | 6 |
| 6 | **CLIMAX, the gate (≈ 70–90% of runtime):** the terminal powers the gate and the siren howls; flyover over the horde; free tactical pause; wave 1 Frostbitten + Hounds, short rest, wave 2 adds the **Brute** (knockdown) | adrenaline, everything learned at once | synthesis | 5–6 |
| 7 | **Airlock:** the gate grinds open, warmth resets the cold, victory and stats, a short EXP/profile glimpse; HQ line plus the Sector-17 hook; "Thank you, tester" card with feedback | exhausted relief, curiosity | — | 2 |

## 5. Creative risks and open questions for the user

1. **What language will testers play in?** All in-game text is English, and Russian is planned for later. A tutorial fails if hints are half understood. *Recommendation:* if the testers are Russian-speaking, bring the Russian pass forward **for demo strings only** (hints, HQ radio, ~5 sequences); otherwise ship English.
2. **Should turn-based mode be forced once?** *Recommendation:* yes. Beat 4 is a scripted encounter that gates progress until the player holds Space once, on guaranteed flat ground. After that it is always optional. It is the Gorky identity, and testers must not miss it.
3. **What happens to a lost Engineer or Medic-Sapper during the lessons?** Losing her in beat 2 or 3 breaks later lessons (defusal, sniper). *Recommendation:* in beats 1–5 a "lethal" hit leaves the specialist **down and revived at the checkpoint**. Permanent loss and the Commander-only defeat apply only in the finale.
4. **Should Panic/Rage be in demo #1?** *Recommendation:* off for demo #1 (design pillar 3, but not taught). Turn it on in demo #2 with its own lesson.
5. **Which map?** *Recommendation:* a new dedicated map (e.g. `L_Demo01`, built by you, greybox and placeholder meshes acceptable) following the Stage 01 layout. `L_MovementTest` stays the test bed and must not be reused for the demo.
6. **Finale difficulty.** The current waves lose to cold, and the bot wins 1–4 runs in 10. *Recommendation:* 2 waves only (Frostbitten + Hound, then + Brute). The generator warm zone should cover the defence line, and wave `cold_drain_mult` should be at most 1.0 (no extra drain). Tune with the bot to an 85–90% win rate before testers play.

Выполнено моделью: Claude Opus 5.5 (claude-opus-5-5)
