# Vehicles: APC BTR-82

> Phases P0 + P1 (boarding, driving, nav agents), P2 (turret, object shot, run-over) and P4 (hull damage / repair). Kinematic, nav-driven (no Chaos).
> Design: `docs/vehicles/APC_00..02`. Place it with `Scripts/Editor/setup_apc_vehicle.py` (editor closed).
>
> Key files:
> - `Vehicles/VehiclePawn`, `VehicleRules`, `VehicleCombatRules`, `VehicleHullRules`; nav `UNavArea_NoVehicle`; tests `CodexTactics.Vehicle.*`
> - Smokes `VehicleSmoke`, `VehicleDamageSmoke`; designer decisions to confirm are in `docs/port/OPEN_QUESTIONS.md`
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Vehicles (old §5)

**Vehicles (APC BTR-82, phases P0 + P1, 2026-10-12, UE-only; design `docs/vehicles/APC_00..02`)** — kinematic, nav-driven
(approach B, no Chaos). `Vehicles/VehicleRules` (pure: seats `AssignSeats` Engineer driver / Commander gunner / anyone if absent, 7
seats; `CanBoard` / `CanDisembark` / `CanDrive` (P1: refused in the tactical pause and turn-based); the Y key `ResolveKeyAction`
(all addressed aboard = get out, else board); `RankExitPoints` (away from the nearest known threat, else the camera side, blocked
skipped); drive model `StepYaw` (yaw rate <= v / R, no pivot), `ShouldReverse` (goal behind >= 120 deg within 15 m),
`StepSpeed` / `GetDesiredSpeed` (cruise 6 / rush 10 / reverse 2.5 m/s, 3 / 6 m/s2, braking distance, slow to turn),
`GetLookaheadPoint` (pure pursuit, 1 m carry past a corner > 25 deg: no cutting round wall ends), `GetHullSideFacing` (cover probe),
noise radii idle / cruise / rush 20 / 40 / 55 m, wheel spin, Ackermann steer, `GetMountedEffects`). `Vehicles/VehiclePawn`
(`AVehiclePawn`, Blueprintable APawn: Root (ground pivot), `Hull` box (WorldDynamic, blocks everything but Camera, overlaps,
no nav), `Mesh` (vendor SK + ABP, no collision; a C++-only spawn loads `DefaultMesh` / `DefaultAnimClass` by soft path),
`CabinHeat` (`UHeatSourceComponent`, on while the engine runs = a crew member aboard), `NavObstacle`; tunables `DriveConfig` /
`CrewConfig` / `EngineConfig`, hull centre / extent, entry points (side doors, rear hatch), ground probes; `DriveTo` (path on the
"Vehicle" navmesh by agent name, partial paths drive to the nearest reachable point; K-turn first when facing > 70 deg away),
pawn sweep ahead (brakes for operatives / enemies, gives up after 6 s), ground conform (4 traces: Z, pitch, roll), vendor ABP
variables by name (`WheelSpeed`, `WheelsAngle`, `DoorAngle`) + BlueprintImplementableEvents On Visual Drive / On Doors Changed / On
Engine Changed, engine noise breaks patrols (`AEnemyCharacter::BreakPatrol(Hearing)`) once a second). `Vehicles/VehicleNavigation`
(`UNavArea_NoVehicle` = unwalkable for agent 1 only; `UNavArea_VehicleHull` = unwalkable for agent 0 only;
`UVehicleNavObstacleComponent` = one rotated box modifier over the hull + 40 cm on the infantry navmesh, off while driving, on when
parked). `Vehicles/VehicleSubsystem` (`UVehicleSubsystem`: registry; mounted map + `VehicleQueries::IsMounted / GetVehicle`;
`HandleBoardKey` (Y / Alt + Y, action bar BOARD / EXIT), `OrderBoard` (walk to the nearest door, retries / another door when one is
crowded, 2 s climb, `CancelBoarding` on a new move / cover order), `OrderDisembark` (exit candidates on the infantry navmesh, free of
bodies, ranked, 1 s, then `OrderTakeCover` at the hull side), `HandleDriveClick`, mount = stop, leave cover, hidden, no collision,
movement off, attached at `CrewAttachOffset`; opt-in vehicle navmesh (strips the Vehicle agent from untagged bounds at BeginPlay,
`EnableVehicleNavigation`); save `world_state.vehicles` (name, class, transform, crew + seats), `PrepareForLoad` / `RestoreSave`).
**The IsMounted list** (keep it short): enemy targeting / perception / hearing / nearest / pounce (`EnemyCharacter`, `GetBelief`),
marksman targets, operative cover / facing / shooting tick, `ResolveIncomingHit` (no damage), `RequestMove` (refused), formation
followers (a mounted leader: the ones outside hold), Commander Mode, mines / tripwires, the controller's group moves and the click
pick, turn-based entry (refused while anyone is aboard). Controller: a ground click with the leader aboard drives; a click on the
vehicle = cover at its hull (`FindCoverSlotForHit` / `TryHandleCoverClick` vehicle exception, `CoverTraceRules` no longer ignores
vehicle pawns). Commands `Vehicle.Board` / `Vehicle.Disembark` / `Vehicle.Drive` (F1 section «Vehicles»), events `VehicleBoarded` (Kind
= seat), `VehicleDisembarked`, `VehicleDriveStarted`, `VehicleArrived`, `VehiclePathBlocked`; `ATutorialTriggerVolume` «Activated By:
Vehicle». Tests `CodexTactics.Vehicle.*` (10), smoke `VehicleSmoke`; editor script `Scripts/Editor/setup_apc_vehicle.py`.

