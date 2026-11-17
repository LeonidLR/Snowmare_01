// Dev-only console command (TACTICAL_MAP P2, designer 2026-11-17: "the dangerous places must already affect gameplay"), on L_Gen_Depot_X4:
//   Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands TacticalPmcSmoke
// A PMC squad spawned with the map's tactical file (UPmcSquadSubsystem::SpawnSquadTactical) along the player squad's way to a mission goal:
// its marksman stands on an overwatch roof spot, an ambusher at the exit of a chokepoint (a hiding spot of that place, outside its outline),
// the seeded ladder-only roof picker ran (part of the roofs); commanded for a while with nobody to fight, the men hold their posts (the
// Ambush order); the positions the commander chose score a lower PMC-side DangerAt than random points along the same way; a fall-back
// near a chokepoint goes round it (PmcTacticalRules::PickFallBack with the map's dangers); without the tactical file the gather is empty
// (the old line spawn). [seed=<n>] [count=<men>].

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcCommanderRules.h"
#include "AI/PmcSquadSubsystem.h"
#include "AI/PmcTacticalRules.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "LevelGen/MissionGoalsSubsystem.h"
#include "LevelGen/TacticalMapRules.h"
#include "LevelGen/TacticalMapSubsystem.h"
#include "Math/RandomStream.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"

namespace TacticalPmcSmoke
{
	constexpr float WarmupSeconds = 2.f;
	constexpr float SettleSeconds = 3.f;
	constexpr float CommandSeconds = 10.f;
	constexpr float TimeoutSeconds = 150.f;
	constexpr int32 TeamId = 7;

	enum class EStage : uint8
	{
		Begin,
		Settle,
		Command,
		Done
	};

