# APC 02: The Squad Vehicle (BTR-82): Design, Level Design, Tutorial

Roles: Game Designer + Level Designer + UX (studio). Date 2026-10-12. Status: DRAFT for the user's review.
Sources read: `CLAUDE.md`, `docs/port/HANDOFF.md` (system map, tutorial framework), `docs/demo/DEMO01_BEAT_SHEET.md`
(decisions block, §2.1 metrics, A6/A7, B), `Source/CodexTactics/Public/Core/PlayerCommands.h` + the controller key binds,
and the APC assets.

**Asset facts:** `BP_East_APC_BTR82` has the parent **`AActor`**, so it is not a pawn and not a Chaos vehicle. The ChaosVehicles plugin is not enabled in the `.uproject`.
- **BP setters:** `SetTurretRotation`, `SetGunElevation`, `SetWheelSpeed`, `SetFrontWheelsAngle`, `SetDoorsAngle`, `SetHatchAngle`, `SetWheelSmokeIntensity`.
- **Effects and damaged mesh:** muzzle FX `PS_MuzzleFire_01`, wreck FX `PS_Wreckage_01`, damaged mesh `SM_East_APC_BTR82_Damaged`.
- **Skeleton:** `turret_jnt`, `gun_jnt` (+ socket), side doors `door_1/2`, side hatches, rear hatches, driver and commander hatches, and 8 wheels with hydraulics.

The rig is therefore a puppet, so I recommend **kinematic, nav-driven movement**: a C++ component steers the actor and feeds those BP setters. This needs no physics driving.

---

## 1. Player-facing design

### 1.1 Boarding

- **The click stays cover (user: correct).** Clicking the APC still gives the cover ghost and the wall hug, because the hull is 2.6 m high cover with corners at both ends.
- **New key Y = Board / Get out.** Free keys today are Y, I, O and M. **Y** sits next to T (Guard) and follows the existing Alt convention:
  - **Y**: the selected operative(s) board the nearest friendly vehicle within 30 m. If more than one is in range, the hovered vehicle is used.
  - **Alt+Y**: the whole squad boards, recruits included.
  - **Context chip:** hovering the APC shows `[Y] Board  [Alt+Y] All aboard` next to the cover ghost.
  - **Action bar:** the bar gets a **Board** button while a vehicle is within 30 m.
- **Flow:**
  1. Each operative runs to the nearest free side door (left or right, between axles 2 and 3) and the door opens (`SetDoorsAngle`).
  2. The climb-in takes **1.0 s**. The model hides and the portrait gets an "in APC" badge.
  3. The two doors work in parallel, so the 3-man squad is in **about 2 s after arrival**.
  4. The rear hatches are a third entry point if both doors are blocked.
- **Seats** (the real BTR-82 has 3 crew + 7 troops). The game uses **driver + gunner + 5 troop seats = 7**, enough for the squad of 3, Susanin and future recruits. Seats fill automatically:
  - **Driver:** Engineer first ("driver-mechanic").
  - **Gunner:** Commander first.
  - **Troops:** everyone else.
  - **Re-assigning:** with the vehicle panel open, click a seat icon, then an occupant.
  - If only one operative is aboard, he drives and the turret is unmanned. A lone driver may switch to the gunner seat, but then the APC stops.
- **Modes:**
  - **Real time:** allowed under fire. The operatives running to the doors are exposed.
  - **Tactical pause:** planned and executed on release. The APC must be within the 12 m order radius; the free pause has no limit.
  - **Turn-based:** **2 AP** from a cell next to a door.

### 1.2 Driving

- **Click-to-move, exactly like the squad.** While the selected leader is aboard, the APC is the active unit:
  - **LMB on the ground:** drive there at cruise speed.
  - **Double click:** rush.
  - **Shift+click:** the gunner watches that sector (the existing WatchSector meaning).
  - **1-4** still select operatives inside, which is used for Get out (Y) and seat swaps.
