// Dev-only headless check of a GENERATED map (docs/design/levelgen/LEVEL_GENERATOR.md section 8): the squad walks the mandatory
// route of the accepted layout the map was built from (Content/Data/LevelGen/accepted/<name>.json, named by the level JSON's
// "levelgen_layout"; Scripts/Editor/build_levelgen_map.py) with the leader's move orders - the FormationLabSmoke carrot pattern
// (a click 6 m ahead on the leg, renewed every second; the squad follows in formation) - through the ladder / stair nav links of
// the map; a leg has three times its walking time to finish (at least 25 s). A leg that drops through a ROOF / FLOOR HOLE has no
// nav link yet: the squad is teleported to the leg's end and the drop is reported as "known" (not a failure). Reports completion
// or the leg where the squad got stuck (the leg's layer, the leader's position, the last order), then PASS / FAIL and exits.
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands LevelGenRouteSmoke -ExtraArgs "-Multiprocess"
//   (map /Game/Maps/Generated/L_Gen_Depot_1, "-LevelJson=L_Gen_Depot_1.json" - Scripts/test_map.json smokeOptions)
// Arguments: map=<L_Gen_...> (the generated map to check - its level JSON / accepted layout; it must be the loaded map: run the game on it,
// e.g. Scripts/levelgen_route_smoke.ps1 -Map /Game/Maps/Generated/<map>), layout=<accepted name> (default: the level JSON's
// levelgen_layout, else the newest accepted file), route=<id> (default the mandatory route), walk=bot|carrot (default bot).
// THE BOT ROUTE MODE (2026-11-02, the hook of LEVEL_GENERATOR.md section 8): when the level JSON has a "bot_route" (the builder writes it
// from the mandatory route, Scripts/Editor/levelgen_bot_route.py) the walk is handed to the playtest bot's route mode (Codex.Bot.Route,
// PlaytestBotRouteMode.cpp: the player's own clicks, the formation, the drop nav links through the holes), the pressure switched off
// (Codex.Pressure.Enabled 0: a walk check, not a fight); the smoke only reads its tracker: PASS = every waypoint of the route reached once
// with none skipped; each hole drop is reported (fell through the drop nav link / scripted by the bot). No bot route (or walk=carrot):
// the carrot walk below, as before (its hole drops teleported).
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Bot/BotRouteRules.h"
#include "Bot/PlaytestBotSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Ladders/LadderClimbComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"

