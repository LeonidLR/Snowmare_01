// Dev-only console command: turret power phase 1 (docs/design/TURRET_POWER.md, designer decisions 2026-11-17) on L_MovementTest:
//   Scripts/smoke_parallel.ps1 -Commands TurretPowerSmoke
// Everything is spawned at runtime in an empty arena (SmokeUtils::FindClearArena): a dry generator G with two level turrets T1 / T2
// on its cable, a running HOSTILE network H (PMC team 1) with turret T3 behind the squad, frozen targets (a monster by T1, a PMC
// fighter by T2, a monster by T3) and two frozen guards for the start noise (20 m and 40 m from G). The squad's fire posture is
// Passive, the followers hold. Checks:
// 1. an unpowered turret is silent (G dry: T1 / T2 dark, no shot), while the hostile T3 is live;
// 2. the leader refuels G (his jerrycan: +10 L) and starts it: owner = the squad, the start noise sends the 20 m guard investigating
//    and not the 40 m one; after the 2 s spin-up T1 shoots the monster and T2 the PMC fighter, never the squad; the fuel burns;
// 3. stopping G powers T1 / T2 down (no more shots);
// 4. the hostile T3 targets the squad and not its own PMC team; observing it 2 s reveals its owner; seizing H (stop + restart) darkens
//    T3 from second 1, then flips it to the squad's side (now the PMC, not the squad) - G's network is untouched.
// Logs "Smoke RESULT: PASS/FAIL", then exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcSquadSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/FirePostureRules.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/BoxComponent.h"
#include "Debug/SmokeUtils.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/TurretActor.h"
#include "Power/PowerGeneratorActor.h"
#include "Power/PowerRules.h"
#include "TimerManager.h"

namespace TurretPowerSmoke
{
	constexpr float StepSeconds = 0.1f;
	constexpr float Timeout = 120.f;

	enum class EPhase : uint8 { Setup, Dark, Refuel, Start, Fire, Stop, StopQuiet, Observe, Seize, Done };

	struct FState
	{
		EPhase Phase = EPhase::Setup;
		float Time = 0.f;
		float PhaseTime = 0.f;
		FVector O = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TWeakObjectPtr<APowerGeneratorActor> G;
		TWeakObjectPtr<APowerGeneratorActor> H;
		TWeakObjectPtr<ATurretActor> T1;
		TWeakObjectPtr<ATurretActor> T2;
		TWeakObjectPtr<ATurretActor> T3;
		TWeakObjectPtr<AActor> M1;
		TWeakObjectPtr<AActor> M2;
		TWeakObjectPtr<AOperativeCharacter> P1;
		TWeakObjectPtr<AEnemyCharacter> Near;
		TWeakObjectPtr<AEnemyCharacter> Far;
		int32 Passed = 0;
		int32 Failed = 0;
		bool bT1HitMonster = false;
		bool bT2HitPmc = false;
		bool bHitSquad = false;
		bool bT3DarkDuringSeize = false;
		bool bSeizeSampled = false;
		bool bRefuelOrdered = false;
		int32 ShotsAtStop = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		(bOk ? State.Passed : State.Failed)++;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
	}

	bool Finish(FState& State)
	{
		const bool bPass = State.Failed == 0 && State.Passed > 0;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke checks: %d passed, %d failed"), State.Passed, State.Failed);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		State.Phase = EPhase::Done;
		FPlatformMisc::RequestExit(false, TEXT("TurretPowerSmoke"));
		return false;
	}

	void Enter(FState& State, EPhase Phase)
	{
		State.Phase = Phase;
		State.PhaseTime = 0.f;
	}

	FVector Ground(UWorld* World, const FVector& Point)
	{
		return SmokeUtils::FreeSpot(World, Point);
	}

