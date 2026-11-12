# 008 - Stealth v2: full 8-direction locomotion with a locked look, Fast stealth run, a real corner lean (designer 2026-11-12)

Codex, on interim/2026-11-12, branch `codex/stealth-v2`. After 004 (hitch), before 005 (ragdoll) - the designer is play-testing
stealth now. Rules: README.md (work rules 2026-11-11). Reuse your 003 native stealth layer; the pack is
/Game/StealthLocomotion (your retargeted copies in /Game/Characters/Animations/Stealth/{Male,Female}); never modify the pack.

## Clips available (InPlace, 62; RootMotion has the same set)
- Slow: Stealth_Slow_{F,FL,FR,L,R,B,BL,BR}_IP + each *_Start_IP;  Fast: Stealth_Fast_{same 8} _IP + *_Start_IP
- Stops: Stealth_Stop_{F,B,L,R}_IP;  turns: Stealth_Turn_90_{L,R}_IP, Stealth_Turn_180_IP;  Stealth_Idle_IP, Stealth_Crouch_IP,
  Stealth_Uncrouch_IP
- Wall: Stealth_{Left,Right}Wall_{Idle,Slow,Slow_Start,Fast,Fast_Start,Stop,Turn_180}_IP and _Lean_Start / _Lean / _Lean_Stop
Use ALL of them where they fit (starts, stops, turns included) - no fallback to the M16 / formation clips while in stealth.

## 1. 8-direction movement with a locked look ("look at the threat")
- In stealth the body never just faces the move direction when there is a threat: if an enemy is VISIBLE TO US (on screen / revealed
  by the squad's sight or a peek) and has NOT detected this operative, the operative locks his look (torso + head, then the whole
  body yaw) on that enemy and moves relative to it: forward / back / strafe / diagonals -> the matching Slow (or Fast) 8-direction
  clip, phase-matched direction blend, Start clips on movement start, Stop_{F,B,L,R} on stop, Turn_90 / Turn_180 for big yaw changes
  in place.
- Target choice (data, defaults): the nearest such enemy inside the operative's view sector (±100°); keep the lock while he stays
  visible + 2 s after losing sight (`stealth.look_lock_keep_s` 2.0); switch only if another becomes >30 % closer.
- No threat -> the look follows the move direction (Slow F + turns), as today.
- If the enemy detects the operative (confirmed detection) -> stealth ends as already implemented.

## 2. Fast stealth (run while sneaking)
- Input (designer 2026-11-12): the existing sprint order = a DOUBLE CLICK on the move target, exactly as today. A double-click
  move while in stealth = a Fast stealth move; a single click = Slow. No new key. It switches Slow -> Fast stealth clips (8 directions,
  Starts, Stops), speed from data (`stealth.fast_speed`, default between crouch-walk and normal run - say the value).
- Detection while Fast is worse than Slow but better than normal: data `stealth.fast_detection_range_mult` default 0.6 (Slow 0.4),
  `stealth.fast_buildup_mult` 0.75 (Slow 0.5); noise as a jog. Fast does NOT break stealth any more (replace the old "sprint breaks
  stealth" rule; firing still breaks it).
- Normal sprint only after leaving stealth (button / 7 again or a confirmed detection).

## 3. A real corner peek (no weapon-aim ghost)
- Today a stealth peek shows the weapon-aim ghost pose (the aiming peek). In stealth use the pack's lean instead:
  Stealth_{Left,Right}Wall_Lean_Start -> Lean (hold) -> Lean_Stop, side from the corner side; the head really passes the edge
  (keep your bone check), the weapon stays lowered; the reveal (enemies behind the corner shown to the squad + on the map) works as now.
- The aim-ghost peek stays only outside stealth.

## Checks (report docs/port/codex/outbox/008-stealth-v2.report.md)
- Unit tests: target choice / lock keep / switch, Slow vs Fast multipliers, "Fast does not break stealth, fire does", lean side.
- StealthModeSmoke extended: with a revealed non-detecting enemy the yaw stays on him while moving in all 8 directions (each
  direction's clip plays: assert the clip name per direction), Start / Stop / Turn clips seen, Fast clips on a double-click move order,
  Fast detection range between Slow and normal, a stealth peek plays Lean_Start/Lean/Lean_Stop and NOT the aim pose; male + female.
- Regression: InjuredLocomotionSmoke, ProneCrawlSmoke, the cover smokes, FormationLabSmoke, SoloStartSmoke.
- Game-camera frames (strafe with locked look, Fast run, the lean) -> MAIN Saved/Orchestrator/stealth/v2/.
Open questions -> the designer in the chat (Russian) with the defaults above; do not block on them.

## 4. BUG (designer 2026-11-12, screenshots + log: MAIN Saved/Orchestrator/stealth/v2/bug-cover-exits-stealth/)
In stealth the commander moves toward a building to press against it:
- hovering the building shows NO cover ghost (outside stealth the hover ghost appears);
- the 1st click shows the blue wall-hug ghost; the 2nd click (confirm) makes him LEAVE stealth, stand up and run to the wall
  (log lines "Run to position!", "Take cover!", label CROUCHED -> STANDING) - loud and visible.
Expected: the cover / wall-hug order keeps stealth - he moves there crouched with the Slow stealth clips (Fast if the order was a
double click), arrives into Stealth_{Left,Right}Wall_Idle via *Wall_Slow_Start / Stop, and stays in stealth; the hover ghost
appears in stealth too (stealth wall pose ghost). Find where the cover order forces run / standing or triggers the
"sprint breaks stealth" rule and route cover moves through the stealth movement. Add it to the StealthModeSmoke (cover order in
stealth: stealth stays on, stance stays crouched, speed <= stealth speed, Wall clips play). Do this FIRST inside 008.

## 5. Manual look anchor: Shift + left click (designer 2026-11-12, VERY important - the whole game is "watch your front")
Today, when the designer spots an enemy in stealth and crawls / sneaks the other way, the operative turns to the move direction,
stops watching the enemy and the enemy becomes invisible again. Wanted:
- **Shift + LMB on the ground (in stealth)** fixes a LOOK POINT (a world position; "vector direction" = from the operative to that
  point, re-aimed as he moves). From then on every stealth move (Slow / Fast, single / double click, cover orders) keeps the body /
  torso / head facing that point and uses the 8-direction clips relative to it (strafe, back-pedal, diagonals) - so the seen enemies
  stay in his view sector and stay visible.
- Priority: manual anchor > the automatic threat lock (section 1) > the move direction.
- Clear it with Shift + LMB on the operative himself (or Shift + RMB anywhere - say which you chose and that it does not clash),
  and automatically on leaving stealth. Show it: a small eye / look marker at the point + a thin line from the operative (like the
  formation look arrows), visible only for the selected operative(s); several selected operatives -> each gets the same point.
- Check first what Shift + LMB already does (IsShiftModifierDown in CodexTacticsPlayerController.cpp:474; Orders.cpp:346 vehicle
  path) - in stealth the anchor wins; outside stealth nothing changes unless the designer asks.
- Smoke: set the anchor, order a move 90 deg to the side -> the yaw stays within 10 deg of the anchor all the way, the side-step
  clip plays (not F), an enemy placed at the anchor stays visible to the squad during the move; clear -> back to section 1 / move dir.
