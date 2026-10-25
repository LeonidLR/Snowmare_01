// Dev-only console command for a headless check of the group cover order (game designer 2026-10-20) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.GroupCoverSmoke
// Everything on its own empty ground (SmokeUtils::FindClearArena), walls spawned at runtime; every order goes through the
// controller's cover confirm (ShowCoverPreview + ConfirmCoverPreview = the second click on the cover ghost):
//  A group mode (Solo off, exploring), an 8 m high wall: all three run into cover on the same side, the leader at the clicked
//    slot, a slot spacing apart (no overlap), the others hold their exact slots; 2 s later they still hold them;
//  C Solo + a box selection of two: both into cover, the third stays where he is;
//  D Solo, only the leader selected: only he takes cover (unchanged single order);
//  B group mode, a 1.5 m low barricade: the leader in cover, the two others prone side by side right behind it, facing it, not
//    overlapping, hidden from an eye 10 m beyond it (the Sprint 08 trace eye 160 -> prone profile 25 cm); they hold while the
//    leader stays; the leader walks 8 m away -> they get up and follow the formation;
//  P a wave fight, tactical pause, all three box-selected, the barricade: the order is only planned (3 planned moves: one slot,
//    two prone); the release -> leader in cover, the two prone behind it.
//  W (2026-10-25, Codex.Formation.Sectors 1, first): group mode = the formation wall layout (FORMATION_SECTORS.md "Wall"): all three
//    crouched along the wall 1 m apart, backs to it, facing out, not in the cover pose; the leader walks on -> they stand up and follow.
//    A .. P then run on the cover-slot order (Codex.Formation.Sectors 0: the console-off behaviour, which a box selection keeps anyway).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/GroupCoverRules.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTypes.h"

namespace GroupCoverSmoke
{
	constexpr float StepSeconds = 0.1f;
	constexpr float WallDistanceCm = 600.f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		FCoverSlot Slot;
		TArray<bool> SawSprint;
		TArray<bool> SawMove;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	/**
	 * Formation sectors (2026-10-25): the cover-slot group order (slots shoulder to shoulder, prone behind, leader priority) runs for a box
	 * selection and with Codex.Formation.Sectors 0; in group mode with the console on the order is the formation wall layout
	 * (FORMATION_SECTORS.md "Wall", FormationLabSmoke). bOld saves the console's value and sets 0, false restores it.
	 */
	void UseCoverSlotOrder(bool bOld)
	{
		static int32 Saved = -1;
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Formation.Sectors"));
		if (!CVar)
		{
			return;
		}
		if (bOld && Saved < 0)
		{
			Saved = CVar->GetInt();
			CVar->Set(0, ECVF_SetByConsole);
		}
		else if (!bOld && Saved >= 0)
		{
			CVar->Set(Saved, ECVF_SetByConsole);
			Saved = -1;
		}
	}