**Vehicles P2 + user requests (2026-10-12, branch `agents/apc2`)** — `Vehicles/VehicleCombatRules` (pure: `FVehicleTurretConfig`
(cannon 45 dmg + 1.5 m splash 20, 2-shell bursts 0.5 s / pause 1.2 s (nerfed by the user 2026-10-12 from 70 / 35 / 3), 40 m, 60 shells per map, hit 0.9, armour factor 0.25 = the HE
shell mostly ignores the Brute's hide; MG 18 dmg x 8 rounds/s, 35 m, hit 0.8, overheats in 4 s / cools in 3 s, no suppression
stagger (removed by the user 2026-10-12: the MG only deals damage); traverse 60 deg/s, elevation -4..+60, dead zone 4 m from the hull side, Defensive radius 20 m),
`ChooseTarget` (Ctrl+click priority in any posture; Passive holds; Defensive only once provoked and within 20 m; Aggressive any;
a Brute within 20 m first, else the nearest; never in the dead zone / out of sight / range), `ChooseWeapon` (cannon for a Brute /
marksman / group of 3 / priority while shells last, MG for the rest; an overheated MG means small targets wait), `StepHeat`,
`GetCannonDamage`, `GetShellsForMap`, `GetHudLine`; `FVehicleCrushConfig` + `EvaluateContact` (operative /
friendly -> Brake; small enemy -> Crush >= 1.5 m/s else Push aside; Brute -> KnockDown >= 4 m/s unless re-knock immune, else Block;
a downed Brute -> Block (option `bCrushDownedRammed`); flying drone -> None), `GetSpeedAfterImpacts` (x0.9 per crush, x0.25 per ram);
`FVehicleFadeConfig` + `FVehicleFadeTimeline` (0.25 s out / hold >= 0.15 s / 0.25 s in, joins while going black, a request during the
fade-in queues one more cycle, hold <= 1 s) + `ShouldStartGroupFade` (the first at a door waits <= 8 s for the mates who will
reach their doors in that time: one fade for the squad, a straggler gets his own)). `Vehicles/VehicleTurretComponent` (`UVehicleTurretComponent` on `AVehiclePawn::Turret`: gunner = the Gunner seat;
retarget every 0.25 s from LOS traces (ECC_Visibility) from the muzzle socket `gun_jntSocket`; slews the vendor ABP
`TurretHorizontalAngle` / `GunElevation` (signs measured by VehicleCombatSmoke); damage through `UHealthComponent::TakeDamage`
(numbers, armour, downed x1.33 as for any hit; `AttackerSource` = the gunner's name = his kill stats), tracers
(`UCombatFeedbackSubsystem::SpawnTracer`, every other MG round), muzzle flash = the vendor Cascade `PS_MuzzleFire_01` attached to the
socket, noise (`NotifySquadNoise` Explosion / Gunshot, once a second); shells saved in `world_state.vehicles[].shells`; held without a
gunner / in turn-based / in the world AI pause). `AVehiclePawn` additions: `CrushConfig` / `FadeConfig`, `HandleContactsAhead`
(replaces the P1 "brake for every pawn" sweep: a brake zone as long as the braking distance + 1 m, a contact zone = the last metre of
the hull + 60 cm; `CrushEnemy` = body thrown beside the hull + `ApplyDirectHealthLoss` with the driver's name (normal death chain:
corpse, kill stats, XP, waves) + "CRUSHED" text + `VehicleRunOver`; `RamEnemy` = ram damage 40 + `UKnockdownComponent::
ForceKnockDownScaled(.., 0.65, 1.5)` (new: the fall and get-up clips at an absolute 0.65 rate, downed x1.5; `KnockdownRules::
ScaleTimeline`) + 80 cm shove), the wheels from the MEASURED ground speed (displacement per tick): the vendor ABP turns the wheels by
`WheelSpeed x WheelSpeedOffset` where `WheelSpeedOffset` is its own clock (seconds) — any speed change multiplied the whole elapsed
time (the whirl / slip the user saw); C++ now pins that clock to 1 s each tick and writes `WheelSpeed = wheel angle / clock`
(`VehicleRules::GetAnimWheelSpeedForAngle`), the wheel angle integrating ground speed / 60 cm; the acceleration ramps from 30 % to full
over 0.8 s when pulling away (`GetLaunchAccelerationFactor`); BlueprintImplementableEvents On Turret Fired / On Ran Over (sound / FX
polish). `UVehicleSubsystem`: boarding / getting out through the fade (`ShouldFade`: the vehicle's fade on, real time, not paused;
the ones at a door wait for the group, then mount on black; exits all go on black once the hull stands; camera `SnapFocusOnTarget`
on black; Slate overlay z 9000, hit-test invisible, real-time clock, watchdog), `HandleTurretTargetClick` (Ctrl+click with the leader
aboard; command `Vehicle.TurretFire`). Data: `Content/Data/Weapons/weapons_tuning.json` "vehicle_weapons" {"apc_turret", "apc_ram"}
(`WeaponTuning::ApplyVehicleWeapons`, dumped by `DumpWeaponTuning`, kept by the Wave Editor), level JSON `"vehicle": {"shells": N}`
(`FLevelCombatConfig::VehicleShells`). HUD: `DrawCombatModeBadge` adds "TURRET AGG   30mm 57/60   MG heat 40%" while the leader is
aboard. Events `VehicleTurretFired` (Kind Cannon per shell / MachineGun per second of fire), `VehicleTurretKill`, `VehicleRunOver`.
Tests `CodexTactics.Vehicle.*` (18), smokes `VehicleSmoke` (+ fades) / `VehicleCombatSmoke`, dev still `VehicleTurretShot`.

