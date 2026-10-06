#include "Characters/SquadAutonomySubsystem.h"

#include "Algo/AnyOf.h"
#include "Bot/PlaytestBotRules.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Data/SquadROE.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/InteractableActor.h"
#include "Quests/QuestChain.h"
#include "Tactics/CoverDecisionRules.h"
#include "Tactics/CoverTraceRules.h"
#include "NavigationSystem.h"

namespace SquadAutonomy
{
	/** Barricades searched for cover (the bot's 20 m) — the stand point must still lie inside the leash. */
	constexpr float CoverSearchRadiusCm = 2000.f;
	/** Enemies farther than this do not make the squad seek cover. */
	constexpr float ThreatRangeCm = 2500.f;
	/** A flanker closer than this makes the operative change the cover side. */
	constexpr float FlankRangeCm = 1200.f;
	/** Leash slack before the operative walks back (cm). */
	constexpr float LeashSlackCm = 50.f;
	/** An autonomous walk gives up after this long (s). */
	constexpr float TaskTimeout = 8.f;

	bool IsAliveOperative(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->HealthComponent && Operative->HealthComponent->IsAlive();
	}

	float HealthFraction(const AOperativeCharacter& Operative)
	{
		const UHealthComponent* Health = Operative.HealthComponent;
		return Health && Health->GetMaxHealth() > 0.f ? Health->GetCurrentHealth() / Health->GetMaxHealth() : 0.f;
	}

	int32 Rounds(const AOperativeCharacter& Operative, const FString& WeaponId)
	{
		const FWeaponAmmoState Ammo = Operative.GetAmmoState(WeaponId);
		return Ammo.Clip + Ammo.Reserve;
	}

	bool HasWeapon(const AOperativeCharacter& Operative, const FString& WeaponId)
	{
		return Operative.AvailableWeapons.ContainsByPredicate([&WeaponId](const UWeaponDataAsset* Weapon) { return Weapon && Weapon->WeaponId == WeaponId; });
	}
}

void USquadAutonomySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UGameFlowSubsystem* Flow = InWorld.GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->OnGameFlowChanged.AddDynamic(this, &USquadAutonomySubsystem::HandleGameFlowChanged);
	}
}

TStatId USquadAutonomySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USquadAutonomySubsystem, STATGROUP_Tickables);
}

void USquadAutonomySubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	// Space: the tactical pause hands the squad back to the player at once (Sprint 07-A).
	if (bWasActive && (Phase != ECodexGamePhase::WaveCombat || CombatMode != ECodexCombatMode::RealTime))
	{
		Freeze();
	}
}

void USquadAutonomySubsystem::Tick(float DeltaTime)
{
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const bool bActive = Flow && Flow->IsSquadAutonomyActive();
	if (!bActive)
	{
		if (bWasActive)
		{
			Freeze();
		}
		return;
	}
	bWasActive = true;
	DecisionTimer -= DeltaTime;
	SummaryTimer += DeltaTime;
	if (DecisionTimer <= 0.f)
	{
		DecisionTimer = DecisionInterval;
		RunDecisions();
	}
	if (SummaryTimer >= 10.f)
	{
		SummaryTimer = 0.f;
		UE_LOG(LogCodexTactics, Display,
			TEXT("Commander Mode: cover %d, stance %d (prone for sniper %d), reload %d, sidearm %d/%d, flank %d, leash %d, aid %d/%d (unsafe %d), targets %d"),
			Stats.CoverMoves, Stats.StanceChanges, Stats.ProneForSniper, Stats.Reloads, Stats.SidearmSwitches, Stats.PrimarySwitches, Stats.FlankShifts,
			Stats.LeashReturns, Stats.AidGiven, Stats.AidMoves, Stats.AidRefusedUnsafe, Stats.TargetPicks);
	}
}

void USquadAutonomySubsystem::Freeze()
{
	bWasActive = false;
	++Stats.Freezes;
	for (TPair<TWeakObjectPtr<AOperativeCharacter>, FOperativeState>& Pair : States)
	{
		AOperativeCharacter* Operative = Pair.Key.Get();
		if (!Operative)
		{
			continue;
		}
		// Only the autonomy's own walks stop: a player order (its anchor differs from the task's) keeps going.
		if (Pair.Value.Task != ETask::None && Operative->TacticalAnchor.Location.Equals(Pair.Value.TaskAnchor, 1.f) && Operative->IsMoving())
		{
			Operative->StopOperative();
		}
		Operative->SetAutonomyTarget(nullptr);
		Pair.Value.Task = ETask::None;
		Pair.Value.Patient.Reset();
	}
}

