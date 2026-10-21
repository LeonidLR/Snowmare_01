// Dev-only check of "an operative who stood in a LOW cover through a skirmish does not move afterwards" (2026-10-21, found in the A4 lake
// smokes: the Medic-Sapper pressed against Overlook_LowCover did not move on a click whose navmesh path runs across it; the path status
// was Moving, the speed 0, a shimmy did nothing either):
//   Scripts/smoke.ps1 -Command CodexTactics.LowCoverMoveSmoke                                   (L_MovementTest: a spawned 1 m low cover)
//   Scripts/smoke.ps1 -Command CodexTactics.LowCoverMoveLakeSmoke -Map /Game/Maps/L_Demo01_A_FrozenLake   (the real Overlook_LowCover)
// An operative takes cover behind the low cover (crouched), a hound comes at her, she fights it from cover, the hound dies (the skirmish is
// over); then, from each side of the cover:
//  1 a move order to a point on the OTHER side of the cover (the navmesh path may run across it): she must leave the cover and get there
//    (or at least round it) - within 20 s she is at least 3 m away from the wall and out of cover;
//  2 a shimmy order along the wall: she moves along it.
// Logs the diagnostics of a stuck operative (path status, speed, movement mode, navmesh projection, the move hold, the vault probe).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAIController.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Tactics/CoverTraceRules.h"
#include "Tutorial/TutorialSubsystem.h"

