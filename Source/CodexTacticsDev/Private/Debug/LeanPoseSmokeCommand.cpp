// Dev-only headless check of the corner LEAN POSE (game designer 2026-10-20: "three operatives crouched behind a low wall, the one at the
// left end is in the corner peek - he sees and is seen - but he looks fully hidden behind the barricade"):
//   Scripts/smoke_parallel.ps1 -Commands LeanPoseSmoke
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -ExecCmds="CodexTactics.LeanPoseSmoke Shot"    -> Saved/Screenshots/lean_left.png, lean_right.png (+ _front views)
// A runtime arena on L_MovementTest, exploring: a 2.5 m high wall (R -200..200) and a 60 cm low barricade (R -1700..-1300), both at F 600
// ahead of the leader (arena frame as in PeekSightSmoke), the mates parked prone behind a screen, no enemies. For each of the four wall
// ends (low left / low right / high left / high right) the leader takes cover 1.5 m from the end, the real click on the end orders the
// corner peek, and with the peek sight state active the body must LEAN OUT past the edge:
//   - the head bone is beyond the wall's edge by >= 20 cm and within 30 cm of the peek eye point (GetPeekSight);
//   - the leading shoulder is beyond the edge and the rifle's muzzle is out of the barricade (past the end or in front of its face);
//   - on leaving the cover the mesh is back at its default place (GetMeshBaseOffsetCm / the tracked entry offset zero).
// Every number is logged ("LeanPose ..."), so before / after a change can be compared. Nothing is saved.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/PeekSightRules.h"
#include "Combat/SightRules.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Tactics/CoverTraceRules.h"
#include "UnrealClient.h"

namespace LeanPoseSmoke
{
	constexpr float StepSeconds = 0.05f;

	/** One wall end the lean is checked at. */
	struct FCase
	{
		const TCHAR* Name;
		bool bHigh;
		/** The edge's R coordinate (arena frame) and the outward direction along R (+1 / -1). */
		float EdgeRight;
		float Outward;
		/** Screenshot name (without .png) in Shot mode, or nullptr. */
		const TCHAR* ShotName;
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		int32 CaseIndex = 0;
		bool bShot = false;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		float BaseMeshOffset = 0.f;
		int32 ShotsPending = 0;
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<AStaticMeshActor> HighWall;
		TWeakObjectPtr<AStaticMeshActor> LowWall;
		TWeakObjectPtr<AStaticMeshActor> Screen;
		TWeakObjectPtr<ACameraActor> Cameras[2];
		TMap<TWeakObjectPtr<AOperativeCharacter>, FVector> Parked;
	};

	const FCase Cases[] = {
		{ TEXT("low wall, left end"), false, -1700.f, -1.f, TEXT("lean_left") },
		{ TEXT("low wall, right end"), false, -1300.f, 1.f, TEXT("lean_right") },
		{ TEXT("high wall, left end"), true, -200.f, -1.f, TEXT("lean_high_left") },
		{ TEXT("high wall, right end"), true, 200.f, 1.f, TEXT("lean_high_right") },
	};

	AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Centre, Scale),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(FTransform(Rotation, Centre, Scale));
		return Block;
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("LeanPoseSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	FVector At(const FState& State, float Forward, float Right)
	{
		const FVector Point = State.P + State.F * Forward + State.R * Right;
		return FVector(Point.X, Point.Y, State.GroundZ);
	}

	float RightOf(const FState& State, const FVector& Where)
	{
		return static_cast<float>(FVector::DotProduct(Where - State.P, State.R));
	}

	float ForwardOf(const FState& State, const FVector& Where)
	{
		return static_cast<float>(FVector::DotProduct(Where - State.P, State.F));
	}

	FString Frame(const FState& State, const FVector& Where)
	{
		return FString::Printf(TEXT("(F %.0f, R %.0f, Z %.0f)"), ForwardOf(State, Where), RightOf(State, Where), Where.Z - State.GroundZ);
	}

	bool TakeCoverAt(UWorld* World, AOperativeCharacter* Op, const FState& State, float Forward, float Right, float HeightCm)
	{
		const FVector Click = At(State, Forward, Right) + FVector(0.f, 0.f, HeightCm);
		FCoverSlot Slot;
		const bool bFound = CoverTraceRules::FindCoverSlotAt(World, Click, State.F, Slot);
		return bFound && Op->OrderTakeCover(Slot, /*bSprint*/ false) == EOperativeOrderResult::Accepted;
	}

	/** The real input: a click on the near face of Wall 20 cm in from its end at EndRight (PeekSightSmoke). */
	bool ClickWallEnd(ACodexTacticsPlayerController* PC, AOperativeCharacter* Op, FState& State, AStaticMeshActor* Wall, float EndRight, float HeightCm)
	{
		const FVector Normal = -State.F;
		const FVector EdgePoint = At(State, 585.f, EndRight - FMath::Sign(EndRight - RightOf(State, Op->GetActorLocation())) * 20.f) + FVector(0.f, 0.f, HeightCm);
		const FHitResult EdgeHit(Wall, Wall->GetStaticMeshComponent(), EdgePoint, Normal);
		ECoverFacing Side = ECoverFacing::Left;
		float Edge = 0.f;
		const bool bTarget = PC->FindCornerPeekForHit(Op, EdgeHit, Side, Edge);
		Check(State, bTarget, FString::Printf(TEXT("the wall's end at %s is a corner peek target (side %s, %.0f cm)"), *Frame(State, EdgePoint),
			Side == ECoverFacing::Left ? TEXT("left") : TEXT("right"), Edge));
		return bTarget && PC->TryHandleCoverClick(EdgeHit, nullptr) && Op->IsManualCornerPeek();
	}

	/** Beyond-the-edge distance of a world point for the case: > 0 = past the wall's end, in the open. */
	float BeyondEdge(const FState& State, const FCase& Case, const FVector& Where)
	{
		return (RightOf(State, Where) - Case.EdgeRight) * Case.Outward;
	}

	/** Puts the still camera on the arena (Shot mode): Angle 0 = the game's view from behind the wall, 1 = from the open side. */
	void PlaceCamera(UWorld* World, FState& State, const AOperativeCharacter* Op, const FCase& Case, int32 Angle)
	{
		const FVector Centre = Op->GetActorLocation() + State.R * Case.Outward * 35.f;
		FVector Eye;
		if (Angle == 0)
		{
			Eye = Centre + State.F * 380.f + State.R * Case.Outward * -60.f + FVector(0.f, 0.f, 430.f);
		}
		else
		{
			Eye = Centre - State.F * 330.f + State.R * Case.Outward * 160.f + FVector(0.f, 0.f, 120.f);
		}
		const FRotator Look = (Centre + FVector(0.f, 0.f, -15.f) - Eye).Rotation();
		// One camera actor per angle (re-targeting the player's view to a fresh actor is what the other still commands do).
		TWeakObjectPtr<ACameraActor>& Slot = State.Cameras[Angle];
		if (!Slot.IsValid())
		{
			Slot = World->SpawnActor<ACameraActor>(Eye, Look);
			if (ACameraActor* Created = Slot.Get())
			{
				Created->GetCameraComponent()->SetFieldOfView(50.f);
				Created->GetCameraComponent()->bConstrainAspectRatio = false;
			}
		}
		if (ACameraActor* Camera = Slot.Get())
		{
			Camera->SetActorLocationAndRotation(Eye, Look);
			UE_LOG(LogCodexTactics, Display, TEXT("LeanPose: camera %d at %s looking %s"), Angle, *Frame(State, Eye), *Look.ToCompactString());
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				PC->SetViewTarget(Camera);
				if (AHUD* Hud = PC->GetHUD())
				{
					Hud->bShowHUD = false;
				}
			}
		}
	}

	void RequestShot(const TCHAR* Name)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / FString::Printf(TEXT("%s.png"), Name);
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogCodexTactics, Display, TEXT("LeanPose: screenshot %s"), *Path);
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
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		if (!Flow || !Squad || !PC || !Squad->GetLeader() || State.Time > 240.f)
		{
			Check(State, false, FString::Printf(TEXT("subsystems / controller / timeout (stage %d, case %d)"), State.Stage, State.CaseIndex));
			return Finish(State, false);
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = true;
			Member->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
		for (const TPair<TWeakObjectPtr<AOperativeCharacter>, FVector>& Pair : State.Parked)
		{
			AOperativeCharacter* Mate = Pair.Key.Get();
			if (Mate && (FVector::Dist2D(Mate->GetActorLocation(), Pair.Value) > 50.f || Mate->GetStance() != EOperativeStance::Prone))
			{
				Mate->StopOperative();
				Mate->TeleportTo(Pair.Value + FVector(0.f, 0.f, Mate->GetSimpleCollisionHalfHeight() + 5.f), State.F.Rotation(), false, true);
				Mate->SetStance(EOperativeStance::Prone);
			}
		}
		AOperativeCharacter* Op = State.Op.Get();
		if (State.Stage > 0 && !Op)
		{
			Check(State, false, TEXT("leader lost"));
			return Finish(State, false);
		}
		const FCase* Case = State.CaseIndex < static_cast<int32>(UE_ARRAY_COUNT(Cases)) ? &Cases[State.CaseIndex] : nullptr;
		switch (State.Stage)
		{
		case 0:
		{
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("exploring"));
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			State.Op = Squad->GetLeader();
			Op = State.Op.Get();
			State.F = Op->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = Op->GetActorLocation();
			State.GroundZ = Op->GetActorLocation().Z - Op->GetSimpleCollisionHalfHeight();
			const FRotator Facing = State.F.Rotation();
			State.HighWall = SpawnBlock(World, At(State, 600.f, 0.f) + FVector(0.f, 0.f, 125.f), Facing, FVector(0.3f, 4.f, 2.5f));
			State.LowWall = SpawnBlock(World, At(State, 600.f, -1500.f) + FVector(0.f, 0.f, 30.f), Facing, FVector(0.3f, 4.f, 0.6f));
			State.Screen = SpawnBlock(World, At(State, -700.f, -500.f) + FVector(0.f, 0.f, 200.f), Facing, FVector(0.3f, 40.f, 4.f));
			Check(State, State.HighWall.IsValid() && State.LowWall.IsValid() && State.Screen.IsValid(), TEXT("arena: high wall, 60 cm low wall, screen"));
			if (!State.HighWall.IsValid() || !State.LowWall.IsValid())
			{
				return Finish(State, false);
			}
			int32 Index = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->StopOperative();
				if (Member != Op)
				{
					const FVector Back = At(State, -900.f, Index++ % 2 == 0 ? -200.f : 200.f);
					Member->TeleportTo(Back + FVector(0.f, 0.f, Member->GetSimpleCollisionHalfHeight() + 5.f), Facing, false, true);
					Member->SetStance(EOperativeStance::Prone);
					State.Parked.Add(Member, Back);
				}
			}
			Op->bIgnoreCoverThreatForTesting = true; // no automatic corner hold: only the ordered peek leans out
			Next(State, 1);
			return true;
		}
		case 1: // into cover 1.5 m from the case's end
		{
			if (!Case)
			{
				return Finish(State, true);
			}
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			if (Op->bInCover)
			{
				Op->LeaveCover(TEXT("LeanPoseSmoke: next case"));
			}
			const float Height = Case->bHigh ? 90.f : 30.f;
			const float Right = Case->EdgeRight - Case->Outward * 150.f;
			Op->bIgnoreCoverThreatForTesting = true;
			Op->SetStance(Case->bHigh ? EOperativeStance::Standing : EOperativeStance::Crouching);
			UE_LOG(LogCodexTactics, Display, TEXT("LeanPose: --- %s (edge R %.0f, outward %+.0f) ---"), Case->Name, Case->EdgeRight, Case->Outward);
			Check(State, TakeCoverAt(World, Op, State, 570.f, Right, Height), FString::Printf(TEXT("%s: he takes cover 1.5 m from the end"), Case->Name));
			Next(State, 2);
			return true;
		}
		case 2: // settled: hugging, no peek -> click the end
		{
			if ((!Op->bInCover || Op->bShimmying) && State.StageTime < 20.f)
			{
				return true;
			}
			if (State.StageTime < 2.f)
			{
				return true;
			}
			const ECoverHeight Want = Case->bHigh ? ECoverHeight::HighCover : ECoverHeight::LowCover;
			Check(State, Op->bInCover && Op->CurrentCoverHeight == Want, FString::Printf(TEXT("%s: in %s cover at %s"), Case->Name,
				Case->bHigh ? TEXT("high") : TEXT("low"), *Frame(State, Op->GetActorLocation())));
			State.BaseMeshOffset = Op->GetMeshBaseOffsetCm();
			const float Height = Case->bHigh ? 150.f : 30.f;
			Check(State, ClickWallEnd(PC, Op, State, Case->bHigh ? State.HighWall.Get() : State.LowWall.Get(), Case->EdgeRight, Height),
				FString::Printf(TEXT("%s: the click on the end orders the corner peek"), Case->Name));
			Next(State, 3);
			return true;
		}
		case 3: // leaned out: measure
		{
			if (!Op->IsPeekingRoundCorner() && State.StageTime < 20.f)
			{
				return true;
			}
			if (State.StageTime < 3.f)
			{
				return true; // enter clip + hold
			}
			USkeletalMeshComponent* Mesh = Op->GetMesh();
			const FVector Head = Mesh->GetBoneLocation(TEXT("head"));
			const FVector ShoulderL = Mesh->GetBoneLocation(TEXT("clavicle_l"));
			const FVector ShoulderR = Mesh->GetBoneLocation(TEXT("clavicle_r"));
			const FVector Pelvis = Mesh->GetBoneLocation(TEXT("pelvis"));
			const FVector Leading = BeyondEdge(State, *Case, ShoulderL) > BeyondEdge(State, *Case, ShoulderR) ? ShoulderL : ShoulderR;
			const FVector Muzzle = Op->GetWeaponMuzzleLocation();
			FVector Eye;
			FVector Direction;
			const bool bPeek = Op->GetPeekSight(Eye, Direction);
			const float HeadBeyond = BeyondEdge(State, *Case, Head);
			const float ShoulderBeyond = BeyondEdge(State, *Case, Leading);
			const float MuzzleBeyond = BeyondEdge(State, *Case, Muzzle);
			const float FireOriginBeyond = BeyondEdge(State, *Case, Op->GetCoverFireOrigin());
			const float PelvisBeyond = BeyondEdge(State, *Case, Pelvis);
			const float EyeBeyond = BeyondEdge(State, *Case, Eye);
			const float HeadToEye2D = static_cast<float>(FVector::Dist2D(Head, Eye));
			const float HeadToEye = static_cast<float>(FVector::Dist(Head, Eye));
			UE_LOG(LogCodexTactics, Display,
				TEXT("LeanPose: %s | beyond edge: head %.0f, leading shoulder %.0f, pelvis %.0f, muzzle %.0f (logical fire origin %.0f), peek eye %.0f | head %s eye %s | head-eye %.0f (2D %.0f) | mesh offset %.0f (base %.0f), entry offset %s | slot edges L %d %.0f R %d %.0f | stand %d"),
				Case->Name, HeadBeyond, ShoulderBeyond, PelvisBeyond, MuzzleBeyond, FireOriginBeyond, EyeBeyond, *Frame(State, Head), *Frame(State, Eye), HeadToEye, HeadToEye2D,
				Op->GetMeshBaseOffsetCm(), State.BaseMeshOffset, *Op->GetCoverEntryMeshOffset().ToCompactString(), Op->GetCoverSlot().bLeftEdgeExposed ? 1 : 0,
				Op->GetCoverSlot().LeftEdgeDistanceCm, Op->GetCoverSlot().bRightEdgeExposed ? 1 : 0, Op->GetCoverSlot().RightEdgeDistanceCm, static_cast<int32>(Op->GetStance()));
			Check(State, Op->IsPeekingRoundCorner() && bPeek && Op->GetPeekSightPose() == EPeekSightPose::CornerLean, FString::Printf(TEXT("%s: the peek sight state is active"), Case->Name));
			Check(State, HeadBeyond >= 20.f, FString::Printf(TEXT("%s: the head is %.0f cm past the wall's end (>= 20)"), Case->Name, HeadBeyond));
			Check(State, ShoulderBeyond >= 0.f, FString::Printf(TEXT("%s: the leading shoulder is past the end (%.0f cm)"), Case->Name, ShoulderBeyond));
			// The rifle points into the space behind the wall (100 deg past the along-wall line), so its muzzle is not always past the end along
			// the wall: it must be out of the barricade: past the end, or in front of the wall's near face (F 585). On the pack _L side (his right corner) the
			// barrel runs along the wall face with its muzzle ~10-25 cm short of the end (the clip; the body and head are past it).
			const float MuzzleForward = ForwardOf(State, Muzzle);
			Check(State, MuzzleBeyond >= 0.f || MuzzleForward <= 584.f, FString::Printf(TEXT("%s: the rifle is not inside the barricade (muzzle %.0f cm past the end, F %.0f; wall face F 585)"),
				Case->Name, MuzzleBeyond, MuzzleForward));
			Check(State, bPeek && HeadToEye <= 30.f, FString::Printf(TEXT("%s: the peek eye point is %.0f cm from the visible head (2D %.0f; <= 30)"), Case->Name, HeadToEye, HeadToEye2D));
			if (State.bShot && Case->ShotName)
			{
				PlaceCamera(World, State, Op, *Case, 0);
				State.ShotsPending = 1;
				Next(State, 31);
				return true;
			}
			Next(State, 4);
			return true;
		}
		case 31: // Shot mode: two stills; the capture happens at the end of the frame, so the camera moves only after a wait
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			if (State.ShotsPending == 1)
			{
				RequestShot(Case->ShotName);
				State.ShotsPending = 2;
			}
			else if (State.ShotsPending == 2)
			{
				PlaceCamera(World, State, Op, *Case, 1);
				State.ShotsPending = 3;
			}
			else
			{
				RequestShot(*FString::Printf(TEXT("%s_front"), Case->ShotName));
				Next(State, 32);
				return true;
			}
			State.StageTime = 0.f;
			return true;
		}
		case 32:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Next(State, 4);
			return true;
		}
		case 4: // out of cover: the mesh is back at its place
		{
			Op->LeaveCover(TEXT("LeanPoseSmoke: peek over"));
			Next(State, 5);
			return true;
		}
		case 5:
		{
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			Check(State, Op->GetMeshBaseOffsetCm() <= 1.f && Op->GetCoverEntryMeshOffset().Size() <= 1.f,
				FString::Printf(TEXT("%s: out of cover the mesh is back at its default place (offset %.1f cm, tracked entry offset %.1f cm)"), Case->Name,
					Op->GetMeshBaseOffsetCm(), Op->GetCoverEntryMeshOffset().Size()));
			++State.CaseIndex;
			Next(State, 1);
			return true;
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
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = Args.Contains(TEXT("Shot"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LeanPoseSmoke"),
		TEXT("Dev check of the corner lean pose (2026-10-20): in the peek sight state the body visibly leans out past the wall's end (head, shoulder, muzzle), low / high wall, both ends; PASS / FAIL. Arg 'Shot': rendered stills."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
