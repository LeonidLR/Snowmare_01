// AEnemyCharacter: real-time combat behaviour - TickBehavior (fire fear, pack-tactics order + flank detour, cutter pounce
// trigger, obstacle smash, melee reach / close-in, belief hunt), target selection, fire zones, blocking obstacles.
// Split out of EnemyCharacter.cpp verbatim (cleanup 2026-10-19; map: docs/port/REFACTOR_ENEMY_2026-10.md).
// Godot reference: Scenes/movements/enemy_base.gd (_process_enemy_behavior, _find_target); pack tactics UE-only.

#include "Characters/EnemyCharacter.h"
#include "Vehicles/VehiclePawn.h"
#include "Vehicles/VehicleSubsystem.h"
#include "Combat/KnockdownRules.h"
#include "Characters/EnemyTacticsSubsystem.h"
#include "CodexTactics.h"
#include "UI/FloatingTextSubsystem.h"
#include "Interactables/TurretActor.h"
#include "Interactables/HeatSourceComponent.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/BarrelActor.h"
#include "EngineUtils.h"
#include "AIController.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Combat/TacticalSightSubsystem.h"

namespace EnemyCharacterLocal
{
	/**
	 * Bug hunt 2026-10-18, ON since the user's approval 2026-10-18: melee enemies close in until they can bite
	 * (EnemyAIRules::MeleeApproachAcceptance). The game value is DATA: Content/Data/AI/ai_tuning.json "cvars"
	 * "Codex.Enemy.MeleeCloseIn": "1" (applied at StartPlay, Data/AITuning.h). The code default 0 is the old stop (AttackRange
	 * x 0.5 + both capsule radii = hounds / frostbitten 1.65-1.75 m off, outside their 1.5 m planar reach) and stays for A/B:
	 * console "Codex.Enemy.MeleeCloseIn 0" or -dpcvars=Codex.Enemy.MeleeCloseIn=0 (wins over the file), or -NoAITuning.
	 */
	static TAutoConsoleVariable<int32> CVarMeleeCloseIn(TEXT("Codex.Enemy.MeleeCloseIn"), 0,
		TEXT("1 (ai_tuning.json, the game value since 2026-10-18): melee enemies close in to their bite reach (MoveToActor acceptance minus the capsule radii); 0 (code default, A/B): the old AttackRange x 0.5 stop (1.65-1.75 m off a hound's 1.5 m reach)"));
}

