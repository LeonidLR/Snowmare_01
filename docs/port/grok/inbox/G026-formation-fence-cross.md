# G026 - formation fast-cross over a fence stalls (designer 2026-11-25, X6) - HIGH, right after G024; check first whether your 120 caused it

Formation mode, squad next to a low fence, DOUBLE CLICK on the ground on the other side (fast cross):
- operative 1 vaults, then STOPS right after landing instead of continuing to the clicked point;
- operative 2 stops next to him (does not vault / does not reach his formation slot);
- operative 3, after several repeated orders, runs AROUND the fence instead of vaulting.
Expected: all three cross (vault queue, Formation.MantleQueue rules) and continue to their formation slots at the clicked point.
Suspects: your 120 changes (AOperativeAIController::FindPathForMoveRequest excludes Mantle / Vault areas while a LOW-cover order is pending
- is a stale pending cover left on followers after the double click? the vault-suppress hold while bInCover / pending low cover; the
move continuation after the mantle ends); the confined-entry / mantle queue in formation; follower partial paths now disallowed.
Do: repro smoke on X6 (3 operatives in formation 2 m from a sector fence, double click 8 m beyond it): PASS = all 3 within 1.5 m of their
slots within 20 s, each crossed by a vault (no detour > 1.5x the straight distance). Run it on main BEFORE 120 (ade54f85^1) and after, to
name the cause. Fix; keep 120's rule (a Cover order never vaults). MantleFenceRouteSmoke, FormationWallHugSmoke, LowCoverMoveLakeSmoke PASS.
BUILD ONLY ON THE WORKER, foreground, PROGRESS lines. Branch grok/G026-formation-fence from origin/main.
Report docs/port/grok/outbox/G026-formation-fence.report.md + "G026 FINISH | ...". Never push.

ADDED (designer 11-25, same fight): UNDER FIRE DURING A VAULT the operative who was almost over the fence is snapped BACK to the start of
the vault. Expected: once a vault / mantle has started it always completes to the far side (a hit may play an additive flinch, take damage,
even knock him down AFTER landing - never cancel the traversal and teleport him back). Find what cancels it (hit reaction / knockdown /
suppression / panic / cover-reaction order interrupting the mantle montage, root-motion / warp target reset, an abort that restores the
start transform). Smoke: operative vaulting an X6 fence takes 3 hits at 30 %, 60 %, 90 % of the clip -> ends on the far side, never
moved back toward the start by > 20 cm. Unit test for the "traversal is not interruptible" rule.

ADDED 2 (designer 11-25): FORMATION COVER AT A FENCE - squad in formation, crouch silhouette at the fence, double click: the first operative
runs to his slot and STANDS there waiting until the others arrive, then crouches. Expected: every operative crouches into his low-cover slot
the moment HE arrives (no waiting for the group; the group cover / wall-sector logic may still assign slots). Find the "wait for the
squad" gate (group cover arrival, formation hold, settle check). Smoke: 3 in formation, group cover order at an X6 fence -> each one's
stance is crouched within 0.5 s of his own arrival.
