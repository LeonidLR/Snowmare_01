// Dev-only check of the cover switch between two BUILDINGS IN A ROW on the lake map (user bug 2026-10-21: "I still can't dash from the edge
// of one building to the edge of the other - to the wall facing the SAME way as mine, on the other building"):
//   Scripts/smoke_parallel.ps1 -Commands CoverSwitchLakeSmoke      (map from Scripts/test_map.json: L_Demo01_A_FrozenLake)
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_Demo01_A_FrozenLake -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -ExecCmds="CodexTactics.CoverSwitchLakeSmoke Shot"  -> Saved/Screenshots/cover_switch_lake_preview.png (hover ghost + dash path),
//     _dash.png, _arrival.png, _settled.png
// The pair: the A3 gap rocks Rock_A3_GapNorth / Rock_A3_GapSouth (3 x 6 m, 2.2 m high, their west faces in ONE plane, a 3 m gap) - the
// geometry of the user's screenshot. Root cause found with it: two faces in one plane counted as ONE wall (CoverTraceRules::IsSameWall:
// normal + plane only), so the click on the other building was a shimmy "along his wall" and the switch never offered; now a face past an
// exposed edge is another wall. The tutorial is skipped (its locks), the enemies parked; nothing is saved.
//  1 the Commander in cover at the north rock's west face, its south end;
//  2 hovering the south rock's west face near its north edge and in its middle: the ghost on that SAME-facing face at its north corner,
//    mirrored (he faces right at his corner -> left there), the dash path;
//  3 the double-click through HandleWorldHit: the switch, a sprint, the sprint run-in, in cover there at the corner, facing left.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Tactics/CoverGhostActor.h"
#include "Tactics/CoverSwitchRules.h"
#include "Tactics/CoverTraceRules.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UnrealClient.h"

