// Dev-only headless check of the APC (phases P0 + P1, docs/vehicles/APC_01_TECHNICAL.md) on L_MovementTest, on a flat test
// area the smoke builds 100 m under the map's layout (floor, a NavMeshBoundsVolume copy tagged "VehicleNav", a wall, a
// NoVehicle zone; nothing is saved):
//   Scripts/smoke.ps1 -Command CodexTactics.VehicleSmoke
//  0. nav agents: Default = the former implicit agent (35 / 144), every operative's NavAgentProps resolve to it, the map's
//     own bounds volume lost the Vehicle agent (opt-in), the test volume has both navmeshes;
//  1. the parked APC carves the infantry navmesh: a path from one side to the other goes round the hull;
//  2. a click on the APC (its roof) = cover preview, a second click = the commander hugs the hull (cover at the vehicle);
//  3. Alt + Y: the whole squad walks to the doors and boards (~2 s): Engineer = driver, Commander = gunner, Medic =
//     passenger; three VehicleBoarded events; mounted = hidden, no collision, unknown to enemies, no damage, refuses move
//     orders; the running engine heats the cabin: no cold drain;
//  4. a ground click (the commander leads, aboard) drives the APC round a wall (detour path, the hull never touches the
//     wall), VehicleArrived; the camera follows; a NoVehicle zone on the straight line is driven round; a short goal
//     behind is reached in reverse;
//  5. save, change (one out, the APC moved), load: the APC is back, all three aboard in their seats;
//  6. Y: the commander gets out and takes cover at the hull; Alt + Y: the others too; the engine stops;
//  7. all exits blocked: nobody gets out ("All exits are blocked").

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Combat/WaveSubsystem.h"
#include "Data/CombatTypes.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Core/PlayerCommands.h"
#include "Core/SaveGameSubsystem.h"
#include "Debug/EventBusProbe.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/FileManager.h"
#include "Interactables/HeatSourceComponent.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavModifierComponent.h"
#include "Subsystems/CodexEventBus.h"
#include "UObject/StrongObjectPtr.h"
#include "Vehicles/VehicleNavigation.h"
#include "Vehicles/VehiclePawn.h"
#include "Vehicles/VehicleSubsystem.h"

