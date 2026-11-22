// Dev-only check of the cursor through the storey cutaway (the game designer's bug 2026-11-22 on the dressed khrushchevka showcase, item 5:
// "with the storey cutaway on, clicks land on the invisible upper storey - he is sent to storey 2, it depends on the camera tilt"; item 4: "a
// click on the flat's floor made him hug the exterior wall outside"). URoofCutawaySubsystem::RefineCursorHit / TraceCursorRay,
// ACodexTacticsPlayerController::GetCursorHit; docs/systems/camera-hud-minimap.md "Roof cutaway".
// On a generated map (grey /Game/Maps/Generated/L_Gen_StoryShowcase or the dressed /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit) the leader is
// put inside a 3-storey house (building=<id>, default the first indexed one with 3 storeys + roof) on each storey of storeys=<0+1>; once the
// cutaway settled, navmesh floor points of that storey at least 5 m from him (the far rooms) are "clicked" from 12 cameras each (pitch -40 /
// -55 / -70, four yaws, 22 m away): the cursor ray is traced as the controller traces it, then the ordinary click (HandleWorldHit) is given.
// FAIL when a ray stops on a piece of a storey above his or on a wall top under the cut storey, when a click's move goal is not on his storey
// near the clicked point, when a floor click becomes a cover order, or when fewer than a quarter of the rays reach the clicked room's floor.
// First with Codex.Cursor.ThroughCutaway 0 (the raw hit: the "before", logged as info), then 1 (judged). PASS / FAIL, then exits.
//   UnrealEditor.exe <uproject> /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit -game -nullrhi -nosound -LevelJson=L_Gen_StoryShowcase_Kit.json
//     -ExecCmds="CodexTactics.CutawayClickSmoke building=S04_rubble_escape storeys=0+1"
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Camera/RoofCutawaySubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/DoorActor.h"
#include "Interactables/DoorSubsystem.h"
#include "Interactables/MantleNavigation.h"
#include "Interactables/WindowBreakage.h"
#include "NavigationPath.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "NavigationSystem.h"

namespace CutawayClickSmoke
{
	constexpr int32 WarmupFrames = 150;
	/** The cutaway settles this long after he is placed (> its 0.1 s pass + 0.5 s restore hysteresis). */
	constexpr int32 SettleFrames = 90;
	constexpr int32 MaxFrames = 60 * 300;
	/** Clicked floor points: at least this far from him (the far rooms), at most this many per storey. */
	constexpr float FarCm = 500.f;
	constexpr int32 MaxTargets = 8;
	/** A floor hit this close to the aimed point is "the clicked room". */
	constexpr float RoomHitCm = 120.f;
	/** The move goal lies this close to the clicked point. */
	constexpr float GoalCm = 150.f;
	constexpr float CameraDistanceCm = 2200.f;

	enum class EStep : uint8 { Warmup, Place, Settle, Next, Done };

	struct FRunner
	{
		EStep Step = EStep::Warmup;
		int32 Frame = 0;
		int32 StepFrame = 0;
		int32 Failures = 0;
		FString BuildingArg;
		FString BuildingId;
		int32 StoreyCount = 0;
		TArray<int32> Storeys = { 0, 1 };
		int32 StoreyIndex = 0;
		FVector Spot = FVector::ZeroVector;
		FDelegateHandle EndFrameHandle;
	};

	TSharedPtr<FRunner> Runner;

	void Check(bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		Runner->Failures += bOk ? 0 : 1;
	}

