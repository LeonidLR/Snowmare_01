// Dev-only console command: R17-3, the unified building click (designer request 2026-11-17, his decisions the same evening; ClickPreviewRules,
// ACodexTacticsPlayerController::HandlePlayerClick / TickClickPreview) on L_Gen_Depot_1:
//   Scripts/smoke_parallel.ps1 -Commands ClickPreviewSmoke -ExtraArgs -Multiprocess
// Every click takes the REAL input path: the cursor's ray from the player's camera (ECC_Visibility) through a point of the wall / obstacle, the hit
// handed to HandlePlayerClick (OnClickReleased's entry); the hovers go through PlayerTick's own hover block (bSimulateCursorForTesting: the
// headless game has no cursor). Cases (Solo unless said):
//   a  hover over a building wall's end and its middle          -> NO ghost (control: Codex.Click.Preview 0 shows the old hover ghost there);
//   b  first click on the wall's face                           -> the cover ghost, no order;
//   c  the cursor along the wall to its end                     -> the ghost slides along the wall, at the corner it turns into the peek;
//   d  the second click there                                   -> he runs to that corner and peeks round it;
//   e  first click on the wall, then a click on the ground away -> the ghost goes, the click is the ordinary move;
//   f  first click on the wall, a slow second click on it       -> he takes cover at the ghost's slot;
//   g  first click on a climbable obstacle's top               -> the climb ghost, no climb; a slow second click -> he walks there and climbs;
//   h  a double click on another obstacle's top                 -> he RUNS there and climbs at once;
//   i  formation: first click on the wall                       -> the ghost shows EVERY member (each follower's silhouette at the wall);
//                 the second click                              -> the squad goes to the wall;
//   j  the sweep "it works on some buildings only": every visible ground-floor wall face of the map, the cursor at 1 / 2.5 / 4 m up its middle
//      and near its ends -> a building meaning (cover / peek) everywhere; the misses are listed with why (A/B: Codex.Click.LowerHighHits 0);
//      a high hit's ghost stands on his floor (raw vs lowered hit counted); the pieces hit in front of a face mean something too; >= 95 % of
//      the exposed ends are the peek (an end at a doorway is the breach). Diagnostics: -ClickSweepOnly (case j alone), -ClickSweepAll (every
//      tall static mesh of the map, not only the generator's tagged walls) - via smoke_parallel -ExtraArgs.
// Enemies are parked far away.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/MantleSmokeSites.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/MantleNavigation.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NavigationSystem.h"
#include "Tactics/CoverGhostActor.h"
#include "Tactics/CoverTraceRules.h"

