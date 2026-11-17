// Dev-only headless check of the designer bug B17-1 (playtest of main 0cc08c4b, 2026-11-17; docs/port/TASK_BOARD.md, the designer bugs of 2026-11-17):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands CornerPickSmoke
//   (map /Game/Maps/L_FormationLab, "-LevelJson=L_FormationLab.json" - Scripts/test_map.json smokeOptions)
// The report: the squad in the WALL formation along a wall (the Commander in the middle, one man each side); the cursor near the wall's
// right free end - the corner ghost flipped between the right-hand man and the Commander from a 1 px cursor move; when it was the Commander,
// he ran to the corner and the right-hand man stood to his right - past the corner, in the open.
// The lab wall W_B_3 (x 1500..4000 at y -2700, 40 cm thick; its south face y -2720), the squad of three south of it:
//   A. two clicks on the face 3.5 m from the east end (the group cover order = the formation wall layout, the Commander in the middle). The
//      hover swept over the east end - face hits at three heights and hits along the game camera's view (the face, the top, the end face), every
//      2.5 cm along the wall, each one evaluated afresh (the hover caches cleared by a hover over the ground between them): (a) every shown
//      ghost is the same man, the one nearest that end along the wall (FORMATION_SECTORS.md "A - the corner stack": the point man), so no two
//      hits within 5 cm of each other show different men; the click there orders that man to the corner.
//   B. (b) the leader himself is the corner man (A's corner man leads after the peek; then the squad in formation in the open, the click on the
//      east end - its run-and-peek: the squad's wall order there, the leader's own place the corner): from his corner slot on, every follower's
//      formation place and body stays behind his slot along the wall - at least 90 cm back (FormationLeaderCornerRules KeepBack) - and nobody
//      passes the wall's end plane.
//   Round 1 (the designer's geometry, a building's corner): a cross wall spawned at the east end as the level generator builds a corner (two cube
//      walls; its narrow end lies in W_B_3's face over the last 40 cm, so a pixel there hits either cube, and the end is a joint for F-2):
//      A and B again. A/B: -PreCmds "Codex.Formation.SquadWallEndOneDetector 0,Codex.Formation.LeaderPeekCorner 0" = before the fix.
// Godot reference: none (UE-only; docs/design/FORMATION_SECTORS.md "B17-1", docs/design/FORMATION_CONTRACT.md section 21).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/PlayerCameraManager.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTypes.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Tactics/CoverGhostActor.h"

namespace CornerPickSmoke
{
	/** W_B_3 (Content/Data/FormationLab/formation_lab_01.json): x 1500..4000, its south face at y -2720. */
	constexpr float WallWestX = 1500.f;
	constexpr float WallEastX = 4000.f;
	constexpr float WallFaceY = -2720.f;
	/** A: the wall layout's click this far in from the east end (the designer's short wall: the right-hand man ~2 m from the end). */
	constexpr float AnchorInCm = 350.f;
	constexpr float StartY = -3300.f;
	/** (a): the sweep over the east end - from this far in to this far past it, this step, cm. */
	constexpr float SweepInCm = 170.f;
	constexpr float SweepPastCm = 40.f;
	constexpr float SweepStepCm = 2.5f;
	/** (a): two hits this close (the designer's 1 px) must show the same man, cm. */
	constexpr float JitterCm = 5.f;
	/** (a): the sweep must show the ghost at least this often (the end window was really swept). */
	constexpr int32 MinGhostSamples = 10;
	/** (b): every follower this far behind the leader's corner slot along the wall at least (FormationLeaderCornerRules::KeepBackCm), cm. */
	constexpr float BehindSlotMinCm = 90.f;
	/** (b): a body / place counts past the end plane this far beyond it (a capsule touching the plane is still in), cm. */
	constexpr float PastPlaneSlackCm = 0.f;
	/** (b): the leader's slot counts as his corner within this of the end along the wall, cm. */
	constexpr float CornerSlotCm = 150.f;
	/** B: the squad in formation in the open: the leader, the followers far behind him. */
	const FVector2D OpenLeaderStart(WallEastX - 900.f, -3250.f);
	const FVector2D OpenFollowerStart[2] = { FVector2D(WallEastX - 1300.f, -4100.f), FVector2D(WallEastX - 1050.f, -4250.f) };
	constexpr float HoldSeconds = 2.f;
	/** Round 1's cross wall: from this X to the east end (W_B_3's thickness), this long northwards, this high, cm. */
	constexpr float CrossWallX0 = WallEastX - 40.f;
	constexpr float CrossWallLengthCm = 400.f;
	constexpr float CrossWallHeightCm = 300.f;
	/** A's click in the corner's last 40 cm (round 1: over the cross wall's end); B's clear of it (on W_B_3 only), cm in from the end. */
	constexpr float ClickAInCm = 20.f;
	constexpr float ClickBInCm = 60.f;
	constexpr float GiveUpSeconds = 30.f;

