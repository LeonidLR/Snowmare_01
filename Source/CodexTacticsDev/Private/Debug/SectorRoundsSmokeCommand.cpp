#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/PanicComponent.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/DeathCinematicSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/MissionSubsystem.h"
#include "Data/CombatTypes.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Training/TrainingSectorSubsystem.h"

namespace SectorRoundsSmoke
{
	enum class EStage : uint8
	{
		WaitRound,
		WaitQuiet,
		WaitRefill,
		KillPmc,
		WaitRound2,
		KillPlayers,
		WaitRound3
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		SmokeUtils::FPreloadGate Preload;
		EStage Stage = EStage::WaitRound;
		double Mark = 0.0;
		double StageAt = 0.0;
		int32 Grenades = -1;
		FSectorPair FirstPair;
		bool bSawQuiet = false;
		int32 ArgSector = 0;
		TOptional<int32> ArgSeed;
		bool bSectorApplied = false;
		bool bPanicMeasure = false;
		bool bMeasureInitialized = false;
		double MeasureStartAt = 0.0;
		bool bAccuracyMeasure = false;
		int32 TargetRounds = 20;
		int32 AccLastRound = 0;
		double AccRoundStartAt = 0.0;
		TSet<TWeakObjectPtr<AOperativeCharacter>> HookedShooters;
		int32 AccPlayerShots[4]{};
		int32 AccPlayerHits[4]{};
		int32 AccPmcShots[4]{};
		int32 AccPmcHits[4]{};
	};

