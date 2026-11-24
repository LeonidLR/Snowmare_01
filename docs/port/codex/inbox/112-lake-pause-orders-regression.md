# Codex 112-lake-pause-orders-regression (after 110)

LakePauseOrdersSmoke (/Game/Maps/L_Demo01_A_FrozenLake) FAILS on main: "Smoke FAIL: Medic-Sapper runs the planned pause order"
(log Saved/Logs/Worker/20261124-195825/Smoke-LakePauseOrdersSmoke-alone.log). Gemini 111 bisect on the worker: last good addfe48bc
(PASS 62.5 s), first bad 386add9b7 "feat(formation): confined entry order - depth queue from the doorway" (2026-11-22,
SquadSubsystemFormationConfined.cpp + FormationConfinedRules.*). Likely the confined-entry queue takes over / holds a member that has
its own planned pause order. Find the exact cause, fix so a member's own planned (pause) order wins over the confined queue, without
breaking the confined entry (FormationConfined* tests + the formation stair / window smokes on the worker). Add the failing case as a
unit test. Branch codex/112-lake-pause-orders.
