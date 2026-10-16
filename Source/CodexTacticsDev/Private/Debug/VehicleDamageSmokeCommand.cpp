// Dev-only headless check of the APC hull (phase P4) + the P2 follow-up (Ctrl+click a barrel = one cannon shell) on
// L_MovementTest, on a flat test area the smoke builds 100 m under the map's layout (floor + a NavMeshBoundsVolume copy
// tagged "VehicleNav"; nothing is saved but the smoke's own save slot):
//   Scripts/smoke.ps1 -Command CodexTactics.VehicleDamageSmoke
//  0. Engineer (driver) + Commander (gunner, Passive: holds fire) aboard;
//  1. a hound goes for the manned APC and bites the hull: x0.1 per bite, the crew untouched (VehicleDamaged SmallMelee);
//  2. a Brute's blow: full damage (HeavyMelee);
//  3. a mine ahead: the APC drives over it -> the flat mine damage (250); save, more damage, load -> the hull HP is back;
//  4. a barrel blast 3 m off the hull side: explosion damage;
//  5. 0 HP -> disabled: engine off, no driving, no boarding, turret held, damaged model + smoke, VehicleDisabled, the crew
//     ejected alive into cover at the hull; save / load keeps it disabled;
//  6. Y with the Commander selected: refused (Engineer only); Alt+Y: the Engineer repairs (~5 s) -> running at 50 %,
//     VehicleRepaired, drivable again;
//  7. Ctrl+click a barrel 20 m ahead with the gunner aboard: one shell, the barrel explodes (shells -1); a barrel in the
//     dead zone is refused.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/PlayerCommands.h"
#include "Core/SaveGameSubsystem.h"
#include "Debug/EventBusProbe.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/ProximityMineActor.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Particles/ParticleSystem.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Particles/ParticleSystemComponent.h"
#include "Subsystems/CodexEventBus.h"
#include "UObject/StrongObjectPtr.h"
#include "Vehicles/VehiclePawn.h"
#include "Vehicles/VehicleRules.h"
#include "Vehicles/VehicleSubsystem.h"
#include "Vehicles/VehicleTurretComponent.h"

