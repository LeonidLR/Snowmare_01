// Dev-only headless check of the TWO-CORNER HOLD (#57, user request 2026-11-07: "the squad pressed against a wall, a peek on the right sends
// the right man to that corner; then a peek on the left sends the left man there - but the right man leaves his position. Each of them must
// hold his own edge. The one left in the middle should scan from the left corner to the right"):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands TwoCornerHoldSmoke
//   (map /Game/Maps/L_FormationLab, "-LevelJson=L_FormationLab.json" - Scripts/test_map.json smokeOptions)
// The lab wall W_B_3 (x 1500..4000 at y -2700; its south face y -2720), the squad of three south of it in group mode (formation). The two
// clicks mid-wall = the formation wall layout; the hover + click on the EAST end (the squad's right, facing out south) -> the east-end man
// peeks there and leads; the click on the WEST end -> the west-end man peeks there and leads, the east man KEEPS his corner peek (never out
// of it), the middle man stays where he stood; over two sweeps the middle man's facing sweeps across the arc between both corners
// (sampled min / max yaw against his directions to both corners along the wall, never into the wall); a repeated west click swaps nothing; a ground click away (a move order)
// ends both holds.
// A/B: -PreCmds "Codex.Formation.TwoCornerHold 0" = the rule before #57 (the east man leaves his corner: the checks fail).
// Godot reference: none (UE-only; docs/systems/squad-and-orders.md "Both corners held").

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Data/SquadWallTuning.h"
#include "Engine/World.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTypes.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

namespace TwoCornerHoldSmoke
{
	/** W_B_3 (Content/Data/FormationLab/formation_lab_01.json): x 1500..4000, its south face at y -2720 (outward = south, yaw -90). */
	constexpr float WallWestX = 1500.f;
	constexpr float WallEastX = 4000.f;
	constexpr float WallFaceY = -2720.f;
	constexpr float OutwardYaw = -90.f;
	constexpr float StartY = -3300.f;
	constexpr float MidX = 2750.f;
	constexpr float HoldSeconds = 1.5f;
	constexpr float AtEndCm = 130.f;
	/** "Stays where he stood": the middle man within this of his place at the second click, cm. */
	constexpr float StayCm = 60.f;
	/** The sweep reaches within this of each corner's look, deg. */
	constexpr float SweepReachDeg = 30.f;

	enum class EClick : uint8
	{
		None,
		WestEnd,
		EastEnd,
		AwayGround
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		float GroundZ = 0.f;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TWeakObjectPtr<AOperativeCharacter> East;
		TWeakObjectPtr<AOperativeCharacter> West;
		TWeakObjectPtr<AOperativeCharacter> MiddleMan;
		FVector MiddleAt = FVector::ZeroVector;
		float MiddleMaxMoveCm = 0.f;
		bool bEastLeft = false;
		float EastLeftAt = -1.f;
		float HoldTime = 0.f;
		/** The sweep: the middle man's facing off the outward normal (min / max), both corner looks, the frames swept. */
		float MinYawOff = 999.f;
		float MaxYawOff = -999.f;
		float CornerOffA = 0.f;
		float CornerOffB = 0.f;
		int32 SweptFrames = 0;
		int32 SecondCornersBefore = 0;
		EClick PendingClick = EClick::None;
		bool bClickDone = false;
		FDelegateHandle PreTickHandle;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		FWorldDelegates::OnWorldPreActorTick.Remove(State.PreTickHandle);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TwoCornerHoldSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.HoldTime = 0.f;
		State.bClickDone = false;
	}

	FString NameOf(const AOperativeCharacter* Op)
	{
		return Op ? Op->DisplayName.ToString() : FString(TEXT("-"));
	}