- **Speeds:** cruise **6 m/s** (2.7x walk), rush **10 m/s** (1.4x sprint), reverse **2.5 m/s**, acceleration **3 m/s²**.
  - Min turning radius **7 m**.
  - **Reverse is automatic.** If the target is behind and within 15 m, or a turn needs a 3-point manoeuvre, the planner reverses. There is no reverse key.
- **Path preview (UX):** a 3 m wide ribbon (the hull width) shows the curves from the turning radius.
  - The ribbon turns **red** where the vehicle cannot go.
  - Clicking an unreachable spot drives to the nearest reachable point. A bark plays: "Can't get her through there. Dismount? [Y]".
- **Camera:** it follows the APC with a 6 m look-ahead along the direction of travel, and auto-zooms out to about 34 m while moving faster than 4 m/s.
- **Modes:**
  - **Real time:** yes.
  - **Tactical pause:** a planned route of **up to 25 m**. The vehicle is exempt from the 12 m radius because it is fast. It executes on release.
  - **Turn-based:** **no driving** (recommended). The 21 x 21 m grid with 1.5 m cells cannot hold a 5 x 2-cell vehicle with a 7 m turning radius. In turn-based the APC is a static high-cover obstacle; boarding, getting out and the turret still work.
- **Squad safety:** the APC never runs over operatives. It brakes, waits 1 s, then re-paths.
- **Commander Mode (Ctrl+T):** the autonomy never boards or drives on its own. The gunner still auto-fires by fire posture.
- **Ramming:** see Q5.

### 1.3 Getting out

- **Keys:** **Y** gets the selected operative(s) out, **Alt+Y** everyone.
- **Exit side:** the side door **away from the nearest known threat**, otherwise the side nearest the camera. If it is blocked, the other door, then the rear hatches.
- **Arrival in cover:** each operative takes **1.0 s**, lands crouched 1.5 m off the hull and **takes the hull cover slot automatically**. This uses existing cover slot logic on a high cover object. Dismounting under fire is the safe default, and the move teaches itself.
- **Turn-based:** 2 AP, onto a free cell next to a door.
- **Forced exit:** see 1.5.

### 1.4 Turret (gunner seat)

The weapon is the BTR-82's 30 mm 2A72 autocannon plus a coaxial 7.62 mm machine gun. The game picks the gun automatically by target, and the player can override it with a target click.

| | Autocannon (HE) | Coaxial MG |
|---|---|---|
| Use | brutes, marksmen, groups | hounds, frostbitten, crawlers |
| Damage | 70 + 1.5 m splash 35 | 18 per round |
| Rate | 1 shot / 0.5 s, 3-round bursts | 8 rounds/s |
| Range | **40 m** (M16 25, deployable turret 12) | 35 m |
| Limit | **ammo per map** (level JSON, default 60 shells; refill from ammo crates) | **overheat**: 4 s of continuous fire, then 3 s cooldown; unlimited ammo |
| Noise | 60 m (explosion class) | 45 m (gunshot class) |

- **Fire control:**
  - **Fire posture** (`, . /` keys, Alt = squad; the gunner follows it):
    - **Passive:** holds fire. This is the stealth case.
    - **Defensive:** fires only at enemies within 20 m that attack the APC or the squad.
    - **Aggressive:** fires at anything visible within 40 m.
  - **Ctrl+click** an enemy or object: priority target, the same as `TargetedShot`. Ctrl+click a barrel is a cannon shot, so the barrel blows.
  - **Shift+click:** watch sector.
- **Turret limits:**
  - Traverse speed **60 deg/s**, elevation -4 to +60 deg.
  - **Dead zone: within 4 m of the hull** the gun cannot depress enough. Hounds at the wheels must be handled by getting out or driving off. This is the core tension that stops the APC from being invulnerable.
- **Modes:**
  - **Real time:** auto-fire by posture.
  - **Tactical pause:** a planned Ctrl+click target fires on release.
  - **Turn-based:** the gunner acts as a unit with 8 AP. A cannon burst costs **4 AP** and an MG burst **3 AP**. The grid targeting is the existing Ctrl+click on the grid.
