// Dev-only console command: R17-5, the doorway breach in the game (designer 2026-11-17 with R17-3; docs/design/DOORWAY_BREACH.md). G018 runs it on
// L_Gen_StoryShowcase_Kit (L_Gen_Depot_1 is archived).
// (its doorways: the level generator layout, UDoorwaySubsystem):
//   Scripts/smoke_parallel.ps1 -Commands DoorBreachSmoke -ExtraArgs -Multiprocess
// Every click takes the real input path (the camera's ray -> ACodexTacticsPlayerController::HandlePlayerClick). Cases:
//   a  formation, first click into a doorway's opening -> the breach ghost: a silhouette per member (up to the four slots), nobody moves;
//   b  the slow second click                           -> the stack goes to its slots: every man reaches his slot, the jamb men against the wall
//                                                         either side of the opening (outside it, peeking in round the jamb), the centre man
//                                                         back from the threshold;
//   c  a click inside the room                         -> the entry: the jamb men and the centre man end up inside, past the threshold;
//   d  Solo, first + second click on a doorway         -> one silhouette; he goes to the nearest jamb and peeks in.
// Enemies are parked far away. The kit showcase is in doors.json enabled_maps and is PMC-only (076): this smoke checks the doorway
// ghost, so openable doors stay off (a shut leaf would take the click) and the kit PMC squad is removed.

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
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/DoorwaySubsystem.h"
#include "Tactics/DoorwayBreachEntryRules.h"
#include "NavigationSystem.h"