void USquadAutonomySubsystem::RunDecisions()
{
	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	if (!Squad)
	{
		return;
	}
	TArray<FEnemyView> Enemies;
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		const UHealthComponent* Health = It->GetHealthComponent();
		if (It->IsDying() || (Health && !Health->IsAlive()))
		{
			continue;
		}
		FEnemyView View;
		View.Enemy = *It;
		View.Location = It->GetActorLocation();
		if (const AMarksmanEnemyCharacter* Marksman = Cast<AMarksmanEnemyCharacter>(*It))
		{
			View.AimTarget = Marksman->IsAimingAtTarget() ? Cast<AOperativeCharacter>(Marksman->GetCurrentTarget()) : nullptr;
		}
		Enemies.Add(View);
	}
	TArray<AOperativeCharacter*> Members;
	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (SquadAutonomy::IsAliveOperative(Member))
		{
			Members.Add(Member);
		}
	}
	for (AOperativeCharacter* Member : Members)
	{
		Decide(*Member, States.FindOrAdd(Member), Enemies, Members, DecisionInterval);
	}
}

void USquadAutonomySubsystem::Decide(AOperativeCharacter& Operative, FOperativeState& State, const TArray<FEnemyView>& Enemies,
	const TArray<AOperativeCharacter*>& Squad, float Elapsed)
{
	using namespace SquadAutonomyRules;
	const FSquadROE& ROE = SquadROE::Get();
	State.MoveCooldown = FMath::Max(0.f, State.MoveCooldown - Elapsed);
	if (Operative.IsPanicking() || Operative.IsRaging() || Operative.bCarrying || Operative.IsVaulting())
	{
		return; // broken nerves or busy hands: the autonomy does not command him
	}
	if (!Operative.TacticalAnchor.bIsActive)
	{
		// No order yet this fight: he guards where he stands.
		Operative.TacticalAnchor = MakeAnchor(ROE, Operative.GetActorLocation(), Operative.GetActorLocation(), Operative.GetActorRotation());
	}
	// Sprint 10: a defense line keeps him on the 5 m defense leash around the defended object.
	FDefenseDirective& Defense = Operative.TacticalAnchor.Defense;
	Operative.TacticalAnchor.Radius = Defense.IsActive() ? DefenseLeashRadius(ROE, Defense, false) : LeashRadius(ROE, false);
	bool bIntruderPresent = false;
	if (Defense.IsActive())
	{
		if (const AActor* Object = Defense.DefendedActor.Get())
		{
			Defense.DefendedLocation = Object->GetActorLocation();
		}
		for (const FEnemyView& View : Enemies)
		{
			bIntruderPresent |= FVector::Dist2D(View.Location, Defense.DefendedLocation) <= Defense.InterceptRadiusCm
				|| (Defense.DefendedActor.IsValid() && View.Enemy->GetCurrentTarget() == Defense.DefendedActor.Get());
		}
	}

	const FVector Position = Operative.GetActorLocation();
	const FEnemyView* Nearest = nullptr;
	float NearestCm = TNumericLimits<float>::Max();
	const FEnemyView* SniperOnMe = nullptr;
	for (const FEnemyView& View : Enemies)
	{
		const float Distance = FVector::Dist2D(View.Location, Position);
		if (Distance < NearestCm)
		{
			NearestCm = Distance;
			Nearest = &View;
		}
		if (View.AimTarget == &Operative)
		{
			SniperOnMe = &View;
		}
	}

	// Weapons and the target first: they act even during a walk.
	UpdateWeapons(Operative, State, NearestCm);
	const AOperativeCharacter* Leader = GetWorld()->GetSubsystem<USquadSubsystem>()->GetLeader();
	ChooseTarget(Operative, Enemies, Leader);

	// Sprint 12: at a wall he fights from the wall (stance / fire mode by CoverDecisionRules), no cover hunting.
	if (Operative.bInCover || Operative.HasPendingCover())
	{
		if (Operative.bInCover)
		{
			DecideInCover(Operative, Enemies, Nearest, NearestCm, SniperOnMe);
		}
		return;
	}

	// «Ни шагу назад»: an enemy at point-blank range — the defender stays where he is and fires (no walk at all).
	if (HoldsGround(ROE, Defense, NearestCm))
	{
		if (State.Task != ETask::None && Operative.TacticalAnchor.Location.Equals(State.TaskAnchor, 1.f))
		{
			Operative.StopOperative();
			State.Task = ETask::None;
			State.Patient.Reset();
		}
		++Stats.DefenseHolds;
		return;
	}

	if (UpdateTask(Operative, State, Enemies, Elapsed))
	{
		return; // an autonomous walk is under way
	}
	if (Operative.IsMoving())
	{
		return; // the player's own order: never overridden
	}

	// Field aid (7-D) before anything else that moves him.
	if (TryAid(Operative, State, Enemies, Squad, bIntruderPresent))
	{
		return;
	}

	// Leash (7-B): back to the anchor.
	if (!IsInsideLeash(Operative.TacticalAnchor, Position, Operative.TacticalAnchor.Radius + SquadAutonomy::LeashSlackCm))
	{
		FVector Goal;
		if (ProjectToNav(Operative.TacticalAnchor.Location, Goal))
		{
			StartTask(Operative, State, ETask::Return, Goal);
			++Stats.LeashReturns;
			return;
		}
	}

	const bool bBehindBarricade = Operative.IsBehindBarricade();
	bool bCoverReachable = bBehindBarricade;
	if (Nearest && NearestCm <= SquadAutonomy::ThreatRangeCm && State.MoveCooldown <= 0.f)
	{
		// Sniper reaction / cover against the threat / cover side against a flanker (7-C).
		const FVector Threat = SniperOnMe ? SniperOnMe->Location : Nearest->Location;
		bool bWantCover = !bBehindBarricade;
		if (bBehindBarricade && NearestCm <= SquadAutonomy::FlankRangeCm)
		{
			FVector BarricadeDirection = Operative.GetActorForwardVector();
			float BestBarricade = TNumericLimits<float>::Max();
			for (TActorIterator<ABarricadeActor> It(GetWorld()); It; ++It)
			{
				const float Distance = FVector::Dist2D(It->GetActorLocation(), Position);
				if (Distance < BestBarricade)
				{
					BestBarricade = Distance;
					BarricadeDirection = It->GetActorLocation() - Position;
				}
			}
			if (IsFlankThreat(BarricadeDirection, Position, Nearest->Location, ROE.FlankDefenseAngleDeg))
			{
				bWantCover = true;
				++Stats.FlankShifts;
			}
		}
		if (SniperOnMe && ROE.SniperReaction == ESniperReaction::DropProne)
		{
			bWantCover = false;
		}
		FVector Stand;
		if (bWantCover && FindCoverInLeash(Operative, Threat, Stand))
		{
			bCoverReachable = true;
			if (FVector::Dist2D(Stand, Position) > 100.f)
			{
				StartTask(Operative, State, ETask::Cover, Stand);
				++Stats.CoverMoves;
				return;
			}
		}
		else if (bWantCover && !bBehindBarricade && Nearest && NearestCm <= SquadAutonomy::FlankRangeCm && Operative.CanHitEnemy(Nearest->Enemy))
		{
			// No cover to change to: pivot onto the closest threat.
			Operative.SetAutonomyTarget(Nearest->Enemy);
		}
	}

	// Stance (7-C) while standing still with enemies around.
	if (Nearest)
	{
		const EOperativeStance Desired = DesiredStance(ROE, bBehindBarricade, SniperOnMe != nullptr, bCoverReachable);
		if (Desired != Operative.GetStance())
		{
			Operative.SetStance(Desired);
			++Stats.StanceChanges;
			Stats.ProneForSniper += SniperOnMe && Desired == EOperativeStance::Prone ? 1 : 0;
		}
	}
}

