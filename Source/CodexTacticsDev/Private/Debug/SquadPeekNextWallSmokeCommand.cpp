// Dev-only headless check of the squad's corner peek moving on to the NEXT wall (the game designer's play-test 2026-11-01 on
// L_FormationLabCombat, bug_04 A, Saved/BugReport/2026-11-01_bug_04_peek_next_wall_camera/6-8.webp: the squad peeking round the end of one
// of two parallel walls, the double-click on the OTHER wall's end - the squad only walked to that wall, the old peeker stayed in his corner,
// the other two crouched in a broken shape, no peek):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands SquadPeekNextWallSmoke
//   (map /Game/Maps/L_FormationLab, "-LevelJson=L_FormationLab.json" - Scripts/test_map.json smokeOptions)
// The lab's parallel walls W_D_9 (x -3500..-1000 at y 550) and W_D_10 (the same at y 1050), 40 cm thick, the corridor between them. The
// player's path: two clicks on W_D_9's south face (the group cover order = the formation wall layout), the hover + click on its END (the
// squad's corner peek; the peek held), then the two clicks on W_D_10's south face next to ITS end (the cover ghost "corner to fire from", its
// confirm) - every click inside the world tick (OnWorldPreActorTick) as the player's. Checks: the old peeker leaves his corner at once and
// walks with the squad, nobody stays at W_D_9, the carried peek is ordered once, a member peeks round W_D_10's end in the peek pose (held 3 s)
// with the others at W_D_10 behind him along its face. Variant 1 = the west ends (the designer's case, the slot 80 cm from the end), 2 = the
// east ends (the slot 40 cm from it: the leader at the end peeks himself).
// A/B: -PreCmds "Codex.Formation.PeekCarry 0" = no carried peek (the peek checks fail; the release still holds).
// "Shot" (rendered: UnrealEditor.exe <uproject> /Game/Maps/L_FormationLab -game -RenderOffscreen -ResX=1280 -ResY=720 -UseFixedTimeStep -FPS=60
// "-LevelJson=L_FormationLab.json" "-ExecCmds=CodexTactics.SquadPeekNextWallSmoke Shot") saves stills to Saved/Screenshots/Bug04/.
// Godot reference: none (UE-only; docs/systems/squad-and-orders.md "The squad's corner peek moves on to the next wall").

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/World.h"
#include "Formation/FormationRuntimeRules.h"
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace SquadPeekNextWallSmoke
{
	/** W_D_9 / W_D_10 (Content/Data/FormationLab/formation_lab_01.json): x -3500..-1000, their south faces at y 530 / 1030. */
	constexpr float WallWestX = -3500.f;
	constexpr float WallEastX = -1000.f;
	constexpr float FirstFaceY = 530.f;
	constexpr float NextFaceY = 1030.f;
	/** A man at a wall stands ~45 cm off its face. */
	constexpr float SlotOffCm = 45.f;
	constexpr float StartY = -200.f;
	constexpr float HoldSeconds = 3.f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		int32 Variant = 0;
		bool bShot = false;
		float GroundZ = 0.f;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TWeakObjectPtr<AOperativeCharacter> OldPeeker;
		int32 ReleasesBefore = 0;
		int32 CarriesBefore = 0;
		bool bOldPeekerOut = false;
		float OldPeekerOutAt = -1.f;
		float HoldTime = 0.f;
		int32 ShotIndex = 0;
		float NextShotTime = 0.f;
		TWeakObjectPtr<ACameraActor> Camera;
		/** The click due inside the next world tick (0 none, 1 the end of W_D_9 - hover + click, 2/3 the next wall's two clicks). */
		int32 PendingClick = 0;
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
		FPlatformMisc::RequestExit(false, TEXT("SquadPeekNextWallSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	bool IsWest(const FState& State)
	{
		return State.Variant == 0;
	}

	float EndX(const FState& State)
	{
		return IsWest(State) ? WallWestX : WallEastX;
	}

	/** Along the walls from the variant's end inwards (+X at the west end). */
	float Inward(const FState& State)
	{
		return IsWest(State) ? 1.f : -1.f;
	}

	FString Label(const FState& State)
	{
		return FString::Printf(TEXT("variant %d (%s ends)"), State.Variant + 1, IsWest(State) ? TEXT("west") : TEXT("east"));
	}

	AOperativeCharacter* Member(const FState& State, int32 Index)
	{
		return State.Members.IsValidIndex(Index) ? State.Members[Index].Get() : nullptr;
	}

	/** The cursor hit on a wall's south face at X (a real trace from the south: the wall's own component, as the cursor gives it). */
	bool FaceHit(UWorld* World, const FState& State, float FaceY, float X, FHitResult& OutHit)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SquadPeekNextWallSmokeFace), false);
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			Params.AddIgnoredActor(Weak.Get());
		}
		const float Z = State.GroundZ + 120.f;
		return World->LineTraceSingleByChannel(OutHit, FVector(X, FaceY - 280.f, Z), FVector(X, FaceY + 15.f, Z), ECC_Visibility, Params)
			&& FMath::Abs(OutHit.ImpactPoint.Y - FaceY) <= 15.f;
	}

	/** The squad of three south of W_D_9, facing it, in group mode (the Commander leading). */
	bool PlaceSquad(FState& State, USquadSubsystem* Squad)
	{
		Squad->ExitSoloMode(false, 0.f);
		AOperativeCharacter* Lead = Member(State, 0);
		if (!Lead)
		{
			return false;
		}
		Squad->SetLeader(Lead);
		const float CentreX = IsWest(State) ? -3000.f : -1500.f;
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
			const FVector At(CentreX + (Index == 0 ? 0.f : (Index == 1 ? -150.f : 150.f)), StartY - (Index == 0 ? 0.f : 200.f),
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

	FString Where(const FState& State)
	{
		FString Line;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			if (const AOperativeCharacter* Op = Weak.Get())
			{
				Line += FString::Printf(TEXT(" %s (%.0f, %.0f)%s%s;"), *Op->DisplayName.ToString(), Op->GetActorLocation().X, Op->GetActorLocation().Y,
					Op->bInCover ? TEXT(" in cover") : TEXT(""), Op->IsManualCornerPeek() ? TEXT(" peeking") : TEXT(""));
			}
		}
		return Line;
	}

	/** The world-tick clicks: the player's clicks land in the controller's PlayerTick, before the squad updates. */
	void WorldTickClick(UWorld* World, FState& State)
	{
		const int32 Click = State.PendingClick;
		State.PendingClick = 0;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !PC || !Leader)
		{
			return;
		}
		FHitResult Hit;
		if (Click == 1)
		{
			// The hover on W_D_9's end (20 cm in from it), then the click there: the squad's corner peek.
			if (!FaceHit(World, State, FirstFaceY, EndX(State) + Inward(State) * 20.f, Hit))
			{
				Check(State, false, FString::Printf(TEXT("%s: the cursor hit on W_D_9's end"), *Label(State)));
				return;
			}
			PC->UpdateCornerPeekHover(Leader, &Hit);
			PC->HandleWorldHit(Hit);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s: the click on W_D_9's end at (%.0f, %.0f) -> peeker %s"), *Label(State), Hit.ImpactPoint.X, Hit.ImpactPoint.Y,
				Squad->GetFormationPeeker() ? *Squad->GetFormationPeeker()->DisplayName.ToString() : TEXT("-"));
			return;
		}
		// The next wall: its south face next to its end (west: 80 cm in, the designer's click; east: 40 cm in).
		const float X = EndX(State) + Inward(State) * (IsWest(State) ? 80.f : 40.f);
		if (!FaceHit(World, State, NextFaceY, X, Hit))
		{
			Check(State, false, FString::Printf(TEXT("%s: the cursor hit on W_D_10's face at x %.0f"), *Label(State), X));
			return;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s: click %d on W_D_10's face at (%.0f, %.0f); squad:%s"), *Label(State), Click - 1, Hit.ImpactPoint.X, Hit.ImpactPoint.Y,
			*Where(State));
		PC->HandleWorldHit(Hit);
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

	/** Shot mode: stills of the corridor end - the squad on its way (every 1 s), then the held peek from two angles. */
	void TakeStill(UWorld* World, FState& State, const TCHAR* Tag, int32 Angle)
	{
		const FVector Corner(EndX(State), NextFaceY - 200.f, State.GroundZ + 80.f);
		const float In = Inward(State);
		const FVector Eye = Angle == 0 ? Corner + FVector(In * 900.f, -1100.f, 900.f) : Corner + FVector(-In * 700.f, -500.f, 450.f);
		PlaceCamera(World, State, Eye, Corner + FVector(In * 150.f, 0.f, 0.f));
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Bug04") / FString::Printf(TEXT("peek_next_wall_v%d_%s_%02d.png"), State.Variant + 1,
			Tag, State.ShotIndex++);
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogCodexTactics, Display, TEXT("SquadPeekNextWallSmoke: still %s"), *Path);
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float DeltaTime)
	{
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 400.f)
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
			FCollisionQueryParams Params(SCENE_QUERY_STAT(SquadPeekNextWallSmokeGround), false);
			for (AOperativeCharacter* Each : Roster)
			{
				Params.AddIgnoredActor(Each);
			}
			State.GroundZ = World->LineTraceSingleByChannel(Ground, FVector(-2300.f, StartY, 2000.f), FVector(-2300.f, StartY, -2000.f), ECC_Visibility, Params)
				? static_cast<float>(Ground.ImpactPoint.Z) : 0.f;
			FHitResult Probe;
			const bool bWalls = FaceHit(World, State, FirstFaceY, -2300.f, Probe) && FaceHit(World, State, NextFaceY, -2300.f, Probe)
				&& FaceHit(World, State, FirstFaceY, WallWestX + 20.f, Probe) && FaceHit(World, State, NextFaceY, WallEastX - 20.f, Probe);
			Check(State, bWalls, FString::Printf(TEXT("the lab's parallel walls W_D_9 / W_D_10 stand where the layout puts them (south faces y %.0f / %.0f; ground z %.0f)"),
				FirstFaceY, NextFaceY, State.GroundZ));
			if (!bWalls || !PlaceSquad(State, Squad))
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
		switch (State.Stage)
		{
		case 1:
		case 2:
		{
			// The two clicks on W_D_9's south face, 3.5 m in from the end: the cover ghost, then its confirm (the formation wall layout).
			if (State.StageTime < (State.Stage == 1 ? 3.f : 1.f))
			{
				return true;
			}
			FHitResult Hit;
			if (FaceHit(World, State, FirstFaceY, EndX(State) + Inward(State) * 350.f, Hit))
			{
				PC->HandleWorldHit(Hit);
			}
			if (State.Stage == 2)
			{
				Check(State, Squad->HasFormationWallOrder(), FString::Printf(TEXT("%s: the clicks on W_D_9 - the group cover order as the formation wall layout"), *Label(State)));
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
			Check(State, bSettled, FString::Printf(TEXT("%s: the squad hugs W_D_9 in the wall layout (%.1f s)"), *Label(State), State.StageTime));
			State.PendingClick = 1;
			Next(State, 4);
			return true;
		}
		case 4:
		{
			// The first peek: held in the peek pose.
			AOperativeCharacter* Peeker = Squad->GetFormationPeeker();
			State.HoldTime = Peeker && InPeekPose(*Peeker) ? State.HoldTime + DeltaTime : 0.f;
			if (State.HoldTime < 2.f && State.StageTime < 30.f)
			{
				return true;
			}
			Check(State, Peeker && State.HoldTime >= 2.f && FMath::Abs(Peeker->GetActorLocation().X - EndX(State)) <= 130.f,
				FString::Printf(TEXT("%s: the first peek round W_D_9's end holds (%s)"), *Label(State), *Where(State)));
			State.OldPeeker = Peeker;
			State.ReleasesBefore = Squad->GetFormationWallReleaseCount();
			State.CarriesBefore = PC->GetSquadPeekCarryCount();
			State.bOldPeekerOut = false;
			State.OldPeekerOutAt = -1.f;
			State.HoldTime = 0.f;
			State.PendingClick = 2;
			Next(State, 5);
			return true;
		}
		case 5:
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			State.PendingClick = 3; // the confirm (the designer's double-click)
			Next(State, 6);
			return true;
		case 6:
		{
			if (State.PendingClick != 0)
			{
				return true;
			}
			if (State.StageTime < 0.1f)
			{
				return true;
			}
			Check(State, Squad->HasFormationWallOrder(), FString::Printf(TEXT("%s: the double-click on W_D_10's end - the group wall order there"), *Label(State)));
			Check(State, PC->IsSquadPeekCarryPending(), FString::Printf(TEXT("%s: the squad's corner peek is carried over to W_D_10's corner"), *Label(State)));
			Check(State, Squad->GetFormationWallReleaseCount() > State.ReleasesBefore || (State.OldPeeker.IsValid() && State.OldPeeker.Get() == Squad->GetLeader()),
				FString::Printf(TEXT("%s: the wall order took the old peeker out of his corner (releases %d -> %d)"), *Label(State), State.ReleasesBefore,
					Squad->GetFormationWallReleaseCount()));
			State.ShotIndex = 0;
			State.NextShotTime = 0.f;
			Next(State, 7);
			return true;
		}
		case 7:
		{
			// The squad on its way: the old peeker out of his corner, then the carried peek at W_D_10's end, held.
			AOperativeCharacter* Old = State.OldPeeker.Get();
			if (Old && !State.bOldPeekerOut && !Old->bInCover && Old->GetVelocity().Size2D() > 50.f)
			{
				State.bOldPeekerOut = true;
				State.OldPeekerOutAt = State.StageTime;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s: the old peeker %s walks with the squad %.2f s after the order"), *Label(State), *Old->DisplayName.ToString(), State.StageTime);
			}
			if (State.bShot && State.StageTime >= State.NextShotTime && State.ShotIndex < 8)
			{
				TakeStill(World, State, TEXT("move"), 0);
				State.NextShotTime += 1.f;
			}
			AOperativeCharacter* Peeker = Squad->GetFormationPeeker();
			const bool bAtNext = Peeker && FMath::Abs(Peeker->GetActorLocation().X - EndX(State)) <= 130.f
				&& FMath::Abs(Peeker->GetActorLocation().Y - (NextFaceY - SlotOffCm)) <= 80.f;
			State.HoldTime = bAtNext && InPeekPose(*Peeker) ? State.HoldTime + DeltaTime : 0.f;
			// ... and the others settled at their stack places (crouched in the wall pose, still).
			bool bOthersSettled = true;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				const AOperativeCharacter* Op = Weak.Get();
				bOthersSettled &= !Op || Op == Peeker || (Op->IsInFormationWallPose() && Op->GetVelocity().Size2D() < 5.f);
			}
			if ((State.HoldTime < HoldSeconds || !bOthersSettled) && State.StageTime < 50.f)
			{
				return true;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s: %.1f s after the order:%s"), *Label(State), State.StageTime, *Where(State));
			Check(State, State.bOldPeekerOut && State.OldPeekerOutAt <= 3.f, FString::Printf(TEXT("%s: the old peeker %s left his corner and walked with the squad (%.2f s after the order)"),
				*Label(State), Old ? *Old->DisplayName.ToString() : TEXT("-"), State.OldPeekerOutAt));
			Check(State, PC->GetSquadPeekCarryCount() == State.CarriesBefore + 1, FString::Printf(TEXT("%s: the carried peek was ordered once (%d -> %d)"), *Label(State),
				State.CarriesBefore, PC->GetSquadPeekCarryCount()));
			Check(State, State.HoldTime >= HoldSeconds, FString::Printf(TEXT("%s: %s peeks round W_D_10's %s end in the peek pose (held %.1f s)"), *Label(State),
				Peeker ? *Peeker->DisplayName.ToString() : TEXT("nobody"), IsWest(State) ? TEXT("west") : TEXT("east"), State.HoldTime));
			// The others at W_D_10 behind him along its face (between the walls, on its south side, within 4 m of the end); nobody at W_D_9.
			bool bStack = Peeker != nullptr;
			FString Stack;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				const AOperativeCharacter* Op = Weak.Get();
				if (!Op || Op == Peeker)
				{
					continue;
				}
				const FVector At = Op->GetActorLocation();
				const float Along = (At.X - EndX(State)) * Inward(State);
				const bool bHere = At.Y > FirstFaceY + 100.f && At.Y < NextFaceY && FMath::Abs(At.Y - (NextFaceY - SlotOffCm)) <= 40.f && Along >= 60.f && Along <= 450.f
					&& Op->IsInFormationWallPose();
				bStack &= bHere;
				Stack += FString::Printf(TEXT(" %s %.0f cm along, %.0f cm off the face%s;"), *Op->DisplayName.ToString(), Along, NextFaceY - At.Y, bHere ? TEXT("") : TEXT(" OFF"));
			}
			Check(State, bStack, FString::Printf(TEXT("%s: the others stand at W_D_10 behind the peeker along its face:%s"), *Label(State), *Stack));
			if (State.bShot)
			{
				State.ShotIndex = 0;
				Next(State, 8);
				return true;
			}
			Next(State, 9);
			return true;
		}
		case 8:
			// Shot mode: the held peek from two angles.
			if (State.StageTime >= 0.4f * static_cast<float>(State.ShotIndex + 1) && State.ShotIndex < 2)
			{
				TakeStill(World, State, TEXT("peek"), State.ShotIndex);
			}
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			if (APlayerController* Player = UGameplayStatics::GetPlayerController(World, 0))
			{
				Player->SetViewTarget(Player->GetPawn());
			}
			Next(State, 9);
			return true;
		case 9:
			if (State.Variant == 0)
			{
				++State.Variant;
				if (!PlaceSquad(State, Squad))
				{
					return Finish(State, false);
				}
				Next(State, 1);
				return true;
			}
			return Finish(State, true);
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
		State->PreTickHandle = FWorldDelegates::OnWorldPreActorTick.AddLambda([WeakWorld, State](UWorld* Ticking, ELevelTick, float)
		{
			if (State->PendingClick != 0 && Ticking == WeakWorld.Get())
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
		TEXT("CodexTactics.SquadPeekNextWallSmoke"),
		TEXT("Dev check (bug_04 A, 2026-11-01): on L_FormationLab the squad peeks round W_D_9's end, the double-click on W_D_10's end - the old peeker leaves his corner, the peek moves on to W_D_10's corner with the others behind; west and east ends; PASS/FAIL, exits. 'Shot' = stills in Saved/Screenshots/Bug04/."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Run(Args, World); }));
}

#endif // !UE_BUILD_SHIPPING
