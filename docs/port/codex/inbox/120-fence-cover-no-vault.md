# Codex 120 - cover order at a low fence VAULTS instead of taking cover (designer, X6 sector fight 2026-11-25) - HIGH priority

Designer: "low covers like fences - you cannot press against them; the operatives jump over even when I clicked the silhouette of the
crouching operative at the wall. A cover order = hide behind / press to the fence, never vault. Vault only when (a) I hover the fence top
and the vault silhouette shows, or (b) I order 'cross to the other side as fast as possible'."
Your 103 (31a65744, "low cover no longer vaulted") fixed this for the case you tested; on X6 (L_Gen_Depot_1_X6, sector 1, recording
Saved/Demos/2026-11-25_L_Gen_Depot_1_X6_094811.jsonl - 17 Cover orders) it still happens. Find which path still vaults: the cover slot
on the FAR side of the fence chosen as nearest / reachable, the nav path crossing the fence via the mantle link to reach the slot, the
group / follower path (non-selected squad members), or a fence type 103 did not cover (generated X6 fences vs props).
Fix: a Cover order picks the slot on the operative's OWN side of a low obstacle (the side he approaches from), and a cover-order path
never uses a mantle / vault link to reach a low-cover slot; vault stays for the top-hover order and the fast-cross order (designer
decisions 11-17: top = walk-climb, face = cover, double = run-climb).
Prove: unit test of the side choice; a smoke on X6 fences (several fence types, approach from both sides, solo + squad followers):
0 vaults on cover orders, vault still works on the top order; CoverSmoke, LowCoverMoveLakeSmoke, MantleSmoke on the worker.
BUILD ON THE WORKER (Scripts/build.ps1 -Remote worker). Branch codex/120-fence-cover. Report + "120 FINISH | ..." in chat. Never push.
