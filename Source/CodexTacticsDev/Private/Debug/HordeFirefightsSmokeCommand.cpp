// Dev-only headless check of horde #12 (game designer 2026-11-02 / 11-03 / 11-08 / 11-09; UFirefightSubsystem, UHordeSubsystem::RequestFirefightHorde,
// UHordeIntroSubsystem::BeginExternalHorde) on L_Gen_Depot_1 with the pinned level JSON SmokeFixtures/horde_firefights_smoke.json (the Depot's
// settings - horde ON after 10 firefights, checked first - with fixed waves, no PMC squads; the smoke then sets the threshold to 2 by the
// console, the pressure off, the squad tough and holding fire):
//   Scripts/smoke_parallel.ps1 -Commands HordeFirefightsSmoke   (map /Game/Maps/Generated/L_Gen_Depot_1, Scripts/test_map.json)
//   any level JSON (the designer's Training Ground waves): -LevelJson=<file> and "CodexTactics.HordeFirefightsSmoke hordes=3" - every check
//   reads the level's own waves.
// 1. Firefight 1: a pressure patrol (2 frostbitten) 12 m ahead, the squad's shot hits one -> it notices (contact + shots) = ONE firefight
//    (more hits in the same encounter do not add one); the HQ hint "one more firefight"; both killed -> the encounter is over (all down).
// 2. Slip-past: a patrol with muted senses comes within its detection reach (10 m) and is left behind (30 m) unseen -> the streak resets.
// 3. Firefight 2: 1 of 2 again (no horde). Firefight 3: 2 of 2 -> the HQ warning, the patrol is killed at once (exploration again), the
//    countdown runs out -> the fight starts with the horde, the horde intro gives its warning (alert, flyover) for the external horde; the
//    horde never counts as a firefight (the streak stays 0).
// 4. Game designer play-test 2026-11-09 (feat/horde-300m-waves), per horde (hordes=N, default 2; the 2nd+ requested straight away): it
//    spawns FAR away (in the 260-340 m band, or - the Depot is 177 x 124 m - among the farthest reachable out-of-sight points), out of
//    sight, somewhere new (>= 30 m from the earlier hordes' points, the bearing >= 90 deg off the previous one); it is the level's wave N
//    (the last repeating; inactive waves skipped) - its composition with SPITTER as MILITARY_ZOMBIE, its max_simultaneous_enemies first,
//    the rest held back in the fight (UWaveSubsystem::GetExternalPendingSpawns) and coming as the living fall, its enemy_hp_mult; the HUD
//    warning points at it, the HQ line gives the distance; each kind's first members run to the squad (arrival times logged:
//    "Smoke: arrival ..."), timed kinds are shot down; the fight ends only when the whole wave is down.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PerceptionRules.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureRules.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/FirefightSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/HordeIntroSubsystem.h"
#include "Combat/HordeSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "UI/GameMessageSubsystem.h"