namespace VehicleDamageSmoke
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
		TWeakObjectPtr<AEnemyCharacter> Enemy;
		TWeakObjectPtr<AActor> Thing;
		TArray<TWeakObjectPtr<AActor>> Spawned;
		TStrongObjectPtr<UEventBusProbe> Probe;
		float HealthBefore = 0.f;
		float FirstHit = -1.f;
		float SavedHealth = 0.f;
		int32 ShellsBefore = 0;
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
		FPlatformMisc::RequestExit(false, TEXT("VehicleDamageSmoke"));
		return false;
	}

	void NextStage(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	void SpawnFloor(FState& State, UWorld* World, const FVector& Centre, const FVector& SizeCm)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FTransform Transform(FRotator::ZeroRotator, Centre, SizeCm / 100.f);
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (!Block)
		{
			return;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(Transform);
		State.Spawned.Add(Block);
	}

	/** An enemy that acts (not held): it goes for the manned APC on its own. */
	AEnemyCharacter* SpawnHunter(FState& State, UWorld* World, EEnemyArchetype Archetype, const FVector& Local)
	{
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		AEnemyCharacter* Enemy = Waves ? Waves->SpawnEnemy(Archetype, State.O + Local + FVector(0.f, 0.f, 130.f), FRotator(0.f, 180.f, 0.f)) : nullptr;
		if (Enemy && Enemy->GetHealthComponent())
		{
			Enemy->GetHealthComponent()->SetMaxHealth(100000.f);
			State.Spawned.Add(Enemy);
		}
		return Enemy;
	}

	ABarrelActor* SpawnBarrel(FState& State, UWorld* World, const FVector& Local)
	{
		const float HalfHeight = GetDefault<ABarrelActor>()->Box->GetUnscaledBoxExtent().Z;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ABarrelActor* Barrel = World->SpawnActor<ABarrelActor>(State.O + Local + FVector(0.f, 0.f, HalfHeight), FRotator::ZeroRotator, Params);
		State.Spawned.Add(Barrel);
		return Barrel;
	}

	bool InCoverAtHull(const AOperativeCharacter* Operative, const AVehiclePawn* Apc)
	{
		return Operative && Operative->bInCover && Operative->GetCoverSlot().WallActor.Get() == Apc;
	}

	void KillEnemy(FState& State)
	{
		if (AEnemyCharacter* Enemy = State.Enemy.Get())
		{
			Enemy->Destroy();
		}
		State.Enemy.Reset();
	}

	USaveGameSubsystem* Saves(UWorld* World)
	{
		USaveGameSubsystem* Subsystem = World->GetSubsystem<USaveGameSubsystem>();
		if (Subsystem && Subsystem->SaveDirectoryOverride.IsEmpty())
		{
			Subsystem->SaveDirectoryOverride = FPaths::ProjectSavedDir() / TEXT("SmokeSaves") / TEXT("VehicleDamageSmoke");
			IFileManager::Get().DeleteDirectory(*Subsystem->SaveDirectoryOverride, false, true);
		}
		return Subsystem;
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
		if (State.Time > 300.f)
		{
			Check(State, false, FString::Printf(TEXT("timed out in stage %d"), State.Stage));
			return Finish(State);
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
			SpawnFloor(State, World, State.O + FVector(0.f, 0.f, -10.f), FVector(10000.f, 10000.f, 20.f));
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
			State.Medic->TeleportTo(State.O + FVector(-2500.f, -4000.f, 100.f), FRotator::ZeroRotator, false, true);
			State.Engineer->TeleportTo(State.O + FVector(-2500.f, -500.f, 100.f), FRotator::ZeroRotator, false, true);
			State.Commander->TeleportTo(State.O + FVector(-2500.f, 500.f, 100.f), FRotator::ZeroRotator, false, true);
			Vehicles->MountImmediately(State.Engineer.Get(), Apc);
			Vehicles->MountImmediately(State.Commander.Get(), Apc);
			Squad->SetLeader(State.Commander.Get());
			Squad->SetSquadPosture(ESquadFirePosture::Passive); // the gunner holds fire: the enemies must reach the hull
			Check(State, Apc->GetHullHealth() == Apc->GetHullMaxHealth() && Apc->GetHullMaxHealth() == 1200.f && !Apc->IsDisabled(),
				FString::Printf(TEXT("a fresh hull: %.0f / %.0f HP (weapons_tuning.json apc_hull)"), Apc->GetHullHealth(), Apc->GetHullMaxHealth()));
			NextStage(State, 1);
			return true;
		}
		case 1:
		{
			ANavigationData* VehicleNav = Apc->GetVehicleNavData();
			FNavLocation OnVehicle;
			FNavLocation OnInfantry;
			const bool bReady = State.StageTime > 2.f && !Nav->IsNavigationBuildInProgress() && VehicleNav
				&& VehicleNav->ProjectPoint(State.O + FVector(500.f, 0.f, 0.f), OnVehicle, FVector(100.f, 100.f, 300.f))
				&& Nav->ProjectPointToNavigation(State.O + FVector(-2500.f, 900.f, 0.f), OnInfantry, FVector(100.f, 100.f, 300.f));
			if (!bReady && State.StageTime < 60.f)
			{
				return true;
			}
			Check(State, bReady, TEXT("vehicle + infantry navmesh on the test area"));
			State.HealthBefore = Apc->GetHullHealth();
			State.Enemy = SpawnHunter(State, World, EEnemyArchetype::FrostHound, FVector(-2500.f, 900.f, 0.f));
			NextStage(State, 2);
			return true;
		}
		case 2:
		{
			// The hound bites the hull.
			if (State.FirstHit < 0.f && Apc->GetDamageTaken(EVehicleDamageSource::SmallMelee) > 0.f)
			{
				State.FirstHit = State.HealthBefore - Apc->GetHullHealth();
			}
			if (Apc->GetDamageTaken(EVehicleDamageSource::SmallMelee) < 3.f && State.StageTime < 25.f)
			{
				return true;
			}
			const AEnemyCharacter* Hound = State.Enemy.Get();
			const float Bite = Hound ? Hound->GetAttackDamage() : 12.f;
			Check(State, State.FirstHit > 0.f && FMath::IsNearlyEqual(State.FirstHit, Bite * 0.1f, 0.01f),
				FString::Printf(TEXT("a hound goes for the manned APC and bites the hull: %.2f per bite (its bite %.0f x0.1)"), State.FirstHit, Bite));
			Check(State, State.Probe->CountEvent(GameplayEvents::VehicleDamaged) >= 1, TEXT("VehicleDamaged raised"));
			Check(State, State.Engineer->HealthComponent->GetCurrentHealth() == State.Engineer->HealthComponent->GetMaxHealth()
				&& State.Commander->HealthComponent->GetCurrentHealth() == State.Commander->HealthComponent->GetMaxHealth()
				&& Vehicles->IsMounted(State.Commander.Get()),
				TEXT("the crew inside is untouched (the hull takes it)"));
			KillEnemy(State);
			State.FirstHit = -1.f;
			State.HealthBefore = Apc->GetHullHealth();
			State.Enemy = SpawnHunter(State, World, EEnemyArchetype::Brute, FVector(-2500.f, -1000.f, 0.f));
			NextStage(State, 3);
			return true;
		}
		case 3:
		{
			if (State.FirstHit < 0.f && Apc->GetDamageTaken(EVehicleDamageSource::HeavyMelee) > 0.f)
			{
				State.FirstHit = State.HealthBefore - Apc->GetHullHealth();
			}
			if (State.FirstHit < 0.f && State.StageTime < 25.f)
			{
				return true;
			}
			const AEnemyCharacter* Brute = State.Enemy.Get();
			const float Blow = Brute ? Brute->GetAttackDamage() : 35.f;
			Check(State, State.FirstHit > 0.f && FMath::IsNearlyEqual(State.FirstHit, Blow, 0.01f),
				FString::Printf(TEXT("a Brute's blow dents the hull in full: %.1f (his blow %.0f)"), State.FirstHit, Blow));
			KillEnemy(State);
			// A level mine 15 m ahead: the APC drives over it.
			const float HalfHeight = GetDefault<AProximityMineActor>()->Box->GetUnscaledBoxExtent().Z;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AProximityMineActor* Mine = World->SpawnActor<AProximityMineActor>(State.O + FVector(-500.f, 0.f, HalfHeight), FRotator::ZeroRotator, Params);
			if (Mine)
			{
				Mine->RestoreSaved(false, true, true, 0.f); // a level mine, armed, seen
				State.Spawned.Add(Mine);
			}
			State.Thing = Mine;
			State.HealthBefore = Apc->GetHullHealth();
			Check(State, Mine && Vehicles->HandleDriveClick(State.Commander.Get(), State.O + FVector(1500.f, 0.f, 0.f), false) && Apc->IsDriving(),
				TEXT("drive across an armed level mine"));
			NextStage(State, 4);
			return true;
		}
		case 4:
		{
			if (Apc->GetDamageTaken(EVehicleDamageSource::Mine) <= 0.f && State.StageTime < 20.f)
			{
				return true;
			}
			Check(State, !State.Thing.IsValid() && FMath::IsNearlyEqual(Apc->GetDamageTaken(EVehicleDamageSource::Mine), 250.f, 0.5f),
				FString::Printf(TEXT("the mine under the wheels: %.0f to the hull (flat 250), the mine gone"), Apc->GetDamageTaken(EVehicleDamageSource::Mine)));
			Check(State, Vehicles->IsMounted(State.Commander.Get()) && State.Commander->HealthComponent->GetCurrentHealth() == State.Commander->HealthComponent->GetMaxHealth(),
				TEXT("the crew rides on unhurt"));
			Apc->StopDriving();
			NextStage(State, 5);
			return true;
		}
		case 5:
		{
			if (!Apc->IsStopped() && State.StageTime < 8.f)
			{
				return true;
			}
			// Save the damaged hull, hurt it more, load: the HP is back.
			State.SavedHealth = Apc->GetHullHealth();
			USaveGameSubsystem* Subsystem = Saves(World);
			Check(State, Subsystem && Subsystem->SaveGame(TEXT("VehicleDamageSmoke")), FString::Printf(TEXT("saved at %.1f HP"), State.SavedHealth));
			Apc->ApplyHullDamage(100.f, EVehicleDamageSource::Other, TEXT("Smoke"));
			Check(State, Subsystem && Subsystem->LoadGame(TEXT("VehicleDamageSmoke")), TEXT("loaded"));
			Check(State, FMath::IsNearlyEqual(Apc->GetHullHealth(), State.SavedHealth, 0.5f) && !Apc->IsDisabled(),
				FString::Printf(TEXT("load: the hull HP is back (%.1f)"), Apc->GetHullHealth()));
			// A barrel blast 3 m off the hull side.
			State.HealthBefore = Apc->GetHullHealth();
			ABarrelActor* Barrel = SpawnBarrel(State, World, FVector(Apc->GetActorLocation().X - State.O.X, 153.f + 300.f, 0.f));
			const bool bBoom = Barrel && AOperativeCharacter::DetonateShotTarget(Barrel, ETargetedShotKind::Barrel, FText::FromString(TEXT("Smoke")));
			Check(State, bBoom && Apc->GetDamageTaken(EVehicleDamageSource::Explosion) > 0.f && Apc->GetHullHealth() < State.HealthBefore,
				FString::Printf(TEXT("a barrel blast 3 m off the side: %.1f explosion damage"), Apc->GetDamageTaken(EVehicleDamageSource::Explosion)));
			// The rest: down to 0.
			Apc->ApplyHullDamage(Apc->GetHullHealth() + 50.f, EVehicleDamageSource::HeavyMelee, TEXT("Smoke"));
			NextStage(State, 6);
			return true;
		}
		case 6:
		{
			AOperativeCharacter* Commander = State.Commander.Get();
			AOperativeCharacter* Engineer = State.Engineer.Get();
			if ((Vehicles->IsMounted(Commander) || Vehicles->IsMounted(Engineer) || Vehicles->IsFadeActive() || !InCoverAtHull(Commander, Apc)
				|| !InCoverAtHull(Engineer, Apc)) && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, Apc->IsDisabled() && Apc->GetHullHealth() == 0.f && State.Probe->CountEvent(GameplayEvents::VehicleDisabled) == 1,
				TEXT("0 HP -> disabled (VehicleDisabled)"));
			Check(State, !Apc->IsEngineRunning() && !Apc->IsDriving(), TEXT("engine off, not driving"));
			Check(State, !Vehicles->IsMounted(Commander) && !Vehicles->IsMounted(Engineer) && Apc->GetCrewCount() == 0,
				TEXT("the crew is ejected"));
			Check(State, Commander->HealthComponent->IsAlive() && Engineer->HealthComponent->IsAlive() && !Commander->IsHidden() && Commander->GetActorEnableCollision(),
				TEXT("alive, visible, colliding"));
			Check(State, InCoverAtHull(Commander, Apc) && InCoverAtHull(Engineer, Apc), TEXT("both in cover at the hull"));
			Check(State, Apc->DriveTo(State.O + FVector(1500.f, 0.f, 0.f), false) == EVehicleDriveResult::Disabled, TEXT("can't drive"));
			Check(State, Vehicles->OrderBoard({ Commander }, Apc) == 0 && !Vehicles->IsBoarding(Commander), TEXT("can't board"));
			Check(State, Apc->DamagedMesh && Apc->DamagedMesh->IsVisible() && Apc->DamagedMesh->GetStaticMesh() && Apc->Mesh && !Apc->Mesh->IsVisible(),
				TEXT("the damaged model replaces the running one"));
			// Headless (-nullrhi) a Cascade emitter never activates: the template is checked there, the activation when rendering.
			Check(State, Apc->DamagedSmoke && Apc->DamagedSmoke->Template && (Apc->DamagedSmoke->IsActive() || !FApp::CanEverRender()),
				FString::Printf(TEXT("the wreck smoke runs (template %s, active %d)"), Apc->DamagedSmoke && Apc->DamagedSmoke->Template ? *Apc->DamagedSmoke->Template->GetName() : TEXT("-"),
					Apc->DamagedSmoke && Apc->DamagedSmoke->IsActive() ? 1 : 0));
			// Save disabled, "repair" by hand, load: disabled again.
			USaveGameSubsystem* Subsystem = Saves(World);
			Check(State, Subsystem && Subsystem->SaveGame(TEXT("VehicleDamageSmoke")), TEXT("saved disabled"));
			Apc->RestoreHull(1200.f, false);
			Check(State, Subsystem && Subsystem->LoadGame(TEXT("VehicleDamageSmoke")), TEXT("loaded"));
			Check(State, Apc->IsDisabled() && Apc->GetHullHealth() == 0.f && Apc->DamagedMesh->IsVisible(), TEXT("load: still disabled"));
			NextStage(State, 7);
			return true;
		}
		case 7:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			// Y with only the Commander selected: refused (the Engineer repairs).
			Squad->SetLeader(State.Commander.Get());
			Check(State, Vehicles->HandleBoardKey(false, Apc) == EVehicleKeyAction::Repair && !Vehicles->IsRepairing(State.Commander.Get())
				&& !Vehicles->IsRepairing(State.Engineer.Get()), TEXT("Y (Commander): refused - only the Engineer repairs"));
			// Alt+Y: the squad - the Engineer goes.
			Check(State, Vehicles->HandleBoardKey(true, Apc) == EVehicleKeyAction::Repair && Vehicles->IsRepairing(State.Engineer.Get()),
				TEXT("Alt+Y: the Engineer repairs"));
			NextStage(State, 8);
			return true;
		}
		case 8:
		{
			if (Apc->IsDisabled() && State.StageTime < 30.f)
			{
				return true;
			}
			Check(State, !Apc->IsDisabled() && FMath::IsNearlyEqual(Apc->GetHullHealth(), 600.f, 0.5f) && State.StageTime >= 4.9f,
				FString::Printf(TEXT("repaired after %.1f s: %.0f HP (50 %%)"), State.StageTime, Apc->GetHullHealth()));
			Check(State, State.Probe->CountEvent(GameplayEvents::VehicleRepaired) == 1, TEXT("VehicleRepaired"));
			Check(State, !Apc->DamagedMesh->IsVisible() && Apc->Mesh->IsVisible() && !Apc->DamagedSmoke->IsActive(), TEXT("the running model is back"));
			Vehicles->MountImmediately(State.Engineer.Get(), Apc);
			Vehicles->MountImmediately(State.Commander.Get(), Apc);
			Squad->SetLeader(State.Commander.Get());
			const EVehicleDriveResult Drive = Apc->DriveTo(State.O + FVector(-1800.f, 0.f, 0.f), false);
			Check(State, Drive == EVehicleDriveResult::Started || Drive == EVehicleDriveResult::StartedPartial, TEXT("drivable again"));
			NextStage(State, 9);
			return true;
		}
		case 9:
		{
			if (Apc->IsDriving() && State.StageTime < 10.f)
			{
				return true;
			}
			if (!Apc->IsStopped() && State.StageTime < 14.f)
			{
				return true;
			}
			// A barrel 20 m ahead of the hull, one in the dead zone (2 m off the side).
			Apc->Turret->SetAimImmediate(0.f, 0.f);
			const FVector Front = Apc->GetHullCentreWorld() + FVector(Apc->HullHalfExtent.X + 2000.f, 0.f, 0.f);
			ABarrelActor* Far = SpawnBarrel(State, World, FVector(Front.X - State.O.X, Front.Y - State.O.Y, 0.f));
			ABarrelActor* Close = SpawnBarrel(State, World, FVector(Apc->GetHullCentreWorld().X - State.O.X, Apc->GetHullCentreWorld().Y - State.O.Y - 153.f - 200.f, 0.f));
			State.ShellsBefore = Apc->Turret->GetShellsLeft();
			Check(State, Vehicles->HandleTurretTargetClick(State.Commander.Get(), Close) && Apc->Turret->GetObjectTarget() == nullptr,
				TEXT("Ctrl+click a barrel in the dead zone: refused"));
			Check(State, Vehicles->HandleTurretTargetClick(State.Commander.Get(), Far) && Apc->Turret->GetObjectTarget() == Far,
				TEXT("Ctrl+click a barrel 20 m ahead: the gunner takes it"));
			State.Thing = Far;
			State.HealthBefore = Apc->GetHullHealth();
			NextStage(State, 10);
			return true;
		}
		case 10:
		{
			const ABarrelActor* Barrel = Cast<ABarrelActor>(State.Thing.Get());
			if (Barrel && !Barrel->IsBurning() && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, Barrel && Barrel->IsBurning() && Apc->Turret->GetObjectShots() == 1,
				FString::Printf(TEXT("the cannon shell detonated the barrel (%.1f s, %d object shots)"), State.StageTime, Apc->Turret->GetObjectShots()));
			Check(State, Apc->Turret->GetShellsLeft() == State.ShellsBefore - 1, FString::Printf(TEXT("shells %d -> %d"), State.ShellsBefore, Apc->Turret->GetShellsLeft()));
			Check(State, Apc->GetHullHealth() == State.HealthBefore, TEXT("its blast 20 m off did not reach the hull"));
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
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.VehicleDamageSmoke"),
		TEXT("Dev check of the APC hull (P4): hound bites x0.1, Brute blow, mine under the wheels, barrel blast, disabled at 0 HP (crew ejected into cover), Engineer repair, save / load, Ctrl+click barrel = cannon shell."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