namespace LowCoverMoveSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		bool bLake = false;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		/** 0 = north/east side first, 1 = the other side. */
		int32 Side = 0;
		TWeakObjectPtr<AStaticMeshActor> Cover;
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		FVector Centre = FVector::ZeroVector;
		FVector Extent = FVector::ZeroVector;
		FVector Normal = FVector::ForwardVector;
		float HalfThick = 20.f;
		float GroundZ = 0.f;
		FVector Start = FVector::ZeroVector;
		FVector Goal = FVector::ZeroVector;
		float Logged = 0.f;
		bool bFought = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("LowCoverMoveSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.Logged = 0.f;
	}

	bool Settled(const AOperativeCharacter* Op)
	{
		return Op && Op->bInCover && !Op->bShimmying && !Op->IsMoving() && Op->GetVelocity().Size2D() < 5.f && Op->GetCoverEntryMeshOffset().Size() < 1.f;
	}

	/** The state of a stuck operative in one log line. */
	void LogDiagnostics(const TCHAR* Tag, const AOperativeCharacter& Op, UWorld* World)
	{
		const AAIController* AI = Cast<AAIController>(Op.GetController());
		const UPathFollowingComponent* Path = AI ? AI->GetPathFollowingComponent() : nullptr;
		const UCharacterMovementComponent* Movement = Op.GetCharacterMovement();
		FNavLocation OnNav;
		const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const bool bProjected = Nav && Nav->ProjectPointToNavigation(Op.GetActorLocation(), OnNav, FVector(5.f, 5.f, 120.f));
		FNavLocation Wider;
		const bool bProjectedWide = Nav && Nav->ProjectPointToNavigation(Op.GetActorLocation(), Wider, FVector(80.f, 80.f, 120.f));
		UE_LOG(LogCodexTactics, Display, TEXT("[LowCoverMove] %s %s: at %s speed %.0f, order %d, cover %d (pending %d, shimmy %d, wrap %d), path status %d, mode %d, maxspeed %.0f, "
			"hold %.2f, stance %d, vaulting %d, knocked %d, raging %d, panicking %d, shots %d, navmesh under him %d (within 5 cm) / %d (within 80 cm: %.0f cm off)"),
			Tag, *Op.DisplayName.ToString(), *Op.GetActorLocation().ToCompactString(), Op.GetVelocity().Size2D(), Op.IsMoving() ? 1 : 0, Op.bInCover ? 1 : 0,
			Op.HasPendingCover() ? 1 : 0, Op.bShimmying ? 1 : 0, Op.IsCoverWrapping() ? 1 : 0, Path ? static_cast<int32>(Path->GetStatus()) : -1,
			Movement ? static_cast<int32>(Movement->MovementMode.GetValue()) : -1, Movement ? Movement->MaxWalkSpeed : -1.f, Op.GetMoveHoldSeconds(),
			static_cast<int32>(Op.GetStance()), Op.IsVaulting() ? 1 : 0, Op.IsKnockedDown() ? 1 : 0, Op.IsRaging() ? 1 : 0, Op.IsPanicking() ? 1 : 0, Op.GetCoverLeanShots(), bProjected ? 1 : 0, bProjectedWide ? 1 : 0,
			bProjectedWide ? FVector::Dist2D(Wider.Location, Op.GetActorLocation()) : -1.f);
	}

	AStaticMeshActor* SpawnLowCover(UWorld* World, const FVector& Centre, const FRotator& Rotation)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FTransform Transform(Rotation, Centre, FVector(0.4f, 6.f, 1.f));
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
		return Block;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 400.f)
		{
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Op = State.Op.Get();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 4.f || !Squad || Squad->GetMembers().Num() < 3)
			{
				return true;
			}
			if (UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>())
			{
				Tutorial->SkipTutorial(); // its locks / confinements are not under test
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				It->SetActorTickEnabled(false);
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				if (Member->SquadRole == EOperativeRole::MedicSapper)
				{
					State.Op = Member;
				}
			}
			if (!State.Op.IsValid())
			{
				State.Op = Squad->GetMembers()[0];
			}
			if (State.bLake)
			{
				for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
				{
					State.Cover = It->GetActorNameOrLabel() == TEXT("Overlook_LowCover") ? *It : State.Cover.Get();
				}
			}
			else
			{
				const AOperativeCharacter* Leader = Squad->GetLeader();
				const FVector Ground = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
				const FVector Forward = Leader->GetActorForwardVector().GetSafeNormal2D();
				const FVector Spot = SmokeUtils::ClearPoint(World, Ground, Ground + Forward * 1200.f);
				State.Cover = SpawnLowCover(World, FVector(Spot.X, Spot.Y, Ground.Z + 50.f), FRotator(0.f, Forward.Rotation().Yaw + 90.f, 0.f));
			}
			Check(State, State.Cover.IsValid(), TEXT("the low cover is there"));
			if (!State.Cover.IsValid())
			{
				return Finish(State, false);
			}
			FVector Origin;
			State.Cover->GetActorBounds(false, Origin, State.Extent);
			State.Centre = Origin;
			State.GroundZ = static_cast<float>(Origin.Z - State.Extent.Z);
			const bool bLongY = State.Extent.Y > State.Extent.X;
			State.HalfThick = static_cast<float>(bLongY ? State.Extent.X : State.Extent.Y);
			UE_LOG(LogCodexTactics, Display, TEXT("[LowCoverMove] %s at %s, extent %s (long axis %s), height %.0f cm, tags %d"), *State.Cover->GetActorNameOrLabel(),
				*Origin.ToCompactString(), *State.Extent.ToCompactString(), bLongY ? TEXT("Y") : TEXT("X"), State.Extent.Z * 2.f, State.Cover->Tags.Num());
			Squad->EnterSoloMode();
			Squad->SetLeader(State.Op.Get());
			Next(State, 1);
			return true;
		}
		case 1:
		{
			// The side: the face normal of the long axis.
			const bool bLongY = State.Extent.Y > State.Extent.X;
			const float Sign = State.Side == 0 ? 1.f : -1.f;
			State.Normal = bLongY ? FVector(Sign, 0.f, 0.f) : FVector(0.f, Sign, 0.f);
			Op->StopOperative();
			Op->LeaveCover(TEXT("smoke reset"));
			Op->SetStance(EOperativeStance::Standing);
			const FVector Out = State.Centre + State.Normal * (State.HalfThick + 300.f);
			Op->TeleportTo(FVector(Out.X, Out.Y, State.GroundZ + Op->GetSimpleCollisionHalfHeight() + 5.f), FRotator(0.f, (-State.Normal).Rotation().Yaw, 0.f), false, true);
			Next(State, 2);
			return true;
		}
		case 2:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			FCoverSlot Slot;
			const FVector Face = State.Centre + State.Normal * (State.HalfThick + 30.f);
			const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, FVector(Face.X, Face.Y, State.GroundZ + 60.f), -State.Normal, Slot);
			Check(State, bSlot && Slot.Height == ECoverHeight::LowCover, FString::Printf(TEXT("side %d: a LOW cover slot at the face (%s)"), State.Side,
				bSlot ? (Slot.Height == ECoverHeight::LowCover ? TEXT("low") : TEXT("high")) : TEXT("none")));
			if (!bSlot)
			{
				return Finish(State, false);
			}
			Op->OrderTakeCover(Slot, false);
			Next(State, 3);
			return true;
		}
		case 3:
		{
			if ((!Settled(Op) || State.StageTime < 1.5f) && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, Op->bInCover && Op->GetCoverSlot().Height == ECoverHeight::LowCover, FString::Printf(TEXT("side %d: in the low cover, crouched (stance %d)"),
				State.Side, static_cast<int32>(Op->GetStance())));
			LogDiagnostics(TEXT("in cover"), *Op, World);
			// The skirmish: a hound comes from the open side of the cover and dies against her fire.
			const FVector At = State.Centre + State.Normal * 700.f + FVector(State.Normal.Y, State.Normal.X, 0.f) * 150.f;
			AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, FVector(At.X, At.Y, State.GroundZ + 100.f));
			State.Hound = Hound;
			if (Hound)
			{
				Hound->CustomTimeDilation = 0.f; // stands there: she shoots it from the cover, it never reaches her
				Hound->GetHealthComponent()->SetMaxHealth(120.f);
				Op->AssignPriorityTarget(Hound); // the targeted shot (Ctrl + click): she fires from the cover
			}
			Next(State, 4);
			return true;
		}
		case 4:
		{
			AEnemyCharacter* Hound = State.Hound.Get();
			const bool bGone = !Hound || Hound->IsDying() || !Hound->GetHealthComponent() || !Hound->GetHealthComponent()->IsAlive();
			if (!bGone && State.StageTime < 20.f)
			{
				return true;
			}
			if (!bGone && Hound)
			{
				Hound->GetHealthComponent()->ApplyDirectHealthLoss(100000.f, TEXT("Smoke")); // the skirmish ends (the squad's fire is not under test)
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[LowCoverMove] skirmish over after %.1f s (%s)"), State.StageTime, bGone ? TEXT("by her fire") : TEXT("forced"));
			Next(State, 5);
			return true;
		}
		case 5:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			LogDiagnostics(TEXT("after the skirmish"), *Op, World);
			// 2: a shimmy along the wall, from the cover she still holds.
			if (Op->bInCover)
			{
				const FVector Along = FVector(-State.Normal.Y, State.Normal.X, 0.f);
				FCoverSlot Shimmy;
				const bool bSlot = CoverTraceRules::FindShimmySlot(World, Op->GetCoverSlot(), Op->GetActorLocation() + Along * 200.f, Shimmy);
				State.Start = Op->GetActorLocation();
				const EOperativeOrderResult Result = bSlot ? Op->OrderShimmyTo(Shimmy) : EOperativeOrderResult::Unreachable;
				UE_LOG(LogCodexTactics, Display, TEXT("[LowCoverMove] shimmy %.0f cm along the wall: slot %d, order %d"), bSlot ? FVector::Dist2D(Shimmy.WorldLocation, State.Start) : -1.f,
					bSlot ? 1 : 0, static_cast<int32>(Result));
				Check(State, bSlot && Result == EOperativeOrderResult::Accepted, FString::Printf(TEXT("side %d: a shimmy along the low cover is accepted after the skirmish"), State.Side));
			}
			Next(State, 51);
			return true;
		}
		case 51:
		{
			if (State.StageTime < 4.f)
			{
				return true;
			}
			const float Shimmied = static_cast<float>(FVector::Dist2D(Op->GetActorLocation(), State.Start));
			Check(State, Shimmied >= 100.f && Op->bInCover, FString::Printf(TEXT("side %d: ... she side-steps %.0f cm along the wall and stays in cover (%d)"), State.Side, Shimmied, Op->bInCover ? 1 : 0));
			if (Shimmied < 100.f)
			{
				LogDiagnostics(TEXT("SHIMMY STUCK"), *Op, World);
			}
			Next(State, 52);
			return true;
		}
		case 52:
		{
			// A move order to the OTHER side of the cover.
			State.Start = Op->GetActorLocation();
			const FVector Far = State.Centre - State.Normal * (State.HalfThick + 450.f);
			State.Goal = SmokeUtils::ClearPoint(World, Op->GetActorLocation(), FVector(Far.X, Far.Y, State.GroundZ + 90.f));
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(World, State.Start, State.Goal) : nullptr;
			UE_LOG(LogCodexTactics, Display, TEXT("[LowCoverMove] move order from %s to %s: navmesh path %d points, partial %d, length %.0f"), *State.Start.ToCompactString(),
				*State.Goal.ToCompactString(), Path ? Path->PathPoints.Num() : -1, Path && Path->IsPartial() ? 1 : 0, Path ? Path->GetPathLength() : -1.f);
			const EOperativeOrderResult Result = Op->OrderMoveTo(State.Goal, false);
			Check(State, Result == EOperativeOrderResult::Accepted, FString::Printf(TEXT("side %d: the move order across the cover is accepted (%d)"), State.Side, static_cast<int32>(Result)));
			Next(State, 6);
			return true;
		}
		case 6:
		{
			State.Logged += StepSeconds;
			if (State.Logged >= 1.f)
			{
				State.Logged = 0.f;
				LogDiagnostics(TEXT("walking"), *Op, World);
			}
			const float Moved = static_cast<float>(FVector::Dist2D(Op->GetActorLocation(), State.Start));
			const bool bArrived = FVector::Dist2D(Op->GetActorLocation(), State.Goal) < 120.f;
			if (!bArrived && State.StageTime < 20.f)
			{
				return true;
			}
			Check(State, !Op->bInCover && Moved >= 300.f,
				FString::Printf(TEXT("side %d: after the skirmish the order moves her out of the cover: %.0f cm in %.1f s (goal %.0f cm away), in cover %d"), State.Side,
					Moved, State.StageTime, FVector::Dist2D(Op->GetActorLocation(), State.Goal), Op->bInCover ? 1 : 0));
			if (Moved < 300.f)
			{
				LogDiagnostics(TEXT("STUCK"), *Op, World);
			}
			Next(State, 7);
			return true;
		}
		case 7:
		{
			// Standing PRESSED against the cover (not in cover state: pushed there), a raw order straight across it.
			Op->StopOperative();
			Op->LeaveCover(TEXT("smoke reset"));
			Op->SetStance(EOperativeStance::Standing);
			const float Radius = Op->GetSimpleCollisionRadius();
			const FVector Against = State.Centre + State.Normal * (State.HalfThick + Radius + 2.f);
			Op->TeleportTo(FVector(Against.X, Against.Y, State.GroundZ + Op->GetSimpleCollisionHalfHeight() + 5.f), FRotator(0.f, (-State.Normal).Rotation().Yaw, 0.f), false, true);
			Next(State, 8);
			return true;
		}
		case 8:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			LogDiagnostics(TEXT("pressed against it"), *Op, World);
			State.Start = Op->GetActorLocation();
			const FVector Far = State.Centre - State.Normal * (State.HalfThick + 450.f);
			State.Goal = FVector(Far.X, Far.Y, State.GroundZ + 90.f);
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(World, State.Start, State.Goal) : nullptr;
			UE_LOG(LogCodexTactics, Display, TEXT("[LowCoverMove] pressed order: navmesh path %d points, partial %d, length %.0f (straight %.0f)"), Path ? Path->PathPoints.Num() : -1,
				Path && Path->IsPartial() ? 1 : 0, Path ? Path->GetPathLength() : -1.f, FVector::Dist2D(State.Start, State.Goal));
			Op->OrderMoveTo(State.Goal, false);
			Next(State, 9);
			return true;
		}
		case 9:
		{
			State.Logged += StepSeconds;
			if (State.Logged >= 1.f)
			{
				State.Logged = 0.f;
				LogDiagnostics(TEXT("pressed, walking"), *Op, World);
			}
			const float Moved = static_cast<float>(FVector::Dist2D(Op->GetActorLocation(), State.Start));
			if (Moved < 300.f && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, Moved >= 300.f, FString::Printf(TEXT("side %d: standing pressed against the low cover, an order across it: %.0f cm in %.1f s"), State.Side, Moved, State.StageTime));
			if (Moved < 300.f)
			{
				LogDiagnostics(TEXT("PRESSED STUCK"), *Op, World);
			}
			if (State.Side == 0)
			{
				State.Side = 1;
				Next(State, 1);
				return true;
			}
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(UWorld* World, bool bLake)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->bLake = bLake;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorld Command(
		TEXT("CodexTactics.LowCoverMoveSmoke"),
		TEXT("Dev check: after a skirmish from a low cover a move order moves the operative out of it (spawned 1 m low cover)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { Run(World, false); }));

	static FAutoConsoleCommandWithWorld LakeCommand(
		TEXT("CodexTactics.LowCoverMoveLakeSmoke"),
		TEXT("Dev check: the same at the lake's real Overlook_LowCover."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { Run(World, true); }));
}

#endif // !UE_BUILD_SHIPPING