namespace LevelGenRouteSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float LevelHeightCm = 300.f;     // the layout's height levels (schema.ts LEVEL_HEIGHT)
	constexpr float PointReachCm = 90.f;
	constexpr float CornerTurnCm = 150.f;
	constexpr float CarrotSeconds = 1.f;
	constexpr float CarrotAheadCm = 600.f;
	constexpr float WalkSpeedCmPerSec = 176.f;  // pace.walk_speed 220 x pace.patrol 0.8 (formation_config.json)
	constexpr float LegMinSeconds = 25.f;
	constexpr float LevelTolCm = 160.f;        // the leader stands on the leg's level within this (a ladder / ramp arrival)
	constexpr float StuckReorderSeconds = 6.f;
	/** The bot route walk must have reached every waypoint within this much game time, s. */
	constexpr float BotRouteLimitSeconds = 1500.f;

	struct FLeg
	{
		FVector2D From;
		FVector2D To;
		int32 LevelFrom = 0;
		int32 LevelTo = 0;
		FString LayerTo;
		/** The link the leg goes through ('' = none; a 'hole_*' link = a drop the smoke teleports across). */
		FString Via;
	};

	enum class EStage : uint8 { Walk, BotRoute, Done };

	struct FState
	{
		FString LayoutName;
		FString RouteId;
		TArray<FLeg> Legs;
		int32 Leg = 0;
		float LegTime = 0.f;
		float CarrotTime = CarrotSeconds;
		float StillTime = 0.f;
		float Total = 0.f;
		int32 Failures = 0;
		int32 Teleports = 0;
		bool bLegClimbed = false;
		FVector LastOrder = FVector::ZeroVector;
		EStage Stage = EStage::Walk;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: LevelGenRouteSmoke %s route %s (%s): %d / %d %s, %.0f s, %d hole drops %s"), *State.LayoutName, *State.RouteId,
			State.Stage == EStage::BotRoute ? TEXT("bot route mode") : TEXT("carrot walk"), State.Leg, State.Stage == EStage::BotRoute ? State.Legs.Num() + 1 : State.Legs.Num(),
			State.Stage == EStage::BotRoute ? TEXT("waypoints") : TEXT("legs"), State.Total, State.Teleports,
			State.Stage == EStage::BotRoute ? TEXT("scripted") : TEXT("teleported"));
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("LevelGenRouteSmoke"));
		return false;
	}

	FVector2D ReadPoint(const TArray<TSharedPtr<FJsonValue>>& Array)
	{
		return Array.Num() >= 2 ? FVector2D(Array[0]->AsNumber(), Array[1]->AsNumber()) : FVector2D::ZeroVector;
	}

	/** The height level of a layer: ground 0, cut -1, floor:<b>:<n> = n, roof:<b> = the building's floors (schema.ts layerLevel). */
	int32 LayerLevel(const TSharedPtr<FJsonObject>& Layout, const FString& Layer)
	{
		if (Layer == TEXT("ground"))
		{
			return 0;
		}
		if (Layer == TEXT("cut"))
		{
			const TSharedPtr<FJsonObject>* Cut = nullptr;
			return Layout->TryGetObjectField(TEXT("cut"), Cut) && Cut ? static_cast<int32>((*Cut)->GetNumberField(TEXT("level"))) : -1;
		}
		TArray<FString> Parts;
		Layer.ParseIntoArray(Parts, TEXT(":"));
		if (Parts.Num() < 2)
		{
			return 0;
		}
		const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
		if (Layout->TryGetArrayField(TEXT("buildings"), Buildings))
		{
			for (const TSharedPtr<FJsonValue>& V : *Buildings)
			{
				const TSharedPtr<FJsonObject>& B = V->AsObject();
				if (B.IsValid() && B->GetStringField(TEXT("id")) == Parts[1])
				{
					return Parts[0] == TEXT("roof") ? static_cast<int32>(B->GetNumberField(TEXT("floors"))) : (Parts.Num() >= 3 ? FCString::Atoi(*Parts[2]) : 1);
				}
			}
		}
		return 0;
	}

	/** The accepted layout file: the argument, the level JSON's "levelgen_layout", else the newest accepted file. */
	/** The loaded map's leaf name (no PIE prefix). */
	FString LoadedMapName(const UWorld* World)
	{
		return World ? World->GetMapName().Replace(TEXT("UEDPIE_0_"), TEXT("")) : FString();
	}

	FString FindLayoutFile(const UWorld* World, const FString& Arg, const FString& MapArg)
	{
		const FString Accepted = FPaths::ProjectContentDir() / TEXT("Data/LevelGen/accepted");
		if (!Arg.IsEmpty())
		{
			return Accepted / Arg + TEXT(".json");
		}
		const FString MapName = MapArg.IsEmpty() ? LoadedMapName(World) : MapArg;
		const FString LevelFile = FPaths::ProjectContentDir() / TEXT("Data/LevelJson") / MapName + TEXT(".json");
		FString Text;
		if (FFileHelper::LoadFileToString(Text, *LevelFile))
		{
			TSharedPtr<FJsonObject> Level;
			if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Level) && Level.IsValid() && Level->HasField(TEXT("levelgen_layout")))
			{
				return FPaths::ProjectDir() / Level->GetStringField(TEXT("levelgen_layout"));
			}
		}
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Accepted / TEXT("*.json")), true, false);
		FString Best;
		FDateTime BestTime = FDateTime::MinValue();
		for (const FString& F : Files)
		{
			if (F.EndsWith(TEXT(".meta.json")))
			{
				continue;
			}
			const FDateTime T = IFileManager::Get().GetTimeStamp(*(Accepted / F));
			if (T > BestTime)
			{
				BestTime = T;
				Best = Accepted / F;
			}
		}
		return Best;
	}

	bool LoadLegs(const UWorld* World, FState& State, const FString& LayoutArg, const FString& RouteArg, const FString& MapArg, FString& Error)
	{
		const FString File = FindLayoutFile(World, LayoutArg, MapArg);
		FString Text;
		if (File.IsEmpty() || !FFileHelper::LoadFileToString(Text, *File))
		{
			Error = FString::Printf(TEXT("no accepted layout file (%s)"), *File);
			return false;
		}
		TSharedPtr<FJsonObject> Layout;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Layout) || !Layout.IsValid())
		{
			Error = FString::Printf(TEXT("%s: not JSON"), *File);
			return false;
		}
		State.LayoutName = Layout->GetStringField(TEXT("name"));
		const TArray<TSharedPtr<FJsonValue>>* Routes = nullptr;
		if (!Layout->TryGetArrayField(TEXT("routes"), Routes))
		{
			Error = TEXT("no routes");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& V : *Routes)
		{
			const TSharedPtr<FJsonObject>& R = V->AsObject();
			if (!R.IsValid())
			{
				continue;
			}
			const FString Id = R->GetStringField(TEXT("id"));
			const bool bMandatory = R->HasField(TEXT("mandatory")) && R->GetBoolField(TEXT("mandatory"));
			if (RouteArg.IsEmpty() ? !bMandatory : Id != RouteArg)
			{
				continue;
			}
			State.RouteId = Id;
			const TArray<TSharedPtr<FJsonValue>>& Points = R->GetArrayField(TEXT("points"));
			const TArray<TSharedPtr<FJsonValue>>* Layers = nullptr;
			R->TryGetArrayField(TEXT("layers"), Layers);
			TMap<int32, FString> Links;
			const TArray<TSharedPtr<FJsonValue>>* LinkArray = nullptr;
			if (R->TryGetArrayField(TEXT("links"), LinkArray))
			{
				for (const TSharedPtr<FJsonValue>& L : *LinkArray)
				{
					const TSharedPtr<FJsonObject>& O = L->AsObject();
					if (O.IsValid())
					{
						Links.Add(static_cast<int32>(O->GetNumberField(TEXT("at"))), O->GetStringField(TEXT("via")));
					}
				}
			}
			auto LayerAt = [&](int32 i) { return Layers && Layers->IsValidIndex(i) ? (*Layers)[i]->AsString() : FString(TEXT("ground")); };
			for (int32 i = 1; i < Points.Num(); ++i)
			{
				FLeg Leg;
				Leg.From = ReadPoint(Points[i - 1]->AsArray());
				Leg.To = ReadPoint(Points[i]->AsArray());
				Leg.LevelFrom = LayerLevel(Layout, LayerAt(i - 1));
				Leg.LevelTo = LayerLevel(Layout, LayerAt(i));
				Leg.LayerTo = LayerAt(i);
				if (const FString* Via = Links.Find(i))
				{
					Leg.Via = *Via;
				}
				State.Legs.Add(Leg);
			}
			return State.Legs.Num() > 0;
		}
		Error = RouteArg.IsEmpty() ? TEXT("no mandatory route in the layout") : FString::Printf(TEXT("no route %s"), *RouteArg);
		return false;
	}

	float GroundZ(const UWorld* World)
	{
		return 0.f; // the generated maps are built at ground_z 0 (build_levelgen_map.py)
	}

	bool Step(UWorld* World, FState& State, float Dt)
	{
		if (State.Stage == EStage::Done)
		{
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Leader)
		{
			Check(State, false, TEXT("a squad leader exists"));
			return Finish(State, false);
		}
		if (State.Leg >= State.Legs.Num())
		{
			Check(State, true, FString::Printf(TEXT("the squad completed the mandatory route %s (%d legs) in %.0f s"), *State.RouteId, State.Legs.Num(), State.Total));
			return Finish(State, true);
		}
		State.Total += Dt;
		State.LegTime += Dt;
		State.CarrotTime += Dt;
		const FLeg& Leg = State.Legs[State.Leg];
		const float TargetZ = GroundZ(World) + Leg.LevelTo * LevelHeightCm;
		const FVector Here3 = Leader->GetActorLocation();
		const FVector2D Here(Here3);
		const float Dist = static_cast<float>(FVector2D::Distance(Here, Leg.To));
		const bool bLast = State.Leg == State.Legs.Num() - 1;
		const bool bLevelOk = FMath::Abs(Here3.Z - Leader->GetSimpleCollisionHalfHeight() - TargetZ) <= LevelTolCm;

		// A hole drop: no nav link for a 3 m drop yet - the squad is put at the leg's end (reported, not failed).
		if (Leg.Via.StartsWith(TEXT("hole")))
		{
			const FVector Spot(Leg.To.X, Leg.To.Y, TargetZ + Leader->GetSimpleCollisionHalfHeight() + 5.f);
			for (AOperativeCharacter* Each : Squad->GetMembers())
			{
				if (Each)
				{
					Each->TeleportTo(Spot + FVector(0.f, 0.f, 0.f), Each->GetActorRotation(), false, true);
				}
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke known: leg %d drops through %s to level %d - the squad teleported to (%.0f, %.0f, %.0f); no nav link for drops yet"),
				State.Leg + 1, *Leg.Via, Leg.LevelTo, Spot.X, Spot.Y, Spot.Z);
			++State.Teleports;
			++State.Leg;
			State.LegTime = 0.f;
			State.CarrotTime = CarrotSeconds;
			State.bLegClimbed = false;
			return true;
		}

		if (Leader->LadderClimb && Leader->LadderClimb->IsBusy())
		{
			State.bLegClimbed = true;
		}
		State.StillTime = Leader->IsMoving() ? 0.f : State.StillTime + Dt;
		const double LegLength = FVector2D::Distance(Leg.From, Leg.To);
		const double LegAlong = FVector2D::DotProduct(Here - Leg.From, (Leg.To - Leg.From).GetSafeNormal());
		const bool bSameLevel = Leg.LevelFrom == Leg.LevelTo;
		const bool bVerticalLevelOk = bSameLevel ? bLevelOk : (FMath::Abs(Here3.Z - Leader->GetSimpleCollisionHalfHeight() - TargetZ) <= 60.f);
		const bool bCornerReached = bSameLevel && !bLast && (Dist <= CornerTurnCm || LegAlong >= LegLength - CornerTurnCm);
		const bool bPointReached = Dist <= PointReachCm && bVerticalLevelOk && (bLast || !bSameLevel ? !Leader->IsMoving() : true);
		if (bCornerReached || bPointReached)
		{
			if (!bSameLevel && Leg.Via.StartsWith(TEXT("LAD_")))
			{
				Check(State, State.bLegClimbed, FString::Printf(TEXT("vertical leg %d (%s): traversed ladder via ALadderActor climb (via=%s)"), State.Leg + 1, *Leg.LayerTo, *Leg.Via));
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: leg %d / %d done (%s, level %d, climbed=%d) at %.0f s"), State.Leg + 1, State.Legs.Num(), *Leg.LayerTo, Leg.LevelTo, State.bLegClimbed ? 1 : 0, State.LegTime);
			++State.Leg;
			State.LegTime = 0.f;
			State.CarrotTime = CarrotSeconds;
			State.bLegClimbed = false;
			return true;
		}
		// The carrot: a click 6 m ahead on the leg (the squad's own path follows the nav links); a level change aims at the end point itself.
		if (State.CarrotTime >= CarrotSeconds || State.StillTime > StuckReorderSeconds)
		{
			State.CarrotTime = 0.f;
			State.StillTime = 0.f;
			FVector2D Goal = Leg.To;
			if (bSameLevel)
			{
				const FVector2D Dir = (Leg.To - Leg.From).GetSafeNormal();
				const double Along = FMath::Clamp(FVector2D::DotProduct(Here - Leg.From, Dir), 0.0, LegLength);
				if (LegLength - Along > CarrotAheadCm + 200.0)
				{
					Goal = Leg.From + Dir * (Along + CarrotAheadCm);
				}
			}
			State.LastOrder = FVector(Goal.X, Goal.Y, TargetZ + 50.f);
			Leader->OrderMoveTo(State.LastOrder, false);
		}
		const float LegCap = FMath::Max(LegMinSeconds, 3.f * static_cast<float>(LegLength) / WalkSpeedCmPerSec + (bSameLevel ? 0.f : 30.f));
		if (State.LegTime > LegCap)
		{
			Check(State, false, FString::Printf(TEXT("stuck on leg %d / %d (to %s, level %d): the leader at (%.0f, %.0f, %.0f) is %.0f cm from (%.0f, %.0f) after %.0f s; last order (%.0f, %.0f, %.0f)"),
				State.Leg + 1, State.Legs.Num(), *Leg.LayerTo, Leg.LevelTo, Here3.X, Here3.Y, Here3.Z, Dist, Leg.To.X, Leg.To.Y, State.LegTime, State.LastOrder.X, State.LastOrder.Y, State.LastOrder.Z));
			return Finish(State, false);
		}
		return true;
	}

	/** The bot route walk: reads the route mode's tracker (the bot drives the squad). */
	bool StepBotRoute(UWorld* World, FState& State, float Dt)
	{
		if (State.Stage == EStage::Done)
		{
			return false;
		}
		State.Total += Dt;
#if CODEX_WITH_PLAYTEST_BOT
		const UPlaytestBotSubsystem* Bot = World->GetSubsystem<UPlaytestBotSubsystem>();
		if (!Bot || !Bot->IsRouteMode())
		{
			if (State.Total > 20.f)
			{
				Check(State, false, TEXT("the bot's route mode started (Codex.Bot.Route)"));
				return Finish(State, false);
			}
			return true;
		}
		const FBotRouteTracker& Tracker = Bot->GetRouteTracker();
		const int32 Num = Tracker.GetConfig().Points.Num();
		State.Leg = FMath::Min(Tracker.GetTotalReached(), Num);
		const bool bSkipped = Tracker.GetTotalSkipped() > 0;
		const bool bAll = Tracker.GetTotalReached() >= Num;
		if (!bSkipped && !bAll && State.Total < BotRouteLimitSeconds)
		{
			return true;
		}
		for (const FBotRouteStuck& Stuck : Tracker.GetStucks())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: stuck at %.0f s on #%d (%s) at (%.0f, %.0f): %s, last order '%s'%s"), Stuck.Time, Stuck.Waypoint, *Stuck.Label,
				Stuck.Position.X, Stuck.Position.Y, *Stuck.Reason, *Stuck.LastOrder, Stuck.bSkipped ? TEXT(" -> SKIPPED") : TEXT(""));
		}
		for (const FBotRouteDrop& Drop : Bot->GetRouteDrops())
		{
			const bool bTeleport = Drop.bScripted;
			State.Teleports += bTeleport ? 1 : 0;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: drop through %s to #%d (%s): %s, %.1f s"), bTeleport ? TEXT("known") : TEXT("ok  "), *Drop.Via, Drop.Waypoint, *Drop.Label,
				Drop.bScripted ? TEXT("SCRIPTED by the bot (the drop leg got stuck)") : (Drop.bFell ? TEXT("the leader fell through the drop nav link") : TEXT("no fall (walked round)")),
				Drop.Seconds);
		}
		Check(State, bAll && !bSkipped, FString::Printf(TEXT("the bot walked the route %s: %d / %d waypoints reached, %d skipped, %d stuck events, %.0f m in %.0f s"),
			*Tracker.GetConfig().Name, Tracker.GetTotalReached(), Num, Tracker.GetTotalSkipped(), Tracker.GetStucks().Num(), Tracker.GetDistanceCm() / 100.f, State.Total));
		return Finish(State, bAll && !bSkipped);
