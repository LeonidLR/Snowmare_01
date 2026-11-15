# 015 - READ-ONLY scout: every fire path that shoots before the body faces the target (board 058c reopened + 061)
Designer (2026-11-15 evening, screenshot: Commander, CROUCHED, BADLY WOUNDED next to a cube): shooting sideways from a corner is still
not fixed - the operative looks at the wall while the shots fly at the enemy. And 061: when an enemy approaches, operatives shoot first
and turn afterwards. Wanted everywhere: turn -> raise the weapon -> only then fire.
Recent fixes to know: 9441d902 (058 suppress-from-cover facing), 0c73fdb6 (058c FireArcRules::BodyArcResidualDeg, CVar
Codex.Fire.BodyArcDeg = 30: a still shooter fires only once the body faces the target), 0175ae00 (058b autofire from cover).
NO code changes. Branch codex/015-fire-facing-scout only for the report commit.

TOKEN BUDGET: light-medium. Use grep to list call sites (FireWeapon / TryFire / FireShot / Suppress - find the real names), read only
~30 lines around each hit; do not read whole files. If a question needs deep reading, write "needs deeper look: <where>" and move on -
Claude finishes it.
Answer with file:line for each:
1. Every code path that fires a shot for an operative: auto-fire, quick fire, suppress (Shift-click), from cover / corner peek, prone,
   crouched, wounded, PMC operatives. For each: which facing / weapon-raised check it passes through before the shot (FireArcRules?
   another gate? none?).
2. The corner case from the screenshot: crouched + badly wounded + corner/cover - which branch fires, and why the 058c body-arc gate does
   not stop it (stance / wounded / cover state skipping the gate? aim offset vs body yaw? the corner-peek pose not counted as "still"?).
3. The weapon-raise state: where "weapon raised / ready" is known (anim notify, timer, ready-time), and whether any fire path waits for it.
4. A proposed single gate ("barrel on target AND weapon raised") - where it should live so every path from 1 goes through it, and the
   list of smokes that cover each path (existing names from test_map.json) + which path has no smoke.
Report: docs/port/codex/outbox/015-fire-facing-scout.report.md (English identifiers, Russian prose ok). Claude writes the fix from it.