namespace CoverSwitchLakeSmoke
{
	constexpr float StepSeconds = 0.05f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShot = false;
		TWeakObjectPtr<AStaticMeshActor> North;
		TWeakObjectPtr<AStaticMeshActor> South;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		/** The rocks' shared west face plane (X), their ends (Y), the ground. */
		float FaceX = 0.f;
		float NorthSouthEndY = 0.f;
		float SouthNorthEndY = 0.f;
		float SouthMidY = 0.f;
		float GroundZ = 0.f;
		FHitResult NearEdgeHit;
		FVector GhostAt = FVector::ZeroVector;
		ECoverFacing GhostFacing = ECoverFacing::Right;
		ECoverFacing FacingBefore = ECoverFacing::Right;
		float MaxDashSpeed = 0.f;
		bool bDashShot = false;
		bool bArrivalShot = false;
		float EntrySeenAt = -1.f;
		int32 SwitchesBefore = 0;
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
		FPlatformMisc::RequestExit(false, TEXT("CoverSwitchLakeSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	void PlaceCamera(UWorld* World, FState& State, const FVector& Eye, const FVector& LookAt, bool bHud)
	{
		const FRotator Look = (LookAt - Eye).Rotation();
		if (!State.Camera.IsValid())
		{
			State.Camera = World->SpawnActor<ACameraActor>(Eye, Look);
			if (ACameraActor* Created = State.Camera.Get())
			{
				Created->GetCameraComponent()->SetFieldOfView(55.f);
				Created->GetCameraComponent()->bConstrainAspectRatio = false;
			}
		}
		if (ACameraActor* Cam = State.Camera.Get())
		{
			Cam->SetActorLocationAndRotation(Eye, Look);
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				PC->SetViewTarget(Cam);
				if (AHUD* Hud = PC->GetHUD())
				{
					Hud->bShowHUD = bHud;
				}
			}
		}
	}

	void RequestShot(const TCHAR* Name)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / FString::Printf(TEXT("%s.png"), Name);
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogCodexTactics, Display, TEXT("CoverSwitchLake: screenshot %s"), *Path);
	}

	bool Settled(const AOperativeCharacter* Op)
	{
		return Op && Op->bInCover && !Op->bShimmying && !Op->IsMoving() && Op->GetVelocity().Size2D() < 5.f && Op->GetCoverEntryMeshOffset().Size() < 1.f;
	}

	/** A hit on the south rock's west face at Y (a horizontal trace from the west at chest height). */
	FHitResult SouthFaceHit(UWorld* World, const FState& State, float Y)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverSwitchLakeHit), false);
		for (const TWeakObjectPtr<AOperativeCharacter>& Each : State.Members)
		{
			Params.AddIgnoredActor(Each.Get());
		}
		FHitResult Hit;
		const FVector Start(State.FaceX - 300.f, Y, State.GroundZ + 120.f);
		World->LineTraceSingleByChannel(Hit, Start, Start + FVector(500.f, 0.f, 0.f), ECC_Visibility, Params);
		return Hit;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 240.f)
		{
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Op = State.Members.IsValidIndex(0) ? State.Members[0].Get() : nullptr;
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 4.f || !Squad || !PC || Squad->GetMembers().Num() < 3)
			{
				return true;
			}
			for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
			{
				const FString Name = It->GetActorNameOrLabel();
				if (Name == TEXT("Rock_A3_GapNorth"))
				{
					State.North = *It;
				}
				else if (Name == TEXT("Rock_A3_GapSouth"))
				{
					State.South = *It;
				}
			}
			Check(State, State.North.IsValid() && State.South.IsValid(), TEXT("the A3 gap rocks (two buildings in a row) are on the map"));
			if (!State.North.IsValid() || !State.South.IsValid())
			{
				return Finish(State, false);
			}
			FVector NorthOrigin;
			FVector NorthExtent;
			FVector SouthOrigin;
			FVector SouthExtent;
			State.North->GetActorBounds(false, NorthOrigin, NorthExtent);
			State.South->GetActorBounds(false, SouthOrigin, SouthExtent);
			State.FaceX = static_cast<float>(NorthOrigin.X - NorthExtent.X);
			State.NorthSouthEndY = static_cast<float>(NorthOrigin.Y - NorthExtent.Y);
			State.SouthNorthEndY = static_cast<float>(SouthOrigin.Y + SouthExtent.Y);
			State.SouthMidY = static_cast<float>(SouthOrigin.Y);
			State.GroundZ = static_cast<float>(NorthOrigin.Z - NorthExtent.Z);
			UE_LOG(LogCodexTactics, Display, TEXT("CoverSwitchLake: west faces X %.0f / %.0f (the same plane), gap Y %.0f .. %.0f"), State.FaceX,
				SouthOrigin.X - SouthExtent.X, State.SouthNorthEndY, State.NorthSouthEndY);
			if (UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>())
			{
				Tutorial->SkipTutorial(); // its command locks / confinements are not under test here
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				It->SetActorTickEnabled(false);
			}
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& X, const AOperativeCharacter& Y) { return X.SquadIndex < Y.SquadIndex; });
			for (int32 Index = 0; Index < 3; ++Index)
			{
				State.Members.Add(Roster[Index]);
				const FVector At = Index == 0 ? FVector(State.FaceX - 250.f, State.NorthSouthEndY + 80.f, State.GroundZ)
					: FVector(State.FaceX - 700.f, State.NorthSouthEndY + 300.f + Index * 150.f, State.GroundZ);
				Roster[Index]->LeaveCover(TEXT("smoke reset"));
				Roster[Index]->StopOperative();
				Roster[Index]->TeleportTo(At + FVector(0.f, 0.f, Roster[Index]->GetSimpleCollisionHalfHeight() + 5.f), FRotator(0.f, 0.f, 0.f), false, true);
			}
			Squad->EnterSoloMode();
			Squad->SetLeader(Roster[0]);
			Next(State, 1);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			// In cover at the north rock's west face, 50 cm from its south end.
			FCoverSlot Slot;
			const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, FVector(State.FaceX, State.NorthSouthEndY + 50.f, State.GroundZ + 90.f), FVector(1.f, 0.f, 0.f), Slot)
				&& Slot.WallActor.Get() == State.North.Get();
			Check(State, bSlot && Slot.Height == ECoverHeight::HighCover, TEXT("1: a high slot at the north rock's west face near its south end"));
			if (!bSlot)
			{
				return Finish(State, false);
			}
			Op->OrderTakeCover(Slot, false);
			Next(State, 2);
			return true;
		}
		case 2:
		{
			if ((!Settled(Op) || State.StageTime < 1.5f) && State.StageTime < 15.f)
			{
				return true;
			}
			State.FacingBefore = Op->CoverFacing;
			Check(State, Op->bInCover && Op->GetCoverSlot().WallActor.Get() == State.North.Get() && Op->bAtCoverCorner,
				FString::Printf(TEXT("1: in cover at the north rock's south end, at the corner, facing %s"), Op->CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right")));
			// The hover over the south rock's SAME-facing (west) face: near its north edge and in its middle.
			for (const float Y : { State.SouthNorthEndY - 40.f, State.SouthMidY })
			{
				const FHitResult Hit = SouthFaceHit(World, State, Y);
				FCoverSlot Probe;
				const bool bProbe = PC->FindCoverSlotForHit(Hit, Probe);
				const bool bHover = Hit.GetActor() == State.South.Get() && PC->UpdateCoverSwitchHover(Op, &Hit) && PC->IsCoverSwitchHoverShown();
				const ACoverGhostActor* Ghost = PC->GetCoverSwitchGhost();
				const FVector At = Ghost ? Ghost->GetSlot().WorldLocation : FVector::ZeroVector;
				const bool bWest = Ghost && Ghost->GetSlot().WallNormal.X < -0.9;
				Check(State, bHover && bWest && FMath::Abs(At.Y - State.SouthNorthEndY) < 90.f && Ghost->GetShownFacing() == CoverSwitchRules::MirrorFacing(State.FacingBefore),
					FString::Printf(TEXT("2: hovering the south rock's west face at Y %.0f -> the ghost on that same-facing face at its north corner (%.0f cm in, west %d), mirrored [same wall %d, verdict %s]"),
						Y, State.SouthNorthEndY - At.Y, bWest ? 1 : 0, bProbe && CoverTraceRules::IsSameWall(Op->GetCoverSlot(), Probe) ? 1 : 0,
						bProbe ? CoverSwitchRules::VerdictName(Op->EvaluateCoverSwitch(Probe)) : TEXT("no slot")));
				if (Y > State.SouthMidY)
				{
					State.NearEdgeHit = Hit;
					State.GhostAt = At;
					State.GhostFacing = Ghost ? Ghost->GetShownFacing() : ECoverFacing::Right;
				}
			}
			TArray<FVector> Path;
			Check(State, PC->GetCoverPreviewPath(Path) && Path.Num() >= 2, TEXT("2: ... and the dash path"));
			// Back to the near-edge hover for the still (the HUD draws the path).
			PC->UpdateCoverSwitchHover(Op, &State.NearEdgeHit);
			if (State.bShot)
			{
				PC->bFreezeCoverHoverForTesting = true;
				PlaceCamera(World, State, FVector(State.FaceX - 900.f, State.NorthSouthEndY + 700.f, State.GroundZ + 850.f),
					FVector(State.FaceX - 60.f, (State.NorthSouthEndY + State.SouthNorthEndY) * 0.5f, State.GroundZ), true);
				Next(State, 3);
				return true;
			}
			Next(State, 4);
			return true;
		}
		case 3: // Shot mode: the hover still
		{
			if (State.StageTime >= 0.4f && State.StageTime < 0.45f)
			{
				RequestShot(TEXT("cover_switch_lake_preview"));
			}
			if (State.StageTime >= 0.8f)
			{
				PC->bFreezeCoverHoverForTesting = false;
				PlaceCamera(World, State, FVector(State.FaceX - 450.f, State.NorthSouthEndY + 350.f, State.GroundZ + 320.f),
					FVector(State.FaceX, (State.NorthSouthEndY + State.SouthNorthEndY) * 0.5f - 60.f, State.GroundZ + 80.f), false);
				Next(State, 4);
			}
			return true;
		}
		case 4:
		{
			PC->UpdateCoverSwitchHover(nullptr, nullptr);
			State.SwitchesBefore = Op->GetCoverSwitchCount();
			PC->HandleWorldHit(State.NearEdgeHit); // the double-click through the click dispatcher
			PC->HandleWorldHit(State.NearEdgeHit);
			Check(State, PC->WasLastCoverConfirmSwitch() && Op->HasPendingCoverSwitch() && FVector::Dist2D(Op->GetPendingCoverSlot().WorldLocation, State.GhostAt) < 5.f,
				FString::Printf(TEXT("3: the double-click is the switch to the ghost's slot (%.0f cm off it; verdict %s)"),
					FVector::Dist2D(Op->GetPendingCoverSlot().WorldLocation, State.GhostAt), CoverSwitchRules::VerdictName(PC->GetLastCoverSwitchVerdict())));
			Next(State, 5);
			return true;
		}
		case 5:
		{
			if (!Op->bInCover)
			{
				State.MaxDashSpeed = FMath::Max(State.MaxDashSpeed, static_cast<float>(Op->GetVelocity().Size2D()));
				if (State.bShot && !State.bDashShot && Op->GetVelocity().Size2D() > 450.f)
				{
					RequestShot(TEXT("cover_switch_lake_dash"));
					State.bDashShot = true;
				}
			}
			if (State.bShot && !State.bArrivalShot && Op->IsCoverSwitchEntry())
			{
				State.EntrySeenAt = State.EntrySeenAt < 0.f ? State.StageTime : State.EntrySeenAt;
				if (State.StageTime - State.EntrySeenAt >= 0.2f)
				{
					RequestShot(TEXT("cover_switch_lake_arrival"));
					State.bArrivalShot = true;
				}
			}
			const bool bDone = Settled(Op) && State.StageTime > 0.5f && Op->GetCoverSlot().WallActor.Get() == State.South.Get();
			if (!bDone && State.StageTime < 15.f)
			{
				return true;
			}
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			const FString Clip = Anim ? Anim->GetLastCoverSwitchClip() : FString();
			const FCoverSlot& Arrived = Op->GetCoverSlot();
			Check(State, bDone, FString::Printf(TEXT("3: in cover at the south rock after %.1f s"), State.StageTime));
			Check(State, State.MaxDashSpeed >= 450.f && Op->GetCoverSwitchCount() > State.SwitchesBefore && Clip.Contains(TEXT("_sprint_")),
				FString::Printf(TEXT("3: a quick dash (top speed %.0f cm/s) arriving through the sprint run-in %s"), State.MaxDashSpeed, *Clip));
			Check(State, Arrived.WallNormal.X < -0.9 && FVector::Dist2D(Arrived.WorldLocation, State.GhostAt) < 10.f && Op->CoverFacing == State.GhostFacing
				&& Op->bAtCoverCorner,
				FString::Printf(TEXT("3: on the SAME-facing (west) face at its north corner, where the ghost stood (%.0f cm off), facing %s (mirrored), at the corner"),
					FVector::Dist2D(Arrived.WorldLocation, State.GhostAt), Op->CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right")));
			if (State.bShot)
			{
				RequestShot(TEXT("cover_switch_lake_settled"));
			}
			Next(State, 6);
			return true;
		}
		case 6:
			return State.StageTime < 0.5f ? true : Finish(State, true);
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
		TEXT("CodexTactics.CoverSwitchLakeSmoke"),
		TEXT("Dev check on L_Demo01_A_FrozenLake (2026-10-21): the cover switch between two buildings in a row (the A3 gap rocks, their faces in one plane) - hover ghost on the other rock's same-facing face at its corner, mirrored; double-click = sprint dash there. Arg 'Shot': stills."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