bool USquadAutonomySubsystem::UpdateTask(AOperativeCharacter& Operative, FOperativeState& State, const TArray<FEnemyView>& Enemies, float Elapsed)
{
	if (State.Task == ETask::None)
	{
		return false;
	}
	if (!Operative.TacticalAnchor.Location.Equals(State.TaskAnchor, 1.f))
	{
		State.Task = ETask::None; // the player gave a new order
		State.Patient.Reset();
		return false;
	}
	State.TaskTime += Elapsed;
	if (State.Task == ETask::Aid)
	{
		AOperativeCharacter* Patient = State.Patient.Get();
		if (!SquadAutonomy::IsAliveOperative(Patient))
		{
			State.Task = ETask::None;
			return false;
		}
		if (FVector::Dist2D(Patient->GetActorLocation(), Operative.GetActorLocation()) <= AOperativeCharacter::AidReachCm)
		{
			Operative.StopOperative();
			if (Operative.HealAlly(*Patient))
			{
				++Stats.AidGiven;
			}
			State.Patient.Reset();
			// Back to the anchor after the aid (7-D).
			FVector Goal;
			if (ProjectToNav(Operative.TacticalAnchor.Location, Goal) && FVector::Dist2D(Goal, Operative.GetActorLocation()) > 100.f)
			{
				StartTask(Operative, State, ETask::Return, Goal);
				return true;
			}
			State.Task = ETask::None;
			return false;
		}
		if (!Operative.IsMoving())
		{
			// The patient moved: walk on to him.
			Operative.AutonomousMoveTo(Patient->GetActorLocation());
		}
	}
	else if (!Operative.IsMoving())
	{
		State.Task = ETask::None;
		return false;
	}
	if (State.TaskTime > SquadAutonomy::TaskTimeout)
	{
		Operative.StopOperative();
		State.Task = ETask::None;
		State.Patient.Reset();
		State.MoveCooldown = 2.f;
		return false;
	}
	return true;
}

