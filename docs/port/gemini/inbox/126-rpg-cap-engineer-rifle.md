# Gemini 126 (Hydrogen; Carbon allowed for the final check) - RPG max 2 rockets per round + Engineer gets an M16 (designer 2026-11-25) - right after 122

Branch gemini/126-rpg-engineer, worktree C:/Work/Lanes/CodexTactics-126. BUILD ONLY ON THE WORKER. Stage files by path.
1. RPG CAP (both sides - player operatives AND PMC): designer saw one of his operatives (after respawn / weapon switch to the RPG) kill
   enemies "one, two" with rocket after rocket. Rule: an RPG carrier has at most 2 rockets per ROUND in total (1 loaded + 1 spare);
   NO resupply for launchers in sector ammo_mode "resupply" (check G019 ResupplyAmmo / round-start reset: a launcher must not be refilled
   mid-round, and the round-start reset gives exactly the cap); a respawn inside a round must not hand out new rockets; picking up / weapon
   switching must not create rockets. Data field launcher_rockets_per_round = 2 (sectors.json top level or the weapon data - say where).
   Find every path that sets launcher ammo (loadouts, pmc_operators.json, level loadouts, resupply, respawn, arena) and cap it.
   Unit tests for the rule; a smoke: RPG carrier fires 2, the 3rd attempt is refused / he switches weapon; after 60 s still 0 rockets.
2. ENGINEER SECOND WEAPON: the Engineer has only the D-1 shotgun and "stands aside" in long-range fights. Give him an M16 (the existing
   operative rifle, same tuning as the other operatives' rifle) as a second weapon. Auto choice: shotgun when the target is <= 12 m
   (data shotgun_switch_range_m), rifle beyond, with hysteresis (switch back only past 15 m) so he does not flip every frame; the player can
   still force a weapon. Loadout data in the existing loadout files (no new weapon asset - reuse the rifle; if a mesh / attach is needed and
   only a .uasset change can do it, STOP and report). Unit test for the switch rule; ShotgunSmoke + a smoke "engineer fires the rifle at
   30 m, the shotgun at 8 m".
Weapons roster rule: rifle / shotgun / pistol / sniper / RPG only (designer list) - no other weapons.
Report docs/port/gemini/outbox/126-rpg-engineer.report.md + "126 FINISH | ..." in chat, PROGRESS lines while working. Never push.