namespace VehicleSmoke
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
		TWeakObjectPtr<AStaticMeshActor> Wall;
		TWeakObjectPtr<AActor> NoVehicleZone;
		TArray<TWeakObjectPtr<AActor>> Spawned;
		TStrongObjectPtr<UEventBusProbe> Probe;
		FVector Goal = FVector::ZeroVector;
		bool bTouchedWall = false;
		bool bEnteredZone = false;
		bool bSawReverse = false;
		bool bSawDetour = false;
		float ColdBefore = 0.f;
		FVector SavedApc = FVector::ZeroVector;
		TMap<FName, EVehicleSeat> SavedSeats;
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
		FPlatformMisc::RequestExit(false, TEXT("VehicleSmoke"));
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

	FHitResult MakeHit(AActor* Actor, const FVector& Point, const FVector& Normal)
	{
		FHitResult Hit;
		Hit.bBlockingHit = true;
		Hit.HitObjectHandle = FActorInstanceHandle(Actor);
		Hit.Component = Actor ? Cast<UPrimitiveComponent>(Actor->GetRootComponent()) : nullptr;
		Hit.ImpactPoint = Point;
		Hit.Location = Point;
		Hit.ImpactNormal = Normal;
		Hit.Normal = Normal;
		return Hit;
	}

	TArray<AOperativeCharacter*> Crew(const FState& State)
	{
		TArray<AOperativeCharacter*> Result;
		for (const TWeakObjectPtr<AOperativeCharacter>& Each : { State.Commander, State.Engineer, State.Medic })
		{
			if (Each.IsValid())
			{
				Result.Add(Each.Get());
			}
		}
		return Result;
	}

	/** A segment-sampled path stays out of the hull footprint. */
	bool PathAvoidsHull(const TArray<FNavPathPoint>& Points, const AVehiclePawn& Apc)
	{
		for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
		{
			const FVector A = Points[Index].Location;
			const FVector B = Points[Index + 1].Location;
			const int32 Samples = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(A, B) / 20.f));
			for (int32 Step = 0; Step <= Samples; ++Step)
			{
				if (Apc.IsInsideFootprint(FMath::Lerp(A, B, Step / static_cast<float>(Samples)), 0.f))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool InCoverAtHull(const AOperativeCharacter* Operative, const AVehiclePawn* Apc)
	{
		return Operative && Operative->bInCover && Operative->GetCoverSlot().WallActor.Get() == Apc;
	}

	void TrackDrive(FState& State)
	{
		const AVehiclePawn* Apc = State.Apc.Get();
		if (!Apc)
		{
			return;
		}
		if (const AStaticMeshActor* Wall = State.Wall.Get())
		{
			const FCollisionShape Shape = FCollisionShape::MakeBox(Apc->HullHalfExtent);
			if (Wall->GetStaticMeshComponent()->OverlapComponent(Apc->GetHullCentreWorld(), Apc->Hull->GetComponentQuat(), Shape))
			{
				State.bTouchedWall = true;
			}
		}
		if (const AActor* Zone = State.NoVehicleZone.Get())
		{
			const FVector Delta = Apc->GetActorLocation() - Zone->GetActorLocation();
			State.bEnteredZone |= FMath::Abs(Delta.X) < 500.f && FMath::Abs(Delta.Y) < 500.f; // 1 m tolerance: areas are not eroded (the doc: inflate NoVehicle volumes)
		}
		State.bSawReverse |= Apc->IsReversing() && Apc->GetSpeed() < -10.f;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.1f;
		State.StageTime += 0.1f;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UVehicleSubsystem* Vehicles = World->GetSubsystem<UVehicleSubsystem>();
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
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
			if (!Vehicles || !Nav || !PC || !State.Commander.IsValid() || !State.Engineer.IsValid() || !State.Medic.IsValid())
			{
				Check(State, false, TEXT("vehicle subsystem, nav system, controller and commander / engineer / medic"));
				return Finish(State);
			}
			State.Probe.Reset(NewObject<UEventBusProbe>());
			State.Probe->ListenGameplayEvents(UCodexEventBus::Get(World));

			// Nav agents (P0): the infantry agent is the former implicit one; ground pawns never pick the vehicle agent.
			const int32 VehicleIndex = Vehicles->GetVehicleAgentIndex();
			const ANavigationData* Infantry = Nav->GetDefaultNavDataInstance();
			Check(State, VehicleIndex == VehicleNavAgents::VehicleIndex && Nav->GetSupportedAgents().Num() == 2,
				FString::Printf(TEXT("two nav agents, Vehicle at index %d"), VehicleIndex));
			Check(State, Infantry && Infantry->GetConfig().Name == TEXT("Default") && FMath::IsNearlyEqual(Infantry->GetConfig().AgentRadius, 35.f)
				&& FMath::IsNearlyEqual(Infantry->GetConfig().AgentHeight, 144.f), TEXT("Default agent = radius 35 / height 144 (the former implicit agent)"));
			bool bAllInfantry = true;
			for (const AOperativeCharacter* Member : Crew(State))
			{
				bAllInfantry &= Nav->GetNavDataForProps(Member->GetNavAgentPropertiesRef(), Member->GetActorLocation()) == Infantry;
			}
			Check(State, bAllInfantry, TEXT("every operative's NavAgentProps resolve to the infantry navmesh"));
			// ... and every enemy archetype's (spawned far away for the check, removed at once).
			if (UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>())
			{
				FString Wrong;
				int32 Checked = 0;
				for (const EEnemyArchetype Archetype : { EEnemyArchetype::FrostHound, EEnemyArchetype::Spitter, EEnemyArchetype::Brute, EEnemyArchetype::Frostbitten,
					EEnemyArchetype::Cutter, EEnemyArchetype::CryoDrone, EEnemyArchetype::Marksman })
				{
					if (AEnemyCharacter* Enemy = Waves->SpawnEnemy(Archetype, SmokeUtils::LayoutTransform(World).GetLocation() + FVector(0.f, 0.f, -20000.f)))
					{
						++Checked;
						if (Nav->GetNavDataForProps(Enemy->GetNavAgentPropertiesRef(), Enemy->GetActorLocation()) != Infantry)
						{
							Wrong += Enemy->GetName() + TEXT(" ");
						}
						Enemy->Destroy();
					}
				}
				Check(State, Checked > 0 && Wrong.IsEmpty(), FString::Printf(TEXT("every enemy archetype (%d) resolves to the infantry navmesh %s"), Checked, *Wrong));
			}
			ANavMeshBoundsVolume* MapVolume = nullptr;
			for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
			{
				MapVolume = MapVolume ? MapVolume : *It;
			}
			Check(State, MapVolume && !MapVolume->SupportedAgents.Contains(VehicleIndex) && MapVolume->SupportedAgents.Contains(0),
				TEXT("the map's bounds volume builds no vehicle navmesh (opt-in), still the infantry one"));

			// The test area: 100 m under the layout.
			const FVector Layout = SmokeUtils::LayoutTransform(World).GetLocation();
			State.O = FVector(Layout.X, Layout.Y, -10000.f);
			SpawnBlock(State, World, State.O + FVector(0.f, 0.f, -10.f), FVector(10000.f, 10000.f, 20.f));
			if (MapVolume)
			{
				FActorSpawnParameters Params;
				Params.Template = MapVolume;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				ANavMeshBoundsVolume* Volume = World->SpawnActor<ANavMeshBoundsVolume>(MapVolume->GetClass(), State.O + FVector(0.f, 0.f, 200.f), FRotator::ZeroRotator, Params);
				if (Volume)
				{
					// Scale the copied brush to 100 x 100 x 10 m.
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
			State.Wall = SpawnBlock(State, World, State.O + FVector(-1000.f, 0.f, 150.f), FVector(100.f, 1600.f, 300.f));
			// NoVehicle zone: a modifier with the failsafe box (no collision) on the straight line of the second drive.
			AActor* Zone = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(State.O + FVector(1250.f, 1500.f, 0.f)));
			if (Zone)
			{
				USceneComponent* ZoneRoot = NewObject<USceneComponent>(Zone, TEXT("ZoneRoot"));
				Zone->SetRootComponent(ZoneRoot);
				ZoneRoot->RegisterComponent();
				Zone->SetActorLocation(State.O + FVector(1250.f, 1500.f, 0.f));
				UNavModifierComponent* Modifier = NewObject<UNavModifierComponent>(Zone, TEXT("NoVehicle"));
				Modifier->FailsafeExtent = FVector(600.f, 600.f, 300.f);
				Modifier->SetAreaClass(UNavArea_NoVehicle::StaticClass());
				Modifier->RegisterComponent();
				State.NoVehicleZone = Zone;
				State.Spawned.Add(Zone);
			}
			// The APC (the Blueprint when it exists, else the C++ class with the vendor mesh by soft path) and the squad.
			UClass* ApcClass = LoadClass<AVehiclePawn>(nullptr, TEXT("/Game/Vehicles/BP_APC_BTR82.BP_APC_BTR82_C"));
			Check(State, true, FString::Printf(TEXT("APC class %s"), ApcClass ? *ApcClass->GetName() : TEXT("AVehiclePawn (no BP_APC_BTR82 yet)")));
			FActorSpawnParameters ApcParams;
			ApcParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Apc = World->SpawnActor<AVehiclePawn>(ApcClass ? ApcClass : AVehiclePawn::StaticClass(), State.O + FVector(-2500.f, 0.f, 5.f), FRotator::ZeroRotator, ApcParams);
			State.Apc = Apc;
			if (!Apc)
			{
				Check(State, false, TEXT("APC spawned"));
				return Finish(State);
			}
			Apc->RestoreTransform(State.O + FVector(-2500.f, 0.f, 0.f), 0.f);
			const FVector Spots[] = { FVector(-3300.f, -900.f, 100.f), FVector(-3500.f, -700.f, 100.f), FVector(-3200.f, -1100.f, 100.f) };
			int32 Next = 0;
			for (AOperativeCharacter* Member : Crew(State))
			{
				Member->StopOperative();
				Member->TeleportTo(State.O + Spots[Next++], FRotator::ZeroRotator, false, true);
				Member->ColdLevel = 0.f;
			}
			Squad->SetLeader(State.Commander.Get());
			NextStage(State, 1);
			return true;
		}
		case 1:
		{
			// Both navmeshes built on the test area.
			ANavigationData* VehicleNav = Apc->GetVehicleNavData();
			FNavLocation OnInfantry;
			FNavLocation OnVehicle;
			const bool bReady = State.StageTime > 2.f && !Nav->IsNavigationBuildInProgress() && VehicleNav
				&& Nav->ProjectPointToNavigation(State.O + FVector(-3300.f, -900.f, 0.f), OnInfantry, FVector(80.f, 80.f, 300.f))
				&& VehicleNav->ProjectPoint(State.O + FVector(500.f, 0.f, 0.f), OnVehicle, FVector(100.f, 100.f, 300.f));
			if (!bReady && State.StageTime < 60.f)
			{
				return true;
			}
			Check(State, bReady, FString::Printf(TEXT("infantry + vehicle navmesh on the test area (%.1f s)"), State.StageTime));
			// Diagnostics: the custom areas on each navmesh and the poly area under the hull / in the zone.
			for (ANavigationData* Data : { static_cast<ANavigationData*>(Nav->GetDefaultNavDataInstance()), VehicleNav })
			{
				const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Data);
				if (!Recast)
				{
					continue;
				}
				const NavNodeRef HullPoly = Recast->FindNearestPoly(Apc->GetHullCentreWorld() - FVector(0.f, 0.f, 150.f), FVector(10.f, 10.f, 200.f));
				const NavNodeRef ZonePoly = Recast->FindNearestPoly(State.O + FVector(1250.f, 1500.f, 0.f), FVector(10.f, 10.f, 200.f));
				UE_LOG(LogCodexTactics, Display, TEXT("[VehicleSmoke] %s (agent %d): area ids hull %d novehicle %d; walkable poly at the hull centre %s (area %d), in the zone %s (area %d); obstacle relevant %d"),
					*Data->GetName(), Nav->GetSupportedAgentIndex(Data), Data->GetAreaID(UNavArea_VehicleHull::StaticClass()), Data->GetAreaID(UNavArea_NoVehicle::StaticClass()),
					HullPoly ? TEXT("yes") : TEXT("no"), HullPoly ? static_cast<int32>(Recast->GetPolyAreaID(HullPoly)) : -1,
					ZonePoly ? TEXT("yes") : TEXT("no"), ZonePoly ? static_cast<int32>(Recast->GetPolyAreaID(ZonePoly)) : -1,
					Apc->NavObstacle->IsNavigationRelevant() ? 1 : 0);
			}
			// The parked APC carves the infantry navmesh: a path across it goes round.
			const FVector From = Apc->GetHullCentreWorld() + FVector(-200.f, -500.f, -150.f);
			const FVector To = Apc->GetHullCentreWorld() + FVector(200.f, 500.f, -150.f);
			UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, From, To);
			Check(State, Path && Path->IsValid() && !Path->IsPartial() && PathAvoidsHull(Path->GetPath()->GetPathPoints(), *Apc),
				FString::Printf(TEXT("an infantry path across the parked APC goes round the hull (%d points)"), Path && Path->IsValid() ? Path->GetPath()->GetPathPoints().Num() : 0));
			// Cover click on the APC's roof: preview, then confirm.
			const FHitResult Hit = MakeHit(Apc, Apc->GetHullCentreWorld() + FVector(0.f, 0.f, Apc->HullHalfExtent.Z), FVector::UpVector);
			PC->HandleWorldHit(Hit);
			Check(State, PC->HasCoverPreview(), TEXT("a click on the APC = the cover ghost (not a move / not refused as a Pawn)"));
			PC->HandleWorldHit(Hit);
			NextStage(State, 2);
			return true;
		}
		case 2:
		{
			AOperativeCharacter* Commander = State.Commander.Get();
			if (!InCoverAtHull(Commander, Apc) && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, InCoverAtHull(Commander, Apc), FString::Printf(TEXT("second click: the commander hugs the APC's hull (in cover %d, %.1f m off the hull centre)"),
				Commander->bInCover ? 1 : 0, FVector::Dist2D(Commander->GetActorLocation(), Apc->GetHullCentreWorld()) / 100.f));
			// Alt + Y: everyone boards.
			PC->VehicleKey(true);
			int32 Boarding = 0;
			for (const AOperativeCharacter* Member : Crew(State))
			{
				Boarding += Vehicles->IsBoarding(Member) ? 1 : 0;
			}
			Check(State, Boarding == 3, FString::Printf(TEXT("Alt + Y: %d / 3 walk to the doors"), Boarding));
			NextStage(State, 3);
			return true;
		}
		case 3:
		{
			int32 Aboard = 0;
			for (const AOperativeCharacter* Member : Crew(State))
			{
				Aboard += Vehicles->IsMounted(Member) ? 1 : 0;
			}
			if (Aboard < 3 && State.StageTime < 25.f)
			{
				return true;
			}
			Check(State, Aboard == 3, FString::Printf(TEXT("all aboard (%d / 3, %.1f s)"), Aboard, State.StageTime));
			Check(State, Apc->GetSeatOf(State.Engineer.Get()) == EVehicleSeat::Driver && Apc->GetSeatOf(State.Commander.Get()) == EVehicleSeat::Gunner
				&& Apc->GetSeatOf(State.Medic.Get()) == EVehicleSeat::Passenger, TEXT("seats: Engineer driver, Commander gunner, Medic passenger"));
			Check(State, State.Probe->CountEvent(GameplayEvents::VehicleBoarded) == 3, FString::Printf(TEXT("3 VehicleBoarded events (%d)"),
				State.Probe->CountEvent(GameplayEvents::VehicleBoarded)));
			bool bEffects = true;
			const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
			for (AOperativeCharacter* Member : Crew(State))
			{
				FVector Belief;
				const float Health = Member->HealthComponent->GetCurrentHealth();
				const float Taken = Member->TakeHit(50.f, TEXT("Smoke"), false, true);
				bEffects &= Member->IsHidden() && !Member->GetActorEnableCollision() && (!Sight || !Sight->GetBelief(nullptr, Member, Belief))
					&& Taken <= 0.f && FMath::IsNearlyEqual(Member->HealthComponent->GetCurrentHealth(), Health)
					&& Member->OrderMoveTo(Member->GetActorLocation() + FVector(500.f, 0.f, 0.f), false) == EOperativeOrderResult::Refused;
				Member->ColdLevel = 50.f;
			}
			Check(State, bEffects, TEXT("mounted: hidden, no collision, unknown to enemies, no damage, move orders refused"));
			Check(State, Apc->IsEngineRunning() && Apc->CabinHeat->IsHeatActive(), TEXT("the engine runs, the cabin heater is on"));
			State.ColdBefore = 50.f;
			NextStage(State, 4);
			return true;
		}
		case 4:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			float MaxCold = 0.f;
			for (const AOperativeCharacter* Member : Crew(State))
			{
				MaxCold = FMath::Max(MaxCold, Member->ColdLevel);
			}
			Check(State, MaxCold <= State.ColdBefore, FString::Printf(TEXT("no cold drain aboard with the engine running (max cold %.1f <= 50)"), MaxCold));
			// Drive round the wall: the commander (aboard) leads, a ground click.
			State.Goal = State.O + FVector(500.f, 0.f, 0.f);
			PC->HandleWorldHit(MakeHit(nullptr, State.Goal, FVector::UpVector));
			State.bSawDetour = Apc->IsDriving() && Apc->GetPathPoints().Num() >= 3;
			Check(State, Apc->IsDriving(), TEXT("a ground click with the leader aboard drives the APC"));
			Check(State, State.bSawDetour, FString::Printf(TEXT("its path goes round the wall (%d points)"), Apc->GetPathPoints().Num()));
			Check(State, State.Probe->CountEvent(GameplayEvents::VehicleDriveStarted) == 1, TEXT("VehicleDriveStarted"));
			NextStage(State, 5);
			return true;
		}
		case 5:
		{
			TrackDrive(State);
			if (Apc->IsDriving() && State.StageTime < 45.f)
			{
				return true;
			}
			Check(State, State.Probe->CountEvent(GameplayEvents::VehicleArrived) == 1 && FVector::Dist2D(Apc->GetActorLocation(), State.Goal) < 400.f,
				FString::Printf(TEXT("arrived round the wall (%.1f m from the goal, %.1f s)"), FVector::Dist2D(Apc->GetActorLocation(), State.Goal) / 100.f, State.StageTime));
			Check(State, !State.bTouchedWall, TEXT("the hull never touched the wall"));
			const ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(PC->GetPawn());
			Check(State, !Camera || (Camera->GetFollowTarget() == State.Commander.Get() && FVector::Dist2D(Camera->GetFocus(), Apc->GetActorLocation()) < 800.f),
				FString::Printf(TEXT("the camera follows the APC (focus %.1f m off)"), Camera ? FVector::Dist2D(Camera->GetFocus(), Apc->GetActorLocation()) / 100.f : 0.f));
			// The NoVehicle zone lies on the straight line to the next goal.
			State.Goal = State.O + FVector(2000.f, 3000.f, 0.f);
			Check(State, Vehicles->HandleDriveClick(State.Commander.Get(), State.Goal, true) && Apc->IsDriving(), TEXT("double click: full speed to the far goal"));
			FString PathText;
			for (const FVector& Point : Apc->GetPathPoints())
			{
				PathText += FString::Printf(TEXT(" (%.0f, %.0f)"), Point.X - State.O.X, Point.Y - State.O.Y);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[VehicleSmoke] path past the NoVehicle zone (test-area coordinates):%s"), *PathText);
			NextStage(State, 6);
			return true;
		}
		case 6:
		{
			TrackDrive(State);
			if (Apc->IsDriving() && State.StageTime < 45.f)
			{
				return true;
			}
			Check(State, FVector::Dist2D(Apc->GetActorLocation(), State.Goal) < 400.f, FString::Printf(TEXT("arrived past the NoVehicle zone (%.1f m)"),
				FVector::Dist2D(Apc->GetActorLocation(), State.Goal) / 100.f));
			Check(State, !State.bEnteredZone, TEXT("the APC never entered the NoVehicle zone"));
			// A short goal behind: reverse.
			const FVector Heading(FMath::Cos(FMath::DegreesToRadians(Apc->GetHeadingYaw())), FMath::Sin(FMath::DegreesToRadians(Apc->GetHeadingYaw())), 0.f);
			State.Goal = Apc->GetActorLocation() - Heading * 800.f;
			Vehicles->HandleDriveClick(State.Commander.Get(), State.Goal, false);
			Check(State, Apc->IsReversing(), TEXT("a goal 8 m behind is driven in reverse"));
			NextStage(State, 7);
			return true;
		}
		case 7:
		{
			TrackDrive(State);
			if (Apc->IsDriving() && State.StageTime < 20.f)
			{
				return true;
			}
			Check(State, State.bSawReverse && FVector::Dist2D(Apc->GetActorLocation(), State.Goal) < 300.f, FString::Printf(TEXT("backed up to the goal (%.1f m)"),
				FVector::Dist2D(Apc->GetActorLocation(), State.Goal) / 100.f));
			// Save with all three aboard.
			USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>();
			Saves->SaveDirectoryOverride = FPaths::ProjectSavedDir() / TEXT("SmokeSaves") / TEXT("VehicleSmoke");
			IFileManager::Get().DeleteDirectory(*Saves->SaveDirectoryOverride, false, true);
			State.SavedApc = Apc->GetActorLocation();
			for (const AOperativeCharacter* Member : Crew(State))
			{
				State.SavedSeats.Add(Member->GetFName(), Apc->GetSeatOf(Member));
			}
			Check(State, Saves->SaveGame(TEXT("VehicleSmoke")), TEXT("saved with the squad aboard"));
			// Change it: the medic out, the APC moved.
			Vehicles->DismountImmediately(State.Medic.Get(), Apc->GetExitCandidatesWorld()[0]);
			Apc->RestoreTransform(State.SavedApc + FVector(-1500.f, -1500.f, 0.f), 45.f);
			Check(State, Saves->LoadGame(TEXT("VehicleSmoke")), TEXT("loaded"));
			bool bSeats = true;
			for (const AOperativeCharacter* Member : Crew(State))
			{
				bSeats &= Vehicles->IsMounted(Member) && Apc->GetSeatOf(Member) == State.SavedSeats.FindRef(Member->GetFName()) && Member->IsHidden();
			}
			Check(State, FVector::Dist2D(Apc->GetActorLocation(), State.SavedApc) < 50.f, FString::Printf(TEXT("load: the APC is back (%.1f m)"),
				FVector::Dist2D(Apc->GetActorLocation(), State.SavedApc) / 100.f));
			Check(State, bSeats && Apc->GetCrewCount() == 3, TEXT("load: all three aboard in their saved seats"));
			// Y: the commander (the selected leader) gets out.
			Squad->SetLeader(State.Commander.Get());
			PC->VehicleKey(false);
			Check(State, Vehicles->IsDisembarking(State.Commander.Get()) && !Vehicles->IsDisembarking(State.Engineer.Get()), TEXT("Y: only the selected commander gets out"));
			NextStage(State, 8);
			return true;
		}
		case 8:
		{
			AOperativeCharacter* Commander = State.Commander.Get();
			if (!InCoverAtHull(Commander, Apc) && State.StageTime < 12.f)
			{
				return true;
			}
			FNavLocation OnNav;
			Check(State, !Vehicles->IsMounted(Commander) && !Commander->IsHidden() && Commander->GetActorEnableCollision()
				&& Nav->ProjectPointToNavigation(Commander->GetActorLocation(), OnNav, FVector(80.f, 80.f, 250.f)),
				TEXT("the commander is out: visible, colliding, on the navmesh"));
			Check(State, InCoverAtHull(Commander, Apc), TEXT("Y: he took cover at the hull"));
			Check(State, State.Probe->CountEvent(GameplayEvents::VehicleDisembarked, Commander->GetFName()) == 1, TEXT("VehicleDisembarked with his id"));
			// Back in at once; Alt + Y with the whole squad aboard: everyone out.
			Vehicles->MountImmediately(Commander, Apc);
			PC->VehicleKey(true);
			int32 Leaving = 0;
			for (const AOperativeCharacter* Member : Crew(State))
			{
				Leaving += Vehicles->IsDisembarking(Member) ? 1 : 0;
			}
			Check(State, Leaving == 3, FString::Printf(TEXT("Alt + Y with all aboard: %d / 3 get out"), Leaving));
			NextStage(State, 9);
			return true;
		}
		case 9:
		{
			bool bAllCover = true;
			bool bAnyAboard = false;
			for (const AOperativeCharacter* Member : Crew(State))
			{
				bAllCover &= InCoverAtHull(Member, Apc);
				bAnyAboard |= Vehicles->IsMounted(Member);
			}
			if ((!bAllCover || bAnyAboard) && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, !bAnyAboard, TEXT("everyone is out"));
			Check(State, bAllCover, TEXT("all three took cover at the hull"));
			Check(State, State.Probe->CountEvent(GameplayEvents::VehicleDisembarked) == 4, FString::Printf(TEXT("4 VehicleDisembarked events (%d)"),
				State.Probe->CountEvent(GameplayEvents::VehicleDisembarked)));
			Check(State, !Apc->IsEngineRunning() && !Apc->CabinHeat->IsHeatActive(), TEXT("empty: the engine and the heater are off"));
			// Everyone aboard again at once, then every exit blocked.
			for (AOperativeCharacter* Member : Crew(State))
			{
				Vehicles->MountImmediately(Member, Apc);
			}
			const FTransform Frame(FRotator(0.f, Apc->GetHeadingYaw(), 0.f), Apc->GetHullCentreWorld());
			const FVector Half = Apc->HullHalfExtent;
			for (const FVector& Local : { FVector(0.f, -(Half.Y + 120.f), 0.f), FVector(0.f, Half.Y + 120.f, 0.f), FVector(-(Half.X + 120.f), 0.f, 0.f) })
			{
				const FVector Size(Local.X != 0.f ? 100.f : 1000.f, Local.Y != 0.f ? 100.f : 600.f, 250.f);
				if (AStaticMeshActor* Block = SpawnBlock(State, World, Frame.TransformPosition(Local), Size))
				{
					Block->SetActorRotation(FRotator(0.f, Apc->GetHeadingYaw(), 0.f));
				}
			}
			const int32 Out = Vehicles->OrderDisembark(Crew(State));
			Check(State, Out == 0, FString::Printf(TEXT("all exits blocked: nobody gets out (%d)"), Out));
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
		TEXT("CodexTactics.VehicleSmoke"),
		TEXT("Dev check of the APC (P0 + P1): nav agents, hull cover, Alt+Y boarding with seats, mounted effects, driving round a wall / a NoVehicle zone / in reverse, save-load, Y disembark into cover, blocked exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