void USquadAutonomySubsystem::StartTask(AOperativeCharacter& Operative, FOperativeState& State, ETask Task, const FVector& Goal)
{
	State.Task = Task;
	State.TaskGoal = Goal;
	State.TaskTime = 0.f;
	State.TaskAnchor = Operative.TacticalAnchor.Location;
	State.MoveCooldown = 3.f;
	if (Operative.AutonomousMoveTo(Goal) == EOperativeOrderResult::Refused)
	{
		State.Task = ETask::None;
	}
}

void USquadAutonomySubsystem::UpdateWeapons(AOperativeCharacter& Operative, FOperativeState& State, float NearestEnemyCm)
{
	using namespace SquadAutonomyRules;
	const FSquadROE& ROE = SquadROE::Get();
	const UWeaponDataAsset* Weapon = Operative.CurrentWeapon;
	if (!Weapon)
	{
		return;
	}
	const bool bOnSidearm = State.bOnAutoSidearm && Weapon->WeaponId != State.PrimaryWeaponId;
	State.bOnAutoSidearm = bOnSidearm;
	if (!bOnSidearm && Operative.UsesAmmo() && Weapon->WeaponId != TEXT("grenade"))
	{
		State.PrimaryWeaponId = Weapon->WeaponId;
	}
	if (State.PrimaryWeaponId.IsEmpty())
	{
		return;
	}
	// Sprint 10: a defender body-blocks a point-blank enemy with the knife.
	if (!bOnSidearm && ShouldDrawMelee(ROE, Operative.TacticalAnchor.Defense, NearestEnemyCm) && State.PrimaryWeaponId != TEXT("knife")
		&& SquadAutonomy::HasWeapon(Operative, TEXT("knife")) && Operative.SwitchToWeaponById(TEXT("knife")))
	{
		State.bOnAutoSidearm = true;
		++Stats.MeleeDraws;
		UE_LOG(LogCodexTactics, Display, TEXT("Commander Mode: %s holds the line with the knife (enemy at %.1f m)"), *Operative.DisplayName.ToString(),
			NearestEnemyCm / 100.f);
		return;
	}
	if (!bOnSidearm)
	{
		// Point-blank with an empty / reloading primary: pistol, else shotgun (7-C).
		for (const TCHAR* Sidearm : { TEXT("pistol"), TEXT("shotgun") })
		{
			if (Sidearm != State.PrimaryWeaponId && SquadAutonomy::HasWeapon(Operative, Sidearm)
				&& ShouldSwitchToSidearm(ROE, NearestEnemyCm, Operative.CurrentClip, Operative.bIsReloading, SquadAutonomy::Rounds(Operative, Sidearm))
				&& Operative.SwitchToWeaponById(Sidearm))
			{
				State.bOnAutoSidearm = true;
				++Stats.SidearmSwitches;
				UE_LOG(LogCodexTactics, Display, TEXT("Commander Mode: %s draws the %s (enemy at %.1f m)"), *Operative.DisplayName.ToString(), Sidearm,
					NearestEnemyCm / 100.f);
				return;
			}
		}
	}
	else if (ShouldSwitchBackToPrimary(ROE, NearestEnemyCm, SquadAutonomy::Rounds(Operative, State.PrimaryWeaponId))
		&& Operative.SwitchToWeaponById(State.PrimaryWeaponId))
	{
		State.bOnAutoSidearm = false;
		++Stats.PrimarySwitches;
		Weapon = Operative.CurrentWeapon;
	}
	if (Weapon && Operative.UsesAmmo()
		&& ShouldReload(ROE, Operative.CurrentClip, Weapon->MaxClipSize, Operative.ReserveAmmo, Operative.bIsReloading, Operative.IsBehindBarricade(),
			NearestEnemyCm))
	{
		Operative.StartReload();
		Stats.Reloads += Operative.bIsReloading ? 1 : 0;
	}
}

