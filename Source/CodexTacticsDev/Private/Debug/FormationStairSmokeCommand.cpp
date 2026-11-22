// Dev-only check of the FORMATION climbing a building's stairs (designer 2026-11-22: "they get stuck on the stairwell too" - the walk smoke
// walks the stairs with the leader only). On a generated map (dressed /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit or the grey showcase) for
// every building of the storey index with 2+ storeys: the squad in formation (not Solo) is put on storey 0 by a stair flight, then the
// player's own click (HandlePlayerClick) on the navmesh floor of the storey above, as far from the stairs as the building allows; within
// 40 s every member must stand on that storey within 8 m of the leader. A member who is not is logged with where he stands and whether he
// is still trying to move (STUCK) or stopped. Then storey 1 -> 2 where the building has it. PASS / FAIL, then exits.
//   UnrealEditor.exe <uproject> /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit -game -nullrhi -nosound -LevelJson=L_Gen_StoryShowcase_Kit.json
//     -ExecCmds="CodexTactics.FormationStairSmoke"
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Camera/RoofCutawaySubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

namespace FormationStairSmoke
{
	constexpr int32 WarmupFrames = 150;
	constexpr int32 SettleFrames = 60;
	constexpr float LegSeconds = 40.f;
	constexpr float TogetherCm = 800.f;

	struct FCase
	{
		FString Building;
		int32 From = 0;
		float FromZ = 0.f;
		float ToZ = 0.f;
		FVector Start = FVector::ZeroVector;
		FVector Goal = FVector::ZeroVector;
	};

	enum class EStep : uint8 { Warmup, Plan, Place, Walk, Done };

	struct FRunner
	{
		EStep Step = EStep::Warmup;
		int32 StepFrame = 0;
		int32 Failures = 0;
		int32 Passed = 0;
		TArray<FCase> Cases;
		int32 CaseIndex = 0;
		FDelegateHandle EndFrameHandle;
	};

	TSharedPtr<FRunner> Runner;

	void Check(bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		Runner->Failures += bOk ? 0 : 1;
		Runner->Passed += bOk ? 1 : 0;
	}