namespace DoorBreachSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float ApproachCm = 600.f;

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		SmokeUtils::FPreloadGate PreloadGate;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TArray<FRuntimeDoorway> Doors;
		int32 DoorIndex = 0;
		FRuntimeDoorway Door;
		FVector Outward = FVector::ForwardVector;
		FVector Stand = FVector::ZeroVector;
		FHitResult ClickHit;
		bool bSolo = false;
		int32 Retries = 0;
		/** The cleared count before the entry, when it rose (stage time), the followers' hold before the stack order. */
		int32 ClearedBefore = 0;
		float ClearedAt = 0.f;
		bool bHoldBefore = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("DoorBreachSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	FVector Feet(const AOperativeCharacter& Operative)
	{
		return Operative.GetActorLocation() - FVector(0.f, 0.f, Operative.GetSimpleCollisionHalfHeight());
	}

	void Teleport(AOperativeCharacter& Operative, const FVector& At, const FVector& Facing)
	{
		if (Operative.bInCover)
		{
			Operative.LeaveCover(TEXT("DoorBreachSmoke teleport"));
		}
		Operative.StopOperative();
		Operative.ClearManualLookPoint();
		Operative.SetStance(EOperativeStance::Standing);
		Operative.TeleportTo(At + FVector(0.f, 0.f, Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.f), Facing.Rotation(), false, true);
	}

	bool OnNav(UWorld* World, const FVector& Point, FVector& Out)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Location;
		if (!Nav || !Nav->ProjectPointToNavigation(Point, Location, FVector(60.f, 60.f, 150.f)) || FVector::Dist2D(Location.Location, Point) > 60.f)
		{
			return false;
		}
		Out = Location.Location;
		return true;
	}

	/** The squad's stand point outside the door and the outward side (the gate's: the stand's side). */
	bool PrepareDoor(UWorld* World, FState& State)
	{
		const FRuntimeDoorway& Door = State.Door;
		FVector Outward = Door.OutwardNormal.GetSafeNormal2D();
		if (Outward.IsNearlyZero())
		{
			Outward = FVector(-Door.WallTangent.Y, Door.WallTangent.X, 0.f).GetSafeNormal();
		}
		for (const float Sign : { 1.f, -1.f })
		{
			FVector Stand;
			FVector Jamb;
			const FVector Side = Door.WallTangent.GetSafeNormal2D() * (Door.WidthCm * 0.5f + 30.f);
			if (OnNav(World, Door.Center + Outward * Sign * ApproachCm, Stand) && OnNav(World, Door.Center + Outward * Sign * 45.f + Side, Jamb)
				&& OnNav(World, Door.Center + Outward * Sign * 45.f - Side, Jamb) && OnNav(World, Door.Center - Outward * Sign * 300.f, Jamb))
			{
				// The entry's two hooks and the centre slot (DoorwayBreachEntryRules). A corner door can put one hook in the wall:
				// that man never leaves his jamb and the room clears short (S01 d20, the engineer, 2 of 3).
				const FVector Chosen = Outward * Sign;
				const FVector In = -Chosen;
				const FVector Right = FVector::CrossProduct(FVector::UpVector, In).GetSafeNormal();
				const float HookSide = Door.WidthCm * 0.5f + DoorwayBreachEntryRules::HookSideCm;
				FVector Hook;
				const bool bEntry = OnNav(World, Door.Center + In * DoorwayBreachEntryRules::HookDepthCm + Right * HookSide, Hook)
					&& OnNav(World, Door.Center + In * DoorwayBreachEntryRules::HookDepthCm - Right * HookSide, Hook)
					&& OnNav(World, Door.Center + In * DoorwayBreachEntryRules::CentreDepthCm, Hook);
				if (!bEntry)
				{
					continue;
				}
				State.Outward = Chosen;
				State.Stand = Stand;
				return Door.bIsGate || Sign > 0.f; // a building door: from its outside only
			}
		}
		return false;
	}

	/** The cursor into the opening: points through it (the room's floor beyond, the threshold, the jamb's face); the first the doorway takes. */
	bool AimIntoOpening(UWorld* World, ACodexTacticsPlayerController& PC, const FState& State, FHitResult& OutHit)
	{
		const FRuntimeDoorway& Door = State.Door;
		const FVector In = -State.Outward;
		const FVector Points[] = { Door.Center + In * 120.f + FVector(0.f, 0.f, 5.f), Door.Center + FVector(0.f, 0.f, 5.f), Door.Center + In * 60.f + FVector(0.f, 0.f, 100.f),
			Door.Center + Door.WallTangent * (Door.WidthCm * 0.5f + 20.f) + State.Outward * 15.f + FVector(0.f, 0.f, 100.f) };
		for (const FVector& Point : Points)
		{
			FHitResult Hit;
			if (MantleSmokeSites::CameraRayHit(World, Point, Hit) && PC.ClassifyClickPreview(Hit) == EClickPreviewKind::Breach)
			{
				// A dense layout: the same ray can sit in a neighbour's portal. Only the doorway under test counts.
				if (PC.GetBreachPreviewDoorwayId() != Door.Id)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("[DoorBreachSmoke] the ray at %s arms %s, not %s"), *Point.ToCompactString(),
						*PC.GetBreachPreviewDoorwayId(), *Door.Id);
					continue;
				}
				OutHit = Hit;
				return true;
			}
		}
		return false;
	}

	bool Step(TSharedRef<FState> State)
	{
		UWorld* World = State->World.Get();
		if (!World)
		{
			return false;
		}
		if (IConsoleVariable* Openable = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Doors.Openable")))
		{
			Openable->Set(0, ECVF_SetByConsole);
		}
		SmokeUtils::StopLevelPmc(World);
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
		if (State->Time > 400.f)
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
		UDoorwaySubsystem* Doors = World->GetSubsystem<UDoorwaySubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		AOperativeCharacter* Leader = State->Leader.Get();
		switch (State->Stage)
		{
		case 0:
		{
			if (!Squad || !Squad->GetLeader() || !PC || !Doors || State->StageTime < 3.f)
			{
				return true;
			}
			State->Leader = Squad->GetLeader();
			Leader = State->Leader.Get();
			const float FeetZ = static_cast<float>(Feet(*Leader).Z);
			for (const FRuntimeDoorway& Door : Doors->GetAllDoorways())
			{
				if (!Door.bIsGate && Door.WidthCm >= 80.f && Door.WidthCm <= 220.f && FMath::Abs(Door.Center.Z - FeetZ) < 80.f)
				{
					State->Doors.Add(Door);
				}
			}
			// Street entries (schema v2 role). An interior door's ray often arms the neighbouring opening (S07 d9 armed d20).
			// A schema v1 layout has no role: every ground doorway stays, as on the depot.
			TArray<FRuntimeDoorway> Entries;
			for (const FRuntimeDoorway& Door : State->Doors)
			{
				if (Door.Role.Equals(TEXT("entry")))
				{
					Entries.Add(Door);
				}
			}
			if (Entries.Num() > 0)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[DoorBreachSmoke] street entries %d of %d ground doorways"), Entries.Num(), State->Doors.Num());
				State->Doors = MoveTemp(Entries);
			}
			const FVector Near = Leader->GetActorLocation();
			State->Doors.Sort([&Near](const FRuntimeDoorway& A, const FRuntimeDoorway& B) { return FVector::DistSquared2D(A.Center, Near) < FVector::DistSquared2D(B.Center, Near); });
			Check(*State, State->Doors.Num() > 0, FString::Printf(TEXT("the map's ground-floor building doorways: %d of %d registered"), State->Doors.Num(), Doors->GetDoorwayCount()));
			if (State->Doors.IsEmpty())
			{
				return Finish(*State, false);
			}
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode();
			}
			Squad->SetFollowersHolding(false);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->bTacticalCeaseFire = true;
			}
			Next(*State, 1);
			return true;
		}
		case 1: // the next doorway with free ground round it; the squad 6 m outside
		{
			while (State->DoorIndex < State->Doors.Num())
			{
				State->Door = State->Doors[State->DoorIndex++];
				if (PrepareDoor(World, *State))
				{
					break;
				}
				State->Door = FRuntimeDoorway();
			}
			if (State->Door.Id.IsEmpty())
			{
				Check(*State, false, TEXT("a doorway with free navmesh at its stand / jambs / room"));
				return Finish(*State, false);
			}
			int32 Index = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const FVector Offset = Member == Leader ? FVector::ZeroVector : State->Door.WallTangent * ((Index % 2 == 0 ? 1.f : -1.f) * 120.f) + State->Outward * (100.f + 60.f * (Index / 2));
				FVector At = State->Stand + Offset;
				OnNav(World, At, At);
				Teleport(*Member, At, -State->Outward);
				Index += Member == Leader ? 0 : 1;
			}
			Next(*State, 2);
			return true;
		}
		case 2: // a: the first click into the opening
		{
			if (State->StageTime < 3.f)
			{
				return true;
			}
			if (!AimIntoOpening(World, *PC, *State, State->ClickHit))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[DoorBreachSmoke] the camera does not see into doorway %s - the next one"), *State->Door.Id);
				Next(*State, ++State->Retries > 16 ? 99 : 1);
				return true;
			}
			// Review 2026-11-17 (finding 2): a click THROUGH the opening onto the room's floor is a plain move through the doorway, no breach ghost.
			if (!State->bSolo)
			{
				FHitResult Floor;
				const FVector In = -State->Outward;
				// The camera seldom sees that floor from outside: the room's floor hit itself (straight down there) - the arming rule reads the hit point.
				const FVector FloorPoint = State->Door.Center + In * 160.f;
				FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(DoorBreachSmokeFloor), false);
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					FloorParams.AddIgnoredActor(Member);
				}
				if (World->LineTraceSingleByChannel(Floor, FloorPoint + FVector(0.f, 0.f, 150.f), FloorPoint - FVector(0.f, 0.f, 150.f), ECC_Visibility, FloorParams)
					&& FMath::Abs(Floor.ImpactNormal.Z) >= 0.7f && FVector::DotProduct(Floor.ImpactPoint - State->Door.Center, In) > 80.0)
				{
					const EClickPreviewKind FloorKind = PC->ClassifyClickPreview(Floor);
					Check(*State, FloorKind != EClickPreviewKind::Breach, FString::Printf(TEXT("a: a click through doorway %s onto the room's floor (%.0f cm in) is no breach ghost (kind %d)"),
						*State->Door.Id, FVector::DotProduct(Floor.ImpactPoint - State->Door.Center, In), static_cast<int32>(FloorKind)));
				}
				else
				{
					UE_LOG(LogCodexTactics, Display, TEXT("[DoorBreachSmoke] the camera does not see doorway %s's room floor (hit %s) - the floor-click check is skipped"),
						*State->Door.Id, *GetNameSafe(Floor.GetActor()));
				}
			}
			PC->HandlePlayerClick(State->ClickHit);
			Next(*State, 3);
			return true;
		}
		case 3:
		{
			if (State->StageTime < 0.6f)
			{
				return true;
			}
			const int32 Members = State->bSolo ? 1 : FMath::Min(Squad->GetMembers().Num(), 4);
			bool bStill = true;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				bStill &= !Member->IsMoving();
			}
			Check(*State, PC->GetClickPreviewKind() == EClickPreviewKind::Breach && PC->GetBreachGhostCount() == Members && bStill, FString::Printf(
				TEXT("%s: the first click into doorway %s (%s, %.0f cm wide) shows the breach ghost - %d silhouettes (%d expected), nobody moves (%d)"), State->bSolo ? TEXT("d") : TEXT("a"),
				*State->Door.Id, *GetNameSafe(State->ClickHit.GetActor()), State->Door.WidthCm, PC->GetBreachGhostCount(), Members, bStill ? 1 : 0));
			State->bHoldBefore = Squad->AreFollowersHolding();
			PC->HandlePlayerClick(State->ClickHit);
			Next(*State, 4);
			return true;
		}
		case 4: // b: the stack
		{
			const bool bStacked = PC->GetBreachPhase() == ACodexTacticsPlayerController::EBreachPhase::Stacked;
			if (!bStacked && State->StageTime < 35.f)
			{
				return true;
			}
			const FRuntimeDoorway& Door = State->Door;
			const FVector Tangent = Door.WallTangent.GetSafeNormal2D();
			int32 Peeking = 0;
			int32 JambOk = 0;
			FString Line;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const FVector P = Feet(*Member);
				const float Out = static_cast<float>(FVector::DotProduct(P - Door.Center, State->Outward));
				const float Lateral = static_cast<float>(FMath::Abs(FVector::DotProduct(P - Door.Center, Tangent)));
				const bool bPeek = Member->IsManualCornerPeek();
				Peeking += bPeek ? 1 : 0;
				// A jamb man: outside the opening (past its edge along the wall), against the wall (within 1.2 m of the threshold line).
				JambOk += bPeek && Lateral >= Door.WidthCm * 0.5f && Out >= 0.f && Out <= 120.f ? 1 : 0;
				Line += FString::Printf(TEXT(" %s out %.0f lateral %.0f peek %d;"), *Member->DisplayName.ToString(), Out, Lateral, bPeek ? 1 : 0);
			}
			const int32 Jambs = State->bSolo ? 1 : FMath::Min(PC->GetBreachParticipantCount(), 2);
			Check(*State, bStacked && PC->GetBreachArrivedCount() == PC->GetBreachParticipantCount() && JambOk >= Jambs, FString::Printf(
				TEXT("%s: the second click - every man reaches his breach slot (%d of %d after %.1f s; phase %d), %d jamb men against the wall either side of the opening ")
				TEXT("peeking in (%d peeking):%s"), State->bSolo ? TEXT("d") : TEXT("b"), PC->GetBreachArrivedCount(), PC->GetBreachParticipantCount(), State->StageTime,
				static_cast<int32>(PC->GetBreachPhase()), JambOk, Peeking, *Line));
			if (State->bSolo)
			{
				return Finish(*State, true);
			}
			// c: a click inside the room - where the camera sees its floor (through the opening; a roof's cutaway).
			FHitResult Inside;
			State->ClearedBefore = PC->GetBreachClearedCount();
			State->ClearedAt = 0.f;
			for (const float Depth : { 120.f, 200.f, 300.f, 80.f })
			{
				if (MantleSmokeSites::CameraRayHit(World, Door.Center - State->Outward * Depth + FVector(0.f, 0.f, 5.f), Inside))
				{
					PC->HandlePlayerClick(Inside);
					if (PC->GetBreachPhase() == ACodexTacticsPlayerController::EBreachPhase::Entering)
					{
						break;
					}
					PC->CancelClickPreview(TEXT("DoorBreachSmoke: not inside"));
				}
			}
			Check(*State, PC->GetBreachPhase() == ACodexTacticsPlayerController::EBreachPhase::Entering, FString::Printf(
				TEXT("c: a click inside the room (%s at %s) starts the entry (phase %d)"), *GetNameSafe(Inside.GetActor()), *Inside.ImpactPoint.ToCompactString(),
				static_cast<int32>(PC->GetBreachPhase())));
			Next(*State, 5);
			return true;
		}
		case 5:
		{
			// The cleared room ends the session at once (review 2026-11-17: it restores the holds / sector looks it set): the cleared count.
			const bool bCleared = PC->GetBreachClearedCount() > State->ClearedBefore;
			if (!bCleared && State->StageTime < 25.f)
			{
				return true;
			}
			if (bCleared)
			{
				if (State->ClearedAt <= 0.f)
				{
					State->ClearedAt = State->StageTime;
				}
				if (State->StageTime - State->ClearedAt < 0.3f)
				{
					return true; // a few frames for the end of the session
				}
			}
			Check(*State, PC->GetBreachPhase() == ACodexTacticsPlayerController::EBreachPhase::None && Squad->AreFollowersHolding() == State->bHoldBefore,
				FString::Printf(TEXT("c: the cleared room ends the session and restores the followers' hold (phase %d, holding %d, before %d)"),
					static_cast<int32>(PC->GetBreachPhase()), Squad->AreFollowersHolding() ? 1 : 0, State->bHoldBefore ? 1 : 0));
			int32 Inside = 0;
			FString Line;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const float Out = static_cast<float>(FVector::DotProduct(Feet(*Member) - State->Door.Center, State->Outward));
				Inside += Out < -40.f ? 1 : 0;
				Line += FString::Printf(TEXT(" %s %.0f cm %s;"), *Member->DisplayName.ToString(), FMath::Abs(Out), Out < 0.f ? TEXT("inside") : TEXT("outside"));
			}
			const int32 Expected = FMath::Min(PC->GetLastBreachParticipantCount(), 3);
			Check(*State, bCleared && Inside >= Expected, FString::Printf(TEXT("c: the entry - %d men past the threshold (%d expected; cleared %d after %.1f s):%s"), Inside, Expected,
				bCleared ? 1 : 0, State->StageTime, *Line));
			// d: Solo, one operative.
			State->bSolo = true;
			Squad->EnterSoloMode();
			Squad->SetFollowersHolding(true);
			Teleport(*Leader, State->Stand, -State->Outward);
			Next(*State, 6);
			return true;
		}
		case 6:
		{
			if (State->StageTime < 3.f)
			{
				return true;
			}
			PC->CancelClickPreview(TEXT("smoke"));
			if (!AimIntoOpening(World, *PC, *State, State->ClickHit))
			{
				Check(*State, false, TEXT("d: the camera sees into the doorway again"));
				return Finish(*State, false);
			}
			PC->HandlePlayerClick(State->ClickHit);
			Next(*State, 3);
			return true;
		}
		case 99:
			Check(*State, false, TEXT("a doorway the camera sees into"));
			return Finish(*State, false);
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
		TEXT("CodexTactics.DoorBreachSmoke"),
		TEXT("Dev check (R17-5 doorway breach, L_Gen_StoryShowcase_Kit): a first click into a doorway shows the squad's breach ghost, the second click stacks the squad on ")
		TEXT("its slots (jamb men peeking in), a click inside the room is the entry (the men end inside); Solo: one man at the nearest jamb; PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