void USquadAutonomySubsystem::ChooseTarget(AOperativeCharacter& Operative, const TArray<FEnemyView>& Enemies, const AOperativeCharacter* Leader)
{
	using namespace SquadAutonomyRules;
	const FSquadROE& ROE = SquadROE::Get();
	const float Reach = (Operative.CurrentWeapon ? Operative.CurrentWeapon->AttackRangeCm : 1400.f) * 1.3f;
	const AActor* LeaderTarget = Leader && Leader != &Operative ? Leader->GetCurrentCombatTarget() : nullptr;
	const AActor* Current = Operative.GetAutonomyTarget();
	TArray<FAutonomyTargetCandidate> Candidates;
	TArray<AEnemyCharacter*> Actors;
	for (const FEnemyView& View : Enemies)
	{
		const float Distance = FVector::Dist2D(View.Location, Operative.GetActorLocation());
		if (Distance > Reach)
		{
			continue; // out of any range: no line trace
		}
		FAutonomyTargetCandidate Candidate;
		Candidate.Archetype = View.Enemy->GetArchetype();
		Candidate.DistanceCm = Distance;
		const UHealthComponent* Health = View.Enemy->GetHealthComponent();
		Candidate.HealthFraction = Health && Health->GetMaxHealth() > 0.f ? Health->GetCurrentHealth() / Health->GetMaxHealth() : 1.f;
		Candidate.bLeaderTarget = View.Enemy == LeaderTarget;
		Candidate.bCurrent = View.Enemy == Current;
		Candidate.bAimingAtSquad = View.AimTarget != nullptr;
		Candidate.bCanHit = Operative.CanHitEnemy(View.Enemy);
		const FDefenseDirective& Defense = Operative.TacticalAnchor.Defense;
		if (Defense.IsActive())
		{
			Candidate.DistanceToDefendedCm = FVector::Dist2D(View.Location, Defense.DefendedLocation);
			Candidate.bAttackingDefended = Defense.DefendedActor.IsValid() && View.Enemy->GetCurrentTarget() == Defense.DefendedActor.Get();
		}
		Candidates.Add(Candidate);
		Actors.Add(View.Enemy);
	}
	const int32 Best = SquadAutonomyRules::PickTarget(ROE, Operative.TacticalAnchor.Defense, Candidates);
	AActor* Chosen = Best == INDEX_NONE ? nullptr : Actors[Best];
	if (Chosen != Current)
	{
		Operative.SetAutonomyTarget(Chosen);
		Stats.TargetPicks += Chosen ? 1 : 0;
	}
}

