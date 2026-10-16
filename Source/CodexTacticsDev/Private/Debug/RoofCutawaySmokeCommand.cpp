// Dev-only check of the roof cutaway (rev 5h, game designer 2026-10-15; URoofCutawaySubsystem) on L_MovementTest:
//   Scripts/smoke_parallel.ps1 -Commands RoofCutawaySmoke
//   rendered still: UnrealEditor.exe <uproject> /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720
//       -ExecCmds="CodexTactics.RoofCutawaySmoke Shot"  -> Saved/Screenshots/roof_cutaway.png (the top-down game camera)
// A graybox building is spawned 10 m ahead of the squad (nothing saved): a 6.4 x 6.4 m roof slab at 2.8 m whose actor also
// carries a back wall standing on the floor; separate wall actors with a door gap; a partial ceiling at 2.4 m (beside the
// middle); an upper floor slab at 4 m; a slab tagged NoRoofCutaway; a lamp tagged RoofCutaway hanging below the head clearance.
//  0. nothing cut while the squad stands outside;
//  1. the squad walks in (the leader to the middle): the roof slab is cut away - not in the main / depth pass, still visible
//     and casting its shadow, ignored by visibility traces (a click from above reaches the floor) - the back wall of the same
//     actor stays; the ceiling + the upper floor (other actors over the footprint, above the head) cut; the NoRoofCutaway slab
//     stays; the RoofCutaway lamp is cut; the separate walls stay;
//  2. all walk out: everything restored after the hysteresis (>= 0.4 s after the last one left, within 2 s), visibility traces
//     blocked again;
//  3. a camera zone with a fixed interior camera over the building: the leader inside with the zone camera as the view - no cut;
//     the M outside view (ACameraZoneVolume::TogglePlayerOutsideView) - the roof is cut away.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraZoneVolume.h"
#include "Camera/RoofCutawaySubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "UnrealClient.h"

