// Dev-only console command for a headless check of suppressive fire (game designer decisions 2026-10-26, docs/design/SUPPRESSION.md)
// on L_MovementTest (its own empty ground, SmokeUtils::FindClearArena; the map is never edited):
//   Scripts/smoke.ps1 -Command CodexTactics.SuppressionSmoke
// A real-time wave fight (its own enemies parked far away), the squad unkillable:
//  A the commander gets the SUPPRESS order (key 7 armed, then the order on a marksman 14 m out - IssueSuppress, the click's target):
//    full-auto bursts, far more rounds than the normal cadence, the marksman becomes SUPPRESSED, stops firing and holds his spot head
//    down while the engineer sprints round his flank; ceased (key 7 again), the marksman's level decays and he fires again; an order
//    with only a little ammo left stops itself ("ammo low") and keeps the last rounds;
//  B the commander alone in the open, firing back at a marksman whose sustained fire suppresses him: his shots go blind (hit chance
//    x the suppressed multiplier), then PINNED - no round leaves at all;
//  C a squad turret fires at a (frozen) spitter: its MG rounds suppress it;
//  D turn-based combat: every level cleared, a running SUPPRESS order ends, a round on the grid counts nothing, the key refuses;
//  E Codex.Combat.Suppression 0: no tier, the order refused.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/PanicComponent.h"
#include "Characters/RageComponent.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/SuppressionSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/TurretActor.h"
#include "Kismet/GameplayStatics.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "TimerManager.h"
#include "UI/ActionBarWidget.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/GameMessageSubsystem.h"