bool USquadAutonomySubsystem::TryAid(AOperativeCharacter& Operative, FOperativeState& State, const TArray<FEnemyView>& Enemies,
	const TArray<AOperativeCharacter*>& Squad, bool bIntruderPresent)
{
	using namespace SquadAutonomyRules;
	const FSquadROE& ROE = SquadROE::Get();
	if (!CanGiveAid(ROE, SquadAutonomy::HealthFraction(Operative), Operative.MedkitsCount))
	{
		return false;
	}
	const FDefenseDirective& Defense = Operative.TacticalAnchor.Defense;
	const float AidLeash = Defense.IsActive() ? DefenseLeashRadius(ROE, Defense, true) : LeashRadius(ROE, true);
	AOperativeCharacter* Patient = nullptr;
	float PatientHealth = 1.f;
	for (AOperativeCharacter* Mate : Squad)
	{
		const float Fraction = SquadAutonomy::HealthFraction(*Mate);
		if (Mate == &Operative || !NeedsAid(ROE, Fraction, false) || Fraction >= PatientHealth
			|| !IsInsideLeash(Operative.TacticalAnchor, Mate->GetActorLocation(), AidLeash))
		{
			continue;
		}
		// One rescuer per patient.
		const bool bTaken = Algo::AnyOf(States, [Mate, &Operative](const TPair<TWeakObjectPtr<AOperativeCharacter>, FOperativeState>& Pair)
				{
					return Pair.Key.Get() != &Operative && Pair.Value.Task == ETask::Aid && Pair.Value.Patient.Get() == Mate;
				});
		if (!bTaken)
		{
			Patient = Mate;
			PatientHealth = Fraction;
		}
	}
	if (!Patient)
	{
		return false;
	}
	// Safe Aid Check: no marksman aim on the rescuer or the patient, nobody within 6 m of the patient.
	bool bSniper = false;
	float NearestToPatient = TNumericLimits<float>::Max();
	for (const FEnemyView& View : Enemies)
	{
		bSniper |= View.AimTarget == &Operative || View.AimTarget == Patient;
		NearestToPatient = FMath::Min(NearestToPatient, static_cast<float>(FVector::Dist2D(View.Location, Patient->GetActorLocation())));
	}
	const float PatientToDefended = Defense.IsActive() ? static_cast<float>(FVector::Dist2D(Patient->GetActorLocation(), Defense.DefendedLocation)) : 0.f;
	if (!CanGiveSafeAid(ROE, Defense, bSniper, NearestToPatient, bIntruderPresent, PatientToDefended))
	{
		// Sprint 10: a defender does not leave the line while intruders are there (or for a mate beyond his leash).
		++(IsSafeAidRoute(ROE, bSniper, NearestToPatient) ? Stats.AidRefusedDefense : Stats.AidRefusedUnsafe);
		return false;
	}
	State.Patient = Patient;
	if (FVector::Dist2D(Patient->GetActorLocation(), Operative.GetActorLocation()) <= AOperativeCharacter::AidReachCm)
	{
		if (Operative.HealAlly(*Patient))
		{
			++Stats.AidGiven;
		}
		State.Patient.Reset();
		return true;
	}
	StartTask(Operative, State, ETask::Aid, Patient->GetActorLocation());
	++Stats.AidMoves;
	UE_LOG(LogCodexTactics, Display, TEXT("Commander Mode: %s runs to patch up %s (%.0f%% HP)"), *Operative.DisplayName.ToString(),
		*Patient->DisplayName.ToString(), PatientHealth * 100.f);
	return State.Task == ETask::Aid;
}

bool USquadAutonomySubsystem::FindCoverInLeash(const AOperativeCharacter& Operative, const FVector& Threat, FVector& OutStand) const
{
	const FSquadROE& ROE = SquadROE::Get();
	const FVector Position = Operative.GetActorLocation();
	float BestScore = -TNumericLimits<float>::Max();
	for (TActorIterator<ABarricadeActor> It(GetWorld()); It; ++It)
	{
		const UHealthComponent* Health = It->FindComponentByClass<UHealthComponent>();
		if (!Health || !Health->IsAlive() || FVector::Dist2D(It->GetActorLocation(), Position) > SquadAutonomy::CoverSearchRadiusCm)
		{
			continue;
		}
		FVector Stand;
		if (!ProjectToNav(PlaytestBotRules::CoverStandPoint(It->GetActorLocation(), Threat), Stand)
			|| !SquadAutonomyRules::IsInsideLeash(Operative.TacticalAnchor, Stand, Operative.TacticalAnchor.Radius))
		{
			continue;
		}
		const bool bElevated = ROE.bPreferHighGround && Stand.Z - Position.Z >= 150.f;
		const float Score = PlaytestBotRules::CoverScore(FVector::Dist2D(Position, Stand),
			Health->GetMaxHealth() > 0.f ? Health->GetCurrentHealth() / Health->GetMaxHealth() : 1.f, bElevated);
		if (Score > BestScore)
		{
			BestScore = Score;
			OutStand = Stand;
		}
	}
	return BestScore > -TNumericLimits<float>::Max();
}

bool USquadAutonomySubsystem::SetDefenseObjective(AOperativeCharacter* Operative, AActor* TargetObject, FVector Point)
{
	using namespace SquadAutonomyRules;
	if (!Operative)
	{
		return false;
	}
	const FSquadROE& ROE = SquadROE::Get();
	const FVector Spot = TargetObject ? TargetObject->GetActorLocation() : Point;
	// He stands next to the object: the closest walkable point (objects such as the generator sit on top of the navmesh hole).
	FVector Stand = Spot;
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	if (Nav && Nav->ProjectPointToNavigation(Spot, Projected, FVector(400.f, 400.f, 400.f)))
	{
		Stand = Projected.Location;
	}
	FTacticalAnchor Anchor = MakeAnchor(ROE, Operative->GetActorLocation(), Stand, Operative->GetActorRotation());
	Anchor.Defense = MakeDefense(ROE, TargetObject, Spot);
	Anchor.Radius = DefenseLeashRadius(ROE, Anchor.Defense, false);
	Operative->TacticalAnchor = Anchor;
	if (FOperativeState* State = States.Find(Operative))
	{
		State->Task = ETask::None;
		State->Patient.Reset();
	}
	if (FVector::Dist2D(Operative->GetActorLocation(), Stand) > Anchor.Radius)
	{
		Operative->AutonomousMoveTo(Stand);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Commander Mode: %s holds the line at %s (%s), intercept %.0f m"), *Operative->DisplayName.ToString(),
		TargetObject ? *TargetObject->GetName() : TEXT("a point"), *Spot.ToCompactString(), Anchor.Defense.InterceptRadiusCm / 100.f);
	return true;
}

bool USquadAutonomySubsystem::ProjectToNav(const FVector& Point, FVector& OutPoint) const
{
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	if (!Nav || !Nav->ProjectPointToNavigation(Point, Projected, FVector(150.f, 150.f, 300.f)))
	{
		return false;
	}
	OutPoint = Projected.Location;
	return true;
}

namespace SquadAutonomy
{
	static void SetCommand(const TArray<FString>& Args, UWorld* World)
	{
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		if (!Squad)
		{
			return;
		}
		if (Args.Num() > 0)
		{
			Squad->SetAutonomousSquadCombat(FCString::Atoi(*Args[0]) != 0);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("CodexTactics.AutonomousSquad = %d"), Squad->IsAutonomousSquadCombat() ? 1 : 0);
	}

	static void ToggleCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr)
		{
			Squad->ToggleAutonomousSquadCombat();
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs SetAutonomy(TEXT("CodexTactics.AutonomousSquad"),
		TEXT("Commander Mode (autonomous squad combat): 1 on, 0 off, no argument prints the state."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetCommand));

	static FAutoConsoleCommandWithWorldAndArgs ToggleAutonomy(TEXT("CodexTactics.ToggleAutonomousCombat"),
		TEXT("Flips Commander Mode (Ctrl + T)."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ToggleCommand));

	static void DefendCommand(const TArray<FString>& Args, UWorld* World)
	{
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		USquadAutonomySubsystem* Autonomy = World ? World->GetSubsystem<USquadAutonomySubsystem>() : nullptr;
		if (!Squad || !Autonomy)
		{
			return;
		}
		const int32 Index = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		AOperativeCharacter* Operative = nullptr;
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Operative = Member->SquadIndex == Index ? Member : Operative;
		}
		if (!Operative)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("CodexTactics.DefendObjective: no operative %d"), Index);
			return;
		}
		// The nearest objective (generator / terminal / gate) within 40 m, else the nearest barricade, else where he stands;
		// «here» as the second argument: where he stands.
		AActor* Target = nullptr;
		float Best = 4000.f;
		const bool bHere = Args.Num() > 1 && Args[1].Equals(TEXT("here"), ESearchCase::IgnoreCase);
		for (TActorIterator<AInteractableActor> It(World); It && !bHere; ++It)
		{
			const bool bObjective = It->ObjectType == EInteractableType::Generator || It->ObjectType == EInteractableType::GateTerminal
				|| It->ObjectType == EInteractableType::Gate;
			const float Distance = FVector::Dist2D(It->GetActorLocation(), Operative->GetActorLocation()) * (bObjective ? 1.f : 3.f);
			if ((bObjective || It->IsA<ABarricadeActor>()) && Distance < Best)
			{
				Best = Distance;
				Target = *It;
			}
		}
		Autonomy->SetDefenseObjective(Operative, Target, Operative->GetActorLocation());
	}

	static FAutoConsoleCommandWithWorldAndArgs DefendObjective(TEXT("CodexTactics.DefendObjective"),
		TEXT("Sprint 10: operative [index] holds the nearest objective (generator / terminal / gate, else a barricade) at all costs; «here»: his spot."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DefendCommand));
}

