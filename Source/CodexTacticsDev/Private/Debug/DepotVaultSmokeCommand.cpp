// Dev-only check of #36 (user 2026-11-04: "the operatives do not vault over the low obstacles on L_Gen_Depot_1" - vaulting works on the other
// maps, VaultSmoke). Run (map + -LevelJson from Scripts/test_map.json):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands DepotVaultSmoke
// Root cause: the level generator's covers carry their layout kind as a tag (sandbag, jersey, barrel, crate, wreck, ...) and none was tagged
// "Vault" - nothing on a generated map was vaultable. ACodexTacticsGameMode::StartPlay now tags the low kinds that fit the vault
// (VaultRules::IsVaultableLevelObject). The smoke: the Depot's sandbags / jersey barriers / barrels are vaultable (tag + the vault nav
// area), its crates (110 cm, over the 105 cm vault) and wrecks stay cover; the leader ordered straight across a jersey barrier (or a sandbag)
// vaults it and arrives behind it; and one put in front of each kind vaults it by itself (TryVault). The map is not edited.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Characters/VaultRules.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/VaultNavigation.h"
#include "NavModifierComponent.h"

namespace DepotVaultSmoke
{
	constexpr float StepSeconds = 0.05f;

	struct FObstacle
	{
		TWeakObjectPtr<AActor> Actor;
		FName Kind;
		FVector Centre = FVector::ZeroVector;
		/** The thin axis (across), its half depth, the long half length. */
		FVector Across = FVector::ForwardVector;
		float HalfDepth = 0.f;
		float HalfLength = 0.f;
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		TArray<FObstacle> Picks; // one per kind tried by hand, the first one also by the path
		int32 Pick = 0;
		FVector Goal = FVector::ZeroVector;
		bool bSawVault = false;
		int32 HandVaults = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("DepotVaultSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	FName KindOf(const AActor& Actor)
	{
		for (const FName& Tag : Actor.Tags)
		{
			for (const TCHAR* Kind : { TEXT("sandbag"), TEXT("jersey"), TEXT("barrel"), TEXT("crate"), TEXT("wreck"), TEXT("boiler") })
			{
				if (Tag == FName(Kind))
				{
					return Tag;
				}
			}
		}
		return NAME_None;
	}

	/** Free ground (no obstacle) on both sides of it at the middle, Out cm off its faces: a straight line across. */
	bool ClearBothSides(UWorld* World, const FObstacle& Obstacle, float Out, const AActor* Ignore)
	{
		for (const float Side : { -1.f, 1.f })
		{
			const FVector Spot = Obstacle.Centre + Obstacle.Across * Side * (Obstacle.HalfDepth + Out);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(DepotVaultSmoke), false, Ignore);
			if (World->OverlapAnyTestByChannel(Spot + FVector(0.f, 0.f, 100.f), FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeCapsule(45.f, 80.f), Params))
			{
				return false;
			}
		}
		return true;
	}