	void Finish()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s FormationStairSmoke: %d / %d stair legs"), Runner->Failures == 0 ? TEXT("PASS") : TEXT("FAIL"),
			Runner->Passed, Runner->Passed + Runner->Failures);
		FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		Runner->Step = EStep::Done;
		FPlatformMisc::RequestExit(false, TEXT("FormationStairSmoke"));
	}

	void Go(EStep Step)
	{
		Runner->Step = Step;
		Runner->StepFrame = 0;
	}

	float FeetZ(const AOperativeCharacter& Member)
	{
		return static_cast<float>(Member.GetActorLocation().Z - Member.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	bool IsStair(const UPrimitiveComponent& Piece)
	{
		const auto IsStairTag = [](const FName& Tag) { return Tag.ToString().StartsWith(TEXT("stair="), ESearchCase::CaseSensitive); };
		const AActor* Owner = Piece.GetOwner();
		return Piece.ComponentTags.ContainsByPredicate(IsStairTag) || (Owner && Owner->Tags.ContainsByPredicate(IsStairTag));
	}

	/** One case per building and storey pair: the start on storey From by a stair flight of it, the goal on From + 1 farthest from it. */
	void PlanCases(UWorld& World, URoofCutawaySubsystem& Roofs)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		Roofs.RebuildStoreyIndex();
		for (const TPair<FString, int32>& Building : Roofs.GetStoreyBuildings())
		{
			for (int32 From = 0; From + 1 < Building.Value - 1 && From < 2; ++From)
			{
				float FromZ = 0.f;
				float ToZ = 0.f;
				if (!Nav || !Roofs.GetStoreyBaseZ(Building.Key, From, FromZ) || !Roofs.GetStoreyBaseZ(Building.Key, From + 1, ToZ))
				{
					continue;
				}
				FVector StairAt = FVector::ZeroVector;
				bool bStair = false;
				for (const TPair<UPrimitiveComponent*, int32>& Piece : Roofs.GetStoreyPieces(Building.Key))
				{
					if (Piece.Value == From && Piece.Key && IsStair(*Piece.Key) && Piece.Key->Bounds.GetBox().GetSize().Z > 60.f)
					{
						StairAt = Piece.Key->Bounds.GetBox().GetCenter();
						bStair = true;
						break;
					}
				}
				FNavLocation Start;
				if (!bStair || !Nav->ProjectPointToNavigation(FVector(StairAt.X, StairAt.Y, FromZ + 40.f), Start, FVector(350.f, 350.f, 50.f))
					|| FMath::Abs(Start.Location.Z - FromZ) > 30.f)
				{
					continue;
				}
				// The goal: the storey-above floor point farthest from the stairs over the building's slabs.
				FVector Goal = FVector::ZeroVector;
				double Best = -1.0;
				for (const FBox& Slab : Roofs.GetStoreySlabs(Building.Key))
				{
					for (const float FX : { 0.2f, 0.5f, 0.8f })
					{
						for (const float FY : { 0.2f, 0.5f, 0.8f })
						{
							FNavLocation OnNav;
							const FVector Try(FMath::Lerp(Slab.Min.X, Slab.Max.X, FX), FMath::Lerp(Slab.Min.Y, Slab.Max.Y, FY), ToZ + 40.f);
							if (Nav->ProjectPointToNavigation(Try, OnNav, FVector(60.f, 60.f, 60.f)) && FMath::Abs(OnNav.Location.Z - ToZ) < 15.f)
							{
								const double D = FVector::Dist2D(OnNav.Location, StairAt);
								if (D > Best)
								{
									Best = D;
									Goal = OnNav.Location;
								}
							}
						}
					}
				}
				if (Best < 0.0)
				{
					continue;
				}
				FCase Case;
				Case.Building = Building.Key;
				Case.From = From;
				Case.FromZ = FromZ;
				Case.ToZ = ToZ;
				Case.Start = Start.Location;
				Case.Goal = Goal;
				Runner->Cases.Add(Case);
			}
		}
	}

	bool Tick()
	{
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			{
				World = Context.World();
			}
		}
		if (!World || !Runner || Runner->Step == EStep::Done)
		{
			return true;
		}
		++Runner->StepFrame;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		URoofCutawaySubsystem* Roofs = World->GetSubsystem<URoofCutawaySubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !Roofs || !PC || !Leader)
		{
			return true;
		}
		FRunner& R = *Runner;
		switch (R.Step)
		{
		case EStep::Warmup:
			if (R.StepFrame >= WarmupFrames)
			{
				Go(EStep::Plan);
			}
			return true;
		case EStep::Plan:
			PlanCases(*World, *Roofs);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %d stair cases"), R.Cases.Num());
			if (R.Cases.IsEmpty())
			{
				Check(false, TEXT("a building with a stair flight and a storey above"));
				Finish();
				return true;
			}
			Go(EStep::Place);
			return true;
		case EStep::Place:
		{
			if (R.CaseIndex >= R.Cases.Num())
			{
				Finish();
				return true;
			}
			const FCase& C = R.Cases[R.CaseIndex];
			if (R.StepFrame == 1)
			{
				if (Squad->IsSoloMode())
				{
					Squad->ExitSoloMode();
				}
				const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				for (int32 Index = 0; Index < Members.Num(); ++Index)
				{
					FVector At = C.Start;
					FNavLocation Near;
					const FVector Offset = FRotator(0.f, Index * 120.f, 0.f).RotateVector(FVector(Index ? 90.f : 0.f, 0.f, 0.f));
					if (Index && Nav && Nav->ProjectPointToNavigation(C.Start + Offset, Near, FVector(40.f, 40.f, 40.f)) && FMath::Abs(Near.Location.Z - C.FromZ) < 30.f)
					{
						At = Near.Location;
					}
					Members[Index]->TeleportTo(At + FVector(0.f, 0.f, Members[Index]->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), FRotator::ZeroRotator, false, true);
					if (AAIController* AI = Cast<AAIController>(Members[Index]->GetController()))
					{
						AI->StopMovement();
					}
				}
				Squad->SetLeader(Members[0]);
			}
			// Wait out the runtime navmesh rebuild (the doors shut at the start, broken windows re-area tiles): a click while the goal's storey
			// tiles are still empty sent the leader to the floor below the goal (worker 2026-11-22, S04).
			UNavigationSystemV1* BuildNav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (R.StepFrame < SettleFrames || (BuildNav && BuildNav->IsNavigationBuildInProgress() && R.StepFrame < SettleFrames + 600))
			{
				return true;
			}
			FHitResult Click(nullptr, nullptr, C.Goal, FVector::UpVector);
			Click.bBlockingHit = true;
			Click.Location = C.Goal;
			Click.TraceStart = C.Goal + FVector(0.f, 0.f, 1200.f);
			PC->HandlePlayerClick(Click);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: case %d/%d %s storey %d -> %d: from %s to %s (formation, %d men)"), R.CaseIndex + 1, R.Cases.Num(), *C.Building,
				C.From, C.From + 1, *C.Start.ToCompactString(), *C.Goal.ToCompactString(), Squad->GetMembers().Num());
			Go(EStep::Walk);
			return true;
		}
		case EStep::Walk:
		{
			const FCase& C = R.Cases[R.CaseIndex];
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			bool bAll = true;
			for (const AOperativeCharacter* Member : Members)
			{
				bAll &= FMath::Abs(FeetZ(*Member) - C.ToZ) < 40.f && FVector::Dist2D(Member->GetActorLocation(), Leader->GetActorLocation()) < TogetherCm;
			}
			const float Seconds = R.StepFrame / 60.f;
			if (R.StepFrame == 20)
			{
				// Where the click sent the leader: the order's goal and his path's end (a goal on the storey below = the click went down a storey).
				const AAIController* LeaderAI = Cast<AAIController>(Leader->GetController());
				const UPathFollowingComponent* Follow = LeaderAI ? LeaderAI->GetPathFollowingComponent() : nullptr;
				const FNavPathSharedPtr Path = Follow ? Follow->GetPath() : nullptr;
				FNavLocation GoalOnNav;
				UNavigationSystemV1* GoalNav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				const bool bGoalNav = GoalNav && GoalNav->ProjectPointToNavigation(C.Goal, GoalOnNav, FVector(40.f, 40.f, 60.f));
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s leader goal %s, path end %s (%d points, partial %d); the goal on the navmesh %s"), *C.Building,
					*Leader->GetLastMoveDestination().ToCompactString(), Path.IsValid() && Path->GetPathPoints().Num() ? *Path->GetEndLocation().ToCompactString() : TEXT("-"),
					Path.IsValid() ? Path->GetPathPoints().Num() : 0, Path.IsValid() && Path->IsPartial() ? 1 : 0, bGoalNav ? *GoalOnNav.Location.ToCompactString() : TEXT("none"));
			}
			if (!bAll && Seconds < LegSeconds)
			{
				return true;
			}
			FString Who;
			for (const AOperativeCharacter* Member : Members)
			{
				const bool bUp = FMath::Abs(FeetZ(*Member) - C.ToZ) < 40.f;
				const AAIController* AI = Cast<AAIController>(Member->GetController());
				const bool bMoving = AI && AI->GetMoveStatus() == EPathFollowingStatus::Moving;
				if (!bUp || FVector::Dist2D(Member->GetActorLocation(), Leader->GetActorLocation()) >= TogetherCm)
				{
					Who += FString::Printf(TEXT(" [%s at %s feet %.0f, %s, %.0f cm from the leader]"), *Member->DisplayName.ToString(), *Member->GetActorLocation().ToCompactString(),
						FeetZ(*Member), bMoving ? TEXT("STUCK (still moving)") : TEXT("stopped"), FVector::Dist2D(Member->GetActorLocation(), Leader->GetActorLocation()));
				}
			}
			Check(bAll, FString::Printf(TEXT("%s storey %d -> %d: the formation is up together (%.1f s)%s"), *C.Building, C.From, C.From + 1, Seconds, *Who));
			++R.CaseIndex;
			Go(EStep::Place);
			return true;
		}
		default:
			return true;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		Runner = MakeShared<FRunner>();
		for (const TCHAR* Name : { TEXT("Codex.Pressure.Enabled"), TEXT("Codex.Pmc.Encounters") })
		{
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
			{
				Var->Set(0, ECVF_SetByConsole);
			}
		}
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddLambda([]() { Tick(); });
		UE_LOG(LogCodexTactics, Display, TEXT("FormationStairSmoke: start"));
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FormationStairSmoke"),
		TEXT("Dev check (designer 2026-11-22: the squad stuck on the stairwell): the formation climbs every building's stairs storey by storey on the player's click; ")
		TEXT("every member must be up within 40 s within 8 m of the leader. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
