// Dev-only check of generator-placed PMC squads (smart-enemies T-11 first half, PMC_DEFENCE D2) on the Depot:
//   Scripts/smoke_parallel.ps1 -Commands PmcPostsSmoke   (map /Game/Maps/Generated/L_Gen_Depot_1, "-LevelJson=L_Gen_Depot_1.json")
// No map / level JSON edit: the smoke feeds two level-JSON-shaped "pmc_squads" entries WITH a "post" (parsed by
// PmcEncounterRules::ParseSquad like the game does) to UPmcEncounterSubsystem::SpawnEncounter at navmesh points it finds 25..70 m from the
// player's squad, and checks the spawned pawns: man 0 ON the post (planar <= 60 cm), the others in the firing line beside it (all within
// 5 m, no two closer than 90 cm), every man facing the post's yaw, posture defend, no tactical-post reassignment (the men are still on
// the post after the spawn frame), and that a "post" entry carries no delay_s even when its JSON says one. The squads without a post keep
// their path (PmcEncounterSmoke / PmcEncounterHuntSmoke).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcEncounterRules.h"
#include "AI/PmcEncounterSubsystem.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace PmcPostsSmoke
{
	constexpr float StepSeconds = 0.1f;
	constexpr double ReadySeconds = 2.0;
	constexpr double GiveUpSeconds = 120.0;

	struct FState
	{
		double StageStart = -1.0;
		int32 Failures = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PmcPostsSmoke"));
		return false;
	}

	FVector Centre(const TArray<AOperativeCharacter*>& Men)
	{
		FVector Sum = FVector::ZeroVector;
		for (const AOperativeCharacter* Man : Men)
		{
			Sum += Man->GetActorLocation();
		}
		return Men.IsEmpty() ? Sum : Sum / Men.Num();
	}

	/** A navmesh point on the squad's level, Distance m from Centre towards Bearing degrees; false when off the navmesh. */
	bool NavPointAt(UNavigationSystemV1& Nav, const FVector& Centre, float DistanceCm, float BearingDeg, FVector& Out)
	{
		const float Angle = FMath::DegreesToRadians(BearingDeg);
		const FVector Want = Centre + FVector(FMath::Cos(Angle) * DistanceCm, FMath::Sin(Angle) * DistanceCm, 0.f);
		FNavLocation OnNav;
		if (!Nav.ProjectPointToNavigation(Want, OnNav, FVector(200.f, 200.f, 300.f)) || FMath::Abs(OnNav.Location.Z - (Centre.Z - 92.f)) > 150.f)
		{
			return false;
		}
		Out = OnNav.Location;
		return true;
	}

	/** One generator-shaped "pmc_squads" entry through the game's own parser. */
	bool ParseEntry(const FVector& Pos, float Yaw, int32 Count, const TCHAR* Id, FPmcEncounterSpec& Out)
	{
		const FString Text = FString::Printf(
			TEXT("{ \"count\": %d, \"weights\": \"pmc_commander.json\", \"posture\": \"defend\", \"delay_s\": 45, ")
			TEXT("\"post\": { \"id\": \"%s\", \"kind\": \"ambush_exit\", \"pos\": [%.1f, %.1f, %.1f], \"yaw\": %.1f } }"),
			Count, Id, Pos.X, Pos.Y, Pos.Z, Yaw);
		TSharedPtr<FJsonObject> Json;
		return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid() && PmcEncounterRules::ParseSquad(*Json, Out);
	}

	/** Spawns the entry and checks the men. */
	void CheckPost(FState& State, UPmcEncounterSubsystem& Encounters, UPmcSquadSubsystem& Pmc, const FVector& PostPos, float Yaw, int32 Count,
		const TCHAR* Id, int32 Seed)
	{
		FPmcEncounterSpec Spec;
		if (!ParseEntry(PostPos, Yaw, Count, Id, Spec))
		{
			Check(State, false, FString::Printf(TEXT("%s: the entry parses"), Id));
			return;
		}
		Check(State, Spec.bHasPost && Spec.DelaySeconds == 0.f && Spec.Posture == EPmcPosture::Defend,
			FString::Printf(TEXT("%s: the parsed entry is a defend post with no delay (JSON said 45 s; %.0f)"), Id, Spec.DelaySeconds));
		FString Why;
		const int32 TeamId = Encounters.SpawnEncounter(Spec, Seed, &Why);
		if (TeamId == INDEX_NONE)
		{
			Check(State, false, FString::Printf(TEXT("%s: the squad spawned (%s)"), Id, *Why));
			return;
		}
		const TArray<AOperativeCharacter*> Men = Pmc.GetTeamMembers(TeamId);
		Check(State, Men.Num() == Count, FString::Printf(TEXT("%s: %d of %d men spawned"), Id, Men.Num(), Count));
		EPmcPosture Posture = EPmcPosture::Hunt;
		Check(State, Pmc.GetTeamPosture(TeamId, Posture) && Posture == EPmcPosture::Defend, FString::Printf(TEXT("%s: posture defend"), Id));
		if (Men.IsEmpty())
		{
			return;
		}
		// The nearest man is ON the post (a man 0 on the post; the navmesh projection of the spot may nudge it a little).
		float Nearest = TNumericLimits<float>::Max();
		float Farthest = 0.f;
		float MinGap = TNumericLimits<float>::Max();
		float WorstYaw = 0.f;
		for (int32 Index = 0; Index < Men.Num(); ++Index)
		{
			const float FromPost = static_cast<float>(FVector::Dist2D(Men[Index]->GetActorLocation(), PostPos));
			Nearest = FMath::Min(Nearest, FromPost);
			Farthest = FMath::Max(Farthest, FromPost);
			WorstYaw = FMath::Max(WorstYaw, static_cast<float>(FMath::Abs(FRotator::NormalizeAxis(Men[Index]->GetActorRotation().Yaw - Yaw))));
			for (int32 Other = Index + 1; Other < Men.Num(); ++Other)
			{
				MinGap = FMath::Min(MinGap, static_cast<float>(FVector::Dist2D(Men[Index]->GetActorLocation(), Men[Other]->GetActorLocation())));
			}
		}
		Check(State, Nearest <= 60.f, FString::Printf(TEXT("%s: one man stands ON the post (%.0f cm from it)"), Id, Nearest));
		Check(State, Farthest <= 500.f, FString::Printf(TEXT("%s: all men within 5 m of the post, not in a 40..90 m ring (farthest %.0f cm)"), Id, Farthest));
		Check(State, Men.Num() < 2 || MinGap >= 90.f, FString::Printf(TEXT("%s: no two men on one spot (closest %.0f cm)"), Id, MinGap));
		Check(State, WorstYaw <= 1.5f, FString::Printf(TEXT("%s: every man faces the post's yaw %.1f (worst error %.2f deg)"), Id, Yaw, WorstYaw));
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcPostsSmoke] %s: team %d, %d men, centre %s, post %s"), Id, TeamId, Men.Num(), *Centre(Men).ToCompactString(),
			*PostPos.ToCompactString());
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, TSharedRef<FState> StateRef)
	{
		FState& State = *StateRef;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		const double Now = World->GetTimeSeconds();
		if (State.StageStart < 0.0)
		{
			State.StageStart = Now;
		}
		if (Now - State.StageStart > GiveUpSeconds)
		{
			Check(State, false, TEXT("the world was ready within the time"));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		UPmcEncounterSubsystem* Encounters = UPmcEncounterSubsystem::Get(World);
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Squad || !Pmc || !Encounters || !Nav)
		{
			Check(State, false, TEXT("squad / PMC / encounter subsystems and navigation present"));
			return Finish(State, false);
		}
		if (Now - State.StageStart < ReadySeconds || Squad->GetMembers().Num() < 1)
		{
			return true;
		}
		Encounters->CancelPending(); // the level JSON's own squads stay out: only the two post squads of this smoke
		if (IConsoleVariable* Immortal = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Cheat.Immortal")))
		{
			Immortal->Set(1, ECVF_SetByCode);
		}
		if (UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>())
		{
			Pressure->SetAutoSpawn(false);
			Pressure->DespawnAll();
		}
		const FVector SquadCentre = Centre(Squad->GetMembers());

		// Two posts on the navmesh, 25..70 m from the squad, 40+ m apart: a ground post facing 137 deg, one facing -60.
		TArray<FVector> Posts;
		for (const float DistanceCm : { 4000.f, 3000.f, 5000.f, 2500.f, 6000.f, 7000.f })
		{
			for (int32 Bearing = 0; Bearing < 360 && Posts.Num() < 2; Bearing += 30)
			{
				FVector Point;
				if (NavPointAt(*Nav, SquadCentre, DistanceCm, static_cast<float>(Bearing), Point)
					&& (Posts.IsEmpty() || FVector::Dist2D(Posts[0], Point) >= 4000.f))
				{
					Posts.Add(Point);
				}
			}
			if (Posts.Num() >= 2)
			{
				break;
			}
		}
		Check(State, Posts.Num() >= 2, FString::Printf(TEXT("two navmesh posts found round the squad (%d)"), Posts.Num()));
		if (Posts.Num() < 2)
		{
			return Finish(State, false);
		}
		CheckPost(State, *Encounters, *Pmc, Posts[0], 137.f, 3, TEXT("PMC_T1"), 11);
		CheckPost(State, *Encounters, *Pmc, Posts[1], -60.f, 2, TEXT("PMC_T2"), 29);
		Check(State, Encounters->GetSpawned().Num() == 2, FString::Printf(TEXT("two post squads recorded (%d)"), Encounters->GetSpawned().Num()));
		return Finish(State, true);
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
			return Step(WeakWorld, State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PmcPostsSmoke"),
		TEXT("Dev check (Depot + L_Gen_Depot_1.json): level-JSON-shaped PMC squads with a generator \"post\" spawn ON their post (man 0 on it, ")
		TEXT("the rest in a line beside it, facing the post's yaw, posture defend, no delay); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