- **Intended kill times:** to be tuned against enemy HP in `DA_Balance`, not hand-typed.
  - Hound: 1 shell or about 0.5 s of MG fire.
  - Brute: 4-5 shells.
  - Marksman: 2 shells.

### 1.5 Protection, damage, breakdown

- **The crew inside cannot be targeted or damaged.** The hull takes the hits.
- **Hull: 1500 HP.** Damage multipliers by source:

| Source | Multiplier / effect |
|---|---|
| Hound / frostbitten / cutter melee | **x0.1** (scratches; a pile of hounds still hurts over time) |
| Marksman | **x0.2** |
| Brute | **x1.0**: the real threat; it targets the APC first, with a turret-like weight of 0.85 |
| Explosions (barrels, grenades, our own tripwires) | **x1.0** |
| **Mines** (level and squad; friendly fire on) | **250** damage, **wheel damage −50 % speed** until repaired |

- **At 25 %:** the engine smokes (wheel smoke FX).
- **At 0:** see Q4. The recommended **"disabled, repairable"** mirrors the generator rule of 2026-10-11:
  1. The engine dies and the turret loses power.
  2. The crew **bails out automatically** (2 s, 1 s stagger, no damage) into hull cover.
  3. The hull stays as high cover.
  4. **Repair:** the Engineer brings it back to 30 % in 8 s (others 16 s). He works outside, in the fight.
  5. It is never a mission loss.
- **Optional permanent wreck** (alternative): swap to `SM_..._Damaged` + `PS_Wreckage`; the wreck burns for 60 s and acts as a 5 m heat source.

### 1.6 Survival: cold and fuel

- **Cabin:** the heater runs while the engine runs, so it counts as a **Closed (x0) cold zone**. With the engine off it is Shelter x0.5. Riding in the APC is a cold rest point, which matches its fiction.
- **Engine deck:** while the engine idles it is a **3 m heat source** (warm exhaust), which serves the "wagon fort" horde defence (§2). It gives no heat while moving, so the APC is not a moving campfire for walkers.
- **Fuel:** a level JSON option, **default OFF** (Q3). When on, a 0-100 % gauge drains 1 % per 15 m and 1 % per 60 s of idling. Refuelling comes from barrels, canisters or tanks, which gives siphon quests a second use.

### 1.7 Noise and stealth

- **Engine noise** (enemy hearing radius, attenuation by walls x0.5, like everything else):

| State | Radius |
|---|---|
| Idle | 20 m |
| Cruise | 40 m |
| Rush | 55 m |

- **Engine off:** automatic after 10 s with no driver, or after 20 s parked with the gunner on Passive. There is no extra key.
- **Horde aggro:** wave enemies weight the APC like a turret (small 0.5x, large 0.85x).
- **Stealth maps:** park 60 m or more out, then go in on foot. The APC is never the stealth tool.

### 1.8 Balance limits (stacked; the user asked for zones and obstacles)

1. **One map only.** The APC never crosses a map transfer. A level JSON `vehicle` block holds `enabled`, `fuel`, `shells`, `spawn` and `can_destroy`.
2. **Terrain built for it:** vehicle-only nav (a separate nav agent) plus "no-vehicle" volumes. Thin ice past the beacons, ruins, gates and steps all block it (§2.2).
3. **The 4 m turret dead zone, and Brutes plus mines** as real hull threats.
4. **Finite shells** per map. **No driving in turn-based.**
5. **Objectives always sit on foot:** buildings, consoles and loot. The APC carries you to the story and never through it.

---

## 2. Level design

### 2.1 How vehicle sections fit a linear game with large maps