namespace SuppressionSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.05f;
	constexpr float Timeout = 240.f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TWeakObjectPtr<AMarksmanEnemyCharacter> Marksman;
		TWeakObjectPtr<AEnemyCharacter> Spitter;
		TWeakObjectPtr<ATurretActor> Turret;
		FVector FlankPoint = FVector::ZeroVector;
		int32 Count = 0;
		int32 ShotsBefore = 0;
		int32 RoundsBefore = 0;
		int32 AmmoBefore = 0;
		int32 FullAutoBefore = 0;
		float OrderTime = 0.f;
		int32 ShotsFiredWhileHeld = 0;
		int32 LastMarksmanShots = 0;
		float FreeHitChance = 0.f;
		int32 BlindBefore = 0;
		int32 HoldsBefore = 0;
		int32 CommanderShots = 0;
		int32 CommanderShotsBefore = 0;
		bool bLeftPinned = false;
		FSuppressionStats StatsBefore;
		FDelegateHandle FiredHandle;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		if (AOperativeCharacter* Commander = State.Members.IsValidIndex(0) ? State.Members[0].Get() : nullptr)
		{
			Commander->OnWeaponFiredNative.Remove(State.FiredHandle);
		}
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Combat.Suppression")))
		{
			Variable->Set(1, ECVF_SetByConsole);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("SuppressionSmoke"));
		return false;
	}

	void GoTo(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	void Next(FState& State)
	{
		GoTo(State, State.Stage + 1);
	}

	FVector Feet(const AActor* Actor)
	{
		return Actor->GetActorLocation() - FVector(0.f, 0.f, Actor->GetSimpleCollisionHalfHeight());
	}

	FHitResult HitOn(AActor* Actor)
	{
		FHitResult Hit(Actor, nullptr, Actor->GetActorLocation(), FVector::UpVector);
		Hit.bBlockingHit = true;
		return Hit;
	}

	int32 CountMessages(UWorld* World, const FString& Part)
	{
		int32 Count = 0;
		if (const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
		{
			for (const FGameMessage& Message : Messages->GetHistory())
			{
				Count += Message.Text.ToString().Contains(Part) ? 1 : 0;
			}
		}
		return Count;
	}

	int32 Ammo(const AOperativeCharacter* Member)
	{
		return Member->CurrentClip + FMath::Max(0, Member->ReserveAmmo);
	}

	FString BarText(ACodexTacticsPlayerController* PC)
	{
		const ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
		UActionBarWidget* Bar = Hud ? Hud->GetActionBar() : nullptr;
		if (!Bar)
		{
			return TEXT("(no action bar)");
		}
		Bar->Refresh();
		return Bar->GetSuppressButtonText().ToString();
	}

	void Freeze(AEnemyCharacter* Enemy)
	{
		if (Enemy)
		{
			Enemy->CustomTimeDilation = 0.f;
			Enemy->SetActorTickEnabled(false);
		}
	}

	/** A marksman that holds 6-25 m out, never kites or flanks, aims AimSeconds and pauses CooldownSeconds between shots. */
	void TuneMarksman(AMarksmanEnemyCharacter* Marksman, float AimSeconds, float CooldownSeconds)
	{
		FMarksmanConfig& Config = Marksman->MarksmanConfig;
		Config.PreferredMinRange = 600.f;
		Config.PreferredMaxRange = 2500.f;
		Config.RetreatDistance = 200.f;
		Config.RetreatCooldownSeconds = 999.f;
		Config.FlankAfterCampSeconds = 999.f;
		Config.AimDuration = AimSeconds;
		Config.ShotCooldown = CooldownSeconds;
		Config.ShotDamage = 5.f; // a 45 HP rifle hit knocks the operative down ("heavy hit"): no fire, no orders - not what is measured
	}

	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		USuppressionSubsystem* Suppression = World->GetSubsystem<USuppressionSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		if (!Squad || !Flow || !TurnBased || !Suppression || !PC || State.Time > Timeout)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d at %.1fs"), State.Stage, State.Time);
			Check(State, false, TEXT("finished in time (and every subsystem there)"));
			return Finish(State, false);
		}
		AOperativeCharacter* Commander = State.Members.IsValidIndex(0) ? State.Members[0].Get() : nullptr;
		AOperativeCharacter* Engineer = State.Members.IsValidIndex(1) ? State.Members[1].Get() : nullptr;
		AMarksmanEnemyCharacter* Marksman = State.Marksman.Get();
		if (State.Stage > 0 && (!Commander || !Engineer))
		{
			Check(State, false, TEXT("the squad is there"));
			return Finish(State, false);
		}
		if (State.Stage > 0)
		{
			// The wave's own enemies stay parked far away (only the smoke's marksman / spitter play).
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				if (*It != Marksman && *It != State.Spitter.Get() && FVector::Dist2D(It->GetActorLocation(), State.P) < 20000.f)
				{
					SmokeUtils::ParkEnemyFarAway(*It);
					It->SetActorTickEnabled(false);
				}
			}
		}

		switch (State.Stage)
		{
		case 0: // Set-up: the smoke's own ground, a real-time wave fight, the squad unkillable and passive.
		{
			TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Members.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
			if (Members.Num() < 3)
			{
				Check(State, false, TEXT("three operatives"));
				return Finish(State, false);
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy(); // the map may hold its own enemies: start from a known set
			}
			TArray<const AActor*> Ignore;
			for (AOperativeCharacter* Member : Members)
			{
				State.Members.Add(Member);
				Ignore.Add(Member);
			}
			Ignore.Add(PC->GetPawn());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet(Members[0]), Members[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-600.f, -900.f), FVector2D(2000.f, 900.f)), Ignore);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: arena %s at %s facing %s (%d tried)"), Arena.bFound ? TEXT("found") : TEXT("NOT found"),
				*Arena.Origin.ToCompactString(), *Arena.Forward.ToCompactString(), Arena.Tried);
			if (!Arena.bFound)
			{
				Check(State, false, TEXT("an empty, flat, navigable 26 x 18 m spot for the smoke's own set-up"));
				return Finish(State, false);
			}
			State.P = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			SmokeUtils::StartWaveFight(Flow);
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("a real-time wave fight"));
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			Squad->SetFollowersHolding(true);
			const FVector Spots[] = { State.P, State.P + State.R * 300.f, State.P - State.F * 3000.f };
			for (int32 Index = 0; Index < 3; ++Index)
			{
				AOperativeCharacter* Member = Members[Index];
				Member->StopOperative();
				Member->SetStance(EOperativeStance::Standing);
				Member->TeleportTo(Spots[Index] + FVector(0.f, 0.f, Member->GetSimpleCollisionHalfHeight() + 5.f), State.F.Rotation(), false, true);
				Member->SwitchToWeaponById(TEXT("m16"));
				if (Member->CurrentWeapon)
				{
					Member->CurrentClip = Member->CurrentWeapon->MaxClipSize;
				}
				Member->ReserveAmmo = 100;
				Member->HealthComponent->SetMaxHealth(100000.f);
				if (Member->PanicComponent)
				{
					Member->PanicComponent->bEnabled = false; // hits would stress him into a panic (no fire): not what is measured
				}
				if (Member->RageComponent)
				{
					Member->RageComponent->Config.RequiredCrits = 100000; // two marksman crits would send him raging (orders ignored)
				}
			}
			Squad->SetLeader(Members[0]);
			Check(State, Members[0]->CurrentWeapon && Members[0]->CurrentWeapon->HasFullAuto(),
				FString::Printf(TEXT("the M16 has full-auto (interval %.2f s, data: weapons_tuning.json)"), Members[0]->CurrentWeapon ? Members[0]->CurrentWeapon->FullAutoInterval : 0.f));
			Marksman = Cast<AMarksmanEnemyCharacter>(World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Marksman,
				State.P + State.F * 1400.f + FVector(0.f, 0.f, 100.f), (-State.F).Rotation()));
			Check(State, Marksman != nullptr, TEXT("a marksman 14 m out"));
			if (!Marksman)
			{
				return Finish(State, false);
			}
			State.Marksman = Marksman;
			// A shot every 3 s: alone he does not suppress the commander (each hit decays before the next).
			TuneMarksman(Marksman, 1.f, 2.f);
			Marksman->GetHealthComponent()->SetMaxHealth(100000.f); // the suppressing rounds also hit: he must live through the check
			State.FiredHandle = Members[0]->OnWeaponFiredNative.AddLambda([&State](AOperativeCharacter*, AActor*, bool) { ++State.CommanderShots; });
			GoTo(State, 10);
			return true;
		}
		case 10: // A: a free marksman fires; then key 7 arms the aim and the order goes on him.
		{
			if (!Marksman)
			{
				Check(State, false, TEXT("A: the marksman is there"));
				return Finish(State, false);
			}
			if (Marksman->GetShotsFired() < 1 && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, Marksman->GetShotsFired() >= 1 && Suppression->GetTier(Marksman) == ESuppressionTier::None,
				FString::Printf(TEXT("A: the free marksman fires (%d shot(s) in %.1f s)"), Marksman->GetShotsFired(), State.StageTime));
			Squad->SetLeader(Commander);
			Check(State, BarText(PC).Contains(TEXT("SUPPRESS")), FString::Printf(TEXT("A: the action bar shows SUPPRESS (\"%s\")"), *BarText(PC).Replace(TEXT("\n"), TEXT(" "))));
			PC->SuppressKey();
			Check(State, PC->IsSuppressAimArmed(), TEXT("A: key 7 arms the suppress aim"));
			Check(State, BarText(PC).Contains(TEXT("AIM")), TEXT("A: ... the button reads AIM..."));
			const int32 Lines = CountMessages(World, TEXT("Suppressive fire!"));
			State.AmmoBefore = Ammo(Commander);
			State.RoundsBefore = Commander->GetSuppressRoundsFired();
			State.FullAutoBefore = Commander->GetFullAutoRoundsFired();
			State.OrderTime = State.Time;
			PC->IssueSuppress(HitOn(Marksman)); // what the armed click on him runs
			Check(State, !PC->IsSuppressAimArmed(), TEXT("A: the click used the aim"));
			Check(State, Commander->HasSuppressOrder() && Commander->GetSuppressTarget() == Marksman, TEXT("A: the commander suppresses the marksman"));
			Check(State, !Engineer->HasSuppressOrder(), TEXT("A: ... only the selected operative"));
			Check(State, CountMessages(World, TEXT("Suppressive fire!")) == Lines + 1, TEXT("A: one 'Suppressive fire!' line"));
			Next(State);
			return true;
		}
		case 11: // A: the marksman becomes suppressed; full-auto ammo use; the engineer starts round his flank.
		{
			if (Suppression->GetTier(Marksman) == ESuppressionTier::None && State.StageTime < 8.f)
			{
				return true;
			}
			const float Elapsed = State.Time - State.OrderTime;
			const int32 Rounds = Commander->GetSuppressRoundsFired() - State.RoundsBefore;
			Check(State, Suppression->GetTier(Marksman) != ESuppressionTier::None,
				FString::Printf(TEXT("A: the marksman is SUPPRESSED %.1f s into the order (level %.2f)"), Elapsed, Suppression->GetLevel(Marksman)));
			Check(State, Commander->GetFullAutoRoundsFired() - State.FullAutoBefore == Rounds && Rounds > 0,
				FString::Printf(TEXT("A: every round on full-auto (%d)"), Rounds));
			const float NormalRate = 1.f / FMath::Max(0.1f, Commander->CurrentWeapon ? Commander->CurrentWeapon->FireRate : 0.65f);
			Check(State, Elapsed > 0.f && Rounds / Elapsed >= 2.f * NormalRate,
				FString::Printf(TEXT("A: lots of ammo - %.1f rounds/s vs %.1f of the normal fire"), Elapsed > 0.f ? Rounds / Elapsed : 0.f, NormalRate));
			Check(State, State.AmmoBefore - Ammo(Commander) >= Rounds, FString::Printf(TEXT("A: the ammo went (%d -> %d)"), State.AmmoBefore, Ammo(Commander)));
			State.LastMarksmanShots = Marksman->GetShotsFired();
			State.ShotsFiredWhileHeld = 0;
			State.FlankPoint = SmokeUtils::FreeSpot(World, Marksman->GetActorLocation() + State.R * 600.f, Engineer);
			Check(State, Engineer->OrderMoveTo(State.FlankPoint, true) == EOperativeOrderResult::Accepted, TEXT("A: the engineer sprints round the marksman's flank"));
			Next(State);
			return true;
		}
		case 12: // A: while the engineer flanks, the suppressed marksman never fires and holds his spot.
		{
			if (Marksman->GetShotsFired() > State.LastMarksmanShots && Suppression->GetTier(Marksman) != ESuppressionTier::None)
			{
				++State.ShotsFiredWhileHeld;
			}
			State.LastMarksmanShots = Marksman->GetShotsFired();
			if (FVector::Dist2D(Engineer->GetActorLocation(), State.FlankPoint) > 250.f && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, FVector::Dist2D(Engineer->GetActorLocation(), State.FlankPoint) <= 250.f,
				FString::Printf(TEXT("A: the engineer reached the flank (%.1f s)"), State.StageTime));
			Check(State, Suppression->GetTier(Marksman) != ESuppressionTier::None,
				FString::Printf(TEXT("A: ... while the marksman is still %s (level %.2f)"), SuppressionRules::TierName(Suppression->GetTier(Marksman)), Suppression->GetLevel(Marksman)));
			Check(State, State.ShotsFiredWhileHeld == 0 && Marksman->GetShotsWhileSuppressed() == 0, TEXT("A: the suppressed marksman did not fire back at all"));
			Check(State, Marksman->GetSuppressionHoldSeconds() >= 1.f && !Marksman->IsAimingAtTarget() && Marksman->GetVelocity().Size2D() < 30.f
				&& Marksman->GetStance() != EOperativeStance::Standing,
				FString::Printf(TEXT("A: he hugs his cover: held %.1f s, no aim, still, stance %d"), Marksman->GetSuppressionHoldSeconds(), static_cast<int32>(Marksman->GetStance())));
			Check(State, Marksman->IsHeldBySuppression(), TEXT("A: IsHeldBySuppression"));
			// Key 7 with the commander suppressing: cease fire.
			Check(State, BarText(PC).Contains(TEXT("STOP")), TEXT("A: the button reads STOP SUPP. while he suppresses"));
			PC->SuppressKey();
			Check(State, !Commander->HasSuppressOrder() && !PC->IsSuppressAimArmed(), TEXT("A: key 7 again ceases the suppressive fire"));
			State.ShotsBefore = Marksman->GetShotsFired();
			Next(State);
			return true;
		}
		case 13: // A: unsuppressed again, the marksman fires again.
		{
			if ((Suppression->GetTier(Marksman) != ESuppressionTier::None || Marksman->GetShotsFired() == State.ShotsBefore) && State.StageTime < 16.f)
			{
				return true;
			}
			Check(State, Suppression->GetTier(Marksman) == ESuppressionTier::None && Marksman->GetShotsFired() > State.ShotsBefore,
				FString::Printf(TEXT("A: the level decays and the marksman fires again (%.1f s)"), State.StageTime));
			// The order stops itself when ammo runs low: 16 rounds, no reserve, the last 10 are kept.
			Commander->CurrentClip = 16;
			Commander->ReserveAmmo = 0;
			Check(State, Commander->OrderSuppress(Marksman, Marksman->GetActorLocation()), TEXT("A: an order with 16 rounds left"));
			Next(State);
			return true;
		}
		case 14:
		{
			if (Commander->HasSuppressOrder() && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, !Commander->HasSuppressOrder() && Commander->GetLastSuppressStop() == ESuppressOrderStop::AmmoLow && Commander->CurrentClip == 10,
				FString::Printf(TEXT("A: it stops itself on low ammo, keeping %d rounds (stop: %s)"), Commander->CurrentClip, SuppressionRules::StopName(Commander->GetLastSuppressStop())));
			Check(State, !Commander->OrderSuppress(Marksman, Marksman->GetActorLocation()), TEXT("A: no new order with only the kept rounds"));
			// B: the commander alone in the open fires back at a marksman whose sustained fire suppresses him.
			Commander->CurrentClip = 30;
			Commander->ReserveAmmo = 200;
			Engineer->StopOperative();
			Engineer->TeleportTo(State.P - State.F * 3000.f + State.R * 300.f + FVector(0.f, 0.f, Engineer->GetSimpleCollisionHalfHeight() + 5.f),
				State.F.Rotation(), false, true);
			Commander->StopOperative();
			Commander->TeleportTo(State.P + FVector(0.f, 0.f, Commander->GetSimpleCollisionHalfHeight() + 5.f), State.F.Rotation(), false, true);
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			TuneMarksman(Marksman, 0.6f, 0.6f);
			Suppression->SetLevelForTesting(Commander, 0.f);
			Suppression->SetLevelForTesting(Marksman, 0.f);
			State.CommanderShotsBefore = State.CommanderShots;
			GoTo(State, 20);
			return true;
		}
		case 20: // B: free, his aimed shots have the weapon's chance.
		{
			if ((State.CommanderShots - State.CommanderShotsBefore < 2 || Suppression->GetTier(Commander) != ESuppressionTier::None) && State.StageTime < 10.f)
			{
				if (Suppression->GetTier(Commander) != ESuppressionTier::None)
				{
					Suppression->SetLevelForTesting(Commander, 0.f); // the baseline first
				}
				return true;
			}
			State.FreeHitChance = Commander->GetLastShotHitChance();
			Check(State, State.CommanderShots - State.CommanderShotsBefore >= 2 && State.FreeHitChance > 0.f,
				FString::Printf(TEXT("B: the free commander fires back aimed (hit chance %.2f)"), State.FreeHitChance));
			Next(State);
			return true;
		}
		case 21: // B: the marksman's fire suppresses him.
		{
			if (Suppression->GetTier(Commander) == ESuppressionTier::None && State.StageTime < 14.f)
			{
				return true;
			}
			Check(State, Suppression->GetTier(Commander) != ESuppressionTier::None,
				FString::Printf(TEXT("B: the marksman's sustained fire SUPPRESSES him (%.1f s, level %.2f)"), State.StageTime, Suppression->GetLevel(Commander)));
			TuneMarksman(Marksman, 0.6f, 1.6f); // slower: he stays in the Suppressed band for a few shots of his own
			State.BlindBefore = Commander->GetSuppressedBlindShots();
			Next(State);
			return true;
		}
		case 22: // B: suppressed, he fires blind with a large spread.
		{
			if (Suppression->GetTier(Commander) == ESuppressionTier::Pinned)
			{
				Suppression->SetLevelForTesting(Commander, 0.6f); // keep him in the Suppressed band for this part
			}
			if (Commander->GetSuppressedBlindShots() - State.BlindBefore < 2 && State.StageTime < 12.f)
			{
				return true;
			}
			const float Blind = Commander->GetLastShotHitChance();
			Check(State, Commander->GetSuppressedBlindShots() - State.BlindBefore >= 2,
				FString::Printf(TEXT("B: suppressed, he still fires - blind (%d shot(s))"), Commander->GetSuppressedBlindShots() - State.BlindBefore));
			Check(State, Blind < State.FreeHitChance * 0.5f, FString::Printf(TEXT("B: ... with a large spread: hit chance %.2f vs %.2f free"), Blind, State.FreeHitChance));
			TuneMarksman(Marksman, 0.15f, 0.15f); // very intense fire
			Next(State);
			return true;
		}
		case 23: // B: very intense fire pins him.
		{
			if (Suppression->GetTier(Commander) != ESuppressionTier::Pinned && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, Suppression->GetTier(Commander) == ESuppressionTier::Pinned,
				FString::Printf(TEXT("B: very intense fire PINS him (level %.2f)"), Suppression->GetLevel(Commander)));
			State.CommanderShotsBefore = State.CommanderShots;
			State.HoldsBefore = Commander->GetPinnedFireHolds();
			State.bLeftPinned = false;
			Next(State);
			return true;
		}
		case 24: // B: pinned for 2 s: not one round leaves.
		{
			State.bLeftPinned |= Suppression->GetTier(Commander) != ESuppressionTier::Pinned;
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, !State.bLeftPinned, TEXT("B: pinned throughout"));
			Check(State, State.CommanderShots == State.CommanderShotsBefore && Commander->GetShotsWhilePinned() == 0,
				FString::Printf(TEXT("B: pinned, he does not fire at all (%d shot(s))"), State.CommanderShots - State.CommanderShotsBefore));
			Check(State, Commander->GetPinnedFireHolds() > State.HoldsBefore, TEXT("B: ... his fire is held by the pin"));
			Check(State, Marksman->GetShotsWhileSuppressed() == 0, TEXT("B: (the marksman was never suppressed meanwhile)"));
			SmokeUtils::ParkEnemyFarAway(Marksman);
			Marksman->SetActorTickEnabled(false);
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			Suppression->SetLevelForTesting(Commander, 0.f);
			// C: a squad turret and a frozen spitter 7 m out.
			AEnemyCharacter* Spitter = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Spitter,
				State.P + State.F * 700.f + FVector(0.f, 0.f, 100.f), (-State.F).Rotation());
			Check(State, Spitter != nullptr, TEXT("C: a spitter 7 m out"));
			if (!Spitter)
			{
				return Finish(State, false);
			}
			State.Spitter = Spitter;
			Spitter->GetHealthComponent()->SetMaxHealth(100000.f);
			Freeze(Spitter);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			State.Turret = World->SpawnActor<ATurretActor>(State.P + State.R * 250.f + FVector(0.f, 0.f, 50.f), State.F.Rotation(), Params);
			Check(State, State.Turret.IsValid() && State.Turret->IsPowered(), TEXT("C: a powered squad turret"));
			State.StatsBefore = Suppression->GetStats();
			GoTo(State, 30);
			return true;
		}
		case 30: // C: the turret's MG rounds suppress the spitter.
		{
			AEnemyCharacter* Spitter = State.Spitter.Get();
			if (!Spitter)
			{
				Check(State, false, TEXT("C: the spitter is there"));
				return Finish(State, false);
			}
			if (Suppression->GetTier(Spitter) == ESuppressionTier::None && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, Suppression->GetTier(Spitter) != ESuppressionTier::None && Suppression->GetStats().Rounds > State.StatsBefore.Rounds,
				FString::Printf(TEXT("C: the turret's MG fire suppresses the spitter (%.1f s, %s, level %.2f)"), State.StageTime,
					SuppressionRules::TierName(Suppression->GetTier(Spitter)), Suppression->GetLevel(Spitter)));
			Check(State, Spitter->IsHeldBySuppression(), TEXT("C: ... a suppressed spitter is held (no fire)"));
			if (ATurretActor* Turret = State.Turret.Get())
			{
				Turret->Destroy();
			}
			// D: turn-based combat with the spitter pinned and a SUPPRESS order running.
			Suppression->SetLevelForTesting(Spitter, 0.95f);
			Commander->CurrentClip = 30;
			Commander->ReserveAmmo = 100;
			Check(State, Commander->OrderSuppress(Spitter, Spitter->GetActorLocation()) && Commander->HasSuppressOrder(), TEXT("D: a SUPPRESS order runs before the grid fight"));
			State.StatsBefore = Suppression->GetStats();
			const EGameFlowResult Result = Flow->RequestEnterTurnBased(true);
			Check(State, Result == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("D: turn-based combat"));
			if (!TurnBased->IsActive())
			{
				return Finish(State, false);
			}
			Next(State);
			return true;
		}
		case 31: // D: no suppression in turn-based combat.
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			AEnemyCharacter* Spitter = State.Spitter.Get();
			Check(State, Spitter && Suppression->GetTier(Spitter) == ESuppressionTier::None && USuppressionSubsystem::GetLevelOf(Spitter) == 0.f,
				TEXT("D: entering turn-based cleared the pinned spitter"));
			Check(State, Suppression->GetStats().Clears > State.StatsBefore.Clears, TEXT("D: ... every level dropped"));
			Check(State, !Commander->HasSuppressOrder(), TEXT("D: the SUPPRESS order ended"));
			FSuppressionRound Round;
			Round.Shooter = Commander;
			Round.Start = Commander->GetActorLocation();
			Round.End = Spitter ? Spitter->GetActorLocation() : State.P;
			Round.Target = Spitter;
			Round.bHitTarget = true;
			Round.Weapon = ESuppressionWeapon::MachineGun;
			const int32 Ignored = Suppression->GetStats().IgnoredRounds;
			Suppression->ReportRound(Round);
			Check(State, Suppression->GetStats().IgnoredRounds == Ignored + 1 && Suppression->GetTier(Spitter) == ESuppressionTier::None,
				TEXT("D: a round on the grid accumulates nothing"));
			const int32 Refusals = CountMessages(World, TEXT("Suppressive fire is real time only"));
			PC->SuppressKey();
			Check(State, !PC->IsSuppressAimArmed() && CountMessages(World, TEXT("Suppressive fire is real time only")) == Refusals + 1,
				TEXT("D: key 7 is refused in turn-based combat"));
			Flow->ExitTurnBasedToRealTime();
			Next(State);
			return true;
		}
		case 32: // D: back in real time the levels stay cleared; E: the A/B switch off.
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			AEnemyCharacter* Spitter = State.Spitter.Get();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime && Spitter && Suppression->GetTier(Spitter) == ESuppressionTier::None,
				TEXT("D: back in real time: nothing carried over"));
			IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Combat.Suppression"));
			Check(State, Variable != nullptr, TEXT("E: console variable Codex.Combat.Suppression"));
			if (Variable && Spitter)
			{
				Variable->Set(0, ECVF_SetByConsole);
				Suppression->SetLevelForTesting(Spitter, 0.95f);
				Check(State, USuppressionSubsystem::GetTierOf(Spitter) == ESuppressionTier::None && !Spitter->IsHeldBySuppression(),
					TEXT("E: Codex.Combat.Suppression 0 - no suppression"));
				Check(State, !Commander->OrderSuppress(Spitter, Spitter->GetActorLocation()), TEXT("E: ... the SUPPRESS order refused"));
				Variable->Set(1, ECVF_SetByConsole);
				Suppression->ClearAll();
			}
			return Finish(State, true);
		}
		default:
			Check(State, false, FString::Printf(TEXT("unknown stage %d"), State.Stage));
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			TSharedRef<FState> State = MakeShared<FState>();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
			{
				UWorld* W = WeakWorld.Get();
				return W && Step(W, *State);
			}), StepSeconds);
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.SuppressionSmoke"),
		TEXT("Dev check (2026-10-26): suppressive fire - the SUPPRESS order (key 7) holds a marksman's fire while a flanker moves, full-auto ammo use, an enemy's fire makes an operative fire blind then pins him, a turret suppresses, none in turn-based, the A/B switch; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