#else
		Check(State, false, TEXT("the playtest bot exists in this build"));
		return Finish(State, false);
#endif
	}

	/** The level JSON has a bot route and the bot exists: the walk goes to the route mode. */
	bool CanUseBotRoute(UWorld* World)
	{
#if CODEX_WITH_PLAYTEST_BOT
		const ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
		const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
		return Level && Level->Config.BotRoute.IsValid() && World->GetSubsystem<UPlaytestBotSubsystem>() != nullptr;
#else
		return false;
#endif
	}

	void Start(UWorld* World, const FString& LayoutArg, const FString& RouteArg, const FString& MapArg, bool bCarrot)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		FString Error;
		if (!MapArg.IsEmpty() && !MapArg.Equals(LoadedMapName(World), ESearchCase::IgnoreCase))
		{
			Check(*State, false, FString::Printf(TEXT("map=%s is the loaded map (loaded: %s; run the game on it: Scripts/levelgen_route_smoke.ps1 -Map /Game/Maps/Generated/%s)"),
				*MapArg, *LoadedMapName(World), *MapArg));
			Finish(*State, false);
			return;
		}
		if (!LoadLegs(World, *State, LayoutArg, RouteArg, MapArg, Error))
		{
			Check(*State, false, FString::Printf(TEXT("layout: %s"), *Error));
			Finish(*State, false);
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		if (!bCarrot && RouteArg.IsEmpty() && CanUseBotRoute(World))
		{
#if CODEX_WITH_PLAYTEST_BOT
			// A walk check: no pressure enemies on the way (the bot batches fight them).
			if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
			{
				Pressure->Set(0, ECVF_SetByConsole);
			}
			// Nor the Depot's level-JSON PMC squads / the manoeuvre under fire (#43): a walk check, not a fight.
			for (const TCHAR* Name : { TEXT("Codex.Pmc.Encounters"), TEXT("Codex.Combat.UnderFireManoeuvre") })
			{
				if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
				{
					Variable->Set(0, ECVF_SetByConsole);
				}
			}
			World->GetSubsystem<UPlaytestBotSubsystem>()->StartRouteRun(BotRouteLimitSeconds + 60.f, false, 1);
			State->Stage = EStage::BotRoute;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: LevelGenRouteSmoke layout %s route %s on %s: the walk handed to the bot's route mode (Codex.Bot.Route; the pressure off)"),
				*State->LayoutName, *State->RouteId, *LoadedMapName(World));
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
			{
				UWorld* W = WeakWorld.Get();
				return W && State->Stage != EStage::Done && StepBotRoute(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
			}), 0.f);
			return;
