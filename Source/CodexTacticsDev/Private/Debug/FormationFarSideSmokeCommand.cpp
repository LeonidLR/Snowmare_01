// Dev-only headless check of the game designer's play-test 2026-11-01 on L_FormationLab (Saved/BugReport/1.11.2026_bug_02.avi, 3-5.webp) and its
// approved fixes:
//   bug_02: the squad at a wall, the player wants it at the FAR side of the same wall - his clicks land on the wall top, the near face or
//           the operatives ("Operative selected: ..."), the cover ghost snaps to the near face, the squad jumbles / stalls going round;
//   split:  the squad crouched at one wall, ordered to the far side of the OPPOSITE wall - on the run it splits round both ends.
// Fixes: a) the wall side by the cursor (the top's far half / the ground behind = the FAR face, the near half = the near face);
//        b) at a wall a click picks an operative only on his body; d) the far side of the same wall: one column round the near end, then the
//        wall layout; e) no split: one route round an obstacle. (Fix c, the occluder fade, was removed on the game designer's decision
//        2026-11-01 (bug_05): walls never fade - the smoke asserts the clicked wall stays solid and every click is the plain cursor ray.)
//   CodexTactics.FormationFarSideSmoke [Shot]
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands FormationFarSideSmoke [-PreCmds "Codex.Formation.WallRoute 0"]
//   (map /Game/Maps/L_FormationLab + -LevelJson=L_FormationLab.json, Scripts/test_map.json)
//   rendered stills ("Shot"; -RenderOffscreen): Saved/Screenshots/FarSide/01_far_side_ghost.png (the ghost on the far face, the wall solid),
//   02_opposite_wall_ghost.png (the ghost on the opposite wall's far face), 03_same_wall_column.png / 04_opposite_wall_column.png (the
//   squad mid-route round the wall's end, from above).
// The REAL click path: every click is the cursor's ray (a Visibility trace from the player camera through the aimed point, the camera's
// yaw locked looking north) handed to ACodexTacticsPlayerController::HandleWorldHit.
// The lab's W_C_8 (y -1500, x 3000..4500, 40 cm thick, 3 m high) and W_B_3 (y -2700, x 1500..4000); the camera looks north, so their south
// faces are the near faces. Scenes:
//   1. the squad walks up, two clicks on W_C_8's south face -> the wall layout there;
//   2. (b) a click on the wall's top right above each crouched follower (within the old 120 cm select radius) is a cover click, not a
//      selection; a click on a follower's body still selects him (the portrait path selects the leader back);
//   3. (a) the top's near half -> the ghost on the near face; the far half -> the ghost on the FAR face (the wall stays solid: the click
//      is the top hit itself); the ground right behind the wall, where visible -> the far face; (d) the confirm -> the squad goes round one end as one
//      column and settles in the wall layout on the far face - nobody round the other end, no stall;
//   4. (e) the squad crouched at W_B_3's north face, the far-side click on the OPPOSITE wall W_C_8 -> everyone round the SAME end.
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTypes.h"
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/WallSideRules.h"
#include "UnrealClient.h"