	enum class EAction : uint8
	{
		None,
		Sweep,
		ClickEast,
		GroundAway
	};

	struct FSample
	{
		int32 Family = 0;
		float Along = 0.f;
		FString Ghost;
		FString Surface;
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		float GroundZ = 0.f;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TWeakObjectPtr<AOperativeCharacter> Expected;
		EAction Pending = EAction::None;
		TArray<FSample> Samples;
		FString ClickGhost;
		/** (b) over the stage: the worst follower place / body relative to the limits (> 0 = a breach), and who / when. */
		float WorstBehind = -TNumericLimits<float>::Max();
		FString WorstBehindWho;
		float WorstPast = -TNumericLimits<float>::Max();
		FString WorstPastWho;
		int32 CornerFrames = 0;
		float HoldTime = 0.f;
		/** The click's checks ran (once, on the first step after it). */
		bool bClickChecked = false;
		/** The east-end click's X (A: in the corner's last 40 cm - over the cross wall's end in round 1; B: clear of it). */
		float ClickX = WallEastX - 20.f;
		/** Round 0: W_B_3's free east end; round 1: a cross wall spawned at that end - a building's corner of two cube walls (the level generator's). */
		int32 Round = 0;
		TWeakObjectPtr<AActor> CrossWall;
		FDelegateHandle PreTickHandle;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s%s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), State.Round == 1 ? TEXT("[L corner] ") : TEXT(""), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		FWorldDelegates::OnWorldPreActorTick.Remove(State.PreTickHandle);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CornerPickSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.bClickChecked = false;
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
				Line += FString::Printf(TEXT(" %s (%.0f, %.0f)%s%s;"), *NameOf(Op), Op->GetActorLocation().X, Op->GetActorLocation().Y, Op->bInCover ? TEXT(" in cover") : TEXT(""),
					Op->IsManualCornerPeek() ? TEXT(" peeking") : TEXT(""));
			}
		}
		return Line;
	}

	FCollisionQueryParams TraceParams(const FState& State)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CornerPickSmoke), false);
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			Params.AddIgnoredActor(Weak.Get());
		}
		return Params;
	}

	/** The cursor hit on W_B_3's south face at X, Z above the ground (a real trace from the south: the wall's own component). */
	bool FaceHit(UWorld* World, const FState& State, float X, float Z, FHitResult& OutHit)
	{
		const float At = State.GroundZ + Z;
		return World->LineTraceSingleByChannel(OutHit, FVector(X, WallFaceY - 300.f, At), FVector(X, WallFaceY + 15.f, At), ECC_Visibility, TraceParams(State))
			&& FMath::Abs(OutHit.ImpactPoint.Y - WallFaceY) <= 15.f;
	}

	bool GroundHit(UWorld* World, const FState& State, float X, float Y, FHitResult& OutHit)
	{
		return World->LineTraceSingleByChannel(OutHit, FVector(X, Y, State.GroundZ + 500.f), FVector(X, Y, State.GroundZ - 500.f), ECC_Visibility, TraceParams(State));
	}

	/** The cursor's hit along the game camera's view through the point (X, Y, Z above the ground): the face, the top or the end face as seen. */
	bool ViewHit(UWorld* World, const FState& State, const FVector& ViewDir, float X, float Y, float Z, FHitResult& OutHit)
	{
		const FVector Through(X, Y, State.GroundZ + Z);
		return World->LineTraceSingleByChannel(OutHit, Through - ViewDir * 3000.f, Through + ViewDir * 300.f, ECC_Visibility, TraceParams(State));
	}

	/** The squad of three south of W_B_3, facing it, in group mode (the Commander leading) round X. */
	bool PlaceSquad(FState& State, USquadSubsystem* Squad, float MidX)
	{
		Squad->ExitSoloMode(false, 0.f);
		AOperativeCharacter* Lead = State.Members.IsValidIndex(0) ? State.Members[0].Get() : nullptr;
		if (!Lead)
		{
			return false;
		}
		Squad->SetLeader(Lead);
		for (int32 Index = 0; Index < State.Members.Num(); ++Index)
		{
			AOperativeCharacter* Each = State.Members[Index].Get();
			if (!Each)
			{
				return false;
			}
			Each->LeaveCover(TEXT("smoke reset"));
			Each->ClearPendingCover();
			Each->StopOperative();
			Each->SetFormationWallPose(false);
			Each->SetStance(EOperativeStance::Standing);
			const FVector At(MidX + (Index == 0 ? 0.f : (Index == 1 ? -150.f : 150.f)), StartY - (Index == 0 ? 0.f : 200.f),
				State.GroundZ + Each->GetSimpleCollisionHalfHeight() + 5.f);
			Each->TeleportTo(At, FRotator(0.f, 90.f, 0.f), false, true);
		}
		Squad->ResetFormationState(FVector(0.f, 1.f, 0.f));
		return true;
	}

	void Place(FState& State, AOperativeCharacter* Op, const FVector2D& At)
	{
		Op->LeaveCover(TEXT("smoke reset"));
		Op->ClearPendingCover();
		Op->StopOperative();
		Op->SetFormationWallPose(false);
		Op->SetStance(EOperativeStance::Standing);
		Op->TeleportTo(FVector(At.X, At.Y, State.GroundZ + Op->GetSimpleCollisionHalfHeight() + 5.f), FRotator(0.f, 90.f, 0.f), false, true);
	}

	/** The member farthest east (nearest the east end along the wall). */
	AOperativeCharacter* EastMost(const FState& State)
	{
		AOperativeCharacter* Best = nullptr;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			AOperativeCharacter* Op = Weak.Get();
			if (Op && (!Best || Op->GetActorLocation().X > Best->GetActorLocation().X))
			{
				Best = Op;
			}
		}
		return Best;
	}

	/** One hover, evaluated afresh: a ground hover clears the hover caches (another actor), then the hit itself. */
	FString HoverGhost(ACodexTacticsPlayerController& PC, AOperativeCharacter& Leader, const FHitResult& Ground, const FHitResult& Hit)
	{
		PC.UpdateCornerPeekHover(&Leader, &Ground);
		const bool bShown = PC.UpdateCornerPeekHover(&Leader, &Hit);
		const ACoverGhostActor* Ghost = PC.GetCornerPeekGhost();
		return bShown && Ghost && Ghost->IsShown() ? NameOf(Ghost->GetOperative()) : FString();
	}

	/** (a): the hover sweep over the east end, in the world tick (as the controller's PlayerTick). */
	void Sweep(UWorld* World, FState& State)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		FHitResult Ground;
		if (!PC || !Leader || !GroundHit(World, State, WallEastX - 1200.f, StartY - 600.f, Ground))
		{
			Check(State, false, TEXT("(a) the sweep: the controller, the leader and a ground hit to clear the hover caches"));
			return;
		}
		const FVector ViewDir = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Vector() : FVector(0.f, 0.6f, -0.8f).GetSafeNormal();
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: (a) the sweep over the east end, the camera looking (%.2f, %.2f, %.2f);%s"), ViewDir.X, ViewDir.Y, ViewDir.Z, *Where(State));
		// Families: 0..2 face hits at three heights; 3..5 the camera's view through the face at two heights and through the top.
		const float FaceZ[3] = { 60.f, 120.f, 180.f };
		const float ViewZ[3] = { 80.f, 160.f, 230.f };
		const float ViewY[3] = { WallFaceY, WallFaceY, WallFaceY + 20.f };
		for (int32 Family = 0; Family < 6; ++Family)
		{
			for (float Along = -SweepInCm; Along <= SweepPastCm + 0.01f; Along += SweepStepCm)
			{
				const float X = WallEastX + Along;
				FHitResult Hit;
				const bool bHit = Family < 3 ? FaceHit(World, State, X, FaceZ[Family], Hit) : ViewHit(World, State, ViewDir, X, ViewY[Family - 3], ViewZ[Family - 3], Hit);
				if (!bHit)
				{
					continue;
				}
				FSample Sample;
				Sample.Family = Family;
				Sample.Along = Along;
				Sample.Ghost = HoverGhost(*PC, *Leader, Ground, Hit);
				Sample.Surface = FString::Printf(TEXT("%s n(%.1f, %.1f, %.1f) at (%.0f, %.0f, %.0f)"), *GetNameSafe(Hit.GetActor()), Hit.ImpactNormal.X, Hit.ImpactNormal.Y,
					Hit.ImpactNormal.Z, Hit.ImpactPoint.X, Hit.ImpactPoint.Y, Hit.ImpactPoint.Z);
				State.Samples.Add(Sample);
			}
		}
		PC->UpdateCornerPeekHover(Leader, &Ground);
	}

	/**
	 * Round 1: the cross wall at W_B_3's east end, a building's corner as the level generator builds it (two cube walls, Scripts/Editor/
	 * build_levelgen_map.py wall_cube): 40 cm thick, x CrossWallX0..WallEastX, running north from the south face's plane - its narrow south
	 * end lies IN W_B_3's south face over the corner's last 40 cm (a pixel there hits either cube), the end no free end any more (a joint).
	 */
	AActor* SpawnCrossWall(UWorld* World, const FState& State)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			return nullptr;
		}
		const FVector Size(WallEastX - CrossWallX0, CrossWallLengthCm, CrossWallHeightCm);
		const FTransform Transform(FRotator::ZeroRotator, FVector(0.5f * (CrossWallX0 + WallEastX), WallFaceY + 0.5f * CrossWallLengthCm, State.GroundZ + 0.5f * CrossWallHeightCm),
			Size / 100.f);
		AStaticMeshActor* Wall = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Wall)
		{
			return nullptr;
		}
		Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Wall->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Wall->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Wall->FinishSpawning(Transform);
		return Wall;
	}

	void WorldTickAction(UWorld* World, FState& State)
	{
		const EAction Action = State.Pending;
		State.Pending = EAction::None;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !PC || !Leader)
		{
			return;
		}
		if (Action == EAction::Sweep)
		{
			Sweep(World, State);
			return;
		}
		FHitResult Hit;
		if (Action == EAction::GroundAway)
		{
			if (GroundHit(World, State, Leader->GetActorLocation().X - 300.f, WallFaceY - 650.f, Hit))
			{
				PC->HandleWorldHit(Hit);
			}
			return;
		}
		// The hover on the east end (20 cm in from it), then the click there.
		if (!FaceHit(World, State, State.ClickX, 120.f, Hit))
		{
			Check(State, false, TEXT("the cursor hit on W_B_3's east end"));
			return;
		}
		FHitResult Ground;
		State.ClickGhost = GroundHit(World, State, WallEastX - 1200.f, StartY - 600.f, Ground) ? HoverGhost(*PC, *Leader, Ground, Hit) : FString();
		PC->HandleWorldHit(Hit);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the click on the east end at (%.0f, %.0f) (ghost of %s) -> leader %s, peeker %s;%s"), Hit.ImpactPoint.X, Hit.ImpactPoint.Y,
			State.ClickGhost.IsEmpty() ? TEXT("nobody") : *State.ClickGhost, *NameOf(Squad->GetLeader()), *NameOf(Squad->GetFormationPeeker()), *Where(State));
	}

	/** (a): every shown ghost the expected man; no two hits within JitterCm of each other (one family) showing different men. */
	void CheckSweep(FState& State)
	{
		const FString Expected = NameOf(State.Expected.Get());
		TMap<FString, int32> Count;
		int32 Shown = 0;
		int32 Flips = 0;
		FString FirstFlip;
		FString FirstWrong;
		for (int32 Index = 0; Index < State.Samples.Num(); ++Index)
		{
			const FSample& Each = State.Samples[Index];
			if (Each.Ghost.IsEmpty())
			{
				continue;
			}
			++Shown;
			++Count.FindOrAdd(Each.Ghost);
			if (Each.Ghost != Expected && FirstWrong.IsEmpty())
			{
				FirstWrong = FString::Printf(TEXT("family %d at %.1f cm from the end: %s on %s"), Each.Family, Each.Along, *Each.Ghost, *Each.Surface);
			}
			for (int32 Other = Index + 1; Other < State.Samples.Num(); ++Other)
			{
				const FSample& Near = State.Samples[Other];
				if (Near.Family == Each.Family && FMath::Abs(Near.Along - Each.Along) <= JitterCm + 0.01f && !Near.Ghost.IsEmpty() && Near.Ghost != Each.Ghost)
				{
					++Flips;
					if (FirstFlip.IsEmpty())
					{
						FirstFlip = FString::Printf(TEXT("family %d: %s at %.1f cm (%s) vs %s at %.1f cm (%s)"), Each.Family, *Each.Ghost, Each.Along, *Each.Surface, *Near.Ghost,
							Near.Along, *Near.Surface);
					}
				}
			}
		}
		FString Tally;
		for (const TPair<FString, int32>& Pair : Count)
		{
			Tally += FString::Printf(TEXT(" %s x%d;"), *Pair.Key, Pair.Value);
		}
		Check(State, Shown >= MinGhostSamples, FString::Printf(TEXT("(a) the sweep over the east end shows the corner ghost (%d of %d hits):%s"), Shown, State.Samples.Num(), *Tally));
		Check(State, Flips == 0, FString::Printf(TEXT("(a) no flip: hits within %.0f cm of each other show the same man (%d flip pair(s)%s%s)"), JitterCm, Flips,
			FirstFlip.IsEmpty() ? TEXT("") : TEXT(", first: "), *FirstFlip));
		Check(State, FirstWrong.IsEmpty() && Shown > 0, FString::Printf(TEXT("(a) every ghost is the man nearest the east end, %s (the corner stack's point man)%s%s"), *Expected,
			FirstWrong.IsEmpty() ? TEXT("") : TEXT(" - not: "), *FirstWrong));
	}

	/**
	 * The leader's corner slot at the east end, if he takes it: his cover slot (held, else pending), else his own formation place (the wall
	 * order's K place at the corner, before his cover order there) - from the click on, not only once he is in cover.
	 */
	bool LeaderCornerSlotX(const USquadSubsystem& Squad, const AOperativeCharacter& Leader, float& OutX)
	{
		const FCoverSlot* Slot = Leader.bInCover ? &Leader.GetCoverSlot() : (Leader.HasPendingCover() ? &Leader.GetPendingCoverSlot() : nullptr);
		FVector At = FVector::ZeroVector;
		float Yaw = 0.f;
		if (Slot && Slot->IsValid())
		{
			At = Slot->WorldLocation;
		}
		else if (!Squad.GetFormationTarget(&Leader, At, Yaw))
		{
			return false;
		}
		if (FMath::Abs(static_cast<float>(At.Y) - WallFaceY) > 120.f || static_cast<float>(At.X) < WallEastX - CornerSlotCm || static_cast<float>(At.X) > WallEastX + 60.f)
		{
			return false;
		}
		OutX = static_cast<float>(At.X);
		return true;
	}

	/** (b) one frame: the leader at his corner - every follower's formation place and body behind his slot along the wall and inside the end plane. */
	void SampleBehind(FState& State, USquadSubsystem& Squad, const AOperativeCharacter& Leader)
	{
		float SlotX = 0.f;
		if (!LeaderCornerSlotX(Squad, Leader, SlotX))
		{
			return;
		}
		++State.CornerFrames;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			const AOperativeCharacter* Op = Weak.Get();
			if (!Op || Op == &Leader)
			{
				continue;
			}
			TArray<TPair<const TCHAR*, FVector>> Points;
			Points.Add(TPair<const TCHAR*, FVector>(TEXT("body"), Op->GetActorLocation()));
			FVector Given;
			if (Squad.GetFormationGivenSlot(Op, Given))
			{
				Points.Add(TPair<const TCHAR*, FVector>(TEXT("place"), Given));
			}
			for (const TPair<const TCHAR*, FVector>& Point : Points)
			{
				const float Behind = static_cast<float>(Point.Value.X) - (SlotX - BehindSlotMinCm);
				const float Past = static_cast<float>(Point.Value.X) - (WallEastX + PastPlaneSlackCm);
				const FString Who = FString::Printf(TEXT("%s's %s (%.0f, %.0f) at %.1f s, the leader's slot x %.0f"), *NameOf(Op), Point.Key, Point.Value.X, Point.Value.Y,
					State.StageTime, SlotX);
				if (Behind > State.WorstBehind)
				{
					State.WorstBehind = Behind;
					State.WorstBehindWho = Who;
				}
				if (Past > State.WorstPast)
				{
					State.WorstPast = Past;
					State.WorstPastWho = Who;
				}
			}
		}
	}

	void ResetBehind(FState& State)
	{
		State.WorstBehind = -TNumericLimits<float>::Max();
		State.WorstPast = -TNumericLimits<float>::Max();
		State.WorstBehindWho.Reset();
		State.WorstPastWho.Reset();
		State.CornerFrames = 0;
		State.HoldTime = 0.f;
	}

	void CheckBehind(FState& State, const TCHAR* Label)
	{
		Check(State, State.CornerFrames > 0, FString::Printf(TEXT("%s: the leader takes the east corner himself (%d frames with his slot at it)"), Label, State.CornerFrames));
		Check(State, State.CornerFrames > 0 && State.WorstBehind <= 0.f, FString::Printf(TEXT("(b) %s: every follower's place and body >= %.0f cm behind the leader's corner slot along the wall (worst %.0f cm over: %s)"),
			Label, BehindSlotMinCm, State.WorstBehind, *State.WorstBehindWho));
		Check(State, State.CornerFrames > 0 && State.WorstPast <= 0.f, FString::Printf(TEXT("(b) %s: nobody past the wall's end plane x %.0f (worst %.0f cm: %s)"), Label, WallEastX,
			State.WorstPast, *State.WorstPastWho));
	}

	bool InPeekPose(const AOperativeCharacter& Op)
	{
		return Op.bInCover && Op.IsManualCornerPeek() && Op.bAtCoverCorner && !Op.bShimmying;
	}

	bool Settled(const FState& State, const AOperativeCharacter& Leader)
	{
		if (!InPeekPose(Leader))
		{
			return false;
		}
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			if (!Weak.IsValid() || Weak->GetVelocity().Size2D() > 5.f)
			{
				return false;
			}
		}
		return true;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float DeltaTime)
	{
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 500.f)
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
			if (!FormationRuntime::IsEnabled())
			{
				Check(State, false, TEXT("Codex.Formation.Sectors is 1 (the squad corner peek is the formation's)"));
				return Finish(State, false);
			}
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& X, const AOperativeCharacter& Y) { return X.SquadIndex < Y.SquadIndex; });
			for (int32 Index = 0; Index < 3; ++Index)
			{
				State.Members.Add(Roster[Index]);
				if (USkeletalMeshComponent* Mesh = Roster[Index]->GetMesh())
				{
					Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				}
			}
			FHitResult Ground;
			State.GroundZ = World->LineTraceSingleByChannel(Ground, FVector(WallEastX - AnchorInCm, StartY, 2000.f), FVector(WallEastX - AnchorInCm, StartY, -2000.f),
				ECC_Visibility, TraceParams(State)) ? static_cast<float>(Ground.ImpactPoint.Z) : 0.f;
			FHitResult Probe;
			const bool bWall = FaceHit(World, State, WallEastX - AnchorInCm, 120.f, Probe) && FaceHit(World, State, WallEastX - 20.f, 120.f, Probe);
			Check(State, bWall, FString::Printf(TEXT("the lab wall W_B_3 stands where the layout puts it (south face y %.0f, east end x %.0f; ground z %.0f)"), WallFaceY, WallEastX,
				State.GroundZ));
			if (!bWall || !PlaceSquad(State, Squad, WallEastX - AnchorInCm))
			{
				return Finish(State, false);
			}
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
			// A: the two clicks on the face (the cover ghost, then its confirm = the group cover order = the formation wall layout).
			if (State.StageTime < (State.Stage == 1 ? 3.f : 1.f))
			{
				return true;
			}
			FHitResult Hit;
			if (FaceHit(World, State, WallEastX - AnchorInCm, 120.f, Hit))
			{
				PC->HandleWorldHit(Hit);
			}
			if (State.Stage == 2)
			{
				Check(State, Squad->HasFormationWallOrder(), FString::Printf(TEXT("A: the clicks on the face (%s leading) - the group cover order as the formation wall layout"),
					*NameOf(Leader)));
			}
			Next(State, State.Stage + 1);
			return true;
		}
		case 3:
		{
			bool bSettled = Squad->GetFormationKind() == EFormationKind::Wall && !Squad->IsFormationSliding();
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				bSettled &= Weak.IsValid() && Weak->IsInFormationWallPose() && Weak->GetVelocity().Size2D() < 5.f;
			}
			if ((!bSettled || State.StageTime < 2.f) && State.StageTime < 25.f)
			{
				return true;
			}
			Check(State, bSettled, FString::Printf(TEXT("A: the squad hugs W_B_3 in the wall layout, crouched (%.1f s):%s"), State.StageTime, *Where(State)));
			State.Expected = EastMost(State);
			Check(State, State.Expected.Get() && State.Expected.Get() != Leader, FString::Printf(TEXT("A: the leader %s in the middle, %s nearest the east end"), *NameOf(Leader),
				*NameOf(State.Expected.Get())));
			State.Pending = EAction::Sweep;
			Next(State, 4);
			return true;
		}
		case 4:
		{
			if (State.Pending != EAction::None)
			{
				return true;
			}
			CheckSweep(State);
			State.ClickX = WallEastX - ClickAInCm;
			State.Pending = EAction::ClickEast;
			ResetBehind(State);
			Next(State, 5);
			return true;
		}
		case 5:
		{
			// A's click: the hover's man takes the corner and leads; then (b) with him at the corner.
			if (State.Pending != EAction::None)
			{
				return true;
			}
			if (!State.bClickChecked)
			{
				State.bClickChecked = true;
				const AOperativeCharacter* Expected = State.Expected.Get();
				Check(State, State.ClickGhost == NameOf(Expected), FString::Printf(TEXT("A: the click's ghost is %s (%s)"), *NameOf(Expected), *State.ClickGhost));
				Check(State, Squad->GetFormationPeeker() == Expected && Leader == Expected, FString::Printf(TEXT("A: the click orders %s to the corner (peeker %s, leader %s)"),
					*NameOf(Expected), *NameOf(Squad->GetFormationPeeker()), *NameOf(Leader)));
			}
			SampleBehind(State, *Squad, *Leader);
			State.HoldTime = Settled(State, *Leader) ? State.HoldTime + DeltaTime : 0.f;
			if (State.HoldTime < HoldSeconds && State.StageTime < GiveUpSeconds)
			{
				return true;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: A: %.1f s after the click:%s"), State.StageTime, *Where(State));
			CheckBehind(State, TEXT("A (the corner man leads)"));
			State.Pending = EAction::GroundAway;
			Next(State, 6);
			return true;
		}
		case 6:
		{
			// B: leave the wall, then the squad in formation in the open (the leader ahead, the followers far behind).
			if (State.Pending != EAction::None || State.StageTime < 3.f)
			{
				return true;
			}
			AOperativeCharacter* Lead = State.Members[0].Get();
			if (!Lead)
			{
				return Finish(State, false);
			}
			Squad->SetSelectedGroup({ Lead }, false);
			Squad->SetLeader(Lead, true);
			Squad->ExitSoloMode(false, 0.f);
			Place(State, Lead, OpenLeaderStart);
			for (int32 Index = 1; Index < 3; ++Index)
			{
				if (AOperativeCharacter* Each = State.Members[Index].Get())
				{
					Place(State, Each, OpenFollowerStart[Index - 1]);
				}
			}
			Squad->ResetFormationState(FVector(0.f, 1.f, 0.f));
			Next(State, 7);
			return true;
		}
		case 7:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, !Squad->IsSoloMode() && !Squad->HasMultiSelection() && Squad->GetLeader() == State.Members[0].Get(),
				FString::Printf(TEXT("B: the squad in formation in the open, %s leads"), *NameOf(Squad->GetLeader())));
			State.Expected = Squad->GetLeader();
			State.ClickX = WallEastX - ClickBInCm;
			State.Pending = EAction::ClickEast;
			ResetBehind(State);
			Next(State, 8);
			return true;
		}
		case 8:
		{
			if (State.Pending != EAction::None)
			{
				return true;
			}
			if (!State.bClickChecked)
			{
				State.bClickChecked = true;
				Check(State, State.ClickGhost == NameOf(Leader), FString::Printf(TEXT("B: the run-and-peek ghost is the leader %s's (%s)"), *NameOf(Leader), *State.ClickGhost));
			}
			SampleBehind(State, *Squad, *Leader);
			const AOperativeCharacter* Peeker = Squad->GetFormationPeeker();
			State.HoldTime = Peeker == Leader && Settled(State, *Leader) ? State.HoldTime + DeltaTime : 0.f;
			if (FMath::FloorToInt32(State.StageTime / 1.f) != FMath::FloorToInt32((State.StageTime - DeltaTime) / 1.f))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke trace %.1f s (B): peeker %s;%s"), State.StageTime, *NameOf(Peeker), *Where(State));
			}
			if (State.HoldTime < HoldSeconds && State.StageTime < GiveUpSeconds)
			{
				return true;
			}
			Check(State, Peeker == Leader && State.HoldTime >= HoldSeconds, FString::Printf(TEXT("B: the leader %s peeks round the east end himself (peeker %s, held %.1f s):%s"),
				*NameOf(Leader), *NameOf(Peeker), State.HoldTime, *Where(State)));
			CheckBehind(State, TEXT("B (the leader runs to the corner)"));
			if (State.Round == 0)
			{
				// Round 1: the same at a building's corner (a cross wall at the east end).
				State.Round = 1;
				State.Samples.Reset();
				State.CrossWall = SpawnCrossWall(World, State);
				FHitResult Probe;
				Check(State, State.CrossWall.IsValid() && World->LineTraceSingleByChannel(Probe, FVector(WallEastX + 300.f, WallFaceY + 200.f, State.GroundZ + 120.f),
					FVector(WallEastX - 20.f, WallFaceY + 200.f, State.GroundZ + 120.f), ECC_Visibility, TraceParams(State)) && Probe.GetActor() == State.CrossWall.Get(),
					TEXT("the cross wall stands at W_B_3's east end (a building's corner of two cube walls)"));
				if (!State.CrossWall.IsValid() || !PlaceSquad(State, Squad, WallEastX - AnchorInCm))
				{
					return Finish(State, false);
				}
				Next(State, 1);
				return true;
			}
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		// The single-corner rules (a peek at the same end again is no two-corner hold).
		if (IConsoleVariable* TwoCorners = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Formation.TwoCornerHold")))
		{
			TwoCorners->Set(0, ECVF_SetByCode);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		State->PreTickHandle = FWorldDelegates::OnWorldPreActorTick.AddLambda([WeakWorld, State](UWorld* Ticking, ELevelTick, float)
		{
			if (State->Pending != EAction::None && Ticking == WeakWorld.Get())
			{
				WorldTickAction(Ticking, *State);
			}
		});
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float DeltaTime)
		{
			const UWorld* Live = WeakWorld.Get();
			return Step(WeakWorld, *State, Live ? Live->GetDeltaSeconds() : DeltaTime);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CornerPickSmoke"),
		TEXT("Dev check (designer bug B17-1, 2026-11-17): on L_FormationLab the squad in the wall layout at W_B_3 - the corner ghost over the east end is always the man nearest it (no flip within 5 cm), the click orders him; with the leader himself at the corner every follower stays >= 90 cm behind his slot and inside the end plane; PASS/FAIL, exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) { Run(World); }));
}

#endif // !UE_BUILD_SHIPPING