| Pattern | Use | Length |
|---|---|---|
| **Traverse** | cut 300-600 m of empty walking on big maps; walk-and-talk becomes "ride-and-talk" in the cabin | 1-3 min |
| **Escape set piece** | a chase down a corridor road: the driver follows the ribbon while the gunner auto-fires; gates and wrecks force turns; a Brute blocks the road | 2-3 min |
| **Wagon fort** | park the APC as the anchor of a horde defence: high cover + 40 m gun + engine heat + the Engineer repairing; the dead zone forces the squad to guard its wheels | a wave fight |
| **Dismount point** | the road ends at ruins or a narrow gate: everyone out, objective on foot; the APC stays as a fallback (the "return to the APC" beat) | - |
| Convoy / escort (later) | protect an NPC vehicle; needs a second vehicle actor | later |

**Rule:** at most one vehicle section per map, and at most about 20 % of the map's play time.

### 2.2 Metrics (APC 7 x 3 x 2.6 m)

| Metric | Value |
|---|---|
| Min straight corridor | **5 m** (3 m hull + 1 m each side) |
| 90 deg corner | **9 x 9 m clear** (outer radius 8.5 m) |
| U-turn without reversing | **16 m** width; otherwise a 3-point turn needs 9 m width x 14 m depth |
| Vehicle gate | **>= 5 m** (the 4.5 m roller doors pass at walking pace; keep them as "tight" moments) |
| Climb | steps <= **0.5 m**, slopes <= **25 deg** |
| Blocks the APC | ridges / walls >= **0.8 m** (vault-able low cover 0.9-1.1 m on foot = blocks the APC: the same piece reads as "infantry only") |
| Blocking ditch / ice crack | >= **1.5 m** wide (the A1 crack, 2 m, already qualifies) |
| Personnel-only gaps | <= **2.5 m** (doors 1.6 m) |
| Thin-ice volume | blocks the vehicle nav only, marked by **missing amber beacons** (amber = safe road, art bible) |
| Camera | at 6-10 m/s the 26-28 m camera needs about 40 m of readable road ahead: no blind S-bends behind 2.5 m walls (occlusion 1.43 x H) |

**Readability:**
- Vehicle roads are **amber-marked** with beacons and tyre ruts.
- Blockers have **silhouettes that read as hard**: concrete teeth, wrecks, collapsed bridges.
- "Infantry only" passages are visibly narrow.

### 2.3 Demo 01 Frozen Lake: recommendation

**Keep Demo 01 (Location A) without driving.**
- The scope is locked at A only, about 45 min. A6's stranded APC **is the fuel source**: the siphon empties it to power the generator, so driving it contradicts the quest. The lake fight is a turret, generator and door lesson; a 40 m autocannon would erase it. The lake also has no enemies before the attack, so there are no live turret targets.
- **What Demo 01 gets instead (small):**
  - Engineer line at A6: "Transmission's shot. With parts from a motor pool I could make her run."
  - Susanin (A11) names the **PMC motor pool** (the existing bridge to B).
