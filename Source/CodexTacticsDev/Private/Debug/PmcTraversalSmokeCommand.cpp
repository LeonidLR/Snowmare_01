// Dev check (smart enemies T-12, docs/design/gdd/smart-enemies-roadmap.md; task 095; UE-only, no Godot reference):
// can an enemy fighter (hostile operative, team 1, commander off) walk every stair, ladder (>= 7 rungs), mantle link and drop link
// of a generated map (L_Gen_Depot_X4)? The same legs are then walked by the player's own operative (team 0) as the control.
//
// Passes (one engine run; the squad mode is switched in code, the level's start mode / -SquadStart= is logged):
//   PMC solo       - the hostile, the player's squad in Solo   (team 1 mantle permission follows the PLAYER squad's Solo mode:
//   PMC formation  - the hostile, the player's squad in formation  AOperativeCharacter::OrderMoveTo bMantlePathAllowed)
//   T0 solo        - the player's operative in Solo
//   T0 formation   - the player's formation leader (followers holding)
// A leg counts as crossed only with evidence of the link itself (review 095):
//   stairs  - the feet seen inside the flight's middle height band near its polyline (PmcTraversalRules::IsOnStairFlight)
//   ladders - on a ladder near this ladder
//   mantles - a climb started (GetMantleCount / IsMantling) whose base is at this link's start; up/over and down counted apart
//   drops   - ULedgeFallComponent took exactly this drop link (GetActiveLink)
// and an arrival on the ground (not mid-air / mid-climb / mid-drop, no vertical speed; PmcTraversalRules::HasArrived).
// Per pass: "N of M crossed" per kind, failed ids with reasons + link coordinates, the failure groups (counted by code),
// then one side-by-side table. RESULT = every PMC pass >= 95 % per kind (knownFail in Scripts/test_map.json until the fixes).
//
// Arguments (console args or command line):
//   -TraversalMantleSample=N (default from config)   -TraversalDropSample=N
//   -TraversalKinds=stairs,ladders,mantles,drops     -TraversalOnly=<id>
//   -TraversalSquad=both|solo|formation (default both) -TraversalNoControl   -TraversalControlSample=N (per kind, 0 = all legs)
// Config: Content/Data/AI/pmc_traversal.json (to be moved there at the merge), else docs/port/gemini/pmc_traversal_config.json.
//
// powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands PmcTraversalSmoke

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcSquadSubsystem.h"
#include "Characters/PmcTraversalRules.h"
#include "AIController.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Falls/DropLinkSubsystem.h"
#include "Falls/LedgeFallComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Interactables/MantleNavigation.h"
#include "Ladders/LadderClimbComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Survival/ColdSurvivalComponent.h"