namespace ClickPreviewSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float StandOffCm = 400.f;
	constexpr float CameraSettleSeconds = 1.5f;
	constexpr float SlideStepCm = 20.f;
	constexpr int32 MaxSweepFaces = 36;

	/** One face of a straight wall piece (the level generator's wall cubes). */
	struct FFace
	{
		TWeakObjectPtr<AActor> Actor;
		FName Building = NAME_None;
		FVector Base = FVector::ZeroVector; // the face's middle at its foot
		FVector Normal = FVector::ForwardVector;
		FVector Tangent = FVector::RightVector;
		float HalfLength = 0.f;
		float Height = 0.f;
		FVector Stand = FVector::ZeroVector;

		FVector Point(float Along, float Up) const { return Base + Tangent * Along + FVector(0.f, 0.f, Up) + Normal * 1.f; }
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		SmokeUtils::FPreloadGate PreloadGate;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TArray<FFace> Faces;
		int32 FaceIndex = 0;
		FFace Face;
		/** The primary face's exposed end: +1 / -1 along its tangent. */
		float EndSign = 1.f;
		float SlideAlong = 0.f;
		float FirstGhostAlong = 0.f;
		float LastGhostAlong = 0.f;
		bool bSawPeek = false;
		FVector GhostSlot = FVector::ZeroVector;
		FVector MoveTarget = FVector::ZeroVector;
		int32 MantlesBefore = 0;
		bool bSawSprint = false;
		int32 MantleCase = 0;
		MantleSmokeSites::FSite Site;
		TSet<const AActor*> UsedSites;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Followers;
		// The sweep.
		int32 SweepFace = 0;
		int32 SweepTried = 0;
		int32 SweepHits = 0;
		int32 SweepMisses = 0;
		int32 SweepMissesNoLower = 0;
		int32 SweepOccluded = 0;
		/** Exposed ends the cursor reached and how many of them showed the corner peek (the cover ghost turns into the peek there). */
		int32 SweepEnds = 0;
		int32 SweepEndPeeks = 0;
		/** Cursor hits on another wall-like piece in front of the face (what the player would click there) and how many had no meaning. */
		/** High cursor hits (>= 2 m up the face) and how many give a cover slot off his floor (raw hit / lowered hit, R17-3 LowerBuildingHit). */
		int32 SweepHigh = 0;
		int32 SweepHighRawOff = 0;
		int32 SweepHighLowOff = 0;
		int32 SweepForeign = 0;
		int32 SweepForeignMisses = 0;
		TArray<FString> MissLines;
		/** Arguments: "sweep" = only case j (diagnostics on any map), "all" = every tall static mesh, not only the generator's tagged walls. */
		bool bSweepOnly = false;
		bool bAnyActor = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		if (UWorld* World = State.World.Get())
		{
			if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController()))
			{
				PC->bSimulateCursorForTesting = false;
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("ClickPreviewSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	void SetCVar(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(Value, ECVF_SetByCode);
		}
	}

	FName BuildingOf(const AActor* Actor)
	{
		if (Actor)
		{
			for (const FName& Tag : Actor->Tags)
			{
				if (Tag.ToString().StartsWith(TEXT("building=")))
				{
					return Tag;
				}
			}
		}
		return NAME_None;
	}

	/** The ground-floor wall pieces' faces with free navmesh ground StandOffCm out (nearest to Near first). */
	TArray<FFace> FindFaces(UWorld* World, const FVector& Near, float FeetZ, bool bAnyActor)
	{
		TArray<FFace> Faces;
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			const bool bWall = bAnyActor || BuildingOf(Actor) != NAME_None || Actor->ActorHasTag(FName(TEXT("wall"))) || Actor->ActorHasTag(FName(TEXT("building")));
			if (!bWall || Actor->ActorHasTag(FName(TEXT("interior"))))
			{
				continue;
			}
			const FBox Local = Actor->CalculateComponentsBoundingBoxInLocalSpace(false);
			if (!Local.IsValid)
			{
				continue;
			}
			const FTransform Transform = Actor->GetActorTransform();
			const FVector Size = Local.GetSize();
			const FVector AxisX = Transform.TransformVector(FVector(Size.X, 0.f, 0.f));
			const FVector AxisY = Transform.TransformVector(FVector(0.f, Size.Y, 0.f));
			const float LenX = static_cast<float>(AxisX.Size2D());
			const float LenY = static_cast<float>(AxisY.Size2D());
			const float Height = static_cast<float>(Transform.TransformVector(FVector(0.f, 0.f, Size.Z)).Size());
			const float Long = FMath::Max(LenX, LenY);
			const float Thick = FMath::Min(LenX, LenY);
			const FVector Centre = Transform.TransformPosition(Local.GetCenter());
			const float Bottom = static_cast<float>(Centre.Z) - Height * 0.5f;
			// bAnyActor (the "all" argument): every tall static mesh, solid blocks and art buildings too (not only the generator's thin wall cubes).
			if (Height < 220.f || Long < 400.f || Thick > (bAnyActor ? 3000.f : 80.f) || Long > 6000.f || FMath::Abs(Bottom - FeetZ) > 60.f)
			{
				continue;
			}
			const FVector Tangent = (LenX >= LenY ? AxisX : AxisY).GetSafeNormal2D();
			const FVector Across = (LenX >= LenY ? AxisY : AxisX).GetSafeNormal2D();
			for (const float Sign : { 1.f, -1.f })
			{
				FFace Face;
				Face.Actor = Actor;
				Face.Building = BuildingOf(Actor);
				Face.Normal = Across * Sign;
				Face.Tangent = Tangent;
				Face.HalfLength = Long * 0.5f;
				Face.Height = Height;
				Face.Base = FVector(Centre.X, Centre.Y, Bottom) + Face.Normal * Thick * 0.5f;
				FNavLocation OnNav;
				const FVector Stand = Face.Base + Face.Normal * StandOffCm;
				if (!Nav || !Nav->ProjectPointToNavigation(Stand, OnNav, FVector(60.f, 60.f, 120.f)) || FVector::Dist2D(OnNav.Location, Stand) > 60.f)
				{
					continue;
				}
				Face.Stand = OnNav.Location;
				Faces.Add(Face);
			}
		}
		Faces.Sort([&Near](const FFace& A, const FFace& B) { return FVector::DistSquared2D(A.Stand, Near) < FVector::DistSquared2D(B.Stand, Near); });
		return Faces;
	}

	/** The face is the one the camera sees (its normal towards the eye). */
	bool FaceSeen(UWorld* World, const FFace& Face)
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		if (!PC || !PC->PlayerCameraManager)
		{
			return false;
		}
		return FVector::DotProduct(Face.Normal, (PC->PlayerCameraManager->GetCameraLocation() - Face.Point(0.f, 120.f)).GetSafeNormal2D()) > 0.25;
	}

	/** The ray hit is on this face's wall (the same actor, the same building's piece in front: lowered / a corner piece). */
	bool OnWall(const FFace& Face, const FHitResult& Hit)
	{
		return Hit.GetActor() == Face.Actor.Get() || (Face.Building != NAME_None && BuildingOf(Hit.GetActor()) == Face.Building
			&& FMath::Abs(FVector::DotProduct(Hit.ImpactPoint - Face.Base, Face.Normal)) < 40.f);
	}

	void Teleport(AOperativeCharacter& Operative, const FVector& Feet, const FVector& Facing)
	{
		if (Operative.bInCover)
		{
			Operative.LeaveCover(TEXT("ClickPreviewSmoke teleport"));
		}
		Operative.StopOperative();
		Operative.SetStance(EOperativeStance::Standing);
		Operative.TeleportTo(Feet + FVector(0.f, 0.f, Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.f), Facing.Rotation(), false, true);
	}

	bool Ray(UWorld* World, const FVector& Point, FHitResult& Hit)
	{
		return MantleSmokeSites::CameraRayHit(World, Point, Hit);
	}

	/** A click: the (simulated) cursor rests where it clicks - as a real mouse does - so PlayerTick's tracking sees that spot, not an old one. */
	void ClickAt(ACodexTacticsPlayerController& PC, const FHitResult& Hit)
	{
		PC.SimulatedCursorHitForTesting = Hit;
		PC.HandlePlayerClick(Hit);
	}

	/** The face's end at Sign (+1 / -1 along its tangent) is an exposed corner: a slot 60 cm short of it sees that edge within 120 cm. */
	bool IsEndExposed(UWorld* World, const FFace& Face, float Sign)
	{
		FCoverSlot Slot;
		if (Face.HalfLength < 150.f || !CoverTraceRules::FindCoverSlotAt(World, Face.Point(Sign * (Face.HalfLength - 60.f), 120.f), -Face.Normal, Slot))
		{
			return false;
		}
		const bool bRightward = FVector::DotProduct(Slot.RightTangent(), Face.Tangent * Sign) > 0.0;
		return (bRightward ? Slot.bRightEdgeExposed : Slot.bLeftEdgeExposed) && (bRightward ? Slot.RightEdgeDistanceCm : Slot.LeftEdgeDistanceCm) <= 120.f;
	}

	const TCHAR* KindName(EClickPreviewKind Kind)
	{
		switch (Kind)
		{
		case EClickPreviewKind::Cover: return TEXT("cover");
		case EClickPreviewKind::CornerPeek: return TEXT("corner peek");
		case EClickPreviewKind::Mantle: return TEXT("climb");
		case EClickPreviewKind::Breach: return TEXT("breach");
		default: return TEXT("none");
		}
	}

	float GhostAlong(const ACodexTacticsPlayerController& PC, const FFace& Face)
	{
		const ACoverGhostActor* Ghost = PC.GetCoverPreviewGhost();
		return Ghost && Ghost->IsShown() ? static_cast<float>(FVector::DotProduct(Ghost->GetSlot().WorldLocation - Face.Base, Face.Tangent)) : TNumericLimits<float>::Max();
	}

	bool Step(TSharedRef<FState> State)
	{
		UWorld* World = State->World.Get();
		if (!World)
		{
			return false;
		}
		switch (State->PreloadGate.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting:
			return true;
		case SmokeUtils::EPreloadGate::Failed:
			Check(*State, false, State->PreloadGate.FailReason);
			return Finish(*State, false);
		default:
			break;
		}
		State->Time += StepSeconds;
		State->StageTime += StepSeconds;
		if (State->Time > 560.f)
		{
			Check(*State, false, FString::Printf(TEXT("time out in stage %d"), State->Stage));
			return Finish(*State, false);
		}
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (It->CustomTimeDilation > 0.f)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
			}
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UMantleSubsystem* Mantle = World->GetSubsystem<UMantleSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		AOperativeCharacter* Leader = State->Leader.Get();
		FFace& Face = State->Face;
		auto Feet = [](const AOperativeCharacter& Operative) { return Operative.GetActorLocation() - FVector(0.f, 0.f, Operative.GetSimpleCollisionHalfHeight()); };
		switch (State->Stage)
		{
		case 0:
		{
			if (!Squad || !Squad->GetLeader() || !Mantle || !PC || State->StageTime < 3.f || (!Mantle->HasGenerated() && State->StageTime < 90.f))
			{
				return true;
			}
			State->Leader = Squad->GetLeader();
			Leader = State->Leader.Get();
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member && Member != Leader)
				{
					State->Followers.Add(Member);
				}
			}
			Squad->EnterSoloMode();
			Squad->SetFollowersHolding(true);
			Leader->bTacticalCeaseFire = true;
			PC->bSimulateCursorForTesting = true;
			PC->SimulatedCursorHitForTesting = FHitResult();
			State->Faces = FindFaces(World, Leader->GetActorLocation(), static_cast<float>(Feet(*Leader).Z), State->bAnyActor);
			Check(*State, State->Faces.Num() > 0, FString::Printf(TEXT("the map's ground-floor wall faces with free ground in front: %d"), State->Faces.Num()));
			if (State->Faces.IsEmpty())
			{
				return Finish(*State, false);
			}
			Next(*State, State->bSweepOnly ? 50 : 1);
			return true;
		}
		case 1: // the primary wall: a face the camera sees, with an exposed end (a corner to peek round)
		{
			if (State->FaceIndex >= State->Faces.Num() || State->FaceIndex >= 40)
			{
				Check(*State, false, TEXT("a visible wall face with an exposed end for the primary cases"));
				return Finish(*State, false);
			}
			Face = State->Faces[State->FaceIndex++];
			Teleport(*Leader, Face.Stand, -Face.Normal);
			Next(*State, 2);
			return true;
		}
		case 2:
		{
			if (State->StageTime < CameraSettleSeconds)
			{
				return true;
			}
			FHitResult Hit;
			const bool bSeen = FaceSeen(World, Face) && Ray(World, Face.Point(0.f, 120.f), Hit) && OnWall(Face, Hit);
			// An exposed end (a corner to peek round): a slot 60 cm short of it sees its edge within 120 cm, the camera sees that end.
			float EndSign = 0.f;
			float EdgeAlong = 0.f;
			for (const float Sign : { 1.f, -1.f })
			{
				FCoverSlot Slot;
				FHitResult EndHit;
				const float NearEnd = Face.HalfLength - 60.f;
				if (!bSeen || Face.HalfLength < 250.f || !CoverTraceRules::FindCoverSlotAt(World, Face.Point(Sign * NearEnd, 120.f), -Face.Normal, Slot)
					|| !Ray(World, Face.Point(Sign * (Face.HalfLength - 30.f), 120.f), EndHit) || !OnWall(Face, EndHit))
				{
					continue;
				}
				const bool bRightward = FVector::DotProduct(Slot.RightTangent(), Face.Tangent * Sign) > 0.0;
				const bool bExposed = bRightward ? Slot.bRightEdgeExposed : Slot.bLeftEdgeExposed;
				const float Edge = bRightward ? Slot.RightEdgeDistanceCm : Slot.LeftEdgeDistanceCm;
				if (bExposed && Edge <= 120.f)
				{
					EndSign = Sign;
					EdgeAlong = NearEnd + Edge;
					break;
				}
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[ClickPreviewSmoke] candidate %s (%s) normal %s: seen %d, half length %.0f, exposed end %s"), *GetNameSafe(Face.Actor.Get()),
				*Face.Building.ToString(), *Face.Normal.ToCompactString(), bSeen ? 1 : 0, Face.HalfLength, EndSign == 0.f ? TEXT("none") : EndSign > 0.f ? TEXT("+") : TEXT("-"));
			if (EndSign == 0.f)
			{
				Next(*State, 1);
				return true;
			}
			State->EndSign = EndSign;
			Face.HalfLength = FMath::Min(Face.HalfLength, EdgeAlong); // the slide's end: the exposed corner
			UE_LOG(LogCodexTactics, Display, TEXT("[ClickPreviewSmoke] primary wall %s (%s), face normal %s, %.0f cm to its exposed end"), *GetNameSafe(Face.Actor.Get()),
				*Face.Building.ToString(), *Face.Normal.ToCompactString(), EdgeAlong);
			Next(*State, 10);
			return true;
		}
		case 10: // a: the hover shows nothing (PlayerTick's hover block on the wall's end)
		{
			if (State->StageTime < 0.1f)
			{
				FHitResult Hit;
				Ray(World, Face.Point(State->EndSign * (Face.HalfLength - 25.f), 120.f), Hit);
				PC->SimulatedCursorHitForTesting = Hit;
				return true;
			}
			if (State->StageTime < 0.6f)
			{
				return true;
			}
			Check(*State, PC->GetClickPreviewGhostCount() == 0 && !PC->IsClickPreviewArmed(), FString::Printf(
				TEXT("a: hovering the wall's end (%s) shows no ghost (%d silhouettes)"), *GetNameSafe(PC->SimulatedCursorHitForTesting.GetActor()), PC->GetClickPreviewGhostCount()));
			SetCVar(TEXT("Codex.Click.Preview"), 0); // the control: the old rules hover there
			Next(*State, 11);
			return true;
		}
		case 11:
		{
			if (State->StageTime < 0.5f)
			{
				return true;
			}
			Check(*State, PC->GetClickPreviewGhostCount() > 0, FString::Printf(
				TEXT("a: the control - with Codex.Click.Preview 0 the same hover shows the old hover ghost (%d silhouettes; the check above can fail)"), PC->GetClickPreviewGhostCount()));
			SetCVar(TEXT("Codex.Click.Preview"), 1);
			PC->UpdateCornerPeekHover(nullptr, nullptr);
			PC->UpdateMantleHover(nullptr, nullptr);
			PC->CancelClickPreview(TEXT("smoke"));
			// The middle of the face, hovered.
			FHitResult Hit;
			Ray(World, Face.Point(0.f, 120.f), Hit);
			PC->SimulatedCursorHitForTesting = Hit;
			Next(*State, 12);
			return true;
		}
		case 12:
		{
			if (State->StageTime < 0.5f)
			{
				return true;
			}
			Check(*State, PC->GetClickPreviewGhostCount() == 0, FString::Printf(TEXT("a: hovering the wall's middle shows no ghost (%d)"), PC->GetClickPreviewGhostCount()));
			// b: the first click on the face.
			FHitResult Hit;
			Ray(World, Face.Point(0.f, 120.f), Hit);
			ClickAt(*PC, Hit);
			Next(*State, 13);
			return true;
		}
		case 13:
		{
			if (State->StageTime < 0.5f)
			{
				return true;
			}
			const ACoverGhostActor* Ghost = PC->GetCoverPreviewGhost();
			Check(*State, PC->GetClickPreviewKind() == EClickPreviewKind::Cover && Ghost && Ghost->IsShown() && !Leader->IsMoving() && !Leader->bInCover,
				FString::Printf(TEXT("b: the first click on the wall shows the cover ghost (%s, ghost %s) and orders nothing (moving %d)"), KindName(PC->GetClickPreviewKind()),
					Ghost && Ghost->IsShown() ? TEXT("up") : TEXT("NOT up"), Leader->IsMoving() ? 1 : 0));
			State->FirstGhostAlong = GhostAlong(*PC, Face);
			State->LastGhostAlong = State->FirstGhostAlong;
			State->SlideAlong = 0.f;
			State->bSawPeek = false;
			Next(*State, 14);
			return true;
		}
		case 14: // c: the cursor along the wall (PlayerTick feeds it to the ghost)
		{
			if (State->StageTime < 0.15f)
			{
				return true;
			}
			if (PC->GetClickPreviewKind() == EClickPreviewKind::CornerPeek)
			{
				State->bSawPeek = true;
			}
			else if (PC->GetClickPreviewKind() == EClickPreviewKind::Cover)
			{
				const float Along = GhostAlong(*PC, Face);
				if (Along < TNumericLimits<float>::Max())
				{
					State->LastGhostAlong = Along;
				}
			}
			if (State->bSawPeek || State->SlideAlong >= Face.HalfLength - 5.f)
			{
				const float Moved = (State->LastGhostAlong - State->FirstGhostAlong) * State->EndSign;
				Check(*State, Moved >= 80.f, FString::Printf(TEXT("c: the cursor along the wall moves the cover ghost along it (%.0f cm towards the end)"), Moved));
				Check(*State, State->bSawPeek, FString::Printf(TEXT("c: at the wall's end the ghost turns into the corner peek (%s at %.0f of %.0f cm)"),
					KindName(PC->GetClickPreviewKind()), State->SlideAlong, Face.HalfLength));
				// d: the second click right there.
				FHitResult Hit;
				Ray(World, Face.Point(State->EndSign * State->SlideAlong, 120.f), Hit);
				ClickAt(*PC, Hit);
				Next(*State, 15);
				return true;
			}
			State->SlideAlong = FMath::Min(State->SlideAlong + SlideStepCm, Face.HalfLength - 5.f);
			FHitResult Hit;
			Ray(World, Face.Point(State->EndSign * State->SlideAlong, 120.f), Hit);
			PC->SimulatedCursorHitForTesting = Hit;
			State->StageTime = 0.f;
			return true;
		}
		case 15:
		{
			const bool bPeek = Leader->IsManualCornerPeek();
			if (!bPeek && State->StageTime < 14.f)
			{
				return true;
			}
			const FVector End = Face.Point(State->EndSign * Face.HalfLength, 0.f);
			Check(*State, bPeek && FVector::Dist2D(Leader->GetActorLocation(), End) <= 170.f, FString::Printf(
				TEXT("d: the second click at the corner - he runs there and peeks round it (peek %d after %.1f s, %.0f cm from the end)"), bPeek ? 1 : 0, State->StageTime,
				FVector::Dist2D(Leader->GetActorLocation(), End)));
			// e: a first click on the wall, then the ground away from it.
			FHitResult Hit;
			Ray(World, Face.Point(0.f, 120.f), Hit);
			ClickAt(*PC, Hit);
			Next(*State, 16);
			return true;
		}
		case 16:
		{
			if (State->StageTime < 0.5f)
			{
				return true;
			}
			const bool bArmed = PC->IsClickPreviewArmed();
			State->MoveTarget = Face.Stand + Face.Tangent * 250.f;
			FHitResult Ground;
			Ray(World, State->MoveTarget + FVector(0.f, 0.f, 2.f), Ground);
			State->MoveTarget = Ground.ImpactPoint;
			ClickAt(*PC, Ground);
			Check(*State, bArmed && !PC->IsClickPreviewArmed() && PC->GetClickPreviewGhostCount() == 0, FString::Printf(
				TEXT("e: a ground click away (%s, normal z %.2f) drops the ghost (armed before %d, after %d, %d silhouettes)"), *GetNameSafe(Ground.GetActor()),
				Ground.ImpactNormal.Z, bArmed ? 1 : 0, PC->IsClickPreviewArmed() ? 1 : 0, PC->GetClickPreviewGhostCount()));
			Next(*State, 17);
			return true;
		}
		case 17:
		{
			const bool bThere = FVector::Dist2D(Leader->GetActorLocation(), State->MoveTarget) <= 120.f;
			if (!bThere && State->StageTime < 10.f)
			{
				return true;
			}
			Check(*State, bThere && !Leader->bInCover, FString::Printf(TEXT("e: ... and it is the ordinary move (%.0f cm from the clicked point after %.1f s, in cover %d)"),
				FVector::Dist2D(Leader->GetActorLocation(), State->MoveTarget), State->StageTime, Leader->bInCover ? 1 : 0));
			// f: a first click on the wall, a slow second one.
			FHitResult Hit;
			Ray(World, Face.Point(0.f, 120.f), Hit);
			ClickAt(*PC, Hit);
			Next(*State, 18);
			return true;
		}
		case 18:
		{
			if (State->StageTime < 0.6f)
			{
				return true;
			}
			const ACoverGhostActor* Ghost = PC->GetCoverPreviewGhost();
			State->GhostSlot = Ghost && Ghost->IsShown() ? Ghost->GetSlot().WorldLocation : FVector::ZeroVector;
			FHitResult Hit;
			Ray(World, Face.Point(0.f, 120.f), Hit);
			ClickAt(*PC, Hit);
			Next(*State, 19);
			return true;
		}
		case 19:
		{
			if (!Leader->bInCover && State->StageTime < 10.f)
			{
				return true;
			}
			const float Off = Leader->bInCover ? static_cast<float>(FVector::Dist2D(Leader->GetCoverSlot().WorldLocation, State->GhostSlot)) : -1.f;
			Check(*State, Leader->bInCover && !State->GhostSlot.IsZero() && Off <= 150.f, FString::Printf(
				TEXT("f: the slow second click on the wall - he takes cover at the ghost's slot (in cover %d after %.1f s, %.0f cm from the ghost)"), Leader->bInCover ? 1 : 0,
				State->StageTime, Off));
			Next(*State, 30);
			return true;
		}
		case 30: // g / h: climbs (Solo)
		{
			if (State->MantleCase >= 2)
			{
				Next(*State, 40);
				return true;
			}
			State->Site = MantleSmokeSites::FindSite(World, Leader, TEXT("wreck"), 140.f, StandOffCm, Leader->GetActorLocation(), State->UsedSites, false, 0.f, true);
			if (!State->Site.bFound)
			{
				Check(*State, false, TEXT("g/h: a 140 cm wreck to climb on the map"));
				Next(*State, 40);
				return true;
			}
			State->UsedSites.Add(State->Site.Actor.Get());
			Teleport(*Leader, State->Site.Feet, State->Site.Dir);
			State->MantlesBefore = Leader->GetMantleCount();
			Next(*State, 31);
			return true;
		}
		case 31:
		{
			if (State->StageTime < CameraSettleSeconds)
			{
				return true;
			}
			const FMantleSite& Probe = State->Site.Probe;
			const FVector Top = Probe.EdgeTop + Probe.Dir * FMath::Max(20.f, Probe.Probe.DepthCm * 0.5f);
			FHitResult Hit;
			if (!Ray(World, FVector(Top.X, Top.Y, Probe.TopZ), Hit) || Hit.GetActor() != State->Site.Actor.Get())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[ClickPreviewSmoke] the camera does not see %s's top (hit %s) - another wreck"), *GetNameSafe(State->Site.Actor.Get()),
					*GetNameSafe(Hit.GetActor()));
				Next(*State, State->UsedSites.Num() > 10 ? 40 : 30);
				return true;
			}
			State->bSawSprint = false;
			ClickAt(*PC, Hit);
			if (State->MantleCase == 1)
			{
				ClickAt(*PC, Hit); // h: the double click (the same frame)
				Next(*State, 33);
				return true;
			}
			Next(*State, 32);
			return true;
		}
		case 32: // g: the first click shows the climb ghost, no climb; then the slow second click
		{
			if (State->StageTime < 1.f)
			{
				return true;
			}
			Check(*State, PC->GetClickPreviewKind() == EClickPreviewKind::Mantle && PC->IsMantleHoverShown() && Leader->GetMantleCount() == State->MantlesBefore
				&& !Leader->IsMoving(), FString::Printf(TEXT("g: the first click on %s's top shows the climb ghost (%s, ghost %d) and does not climb yet (moving %d)"),
				*GetNameSafe(State->Site.Actor.Get()), KindName(PC->GetClickPreviewKind()), PC->IsMantleHoverShown() ? 1 : 0, Leader->IsMoving() ? 1 : 0));
			const FMantleSite& Probe = State->Site.Probe;
			const FVector Top = Probe.EdgeTop + Probe.Dir * FMath::Max(20.f, Probe.Probe.DepthCm * 0.5f);
			FHitResult Hit;
			Ray(World, FVector(Top.X, Top.Y, Probe.TopZ), Hit);
			ClickAt(*PC, Hit);
			Next(*State, 33);
			return true;
		}
		case 33:
		{
			const bool bClimbed = Leader->GetMantleCount() > State->MantlesBefore;
			State->bSawSprint |= !bClimbed && Leader->IsSprinting();
			if (!bClimbed && State->StageTime < 12.f)
			{
				return true;
			}
			const bool bRun = State->MantleCase == 1;
			Check(*State, bClimbed && State->bSawSprint == bRun, FString::Printf(TEXT("%s: %s on %s's top - he %s there and climbs it (climbed %d, ran %d)"),
				bRun ? TEXT("h") : TEXT("g"), bRun ? TEXT("a double click") : TEXT("the slow second click"), *GetNameSafe(State->Site.Actor.Get()),
				bRun ? TEXT("RUNS") : TEXT("walks"), bClimbed ? 1 : 0, State->bSawSprint ? 1 : 0));
			Next(*State, 34);
			return true;
		}
		case 34:
		{
			if (Leader->IsMantling() && State->StageTime < 12.f)
			{
				return true;
			}
			++State->MantleCase;
			Next(*State, 30);
			return true;
		}
		case 40: // i: formation - the ghost shows every member
		{
			Squad->ExitSoloMode();
			Squad->SetFollowersHolding(false);
			Teleport(*Leader, Face.Stand, -Face.Normal);
			int32 Index = 0;
			for (const TWeakObjectPtr<AOperativeCharacter>& Each : State->Followers)
			{
				if (AOperativeCharacter* Follower = Each.Get())
				{
					const float Side = (Index % 2 == 0 ? 1.f : -1.f) * (120.f + 60.f * (Index / 2));
					Teleport(*Follower, Face.Stand + Face.Tangent * Side + Face.Normal * 150.f, -Face.Normal);
					++Index;
				}
			}
			Next(*State, 41);
			return true;
		}
		case 41:
		{
			if (State->StageTime < 3.f)
			{
				return true;
			}
			FHitResult Hit;
			Ray(World, Face.Point(0.f, 120.f), Hit);
			ClickAt(*PC, Hit);
			Next(*State, 42);
			return true;
		}
		case 42:
		{
			if (State->StageTime < 0.6f)
			{
				return true;
			}
			const TArray<AOperativeCharacter*> Others = Squad->GetGroupCoverMembers(Leader);
			const TArray<const ACoverGhostActor*> Ghosts = PC->GetClickPreviewGroupGhosts();
			TSet<const AOperativeCharacter*> Shown;
			FString Line;
			bool bAtWall = true;
			for (const ACoverGhostActor* Ghost : Ghosts)
			{
				Shown.Add(Ghost->GetOperative());
				const float Off = static_cast<float>(FVector::DotProduct(Ghost->GetSlot().WorldLocation - Face.Base, Face.Normal));
				bAtWall &= Off >= -20.f && Off <= 250.f;
				Line += FString::Printf(TEXT(" %s %.0f cm off the face;"), Ghost->GetOperative() ? *Ghost->GetOperative()->DisplayName.ToString() : TEXT("?"), Off);
			}
			bool bAll = !Others.IsEmpty();
			for (const AOperativeCharacter* Other : Others)
			{
				bAll &= Shown.Contains(Other);
			}
			Check(*State, PC->GetClickPreviewKind() == EClickPreviewKind::Cover && bAll && Ghosts.Num() == Others.Num() && bAtWall, FString::Printf(
				TEXT("i: formation - the first click on the wall shows every member who goes along (%d of %d silhouettes besides the leader's, %s):%s"), Ghosts.Num(), Others.Num(),
				KindName(PC->GetClickPreviewKind()), *Line));
			State->FaceIndex = Others.Num(); // reused: how many go along
			Next(*State, 43);
			return true;
		}
		case 43:
		{
			if (State->StageTime < 0.1f)
			{
				return true;
			}
			FHitResult Hit;
			Ray(World, Face.Point(0.f, 120.f), Hit);
			ClickAt(*PC, Hit);
			Next(*State, 44);
			return true;
		}
		case 44:
		{
			if (State->StageTime < 9.f)
			{
				return true;
			}
			int32 AtWall = 0;
			FString Line;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const float Off = static_cast<float>(FVector::DotProduct(Feet(*Member) - Face.Base, Face.Normal));
				AtWall += Off <= 200.f ? 1 : 0;
				Line += FString::Printf(TEXT(" %s %.0f cm;"), *Member->DisplayName.ToString(), Off);
			}
			Check(*State, AtWall >= 1 + State->FaceIndex, FString::Printf(TEXT("i: the second click - the squad goes to the wall (%d of %d within 2 m of the face):%s"), AtWall,
				Squad->GetMembers().Num(), *Line));
			Squad->EnterSoloMode();
			Squad->SetFollowersHolding(true);
			State->SweepFace = 0;
			Next(*State, 50);
			return true;
		}
		case 50: // j: the sweep over every visible wall face
		{
			if (State->SweepFace >= State->Faces.Num() || State->SweepTried >= (State->bAnyActor ? MaxSweepFaces * 3 : MaxSweepFaces))
			{
				for (const FString& Miss : State->MissLines)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("[ClickPreviewSmoke] sweep miss: %s"), *Miss);
				}
				Check(*State, State->SweepTried >= 6 && State->SweepMisses == 0, FString::Printf(
					TEXT("j: every building click has a building meaning - %d wall faces, %d cursor hits, %d misses (%d occluded / not seen skipped); ")
					TEXT("with Codex.Click.LowerHighHits 0 the same hits miss %d times"), State->SweepTried, State->SweepHits, State->SweepMisses, State->SweepOccluded,
					State->SweepMissesNoLower));
				Check(*State, State->SweepHighLowOff == 0, FString::Printf(
					TEXT("j: a click high up a wall puts the ghost on his floor - %d high hits: %d slots off his floor with the lowered hit (the raw hit: %d)"),
					State->SweepHigh, State->SweepHighLowOff, State->SweepHighRawOff));
				Check(*State, State->SweepForeignMisses == 0, FString::Printf(
					TEXT("j: the pieces in front of those faces the cursor hits instead (a cross wall, a pillar, a sill): %d hits, %d without a meaning"), State->SweepForeign,
					State->SweepForeignMisses));
				// The corner peek's own finder (RunCornerPeekRules / FindCornerPeekForSlot, unchanged by R17-3) decides an end: >= 95 % of the exposed ends
				// (2026-11-17 on L_Gen_Depot_1: 36 of 37; the one miss is listed above with the finder's verdict - KNOWN, open).
				Check(*State, State->SweepEnds >= 10 && State->SweepEndPeeks * 100 >= State->SweepEnds * 95, FString::Printf(
					TEXT("j: a click near every exposed wall end is the corner peek - %d of %d ends"), State->SweepEndPeeks, State->SweepEnds));
				return Finish(*State, true);
			}
			Face = State->Faces[State->SweepFace++];
			Teleport(*Leader, Face.Stand, -Face.Normal);
			Next(*State, 51);
			return true;
		}
		case 51:
		{
			if (State->StageTime < CameraSettleSeconds)
			{
				return true;
			}
			if (!FaceSeen(World, Face))
			{
				++State->SweepOccluded;
				Next(*State, 50);
				return true;
			}
			++State->SweepTried;
			struct FAim { float Along; float Up; };
			TArray<FAim> Aims = { { 0.f, 100.f }, { 0.f, 250.f }, { 0.f, 400.f }, { Face.HalfLength - 30.f, 120.f }, { -(Face.HalfLength - 30.f), 120.f } };
			for (const FAim& Aim : Aims)
			{
				if (Aim.Up > Face.Height - 20.f)
				{
					continue;
				}
				FHitResult Hit;
				if (!Ray(World, Face.Point(Aim.Along, Aim.Up), Hit))
				{
					++State->SweepOccluded;
					continue;
				}
				if (!OnWall(Face, Hit))
				{
					++State->SweepOccluded;
					// What the player clicks there instead: another wall-like piece of the scenery (not the ground, a body, a roof) must mean something too.
					const AActor* Other = Hit.GetActor();
					if (Other && Other->IsA<AStaticMeshActor>() && FMath::Abs(Hit.ImpactNormal.Z) < 0.3f
						&& Hit.ImpactPoint.Z - Feet(*Leader).Z < 400.f && FVector::Dist2D(Hit.ImpactPoint, Leader->GetActorLocation()) < 1500.f)
					{
						++State->SweepForeign;
						FString Why;
						if (PC->ClassifyClickPreview(Hit, &Why) == EClickPreviewKind::None)
						{
							++State->SweepForeignMisses;
							State->MissLines.Add(FString::Printf(TEXT("(in front of %s) %s at %s: %s"), *GetNameSafe(Face.Actor.Get()), *GetNameSafe(Other),
								*Hit.ImpactPoint.ToCompactString(), *Why));
						}
					}
					continue;
				}
				++State->SweepHits;
				if (Aim.Up >= 200.f)
				{
					// A high hit (a tall / two-storey building): the slot the raw hit gives vs the lowered one - off his floor = a ghost in the air, no way there.
					FCoverSlot Raw;
					FCoverSlot Low;
					const FHitResult Lowered = PC->LowerBuildingHit(Hit);
					++State->SweepHigh;
					State->SweepHighRawOff += !CoverTraceRules::FindCoverSlotAt(World, Hit.ImpactPoint, -Face.Normal, Raw)
						|| FMath::Abs(Raw.WorldLocation.Z - Feet(*Leader).Z) > 50.f ? 1 : 0;
					State->SweepHighLowOff += !CoverTraceRules::FindCoverSlotAt(World, Lowered.ImpactPoint, -Face.Normal, Low)
						|| FMath::Abs(Low.WorldLocation.Z - Feet(*Leader).Z) > 50.f ? 1 : 0;
				}
				FString Why;
				const EClickPreviewKind Kind = PC->ClassifyClickPreview(Hit, &Why);
				const bool bEndAim = FMath::Abs(Aim.Along) > 1.f && Aim.Up < 200.f;
				// An exposed end: the peek - or, where the end is a doorway's jamb (within 40 cm of the opening), the breach (designer answer 4, 2026-11-11).
				if (bEndAim && Kind != EClickPreviewKind::Breach && IsEndExposed(World, Face, FMath::Sign(Aim.Along)))
				{
					++State->SweepEnds;
					State->SweepEndPeeks += Kind == EClickPreviewKind::CornerPeek ? 1 : 0;
					if (Kind != EClickPreviewKind::CornerPeek)
					{
						// Why: the run-and-peek finder's verdict and the cover slot at the click (its wall actor, its edges).
						FSquadCornerPeekTarget Target;
						const bool bRunPeek = PC->FindRunCornerPeekForHit(Leader, Hit, Target);
						FCoverSlot Slot;
						const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, Hit.ImpactPoint, -Face.Normal, Slot);
						State->MissLines.Add(FString::Printf(TEXT("end of %s (%s) at %.0f along (hit %s.%s at %s, %.0f cm over his feet, %.0f cm from him): %s, not the peek - run-peek finder %d (corner %.0f cm over his feet), ")
							TEXT("slot %d on %s, height %d, edges L %d %.0f / R %d %.0f"), *GetNameSafe(Face.Actor.Get()), *Face.Building.ToString(), Aim.Along, *GetNameSafe(Hit.GetActor()),
							*GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToCompactString(), Hit.ImpactPoint.Z - Feet(*Leader).Z, FVector::Dist2D(Hit.ImpactPoint, Leader->GetActorLocation()),
							KindName(Kind), bRunPeek ? 1 : 0,
							bRunPeek ? Target.CornerSlot.WorldLocation.Z - Feet(*Leader).Z : 0.0, bSlot ? 1 : 0, *GetNameSafe(Slot.WallActor.Get()), static_cast<int32>(Slot.Height),
							Slot.bLeftEdgeExposed ? 1 : 0, Slot.LeftEdgeDistanceCm, Slot.bRightEdgeExposed ? 1 : 0, Slot.RightEdgeDistanceCm));
					}
				}
				SetCVar(TEXT("Codex.Click.LowerHighHits"), 0);
				const EClickPreviewKind Raw = PC->ClassifyClickPreview(Hit);
				SetCVar(TEXT("Codex.Click.LowerHighHits"), 1);
				State->SweepMissesNoLower += Raw == EClickPreviewKind::None ? 1 : 0;
				if (Kind == EClickPreviewKind::None)
				{
					++State->SweepMisses;
					State->MissLines.Add(FString::Printf(TEXT("%s (%s) at %.0f along / %.0f up: %s"), *GetNameSafe(Face.Actor.Get()), *Face.Building.ToString(), Aim.Along, Aim.Up, *Why));
				}
			}
			Next(*State, 50);
			return true;
		}
		default:
			return Finish(*State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		for (const FString& Arg : Args)
		{
			State->bSweepOnly |= Arg.Equals(TEXT("sweep"), ESearchCase::IgnoreCase);
			State->bAnyActor |= Arg.Equals(TEXT("all"), ESearchCase::IgnoreCase);
		}
		// The same from the command line (-ExecCmds passes no arguments): -ClickSweepOnly, -ClickSweepAll.
		State->bSweepOnly |= FParse::Param(FCommandLine::Get(), TEXT("ClickSweepOnly"));
		State->bAnyActor |= FParse::Param(FCommandLine::Get(), TEXT("ClickSweepAll"));
		// The Depot's scheduled PMC squads (pmc_squads at 30 s / 120 s) open a firefight mid-case (the men go prone / take cover against the flash and
		// leave their orders); this smoke checks the clicks, not a fight (as LevelGenRampSmoke, 2026-11-10).
		if (IConsoleVariable* Encounters = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pmc.Encounters")))
		{
			Encounters->Set(0, ECVF_SetByConsole);
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.ClickPreviewSmoke"),
		TEXT("Dev check (R17-3 unified building click, L_Gen_Depot_1): hovering shows no ghost, the first click on a wall shows the cover ghost, the cursor ")
		TEXT("along it slides it and turns it into the peek at the corner, the second click carries it out, a ground click cancels, climbs (click / double click), ")
		TEXT("the formation ghost shows every member, every visible wall face has a building meaning; PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
