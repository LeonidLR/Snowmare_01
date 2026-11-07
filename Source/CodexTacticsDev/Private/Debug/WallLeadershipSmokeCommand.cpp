// Dev-only headless check of WALL LEADERSHIP (the game designer's decisions 2026-11-01, docs/design/FORMATION_SECTORS.md "Wall leadership"):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands WallLeadershipSmoke
//   (map /Game/Maps/L_FormationLab, "-LevelJson=L_FormationLab.json" - Scripts/test_map.json smokeOptions)
// The lab wall W_B_3 (x 1500..4000 at y -2700, 40 cm thick; its south face y -2720), the squad of three south of it. The player's path, every
// end click inside the world tick (OnWorldPreActorTick) as the player's click lands (the controller's PlayerTick): two clicks on the face
// mid-wall (the group cover order = the formation wall layout, the Commander mid-wall); the hover + click on the WEST end -> the west-end man
// peeks and IS the formation leader (selection, ring, camera follow), nobody passes him or stands past the corner, the others behind him along
// the near face in the order they stood; then the EAST end from that stack -> the east-end man leads, the old leader leaves his corner, the
// same checks mirrored; then a ground click away from the wall -> the new leader and the followers' order are kept (no revert); then the
// layout again mid-wall, the west-end man knocked down, the west end clicked -> the next nearest takes the corner and the lead.
// A/B: -PreCmds "Codex.Formation.WallLeadership 0" = the v2.4 squad peek (the leader stays: the leadership checks fail).
// #57 (2026-11-07): both runs set Codex.Formation.TwoCornerHold 0 - the east peek from the west stack moves the stack (the rule before #57; the
// default two-corner hold is TwoCornerHoldSmoke's).
// CodexTactics.WallLeadershipSlideSmoke (the game designer's bug 2026-11-03 on L_Gen_Depot_1: the right-end peek after the left-end one at the
// same long wall - all three stood up, faced their walk and walked along the wall, the rear wide open; item 2's re-form rules): the same
// path up to the east peek from the west stack (a 22 m slide along the SAME wall), checked for: no follower stands up, someone keeps
// looking back the way they came (the rear covered) through the walk, the stack settled in a reasonable time (crouched pace), <= 1
// restart per man. A/B: -PreCmds "Codex.Formation.ReformAlongWall 0" = item 2's stand-up / face-the-walk (the checks fail).
// "Shot" there saves a still mid-walk and of each stack to Saved/Screenshots/WallSlide/.
// "Shot" (rendered: UnrealEditor.exe <uproject> /Game/Maps/L_FormationLab -game -RenderOffscreen -ResX=1280 -ResY=720 -UseFixedTimeStep -FPS=60
// "-LevelJson=L_FormationLab.json" "-ExecCmds=CodexTactics.WallLeadershipSmoke Shot") saves stills of the stack behind the new leader to
// Saved/Screenshots/WallLeadership/.
// Godot reference: none (UE-only; docs/systems/squad-and-orders.md "Wall leadership").

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/KnockdownComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/World.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTypes.h"
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace WallLeadershipSmoke
{
	/** W_B_3 (Content/Data/FormationLab/formation_lab_01.json): x 1500..4000, its south face at y -2720. */
	constexpr float WallWestX = 1500.f;
	constexpr float WallEastX = 4000.f;
	constexpr float WallFaceY = -2720.f;
	/** A man at a wall stands ~45 cm off its face. */
	constexpr float SlotOffCm = 45.f;
	constexpr float StartY = -3300.f;
	constexpr float MidX = 2750.f;
	constexpr float HoldSeconds = 2.f;
	/** Nobody stands past the corner: a body centre this far inside the wall's end at least (the leader's capsule at his corner ~40 cm in). */
	constexpr float PastCornerMarginCm = 10.f;
	/** Nobody passes the leader: every other body centre at least this far behind him along the wall, cm. */
	constexpr float BehindLeaderMinCm = 40.f;

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
		bool bShot = false;
		float GroundZ = 0.f;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		/** The man the order should hand the lead to, the leader before it, the switch count before it. */
		TWeakObjectPtr<AOperativeCharacter> Expected;
		TWeakObjectPtr<AOperativeCharacter> OldLeader;
		TWeakObjectPtr<AOperativeCharacter> Knocked;
		int32 SwitchesBefore = 0;
		/** Over the walk into the stack: the closest any other body came to the leader along the wall (negative = past him) and to the corner. */
		float MinBehindCm = TNumericLimits<float>::Max();
		FString MinBehindWho;
		float MinInsideCm = TNumericLimits<float>::Max();
		FString MinInsideWho;
		bool bOldLeaderOut = false;
		float OldLeaderOutAt = -1.f;
		float HoldTime = 0.f;
		/** The followers' order fixed in the stack (nearest the leader first) - kept after leaving the wall (decision 6). */
		TArray<TWeakObjectPtr<AOperativeCharacter>> StackOrder;
		int32 LeaderLostFrames = 0;
		int32 ShotIndex = 0;
		TWeakObjectPtr<ACameraActor> Camera;
		EClick PendingClick = EClick::None;
		bool bClickDone = false;
		FDelegateHandle PreTickHandle;
		/**
		 * Item 2 (the game designer's bug report 2026-11-02: the re-form behind the new leader takes far too long, the others jostle and shuffle
		 * in place): when the leader arrived at his corner (in the peek pose), per member whether he moved / stood still how long / how often
		 * he started walking again after a stop (a re-path that shows as a shuffle), and the stage time the whole stack settled.
		 */
		float LeaderArriveAt = -1.f;
		TArray<bool> Moved;
		TArray<float> StillFor;
		TArray<int32> Restarts;
		TArray<float> PathCm;
		TArray<FVector> LastPos;
		/** The worst re-form of the run (s after the leader's arrival) and the most restarts of one man in one re-form. */
		float WorstReform = 0.f;
		int32 WorstRestarts = 0;
		/**
		 * WallLeadershipSlideSmoke (bug 2026-11-03): the run checks the long same-wall slides - per member whether he stood up during the walk;
		 * the walk's frames and those with a follower looking back the way they came; the mid-walk still taken.
		 */
		bool bSlideMode = false;
		TArray<bool> Stood;
		int32 WalkFrames = 0;
		int32 RearFrames = 0;
		float LongestGapSeconds = 0.f;
		float GapSeconds = 0.f;
		int32 MidShot = 0;
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
		FPlatformMisc::RequestExit(false, TEXT("WallLeadershipSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
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
				Line += FString::Printf(TEXT(" %s (%.0f, %.0f)%s%s%s;"), *NameOf(Op), Op->GetActorLocation().X, Op->GetActorLocation().Y, Op->bInCover ? TEXT(" in cover") : TEXT(""),
					Op->IsManualCornerPeek() ? TEXT(" peeking") : TEXT(""), Op->IsKnockedDown() ? TEXT(" DOWN") : TEXT(""));
			}
		}
		return Line;
	}

	/** The cursor hit on W_B_3's south face at X (a real trace from the south: the wall's own component, as the cursor gives it). */
	bool FaceHit(UWorld* World, const FState& State, float X, FHitResult& OutHit)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WallLeadershipSmokeFace), false);
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
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WallLeadershipSmokeGround), false);
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			Params.AddIgnoredActor(Weak.Get());
		}
		return World->LineTraceSingleByChannel(OutHit, FVector(X, Y, State.GroundZ + 500.f), FVector(X, Y, State.GroundZ - 500.f), ECC_Visibility, Params);
	}

	/** The squad of three south of W_B_3, facing it, in group mode (the Commander leading). */
	bool PlaceSquad(FState& State, USquadSubsystem* Squad)
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

	bool InPeekPose(const AOperativeCharacter& Op)
	{
		return Op.bInCover && Op.IsManualCornerPeek() && Op.IsCornerAimActive() && Op.bAtCoverCorner && !Op.bShimmying;
	}

	/** The members sorted along the wall (west first). */
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

	/** The world-tick clicks: the player's clicks land in the controller's PlayerTick, before the squad updates. */
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
			if (GroundHit(World, State, Leader->GetActorLocation().X - 300.f, WallFaceY - 650.f, Hit))
			{
				PC->HandleWorldHit(Hit);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the ground click 6.5 m off the wall at (%.0f, %.0f)"), Hit.ImpactPoint.X, Hit.ImpactPoint.Y);
			return;
		}
		// The hover on the end (20 cm in from it), then the click there: the squad's corner peek.
		const bool bWest = Click == EClick::WestEnd;
		const float X = bWest ? WallWestX + 20.f : WallEastX - 20.f;
		if (!FaceHit(World, State, X, Hit))
		{
			Check(State, false, FString::Printf(TEXT("the cursor hit on W_B_3's %s end"), bWest ? TEXT("west") : TEXT("east")));
			return;
		}
		const bool bGhost = PC->UpdateCornerPeekHover(Leader, &Hit);
		PC->HandleWorldHit(Hit);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the click on the %s end at (%.0f, %.0f) (ghost %d) -> leader %s, peeker %s;%s"), bWest ? TEXT("west") : TEXT("east"),
			Hit.ImpactPoint.X, Hit.ImpactPoint.Y, bGhost ? 1 : 0, *NameOf(Squad->GetLeader()), *NameOf(Squad->GetFormationPeeker()), *Where(State));
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
				// No motion blur: the still is taken a few frames after the cut, but the cut itself smeared the first try.
				Created->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
				Created->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
				Created->GetCameraComponent()->PostProcessBlendWeight = 1.f;
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

	/** Shot mode: the camera on the stack at the corner (Angle 0 high from the open side, 1 low along the face); the still a few frames later. */
	void AimStill(UWorld* World, FState& State, bool bWest, int32 Angle)
	{
		const float In = bWest ? 1.f : -1.f;
		const FVector Corner(bWest ? WallWestX : WallEastX, WallFaceY - 60.f, State.GroundZ + 80.f);
		const FVector Eye = Angle == 0 ? Corner + FVector(In * 450.f, -900.f, 750.f) : Corner + FVector(-In * 350.f, -420.f, 260.f);
		PlaceCamera(World, State, Eye, Corner + FVector(In * 200.f, 0.f, 0.f));
	}

	void ShootStill(FState& State, const TCHAR* Tag, int32 Angle)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / (State.bSlideMode ? TEXT("WallSlide") : TEXT("WallLeadership"))
			/ FString::Printf(TEXT("%s_%s_%d_%02d.png"), State.bSlideMode ? TEXT("wall_slide") : TEXT("wall_leadership"), Tag, Angle, State.ShotIndex++);
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogCodexTactics, Display, TEXT("WallLeadershipSmoke: still %s"), *Path);
	}

	/** The leader switch the order made: lead, selection, ring, camera, peek. */
	void CheckSwitch(FState& State, USquadSubsystem* Squad, ACodexTacticsPlayerController* PC, const TCHAR* Label)
	{
		AOperativeCharacter* Expected = State.Expected.Get();
		AOperativeCharacter* Leader = Squad->GetLeader();
		const TArray<AOperativeCharacter*> Selected = Squad->GetSelectedGroup();
		const ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(PC->GetPawn());
		const AOperativeCharacter* Old = State.OldLeader.Get();
		Check(State, Expected && Leader == Expected, FString::Printf(TEXT("%s: the man nearest that corner (%s) is the formation leader (leader %s, was %s)"), Label, *NameOf(Expected),
			*NameOf(Leader), *NameOf(Old)));
		Check(State, Squad->GetFormationPeeker() == Expected, FString::Printf(TEXT("%s: ... and he peeks (peeker %s)"), Label, *NameOf(Squad->GetFormationPeeker())));
		Check(State, Selected.Num() == 1 && Selected[0] == Expected && Expected && Expected->IsSelectionRingShown() && (!Old || Old == Expected || !Old->IsSelectionRingShown()),
			FString::Printf(TEXT("%s: the selection and the ring moved to him (selected %d: %s; his ring %d, the old leader's %d)"), Label, Selected.Num(),
				Selected.Num() > 0 ? *NameOf(Selected[0]) : TEXT("-"), Expected && Expected->IsSelectionRingShown() ? 1 : 0, Old && Old->IsSelectionRingShown() ? 1 : 0));
		Check(State, Camera && Camera->GetFollowTarget() == Expected, FString::Printf(TEXT("%s: the camera follows him (%s)"), Label,
			Camera ? *GetNameSafe(Camera->GetFollowTarget()) : TEXT("no camera pawn")));
		Check(State, Old == Expected || Squad->GetWallLeadershipSwitchCount() == State.SwitchesBefore + 1, FString::Printf(TEXT("%s: one wall-leadership switch (%d -> %d)"), Label,
			State.SwitchesBefore, Squad->GetWallLeadershipSwitchCount()));
		State.MinBehindCm = TNumericLimits<float>::Max();
		State.MinInsideCm = TNumericLimits<float>::Max();
		State.MinBehindWho.Reset();
		State.MinInsideWho.Reset();
		State.HoldTime = 0.f;
		State.bOldLeaderOut = false;
		State.OldLeaderOutAt = -1.f;
		State.LeaderArriveAt = -1.f;
		State.Stood.Init(false, State.Members.Num());
		State.WalkFrames = 0;
		State.RearFrames = 0;
		State.LongestGapSeconds = 0.f;
		State.GapSeconds = 0.f;
		State.MidShot = 0;
		State.Moved.Init(false, State.Members.Num());
		State.StillFor.Init(0.f, State.Members.Num());
		State.Restarts.Init(0, State.Members.Num());
		State.PathCm.Init(0.f, State.Members.Num());
		State.LastPos.Reset();
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			State.LastPos.Add(Weak.IsValid() ? Weak->GetActorLocation() : FVector::ZeroVector);
		}
	}

	/** Moving above this speed, standing below this one for at least RestartStillSeconds = a stop (cm/s, s). */
	constexpr float ReformMovingSpeed = 30.f;
	constexpr float ReformStillSpeed = 5.f;
	constexpr float RestartStillSeconds = 0.2f;
	/** Item 2 targets (the game designer's bug report 2026-11-02; pending his confirmation): everyone settled this soon after the leader is at his corner, s, and no man starts walking again after a stop more than this often. */
	constexpr float ReformTargetSeconds = 1.5f;
	constexpr int32 ReformMaxRestarts = 1;

	/**
	 * The game designer's bug 2026-11-03: a re-form whose longest walk is farther than this runs along the same wall crouched, the rear-looker
	 * walking backwards (squad_walls.json reform_along_wall_off_m) - its target is the slide's crouched pace below, not item 2's 1.5 s after
	 * the leader (who walks upright to his corner); a shorter re-form keeps item 2's target, cm.
	 */
	constexpr float ReformLongSlideCm = 300.f;
	/**
	 * A reasonable time for the slide at the crouched pace: settled within this much time per metre of the longest walk plus a margin - the
	 * rear-looker walks backwards at move.backward_mult 0.9 x the crouch walk 125 cm/s = 112 cm/s (0.89 s/m).
	 */
	constexpr float SlideSecondsPerMetre = 0.9f;
	constexpr float SlideMarginSeconds = 4.f;

	/** One frame of the re-form's measurement (item 2). */
	void SampleReform(FState& State, const AOperativeCharacter* Leader, bool bWest, float DeltaTime, const USquadSubsystem* Squad)
	{
		// The re-form's trace every 0.5 s (the evidence of a slow / jostling re-form).
		if (Squad && FMath::FloorToInt32(State.StageTime / 0.5f) != FMath::FloorToInt32((State.StageTime - DeltaTime) / 0.5f))
		{
			FString Line;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				const AOperativeCharacter* Op = Weak.Get();
				FVector Slot = FVector::ZeroVector;
				float Look = 0.f;
				const bool bSlot = Op && Squad->GetFormationTarget(Op, Slot, Look);
				if (Op)
				{
					Line += FString::Printf(TEXT(" %s x %.0f y %.0f v %.0f stance %d%s slot x %.0f (%.0f off);"), *NameOf(Op), Op->GetActorLocation().X, Op->GetActorLocation().Y,
						Op->GetVelocity().Size2D(), static_cast<int32>(Op->GetStance()), Op->IsSprinting() ? TEXT(" sprint") : TEXT(""), bSlot ? Slot.X : 0.f,
						bSlot ? FVector::Dist2D(Slot, Op->GetActorLocation()) : -1.f);
				}
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke trace %.1f s:%s"), State.StageTime, *Line);
		}
		if (State.LeaderArriveAt < 0.f && Leader && InPeekPose(*Leader) && FMath::Abs(Leader->GetActorLocation().X - (bWest ? WallWestX : WallEastX)) <= 130.f)
		{
			State.LeaderArriveAt = State.StageTime;
		}
		for (int32 Index = 0; Index < State.Members.Num() && Index < State.Moved.Num(); ++Index)
		{
			const AOperativeCharacter* Op = State.Members[Index].Get();
			if (!Op || Op == Leader || Op->IsKnockedDown() || Op == State.Knocked.Get())
			{
				continue; // the knocked-down man is no part of the re-form (he may get up and walk on later)
			}
			State.PathCm[Index] += static_cast<float>(FVector::Dist2D(Op->GetActorLocation(), State.LastPos[Index]));
			State.LastPos[Index] = Op->GetActorLocation();
			const float Speed = static_cast<float>(Op->GetVelocity().Size2D());
			if (Speed >= ReformMovingSpeed)
			{
				if (State.Moved[Index] && State.StillFor[Index] >= RestartStillSeconds)
				{
					++State.Restarts[Index];
				}
				State.Moved[Index] = true;
				State.StillFor[Index] = 0.f;
			}
			else if (Speed < ReformStillSpeed)
			{
				State.StillFor[Index] += DeltaTime;
			}
		}
	}

	/** The re-form's numbers once the stack is held (item 2); checked against the targets. */
	void CheckReform(FState& State, const TCHAR* Label, const AOperativeCharacter* Leader)
	{
		const float SettledAt = State.StageTime - State.HoldTime;
		const float Reform = State.LeaderArriveAt >= 0.f ? FMath::Max(0.f, SettledAt - State.LeaderArriveAt) : SettledAt;
		int32 MostRestarts = 0;
		FString Line;
		for (int32 Index = 0; Index < State.Members.Num() && Index < State.Restarts.Num(); ++Index)
		{
			const AOperativeCharacter* Op = State.Members[Index].Get();
			if (Op && Op != Leader && !Op->IsKnockedDown() && Op != State.Knocked.Get())
			{
				MostRestarts = FMath::Max(MostRestarts, State.Restarts[Index]);
				Line += FString::Printf(TEXT(" %s %d restarts, %.0f cm walked;"), *NameOf(Op), State.Restarts[Index], State.PathCm[Index]);
			}
		}
		float LongestCm = 0.f;
		for (int32 Index = 0; Index < State.Members.Num() && Index < State.PathCm.Num(); ++Index)
		{
			const AOperativeCharacter* Op = State.Members[Index].Get();
			if (Op && Op != Leader && !Op->IsKnockedDown() && Op != State.Knocked.Get())
			{
				LongestCm = FMath::Max(LongestCm, State.PathCm[Index]);
			}
		}
		State.WorstReform = FMath::Max(State.WorstReform, Reform);
		State.WorstRestarts = FMath::Max(State.WorstRestarts, MostRestarts);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke metric: %s: re-form %.2f s after the leader reached his corner (he arrived %.2f s, all settled %.2f s after the click; the longest walk %.0f cm):%s"), Label,
			Reform, State.LeaderArriveAt, SettledAt, LongestCm, *Line);
		if (LongestCm > ReformLongSlideCm)
		{
			// Bug 2026-11-03: a long slide along the same wall - crouched, the rear watched: the crouched pace's bound from the click.
			const float Limit = LongestCm / 100.f * SlideSecondsPerMetre + SlideMarginSeconds;
			Check(State, SettledAt <= Limit, FString::Printf(TEXT("%s: a long slide along the same wall (%.0f cm) - the stack settled %.2f s after the click at the crouched pace (<= %.1f s)"),
				Label, LongestCm, SettledAt, Limit));
		}
		else
		{
			Check(State, Reform <= ReformTargetSeconds, FString::Printf(TEXT("%s: the stack settled %.2f s after the leader reached his corner (target <= %.1f s)"), Label, Reform,
				ReformTargetSeconds));
		}
		Check(State, MostRestarts <= ReformMaxRestarts, FString::Printf(TEXT("%s: no milling about - at most %d restart(s) after a stop per man (%d):%s"), Label, ReformMaxRestarts,
			MostRestarts, *Line));
	}

	/** A follower looks back the way they came within this of straight back along the wall (his sight sector covers the rear), deg. */
	constexpr float RearConeDeg = 70.f;
	/** The walk lasts while a follower is farther than this from his place in the stack, cm. */
	constexpr float WalkFarCm = 150.f;
	/** The rear covered on at least this share of the walk's frames (a turn round a team-mate may take a moment). */
	constexpr float RearShareMin = 0.85f;
	/** ... and never unwatched longer than this at a stretch, s. */
	constexpr float RearGapMaxSeconds = 1.5f;

	/** WallLeadershipSlideSmoke: one frame of the slide - nobody stands, the rear watched (bWest = the walk goes west). */
	void SampleSlide(FState& State, const AOperativeCharacter* Leader, bool bWest, float DeltaTime, const USquadSubsystem* Squad)
	{
		const float RearYaw = bWest ? 0.f : 180.f; // back along the wall, the way they came
		bool bWalking = false;
		bool bRear = false;
		for (int32 Index = 0; Index < State.Members.Num() && Index < State.Stood.Num(); ++Index)
		{
			const AOperativeCharacter* Op = State.Members[Index].Get();
			if (!Op || Op == Leader || Op->IsKnockedDown())
			{
				continue;
			}
			State.Stood[Index] |= Op->GetStance() == EOperativeStance::Standing;
			FVector Slot = FVector::ZeroVector;
			float Look = 0.f;
			bWalking |= Squad && Squad->GetFormationTarget(Op, Slot, Look) && FVector::Dist2D(Slot, Op->GetActorLocation()) > WalkFarCm;
			const float Yaw = static_cast<float>(Op->GetSightFacing().Rotation().Yaw);
			bRear |= FMath::Abs(FRotator::NormalizeAxis(Yaw - RearYaw)) <= RearConeDeg;
		}
		if (!bWalking || State.StageTime < 1.f)
		{
			return; // the first second: the order's turn
		}
		++State.WalkFrames;
		State.RearFrames += bRear ? 1 : 0;
		State.GapSeconds = bRear ? 0.f : State.GapSeconds + DeltaTime;
		State.LongestGapSeconds = FMath::Max(State.LongestGapSeconds, State.GapSeconds);
	}

	/** WallLeadershipSlideSmoke: the slide's checks once the stack is held. */
	void CheckSlide(FState& State, const TCHAR* Label, const AOperativeCharacter* Leader)
	{
		const float SettledAt = State.StageTime - State.HoldTime;
		float LongestCm = 0.f;
		int32 MostRestarts = 0;
		FString Stood;
		FString Line;
		for (int32 Index = 0; Index < State.Members.Num() && Index < State.Stood.Num(); ++Index)
		{
			const AOperativeCharacter* Op = State.Members[Index].Get();
			if (!Op || Op == Leader || Op->IsKnockedDown())
			{
				continue;
			}
			LongestCm = FMath::Max(LongestCm, State.PathCm.IsValidIndex(Index) ? State.PathCm[Index] : 0.f);
			MostRestarts = FMath::Max(MostRestarts, State.Restarts.IsValidIndex(Index) ? State.Restarts[Index] : 0);
			Stood += State.Stood[Index] ? FString::Printf(TEXT(" %s;"), *NameOf(Op)) : FString();
			Line += FString::Printf(TEXT(" %s %.0f cm, %d restarts;"), *NameOf(Op), State.PathCm.IsValidIndex(Index) ? State.PathCm[Index] : 0.f,
				State.Restarts.IsValidIndex(Index) ? State.Restarts[Index] : 0);
		}
		const float Share = State.WalkFrames > 0 ? static_cast<float>(State.RearFrames) / static_cast<float>(State.WalkFrames) : 1.f;
		const float Limit = LongestCm / 100.f * SlideSecondsPerMetre + SlideMarginSeconds;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke metric: %s slide: settled %.2f s after the click (limit %.1f s for the longest walk %.0f cm; the leader at his corner %.2f s), rear watched %.0f %% of %d walk frames (longest gap %.2f s), stood up:%s;%s"),
			Label, SettledAt, Limit, LongestCm, State.LeaderArriveAt, Share * 100.f, State.WalkFrames, State.LongestGapSeconds, Stood.IsEmpty() ? TEXT(" nobody") : *Stood, *Line);
		Check(State, Stood.IsEmpty(), FString::Printf(TEXT("%s slide: no follower stood up for the walk along the same wall (stood:%s)"), Label, Stood.IsEmpty() ? TEXT(" nobody") : *Stood));
		Check(State, Share >= RearShareMin && State.LongestGapSeconds <= RearGapMaxSeconds, FString::Printf(TEXT("%s slide: the rear watched - a follower looked back the way they came on %.0f %% of the walk (>= %.0f %%), the longest gap %.2f s (<= %.1f s)"),
			Label, Share * 100.f, RearShareMin * 100.f, State.LongestGapSeconds, RearGapMaxSeconds));
		Check(State, SettledAt <= Limit, FString::Printf(TEXT("%s slide: the stack settled in a reasonable time - %.2f s after the click (<= %.1f s for %.0f cm)"), Label, SettledAt, Limit, LongestCm));
		Check(State, MostRestarts <= ReformMaxRestarts, FString::Printf(TEXT("%s slide: no milling about - at most %d restart(s) per man (%d):%s"), Label, ReformMaxRestarts, MostRestarts, *Line));
	}

	/** One frame of the walk into the stack: nobody passes the leader, nobody past the corner. */
	void SampleWalk(FState& State, const AOperativeCharacter* Leader, bool bWest)
	{
		if (!Leader)
		{
			return;
		}
		const float Toward = bWest ? -1.f : 1.f;
		const float EndX = bWest ? WallWestX : WallEastX;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			const AOperativeCharacter* Op = Weak.Get();
			if (!Op)
			{
				continue;
			}
			const float Inside = (EndX - Op->GetActorLocation().X) * Toward;
			if (Inside < State.MinInsideCm)
			{
				State.MinInsideCm = Inside;
				State.MinInsideWho = NameOf(Op);
			}
			if (Op != Leader && !Op->IsKnockedDown() && Op != State.Knocked.Get())
			{
				const float Behind = (Leader->GetActorLocation().X - Op->GetActorLocation().X) * Toward;
				if (Behind < State.MinBehindCm)
				{
					State.MinBehindCm = Behind;
					State.MinBehindWho = NameOf(Op);
				}
			}
		}
	}

	/** The held stack: the leader peeking at the corner, the others settled behind him along the near face. False while it is not there yet. */
	bool StackSettled(const FState& State, const AOperativeCharacter* Leader, bool bWest)
	{
		if (!Leader || !InPeekPose(*Leader) || FMath::Abs(Leader->GetActorLocation().X - (bWest ? WallWestX : WallEastX)) > 130.f)
		{
			return false;
		}
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			const AOperativeCharacter* Op = Weak.Get();
			if (Op && Op != Leader && !Op->IsKnockedDown() && !(Op->IsInFormationWallPose() && Op->GetVelocity().Size2D() < 5.f))
			{
				return false;
			}
		}
		return true;
	}

	/** The held stack's checks (decision 4) and the order it fixed. */
	void CheckStack(FState& State, const TCHAR* Label, const AOperativeCharacter* Leader, bool bWest)
	{
		const float Toward = bWest ? -1.f : 1.f;
		Check(State, State.HoldTime >= HoldSeconds, FString::Printf(TEXT("%s: %s peeks round the %s corner in the peek pose (held %.1f s, %.1f s after the click)"), Label, *NameOf(Leader),
			bWest ? TEXT("west") : TEXT("east"), State.HoldTime, State.StageTime));
		Check(State, State.MinBehindCm >= BehindLeaderMinCm, FString::Printf(TEXT("%s: nobody passed the leader on the way (closest %.0f cm behind him: %s)"), Label,
			State.MinBehindCm, *State.MinBehindWho));
		Check(State, State.MinInsideCm >= PastCornerMarginCm, FString::Printf(TEXT("%s: nobody stood past the corner (closest %.0f cm inside the end: %s)"), Label, State.MinInsideCm,
			*State.MinInsideWho));
		bool bBehind = Leader != nullptr;
		FString Line;
		TArray<AOperativeCharacter*> Others;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			AOperativeCharacter* Op = Weak.Get();
			if (!Op || Op == Leader || !Leader)
			{
				continue;
			}
			Others.Add(Op);
			const FVector At = Op->GetActorLocation();
			const float Behind = (Leader->GetActorLocation().X - At.X) * Toward;
			const bool bHere = Op->IsKnockedDown() || (FMath::Abs(At.Y - (WallFaceY - SlotOffCm)) <= 40.f && Behind >= 60.f && Behind <= 450.f && Op->IsInFormationWallPose());
			bBehind &= bHere;
			Line += FString::Printf(TEXT(" %s %.0f cm behind, %.0f cm off the face%s%s;"), *NameOf(Op), Behind, WallFaceY - At.Y, Op->IsKnockedDown() ? TEXT(" (down)") : TEXT(""),
				bHere ? TEXT("") : TEXT(" OFF"));
		}
		Check(State, bBehind, FString::Printf(TEXT("%s: all the others line up along the near face behind him:%s"), Label, *Line));
		// The order fixed in the stack: the nearest him first (decision 6 checks it is kept off the wall).
		Others.Sort([Leader, Toward](const AOperativeCharacter& A, const AOperativeCharacter& B)
		{
			return (Leader->GetActorLocation().X - A.GetActorLocation().X) * Toward < (Leader->GetActorLocation().X - B.GetActorLocation().X) * Toward;
		});
		State.StackOrder.Reset();
		for (AOperativeCharacter* Op : Others)
		{
			State.StackOrder.Add(Op);
		}
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
			Check(State, USquadSubsystem::IsWallLeadershipEnabled(), TEXT("Codex.Formation.WallLeadership is 1 (0 = the v2.4 squad peek: the A/B)"));
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
			FCollisionQueryParams Params(SCENE_QUERY_STAT(WallLeadershipSmokeStart), false);
			for (AOperativeCharacter* Each : Roster)
			{
				Params.AddIgnoredActor(Each);
			}
			State.GroundZ = World->LineTraceSingleByChannel(Ground, FVector(MidX, StartY, 2000.f), FVector(MidX, StartY, -2000.f), ECC_Visibility, Params)
				? static_cast<float>(Ground.ImpactPoint.Z) : 0.f;
			FHitResult Probe;
			const bool bWall = FaceHit(World, State, MidX, Probe) && FaceHit(World, State, WallWestX + 20.f, Probe) && FaceHit(World, State, WallEastX - 20.f, Probe);
			Check(State, bWall, FString::Printf(TEXT("the lab wall W_B_3 stands where the layout puts it (south face y %.0f, x %.0f..%.0f; ground z %.0f)"), WallFaceY, WallWestX,
				WallEastX, State.GroundZ));
			if (!bWall || !PlaceSquad(State, Squad))
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
			if (State.Stage == 2)
			{
				Check(State, Squad->HasFormationWallOrder(), FString::Printf(TEXT("the clicks mid-wall (%s leading) - the group cover order as the formation wall layout"),
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
			Check(State, bSettled, FString::Printf(TEXT("the squad hugs W_B_3 in the wall layout, crouched (%.1f s):%s"), State.StageTime, *Where(State)));
			const TArray<AOperativeCharacter*> Sorted = AlongWall(State);
			if (Sorted.Num() != 3)
			{
				return Finish(State, false);
			}
			// Decisions 2 / 3: the Commander mid-wall, the west-end man takes the west corner.
			Check(State, Sorted[1] == Leader, FString::Printf(TEXT("the leader %s stands mid-wall (west end %s, east end %s)"), *NameOf(Leader), *NameOf(Sorted[0]), *NameOf(Sorted[2])));
			State.OldLeader = Leader;
			State.SwitchesBefore = Squad->GetWallLeadershipSwitchCount();
			State.Expected = Sorted[0];
			State.PendingClick = EClick::WestEnd;
			Next(State, 4);
			return true;
		}
		case 23:
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			State.PendingClick = EClick::WestEnd;
			Next(State, 24);
			return true;
		case 4:
		case 6:
		case 24:
		{
			// The end click ran in the world tick: the leader switch.
			if (State.PendingClick != EClick::None)
			{
				return true;
			}
			const TCHAR* Label = State.Stage == 4 ? TEXT("west peek") : State.Stage == 6 ? TEXT("east peek (from the west stack)") : TEXT("west peek from the east stack, its west man down");
			CheckSwitch(State, Squad, PC, Label);
			if (State.Stage == 24)
			{
				const AOperativeCharacter* Down = State.Knocked.Get();
				Check(State, Down && Squad->GetLeader() != Down && Squad->GetFormationPeeker() != Down, FString::Printf(TEXT("%s: the knocked-down %s neither leads nor peeks"), Label,
					*NameOf(Down)));
			}
			Next(State, State.Stage + 1);
			return true;
		}
		case 5:
		case 7:
		case 25:
		{
			const bool bWest = State.Stage != 7;
			SampleWalk(State, Leader, bWest);
			const AOperativeCharacter* Old = State.OldLeader.Get();
			if (State.Stage != 5 && Old && !State.bOldLeaderOut && !Old->bInCover)
			{
				State.bOldLeaderOut = true;
				State.OldLeaderOutAt = State.StageTime;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the old leader %s left his corner %.2f s after the order"), *NameOf(Old), State.StageTime);
			}
			if (State.HoldTime <= 0.f)
			{
				SampleReform(State, Leader, bWest, DeltaTime, Squad);
				if (State.bSlideMode)
				{
					SampleSlide(State, Leader, bWest, DeltaTime, Squad);
				}
			}
			// WallLeadershipSlideSmoke "Shot": a still of the walk along the wall (the followers crouched, the rear watched).
			if (State.bShot && State.bSlideMode && State.Stage == 7 && State.MidShot == 0 && State.StageTime >= 3.f)
			{
				const FVector Middle(0.5f * (WallWestX + WallEastX), WallFaceY - 60.f, State.GroundZ + 80.f);
				PlaceCamera(World, State, Middle + FVector(-300.f, -1400.f, 1100.f), Middle + FVector(-200.f, 0.f, 0.f));
				State.MidShot = 1;
			}
			else if (State.bShot && State.bSlideMode && State.MidShot == 1 && State.StageTime >= 3.6f)
			{
				ShootStill(State, TEXT("walk"), 0);
				State.MidShot = 2;
			}
			State.HoldTime = StackSettled(State, Leader, bWest) ? State.HoldTime + DeltaTime : 0.f;
			if (State.HoldTime < HoldSeconds && State.StageTime < 50.f)
			{
				return true;
			}
			const TCHAR* Label = State.Stage == 5 ? TEXT("west peek") : State.Stage == 7 ? TEXT("east peek") : TEXT("west peek, the west man down");
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s: %.1f s after the click:%s"), Label, State.StageTime, *Where(State));
			Check(State, Leader == State.Expected.Get(), FString::Printf(TEXT("%s: he still leads (%s)"), Label, *NameOf(Leader)));
			if (State.Stage != 5)
			{
				Check(State, State.bOldLeaderOut && State.OldLeaderOutAt <= 3.f, FString::Printf(TEXT("%s: the old leader %s left his corner (%.2f s after the order)"), Label,
					*NameOf(Old), State.OldLeaderOutAt));
			}
			CheckStack(State, Label, Leader, bWest);
			if (State.bSlideMode)
			{
				CheckSlide(State, Label, Leader);
			}
			else
			{
				CheckReform(State, Label, Leader);
			}
			if (State.bShot)
			{
				AimStill(World, State, bWest, 0);
				State.ShotIndex = 0;
			}
			Next(State, State.Stage + 100);
			return true;
		}
		case 105:
		case 107:
		case 125:
		{
			// Shot mode: the two angles (each still 0.6 s after its camera cut), then the view back to the player's pawn.
			const bool bWest = State.Stage != 107;
			const TCHAR* Tag = State.Stage == 105 ? TEXT("west") : State.Stage == 107 ? TEXT("east") : TEXT("down");
			if (State.bShot && State.ShotIndex == 0 && State.StageTime >= 0.6f)
			{
				ShootStill(State, Tag, 0);
				AimStill(World, State, bWest, 1);
			}
			else if (State.bShot && State.ShotIndex == 1 && State.StageTime >= 1.2f)
			{
				ShootStill(State, Tag, 1);
			}
			if (State.bShot && State.ShotIndex < 2)
			{
				return true;
			}
			Next(State, State.Stage * 10);
			return true;
		}
		case 1050:
		case 1070:
		case 1250:
		{
			if (State.bShot && State.StageTime < 0.5f)
			{
				return true;
			}
			if (APlayerController* Player = UGameplayStatics::GetPlayerController(World, 0); Player && State.bShot)
			{
				Player->SetViewTarget(Player->GetPawn());
			}
			State.OldLeader = Leader;
			State.SwitchesBefore = Squad->GetWallLeadershipSwitchCount();
			const TArray<AOperativeCharacter*> Sorted = AlongWall(State);
			if (State.Stage == 1070 && State.bSlideMode)
			{
				Next(State, 30); // WallLeadershipSlideSmoke: the slides are checked
				return true;
			}
			if (State.Stage == 1050 && Sorted.Num() == 3)
			{
				// Decision 3: then the east corner from that stack.
				State.Expected = Sorted.Last();
				State.PendingClick = EClick::EastEnd;
				Next(State, 6);
				return true;
			}
			if (State.Stage == 1070 && Sorted.Num() == 3)
			{
				// Decision 7: the west corner from the east stack with its west man (the tail) knocked down - the next nearest (the middle man,
				// not the leader at the east corner) takes the corner and the lead.
				AOperativeCharacter* Down = Sorted[0];
				const bool bDown = Down->KnockdownComponent && Down->KnockdownComponent->ForceKnockDown(EKnockdownDirection::Back, EKnockdownCause::HeavyHit);
				Check(State, bDown && Down->IsKnockedDown(), FString::Printf(TEXT("the stack's west man %s is knocked down"), *NameOf(Down)));
				State.Knocked = Down;
				State.Expected = Sorted[1];
				Next(State, 23);
				return true;
			}
			// Decision 6: leave the wall - the leader and the order stay.
			State.Expected = Leader;
			State.LeaderLostFrames = 0;
			State.PendingClick = EClick::AwayGround;
			Next(State, 8);
			return true;
		}
		case 8:
		{
			if (State.PendingClick != EClick::None)
			{
				return true;
			}
			State.LeaderLostFrames += Squad->GetLeader() == State.Expected.Get() ? 0 : 1;
			if (State.StageTime < 6.f)
			{
				return true;
			}
			// The order kept: the man right behind the leader in the stack does not become the rear guard, the stack's last man does.
			EFormationRole First = EFormationRole::Commander;
			EFormationRole Last = EFormationRole::Commander;
			const AOperativeCharacter* FirstOp = State.StackOrder.IsEmpty() ? nullptr : State.StackOrder[0].Get();
			const AOperativeCharacter* LastOp = State.StackOrder.IsEmpty() ? nullptr : State.StackOrder.Last().Get();
			const bool bRoles = FirstOp && LastOp && Squad->GetFormationRole(FirstOp, First) && Squad->GetFormationRole(LastOp, Last);
			const bool bMovedAway = Leader->GetActorLocation().Y < WallFaceY - 300.f;
			Check(State, bMovedAway && !Squad->HasFormationWallOrder() && !Squad->GetFormationPeeker(), FString::Printf(TEXT("leaving the wall: the squad walks away, the wall order and the peek over (leader at y %.0f)"),
				Leader->GetActorLocation().Y));
			Check(State, State.LeaderLostFrames == 0 && Leader == State.Expected.Get(), FString::Printf(TEXT("leaving the wall: the new leader %s is kept - no revert (%d frames with another leader)"),
				*NameOf(State.Expected.Get()), State.LeaderLostFrames));
			Check(State, bRoles && First != EFormationRole::Rear && Last == EFormationRole::Rear, FString::Printf(TEXT("leaving the wall: the stack's order kept - %s (2nd in the stack) is %s, %s (last) the rear guard %s;%s"),
				*NameOf(FirstOp), bRoles ? FormationNames::ToString(First) : TEXT("-"), *NameOf(LastOp), bRoles ? FormationNames::ToString(Last) : TEXT("-"), *Where(State)));
			Next(State, 30);
			return true;
		}
		case 30:
			return Finish(State, true);
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World, bool bSlideMode)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->bSlideMode = bSlideMode;
		State->bShot = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Shot"), ESearchCase::IgnoreCase); });
		// #57 (user 2026-11-07): by default a peek at the other end of the same wall is the two-corner hold (TwoCornerHoldSmoke) - these runs
		// check the stack moving to the new end, i.e. that rule's A/B "off" path (Codex.Formation.TwoCornerHold 0).
		if (IConsoleVariable* TwoCorners = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Formation.TwoCornerHold")))
		{
			TwoCorners->Set(0, ECVF_SetByCode);
		}
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
		TEXT("CodexTactics.WallLeadershipSmoke"),
		TEXT("Dev check (game designer 2026-11-01, wall leadership): on L_FormationLab the squad at W_B_3 with the Commander mid-wall - a west-corner peek makes the west-end man the leader with the others behind him, then the east corner the east-end man, leaving the wall keeps them, a knocked-down end man is skipped; PASS/FAIL, exits. 'Shot' = stills in Saved/Screenshots/WallLeadership/."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Run(Args, World, false); }));

	static FAutoConsoleCommandWithWorldAndArgs SlideCommand(
		TEXT("CodexTactics.WallLeadershipSlideSmoke"),
		TEXT("Dev check (the game designer's bug 2026-11-03): on L_FormationLab the squad at W_B_3 - the west-corner peek, then the east corner (a 22 m slide along the SAME wall): no follower stands up, the rear stays watched, the stack settles in a reasonable time; PASS/FAIL, exits. 'Shot' = stills in Saved/Screenshots/WallSlide/."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Run(Args, World, true); }));
}

#endif // !UE_BUILD_SHIPPING