namespace PmcTraversalSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float LevelHeightCm = 300.f;
	/** The hostile's spawn / parking spot on L_Gen_Depot_X4 (projected onto the navmesh). */
	const FVector SpawnOrigin(2356.f, -8700.f, 50.f);

	struct FPmcTraversalConfig
	{
		float ArriveRadiusCm = 150.f;
		float LevelToleranceCm = 70.f;
		float StuckMoveCm = 50.f;
		float StuckSeconds = 4.f;
		float LadderArriveRadiusCm = 200.f;
		float StairInwardCm = 200.f;
		float StairFlightRadiusCm = 150.f;
		float MantleApproachOffsetCm = 120.f;
		float MantleGoalOffsetCm = 120.f;
		float MantleBaseToleranceCm = 200.f;
		float LadderNearCm = 300.f;
		float DropApproachOffsetCm = 150.f;
		float DropGoalOffsetCm = 100.f;
		float SettleMinSeconds = 0.25f;
		float SettleMaxSeconds = 3.f;
		int32 DefaultMantleSample = 120;
		int32 DefaultDropSample = 80;
		FPmcTraversalTimeout Timeout;
		FString Source = TEXT("defaults");

		static FPmcTraversalConfig Load()
		{
			FPmcTraversalConfig Config;
			// Content/Data/AI/pmc_traversal.json once merged (a gemini/* branch may not add it: pre-push guard); the docs copy until then.
			FString Path = FPaths::ProjectContentDir() / TEXT("Data/AI/pmc_traversal.json");
			if (!FPaths::FileExists(Path))
			{
				Path = FPaths::ProjectDir() / TEXT("docs/port/gemini/pmc_traversal_config.json");
			}
			FString JsonText;
			TSharedPtr<FJsonObject> Obj;
			if (!FFileHelper::LoadFileToString(JsonText, *Path) ||
				!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Obj) || !Obj.IsValid())
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("Smoke: traversal config %s missing / invalid - defaults"), *Path);
				return Config;
			}
			Config.Source = Path;
			Obj->TryGetNumberField(TEXT("arrive_radius_cm"), Config.ArriveRadiusCm);
			Obj->TryGetNumberField(TEXT("level_tolerance_cm"), Config.LevelToleranceCm);
			Obj->TryGetNumberField(TEXT("stuck_move_cm"), Config.StuckMoveCm);
			Obj->TryGetNumberField(TEXT("stuck_seconds"), Config.StuckSeconds);
			Obj->TryGetNumberField(TEXT("ladder_arrive_radius_cm"), Config.LadderArriveRadiusCm);
			Obj->TryGetNumberField(TEXT("stair_inward_cm"), Config.StairInwardCm);
			Obj->TryGetNumberField(TEXT("stair_flight_radius_cm"), Config.StairFlightRadiusCm);
			Obj->TryGetNumberField(TEXT("mantle_approach_offset_cm"), Config.MantleApproachOffsetCm);
			Obj->TryGetNumberField(TEXT("mantle_goal_offset_cm"), Config.MantleGoalOffsetCm);
			Obj->TryGetNumberField(TEXT("mantle_base_tolerance_cm"), Config.MantleBaseToleranceCm);
			Obj->TryGetNumberField(TEXT("ladder_near_cm"), Config.LadderNearCm);
			Obj->TryGetNumberField(TEXT("drop_approach_offset_cm"), Config.DropApproachOffsetCm);
			Obj->TryGetNumberField(TEXT("drop_goal_offset_cm"), Config.DropGoalOffsetCm);
			Obj->TryGetNumberField(TEXT("settle_min_seconds"), Config.SettleMinSeconds);
			Obj->TryGetNumberField(TEXT("settle_max_seconds"), Config.SettleMaxSeconds);
			Obj->TryGetNumberField(TEXT("leg_timeout_seconds"), Config.Timeout.BaseSeconds);
			Obj->TryGetNumberField(TEXT("timeout_walk_speed_cms"), Config.Timeout.WalkSpeedCms);
			Obj->TryGetNumberField(TEXT("timeout_ladder_climb_cms"), Config.Timeout.LadderClimbCms);
			Obj->TryGetNumberField(TEXT("timeout_transition_seconds"), Config.Timeout.TransitionSeconds);
			Obj->TryGetNumberField(TEXT("default_mantle_sample"), Config.DefaultMantleSample);
			Obj->TryGetNumberField(TEXT("default_drop_sample"), Config.DefaultDropSample);
			return Config;
		}
	};

	struct FProgress
	{
		FVector Mark = FVector::ZeroVector;
		double MarkTime = 0.0;

		void Reset(double Now, const FVector& Position)
		{
			Mark = Position;
			MarkTime = Now;
		}

		bool IsStuck(double Now, const FVector& Position, double MoveCm, double Seconds)
		{
			if (FVector::Dist(Position, Mark) >= MoveCm)
			{
				Reset(Now, Position);
				return false;
			}
			return (Now - MarkTime) >= Seconds;
		}
	};

	enum class ELinkKind : uint8
	{
		Stair,
		Ladder,
		Mantle,
		Drop,
		Count
	};
	constexpr int32 NumKinds = static_cast<int32>(ELinkKind::Count);

	const TCHAR* KindName(ELinkKind Kind)
	{
		switch (Kind)
		{
		case ELinkKind::Stair: return TEXT("stairs");
		case ELinkKind::Ladder: return TEXT("ladders");
		case ELinkKind::Mantle: return TEXT("mantles");
		case ELinkKind::Drop: return TEXT("drops");
		default: return TEXT("unknown");
		}
	}

	struct FLeg
	{
		ELinkKind Kind = ELinkKind::Stair;
		FString Id;
		/** UMantleSubsystem / UDropLinkSubsystem link index (mantles, drops). */
		int32 LinkIndex = INDEX_NONE;
		FVector StartSpot = FVector::ZeroVector;
		FRotator StartFacing = FRotator::ZeroRotator;
		FVector Goal = FVector::ZeroVector;
		FVector NavGoal = FVector::ZeroVector;
		FVector LinkStart = FVector::ZeroVector;
		FVector LinkEnd = FVector::ZeroVector;
		/** Stairs: foot -> mid -> top (2D). Ladders: the ladder's own position. */
		TArray<FVector2D> Flight;
		float LowZ = 0.f;
		float HighZ = 0.f;
		bool bMantleDown = false;
		float TimeoutSeconds = 15.f;
	};

	struct FFailure
	{
		FString Id;
		FString Reason;
		/** "<kind>[ up/over| down]: <reason without the [..] suffix>" - the printed failure groups. */
		FString GroupKey;
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
	};

	struct FKindStats
	{
		int32 TotalInMap = 0;
		int32 Sampled = 0;
		int32 Crossed = 0;
		int32 UpSampled = 0;
		int32 UpCrossed = 0;
		int32 DownSampled = 0;
		int32 DownCrossed = 0;
		/** Drops crossed whose landing was a heavy wound / lethal by the fall rules (knockdown and death are off in this check). */
		int32 HurtLandings = 0;
		TArray<FFailure> Failures;
	};

	struct FPass
	{
		FString Name;
		bool bTeam0 = false;
		bool bSolo = true;
		FKindStats Stats[NumKinds];
		int32 OrdersGiven = 0;
		int32 OrdersMantleAllowed = 0;
		bool bRan = false;
	};

	enum class EPhase : uint8
	{
		WaitSubsystems,
		Setup,
		BeginPass,
		PrepareLeg,
		Settle,
		OrderLeg,
		RunLeg,
		Done
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		FString MapName;
		FPmcTraversalConfig Config;
		EPhase Phase = EPhase::WaitSubsystems;
		float PhaseTime = 0.f;
		double LegStartTime = 0.0;
		TWeakObjectPtr<AOperativeCharacter> Hostile;
		TWeakObjectPtr<AOperativeCharacter> Walker;
		FVector ParkSpot = FVector::ZeroVector;
		TArray<FLeg> Legs;
		TArray<FPass> Passes;
		int32 PassIndex = 0;
		/** Legs of the current pass (indices into Legs). */
		TArray<int32> PassLegs;
		int32 LegCursor = 0;
		FProgress Progress;
		FString LevelStartMode;
		FString SquadStartArg;

		// Evidence of the current leg.
		bool bOnLink = false;
		bool bOtherLink = false;
		bool bPathChecked = false;
		bool bPathUsesLink = false;
		bool bWasMantling = false;
		bool bWasOnLadder = false;
		int32 InitialMantleCount = 0;
		int32 InitialDescentCount = 0;
		int32 InitialDropCount = 0;
		int32 InitialHurt = 0;
		int32 InitialRescues = 0;

		FString OnlyFilter;
		TSet<FString> KindsFilter;
		FString SquadModes = TEXT("both");
		int32 MantleSampleCount = 0;
		int32 DropSampleCount = 0;
		bool bRunControl = true;
		int32 ControlSamplePerKind = 0;
		FString AbortReason;
	};

	FPass& CurrentPass(FState& State) { return State.Passes[State.PassIndex]; }

	const FLeg& CurrentLeg(const FState& State) { return State.Legs[State.PassLegs[State.LegCursor]]; }

	FVector2D ReadPoint(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		return Object.IsValid() && Object->TryGetArrayField(Field, Array) && Array->Num() >= 2
			? FVector2D((*Array)[0]->AsNumber(), (*Array)[1]->AsNumber())
			: FVector2D::ZeroVector;
	}

	TSharedPtr<FJsonObject> LoadLayout(const FString& MapName, FString& OutError)
	{
		FString LevelText;
		TSharedPtr<FJsonObject> Level;
		FString File;
		const FString LevelFile = FPaths::ProjectContentDir() / TEXT("Data/LevelJson") / MapName + TEXT(".json");
		if (FFileHelper::LoadFileToString(LevelText, *LevelFile) &&
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LevelText), Level) &&
			Level.IsValid() && Level->HasField(TEXT("levelgen_layout")))
		{
			File = FPaths::ProjectDir() / Level->GetStringField(TEXT("levelgen_layout"));
		}
		FString Text;
		TSharedPtr<FJsonObject> Layout;
		if (File.IsEmpty() || !FFileHelper::LoadFileToString(Text, *File) ||
			!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Layout) || !Layout.IsValid())
		{
			OutError = FString::Printf(TEXT("no accepted layout for %s (level json %s, layout '%s')"), *MapName, *LevelFile, *File);
			return nullptr;
		}
		return Layout;
	}

	bool ProjectNav(UWorld* World, const FVector& Spot, FVector& OutNav, const FVector& Extent = FVector(150.f, 150.f, 200.f))
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Out;
		if (Nav && Nav->ProjectPointToNavigation(Spot, Out, Extent))
		{
			OutNav = Out.Location;
			return true;
		}
		return false;
	}

	FVector Feet(const AOperativeCharacter& Operative)
	{
		return Operative.GetActorLocation() - FVector(0.f, 0.f, Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	int32 HurtCount(const AOperativeCharacter& Operative)
	{
		return Operative.LedgeFall ? Operative.LedgeFall->GetHeavyWoundCount() + Operative.LedgeFall->GetLethalFallCount() : 0;
	}

	FString Vec(const FVector& V)
	{
		return FString::Printf(TEXT("(%.0f,%.0f,%.0f)"), V.X, V.Y, V.Z);
	}

	/** Ends the current leg: a failure (Reason) or a crossing (Reason empty). */
	void EndLeg(FState& State, const FString& Reason)
	{
		FPass& Pass = CurrentPass(State);
		const FLeg& Leg = CurrentLeg(State);
		FKindStats& Stats = Pass.Stats[static_cast<int32>(Leg.Kind)];
		const AOperativeCharacter* Walker = State.Walker.Get();
		if (Reason.IsEmpty())
		{
			++Stats.Crossed;
			if (Leg.Kind == ELinkKind::Mantle)
			{
				++(Leg.bMantleDown ? Stats.DownCrossed : Stats.UpCrossed);
			}
			const bool bHurt = Walker && Leg.Kind == ELinkKind::Drop && HurtCount(*Walker) > State.InitialHurt;
			Stats.HurtLandings += bHurt ? 1 : 0;
			UE_LOG(LogCodexTactics, Log, TEXT("Smoke: [%s] %s %s crossed in %.1f s%s"), *Pass.Name, KindName(Leg.Kind), *Leg.Id,
				State.World.IsValid() ? State.World->GetTimeSeconds() - State.LegStartTime : 0.0, bHurt ? TEXT(" (hurt landing)") : TEXT(""));
		}
		else
		{
			const FString SubKind = Leg.Kind == ELinkKind::Mantle ? FString(Leg.bMantleDown ? TEXT("down") : TEXT("up/over")) : FString();
			Stats.Failures.Add({ Leg.Id, Reason, PmcTraversalRules::FailureGroupKey(KindName(Leg.Kind), SubKind, Reason), Leg.LinkStart, Leg.LinkEnd });
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: [%s] %s %s FAIL (%s) link %s -> %s, nav goal %s, walker feet %s"), *Pass.Name,
				KindName(Leg.Kind), *Leg.Id, *Reason, *Vec(Leg.LinkStart), *Vec(Leg.LinkEnd), *Vec(Leg.NavGoal), Walker ? *Vec(Feet(*Walker)) : TEXT("-"));
			if (Leg.Kind == ELinkKind::Stair)
			{
				const AAIController* AI = Walker ? Cast<AAIController>(Walker->GetController()) : nullptr;
				const UPathFollowingComponent* Follow = AI ? AI->GetPathFollowingComponent() : nullptr;
				const FNavPathSharedPtr Path = Follow ? Follow->GetPath() : nullptr;
				const bool bValidPath = Path.IsValid() && Path->IsValid();
				const bool bPartial = bValidPath && Path->IsPartial();
				const int32 NumPts = bValidPath ? Path->GetPathPoints().Num() : 0;
				const FVector LastPt = bValidPath && NumPts > 0 ? Path->GetPathPoints().Last().Location : FVector::ZeroVector;
				const FNavMeshPath* NavMeshPath = Path.IsValid() ? Path->CastPath<FNavMeshPath>() : nullptr;
				const NavNodeRef EndPoly = NavMeshPath && NavMeshPath->PathCorridor.Num() > 0 ? NavMeshPath->PathCorridor.Last() : 0;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke diag stair fail: %s at feet %s, partial=%d, numPts=%d, endPoly=%llu, lastPt=%s, distToNavGoal=%.0f (Z=%.0f)"),
					*Leg.Id, Walker ? *Vec(Feet(*Walker)) : TEXT("-"), bPartial ? 1 : 0, NumPts, static_cast<uint64>(EndPoly), *Vec(LastPt),
					Walker ? static_cast<float>(FVector::Dist2D(Feet(*Walker), Leg.NavGoal)) : 0.f,
					Walker ? static_cast<float>(FMath::Abs(Feet(*Walker).Z - Leg.NavGoal.Z)) : 0.f);
			}
		}
		++State.LegCursor;
		State.Phase = EPhase::PrepareLeg;
		State.PhaseTime = 0.f;
	}

	template <typename TLink>
	TArray<int32> SortedLinkOrder(const TArray<TLink>& Links)
	{
		TArray<int32> Order;
		for (int32 i = 0; i < Links.Num(); ++i)
		{
			Order.Add(i);
		}
		auto Key = [](const FVector& V) { return FIntVector(FMath::RoundToInt32(V.X), FMath::RoundToInt32(V.Y), FMath::RoundToInt32(V.Z)); };
		Order.Sort([&Links, &Key](int32 A, int32 B)
		{
			const FIntVector As = Key(Links[A].Start), Bs = Key(Links[B].Start), Ae = Key(Links[A].End), Be = Key(Links[B].End);
			if (As.X != Bs.X) return As.X < Bs.X;
			if (As.Y != Bs.Y) return As.Y < Bs.Y;
			if (As.Z != Bs.Z) return As.Z < Bs.Z;
			if (Ae.X != Be.X) return Ae.X < Be.X;
			if (Ae.Y != Be.Y) return Ae.Y < Be.Y;
			return Ae.Z < Be.Z;
		});
		return Order;
	}

	bool KindWanted(const FState& State, ELinkKind Kind)
	{
		return State.KindsFilter.IsEmpty() || State.KindsFilter.Contains(KindName(Kind));
	}

	void BuildLegs(FState& State, UWorld* World)
	{
		FString LayoutError;
		const TSharedPtr<FJsonObject> Layout = LoadLayout(State.MapName, LayoutError);
		if (!Layout.IsValid())
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Smoke: layout load error: %s - no stairs / ladders"), *LayoutError);
		}
		const float GroundZ = Layout.IsValid() && Layout->HasField(TEXT("ground_z")) ? static_cast<float>(Layout->GetNumberField(TEXT("ground_z"))) : 0.f;
		FKindStats Totals[NumKinds];

		// 1. Stairs: every flight of the layout.
		const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
		if (KindWanted(State, ELinkKind::Stair) && Layout.IsValid() && Layout->TryGetArrayField(TEXT("buildings"), Buildings))
		{
			for (const TSharedPtr<FJsonValue>& BV : *Buildings)
			{
				const TSharedPtr<FJsonObject> B = BV.IsValid() ? BV->AsObject() : nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Stairs = nullptr;
				if (!B.IsValid() || !B->TryGetArrayField(TEXT("stairs"), Stairs))
				{
					continue;
				}
				for (const TSharedPtr<FJsonValue>& SV : *Stairs)
				{
					const TSharedPtr<FJsonObject> S = SV.IsValid() ? SV->AsObject() : nullptr;
					FString Id;
					if (!S.IsValid() || !S->TryGetStringField(TEXT("id"), Id))
					{
						continue;
					}
					++Totals[static_cast<int32>(ELinkKind::Stair)].TotalInMap;
					if (!State.OnlyFilter.IsEmpty() && State.OnlyFilter != Id)
					{
						continue;
					}
					const FVector2D Foot2D = ReadPoint(S, TEXT("pos"));
					const FVector2D Top2D = ReadPoint(S, TEXT("top"));
					const bool bHasMid = S->HasField(TEXT("mid"));
					const FVector2D Mid2D = bHasMid ? ReadPoint(S, TEXT("mid")) : Top2D;
					const FVector2D Dir = (Mid2D - Foot2D).GetSafeNormal();
					FString KindStr;
					S->TryGetStringField(TEXT("kind"), KindStr);
					const FVector2D Inward = KindStr == TEXT("external") ? (Top2D - Mid2D).GetSafeNormal() : Dir;
					double From = 0.0, To = 1.0;
					S->TryGetNumberField(TEXT("from"), From);
					S->TryGetNumberField(TEXT("to"), To);

					FLeg Leg;
					Leg.Kind = ELinkKind::Stair;
					Leg.Id = Id;
					Leg.LowZ = GroundZ + static_cast<float>(From) * LevelHeightCm;
					Leg.HighZ = GroundZ + static_cast<float>(To) * LevelHeightCm;
					Leg.StartSpot = FVector(Foot2D - Dir * 100.f, Leg.LowZ);
					Leg.StartFacing = FVector(Dir, 0.f).Rotation();
					Leg.Goal = FVector(Top2D + Inward * State.Config.StairInwardCm, Leg.HighZ + 50.f);
					Leg.LinkStart = FVector(Foot2D, Leg.LowZ);
					Leg.LinkEnd = FVector(Top2D, Leg.HighZ);
					Leg.Flight = { Foot2D };
					if (bHasMid)
					{
						Leg.Flight.Add(Mid2D);
					}
					Leg.Flight.Add(Top2D);
					Leg.TimeoutSeconds = PmcTraversalRules::LegTimeoutSeconds(State.Config.Timeout,
						static_cast<float>(FVector::Dist(Leg.StartSpot, Leg.Goal)) * 1.5f, 0.f, false);
					State.Legs.Add(Leg);
				}
			}
		}

		// 2. Ladders: every layout ladder with >= 7 rungs.
		const TArray<TSharedPtr<FJsonValue>>* Ladders = nullptr;
		if (KindWanted(State, ELinkKind::Ladder) && Layout.IsValid() && Layout->TryGetArrayField(TEXT("ladders"), Ladders))
		{
			for (const TSharedPtr<FJsonValue>& LV : *Ladders)
			{
				const TSharedPtr<FJsonObject> L = LV.IsValid() ? LV->AsObject() : nullptr;
				FString Id;
				double Rungs = 0.0;
				if (!L.IsValid() || !L->TryGetStringField(TEXT("id"), Id) || !L->TryGetNumberField(TEXT("rungs"), Rungs) || Rungs < 7.0)
				{
					continue;
				}
				++Totals[static_cast<int32>(ELinkKind::Ladder)].TotalInMap;
				if (!State.OnlyFilter.IsEmpty() && State.OnlyFilter != Id)
				{
					continue;
				}
				const FVector2D Pos = ReadPoint(L, TEXT("pos"));
				const FVector2D Foot = ReadPoint(L, TEXT("foot"));
				const FVector2D Top = ReadPoint(L, TEXT("top"));
				const FVector2D Out = (Foot - Pos).GetSafeNormal();
				double From = 0.0, To = 1.0;
				L->TryGetNumberField(TEXT("level_from"), From);
				L->TryGetNumberField(TEXT("level_to"), To);

				FLeg Leg;
				Leg.Kind = ELinkKind::Ladder;
				Leg.Id = Id;
				Leg.LowZ = GroundZ + static_cast<float>(From) * LevelHeightCm;
				Leg.HighZ = GroundZ + static_cast<float>(To) * LevelHeightCm;
				Leg.StartSpot = FVector(Foot + Out * 150.f, Leg.LowZ);
				Leg.StartFacing = FVector(-Out, 0.f).Rotation();
				Leg.Goal = FVector(Top - Out * 50.f, Leg.HighZ + 50.f);
				Leg.LinkStart = FVector(Foot, Leg.LowZ);
				Leg.LinkEnd = FVector(Top, Leg.HighZ);
				Leg.Flight = { Pos };
				// Review 095 item 3: the flat 15 s cut the 24-rung 874 cm ladders mid ClimbUp - the budget grows with the height.
				Leg.TimeoutSeconds = PmcTraversalRules::LegTimeoutSeconds(State.Config.Timeout,
					static_cast<float>(FVector::Dist2D(Leg.StartSpot, Leg.Goal)) + 300.f, FMath::Abs(Leg.HighZ - Leg.LowZ), true);
				State.Legs.Add(Leg);
			}
		}

		// 3. Mantle links: a deterministic stride sample (sorted by coordinates).
		const UMantleSubsystem* MantleSub = World->GetSubsystem<UMantleSubsystem>();
		if (KindWanted(State, ELinkKind::Mantle) && MantleSub)
		{
			const TArray<FMantleLink>& Links = MantleSub->GetLinks();
			Totals[static_cast<int32>(ELinkKind::Mantle)].TotalInMap = Links.Num();
			const TArray<int32> Order = SortedLinkOrder(Links);
			const int32 Want = State.MantleSampleCount > 0 ? State.MantleSampleCount : State.Config.DefaultMantleSample;
			const int32 Count = FMath::Min(Want, Order.Num());
			const double Stride = Count > 0 ? static_cast<double>(Order.Num()) / Count : 1.0;
			for (int32 i = 0; i < Count; ++i)
			{
				const int32 Sorted = FMath::Clamp(static_cast<int32>(i * Stride), 0, Order.Num() - 1);
				const FMantleLink& Link = Links[Order[Sorted]];
				const FString Id = FString::Printf(TEXT("mantle_%d"), Sorted);
				if (!State.OnlyFilter.IsEmpty() && State.OnlyFilter != Id)
				{
					continue;
				}
				FLeg Leg;
				Leg.Kind = ELinkKind::Mantle;
				Leg.Id = Id;
				Leg.LinkIndex = Order[Sorted];
				Leg.StartSpot = Link.Start - Link.Dir * State.Config.MantleApproachOffsetCm;
				Leg.StartFacing = Link.Dir.Rotation();
				Leg.Goal = Link.End + Link.Dir * State.Config.MantleGoalOffsetCm;
				Leg.LinkStart = Link.Start;
				Leg.LinkEnd = Link.End;
				Leg.bMantleDown = Link.bDown;
				Leg.TimeoutSeconds = PmcTraversalRules::LegTimeoutSeconds(State.Config.Timeout,
					static_cast<float>(FVector::Dist(Leg.StartSpot, Leg.Goal)), 0.f, true);
				State.Legs.Add(Leg);
			}
		}

		// 4. Drop links: a deterministic stride sample.
		const UDropLinkSubsystem* DropSub = World->GetSubsystem<UDropLinkSubsystem>();
		if (KindWanted(State, ELinkKind::Drop) && DropSub)
		{
			const TArray<FDropLink>& Links = DropSub->GetLinks();
			Totals[static_cast<int32>(ELinkKind::Drop)].TotalInMap = Links.Num();
			const TArray<int32> Order = SortedLinkOrder(Links);
			const int32 Want = State.DropSampleCount > 0 ? State.DropSampleCount : State.Config.DefaultDropSample;
			const int32 Count = FMath::Min(Want, Order.Num());
			const double Stride = Count > 0 ? static_cast<double>(Order.Num()) / Count : 1.0;
			for (int32 i = 0; i < Count; ++i)
			{
				const int32 Sorted = FMath::Clamp(static_cast<int32>(i * Stride), 0, Order.Num() - 1);
				const FDropLink& Link = Links[Order[Sorted]];
				const FString Id = FString::Printf(TEXT("drop_%d"), Sorted);
				if (!State.OnlyFilter.IsEmpty() && State.OnlyFilter != Id)
				{
					continue;
				}
				FLeg Leg;
				Leg.Kind = ELinkKind::Drop;
				Leg.Id = Id;
				Leg.LinkIndex = Order[Sorted];
				Leg.StartSpot = Link.Start - Link.Dir * State.Config.DropApproachOffsetCm;
				Leg.StartFacing = Link.Dir.Rotation();
				Leg.Goal = Link.End + Link.Dir * State.Config.DropGoalOffsetCm;
				Leg.LinkStart = Link.Start;
				Leg.LinkEnd = Link.End;
				Leg.TimeoutSeconds = PmcTraversalRules::LegTimeoutSeconds(State.Config.Timeout,
					static_cast<float>(FVector::Dist(Leg.StartSpot, Leg.Goal)), 0.f, true);
				State.Legs.Add(Leg);
			}
		}

		// The passes.
		const bool bSolo = State.SquadModes != TEXT("formation");
		const bool bFormation = State.SquadModes != TEXT("solo");
		auto AddPass = [&State, &Totals](const TCHAR* Name, bool bTeam0, bool bSoloMode)
		{
			FPass& Pass = State.Passes.AddDefaulted_GetRef();
			Pass.Name = Name;
			Pass.bTeam0 = bTeam0;
			Pass.bSolo = bSoloMode;
			for (int32 k = 0; k < NumKinds; ++k)
			{
				Pass.Stats[k].TotalInMap = Totals[k].TotalInMap;
			}
		};
		if (bSolo) { AddPass(TEXT("PMC solo"), false, true); }
		if (bFormation) { AddPass(TEXT("PMC formation"), false, false); }
		if (State.bRunControl)
		{
			if (bSolo) { AddPass(TEXT("T0 solo"), true, true); }
			if (bFormation) { AddPass(TEXT("T0 formation"), true, false); }
		}

		int32 PerKind[NumKinds] = {};
		for (const FLeg& Leg : State.Legs)
		{
			++PerKind[static_cast<int32>(Leg.Kind)];
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: traversal plan: %d legs (stairs %d of %d, ladders %d of %d, mantles %d of %d, drops %d of %d), %d passes, config %s"),
			State.Legs.Num(), PerKind[0], Totals[0].TotalInMap, PerKind[1], Totals[1].TotalInMap, PerKind[2], Totals[2].TotalInMap,
			PerKind[3], Totals[3].TotalInMap, State.Passes.Num(), *State.Config.Source);
	}

	/** The legs of a pass: all of them; a control pass limited by -TraversalControlSample (per kind, up / down mantles apart). */
	void SelectPassLegs(FState& State)
	{
		const FPass& Pass = CurrentPass(State);
		State.PassLegs.Reset();
		TMap<int32, int32> Taken;
		for (int32 i = 0; i < State.Legs.Num(); ++i)
		{
			const FLeg& Leg = State.Legs[i];
			if (Pass.bTeam0 && State.ControlSamplePerKind > 0)
			{
				const int32 Bucket = static_cast<int32>(Leg.Kind) * 2 + (Leg.bMantleDown ? 1 : 0);
				int32& Count = Taken.FindOrAdd(Bucket);
				if (Count >= State.ControlSamplePerKind)
				{
					continue;
				}
				++Count;
			}
			State.PassLegs.Add(i);
		}
		FPass& Mutable = CurrentPass(State);
		for (const int32 Index : State.PassLegs)
		{
			const FLeg& Leg = State.Legs[Index];
			FKindStats& Stats = Mutable.Stats[static_cast<int32>(Leg.Kind)];
			++Stats.Sampled;
			if (Leg.Kind == ELinkKind::Mantle)
			{
				++(Leg.bMantleDown ? Stats.DownSampled : Stats.UpSampled);
			}
		}
	}

	void Isolate(AOperativeCharacter& Operative)
	{
		Operative.ColdLevel = 0.f;
		if (Operative.ColdSurvival)
		{
			Operative.ColdSurvival->SetComponentTickEnabled(false);
		}
		Operative.bTacticalCeaseFire = true;
	}

	AOperativeCharacter* SpawnHostile(FState& State, UWorld* World)
	{
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		if (!Pmc)
		{
			return nullptr;
		}
		const TArray<AOperativeCharacter*> Spawned = Pmc->SpawnSquad(1, State.ParkSpot, FRotator::ZeroRotator, 1, 1);
		AOperativeCharacter* Hostile = Spawned.IsEmpty() ? nullptr : Spawned[0];
		if (Hostile)
		{
			Pmc->SetTeamCommanded(1, false);
			Isolate(*Hostile);
			State.Hostile = Hostile;
		}
		return Hostile;
	}

	bool IsUsable(const AOperativeCharacter* Operative)
	{
		return Operative && !Operative->IsKilledInAction() && Operative->GetController() &&
			(!Operative->HealthComponent || Operative->HealthComponent->IsAlive());
	}

	void Teleport(AOperativeCharacter& Operative, const FVector& NavSpot, const FRotator& Facing)
	{
		const float HalfHeight = Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		if (AAIController* AI = Cast<AAIController>(Operative.GetController()))
		{
			AI->StopMovement();
		}
		Operative.TeleportTo(NavSpot + FVector(0.f, 0.f, HalfHeight + 5.f), Facing, false, true);
		if (UCharacterMovementComponent* Move = Operative.GetCharacterMovement())
		{
			Move->StopMovementImmediately();
			Move->Velocity = FVector::ZeroVector;
		}
	}

	/** Did the walker's current path go through this mantle / drop link? (path points + the corridor's nav link user ids) */
	bool PathUsesLink(const FState& State, const FLeg& Leg, const FNavigationPath& Path)
	{
		UWorld* World = State.World.Get();
		if (!World || Leg.LinkIndex == INDEX_NONE)
		{
			return false;
		}
		if (Leg.Kind == ELinkKind::Mantle)
		{
			const UMantleSubsystem* MantleSub = World->GetSubsystem<UMantleSubsystem>();
			TArray<int32> OnPath;
			if (MantleSub)
			{
				MantleSub->FindLinksOnPath(Path, OnPath);
			}
			return OnPath.Contains(Leg.LinkIndex);
		}
		const UDropLinkSubsystem* DropSub = World->GetSubsystem<UDropLinkSubsystem>();
		if (!DropSub)
		{
			return false;
		}
		for (const FNavPathPoint& Point : Path.GetPathPoints())
		{
			if (DropSub->FindLinkById(Point.CustomNavLinkId) == Leg.LinkIndex)
			{
				return true;
			}
		}
		const FNavMeshPath* NavPath = Path.CastPath<FNavMeshPath>();
		const ARecastNavMesh* Recast = NavPath ? Cast<ARecastNavMesh>(NavPath->GetNavigationDataUsed()) : nullptr;
		if (Recast)
		{
			for (const NavNodeRef Ref : NavPath->PathCorridor)
			{
				if (DropSub->FindLinkById(Recast->GetNavLinkUserId(Ref)) == Leg.LinkIndex)
				{
					return true;
				}
			}
		}
		return false;
	}

	FString PathNote(const FState& State)
	{
		return !State.bPathChecked ? TEXT("no path seen") : State.bPathUsesLink ? TEXT("link on path") : TEXT("path avoided link");
	}

	/** Stairs: was the march itself walked (the feet in the flight's mid band) before the failure? */
	FString StairNote(const FState& State, const FLeg& Leg)
	{
		return Leg.Kind != ELinkKind::Stair ? FString() : State.bOnLink ? FString(TEXT(", march seen")) : FString(TEXT(", no march seen"));
	}

	FString PhaseNote(const AOperativeCharacter& Walker, const FLeg& Leg)
	{
		if (Walker.IsMantling())
		{
			return TEXT("mid mantle");
		}
		if (Walker.LadderClimb && Walker.LadderClimb->IsOnLadder())
		{
			return TEXT("on ladder");
		}
		if (Walker.LedgeFall && Walker.LedgeFall->IsWaiting())
		{
			return TEXT("waiting at drop");
		}
		if (Walker.LedgeFall && Walker.LedgeFall->IsBusy())
		{
			return TEXT("mid drop");
		}
		// Re-review 095: the nearer end in 3D (feet) - a walker at the base under a top's 2D spot is "at link start", not "end".
		const FVector FeetNow = Feet(Walker);
		const float ToStart = static_cast<float>(FVector::Dist(FeetNow, Leg.LinkStart));
		const float ToEnd = static_cast<float>(FVector::Dist(FeetNow, Leg.LinkEnd));
		if (FMath::Min(ToStart, ToEnd) >= 250.f)
		{
			return TEXT("walking");
		}
		return ToEnd < ToStart ? TEXT("at link end") : TEXT("at link start");
	}

	void PrintPass(const FPass& Pass)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("=== PASS %s (team %d, squad %s): mantle path allowed on %d of %d orders ==="), *Pass.Name,
			Pass.bTeam0 ? 0 : 1, Pass.bSolo ? TEXT("SOLO") : TEXT("formation"), Pass.OrdersMantleAllowed, Pass.OrdersGiven);
		TArray<FString> Keys;
		for (int32 k = 0; k < NumKinds; ++k)
		{
			const ELinkKind Kind = static_cast<ELinkKind>(k);
			const FKindStats& KS = Pass.Stats[k];
			FString Extra;
			if (Kind == ELinkKind::Mantle)
			{
				Extra = FString::Printf(TEXT(" [up/over %d of %d, down %d of %d]"), KS.UpCrossed, KS.UpSampled, KS.DownCrossed, KS.DownSampled);
			}
			else if (Kind == ELinkKind::Drop)
			{
				Extra = FString::Printf(TEXT(" [hurt landings %d]"), KS.HurtLandings);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke summary [%s]: %s: %d of %d crossed (%.1f%%)%s [total in map: %d]"), *Pass.Name, KindName(Kind),
				KS.Crossed, KS.Sampled, PmcTraversalRules::RatePercent(KS.Crossed, KS.Sampled), *Extra, KS.TotalInMap);
			if (!KS.Failures.IsEmpty())
			{
				FString List;
				for (const FFailure& Fail : KS.Failures)
				{
					List += FString::Printf(TEXT("%s[%s: %s %s->%s]"), List.IsEmpty() ? TEXT("") : TEXT(", "), *Fail.Id, *Fail.Reason, *Vec(Fail.Start), *Vec(Fail.End));
					Keys.Add(Fail.GroupKey);
				}
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke [%s]: %s failed %d: %s"), *Pass.Name, KindName(Kind), KS.Failures.Num(), *List);
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke [%s]: failure groups (%d failures):"), *Pass.Name, Keys.Num());
		for (const TPair<FString, int32>& Group : PmcTraversalRules::GroupCounts(Keys))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke [%s]:   %3d  %s"), *Pass.Name, Group.Value, *Group.Key);
		}
	}

	bool Finish(FState& State)
	{
		State.Phase = EPhase::Done;
		UE_LOG(LogCodexTactics, Display, TEXT("=== PMC TRAVERSAL SMOKE SUMMARY (map %s, level start %s, -SquadStart=%s) ==="), *State.MapName,
			*State.LevelStartMode, State.SquadStartArg.IsEmpty() ? TEXT("-") : *State.SquadStartArg);
		for (const FPass& Pass : State.Passes)
		{
			if (Pass.bRan)
			{
				PrintPass(Pass);
			}
		}

		// Side by side.
		FString Header = FString::Printf(TEXT("%-14s"), TEXT("kind"));
		for (const FPass& Pass : State.Passes)
		{
			Header += FString::Printf(TEXT(" | %-20s"), *Pass.Name);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("=== SIDE BY SIDE ==="));
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke table: %s"), *Header);
		auto Row = [&State](const TCHAR* Label, TFunctionRef<TPair<int32, int32>(const FPass&)> Get)
		{
			FString Line = FString::Printf(TEXT("%-14s"), Label);
			for (const FPass& Pass : State.Passes)
			{
				const TPair<int32, int32> CS = Get(Pass);
				Line += FString::Printf(TEXT(" | %-20s"), *FString::Printf(TEXT("%d/%d (%.0f%%)"), CS.Key, CS.Value, PmcTraversalRules::RatePercent(CS.Key, CS.Value)));
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke table: %s"), *Line);
		};
		Row(TEXT("stairs"), [](const FPass& P) { return TPair<int32, int32>(P.Stats[0].Crossed, P.Stats[0].Sampled); });
		Row(TEXT("ladders"), [](const FPass& P) { return TPair<int32, int32>(P.Stats[1].Crossed, P.Stats[1].Sampled); });
		Row(TEXT("mantle up/over"), [](const FPass& P) { return TPair<int32, int32>(P.Stats[2].UpCrossed, P.Stats[2].UpSampled); });
		Row(TEXT("mantle down"), [](const FPass& P) { return TPair<int32, int32>(P.Stats[2].DownCrossed, P.Stats[2].DownSampled); });
		Row(TEXT("drops"), [](const FPass& P) { return TPair<int32, int32>(P.Stats[3].Crossed, P.Stats[3].Sampled); });

		bool bPass = State.AbortReason.IsEmpty() && !State.Legs.IsEmpty();
		for (const FPass& Pass : State.Passes)
		{
			if (Pass.bTeam0)
			{
				continue;
			}
			bPass &= Pass.bRan;
			// Re-review 095: mantle up/over and down are judged apart; a filtered run (-TraversalOnly / kinds) judges what it sampled
			// (an empty plan already fails above), an unfiltered one also fails a kind it sampled nothing of.
			const bool bUnfiltered = State.OnlyFilter.IsEmpty();
			auto Judge = [&bPass](int32 Crossed, int32 Sampled, bool bRequired)
			{
				if (Sampled > 0 || bRequired)
				{
					bPass &= PmcTraversalRules::MeetsBar(Crossed, Sampled);
				}
			};
			for (int32 k = 0; k < NumKinds; ++k)
			{
				const ELinkKind Kind = static_cast<ELinkKind>(k);
				const FKindStats& KS = Pass.Stats[k];
				const bool bRequired = bUnfiltered && KindWanted(State, Kind);
				if (Kind == ELinkKind::Mantle)
				{
					Judge(KS.UpCrossed, KS.UpSampled, false);
					Judge(KS.DownCrossed, KS.DownSampled, false);
					Judge(KS.Crossed, KS.Sampled, bRequired);
				}
				else
				{
					Judge(KS.Crossed, KS.Sampled, bRequired);
				}
			}
		}
		if (State.Legs.IsEmpty())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: no legs (filter '%s' / kinds matched nothing, or no layout / links)"), *State.OnlyFilter);
		}
		if (!State.AbortReason.IsEmpty())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: aborted: %s"), *State.AbortReason);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PmcTraversalSmoke"));
		return false;
	}

	bool Abort(FState& State, const FString& Reason)
	{
		State.AbortReason = Reason;
		UE_LOG(LogCodexTactics, Error, TEXT("Smoke FAIL: %s"), *Reason);
		return Finish(State);
	}

	bool Step(TSharedRef<FState> StateRef, float DeltaTime)
	{
		FState& State = *StateRef;
		UWorld* World = State.World.Get();
		if (!World)
		{
			UE_LOG(LogCodexTactics, Error, TEXT("Smoke FAIL: world gone"));
			FPlatformMisc::RequestExit(false, TEXT("PmcTraversalSmoke"));
			return false;
		}
		State.PhaseTime += DeltaTime;

		switch (State.Phase)
		{
		case EPhase::WaitSubsystems:
		{
			const UDropLinkSubsystem* DropSub = World->GetSubsystem<UDropLinkSubsystem>();
			const UMantleSubsystem* MantleSub = World->GetSubsystem<UMantleSubsystem>();
			const bool bDropReady = DropSub && DropSub->HasGenerated();
			const bool bMantleReady = MantleSub && MantleSub->HasGenerated();
			if (!bDropReady || !bMantleReady)
			{
				return State.PhaseTime < 180.f ? true
					: Abort(State, FString::Printf(TEXT("links not generated within 180 s (drops %d, mantles %d)"), bDropReady, bMantleReady));
			}
			// No pressure waves / random PMC encounters during the check.
			for (const TCHAR* Name : { TEXT("Codex.Pressure.Enabled"), TEXT("Codex.Pmc.Encounters") })
			{
				if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
				{
					Var->Set(0, ECVF_SetByConsole);
				}
				else
				{
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke: cvar %s not found (skipped)"), Name);
				}
			}
			State.Phase = EPhase::Setup;
			State.PhaseTime = 0.f;
			return true;
		}
		case EPhase::Setup:
		{
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			if (!Squad || Squad->GetMembers().IsEmpty())
			{
				return Abort(State, TEXT("no player squad"));
			}
			State.LevelStartMode = Squad->IsSoloMode() ? TEXT("SOLO") : TEXT("formation");
			FParse::Value(FCommandLine::Get(), TEXT("SquadStart="), State.SquadStartArg);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member)
				{
					Isolate(*Member);
				}
			}
			State.ParkSpot = SpawnOrigin;
			ProjectNav(World, SpawnOrigin, State.ParkSpot);
			if (!SpawnHostile(State, World))
			{
				return Abort(State, TEXT("could not spawn the team 1 hostile"));
			}
			BuildLegs(State, World);
			if (State.Legs.IsEmpty() || State.Passes.IsEmpty())
			{
				return Finish(State);
			}
			State.PassIndex = 0;
			State.Phase = EPhase::BeginPass;
			State.PhaseTime = 0.f;
			return true;
		}
		case EPhase::BeginPass:
		{
			if (State.PassIndex >= State.Passes.Num())
			{
				return Finish(State);
			}
			FPass& Pass = CurrentPass(State);
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			if (!Squad)
			{
				return Abort(State, TEXT("squad subsystem gone"));
			}
			if (Pass.bSolo && !Squad->IsSoloMode())
			{
				Squad->EnterSoloMode();
			}
			else if (!Pass.bSolo && Squad->IsSoloMode())
			{
				Squad->ExitSoloMode();
			}
			Squad->SetFollowersHolding(true);
			if (Squad->IsSoloMode() != Pass.bSolo)
			{
				return Abort(State, FString::Printf(TEXT("could not switch the squad to %s"), Pass.bSolo ? TEXT("Solo") : TEXT("formation")));
			}
			AOperativeCharacter* Walker = nullptr;
			if (Pass.bTeam0)
			{
				Walker = Squad->GetFormationLeader();
				if (!IsUsable(Walker))
				{
					for (AOperativeCharacter* Member : Squad->GetMembers())
					{
						if (IsUsable(Member))
						{
							Walker = Member;
							break;
						}
					}
				}
				// The hostile waits at the parking spot, out of the control walker's way.
				if (AOperativeCharacter* Hostile = State.Hostile.Get())
				{
					Teleport(*Hostile, State.ParkSpot, FRotator::ZeroRotator);
				}
			}
			else
			{
				Walker = IsUsable(State.Hostile.Get()) ? State.Hostile.Get() : SpawnHostile(State, World);
			}
			if (!IsUsable(Walker))
			{
				return Abort(State, FString::Printf(TEXT("no usable walker for pass %s"), *Pass.Name));
			}
			State.Walker = Walker;
			Pass.bRan = true;
			SelectPassLegs(State);
			State.LegCursor = 0;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: === PASS %s: %d legs, walker %s (team %d), squad IsSoloMode=%d, followers holding ==="),
				*Pass.Name, State.PassLegs.Num(), *Walker->GetName(), Walker->TeamId, Squad->IsSoloMode() ? 1 : 0);
			State.Phase = EPhase::PrepareLeg;
			State.PhaseTime = 0.f;
			return true;
		}
		case EPhase::PrepareLeg:
		{
			if (State.LegCursor >= State.PassLegs.Num())
			{
				++State.PassIndex;
				State.Phase = EPhase::BeginPass;
				State.PhaseTime = 0.f;
				return true;
			}
			FPass& Pass = CurrentPass(State);
			AOperativeCharacter* Walker = State.Walker.Get();
			if (!IsUsable(Walker))
			{
				// Review LOW: never run on with a dead / uncontrolled walker (every later leg would read "no path").
				Walker = Pass.bTeam0 ? nullptr : SpawnHostile(State, World);
				if (!IsUsable(Walker))
				{
					return Abort(State, FString::Printf(TEXT("walker of pass %s lost and not replaced"), *Pass.Name));
				}
				State.Walker = Walker;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: [%s] walker replaced by %s"), *Pass.Name, *Walker->GetName());
			}
			// Clean state before every leg (no failure cascade). Health / knockdown stay off: this checks the movement; hurt landings are counted.
			Walker->ColdLevel = 0.f;
			Walker->bTacticalCeaseFire = true;
			if (Walker->LadderClimb && Walker->LadderClimb->IsOnLadder())
			{
				Walker->LadderClimb->AbortClimb(ELadderAbortReason::None);
			}
			if (Walker->HealthComponent)
			{
				Walker->HealthComponent->SetMaxHealth(100000.f, true);
			}
			if (Walker->KnockdownComponent)
			{
				Walker->KnockdownComponent->bCanBeKnockedDown = false;
				Walker->KnockdownComponent->ResetForLoad();
			}
			Walker->SetActorEnableCollision(true);
			Walker->SetStance(EOperativeStance::Standing);

			FLeg& Leg = State.Legs[State.PassLegs[State.LegCursor]];
			FVector NavStart = Leg.StartSpot;
			if (!ProjectNav(World, Leg.StartSpot, NavStart))
			{
				EndLeg(State, TEXT("no path (start off navmesh)"));
				return true;
			}
			if (!ProjectNav(World, Leg.Goal, Leg.NavGoal))
			{
				EndLeg(State, TEXT("no path (goal off navmesh)"));
				return true;
			}
			// Review 095 item 6: zero velocity after the teleport, settle on the ground before the order.
			Teleport(*Walker, NavStart, Leg.StartFacing);
			State.Phase = EPhase::Settle;
			State.PhaseTime = 0.f;
			return true;
		}
		case EPhase::Settle:
		{
			AOperativeCharacter* Walker = State.Walker.Get();
			if (!Walker)
			{
				return Abort(State, TEXT("walker gone while settling"));
			}
			const UCharacterMovementComponent* Move = Walker->GetCharacterMovement();
			const bool bSettled = Move && Move->IsMovingOnGround() && Move->Velocity.Size() < 10.f;
			if (bSettled && State.PhaseTime >= State.Config.SettleMinSeconds)
			{
				State.Phase = EPhase::OrderLeg;
				State.PhaseTime = 0.f;
			}
			else if (State.PhaseTime > State.Config.SettleMaxSeconds)
			{
				EndLeg(State, TEXT("start not grounded"));
			}
			return true;
		}
		case EPhase::OrderLeg:
		{
			AOperativeCharacter* Walker = State.Walker.Get();
			if (!Walker)
			{
				return Abort(State, TEXT("walker gone before the order"));
			}
			FPass& Pass = CurrentPass(State);
			const FLeg& Leg = CurrentLeg(State);
			State.InitialMantleCount = Walker->GetMantleCount();
			State.InitialDescentCount = Walker->GetMantleDescentCount();
			State.InitialDropCount = Walker->LedgeFall ? Walker->LedgeFall->GetDropCount() : 0;
			State.InitialHurt = HurtCount(*Walker);
			State.InitialRescues = Walker->LedgeFall ? Walker->LedgeFall->GetRescueCount() : 0;
			const EOperativeOrderResult Result = Walker->OrderMoveTo(Leg.NavGoal, false);
			// Review 095 item 2: the permission is decided inside OrderMoveTo - read it after the order.
			++Pass.OrdersGiven;
			Pass.OrdersMantleAllowed += Walker->IsMantlePathAllowed() ? 1 : 0;
			if (Leg.Kind == ELinkKind::Mantle)
			{
				UE_LOG(LogCodexTactics, Log, TEXT("Smoke: [%s] %s (%s) order %d, IsMantlePathAllowed=%d"), *Pass.Name, *Leg.Id,
					Leg.bMantleDown ? TEXT("down") : TEXT("up/over"), static_cast<int32>(Result), Walker->IsMantlePathAllowed() ? 1 : 0);
			}
			if (Leg.Kind == ELinkKind::Stair)
			{
				const AAIController* AI = Cast<AAIController>(Walker->GetController());
				const UPathFollowingComponent* Follow = AI ? AI->GetPathFollowingComponent() : nullptr;
				const FNavPathSharedPtr Path = Follow ? Follow->GetPath() : nullptr;
				const bool bValidPath = Path.IsValid() && Path->IsValid();
				const bool bPartial = bValidPath && Path->IsPartial();
				const int32 NumPts = bValidPath ? Path->GetPathPoints().Num() : 0;
				FString PtsStr;
				if (bValidPath)
				{
					for (int32 PtIdx = 0; PtIdx < FMath::Min(NumPts, 5); ++PtIdx)
					{
						PtsStr += FString::Printf(TEXT(" pt%d=%s"), PtIdx, *Vec(Path->GetPathPoints()[PtIdx].Location));
					}
					if (NumPts > 5)
					{
						PtsStr += FString::Printf(TEXT(" ... pt%d=%s"), NumPts - 1, *Vec(Path->GetPathPoints().Last().Location));
					}
				}
				const FNavMeshPath* NavMeshPath = Path.IsValid() ? Path->CastPath<FNavMeshPath>() : nullptr;
				const int32 NumPolys = NavMeshPath ? NavMeshPath->PathCorridor.Num() : 0;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke diag: [%s] stair %s: order=%d, validPath=%d, partial=%d, numPts=%d, numPolys=%d, len=%.0f, pts:%s"),
					*Pass.Name, *Leg.Id, static_cast<int32>(Result), bValidPath ? 1 : 0, bPartial ? 1 : 0, NumPts, NumPolys,
					bValidPath ? Path->GetLength() : 0.f, *PtsStr);
			}
			switch (Result)
			{
			case EOperativeOrderResult::Accepted:
				break;
			case EOperativeOrderResult::Unreachable:
				EndLeg(State, TEXT("no path"));
				return true;
			case EOperativeOrderResult::NoController:
				return Abort(State, FString::Printf(TEXT("walker %s has no controller"), *Walker->GetName()));
			case EOperativeOrderResult::Refused:
				EndLeg(State, TEXT("refused"));
				return true;
			default:
				EndLeg(State, FString::Printf(TEXT("order result %d"), static_cast<int32>(Result)));
				return true;
			}
			State.Progress.Reset(World->GetTimeSeconds(), Walker->GetActorLocation());
			State.LegStartTime = World->GetTimeSeconds();
			State.bOnLink = false;
			State.bOtherLink = false;
			State.bPathChecked = false;
			State.bPathUsesLink = false;
			State.bWasMantling = false;
			State.bWasOnLadder = false;
			State.Phase = EPhase::RunLeg;
			State.PhaseTime = 0.f;
			return true;
		}
		case EPhase::RunLeg:
		{
			AOperativeCharacter* Walker = State.Walker.Get();
			if (!Walker)
			{
				return Abort(State, TEXT("walker gone mid leg"));
			}
			const FLeg& Leg = CurrentLeg(State);
			if (Walker->IsKilledInAction() || (Walker->HealthComponent && !Walker->HealthComponent->IsAlive()))
			{
				EndLeg(State, TEXT("died"));
				return true;
			}
			const double Now = World->GetTimeSeconds();
			const FVector Location = Walker->GetActorLocation();
			const FVector FeetNow = Feet(*Walker);

			// The path (mantles / drops): did it go through this link at any time?
			if (Leg.Kind == ELinkKind::Mantle || Leg.Kind == ELinkKind::Drop)
			{
				const AAIController* AI = Cast<AAIController>(Walker->GetController());
				const UPathFollowingComponent* Follow = AI ? AI->GetPathFollowingComponent() : nullptr;
				const FNavPathSharedPtr Path = Follow ? Follow->GetPath() : nullptr;
				if (Path.IsValid() && Path->IsValid())
				{
					State.bPathChecked = true;
					State.bPathUsesLink |= PathUsesLink(State, Leg, *Path);
				}
			}

			// Evidence of the link itself.
			switch (Leg.Kind)
			{
			case ELinkKind::Stair:
				State.bOnLink |= PmcTraversalRules::IsOnStairFlight(FeetNow, Leg.Flight, Leg.LowZ, Leg.HighZ, State.Config.StairFlightRadiusCm);
				break;
			case ELinkKind::Ladder:
				if (Walker->LadderClimb && Walker->LadderClimb->IsClimbing())
				{
					State.bWasOnLadder = true;
					const bool bThis = !Leg.Flight.IsEmpty() && FVector2D::Distance(FVector2D(Location), Leg.Flight[0]) <= State.Config.LadderNearCm;
					State.bOnLink |= bThis;
					State.bOtherLink |= !bThis;
				}
				break;
			case ELinkKind::Mantle:
				if (Leg.bMantleDown)
				{
					// A down link is no climb clip: the batch 2 descent (walk off the edge + fall; GetMantleDescentCount) or the old hop
					// (a vault-like arc, IsVaulting) - HandleMantleLink. Evidence: it started near this link's top.
					const bool bDescent = Walker->IsMantleDescending() || Walker->GetMantleDescentCount() > State.InitialDescentCount;
					const bool bHop = Walker->IsVaulting();
					if (!State.bWasMantling && (bDescent || bHop))
					{
						State.bWasMantling = true;
						const bool bThis = bDescent
							? FVector::Dist2D(Walker->GetMantleDescentLanding(), Leg.LinkEnd) <= State.Config.MantleBaseToleranceCm
							: FVector::Dist2D(FeetNow, Leg.LinkStart) <= State.Config.MantleBaseToleranceCm;
						State.bOnLink |= bThis;
						State.bOtherLink |= !bThis;
						UE_LOG(LogCodexTactics, Log, TEXT("Smoke: %s down started (%s), feet %s, landing %s (link %s -> %s)"), *Leg.Id,
							bDescent ? TEXT("descent") : TEXT("hop"), *Vec(FeetNow), *Vec(Walker->GetMantleDescentLanding()), *Vec(Leg.LinkStart),
							*Vec(Leg.LinkEnd));
					}
				}
				else if (!State.bWasMantling && (Walker->IsMantling() || Walker->GetMantleCount() > State.InitialMantleCount))
				{
					// Review 095 item 1: a pass proves a climb happened - and at this link's start, not another obstacle.
					State.bWasMantling = true;
					const FVector Base = Walker->GetMantleBaseFeet();
					const bool bThis = FVector::Dist2D(Base, Leg.LinkStart) <= State.Config.MantleBaseToleranceCm &&
						FMath::Abs(Base.Z - Leg.LinkStart.Z) <= 120.f;
					State.bOnLink |= bThis;
					State.bOtherLink |= !bThis;
					UE_LOG(LogCodexTactics, Log, TEXT("Smoke: %s climb started, base %s (link start %s) class %s"), *Leg.Id, *Vec(Base),
						*Vec(Leg.LinkStart), *Walker->GetLastMantleClass().ToString());
				}
				break;
			case ELinkKind::Drop:
				if (Walker->LedgeFall)
				{
					const int32 Active = Walker->LedgeFall->GetActiveLink();
					State.bOnLink |= Active == Leg.LinkIndex;
					State.bOtherLink |= Active != INDEX_NONE && Active != Leg.LinkIndex;
				}
				break;
			default:
				break;
			}

			const UCharacterMovementComponent* Move = Walker->GetCharacterMovement();
			FPmcTraversalArrival Arrival;
			Arrival.Dist2DCm = static_cast<float>(FVector::Dist2D(Location, Leg.NavGoal));
			Arrival.FeetDistZCm = static_cast<float>(FMath::Abs(FeetNow.Z - Leg.NavGoal.Z));
			Arrival.bMovingOnGround = Move && Move->IsMovingOnGround();
			Arrival.VelocityZ = Move ? static_cast<float>(Move->Velocity.Z) : 0.f;
			Arrival.bBusyTraversing = Walker->IsMantling() || Walker->IsMantleDescending() || Walker->IsVaulting() || (Walker->LadderClimb && Walker->LadderClimb->IsOnLadder()) ||
				(Walker->LedgeFall && Walker->LedgeFall->IsBusy());
			const float Radius = Leg.Kind == ELinkKind::Ladder ? State.Config.LadderArriveRadiusCm : State.Config.ArriveRadiusCm;
			if (PmcTraversalRules::HasArrived(Arrival, Radius, State.Config.LevelToleranceCm))
			{
				const bool bRescued = Walker->LedgeFall && Walker->LedgeFall->GetRescueCount() > State.InitialRescues;
				if (State.bOnLink && Leg.Kind == ELinkKind::Drop)
				{
					// Re-review 095: the link taken is not enough - a drop that missed its lower surface or ended in the off-navmesh
					// rescue teleport (log: drop_327 after an 882 cm rescue, drop_291 "MISSED the link's lower surface") is no crossing.
					const ULedgeFallComponent* Fall = Walker->LedgeFall;
					if (bRescued)
					{
						EndLeg(State, TEXT("drop: rescued off navmesh"));
					}
					else if (!Fall || Fall->GetDropCount() <= State.InitialDropCount)
					{
						EndLeg(State, TEXT("drop: link taken, no drop counted"));
					}
					else if (!Fall->DidLastDropLandOnTarget())
					{
						EndLeg(State, TEXT("drop: missed the lower surface"));
					}
					else
					{
						EndLeg(State, FString());
					}
					return true;
				}
				if (State.bOnLink)
				{
					EndLeg(State, FString());
					return true;
				}
				switch (Leg.Kind)
				{
				case ELinkKind::Stair:
					EndLeg(State, TEXT("bypassed stair, no march seen"));
					break;
				case ELinkKind::Ladder:
					EndLeg(State, State.bOtherLink ? TEXT("climbed another ladder") : TEXT("bypassed ladder"));
					break;
				case ELinkKind::Mantle:
					EndLeg(State, State.bOtherLink
						? FString::Printf(TEXT("mantle %s: climbed another obstacle"), Leg.bMantleDown ? TEXT("down") : TEXT("up/over"))
						: FString::Printf(TEXT("mantle %s: no climb (%s)"), Leg.bMantleDown ? TEXT("down") : TEXT("up/over"), *PathNote(State)));
					break;
				case ELinkKind::Drop:
					EndLeg(State, bRescued ? TEXT("drop: rescued off navmesh")
						: State.bOtherLink ? TEXT("drop: took another drop link")
						: FString::Printf(TEXT("drop: no drop (%s)"), *PathNote(State)));
					break;
				default:
					EndLeg(State, TEXT("bypassed"));
					break;
				}
				return true;
			}

			if (Arrival.bBusyTraversing)
			{
				State.Progress.Reset(Now, Location); // a climb / drop in progress is progress (it ends by itself or by the timeout)
			}
			else if (State.Progress.IsStuck(Now, Location, State.Config.StuckMoveCm, State.Config.StuckSeconds))
			{
				EndLeg(State, FString::Printf(TEXT("stuck (%s)%s"), *PhaseNote(*Walker, Leg), *StairNote(State, Leg)));
				return true;
			}
			if (Now - State.LegStartTime > Leg.TimeoutSeconds)
			{
				// The budget goes after " [" - the failure group key leaves it out (re-review 095: "23 s" / "33 s" split one group).
				EndLeg(State, FString::Printf(TEXT("timed out (%s)%s [budget %.0f s]"), *PhaseNote(*Walker, Leg), *StairNote(State, Leg), Leg.TimeoutSeconds));
				return true;
			}
			return true;
		}
		case EPhase::Done:
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
		State->World = World;
		State->MapName = World->GetMapName();
		State->MapName.RemoveFromStart(World->StreamingLevelsPrefix);
		State->Config = FPmcTraversalConfig::Load();

		auto AddKinds = [&State](const FString& List)
		{
			TArray<FString> Kinds;
			List.ParseIntoArray(Kinds, TEXT(","), true);
			for (const FString& K : Kinds)
			{
				State->KindsFilter.Add(K.TrimStartAndEnd().ToLower());
			}
		};
		for (const FString& Arg : Args)
		{
			FString Value;
			if (Arg.Split(TEXT("="), nullptr, &Value))
			{
				const FString Key = Arg.Left(Arg.Find(TEXT("="))).ToLower();
				if (Key == TEXT("only")) { State->OnlyFilter = Value; }
				else if (Key == TEXT("kinds")) { AddKinds(Value); }
				else if (Key == TEXT("mantlesample")) { State->MantleSampleCount = FCString::Atoi(*Value); }
				else if (Key == TEXT("dropsample")) { State->DropSampleCount = FCString::Atoi(*Value); }
				else if (Key == TEXT("controlsample")) { State->ControlSamplePerKind = FCString::Atoi(*Value); }
				else if (Key == TEXT("squad")) { State->SquadModes = Value.ToLower(); }
			}
			else if (Arg.Equals(TEXT("nocontrol"), ESearchCase::IgnoreCase))
			{
				State->bRunControl = false;
			}
		}
		const TCHAR* Cmd = FCommandLine::Get();
		FParse::Value(Cmd, TEXT("-TraversalMantleSample="), State->MantleSampleCount);
		FParse::Value(Cmd, TEXT("-TraversalDropSample="), State->DropSampleCount);
		FParse::Value(Cmd, TEXT("-TraversalControlSample="), State->ControlSamplePerKind);
		if (FParse::Param(Cmd, TEXT("TraversalNoControl")))
		{
			State->bRunControl = false;
		}
		if (FString Kinds; FParse::Value(Cmd, TEXT("-TraversalKinds="), Kinds))
		{
			AddKinds(Kinds);
		}
		FParse::Value(Cmd, TEXT("-TraversalOnly="), State->OnlyFilter);
		if (FString Squad; FParse::Value(Cmd, TEXT("-TraversalSquad="), Squad))
		{
			State->SquadModes = Squad.ToLower();
		}
		if (State->SquadModes != TEXT("solo") && State->SquadModes != TEXT("formation"))
		{
			State->SquadModes = TEXT("both");
		}

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float DeltaTime)
		{
			return Step(State, DeltaTime);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PmcTraversalSmoke"),
		TEXT("Dev check (smart enemies T-12): a hostile operative (team 1, commander off) and the player's operative (control) walk the stairs, ")
		TEXT("ladders, mantle and drop links of a generated map, the squad in Solo and in formation. Args: only=<id> kinds=a,b mantlesample=N ")
		TEXT("dropsample=N squad=both|solo|formation nocontrol controlsample=N. Logs the tables + RESULT, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