namespace HordeFirefightsSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		float Total = 0.f;
		int32 Failures = 0;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Group;
		int32 FeedMark = 0;
		/** Which firefight the running stage plays (1..3). */
		int32 Firefight = 0;
		/** The horde loop (stages 10-12). */
		int32 HordeK = 1;
		int32 HordesToRun = 2;
		TSet<TWeakObjectPtr<AEnemyCharacter>> Seen;
		TWeakObjectPtr<AEnemyCharacter> LastSeen;
		TMap<FString, int32> SpawnedByLabel;
		TMap<FString, int32> Expected;
		int32 ExpectedTotal = 0;
		bool bCapChecked = false;
		bool bIntroChecked = false;
		int32 WarningsBefore = 0;
		bool bHuntersOnly = true;
		bool bModifierChecked = false;
		float WaveHpMult = 1.f;
		float HoundBaseHealth = 0.f;
		/** A kind's members are left alive this long to reach the squad, then shot down anyway. */
		float TypeTimeout = 200.f;
		TMap<FString, UHordeSubsystem::FArrival> Timed;
		TArray<float> Distances;
		TArray<FVector> SpawnPoints;
		TArray<float> Bearings;
	};

	/** The label the horde logs for an enemy (CombatPressureRules::EnemyLabel; the Military Zombie by its Blueprint). */
	FString LabelOf(const AEnemyCharacter& Enemy)
	{
		const bool bZombie = GetNameSafe(Enemy.GetClass()).Contains(TEXT("MilitaryZombie"));
		return CombatPressureRules::EnemyLabel(FPressureEnemyType(Enemy.GetArchetype(), bZombie ? CombatPressureRules::MilitaryZombieVariant() : NAME_None))
			.Replace(TEXT("EEnemyArchetype::"), TEXT(""));
	}

	const TArray<FWaveDefinition>& LevelWavesOf(UWorld* World)
	{
		static const TArray<FWaveDefinition> None;
		const ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
		const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
		return Level ? Level->Config.Waves : None;
	}

	/** What a wave should bring as a horde, by label - read straight from its spawn entries (SPITTER -> MILITARY_ZOMBIE). */
	TMap<FString, int32> ExpectedLabels(const TArray<FWaveDefinition>& Waves, int32 WaveIndex)
	{
		TMap<FString, int32> Out;
		if (!Waves.IsValidIndex(WaveIndex))
		{
			return Out;
		}
		for (const FEnemySpawnEntry& Entry : Waves[WaveIndex].Spawns)
		{
			if (Entry.EnemyType == EEnemyArchetype::Base || Entry.Count <= 0)
			{
				continue;
			}
			const FString Label = Entry.EnemyType == EEnemyArchetype::Spitter ? FString(TEXT("MILITARY_ZOMBIE"))
				: CombatPressureRules::EnemyLabel(FPressureEnemyType(Entry.EnemyType, Entry.Variant)).Replace(TEXT("EEnemyArchetype::"), TEXT(""));
			Out.FindOrAdd(Label) += Entry.Count;
		}
		return Out;
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void SetCVar(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByConsole);
		}
	}

	bool Finish(const FState& State, bool bComplete)
	{
		SetCVar(TEXT("Codex.Horde.Enabled"), -1.f);
		SetCVar(TEXT("Codex.Horde.AfterFirefights"), -1.f);
		SetCVar(TEXT("Codex.Horde.FirefightWarningSeconds"), -1.f);
		SetCVar(TEXT("Codex.Pressure.Enabled"), -1.f);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("HordeFirefightsSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	/** Feed lines posted since the mark, joined. */
	FString FeedSince(UWorld* World, int32 Mark)
	{
		FString Out;
		if (const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
		{
			for (int32 Index = FMath::Max(Mark, 0); Index < Messages->GetHistory().Num(); ++Index)
			{
				Out += Messages->GetHistory()[Index].Text.ToString() + TEXT(" | ");
			}
		}
		return Out;
	}

	int32 FeedCount(UWorld* World)
	{
		const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>();
		return Messages ? Messages->GetHistory().Num() : 0;
	}

	/** A navmesh spot Distance cm ahead of the leader (turned until a clear walk reaches it). */
	FVector SpotAhead(UWorld* World, const AOperativeCharacter& Leader, float Distance, float TurnDeg = 0.f)
	{
		const FVector From = Leader.GetActorLocation();
		const FVector Ahead = Leader.GetActorForwardVector().GetSafeNormal2D().RotateAngleAxis(TurnDeg, FVector::UpVector);
		return SmokeUtils::ClearPoint(World, From, From + Ahead * Distance);
	}

	void SpawnGroup(UWorld* World, FState& State, const AOperativeCharacter& Leader, float Distance, float TurnDeg)
	{
		State.Group.Reset();
		UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>();
		if (!Pressure)
		{
			return;
		}
		const FVector Feet = SpotAhead(World, Leader, Distance, TurnDeg);
		for (AEnemyCharacter* Enemy : Pressure->SpawnPatrolGroupAt(Feet, { FPressureEnemyType(EEnemyArchetype::Frostbitten), FPressureEnemyType(EEnemyArchetype::Frostbitten) }))
		{
			State.Group.Add(Enemy);
		}
	}

	void Hit(AEnemyCharacter* Enemy, const AOperativeCharacter& Shooter, float Amount)
	{
		if (Enemy && Enemy->GetHealthComponent())
		{
			FDamageSpec Spec;
			Spec.Amount = Amount;
			Spec.ArmorPenetration = 1.f;
			Spec.AttackerSource = Shooter.DisplayName.ToString();
			Enemy->GetHealthComponent()->TakeDamage(Spec);
		}
	}

	bool GroupDown(const FState& State)
	{
		for (const TWeakObjectPtr<AEnemyCharacter>& Member : State.Group)
		{
			if (Member.IsValid() && !Member->IsDying())
			{
				return false;
			}
		}
		return true;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.StageTime += StepSeconds;
		State.Total += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Total > 1500.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return Finish(State, false);
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UHordeSubsystem* Horde = World->GetSubsystem<UHordeSubsystem>();
		UFirefightSubsystem* Fire = World->GetSubsystem<UFirefightSubsystem>();
		UHordeIntroSubsystem* Intro = World->GetSubsystem<UHordeIntroSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Flow || !Squad || !Horde || !Fire || !Intro || !Waves || !Leader)
		{
			return State.Total < 10.f || Finish(State, false);
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = true; // only the smoke's own hits count as the squad's shots
			Member->HealthComponent->SetMaxHealth(100000.f, false);
			Member->HealthComponent->Heal(100000.f);
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			// The Depot's settings (game designer 2026-11-09; the pinned fixture: on, after 10 firefights in a row - another -LevelJson its own
			// horde_after_firefights); no real-time-fight clock.
			Horde->RefreshConfig();
			const ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
			const ULevelConfigAsset* LevelConfig = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
			const int32 LevelThreshold = LevelConfig ? LevelConfig->Config.HordeAfterFirefights : 10;
			Check(State, Horde->GetConfig().bEnabled && Horde->GetConfig().AfterFirefights == LevelThreshold && Fire->GetThreshold() == LevelThreshold,
				FString::Printf(TEXT("the Depot's level JSON: horde %s after %d firefights in a row"), Horde->GetConfig().bEnabled ? TEXT("on") : TEXT("OFF"),
					Horde->GetConfig().AfterFirefights));
			Check(State, Horde->GetConfig().FirefightSpawnDistanceCm == 30000.f && Horde->GetConfig().bWavesFeedHorde,
				TEXT("... the firefight horde 300 m away, the level's waves feed it (horde.json)"));
			Check(State, !LevelWavesOf(World).IsEmpty(), FString::Printf(TEXT("... %d active level waves"), LevelWavesOf(World).Num()));
			Check(State, IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Horde.TriggerSeconds")) == nullptr, TEXT("... no real-time-fight horde clock"));
			SetCVar(TEXT("Codex.Pressure.Enabled"), 0.f);
			SetCVar(TEXT("Codex.Horde.Enabled"), 1.f);
			SetCVar(TEXT("Codex.Horde.AfterFirefights"), 2.f);
			SetCVar(TEXT("Codex.Horde.FirefightWarningSeconds"), 4.f);
			Horde->RefreshConfig(); // the console values above
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			Fire->ResetForTest();
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("exploring the Depot"));
			Check(State, Fire->GetThreshold() == 2 && Horde->GetConfig().bEnabled,
				FString::Printf(TEXT("the horde after %d firefights in a row (level horde_after_firefights 10 -> console 2), horde on"), Fire->GetThreshold()));
			Next(State);
			return true;
		}
		case 1: case 5: case 7:
		{
			// A firefight: the patrol appears 12 m ahead, the squad's hit makes it notice (a fresh encounter: the last one is over).
			if (State.StageTime < 0.5f || ((Fire->IsEncounterActive() || Flow->GetPhase() != ECodexGamePhase::Exploration) && State.StageTime < 6.f))
			{
				return true;
			}
			State.Firefight = State.Stage == 1 ? 1 : State.Stage == 5 ? 2 : 3;
			State.FeedMark = FeedCount(World);
			SpawnGroup(World, State, *Leader, 1200.f, State.Firefight * 25.f);
			Check(State, State.Group.Num() == 2, FString::Printf(TEXT("firefight %d: a patrol of %d, 12 m ahead"), State.Firefight, State.Group.Num()));
			Hit(State.Group.Num() > 0 ? State.Group[0].Get() : nullptr, *Leader, 10.f);
			Next(State);
			return true;
		}
		case 2: case 6: case 8:
		{
			if (!Fire->IsFirefightActive() && State.StageTime < 5.f)
			{
				return true;
			}
			const int32 ExpectedStreak = State.Firefight == 3 ? 0 : 1; // the 2nd of 2 (firefight 3) brings the horde and starts again
			Check(State, Fire->IsFirefightActive(), FString::Printf(TEXT("firefight %d: contact + shots exchanged = a firefight (%s)"), State.Firefight,
				*FeedSince(World, State.FeedMark)));
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat, TEXT("... the contact started the skirmish"));
			const int32 Before = Fire->GetFirefightCount();
			Hit(State.Group.Num() > 1 ? State.Group[1].Get() : nullptr, *Leader, 10.f); // more shots, the same encounter
			Hit(State.Group.Num() > 0 ? State.Group[0].Get() : nullptr, *Leader, 10.f);
			Check(State, Fire->GetFirefightCount() == Before && Fire->GetFirefightCount() == State.Firefight,
				FString::Printf(TEXT("... one per encounter, not per shot (%d firefights on the level)"), Fire->GetFirefightCount()));
			Check(State, Fire->GetStreak() == ExpectedStreak, FString::Printf(TEXT("... %d in a row now"), Fire->GetStreak()));
			const FString Feed = FeedSince(World, State.FeedMark);
			if (State.Firefight == 3)
			{
				Check(State, Horde->IsFirefightHordePending() && Feed.Contains(TEXT("HORDE is closing")),
					FString::Printf(TEXT("2 in a row: the HQ warning before the horde (%.1f s to go)"), Horde->GetFirefightHordeSecondsLeft()));
			}
			else
			{
				Check(State, !Horde->IsFirefightHordePending() && Feed.Contains(TEXT("One more firefight")), TEXT("... no horde yet, the HQ hint \"one more firefight\""));
			}
			for (const TWeakObjectPtr<AEnemyCharacter>& Member : State.Group)
			{
				Hit(Member.Get(), *Leader, 100000.f);
			}
			Next(State);
			return true;
		}
		case 3: case 9:
		{
			// The patrol is down: the encounter is over, the skirmish too.
			if ((Fire->IsEncounterActive() || Flow->GetPhase() != ECodexGamePhase::Exploration) && State.StageTime < 5.f)
			{
				return true;
			}
			Check(State, GroupDown(State) && !Fire->IsEncounterActive() && Fire->GetLastEnd() == EFirefightEnd::AllDown,
				TEXT("every engaged enemy down: the encounter is over"));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("... back to exploration"));
			if (State.Stage == 3)
			{
				// 2. The slip-past: muted senses (it can never spot the squad), 10 m away - inside its detection reach.
				State.FeedMark = FeedCount(World);
				SpawnGroup(World, State, *Leader, 1000.f, -60.f);
				for (const TWeakObjectPtr<AEnemyCharacter>& Member : State.Group)
				{
					if (AEnemyCharacter* Enemy = Member.Get())
					{
						Enemy->PerceptionOverride = PerceptionRules::Scaled(Enemy->GetPerception(), 0.f);
						Enemy->PerceptionOverride.SmellDetectCm = 0.f;
						Enemy->bOverridePerception = true;
						Enemy->CustomTimeDilation = 0.f;
						Enemy->SetActorTickEnabled(false);
					}
				}
				Check(State, State.Group.Num() == 2 && Fire->GetStreak() == 1, FString::Printf(TEXT("slip-past: a blind patrol 10 m away, the streak at %d"),
					Fire->GetStreak()));
			}
			Next(State);
			return true;
		}
		case 4:
		{
			if (State.StageTime < 1.f)
			{
				return true; // a group pass sees it within the reach
			}
			if (State.StageTime < 1.2f)
			{
				// The squad leaves it behind: 30 m (the reach 15 m x 1.25 = 18.75 m) - moved, never woken.
				const FVector Away = (State.Group.Num() > 0 && State.Group[0].IsValid() ? State.Group[0]->GetActorLocation() - Leader->GetActorLocation()
					: FVector(1.f, 0.f, 0.f)).GetSafeNormal2D();
				for (const TWeakObjectPtr<AEnemyCharacter>& Member : State.Group)
				{
					if (AEnemyCharacter* Enemy = Member.Get())
					{
						Enemy->SetActorLocation(Enemy->GetActorLocation() + Away * 2000.f, false, nullptr, ETeleportType::TeleportPhysics);
					}
				}
				return true;
			}
			if (Fire->GetSlipPastCount() == 0 && State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, Fire->GetSlipPastCount() == 1 && Fire->GetStreak() == 0,
				FString::Printf(TEXT("slipped past unseen: the streak resets (%d -> %d, %d slip-past)"), 1, Fire->GetStreak(), Fire->GetSlipPastCount()));
			Check(State, FeedSince(World, State.FeedMark).Contains(TEXT("never saw you")), TEXT("... the HQ line \"they never saw you\""));
			Check(State, Fire->GetFirefightCount() == 1 && !Fire->IsEncounterActive(), TEXT("... no contact, no firefight"));
			for (const TWeakObjectPtr<AEnemyCharacter>& Member : State.Group)
			{
				if (Member.IsValid())
				{
					Member->Destroy();
				}
			}
			State.Group.Reset();
			Next(State);
			return true;
		}
		case 10:
		{
			// Horde K is announced: the first by the firefight streak (above), the next ones straight away (what the 2nd / 3rd threshold does).
			if (State.HordeK > 1)
			{
				if (Flow->GetPhase() != ECodexGamePhase::Exploration && State.StageTime < 15.f)
				{
					return true;
				}
				State.FeedMark = FeedCount(World);
				Check(State, Horde->RequestFirefightHorde(), FString::Printf(TEXT("horde %d: requested (the level's %d-th firefight horde)"), State.HordeK, State.HordeK));
			}
			State.Seen.Reset();
			State.SpawnedByLabel.Reset();
			State.bCapChecked = false;
			Next(State);
			return true;
		}
		case 11:
		{
			// The countdown runs in exploration: the horde starts a fight of its own, far away.
			if (Horde->GetFirefightHordesReleased() < State.HordeK && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, Horde->GetFirefightHordesReleased() == State.HordeK && Flow->GetPhase() == ECodexGamePhase::WaveCombat,
				FString::Printf(TEXT("horde %d came %.1f s after the warning and the fight is on"), State.HordeK, State.StageTime));
			const FHordePlan& Plan = Horde->GetActivePlan();
			const FVector Point = Horde->GetLastSpawnPoint();
			const float Distance = FVector::Dist2D(Point, Horde->GetLastSquadCentre());
			const float BandMin = HordeRules::FarBandMinCm(Horde->GetConfig());
			const bool bInBand = Horde->GetLastSpawnPick() == EHordeFarPick::InBand;
			Check(State, bInBand ? Distance >= BandMin - 1.f : Horde->GetLastSpawnPick() == EHordeFarPick::Farthest && Distance >= Horde->GetConfig().MinDistanceCm,
				FString::Printf(TEXT("horde %d: %.0f m from the squad at bearing %.0f deg (%s; search %.1f ms)"), State.HordeK, Distance / 100.f,
					Horde->GetLastSpawnBearingDeg(), bInBand ? TEXT("in the 260-340 m band") : TEXT("this map has no point 260-340 m away: the farthest"),
					Horde->GetLastSpawnSearchMs()));
			Check(State, Horde->WasLastSpawnHidden(), TEXT("... out of the squad's sight"));
			State.Distances.Add(Distance);
			for (int32 Index = 0; Index < State.SpawnPoints.Num(); ++Index)
			{
				const float Apart = FVector::Dist2D(State.SpawnPoints[Index], Point);
				const float Turn = FMath::Abs(FMath::FindDeltaAngleDegrees(State.Bearings[Index], Horde->GetLastSpawnBearingDeg()));
				// The previous horde's bearing is kept off unless no reachable out-of-sight point off it existed (logged by the horde).
				const bool bLast = Index == State.SpawnPoints.Num() - 1;
				Check(State, Apart > 3000.f && (!bLast || Turn >= Horde->GetConfig().FirefightBearingChangeDeg - 1.f || Horde->WasLastSpawnBearingRelaxed()),
					FString::Printf(TEXT("... a different place than horde %d: %.0f m apart, the bearing turned %.0f deg%s"), Index + 1, Apart / 100.f, Turn,
						bLast && Horde->WasLastSpawnBearingRelaxed() ? TEXT(" (no out-of-sight point off the previous bearing: relaxed)") : TEXT("")));
			}
			State.SpawnPoints.Add(Point);
			State.Bearings.Add(Horde->GetLastSpawnBearingDeg());
			// The wave it comes from: the level's active waves in order, the last repeating.
			const TArray<FWaveDefinition>& LevelWaves = LevelWavesOf(World);
			const int32 WaveIndex = HordeRules::WaveForHorde(State.HordeK, LevelWaves.Num());
			State.Expected = ExpectedLabels(LevelWaves, WaveIndex);
			State.ExpectedTotal = 0;
			for (const TPair<FString, int32>& Each : State.Expected)
			{
				State.ExpectedTotal += Each.Value;
			}
			const int32 Cap = LevelWaves.IsValidIndex(WaveIndex) ? FMath::Max(1, LevelWaves[WaveIndex].MaxSimultaneousEnemies) : 0;
			Check(State, Plan.SourceWave == WaveIndex + 1 && Plan.Members.Num() == State.ExpectedTotal && Plan.MaxAlive == Cap,
				FString::Printf(TEXT("horde %d = level wave %d: %s (%d, at most %d alive)"), State.HordeK, Plan.SourceWave, *Plan.Describe(),
					Plan.Members.Num(), Plan.MaxAlive));
			const int32 First = FMath::Min(Cap, State.ExpectedTotal);
			Check(State, Horde->GetActiveSpawned() == First && Waves->GetExternalPendingSpawns() == State.ExpectedTotal - First,
				FString::Printf(TEXT("... %d came first (the wave's max_simultaneous_enemies), %d held back for the fight"), Horde->GetActiveSpawned(),
					Waves->GetExternalPendingSpawns()));
			if (LevelWaves.IsValidIndex(WaveIndex))
			{
				State.WaveHpMult = LevelWaves[WaveIndex].Modifiers.EnemyHpMult;
			}
			FVector WarningAt;
			int32 WarningCount = 0;
			float WarningLeft = 0.f;
			Check(State, Horde->GetActiveWarning(WarningAt, WarningCount, WarningLeft) && WarningAt.Equals(Point, 1.f),
				TEXT("... the HUD \"HORDE!\" warning + arrow points at it"));
			Check(State, FeedSince(World, State.FeedMark).Contains(FString::Printf(TEXT("%.0f m!"), Distance / 100.f)),
				TEXT("... the HQ line gives its distance"));
			if (State.HordeK == 1)
			{
				Check(State, Intro->GetExternalIntroCount() == 1, TEXT("the horde intro takes the external horde"));
			}
			State.WarningsBefore = Intro->GetWarningCount();
			Next(State);
			return true;
		}
		case 12:
		{
			// The horde runs in: each kind's first members are left to reach the squad (the arrival times); kinds already timed are shot down so
			// the held-back members come (from the horde's spawn point, as the living fall). The whole wave must come, then the fight ends.
			if (Intro->IsIntroPending() && (Intro->GetWarningCount() > State.WarningsBefore || State.StageTime > 15.f))
			{
				if (State.HordeK == 1 && !State.bIntroChecked)
				{
					State.bIntroChecked = true;
					Check(State, Intro->GetWarningCount() == 1 && Intro->GetStartedCount() == 1, TEXT("the horde intro's warning + the level's first flyover"));
				}
				Intro->DismissIntro(); // the run-in is measured in real time
			}
			int32 Alive = 0;
			for (const TWeakObjectPtr<AEnemyCharacter>& Member : Horde->GetLastHorde())
			{
				AEnemyCharacter* Enemy = Member.Get();
				if (!Enemy)
				{
					continue;
				}
				if (!State.Seen.Contains(TWeakObjectPtr<AEnemyCharacter>(Enemy)))
				{
					State.Seen.Add(TWeakObjectPtr<AEnemyCharacter>(Enemy));
					State.LastSeen = Enemy;
					++State.SpawnedByLabel.FindOrAdd(LabelOf(*Enemy));
					State.bHuntersOnly &= Enemy->bKnowsSquadPosition;
					if (State.WaveHpMult > 1.01f && Enemy->GetArchetype() == EEnemyArchetype::FrostHound && State.HoundBaseHealth > 0.f && !State.bModifierChecked)
					{
						State.bModifierChecked = true;
						const float Ratio = Enemy->GetHealthComponent()->GetMaxHealth() / State.HoundBaseHealth;
						Check(State, FMath::IsNearlyEqual(Ratio, State.WaveHpMult, 0.02f),
							FString::Printf(TEXT("the wave's enemy_hp_mult x%.2f on the horde's hounds (x%.2f measured)"), State.WaveHpMult, Ratio));
					}
					if (Enemy->GetArchetype() == EEnemyArchetype::FrostHound && State.WaveHpMult <= 1.01f && State.HoundBaseHealth <= 0.f)
					{
						State.HoundBaseHealth = Enemy->GetHealthComponent()->GetMaxHealth();
					}
				}
				if (Enemy->IsDying())
				{
					continue;
				}
				++Alive;
				const FString Label = LabelOf(*Enemy);
				const bool bTimed = State.Timed.Contains(Label);
				if (bTimed || State.StageTime > State.TypeTimeout)
				{
					Hit(Enemy, *Leader, 100000.f);
				}
			}
			for (const UHordeSubsystem::FArrival& Arrival : Horde->GetArrivals())
			{
				if (!State.Timed.Contains(Arrival.Label))
				{
					State.Timed.Add(Arrival.Label, Arrival);
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke: arrival %s %.1f s from %.0f m (%.1f m/s straight line; 300 m = ~%.0f s)"), *Arrival.Label,
						Arrival.Seconds, Arrival.SpawnDistanceCm / 100.f, Arrival.SpawnDistanceCm / 100.f / FMath::Max(Arrival.Seconds, 0.1f),
						Arrival.Seconds * 30000.f / FMath::Max(Arrival.SpawnDistanceCm, 1.f));
				}
			}
			if (!State.bCapChecked && Horde->GetActiveSpawned() > FMath::Min(Horde->GetActivePlan().MaxAlive, State.ExpectedTotal))
			{
				State.bCapChecked = true;
				Check(State, FVector::Dist2D((State.LastSeen.IsValid() ? State.LastSeen->GetActorLocation() : FVector::ZeroVector), Horde->GetLastSpawnPoint()) < 30000.f,
					TEXT("the held-back members come as the living fall"));
			}
			const bool bAllCame = Horde->GetActiveQueued() == 0 && Horde->GetActiveSpawned() == State.ExpectedTotal;
			if ((!bAllCame || Alive > 0 || Flow->GetPhase() != ECodexGamePhase::Exploration) && State.StageTime < State.TypeTimeout + 60.f)
			{
				return true;
			}
			Check(State, bAllCame && Alive == 0, FString::Printf(TEXT("horde %d: the whole wave came (%d / %d), all down"), State.HordeK,
				Horde->GetActiveSpawned(), State.ExpectedTotal));
			TArray<FString> Got;
			bool bSame = State.SpawnedByLabel.Num() == State.Expected.Num();
			for (const TPair<FString, int32>& Each : State.Expected)
			{
				const int32* Count = State.SpawnedByLabel.Find(Each.Key);
				bSame &= Count && *Count == Each.Value;
			}
			for (const TPair<FString, int32>& Each : State.SpawnedByLabel)
			{
				Got.Add(FString::Printf(TEXT("%d %s"), Each.Value, *Each.Key));
			}
			Check(State, bSame && !State.SpawnedByLabel.Contains(TEXT("Spitter")),
				FString::Printf(TEXT("... the wave's composition, SPITTER as MILITARY_ZOMBIE (%s)"), *FString::Join(Got, TEXT(", "))));
			Check(State, State.bHuntersOnly, TEXT("... they all knew where the squad was"));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && Waves->GetExternalPendingSpawns() == 0,
				TEXT("... the fight held until the last held-back member was down, then back to exploration"));
			if (State.HordeK < State.HordesToRun)
			{
				++State.HordeK;
				State.Stage = 10;
				State.StageTime = 0.f;
				return true;
			}
			Next(State);
			return true;
		}
		case 13:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			FString Times;
			for (const TPair<FString, UHordeSubsystem::FArrival>& Each : State.Timed)
			{
				Times += FString::Printf(TEXT(" %s %.1f s / %.0f m (300 m ~%.0f s);"), *Each.Key, Each.Value.Seconds, Each.Value.SpawnDistanceCm / 100.f,
					Each.Value.Seconds * 30000.f / FMath::Max(Each.Value.SpawnDistanceCm, 1.f));
			}
			FString Distances;
			for (const float Each : State.Distances)
			{
				Distances += FString::Printf(TEXT(" %.0f m"), Each / 100.f);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: horde spawn distances:%s; arrivals (within %.0f m of the squad):%s"), *Distances,
				UHordeSubsystem::ArrivalRangeCm / 100.f, *Times);
			Check(State, State.Timed.Num() > 0, TEXT("arrival times measured"));
			Check(State, Fire->GetStreak() == 0 && Fire->GetFirefightCount() == 3,
				FString::Printf(TEXT("the hordes' own fights are no firefights (streak %d, %d on the level)"), Fire->GetStreak(), Fire->GetFirefightCount()));
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
		for (const FString& Arg : Args)
		{
			FString Value;
			if (Arg.Split(TEXT("="), nullptr, &Value) && Arg.StartsWith(TEXT("hordes=")))
			{
				State->HordesToRun = FMath::Clamp(FCString::Atoi(*Value), 1, 6);
			}
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.HordeFirefightsSmoke"),
		TEXT("Dev check of horde #12 on L_Gen_Depot_1: firefights in a row, the slip-past reset, the warning, then N hordes (hordes=N, default 2) far away, each from the level's next wave; PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