	bool Step(TSharedRef<FState> State)
	{
		State->Time += StepSeconds;
		State->StageTime += StepSeconds;
		UWorld* World = State->World.Get();
		if (!World)
		{
			return false;
		}
		if (State->Time > 240.f)
		{
			Check(*State, false, FString::Printf(TEXT("time out in stage %d"), State->Stage));
			return Finish(*State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = State->Leader.Get();
		switch (State->Stage)
		{
		case 0:
		{
			if (!Squad || !Squad->GetLeader() || State->StageTime < 4.f)
			{
				return true;
			}
			State->Leader = Squad->GetLeader();
			Leader = State->Leader.Get();
			Squad->EnterSoloMode();
			Squad->SetFollowersHolding(true);
			Leader->bTacticalCeaseFire = true;
			// The kinds: vaultable ones tagged + their nav area, the tall ones not.
			TMap<FName, int32> Total;
			TMap<FName, int32> Vaultable;
			TMap<FName, FObstacle> Nearest;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				const FName Kind = KindOf(**It);
				if (Kind.IsNone())
				{
					continue;
				}
				++Total.FindOrAdd(Kind);
				const bool bVault = VaultNavigation::IsVaultable(*It) && VaultNavigation::FindVaultNavModifier(*It) != nullptr;
				Vaultable.FindOrAdd(Kind) += bVault ? 1 : 0;
				if (!bVault)
				{
					continue;
				}
				FObstacle Obstacle;
				Obstacle.Actor = *It;
				Obstacle.Kind = Kind;
				const FVector Size = It->CalculateComponentsBoundingBoxInLocalSpace(false).GetSize() * It->GetActorScale3D().GetAbs();
				const bool bXThin = Size.X <= Size.Y;
				Obstacle.Across = (bXThin ? It->GetActorForwardVector() : It->GetActorRightVector()).GetSafeNormal2D();
				Obstacle.HalfDepth = static_cast<float>(0.5 * (bXThin ? Size.X : Size.Y));
				Obstacle.HalfLength = static_cast<float>(0.5 * (bXThin ? Size.Y : Size.X));
				FVector Origin;
				FVector Extent;
				It->GetActorBounds(false, Origin, Extent);
				Obstacle.Centre = FVector(Origin.X, Origin.Y, Origin.Z - Extent.Z);
				if (!ClearBothSides(World, Obstacle, 160.f, *It))
				{
					continue;
				}
				FObstacle* Best = Nearest.Find(Kind);
				const float Distance = static_cast<float>(FVector::Dist2D(Obstacle.Centre, Leader->GetActorLocation()));
				if (!Best || Distance < FVector::Dist2D(Best->Centre, Leader->GetActorLocation()))
				{
					Nearest.Add(Kind, Obstacle);
				}
			}
			for (const TCHAR* Kind : { TEXT("sandbag"), TEXT("jersey"), TEXT("barrel"), TEXT("crate"), TEXT("wreck") })
			{
				const int32 All = Total.FindRef(FName(Kind));
				const int32 Vault = Vaultable.FindRef(FName(Kind));
				const bool bShouldVault = VaultRules::IsVaultableCoverKind(FName(Kind));
				UE_LOG(LogCodexTactics, Display, TEXT("[DepotVault] %s: %d on the map, %d vaultable"), Kind, All, Vault);
				if (All > 0)
				{
					Check(*State, bShouldVault ? Vault == All : Vault == 0, FString::Printf(TEXT("%s: %d of %d vaultable (%s)"), Kind, Vault, All,
						bShouldVault ? TEXT("all: low enough to climb over") : TEXT("none: taller than the 105 cm vault - cover")));
				}
			}
			for (const TCHAR* Kind : { TEXT("jersey"), TEXT("sandbag"), TEXT("barrel") })
			{
				if (const FObstacle* Found = Nearest.Find(FName(Kind)))
				{
					State->Picks.Add(*Found);
				}
			}
			Check(*State, !State->Picks.IsEmpty(), FString::Printf(TEXT("vaultable low covers with free ground on both sides found (%d kinds)"), State->Picks.Num()));
			if (State->Picks.IsEmpty())
			{
				return Finish(*State, false);
			}
			// The path: straight across the first pick's middle, 80 cm off each face.
			const FObstacle& First = State->Picks[0];
			const FVector Start = SmokeUtils::FreeSpot(World, First.Centre - First.Across * (First.HalfDepth + 80.f) + FVector(0.f, 0.f, 100.f), Leader);
			Leader->StopOperative();
			Leader->TeleportTo(FVector(Start.X, Start.Y, Start.Z + 4.f), First.Across.Rotation(), false, true);
			State->Goal = First.Centre + First.Across * (First.HalfDepth + 80.f);
			Next(*State, 1);
			return true;
		}
		case 1:
		{
			if (State->StageTime < 1.f)
			{
				return true;
			}
			const FObstacle& First = State->Picks[0];
			Check(*State, Leader->OrderMoveTo(State->Goal, false) == EOperativeOrderResult::Accepted,
				FString::Printf(TEXT("the leader ordered straight across the %s (%.0f cm deep, %.0f cm long)"), *First.Kind.ToString(), First.HalfDepth * 2.f, First.HalfLength * 2.f));
			State->bSawVault = false;
			Next(*State, 2);
			return true;
		}
		case 2:
		{
			State->bSawVault |= Leader->IsVaulting();
			const float Remaining = static_cast<float>(FVector::Dist2D(Leader->GetActorLocation(), State->Goal));
			if ((Remaining > 80.f || Leader->IsVaulting()) && State->StageTime < 12.f)
			{
				return true;
			}
			const FObstacle& First = State->Picks[0];
			Check(*State, State->bSawVault, FString::Printf(TEXT("he vaulted the %s on the way (the path across its vault area)"), *First.Kind.ToString()));
			Check(*State, Remaining <= 80.f, FString::Printf(TEXT("... and arrived behind it (%.0f cm from the goal, %.1f s)"), Remaining, State->StageTime));
			State->Pick = 0;
			Next(*State, 3);
			return true;
		}
		case 3:
		{
			// By hand: in front of each kind's pick, facing it, 60 cm off its face - TryVault.
			if (!State->Picks.IsValidIndex(State->Pick))
			{
				return Finish(*State, true);
			}
			const FObstacle& Obstacle = State->Picks[State->Pick];
			const FVector Spot = SmokeUtils::FreeSpot(World, Obstacle.Centre - Obstacle.Across * (Obstacle.HalfDepth + 70.f) + FVector(0.f, 0.f, 100.f), Leader);
			Leader->StopOperative();
			Leader->TeleportTo(FVector(Spot.X, Spot.Y, Spot.Z + 4.f), Obstacle.Across.Rotation(), false, true);
			Next(*State, 4);
			return true;
		}
		case 4:
		{
			if (State->StageTime < 0.8f)
			{
				return true;
			}
			const FObstacle& Obstacle = State->Picks[State->Pick];
			const bool bVault = Leader->IsVaulting() || Leader->TryVault(Obstacle.Across, true);
			Check(*State, bVault, FString::Printf(TEXT("in front of the %s he vaults it (TryVault)"), *Obstacle.Kind.ToString()));
			Next(*State, 5);
			return true;
		}
		case 5:
		{
			if (Leader->IsVaulting() && State->StageTime < 4.f)
			{
				return true;
			}
			const FObstacle& Obstacle = State->Picks[State->Pick];
			const float Beyond = static_cast<float>(FVector::DotProduct(Leader->GetActorLocation() - Obstacle.Centre, Obstacle.Across));
			Check(*State, Beyond > Obstacle.HalfDepth + 20.f && !VaultNavigation::IsStandingOnObstacle(*Leader),
				FString::Printf(TEXT("... and lands behind the %s (%.0f cm past its middle, not on top)"), *Obstacle.Kind.ToString(), Beyond));
			++State->Pick;
			Next(*State, 3);
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
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float) { return Step(State); }), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.DepotVaultSmoke"),
		TEXT("Dev check (#36, user 2026-11-04): on L_Gen_Depot_1 the generated sandbags / jersey barriers / barrels are vaultable (crates / wrecks stay cover), the leader vaults one on his path and by hand; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