	bool Finish(FState& State, bool bComplete)
	{
		UseCoverSlotOrder(false);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		if (State.Wall.IsValid())
		{
			State.Wall->Destroy();
		}
		FPlatformMisc::RequestExit(false, TEXT("GroupCoverSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	AOperativeCharacter* Member(const FState& State, int32 Index)
	{
		return State.Members.IsValidIndex(Index) ? State.Members[Index].Get() : nullptr;
	}

	/** A box wall LengthCm long (along R), HeightCm high, 40 cm thick, its near face WallDistanceCm ahead of the arena origin. */
	void SpawnWall(UWorld* World, FState& State, float LengthCm, float HeightCm)
	{
		if (State.Wall.IsValid())
		{
			State.Wall->Destroy();
		}
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FVector Centre = State.P + State.F * (WallDistanceCm + 20.f);
		const FTransform Transform(State.F.Rotation(), FVector(Centre.X, Centre.Y, State.GroundZ + HeightCm * 0.5f), FVector(0.4f, LengthCm / 100.f, HeightCm / 100.f));
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (Block)
		{
			Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
			Block->FinishSpawning(Transform);
		}
		State.Wall = Block;
	}

	/** Everyone out of cover, standing, back at the start (leader at the origin, the others 2.5 m behind, 2 m to the sides). */
	void ResetSquad(USquadSubsystem* Squad, FState& State)
	{
		const FVector Offsets[] = { FVector::ZeroVector, -State.F * 250.f - State.R * 200.f, -State.F * 250.f + State.R * 200.f };
		for (int32 Index = 0; Index < State.Members.Num(); ++Index)
		{
			AOperativeCharacter* Each = Member(State, Index);
			if (!Each)
			{
				continue;
			}
			Each->LeaveCover(TEXT("smoke reset"));
			Each->ClearPendingCover();
			Each->StopOperative();
			Each->SetStance(EOperativeStance::Standing);
			Each->TeleportTo(State.P + Offsets[FMath::Min(Index, 2)] + FVector(0.f, 0.f, Each->GetSimpleCollisionHalfHeight() + 5.f), State.F.Rotation(), false, true);
		}
		Squad->SetLeader(Member(State, 0));
		State.SawSprint.Init(false, State.Members.Num());
		State.SawMove.Init(false, State.Members.Num());
	}

	/** The slot the player's click on the middle of the wall's near face gives. */
	bool FindMiddleSlot(UWorld* World, FState& State, float ClickHeightCm)
	{
		const FVector Face = State.P + State.F * WallDistanceCm;
		return CoverTraceRules::FindCoverSlotAt(World, FVector(Face.X, Face.Y, State.GroundZ + ClickHeightCm), State.F, State.Slot);
	}

	void TrackRun(FState& State)
	{
		for (int32 Index = 0; Index < State.Members.Num(); ++Index)
		{
			if (const AOperativeCharacter* Each = Member(State, Index); Each && Each->GetVelocity().Size2D() > 50.f)
			{
				State.SawMove[Index] = true;
				State.SawSprint[Index] |= Each->IsSprinting();
			}
		}
	}

	float Along(const FState& State, const FVector& Point)
	{
		return static_cast<float>(FVector::DotProduct(Point - State.Slot.WorldLocation, State.R));
	}

	/** Off the wall's near face towards the squad's side, cm. */
	float Depth(const FState& State, const FVector& Point)
	{
		return static_cast<float>(FVector::DotProduct((State.P + State.F * WallDistanceCm) - Point, State.F));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 200.f)
		{
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* A = Member(State, 0);
		AOperativeCharacter* B = Member(State, 1);
		AOperativeCharacter* C = Member(State, 2);
		const FGroupCoverConfig Config;
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f || !Squad || !PC || Squad->GetMembers().Num() < 3)
			{
				return true;
			}
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& X, const AOperativeCharacter& Y) { return X.SquadIndex < Y.SquadIndex; });
			TArray<const AActor*> Ignore;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				State.Members.Add(Roster[Index]);
				Ignore.Add(Roster[Index]);
			}
			Ignore.Add(PC->GetPawn());
			const FVector Feet = Roster[0]->GetActorLocation() - FVector(0.f, 0.f, Roster[0]->GetSimpleCollisionHalfHeight());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Roster[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-1000.f, -700.f), FVector2D(900.f, 700.f)), Ignore);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: arena %s at %s facing %s (%d tried)"), Arena.bFound ? TEXT("found") : TEXT("NOT found"),
				*Arena.Origin.ToCompactString(), *Arena.Forward.ToCompactString(), Arena.Tried);
			if (!Arena.bFound)
			{
				Check(State, false, TEXT("an empty, flat, navigable 19 x 14 m spot for the smoke's own set-up"));
				return Finish(State, false);
			}
			State.P = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			State.GroundZ = Arena.Origin.Z;
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode(false, 0.f);
			}
			ResetSquad(Squad, State);
			SpawnWall(World, State, 800.f, 300.f);
			Check(State, State.Wall.IsValid(), TEXT("A: an 8 m high wall 6 m ahead"));
			// Formation sectors on: scene W first (the group mode order is the wall layout), then A.. on the cover-slot order.
			Next(State, FormationRuntime::IsEnabled() ? 100 : 1);
			return true;
		}
		// --- W: group mode with Codex.Formation.Sectors 1 = the formation wall layout (FORMATION_SECTORS.md "Wall") ------------------
		case 100:
		{
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			if (!FindMiddleSlot(World, State, 90.f))
			{
				Check(State, false, TEXT("W: a cover slot in the middle of the wall"));
				return Finish(State, false);
			}
			PC->ShowCoverPreview(A, State.Slot);
			PC->ConfirmCoverPreview();
			Check(State, Squad->HasFormationWallOrder(), TEXT("W: group mode, sectors on - the group cover order is the formation wall layout"));
			Next(State, 101);
			return true;
		}
		case 101:
		{
			const bool bAll = A->IsInFormationWallPose() && B->IsInFormationWallPose() && C->IsInFormationWallPose();
			if (!bAll && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, bAll, FString::Printf(TEXT("W: all three crouched at the wall facing out (%d %d %d) after %.1f s"), A->IsInFormationWallPose(),
				B->IsInFormationWallPose(), C->IsInFormationWallPose(), State.StageTime));
			const AOperativeCharacter* Three[] = { A, B, C };
			float MinGap = 1.0e9f;
			for (int32 I = 0; I < 3; ++I)
			{
				const float Facing = static_cast<float>(Three[I]->GetActorRotation().Yaw);
				const float Out = static_cast<float>((-State.F).Rotation().Yaw);
				Check(State, !Three[I]->bInCover && Three[I]->GetStance() == EOperativeStance::Crouching && FormationMath::AngleBetween(Facing, Out) < 90.f
					&& Depth(State, Three[I]->GetActorLocation()) < 140.f,
					FString::Printf(TEXT("W: %s along the wall (%.0f cm off its face), crouched, back to it, facing out (facing %.0f, out %.0f), not in the cover pose"),
						*Three[I]->DisplayName.ToString(), Depth(State, Three[I]->GetActorLocation()), Facing, Out));
				for (int32 J = I + 1; J < 3; ++J)
				{
					MinGap = FMath::Min(MinGap, static_cast<float>(FVector::Dist2D(Three[I]->GetActorLocation(), Three[J]->GetActorLocation())));
				}
			}
			Check(State, MinGap >= 80.f, FString::Printf(TEXT("W: 1 m apart (closest two %.0f cm)"), MinGap));
			A->OrderMoveTo(State.P - State.F * 400.f, false);
			Next(State, 102);
			return true;
		}
		case 102:
		{
			if (State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, !Squad->HasFormationWallOrder() && !B->IsInFormationWallPose() && !C->IsInFormationWallPose()
				&& B->GetStance() == EOperativeStance::Standing && C->GetStance() == EOperativeStance::Standing,
				TEXT("W: the leader walks on - the wall order is over, the others stand up and follow"));
			// A.. check the cover-slot order (a box selection / the console off) on the old rule.
			UseCoverSlotOrder(true);
			ResetSquad(Squad, State);
			Next(State, 1);
			return true;
		}
		// --- A: group mode, long high wall -------------------------------------------------------------------------------
		case 1:
		{
			if (State.StageTime < 2.5f)
			{
				return true; // the navmesh rebuilds round the wall, the followers settle
			}
			if (!FindMiddleSlot(World, State, 90.f) || State.Slot.Height != ECoverHeight::HighCover)
			{
				Check(State, false, TEXT("A: a high cover slot in the middle of the wall"));
				return Finish(State, false);
			}
			const TArray<AOperativeCharacter*> Joining = Squad->GetGroupCoverMembers(A);
			Check(State, Joining.Num() == 2 && !Squad->IsSoloMode(), FString::Printf(TEXT("A: group mode - the two followers go along (%d)"), Joining.Num()));
			PC->ShowCoverPreview(A, State.Slot);
			PC->ConfirmCoverPreview();
			Check(State, PC->GetLastGroupCoverSlotCount() == 3 && PC->GetLastGroupCoverProneCount() == 0,
				FString::Printf(TEXT("A: the long wall has a slot for each (%d in slots, %d prone)"), PC->GetLastGroupCoverSlotCount(), PC->GetLastGroupCoverProneCount()));
			Next(State, 2);
			return true;
		}
		case 2:
		{
			TrackRun(State);
			const bool bAll = A->bInCover && B->bInCover && C->bInCover;
			if (!bAll && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, bAll, FString::Printf(TEXT("A: all three in cover (%d %d %d) after %.1f s"), A->bInCover, B->bInCover, C->bInCover, State.StageTime));
			const AOperativeCharacter* Three[] = { A, B, C };
			bool bSameWall = true;
			bool bSameSide = true;
			float MinGap = 1.0e9f;
			for (int32 I = 0; I < 3; ++I)
			{
				const FCoverSlot& Each = Three[I]->GetCoverSlot();
				bSameWall &= Each.WallActor.Get() == State.Wall.Get();
				bSameSide &= FVector::DotProduct(Each.WallNormal, -State.F) > 0.9f;
				for (int32 J = I + 1; J < 3; ++J)
				{
					MinGap = FMath::Min(MinGap, static_cast<float>(FVector::Dist2D(Three[I]->GetActorLocation(), Three[J]->GetActorLocation())));
				}
			}
			Check(State, bSameWall && bSameSide, TEXT("A: the same wall, the same (the squad's) side"));
			Check(State, MinGap >= Config.SlotSpacingCm - 15.f, FString::Printf(TEXT("A: shoulder to shoulder, no overlap (closest two %.0f cm apart)"), MinGap));
			const float LeaderOff = FVector::Dist2D(A->GetActorLocation(), State.Slot.WorldLocation);
			Check(State, LeaderOff < 40.f, FString::Printf(TEXT("A: the leader (direct order) at the clicked slot (%.0f cm off)"), LeaderOff));
			Check(State, B->IsHoldingGroupCoverSlot() && C->IsHoldingGroupCoverSlot() && A->IsHoldingGroupCoverSlot(),
				TEXT("A: everyone holds his exact group slot (no walk to a corner)"));
			Check(State, State.SawSprint[0] && State.SawSprint[1] && State.SawSprint[2], FString::Printf(TEXT("A: they ran (sprint %d %d %d) - a cover order is a run"),
				State.SawSprint[0], State.SawSprint[1], State.SawSprint[2]));
			Next(State, 3);
			return true;
		}
		case 3:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, A->bInCover && B->bInCover && C->bInCover && B->IsHoldingGroupCoverSlot() && C->IsHoldingGroupCoverSlot(),
				TEXT("A: 2 s later all still at their slots (the formation does not pull them)"));
			// C: Solo, a box selection of two.
			ResetSquad(Squad, State);
			Squad->EnterSoloMode();
			Squad->SetSelectedGroup({ A, B });
			Next(State, 4);
			return true;
		}
		// --- C: Solo + box selection of two --------------------------------------------------------------------------------
		case 4:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, Squad->IsSoloMode() && Squad->HasMultiSelection() && Squad->GetLeader() == A, TEXT("C: Solo, A + B box-selected, A leads"));
			const TArray<AOperativeCharacter*> Joining = Squad->GetGroupCoverMembers(A);
			Check(State, Joining.Num() == 1 && Joining[0] == B, FString::Printf(TEXT("C: only the selected B goes along (%d)"), Joining.Num()));
			State.Members[2]->StopOperative();
			PC->ShowCoverPreview(A, State.Slot);
			PC->ConfirmCoverPreview();
			Next(State, 5);
			return true;
		}
		case 5:
		{
			const bool bBoth = A->bInCover && B->bInCover;
			if (!bBoth && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, bBoth, TEXT("C: both selected in cover"));
			Check(State, !C->bInCover && !C->HasPendingCover() && !Squad->HasGroupCoverProne(C), TEXT("C: the unselected third stays out of it"));
			Check(State, FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()) >= Config.SlotSpacingCm - 15.f, TEXT("C: the two a slot apart"));
			// D: Solo, only the leader.
			ResetSquad(Squad, State);
			Squad->SetLeader(A); // drops the box selection
			Next(State, 6);
			return true;
		}
		// --- D: Solo, single selection -------------------------------------------------------------------------------------
		case 6:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, Squad->IsSoloMode() && !Squad->HasMultiSelection() && Squad->GetGroupCoverMembers(A).IsEmpty(),
				TEXT("D: Solo, only A selected: nobody goes along"));
			PC->ShowCoverPreview(A, State.Slot);
			PC->ConfirmCoverPreview();
			Next(State, 7);
			return true;
		}
		case 7:
		{
			TrackRun(State);
			if (!A->bInCover && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, A->bInCover && !A->IsHoldingGroupCoverSlot(), TEXT("D: A alone in cover (the single order, no group slot)"));
			Check(State, !B->bInCover && !C->bInCover && !B->HasPendingCover() && !C->HasPendingCover() && !State.SawMove[1] && !State.SawMove[2],
				TEXT("D: the other two did not move"));
			// B: group mode, the short low barricade.
			Squad->ExitSoloMode(false, 0.f);
			ResetSquad(Squad, State);
			SpawnWall(World, State, 150.f, 60.f);
			Next(State, 8);
			return true;
		}
		// --- B: group mode, 1.5 m low barricade ----------------------------------------------------------------------------
		case 8:
		{
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			if (!FindMiddleSlot(World, State, 40.f) || State.Slot.Height != ECoverHeight::LowCover)
			{
				Check(State, false, TEXT("B: a low cover slot in the middle of the barricade"));
				return Finish(State, false);
			}
			Check(State, Squad->GetGroupCoverMembers(A).Num() == 2, TEXT("B: group mode again - both followers go along"));
			PC->ShowCoverPreview(A, State.Slot);
			PC->ConfirmCoverPreview();
			Check(State, PC->GetLastGroupCoverSlotCount() == 1 && PC->GetLastGroupCoverProneCount() == 2,
				FString::Printf(TEXT("B: too short - the leader at it, two prone behind it (%d / %d)"), PC->GetLastGroupCoverSlotCount(), PC->GetLastGroupCoverProneCount()));
			Next(State, 9);
			return true;
		}
		case 9:
		{
			TrackRun(State);
			const bool bDone = A->bInCover && Squad->HasGroupCoverProne(B, true) && Squad->HasGroupCoverProne(C, true)
				&& B->GetStance() == EOperativeStance::Prone && C->GetStance() == EOperativeStance::Prone && B->GetMoveHoldSeconds() <= 0.f
				&& C->GetMoveHoldSeconds() <= 0.f;
			if (!bDone && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, bDone, FString::Printf(TEXT("B: leader in cover (%d), B / C prone at their spots (%d %d)"), A->bInCover,
				Squad->HasGroupCoverProne(B, true), Squad->HasGroupCoverProne(C, true)));
			const float Half = 75.f;
			for (const AOperativeCharacter* Each : { B, C })
			{
				const float D = Depth(State, Each->GetActorLocation());
				const float L = Along(State, Each->GetActorLocation());
				FVector Spot = FVector::ZeroVector;
				const float ToSpot = Squad->GetGroupCoverProneSpot(Each, Spot) ? static_cast<float>(FVector::Dist2D(Each->GetActorLocation(), Spot)) : 1.0e9f;
				Check(State, D > 100.f && D < 250.f && FMath::Abs(L) <= Half + 10.f && ToSpot <= 45.f,
					FString::Printf(TEXT("B: %s right behind the barricade (%.0f cm off it, %.0f along, %.0f cm off his spot)"), *Each->DisplayName.ToString(), D, L, ToSpot));
				const float Yaw = FMath::Abs(FMath::FindDeltaAngleDegrees(Each->GetActorRotation().Yaw, State.F.Rotation().Yaw));
				Check(State, Yaw <= 35.f, FString::Printf(TEXT("B: %s faces the cover (%.0f deg off)"), *Each->DisplayName.ToString(), Yaw));
				Check(State, FVector::Dist2D(Each->GetActorLocation(), A->GetActorLocation()) >= 90.f, FString::Printf(TEXT("B: %s clear of the leader at the wall"),
					*Each->DisplayName.ToString()));
			}
			const float Gap = FVector::Dist2D(B->GetActorLocation(), C->GetActorLocation());
			Check(State, Gap >= 55.f, FString::Printf(TEXT("B: the two prone ones side by side, not overlapping (%.0f cm)"), Gap));
			Check(State, State.SawSprint[1] && State.SawSprint[2], TEXT("B: the prone ones ran to their spots"));
			Next(State, 10);
			return true;
		}
		case 10:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, A->bInCover && Squad->HasGroupCoverProne(B, true) && Squad->HasGroupCoverProne(C, true)
				&& B->GetStance() == EOperativeStance::Prone && C->GetStance() == EOperativeStance::Prone, TEXT("B: 2 s later they still lie there"));
			// Hidden from an enemy 10 m beyond the barricade: the Sprint 08 trace (eye 160 cm -> the prone profile), pawns ignored.
			FCollisionQueryParams Params(SCENE_QUERY_STAT(GroupCoverSmokeSight), false);
			for (TActorIterator<APawn> It(World); It; ++It)
			{
				Params.AddIgnoredActor(*It);
			}
			const FVector Eye = State.P + State.F * (WallDistanceCm + 40.f + 1000.f) + FVector(0.f, 0.f, 160.f); // the origin lies on the ground: eye height 160 cm
			for (const AOperativeCharacter* Each : { B, C })
			{
				FHitResult Hit;
				const bool bBlocked = World->LineTraceSingleByChannel(Hit, Eye, UTacticalSightSubsystem::ProfilePoint(*Each), ECC_Visibility, Params);
				Check(State, bBlocked && Hit.GetActor() == State.Wall.Get(), FString::Printf(TEXT("B: %s prone behind it is hidden from the far side (%s)"),
					*Each->DisplayName.ToString(), Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("nothing")));
			}
			// The player moves the group again: the leader walks 8 m back -> they get up and follow.
			A->OrderMoveTo(State.P - State.F * 800.f, false);
			Next(State, 11);
			return true;
		}
		case 11:
		{
			const bool bFollow = !Squad->HasGroupCoverProne(B) && !Squad->HasGroupCoverProne(C) && B->GetStance() != EOperativeStance::Prone
				&& C->GetStance() != EOperativeStance::Prone && FVector::Dist2D(B->GetActorLocation(), A->GetActorLocation()) < 600.f
				&& FVector::Dist2D(C->GetActorLocation(), A->GetActorLocation()) < 600.f;
			if (!bFollow && State.StageTime < 20.f)
			{
				return true;
			}
			Check(State, bFollow, FString::Printf(TEXT("B: the leader walked away - both got up and rejoined the formation (%.0f / %.0f cm)"),
				FVector::Dist2D(B->GetActorLocation(), A->GetActorLocation()), FVector::Dist2D(C->GetActorLocation(), A->GetActorLocation())));
			// P: the wave fight, tactical pause, all three box-selected.
			ResetSquad(Squad, State);
			SmokeUtils::UsePreparedWaveStart();
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				It->SetActorTickEnabled(false);
			}
			Squad->SetSelectedGroup({ A, B, C });
			Squad->SetLeader(A, false);
			Check(State, Flow->ToggleTacticalPause() == EGameFlowResult::Ok && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause,
				TEXT("P: tactical pause in the fight"));
			Next(State, 12);
			return true;
		}
		// --- P: tactical pause, box selection of three ---------------------------------------------------------------------
		case 12:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, Squad->HasMultiSelection() && Squad->GetGroupCoverMembers(A).Num() == 2, TEXT("P: the fight - the box selection goes along"));
			PC->ShowCoverPreview(A, State.Slot);
			PC->ConfirmCoverPreview();
			Check(State, Squad->GetPlannedOrderCount() == 3 && A->HasPendingCover() && Squad->HasGroupCoverProne(B) && Squad->HasGroupCoverProne(C)
				&& !Squad->HasGroupCoverProne(B, true), FString::Printf(TEXT("P: only planned (%d planned moves: the slot pending, two prone walks)"), Squad->GetPlannedOrderCount()));
			Check(State, !A->IsMoving() && !B->IsMoving() && !C->IsMoving(), TEXT("P: nobody moves during the pause"));
			Flow->ToggleTacticalPause();
			Next(State, 13);
			return true;
		}
		case 13:
		{
			const bool bDone = A->bInCover && Squad->HasGroupCoverProne(B, true) && Squad->HasGroupCoverProne(C, true)
				&& B->GetStance() == EOperativeStance::Prone && C->GetStance() == EOperativeStance::Prone;
			if (!bDone && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, bDone, FString::Printf(TEXT("P: after the release - leader in cover (%d), two prone behind it (%d %d)"), A->bInCover,
				Squad->HasGroupCoverProne(B, true), Squad->HasGroupCoverProne(C, true)));
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
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.GroupCoverSmoke"),
		TEXT("Dev check: group cover order - long wall (all in cover), short barricade (prone overflow), box-select / Solo, pause; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
