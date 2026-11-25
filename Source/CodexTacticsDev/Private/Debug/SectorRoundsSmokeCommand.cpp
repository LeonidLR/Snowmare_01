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
		if (!State->bPanicMeasure)
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
				UE_LOG(LogCodexTactics, Display, TEXT("[SectorRounds] round 2 pair start %d post %d player %.0f cm pmc %.0f cm"), View.Pair.StartIndex,
					View.Pair.PostIndex, PlayerD, PmcD);
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
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float) { return Step(State); }), 0.f);
	}

	FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.SectorRoundsSmoke"),
		TEXT("X6 sector 5: empty reserves refill after the quiet delay, a wipe starts the next round on a different pair, and the mission stays up."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif
