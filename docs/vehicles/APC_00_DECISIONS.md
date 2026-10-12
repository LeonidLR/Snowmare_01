# APC (BTR-82) — user decisions 2026-10-12 ("all as recommended")

1. Board / disembark: **Y** (selected operatives), **Alt+Y** (whole squad) + a "Board" entry in the APC click menu; clicking the APC still = take cover at its hull.
2. Seats auto-filled: **Engineer drives, Commander mans the turret**; anyone else if they are absent.
3. Demo 01 Frozen Lake: **no driving**; the stranded PMC APC stays the diesel source for the siphon quest. Driving is taught on a later map (or an optional A12 "drive out").
4. Fuel: **off by default**, a per-level option.
5. ~~No running over enemies~~ **Changed 2026-10-12 (user):** while driving, SMALL enemies (hound, frostbitten, cutter, spitter, marksman) in the hull path are run over at >= 1.5 m/s (a proper kill, credit to the driver, no braking, the APC slows slightly); slower they are shoved aside. The BRUTE is never crushed: hit at >= 4 m/s he is knocked down with slower, heavier clips (0.65x, downed 1.5x, small ram damage) and the APC loses its speed and stops against him; slower it just stops. Operatives / recruits / NPCs: always braked for. Numbers: `weapons_tuning.json` "vehicle_weapons.apc_ram". Small enemies deal x0.1 to the hull; Brutes and mines are the threat (P4).
6. At 0 HP: **disabled, repairable** by the Engineer; the crew is ejected.
7. Turn-based first version: **parked APC, turret fires for AP**; driving on the grid later if needed.
Approach: **kinematic vehicle pawn on a separate Vehicle nav agent** (no Chaos physics), visual = a reparented copy of the vendor BP. Phases P0–P5 per APC_01_TECHNICAL.md; start with P0 + P1 on a test map `L_VehicleTest`.

## Additions 2026-10-12 (user, after P1)

- Kept as is: vehicle orders refused in the tactical pause; turn-based entry refused while anyone is aboard (until P3); Alt+Y boards the rest when partly aboard; no red path ribbon / no click-menu Board entry / no engine auto-off; slope limit 44 deg.
- **No boarding animations:** every board / get-out is covered by a quick fade to black and back (0.25 s out, hold while they are moved, 0.25 s in; one fade per group; never in the pause / turn-based; watchdog against a stuck black).
- **Turret (P2, real time):** gunner seat (Commander, else anyone aboard); 30 mm cannon (shells per map) + coaxial MG (overheats); fires by the gunner's fire posture and on Ctrl+click; dead zone 4 m round the hull.
- **Wheels:** the wheel spin follows the hull's real ground speed (no whirl / slip at the start); the acceleration ramps up when pulling away.
- **Turret MG (2026-10-12, user):** the coaxial MG just deals damage - **no "suppression stagger"** (the P2 0.5 s stagger after 5 hits in a row is removed); damage, rate and heat are unchanged.