	FString Where(const FState& State)
	{
		FString Line;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			if (const AOperativeCharacter* Op = Weak.Get())
			{
				Line += FString::Printf(TEXT(" %s (%.0f, %.0f) yaw %.0f%s%s%s;"), *NameOf(Op), Op->GetActorLocation().X, Op->GetActorLocation().Y, Op->GetActorRotation().Yaw,
					Op->bInCover ? TEXT(" in cover") : TEXT(""), Op->IsManualCornerPeek() ? TEXT(" peeking") : TEXT(""), Op->IsInFormationWallPose() ? TEXT(" wall pose") : TEXT(""));
			}
		}
		return Line;
	}

	bool FaceHit(UWorld* World, const FState& State, float X, FHitResult& OutHit)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TwoCornerHoldSmokeFace), false);
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			Params.AddIgnoredActor(Weak.Get());
		}
		const float Z = State.GroundZ + 120.f;
		return World->LineTraceSingleByChannel(OutHit, FVector(X, WallFaceY - 300.f, Z), FVector(X, WallFaceY + 15.f, Z), ECC_Visibility, Params)
			&& FMath::Abs(OutHit.ImpactPoint.Y - WallFaceY) <= 15.f;
	}

	bool GroundHit(UWorld* World, const FState& State, float X, float Y, FHitResult& OutHit)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TwoCornerHoldSmokeGround), false);
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			Params.AddIgnoredActor(Weak.Get());
		}
		return World->LineTraceSingleByChannel(OutHit, FVector(X, Y, State.GroundZ + 500.f), FVector(X, Y, State.GroundZ - 500.f), ECC_Visibility, Params);
	}

	bool InPeekPose(const AOperativeCharacter* Op)
	{
		return Op && Op->bInCover && Op->IsManualCornerPeek() && Op->IsCornerAimActive() && Op->bAtCoverCorner && !Op->bShimmying;
	}

	bool AtEnd(const AOperativeCharacter* Op, bool bWest)
	{
		return Op && FMath::Abs(Op->GetActorLocation().X - (bWest ? WallWestX : WallEastX)) <= AtEndCm;
	}

	TArray<AOperativeCharacter*> AlongWall(const FState& State)
	{
		TArray<AOperativeCharacter*> Sorted;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			if (AOperativeCharacter* Op = Weak.Get())
			{
				Sorted.Add(Op);
			}
		}
		Sorted.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.GetActorLocation().X < B.GetActorLocation().X; });
		return Sorted;
	}

	/** The player's click inside the world tick (as the controller's PlayerTick lands it): the hover on the end, then the click. */
	void WorldTickClick(UWorld* World, FState& State)
	{
		const EClick Click = State.PendingClick;
		State.PendingClick = EClick::None;
		State.bClickDone = true;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !PC || !Leader)
		{
			return;
		}
		FHitResult Hit;
		if (Click == EClick::AwayGround)
		{
			if (GroundHit(World, State, MidX, WallFaceY - 700.f, Hit))
			{
				PC->HandleWorldHit(Hit);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the ground click 7 m off the wall at (%.0f, %.0f)"), Hit.ImpactPoint.X, Hit.ImpactPoint.Y);
			return;
		}
		const bool bWest = Click == EClick::WestEnd;
		const float X = bWest ? WallWestX + 20.f : WallEastX - 20.f;
		if (!FaceHit(World, State, X, Hit))
		{
			Check(State, false, FString::Printf(TEXT("the cursor hit on W_B_3's %s end"), bWest ? TEXT("west") : TEXT("east")));
			return;
		}
		const bool bGhost = PC->UpdateCornerPeekHover(Leader, &Hit);
		PC->HandleWorldHit(Hit);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the click on the %s end (ghost %d) -> leader %s, peeker %s, held corner %s;%s"), bWest ? TEXT("west") : TEXT("east"),
			bGhost ? 1 : 0, *NameOf(Squad->GetLeader()), *NameOf(Squad->GetFormationPeeker()), *NameOf(Squad->GetHeldCornerOperative()), *Where(State));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float DeltaTime)
	{
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 300.f)
		{
			Check(State, false, FString::Printf(TEXT("timeout (stage %d)"), State.Stage));
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		if (State.Stage == 0)
		{
			if (State.StageTime < 3.f || !Squad || !PC || Squad->GetMembers().Num() < 3)
			{
				return true;
			}
			Check(State, FormationRuntime::IsEnabled() && USquadSubsystem::IsWallLeadershipEnabled() && USquadSubsystem::IsTwoCornerHoldEnabled(),
				TEXT("Codex.Formation.Sectors / WallLeadership / TwoCornerHold are on"));
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& X, const AOperativeCharacter& Y) { return X.SquadIndex < Y.SquadIndex; });
			FCollisionQueryParams Params(SCENE_QUERY_STAT(TwoCornerHoldSmokeStart), false);
			for (int32 Index = 0; Index < 3; ++Index)
			{
				State.Members.Add(Roster[Index]);
				Params.AddIgnoredActor(Roster[Index]);
				if (USkeletalMeshComponent* Mesh = Roster[Index]->GetMesh())
				{
					Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				}
			}
			FHitResult Ground;
			State.GroundZ = World->LineTraceSingleByChannel(Ground, FVector(MidX, StartY, 2000.f), FVector(MidX, StartY, -2000.f), ECC_Visibility, Params)
				? static_cast<float>(Ground.ImpactPoint.Z) : 0.f;
			FHitResult Probe;
			const bool bWall = FaceHit(World, State, MidX, Probe) && FaceHit(World, State, WallWestX + 20.f, Probe) && FaceHit(World, State, WallEastX - 20.f, Probe);
			Check(State, bWall, TEXT("the lab wall W_B_3 stands where the layout puts it"));
			if (!bWall)
			{
				return Finish(State, false);
			}
			// The squad of three south of the wall in group mode (formation), the Commander leading.
			Squad->ExitSoloMode(false, 0.f);
			Squad->SetLeader(Roster[0]);
			for (int32 Index = 0; Index < 3; ++Index)
			{
				AOperativeCharacter* Each = Roster[Index];
				Each->LeaveCover(TEXT("smoke reset"));
				Each->ClearPendingCover();
				Each->StopOperative();
				Each->SetFormationWallPose(false);
				Each->SetStance(EOperativeStance::Standing);
				const FVector At(MidX + (Index == 0 ? 0.f : (Index == 1 ? -150.f : 150.f)), StartY - (Index == 0 ? 0.f : 200.f), State.GroundZ + Each->GetSimpleCollisionHalfHeight() + 5.f);
				Each->TeleportTo(At, FRotator(0.f, 90.f, 0.f), false, true);
			}
			Squad->ResetFormationState(FVector(0.f, 1.f, 0.f));
			Next(State, 1);
			return true;
		}
		if (!Squad || !PC || !Squad->GetLeader())
		{
			return Finish(State, false);
		}
		AOperativeCharacter* Leader = Squad->GetLeader();
		switch (State.Stage)
		{
		case 1:
		case 2:
		{
			// The two clicks on the face mid-wall: the cover ghost, then its confirm (the group cover order = the formation wall layout).
			if (State.StageTime < (State.Stage == 1 ? 3.f : 1.f))
			{
				return true;
			}
			FHitResult Hit;
			if (FaceHit(World, State, MidX, Hit))
			{
				PC->HandleWorldHit(Hit);
			}
			Next(State, State.Stage + 1);
			return true;
		}
		case 3:
		{
			bool bSettled = Squad->HasFormationWallOrder() && !Squad->IsFormationSliding();
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				bSettled &= Weak.IsValid() && Weak->IsInFormationWallPose() && Weak->GetVelocity().Size2D() < 5.f;
			}
			if ((!bSettled || State.StageTime < 2.f) && State.StageTime < 25.f)
			{
				return true;
			}
			Check(State, bSettled, FString::Printf(TEXT("the squad hugs W_B_3 in the formation wall layout (%.1f s):%s"), State.StageTime, *Where(State)));
			const TArray<AOperativeCharacter*> Sorted = AlongWall(State);
			if (!bSettled || Sorted.Num() != 3)
			{
				return Finish(State, false);
			}
			State.West = Sorted[0];
			State.MiddleMan = Sorted[1];
			State.East = Sorted[2];
			State.PendingClick = EClick::EastEnd; // facing out (south) the squad's RIGHT is the east
			Next(State, 4);
			return true;
		}
		case 4:
		{
			// The right (east) corner: its end man there, leading (today's wall leadership).
			if (State.PendingClick != EClick::None)
			{
				return true;
			}
			AOperativeCharacter* East = State.East.Get();
			// The east man in his peek pose and the others settled behind him in the stack.
			bool bStackSettled = true;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				bStackSettled &= !Weak.IsValid() || Weak.Get() == East || (Weak->IsInFormationWallPose() && Weak->GetVelocity().Size2D() < 5.f);
			}
			State.HoldTime = InPeekPose(East) && AtEnd(East, false) && bStackSettled ? State.HoldTime + DeltaTime : 0.f;
			if (State.HoldTime < HoldSeconds && State.StageTime < 25.f)
			{
				return true;
			}
			Check(State, State.HoldTime >= HoldSeconds && Leader == East, FString::Printf(TEXT("peek right: the east-end man %s peeks at the east corner and leads (leader %s, %.1f s);%s"),
				*NameOf(East), *NameOf(Leader), State.StageTime, *Where(State)));
			AOperativeCharacter* Middle = State.MiddleMan.Get();
			State.MiddleAt = Middle ? Middle->GetActorLocation() : FVector::ZeroVector;
			State.MiddleMaxMoveCm = 0.f;
			State.bEastLeft = false;
			State.SecondCornersBefore = Squad->GetSecondCornerPeekCount();
			// The middle man walked into the stack behind the east man: his place from here on.
			State.PendingClick = EClick::WestEnd;
			Next(State, 5);
			return true;
		}
		case 5:
		{
			// The left (west) corner: its end man there; the east man keeps his corner, the middle man stays.
			if (State.PendingClick != EClick::None)
			{
				return true;
			}
			AOperativeCharacter* East = State.East.Get();
			AOperativeCharacter* West = State.West.Get();
			AOperativeCharacter* Middle = State.MiddleMan.Get();
			if (State.StageTime < 0.1f)
			{
				// The middle man's place right after the click (he settled behind the east man).
				State.MiddleAt = Middle ? Middle->GetActorLocation() : FVector::ZeroVector;
			}
			if (!InPeekPose(East) && !State.bEastLeft)
			{
				State.bEastLeft = true;
				State.EastLeftAt = State.StageTime;
			}
			if (Middle)
			{
				State.MiddleMaxMoveCm = FMath::Max(State.MiddleMaxMoveCm, static_cast<float>(FVector::Dist2D(Middle->GetActorLocation(), State.MiddleAt)));
			}
			State.HoldTime = InPeekPose(West) && AtEnd(West, true) ? State.HoldTime + DeltaTime : 0.f;
			if (FMath::FloorToInt32(State.StageTime) != FMath::FloorToInt32(State.StageTime - DeltaTime))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke trace %.1f s (west peek):%s"), State.StageTime, *Where(State));
			}
			if (State.HoldTime < HoldSeconds && State.StageTime < 40.f)
			{
				return true;
			}
			Check(State, State.HoldTime >= HoldSeconds && Leader == West, FString::Printf(TEXT("peek left: the west-end man %s peeks at the west corner and leads (leader %s, %.1f s)"),
				*NameOf(West), *NameOf(Leader), State.StageTime));
			Check(State, !State.bEastLeft && InPeekPose(East) && AtEnd(East, false), FString::Printf(TEXT("... and the east man %s KEEPS his corner peek all the way (left it at %.1f s)"),
				*NameOf(East), State.EastLeftAt));
			Check(State, Squad->IsTwoCornerHoldActive() && Squad->GetHeldCornerOperative() == East && Squad->GetSecondCornerPeekCount() == State.SecondCornersBefore + 1,
				FString::Printf(TEXT("... both corners held (held corner %s, second corners %d -> %d)"), *NameOf(Squad->GetHeldCornerOperative()), State.SecondCornersBefore,
					Squad->GetSecondCornerPeekCount()));
			Check(State, Middle && State.MiddleMaxMoveCm <= StayCm && Middle->IsInFormationWallPose(), FString::Printf(TEXT("... the middle man %s stays at the wall (moved %.0f cm, wall pose %d)"),
				*NameOf(Middle), State.MiddleMaxMoveCm, Middle && Middle->IsInFormationWallPose() ? 1 : 0));
			State.MinYawOff = 999.f;
			State.MaxYawOff = -999.f;
			State.SweptFrames = 0;
			Next(State, 6);
			return true;
		}
		case 6:
		{
			// Two sweeps: the middle man's facing across the arc between both corner looks (and the corner men still on their corners).
			AOperativeCharacter* East = State.East.Get();
			AOperativeCharacter* West = State.West.Get();
			AOperativeCharacter* Middle = State.MiddleMan.Get();
			// The sweep's ends from where he stands: towards each corner along the wall (at most 85 deg off the outward normal).
			if (Middle)
			{
				const FVector Here = Middle->GetActorLocation();
				State.CornerOffA = FMath::Clamp(FMath::FindDeltaAngleDegrees(OutwardYaw, static_cast<float>((FVector(WallEastX, WallFaceY, Here.Z) - Here).Rotation().Yaw)), -85.f, 85.f);
				State.CornerOffB = FMath::Clamp(FMath::FindDeltaAngleDegrees(OutwardYaw, static_cast<float>((FVector(WallWestX, WallFaceY, Here.Z) - Here).Rotation().Yaw)), -85.f, 85.f);
			}
			if (Middle && Squad->GetCornerSweepMemberCount() > 0)
			{
				const float Off = FMath::FindDeltaAngleDegrees(OutwardYaw, static_cast<float>(Middle->GetActorRotation().Yaw));
				State.MinYawOff = FMath::Min(State.MinYawOff, Off);
				State.MaxYawOff = FMath::Max(State.MaxYawOff, Off);
				++State.SweptFrames;
			}
			State.bEastLeft |= !InPeekPose(East);
			const float Cycle = 2.f * SquadWallTuning::Get().CornerSweepSeconds;
			if (State.StageTime < Cycle * 1.25f)
			{
				return true;
			}
			const float Low = FMath::Min(State.CornerOffA, State.CornerOffB);
			const float High = FMath::Max(State.CornerOffA, State.CornerOffB);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke metric: the middle man's facing off the outward normal %.0f .. %.0f deg over %d swept frames; the corner looks %.0f (east) / %.0f (west)"),
				State.MinYawOff, State.MaxYawOff, State.SweptFrames, State.CornerOffA, State.CornerOffB);
			Check(State, State.SweptFrames > 0 && State.MinYawOff <= Low + SweepReachDeg && State.MaxYawOff >= High - SweepReachDeg,
				FString::Printf(TEXT("the middle man sweeps from one corner's look to the other's (%.0f .. %.0f, the corners %.0f / %.0f, within %.0f deg)"), State.MinYawOff,
					State.MaxYawOff, Low, High, SweepReachDeg));
			Check(State, State.MinYawOff >= -90.f && State.MaxYawOff <= 90.f, FString::Printf(TEXT("... never into the wall (%.0f .. %.0f within +-90 of the outward normal)"),
				State.MinYawOff, State.MaxYawOff));
			Check(State, !State.bEastLeft && InPeekPose(West), TEXT("both corner men kept peeking through the sweep"));
			State.PendingClick = EClick::WestEnd;
			Next(State, 7);
			return true;
		}
		case 7:
		{
			// The same side again: nobody swaps.
			if (State.PendingClick != EClick::None || State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, Leader == State.West.Get() && Squad->GetHeldCornerOperative() == State.East.Get() && InPeekPose(State.West.Get()) && InPeekPose(State.East.Get()),
				FString::Printf(TEXT("a repeated peek on the left swaps nothing (leader %s, held %s);%s"), *NameOf(Leader), *NameOf(Squad->GetHeldCornerOperative()), *Where(State)));
			State.PendingClick = EClick::AwayGround;
			Next(State, 8);
			return true;
		}
		case 8:
		{
			// A move order ends both holds.
			if (State.PendingClick != EClick::None || State.StageTime < 2.f)
			{
				return true;
			}
			const bool bOver = !Squad->IsTwoCornerHoldActive() && !Squad->GetHeldCornerOperative() && !Squad->GetFormationPeeker();
			const AOperativeCharacter* East = State.East.Get();
			if ((!bOver || (East && East->IsManualCornerPeek())) && State.StageTime < 20.f)
			{
				return true;
			}
			Check(State, bOver && East && !East->IsManualCornerPeek(), FString::Printf(TEXT("a move order ends both corner holds (%.1f s);%s"), State.StageTime, *Where(State)));
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
		TWeakObjectPtr<UWorld> WeakWorld(World);
		State->PreTickHandle = FWorldDelegates::OnWorldPreActorTick.AddLambda([WeakWorld, State](UWorld* Ticking, ELevelTick, float)
		{
			if (State->PendingClick != EClick::None && Ticking == WeakWorld.Get())
			{
				WorldTickClick(Ticking, *State);
			}
		});
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float DeltaTime)
		{
			const UWorld* Live = WeakWorld.Get();
			return Step(WeakWorld, *State, Live ? Live->GetDeltaSeconds() : DeltaTime);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TwoCornerHoldSmoke"),
		TEXT("Dev check (#57, two-corner hold): on L_FormationLab the squad at W_B_3 in formation - peek right -> the east man at the east corner; peek left -> the west man at the west corner AND the east man still peeking at his; the middle man stays and sweeps his look between both corner looks; a repeated left peek swaps nothing; a move order ends both. PASS/FAIL, exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Run(Args, World); }));
}

#endif // !UE_BUILD_SHIPPING
