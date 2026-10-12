// Dev-only headless check of the APC's combat (phase P2 turret + the 2026-10-12 run-over / ram / wheel requests) on
// L_MovementTest, on a flat test area the smoke builds 100 m under the map's layout (floor + a NavMeshBoundsVolume copy
// tagged "VehicleNav"; nothing is saved):
//   Scripts/smoke.ps1 -Command CodexTactics.VehicleCombatSmoke
//  0. Engineer (driver) + Commander (gunner) aboard at once; the vendor rig: wheel radius from the wheel bone, turret yaw /
//     gun elevation signs (the muzzle socket follows the aim);
//  1. turret, Aggressive: a hound 25 m ahead dies to the MG (kill credit = the commander, VehicleTurretKill, corpse);
//     a frostbitten off to the side too; the MG heats;
//  2. dead zone: a hound 1.5 m off the hull side is never fired at (even as the Ctrl+click priority target);
//  3. a Brute at 30 m gets the 30 mm cannon (shells decrease);
//  4. a hound that will not die: the MG overheats, falls silent, cools down; it is never staggered (user decision 2026-10-12:
//     the MG only deals damage, no suppression stagger);
//  5. Passive gunner, rush through a pack of 3 hounds: all 3 crushed (dead, corpses, kill credit = the driver, 3
//     VehicleRunOver), the drive goes on; wheels: 0 while parked, the ABP's wheel spin matches the ground speed (no slip),
//     rolls forward;
//  6. a Brute hit at full speed: knocked down with the slower clips (play rate 0.65, longer downed), alive, the APC stops
//     short of him; 7. a Brute met slowly: no knockdown, the APC stops; 8. an operative in the path: the APC brakes, he
//     lives.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Combat/KnockdownRules.h"
#include "Combat/WaveSubsystem.h"
#include "Combat/WaveVictorySubsystem.h"
#include "Components/BrushComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/PlayerCommands.h"
#include "Debug/EventBusProbe.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Subsystems/CodexEventBus.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"
#include "Vehicles/VehiclePawn.h"
#include "Vehicles/VehicleRules.h"
#include "Vehicles/VehicleSubsystem.h"
#include "Vehicles/VehicleTurretComponent.h"