	/** Puts Operative at Point (feet), stopped, facing Target. */
	void Place(AOperativeCharacter* Operative, const FVector& Point, const FVector& Target)
	{
		Operative->StopOperative();
		Operative->SetActorLocation(Point + FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight() + 5.f), false, nullptr, ETeleportType::TeleportPhysics);
		const FVector Facing = (Target - Point).GetSafeNormal2D();
		if (!Facing.IsNearlyZero())
		{
			Operative->SetActorRotation(Facing.Rotation());
		}
	}

	APowerGeneratorActor* SpawnGenerator(UWorld* World, const FVector& Feet, EGeneratorStart Start, float Fuel)
	{
		const FTransform Transform(FRotator::ZeroRotator, Feet + FVector(0.f, 0.f, 60.f));
		APowerGeneratorActor* Generator = World->SpawnActorDeferred<APowerGeneratorActor>(APowerGeneratorActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Generator)
		{
			return nullptr;
		}
		Generator->bAutoLinkTurrets = false;
		Generator->InitLevelGenerator(Start, Fuel);
		Generator->FinishSpawning(Transform);
		return Generator;
	}

	ATurretActor* SpawnTurret(UWorld* World, const FVector& Feet, APowerGeneratorActor* Generator)
	{
		const FTransform Transform(FRotator::ZeroRotator, Feet + FVector(0.f, 0.f, 40.f));
		ATurretActor* Turret = World->SpawnActorDeferred<ATurretActor>(ATurretActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Turret)
		{
			return nullptr;
		}
		Turret->InitLevelTurret(ETurretFaction::Neutral, false);
		Turret->FinishSpawning(Transform);
		if (Generator)
		{
			Generator->LinkTurret(Turret);
		}
		return Turret;
	}

	/** A frozen target (CustomTimeDilation 0: it neither moves nor attacks, it still takes damage). */
	AEnemyCharacter* SpawnFrozenEnemy(UWorld* World, EEnemyArchetype Type, const FVector& Feet, bool bGuard)
	{
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		AEnemyCharacter* Enemy = Waves ? Waves->SpawnEnemy(Type, Ground(World, Feet) + FVector(0.f, 0.f, 10.f)) : nullptr;
		if (Enemy && bGuard)
		{
			if (ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>())
			{
				Encounter->MarkLevelEnemy(Enemy); // a guard on its post: a patrol-duty listener of the stealth lure
			}
		}
		if (Enemy)
		{
			Enemy->CustomTimeDilation = 0.f;
		}
		return Enemy;
	}

	bool IsSquad(UWorld* World, const AActor* Actor)
	{
		const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		return Actor && Squad && Squad->GetMembers().Contains(Actor);
	}

	FVector ApproachOf(const APowerGeneratorActor* Generator, const FVector& Side)
	{
		return Generator->GetActorLocation() - FVector(0.f, 0.f, Generator->Box->GetScaledBoxExtent().Z) + Side * 130.f;
	}

	bool Setup(UWorld* World, FState& State)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		if (!Leader || !Pmc)
		{
			Check(State, false, TEXT("a squad leader and the PMC subsystem"));
			return Finish(State);
		}
		TArray<const AActor*> Ignore;
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			Ignore.Add(Member);
		}
		const FVector Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
		const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Leader->GetActorForwardVector(),
			FBox2D(FVector2D(-2500.f, -1100.f), FVector2D(2200.f, 1100.f)), Ignore);
		State.O = Arena.bFound ? Arena.Origin : Feet;
		State.F = Arena.bFound ? Arena.Forward : Leader->GetActorForwardVector().GetSafeNormal2D();
		State.R = Arena.bFound ? Arena.Right : FVector::CrossProduct(FVector::UpVector, State.F);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke arena %s at %s (tried %d)"), Arena.bFound ? TEXT("found") : TEXT("NOT found - the leader's spot"),
			*State.O.ToCompactString(), Arena.Tried);
		const FVector O = State.O;
		const FVector F = State.F;
		const FVector R = State.R;

		Squad->SetSquadPosture(ESquadFirePosture::Passive);
		Squad->SetFollowersHolding(true);
		int32 Index = 0;
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Place(Member, Ground(World, O - F * (Index == 0 ? 0.f : 150.f) + R * (Index == 0 ? 0.f : (Index % 2 ? 150.f : -150.f))), O + F * 1000.f);
			++Index;
		}

		APowerGeneratorActor* G = SpawnGenerator(World, Ground(World, O + F * 600.f), EGeneratorStart::Dry, 0.f);
		APowerGeneratorActor* H = SpawnGenerator(World, Ground(World, O - F * 1000.f), EGeneratorStart::RunningHostile, 15.f);
		State.G = G;
		State.H = H;
		State.T1 = SpawnTurret(World, Ground(World, O + F * 600.f + R * 800.f), G);
		State.T2 = SpawnTurret(World, Ground(World, O + F * 600.f - R * 800.f), G);
		State.T3 = SpawnTurret(World, Ground(World, O - F * 2300.f), H);
		State.M1 = SpawnFrozenEnemy(World, EEnemyArchetype::Frostbitten, O + F * 1000.f + R * 800.f, false);
		State.M2 = SpawnFrozenEnemy(World, EEnemyArchetype::Frostbitten, O - F * 2300.f + R * 500.f, false);
		State.Near = SpawnFrozenEnemy(World, EEnemyArchetype::Frostbitten, O + F * 2000.f + R * 1000.f, true);
		State.Far = SpawnFrozenEnemy(World, EEnemyArchetype::Frostbitten, O + F * 4600.f, true);
		AOperativeCharacter* P1 = Pmc->SpawnFighter(1, Ground(World, O + F * 1000.f - R * 800.f) + FVector(0.f, 0.f, 100.f), (-F).Rotation(), 1, 0);
		if (P1)
		{
			P1->CustomTimeDilation = 0.f;
		}
		State.P1 = P1;
		const bool bAll = G && H && State.T1.IsValid() && State.T2.IsValid() && State.T3.IsValid() && State.M1.IsValid() && State.M2.IsValid()
			&& State.Near.IsValid() && State.Far.IsValid() && P1;
		Check(State, bAll, TEXT("set-up: 2 generators, 3 turrets, 2 frozen monsters, a frozen PMC fighter, 2 frozen guards"));
		if (!bAll)
		{
			return Finish(State);
		}
		Check(State, G->GetLinkedTurrets().Num() == 2 && H->GetLinkedTurrets().Num() == 1 && State.T1->GetPowerSource() == G,
			TEXT("G feeds T1 + T2, H feeds T3"));
		Enter(State, EPhase::Dark);
		return true;
	}

	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		State.PhaseTime += StepSeconds;
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("finished before the timeout (stuck in phase %d)"), static_cast<int32>(State.Phase)));
			return Finish(State);
		}
		if (State.Phase == EPhase::Setup)
		{
			return Setup(World, State);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		APowerGeneratorActor* G = State.G.Get();
		APowerGeneratorActor* H = State.H.Get();
		ATurretActor* T1 = State.T1.Get();
		ATurretActor* T2 = State.T2.Get();
		ATurretActor* T3 = State.T3.Get();
		if (!Leader || !G || !H || !T1 || !T2 || !T3)
		{
			Check(State, false, TEXT("the leader, the generators and the turrets stay in the world"));
			return Finish(State);
		}
		// Every step: a friendly / dark turret of G never shoots a squad member.
		for (const ATurretActor* Turret : { T1, T2 })
		{
			State.bHitSquad |= IsSquad(World, Turret->GetLastTarget());
		}
		State.bT1HitMonster |= T1->GetLastTarget() && T1->GetLastTarget() == State.M1.Get();
		State.bT2HitPmc |= T2->GetLastTarget() && T2->GetLastTarget() == State.P1.Get();

		switch (State.Phase)
		{
		case EPhase::Dark:
			if (State.PhaseTime >= 3.f)
			{
				Check(State, G->GetState() == EGeneratorState::Dry, TEXT("G starts dry"));
				Check(State, !T1->IsPowered() && !T2->IsPowered() && T1->GetShotsFired() == 0 && T2->GetShotsFired() == 0,
					FString::Printf(TEXT("an unpowered turret is silent (T1 shots %d, T2 shots %d, a monster 4 m from T1)"), T1->GetShotsFired(), T2->GetShotsFired()));
				Check(State, H->IsRunning() && H->GetOwnerTeam() == 1 && T3->IsLive(), TEXT("the hostile network H runs for PMC team 1, T3 is live"));
				Check(State, T3->MayTarget(Leader) && !T3->MayTarget(State.P1.Get()), TEXT("hostile T3 would fire at the squad, not at its own PMC team"));
				Check(State, T3->GetShotsFired() > 0, FString::Printf(TEXT("hostile T3 shoots the monster in its reach (%d shots)"), T3->GetShotsFired()));
				Leader->DieselCansCount = 1;
				Place(Leader, Ground(World, ApproachOf(G, -State.F)), G->GetActorLocation());
				Enter(State, EPhase::Refuel);
			}
			return true;

		case EPhase::Refuel:
			if (State.PhaseTime >= 0.3f && !State.bRefuelOrdered)
			{
				State.bRefuelOrdered = true;
				const FActionMenuRequest Menu = G->BuildActionMenu(Leader);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke G menu \"%s\" [%s%s]"), *Menu.Menu.Title.ToString(), *Menu.Menu.ConfirmText.ToString(),
					Menu.Menu.bConfirmDisabled ? TEXT(", disabled") : TEXT(""));
				G->PerformAction(Leader);
				Check(State, G->GetActionInProgress() == EGeneratorAction::Refuel, TEXT("the leader starts refuelling G (4 s)"));
			}
			else if (State.PhaseTime > 0.5f && G->GetActionInProgress() == EGeneratorAction::None)
			{
				Check(State, FMath::IsNearlyEqual(G->GetFuelL(), 10.f) && Leader->DieselCansCount == 0 && G->GetState() == EGeneratorState::Fuelled,
					FString::Printf(TEXT("refuelled: %.1f L, %d can(s) left, %s"), G->GetFuelL(), Leader->DieselCansCount, *PowerRules::StateName(G->GetState())));
				Check(State, !T1->IsPowered(), TEXT("fuel alone powers nothing"));
				G->PerformAction(Leader);
				Check(State, G->GetActionInProgress() == EGeneratorAction::Start, TEXT("the leader starts G (3 s)"));
				Enter(State, EPhase::Start);
			}
			return true;

		case EPhase::Start:
			if (State.PhaseTime > 0.5f && G->GetActionInProgress() == EGeneratorAction::None)
			{
				Check(State, G->IsRunning() && G->GetOwnerTeam() == PowerRules::PlayerTeam && G->GetStartCount() == 1,
					TEXT("G runs, owned by the squad (whoever started it)"));
				Check(State, T1->IsPowered() && T2->IsPowered() && T1->GetFaction() == ETurretFaction::Friendly && T1->IsOwnerKnown(),
					TEXT("its turrets are powered and friendly (known: we started it)"));
				Check(State, !T1->IsLive(), TEXT("a 2 s spin-up before the first shot"));
				const AEnemyCharacter* Near = State.Near.Get();
				const AEnemyCharacter* Far = State.Far.Get();
				Check(State, G->GetLastNoiseListeners() >= 1 && Near && Near->IsSearching(),
					FString::Printf(TEXT("the start is loud: the guard 20 m away investigates (%d listener(s))"), G->GetLastNoiseListeners()));
				Check(State, Far && !Far->IsSearching(), TEXT("the guard 40 m away does not hear it (30 m)"));
				Check(State, T1->MayTarget(State.P1.Get()) && T1->MayTarget(State.M1.Get()) && !T1->MayTarget(Leader),
					TEXT("friendly T1 targets the PMC and monsters, never the squad"));
				Enter(State, EPhase::Fire);
			}
			return true;

		case EPhase::Fire:
			if (State.PhaseTime >= 5.f)
			{
				Check(State, T1->GetShotsFired() > 0 && State.bT1HitMonster, FString::Printf(TEXT("friendly T1 shot the monster (%d shots)"), T1->GetShotsFired()));
				Check(State, T2->GetShotsFired() > 0 && State.bT2HitPmc, FString::Printf(TEXT("friendly T2 shot the PMC fighter (%d shots)"), T2->GetShotsFired()));
				Check(State, !State.bHitSquad, TEXT("no shot of G's turrets ever went at the squad"));
				Check(State, G->GetFuelL() < 10.f, FString::Printf(TEXT("the fuel burns (%.3f L left, %.0f s at this load)"), G->GetFuelL(), G->GetSecondsLeft()));
				G->PerformAction(Leader);
				Check(State, G->GetActionInProgress() == EGeneratorAction::Stop, TEXT("the leader stops G (1 s)"));
				Enter(State, EPhase::Stop);
			}
			return true;

		case EPhase::Stop:
			if (State.PhaseTime > 0.3f && G->GetActionInProgress() == EGeneratorAction::None)
			{
				Check(State, G->GetState() == EGeneratorState::Fuelled && G->GetOwnerTeam() == PowerRules::NoOwner, TEXT("G stopped: fuelled, no owner"));
				Check(State, !T1->IsPowered() && !T2->IsPowered(), TEXT("stopping the generator powers its turrets down"));
				State.ShotsAtStop = T1->GetShotsFired() + T2->GetShotsFired();
				Enter(State, EPhase::StopQuiet);
			}
			return true;

		case EPhase::StopQuiet:
			if (State.PhaseTime >= 1.5f)
			{
				Check(State, T1->GetShotsFired() + T2->GetShotsFired() == State.ShotsAtStop, TEXT("no shot after the stop"));
				Place(Leader, Ground(World, ApproachOf(H, State.F)), H->GetActorLocation());
				Enter(State, EPhase::Observe);
			}
			return true;

		case EPhase::Observe:
			if (State.PhaseTime >= 3.f)
			{
				const float ToT3 = FVector::Dist(Leader->GetActorLocation(), T3->GetActorLocation());
				Check(State, T3->IsOwnerKnown(), FString::Printf(TEXT("observing T3 for 2 s from %.1f m reveals its owner"), ToT3 / 100.f));
				Check(State, H->IsOwnerKnown(), TEXT("an observed turret reveals its whole network (the generator knows its owner too)"));
				H->PerformAction(Leader);
				Check(State, H->GetActionInProgress() == EGeneratorAction::Seize, TEXT("the leader seizes the hostile network (4 s)"));
				Enter(State, EPhase::Seize);
			}
			return true;

		case EPhase::Seize:
			if (State.PhaseTime >= 1.5f && !State.bSeizeSampled)
			{
				State.bSeizeSampled = true;
				State.bT3DarkDuringSeize = !T3->IsPowered() && !H->IsRunning();
			}
			if (State.PhaseTime > 2.f && H->GetActionInProgress() == EGeneratorAction::None)
			{
				Check(State, State.bT3DarkDuringSeize, TEXT("the network is dark from second 1 of the seize"));
				Check(State, H->IsRunning() && H->GetOwnerTeam() == PowerRules::PlayerTeam && H->GetStartCount() == 1,
					TEXT("H runs again, now the squad's"));
				Check(State, T3->IsPowered() && T3->GetFaction() == ETurretFaction::Friendly && T3->MayTarget(State.P1.Get()) && !T3->MayTarget(Leader),
					TEXT("capturing a hostile network flips its turret: it now fires at the PMC, not the squad"));
				Check(State, G->GetState() == EGeneratorState::Fuelled && !T1->IsPowered(), TEXT("the other network is untouched"));

				// Review 2026-11-17 fixes, in the same world:
				// #1 a save round trip of a generator restores the state exactly (fuel, switch, owner, knowledge) and re-pushes its turret.
				const TSharedRef<FJsonObject> Saved = H->CaptureSave();
				const float SavedFuel = H->GetFuelL();
				H->StopEngine(nullptr, false);
				H->RestoreSave(*Saved);
				Check(State, H->IsRunning() && H->GetOwnerTeam() == PowerRules::PlayerTeam && FMath::IsNearlyEqual(H->GetFuelL(), SavedFuel)
					&& H->IsOwnerKnown() && T3->IsPowered() && T3->GetFaction() == ETurretFaction::Friendly && T3->IsOwnerKnown(),
					TEXT("save round trip: the seized network comes back running, ours, known, its turret live"));
				// #2 a network turret's saved power flag never overrides its generator.
				T3->RestoreSaved(/*bInPowered=*/ false, /*bInBroken=*/ false);
				Check(State, T3->IsPowered() && T3->GetOwnerTeam() == PowerRules::PlayerTeam, TEXT("a restored network turret takes its generator's state"));
				// #5 a PMC takes the network over: its side is unknown again.
				H->StopEngine(nullptr, false);
				H->StartBy(1, nullptr);
				Check(State, !H->IsOwnerKnown() && !T3->IsOwnerKnown() && T3->GetFaction() == ETurretFaction::Hostile,
					TEXT("a network re-seized by a PMC team hides its new side again"));
				// #6 a turret taken off its cable gets its level side and deploy flags back (not the squad's turret).
				G->UnlinkTurret(T2);
				Check(State, !T2->IsNetworkLinked() && T2->GetFaction() == ETurretFaction::Neutral && !T2->IsPowered(),
					TEXT("an unlinked turret is dark with its level side (neutral), not the squad's"));
				// #3 the legacy repair entry on a power generator is the network repair: intact and off, no owner.
				H->RepairGenerator();
				Check(State, H->GetState() == EGeneratorState::Fuelled && H->GetOwnerTeam() == PowerRules::NoOwner && !T3->IsPowered(),
					TEXT("RepairGenerator on a power generator = RepairNow (off, no owner, its turret dark)"));
				return Finish(State);
			}
			return true;

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
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TSharedRef<FState> State = MakeShared<FState>();
		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle StartHandle;
		World->GetTimerManager().SetTimer(StartHandle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
		{
			UWorld* W = WeakWorld.Get();
			if (!W)
			{
				return;
			}
			W->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
			{
				UWorld* Inner = WeakWorld.Get();
				if (Inner && !Step(Inner, *State))
				{
					Inner->GetTimerManager().ClearTimer(*Handle);
				}
			}), StepSeconds, true);
		}), 3.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TurretPowerSmoke"),
		TEXT("Dev check (TURRET_POWER.md phase 1): a dry generator's turrets are silent; refuel + start = friendly turrets that shoot PMC / monsters, not the squad, ")
		TEXT("and a loud start; stop = dark; seizing a hostile network flips its turret. Logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