**Vehicles P4 hull damage + the P2 object shot (2026-10-12, branch `agents/work`)** — `Vehicles/VehicleHullRules` (pure:
`FVehicleHullConfig` (1200 HP; armour per `EVehicleDamageSource`: SmallMelee x0.1 (hound / frostbitten / cutter), HeavyMelee x1.0
(Brute), Acid x0.5 (spitter / cryo drone), Rifle x0.25 (marksman), Explosion x1.0 (barrel / grenade / trap / a mine beside it), Mine
= flat 250 (a mine / tripwire the vehicle set off itself), Ram (the fast Brute ram: `FVehicleCrushConfig::HullRamDamage` 30), low
mark 25 %, repair: Engineer 5 s (others 0 = not allowed) to 50 %, enemy weights small 1.0 / Brute 0.85, awareness 40 m),
`GetHullDamage`, `ClassifyEnemyAttack`, `ApplyDamage` (-> Damaged / DamagedLow / Disabled; never destroyed, nothing more once
disabled), `Repair`, `RestoreHull`, `IsEnemyTarget` (manned, running, a horde or within 40 m), `GetBlastHullDamage`, `GetHudLine`,
`CheckObjectShot` (barrel / mine / trapped object: gunner, disabled, held, shells, 40 m, 4 m dead zone, line)); `VehicleRules::
GetEjectionOrder` (safe side first, blocked exits after: nobody stays inside), `CanBoard` / `CanDrive` refuse `Disabled`.
`AVehiclePawn`: `HullConfig` (weapons_tuning.json `vehicle_weapons.apc_hull` overrides), `ApplyHullDamage` (damage number,
`VehicleDamaged` Kind = source, low-mark feed line, at 0: stop, engine off, `ApplyDisabledVisuals` = `DamagedMesh` (vendor
`SM_East_APC_BTR82_Damaged`, loaded on first use) instead of the skeletal mesh + `DamagedSmoke` (vendor Cascade `PS_Wreckage_01`),
BP events On Hull Damaged / On Disabled Changed, `OnDisabled`), `RepairHull`, `RestoreHull`, `GetClosestHullPoint` /
`GetHullAttackPoint` (enemies close in on the hull side), engine can't start disabled, `DriveTo` -> `Disabled`.
`UVehicleSubsystem`: `HandleDisabled` -> `EjectCrew` (forced exits also in the pause, the fade in real time, into hull cover;
boarders turned away) + `VehicleDisabled`; `OrderRepair` / `UpdateRepairs` (Y near a disabled vehicle: the Engineer among the
addressed walks to the hull, crouches, works 5 s; a new order (CancelBoarding) / knockdown / walking off interrupts; `VehicleRepaired`),
`ApplyBlastToVehicles` (from `AInteractableActor::ApplyBlast` - barrels, mines, tripwires, trapped objects - and the grenade:
hull distance falloff; `BlastTriggeredByVehicle` = the flat mine damage), `FindEnemyTargetVehicle`; save `hull_hp` / `disabled`.
Mines (`AProximityMineActor::Tick`: the hull over the mine, level mines / squad mines with friendly fire) and tripwires (the hull
crosses the wire, friendly side) are set off by the vehicle. Enemies: `EEnemyTargetKind::Vehicle` (+ candidate `Weight`) in
`EnemyAIRules::SelectTarget`; `AEnemyCharacter::FindTarget` adds the manned APC at its nearest hull point (a horde's "squad only"
rule lets it through), melee range / approach to the hull side, `AttackObject` -> `ApplyHullDamage`, `NotifySquadAttacked`
also for the vehicle (Defensive gunner provoked); spitters / cryo drones and the marksman shoot the hull when the squad is aboard
(`TickRangedVsVehicle`). The crew inside is never hit (TakeHit, blasts) nor knocked down (`UKnockdownComponent::NotifyExplosion`
skips the mounted). Turret: held when disabled; `SetObjectTarget` (Ctrl+click on a barrel / mine / trapped object = one cannon
shell once aimed, the same detonation as an operative's targeted shot: `AOperativeCharacter::DetonateShotTarget`, shells -1;
auto-fire stays enemy-only). HUD: `HULL 840/1200` under the turret line while the leader is aboard + an overhead label over a
damaged / disabled hull; action bar REPAIR [Y]. Command `Vehicle.Repair`; events `VehicleDamaged` / `VehicleDisabled` /
`VehicleRepaired`. Tests `CodexTactics.Vehicle.*` (24), smoke `VehicleDamageSmoke`, dev still `VehicleDisabledShot`.

