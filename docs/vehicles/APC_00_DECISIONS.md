# APC (BTR-82) — user decisions 2026-10-12 ("all as recommended")

1. Board / disembark: **Y** (selected operatives), **Alt+Y** (whole squad) + a "Board" entry in the APC click menu; clicking the APC still = take cover at its hull.
2. Seats auto-filled: **Engineer drives, Commander mans the turret**; anyone else if they are absent.
3. Demo 01 Frozen Lake: **no driving**; the stranded PMC APC stays the diesel source for the siphon quest. Driving is taught on a later map (or an optional A12 "drive out").
4. Fuel: **off by default**, a per-level option.
5. No running over enemies; small enemies deal x0.1 to the hull; Brutes and mines are the threat.
6. At 0 HP: **disabled, repairable** by the Engineer; the crew is ejected.
7. Turn-based first version: **parked APC, turret fires for AP**; driving on the grid later if needed.
Approach: **kinematic vehicle pawn on a separate Vehicle nav agent** (no Chaos physics), visual = a reparented copy of the vendor BP. Phases P0–P5 per APC_01_TECHNICAL.md; start with P0 + P1 on a test map `L_VehicleTest`.