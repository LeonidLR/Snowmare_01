# APC (BTR-82) as squad transport: technical assessment and plan

Role: Unreal specialist / technical director. Date: 2026-10-12. Read-only investigation. I scanned the asset strings and ran one
headless Python probe with `-nullrhi` and no saves; no editor had the main project open.

## 1. What the pack actually is

| Item | Finding |
|---|---|
| `BP_East_APC_BTR82` | **Plain `AActor`** (parent `/Script/Engine.Actor`), UE 4.26 content. It is **not** a Pawn or a Chaos `AWheeledVehiclePawn`, and it has no movement, input or physics simulation. It is a *showcase puppet*. |
| Components | `DefaultSceneRoot`; `SkeletalMesh` (ABP_East_APC_BTR82, **NoCollision**); `DamagedModel` (SM, hidden, **BlockAllDynamic, QueryAndPhysics**: the hidden wreck is the BP's only collision); `CannonFX` (muzzle), `DamagedSmoke` (wreckage); `WheelSmokeL1..4 / R1..4` (dust). All FX are **Cascade** `ParticleSystemComponent`s, which are legacy but still load in 5.8. |
| BP API (ready to call) | `SetTurretRotation`, `SetGunElevation`, `SetWheelSpeed`, `SetFrontWheelsAngle`, `SetDoorsAngle`, `SetHatchAngle`, `SetActivateFiring(bool)`, `SetShowDamaged(bool)` (swaps to the damaged mesh and starts the smoke), `SetSkinType` (Default / Desert / Forest), `SetLightsEmissivity`, `SetWheelSmokeIntensity`. |
| ABP | Parent `UAnimInstance`; MeshRefPose plus `ModifyBone` nodes only (no clips). Variables: `TurretRotation` / `TurretHorizontalAngle`, `GunElevation`, `WheelSpeed` (+ `WheelSpeedOffset`, front / back wheel spin), `WheelsAngle` (front steering), `DoorAngle` (left / right), `HatchAngle`, `FrontWindowAngle`. Driven bones: `turret_jnt`, `gun_jnt`, `lf/rt_wheel_1..4_jnt`, `lf/rt_side_hatch_1/2_jnt` (doors), `commander_hatch_jnt`, `driver_hatch_jnt`, `lf/rt_hatch_jnt`, `lf/rt_shields_jnt`. The suspension bones `*_wheel_N_hidr_jnt` exist but are **not driven**, so there is no suspension animation today. |
| Skeleton / sockets | `root_jnt` > `cog_jnt` > turret / gun / wheels / hatches / mirrors / antennas. The only socket is **`gun_jntSocket`** (muzzle). There are **no seat sockets and no exit sockets**, so we define those ourselves. |
| PHYS | Bodies on `cog_jnt`, `gun_jnt` and 8 wheels; `PhysType_Kinematic` with constraints. It is not set up for vehicle simulation. |
| Scale | Mesh bounds 7.78 × 3.07 m (half extents 389 × 153 cm). The bounds are 4.96 m high because of the antennas; the hull collision is about 2.9 m. The pivot is 58 cm behind the bounds centre. Wheels sit at x = +270 / +125 / −55 / −190 cm and y = ±130 cm, which gives a **wheelbase of about 4.6 m and a track of 2.6 m**. Textures are 4K (about 750 MB in the pack, mostly the `_LTG` demo map's BuiltData). |
| Demo maps | `East_APC_BTR82*.umap`: showcase platform, cameras, `BP_East_APC_BTR82_Showcase`, Vigilante HUD widgets. There is nothing reusable for gameplay. |
| Other vehicles | **None.** `VigilanteContent/Vehicles` holds only the BTR-82, and `Codex/ASSETS` has no vehicles. |
| Plugins | **ChaosVehiclesPlugin is not enabled** in `CodexTactics.uproject`. In 5.8 it ships under `Plugins/Experimental` with `IsExperimentalVersion: true` and `EnabledByDefault: false`. |
| Our maps | `L_Demo01_A_FrozenLake` places the APC as a **`SkeletalMeshActor`** (SK only, not the BP) at (−1770, −2330, 0), yaw 80. It uses the `PhysicsActor` profile and has `CanEverAffectNavigation = false`, so the Default navmesh runs under the hull. Clicking it gives cover today because cover uses Visibility traces (`CoverTraceRules`). Landscape is about 105 × 120 m; the NavMeshBounds is 80 × 80 m; nav is `RuntimeGeneration=Dynamic` with **one implicit agent** (no `SupportedAgents`). |

## 2. Approach comparison

| Criterion | (A) Chaos `AWheeledVehiclePawn` + AI throttle / steer | (B) **Kinematic vehicle pawn on a vehicle navmesh** |
|---|---|---|
| Asset work | New BP from `AWheeledVehiclePawn`; rebuild PHYS (simulated chassis, 8 wheel setups, mass, torque curves); enable an experimental plugin | Duplicate the BP, reparent it to our C++ class, and add one box hull collider |
| Driving AI | Write a throttle / steer / brake controller and tune it for an 8 × 8, 7.8 m vehicle on a landscape: overshoot, flipping on ridges, snow friction | `AAIController::MoveTo` + `UPathFollowingComponent` with our own `UPawnMovementComponent` (pure pursuit, Ackermann turning limit, ground conform) |
| Tactical pause (dilation 0.02) / world AI pause | Physics at tiny dt plus engine sim: needs testing; freezing it means sleeping the bodies | Works through world delta time, like the operatives today |
| Turn-based grid | Physics cannot land on exact cells; needs teleporting and freezing | Exact: teleport to a footprint of cells, same as barricades (`_register_barricade_cells`) |
| Save / load | Restoring velocities and suspension state is fragile | Transform + state only |
| Smokes / determinism | Poor | Good: same as the operatives' smokes |
| Visual fidelity | Real suspension and body roll | Pitch / roll from 4 ground traces; wheel spin = distance / radius; steering angle; fake suspension later through `*_hidr_jnt` (the user's ABP polish) |
| Effort | XL, with high tuning risk | L |

**Recommendation: (B).** A linear tactical game needs predictable, plannable and saveable movement, not driving physics. Chaos would
fight time dilation, the grid and saves, and the plugin is still experimental in 5.8.

## 3. Architecture (B)

- **`AVehiclePawn` (C++, `Source/CodexTactics/Vehicles/`)**: an `APawn` with a box hull collider sized to the hull
  (`ECC_WorldStatic` blocking, so `BakeObstacles` and cover traces see it), `UHealthComponent`, `UVehicleSeatsComponent`,
  `UVehicleKinematicMovement`, an optional `UHeatSourceComponent`, `UNavModifierComponent`, and tunables (speed 6–8 m/s cruise, accel, gameplay
  minimum turn radius about 6 m, reverse allowed). The visual BP is **`/Game/Blueprints/Vehicles/BP_Vehicle_BTR82`**: a copy of
  the vendor BP reparented to `AVehiclePawn`. The vendor original stays untouched. C++ calls BlueprintImplementableEvents
  `OnVisualDrive(Speed, Steer)`, `OnTurretAim(Yaw, Pitch)`, `OnDoors(Open)`, `OnFired`, `OnDestroyedVisual`, which the BP wires to its
  existing `Set*` functions. This follows the rule that characters and vehicles are user BPs while C++ holds the logic.
- **`AVehicleAIController`** plus a **second nav agent "Vehicle"** in `DefaultEngine.ini` `SupportedAgents` (radius about 190–220 cm,
  height 300, step 40, slope about 25°). The Default agent must be re-declared with the **current implicit values** (radius 34 / height 144)
  so the operatives' navmesh does not change. Areas are limited in two ways. (1) The Vehicle navmesh exists only inside
  `NavMeshBoundsVolume`s whose agent mask includes Vehicle, so a designer paints where driving is possible. (2) A new
  `UNavArea_NoVehicle`, excluded by the vehicle's query filter and placed with `NavModifierVolume`s, marks ice, ditches and
  narrow alleys. Narrow gaps are also blocked by navmesh erosion. The precedent is `UNavArea_Tripwire` / `UNavArea_Vault`.
- **Movement**: path corners go into pure pursuit with lookahead about 4 m and a turn-radius clamp; a reverse manoeuvre starts when the target is
  more than 120° behind. Each tick, 4 down-traces (front/back/left/right) set pitch, roll and Z. Each tick, a sweep of the hull ahead stops the vehicle for operatives and
  enemies (no running over; that would be a balance decision for the user). Wheel spin and steering feed the ABP. While moving, the vehicle does
  not affect the Default navmesh. On stop, its `NavModifier` (Null area) carves the Default navmesh so operatives path around it, avoiding
  constant tile rebuilds.
- **Seats and boarding**: seats Driver / Gunner / Passenger ×N (the real BTR-82 carries 3 crew and 7 troops, so the whole squad of 3 + recruit fits).
  Entry is through the side doors (`lf/rt_side_hatch`) or the rear hatches. Exit points are data offsets (rear L/R, sides). Each is checked with a capsule overlap plus
  `ProjectPointToNavigation`; the first free one is used, or the order is refused with a feed line when all are blocked. A boarding operative walks to the nearest
  entry point (reuse the use-order approach in `UInteractionSubsystem`), the door opens, then he is hidden, attached, has collision and movement off and AI stopped,
  and `IsMounted()` becomes true. **Mounted operatives stay registered and co-located**: the vehicle is their **damage proxy**
  (`AOperativeCharacter::TakeHit` / `UHealthComponent` route to the vehicle), so the many world systems keep working.
- **Integration points**:
  - `ACodexTacticsPlayerController::HandleWorldHit`: when the leader (or selection) is mounted, a ground click becomes a vehicle MoveTo. The real-time order lock and
    pause planning apply unchanged (`PlanMove` equivalent plus cyan route markers in `UCombatFeedbackSubsystem`).
    **Trap:** `FindCoverSlotForHit` / `TryHandleCoverClick` reject `IsA<APawn>()`, so making the APC a Pawn would break today's
    "click = cover". Add an explicit exception (`AVehiclePawn` provides cover while parked). On drive start, evict cover users (`CoverLeft`).
  - `USquadSubsystem`: `GetMembers()` has **278 call sites in 125 files**. Do not filter globally. Add `IsMounted()` checks only at
    formation / followers, autonomy, enemy target pick (`EnemyAIController` / `EnemyTacticsSubsystem`, which retarget to the vehicle),
    `TacticalSightSubsystem` (sight from the vehicle), vision cones and selection rings (hidden), cold, and the TB units.
  - `UInteractionSubsystem`: a plain click keeps cover. **Board / disembark goes through a new command**, proposed as `Ctrl+B` (B = solo; the Alt
    modifier = whole squad, following the existing convention), with `Vehicle.Board`, `Vehicle.Disembark`, `Vehicle.Drive`, `Vehicle.TurretTarget` in
    `PlayerCommands.cpp`. That also gives the F1 page. Gameplay events `VehicleBoarded`, `VehicleLeft`, `VehicleArrived`, `TurretFired` feed the tutorial.
  - `ATacticalCameraPawn`: the follow anchor is the vehicle when the leader is mounted (`SetFollowTarget`, `HandleLeaderChanged`).
  - `USaveGameSubsystem`: new `world_state.vehicles[]` entries {node_name, transform, health, ammo, fuel?, destroyed, turret yaw,
    seats → operative names}; operatives get `mounted_in`. Extend `SaveRoundTripSmoke`.
  - `UTurnBasedCombatSubsystem`: the vehicle is a multi-cell occupant (rotated footprint about 6 × 2 cells of 1.5 m, barricade code).
    **The 14 × 14 grid around the leader is cramped** with a 6 × 2 vehicle in it; centre the grid on the vehicle and widen it to about 20 × 20 when one is present.
- **Turret**: fires only with a **Gunner** seated. Real time: auto-target follows the gunner's fire posture (`ESquadFirePosture`); targeting is
  extracted from `ATurretActor` (nearest visible, LoS from `gun_jntSocket`, cover multiplier) into pure `TurretTargetRules`;
  Ctrl+click sets a priority target; yaw / pitch slew rates drive the ABP. Shots use the existing `UHealthComponent` and `CombatFeedbackSubsystem` tracers
  and damage numbers. Ammo is a vehicle pool. Numbers go in `DA_GameBalanceConfig` (Unreal is master). Turn-based: the gunner's attack = turret burst, 3 AP.
- **Damage / destruction**: armour profile (`UHealthComponent` armour + elements) so bites and claws do about 0 while explosives, acid and the marksman do harm.
  At 0 HP: `SetShowDamaged(true)` (damaged mesh plus wreckage FX), the crew is ejected at free exits with damage and knockdown, and the wreck
  stays as static cover and a nav obstacle.
- **Cold**: the interior is sheltered. Mounted crew get a `FColdEnvironment` override (no accumulation, slow warming), or an interior
  `UHeatSourceComponent`. Fuel is optional; see the questions.
- **Performance**: one vehicle, about 5 traces per tick, which is negligible. The real cost is the **second dynamic navmesh** (build at load and tile
  rebuilds), limited by the per-agent bounds volumes. The 8 Cascade emitters are cheap, but a Niagara rewrite is a Gemini request in TANDEM
  (VFX ownership). The 4K textures are a cook-size item (QA R11).

## 4. Phased plan

| Phase | Content | Size |
|---|---|---|
| P0 Setup | Vehicle nav agent + Default re-declared (verify all smokes unchanged), `UNavArea_NoVehicle` + filter, `BP_Vehicle_BTR82` copy + reparent + hull box, level JSON flag `"vehicles_enabled"` | S |
| P1 MVP (exploration) | `AVehiclePawn`, kinematic movement, AI controller, seats / board / exit / blocked exits, drive by click, pause planning, camera follow, cover exception + eviction, nav carve on stop, save / load, commands + events, pure `VehicleRules` tests (exit pick, seat assign, steering clamp, footprint cells), `VehicleSmoke` on a test map | L |
| P2 Turret, real time | Gunner seat, auto-fire by posture, priority target, ammo, FX / tracers / damage numbers, ABP aim | M |
| P3 Turn-based | Vehicle grid occupant (footprint, enlarged grid); MVP-TB = immobile emplacement (turret 3 AP, dismount 2 AP to a free exit cell); later grid driving (1 AP per cell straight, 2 AP per 45° turn, footprint check per step) | M (immobile) / L (mobile) |
| P4 Damage | Armour profile, damage proxy for the crew, enemy targeting of the vehicle, destruction, crew ejection, wreck, save | M |
| P5 Tutorial + cold | Tutorial beats (board → drive → turret → disembark) through the step JSON + events, sheltered-cold rule, F1 texts | S–M |

## 5. Risks

1. Adding `SupportedAgents` changes nav-agent selection globally; if the Default values drift, every smoke regresses. Gate it with the full smoke suite.
2. The mounted state leaks into the 278 `GetMembers()` users. Mitigation: co-location plus the damage proxy, a short audited list of filters, and a smoke where enemies attack the parked APC.
3. Path quality for a 7.8 m hull: a cylinder agent does not model length, so the hull can clip corners in turns. Mitigation: a larger radius, the forward sweep with
   stop-and-replan, and designer `NoVehicle` volumes at tight spots.
4. The cover click breaks if the Pawn exception is forgotten. The FrozenLake APC is a `SkeletalMeshActor` that does not affect nav, so operatives may path under its hull today. Replacing it with the BP fixes that.
5. The vendor BP is UE 4.26 with Cascade FX. Reparent a copy, never the original.
6. Turn-based grid size and centring with a 6 × 2 occupant.
7. Balance: speed, armour and turret damage can trivialise waves. Gate this with per-map enablement and NoVehicle areas, as the user asked.

## 6. Questions for the user

1. Demo 01: the beat sheet's APC is **stranded and siphoned for diesel** (quest chain canister → APC → generator). Is this the drivable
   one (for example, refuel it instead of siphoning, then drive), or a separate wreck? Do we need fuel at all?
2. Who drives / shoots: is any operative allowed, or is there a role preference (Engineer drives, Commander guns)? Can the vehicle move without a driver seated?
3. Can the vehicle run over or knock down enemies, and can hounds damage it (0, or chip damage)?
4. Turn-based: is a parked emplacement (turret only) enough for the first version, or must it drive on the grid?
5. Key combo: is `Ctrl+B` acceptable for board / disembark?

Выполнено моделью: Claude Opus 5.5 (claude-opus-5-5)