void AEnemyCharacter::TickBehavior(float DeltaTime)
{
	if (bIsDying || !HealthComponent || !HealthComponent->IsAlive() || IsKnockedDown())
	{
		return; // knocked down: lies until the knockdown component gets it up
	}
	AAIController* AIC = Cast<AAIController>(GetController());

	if (JumpCooldownTimer > 0.f)
	{
		JumpCooldownTimer -= DeltaTime;
	}
	if (IsJumpAttacking())
	{
		TickJumpAttack(DeltaTime);
		return;
	}
	// A dialogue / cutscene is up: no perception, no attack, no walking (user request 2026-10-06).
	if (HoldForWorldAIPause())
	{
		return;
	}

	// Godot: stagger freezes the enemy.
	if (HealthComponent->HasStatusEffect(EStatusEffect::Stagger))
	{
		if (AIC)
		{
			AIC->StopMovement();
		}
		return;
	}
	if (AttackTimer > 0.0f)
	{
		AttackTimer -= DeltaTime;
	}
	// Sprint 11: a patroller / escort walks its route until alerted (then the normal combat AI below takes over).
	if (bPatrolActive)
	{
		TickPatrolBehavior(DeltaTime);
		if (bPatrolActive)
		{
			return;
		}
	}
	// Godot is_attacking: while the attack clip plays the enemy stands and turns to its target.
	if (AttackLockTimer > 0.f)
	{
		AttackLockTimer -= DeltaTime;
		if (AIC)
		{
			AIC->StopMovement();
		}
		if (const AActor* LockTarget = AttackLockTarget.Get())
		{
			const FVector ToTarget = LockTarget->GetActorLocation() - GetActorLocation();
			if (!ToTarget.IsNearlyZero(1.f))
			{
				FaceYaw(ToTarget.Rotation().Yaw, DeltaTime, 10.f);
			}
		}
		return;
	}
	// Godot current_max_speed: frost halves the speed; fleeing from fire speeds it up.
	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed * (HealthComponent->HasStatusEffect(EStatusEffect::Frozen) ? 0.5f : 1.f)
		* (bFleeingFire ? AIConfig.FireFearFleeSpeedMultiplier : 1.f);

	// 0. Panic fear of fire (hounds, cutters, frostbitten). Once fleeing, the enemy calms down only 1 m past the edge
	// (without it the fear switched on and off every frame at the edge and the enemy shook on the spot).
	FVector Fire;
	float FireRadius = 0.f;
	const bool bNearFire = bFearsFire && AIConfig.bFireFearEnabled && !bBravingFire && FindNearestFireZone(bFleeingFire ? 100.f : 0.f, Fire, FireRadius);
	if (bNearFire)
	{
		const AActor* Victim = CurrentTarget.Get();
		const FVector VictimLocation = Victim ? Victim->GetActorLocation() : FVector::ZeroVector;
		const FVector Direction = EnemyAIRules::FleeDirection(GetActorLocation(), Fire, Victim ? &VictimLocation : nullptr);
		if (AIC)
		{
			RequestAIMoveToLocation(AIC, GetActorLocation() + Direction * 300.f, 50.f);
		}
		if (!bFleeingFire)
		{
			bFleeingFire = true;
			UFloatingTextSubsystem::SpawnAboveEnemy(this, TEXT("🔥😱 FEAR OF FIRE!"), FLinearColor(1.f, 0.45f, 0.1f));
		}
		return;
	}
	bFleeingFire = false;

	if (Archetype == EEnemyArchetype::Spitter || Archetype == EEnemyArchetype::CryoDrone)
	{
		TickSpitter(DeltaTime);
		return;
	}

	AActor* Target = FindTarget();
	// Pack tactics (EnemyTacticsSubsystem): another operative (surround / the wounded / the straggler), a flank route
	// or a morale fall-back. A turret / generator target of the Godot rules is kept.
	FEnemyTacticOrder Order;
	UEnemyTacticsSubsystem* Tactics = GetWorld()->GetSubsystem<UEnemyTacticsSubsystem>();
	const bool bHasOrder = Target && Target->IsA<AOperativeCharacter>() && Tactics && Tactics->GetOrder(this, Order);
	if (bHasOrder && Order.Role == EEnemyTacticRole::FallBack && !bKnowsSquadPosition) // a horde never falls back
	{
		if (!bFallingBack)
		{
			bFallingBack = true;
			UFloatingTextSubsystem::SpawnAboveEnemy(this, TEXT("↩ FALLING BACK"), FLinearColor(0.7f, 0.85f, 1.f));
		}
		if (AIC)
		{
			RequestAIMoveToLocation(AIC, Order.MovePoint, 80.f);
		}
		CurrentTarget = nullptr;
		return;
	}
	bFallingBack = false;
	if (bHasOrder && Order.Target.IsValid())
	{
		Target = Order.Target.Get();
	}
	// Flank detour (user decision 2026-10-18, a real detour): a flanker walks to its flank point first and closes in from there -
	// or at once when already within bite reach on the flank side; a blocked point / a long detour falls back to straight in.
	// Latched per target (no switching back to the detour: FacingSmoke's crowding hounds turned back and forth).
	if (bHasOrder && Order.Role == EEnemyTacticRole::Flank && Target && FlankDoneTarget.Get() != Target)
	{
		const FVector MyFeet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
		if (FlankDetourTarget.Get() != Target)
		{
			FlankDetourTarget = Target;
			FlankDetourElapsed = 0.f;
			FlankDetourStuck = 0.f;
			FlankDetourProbe = MyFeet;
		}
		FlankDetourElapsed += DeltaTime;
		if (FVector::Dist2D(MyFeet, FlankDetourProbe) > 50.f)
		{
			FlankDetourProbe = MyFeet;
			FlankDetourStuck = 0.f;
		}
		else
		{
			FlankDetourStuck += DeltaTime;
		}
		const FVector TargetFeet = GodotPosition(Target);
		FFlankDetourInput Detour;
		Detour.DistanceToPointCm = FVector::Dist2D(MyFeet, Order.MovePoint);
		Detour.DistanceToTargetCm = FVector::Dist2D(MyFeet, TargetFeet);
		Detour.BiteReachCm = EnemyAIRules::MeleeReachPlanar(AttackRange, FMath::Abs(Target->GetActorLocation().Z - MyFeet.Z));
		const FVector FromTarget = (MyFeet - TargetFeet).GetSafeNormal2D();
		const FVector FlankDirection = (Order.MovePoint - TargetFeet).GetSafeNormal2D();
		Detour.AngleFromFlankDeg = FlankDirection.IsNearlyZero() ? 180.f
			: FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(FromTarget, FlankDirection)), -1.f, 1.f)));
		Detour.ElapsedSeconds = FlankDetourElapsed;
		Detour.StuckSeconds = FlankDetourStuck;
		const EFlankDetourResult Result = EnemyTacticsRules::EvaluateFlankDetour(Detour, UEnemyTacticsSubsystem::GetFlankDetourConfig());
		if (Result != EFlankDetourResult::Continue)
		{
			FlankDoneTarget = Target;
			UE_LOG(LogCodexTactics, Verbose, TEXT("[EnemyTactics] %s flank detour ends after %.1f s: %s (%.0f cm from the point, %.0f deg off the flank)"),
				*GetName(), FlankDetourElapsed, EnemyTacticsRules::FlankDetourResultName(Result), Detour.DistanceToPointCm, Detour.AngleFromFlankDeg);
		}
	}
	CurrentTarget = Target;
	if (!Target)
	{
		// Sprint 11: an alerted patroller that knows of no operative investigates where the alarm came from.
		if (bHasPatrolAlertLocation && AIC && FVector::Dist2D(GetActorLocation(), PatrolAlertLocation) > 150.f)
		{
			RequestAIMoveToLocation(AIC, PatrolAlertLocation, 100.f);
			return;
		}
		bHasPatrolAlertLocation = false;
		if (AIC)
		{
			AIC->StopMovement();
		}
		return;
	}
	bHasPatrolAlertLocation = false;

	auto Face =[this, DeltaTime](const AActor* Actor)
	{
		FaceYaw((Actor->GetActorLocation() - GetActorLocation()).Rotation().Yaw, DeltaTime, 10.f);
	};

	// Godot enemy_cutter.gd _process_enemy_behavior: a pounce at 3.5-9 m (at most 2.5 m of height difference).
	AVehiclePawn* TargetVehicle = Cast<AVehiclePawn>(Target);
	if (Archetype == EEnemyArchetype::Cutter && bJumpAttackEnabled && JumpCooldownTimer <= 0.f && !TargetVehicle)
	{
		const FVector JumpFeet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
		const FVector JumpTargetPosition = GodotPosition(Target);
		const float JumpDistance = FVector::Dist(JumpFeet, JumpTargetPosition);
		if (JumpDistance >= JumpMinDistance && JumpDistance <= JumpMaxDistance && FMath::Abs(JumpTargetPosition.Z - JumpFeet.Z) <= 250.f
			&& StartJumpAttack(Target))
		{
			return;
		}
	}

	// 1. A barricade / turret in the way is smashed first.
	if (AActor* Obstacle = FindBlockingObstacle(Target))
	{
		if (AIC)
		{
			AIC->StopMovement();
		}
		Face(Obstacle);
		if (AttackTimer <= 0.f)
		{
			AttackObject(Obstacle);
		}
		return;
	}

	// 2. Melee reach (Godot: attack range and at most 1.2 m of height difference), else close in.
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	// APC P4: a vehicle is reached at its nearest hull side (a 7.8 m hull: its pivot is metres away), at the feet's height.
	const FVector TargetPosition = TargetVehicle ? FVector(FVector2D(TargetVehicle->GetClosestHullPoint(Feet)), Feet.Z) : GodotPosition(Target);
	if (EnemyAIRules::CanMelee(FVector::Dist(Feet, TargetPosition), FMath::Abs(TargetPosition.Z - Feet.Z), AttackRange))
	{
		if (AIC)
		{
			AIC->StopMovement();
		}
		Face(Target);
		if (AttackTimer <= 0.0f)
		{
			if (Target->IsA<AOperativeCharacter>())
			{
				AttackTarget(Target);
			}
			else
			{
				AttackObject(Target);
			}
		}
	}
	else if (AIC)
	{
		// Stuck (no path to the target, or blocked): after 3 s without 1 m of progress the target is skipped for 8 s.
		if (Target != ProgressTarget.Get() || FVector::Dist2D(Feet, ProgressLocation) > 100.f)
		{
			ProgressTarget = Target;
			ProgressLocation = Feet;
			ProgressTimer = 0.f;
		}
		else if ((ProgressTimer += DeltaTime) > 3.f)
		{
			UnreachableUntil.Add(Target, GetWorld()->GetTimeSeconds() + 8.0);
			ProgressTimer = 0.f;
			UE_LOG(LogCodexTactics, Log, TEXT("%s: no way to %s, picking another target"), *EnemyDisplayName, *Target->GetName());
		}
		// Around a fire / heat zone instead of into it (and back out, and in again).
		FVector Zone;
		float ZoneRadius = 0.f;
		FVector Waypoint;
		if (TargetVehicle)
		{
			// APC P4: up to the hull side nearest it (the parked hull is carved out of the navmesh).
			RequestAIMoveToLocation(AIC, TargetVehicle->GetHullAttackPoint(Feet, GetSimpleCollisionRadius() + 30.f), 40.f);
		}
		else if (bFearsFire && AIConfig.bFireFearEnabled && !bBravingFire && FindNearestFireZone(800.f, Zone, ZoneRadius)
			&& EnemyAIRules::FireDetourWaypoint(Feet, Zone, ZoneRadius + 50.f, TargetPosition, Waypoint))
		{
			if (FVector::Dist2D(Feet, Waypoint) > 60.f)
			{
				RequestAIMoveToLocation(AIC, Waypoint, 30.f);
			}
			else
			{
				AIC->StopMovement(); // waiting at the edge for a target inside the zone
				Face(Target);
			}
		}
		else if (bHasOrder && Order.Role == EEnemyTacticRole::Flank && FlankDoneTarget.Get() != Target)
		{
			RequestAIMoveToLocation(AIC, Order.MovePoint, 60.f); // round his side, then in (the detour latch above ends it)
		}
		else
		{
			// Sprint 08: an operative it does not perceive now is hunted at his last known spot.
			FVector Belief;
			bool bPerceived = true;
			const UTacticalSightSubsystem* Sight = GetWorld()->GetSubsystem<UTacticalSightSubsystem>();
			if (Sight && Target->IsA<AOperativeCharacter>() && Sight->GetBelief(this, Target, Belief, &bPerceived) && !bPerceived)
			{
				RequestAIMoveToLocation(AIC, Belief, 50.f);
			}
			else
			{
				// MoveToActor's reach test adds both capsule radii: AttackRange x 0.5 stops hounds 1.7 m off, out of their own
				// reach (bug hunt 2026-10-18). Codex.Enemy.MeleeCloseIn 1 (ai_tuning.json, user approval 2026-10-18) closes in.
				const float Acceptance = EnemyCharacterLocal::CVarMeleeCloseIn.GetValueOnGameThread() != 0
					? EnemyAIRules::MeleeApproachAcceptance(AttackRange, FMath::Abs(TargetPosition.Z - Feet.Z), GetSimpleCollisionRadius(),
						Target->GetSimpleCollisionRadius())
					: AttackRange * 0.5f;
				RequestAIMoveToActor(AIC, Target, Acceptance);
			}
		}
	}
}

