# System docs (split from HANDOFF.md, 2026-10-19)

One file per system. [docs/port/HANDOFF.md](../port/HANDOFF.md) is the short index; the per-system detail that used to be HANDOFF §5 / §6 lives here.
Every doc starts with a short summary and key files, followed by the old text moved verbatim (no fact was dropped).

| Doc | What is in it |
|---|---|
| [squad-and-orders](squad-and-orders.md) | GameMode squad spawn, input (PlayerController), orders / who owns an operative's movement, formation, fire postures, per-member Blueprints. |
| [stances-and-movement](stances-and-movement.md) | AOperativeCharacter overview: stances / capsule per stance, sprint, vault, carrying, inventory limits, source layout of the 16 cluster files. |
| [cover-and-peek](cover-and-peek.md) | Cover slots / enter / leave / shimmy, followers leaving cover, the group cover order (others beside / prone behind), corner peek that reveals traps (physically, one by one), return to cover after a knockdown. |
| [combat-and-fire](combat-and-fire.md) | Targeted shots, quick fire (Ctrl + click), fire postures / ROE, combat feedback, knockdown values, M16 range, turn-based combat manager and grid. |
| [sight-and-vision-cones](sight-and-vision-cones.md) | Squad sight (symmetric, also while exploring), peek sight from the leaned-out eye, occluded vision cones, how to switch cones on. |
| [enemy-ai-and-perception](enemy-ai-and-perception.md) | Enemy registry (no actor iterators in hot paths), pack flanking detour; parity notes for enemy AI live in KNOWN_GAPS. |
| [stealth-lure-and-scent](stealth-lure-and-scent.md) | Whistle / stone lures, enemy investigation and re-target rules, hearing round walls, hound scent (sniff, follow, detect), tutorial locks. |
| [survival-cold-heat](survival-cold-heat.md) | Cold accumulation / tiers, mobile and tactical heat (campfire, stove, flares, heat packs, fire puddle), weather idle poses and the squad gathering at the fire. |
| [interactables-traps-loot](interactables-traps-loot.md) | Interaction menus, barrels, relocation, deployables (barricade / mine / turret), loot crates, generator, quest chain and gate, spotted-trap Solo, PMC tripwire how-to. |
| [waves-horde-encounters](waves-horde-encounters.md) | Phases (Exploration -> Preparation -> WaveCombat -> ...), sudden horde start, horde intro flyover, level enemies / guard posts / ambush start, wave data. |
| [tutorial-engine](tutorial-engine.md) | **The steps JSON schema reference** (every key, default, precedence, save behaviour, examples), command choke point and locks, state machine / overlay, demo flags, how to place volumes / markers / steps; revision history below it. |
| [demo01-lake](demo01-lake.md) | Per-revision decision blocks of the Demo 01 tutorial level (rev 4 .. rev 5i, 2026-10-19), what the designer places in the lake map, lessons (posture, medkit, A4 ambush). |
| [save-load](save-load.md) | JSON save slots (format v3), save policy (outside combat / dialogue), load = reopen the level, the 11-step restore order, the state inventory (saved / derived / gaps), how to add saved state, robustness (corrupt files, missing map). |
| [camera-hud-minimap](camera-hud-minimap.md) | Tactical camera and camera zones, roof cutaway, building exits, minimap / full map, HUD, phase banners, action bar. |
| [dialogue-frontend-mission](dialogue-frontend-mission.md) | Dialogue sequences, CommonUI frontend (title, main menu, pause menu, save slots), mission start / mission rules, narrative pipeline. |
| [vehicles](vehicles.md) | Kinematic nav-driven APC: boarding / seats, driving, cover, turret and cannon, hull damage / repair, run-over decisions, how to place it and make L_VehicleTest. |
| [animation-setup](animation-setup.md) | AnimInstance, left-hand IK and aim offsets (what the user adds to the graph), grenade / knockdown / hit / sniper clips, interaction clips, art (operatives, enemies). |
| [tools-bot-waveeditor](tools-bot-waveeditor.md) | Editor python scripts, the playtest bot, the Wave Editor and Jev (TypeSafe) tooling - pointers into the code and the change log. |
| [build-test-smokes](build-test-smokes.md) | Unreal MCP, build / test / verify commands, verification tiers, fixed-step smokes, and the full table of every smoke command with what it checks. |

## Where did the old HANDOFF sections go?

Old references like `HANDOFF §9` or `HANDOFF §6 «ABP: aim offset»` (in code comments, TANDEM, briefs) resolve with this table.

| Old HANDOFF.md | Now |
|---|---|
| §1 Read first | [HANDOFF.md](../port/HANDOFF.md) §1 |
| §2 Environment facts and traps | [TRAPS.md](../port/TRAPS.md) (full table), condensed list in [HANDOFF.md](../port/HANDOFF.md) §2 |
| §3 Build, test, verify (+ the smoke table) | [build-test-smokes.md](build-test-smokes.md) |
| §4 Conventions | [HANDOFF.md](../port/HANDOFF.md) §4 |
| §5 System map | this folder, by system (table above) |
| §6 «Tutorial: how the user places volumes ...» | [tutorial-engine.md](tutorial-engine.md) (how-to) and [demo01-lake.md](demo01-lake.md) (rev blocks) |
| §6 «APC: how the user places it ...» | [vehicles.md](vehicles.md) |
| §6 «How to place a PMC tripwire» | [interactables-traps-loot.md](interactables-traps-loot.md) |
| §6 «ABP: left-hand IK / aim offset / grenade / knockdown / hit reactions / Female Soldier + sniper», «Interaction clips» | [animation-setup.md](animation-setup.md) |
| §6 «Weather idle poses» | [survival-cold-heat.md](survival-cold-heat.md) |
| §6 «Frontend: what the artist edits», «Narrative pipeline» | [dialogue-frontend-mission.md](dialogue-frontend-mission.md) |
| §6 «Save system» | [save-load.md](save-load.md) |
| §6 data blocks (tactical heat, vision cones, M16 range, sudden horde, per-member BP, GameMode squad) | [survival-cold-heat](survival-cold-heat.md), [sight-and-vision-cones](sight-and-vision-cones.md), [combat-and-fire](combat-and-fire.md), [waves-horde-encounters](waves-horde-encounters.md), [squad-and-orders](squad-and-orders.md) |
| §7 Open user decisions / questions | [OPEN_QUESTIONS.md](../port/OPEN_QUESTIONS.md) (condensed in [HANDOFF.md](../port/HANDOFF.md) §7) |
| §8 Next steps | [NEXT_STEPS.md](../port/NEXT_STEPS.md) |
| §9 Known gaps / tech debt | [KNOWN_GAPS.md](../port/KNOWN_GAPS.md) |
| §10 Change log | [CHANGELOG.md](../port/CHANGELOG.md) |

## Old §5 / §6 section headers and the intro of the system map (verbatim)

## 5. System map (UE → Godot)

Module `CodexTactics` (runtime). Folder → class → Godot reference. The dev console commands (smokes, probes, shot commands, `UEventBusProbe`) are
in the separate module `CodexTacticsDev` (`Source/CodexTacticsDev/Private/Debug/`, Type DeveloperTool: editor + Debug / Development
game targets, never Test / Shipping; cleanup 2026-10-16, audit #3).

## 6. Data / content status