	struct FState
	{
		EStage Stage = EStage::Begin;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		int32 Seed = 7;
		int32 Count = 4;
		FPmcWeights Weights;
		TArray<FVector> Route;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Fighters;
		TArray<FPmcTacticalPost> Posts;
		TArray<int32> LadderPicked;
		SmokeUtils::FPreloadGate PreloadGate;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, UWorld* World, bool bComplete)
	{
		State.Stage = EStage::Done;
		if (UPmcSquadSubsystem* Pmc = World ? UPmcSquadSubsystem::Get(World) : nullptr)
		{
			Pmc->SetTeamCommanded(TeamId, false);
			Pmc->DestroyTeam(TeamId);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TacticalPmcSmoke"));
		return false;
	}

	FVector FeetOf(const AOperativeCharacter& Operative)
	{
		return Operative.GetActorLocation() - FVector(0.f, 0.f, Operative.GetSimpleCollisionHalfHeight());
	}

	/** The player's squad out of the way (hidden, no collision, frozen, 1 km off): the PMC has nobody to fight - only its posts count. */
	void ParkPlayerSquad(UWorld* World)
	{
		if (USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->StopOperative();
				Member->SetActorHiddenInGame(true);
				Member->SetActorEnableCollision(false);
				Member->CustomTimeDilation = 0.f;
				Member->SetActorLocation(Member->GetActorLocation() + FVector(100000.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
	}

	/** The squad's way: the level start (the squad leader / a player start) to the mission goal whose route offers the most posts (with a roof + an ambush). */
	bool PickRoute(UWorld* World, FState& State, UPmcSquadSubsystem* Pmc)
	{
		FVector Start = FVector::ZeroVector;
		const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!Squad || !Squad->GetLeader())
		{
			return false;
		}
		Start = FeetOf(*Squad->GetLeader());
		const UMissionGoalsSubsystem* Goals = World->GetSubsystem<UMissionGoalsSubsystem>();
		if (!Goals || Goals->GetGoals().IsEmpty())
		{
			return false;
		}
		int32 BestCount = -1;
		for (const FMissionGoal& Goal : Goals->GetGoals())
		{
			const TArray<FVector> Route = Pmc->RouteBetween(Start, Goal.Location);
			const TArray<FPmcPostCandidate> Candidates = Pmc->GatherPostCandidates(Route, State.Seed, State.Weights);
			const bool bRoof = Candidates.ContainsByPredicate([](const FPmcPostCandidate& C) { return C.Kind == EPmcPostKind::Overwatch; });
			const bool bAmbush = Candidates.ContainsByPredicate([](const FPmcPostCandidate& C) { return C.Kind == EPmcPostKind::Ambush; });
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke   route to %s: %d points, %d post candidates (roof %d, ambush %d)"), *Goal.Id, Route.Num(), Candidates.Num(),
				bRoof ? 1 : 0, bAmbush ? 1 : 0);
			const int32 Count = Candidates.Num() + (bRoof && bAmbush ? 10000 : 0);
			if (Route.Num() >= 2 && Count > BestCount)
			{
				BestCount = Count;
				State.Route = Route;
			}
		}
		return State.Route.Num() >= 2;
	}

	/** Mean PMC-side danger of seeded navmesh points within 8 m of the route (the "random positions along the same way"). */
	float RandomRouteDanger(UWorld* World, const FState& State, const UTacticalMapSubsystem* Map, int32& OutSamples)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FRandomStream Stream(State.Seed * 389 + 3);
		float Sum = 0.f;
		OutSamples = 0;
		for (int32 Try = 0; Try < 400 && OutSamples < 120 && Nav; ++Try)
		{
			const int32 Leg = Stream.RandRange(0, State.Route.Num() - 2);
			const FVector OnRoute = FMath::Lerp(State.Route[Leg], State.Route[Leg + 1], Stream.FRand());
			const FVector Probe = OnRoute + FVector(Stream.FRandRange(-800.f, 800.f), Stream.FRandRange(-800.f, 800.f), 0.f);
			FNavLocation OnNav;
			if (Nav->ProjectPointToNavigation(Probe, OnNav, FVector(200.f, 200.f, 150.f)) && FMath::Abs(OnNav.Location.Z - OnRoute.Z) <= 150.f)
			{
				Sum += Map->DangerAt(OnNav.Location, ETacticalSide::Pmc).Danger;
				++OutSamples;
			}
		}
		return OutSamples > 0 ? Sum / OutSamples : 0.f;
	}

	void CheckPlacement(UWorld* World, FState& State, UPmcSquadSubsystem* Pmc, const UTacticalMapSubsystem* Map)
	{
		const FTacticalMapFile& File = Map->GetFile();
		const float Tolerance = State.Weights.LevelToleranceCm;
		int32 Posted = 0;
		bool bMarksman = false;
		bool bAmbusher = false;
		bool bLadderMan = false;
		for (int32 Index = 0; Index < State.Fighters.Num(); ++Index)
		{
			const AOperativeCharacter* Fighter = State.Fighters[Index].Get();
			const FPmcTacticalPost* Post = Pmc->GetFighterPost(Fighter);
			if (!Fighter || !Post)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke   fighter %d: no post (line spawn)"), Index);
				continue;
			}
			++Posted;
			const FVector Feet = FeetOf(*Fighter);
			const bool bAt = PmcTacticalRules::IsAtPost(Feet, Post->Location, 250.f, Tolerance);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke   %s: %s post %s (%s), feet %s, on it %d, danger %.2f"), *Fighter->DisplayName.ToString(),
				PmcTacticalRules::PostKindName(Post->Kind), *Post->Location.ToCompactString(), *Post->Group, *Feet.ToCompactString(), bAt ? 1 : 0, Post->Danger);
			if (Post->Kind == EPmcPostKind::Overwatch && bAt && File.OverwatchRoofs.IsValidIndex(Post->SourceIndex))
			{
				bMarksman = true;
			}
			if (Post->Kind == EPmcPostKind::Ambush && bAt && File.HidingSpots.IsValidIndex(Post->SourceIndex))
			{
				// At a chokepoint's EXIT: the hiding spot belongs to a place and he stands outside its outline (not IN it).
				const FTacticalHidingSpot& Spot = File.HidingSpots[Post->SourceIndex];
				const bool bOfPlace = File.Chokepoints.IsValidIndex(Spot.PlaceIndex);
				const bool bOutside = bOfPlace && TacticalMapRules::GetZonesNear(File, Map->GetConfig(), Spot.Location, 0.f).Find(Spot.PlaceIndex) == INDEX_NONE;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke     hiding spot %s of place %s, outside its outline %d"), *Spot.Id, *Spot.Place, bOutside ? 1 : 0);
				bAmbusher |= bOfPlace && bOutside;
			}
			if (Post->Kind == EPmcPostKind::LadderRoof && File.LadderOnlyRoofs.IsValidIndex(Post->SourceIndex))
			{
				bLadderMan |= State.LadderPicked.Contains(Post->SourceIndex) && FMath::Abs(Feet.Z - File.LadderOnlyRoofs[Post->SourceIndex].Z) <= Tolerance;
			}
		}
		Check(State, Posted >= 2, FString::Printf(TEXT("the squad spawned on tactical posts (%d of %d men)"), Posted, State.Fighters.Num()));
		Check(State, bMarksman, TEXT("the marksman stands on an overwatch roof spot"));
		Check(State, bAmbusher, TEXT("an ambusher stands at a chokepoint's exit (its hiding spot, outside the place's outline)"));
		const TArray<int32> Again = Map->PickLadderOnlyRoofs(State.Seed);
		Check(State, Again == State.LadderPicked && !State.LadderPicked.IsEmpty() && State.LadderPicked.Num() < File.LadderOnlyRoofs.Num() + 1,
			FString::Printf(TEXT("the seeded ladder-only roof picker ran: %d of %d roofs get an enemy (the file's %.2f), deterministic"), State.LadderPicked.Num(),
				File.LadderOnlyRoofs.Num(), File.Thresholds.LadderOnlyFactor));
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke   a man on a picked ladder-only roof: %s"), bLadderMan ? TEXT("yes") : TEXT("no (none near this route)"));
	}

	void CheckCommanded(UWorld* World, FState& State, UPmcSquadSubsystem* Pmc, const UTacticalMapSubsystem* Map)
	{
		// The positions the commander chose: where his men stand after the commanded spell (on their posts, Ambush) and the posts.
		float ChosenSum = 0.f;
		int32 Chosen = 0;
		int32 Holding = 0;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Fighters)
		{
			const AOperativeCharacter* Fighter = Weak.Get();
			if (!Fighter)
			{
				continue;
			}
			const FVector Feet = FeetOf(*Fighter);
			ChosenSum += Map->DangerAt(Feet, ETacticalSide::Pmc).Danger;
			++Chosen;
			const FPmcTacticalPost* Post = Pmc->GetFighterPost(Fighter);
			const EPmcOrder Order = Pmc->GetFighterOrder(Fighter);
			Holding += Post && Order == EPmcOrder::Ambush && PmcTacticalRules::IsAtPost(Feet, Post->Location, 300.f, State.Weights.LevelToleranceCm) ? 1 : 0;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke   %s after %.0f s commanded: %s at %s (danger %.2f)"), *Fighter->DisplayName.ToString(), CommandSeconds,
				PmcCommanderRules::OrderName(Order), *Feet.ToCompactString(), Map->DangerAt(Feet, ETacticalSide::Pmc).Danger);
		}
		Check(State, Holding >= 2, FString::Printf(TEXT("commanded with nobody in sight the men wait on their posts (Ambush): %d"), Holding));
		int32 Samples = 0;
		const float Random = RandomRouteDanger(World, State, Map, Samples);
		const float Mean = Chosen > 0 ? ChosenSum / Chosen : 1.f;
		Check(State, Samples >= 30 && Random > 0.f && Mean < Random,
			FString::Printf(TEXT("the commander's positions are safer for the PMC: mean DangerAt %.3f < %.3f of %d random points along the same way"), Mean, Random, Samples));
		// A fall-back next to the route's worst chokepoint goes round it (the map's dangers through PickFallBack).
		const FTacticalMapFile& File = Map->GetFile();
		int32 Worst = INDEX_NONE;
		for (int32 Index = 0; Index < File.Chokepoints.Num(); ++Index)
		{
			if (PmcTacticalRules::DistanceToRoute(State.Route, File.Chokepoints[Index].Location) <= 1500.f && File.Chokepoints[Index].Poly.Num() >= 3
				&& (Worst == INDEX_NONE || File.Chokepoints[Index].Score > File.Chokepoints[Worst].Score))
			{
				Worst = Index;
			}
		}
		if (Worst != INDEX_NONE)
		{
			const FTacticalChokepoint& Zone = File.Chokepoints[Worst];
			// A man 8 m before the place's narrowest point, the threat behind him: straight away runs him into it.
			const FVector Into = Zone.Line.Num() >= 2 ? (Zone.Line.Last() - Zone.Line[0]).GetSafeNormal2D() : FVector::ForwardVector;
			const FVector Man = Zone.Location - Into * 800.f;
			const FVector Threat = Man - Into * 2000.f;
			const FVector Straight = PmcCommanderRules::FallBackPoint(Man, Threat);
			const TArray<float> Angles = PmcTacticalRules::FallBackAngles();
			TArray<float> Dangers;
			for (const float Angle : Angles)
			{
				Dangers.Add(Map->DangerAt(Man + (Straight - Man).RotateAngleAxis(Angle, FVector::UpVector), ETacticalSide::Pmc).Danger);
			}
			const int32 Pick = PmcTacticalRules::PickFallBack(Angles, Dangers, State.Weights);
			Check(State, Pick != INDEX_NONE && Dangers[Pick] <= Dangers[0],
				FString::Printf(TEXT("a fall-back before %s (score %.2f) avoids it: %+.0f deg, danger %.2f (straight %.2f)"), *Zone.Id, Zone.Score,
					Pick != INDEX_NONE ? Angles[Pick] : 0.f, Pick != INDEX_NONE ? Dangers[Pick] : 1.f, Dangers[0]));
		}
	}

	bool Step(UWorld* World, FState& State, float DeltaSeconds)
	{
		switch (State.PreloadGate.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting:
			return true;
		case SmokeUtils::EPreloadGate::Failed:
			Check(State, false, State.PreloadGate.FailReason);
			return Finish(State, World, false);
		default:
			break;
		}
		State.Time += DeltaSeconds;
		State.StageTime += DeltaSeconds;
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		UTacticalMapSubsystem* Tactical = World->GetSubsystem<UTacticalMapSubsystem>();
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Pmc || !Tactical || State.Time > TimeoutSeconds)
		{
			Check(State, false, FString::Printf(TEXT("finished in time (stage %d, %.1f s)"), static_cast<int32>(State.Stage), State.Time));
			return Finish(State, World, false);
		}
		switch (State.Stage)
		{
		case EStage::Begin:
		{
			if (State.StageTime < WarmupSeconds || !Nav || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))
			{
				return true;
			}
			Check(State, Pmc->GetTacticalMap() != nullptr, FString::Printf(TEXT("the map's tactical file is loaded (%s)"), *FPaths::GetCleanFilename(Tactical->GetLoadedPath())));
			if (!Pmc->GetTacticalMap() || !PickRoute(World, State, Pmc))
			{
				Check(State, false, TEXT("a route from the squad's start to a mission goal"));
				return Finish(State, World, false);
			}
			// Without the tactical file the gather is empty: the old line spawn (PmcTacticalRules gets no candidates).
			const FString Real = Tactical->GetLoadedPath();
			Tactical->Clear();
			Check(State, Pmc->GetTacticalMap() == nullptr && Pmc->GatherPostCandidates(State.Route, State.Seed, State.Weights).IsEmpty(),
				TEXT("no tactical file: no post candidates (the old placement / commander)"));
			Check(State, Tactical->LoadFromFile(Real), TEXT("the map's tactical file loads again"));
			ParkPlayerSquad(World);
			Pmc->SetTeamWeights(TeamId, State.Weights);
			const FVector End = State.Route.Last();
			const FRotator Facing = (State.Route[0] - End).GetSafeNormal2D().Rotation();
			for (AOperativeCharacter* Fighter : Pmc->SpawnSquadTactical(TeamId, State.Route, End, Facing, State.Count, State.Seed, &State.Posts, &State.LadderPicked))
			{
				State.Fighters.Add(Fighter);
			}
			Check(State, State.Fighters.Num() == State.Count, FString::Printf(TEXT("%d of %d PMC fighters spawned"), State.Fighters.Num(), State.Count));
			State.Stage = EStage::Settle;
			State.StageTime = 0.f;
			return true;
		}
		case EStage::Settle:
			if (State.StageTime < SettleSeconds)
			{
				return true;
			}
			CheckPlacement(World, State, Pmc, Tactical);
			Pmc->SetTeamCommanded(TeamId, true, &State.Weights);
			State.Stage = EStage::Command;
			State.StageTime = 0.f;
			return true;
		case EStage::Command:
			if (State.StageTime < CommandSeconds)
			{
				return true;
			}
			CheckCommanded(World, State, Pmc, Tactical);
			if (const FPmcTeamStats* Stats = Pmc->GetTeamStats(TeamId))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke   team stats: orders %d, post walks %d, tactical fall-backs %d"), Stats->OrdersIssued, Stats->PostMoves, Stats->TacticalFallBacks);
			}
			return Finish(State, World, true);
		default:
			return false;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		for (const FString& Arg : Args)
		{
			FString Key;
			FString Value;
			if (Arg.Split(TEXT("="), &Key, &Value))
			{
				if (Key.Equals(TEXT("seed"), ESearchCase::IgnoreCase)) { State->Seed = FCString::Atoi(*Value); }
				else if (Key.Equals(TEXT("count"), ESearchCase::IgnoreCase)) { State->Count = FMath::Clamp(FCString::Atoi(*Value), 2, 6); }
			}
		}
		PmcCommanderRules::LoadWeightsFile(FString(), State->Weights);
		// The squad meets nobody: the pressure director, the level's PMC encounters and the horde stay off.
		for (const TCHAR* Name : { TEXT("Codex.Pressure.Enabled"), TEXT("Codex.Pmc.Encounters"), TEXT("Codex.Horde.Enabled") })
		{
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
			{
				Variable->Set(0, ECVF_SetByConsole);
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: TacticalPmcSmoke on %s (seed %d, %d men)"), *World->GetMapName(), State->Seed, State->Count);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			if (!W)
			{
				return Finish(*State, nullptr, false);
			}
			return State->Stage != EStage::Done && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TacticalPmcSmoke"),
		TEXT("Dev check (TACTICAL_MAP P2, designer 2026-11-17): a PMC squad spawned with the generated map's tactical file - the marksman on an overwatch ")
		TEXT("roof spot, an ambusher at a chokepoint exit, the seeded ladder-only roof picker, the men hold their posts when commanded, the chosen ")
		TEXT("positions safer (DangerAt) than random ones along the way, fall-backs round a chokepoint, no file = no posts. [seed=<n>] [count=<men>]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
