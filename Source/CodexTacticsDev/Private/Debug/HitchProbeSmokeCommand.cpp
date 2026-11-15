// Cold first-use frame and spawn timing on the active level (004 enemy-appear hitch). No asset preloading is performed here: the world
// preload (UGameplayPreloadSubsystem, Codex.Preload.Enabled) is what is measured; smoke_parallel -PreCmds "Codex.Preload.Enabled 0" is the A/B.
// Gates (HitchGateRules, Codex.HitchProbe.* console variables): the hard 250 ms limit on every frame / spawn, and the stricter spawn-window
// gate (the slowest frame within a few frames of an enemy spawn <= 50 ms; frames > 33 ms counted and reported).
#include "CoreMinimal.h"
#include "Core/HitchGateRules.h"
#include "Core/GameplayPreloadSubsystem.h"
#include "AI/PmcEncounterSubsystem.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Ladders/LadderPreparedClipsSubsystem.h"
#include "UObject/GarbageCollection.h"

#if !UE_BUILD_SHIPPING
#include "AI/PmcSquadSubsystem.h"
#include "AI/PmcOperatorRules.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureRules.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Debug/SmokeUtils.h"
#include "Falls/DropLinkSubsystem.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "NavigationSystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "TimerManager.h"

namespace HitchProbeSmoke
{
	const double ModuleStart = FPlatformTime::Seconds();