	void SetThrough(bool bOn)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Cursor.ThroughCutaway")))
		{
			Var->Set(bOn ? 1 : 0, ECVF_SetByConsole);
		}
	}

	void Finish()
	{
		SetThrough(true);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), Runner->Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		Runner->Step = EStep::Done;
		FPlatformMisc::RequestExit(false, TEXT("CutawayClickSmoke"));
	}

	bool PickBuilding(URoofCutawaySubsystem& Roofs)
	{
		Roofs.RebuildStoreyIndex();
		for (const TPair<FString, int32>& Building : Roofs.GetStoreyBuildings())
		{
			if (Runner->BuildingArg.IsEmpty() ? Building.Value >= 4 : Building.Key == Runner->BuildingArg)
			{
				Runner->BuildingId = Building.Key;
				Runner->StoreyCount = Building.Value;
				return true;
			}
		}
		return false;
	}

	/** Navmesh floor points on the storey at BaseZ over the building's slabs (a grid across each slab). */
	TArray<FVector> FloorPoints(UWorld* World, const URoofCutawaySubsystem& Roofs, float BaseZ)
	{
		TArray<FVector> Out;
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const float Fractions[] = { 0.12f, 0.3f, 0.5f, 0.7f, 0.88f };
		for (const FBox& Slab : Roofs.GetStoreySlabs(Runner->BuildingId))
		{
			for (const float FX : Fractions)
			{
				for (const float FY : Fractions)
				{
					const FVector Try(FMath::Lerp(Slab.Min.X, Slab.Max.X, FX), FMath::Lerp(Slab.Min.Y, Slab.Max.Y, FY), BaseZ + 40.f);
					FNavLocation OnNav;
					if (Nav && Nav->ProjectPointToNavigation(Try, OnNav, FVector(40.f, 40.f, 80.f)) && FMath::Abs(OnNav.Location.Z - BaseZ) < 60.f
						&& !Out.ContainsByPredicate([&OnNav](const FVector& P) { return FVector::Dist2D(P, OnNav.Location) < 150.f; }))
					{
						Out.Add(OnNav.Location);
					}
				}
			}
		}
		return Out;
	}

	void Put(AOperativeCharacter& Member, const FVector& Spot)
	{
		Member.TeleportTo(Spot + FVector(0.f, 0.f, Member.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), FRotator(0.f, 90.f, 0.f), false, true);
		if (AAIController* AI = Cast<AAIController>(Member.GetController()))
		{
			AI->StopMovement();
		}
	}

	struct FTally
	{
		int32 Rays = 0;
		int32 Upper = 0;
		int32 WallTop = 0;
		int32 RoomFloor = 0;
		int32 OtherFloor = 0;
		int32 Occluded = 0;
		int32 Pawn = 0;
		int32 Miss = 0;
		int32 Clicks = 0;
		int32 BadGoals = 0;
		/** Clicks behind a LOCKED door redirected to try that door (UDoorSubsystem::RedirectMoveToLockedDoor; doors on by default on the showcase). */
		int32 LockedRedirects = 0;
		/** Clicks on a spot of his storey reachable only through a window (the formation never paths through one): he goes as near as he can. */
		int32 WindowOnly = 0;
		/** Clicks landing just behind a window opening the ray passed (WindowBreakage::FindWindowSillAt): a window order by the designer's rule. */
		int32 WindowClicks = 0;
		int32 CoverOrders = 0;
		FString Examples;
	};

	/** All rays at every target from every camera; judged when bJudge (ThroughCutaway on), the clicks given only then. */
	FTally Sweep(UWorld* World, URoofCutawaySubsystem& Roofs, ACodexTacticsPlayerController* PC, AOperativeCharacter& Leader, int32 Storey, bool bJudge)
	{
		FTally T;
		float BaseZ = 0.f;
		float CeilZ = TNumericLimits<float>::Max();
		Roofs.GetStoreyBaseZ(Runner->BuildingId, Storey, BaseZ);
		Roofs.GetStoreyBaseZ(Runner->BuildingId, Storey + 1, CeilZ);
		TMap<const UPrimitiveComponent*, int32> StoreyOf;
		for (const TPair<UPrimitiveComponent*, int32>& Piece : Roofs.GetStoreyPieces(Runner->BuildingId))
		{
			StoreyOf.Add(Piece.Key, Piece.Value);
		}
		TArray<FVector> Targets = FloorPoints(World, Roofs, BaseZ);
		Targets.RemoveAll([&Leader](const FVector& P) { return FVector::Dist2D(P, Leader.GetActorLocation()) < FarCm; });
		Targets.Sort([&Leader](const FVector& A, const FVector& B) { return FVector::Dist2D(A, Leader.GetActorLocation()) > FVector::Dist2D(B, Leader.GetActorLocation()); });
		if (Targets.Num() > MaxTargets)
		{
			Targets.SetNum(MaxTargets);
		}
		const float Pitches[] = { -40.f, -55.f, -70.f };
		const float Yaws[] = { 20.f, 110.f, 200.f, 290.f };
		const FVector Home = Leader.GetActorLocation();
		for (const FVector& Target : Targets)
		{
			for (const float Pitch : Pitches)
			{
				for (const float Yaw : Yaws)
				{
					const FVector Dir = FRotator(Pitch, Yaw, 0.f).Vector();
					const FVector Aim = Target + FVector(0.f, 0.f, 2.f);
					const FVector Eye = Aim - Dir * CameraDistanceCm;
					FHitResult Hit;
					++T.Rays;
					if (!Roofs.TraceCursorRay(Eye, Aim + Dir * 3000.f, ECC_Visibility, &Leader, Hit))
					{
						++T.Miss;
						continue;
					}
					const int32* PieceStorey = StoreyOf.Find(Hit.GetComponent());
					const float Z = static_cast<float>(Hit.ImpactPoint.Z);
					FString Kind;
					if (Cast<APawn>(Hit.GetActor()))
					{
						++T.Pawn;
						continue;
					}
					if (PieceStorey && *PieceStorey > Storey)
					{
						++T.Upper;
						Kind = TEXT("UPPER STOREY");
					}
					else if (PieceStorey && Z >= CeilZ - 30.f && !Hit.GetActor()->Tags.ContainsByPredicate([](const FName& Tag) { return Tag.ToString().StartsWith(TEXT("stair=")); }))
					{
						++T.WallTop;
						Kind = TEXT("WALL TOP");
					}
					else if (Hit.ImpactNormal.Z > 0.7f && FMath::Abs(Z - BaseZ) < 60.f)
					{
						if (FVector::Dist2D(Hit.ImpactPoint, Target) <= RoomHitCm)
						{
							++T.RoomFloor;
						}
						else
						{
							++T.OtherFloor;
						}
					}
					else
					{
						++T.Occluded;
					}
					if (!Kind.IsEmpty() && T.Examples.Len() < 900)
					{
						T.Examples += FString::Printf(TEXT(" [%s %s.%s z %.0f, pitch %.0f yaw %.0f]"), *Kind, *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), Z, Pitch, Yaw);
					}
					// The ordinary click on a floor of his storey: the move goal must be there (never a cover order, never another storey).
					if (bJudge && PC && Kind.IsEmpty() && Hit.ImpactNormal.Z > 0.7f && FMath::Abs(Z - BaseZ) < 60.f)
					{
						const bool bCoverBefore = Leader.bInCover || PC->HasCoverPreview();
						PC->HandleWorldHit(Hit);
						++T.Clicks;
						const FVector Goal = Leader.GetLastMoveDestination();
						const float GoalFeetZ = static_cast<float>(Goal.Z);
						const bool bGoalOk = FVector::Dist2D(Goal, Hit.ImpactPoint) <= GoalCm && GoalFeetZ >= BaseZ - 60.f && GoalFeetZ <= FMath::Min(CeilZ - 60.f, BaseZ + 200.f); // feet or capsule centre, never the storey above
						// Doors on (Lead 2026-11-22): a click into a room behind a LOCKED door is sent to try that door - expected when the goal is by
						// a locked door at the clicked level with the click on its far side; any other redirect is a bad goal.
						UDoorSubsystem* Doors = World->GetSubsystem<UDoorSubsystem>();
						bool bLockedRedirect = false;
						if (!bGoalOk && Doors && Doors->HasPendingInteraction(&Leader))
						{
							const ADoorActor* Door = Doors->FindNearestDoor(Goal, 250.f);
							bLockedRedirect = Door && Door->GetDoorState() == EDoorLeafState::Locked && Door->IsAtDoorLevel(Hit.ImpactPoint)
								&& Door->ToLocal(Hit.ImpactPoint).Y * Door->ToLocal(Goal).Y < 0.f;
							if (bLockedRedirect)
							{
								++T.LockedRedirects;
								if (T.Examples.Len() < 900)
								{
									T.Examples += FString::Printf(TEXT(" [LOCKED %s %s: goal %s for a click at %s]"), *Door->GetBuildingId(), *Door->GetOpeningId(),
										*Goal.ToCompactString(), *Hit.ImpactPoint.ToCompactString());
								}
							}
						}
						if (Doors)
						{
							Doors->CancelInteraction(&Leader);
						}
						// A spot reachable only through a window (designer 2026-11-22: the formation never climbs a window without a window order):
						// the move ends where the no-window path does - expected when the goal is at that path's end and a window path gets there.
						bool bWindowOnly = false;
						FString PathWhy;
						const bool bWindowClick = !bGoalOk && !bLockedRedirect && WindowBreakage::FindWindowSillAt(*World, Hit) != nullptr;
						if (bWindowClick)
						{
							++T.WindowClicks;
						}
						if (!bGoalOk && !bLockedRedirect && !bWindowClick)
						{
							const FVector From = Home;
							const UNavigationPath* Any = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, Hit.ImpactPoint, &Leader, nullptr); // the default filter: windows and mantles open
							const UNavigationPath* NoWin = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, Hit.ImpactPoint, &Leader,
								MantleNavigation::NoWindowVariant(nullptr, true));
							const auto Reaches = [&Hit](const UNavigationPath* P) { return P && P->IsValid() && !P->IsPartial() && !P->PathPoints.IsEmpty()
								&& FVector::Dist2D(P->PathPoints.Last(), Hit.ImpactPoint) <= 60.f; };
							const FVector NoWinEnd = NoWin && !NoWin->PathPoints.IsEmpty() ? NoWin->PathPoints.Last() : From;
							bWindowOnly = Reaches(Any) && !Reaches(NoWin) && FVector::Dist2D(NoWinEnd, Goal) <= 150.f;
							PathWhy = FString::Printf(TEXT(" path: with windows %s, without %s (end %s)"), Reaches(Any) ? TEXT("reaches") : TEXT("short"),
								Reaches(NoWin) ? TEXT("reaches") : TEXT("short"), *NoWinEnd.ToCompactString());
							if (bWindowOnly)
							{
								++T.WindowOnly;
								if (T.Examples.Len() < 900)
								{
									T.Examples += FString::Printf(TEXT(" [WINDOW-ONLY goal %s for a click at %s]"), *Goal.ToCompactString(), *Hit.ImpactPoint.ToCompactString());
								}
							}
						}
						if (!bGoalOk && !bLockedRedirect && !bWindowOnly && !bWindowClick)
						{
							++T.BadGoals;
							if (T.Examples.Len() < 900)
							{
								T.Examples += FString::Printf(TEXT(" [GOAL %s for a click at %s, pitch %.0f yaw %.0f;%s]"), *Goal.ToCompactString(), *Hit.ImpactPoint.ToCompactString(), Pitch, Yaw, *PathWhy);
							}
						}
						if (!bCoverBefore && PC->HasCoverPreview())
						{
							++T.CoverOrders;
							PC->HideCoverPreview();
						}
						Leader.StopOperative();
						Put(Leader, Home - FVector(0.f, 0.f, Leader.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f));
					}
				}
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s storey %d (base %.0f, ceiling %.0f), ThroughCutaway %d: %d targets, %d rays - room floor %d, other floor of his storey %d, occluded by his storey %d, pawn %d, miss %d, UPPER STOREY %d, WALL TOP %d; clicks %d, bad goals %d, locked-door redirects %d, window-only %d, window clicks %d, cover orders %d%s"),
			*Runner->BuildingId, Storey, BaseZ, CeilZ, bJudge ? 1 : 0, Targets.Num(), T.Rays, T.RoomFloor, T.OtherFloor, T.Occluded, T.Pawn, T.Miss, T.Upper, T.WallTop,
			T.Clicks, T.BadGoals, T.LockedRedirects, T.WindowOnly, T.WindowClicks, T.CoverOrders, *T.Examples);
		if (bJudge)
		{
			Check(Targets.Num() > 0, FString::Printf(TEXT("%s storey %d: far floor points to click (%d)"), *Runner->BuildingId, Storey, Targets.Num()));
			Check(T.Upper == 0 && T.WallTop == 0, FString::Printf(TEXT("%s storey %d: no ray stops on a hidden storey / a wall top under it (%d upper, %d wall tops of %d rays)"),
				*Runner->BuildingId, Storey, T.Upper, T.WallTop, T.Rays));
			Check(T.BadGoals == 0 && T.CoverOrders == 0, FString::Printf(TEXT("%s storey %d: every floor click moves him on his storey to the clicked point or to the locked door in its way (%d clicks, %d bad goals, %d locked-door redirects, %d cover orders)"),
				*Runner->BuildingId, Storey, T.Clicks, T.BadGoals, T.LockedRedirects, T.CoverOrders));
			Check(T.RoomFloor * 4 >= T.Rays, FString::Printf(TEXT("%s storey %d: at least a quarter of the rays reach the clicked room's floor (%d of %d)"),
				*Runner->BuildingId, Storey, T.RoomFloor, T.Rays));
		}
		return T;
	}

	void Tick()
	{
		FRunner& R = *Runner;
		if (R.Step == EStep::Done)
		{
			return;
		}
		++R.Frame;
		++R.StepFrame;
		if (R.Frame > MaxFrames)
		{
			Check(false, FString::Printf(TEXT("timeout in step %d"), static_cast<int32>(R.Step)));
			Finish();
			return;
		}
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		URoofCutawaySubsystem* Roofs = World ? World->GetSubsystem<URoofCutawaySubsystem>() : nullptr;
		if (!World || !Squad || !Roofs || !Squad->GetLeader())
		{
			return;
		}
		auto Go = [&R](EStep Step)
		{
			R.Step = Step;
			R.StepFrame = 0;
		};
		const int32 Storey = R.Storeys.IsValidIndex(R.StoreyIndex) ? R.Storeys[R.StoreyIndex] : 0;
		switch (R.Step)
		{
		case EStep::Warmup:
			if (R.StepFrame < WarmupFrames)
			{
				return;
			}
			if (!PickBuilding(*Roofs))
			{
				Check(false, FString::Printf(TEXT("a 3-storey building of the storey index (%s; %d indexed)"), *R.BuildingArg, Roofs->GetStoreyBuildings().Num()));
				Finish();
				return;
			}
			Check(true, FString::Printf(TEXT("%s: %s has %d storeys incl. the roof"), *World->GetMapName(), *R.BuildingId, R.StoreyCount));
			if (!Squad->IsSoloMode())
			{
				Squad->EnterSoloMode();
			}
			Squad->SetFollowersHolding(true);
			Go(EStep::Place);
			return;
		case EStep::Place:
		{
			float BaseZ = 0.f;
			TArray<FVector> Points = Roofs->GetStoreyBaseZ(R.BuildingId, Storey, BaseZ) ? FloorPoints(World, *Roofs, BaseZ) : TArray<FVector>();
			if (Points.IsEmpty())
			{
				Check(false, FString::Printf(TEXT("%s: a navmesh spot on storey %d"), *R.BuildingId, Storey));
				Go(EStep::Next);
				return;
			}
			// The middle-most point: the far rooms lie round him.
			FBox All(ForceInit);
			for (const FVector& P : Points)
			{
				All += P;
			}
			Points.Sort([&All](const FVector& A, const FVector& B) { return FVector::Dist2D(A, All.GetCenter()) < FVector::Dist2D(B, All.GetCenter()); });
			R.Spot = Points[0];
			TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Members.Remove(nullptr);
			for (int32 Index = 0; Index < Members.Num(); ++Index)
			{
				Put(*Members[Index], Points[FMath::Min(Index, Points.Num() - 1)]);
			}
			Squad->SetLeader(Members[0]);
			Go(EStep::Settle);
			return;
		}
		case EStep::Settle:
		{
			if (R.StepFrame < SettleFrames)
			{
				return;
			}
			AOperativeCharacter* Leader = Squad->GetLeader();
			FString Building;
			int32 InStorey = INDEX_NONE;
			const bool bIn = Roofs->GetOperativeStorey(*Leader, Building, InStorey);
			Check(bIn && Building == R.BuildingId && InStorey == Storey, FString::Printf(TEXT("the leader is in %s on storey %d (expected %s / %d)"),
				bIn ? *Building : TEXT("-"), InStorey, *R.BuildingId, Storey));
			ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
			SetThrough(false);
			Sweep(World, *Roofs, PC, *Leader, Storey, false);
			SetThrough(true);
			Sweep(World, *Roofs, PC, *Leader, Storey, true);
			Go(EStep::Next);
			return;
		}
		case EStep::Next:
			++R.StoreyIndex;
			if (R.StoreyIndex < R.Storeys.Num() && R.Storeys[R.StoreyIndex] < R.StoreyCount - 1)
			{
				Go(EStep::Place);
				return;
			}
			Finish();
			return;
		default:
			return;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* /*World*/)
	{
		Runner = MakeShared<FRunner>();
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("building=")))
			{
				Runner->BuildingArg = Arg.Mid(9);
			}
			else if (Arg.StartsWith(TEXT("storeys=")))
			{
				TArray<FString> Parts;
				const TCHAR* Separators[] = { TEXT("+"), TEXT("/"), TEXT(",") };
				Arg.Mid(8).ParseIntoArray(Parts, Separators, UE_ARRAY_COUNT(Separators));
				Runner->Storeys.Reset();
				for (const FString& Part : Parts)
				{
					Runner->Storeys.Add(FCString::Atoi(*Part));
				}
			}
		}
		if (Runner->Storeys.IsEmpty())
		{
			Runner->Storeys = { 0 };
		}
		if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
		{
			Pressure->Set(0, ECVF_SetByConsole);
		}
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
		UE_LOG(LogCodexTactics, Display, TEXT("CutawayClickSmoke: building %s, %d storeys"), Runner->BuildingArg.IsEmpty() ? TEXT("(auto)") : *Runner->BuildingArg, Runner->Storeys.Num());
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CutawayClickSmoke"),
		TEXT("Dev check (designer bug 2026-11-22 items 4 / 5): the leader inside a 3-storey generated house on storeys=<0+1>; far floor points of his storey are clicked from 12 ")
		TEXT("cameras: no cursor ray may stop on a hidden storey, every floor click moves him on his storey to the clicked point (no cover order). [building=<id>]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
