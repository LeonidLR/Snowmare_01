# Quick fire: Ctrl + click on a point (game designer request 2026-10-19)

> «A single click makes the squad fire immediately in the chosen direction: one operative, two, or the whole squad —
> whatever is selected — turns toward that point and shoots at it at once.»

The first draft bound it to a right-click; **user decision 2026-10-19: Ctrl + left click on any point**, the targeted shot
staying exactly as it was. UE-only (the Godot archive has no fire at a point). Code: `Combat/QuickFireRules` (pure, unit tests
`CodexTactics.Combat.QuickFire.*`), `AOperativeCharacter` cluster `OperativeCharacterQuickFire.cpp`, controller
`IssueCtrlClick` / `IssueQuickFire` (Orders unit, called from `HandleWorldHit`'s Ctrl branch). Smoke `QuickFireSmoke`.

## What a Ctrl + click does

| Ctrl + click on | Result |
|---|---|
| a live enemy the squad sees, or the ground / anything within **1.5 m** (planar) of one | the targeted shot exactly as before (`IssueTargetedShot`: the leader's priority target; in cover the corner shot) |
| a barrel / a mine / a supply crate / a trapped object | the targeted shot exactly as before (one aimed round: explodes / mine hit chance / remote detonation; an untrapped crate only pierced) |
| a squad mate | the targeted-shot hint, no fire |
| anything else - the ground, a wall, an unseen enemy's body | **quick fire at the point** by the selection |
| (turn-based) | unchanged: the grid attack on the enemy / object there with its AP, an empty spot = the grid's hint |
| (aboard a vehicle) | unchanged: the turret target |

RMB never fires: RMB drag = camera rotation, RMB on the minimap = move order, Shift + RMB = hold the line (all as before).

## Who fires

The selection: the box-selected group, else the operative you control (Solo too). Nobody aboard a vehicle. Each one is
refused on his own with a floating text: enraged, panicking, the knife in hands, a frozen weapon, hands busy (carrying);
dead / knocked down silently. The fire posture does not matter (an explicit order: Passive fires too, Commander Mode's
cover hold too).

## The round

Area fire at the point: each round flies from the stance's muzzle height (round the corner from cover) to the point (stance
spread 1.5 / 1.0 / 0.7 deg, 30 cm past it) but never further than the weapon's `AttackRangeCm` (beyond the range the order
still fires - the rounds fall short). The visibility trace (the squad ignored: no friendly fire) stops it at a wall / the
ground / an object: a **barrel explodes**, a **mine goes off**, a **trapped crate / object detonates** (an untrapped crate is
only pierced). An enemy whose capsule (+ 20 cm) the round crosses first - **seen or not** - is hit with the weapon's chance at
that distance x **0.5** (unaimed fire). So an unseen enemy is never targeted, but firing at the spot where it stands may still
hit it. (`OrderQuickFire` also takes an aimed Enemy for direct calls - then the normal `ShootAtTarget`; the Ctrl + click never
passes one, a visible enemy gets the targeted shot.)

## The burst

The weapon's normal fire: a magazine weapon with a clip of 20+ (M16) fires **3 rounds**, single-shot weapons (sniper rifle,
pistol, shotgun) **1**, at the weapon's own cadence (`FireRate`, M16 0.65 s): repeated clicks cannot beat the normal rate of
fire. Ammo is spent, an empty clip reloads first (the order waits), an empty weapon switches like the automatic fire; cold
misfires apply. The operative stops, turns his body to the point (eased, never slower than `AimTurnRateDegPerSec`) and fires
only once the aim residual is inside his `AimConeDeg`; the formation does not pull him away meanwhile. The **sniper rifle**:
the direct-order rule - she stops and kneels by herself, then fires. A move order cancels the burst; an order that could not
fire for 6 s (cease fire, a reload that never ends, a frozen weapon) is dropped.

**From cover** (as the Ctrl + click targeted shot, `ShootAtObject`): a point round the corner / over a low wall
(`IsCornerShotTarget`) is fired at from the cover pose - the point becomes the threat side (`GatherCoverThreat`), he leans out
(or fires blind by the cover fire mode) and stays in cover; a point in front of the cover: he steps off the wall (the open
shot), fires and walks back to the slot. A high wall without an exposed edge: dropped (no line out).

## Side effects

Every round is a gunshot (`NotifySquadNoise`, the enemies' hearing) and a muzzle flash (`NotifyFired`, demasked): a patrol
that hears it reacts the normal way (detection -> the fight starts through the encounter rules). A hit enemy reports
`EnemyDamaged` as any hit.

## Modes

| Mode | Behaviour |
|---|---|
| Exploration / real-time fight | immediate (only the optional order lock `Codex.RealTimeOrders 0` blocks it, with its hint) |
| Tactical pause | planned (`PlanQuickFire`): a plan marker, the feed line «📋 [PLAN] Fire at the point planned (N)! [SPACE - fire]»; the release orders it first, before the planned targeted shots and moves; a pause left into turn-based drops it |
| Turn-based | Ctrl + click stays the grid attack (its AP); a real-time burst still running when the grid starts is dropped |
| Tutorial | command `Attack.QuickFire` (under `Attack.*`): refused with the usual one-line feed while locked; Demo 01 frees it with `Attack.TargetedShot` (beats A3 / A4) |

## Feedback

A short orange disc at the point (`UCombatFeedbackSubsystem::SpawnFirePing`), the usual tracers / muzzle flash / fire clip per
round, one feed line «Fire!» per order (the operative's name, «SQUAD» for a group). F1 Controls (Combat): «Ctrl+Click - Fire at
the enemy: targeted shot ...» and «Ctrl+Click - Fire at the point: anywhere else the selected operative(s) turn and fire there at
once».
