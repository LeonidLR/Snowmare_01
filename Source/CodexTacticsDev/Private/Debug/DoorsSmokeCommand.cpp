// Dev-only console command: openable doors R1 (designer 2026-11-21; Interactables/DoorSubsystem, DoorActor, DoorRules) on the story showcase
// /Game/Maps/Generated/L_Gen_StoryShowcase with the switch Codex.Doors.Openable ON (set by the smoke itself):
//   Scripts/smoke.ps1 -Command CodexTactics.DoorsSmoke -Map /Game/Maps/Generated/L_Gen_StoryShowcase
//   A  adoption: closed / locked leaves became doors (S01 front closed, S03 front locked, S03 gable closed); a closed door blocks sight
//      (Visibility across the doorway); the navmesh: a path through the closed S01 door exists, no path runs through the locked S03 door;
//   B  Solo, the leader's CLICK on the closed S01 door (HandlePlayerClick): he walks up and opens it; the doorway passes sight; the cold
//      shelter sees the opening open;
//   C  the click on the floor by the open leaf closes it: sight blocked again, the cold shelter sealed;
//   D  the squad (formation) ordered through the other closed S01 door: the door opens by itself (auto-open), everybody ends inside;
//   E  story 3: the leader ordered into S03 behind its LOCKED front door walks round through the gable door (it auto-opens), never through
//      the locked one, which stays locked;
//   F  the kick hook: his click on the locked door from inside: he stands t_kick, the door bursts open (kicked);
//   G  the switch OFF at runtime: every door released, the builder leaves back (visible, blocking sight, walk-through) - as before R1.
// Enemies are parked far away; no pressure / PMC encounters.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/Explosion.h"
#include "Combat/HealthComponent.h"
#include "Data/WeaponDataAsset.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/DoorActor.h"
#include "Interactables/DoorRules.h"
#include "Interactables/DoorSubsystem.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Survival/ColdShelterSubsystem.h"