void USquadAutonomySubsystem::DecideInCover(AOperativeCharacter& Operative, const TArray<FEnemyView>& Enemies, const FEnemyView* Nearest, float NearestCm,
	const FEnemyView* SniperOnMe)
{
	const FCoverDecisionConfig Config;
	const FCoverSlot& Slot = Operative.GetCoverSlot();
	const FVector Position = Operative.GetActorLocation();
	int32 Shooters = 0;
	bool bElevatedEnemy = false;
	for (const FEnemyView& View : Enemies)
	{
		if (View.Enemy && View.Enemy->GetCurrentTarget() == &Operative && FVector::Dist2D(View.Location, Position) <= 3000.f)
		{
			++Shooters;
		}
		if (FVector::Dist2D(View.Location, Position) <= 3000.f && View.Location.Z > Position.Z + 150.f)
		{
			bElevatedEnemy = true;
		}
	}
	const float Health = Operative.HealthComponent ? Operative.HealthComponent->GetHealthFraction() : 1.f;
	const float Suppression = CoverDecisionRules::SuppressionFromShooters(Config, Shooters);
	const bool bLaser = SniperOnMe != nullptr;

	// The corner towards the nearest threat.
	if (Nearest)
	{
		Operative.SetCoverFacing(CoverTraceRules::ChooseFacing(Slot, &Nearest->Location));
	}

	FCoverFireSituation Fire;
	Fire.Height = Operative.CurrentCoverHeight;
	Fire.bEdgeExposed = Operative.CurrentCoverHeight == ECoverHeight::LowCover || Slot.HasExposedEdge();
	Fire.bSniperLaserOnMe = bLaser;
	Fire.HealthFraction = Health;
	Fire.SuppressionPressure = Suppression;
	Fire.RecentIncomingDamage = Operative.RecentIncomingDamage;
	Fire.DistanceToEnemyCm = Nearest ? NearestCm : 100000.f;
	const ECoverFireDecision Decision = Nearest ? CoverDecisionRules::DecideFire(Config, Fire) : ECoverFireDecision::Hold;
	const bool bHold = Decision == ECoverFireDecision::Hold;
	if (Operative.bCoverHoldFire != bHold || (!bHold && Operative.CoverFireMode != CoverDecisionRules::ToFireMode(Decision)))
	{
		Operative.bCoverHoldFire = bHold;
		if (!bHold)
		{
			Operative.SetCoverFireMode(CoverDecisionRules::ToFireMode(Decision));
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s decides %s (hp %.0f %%, pressure %.2f, recent dmg %.0f, enemy %.0f m%s)"),
			*Operative.DisplayName.ToString(), CoverDecisionRules::FireDecisionName(Decision), Health * 100.f, Suppression,
			Operative.RecentIncomingDamage, Nearest ? NearestCm / 100.f : -1.f, bLaser ? TEXT(", laser on him") : TEXT(""));
		switch (Decision)
		{
		case ECoverFireDecision::CornerPeek: ++Stats.CoverPeeks; break;
		case ECoverFireDecision::BlindFire: ++Stats.CoverBlindFires; break;
		default: ++Stats.CoverHolds; break;
		}
	}

	FCoverStanceSituation StanceSituation;
	StanceSituation.Height = Operative.CurrentCoverHeight;
	StanceSituation.bSniperLaserOnMe = bLaser;
	StanceSituation.HealthFraction = Health;
	StanceSituation.SuppressionPressure = Suppression;
	StanceSituation.bEnemyElevated = bElevatedEnemy;
	StanceSituation.bWantsAimedFire = Decision == ECoverFireDecision::CornerPeek;
	const EOperativeStance Wanted = CoverDecisionRules::ToStance(CoverDecisionRules::DecideStance(Config, StanceSituation));
	if (Operative.GetStance() != EOperativeStance::Prone && Operative.GetStance() != Wanted)
	{
		Operative.SetStance(Wanted);
		++Stats.CoverStanceChanges;
		Stats.CoverCrouchForLaser += bLaser && Wanted == EOperativeStance::Crouching ? 1 : 0;
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s %s at the wall%s"), *Operative.DisplayName.ToString(),
			Wanted == EOperativeStance::Crouching ? TEXT("crouches") : TEXT("stands"), bLaser ? TEXT(" (sniper laser)") : TEXT(""));
	}
}