	struct FTiming
	{
		FString Label;
		double Ms = 0.0;
		uint64 Frame = 0;
	};
	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TArray<FPressureEnemyType> Roster;
		TArray<FTiming> Frames;
		TArray<FTiming> Spawns;
		/** Frames of the first-use roster / PMC spawns and of the scripted wave spawn (spawn windows of the stricter gate). */
		TArray<uint64> RosterSpawnFrames;
		TArray<uint64> WaveSpawnFrames;
		FHitchGateLimits Limits;
		FString PreloadState;
		int32 WaveEnemies = -1;
		bool bWaveStarted = false;
		FDelegateHandle BeginHandle;
		FDelegateHandle EndHandle;
		FVector Origin = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		FVector Right = FVector::RightVector;
		FString FrameLabel = TEXT("gameplay");
		double FrameStart = 0.0;
		double SetupStart = 0.0;
		double NextSpawnAt = 0.0;
		float MaxLeaderTravelCm = 0.f;
		int32 Next = 0;
		int32 Failures = 0;
		bool bObservedPreparation = false;
		bool bPreparationClocksHeld = true;
		bool bPreparationPositionsHeld = true;
		float InitialPressureClock = 0.f;
		float InitialPmcClock = 0.f;
		TMap<TWeakObjectPtr<AOperativeCharacter>, FVector> PreparingPositions;
		bool bStarted = false;
		bool bMeasure = false;
		bool bFrameArmed = false;
		bool bFinish = false;
	};
	static TSharedPtr<FState> Active;

	void Check(FState& S, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		S.Failures += bOk ? 0 : 1;
	}

	void BeginFrame()
	{
		if (Active && Active->bMeasure)
		{
			Active->FrameStart = FPlatformTime::Seconds();
			Active->bFrameArmed = true;
			Active->FrameLabel = TEXT("gameplay");
		}
	}

	void EndFrame()
	{
		TSharedPtr<FState> S = Active;
		if (!S) { return; }
		if (S->bFrameArmed)
		{
			const double Ms = (FPlatformTime::Seconds() - S->FrameStart) * 1000.0;
			S->Frames.Add({ S->FrameLabel, Ms, GFrameCounter });
			S->bFrameArmed = false;
		}
		if (!S->bFinish) { return; }
		S->Frames.Sort([](const FTiming& A, const FTiming& B) { return A.Ms > B.Ms; });
		S->Spawns.Sort([](const FTiming& A, const FTiming& B) { return A.Ms > B.Ms; });
		for (int32 Index = 0; Index < FMath::Min(10, S->Frames.Num()); ++Index)
		{
			const FTiming& T = S->Frames[Index];
			UE_LOG(LogCodexTactics, Display, TEXT("[HitchProbe] frame #%llu %s %.2f ms"), T.Frame, *T.Label, T.Ms);
		}
		for (int32 Index = 0; Index < FMath::Min(10, S->Spawns.Num()); ++Index)
		{
			const FTiming& T = S->Spawns[Index];
			UE_LOG(LogCodexTactics, Display, TEXT("[HitchProbe] spawn #%llu %s %.2f ms"), T.Frame, *T.Label, T.Ms);
		}
		const UWorld* ProbeWorld = S->World.Get();
		if (const ULadderPreparedClipsSubsystem* Cache = ProbeWorld ? ProbeWorld->GetSubsystem<ULadderPreparedClipsSubsystem>() : nullptr)
		{
			Check(*S, Cache->GetEntryCount() > 0 && Cache->GetReuseCount() >= 2,
				FString::Printf(TEXT("world ladder cache: %d prepared basis entries, %d reuses"), Cache->GetEntryCount(), Cache->GetReuseCount()));
		}
		Check(*S, S->MaxLeaderTravelCm >= 30.f, FString::Printf(TEXT("leader actually moved %.1f cm during the probe"), S->MaxLeaderTravelCm));
		const FHitchGateLimits& Limits = S->Limits;
		TMap<uint64, double> SpawnMsByFrame;
		for (const FTiming& Spawn : S->Spawns) { SpawnMsByFrame.FindOrAdd(Spawn.Frame) += Spawn.Ms; }
		double MaxSpawnFrameMs = 0.0;
		for (const TPair<uint64, double>& Pair : SpawnMsByFrame) { MaxSpawnFrameMs = FMath::Max(MaxSpawnFrameMs, Pair.Value); }
		const double MaxSpawnMs = S->Spawns.IsEmpty() ? 0.0 : S->Spawns[0].Ms;
		Check(*S, !S->Spawns.IsEmpty() && MaxSpawnMs <= Limits.HardLimitMs,
			FString::Printf(TEXT("slowest spawn %.2f ms <= %.0f ms"), MaxSpawnMs, Limits.HardLimitMs));
		Check(*S, !SpawnMsByFrame.IsEmpty() && MaxSpawnFrameMs <= Limits.HardLimitMs,
			FString::Printf(TEXT("same-frame spawn sum %.2f ms <= %.0f ms"), MaxSpawnFrameMs, Limits.HardLimitMs));
		const double MaxMs = S->Frames.IsEmpty() ? 0.0 : S->Frames[0].Ms;
		Check(*S, !S->Frames.IsEmpty() && MaxMs <= Limits.HardLimitMs,
			FString::Printf(TEXT("%d post-setup game-thread frames, max %.2f ms <= %.0f ms"), S->Frames.Num(), MaxMs, Limits.HardLimitMs));
		// The stricter gate: frames within SpawnWindowFrames of an enemy spawn (the visible stutter when enemies appear).
		TArray<FHitchFrameSample> Samples;
		for (const FTiming& T : S->Frames) { Samples.Add({ T.Frame, T.Ms }); }
		TArray<uint64> AllSpawnFrames = S->RosterSpawnFrames;
		AllSpawnFrames.Append(S->WaveSpawnFrames);
		const FHitchWindowStats Roster = HitchGateRules::Summarize(Samples, S->RosterSpawnFrames, Limits);
		const FHitchWindowStats Wave = HitchGateRules::Summarize(Samples, S->WaveSpawnFrames, Limits);
		const FHitchWindowStats Spawn = HitchGateRules::Summarize(Samples, AllSpawnFrames, Limits);
		const FHitchWindowStats All = HitchGateRules::Summarize(Samples, {}, Limits, true);
		UE_LOG(LogCodexTactics, Display, TEXT("[HitchProbe] SUMMARY preload=%s | first-use roster+PMC: %d frames, max %.2f ms, %d > %.0f ms | wave (%d enemies): %d frames, max %.2f ms, %d > %.0f ms | all: %d frames, max %.2f ms, %d > %.0f ms"),
			*S->PreloadState, Roster.Frames, Roster.MaxMs, Roster.SlowFrames, Limits.SlowFrameMs, S->WaveEnemies, Wave.Frames, Wave.MaxMs, Wave.SlowFrames,
			Limits.SlowFrameMs, All.Frames, All.MaxMs, All.SlowFrames, Limits.SlowFrameMs);
		const FString Budget = Limits.MaxSlowSpawnFrames < 0 ? FString(TEXT("report only")) : FString::FromInt(Limits.MaxSlowSpawnFrames);
		Check(*S, HitchGateRules::PassesSpawnGate(Roster, Limits),
			FString::Printf(TEXT("first-use spawn windows (%d spawn frames, +%d frames each): %d frames, max %.2f ms (frame #%llu) <= %.0f ms; %d frames > %.0f ms (budget %s)"),
				S->RosterSpawnFrames.Num(), Limits.SpawnWindowFrames, Roster.Frames, Roster.MaxMs, Roster.MaxFrame, Limits.SpawnMaxFrameMs, Roster.SlowFrames,
				Limits.SlowFrameMs, *Budget));
		const FString WaveLimit = Limits.WaveMaxFrameMs < 0.0 ? FString(TEXT("report only")) : FString::Printf(TEXT("<= %.0f ms"), Limits.WaveMaxFrameMs);
		Check(*S, S->WaveEnemies < 0 || HitchGateRules::PassesWaveGate(Wave, Limits),
			FString::Printf(TEXT("scripted wave window (%d enemies in one call): max %.2f ms (%s); all spawn windows max %.2f ms, %d frames > %.0f ms"),
				S->WaveEnemies, Wave.MaxMs, *WaveLimit, Spawn.MaxMs, Spawn.SlowFrames, Limits.SlowFrameMs));
		FCoreDelegates::OnBeginFrame.Remove(S->BeginHandle);
		FCoreDelegates::OnEndFrame.Remove(S->EndHandle);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), S->Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		Active.Reset();
		FPlatformMisc::RequestExit(false, TEXT("HitchProbeSmoke"));
	}

	void Record(FState& S, const FString& Label, double Start)
	{
		const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;
		S.Spawns.Add({ Label, Ms, GFrameCounter });
		UE_LOG(LogCodexTactics, Display, TEXT("[HitchProbe] spawn %s %.2f ms"), *Label, Ms);
	}

	bool Step(float)
	{
		TSharedPtr<FState> S = Active;
		UWorld* World = S ? S->World.Get() : nullptr;
		if (!S) { return false; }
		if (!World)
		{
			++S->Failures;
			S->bFinish = true;
			return false;
		}
		if (FPlatformTime::Seconds() - S->SetupStart > 120.0)
		{
			Check(*S, false, TEXT("probe timed out before all spawns"));
			S->bFinish = true;
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		UPmcSquadSubsystem* Pmc = World->GetSubsystem<UPmcSquadSubsystem>();
		ACodexTacticsGameMode* Mode = World->GetAuthGameMode<ACodexTacticsGameMode>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!S->bStarted)
		{
			if (UGameplayPreloadSubsystem::IsPreparing(World))
			{
				const UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>();
				const UPmcEncounterSubsystem* Encounters = World->GetSubsystem<UPmcEncounterSubsystem>();
				if (!S->bObservedPreparation)
				{
					S->InitialPressureClock = Pressure ? Pressure->GetClock() : 0.f;
					S->InitialPmcClock = Encounters ? Encounters->GetClock() : 0.f;
					S->bObservedPreparation = true;
				}
				S->bPreparationClocksHeld &= (!Pressure || FMath::IsNearlyEqual(Pressure->GetClock(), S->InitialPressureClock))
					&& (!Encounters || FMath::IsNearlyEqual(Encounters->GetClock(), S->InitialPmcClock));
				if (Squad)
				{
					for (AOperativeCharacter* Member : Squad->GetMembers())
					{
						if (!Member) { continue; }
						const TWeakObjectPtr<AOperativeCharacter> Key(Member);
						if (const FVector* Before = S->PreparingPositions.Find(Key))
						{
							S->bPreparationPositionsHeld &= FVector::Dist(*Before, Member->GetActorLocation()) < 0.1;
						}
						else { S->PreparingPositions.Add(Key, Member->GetActorLocation()); }
					}
				}
				return true;
			}
			// The level-start one-shot work (drop links + the minimap floor scan, ~0.1 s in one frame, a world timer 1 s after begin play -
			// after the preload release when it runs) is no enemy-appear cost: measure after it, never in the first spawn's window.
			if (const UDropLinkSubsystem* Drops = World->GetSubsystem<UDropLinkSubsystem>(); Drops && Drops->IsEnabledForWorld() && !Drops->HasGenerated())
			{
				if (FPlatformTime::Seconds() - S->SetupStart < 60.0) { return true; }
				UE_LOG(LogCodexTactics, Display, TEXT("[HitchProbe] drop links still not generated after 60 s; measuring anyway"));
			}
			S->PreloadState = UGameplayPreloadSubsystem::StateName(UGameplayPreloadSubsystem::GetState(World));
			if (UGameplayPreloadSubsystem::GetState(World) == EGameplayPreloadState::Disabled)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[HitchProbe] world preload disabled (Codex.Preload.Enabled 0): measuring the synchronous first-use path"));
			}
			else if (const UGameplayPreloadSubsystem* Preload = World->GetSubsystem<UGameplayPreloadSubsystem>())
			{
				Check(*S, Preload->IsReady() && !Preload->HasFailed(), TEXT("world preload completed successfully before measurement"));
				if (Preload->HasFailed()) { S->bFinish = true; return false; }
			}
			if (!World->HasBegunPlay() || !Leader || !Waves || !Pmc || !Mode || !Mode->GetActiveLevelConfig())
			{
				if (FPlatformTime::Seconds() - S->SetupStart < 30.0) { return true; }
				Check(*S, false, TEXT("world, active level, squad and spawn subsystems ready"));
				S->bFinish = true; return false;
			}
			if (S->bObservedPreparation)
			{
				Check(*S, S->bPreparationClocksHeld, TEXT("observed preparation holds Pressure and PMC clocks"));
				Check(*S, S->bPreparationPositionsHeld, TEXT("observed preparation holds operative positions"));
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[HitchProbe] preparation already finished before the probe; hold checks not observed"));
			}
			if (UGameplayPreloadSubsystem* Preload = World->GetSubsystem<UGameplayPreloadSubsystem>())
			{
				const bool bFirstTicket = Preload->TryClaimAutomaticSpawn();
				const bool bSecondTicket = Preload->TryClaimAutomaticSpawn();
				Check(*S, bFirstTicket && !bSecondTicket, TEXT("only one automatic dispatch ticket per world frame"));
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS); // Test that retained preload/cache references survive GC before delayed first use.
			const FLevelCombatConfig& Level = Mode->GetActiveLevelConfig()->Config;
			for (const FPressureTypeWeight& Weight : CombatPressureRules::EffectiveWeights(Level.Pressure.TypeWeights))
			{
				S->Roster.AddUnique(FPressureEnemyType(Weight.Type, Weight.Variant));
			}
			for (const FWaveDefinition& Wave : Level.Waves)
			{
				for (const FEnemySpawnEntry& Entry : Wave.Spawns) { S->Roster.AddUnique(FPressureEnemyType(Entry.EnemyType, Entry.Variant)); }
			}
			// Include the engine-registered roster as well: alternate horde/weather rolls may introduce another class.
			for (const TPair<EEnemyArchetype, TSoftClassPtr<AEnemyCharacter>>& Entry : Mode->EnemyClasses)
			{
				if (!Entry.Value.IsNull()) { S->Roster.AddUnique(FPressureEnemyType(Entry.Key)); }
			}
			S->Roster.Sort([](const FPressureEnemyType& A, const FPressureEnemyType& B)
			{
				return A.Type != B.Type ? static_cast<uint8>(A.Type) < static_cast<uint8>(B.Type) : A.Variant.ToString() < B.Variant.ToString();
			});
			const FPressureEnemyType Military(EEnemyArchetype::Frostbitten, CombatPressureRules::MilitaryZombieVariant());
			S->Roster.Remove(Military);
			S->Roster.Insert(Military, 0); // reproduce the first pressure/PMC collision while both paths are cold
			S->Origin = Leader->GetActorLocation();
			S->Forward = Leader->GetActorForwardVector().GetSafeNormal2D();
			if (S->Forward.IsNearlyZero()) { S->Forward = FVector::ForwardVector; }
			S->Right = FVector::CrossProduct(FVector::UpVector, S->Forward);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member && Member->HealthComponent) { Member->HealthComponent->SetMaxHealth(100000.f); }
				if (Member) { Member->bTacticalCeaseFire = true; }
			}
			FNavLocation Goal;
			const FVector Wanted = S->Origin + S->Forward * 600.f;
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			const bool bProjected = Nav && Nav->ProjectPointToNavigation(Wanted, Goal, FVector(500.f, 500.f, 500.f));
			const bool bMove = bProjected && Leader->OrderMoveTo(Goal.Location, false) == EOperativeOrderResult::Accepted;
			Check(*S, bMove, TEXT("real squad leader movement order accepted"));
			UE_LOG(LogCodexTactics, Display, TEXT("[HitchProbe] module-to-setup %.2f ms; setup %.2f ms; roster %d"),
				(FPlatformTime::Seconds() - ModuleStart) * 1000.0, (FPlatformTime::Seconds() - S->SetupStart) * 1000.0, S->Roster.Num());
			S->bStarted = true;
			S->bMeasure = true; // BeginFrame arms measurement on the next frame, after setup
			S->NextSpawnAt = World->GetTimeSeconds() + 1.0;
			return true;
		}
		if (Leader) { S->MaxLeaderTravelCm = FMath::Max(S->MaxLeaderTravelCm, static_cast<float>(FVector::Dist2D(S->Origin, Leader->GetActorLocation()))); }
		if (World->GetTimeSeconds() < S->NextSpawnAt) { return true; }
		if (S->Next < S->Roster.Num())
		{
			const FPressureEnemyType Entry = S->Roster[S->Next];
			const FString Label = CombatPressureRules::EnemyLabel(Entry);
			S->FrameLabel = Label;
			const FVector Location = S->Origin + S->Forward * (1600.f + 180.f * S->Next) + S->Right * (300.f * (S->Next % 3)) + FVector(0.f, 0.f, 100.f);
			const TSoftClassPtr<AEnemyCharacter>* Expected = Entry.Variant.IsNone() ? Mode->EnemyClasses.Find(Entry.Type) : Mode->EnemyVariantClasses.Find(Entry.Variant);
			UE_LOG(LogCodexTactics, Display, TEXT("[HitchProbe] %s class resident before spawn: %s"), *Label,
				Expected && Expected->Get() ? TEXT("yes") : TEXT("no"));
			const double Start = FPlatformTime::Seconds();
			AEnemyCharacter* Enemy = Waves->SpawnLevelEnemy(Entry.Type, Location, (-S->Forward).Rotation(), Entry.Variant);
			Record(*S, Label, Start);
			S->RosterSpawnFrames.AddUnique(GFrameCounter);
			const bool bVariantConfigured = Entry.Variant.IsNone() || (Expected && !Expected->IsNull());
			const bool bCorrectClass = Enemy && (!Expected || Expected->IsNull() || Enemy->GetClass()->GetPathName() == Expected->ToSoftObjectPath().ToString());
			Check(*S, bVariantConfigured && bCorrectClass,
				FString::Printf(TEXT("%s spawned its configured class (%s)"), *Label, *GetNameSafe(Enemy ? Enemy->GetClass() : nullptr)));
			const USkeletalMesh* Mesh = Enemy && Enemy->GetMesh() ? Enemy->GetMesh()->GetSkeletalMeshAsset() : nullptr;
			UE_LOG(LogCodexTactics, Display, TEXT("[HitchProbe] %s actual class %s, mesh %s"), *Label,
				*GetPathNameSafe(Enemy ? Enemy->GetClass() : nullptr), *GetPathNameSafe(Mesh));
			if (S->Next == 0)
			{
				S->FrameLabel += TEXT(" + PMC");
				const FString& BodyPath = PmcOperatorRules::Get().BodyMesh;
				UE_LOG(LogCodexTactics, Display, TEXT("[HitchProbe] PMC body resident before spawn: %s (%s)"),
					!BodyPath.IsEmpty() && FSoftObjectPath(BodyPath).ResolveObject() ? TEXT("yes") : TEXT("no"), *BodyPath);
				const double PmcStart = FPlatformTime::Seconds();
				const TArray<AOperativeCharacter*> Fighters = Pmc->SpawnSquad(101, S->Origin + S->Forward * 2200.f + S->Right * 700.f, (-S->Forward).Rotation(), 3, 5);
				Record(*S, TEXT("PMC squad (3)"), PmcStart);
				S->RosterSpawnFrames.AddUnique(GFrameCounter);
				Check(*S, Fighters.Num() == 3, TEXT("three PMC fighters spawned"));
			}
			++S->Next;
			S->NextSpawnAt = World->GetTimeSeconds() + 1.0;
			return true;
		}
		// Then a scripted wave the way a wave start spawns one (ResolveRoster -> SpawnLevelWave): the whole roster in one call.
		if (!S->bWaveStarted)
		{
			S->bWaveStarted = true;
			const FLevelCombatConfig& Level = Mode->GetActiveLevelConfig()->Config;
			if (Level.Waves.IsEmpty())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[HitchProbe] the level has no scripted wave; wave window not measured"));
				S->bFinish = true;
				return false;
			}
			S->FrameLabel = TEXT("wave 1");
			const double Start = FPlatformTime::Seconds();
			S->WaveEnemies = Waves->SpawnScriptedWaveNow(1); // the depot is a skirmish level: StartWave would only adopt engaged enemies
			Record(*S, FString::Printf(TEXT("wave 1 (%d enemies)"), S->WaveEnemies), Start);
			S->WaveSpawnFrames.AddUnique(GFrameCounter);
			Check(*S, S->WaveEnemies > 0, FString::Printf(TEXT("the scripted wave spawned %d enemies"), S->WaveEnemies));
			S->NextSpawnAt = World->GetTimeSeconds() + 2.0; // its window plus the first AI / animation frames
			return true;
		}
		S->bFinish = true;
		return false;
	}

	void Run(const TArray<FString>&, UWorld* World)
	{
		if (!World || Active) { return; }
		Active = MakeShared<FState>();
		Active->World = World;
		Active->SetupStart = FPlatformTime::Seconds();
		Active->Limits = HitchGateRules::FromConsole();
		Active->BeginHandle = FCoreDelegates::OnBeginFrame.AddStatic(&BeginFrame);
		Active->EndHandle = FCoreDelegates::OnEndFrame.AddStatic(&EndFrame);
		UE_LOG(LogCodexTactics, Display, TEXT("[HitchProbe] command at module + %.2f ms; use -trace=cpu,loadtime,frame for scope detail"),
			(FPlatformTime::Seconds() - ModuleStart) * 1000.0);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Step), 0.1f);
	}
	static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.HitchProbeSmoke"),
		TEXT("Measures post-setup game-thread frames and cold first-use spawns on the active level."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}
#endif