namespace DoorsSmoke
{
	constexpr float StepSeconds = 0.05f;

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		SmokeUtils::FPreloadGate PreloadGate;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<ADoorActor> FrontS01;
		TWeakObjectPtr<ADoorActor> BackS01;
		TWeakObjectPtr<ADoorActor> LockedS03;
		TWeakObjectPtr<ADoorActor> GableS03;
		TWeakObjectPtr<ADoorActor> FrontS09;
		TWeakObjectPtr<ADoorActor> GableS09;
		TWeakObjectPtr<ADoorActor> FrontS04;
		int32 KicksBefore = 0;
		int32 InjuriesBefore = 0;
		float HealthBefore = 0.f;
		bool bSecondKickArmed = false;
		bool bKeyClicked = false;
		float KeyClickAt = 0.f;
		int32 ClipBefore = 0;
		FVector Target = FVector::ZeroVector;
		float InsideSign = 1.f;
		int32 AutoOpensBefore = 0;
		bool bCrossedLocked = false;
		FVector2D LastLockedLocal = FVector2D::ZeroVector;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s DoorsSmoke (%d failure(s)%s)"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"),
			State.Failures, bComplete ? TEXT("") : TEXT(", incomplete"));
		FPlatformMisc::RequestExit(false, TEXT("DoorsSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	void SetDoorsSwitch(int32 Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Doors.Openable")))
		{
			Variable->Set(Value, ECVF_SetByConsole);
		}
	}

	FVector Feet(const AOperativeCharacter& Operative)
	{
		return Operative.GetActorLocation() - FVector(0.f, 0.f, Operative.GetSimpleCollisionHalfHeight());
	}

	bool OnNav(UWorld* World, const FVector& Point, FVector& Out)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Location;
		if (!Nav || !Nav->ProjectPointToNavigation(Point, Location, FVector(60.f, 60.f, 150.f)) || FVector::Dist2D(Location.Location, Point) > 70.f)
		{
			return false;
		}
		Out = Location.Location;
		return true;
	}

	void Teleport(AOperativeCharacter& Operative, const FVector& At, const FVector& Facing)
	{
		if (Operative.bInCover)
		{
			Operative.LeaveCover(TEXT("DoorsSmoke teleport"));
		}
		Operative.StopOperative();
		Operative.ClearManualLookPoint();
		Operative.SetStance(EOperativeStance::Standing);
		Operative.TeleportTo(At + FVector(0.f, 0.f, Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.f), Facing.Rotation(), false, true);
	}

	/** +1 when the door's +Y side is inside its building (a ceiling over the point 150 cm in), else -1. */
	float InsideSign(UWorld* World, const ADoorActor& Door)
	{
		for (const float Sign : { 1.f, -1.f })
		{
			const FVector Point = Door.GetActorLocation() + Door.GetNormal() * (Sign * 150.f) + FVector(0.f, 0.f, 100.f);
			FHitResult Hit;
			FCollisionQueryParams Params(FName(TEXT("DoorsSmokeCeiling")), false);
			if (World->LineTraceSingleByChannel(Hit, Point, Point + FVector(0.f, 0.f, 500.f), ECC_Visibility, Params))
			{
				return Sign;
			}
		}
		return 1.f;
	}

	/** A Visibility trace across the doorway at chest height (60 cm each side): what it hits (null = clear). */
	AActor* TraceAcross(UWorld* World, const ADoorActor& Door)
	{
		// 20 cm off the middle: a pair of leaves meets on the middle line (a ray along the seam may slip between them).
		const FVector Center = Door.GetActorLocation() + Door.GetTangent() * 20.f + FVector(0.f, 0.f, 130.f);
		FHitResult Hit;
		FCollisionQueryParams Params(FName(TEXT("DoorsSmokeSight")), false);
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		return World->LineTraceSingleByChannel(Hit, Center + Door.GetNormal() * 60.f, Center - Door.GetNormal() * 60.f, ECC_Visibility, Params) ? Hit.GetActor() : nullptr;
	}

	/** The navmesh path From -> To for the leader; OutCrosses: it runs through Door's doorway. False when there is no complete path. */
	bool PathThrough(UWorld* World, const AOperativeCharacter& Agent, const FVector& From, const FVector& To, const ADoorActor& Door, bool& bOutCrosses, double& OutLength)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(World, From, To, const_cast<AOperativeCharacter*>(&Agent)) : nullptr;
		bOutCrosses = false;
		OutLength = 0.0;
		if (!Path || !Path->IsValid() || Path->IsPartial())
		{
			return false;
		}
		TArray<FVector2D> Local;
		for (const FVector& Point : Path->PathPoints)
		{
			Local.Add(Door.ToLocal(Point));
		}
		for (int32 Index = 1; Index < Path->PathPoints.Num(); ++Index)
		{
			OutLength += FVector::Dist(Path->PathPoints[Index - 1], Path->PathPoints[Index]);
		}
		bOutCrosses = DoorRules::PathCrossesDoorway(Local, Door.GetWidthCm() * 0.5f, 1.0e7f);
		return true;
	}

	FHitResult FloorHit(const FVector& Point)
	{
		FHitResult Hit(nullptr, nullptr, Point, FVector::UpVector);
		Hit.bBlockingHit = true;
		Hit.Location = Point;
		return Hit;
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
		if (State->Time > 420.f)
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
		UDoorSubsystem* Doors = World->GetSubsystem<UDoorSubsystem>();
		UColdShelterSubsystem* Cold = World->GetSubsystem<UColdShelterSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		AOperativeCharacter* Leader = State->Leader.Get();
		const bool bNavBusy = UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World);
		switch (State->Stage)
		{
		case 0: // A: adoption, sight, navmesh
		{
			if (!Squad || !Squad->GetLeader() || !PC || !Doors || !Doors->IsActive() || bNavBusy || State->StageTime < 3.f)
			{
				return true;
			}
			State->Leader = Squad->GetLeader();
			Leader = State->Leader.Get();
			Check(*State, Doors->GetDoors().Num() > 0, FString::Printf(TEXT("A: %d doors adopted from the builder leaves"), Doors->GetDoors().Num()));
			State->FrontS01 = Doors->FindDoor(TEXT("S01_open"), TEXT("S01_open_f0_d19"));
			State->BackS01 = Doors->FindDoor(TEXT("S01_open"), TEXT("S01_open_f0_d20"));
			State->LockedS03 = Doors->FindDoor(TEXT("S03_locked_door"), TEXT("S03_locked_door_f0_d19"));
			State->GableS03 = Doors->FindDoor(TEXT("S03_locked_door"), TEXT("S03_locked_door_f0_d20"));
			State->FrontS09 = Doors->FindDoor(TEXT("S09_broken_back_windows"), TEXT("S09_broken_back_windows_f0_d19"));
			State->GableS09 = Doors->FindDoor(TEXT("S09_broken_back_windows"), TEXT("S09_broken_back_windows_f0_d20"));
			State->FrontS04 = Doors->FindDoor(TEXT("S04_rubble_escape"), TEXT("S04_rubble_escape_f0_d19"));
			Check(*State, State->FrontS09.IsValid() && State->FrontS09->GetDoorState() == EDoorLeafState::Locked, TEXT("A: S09 front door d19 is LOCKED"));
			Check(*State, State->GableS09.IsValid() && State->GableS09->GetDoorState() == EDoorLeafState::Locked, TEXT("A: S09 gable door d20 is LOCKED (story 9)"));
			Check(*State, State->FrontS04.IsValid() && State->FrontS04->GetDoorState() == EDoorLeafState::Closed, TEXT("A: S04 door d19 is CLOSED"));
			if (!State->FrontS09.IsValid() || !State->GableS09.IsValid() || !State->FrontS04.IsValid())
			{
				return Finish(*State, false);
			}
			ADoorActor* Front = State->FrontS01.Get();
			ADoorActor* Back = State->BackS01.Get();
			ADoorActor* Locked = State->LockedS03.Get();
			ADoorActor* Gable = State->GableS03.Get();
			Check(*State, Front && Front->GetDoorState() == EDoorLeafState::Closed, TEXT("A: S01 door d19 is a CLOSED door"));
			Check(*State, Back && Back->GetDoorState() == EDoorLeafState::Closed, TEXT("A: S01 door d20 is a CLOSED door"));
			Check(*State, Locked && Locked->GetDoorState() == EDoorLeafState::Locked, TEXT("A: S03 front door d19 is LOCKED (story 3)"));
			Check(*State, Gable && Gable->GetDoorState() == EDoorLeafState::Closed, TEXT("A: S03 gable door d20 is CLOSED (the quiet way)"));
			if (!Front || !Back || !Locked || !Gable)
			{
				return Finish(*State, false);
			}
			Check(*State, Front->GetWidthCm() > 140.f && Front->GetWidthCm() < 160.f, FString::Printf(TEXT("A: S01 d19 is %.0f cm wide (150, a pair of leaves)"), Front->GetWidthCm()));
			Check(*State, Cast<ADoorActor>(TraceAcross(World, *Front)) == Front, TEXT("A: the closed door blocks sight across its doorway"));
			// Navmesh: through the closed door (priced) yes; through the locked one no.
			bool bCrosses = false;
			double Length = 0.0;
			const float FrontIn = InsideSign(World, *Front);
			FVector From, To;
			const bool bFrontPoints = OnNav(World, Front->GetActorLocation() - Front->GetNormal() * (FrontIn * 250.f), From)
				&& OnNav(World, Front->GetActorLocation() + Front->GetNormal() * (FrontIn * 200.f), To);
			const bool bFrontPath = bFrontPoints && PathThrough(World, *Leader, From, To, *Front, bCrosses, Length);
			Check(*State, bFrontPath && bCrosses, FString::Printf(TEXT("A: a navmesh path runs through the closed S01 door (%.0f cm)"), Length));
			const float LockedIn = InsideSign(World, *Locked);
			const bool bLockedPoints = OnNav(World, Locked->GetActorLocation() - Locked->GetNormal() * (LockedIn * 250.f), From)
				&& OnNav(World, Locked->GetActorLocation() + Locked->GetNormal() * (LockedIn * 200.f), To);
			const bool bLockedPath = bLockedPoints && PathThrough(World, *Leader, From, To, *Locked, bCrosses, Length);
			Check(*State, bLockedPoints && !(bLockedPath && bCrosses), FString::Printf(TEXT("A: no navmesh path through the locked S03 door (%s, %.0f cm)"),
				bLockedPath ? TEXT("the path goes round") : TEXT("no path"), Length));
			// B: Solo, the leader 3 m outside the front door, his click on the door.
			Squad->EnterSoloMode();
			Squad->SetFollowersHolding(true);
			FVector Stand;
			if (!OnNav(World, Front->GetActorLocation() - Front->GetNormal() * (FrontIn * 300.f), Stand))
			{
				Check(*State, false, TEXT("B: a navmesh stand 3 m outside the S01 door"));
				return Finish(*State, false);
			}
			State->InsideSign = FrontIn;
			Teleport(*Leader, Stand, Front->GetNormal() * FrontIn);
			Next(*State, 1);
			return true;
		}
		case 1:
		{
			ADoorActor* Front = State->FrontS01.Get();
			if (State->StageTime < 1.f)
			{
				return true;
			}
			FHitResult Hit(Front, nullptr, Front->GetDoorwayCenter() - Front->GetNormal() * (State->InsideSign * 3.f), -Front->GetNormal() * State->InsideSign);
			Hit.bBlockingHit = true;
			PC->HandlePlayerClick(Hit);
			Check(*State, Doors->HasPendingInteraction(Leader), TEXT("B: the click on the door is a door order (he walks up)"));
			Next(*State, 2);
			return true;
		}
		case 2:
		{
			ADoorActor* Front = State->FrontS01.Get();
			if (!Front->IsOpen() && State->StageTime < 15.f)
			{
				return true;
			}
			Check(*State, Front->IsOpen(), FString::Printf(TEXT("B: the leader opened the S01 door by hand (%.1f s incl. the walk-up + hand_open_s %.1f, %.0f cm from it)"),
				State->StageTime, DoorTuning::Get().HandOpenSeconds, FVector::Dist2D(Feet(*Leader), Front->GetActorLocation())));
			Next(*State, 3); // stage 3 waits 1 s: the swing is over
			return true;
		}
		case 3:
		{
			ADoorActor* Front = State->FrontS01.Get();
			if (State->StageTime < 1.f)
			{
				return true;
			}
			Check(*State, TraceAcross(World, *Front) == nullptr, TEXT("B: the open doorway passes sight"));
			if (Cold && Cold->HasLayout())
			{
				Check(*State, Cold->IsOpeningOpen(Front->GetBuildingId(), Front->GetOpeningId()), TEXT("B: the cold shelter sees the opening open"));
			}
			// C: a click on the floor 20 cm beside the open leaf's middle.
			const FVector LeafPoint = Front->GetLeafFootprintMid(0) + Front->GetTangent() * 20.f + FVector(0.f, 0.f, 2.f);
			const bool bFound = Front->IsNearOpenLeaf(LeafPoint);
			Check(*State, bFound, TEXT("C: a floor point by the open leaf"));
			if (!bFound)
			{
				return Finish(*State, false);
			}
			PC->HandlePlayerClick(FloorHit(LeafPoint));
			Next(*State, 4);
			return true;
		}
		case 4:
		{
			ADoorActor* Front = State->FrontS01.Get();
			if (Front->GetDoorState() != EDoorLeafState::Closed && State->StageTime < 15.f)
			{
				return true;
			}
			Check(*State, Front->GetDoorState() == EDoorLeafState::Closed, FString::Printf(TEXT("C: the click by the open leaf closed it (%.1f s)"), State->StageTime));
			Check(*State, Cast<ADoorActor>(TraceAcross(World, *Front)) == Front, TEXT("C: the shut door blocks sight again"));
			if (Cold && Cold->HasLayout())
			{
				Check(*State, !Cold->IsOpeningOpen(Front->GetBuildingId(), Front->GetOpeningId()), TEXT("C: the cold shelter sees it sealed"));
			}
			// D: the squad in formation outside the other S01 door, ordered 3 m inside.
			ADoorActor* Back = State->BackS01.Get();
			const float BackIn = InsideSign(World, *Back);
			FVector Stand, Inside;
			if (!OnNav(World, Back->GetActorLocation() - Back->GetNormal() * (BackIn * 350.f), Stand)
				|| !OnNav(World, Back->GetActorLocation() + Back->GetNormal() * (BackIn * 250.f), Inside))
			{
				Check(*State, false, TEXT("D: navmesh points outside / inside the S01 d20 door"));
				return Finish(*State, false);
			}
			Squad->SetFollowersHolding(false);
			Squad->ExitSoloMode();
			int32 Slot = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				FVector At = Stand;
				if (Member != Leader)
				{
					++Slot;
					OnNav(World, Stand - Back->GetNormal() * (BackIn * 120.f * Slot) + Back->GetTangent() * ((Slot % 2) ? 60.f : -60.f), At);
				}
				Teleport(*Member, At, Back->GetNormal() * BackIn);
			}
			State->InsideSign = BackIn;
			State->Target = Inside;
			State->AutoOpensBefore = Doors->GetAutoOpenCount();
			Next(*State, 5);
			return true;
		}
		case 5:
		{
			if (State->StageTime < 1.5f)
			{
				return true;
			}
			PC->HandleWorldHit(FloorHit(State->Target));
			Next(*State, 6);
			return true;
		}
		case 6:
		{
			ADoorActor* Back = State->BackS01.Get();
			int32 Inside = 0;
			int32 Living = 0;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				++Living;
				Inside += Back->ToLocal(Feet(*Member)).Y * State->InsideSign > 60.f ? 1 : 0;
			}
			if (Inside < Living && State->StageTime < 45.f)
			{
				return true;
			}
			Check(*State, Back->IsOpen() && Doors->GetAutoOpenCount() > State->AutoOpensBefore,
				FString::Printf(TEXT("D: the closed door opened by itself on the squad's way (auto-opens %d -> %d)"), State->AutoOpensBefore, Doors->GetAutoOpenCount()));
			Check(*State, Inside == Living, FString::Printf(TEXT("D: the squad passed through: %d / %d inside (%.1f s)"), Inside, Living, State->StageTime));
			// E: Solo, the leader outside the locked S03 front door, ordered 2.5 m behind it.
			ADoorActor* Locked = State->LockedS03.Get();
			const float LockedIn = InsideSign(World, *Locked);
			FVector Stand, Target;
			if (!OnNav(World, Locked->GetActorLocation() - Locked->GetNormal() * (LockedIn * 250.f), Stand)
				|| !OnNav(World, Locked->GetActorLocation() + Locked->GetNormal() * (LockedIn * 250.f), Target))
			{
				Check(*State, false, TEXT("E: navmesh points outside / inside the S03 locked door"));
				return Finish(*State, false);
			}
			Squad->EnterSoloMode();
			Squad->SetFollowersHolding(true);
			Teleport(*Leader, Stand, Locked->GetNormal() * LockedIn);
			State->InsideSign = LockedIn;
			State->Target = Target;
			State->bCrossedLocked = false;
			State->LastLockedLocal = Locked->ToLocal(Stand);
			Next(*State, 7);
			return true;
		}
		case 7:
		{
			if (State->StageTime < 1.5f)
			{
				return true;
			}
			PC->HandleWorldHit(FloorHit(State->Target));
			Next(*State, 8);
			return true;
		}
		case 8:
		{
			ADoorActor* Locked = State->LockedS03.Get();
			const FVector2D Local = Locked->ToLocal(Feet(*Leader));
			if ((Local.Y > 0.0) != (State->LastLockedLocal.Y > 0.0) && FMath::Abs(Local.X) < Locked->GetWidthCm() * 0.5f + 20.f)
			{
				State->bCrossedLocked = true;
			}
			State->LastLockedLocal = Local;
			const bool bArrived = FVector::Dist2D(Feet(*Leader), State->Target) < 120.f && !Leader->IsMoving();
			if (!bArrived && State->StageTime < 90.f)
			{
				return true;
			}
			Check(*State, bArrived, FString::Printf(TEXT("E: the leader reached the room behind the locked door (%.1f s)"), State->StageTime));
			Check(*State, !State->bCrossedLocked, TEXT("E: he never walked through the locked door"));
			Check(*State, Locked->GetDoorState() == EDoorLeafState::Locked, TEXT("E: the locked door stayed locked"));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: E: the way round - the gable door %s (auto-opens so far %d)"),
				State->GableS03->IsOpen() ? TEXT("opened on his way") : TEXT("stayed shut (another way in, e.g. a window)"), Doors->GetAutoOpenCount());
			// F: his DOUBLE click on the locked door from inside: the kick - the first attempt holds (forced roll), the second gives and hurts him
			// (the 150 cm entrance is heavy: injury chance kick_injury_heavy).
			State->KicksBefore = Doors->GetKickAttempts();
			State->InjuriesBefore = Doors->GetKickInjuries();
			const UHealthComponent* Health = Leader->FindComponentByClass<UHealthComponent>();
			State->HealthBefore = Health ? Health->GetCurrentHealth() : 0.f;
			Doors->KickRollForTesting = 0.99f;
			Doors->InjuryRollForTesting = 0.99f;
			FHitResult Hit(Locked, nullptr, Locked->GetDoorwayCenter() + Locked->GetNormal() * (State->InsideSign * 3.f), Locked->GetNormal() * State->InsideSign);
			Hit.bBlockingHit = true;
			PC->HandlePlayerClick(Hit);
			PC->HandlePlayerClick(Hit);
			Next(*State, 9);
			return true;
		}
		case 9:
		{
			ADoorActor* Locked = State->LockedS03.Get();
			if (Doors->GetKickAttempts() == State->KicksBefore + 1 && Doors->KickRollForTesting < 0.f && !State->bSecondKickArmed)
			{
				State->bSecondKickArmed = true;
				Doors->KickRollForTesting = 0.f;
				Doors->InjuryRollForTesting = 0.f;
			}
			if (!Locked->IsOpen() && State->StageTime < 25.f)
			{
				return true;
			}
			const UHealthComponent* Health = Leader->FindComponentByClass<UHealthComponent>();
			const int32 Kicks = Doors->GetKickAttempts() - State->KicksBefore;
			Check(*State, Locked->IsOpen() && Locked->WasKicked(), FString::Printf(TEXT("F: the double click kicked the locked door open (%d kicks, %.1f s, kick_s %.1f)"),
				Kicks, State->StageTime, DoorTuning::Get().KickSeconds));
			Check(*State, Kicks == 2, TEXT("F: the first kick held, the second gave"));
			Check(*State, State->StageTime >= 2.f * DoorTuning::Get().KickSeconds - 0.1f, TEXT("F: every kick took its time"));
			// The health loss goes through UHealthComponent::ApplyDirectHealthLoss (an immortal cheat on the map absorbs it: the health may stay).
			Check(*State, Doors->GetKickInjuries() == State->InjuriesBefore + 1,
				FString::Printf(TEXT("F: the bad kick hurt him (one injury; health %.0f -> %.0f)"), State->HealthBefore, Health ? Health->GetCurrentHealth() : 0.f));
			// K: the key - S09's gable door (locked by story 9); the squad gets the building's key, his single click.
			ADoorActor* Gable = State->GableS09.Get();
			const float In = InsideSign(World, *Gable);
			FVector Stand;
			if (!OnNav(World, Gable->GetActorLocation() - Gable->GetNormal() * (In * 300.f), Stand))
			{
				Check(*State, false, TEXT("K: a navmesh stand outside the S09 gable door"));
				return Finish(*State, false);
			}
			Teleport(*Leader, Stand, Gable->GetNormal() * In);
			State->InsideSign = In;
			Next(*State, 10);
			return true;
		}
		case 10:
		{
			ADoorActor* Gable = State->GableS09.Get();
			if (State->StageTime < 1.f)
			{
				return true;
			}
			FHitResult NoKey(Gable, nullptr, Gable->GetDoorwayCenter() - Gable->GetNormal() * (State->InsideSign * 3.f), -Gable->GetNormal() * State->InsideSign);
			NoKey.bBlockingHit = true;
			PC->HandlePlayerClick(NoKey);
			Check(*State, !Doors->HasPendingInteraction(Leader) && Gable->GetDoorState() == EDoorLeafState::Locked, TEXT("K: without the key the click only says how"));
			Doors->AddKey(Gable->GetBuildingId());
			Next(*State, 11);
			return true;
		}
		case 11:
		{
			ADoorActor* Gable = State->GableS09.Get();
			if (State->StageTime < 1.f) // past the double-click window
			{
				return true;
			}
			if (!State->bKeyClicked)
			{
				State->bKeyClicked = true;
				FHitResult Hit(Gable, nullptr, Gable->GetDoorwayCenter() - Gable->GetNormal() * (State->InsideSign * 3.f), -Gable->GetNormal() * State->InsideSign);
				Hit.bBlockingHit = true;
				PC->HandlePlayerClick(Hit);
				State->KeyClickAt = State->StageTime;
				return true;
			}
			if (!Gable->IsOpen() && State->StageTime < 20.f)
			{
				return true;
			}
			Check(*State, Gable->IsOpen() && !Gable->WasKicked() && !Gable->WasLockShot(), FString::Printf(TEXT("K: with the key he unlocked it and opened it by hand (%.1f s)"),
				State->StageTime - State->KeyClickAt));
			// S: S09's front door (locked by story 3): Ctrl + click = he shoots the lock.
			ADoorActor* Front = State->FrontS09.Get();
			const float In = InsideSign(World, *Front);
			FVector Stand;
			if (!OnNav(World, Front->GetActorLocation() - Front->GetNormal() * (In * 300.f), Stand))
			{
				Check(*State, false, TEXT("S: a navmesh stand outside the S09 front door"));
				return Finish(*State, false);
			}
			Teleport(*Leader, Stand, Front->GetNormal() * In);
			State->InsideSign = In;
			Next(*State, 12);
			return true;
		}
		case 12:
		{
			ADoorActor* Front = State->FrontS09.Get();
			if (State->StageTime < 1.5f)
			{
				return true;
			}
			Doors->LockShotRollForTesting = 0.f;
			FHitResult Hit(Front, nullptr, Front->GetLockPoint() - Front->GetNormal() * (State->InsideSign * 3.f), -Front->GetNormal() * State->InsideSign);
			Hit.bBlockingHit = true;
			State->ClipBefore = Leader->CurrentClip;
			PC->SetSimulatedAttackModifier(true);
			PC->HandlePlayerClick(Hit);
			PC->SetSimulatedAttackModifier(false);
			Next(*State, 13);
			return true;
		}
		case 13:
		{
			ADoorActor* Front = State->FrontS09.Get();
			if (!Front->IsOpen() && State->StageTime < 12.f)
			{
				return true;
			}
			Check(*State, Front->IsOpen() && Front->WasLockShot(), FString::Printf(TEXT("S: Ctrl + click shot the lock off (%.1f s, weapon %s, clip %d -> %d)"),
				State->StageTime, Leader->CurrentWeapon ? *Leader->CurrentWeapon->WeaponId : TEXT("none"), State->ClipBefore, Leader->CurrentClip));
			// X: a blast by S04's front door breaks it.
			ADoorActor* Blasted = State->FrontS04.Get();
			const float In = InsideSign(World, *Blasted);
			Explosions::Detonate(World, Explosions::GrenadeSpec(Blasted->GetDoorwayCenter() - Blasted->GetNormal() * (In * 120.f), 0.f, 400.f, TEXT("DoorsSmoke"), nullptr));
			Next(*State, 14);
			return true;
		}
		case 14:
		{
			ADoorActor* Blasted = State->FrontS04.Get();
			if (State->StageTime < 1.f)
			{
				return true;
			}
			Check(*State, Blasted->IsBroken() && Blasted->IsOpen(), TEXT("X: the explosion broke the closed S04 door (doorway open)"));
			Check(*State, TraceAcross(World, *Blasted) == nullptr, TEXT("X: the broken doorway passes sight"));
			Check(*State, !Blasted->CloseDoor(Leader), TEXT("X: a blown-off leaf cannot shut"));
			// G: the switch off.
			SetDoorsSwitch(0);
			Next(*State, 15);
			return true;
		}
		case 15:
		{
			if ((Doors->IsActive() || State->StageTime < 1.f) && State->StageTime < 20.f)
			{
				return true;
			}
			Check(*State, !Doors->IsActive() && Doors->GetDoors().Num() == 0, TEXT("G: the switch off released every door"));
			int32 Leaves = 0;
			int32 Visible = 0;
			for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
			{
				FString StateText;
				if (It->Tags.Contains(FName(TEXT("door_leaf"))) && DoorRules::FindTagValue(It->Tags, TEXT("door_state"), StateText)
					&& (StateText == TEXT("closed") || StateText == TEXT("locked")))
				{
					++Leaves;
					Visible += (!It->IsHidden() || It->Tags.Contains(FName(TEXT("KitDressProxy")))) && It->GetActorEnableCollision() ? 1 : 0;
				}
			}
			Check(*State, Leaves > 0 && Visible == Leaves, FString::Printf(TEXT("G: the builder leaves are back (%d / %d visible with their collision)"), Visible, Leaves));
			return Finish(*State, true);
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
		for (const TCHAR* Name : { TEXT("Codex.Pressure.Enabled"), TEXT("Codex.Pmc.Encounters"), TEXT("Codex.Combat.UnderFireManoeuvre") })
		{
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
			{
				Variable->Set(0, ECVF_SetByConsole);
			}
		}
		SetDoorsSwitch(1);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.DoorsSmoke"),
		TEXT("Dev check (openable doors R1, L_Gen_StoryShowcase; sets Codex.Doors.Openable 1): adoption, a closed door blocks sight / is priced on the navmesh, the ")
		TEXT("locked door is not; the leader's click opens / closes a door; the squad passes a closed door (auto-open); story 3: the locked door sends him ")
		TEXT("round through the gable door; the kick; the switch off restores the decorative leaves. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