	bool Finish(bool bPass, const TCHAR* What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bPass ? TEXT("ok  ") : TEXT("FAIL"), What);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("SectorRoundsSmoke"));
		return false;
	}

	void Cease(UWorld& World)
	{
		for (TActorIterator<AOperativeCharacter> It(&World); It; ++It)
		{
			It->bTacticalCeaseFire = true;
		}
	}

	bool MissionStillOn(UWorld& World, FString& Why)
	{
		const UMissionSubsystem* Mission = World.GetSubsystem<UMissionSubsystem>();
		const UDeathCinematicSubsystem* Death = World.GetSubsystem<UDeathCinematicSubsystem>();
		if (!Mission)
		{
			Why = TEXT("no mission subsystem");
			return false;
		}
		if (Mission->IsMissionFailed())
		{
			Why = TEXT("mission failed");
			return false;
		}
		if (Death && (Death->IsActive() || Death->IsDefeatPending()))
		{
			Why = TEXT("death cinematic started");
			return false;
		}
		return true;
	}

	void ZeroAmmo(AOperativeCharacter& Op)
	{
		Op.TakePrimaryWeapon();
		Op.ReserveAmmo = 0;
		Op.CurrentClip = 0;
		for (TPair<FString, FWeaponAmmoState>& Entry : Op.AmmoInventory)
		{
			if (Entry.Key != TEXT("grenade"))
			{
				Entry.Value.Reserve = 0;
				Entry.Value.Clip = 0;
			}
		}
	}

	void KillSide(UWorld& World, bool bPlayers)
	{
		for (TActorIterator<AOperativeCharacter> It(&World); It; ++It)
		{
			AOperativeCharacter& Op = **It;
			if (Op.IsActorBeingDestroyed() || Op.IsPlayerTeam() != bPlayers || !Op.HealthComponent || !Op.HealthComponent->IsAlive())
			{
				continue;
			}
			FDamageSpec Spec;
			Spec.Amount = 250000.f;
			Spec.ArmorPenetration = 1.f;
			Spec.AttackerSource = TEXT("SectorRoundsSmoke");
			Op.HealthComponent->TakeDamage(Spec);
		}
	}

	int32 GrenadeCount(UWorld& World)
	{
		int32 Count = 0;
		for (TActorIterator<AOperativeCharacter> It(&World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed() && It->IsPlayerTeam())
			{
				Count += It->GrenadesCount;
			}
		}
		return Count;
	}

	void CountAmmo(UWorld& World, int32& Empty, int32& Filled)
	{
		Empty = 0;
		Filled = 0;
		for (TActorIterator<AOperativeCharacter> It(&World); It; ++It)
		{
			const AOperativeCharacter& Op = **It;
			if (Op.IsActorBeingDestroyed() || !Op.UsesAmmo() || !Op.CurrentWeapon || Op.IsLauncherEquipped() || Op.IsKilledInAction())
			{
				continue;
			}
			if (Op.ReserveAmmo <= 0)
			{
				++Empty;
			}
			else
			{
				++Filled;
			}
		}
	}

	double Closest(UWorld& World, const FVector& Pos, bool bPlayers)
	{
		double Best = TNumericLimits<double>::Max();
		for (TActorIterator<AOperativeCharacter> It(&World); It; ++It)
		{
			if (It->IsActorBeingDestroyed() || It->IsPlayerTeam() != bPlayers || It->IsKilledInAction())
			{
				continue;
			}
			Best = FMath::Min(Best, FVector::Dist2D(It->GetActorLocation(), Pos));
		}
		return Best;
	}

	bool Step(TSharedRef<FState> State)
	{
		UWorld* World = State->World.Get();
		if (!World)
		{
			return false;
		}
		if (!State->bPanicMeasure && !State->bAccuracyMeasure)
		{
			Cease(*World);
		}
		switch (State->Preload.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting: return true;
		case SmokeUtils::EPreloadGate::Failed: return Finish(false, TEXT("gameplay preload failed"));
		default: break;
		}
		UTrainingSectorSubsystem* Training = UTrainingSectorSubsystem::Get(World);
		if (Training && !State->bSectorApplied && (State->ArgSector > 0 || State->ArgSeed.IsSet()))
		{
			State->bSectorApplied = true;
			const int32 TargetSector = State->ArgSector > 0 ? State->ArgSector : Training->GetSectorId();
			if (!Training->SelectSector(TargetSector, State->ArgSeed))
			{
				return Finish(false, TEXT("failed to select requested sector/seed"));
			}
			State->StageAt = World->GetTimeSeconds();
		}
		if (!Training || !Training->HasActiveSector() || !Training->GetSector())
		{
			return Finish(false, TEXT("X6 sector 5 did not activate"));
		}
		if (State->bPanicMeasure)
		{
			const double Now = World->GetTimeSeconds();
			const FSectorRoundView View = Training->GetRoundView();
			if (View.Phase != ESectorRoundPhase::Fighting)
			{
				return Now - State->StageAt < 60.0 || Finish(false, TEXT("panic measure never entered Fighting"));
			}
			if (!State->bMeasureInitialized)
			{
				State->bMeasureInitialized = true;
				State->MeasureStartAt = Now;
				if (USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
				{
					Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
					Squad->SetFollowersHolding(true);
				}
				if (UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World))
				{
					Pmc->SetTeamCommanded(0, true);
					if (const FPmcEncounterSpec* Enc = Training->GetEncounter())
					{
						Pmc->SetTeamAssault(0, &Enc->Post.Pos);
					}
				}
				for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
				{
					It->bTacticalCeaseFire = false;
					if (It->IsPlayerTeam())
					{
						ZeroAmmo(**It);
						if (It->HealthComponent)
						{
							It->HealthComponent->RestoreHealth(25.f, 100.f);
						}
					}
				}
			}
			if (Now - State->MeasureStartAt >= 90.0 || View.Number > 1)
			{
				return Finish(true, TEXT("panic measure 90s completed"));
			}
			return true;
		}
		if (State->bAccuracyMeasure)
		{
			const double Now = World->GetTimeSeconds();
			const FSectorRoundView View = Training->GetRoundView();
			if (View.Number > State->TargetRounds || (View.Number == State->TargetRounds && View.Phase != ESectorRoundPhase::Fighting && State->AccLastRound == State->TargetRounds))
			{
				auto Sum4 = [](const int32 (&A)[4]) { return A[0] + A[1] + A[2] + A[3]; };
				const int32 PS = Sum4(State->AccPlayerShots), PH = Sum4(State->AccPlayerHits);
				const int32 MS = Sum4(State->AccPmcShots), MH = Sum4(State->AccPmcHits);
				const float PPct = PS > 0 ? 100.f * PH / PS : 0.f;
				const float MPct = MS > 0 ? 100.f * MH / MS : 0.f;
				UE_LOG(LogCodexTactics, Display,
					TEXT("[AccuracyMeasure] Rounds=%d PlayerShots=%d+%d+%d+%d PlayerHits=%d+%d+%d+%d (%.1f%%) | PmcShots=%d+%d+%d+%d PmcHits=%d+%d+%d+%d (%.1f%%)"),
					State->TargetRounds,
					State->AccPlayerShots[0], State->AccPlayerShots[1], State->AccPlayerShots[2], State->AccPlayerShots[3],
					State->AccPlayerHits[0], State->AccPlayerHits[1], State->AccPlayerHits[2], State->AccPlayerHits[3], PPct,
					State->AccPmcShots[0], State->AccPmcShots[1], State->AccPmcShots[2], State->AccPmcShots[3],
					State->AccPmcHits[0], State->AccPmcHits[1], State->AccPmcHits[2], State->AccPmcHits[3], MPct);
				return Finish(true, TEXT("accuracy measure completed"));
			}
			for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
			{
				AOperativeCharacter* Op = *It;
				if (!Op || Op->IsActorBeingDestroyed() || State->HookedShooters.Contains(Op))
				{
					continue;
				}
				State->HookedShooters.Add(Op);
				TWeakPtr<FState> WeakState = State;
				Op->OnWeaponFiredNative.AddLambda([WeakState](AOperativeCharacter* Shooter, AActor* Target, bool bHit)
				{
					const TSharedPtr<FState> S = WeakState.Pin();
					if (!S || !Shooter || !Target) { return; }
					const float DistM = static_cast<float>(FVector::Dist2D(Shooter->GetActorLocation(), Target->GetActorLocation())) / 100.f;
					const int32 B = DistM < 10.f ? 0 : (DistM < 25.f ? 1 : (DistM < 50.f ? 2 : 3));
					if (Shooter->IsPlayerTeam())
					{
						++S->AccPlayerShots[B];
						if (bHit) { ++S->AccPlayerHits[B]; }
					}
					else
					{
						++S->AccPmcShots[B];
						if (bHit) { ++S->AccPmcHits[B]; }
					}
				});
			}
			if (View.Phase == ESectorRoundPhase::Fighting && View.Number != State->AccLastRound)
			{
				State->AccLastRound = View.Number;
				State->AccRoundStartAt = Now;
				if (USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
				{
					Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
					Squad->SetFollowersHolding(false);
				}
				const FVector StartPos = Training->GetStart().GetLocation();
				UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
				TSet<int32> PmcTeams;
				for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
				{
					It->bTacticalCeaseFire = false;
					if (!It->IsPlayerTeam())
					{
						PmcTeams.Add(It->TeamId);
					}
				}
				if (Pmc)
				{
					for (int32 PmcTeam : PmcTeams)
					{
						Pmc->SetTeamCommanded(PmcTeam, true);
						Pmc->SetTeamAssault(PmcTeam, &StartPos);
					}
				}
			}
			else if (View.Phase == ESectorRoundPhase::Fighting && Now - State->AccRoundStartAt >= 90.0)
			{
				KillSide(*World, false);
			}
			return Now < State->StageAt + 1200.0 || Finish(false, TEXT("accuracy measure timed out"));
		}
		const double Now = World->GetTimeSeconds();
		const FSectorRoundView View = Training->GetRoundView();
		const FSectorFightConfig& Config = Training->GetFightConfig();
		const FTrainingSector& Sector = *Training->GetSector();
		FString MissionWhy;
		if (View.Phase != ESectorRoundPhase::Idle && !MissionStillOn(*World, MissionWhy))
		{
			return Finish(false, *MissionWhy);
		}
		switch (State->Stage)
		{
		case EStage::WaitRound:
			if (View.Number > 1)
			{
				return Finish(false, TEXT("the fight advanced before the first pair was recorded"));
			}
			if (View.Phase == ESectorRoundPhase::Fighting && View.Number == 1)
			{
				if (!FMath::IsNearlyEqual(Config.ResupplyDelayS, 20.f) || !FMath::IsNearlyEqual(Config.RoundRestartDelayS, 8.f)
					|| Config.AmmoMode != ESectorAmmoMode::Resupply)
				{
					return Finish(false, TEXT("fight config is not resupply / 20 s / 8 s"));
				}
				State->FirstPair = View.Pair;
				for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
				{
					ZeroAmmo(**It);
				}
				State->Grenades = GrenadeCount(*World);
				State->Mark = Now;
				State->StageAt = Now;
				State->Stage = EStage::WaitQuiet;
				UE_LOG(LogCodexTactics, Display, TEXT("[SectorRounds] round 1 pair start %d post %d grenades %d"), View.Pair.StartIndex, View.Pair.PostIndex, State->Grenades);
			}
			return Now < 60.0 || Finish(false, TEXT("round 1 never started"));
		case EStage::WaitQuiet:
		{
			int32 Empty = 0;
			int32 Filled = 0;
			CountAmmo(*World, Empty, Filled);
			if (Filled > 0)
			{
				return Finish(false, TEXT("reserve refilled before the quiet delay"));
			}
			if (GrenadeCount(*World) != State->Grenades)
			{
				return Finish(false, TEXT("grenades changed during resupply"));
			}
			if (Now >= State->Mark + Config.ResupplyDelayS - 1.0 && Empty > 0)
			{
				State->bSawQuiet = true;
				State->Stage = EStage::WaitRefill;
			}
			return Now < State->Mark + Config.ResupplyDelayS + 5.0 || Finish(false, TEXT("nobody stayed out of ammo until the delay"));
		}
		case EStage::WaitRefill:
		{
			int32 Empty = 0;
			int32 Filled = 0;
			CountAmmo(*World, Empty, Filled);
			if (GrenadeCount(*World) != State->Grenades)
			{
				return Finish(false, TEXT("grenades changed when the reserve came back"));
			}
			if (Now >= State->Mark + Config.ResupplyDelayS + 0.5 && Filled > 0 && State->bSawQuiet)
			{
				State->Stage = EStage::KillPmc;
				State->StageAt = Now;
				UE_LOG(LogCodexTactics, Display, TEXT("[SectorRounds] reserve back after %.1f s (%d fighters)"), Now - State->Mark, Filled);
				return true;
			}
			if (Now >= State->Mark + Config.ResupplyDelayS + 0.5 && Filled > 0 && Now < State->Mark + Config.ResupplyDelayS - 1.0)
			{
				return Finish(false, TEXT("reserve refilled inside the quiet window"));
			}
			return Now < State->Mark + Config.ResupplyDelayS + 8.0 || Finish(false, TEXT("empty reserve was not refilled after the delay"));
		}
		case EStage::KillPmc:
			if (View.Phase == ESectorRoundPhase::Fighting)
			{
				KillSide(*World, false);
			}
			if (Training->GetRoundBanner().Equals(TrainingSectorRules::RoundBanner(View.Number, ESectorRoundEnd::Victory)))
			{
				State->Stage = EStage::WaitRound2;
				State->StageAt = Now;
				return true;
			}
			return Now - State->StageAt < 8.0 || Finish(false, TEXT("a PMC wipe did not show the victory line"));
		case EStage::WaitRound2:
			if (View.Phase == ESectorRoundPhase::Fighting && View.Number == 2)
			{
				if (View.Pair == State->FirstPair || !View.Pair.IsValid() || !Sector.Starts.IsValidIndex(View.Pair.StartIndex) || !Sector.Posts.IsValidIndex(View.Pair.PostIndex))
				{
					return Finish(false, TEXT("round 2 reused the round 1 start and post"));
				}
				const double PlayerD = Closest(*World, Sector.Starts[View.Pair.StartIndex].Position, true);
				const double PmcD = Closest(*World, Sector.Posts[View.Pair.PostIndex].Pos, false);
				const double PairDistM = FVector::Dist2D(Sector.Starts[View.Pair.StartIndex].Position, Sector.Posts[View.Pair.PostIndex].Pos) / 100.0;
				UE_LOG(LogCodexTactics, Display, TEXT("[SectorRounds] round 2 pair start %d post %d dist %.1f m player %.0f cm pmc %.0f cm"), View.Pair.StartIndex,
					View.Pair.PostIndex, PairDistM, PlayerD, PmcD);
				if (PairDistM < Config.MinStartPostDistanceM)
				{
					return Finish(false, TEXT("round 2 start/post distance is below min_start_post_distance_m"));
				}
				if (PlayerD > 900.0 || PmcD > 800.0)
				{
					return Finish(false, TEXT("the new squads are not on the new start and post"));
				}
				State->Stage = EStage::KillPlayers;
				State->StageAt = Now;
			}
			return Now - State->StageAt < 40.0 || Finish(false, TEXT("round 2 did not start on a new pair"));
		case EStage::KillPlayers:
			if (View.Phase == ESectorRoundPhase::Fighting)
			{
				KillSide(*World, true);
			}
			if (Training->GetRoundBanner().Equals(TrainingSectorRules::RoundBanner(View.Number, ESectorRoundEnd::Defeat)))
			{
				State->Stage = EStage::WaitRound3;
				State->StageAt = Now;
				return true;
			}
			return Now - State->StageAt < 8.0 || Finish(false, TEXT("a player wipe did not show the defeat line"));
		case EStage::WaitRound3:
			if (View.Phase == ESectorRoundPhase::Fighting && View.Number >= 3)
			{
				return Finish(true, TEXT("wipe, resupply and the next round on a different pair"));
			}
			return Now - State->StageAt < 40.0 || Finish(false, TEXT("round 3 did not start after the player wipe"));
		default:
			return Finish(false, TEXT("unknown stage"));
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		const TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		State->StageAt = World->GetTimeSeconds();
		for (const FString& Arg : Args)
		{
			int32 Val = 0;
			if (FParse::Value(*Arg, TEXT("Sector="), Val))
			{
				State->ArgSector = Val;
			}
			else if (FParse::Value(*Arg, TEXT("Seed="), Val))
			{
				State->ArgSeed = Val;
			}
			else if (FParse::Value(*Arg, TEXT("PanicMeasure="), Val))
			{
				State->bPanicMeasure = Val != 0;
			}
			else if (FParse::Value(*Arg, TEXT("AccuracyMeasure="), Val))
			{
				State->bAccuracyMeasure = Val != 0;
			}
			else if (FParse::Value(*Arg, TEXT("Rounds="), Val) && Val > 0)
			{
				State->TargetRounds = Val;
			}
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float) { return Step(State); }), 0.f);
	}

	FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.SectorRoundsSmoke"),
		TEXT("X6 sector 5: empty reserves refill after the quiet delay, a wipe starts the next round on a different pair, and the mission stays up."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif
