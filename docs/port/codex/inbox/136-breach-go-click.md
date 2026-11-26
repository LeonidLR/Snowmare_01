# Codex 136 (Lead reviews) - door breach: second click on the door = go in (designer 2026-11-26)

Now: the squad can take the tactical stack at a door (DoorwayBreachGhostActor preview, 127 breach entry), but there is no clear way to order the entry.
Wanted (designer): while the squad stands stacked at a door, hovering the SAME door again shows a breach cursor icon (arrow + fist or similar);
clicking it starts the entry: the right-side operative clears the right corner, the left-side one clears the left corner, the centre one goes in.
Later (NOT now): a grenade thrown in before the entry.
- Reuse the existing breach slots / 127 cleared-corner holds; add only the "stacked -> hover door -> icon -> click -> enter" step.
- Hover icon = same hover / cursor system as other context clicks (ClickPreview). Do not break the first click (stack at the door).
- Pure rules (who takes which corner from the stack side, when the second click is allowed) -> unit tests. Smoke: DoorsSmoke or a breach smoke step
  that stacks, clicks again and checks the three end positions.
- Branch codex/136-breach-go-click from origin/main; build via machine_dispatch.py / build.ps1 -Remote worker; report docs/port/codex/outbox/136-breach-go-click.report.md;
  FINISH `136 FINISH | PASS/FAIL | <hash> | ...`. Queue: after 134, before G022. TypeSafe audit: local boundary audit only (designer 11-26).

Designer finding 2026-11-26 (later): the entry already works when he can click the FLOOR inside - i.e. only when the roof cutaway hid the roof.
When the roof does not disappear there is nothing inside to click, so the entry is impossible. So 136 = make the door itself the "go in" target
(no dependency on the roof cutaway), reusing the existing floor-click entry path. Also check why the roof sometimes does not hide while the squad
is stacked at that building's door (stacked at a door = treat the squad as "at the building" for the cutaway); report it, fix if small.

## 136 part B - door peek reveals the building (designer 2026-11-26, for enemies inside buildings later)
An operative who runs up to a door, opens it and looks inside (like a corner peek, through the open doorway) "lights up" that building:
its roof (and storeys above his) hide via the roof cutaway, showing the floor he stands on. Enemies inside are still revealed only by
line of sight / vision cone (fog of war), the cutaway just lets the player see the room layout. Reveal ends when nobody of the squad
watches through that doorway (or after a short hold, ~5 s). Reuse URoofCutawaySubsystem (same hook as the stacked-at-door finding).
Smoke step: open a door, peek -> that building's roof hidden, storey = his; walk away -> roof back.