namespace RoofCutawaySmoke
{
	constexpr float StepSeconds = 0.05f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShot = false;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		FVector Outside = FVector::ZeroVector;
		TWeakObjectPtr<AStaticMeshActor> Building;
		TWeakObjectPtr<UStaticMeshComponent> BackWall;
		TWeakObjectPtr<AStaticMeshActor> Ceiling;
		TWeakObjectPtr<AStaticMeshActor> Upper;
		TWeakObjectPtr<AStaticMeshActor> OptOut;
		TWeakObjectPtr<AStaticMeshActor> Lamp;
		TArray<TWeakObjectPtr<AStaticMeshActor>> Walls;
		TWeakObjectPtr<ACameraZoneVolume> Zone;
		float AllOutTime = -1.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("RoofCutawaySmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Centre, const FVector& Size, const FRotator& Rotation, bool bCollision, FName Tag = NAME_None)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Block = World->SpawnActor<AStaticMeshActor>(Centre, Rotation, Params);
		if (!Block)
		{
			return nullptr;
		}
		Block->SetMobility(EComponentMobility::Movable);
		UStaticMeshComponent* Mesh = Block->GetStaticMeshComponent();
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Mesh->SetCanEverAffectNavigation(false);
		Block->SetActorScale3D(Size / 100.f);
		if (bCollision)
		{
			Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		}
		else
		{
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		if (!Tag.IsNone())
		{
			Block->Tags.Add(Tag);
		}
		return Block;
	}

	/** Cut away the way the system does it: drawn neither in the main nor the depth pass, still visible + casting its shadow. */
	bool IsCut(const URoofCutawaySubsystem* Roofs, const UPrimitiveComponent* Piece)
	{
		return Piece && Roofs->IsCutAway(Piece) && !Piece->bRenderInMainPass && !Piece->bRenderInDepthPass && Piece->IsVisible()
			&& Piece->CastShadow && Piece->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Ignore;
	}

	bool IsShown(const URoofCutawaySubsystem* Roofs, const UPrimitiveComponent* Piece)
	{
		return Piece && !Roofs->IsCutAway(Piece) && Piece->bRenderInMainPass && Piece->IsVisible();
	}

	UStaticMeshComponent* MeshOf(const TWeakObjectPtr<AStaticMeshActor>& Actor)
	{
		return Actor.IsValid() ? Actor->GetStaticMeshComponent() : nullptr;
	}

	/** The squad, all of them, ordered to Goal through the controller's real click (a box-selected group). */
	void OrderSquad(UWorld* World, USquadSubsystem* Squad, const FVector& Goal, bool bGroup)
	{
		Squad->SetSelectedGroup(bGroup ? Squad->GetMembers() : TArray<AOperativeCharacter*>{ Squad->GetLeader() }, false);
		if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController()))
		{
			FHitResult Hit;
			Hit.ImpactPoint = Goal;
			Hit.Location = Goal;
			Hit.ImpactNormal = FVector::UpVector;
			Hit.Normal = FVector::UpVector;
			Hit.bBlockingHit = true;
			PC->HandleWorldHit(Hit);
		}
	}

	/** Some operative's centre is under the 6.4 m roof slab (the cutaway's upward trace starts there). */
	bool AnyUnderRoof(const FState& State, USquadSubsystem* Squad)
	{
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			const FVector Local = Member->GetActorLocation() - State.P;
			if (FMath::Abs(FVector::DotProduct(Local, State.F)) < 325.f && FMath::Abs(FVector::DotProduct(Local, State.R)) < 325.f)
			{
				return true;
			}
		}
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		if (State.Time < 3.f)
		{
			return true;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		URoofCutawaySubsystem* Roofs = World->GetSubsystem<URoofCutawaySubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !Roofs || !Leader || State.Time > 150.f)
		{
			Check(State, false, FString::Printf(TEXT("squad / roof subsystem / timeout (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		UStaticMeshComponent* Roof = MeshOf(State.Building);
		switch (State.Stage)
		{
		case 0:
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			FNavLocation OnNav;
			if (!(Nav && Nav->ProjectPointToNavigation(Leader->GetActorLocation(), OnNav, FVector(60.f, 60.f, 200.f))) && State.StageTime < 30.f)
			{
				return true;
			}
			State.F = Leader->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.GroundZ = Leader->GetActorLocation().Z - Leader->GetSimpleCollisionHalfHeight();
			const FVector Ground(Leader->GetActorLocation().X, Leader->GetActorLocation().Y, State.GroundZ);
			State.P = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Ground + State.F * 1000.f);
			State.P.Z = State.GroundZ;
			State.Outside = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Ground - State.F * 300.f);
			const FRotator Yaw = State.F.Rotation();
			const FVector Up = FVector::UpVector;
			// The roof slab (2.8 - 3.0 m) and, on the same actor, the back wall standing on the floor.
			AStaticMeshActor* Building = SpawnBlock(World, State.P + Up * 290.f, FVector(640.f, 640.f, 20.f), Yaw, true);
			if (!Building)
			{
				return Finish(State, false);
			}
			UStaticMeshComponent* BackWall = NewObject<UStaticMeshComponent>(Building, TEXT("BackWall"));
			BackWall->SetMobility(EComponentMobility::Movable);
			BackWall->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			BackWall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			BackWall->SetCanEverAffectNavigation(false);
			BackWall->RegisterComponent();
			BackWall->SetWorldLocationAndRotation(State.P + State.F * 310.f + Up * 140.f, Yaw);
			BackWall->SetWorldScale3D(FVector(0.2f, 6.4f, 2.8f));
			BackWall->AttachToComponent(Building->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
			State.Building = Building;
			State.BackWall = BackWall;
			// Separate wall actors (graybox), the front one with a 2 m door: they stand on the floor, never cut.
			State.Walls.Add(SpawnBlock(World, State.P + State.R * 310.f + Up * 140.f, FVector(640.f, 20.f, 280.f), Yaw, false));
			State.Walls.Add(SpawnBlock(World, State.P - State.R * 310.f + Up * 140.f, FVector(640.f, 20.f, 280.f), Yaw, false));
			State.Walls.Add(SpawnBlock(World, State.P - State.F * 310.f + State.R * 210.f + Up * 140.f, FVector(20.f, 220.f, 280.f), Yaw, false));
			State.Walls.Add(SpawnBlock(World, State.P - State.F * 310.f - State.R * 210.f + Up * 140.f, FVector(20.f, 220.f, 280.f), Yaw, false));
			// A partial ceiling at 2.4 m beside the middle, an upper floor at 4 m, an opted-out slab, an opted-in lamp (1.5 - 2.7 m).
			State.Ceiling = SpawnBlock(World, State.P + State.R * 180.f + Up * 245.f, FVector(500.f, 200.f, 10.f), Yaw, true);
			State.Upper = SpawnBlock(World, State.P + Up * 410.f, FVector(600.f, 600.f, 20.f), Yaw, true);
			State.OptOut = SpawnBlock(World, State.P - State.R * 200.f + State.F * 150.f + Up * 265.f, FVector(150.f, 150.f, 10.f), Yaw, true,
				RoofCutawayRules::OptOutTag());
			State.Lamp = SpawnBlock(World, State.P - State.R * 150.f - State.F * 150.f + Up * 210.f, FVector(30.f, 30.f, 120.f), Yaw, false,
				RoofCutawayRules::OptInTag());
			Next(State);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, Roofs->GetCutAwayCount() == 0 && IsShown(Roofs, Roof), FString::Printf(TEXT("the squad outside: nothing cut (%d)"), Roofs->GetCutAwayCount()));
			OrderSquad(World, Squad, State.P, true);
			Next(State);
			return true;
		}
		case 2:
		{
			if (FVector::Dist2D(Leader->GetActorLocation(), State.P) > 120.f && State.StageTime < 20.f)
			{
				return true;
			}
			if (State.StageTime < 1.f)
			{
				return true; // a few updates under the roof
			}
			Check(State, FVector::Dist2D(Leader->GetActorLocation(), State.P) <= 120.f, TEXT("the leader stands in the middle of the building"));
			Check(State, IsCut(Roofs, Roof), TEXT("the roof slab is cut away: not in the main / depth pass, still visible + casting its shadow, ignored by visibility traces"));
			Check(State, IsShown(Roofs, State.BackWall.Get()), TEXT("the back wall of the same actor (standing on the floor) stays"));
			Check(State, IsCut(Roofs, MeshOf(State.Ceiling)), TEXT("the partial ceiling (another actor over the footprint, above the head) is cut"));
			Check(State, IsCut(Roofs, MeshOf(State.Upper)), TEXT("the upper floor is cut"));
			Check(State, IsShown(Roofs, MeshOf(State.OptOut)), TEXT("the NoRoofCutaway slab stays"));
			Check(State, Roofs->IsCutAway(MeshOf(State.Lamp)), TEXT("the RoofCutaway lamp (below the head clearance) is cut"));
			bool bWallsShown = true;
			for (const TWeakObjectPtr<AStaticMeshActor>& Wall : State.Walls)
			{
				bWallsShown &= IsShown(Roofs, MeshOf(Wall));
			}
			Check(State, bWallsShown, TEXT("the separate walls stay"));
			// A click from above at the leader's feet reaches the floor, not the hidden roof / ceiling / upper floor.
			FCollisionQueryParams Params(SCENE_QUERY_STAT(RoofSmokeClick), false);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Params.AddIgnoredActor(Member);
			}
			FHitResult Hit;
			const FVector Down = State.P + State.R * 120.f;
			const bool bHit = World->LineTraceSingleByChannel(Hit, Down + FVector(0.f, 0.f, 2000.f), Down - FVector(0.f, 0.f, 100.f), ECC_Visibility, Params);
			Check(State, bHit && Hit.GetActor() != State.Building.Get() && Hit.GetActor() != State.Ceiling.Get() && Hit.GetActor() != State.Upper.Get()
				&& FMath::Abs(Hit.ImpactPoint.Z - State.GroundZ) < 30.f,
				FString::Printf(TEXT("a click from above reaches the floor inside (hit %s at %.0f cm)"), bHit && Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("-"),
					bHit ? Hit.ImpactPoint.Z - State.GroundZ : -1.f));
			Next(State);
			if (State.bShot)
			{
				State.Stage = 90;
			}
			return true;
		}
		case 90:
			// Rendered still: shaders / textures settle first.
			if (State.StageTime < 8.f)
			{
				return true;
			}
			{
				const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("roof_cutaway.png");
				FScreenshotRequest::RequestScreenshot(Path, false, false);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke screenshot %s"), *Path);
			}
			State.Stage = 91;
			State.StageTime = 0.f;
			return true;
		case 91:
			if (State.StageTime < 2.f)
			{
				return true;
			}
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		case 3:
			OrderSquad(World, Squad, State.Outside, true);
			Next(State);
			return true;
		case 4:
		{
			if (AnyUnderRoof(State, Squad) && State.StageTime < 20.f)
			{
				State.AllOutTime = -1.f;
				return true;
			}
			if (State.AllOutTime < 0.f)
			{
				State.AllOutTime = State.StageTime;
			}
			if (Roofs->GetCutAwayCount() > 0 && State.StageTime - State.AllOutTime < 3.f)
			{
				return true;
			}
			const float Delay = State.StageTime - State.AllOutTime;
			Check(State, !AnyUnderRoof(State, Squad), TEXT("everyone walked out"));
			Check(State, Roofs->GetCutAwayCount() == 0 && IsShown(Roofs, Roof) && IsShown(Roofs, MeshOf(State.Ceiling)) && IsShown(Roofs, MeshOf(State.Upper))
				&& MeshOf(State.Lamp) && MeshOf(State.Lamp)->bRenderInMainPass,
				FString::Printf(TEXT("all restored %.2f s after the last one left"), Delay));
			Check(State, Delay >= 0.35f && Delay <= 1.2f, FString::Printf(TEXT("the hysteresis held them %.2f s after the last one left (0.5 s + one 0.1 s update)"), Delay));
			Check(State, Roof && Roof->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block && Roof->bRenderInDepthPass,
				TEXT("the roof blocks visibility traces again, depth pass back"));
			// Phase 3: a camera zone with a fixed interior camera over the building; only the leader goes in.
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ACameraActor* Camera = World->SpawnActor<ACameraActor>(State.P + FVector(0.f, 0.f, 250.f) - State.F * 250.f, (State.F - FVector(0.f, 0.f, 0.5f)).Rotation(), Params);
			ACameraZoneVolume* Zone = World->SpawnActor<ACameraZoneVolume>(State.P + FVector(0.f, 0.f, 150.f), State.F.Rotation(), Params);
			if (!Camera || !Zone)
			{
				Check(State, false, TEXT("camera zone"));
				return Finish(State, false);
			}
			Zone->TargetCamera = Camera;
			State.Zone = Zone;
			OrderSquad(World, Squad, State.P, false);
			Next(State);
			return true;
		}
		case 5:
		{
			ACameraZoneVolume* Zone = State.Zone.Get();
			if (!(Zone && Zone->IsZoneCameraShown() && FVector::Dist2D(Leader->GetActorLocation(), State.P) <= 150.f) && State.StageTime < 20.f)
			{
				return true;
			}
			if (State.StageTime < 1.f)
			{
				return true;
			}
			const bool bOthersOut = !Squad->GetMembers().ContainsByPredicate([&State, Leader](const AOperativeCharacter* Member)
			{
				const FVector Local = Member->GetActorLocation() - State.P;
				return Member != Leader && FMath::Abs(FVector::DotProduct(Local, State.F)) < 420.f && FMath::Abs(FVector::DotProduct(Local, State.R)) < 420.f;
			});
			Check(State, Zone && Zone->IsZoneCameraShown(), TEXT("the leader inside the zone: its fixed interior camera is the view"));
			Check(State, !bOthersOut || IsShown(Roofs, Roof), FString::Printf(TEXT("... the roof is NOT cut (the interior camera sees inside; others %s)"),
				bOthersOut ? TEXT("outside") : TEXT("under the roof too")));
			if (Zone)
			{
				Zone->TogglePlayerOutsideView(); // M
			}
			Next(State);
			return true;
		}
		case 6:
		{
			if (State.StageTime < 0.6f)
			{
				return true;
			}
			ACameraZoneVolume* Zone = State.Zone.Get();
			Check(State, Zone && Zone->IsPlayerOutsideView() && !Zone->IsZoneCameraShown(), TEXT("M: the outside (top-down) view inside the zone"));
			Check(State, IsCut(Roofs, Roof) && IsCut(Roofs, MeshOf(State.Ceiling)), TEXT("... the roof is cut away there too"));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		FTimerHandle PlaceHandle;
		TWeakObjectPtr<UWorld> PlaceWorld(World);
		World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
		{
			SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
		}), 0.5f, false);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Shot"), ESearchCase::IgnoreCase); });
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.RoofCutawaySmoke"),
		TEXT("Dev check (rev 5h): roofs / ceilings over operatives are cut away (shadow kept) and restored after they leave; not under a shown interior camera, cut in the M outside view. 'Shot' saves roof_cutaway.png."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
