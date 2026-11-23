// Dev-only console command (TACTICAL_MAP P1, designer 2026-11-17: "bots read the terrain"), on L_Gen_Depot_X4:
//   Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands TacticalMapSmoke
// UTacticalMapSubsystem loaded Content/Data/LevelGen/tactical/L_Gen_Depot_X4.tactical.json at BeginPlay; the item counts equal the
// JSON's lists; every spot (place, drop edge / land, window stand, roof spot, hiding spot + its threatened point, ladder-only roof
// centre / ladder top) projects onto the navmesh or stands on a walkable surface (roof, wall top, sill) within the tolerance of
// tactical_runtime.json; DangerAt is high inside the top place and low on an open street; the queries answer on the real map; the
// ladder-only roof picker is deterministic per seed and picks PART of them; a missing file = an empty map; the debug draw runs.
// [map=<L_Gen_...>] for another generated map.
// T-02 (smart-enemies roadmap): load-only aliases TacticalMapLabCrossroadsSmoke / TacticalMapLabAlleySmoke / TacticalMapLabBridgeSmoke for the
// lab maps L_Lab_* (file loaded, counts = JSON, every spot grounded - an empty list is fine -, missing-file reload, draw / dump; no queries).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "LevelGen/TacticalMapRules.h"
#include "LevelGen/TacticalMapSubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace TacticalMapSmoke
{
	constexpr float WarmupSeconds = 2.f;
	constexpr float DrawSeconds = 0.5f;
	constexpr float TimeoutSeconds = 120.f;

	enum class EStage : uint8
	{
		Begin,
		Draw,
		Done
	};

	struct FState
	{
		EStage Stage = EStage::Begin;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		bool bLoadOnly = false; // T-02: the lab-map aliases skip the queries and allow empty spot lists
		FString MapName;
		SmokeUtils::FPreloadGate PreloadGate;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		State.Stage = EStage::Done;
		if (IConsoleVariable* Draw = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.TacticalMap.Draw")))
		{
			Draw->Set(0, ECVF_SetByConsole);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TacticalMapSmoke"));
		return false;
	}

	/** The number of items of each top-level list in the raw JSON. */
	TMap<FString, int32> RawCounts(const FString& Path)
	{
		TMap<FString, int32> Out;
		FString Json;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Json, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
		{
			return Out;
		}
		for (const TCHAR* Key : { TEXT("chokepoints"), TEXT("drop_spots"), TEXT("windows"), TEXT("overwatch_roofs"), TEXT("hiding_spots"), TEXT("ladder_only_roofs") })
		{
			const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
			Out.Add(Key, Root->TryGetArrayField(Key, List) && List ? List->Num() : -1);
		}
		return Out;
	}

	/** On the navmesh (within the projection box) or on a walkable surface within the tolerance of the spot's z (roof / wall top / sill). */
	bool Grounded(UWorld* World, UNavigationSystemV1* Nav, const FTacticalRuntimeConfig& Config, const FVector& Spot)
	{
		FNavLocation OnNav;
		if (Nav && Nav->ProjectPointToNavigation(Spot, OnNav, Config.ProjectExtentCm))
		{
			return true;
		}
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TacticalMapSmoke), true);
		const FVector From = Spot + FVector(0.f, 0.f, Config.SurfaceToleranceCm);
		const FVector To = Spot - FVector(0.f, 0.f, Config.SurfaceToleranceCm);
		return World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params) && Hit.ImpactNormal.Z > 0.7
			&& FMath::Abs(Hit.ImpactPoint.Z - Spot.Z) <= Config.SurfaceToleranceCm;
	}

	/** Checks one category of spots; logs the first misses. */
	void CheckGrounded(FState& State, UWorld* World, UNavigationSystemV1* Nav, const FTacticalRuntimeConfig& Config, const TCHAR* What,
		const TArray<TPair<FString, FVector>>& Spots, bool bAllowEmpty = false)
	{
		int32 Misses = 0;
		for (const TPair<FString, FVector>& Spot : Spots)
		{
			if (!Grounded(World, Nav, Config, Spot.Value))
			{
				if (++Misses <= 5)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke   %s %s at %s: no navmesh / walkable surface within tolerance"), What, *Spot.Key, *Spot.Value.ToCompactString());
				}
			}
		}
		Check(State, (bAllowEmpty || Spots.Num() > 0) && Misses == 0, FString::Printf(TEXT("%s: %d of %d on the navmesh or a walkable surface"), What, Spots.Num() - Misses, Spots.Num()));
	}

	/** The least dangerous outdoor navmesh point on the ground at least OpenClearanceCm from every place (the "open street"). */
	bool FindOpenStreet(UWorld* World, UNavigationSystemV1* Nav, const UTacticalMapSubsystem* Tactical, FVector& Out, float& OutDanger)
	{
		const FTacticalMapFile& File = Tactical->GetFile();
		const FTacticalRuntimeConfig& Config = Tactical->GetConfig();
		FBox2D Box(ForceInit);
		for (const FTacticalChokepoint& Zone : File.Chokepoints)
		{
			Box += FVector2D(Zone.Location.X, Zone.Location.Y);
		}
		for (const FTacticalHidingSpot& Spot : File.HidingSpots)
		{
			Box += FVector2D(Spot.Location.X, Spot.Location.Y);
		}
		if (!Box.bIsValid)
		{
			return false;
		}
		bool bFound = false;
		OutDanger = 2.f;
		constexpr double Step = 1000.0;
		for (double X = Box.Min.X; X <= Box.Max.X; X += Step)
		{
			for (double Y = Box.Min.Y; Y <= Box.Max.Y; Y += Step)
			{
				const FVector Point(X, Y, File.GroundZ);
				bool bClear = true;
				for (const FTacticalChokepoint& Zone : File.Chokepoints)
				{
					if (TacticalMapRules::DistanceToPolygon(FVector2D(X, Y), Zone.Poly) < Config.SmokeOpenClearanceCm)
					{
						bClear = false;
						break;
					}
				}
				FNavLocation OnNav;
				if (!bClear || !Nav->ProjectPointToNavigation(Point, OnNav, FVector(50.f, 50.f, 100.f)) || FMath::Abs(OnNav.Location.Z - File.GroundZ) > 60.f)
				{
					continue;
				}
				FHitResult Roof;
				if (World->LineTraceSingleByChannel(Roof, OnNav.Location + FVector(0.f, 0.f, 150.f), OnNav.Location + FVector(0.f, 0.f, 3000.f), ECC_Visibility))
				{
					continue; // under a roof: not a street
				}
				const float Danger = Tactical->DangerAt(OnNav.Location, ETacticalSide::Squad).Danger;
				if (Danger < OutDanger)
				{
					OutDanger = Danger;
					Out = OnNav.Location;
					bFound = true;
				}
			}
		}
		return bFound;
	}

	void RunChecks(UWorld* World, FState& State, UTacticalMapSubsystem* Tactical, UNavigationSystemV1* Nav)
	{
		const FString Expected = TacticalMapRules::GetDefaultPathForMap(State.MapName);
		Check(State, Tactical->IsLoaded() && FPaths::IsSamePath(Tactical->GetLoadedPath(), Expected),
			FString::Printf(TEXT("the map's tactical file loaded at BeginPlay (%s)"), *FPaths::GetCleanFilename(Tactical->GetLoadedPath())));
		if (!Tactical->IsLoaded())
		{
			return;
		}
		const FTacticalMapFile& File = Tactical->GetFile();
		const FTacticalRuntimeConfig& Config = Tactical->GetConfig();
		Check(State, File.Map == State.MapName, FString::Printf(TEXT("the file is for this map (%s)"), *File.Map));

		// the counts match the JSON
		const TMap<FString, int32> Raw = RawCounts(Expected);
		const TPair<const TCHAR*, int32> Counts[] = {
			{ TEXT("chokepoints"), File.Chokepoints.Num() }, { TEXT("drop_spots"), File.DropSpots.Num() }, { TEXT("windows"), File.Windows.Num() },
			{ TEXT("overwatch_roofs"), File.OverwatchRoofs.Num() }, { TEXT("hiding_spots"), File.HidingSpots.Num() }, { TEXT("ladder_only_roofs"), File.LadderOnlyRoofs.Num() } };
		for (const TPair<const TCHAR*, int32>& Count : Counts)
		{
			const int32* Found = Raw.Find(Count.Key);
			const int32 InJson = Found ? *Found : -1;
			Check(State, InJson == Count.Value && InJson >= 0, FString::Printf(TEXT("%s: %d loaded = %d in the JSON"), Count.Key, Count.Value, InJson));
		}
		if (!State.bLoadOnly)
		{
			Check(State, !File.Chokepoints.IsEmpty() && !File.HidingSpots.IsEmpty() && !File.OverwatchRoofs.IsEmpty(), TEXT("the map has places, hiding spots and roofs"));
			if (File.Chokepoints.IsEmpty())
			{
				return; // the checks below read the top place
			}
		}

		// every spot projects onto the navmesh or a walkable surface
		TArray<TPair<FString, FVector>> Places, DropLand, DropEdge, Windows, Roofs, Hiding, Threats, LadderRoofs;
		for (const FTacticalChokepoint& Zone : File.Chokepoints) { Places.Emplace(Zone.Id, Zone.Location); }
		for (const FTacticalDropSpot& Spot : File.DropSpots) { DropLand.Emplace(Spot.Id, Spot.Land); DropEdge.Emplace(Spot.Id, Spot.Edge); }
		for (const FTacticalWindow& W : File.Windows) { Windows.Emplace(W.Id, W.Stand); }
		for (const FTacticalOverwatchRoof& Roof : File.OverwatchRoofs)
		{
			for (int32 I = 0; I < Roof.Spots.Num(); ++I) { Roofs.Emplace(FString::Printf(TEXT("%s#%d"), *Roof.Building, I), Roof.Spots[I].Location); }
		}
		for (const FTacticalHidingSpot& Spot : File.HidingSpots) { Hiding.Emplace(Spot.Id, Spot.Location); Threats.Emplace(Spot.Id, Spot.ThreatLocation); }
		for (const FTacticalLadderRoof& Roof : File.LadderOnlyRoofs)
		{
			LadderRoofs.Emplace(Roof.Building, Roof.Center);
			for (int32 I = 0; I < Roof.LadderTops.Num(); ++I) { LadderRoofs.Emplace(FString::Printf(TEXT("%s top %d"), *Roof.Building, I), Roof.LadderTops[I]); }
		}
		CheckGrounded(State, World, Nav, Config, TEXT("places"), Places, State.bLoadOnly);
		CheckGrounded(State, World, Nav, Config, TEXT("drop landing points"), DropLand, State.bLoadOnly);
		CheckGrounded(State, World, Nav, Config, TEXT("drop edges (roof / wall top)"), DropEdge, State.bLoadOnly);
		CheckGrounded(State, World, Nav, Config, TEXT("window stand points"), Windows, State.bLoadOnly);
		CheckGrounded(State, World, Nav, Config, TEXT("overwatch roof spots"), Roofs, State.bLoadOnly);
		CheckGrounded(State, World, Nav, Config, TEXT("hiding spots"), Hiding, State.bLoadOnly);
		CheckGrounded(State, World, Nav, Config, TEXT("threatened route points"), Threats, State.bLoadOnly);
		CheckGrounded(State, World, Nav, Config, TEXT("ladder-only roofs (centre + ladder tops)"), LadderRoofs, State.bLoadOnly);

		if (State.bLoadOnly)
		{
			return; // T-02: the queries below need places, hiding spots and roofs
		}
		// DangerAt: high inside the top place, low on an open street
		const FTacticalChokepoint& Top = File.Chokepoints[0];
		const FTacticalDanger InTop = Tactical->DangerAt(Top.Location, ETacticalSide::Squad);
		Check(State, InTop.Danger >= Config.SmokeHighDanger && InTop.Danger >= Top.Score,
			FString::Printf(TEXT("danger inside the top place %s (%s %.2f) = %.2f >= %.2f (%s)"), *Top.Id, *Top.Kind.ToString(), Top.Score, InTop.Danger,
				Config.SmokeHighDanger, *TacticalMapRules::JoinKinds(InTop.Kinds)));
		FVector Open = FVector::ZeroVector;
		float OpenDanger = 1.f;
		const bool bOpen = FindOpenStreet(World, Nav, Tactical, Open, OpenDanger);
		Check(State, bOpen && OpenDanger <= Config.SmokeLowDanger,
			FString::Printf(TEXT("an open street %s (>= %.0f m from every place, no roof) has danger %.2f <= %.2f"), *Open.ToCompactString(),
				Config.SmokeOpenClearanceCm / 100.f, OpenDanger, Config.SmokeLowDanger));
		Check(State, Tactical->DangerAt(Top.Location, ETacticalSide::Pmc).Danger >= Top.Score - KINDA_SMALL_NUMBER, TEXT("PMC side: the top place is dangerous for it too"));

		// the queries on the real map
		const FVector LineStart = Top.Line.Num() >= 2 ? Top.Line[0] : Top.Location - FVector(500.f, 0.f, 0.f);
		const FVector LineEnd = Top.Line.Num() >= 2 ? Top.Line.Last() : Top.Location + FVector(500.f, 0.f, 0.f);
		const FVector Before = LineStart + (LineStart - LineEnd).GetSafeNormal() * 500.f, After = LineEnd + (LineEnd - LineStart).GetSafeNormal() * 500.f;
		int32 Crossed = INDEX_NONE;
		const FVector Path[] = { Before, After };
		const bool bCrossed = Tactical->IsChokepoint(Path, &Crossed) && Crossed != INDEX_NONE; // before the message reads Crossed
		Check(State, bCrossed,
			FString::Printf(TEXT("IsChokepoint: the path along %s crosses %s"), *Top.Id, Crossed != INDEX_NONE ? *File.Chokepoints[Crossed].Id : TEXT("nothing")));
		const FTacticalDetour Detour = Tactical->SafeDetour(Before, After);
		Check(State, Detour.bCrossesChokepoint, FString::Printf(TEXT("SafeDetour: crosses (%s, detour %.1f m, take it: %s, only way: %s)"), *Detour.Alternative, Detour.DetourM,
			Detour.bTakeDetour ? TEXT("yes") : TEXT("no"), Detour.bOnlyWay ? TEXT("yes") : TEXT("no")));
		const TArray<FTacticalPick> Ambush = Tactical->BestAmbushSpots(Before, After, 3);
		Check(State, !Ambush.IsEmpty(), FString::Printf(TEXT("BestAmbushSpots along %s: %d (best %s %.2f)"), *Top.Id, Ambush.Num(),
			Ambush.IsEmpty() ? TEXT("-") : *File.HidingSpots[Ambush[0].Index].Id, Ambush.IsEmpty() ? 0.f : Ambush[0].Score));
		const TArray<FTacticalPick> Over = Tactical->OverwatchSpots(Top.Location, 0.f, 3);
		Check(State, !Over.IsEmpty(), FString::Printf(TEXT("OverwatchSpots over %s: %d (best %s)"), *Top.Id, Over.Num(), Over.IsEmpty() ? TEXT("-") : *File.OverwatchRoofs[Over[0].Index].Building));
		const TArray<FTacticalPick> Windows2 = Tactical->WindowPositions(Top.Location, 500.f);
		Check(State, Top.Windows.IsEmpty() || !Windows2.IsEmpty(), FString::Printf(TEXT("WindowPositions over %s: %d (the analysis lists %d)"), *Top.Id, Windows2.Num(), Top.Windows.Num()));
		if (!File.DropSpots.IsEmpty())
		{
			const TArray<FTacticalPick> Drops = Tactical->DropSpotsAbove(File.DropSpots[0].Land, 100.f);
			Check(State, !Drops.IsEmpty() && Drops[0].Index == 0, FString::Printf(TEXT("DropSpotsAbove %s's landing point finds it"), *File.DropSpots[0].Id));
		}
		Check(State, !Tactical->GetZonesNear(Top.Location, 100.f).IsEmpty(), TEXT("GetZonesNear the top place finds it"));

		// the ladder-only roof picker: deterministic per seed, PART of them on average
		const int32 RoofCount = File.LadderOnlyRoofs.Num();
		Check(State, RoofCount > 0, FString::Printf(TEXT("ladder-only roofs: %d (factor %.2f)"), RoofCount, File.Thresholds.LadderOnlyFactor));
		if (RoofCount > 0)
		{
			const TArray<int32> First = Tactical->PickLadderOnlyRoofs(42);
			Check(State, Tactical->PickLadderOnlyRoofs(42) == First, FString::Printf(TEXT("the picker is deterministic per seed (seed 42: %d of %d)"), First.Num(), RoofCount));
			int32 Total = 0;
			bool bVaries = false;
			for (int32 Seed = 1; Seed <= 200; ++Seed)
			{
				const TArray<int32> Pick = Tactical->PickLadderOnlyRoofs(Seed);
				Total += Pick.Num();
				bVaries |= Pick != First;
			}
			const float Share = static_cast<float>(Total) / (200.f * RoofCount);
			Check(State, bVaries && FMath::Abs(Share - File.Thresholds.LadderOnlyFactor) <= 0.1f,
				FString::Printf(TEXT("over 200 seeds the picks vary and %.0f%% of the roofs get an enemy (factor %.0f%%)"), Share * 100.f, File.Thresholds.LadderOnlyFactor * 100.f));
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
			return Finish(State, false);
		default:
			break;
		}
		State.Time += DeltaSeconds;
		State.StageTime += DeltaSeconds;
		UTacticalMapSubsystem* Tactical = World->GetSubsystem<UTacticalMapSubsystem>();
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Tactical || State.Time > TimeoutSeconds)
		{
			Check(State, false, FString::Printf(TEXT("finished in time (stage %d, %.1f s)"), static_cast<int32>(State.Stage), State.Time));
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case EStage::Begin:
		{
			if (State.StageTime < WarmupSeconds || !Nav || Nav->IsNavigationBuildInProgress())
			{
				return true;
			}
			RunChecks(World, State, Tactical, Nav);
			// a missing file = an empty map (no errors, every query empty), then the real one again
			const FString Real = Tactical->GetLoadedPath();
			const bool bMissing = Tactical->LoadFromFile(FPaths::ProjectSavedDir() / TEXT("TacticalMapSmoke/none.tactical.json"));
			Check(State, !bMissing && !Tactical->IsLoaded() && Tactical->GetFile().IsEmpty() && Tactical->GetDangerAt(FVector::ZeroVector) == 0.f
				&& Tactical->BestAmbushSpots(FVector::ZeroVector, FVector(1000.f, 0.f, 0.f), 3).IsEmpty(), TEXT("a missing file = an empty map, every query answers nothing"));
			Check(State, !Real.IsEmpty() && Tactical->LoadFromFile(Real), TEXT("the map's file loads again"));
			if (IConsoleVariable* Draw = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.TacticalMap.Draw")))
			{
				Draw->Set(1, ECVF_SetByConsole);
			}
			State.Stage = EStage::Draw;
			State.StageTime = 0.f;
			return true;
		}
		case EStage::Draw:
			if (State.StageTime < DrawSeconds)
			{
				return true;
			}
			Tactical->Dump();
			Check(State, true, TEXT("Codex.TacticalMap.Draw 1 ran for half a second and Codex.TacticalMap.Dump logged"));
			return Finish(State, true);
		default:
			return false;
		}
	}

	void Start(const TArray<FString>& Args, UWorld* World, bool bLoadOnly)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->bLoadOnly = bLoadOnly;
		State->MapName = UWorld::RemovePIEPrefix(World->GetMapName());
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("map=")))
			{
				State->MapName = FPaths::GetBaseFilename(Arg.Mid(4));
			}
		}
		// a data check, not a fight: the pressure director and the level's PMC encounters stay off
		for (const TCHAR* Name : { TEXT("Codex.Pressure.Enabled"), TEXT("Codex.Pmc.Encounters") })
		{
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
			{
				Variable->Set(0, ECVF_SetByConsole);
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: TacticalMapSmoke on %s%s"), *State->MapName, bLoadOnly ? TEXT(" (load-only)") : TEXT(""));
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			if (!W)
			{
				return Finish(*State, false);
			}
			return State->Stage != EStage::Done && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
		}), 0.f);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		Start(Args, World, false);
	}

	void RunLoadOnly(const TArray<FString>& Args, UWorld* World)
	{
		Start(Args, World, true);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TacticalMapSmoke"),
		TEXT("Dev check (TACTICAL_MAP P1, designer 2026-11-17): the generated map's tactical file - loaded at BeginPlay, counts = JSON, every spot on the ")
		TEXT("navmesh / a walkable surface, danger high in the top place and low on an open street, the queries, the seeded ladder-roof picker, ")
		TEXT("a missing file = an empty map, the debug draw. [map=<L_Gen_...>]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));

	// T-02 (smart-enemies roadmap): load-only aliases for the lab maps L_Lab_*
	static FAutoConsoleCommandWithWorldAndArgs LabCrossroadsCommand(TEXT("CodexTactics.TacticalMapLabCrossroadsSmoke"),
		TEXT("Dev check (T-02, DS-00A): L_Lab_Crossroads tactical file, load-only - see CodexTactics.TacticalMapSmoke. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunLoadOnly));
	static FAutoConsoleCommandWithWorldAndArgs LabAlleyCommand(TEXT("CodexTactics.TacticalMapLabAlleySmoke"),
		TEXT("Dev check (T-02, DS-00B): L_Lab_Alley tactical file, load-only - see CodexTactics.TacticalMapSmoke. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunLoadOnly));
	static FAutoConsoleCommandWithWorldAndArgs LabBridgeCommand(TEXT("CodexTactics.TacticalMapLabBridgeSmoke"),
		TEXT("Dev check (T-02, DS-00C): L_Lab_Bridge tactical file, load-only - see CodexTactics.TacticalMapSmoke. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunLoadOnly));
}

#endif // !UE_BUILD_SHIPPING