bool AEnemyCharacter::IsTargetUsableForTactics(const AActor* Candidate) const
{
	if (!Candidate)
	{
		return false;
	}
	const double* Until = UnreachableUntil.Find(Candidate);
	if (Until && *Until > GetWorld()->GetTimeSeconds())
	{
		return false;
	}
	// Sprint 08: an operative it neither perceives nor remembers is no target.
	FVector Belief;
	if (const UTacticalSightSubsystem* Sight = GetWorld()->GetSubsystem<UTacticalSightSubsystem>();
		Sight && Candidate->IsA<AOperativeCharacter>() && !Sight->GetBelief(this, Candidate, Belief))
	{
		return false;
	}
	return !(bFearsFire && AIConfig.bFireFearEnabled && !bBravingFire && IsInFearZone(GodotPosition(Candidate), Candidate));
}

AActor* AEnemyCharacter::FindTarget() const
{
	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
	if (!Squad)
	{
		return nullptr;
	}
	TArray<AActor*> Actors;
	TArray<FEnemyTargetCandidate> Candidates;
	// Sprint 08: operatives where it believes them (seen / heard / remembered); unknown ones are no candidates.
	const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
	TArray<AOperativeCharacter*> Known;
	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		FVector Belief = Member->GetActorLocation();
		if (Member->HealthComponent && Member->HealthComponent->IsAlive() && !VehicleQueries::IsMounted(Member) && (!Sight || Sight->GetBelief(this, Member, Belief)))
		{
			Known.Add(Member);
			Actors.Add(Member);
			Candidates.Add({ EEnemyTargetKind::Operative, Belief - FVector(0.f, 0.f, Member->GetSimpleCollisionHalfHeight() - 100.f), true });
		}
	}
	for (TActorIterator<ATurretActor> It(World); It; ++It)
	{
		const UHealthComponent* TurretHealth = It->FindComponentByClass<UHealthComponent>();
		Actors.Add(*It);
		Candidates.Add({ EEnemyTargetKind::Turret, GodotPosition(*It), !It->IsBroken() && TurretHealth && TurretHealth->IsAlive() });
	}
	for (TActorIterator<AInteractableActor> It(World); It; ++It)
	{
		if (It->ObjectType == EInteractableType::Generator)
		{
			Actors.Add(*It);
			Candidates.Add({ EEnemyTargetKind::Generator, GodotPosition(*It), !It->bGeneratorBroken && It->GeneratorHealth > 0.f });
		}
	}
	// APC P4: a manned vehicle (the crew inside is the damage proxy's: the hull takes the blows), at its nearest hull point.
	if (AVehiclePawn* Vehicle = FindVehicleTarget())
	{
		const FVector EnemyFeet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
		FEnemyTargetCandidate Candidate{ EEnemyTargetKind::Vehicle, Vehicle->GetClosestHullPoint(EnemyFeet), true };
		Candidate.Weight = VehicleHullRules::GetEnemyTargetWeight(Archetype != EEnemyArchetype::Brute,
			Vehicle->HullConfig);
		Actors.Add(Vehicle);
		Candidates.Add(Candidate);
	}
	// Targets it could not get closer to recently (no path on the navmesh) are skipped for a while, and an enemy that
	// fears fire prefers targets outside fire / heat zones (it would only wait at their edge).
	const double Now = World->GetTimeSeconds();
	const TArray<FEnemyTargetCandidate> AllCandidates = Candidates;
	bool bAnyUsable = false;
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		const double* Until = UnreachableUntil.Find(Actors[Index]);
		if ((Until && *Until > Now) || (bFearsFire && AIConfig.bFireFearEnabled && IsInFearZone(Candidates[Index].Location, Actors[Index]))
			|| (bKnowsSquadPosition && !Known.IsEmpty() && Candidates[Index].Kind != EEnemyTargetKind::Operative
				&& Candidates[Index].Kind != EEnemyTargetKind::Vehicle)) // a horde goes for the squad (and its manned APC)
		{
			Candidates[Index].bUsable = false;
		}
		bAnyUsable |= Candidates[Index].bUsable;
	}
	bBravingFire = false;
	if (!bAnyUsable)
	{
		// Every target is in a warm zone or out of reach: go for the nearest operative and brave the fire (user decision
		// 2026-10-02 — waiting at the edge of the generator's warm zone stalled the waves).
		AActor* Nearest = nullptr;
		float NearestDistance = TNumericLimits<float>::Max();
		const FVector From = GetActorLocation();
		for (AOperativeCharacter* Member : Known)
		{
			if (FVector::Dist(From, Member->GetActorLocation()) < NearestDistance)
			{
				NearestDistance = FVector::Dist(From, Member->GetActorLocation());
				Nearest = Member;
			}
		}
		if (Nearest)
		{
			bBravingFire = true;
			return Nearest;
		}
		Candidates = AllCandidates;
	}
	const bool bTurretHit = LastAttackerSource.Contains(TEXT("Турель")) || LastAttackerSource.Contains(TEXT("Turret")); // cyrillic-ok: legacy Russian data
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	const int32 Index = EnemyAIRules::SelectTarget(AIConfig, EnemyAIRules::IsSmallEnemy(Archetype), Feet, Candidates, bTurretHit);
	if (Actors.IsValidIndex(Index))
	{
		// Sprint 14: a melee enemy goes for a knocked-down operative close by (he takes x1.33, cannot hit back); spitters keep their pick.
		const bool bMelee = Archetype != EEnemyArchetype::Spitter && Archetype != EEnemyArchetype::Marksman;
		const float CurrentDistance = FVector::Dist2D(Feet, Actors[Index]->GetActorLocation());
		for (AOperativeCharacter* Member : Known)
		{
			if (Member != Actors[Index] && Member->IsKnockedDown()
				&& KnockdownRules::PreferDownedTarget(KnockdownRules::GetConfig(), bMelee, FVector::Dist2D(Feet, Member->GetActorLocation()), CurrentDistance))
			{
				return Member;
			}
		}
		return Actors[Index];
	}
	// Everything skipped: never stand idle — hunt the nearest living operative it knows about.
	AActor* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();
	for (AOperativeCharacter* Member : Known)
	{
		if (FVector::Dist(Feet, Member->GetActorLocation()) < NearestDistance)
		{
			NearestDistance = FVector::Dist(Feet, Member->GetActorLocation());
			Nearest = Member;
		}
	}
	return Nearest;
}

