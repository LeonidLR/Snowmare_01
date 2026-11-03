// Dev-only headless check of the BOX-SELECTED GROUP'S CORNER PEEK (the game designer's bug 2026-11-03 on L_Gen_Depot_1: solo mode, all three
// box-selected in cover along a wall, a corner peek ordered at its left / right end - the MIDDLE man, the selection's leader, side-stepped
// slowly to the corner, round the end man off the wall; in formation mode the man nearest that end takes it, the others stack behind him):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands GroupCornerPeekSmoke
//   (map /Game/Maps/L_FormationLab, "-LevelJson=L_FormationLab.json" - Scripts/test_map.json smokeOptions)
// The lab wall W_B_3 (x 1500..4000 at y -2700, its south face y -2720), the squad of three south of it. Solo mode, all three box-selected (the
// Commander in the middle stays the selection's leader); the two clicks on the face 4 m from the west end (the group cover order: each in a
// cover slot along the wall, shoulder to shoulder); then the hover + click on the WEST end -> the west-end man takes the corner and peeks,
// the others side-step along the wall into a stack behind him (nobody steps off the wall, nobody passes him, the selection's leader does not
// peek), settled at a normal pace; then the EAST end from that stack -> the east-end man, the others behind him.
// A/B: -PreCmds "Codex.Squad.GroupCornerPeek 0" = the old rule (the selection's leader alone peeks: the checks fail).
// "Shot" (rendered: UnrealEditor.exe <uproject> /Game/Maps/L_FormationLab -game -RenderOffscreen -ResX=1280 -ResY=720 -UseFixedTimeStep -FPS=60
// "-LevelJson=L_FormationLab.json" "-ExecCmds=CodexTactics.GroupCornerPeekSmoke Shot") saves stills of each stack to Saved/Screenshots/WallSlide/.
// Godot reference: none (UE-only; docs/systems/squad-and-orders.md "The squad at walls").

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
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace GroupCornerPeekSmoke
{
	/** W_B_3 (Content/Data/FormationLab/formation_lab_01.json): x 1500..4000, its south face at y -2720. */
	constexpr float WallWestX = 1500.f;
	constexpr float WallEastX = 4000.f;
	constexpr float WallFaceY = -2720.f;
	/** A man in cover stands ~45 cm off the face. */
	constexpr float SlotOffCm = 45.f;
	/** "Off the wall": a body centre farther than this from the cover line (the slot line ~45 cm off the face), cm. */
	constexpr float OffWallCm = 45.f;
	constexpr float StartY = -3300.f;
	/** The group cover click: 4 m from the west end (the selection's leader there, the others either side of him). */
	constexpr float CoverX = 1900.f;
	constexpr float HoldSeconds = 1.5f;
	/**
	 * A normal pace: the stack settled within the longest walk at this average pace plus a margin (the cover side-step's crouch pace is
	 * 125 cm/s; the old rule's middle man round the end man made ~70 cm/s), cm/s, s.
	 */
	constexpr float NormalPaceCmPerSecond = 95.f;
	constexpr float PaceMarginSeconds = 2.5f;
	/** The stage's give-up time after the click, s. */
	constexpr float WestGiveUpSeconds = 15.f;
	constexpr float EastGiveUpSeconds = 40.f;
	/** Nobody passes the peeker: every other body centre at least this far behind him along the wall, cm. */
	constexpr float BehindPeekerMinCm = 40.f;

	enum class EClick : uint8
	{
		None,
		Face,
		WestEnd,
		EastEnd
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
		TWeakObjectPtr<AOperativeCharacter> SelectionLeader;
		TWeakObjectPtr<AOperativeCharacter> Expected;
		/** Over the walk: the farthest any body got off the cover line, the closest any other came to the peeker along the wall. */
		float MaxOffWallCm = 0.f;
		FString MaxOffWallWho;
		float MinBehindCm = TNumericLimits<float>::Max();
		FString MinBehindWho;
		bool bOtherPeeked = false;
		FString OtherPeekedWho;
		float HoldTime = 0.f;
		/** Each member's path walked since the end click, cm. */
		TArray<float> PathCm;
		TArray<FVector> LastPos;
		int32 ShotIndex = 0;
		TWeakObjectPtr<ACameraActor> Camera;
		EClick PendingClick = EClick::None;
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
		FPlatformMisc::RequestExit(false, TEXT("GroupCornerPeekSmoke"));
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
				Line += FString::Printf(TEXT(" %s (%.0f, %.0f)%s%s%s stance %d;"), *NameOf(Op), Op->GetActorLocation().X, Op->GetActorLocation().Y, Op->bInCover ? TEXT(" in cover") : TEXT(""),
					Op->IsManualCornerPeek() ? TEXT(" peeking") : TEXT(""), Op->bShimmying ? TEXT(" shimmying") : TEXT(""), static_cast<int32>(Op->GetStance()));
			}
		}
		return Line;
	}

	bool FaceHit(UWorld* World, const FState& State, float X, FHitResult& OutHit)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GroupCornerPeekSmokeFace), false);
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			Params.AddIgnoredActor(Weak.Get());
		}
		const float Z = State.GroundZ + 120.f;
		return World->LineTraceSingleByChannel(OutHit, FVector(X, WallFaceY - 300.f, Z), FVector(X, WallFaceY + 15.f, Z), ECC_Visibility, Params)
			&& FMath::Abs(OutHit.ImpactPoint.Y - WallFaceY) <= 15.f;
	}

	bool InPeekPose(const AOperativeCharacter& Op)
	{
		return Op.bInCover && Op.IsManualCornerPeek() && Op.IsCornerAimActive() && Op.bAtCoverCorner && !Op.bShimmying;
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

	/** The player's click inside the world tick (as the controller's PlayerTick lands it): the hover first, then the click. */
	void WorldTickClick(UWorld* World, FState& State)
	{
		const EClick Click = State.PendingClick;
		State.PendingClick = EClick::None;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !PC || !Leader)
		{
			return;
		}
		const float X = Click == EClick::Face ? CoverX : Click == EClick::WestEnd ? WallWestX + 20.f : WallEastX - 20.f;
		FHitResult Hit;
		if (!FaceHit(World, State, X, Hit))
		{
			Check(State, false, FString::Printf(TEXT("the cursor hit on W_B_3's face at x %.0f"), X));
			return;
		}
		const bool bGhost = Click != EClick::Face && PC->UpdateCornerPeekHover(Leader, &Hit);
		PC->HandleWorldHit(Hit);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the click at (%.0f, %.0f) (peek ghost %d) - selection leader %s, %d selected, solo %d;%s"), Hit.ImpactPoint.X, Hit.ImpactPoint.Y,
			bGhost ? 1 : 0, *NameOf(Squad->GetLeader()), Squad->GetSelectedGroup().Num(), Squad->IsSoloMode() ? 1 : 0, *Where(State));
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

	void ShootStill(FState& State, const TCHAR* Tag)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("WallSlide") / FString::Printf(TEXT("group_corner_peek_%s_%02d.png"), Tag, State.ShotIndex++);
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogCodexTactics, Display, TEXT("GroupCornerPeekSmoke: still %s"), *Path);
	}

	/** One frame of the walk into the stack: on the wall, nobody past the peeker, nobody else peeking. */
	void SampleWalk(FState& State, const AOperativeCharacter* Peeker, bool bWest)
	{
		const float Toward = bWest ? -1.f : 1.f;
		for (int32 Index = 0; Index < State.Members.Num(); ++Index)
		{
			const AOperativeCharacter* Op = State.Members[Index].Get();
			if (!Op)
			{
				continue;
			}
			if (State.PathCm.IsValidIndex(Index) && State.LastPos.IsValidIndex(Index))
			{
				State.PathCm[Index] += static_cast<float>(FVector::Dist2D(Op->GetActorLocation(), State.LastPos[Index]));
				State.LastPos[Index] = Op->GetActorLocation();
			}
			const float Off = FMath::Abs(static_cast<float>(Op->GetActorLocation().Y) - (WallFaceY - SlotOffCm));
			if (Off > State.MaxOffWallCm)
			{
				State.MaxOffWallCm = Off;
				State.MaxOffWallWho = NameOf(Op);
			}
			if (Peeker && Op != Peeker)
			{
				const float Behind = static_cast<float>(Peeker->GetActorLocation().X - Op->GetActorLocation().X) * Toward;
				if (Behind < State.MinBehindCm)
				{
					State.MinBehindCm = Behind;
					State.MinBehindWho = NameOf(Op);
				}
				if (Op->IsManualCornerPeek())
				{
					State.bOtherPeeked = true;
					State.OtherPeekedWho = NameOf(Op);
				}
			}
		}
	}

	/** A new end click: the walk's measurements start over. */
	void ResetWalk(FState& State)
	{
		State.MaxOffWallCm = 0.f;
		State.MaxOffWallWho.Reset();
		State.MinBehindCm = TNumericLimits<float>::Max();
		State.MinBehindWho.Reset();
		State.bOtherPeeked = false;
		State.OtherPeekedWho.Reset();
		State.HoldTime = 0.f;
		State.PathCm.Init(0.f, State.Members.Num());
		State.LastPos.Reset();
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			State.LastPos.Add(Weak.IsValid() ? Weak->GetActorLocation() : FVector::ZeroVector);
		}
	}

	bool Settled(const FState& State, const AOperativeCharacter* Peeker, bool bWest)
	{
		if (!Peeker || !InPeekPose(*Peeker) || FMath::Abs(Peeker->GetActorLocation().X - (bWest ? WallWestX : WallEastX)) > 130.f)
		{
			return false;
		}
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			const AOperativeCharacter* Op = Weak.Get();
			if (Op && Op != Peeker && (!Op->bInCover || Op->bShimmying || Op->GetVelocity().Size2D() > 5.f))
			{
				return false;
			}
		}
		return true;
	}

	void CheckStack(FState& State, const TCHAR* Label, const AOperativeCharacter* Peeker, bool bWest)
	{
		const float Toward = bWest ? -1.f : 1.f;
		const float SettledAt = State.StageTime - State.HoldTime;
		float LongestCm = 0.f;
		FString Paths;
		for (int32 Index = 0; Index < State.Members.Num() && Index < State.PathCm.Num(); ++Index)
		{
			LongestCm = FMath::Max(LongestCm, State.PathCm[Index]);
			Paths += FString::Printf(TEXT(" %s %.0f cm;"), *NameOf(State.Members[Index].Get()), State.PathCm[Index]);
		}
		const float Limit = LongestCm / NormalPaceCmPerSecond + PaceMarginSeconds;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke metric: %s: the stack settled %.2f s after the click (limit %.1f s for the longest walk %.0f cm), max %.0f cm off the cover line (%s), closest %.0f cm behind the peeker (%s); walked:%s"),
			Label, SettledAt, Limit, LongestCm, State.MaxOffWallCm, *State.MaxOffWallWho, State.MinBehindCm, *State.MinBehindWho, *Paths);
		Check(State, Peeker == State.Expected.Get(), FString::Printf(TEXT("%s: the man nearest that end (%s) takes the corner (peeker %s; the selection's leader %s)"), Label,
			*NameOf(State.Expected.Get()), *NameOf(Peeker), *NameOf(State.SelectionLeader.Get())));
		Check(State, State.HoldTime >= HoldSeconds && Peeker && InPeekPose(*Peeker), FString::Printf(TEXT("%s: %s peeks round the %s corner in the peek pose (held %.1f s)"), Label,
			*NameOf(Peeker), bWest ? TEXT("west") : TEXT("east"), State.HoldTime));
		Check(State, SettledAt <= Limit, FString::Printf(TEXT("%s: a normal pace - the stack settled %.2f s after the click (<= %.1f s: the longest walk %.0f cm at %.0f cm/s + %.1f s)"), Label,
			SettledAt, Limit, LongestCm, NormalPaceCmPerSecond, PaceMarginSeconds));
		Check(State, State.MaxOffWallCm <= OffWallCm, FString::Printf(TEXT("%s: nobody stepped off the wall (max %.0f cm off the cover line: %s; <= %.0f)"), Label, State.MaxOffWallCm,
			*State.MaxOffWallWho, OffWallCm));
		Check(State, State.MinBehindCm >= BehindPeekerMinCm, FString::Printf(TEXT("%s: nobody passed the peeker (closest %.0f cm behind him: %s)"), Label, State.MinBehindCm,
			*State.MinBehindWho));
		Check(State, !State.bOtherPeeked, FString::Printf(TEXT("%s: only he peeks (%s)"), Label, State.bOtherPeeked ? *State.OtherPeekedWho : TEXT("nobody else")));
		bool bBehind = Peeker != nullptr;
		FString Line;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			const AOperativeCharacter* Op = Weak.Get();
			if (!Op || Op == Peeker || !Peeker)
			{
				continue;
			}
			const float Behind = static_cast<float>(Peeker->GetActorLocation().X - Op->GetActorLocation().X) * Toward;
			const bool bHere = Op->bInCover && Behind >= 60.f && Behind <= 320.f;
			bBehind &= bHere;
			Line += FString::Printf(TEXT(" %s %.0f cm behind%s;"), *NameOf(Op), Behind, bHere ? TEXT("") : TEXT(" OFF"));
		}
		Check(State, bBehind, FString::Printf(TEXT("%s: the others in cover in a stack behind him along the wall:%s"), Label, *Line));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float DeltaTime)
	{
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 240.f)
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
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& X, const AOperativeCharacter& Y) { return X.SquadIndex < Y.SquadIndex; });
			FCollisionQueryParams Params(SCENE_QUERY_STAT(GroupCornerPeekSmokeStart), false);
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
			State.GroundZ = World->LineTraceSingleByChannel(Ground, FVector(CoverX, StartY, 2000.f), FVector(CoverX, StartY, -2000.f), ECC_Visibility, Params)
				? static_cast<float>(Ground.ImpactPoint.Z) : 0.f;
			FHitResult Probe;
			const bool bWall = FaceHit(World, State, CoverX, Probe) && FaceHit(World, State, WallWestX + 20.f, Probe) && FaceHit(World, State, WallEastX - 20.f, Probe);
			Check(State, bWall, FString::Printf(TEXT("the lab wall W_B_3 stands where the layout puts it (south face y %.0f, x %.0f..%.0f)"), WallFaceY, WallWestX, WallEastX));
			if (!bWall)
			{
				return Finish(State, false);
			}
			// The squad south of the wall: the Commander in the middle (the selection's leader), the others 1.5 m either side.
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
				const FVector At(CoverX + (Index == 0 ? 0.f : (Index == 1 ? -150.f : 150.f)), StartY - (Index == 0 ? 0.f : 150.f),
					State.GroundZ + Each->GetSimpleCollisionHalfHeight() + 5.f);
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
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			// Solo mode, all three box-selected (the Commander stays the selection's leader).
			Squad->EnterSoloMode();
			TArray<AOperativeCharacter*> All;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				All.Add(Weak.Get());
			}
			Squad->SetSelectedGroup(All);
			State.SelectionLeader = Squad->GetLeader();
			Check(State, Squad->IsSoloMode() && Squad->HasMultiSelection() && Squad->GetSelectedGroup().Num() == 3,
				FString::Printf(TEXT("solo mode, all three box-selected (leader %s)"), *NameOf(Squad->GetLeader())));
			Next(State, 2);
			return true;
		}
		case 2:
		case 3:
		{
			// The two clicks on the face (the cover ghost, then its confirm): the box selection's group cover order.
			if (State.PendingClick != EClick::None || State.StageTime < 1.f)
			{
				return true;
			}
			State.PendingClick = EClick::Face;
			Next(State, State.Stage + 1);
			return true;
		}
		case 4:
		{
			if (State.PendingClick != EClick::None)
			{
				return true;
			}
			bool bInCover = true;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				bInCover &= Weak.IsValid() && Weak->bInCover && !Weak->bShimmying && !Weak->HasPendingCover() && Weak->GetVelocity().Size2D() < 5.f;
			}
			if ((!bInCover || State.StageTime < 2.f) && State.StageTime < 20.f)
			{
				return true;
			}
			Check(State, bInCover, FString::Printf(TEXT("the box selection in cover along W_B_3 (%.1f s):%s"), State.StageTime, *Where(State)));
			const TArray<AOperativeCharacter*> Sorted = AlongWall(State);
			if (!bInCover || Sorted.Num() != 3)
			{
				return Finish(State, false);
			}
			Check(State, Sorted[1] == State.SelectionLeader.Get(), FString::Printf(TEXT("the selection's leader %s stands in the middle (west %s, east %s)"),
				*NameOf(State.SelectionLeader.Get()), *NameOf(Sorted[0]), *NameOf(Sorted[2])));
			State.Expected = Sorted[0];
			ResetWalk(State);
			State.PendingClick = EClick::WestEnd;
			Next(State, 5);
			return true;
		}
		case 5:
		case 6:
		{
			if (State.PendingClick != EClick::None)
			{
				return true;
			}
			const bool bWest = State.Stage == 5;
			const TCHAR* Label = bWest ? TEXT("west end") : TEXT("east end (from the west stack)");
			AOperativeCharacter* Peeker = nullptr;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				if (Weak.IsValid() && Weak->IsManualCornerPeek() && (!Peeker || Weak.Get() == State.Expected.Get()))
				{
					Peeker = Weak.Get();
				}
			}
			if (State.HoldTime <= 0.f)
			{
				SampleWalk(State, Peeker, bWest);
			}
			State.HoldTime = Settled(State, Peeker, bWest) ? State.HoldTime + DeltaTime : 0.f;
			if (FMath::FloorToInt32(State.StageTime / 0.5f) != FMath::FloorToInt32((State.StageTime - DeltaTime) / 0.5f))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke trace %.1f s (%s):%s"), State.StageTime, Label, *Where(State));
			}
			if (State.HoldTime < HoldSeconds && State.StageTime < (bWest ? WestGiveUpSeconds : EastGiveUpSeconds))
			{
				return true;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s: %.1f s after the click:%s"), Label, State.StageTime, *Where(State));
			CheckStack(State, Label, Peeker, bWest);
			if (State.bShot)
			{
				const FVector Corner(bWest ? WallWestX : WallEastX, WallFaceY - 60.f, State.GroundZ + 80.f);
				const float In = bWest ? 1.f : -1.f;
				PlaceCamera(World, State, Corner + FVector(In * 450.f, -900.f, 750.f), Corner + FVector(In * 200.f, 0.f, 0.f));
			}
			Next(State, State.Stage + 100);
			return true;
		}
		case 105:
		case 106:
		{
			if (State.bShot && State.StageTime < 0.6f)
			{
				return true;
			}
			if (State.bShot)
			{
				ShootStill(State, State.Stage == 105 ? TEXT("west") : TEXT("east"));
			}
			if (State.Stage == 106)
			{
				Next(State, 30);
				return true;
			}
			const TArray<AOperativeCharacter*> Sorted = AlongWall(State);
			State.Expected = Sorted.Num() == 3 ? Sorted.Last() : nullptr;
			ResetWalk(State);
			State.PendingClick = EClick::EastEnd;
			Next(State, 6);
			return true;
		}
		case 30:
			if (State.bShot && State.StageTime < 0.5f)
			{
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
		TEXT("CodexTactics.GroupCornerPeekSmoke"),
		TEXT("Dev check (the game designer's bug 2026-11-03): on L_FormationLab, solo mode, all three box-selected in cover along W_B_3 - a corner peek at the west end makes the west-end man peek with the others side-stepping into a stack behind him (not the selection's leader in the middle), then the east end the east-end man; nobody off the wall, a normal pace; PASS/FAIL, exits. 'Shot' = stills in Saved/Screenshots/WallSlide/."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Run(Args, World); }));
}

#endif // !UE_BUILD_SHIPPING