namespace VehicleCombatSmoke
{
	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector O = FVector::ZeroVector;
		TWeakObjectPtr<AOperativeCharacter> Commander;
		TWeakObjectPtr<AOperativeCharacter> Engineer;
		TWeakObjectPtr<AOperativeCharacter> Medic;
		TWeakObjectPtr<AVehiclePawn> Apc;
		TArray<TWeakObjectPtr<AActor>> Spawned;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Targets;
		TStrongObjectPtr<UEventBusProbe> Probe;
		FVector MuzzleAtZero = FVector::ZeroVector;
		float MuzzleYawAtZero = 0.f;
		int32 CommanderKillsBefore = 0;
		int32 EngineerKillsBefore = 0;
		int32 ShotsBefore = 0;
		int32 ShellsBefore = 0;
		int32 RoundsBefore = 0;
		float HeatSeen = 0.f;
		float HeatAtOverheat = 0.f;
		float MaxSpeed = 0.f;
		// Wheel sampling (stage 5).
		FQuat LastWheelQuat = FQuat::Identity;
		bool bHasOffset = false;
		double MeasuredSpinDeg = 0.0;
		double ExpectedSpinDeg = 0.0;
		int32 WheelSamples = 0;
		int32 RollForward = 0;
		int32 RollBackward = 0;
		FVector LastWheelPoint = FVector::ZeroVector;
		FVector LastWheelCentre = FVector::ZeroVector;
		bool bHasWheelPoint = false;
		bool bSpinWhileStopped = false;
		bool bProportional = true;
		bool bPassedBrute = false;
		bool bStaggerSeen = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		State.Probe.Reset();
		FPlatformMisc::RequestExit(false, TEXT("VehicleCombatSmoke"));
		return false;
	}

	void NextStage(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	AStaticMeshActor* SpawnBlock(FState& State, UWorld* World, const FVector& Centre, const FVector& SizeCm)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FTransform Transform(FRotator::ZeroRotator, Centre, SizeCm / 100.f);
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(Transform);
		State.Spawned.Add(Block);
		return Block;
	}

	AEnemyCharacter* SpawnHeld(FState& State, UWorld* World, EEnemyArchetype Archetype, const FVector& Local, float Health = -1.f)
	{
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		AEnemyCharacter* Enemy = Waves ? Waves->SpawnEnemy(Archetype, State.O + Local + FVector(0.f, 0.f, 130.f), FRotator(0.f, 180.f, 0.f)) : nullptr;
		if (Enemy)
		{
			Enemy->SetTurnBasedHeld(true); // stands where it was put (the smoke drives / aims at fixed spots)
			if (Health > 0.f && Enemy->GetHealthComponent())
			{
				Enemy->GetHealthComponent()->SetMaxHealth(Health);
			}
			State.Spawned.Add(Enemy);
		}
		return Enemy;
	}

	bool IsDead(const AEnemyCharacter* Enemy)
	{
		return !Enemy || Enemy->IsDying() || (Enemy->GetHealthComponent() && !Enemy->GetHealthComponent()->IsAlive());
	}

	int32 TotalKills(const FMemberKillStats& Stats)
	{
		return Stats.Total;
	}

	const FSquadKillStats* KillStats(UWorld* World)
	{
		const UWaveVictorySubsystem* Victory = World->GetSubsystem<UWaveVictorySubsystem>();
		return Victory ? &Victory->GetKillStats() : nullptr;
	}

	void Clear(FState& State)
	{
		for (const TWeakObjectPtr<AEnemyCharacter>& Each : State.Targets)
		{
			if (AEnemyCharacter* Enemy = Each.Get(); Enemy && !Enemy->IsDying())
			{
				Enemy->Destroy();
			}
		}
		State.Targets.Reset();
	}

	float MuzzleYaw(const AVehiclePawn* Apc)
	{
		const FVector Muzzle = Apc->Turret->GetMuzzleLocation();
		const FName TurretBone(TEXT("turret_jnt"));
		const FVector From = Apc->Mesh && Apc->Mesh->GetBoneIndex(TurretBone) != INDEX_NONE ? Apc->Mesh->GetBoneLocation(TurretBone) : Apc->GetHullCentreWorld();
		return FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(Muzzle.Y - From.Y, Muzzle.X - From.X)) - Apc->GetHeadingYaw());
	}

	/** Samples the wheels while the hull moves: the ABP's integrated spin vs the no-slip spin, and the rolling direction. */
	void SampleWheels(FState& State, AVehiclePawn* Apc, float Dt)
	{
		const float Ground = Apc->GetGroundSpeed();
		// Exactly 0 while the hull stands; proportional while it moves.
		if (FMath::Abs(Ground) < 1.f)
		{
			State.bSpinWhileStopped |= !FMath::IsNearlyZero(Apc->GetWheelSpinRate());
		}
		else
		{
			const float Expected = VehicleRules::GetWheelSpinDegPerSec(Ground, Apc->DriveConfig.WheelRadiusCm) * Apc->AnimWheelSpinSign;
			State.bProportional &= FMath::IsNearlyEqual(Apc->GetWheelSpinRate(), Expected, FMath::Abs(Expected) * 0.02f + 0.01f);
		}
	}

	/** Every frame while the drive of stage 11 runs: how far the wheel bone really turned vs the no-slip spin. */
	bool SampleFrame(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		UWorld* World = WeakWorld.Get();
		AVehiclePawn* Apc = State.Apc.Get();
		if (!World || State.Stage > 11)
		{
			return State.Stage <= 11 && World != nullptr;
		}
		if (State.Stage != 11 || !Apc || !Apc->Mesh)
		{
			return true;
		}
		const FName SpinBone(TEXT("lf_wheel_3_jnt"));
		if (Apc->Mesh->GetBoneIndex(SpinBone) == INDEX_NONE)
		{
			return true;
		}
		const FQuat Relative = Apc->Mesh->GetComponentQuat().Inverse() * Apc->Mesh->GetBoneQuaternion(SpinBone);
		const float Ground = Apc->GetGroundSpeed();
		const float Dt = World->GetDeltaSeconds();
		if (State.bHasOffset && FMath::Abs(Ground) > 150.f && Dt > 0.f)
		{
			float Angle = FMath::RadiansToDegrees((State.LastWheelQuat.Inverse() * Relative).GetAngle());
			Angle = Angle > 180.f ? 360.f - Angle : Angle;
			const float Expected = FMath::Abs(VehicleRules::GetWheelSpinDegPerSec(Ground, Apc->DriveConfig.WheelRadiusCm)) * Dt;
			if (State.WheelSamples < 6 || State.WheelSamples % 40 == 0)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[VehicleCombatSmoke] wheel frame: dt %.4f ground %.0f cm/s, bone turned %.2f deg, no-slip %.2f deg, spin %.1f deg/s"),
					Dt, Ground, Angle, Expected, Apc->GetWheelSpinRate());
			}
			State.MeasuredSpinDeg += Angle;
			State.ExpectedSpinDeg += Expected;
			++State.WheelSamples;
		}
		// Rolling direction: a point fixed on the front-left wheel moves forward at the top, backward at the bottom.
		const FName Bone(TEXT("lf_wheel_2_jnt"));
		if (Apc->Mesh && Apc->Mesh->GetBoneIndex(Bone) != INDEX_NONE)
		{
			const FTransform Wheel = Apc->Mesh->GetBoneTransform(Apc->Mesh->GetBoneIndex(Bone));
			const FVector Point = Wheel.TransformPosition(FVector(25.f, 25.f, 25.f));
			const FVector Centre = Wheel.GetLocation();
			if (State.bHasWheelPoint && FMath::Abs(Ground) > 150.f)
			{
				const FVector Heading(FMath::Cos(FMath::DegreesToRadians(Apc->GetHeadingYaw())), FMath::Sin(FMath::DegreesToRadians(Apc->GetHeadingYaw())), 0.f);
				const FVector FromAxle = (Point - Centre);
				const float Along = FVector::DotProduct((Point - State.LastWheelPoint) - (Centre - State.LastWheelCentre), Heading);
				const float Up = FromAxle.Z;
				if (FMath::Abs(Up) > 5.f && FMath::Abs(Along) > 0.5f)
				{
					// Forward roll: the part above the axle moves forward relative to the axle.
					const bool bForwardRoll = (Along > 0.f) == (Up > 0.f);
					(bForwardRoll == (Ground > 0.f) ? State.RollForward : State.RollBackward) += 1;
				}
			}
			State.LastWheelPoint = Point;
			State.LastWheelCentre = Centre;
			State.bHasWheelPoint = true;
		}
		State.LastWheelQuat = Relative;
		State.bHasOffset = true;
		return true;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		const float Dt = 0.1f;
		State.Time += Dt;
		State.StageTime += Dt;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UVehicleSubsystem* Vehicles = World->GetSubsystem<UVehicleSubsystem>();
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		AVehiclePawn* Apc = State.Apc.Get();
		UVehicleTurretComponent* Turret = Apc ? Apc->Turret.Get() : nullptr;
		if (State.Time > 360.f)
		{
			Check(State, false, FString::Printf(TEXT("timed out in stage %d"), State.Stage));
			return Finish(State);
		}
		if (Turret)
		{
			State.HeatSeen = FMath::Max(State.HeatSeen, Turret->GetMgHeat());
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				State.Commander = Member->SquadRole == EOperativeRole::Commander ? Member : State.Commander.Get();
				State.Engineer = Member->SquadRole == EOperativeRole::Engineer ? Member : State.Engineer.Get();
				State.Medic = Member->SquadRole == EOperativeRole::MedicSapper ? Member : State.Medic.Get();
			}
			if (!Vehicles || !Nav || !State.Commander.IsValid() || !State.Engineer.IsValid() || !State.Medic.IsValid())
			{
				Check(State, false, TEXT("vehicle subsystem, nav system and commander / engineer / medic"));
				return Finish(State);
			}
			State.Probe.Reset(NewObject<UEventBusProbe>());
			State.Probe->ListenGameplayEvents(UCodexEventBus::Get(World));
			const FVector Layout = SmokeUtils::LayoutTransform(World).GetLocation();
			State.O = FVector(Layout.X, Layout.Y, -10000.f);
			SpawnBlock(State, World, State.O + FVector(0.f, 0.f, -10.f), FVector(10000.f, 10000.f, 20.f));
			ANavMeshBoundsVolume* MapVolume = nullptr;
			for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
			{
				MapVolume = MapVolume ? MapVolume : *It;
			}
			if (MapVolume)
			{
				FActorSpawnParameters Params;
				Params.Template = MapVolume;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				if (ANavMeshBoundsVolume* Volume = World->SpawnActor<ANavMeshBoundsVolume>(MapVolume->GetClass(), State.O + FVector(0.f, 0.f, 200.f), FRotator::ZeroRotator, Params))
				{
					Volume->SetActorRotation(FRotator::ZeroRotator);
					Volume->SetActorLocation(State.O + FVector(0.f, 0.f, 200.f));
					Volume->GetBrushComponent()->UpdateBounds();
					const FVector Extent = Volume->GetBrushComponent()->Bounds.BoxExtent;
					const FVector Scale = Volume->GetActorScale3D();
					Volume->SetActorScale3D(FVector(Scale.X * 5000.f / FMath::Max(1.0, Extent.X), Scale.Y * 5000.f / FMath::Max(1.0, Extent.Y),
						Scale.Z * 500.f / FMath::Max(1.0, Extent.Z)));
					Volume->SupportedAgents = FNavAgentSelector();
					Vehicles->EnableVehicleNavigation(Volume);
					State.Spawned.Add(Volume);
				}
			}
			UClass* ApcClass = LoadClass<AVehiclePawn>(nullptr, TEXT("/Game/Vehicles/BP_APC_BTR82.BP_APC_BTR82_C"));
			FActorSpawnParameters ApcParams;
			ApcParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Apc = World->SpawnActor<AVehiclePawn>(ApcClass ? ApcClass : AVehiclePawn::StaticClass(), State.O + FVector(-2500.f, 0.f, 5.f), FRotator::ZeroRotator, ApcParams);
			State.Apc = Apc;
			if (!Apc || !Apc->Turret)
			{
				Check(State, false, TEXT("APC with a turret spawned"));
				return Finish(State);
			}
			Apc->RestoreTransform(State.O + FVector(-2500.f, 0.f, 0.f), 0.f);
			for (AOperativeCharacter* Member : { State.Commander.Get(), State.Engineer.Get(), State.Medic.Get() })
			{
				Member->StopOperative();
			}
			State.Medic->TeleportTo(State.O + FVector(-2500.f, -3500.f, 100.f), FRotator::ZeroRotator, false, true);
			State.Engineer->TeleportTo(State.O + FVector(-2500.f, -500.f, 100.f), FRotator::ZeroRotator, false, true);
			State.Commander->TeleportTo(State.O + FVector(-2500.f, 500.f, 100.f), FRotator::ZeroRotator, false, true);
			Vehicles->MountImmediately(State.Engineer.Get(), Apc);
			Vehicles->MountImmediately(State.Commander.Get(), Apc);
			Squad->SetLeader(State.Commander.Get());
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			Check(State, Apc->GetSeatOf(State.Engineer.Get()) == EVehicleSeat::Driver && Apc->Turret->GetGunner() == State.Commander.Get(),
				TEXT("Engineer drives, Commander mans the turret"));
			// The vendor rig: the wheel bone's height over the ground = the wheel radius the spin uses.
			const FName Wheel(TEXT("lf_wheel_2_jnt"));
			if (Apc->Mesh && Apc->Mesh->GetBoneIndex(Wheel) != INDEX_NONE)
			{
				const float Height = Apc->Mesh->GetBoneLocation(Wheel).Z - Apc->GetActorLocation().Z;
				UE_LOG(LogCodexTactics, Display, TEXT("[VehicleCombatSmoke] wheel bone %s %.1f cm above the ground (WheelRadiusCm %.1f)"), *Wheel.ToString(), Height,
					Apc->DriveConfig.WheelRadiusCm);
				Check(State, FMath::Abs(Height - Apc->DriveConfig.WheelRadiusCm) < 12.f, FString::Printf(TEXT("wheel radius %.0f cm matches the rig's wheel centre height %.0f cm"),
					Apc->DriveConfig.WheelRadiusCm, Height));
			}
			Apc->Turret->SetAimImmediate(0.f, 0.f);
			NextStage(State, 1);
			return true;
		}
		case 1:
		{
			ANavigationData* VehicleNav = Apc->GetVehicleNavData();
			FNavLocation OnVehicle;
			const bool bReady = State.StageTime > 2.f && !Nav->IsNavigationBuildInProgress() && VehicleNav
				&& VehicleNav->ProjectPoint(State.O + FVector(500.f, 0.f, 0.f), OnVehicle, FVector(100.f, 100.f, 300.f));
			if (!bReady && State.StageTime < 60.f)
			{
				return true;
			}
			Check(State, bReady, TEXT("vehicle navmesh on the test area"));
			Check(State, Apc->Turret->GetShellsMax() > 0 && Apc->Turret->GetShellsLeft() == Apc->Turret->GetShellsMax(),
				FString::Printf(TEXT("shells per map %d / %d (weapons_tuning.json / level JSON)"), Apc->Turret->GetShellsLeft(), Apc->Turret->GetShellsMax()));
			State.MuzzleAtZero = Apc->Turret->GetMuzzleLocation();
			State.MuzzleYawAtZero = MuzzleYaw(Apc);
			Check(State, !State.MuzzleAtZero.IsNearlyZero() && FMath::Abs(State.MuzzleYawAtZero) < 25.f,
				FString::Printf(TEXT("the muzzle socket points forward at aim 0 (yaw %.1f)"), State.MuzzleYawAtZero));
			Apc->Turret->SetAimImmediate(40.f, 0.f);
			NextStage(State, 2);
			return true;
		}
		case 2:
		{
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			const float Yaw = MuzzleYaw(Apc);
			Check(State, FMath::Abs(FMath::FindDeltaAngleDegrees(State.MuzzleYawAtZero + 40.f, Yaw)) < 12.f,
				FString::Printf(TEXT("turret yaw +40 turns the muzzle to %.1f deg (ABP TurretHorizontalAngle sign %.0f)"), Yaw, Apc->Turret->AnimTurretYawSign));
			Apc->Turret->SetAimImmediate(0.f, 15.f);
			NextStage(State, 3);
			return true;
		}
		case 3:
		{
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			const float Rise = Apc->Turret->GetMuzzleLocation().Z - State.MuzzleAtZero.Z;
			Check(State, Rise > 5.f, FString::Printf(TEXT("gun elevation +15 raises the muzzle (%.1f cm; ABP GunElevation sign %.0f)"), Rise, Apc->Turret->AnimGunPitchSign));
			Apc->Turret->SetAimImmediate(0.f, 0.f);
			if (const FSquadKillStats* Stats = KillStats(World))
			{
				State.CommanderKillsBefore = TotalKills(Stats->Commander);
			}
			State.Targets = { SpawnHeld(State, World, EEnemyArchetype::FrostHound, FVector(0.f, 0.f, 0.f)) };
			NextStage(State, 4);
			return true;
		}
		case 4:
		{
			AEnemyCharacter* Hound = State.Targets.Num() > 0 ? State.Targets[0].Get() : nullptr;
			if (!IsDead(Hound) && State.StageTime < 20.f)
			{
				return true;
			}
			const FSquadKillStats* Stats = KillStats(World);
			Check(State, IsDead(Hound), FString::Printf(TEXT("the turret killed the hound 25 m ahead (%.1f s, MG %d rounds, cannon %d)"), State.StageTime,
				Turret->GetShotsFired(EVehicleTurretWeapon::MachineGun), Turret->GetShotsFired(EVehicleTurretWeapon::Cannon)));
			Check(State, Turret->GetShotsFired(EVehicleTurretWeapon::MachineGun) > 0, TEXT("a hound gets the coaxial MG"));
			Check(State, State.Probe->CountEvent(GameplayEvents::VehicleTurretKill) == 1 && State.Probe->CountEvent(GameplayEvents::VehicleTurretFired) >= 1,
				FString::Printf(TEXT("VehicleTurretFired (%d) + VehicleTurretKill (%d)"), State.Probe->CountEvent(GameplayEvents::VehicleTurretFired),
					State.Probe->CountEvent(GameplayEvents::VehicleTurretKill)));
			Check(State, Stats && TotalKills(Stats->Commander) == State.CommanderKillsBefore + 1, TEXT("kill credit: the gunner (commander)"));
			Check(State, Hound != nullptr && IsValid(Hound) && Hound->IsDying(), TEXT("a corpse (dying actor) stays"));
			Check(State, State.HeatSeen > 0.f, FString::Printf(TEXT("the MG heats while it fires (max %.0f %%)"), State.HeatSeen * 100.f));
			State.Targets = { SpawnHeld(State, World, EEnemyArchetype::Frostbitten, FVector(-700.f, 1300.f, 0.f)) };
			NextStage(State, 5);
			return true;
		}
		case 5:
		{
			AEnemyCharacter* Frostbitten = State.Targets.Num() > 0 ? State.Targets[0].Get() : nullptr;
			if (!IsDead(Frostbitten) && State.StageTime < 20.f)
			{
				return true;
			}
			Check(State, IsDead(Frostbitten), FString::Printf(TEXT("the turret killed a frostbitten off to the side (%.1f s)"), State.StageTime));
			// Dead zone: a hound 1.5 m off the hull side.
			State.ShotsBefore = Turret->GetShotsFired(EVehicleTurretWeapon::MachineGun) + Turret->GetShotsFired(EVehicleTurretWeapon::Cannon);
			AEnemyCharacter* Close = SpawnHeld(State, World, EEnemyArchetype::FrostHound, FVector(-2442.f, -(153.f + 150.f), 0.f));
			State.Targets = { Close };
			NextStage(State, 6);
			return true;
		}
		case 6:
		{
			AEnemyCharacter* Close = State.Targets.Num() > 0 ? State.Targets[0].Get() : nullptr;
			if (State.StageTime > 1.5f && State.StageTime < 1.65f && Close)
			{
				Vehicles->HandleTurretTargetClick(State.Commander.Get(), Close); // Ctrl + click: still out of reach
			}
			if (State.StageTime < 4.f)
			{
				return true;
			}
			const int32 Shots = Turret->GetShotsFired(EVehicleTurretWeapon::MachineGun) + Turret->GetShotsFired(EVehicleTurretWeapon::Cannon);
			Check(State, Close && !IsDead(Close) && Shots == State.ShotsBefore && Turret->GetDeadZoneRefusals() > 0,
				FString::Printf(TEXT("dead zone: a hound %.1f m off the hull is never fired at (%d shots, %d refusals)"),
					Close ? Apc->GetHullDistance2D(Close->GetActorLocation()) / 100.f : -1.f, Shots - State.ShotsBefore, Turret->GetDeadZoneRefusals()));
			Clear(State);
			Turret->SetPriorityTarget(nullptr);
			State.ShellsBefore = Turret->GetShellsLeft();
			State.Targets = { SpawnHeld(State, World, EEnemyArchetype::Brute, FVector(500.f, 0.f, 0.f)) };
			NextStage(State, 7);
			return true;
		}
		case 7:
		{
			AEnemyCharacter* Brute = State.Targets.Num() > 0 ? State.Targets[0].Get() : nullptr;
			if (Turret->GetShellsLeft() >= State.ShellsBefore && State.StageTime < 15.f)
			{
				return true;
			}
			if (!IsDead(Brute) && State.StageTime < 12.f)
			{
				return true; // a few bursts
			}
			Check(State, Turret->GetShellsLeft() < State.ShellsBefore && Turret->GetShotsFired(EVehicleTurretWeapon::Cannon) > 0,
				FString::Printf(TEXT("a Brute at 30 m gets the 30 mm cannon: shells %d -> %d"), State.ShellsBefore, Turret->GetShellsLeft()));
			Check(State, Brute && (IsDead(Brute) || Brute->GetHealthComponent()->GetCurrentHealth() < Brute->GetHealthComponent()->GetMaxHealth()),
				FString::Printf(TEXT("the shells hurt the Brute (dead %d)"), IsDead(Brute) ? 1 : 0));
			Clear(State);
			State.Targets = { SpawnHeld(State, World, EEnemyArchetype::FrostHound, FVector(-500.f, 0.f, 0.f), 1000000.f) };
			NextStage(State, 8);
			return true;
		}
		case 8:
		{
			if (const AEnemyCharacter* Tough = State.Targets.Num() > 0 ? State.Targets[0].Get() : nullptr; Tough && Tough->GetHealthComponent())
			{
				State.bStaggerSeen |= Tough->GetHealthComponent()->HasStatusEffect(EStatusEffect::Stagger);
			}
			if (!Turret->IsMgOverheated() && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, Turret->IsMgOverheated(), FString::Printf(TEXT("continuous fire overheats the MG (%.1f s)"), State.StageTime));
			Check(State, !State.bStaggerSeen, FString::Printf(TEXT("the MG only deals damage: the hound was never staggered (%d rounds)"),
				Turret->GetShotsFired(EVehicleTurretWeapon::MachineGun)));
			State.RoundsBefore = Turret->GetShotsFired(EVehicleTurretWeapon::MachineGun);
			State.HeatAtOverheat = Turret->GetMgHeat();
			NextStage(State, 9);
			return true;
		}
		case 9:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, Turret->GetShotsFired(EVehicleTurretWeapon::MachineGun) == State.RoundsBefore, TEXT("an overheated MG stays silent"));
			Check(State, Turret->GetMgHeat() < State.HeatAtOverheat, FString::Printf(TEXT("it cools down (%.0f %% -> %.0f %%)"), State.HeatAtOverheat * 100.f,
				Turret->GetMgHeat() * 100.f));
			Clear(State);
			// Run over: the gunner holds fire (Passive), three hounds in the path.
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			if (const FSquadKillStats* Stats = KillStats(World))
			{
				State.EngineerKillsBefore = TotalKills(Stats->Engineer);
			}
			State.Targets = { SpawnHeld(State, World, EEnemyArchetype::FrostHound, FVector(-700.f, -60.f, 0.f)),
				SpawnHeld(State, World, EEnemyArchetype::FrostHound, FVector(-100.f, 70.f, 0.f)),
				SpawnHeld(State, World, EEnemyArchetype::FrostHound, FVector(500.f, 0.f, 0.f)) };
			NextStage(State, 10);
			return true;
		}
		case 10:
		{
			SampleWheels(State, Apc, Dt);
			if (State.StageTime < 1.f)
			{
				return true; // parked: the wheels must not spin
			}
			Check(State, Vehicles->HandleDriveClick(State.Commander.Get(), State.O + FVector(2500.f, 0.f, 0.f), true) && Apc->IsDriving(), TEXT("full speed through the pack"));
			NextStage(State, 11);
			return true;
		}
		case 11:
		{
			SampleWheels(State, Apc, Dt);
			State.MaxSpeed = FMath::Max(State.MaxSpeed, FMath::Abs(Apc->GetSpeed()));
			if (Apc->IsDriving() && State.StageTime < 30.f)
			{
				return true;
			}
			int32 Dead = 0;
			int32 Corpses = 0;
			for (const TWeakObjectPtr<AEnemyCharacter>& Each : State.Targets)
			{
				Dead += IsDead(Each.Get()) ? 1 : 0;
				Corpses += Each.IsValid() && Each->IsDying() ? 1 : 0;
			}
			const FSquadKillStats* Stats = KillStats(World);
			Check(State, Apc->GetCrushCount() == 3 && Dead == 3, FString::Printf(TEXT("all 3 hounds run over (crushed %d, dead %d, top speed %.0f cm/s)"),
				Apc->GetCrushCount(), Dead, State.MaxSpeed));
			Check(State, Corpses == 3, FString::Printf(TEXT("3 corpses beside the track (%d)"), Corpses));
			Check(State, State.Probe->CountEvent(GameplayEvents::VehicleRunOver) == 3, FString::Printf(TEXT("3 VehicleRunOver events (%d)"),
				State.Probe->CountEvent(GameplayEvents::VehicleRunOver)));
			Check(State, Stats && TotalKills(Stats->Engineer) == State.EngineerKillsBefore + 3, TEXT("kill credit: the driver (engineer)"));
			Check(State, FVector::Dist2D(Apc->GetActorLocation(), State.O + FVector(2500.f, 0.f, 0.f)) < 400.f,
				FString::Printf(TEXT("no braking for them: arrived (%.1f m off)"), FVector::Dist2D(Apc->GetActorLocation(), State.O + FVector(2500.f, 0.f, 0.f)) / 100.f));
			// Wheels (user report 2026-10-12): no spin while standing, spin = ground speed / radius, rolls forward.
			Check(State, !State.bSpinWhileStopped, TEXT("wheel spin 0 while the hull stands"));
			Check(State, State.bProportional, TEXT("wheel spin proportional to the measured ground speed"));
			const double Ratio = State.ExpectedSpinDeg > 1.0 ? State.MeasuredSpinDeg / State.ExpectedSpinDeg : 0.0;
			UE_LOG(LogCodexTactics, Display, TEXT("[VehicleCombatSmoke] wheel spin: bone %.0f deg vs no-slip %.0f deg over %d frames (ratio %.3f)"),
				State.MeasuredSpinDeg, State.ExpectedSpinDeg, State.WheelSamples, Ratio);
			Check(State, State.WheelSamples > 30 && Ratio > 0.8 && Ratio < 1.25, FString::Printf(TEXT("tread speed matches the hull, also while it speeds up / brakes (bone spin / no-slip spin %.2f)"), Ratio));
			Check(State, State.RollForward > State.RollBackward * 3, FString::Printf(TEXT("the wheels roll the right way (%d / %d samples)"), State.RollForward, State.RollBackward));
			Clear(State);
			// A Brute met at full speed.
			Apc->RestoreTransform(State.O + FVector(-2500.f, 0.f, 0.f), 0.f);
			State.Targets = { SpawnHeld(State, World, EEnemyArchetype::Brute, FVector(0.f, 0.f, 0.f)) };
			NextStage(State, 12);
			return true;
		}
		case 12:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Vehicles->HandleDriveClick(State.Commander.Get(), State.O + FVector(2500.f, 0.f, 0.f), true);
			State.MaxSpeed = 0.f;
			NextStage(State, 13);
			return true;
		}
		case 13:
		{
			AEnemyCharacter* Brute = State.Targets.Num() > 0 ? State.Targets[0].Get() : nullptr;
			State.MaxSpeed = FMath::Max(State.MaxSpeed, FMath::Abs(Apc->GetSpeed()));
			State.bPassedBrute |= Brute && Apc->IsInsideFootprint(Brute->GetActorLocation(), -40.f);
			if (Apc->GetRamKnockdownCount() == 0 && Apc->IsDriving() && State.StageTime < 20.f)
			{
				return true;
			}
			if (State.StageTime < 3.f && Apc->GetRamKnockdownCount() > 0 && !FMath::IsNearlyZero(Apc->GetSpeed(), 5.f))
			{
				return true; // let it brake to a stop
			}
			const UKnockdownComponent* Knockdown = Brute ? Brute->FindComponentByClass<UKnockdownComponent>() : nullptr;
			Check(State, Apc->GetRamKnockdownCount() == 1 && Brute && Brute->IsKnockedDown() && !IsDead(Brute),
				FString::Printf(TEXT("a Brute hit at %.0f cm/s is knocked down and alive"), State.MaxSpeed));
			Check(State, Knockdown && FMath::IsNearlyEqual(Knockdown->GetFallPlayRate(), Apc->CrushConfig.RamKnockdownPlayRate, 0.01f)
				&& FMath::IsNearlyEqual(Knockdown->GetGetUpPlayRate(), Apc->CrushConfig.RamKnockdownPlayRate, 0.01f)
				&& Knockdown->GetState().DownedDuration > KnockdownRules::GetConfig().DownedSeconds * 1.2f,
				FString::Printf(TEXT("slower clips: fall at %.2fx, get-up at %.2fx, downed %.2f s"), Knockdown ? Knockdown->GetFallPlayRate() : 0.f,
					Knockdown ? Knockdown->GetGetUpPlayRate() : 0.f, Knockdown ? Knockdown->GetState().DownedDuration : 0.f));
			Check(State, Brute && !State.bPassedBrute && Apc->GetCrushCount() == 3, TEXT("the APC did not drive over the Brute"));
			NextStage(State, 14);
			return true;
		}
		case 14:
		{
			AEnemyCharacter* Brute = State.Targets.Num() > 0 ? State.Targets[0].Get() : nullptr;
			if (Brute && Brute->IsKnockedDown() && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, Brute && !Brute->IsKnockedDown() && !IsDead(Brute), FString::Printf(TEXT("the Brute got up again (%.1f s)"), State.StageTime));
			Apc->StopDriving();
			Clear(State);
			// Slow contact: cruise at 3 m/s into a Brute.
			Apc->RestoreTransform(State.O + FVector(-2500.f, 0.f, 0.f), 0.f);
			Apc->DriveConfig.CruiseSpeedCmS = 300.f;
			State.Targets = { SpawnHeld(State, World, EEnemyArchetype::Brute, FVector(-1300.f, 0.f, 0.f)) };
			NextStage(State, 15);
			return true;
		}
		case 15:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Vehicles->HandleDriveClick(State.Commander.Get(), State.O + FVector(2500.f, 0.f, 0.f), false);
			State.MaxSpeed = 0.f;
			NextStage(State, 16);
			return true;
		}
		case 16:
		{
			State.MaxSpeed = FMath::Max(State.MaxSpeed, FMath::Abs(Apc->GetSpeed()));
			if (State.StageTime < 6.f)
			{
				return true;
			}
			AEnemyCharacter* Brute = State.Targets.Num() > 0 ? State.Targets[0].Get() : nullptr;
			Check(State, Apc->GetRamKnockdownCount() == 1 && Brute && !Brute->IsKnockedDown() && !IsDead(Brute),
				FString::Printf(TEXT("a Brute met at %.0f cm/s: no knockdown"), State.MaxSpeed));
			Check(State, Brute && FMath::Abs(Apc->GetSpeed()) < 10.f && Apc->GetLastBlocker() == Brute && Apc->GetHullDistance2D(Brute->GetActorLocation()) > 0.f,
				FString::Printf(TEXT("the APC stopped short of him (%.1f m off the hull)"), Brute ? Apc->GetHullDistance2D(Brute->GetActorLocation()) / 100.f : -1.f));
			Apc->StopDriving();
			Apc->DriveConfig.CruiseSpeedCmS = 600.f;
			Clear(State);
			// An operative in the path.
			Apc->RestoreTransform(State.O + FVector(-2500.f, 0.f, 0.f), 0.f);
			State.Medic->StopOperative();
			State.Medic->TeleportTo(State.O + FVector(-800.f, 0.f, 100.f), FRotator::ZeroRotator, false, true);
			NextStage(State, 17);
			return true;
		}
		case 17:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Vehicles->HandleDriveClick(State.Commander.Get(), State.O + FVector(2500.f, 0.f, 0.f), true);
			NextStage(State, 18);
			return true;
		}
		case 18:
		{
			AOperativeCharacter* Medic = State.Medic.Get();
			if (State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Medic && Medic->HealthComponent->IsAlive() && !Apc->IsInsideFootprint(Medic->GetActorLocation(), 0.f)
				&& FMath::Abs(Apc->GetSpeed()) < 10.f && Apc->GetLastBlocker() == Medic,
				FString::Printf(TEXT("an operative in the path: the APC braked, he lives (%.1f m off the hull)"),
					Medic ? Apc->GetHullDistance2D(Medic->GetActorLocation()) / 100.f : -1.f));
			Apc->StopDriving();
			return Finish(State);
		}
		default:
			return Finish(State);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.1f);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return SampleFrame(WeakWorld, *State);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.VehicleCombatSmoke"),
		TEXT("Dev check of the APC's combat (P2): turret by posture (MG / cannon), dead zone, shells / heat, running over small enemies, ramming a Brute (slow knockdown), braking for operatives, wheel spin = ground speed."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