bool AEnemyCharacter::IsInFearZone(const FVector& Location, const AActor* Candidate) const
{
	UWorld* World = GetWorld();
	for (TActorIterator<ABarrelActor> It(World); It; ++It)
	{
		if (It->IsBurning() && FVector::Dist2D(Location, It->GetActorLocation()) < AIConfig.FireFearRadius)
		{
			return true;
		}
	}
	for (const TWeakObjectPtr<UHeatSourceComponent>& Source : UHeatSourceComponent::GetAllSources())
	{
		if (Source.IsValid() && Source->GetWorld() == World && Source->IsHeatActive() && Source->GetOwner() != Candidate
			&& FVector::Dist2D(Location, Source->GetComponentLocation()) < FMath::Max(AIConfig.FireFearRadius, Source->Radius))
		{
			return true;
		}
	}
	return false;
}

bool AEnemyCharacter::FindNearestFireZone(float Extra, FVector& OutFire, float& OutRadius) const
{
	UWorld* World = GetWorld();
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	float BestMargin = TNumericLimits<float>::Max();
	bool bFound = false;
	auto Consider = [&](const FVector& Location, float Radius)
	{
		const float Distance = FVector::Dist2D(Feet, Location);
		if (Distance < Radius + Extra && Distance - Radius < BestMargin)
		{
			BestMargin = Distance - Radius;
			OutFire = Location;
			OutRadius = Radius;
			bFound = true;
		}
	};
	for (TActorIterator<ABarrelActor> It(World); It; ++It)
	{
		if (It->IsBurning())
		{
			Consider(It->GetActorLocation(), AIConfig.FireFearRadius);
		}
	}
	const AActor* OwnTarget = CurrentTarget.Get();
	for (const TWeakObjectPtr<UHeatSourceComponent>& Source : UHeatSourceComponent::GetAllSources())
	{
		// The warm zone of the generator this enemy is sent to break does not stop it (small enemies go for the
		// generator first; otherwise they froze at its edge).
		if (Source.IsValid() && Source->GetWorld() == World && Source->IsHeatActive() && Source->GetOwner() != OwnTarget)
		{
			Consider(Source->GetComponentLocation(), FMath::Max(AIConfig.FireFearRadius, Source->Radius));
		}
	}
	return bFound;
}

