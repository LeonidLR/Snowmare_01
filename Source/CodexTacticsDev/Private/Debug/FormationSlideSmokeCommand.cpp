// Dev-only headless check of the rules v2.2 WALL SLIDE (behind Codex.Formation.Sectors; FormationWallRules::Slide in USquadSubsystem) - the
// user's report 2026-10-25: "from the wall layout a move along the wall broke the formation - one man went round behind the wall, then an
// echelon"; user-approved fix ("Все отлично"): everyone side-steps together at the patrol pace, the order kept, nobody passes anybody, nobody
// goes round the wall, the leading end looks ahead along the wall, the leader out, the trailing end back; the wall layout again on the stop.
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands FormationSlideSmoke [-PreCmds "Codex.Formation.Sectors 0"]
//   rendered stills: UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     "-ExecCmds=CodexTactics.FormationSlideSmoke Shot" -> Saved/Screenshots/FormationV22/slide_*.png (mid-move, the squad's vision cones on)
// A runtime arena on L_MovementTest (SmokeUtils::FindClearArena, nothing saved): one long wall (3 m high, 17 m, near face 4 m ahead).
// Group mode: the group cover order at its middle = the wall layout (crouched, facing out); then the leader's move 5 m to the RIGHT along
// the wall (100 cm off it): the slide - the order along the wall kept (20 cm tolerance), nobody on the wall's far side, the open half
// (the outward normal +- 90) covered by the cones >= 90 % of the slide time, the formation stays the wall layout, the patrol pace; on the stop
// the wall layout again (everyone on his slot, crouched, facing out); then 7 m to the LEFT, the same. Codex.Formation.Sectors 0: the old
// group cover (cover slots), no slide. Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/VisionConeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTuning.h"
#include "Formation/FormationWallGroupRules.h"
#include "GameFramework/HUD.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Tactics/CoverTraceRules.h"
#include "UnrealClient.h"

