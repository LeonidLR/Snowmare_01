# G024 - window climb order: commander runs around the building and climbs OUT through the ordered window (designer, X6 fight 2026-11-25)

Designer: he ordered the Commander to climb through a window from OUTSIDE; the Commander refused, ran around the building, got in
through another window and then climbed out from the INSIDE through the window originally ordered. Expected: approach the window from
the side he is on (the click side), climb in. Suspects: the window order's approach point / side chosen from the window's forward axis
instead of the operative's side; the "best side" picker prefers the reachable side by nav cost; the mantle link direction one-way.
Branch grok/G024-window-side from origin/main. Repro smoke on X6 (window of a sector building, operative outside 6 m away, order
climb-in -> FAIL if he enters any other opening or ends outside); fix; unit test for the side choice; WindowCoverFireSmoke,
CutawayClickSmoke still PASS. BUILD ONLY ON THE WORKER, long commands in the foreground, PROGRESS lines in chat.
Report docs/port/grok/outbox/G024-window-detour.report.md + "G024 FINISH | ...". Never push.