#endif
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: LevelGenRouteSmoke layout %s route %s: %d legs; the carrot walk (%s)"), *State->LayoutName, *State->RouteId, State->Legs.Num(),
			bCarrot ? TEXT("walk=carrot") : (RouteArg.IsEmpty() ? TEXT("no bot route in the level JSON") : TEXT("a route other than the bot's")));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			return W && State->Stage != EStage::Done && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
		}), 0.f);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		FString LayoutArg, RouteArg, MapArg;
		bool bCarrot = false;
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("map=")))
			{
				MapArg = FPaths::GetBaseFilename(Arg.Mid(4));
			}
			bCarrot |= Arg == TEXT("walk=carrot");
			if (Arg.StartsWith(TEXT("layout=")))
			{
				LayoutArg = Arg.Mid(7);
			}
			else if (Arg.StartsWith(TEXT("route=")))
			{
				RouteArg = Arg.Mid(6);
			}
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, LayoutArg, RouteArg, MapArg, bCarrot]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Start(W, LayoutArg, RouteArg, MapArg, bCarrot);
			}
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LevelGenRouteSmoke"),
		TEXT("Dev check (2026-11-01, docs/design/levelgen/LEVEL_GENERATOR.md 8): the squad walks the mandatory route of the generated map's accepted layout ")
		TEXT("- by the bot's route mode when the level JSON has a bot_route (drops through the drop nav links), else the carrot walk (hole drops teleported); ")
		TEXT("[map=<L_Gen_...>] [layout=<accepted name>] [route=<id>] [walk=carrot]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