bool AEnemyCharacter::FindNearestFire(FVector& OutFire) const
{
	UWorld* World = GetWorld();
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	float MinDistance = AIConfig.FireFearRadius;
	bool bFound = false;
	for (TActorIterator<ABarrelActor> It(World); It; ++It)
	{
		const float Distance = FVector::Dist(Feet, It->GetActorLocation());
		if (It->IsBurning() && Distance < MinDistance)
		{
			MinDistance = Distance;
			OutFire = It->GetActorLocation();
			bFound = true;
		}
	}
	for (const TWeakObjectPtr<UHeatSourceComponent>& Source : UHeatSourceComponent::GetAllSources())
	{
		if (!Source.IsValid() || Source->GetWorld() != World || !Source->IsHeatActive())
		{
			continue;
		}
		const float Distance = FVector::Dist(Feet, Source->GetComponentLocation());
		if (Distance < FMath::Max(AIConfig.FireFearRadius, Source->Radius) && Distance < MinDistance)
		{
			MinDistance = Distance;
			OutFire = Source->GetComponentLocation();
			bFound = true;
		}
	}
	return bFound;
}

AActor* AEnemyCharacter::FindBlockingObstacle(const AActor* Target) const
{
	UWorld* World = GetWorld();
	TArray<AActor*> Actors;
	TArray<FVector> Obstacles;
	for (TActorIterator<ABarricadeActor> It(World); It; ++It)
	{
		const UHealthComponent* BarricadeHealth = It->FindComponentByClass<UHealthComponent>();
		if (BarricadeHealth && BarricadeHealth->IsAlive())
		{
			Actors.Add(*It);
			Obstacles.Add(GodotPosition(*It));
		}
	}
	if (AIConfig.bCanTargetTurrets)
	{
		for (TActorIterator<ATurretActor> It(World); It; ++It)
		{
			const UHealthComponent* TurretHealth = It->FindComponentByClass<UHealthComponent>();
			if (*It != Target && TurretHealth && TurretHealth->IsAlive())
			{
				Actors.Add(*It);
				Obstacles.Add(GodotPosition(*It));
			}
		}
	}
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	const FVector TargetPosition = Target ? GodotPosition(Target) : FVector::ZeroVector;
	const int32 Index = EnemyAIRules::SelectBlockingObstacle(Feet, Target ? &TargetPosition : nullptr, Obstacles);
	return Actors.IsValidIndex(Index) ? Actors[Index] : nullptr;
}

AVehiclePawn* AEnemyCharacter::FindVehicleTarget() const
{
	const UVehicleSubsystem* Vehicles = GetWorld() ? GetWorld()->GetSubsystem<UVehicleSubsystem>() : nullptr;
	return Vehicles && Vehicles->HasMountedOperatives() ? Vehicles->FindEnemyTargetVehicle(GetActorLocation(), bKnowsSquadPosition) : nullptr;
}

AActor* AEnemyCharacter::FindClosestSquadMember() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	if (!Squad)
	{
		return nullptr;
	}

	AActor* Closest = nullptr;
	float MinDistSq = TNumericLimits<float>::Max();
	const FVector MyLoc = GetActorLocation();

	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (!Member || !Member->HealthComponent || !Member->HealthComponent->IsAlive() || VehicleQueries::IsMounted(Member))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(MyLoc, Member->GetActorLocation());
		if (DistSq < MinDistSq)
		{
			MinDistSq = DistSq;
			Closest = Member;
		}
	}

	return Closest;
}
