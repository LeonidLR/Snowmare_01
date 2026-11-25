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