## APC: how the user places it / makes L_VehicleTest / nav for vehicles (old §6)

### APC: how the user places it / makes L_VehicleTest / nav for vehicles (P0 + P1, 2026-10-12)

1. **The Blueprint (once, editor CLOSED):** `UnrealEditor-Cmd.exe "<repo>/CodexTactics.uproject" -run=pythonscript
   -script="<repo>/Scripts/Editor/setup_apc_vehicle.py" -unattended -nullrhi` creates `/Game/Vehicles/BP_APC_BTR82` (child of
   `AVehiclePawn`, Mesh = `SK_East_APC_BTR82`, Anim Class = `ABP_East_APC_BTR82`; the vendor folder is untouched; re-running keeps
   your edits). Or by hand: Content Browser -> Blueprint Class -> parent **VehiclePawn**, name it `BP_APC_BTR82` in `Content/Vehicles`,
   select **Mesh** -> Skeletal Mesh Asset `SK_East_APC_BTR82`, Anim Class `ABP_East_APC_BTR82`. Without the BP a dropped
   `VehiclePawn` (C++) shows the same vendor model by soft path. Polish in the BP: the events **On Visual Drive** (speed, steer),
   **On Doors Changed**, **On Engine Changed** (sound, dust FX); the ABP variables C++ writes are `WheelSpeed` (deg/s), `WheelsAngle`
   (deg), `DoorAngle` (0 / `Door Open Angle` 80) - rename them in **Visual** if the ABP changes.