- **Teach driving in Location B "PMC motor pool / garage"** (postponed, B0-B4) or on the first map that needs it. The fiction lines up with B: the squad finds a **working BTR-82 in the motor pool**, and B4/B5 becomes "drive out" (traverse + an escape with the 4-minute horde behind) and the vehicle tutorial beat V1-V4.
- **Alternative (the user's call, Q1):** an optional **A12 "Drive out"**.
  - After the fight the Engineer patches the A6 APC (8 s) and it is refuelled from the station's tank.
  - The squad drives 200 m to the east gate, with a 6-hound chase for turret practice.
  - Cost: about +5 min to the demo, plus the whole vehicle system before the demo ships.
- **Prerequisite either way:** a test map **`L_VehicleTest`** plus the smoke command `VehicleSmoke`, before any demo integration.

---

## 3. Tutorial: beat V (V1-V4), reusable in any map's steps file

**Framework additions needed:**
- `ATutorialTriggerVolume` gets an **`Activated By: Vehicle`** option.
- New commands are added to the registry, Controls section "Vehicle":
  - `Vehicle.Board` (Y)
  - `Vehicle.BoardSquad` (Alt+Y)
  - `Vehicle.Drive` (LMB)
  - `Vehicle.Rush` (double click)
  - `Vehicle.TurretFire` (Ctrl+Click)
  - `Vehicle.TurretSector` (Shift+Click)
  - `Vehicle.Unboard` (Y)
- New events: `VehicleBoarded(Operative, Seat)`, `VehicleDriveStarted`, `VehicleArrived`, `VehiclePathBlocked`, `VehicleTurretFired(Target, Kills)`, `VehicleUnboarded(Operative)`, `VehicleDamaged`, `VehicleDisabled`, `VehicleRepaired`.

| Step | Card text (keys via `{key:...}`) | Completion |
|---|---|---|
| **V1 Board** (`beat_start`, marker on the APC) | "Clicking the APC = cover. To get in: **[Y]** the selected operative, **[Alt+Y]** the whole squad." | `{"event":"VehicleBoarded","count":3,"distinct_operatives":true}` |
| V1b Seats (radio) | Engineer: "I'll drive." Commander: "I've got the gun." | lines played |
| **V2 Drive** (marked route: 3 `TutorialMarkerActor` gates, 150-250 m, one 90 deg turn, one 4.5 m gate) | "Click the ground: the APC drives there. Double click: full speed. A red path means she won't fit." | volume `VOL_V2_END` (Activated By: Vehicle) |
| V2b Blocked (first_use `VehiclePathBlocked`) | "Too narrow for 13 tonnes. Find the beacon road, or get out." | `timeout_s` 12 |
| **V3 Turret** (vehicle map: 4 PMC target barrels + a scripted pack of 4 hounds) | "The gunner fires on his own in Aggressive **[/]**. **[Ctrl+Click]** picks his target. Shells are limited: the machine gun is free." | `{"event":"VehicleTurretFired","count":3}` + 2 kills |
| V3b Dead zone (on a hound within 4 m) | "Too close for the gun. Drive off, or get out and deal with it." | `timeout_s` 12 |
| **V4 Get out** (dismount volume before ruins / a narrow gate) | "The road ends here. **[Y]** gets the selected one out, **[Alt+Y]** everyone. They exit on the safe side and hug the hull." | `{"event":"VehicleUnboarded","count":3,"distinct_operatives":true}` |
| V5 Repair (optional, if disabled) | "Send the Engineer **[2]**: he repairs her twice as fast." | `VehicleRepaired` |

- **Lesson rule:** V1-V4 are `lesson: true`, with a checkpoint at V1.
- **Metrics** (testers' log `tutorial.jsonl` + bot):

| Metric | Target |
|---|---|
| V1 done | < 45 s |
| V2 done | < 3 min |
| Stuck events (no progress for 5 s while driving) | 0 |
| `VehiclePathBlocked` per tester | <= 3 |
| Turret hit rate | >= 60 % |
| V4 operatives in hull cover after getting out | 3/3 |
| Cards skipped | < 20 % |

---

## 4. Questions for the user (with recommended answers)

1. **Demo 01: drive or not?** Recommendation: **no driving in Location A**. Seed the APC there and teach driving in B (PMC motor pool) or the next map. The alternative is the optional A12 "Drive out" (+5 min).
2. **Keys:** **Y** = board / get out for the selected operative(s), **Alt+Y** = whole squad. The APC click stays cover, with a hover chip and an action-bar button. Recommendation: yes.
3. **Limits:** zones, obstacles and finite shells always; **fuel OFF by default** (a per-level option); **no driving in turn-based** (turret, boarding and getting out still allowed). Recommendation: yes.
4. **At 0 HP:** **disabled and repairable** (the crew bails out into hull cover; the Engineer repairs; never a loss, like the generator), or a permanent burning wreck? Recommendation: disabled and repairable.
5. **Ramming:** may the APC run down hounds and frostbitten (80 damage + knockback above 3 m/s; Brutes stop it)? Recommendation: **yes for small enemies, never operatives**, because players will try it anyway.
6. **Who sits where:** auto seats (Engineer drives, Commander guns), the APC moves only with a driver aboard, and 7 seats. Recommendation: yes.

Выполнено моделью: Claude Opus 5.5