namespace FormationSlideSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float WallNearF = 400.f;
	/** A pair of neighbours out of the start order by more than this counts as passing, cm (contract 11.5 check 11). */
	constexpr float OrderToleranceCm = 20.f;
	constexpr float OrderMinSeconds = 0.3f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShot = false;
		bool bSectors = true;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		FCoverSlot Slot;
		/** The current slide: the members sorted along R at its start, the leader's start, what was seen. */
		TArray<TWeakObjectPtr<AOperativeCharacter>> Order;
		float LeaderStartR = 0.f;
		float WantAlongCm = 0.f;
		int32 SlidesBefore = 0;
		bool bSawSlide = false;
		float SlideTime = 0.f;
		float CoveredTime = 0.f;
		float OrderBrokenTime = 0.f;
		float OrderBrokenWorst = 0.f;
		float FarSideWorst = -1.e9f;
		float NotWallTime = 0.f;
		TArray<float> LeaderSpeeds;
		float PaceMultiplier = 0.f;
		bool bShotTaken = false;
		float ShotFreezeTime = -1.f;
		TWeakObjectPtr<ACameraActor> Camera;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		if (State.Wall.IsValid())
		{
			State.Wall->Destroy();
		}
		FPlatformMisc::RequestExit(false, TEXT("FormationSlideSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	AOperativeCharacter* Member(const FState& State, int32 Index)
	{
		return State.Members.IsValidIndex(Index) ? State.Members[Index].Get() : nullptr;
	}

	FVector Local(const FState& State, float AlongF, float AlongR, float Up = 0.f)
	{
		return State.P + State.F * AlongF + State.R * AlongR + FVector(0.f, 0.f, State.GroundZ - State.P.Z + Up);
	}

	float AlongR(const FState& State, const FVector& Point)
	{
		return static_cast<float>(FVector::DotProduct(Point - State.P, State.R));
	}

	float AlongF(const FState& State, const FVector& Point)
	{
		return static_cast<float>(FVector::DotProduct(Point - State.P, State.F));
	}

	void SpawnWall(UWorld* World, FState& State)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FTransform Transform(State.F.Rotation(), Local(State, WallNearF + 20.f, 0.f, 150.f), FVector(0.4f, 17.f, 3.f));
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (Block)
		{
			Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
			Block->FinishSpawning(Transform);
		}
		State.Wall = Block;
	}

	void PlaceCamera(UWorld* World, FState& State, const FVector& Eye, const FVector& LookAt)
	{
		if (!State.Camera.IsValid())
		{
			State.Camera = World->SpawnActor<ACameraActor>(Eye, (LookAt - Eye).Rotation());
			if (ACameraActor* Created = State.Camera.Get())
			{
				Created->GetCameraComponent()->SetFieldOfView(60.f);
				Created->GetCameraComponent()->bConstrainAspectRatio = false;
			}
		}
		if (ACameraActor* Cam = State.Camera.Get())
		{
			Cam->SetActorLocationAndRotation(Eye, (LookAt - Eye).Rotation());
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				PC->SetViewTarget(Cam);
				if (AHUD* Hud = PC->GetHUD())
				{
					Hud->bShowHUD = false;
				}
			}
		}
	}

	/** Everyone crouched in the wall layout's pose on his slot, turned to his look. */
	bool InWallLayout(const USquadSubsystem* Squad, const FState& State, FString* OutWhy = nullptr)
	{
		bool bAll = Squad->HasFormationWallOrder() && Squad->GetFormationKind() == EFormationKind::Wall && !Squad->IsFormationSliding();
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			const AOperativeCharacter* Each = Weak.Get();
			FVector Slot;
			float Look = 0.f;
			const bool bTarget = Each && Squad->GetFormationTarget(Each, Slot, Look);
			const float Off = bTarget ? static_cast<float>(FVector::Dist2D(Slot, Each->GetActorLocation())) : -1.f;
			const bool bOk = bTarget && Each->IsInFormationWallPose() && Off <= 60.f && Each->GetVelocity().Size2D() < 5.f;
			bAll &= bOk;
			if (OutWhy && Each)
			{
				*OutWhy += FString::Printf(TEXT(" [%s %.0f cm off, pose %d, stance %d]"), *Each->DisplayName.ToString(), Off, Each->IsInFormationWallPose() ? 1 : 0,
					static_cast<int32>(Each->GetStance()));
			}
		}
		return bAll;
	}

	/** Starts a slide order: the leader's move along the wall by AlongCm (100 cm off the face). */
	void StartSlide(FState& State, USquadSubsystem* Squad, AOperativeCharacter* Op, float AlongCm)
	{
		State.Order = State.Members;
		State.Order.Sort([&State](const TWeakObjectPtr<AOperativeCharacter>& A, const TWeakObjectPtr<AOperativeCharacter>& B)
		{
			return AlongR(State, A->GetActorLocation()) < AlongR(State, B->GetActorLocation());
		});
		State.LeaderStartR = AlongR(State, Op->GetActorLocation());
		State.WantAlongCm = AlongCm;
		State.SlidesBefore = Squad->GetFormationSlideCount();
		State.bSawSlide = false;
		State.SlideTime = 0.f;
		State.CoveredTime = 0.f;
		State.OrderBrokenTime = 0.f;
		State.OrderBrokenWorst = 0.f;
		State.FarSideWorst = -1.e9f;
		State.NotWallTime = 0.f;
		State.LeaderSpeeds.Reset();
		State.PaceMultiplier = 0.f;
		State.bShotTaken = false;
		State.ShotFreezeTime = -1.f;
		const FVector Destination = Local(State, WallNearF - 100.f, State.LeaderStartR + AlongCm, 0.f) + FVector(0.f, 0.f, Op->GetSimpleCollisionHalfHeight());
		Op->OrderMoveTo(Destination, false);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the leader's move %.0f cm along the wall (from R %.0f)"), AlongCm, State.LeaderStartR);
	}

	/** One step of the slide's per-frame checks. */
	void TrackSlide(UWorld* World, FState& State, USquadSubsystem* Squad, AOperativeCharacter* Op)
	{
		const bool bSliding = Squad->IsFormationSliding();
		State.bSawSlide |= bSliding;
		if (!bSliding)
		{
			return;
		}
		State.SlideTime += StepSeconds;
		State.NotWallTime += Squad->GetFormationKind() == EFormationKind::Wall ? 0.f : StepSeconds;
		// The order along the wall (sorted at the start) and the far side.
		float Worst = 0.f;
		for (int32 Index = 0; Index + 1 < State.Order.Num(); ++Index)
		{
			const AOperativeCharacter* A = State.Order[Index].Get();
			const AOperativeCharacter* B = State.Order[Index + 1].Get();
			if (A && B)
			{
				Worst = FMath::Max(Worst, AlongR(State, A->GetActorLocation()) - AlongR(State, B->GetActorLocation()));
			}
		}
		State.OrderBrokenTime = Worst > OrderToleranceCm ? State.OrderBrokenTime + StepSeconds : 0.f;
		State.OrderBrokenWorst = FMath::Max(State.OrderBrokenWorst, State.OrderBrokenTime >= OrderMinSeconds ? Worst : 0.f);
		TArray<float> Looks;
		TArray<float> Fovs;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			if (const AOperativeCharacter* Each = Weak.Get())
			{
				State.FarSideWorst = FMath::Max(State.FarSideWorst, AlongF(State, Each->GetActorLocation()) - WallNearF);
				Looks.Add(static_cast<float>(Each->GetSightFacing().Rotation().Yaw));
				Fovs.Add(FormationTuning::Get().SightFov);
			}
		}
		float NormalYaw = 0.f;
		Squad->GetFormationWallNormalYaw(NormalYaw);
		State.CoveredTime += FormationWallGroup::CoveredFraction(Looks, Fovs, NormalYaw, 90.f) >= 0.9f ? StepSeconds : 0.f;
		if (Op->GetVelocity().Size2D() > 30.f)
		{
			State.LeaderSpeeds.Add(static_cast<float>(Op->GetVelocity().Size2D()));
			State.PaceMultiplier = Op->GetFormationPaceMultiplier();
		}
	}

	void CheckSlide(FState& State, USquadSubsystem* Squad, AOperativeCharacter* Op, const TCHAR* Label)
	{
		Check(State, State.bSawSlide && Squad->GetFormationSlideCount() > State.SlidesBefore,
			FString::Printf(TEXT("%s: the move along the wall is the wall slide (%.1f s)"), Label, State.SlideTime));
		const float Moved = AlongR(State, Op->GetActorLocation()) - State.LeaderStartR;
		Check(State, FMath::Abs(Moved - State.WantAlongCm) <= 80.f, FString::Printf(TEXT("%s: the leader moved %.0f cm along the wall (ordered %.0f)"), Label, Moved,
			State.WantAlongCm));
		Check(State, State.OrderBrokenWorst <= 0.f, FString::Printf(TEXT("%s: the order along the wall kept - nobody passes anybody (worst %.0f cm over %.0f for >= %.1f s)"),
			Label, State.OrderBrokenWorst, OrderToleranceCm, OrderMinSeconds));
		Check(State, State.FarSideWorst < 0.f, FString::Printf(TEXT("%s: nobody behind the wall (the farthest %.0f cm short of its face)"), Label, -State.FarSideWorst));
		Check(State, State.NotWallTime < 0.2f, FString::Printf(TEXT("%s: the formation stays the wall layout while sliding (%.1f s otherwise)"), Label, State.NotWallTime));
		const float Covered = State.SlideTime > 0.f ? State.CoveredTime / State.SlideTime : 0.f;
		Check(State, Covered >= 0.9f, FString::Printf(TEXT("%s: the open half (normal +- 90) covered >= 90 %% by the cones for %.0f %% of the slide"), Label, Covered * 100.f));
		// The steady pace (80th percentile of the moving samples: a short slide is mostly speeding up and slowing down).
		TArray<float> Speeds = State.LeaderSpeeds;
		Speeds.Sort();
		const float Pace = Speeds.IsEmpty() ? 0.f : Speeds[FMath::Clamp(FMath::FloorToInt32(0.8f * Speeds.Num()), 0, Speeds.Num() - 1)];
		const float Want = Op->MovementConfig.WalkSpeed * FormationTuning::Get().PacePatrol;
		Check(State, FMath::IsNearlyEqual(State.PaceMultiplier, FormationTuning::Get().PacePatrol, 0.01f) && FMath::Abs(Pace - Want) <= 0.1f * Want,
			FString::Printf(TEXT("%s: at the patrol pace (x%.2f, steady %.0f cm/s, want %.0f)"), Label, State.PaceMultiplier, Pace, Want));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 200.f)
		{
			Check(State, false, FString::Printf(TEXT("timeout (stage %d)"), State.Stage));
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Op = Member(State, 0);
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f || !Squad || !PC || Squad->GetMembers().Num() < 3)
			{
				return true;
			}
			State.bSectors = FormationRuntime::IsEnabled();
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& X, const AOperativeCharacter& Y) { return X.SquadIndex < Y.SquadIndex; });
			TArray<const AActor*> Ignore;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				State.Members.Add(Roster[Index]);
				Ignore.Add(Roster[Index]);
			}
			Ignore.Add(PC->GetPawn());
			const FVector Feet = Roster[0]->GetActorLocation() - FVector(0.f, 0.f, Roster[0]->GetSimpleCollisionHalfHeight());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Roster[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-500.f, -950.f), FVector2D(800.f, 950.f)), Ignore);
			if (!Arena.bFound)
			{
				Check(State, false, TEXT("an empty, flat, navigable 13 x 19 m spot for the smoke's own set-up"));
				return Finish(State, false);
			}
			State.P = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			State.GroundZ = Arena.Origin.Z;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: arena at %s, Codex.Formation.Sectors %d"), *Arena.Origin.ToCompactString(), State.bSectors ? 1 : 0);
			Squad->ExitSoloMode(false, 0.f);
			Squad->SetLeader(Roster[0]);
			for (int32 Index = 0; Index < 3; ++Index)
			{
				AOperativeCharacter* Each = Roster[Index];
				Each->StopOperative();
				Each->SetStance(EOperativeStance::Standing);
				Each->TeleportTo(Local(State, Index == 0 ? 0.f : -300.f, Index == 0 ? 0.f : (Index == 1 ? -150.f : 150.f), Each->GetSimpleCollisionHalfHeight() + 5.f),
					State.F.Rotation(), false, true);
			}
			Squad->ResetFormationState(State.F);
			SpawnWall(World, State);
			if (State.bShot)
			{
				UVisionConeComponent::SetPlayerSquadCones(true);
			}
			Next(State, 1);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 3.f)
			{
				return true; // the navmesh rebuilds round the wall
			}
			const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, Local(State, WallNearF, 0.f, 90.f), State.F, State.Slot);
			Check(State, bSlot, TEXT("set-up: a cover slot in the middle of the long wall"));
			const bool bOrdered = bSlot && PC->OrderGroupCover(Op, State.Slot, false, 1200.f, false);
			Check(State, bOrdered && (State.bSectors ? Squad->HasFormationWallOrder() : !Squad->HasFormationWallOrder()),
				FString::Printf(TEXT("the group cover order in group mode %s"), State.bSectors ? TEXT("runs as the formation wall layout") : TEXT("is the old cover-slot order [Sectors 0]")));
			if (!bOrdered)
			{
				return Finish(State, false);
			}
			Next(State, 2);
			return true;
		}
		case 2:
		case 5:
		case 8:
		{
			// The wall layout stands (crouched, facing out) - at the start and after each slide.
			if (!State.bSectors)
			{
				if (State.StageTime < 8.f)
				{
					return true;
				}
				// Sectors 0: the old order - the leader's move along the wall is an ordinary move, no slide.
				StartSlide(State, Squad, Op, 500.f);
				Next(State, 20);
				return true;
			}
			FString Why;
			const bool bLayout = InWallLayout(Squad, State, &Why);
			if ((!bLayout || State.StageTime < 1.f) && State.StageTime < 25.f)
			{
				return true;
			}
			const TCHAR* Label = State.Stage == 2 ? TEXT("start") : State.Stage == 5 ? TEXT("after the slide right") : TEXT("after the slide left");
			Check(State, bLayout, FString::Printf(TEXT("%s: the wall layout - everyone on his slot, crouched facing out [%s ]"), Label, *Why));
			if (State.Stage == 8)
			{
				return Finish(State, true);
			}
			StartSlide(State, Squad, Op, State.Stage == 2 ? 500.f : -700.f);
			Next(State, State.Stage + 1);
			return true;
		}
		case 3:
		case 6:
		{
			// The slide: track until the leader stopped and the layout stands again.
			if (State.bShot && !State.bShotTaken && Squad->IsFormationSliding() && State.SlideTime >= 1.6f)
			{
				// Mid-move still: the game slowed to a standstill, the camera above the line.
				if (State.ShotFreezeTime < 0.f)
				{
					UGameplayStatics::SetGlobalTimeDilation(World, 0.01f);
					FVector Centre = FVector::ZeroVector;
					for (int32 Index = 0; Index < 3; ++Index)
					{
						Centre += Member(State, Index)->GetActorLocation() / 3.f;
					}
					PlaceCamera(World, State, Centre - State.F * 700.f + FVector(0.f, 0.f, 800.f), Centre - State.F * 120.f);
					State.ShotFreezeTime = State.StageTime;
					return true;
				}
				if (State.StageTime - State.ShotFreezeTime >= 2.f)
				{
					const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("FormationV22") /
						(State.Stage == 3 ? TEXT("slide_right_midmove.png") : TEXT("slide_left_midmove.png"));
					FScreenshotRequest::RequestScreenshot(Path, false, false);
					UE_LOG(LogCodexTactics, Display, TEXT("FormationSlide: screenshot %s"), *Path);
					State.bShotTaken = true;
					State.ShotFreezeTime = State.StageTime;
				}
				return true;
			}
			if (State.bShot && State.bShotTaken && State.ShotFreezeTime >= 0.f)
			{
				if (State.StageTime - State.ShotFreezeTime < 1.f)
				{
					return true;
				}
				UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
				PC->SetViewTarget(PC->GetPawn());
				State.ShotFreezeTime = -1.f;
			}
			TrackSlide(World, State, Squad, Op);
			const bool bDone = State.bSawSlide && !Squad->IsFormationSliding() && !Op->IsMoving();
			if (!bDone && State.StageTime < 30.f)
			{
				return true;
			}
			CheckSlide(State, Squad, Op, State.Stage == 3 ? TEXT("slide right (5 m)") : TEXT("slide left (7 m)"));
			Next(State, State.Stage + 2);
			return true;
		}
		case 20:
		{
			// Sectors 0: no slide; the order is an ordinary move.
			if (State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, !Squad->IsFormationSliding() && Squad->GetFormationSlideCount() == State.SlidesBefore,
				TEXT("[Sectors 0] the move along the wall is an ordinary move (no slide)"));
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
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Shot"), ESearchCase::IgnoreCase); });
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FormationSlideSmoke"),
		TEXT("Dev check (rules v2.2 wall slide): from the wall layout a move along the same wall side-steps the line, the order kept; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