2. **Tunables (Details, CodexTactics | Vehicle):** Drive Config (cruise / rush / reverse speed, acceleration, braking, **Min Turn Radius
   Cm** 700, look-ahead, reverse distance 15 m), Crew Config (passenger seats 5, board range 30 m, board 2 s / get out 1 s, exit
   offset), Engine Config (noise radii 20 / 40 / 55 m, cabin heat radius 3.5 m), **Hull Centre / Hull Half Extent** (the collision box,
   shown light blue), **Entry Points** (doors, pivot space), Crew Attach Offset, Ground Probes.
3. **Make `L_VehicleTest`:** a new level (File -> New Level -> Basic, or duplicate L_PatrolTest), a big flat floor (60 x 60 m or a
   landscape), **a NavMeshBoundsVolume** over the drivable area with the actor **Tag `VehicleNav`** (Details -> Actor -> Tags) - only
   tagged volumes (or a volume with only **Vehicle** ticked under Navigation -> Supported Agents) build the vehicle navmesh, every
   other volume keeps only the infantry one (no cost on maps without vehicles). A PlayerStart; drop **BP_APC_BTR82** 10-20 m from it,
   wheels on the ground (it snaps to the floor at BeginPlay). Walls / wrecks / gates as obstacles (>= 5 m wide passages: the vehicle
   agent's radius is 2 m). **Blocked spots** (thin ice, narrow alleys, steps): a **NavModifierVolume** with Area Class
   **NavArea_NoVehicle** - only the vehicle avoids it; make it ~2 m bigger than the spot (areas are not eroded: the hull centre stays
   out, half the hull may overhang). Press **P** in the editor to see the navmeshes (the vehicle one is drawn in orange).
4. **Play:** select an operative, **Y** = he boards (walks to the nearest door, ~2 s), **Alt+Y** = the whole squad; seats fill
   automatically (Engineer drives, Commander guns, the rest ride; anyone if absent). With the leader aboard **LMB on the ground** =
   drive there (double click = full speed; a short goal behind = reverse; the camera follows). **Y** again = the selected get out,
   **Alt+Y** = everyone (they exit on the side away from known enemies and take cover at the hull). Clicking the APC itself is still
   "take cover at its hull". The action bar shows **BOARD [Y]** near a vehicle and **EXIT [Y]** aboard. Limits (kept by the user
   2026-10-12): no driving / boarding in the tactical pause or turn-based (feed line), turn-based entry refused while anyone is
   aboard, no damage yet (P4), engine noise only breaks patrols.
   **P2 (2026-10-12):** boarding / getting out is a quick fade to black (no climbing animation; one fade for the group). With a
   second operative aboard (gunner seat) the **turret** fires on its own by the gunner's fire posture (`,` `.` `/`), **Ctrl+click**
   an enemy = its target; the HUD shows `TURRET AGG 30mm 57/60 MG heat 40%`. Nothing within 4 m of the hull can be shot (drive off or
   get out). Driving through small enemies (>= 1.5 m/s) **runs them over**; a Brute is knocked down at >= 4 m/s and stops the APC;
   operatives are never run over.
   **P2 tunables:** Details -> **Turret** component -> Config (cannon / MG / mount numbers; `weapons_tuning.json`
   "vehicle_weapons.apc_turret" overrides at play — edit the JSON or the Wave Editor file, it is the master copy at runtime; shells
   per map also by level JSON `"vehicle": {"shells": N}`); **Crush Config** (crushable / rammed archetypes, crush 1.5 m/s, knockdown
   4 m/s, ram damage, slowdowns, the Brute's fall play rate 0.65 / downed x1.5, run over a downed Brute) - JSON "apc_ram" overrides;
   **Fade Config** (on / off, 0.25 / 0.15 / 0.25 s, max hold 1 s, group wait 8 s). Polish events in BP_APC_BTR82: **On
   Turret Fired** (weapon, muzzle: the cannon / MG sound), **On Ran Over** (victim, location: crunch + blood / snow FX).
   **P4 hull (2026-10-12):** the APC has **1200 HP**; the HUD shows `HULL 840/1200` while the leader is aboard and a label over a
   damaged / disabled hull. Hounds / frostbitten / cutters scratch it (x0.1), a Brute's blow and blasts (barrels, grenades, traps)
   hurt in full, spitter acid x0.5, the marksman x0.25, a **mine or tripwire under the wheels = 250**, a fast ram on a Brute 30.
   Enemies go for the APC when it is manned and the closest threat; the crew inside is never hit. At 0 HP it is **disabled**: engine
   off, no driving, the turret silent, the damaged model + smoke, the crew jumps out (fade) on the safe side into hull cover.
   **Y** near it (the action bar shows **REPAIR [Y]**) sends the **Engineer** (select him, or Alt+Y): he walks to the hull and
   works ~5 s -> running again at 50 %. **Ctrl+click a barrel / mine / trapped object** while aboard with a gunner = one 30 mm shell
   (it blows up like an operative's shot; not within 4 m of the hull, not beyond 40 m, needs a line). Tunables: Details ->
   **Hull Config** (`weapons_tuning.json` `vehicle_weapons.apc_hull` overrides at play); the **Damaged Mesh** / **Damaged Smoke**
   components (empty = the vendor damaged mesh / wreck smoke; move them in the BP if the wreck sits off); BP events **On Hull
   Damaged** (sparks / clang) and **On Disabled Changed**.
5. **Nav agents (DefaultEngine.ini, for any map):** `SupportedAgents` = `Default` (35 / 144: the engine's former implicit agent,
   infantry unchanged) + `Vehicle` (200 / 300, step 40, `bCanFly=True` so no ground pawn ever picks it). Keep the order (the nav areas
   address the agents by index 0 / 1). Opening an old map shows no change except the tag rule above.

**Group boarding fade fix (2026-10-22 e).** `VehicleSmoke` failed "squad boarding = ONE fade (2)": the Medic-Sapper set off from the group cover
at ~1 m/s (accelerating / turning out of cover) 10.8 m from her door, the arrival estimate used that momentary speed (13 s, beyond the 8 s group wait) and
the first two went in without her. A source bisect (today's content with the source of 8de6b30, the last passing run of 2026-10-21) fails too, so no
source commit after 2026-10-21 broke it - the last pass was a main-checkout run with the content of that day; the estimate was fragile. Now
`VehicleCombatRules::EstimateWalkerSpeed`: a MOVING walker counts at least `WalkerCruiseFraction` (0.6) of his top speed; a standing one keeps his
velocity (the 150 cm/s floor applies). Log line per group fade: `[Vehicle] group fade after N s at the door; walkers' ETA: ...`. Test
`CodexTactics.Vehicle.Fade.GroupBoarding` (+ the setting-off case).