namespace FormationFarSideSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** The wall layout counts as settled once everyone is in the wall pose and still this long; the cap of a settle, s. */
	constexpr float SettleHoldSeconds = 1.5f;
	constexpr float SettleCapSeconds = 30.f;
	/** The route round a wall: its cap, s. */
	constexpr float RouteCapSeconds = 45.f;
	/** A member walking (not in the wall pose) slower than this, farther than StallFarCm from where he is sent, stalls, cm/s / cm. */
	constexpr float StallSpeed = 15.f;
	constexpr float StallFarCm = 120.f;
	/** At most this many seconds of stalling per member on a route. */
	constexpr float MaxStallSeconds = 2.f;
	/** A man in the wall layout stands at most this far off the face (the solo cover stand-off 45 + a margin), cm. */
	constexpr float MaxOffFaceCm = 65.f;
	/** Two members closer than this are bunched up (the layout puts them 1 m apart), cm. */
	constexpr float MinSpacingCm = 70.f;
	/** The wall's top: 3 m over the lab floor (formation_lab_01.json). */
	constexpr float WallHeightCm = 300.f;

	/** A lab wall along X: its centre line y, x from .. to, thickness. */
	struct FLabWall
	{
		FString Id;
		double Y = 0.0;
		double X0 = 0.0;
		double X1 = 0.0;
		double Thickness = 40.0;
		double Mid() const { return 0.5 * (X0 + X1); }
		double SouthFace() const { return Y - 0.5 * Thickness; }
		double NorthFace() const { return Y + 0.5 * Thickness; }
	};

	enum class EStage : uint8
	{
		Setup,
		WalkUp,
		NearOrder,
		NearSettle,
		TopPicks,
		BodyPick,
		NearAndFarTop,
		FarConfirm,
		RouteSame,
		OppositeSetup,
		OppositeSettle,
		OppositeClick,
		OppositeConfirm,
		RouteOpposite,
		Done
	};

	/** One member's record on a route. */
	struct FTrack
	{
		TWeakObjectPtr<AOperativeCharacter> Who;
		TOptional<double> CrossX;
		FVector2D Last = FVector2D::ZeroVector;
		bool bHasLast = false;
		float StallSeconds = 0.f;
		bool bRouted = false;
	};

	struct FState
	{
		EStage Stage = EStage::Setup;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		bool bShot = false;
		float GroundZ = 0.f;
		FLabWall WallC;
		FLabWall WallB;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		float StableSince = -1.f;
		int32 RoutesBefore = 0;
		TArray<FTrack> Tracks;
		float MinPairCm = 100000.f;
		float MaxSpreadCm = 0.f;
		TArray<FString> Table;
		// Stills.
		int32 PhotoPhase = 0;
		float PhotoTime = 0.f;
		FString PhotoName;
		bool bPhotoTopDown = false;
		FVector2D PhotoCentre = FVector2D::ZeroVector;
		bool bRoutePhotoTaken = false;
		TWeakObjectPtr<ACameraActor> Camera;
		/** The far-face click's X of the scene in progress (FindSplitX) and the wall's end nearer the leader's way there (-1 west, +1 east). */
		double ClickX = 0.0;
		int32 NearEnd = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		for (const FString& Row : State.Table)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke table: %s"), *Row);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("FormationFarSideSmoke"));
		return false;
	}

	void Enter(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.StableSince = -1.f;
	}

	AOperativeCharacter* Member(const FState& State, int32 Index)
	{
		return State.Members.IsValidIndex(Index) ? State.Members[Index].Get() : nullptr;
	}

	FVector At(const FState& State, double X, double Y, float Up = 0.f)
	{
		return FVector(X, Y, State.GroundZ + Up);
	}

	bool LoadWalls(FState& State)
	{
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data/FormationLab/formation_lab_01.json");
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			return false;
		}
		State.GroundZ = static_cast<float>(Root->GetNumberField(TEXT("ground_z")));
		int32 Found = 0;
		for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("walls")))
		{
			const TSharedPtr<FJsonObject>& Object = Value->AsObject();
			const FString Id = Object->GetStringField(TEXT("id"));
			if (Id != TEXT("W_C_8") && Id != TEXT("W_B_3"))
			{
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>& A = Object->GetArrayField(TEXT("a"));
			const TArray<TSharedPtr<FJsonValue>>& B = Object->GetArrayField(TEXT("b"));
			FLabWall& Wall = Id == TEXT("W_C_8") ? State.WallC : State.WallB;
			Wall.Id = Id;
			Wall.X0 = FMath::Min(A[0]->AsNumber(), B[0]->AsNumber());
			Wall.X1 = FMath::Max(A[0]->AsNumber(), B[0]->AsNumber());
			Wall.Y = A[1]->AsNumber();
			Wall.Thickness = Object->GetNumberField(TEXT("thickness"));
			++Found;
		}
		return Found == 2;
	}

	/** The cursor over Aim: the player camera's ray through it (a Visibility trace, as GetHitResultUnderCursor). */
	bool CursorHit(UWorld* World, const ACodexTacticsPlayerController* PC, const FVector& Aim, FHitResult& OutHit)
	{
		if (!PC || !PC->PlayerCameraManager)
		{
			return false;
		}
		const FVector Camera = PC->PlayerCameraManager->GetCameraLocation();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FarSideCursor), false);
		return World->LineTraceSingleByChannel(OutHit, Camera, Camera + (Aim - Camera) * 1.6, ECC_Visibility, Params);
	}

	/** The click, as OnClick hands the cursor's hit on. */
	void Click(FState& State, UWorld* World, ACodexTacticsPlayerController* PC, const FHitResult& Hit)
	{
		PC->HandleWorldHit(Hit);
	}

	/** A wall piece drawn as before (game designer 2026-11-01, bug_05: walls never fade): visible and blocking the cursor. */
	bool IsSolid(const UPrimitiveComponent* Piece)
	{
		return Piece && Piece->IsVisible() && Piece->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block;
	}

	FString Where(const FVector& Point)
	{
		return FString::Printf(TEXT("(%.0f, %.0f, %.0f)"), Point.X, Point.Y, Point.Z);
	}

	/** Which end of WallC the navmesh path From -> To passes (where it crosses the wall's line): -1 west, +1 east, 0 none. */
	int32 PathEnd(UWorld* World, const FState& State, const FVector& From, const FVector& To)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(World, From, To) : nullptr;
		if (!Path || !Path->IsValid())
		{
			return 0;
		}
		const TArray<FVector>& Points = Path->PathPoints;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const double A = Points[Index - 1].Y - State.WallC.Y;
			const double B = Points[Index].Y - State.WallC.Y;
			if (A * B <= 0.0 && !FMath::IsNearlyEqual(A, B))
			{
				const double X = FMath::Lerp(Points[Index - 1].X, Points[Index].X, A / (A - B));
				return X < State.WallC.Mid() ? -1 : 1;
			}
		}
		return 0;
	}

	/**
	 * The far-face click X along WallC where the OLD rule (each member his own navmesh path to his wall slot: the leader K at X, the others
	 * at X -/+ wall.spacing by travel) sends the squad round BOTH ends - the split of the play-test, reproduced by geometry, not luck.
	 * Default when there is none (the scene then still checks one route).
	 */
	double FindSplitX(UWorld* World, const FState& State, double Default)
	{
		const AOperativeCharacter* Lead = State.Leader.Get();
		const AOperativeCharacter* A = Member(State, 1);
		const AOperativeCharacter* B = Member(State, 2);
		if (!Lead || !A || !B)
		{
			return Default;
		}
		const double FaceY = State.WallC.NorthFace() + 45.0;
		const double Z = Lead->GetActorLocation().Z;
		for (double X = State.WallC.X0 + 400.0; X <= State.WallC.X1 - 400.0; X += 50.0)
		{
			const FVector Left(X - 100.0, FaceY, Z);
			const FVector Right(X + 100.0, FaceY, Z);
			const bool bSwap = FVector::Dist2D(A->GetActorLocation(), Right) + FVector::Dist2D(B->GetActorLocation(), Left)
				< FVector::Dist2D(A->GetActorLocation(), Left) + FVector::Dist2D(B->GetActorLocation(), Right);
			const int32 Ends[3] = { PathEnd(World, State, Lead->GetActorLocation(), FVector(X, FaceY, Z)),
				PathEnd(World, State, A->GetActorLocation(), bSwap ? Right : Left), PathEnd(World, State, B->GetActorLocation(), bSwap ? Left : Right) };
			const bool bWest = Ends[0] < 0 || Ends[1] < 0 || Ends[2] < 0;
			const bool bEast = Ends[0] > 0 || Ends[1] > 0 || Ends[2] > 0;
			if (bWest && bEast)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: the far-face click at x %.0f - each member's own navmesh path to his slot goes round: %s %s, %s %s, %s %s (the old rule splits here)"),
					X, *Lead->DisplayName.ToString(), Ends[0] < 0 ? TEXT("west") : TEXT("east"), *A->DisplayName.ToString(), Ends[1] < 0 ? TEXT("west") : TEXT("east"),
					*B->DisplayName.ToString(), Ends[2] < 0 ? TEXT("west") : TEXT("east"));
				return X;
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: no far-face click splits the old rule from here - the click at x %.0f"), Default);
		return Default;
	}

	bool AllSettled(const FState& State)
	{
		for (int32 Index = 0; Index < State.Members.Num(); ++Index)
		{
			const AOperativeCharacter* Each = Member(State, Index);
			if (!Each || !Each->IsInFormationWallPose() || Each->GetVelocity().Size2D() > 5.f)
			{
				return false;
			}
		}
		return State.Members.Num() > 0;
	}

	/** Waits for AllSettled to hold SettleHoldSeconds; true once judged (bOk = settled before the cap). */
	bool WaitSettled(FState& State, float CapSeconds, bool& bOk)
	{
		if (!AllSettled(State))
		{
			State.StableSince = -1.f;
			if (State.StageTime < CapSeconds)
			{
				return false;
			}
			bOk = false;
			return true;
		}
		if (State.StableSince < 0.f)
		{
			State.StableSince = State.StageTime;
		}
		if (State.StageTime - State.StableSince < SettleHoldSeconds)
		{
			return false;
		}
		bOk = true;
		return true;
	}

	/** The layout at a wall's face: everyone on that side within MaxOffFaceCm, not bunched; logs the places. */
	void JudgeLayout(FState& State, const FString& Name, const FLabWall& Wall, bool bNorthFace, bool bSettled, float Seconds)
	{
		const double Face = bNorthFace ? Wall.NorthFace() : Wall.SouthFace();
		const double Out = bNorthFace ? 1.0 : -1.0;
		float WorstOff = 0.f;
		bool bWrongSide = false;
		float Closest = 100000.f;
		FString Line;
		for (int32 A = 0; A < State.Members.Num(); ++A)
		{
			const AOperativeCharacter* Each = Member(State, A);
			if (!Each)
			{
				continue;
			}
			const FVector Here = Each->GetActorLocation();
			const float Off = static_cast<float>((Here.Y - Face) * Out);
			WorstOff = FMath::Max(WorstOff, Off);
			bWrongSide |= Off <= 0.f || Here.X < Wall.X0 - 50.0 || Here.X > Wall.X1 + 50.0;
			Line += FString::Printf(TEXT(" [%s at (%.0f, %.0f), %.0f cm off the face%s]"), *Each->DisplayName.ToString(), Here.X, Here.Y, Off,
				Each->IsInFormationWallPose() ? TEXT(", wall pose") : TEXT(", NOT in the wall pose"));
			for (int32 B = A + 1; B < State.Members.Num(); ++B)
			{
				if (const AOperativeCharacter* Other = Member(State, B))
				{
					Closest = FMath::Min(Closest, static_cast<float>(FVector::Dist2D(Here, Other->GetActorLocation())));
				}
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s:%s"), *Name, *Line);
		Check(State, bSettled, FString::Printf(TEXT("%s: all three in the wall layout, crouched facing out (%.1f s)"), *Name, Seconds));
		Check(State, !bWrongSide && WorstOff <= MaxOffFaceCm, FString::Printf(TEXT("%s: everyone at the %s face of %s, within %.0f cm of it (worst %.0f)"), *Name,
			bNorthFace ? TEXT("north (far)") : TEXT("south (near)"), *Wall.Id, MaxOffFaceCm, WorstOff));
		Check(State, Closest >= MinSpacingCm, FString::Printf(TEXT("%s: not bunched - the closest pair %.0f cm apart (>= %.0f)"), *Name, Closest, MinSpacingCm));
	}

	/** Teleports the squad (Solo while placed): the leader at Lead, the others either side of him, facing Yaw. */
	void PlaceSquad(UWorld* World, USquadSubsystem* Squad, FState& State, const FVector2D& Lead, float Yaw)
	{
		Squad->EnterSoloMode();
		const FVector2D Side(150.0, 0.0);
		const FVector2D Spots[] = { Lead, Lead - Side + FVector2D(0.0, -150.0), Lead + Side + FVector2D(0.0, -150.0) };
		for (int32 Index = 0; Index < State.Members.Num(); ++Index)
		{
			AOperativeCharacter* Each = Member(State, Index);
			if (!Each)
			{
				continue;
			}
			Each->LeaveCover(TEXT("smoke reset"));
			Each->SetFormationWallPose(false);
			Each->ClearPendingCover();
			Each->StopOperative();
			Each->SetStance(EOperativeStance::Standing);
			const FVector Spot = SmokeUtils::FreeSpot(World, At(State, Spots[FMath::Min(Index, 2)].X, Spots[FMath::Min(Index, 2)].Y, 92.f), Each);
			Each->TeleportTo(Spot + FVector(0.f, 0.f, 4.f), FRotator(0.f, Yaw, 0.f), false, true);
		}
		Squad->SetLeader(State.Leader.Get());
		Squad->ExitSoloMode(false, 0.f);
		Squad->ResetFormationState(FRotator(0.f, Yaw, 0.f).Vector());
	}

	/** Stills: phase 1 = place the camera (top-down) / settle, 2 = the request, 0 = idle. True while a still is in progress. */
	void RequestPhoto(FState& State, const FString& Name, bool bTopDown, const FVector2D& Centre)
	{
		if (!State.bShot || State.PhotoPhase != 0)
		{
			return;
		}
		State.PhotoPhase = 1;
		State.PhotoTime = 0.f;
		State.PhotoName = Name;
		State.bPhotoTopDown = bTopDown;
		State.PhotoCentre = Centre;
	}

	void StepPhoto(UWorld* World, FState& State)
	{
		if (State.PhotoPhase == 0)
		{
			return;
		}
		State.PhotoTime += StepSeconds;
		APlayerController* Player = UGameplayStatics::GetPlayerController(World, 0);
		if (State.PhotoPhase == 1)
		{
			if (State.bPhotoTopDown)
			{
				const FVector Eye = At(State, State.PhotoCentre.X, State.PhotoCentre.Y, 1800.f);
				const FRotator Down(-90.f, 90.f, 0.f);
				if (!State.Camera.IsValid())
				{
					State.Camera = World->SpawnActor<ACameraActor>(Eye, Down);
					if (ACameraActor* Created = State.Camera.Get())
					{
						Created->GetCameraComponent()->SetFieldOfView(50.f);
						Created->GetCameraComponent()->bConstrainAspectRatio = false;
						Created->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
						Created->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
					}
				}
				if (ACameraActor* Cam = State.Camera.Get())
				{
					Cam->SetActorLocationAndRotation(Eye, Down);
					if (Player)
					{
						Player->SetViewTarget(Cam);
					}
				}
			}
			if (Player && Player->GetHUD())
			{
				Player->GetHUD()->bShowHUD = false;
			}
			State.PhotoPhase = 2;
			return;
		}
		if (State.PhotoPhase == 2 && State.PhotoTime >= 0.6f)
		{
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("FarSide") / State.PhotoName;
			FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogCodexTactics, Display, TEXT("FormationFarSide: screenshot %s"), *Path);
			State.PhotoPhase = 3;
			return;
		}
		if (State.PhotoPhase == 3 && State.PhotoTime >= 1.1f)
		{
			if (Player && State.bPhotoTopDown)
			{
				Player->SetViewTarget(Player->GetPawn());
			}
			if (Player && Player->GetHUD())
			{
				Player->GetHUD()->bShowHUD = true;
			}
			State.PhotoPhase = 0;
		}
	}

	void BeginRoute(FState& State, USquadSubsystem* Squad)
	{
		// The near end: the shorter way from the leader round an end (150 cm off it) to his slot on the far face.
		if (const AOperativeCharacter* Lead = State.Leader.Get())
		{
			const FVector2D From(Lead->GetActorLocation());
			const FVector2D Slot(State.ClickX, State.WallC.NorthFace() + 45.0);
			const FVector2D West(State.WallC.X0 - 150.0, State.WallC.Y);
			const FVector2D East(State.WallC.X1 + 150.0, State.WallC.Y);
			State.NearEnd = FVector2D::Distance(From, West) + FVector2D::Distance(West, Slot) <= FVector2D::Distance(From, East) + FVector2D::Distance(East, Slot) ? -1 : 1;
		}
		State.Tracks.Reset();
		State.MinPairCm = 100000.f;
		State.MaxSpreadCm = 0.f;
		State.bRoutePhotoTaken = false;
		State.RoutesBefore = Squad->GetFormationWallRouteCount();
		for (const TWeakObjectPtr<AOperativeCharacter>& Each : State.Members)
		{
			FTrack& Track = State.Tracks.AddDefaulted_GetRef();
			Track.Who = Each;
		}
	}

	/** One frame of the route round WallC to its north face: the crossing end per member, stalls, the column's spread. */
	void TrackRoute(FState& State, USquadSubsystem* Squad, const FString& PhotoName)
	{
		const AOperativeCharacter* Lead = State.Leader.Get();
		TArray<FVector2D> Places;
		for (FTrack& Track : State.Tracks)
		{
			AOperativeCharacter* Who = Track.Who.Get();
			if (!Who)
			{
				continue;
			}
			const FVector2D Here(Who->GetActorLocation());
			Places.Add(Here);
			if (Track.bHasLast && !Track.CrossX.IsSet() && Track.Last.Y < State.WallC.Y && Here.Y >= State.WallC.Y)
			{
				Track.CrossX = 0.5 * (Track.Last.X + Here.X);
			}
			Track.Last = Here;
			Track.bHasLast = true;
			Track.bRouted |= Squad->IsFormationWallRouted(Who);
			// A stall: walking into the layout (not in the wall pose yet), standing, far from where he is sent.
			FVector Sent = Who->GetActorLocation();
			if (Who == Lead)
			{
				Sent = Who->GetLastMoveDestination();
			}
			else
			{
				Squad->GetFormationGivenSlot(Who, Sent);
			}
			if (!Who->IsInFormationWallPose() && Who->GetVelocity().Size2D() < StallSpeed && FVector::Dist2D(Sent, Who->GetActorLocation()) > StallFarCm
				&& Who->GetMoveHoldSeconds() <= 0.f)
			{
				Track.StallSeconds += StepSeconds;
			}
		}
		for (int32 A = 0; A < Places.Num(); ++A)
		{
			for (int32 B = A + 1; B < Places.Num(); ++B)
			{
				State.MinPairCm = FMath::Min(State.MinPairCm, static_cast<float>(FVector2D::Distance(Places[A], Places[B])));
			}
			if (Lead)
			{
				State.MaxSpreadCm = FMath::Max(State.MaxSpreadCm, static_cast<float>(FVector2D::Distance(Places[A], FVector2D(Lead->GetActorLocation()))));
			}
		}
		// The still mid-route: the leader round the end, the others behind him.
		if (Lead && !State.bRoutePhotoTaken && FMath::Abs(Lead->GetActorLocation().Y - State.WallC.Y) < 250.0
			&& (Lead->GetActorLocation().X < State.WallC.X0 + 50.0 || Lead->GetActorLocation().X > State.WallC.X1 - 50.0))
		{
			State.bRoutePhotoTaken = true;
			const double EndX = Lead->GetActorLocation().X < State.WallC.Mid() ? State.WallC.X0 : State.WallC.X1;
			RequestPhoto(State, PhotoName, true, FVector2D(EndX, State.WallC.Y - 200.0));
		}
	}

	void JudgeRoute(FState& State, USquadSubsystem* Squad, const FString& Name, bool bSettled)
	{
		const FTrack* LeadTrack = State.Tracks.FindByPredicate([&State](const FTrack& Track) { return Track.Who == State.Leader; });
		const TOptional<double> LeadX = LeadTrack ? LeadTrack->CrossX : TOptional<double>();
		const auto EndOf = [&State](double X) { return X < State.WallC.Mid() ? TEXT("west") : TEXT("east"); };
		bool bSameEnd = LeadX.IsSet();
		float WorstStall = 0.f;
		int32 Routed = 0;
		FString Line;
		for (const FTrack& Track : State.Tracks)
		{
			const AOperativeCharacter* Who = Track.Who.Get();
			if (!Who)
			{
				continue;
			}
			bSameEnd &= Track.CrossX.IsSet() && LeadX.IsSet() && (Track.CrossX.GetValue() < State.WallC.Mid()) == (LeadX.GetValue() < State.WallC.Mid());
			WorstStall = FMath::Max(WorstStall, Track.StallSeconds);
			Routed += Track.bRouted ? 1 : 0;
			Line += FString::Printf(TEXT(" [%s: round the %s end (crossed at x %.0f), stalled %.1f s%s]"), *Who->DisplayName.ToString(),
				Track.CrossX.IsSet() ? EndOf(Track.CrossX.GetValue()) : TEXT("NO"), Track.CrossX.Get(0.0), Track.StallSeconds,
				Track.bRouted ? TEXT(", walked the leader's route") : TEXT(""));
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s route:%s; closest pair on the way %.0f cm, farthest from the leader %.0f cm, wall routes %d -> %d"),
			*Name, *Line, State.MinPairCm, State.MaxSpreadCm, State.RoutesBefore, Squad->GetFormationWallRouteCount());
		Check(State, bSameEnd, FString::Printf(TEXT("%s: no split - everyone went round the leader's (%s) end of %s"), *Name,
			LeadX.IsSet() ? EndOf(LeadX.GetValue()) : TEXT("?"), *State.WallC.Id));
		Check(State, LeadX.IsSet() && (LeadX.GetValue() < State.WallC.Mid() ? -1 : 1) == State.NearEnd, FString::Printf(TEXT("%s: the leader went round the NEAR end (%s, the shorter way to the far-face spot at x %.0f)"),
			*Name, State.NearEnd < 0 ? TEXT("west") : TEXT("east"), State.ClickX));
		Check(State, WorstStall <= MaxStallSeconds, FString::Printf(TEXT("%s: no stall on the way (worst %.1f s standing short of where he was sent, <= %.1f)"), *Name,
			WorstStall, MaxStallSeconds));
		State.Table.Add(FString::Printf(TEXT("| %s | same end %s | worst stall %.1f s | closest pair %.0f cm | spread %.0f cm | routed %d | settled %s |"), *Name,
			bSameEnd ? TEXT("yes") : TEXT("NO"), WorstStall, State.MinPairCm, State.MaxSpreadCm, Routed, bSettled ? TEXT("yes") : TEXT("NO")));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 500.f)
		{
			Check(State, false, FString::Printf(TEXT("timeout (stage %d)"), static_cast<int32>(State.Stage)));
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Lead = State.Leader.Get();
		StepPhoto(World, State);
		if (State.PhotoPhase != 0 && State.Stage != EStage::RouteSame && State.Stage != EStage::RouteOpposite)
		{
			return true; // a still in progress holds the scene (the routes go on: their still is taken on the run)
		}
		switch (State.Stage)
		{
		case EStage::Setup:
		{
			if (State.StageTime < 3.f || !Squad || !PC || Squad->GetMembers().Num() < 3)
			{
				return State.Time < 60.f || Finish(State, false);
			}
			if (!FormationRuntime::IsEnabled() || !LoadWalls(State))
			{
				Check(State, false, TEXT("Codex.Formation.Sectors is on and the lab layout (W_B_3, W_C_8) is read"));
				return Finish(State, false);
			}
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& X, const AOperativeCharacter& Y) { return X.SquadIndex < Y.SquadIndex; });
			for (int32 Index = 0; Index < 3; ++Index)
			{
				State.Members.Add(Roster[Index]);
			}
			State.Leader = Roster[0];
			// The player's view: the camera looks north (the walls' south faces are the near faces), the hovers are the smoke's own.
			if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(PC->GetPawn()))
			{
				Camera->SetCinematicLock(true);
				Camera->SetCinematicYaw(90.f);
			}
			PC->bFreezeCoverHoverForTesting = true;
			PC->bFreezeCornerPeekHoverForTesting = true;
			PlaceSquad(World, Squad, State, FVector2D(State.WallC.Mid(), -2150.0), 90.f);
			Enter(State, EStage::WalkUp);
			return true;
		}
		case EStage::WalkUp:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			if (State.StageTime < 0.6f)
			{
				Lead->OrderMoveTo(At(State, State.WallC.Mid(), -1800.0), false);
				return true;
			}
			if ((Lead->IsMoving() || State.StageTime < 5.f) && State.StageTime < 20.f)
			{
				return true;
			}
			// Scene 1: two clicks on W_C_8's south face (the near face) -> the wall layout there.
			FHitResult Hit;
			const bool bFace = CursorHit(World, PC, At(State, State.WallC.Mid(), State.WallC.SouthFace() - 1.0, 150.f), Hit)
				&& Hit.ImpactNormal.Y < -0.9 && FMath::Abs(Hit.ImpactPoint.Y - State.WallC.SouthFace()) < 5.0;
			Check(State, bFace, FString::Printf(TEXT("scene 1: the cursor's ray lands on %s's south face (hit %s on %s)"), *State.WallC.Id, *Where(Hit.ImpactPoint),
				*GetNameSafe(Hit.GetActor())));
			if (!bFace)
			{
				return Finish(State, false);
			}
			Click(State, World, PC, Hit);
			Check(State, PC->HasCoverPreview() && PC->GetCoverPreviewSlot().WorldLocation.Y < State.WallC.SouthFace(),
				FString::Printf(TEXT("scene 1: the face click shows the ghost on the near face (%s)"), *Where(PC->GetCoverPreviewSlot().WorldLocation)));
			Enter(State, EStage::NearOrder);
			return true;
		}
		case EStage::NearOrder:
		{
			if (State.StageTime < 0.4f)
			{
				return true;
			}
			FHitResult Hit;
			CursorHit(World, PC, At(State, State.WallC.Mid(), State.WallC.SouthFace() - 1.0, 150.f), Hit);
			Click(State, World, PC, Hit);
			Check(State, Squad->HasFormationWallOrder(), TEXT("scene 1: the confirm -> the group cover order as the formation wall layout"));
			Enter(State, EStage::NearSettle);
			return true;
		}
		case EStage::NearSettle:
		{
			bool bOk = false;
			if (!WaitSettled(State, SettleCapSeconds, bOk))
			{
				return true;
			}
			JudgeLayout(State, TEXT("scene 1 (the near face)"), State.WallC, false, bOk, State.StageTime);
			Enter(State, EStage::TopPicks);
			return true;
		}
		case EStage::TopPicks:
		{
			// Scene 2 (b): the wall's top right above each crouched follower - inside the old 120 cm select radius of his body.
			Check(State, Squad->IsSquadAtWall(), TEXT("scene 2: the squad counts as at a wall (the wall layout stands)"));
			for (int32 Index = 1; Index < State.Members.Num(); ++Index)
			{
				AOperativeCharacter* Follower = Member(State, Index);
				if (!Follower)
				{
					continue;
				}
				FHitResult Hit;
				const FVector Aim = At(State, Follower->GetActorLocation().X, State.WallC.Y - 8.0, WallHeightCm);
				const bool bTop = CursorHit(World, PC, Aim, Hit) && Hit.ImpactNormal.Z > 0.7f && Hit.ImpactPoint.Z > State.GroundZ + 200.f;
				const float Near = static_cast<float>(FVector::Dist2D(Hit.ImpactPoint, Follower->GetActorLocation()));
				Click(State, World, PC, Hit);
				const bool bStill = Squad->GetLeader() == Lead;
				Check(State, bTop && bStill, FString::Printf(TEXT("scene 2: a click on the wall's top %.0f cm from %s (old select radius %.0f) selects nobody - leader still %s"),
					Near, *Follower->DisplayName.ToString(), PC->SelectRadius, Squad->GetLeader() ? *Squad->GetLeader()->DisplayName.ToString() : TEXT("-")));
				Check(State, PC->HasCoverPreview() && PC->GetCoverPreviewSlot().WorldLocation.Y < State.WallC.SouthFace(),
					FString::Printf(TEXT("scene 2: ... it is the cover click on the near face (the top's near half; ghost %s)"),
						PC->HasCoverPreview() ? *Where(PC->GetCoverPreviewSlot().WorldLocation) : TEXT("none")));
				PC->HideCoverPreview();
				if (!bStill)
				{
					Squad->SetLeader(Lead); // the bug: he was selected - the portrait path selects the leader back to go on
				}
				// The face right beside him, above his head: no selection either.
				FHitResult Face;
				CursorHit(World, PC, At(State, Follower->GetActorLocation().X + 60.0, State.WallC.SouthFace() - 1.0, 220.f), Face);
				Click(State, World, PC, Face);
				Check(State, Squad->GetLeader() == Lead && FMath::Abs(Face.ImpactNormal.Y) > 0.9, FString::Printf(TEXT("scene 2: a click on the face beside %s (%.0f cm) selects nobody"),
					*Follower->DisplayName.ToString(), FVector::Dist2D(Face.ImpactPoint, Follower->GetActorLocation())));
				PC->HideCoverPreview();
				if (Squad->GetLeader() != Lead)
				{
					Squad->SetLeader(Lead);
				}
			}
			Enter(State, EStage::BodyPick);
			return true;
		}
		case EStage::BodyPick:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			// A click on a follower's body still selects him; the portrait (SetLeader) takes the leader back.
			AOperativeCharacter* Follower = Member(State, 1);
			FHitResult Hit;
			const bool bBody = Follower && CursorHit(World, PC, Follower->GetActorLocation() + FVector(0.f, 0.f, 10.f), Hit) && Hit.GetActor() == Follower;
			if (bBody)
			{
				Click(State, World, PC, Hit);
				Check(State, Squad->GetLeader() == Follower, FString::Printf(TEXT("scene 2: a click on %s's body selects him"), *Follower->DisplayName.ToString()));
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: scene 2: the cursor's ray to %s's chest hit %s - the body pick is not judged"),
					Follower ? *Follower->DisplayName.ToString() : TEXT("-"), *GetNameSafe(Hit.GetActor()));
			}
			Squad->SetLeader(Lead);
			Enter(State, EStage::NearAndFarTop);
			return true;
		}
		case EStage::NearAndFarTop:
		{
			if (State.StageTime < 2.f)
			{
				return true; // the wall layout passed back with the leader settles again
			}
			const double KX = Lead->GetActorLocation().X;
			// Scene 3 (a): the top's near half -> the near face.
			FHitResult NearTop;
			CursorHit(World, PC, At(State, KX, State.WallC.Y - 10.0, WallHeightCm), NearTop);
			UPrimitiveComponent* WallPiece = NearTop.GetComponent();
			FCoverSlot Slot;
			EWallClickSide Side = EWallClickSide::None;
			const bool bNear = PC->ResolveWallSideSlot(NearTop, Slot, Side) && Side == EWallClickSide::Near && Slot.WorldLocation.Y < State.WallC.SouthFace();
			Click(State, World, PC, NearTop);
			Check(State, bNear && PC->HasCoverPreview() && PC->GetCoverPreviewSlot().WorldLocation.Y < State.WallC.SouthFace(),
				FString::Printf(TEXT("scene 3: the top's near half (hit %s) -> the ghost on the near face (%s)"), *Where(NearTop.ImpactPoint),
					PC->HasCoverPreview() ? *Where(PC->GetCoverPreviewSlot().WorldLocation) : TEXT("none")));
			PC->HideCoverPreview();
			Check(State, IsSolid(WallPiece), TEXT("scene 3: the wall stays solid (no fade)"));
			// The top's far half -> the far face (the click is the top hit itself: the wall stays solid, nothing fades). Where: the spot along
			// the far face from which the old rule (each man his own path to his slot) would split the squad round both ends.
			State.ClickX = FindSplitX(World, State, KX);
			FHitResult FarTop;
			CursorHit(World, PC, At(State, State.ClickX, State.WallC.Y + 10.0, WallHeightCm), FarTop);
			const bool bFar = PC->ResolveWallSideSlot(FarTop, Slot, Side) && Side == EWallClickSide::Far && Slot.WorldLocation.Y > State.WallC.NorthFace();
			Check(State, bFar, FString::Printf(TEXT("scene 3: the top's far half (hit %s) means the FAR face (slot %s)"), *Where(FarTop.ImpactPoint), *Where(Slot.WorldLocation)));
			Check(State, FarTop.GetComponent() == WallPiece && IsSolid(WallPiece), FString::Printf(TEXT("scene 3: the far half's ray stops on the same wall's top (%s) - solid, no fade"),
				*GetNameSafe(WallPiece ? WallPiece->GetOwner() : nullptr)));
			Click(State, World, PC, FarTop);
			Check(State, PC->HasCoverPreview() && PC->GetCoverPreviewSlot().WorldLocation.Y > State.WallC.NorthFace() && PC->GetCoverPreviewSlot().WallNormal.Y > 0.9,
				FString::Printf(TEXT("scene 3: the click -> the ghost on the FAR face (%s)"), PC->HasCoverPreview() ? *Where(PC->GetCoverPreviewSlot().WorldLocation) : TEXT("none")));
			// The ground right behind the wall (the first visible ground past its shadow) means the far face too.
			bool bGroundTried = false;
			for (double Back = 300.0; Back <= 900.0; Back += 20.0)
			{
				FHitResult Ground;
				const FVector Aim = At(State, KX + 120.0, State.WallC.NorthFace() + Back, 0.f);
				if (!CursorHit(World, PC, Aim, Ground) || Ground.ImpactNormal.Z < 0.7f || FVector::Dist2D(Ground.ImpactPoint, Aim) > 30.0 || Ground.ImpactPoint.Z > State.GroundZ + 20.f)
				{
					continue; // hidden by the wall: the next one
				}
				bGroundTried = true;
				FCoverSlot GroundSlot;
				EWallClickSide GroundSide = EWallClickSide::None;
				const bool bGround = PC->ResolveWallSideSlot(Ground, GroundSlot, GroundSide) && GroundSide == EWallClickSide::Far
					&& GroundSlot.WorldLocation.Y > State.WallC.NorthFace();
				Check(State, bGround, FString::Printf(TEXT("scene 3: the first visible ground behind the wall, %.0f cm past its far face, means the FAR face (slot %s)"), Back,
					*Where(GroundSlot.WorldLocation)));
				break;
			}
			if (!bGroundTried)
			{
				// Accepted (game designer 2026-11-01, bug_05): with no fade the wall may hide all the ground right behind it - the top's far half
				// is then the way to the far face.
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: scene 3: no ground right behind the wall is visible from this camera (the wall hides it) - the far top half is the far-side click"));
			}
			// The ghost of the far-half click: the still of the ghost on the far face, the wall solid.
			RequestPhoto(State, TEXT("01_far_side_ghost.png"), false, FVector2D::ZeroVector);
			Enter(State, EStage::FarConfirm);
			return true;
		}
		case EStage::FarConfirm:
		{
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			FHitResult FarTop;
			CursorHit(World, PC, At(State, State.ClickX, State.WallC.Y + 10.0, WallHeightCm), FarTop);
			Click(State, World, PC, FarTop);
			Check(State, Squad->HasFormationWallOrder() && !PC->HasCoverPreview(), TEXT("scene 3 (d): the second click confirms - the wall layout ordered on the FAR face"));
			BeginRoute(State, Squad);
			Enter(State, EStage::RouteSame);
			return true;
		}
		case EStage::RouteSame:
		case EStage::RouteOpposite:
		{
			const bool bSame = State.Stage == EStage::RouteSame;
			TrackRoute(State, Squad, bSame ? TEXT("03_same_wall_column.png") : TEXT("04_opposite_wall_column.png"));
			bool bOk = false;
			if (!WaitSettled(State, RouteCapSeconds, bOk) || State.StageTime < 2.f || State.PhotoPhase != 0)
			{
				return true;
			}
			const FString Name = bSame ? TEXT("scene 3 (d: the far side of the same wall)") : TEXT("scene 4 (e: the far side of the opposite wall)");
			JudgeRoute(State, Squad, Name, bOk);
			JudgeLayout(State, Name, State.WallC, true, bOk, State.StageTime);
			Enter(State, bSame ? EStage::OppositeSetup : EStage::Done);
			if (!bSame)
			{
				return Finish(State, true);
			}
			return true;
		}
		case EStage::OppositeSetup:
		{
			// Scene 4 (e): the squad crouched at W_B_3's north face (its far face from this camera: the order is given straight).
			PlaceSquad(World, Squad, State, FVector2D(State.WallC.Mid(), -2450.0), -90.f);
			FCoverSlot Slot;
			const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, At(State, State.WallC.Mid(), State.WallB.NorthFace() + 1.0, 120.f), FVector(0.f, -1.f, 0.f), Slot);
			const TArray<AOperativeCharacter*> Others = Squad->GetGroupCoverMembers(Lead);
			const bool bOrdered = bSlot && Squad->OrderFormationWall(Lead, Others, Slot);
			Check(State, bOrdered, FString::Printf(TEXT("scene 4: the squad ordered along %s's north face (%d others)"), *State.WallB.Id, Others.Num()));
			if (!bOrdered)
			{
				return Finish(State, false);
			}
			Enter(State, EStage::OppositeSettle);
			return true;
		}
		case EStage::OppositeSettle:
		{
			bool bOk = false;
			if (!WaitSettled(State, SettleCapSeconds, bOk))
			{
				return true;
			}
			Check(State, bOk, FString::Printf(TEXT("scene 4: crouched at %s's north face (%.1f s)"), *State.WallB.Id, State.StageTime));
			Enter(State, EStage::OppositeClick);
			return true;
		}
		case EStage::OppositeClick:
		{
			if (State.StageTime < 1.f)
			{
				return true; // the camera follows the leader to the new wall
			}
			// The far half of the OPPOSITE wall's top, 11 m away: clicked -> the ghost on its far face (the wall stays solid).
			FHitResult FarTop;
			State.ClickX = FindSplitX(World, State, State.WallC.Mid());
			const bool bTop = CursorHit(World, PC, At(State, State.ClickX, State.WallC.Y + 10.0, WallHeightCm), FarTop) && FarTop.ImpactNormal.Z > 0.7f;
			UPrimitiveComponent* WallPiece = FarTop.GetComponent();
			Check(State, bTop && IsSolid(WallPiece), FString::Printf(TEXT("scene 4: the far half of %s's top under the cursor (hit %s) - the wall stays solid"), *State.WallC.Id,
				*Where(FarTop.ImpactPoint)));
			Click(State, World, PC, FarTop);
			Check(State, PC->HasCoverPreview() && PC->GetCoverPreviewSlot().WorldLocation.Y > State.WallC.NorthFace(),
				FString::Printf(TEXT("scene 4: the ghost on %s's FAR face (%s)"), *State.WallC.Id, PC->HasCoverPreview() ? *Where(PC->GetCoverPreviewSlot().WorldLocation) : TEXT("none")));
			RequestPhoto(State, TEXT("02_opposite_wall_ghost.png"), false, FVector2D::ZeroVector);
			Enter(State, EStage::OppositeConfirm);
			return true;
		}
		case EStage::OppositeConfirm:
		{
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			FHitResult FarTop;
			CursorHit(World, PC, At(State, State.ClickX, State.WallC.Y + 10.0, WallHeightCm), FarTop);
			Click(State, World, PC, FarTop);
			Check(State, Squad->HasFormationWallOrder() && !PC->HasCoverPreview(), TEXT("scene 4 (e): the confirm - the wall layout ordered on the opposite wall's far face"));
			BeginRoute(State, Squad);
			Enter(State, EStage::RouteOpposite);
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
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Shot"), ESearchCase::IgnoreCase); });
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FormationFarSideSmoke"),
		TEXT("Dev check on L_FormationLab (game designer play-test 2026-11-01): at a wall a top click selects nobody, the top's halves / the ground behind decide the wall side, a wall hiding the cursor's target fades (the cursor looks through it), the far side of the same / the opposite wall as one column round one end; logs PASS/FAIL, then exits. Shot = rendered stills in Saved/Screenshots/FarSide/."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
