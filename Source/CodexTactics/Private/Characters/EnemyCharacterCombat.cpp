// AEnemyCharacter: attacks - melee blow, object / vehicle attacks, spitter fire, cutter jump attack, knocking operatives down.
// Split out of EnemyCharacter.cpp verbatim (cleanup 2026-10-19; map: docs/port/REFACTOR_ENEMY_2026-10.md).
// Godot reference: Scenes/movements/enemy_base.gd (_attack_target), enemy_frost_spitter.gd, enemy_cutter.gd (jump attack).

#include "Characters/EnemyCharacter.h"
#include "Vehicles/VehiclePawn.h"
#include "Vehicles/VehicleSubsystem.h"
#include "Combat/KnockdownComponent.h"
#include "Combat/KnockdownRules.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "Characters/EnemyTacticsSubsystem.h"
#include "CodexTactics.h"
#include "UI/FloatingTextSubsystem.h"
#include "Interactables/BarricadeActor.h"
#include "EngineUtils.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "AIController.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/EnemyAnimInstance.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/SuppressionSubsystem.h"

void AEnemyCharacter::TryKnockDownOperative(AOperativeCharacter* Operative, float DamageDealt)
{
	UKnockdownComponent* Knockdown = Operative ? Operative->KnockdownComponent.Get() : nullptr;
	if (!Knockdown || DamageDealt <= 0.f)
	{
		return; // dodged
	}
	if (Archetype == EEnemyArchetype::Brute)
	{
		Knockdown->TryKnockDown(EKnockdownCause::HeavyMelee, GetActorLocation(), DamageDealt);
	}
	else if (Archetype == EEnemyArchetype::FrostHound
		&& KnockdownRules::IsHoundPounce(KnockdownRules::GetConfig(), RunUpSeconds, SecondsSinceHoundPounce)
		&& Knockdown->TryKnockDown(EKnockdownCause::Pounce, GetActorLocation(), DamageDealt))
	{
		SecondsSinceHoundPounce = 0.f;
		UFloatingTextSubsystem::SpawnAboveEnemy(this, TEXT("POUNCE!"), FLinearColor(1.f, 0.35f, 0.1f));
	}
}

void AEnemyCharacter::AttackObject(AActor* Object)
{
	AttackTimer = AttackCooldown;
	StartAttackAnimation(Object);
	OnAttackStarted(Object);
	if (AVehiclePawn* Vehicle = Cast<AVehiclePawn>(Object))
	{
		// APC P4: claws scratch the armour, a Brute's blow dents it (VehicleHullRules armour per source).
		NotifySquadAttacked(Vehicle);
		if (UTacticalSightSubsystem* Sight = GetWorld()->GetSubsystem<UTacticalSightSubsystem>())
		{
			Sight->NotifyFired(this);
		}
		Vehicle->ApplyHullDamage(AttackDamage, VehicleHullRules::ClassifyEnemyAttack(Archetype), EnemyDisplayName, this);
		return;
	}
	if (AInteractableActor* Interactable = Cast<AInteractableActor>(Object);
		Interactable && Interactable->ObjectType == EInteractableType::Generator && !Object->IsA<ADeployableActor>())
	{
		Interactable->TakeGeneratorDamage(AttackDamage);
		return;
	}
	UHealthComponent* ObjectHealth = Object->FindComponentByClass<UHealthComponent>();
	if (!ObjectHealth)
	{
		return;
	}
	// Godot barricade / turret take_damage: raw damage; an enemy blow sets off a trap. Brutes hit barricades twice.
	const bool bBarricade = Object->IsA<ABarricadeActor>();
	ObjectHealth->ApplyDirectHealthLoss(AttackDamage * (bBarricade && Archetype == EEnemyArchetype::Brute ? 2.f : 1.f), EnemyDisplayName);
	if (AInteractableActor* Trapped = Cast<AInteractableActor>(Object); Trapped && Trapped->bTrapped && IsValid(Trapped))
	{
		Trapped->DetonateTrap(false, NSLOCTEXT("EnemyCharacter", "EnemyBlow", "Enemy strike"));
	}
	else if (ABarricadeActor* Barricade = Cast<ABarricadeActor>(Object); Barricade && IsValid(Barricade))
	{
		Barricade->RetaliateAgainst(this); // Godot barricade.gd take_damage: spikes / fire / cryo / energy strike back
	}
}

void AEnemyCharacter::TickSpitter(float DeltaTime)
{
	UWorld* World = GetWorld();
	AAIController* AIC = Cast<AAIController>(GetController());
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	TArray<AOperativeCharacter*> Members;
	TArray<FVector> Positions;
	TArray<bool> Elevated;
	const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
	for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		FVector Belief;
		if (Member->HealthComponent && Member->HealthComponent->IsAlive() && !VehicleQueries::IsMounted(Member) && (!Sight || Sight->GetBelief(this, Member, Belief)))
		{
			Members.Add(Member);
			Positions.Add(GodotPosition(Member));
			Elevated.Add(GodotPosition(Member).Z >= 240.f); // Godot: y >= 2.4 m counts as elevated ground
		}
	}
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	const int32 Index = EnemyAIRules::SelectSpitterTarget(Feet, Positions, Elevated);
	AOperativeCharacter* Target = Members.IsValidIndex(Index) ? Members[Index]
		: (Sight && Sight->IsActive() ? nullptr : Cast<AOperativeCharacter>(FindClosestSquadMember()));
	CurrentTarget = Target;
	if (!Target)
	{
		// APC P4: the squad is aboard - spit at the hull (acid x the hull's acid factor).
		if (AVehiclePawn* Vehicle = FindVehicleTarget())
		{
			CurrentTarget = Vehicle;
			TickRangedVsVehicle(*Vehicle, DeltaTime, AttackRange, AttackDamage, AttackCooldown, 40.f, 1.f);
			return;
		}
		if (AIC)
		{
			AIC->StopMovement();
		}
		return;
	}
	// Line of fire: from 0.4 m above the feet to the target's stance height (Godot _check_line_of_sight).
	const FVector Start = Feet + FVector(0.f, 0.f, 40.f);
	const float TargetHeight = Target->GetStance() == EOperativeStance::Prone ? 30.f : (Target->GetStance() == EOperativeStance::Crouching ? 90.f : 150.f);
	const FVector End = GodotPosition(Target) - FVector(0.f, 0.f, 100.f - TargetHeight);
	FSpitterLine Line;
	if (End.Z - Feet.Z >= 180.f && FVector::Dist2D(End, Feet) <= 220.f)
	{
		Line.bHasLos = false; // under the platform
	}
	else
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SpitterLine), false, this);
		Params.AddIgnoredActor(Target);
		for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
		{
			Params.AddIgnoredActor(Enemy); // Godot mask: walls and barricades only
		}
		FHitResult Hit;
		EShotLineHit Kind = EShotLineHit::Clear;
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) && Hit.GetActor() && !Hit.GetActor()->IsA<AOperativeCharacter>())
		{
			Kind = Hit.GetActor()->IsA<ABarricadeActor>() ? EShotLineHit::Barricade : EShotLineHit::Blocked;
		}
		Line = EnemyAIRules::JudgeSpitterLine(Kind, Target->GetStance(), AIConfig.CrouchCoverReduction);
	}
	const float Distance = FVector::Dist(Feet, GodotPosition(Target));
	FaceYaw((Target->GetActorLocation() - GetActorLocation()).Rotation().Yaw, DeltaTime, 8.f);
	// Suppressive fire (game designer decision 2026-10-26): suppressed / pinned he does not fire back at all and holds his spot.
	if (IsHeldBySuppression())
	{
		SuppressionHoldSeconds += DeltaTime;
		if (AIC)
		{
			AIC->StopMovement();
		}
		return;
	}
	switch (EnemyAIRules::SpitterMove(Line.bHasLos, Distance, AIConfig.SpitterPreferredRange))
	{
	case ESpitterMove::Approach:
		if (AIC)
		{
			FVector Belief;
			bool bPerceived = true;
			if (Sight && Sight->GetBelief(this, Target, Belief, &bPerceived) && !bPerceived)
			{
				RequestAIMoveToLocation(AIC, Belief, 50.f); // to where it last knew him
			}
			else
			{
				RequestAIMoveToActor(AIC, Target, AIConfig.SpitterPreferredRange * 0.5f);
			}
		}
		break;
	case ESpitterMove::Retreat:
		if (AIC)
		{
			const FVector Back = (GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
			RequestAIMoveToLocation(AIC, GetActorLocation() + Back * 300.f, 50.f);
		}
		break;
	default:
		if (AIC)
		{
			AIC->StopMovement();
		}
		break;
	}
	// Fires only with a clear line (Godot _shoot_at_target: crit, cover, red tracer).
	if (Line.bHasLos && Distance <= AttackRange && AttackTimer <= 0.f)
	{
		AttackTimer = AttackCooldown;
		NotifySquadAttacked(Target);
		if (UTacticalSightSubsystem* FireSight = World->GetSubsystem<UTacticalSightSubsystem>())
		{
			FireSight->NotifyFired(this); // the acid spit gives it away (Sprint 08-E)
		}
		const bool bIsCrit = FMath::FRand() < CritChance;
		Target->TakeHit(AttackDamage * (bIsCrit ? CritMultiplier : 1.f) * Line.Cover, EnemyDisplayName, bIsCrit, false, this, bIsCrit ? CritMultiplier : 1.f);
		if (UCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UCombatFeedbackSubsystem>())
		{
			Feedback->SpawnTracer(Feet + FVector(0.f, 0.f, 90.f), End, FLinearColor(1.f, 0.2f, 0.2f));
		}
		// Suppression (2026-10-26): the acid that hits him suppresses him (and anyone it passes near).
		ReportRoundAtSquad(Feet + FVector(0.f, 0.f, 90.f), End, Target, true, false, ESuppressionWeapon::Acid);
	}
}

void AEnemyCharacter::TickRangedVsVehicle(AVehiclePawn& Vehicle, float DeltaTime, float RangeCm, float Damage, float CooldownSeconds,
	float MuzzleHeightCm, float HitChance)
{
	UWorld* World = GetWorld();
	AAIController* AIC = Cast<AAIController>(GetController());
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	const FVector HullPoint = Vehicle.GetClosestHullPoint(Feet);
	const float Distance = Vehicle.GetHullDistance2D(Feet);
	const FVector Aim = FVector(FVector2D(HullPoint), Vehicle.GetHullCentreWorld().Z);
	FaceYaw((Aim - GetActorLocation()).Rotation().Yaw, DeltaTime, 8.f);
	// Suppressive fire (2026-10-26): the APC's MG suppresses him too - no fire, he holds his spot.
	if (IsHeldBySuppression())
	{
		SuppressionHoldSeconds += DeltaTime;
		if (AIC)
		{
			AIC->StopMovement();
		}
		return;
	}
	// The line: the hull must be the first thing the shot meets (walls / rocks in between block it; other enemies do not).
	const FVector Start = Feet + FVector(0.f, 0.f, MuzzleHeightCm);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyVehicleLine), false, this);
	for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
	{
		Params.AddIgnoredActor(Enemy);
	}
	FHitResult Hit;
	const bool bLine = !World->LineTraceSingleByChannel(Hit, Start, Vehicle.GetHullCentreWorld(), ECC_Visibility, Params) || Hit.GetActor() == &Vehicle;
	if (AIC)
	{
		if (!bLine || Distance > RangeCm * 0.9f)
		{
			RequestAIMoveToLocation(AIC, Vehicle.GetHullAttackPoint(Feet, FMath::Min(RangeCm * 0.5f, 600.f)), 100.f);
		}
		else
		{
			AIC->StopMovement();
		}
	}
	if (!bLine || Distance > RangeCm || AttackTimer > 0.f)
	{
		return;
	}
	AttackTimer = CooldownSeconds;
	NotifySquadAttacked(&Vehicle);
	OnAttackStarted(&Vehicle);
	if (UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>())
	{
		Sight->NotifyFired(this);
	}
	if (UEnemyAnimInstance* Anim = GetMesh() ? Cast<UEnemyAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr)
	{
		Anim->NotifyAttack();
	}
	const bool bHit = FMath::FRand() < HitChance;
	if (bHit)
	{
		Vehicle.ApplyHullDamage(Damage, VehicleHullRules::ClassifyEnemyAttack(Archetype), EnemyDisplayName, this);
	}
	if (UCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UCombatFeedbackSubsystem>())
	{
		const FVector End = bHit ? (Hit.bBlockingHit ? Hit.ImpactPoint : Aim) : Aim + FVector(FMath::FRandRange(-150.f, 150.f), FMath::FRandRange(-150.f, 150.f), 120.f);
		Feedback->SpawnTracer(Start, End, Archetype == EEnemyArchetype::Marksman ? FLinearColor(1.f, 0.85f, 0.3f) : FLinearColor(1.f, 0.2f, 0.2f));
	}
}

void AEnemyCharacter::StartAttackAnimation(AActor* Target)
{
	UEnemyAnimInstance* Anim = GetMesh() ? Cast<UEnemyAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	const float ClipSeconds = Anim ? Anim->NotifyAttack() : 0.f;
	AttackLockTimer = ClipSeconds;
	AttackLockTarget = Target;
	AttackTimer = FMath::Max(AttackTimer, ClipSeconds);
}

void AEnemyCharacter::AttackTarget(AActor* Target)
{
	if (!Target || bIsDying)
	{
		return;
	}
	if (UTacticalSightSubsystem* Sight = GetWorld() ? GetWorld()->GetSubsystem<UTacticalSightSubsystem>() : nullptr)
	{
		Sight->NotifyFired(this); // a blow gives it away (Sprint 08-E)
	}

	NotifySquadAttacked(Target); // before the blow: a Defensive operative hit by it is already in the fight
	AttackTimer = AttackCooldown;
	StartAttackAnimation(Target);
	OnAttackStarted(Target);
	// A world-less enemy (CodexTactics.Combat.Enemy.AttackDealsDamageToTarget) has no pack tactics.
	if (UEnemyTacticsSubsystem* Tactics = GetWorld() ? GetWorld()->GetSubsystem<UEnemyTacticsSubsystem>() : nullptr)
	{
		if (Target->IsA<AOperativeCharacter>() && EnemyTacticsRules::IsBehind(Target->GetActorLocation(), Target->GetActorForwardVector(), GetActorLocation()))
		{
			Tactics->NotifyBackstab();
		}
	}

	const bool bIsCrit = (FMath::FRand() < CritChance);
	const float FinalDamage = AttackDamage * (bIsCrit ? CritMultiplier : 1.0f);

	FDamageSpec Spec;
	Spec.Amount = FinalDamage;
	Spec.DamageType = EDamageType::Kinetic;
	Spec.AttackerSource = EnemyDisplayName;

	// Godot _attack_target -> player.gd take_damage (dodge, stance, fortitude) for operatives.
	if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Target))
	{
		const float Dealt = Operative->TakeHit(FinalDamage, EnemyDisplayName, bIsCrit, false, this, bIsCrit ? CritMultiplier : 1.f);
		// One line per landed blow / bite (melee balance read-outs, e.g. the close-in A/B of 2026-10-18), with the side it came
		// from: the angle between his facing and the attacker (front < 60 < side < 120 < back; pack flanking 2026-10-18).
		const FVector ToAttacker = (GetActorLocation() - Operative->GetActorLocation()).GetSafeNormal2D();
		const float FromAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(
			Operative->GetActorForwardVector().GetSafeNormal2D(), ToAttacker)), -1.f, 1.f)));
		UE_LOG(LogCodexTactics, Display, TEXT("[Melee] %s hits %s for %.1f%s from the %s (%.0f deg)"), *GetName(), *Operative->DisplayName.ToString(), Dealt,
			bIsCrit ? TEXT(" (crit)") : TEXT(""), FromAngle < 60.f ? TEXT("front") : (FromAngle <= 120.f ? TEXT("side") : TEXT("back")), FromAngle);
		TryKnockDownOperative(Operative, Dealt); // Sprint 14: Brute heavy melee / hound pounce
		// Godot enemy_frostbitten.gd _attack_target: the blow shoves the operative 2.5 m/s away (velocity added).
		FVector Push = Operative->GetActorLocation() - GetActorLocation();
		Push.Z = 0.f;
		if (Archetype == EEnemyArchetype::Frostbitten && Push.SizeSquared() > 0.001f * 10000.f)
		{
			Operative->LaunchCharacter(Push.GetSafeNormal() * 250.f, false, false);
		}
	}
	else if (UHealthComponent* TargetHealth = Target->FindComponentByClass<UHealthComponent>())
	{
		TargetHealth->ApplyDamage(Spec, this);
	}
}

bool AEnemyCharacter::IsHeldBySuppression() const
{
	// Only the shooters fire back; the melee archetypes are not held (docs/design/SUPPRESSION.md, open question).
	const bool bRanged = Archetype == EEnemyArchetype::Spitter || Archetype == EEnemyArchetype::CryoDrone || Archetype == EEnemyArchetype::Marksman;
	return bRanged && !bIsDying && !USuppressionSubsystem::MayEnemyFire(this);
}

void AEnemyCharacter::ReportRoundAtSquad(const FVector& Start, const FVector& End, const AActor* Target, bool bHit, bool bStruckCover,
	ESuppressionWeapon Weapon)
{
	ShotsWhileSuppressed += IsHeldBySuppression() ? 1 : 0;
	FSuppressionRound Round;
	Round.Shooter = this;
	Round.bAtEnemies = false;
	Round.Start = Start;
	Round.End = End;
	Round.Target = Target;
	Round.bHitTarget = bHit;
	Round.bStruckCover = bStruckCover;
	Round.Weapon = Weapon;
	// The enemy-AI hook: a deliberate "suppress target" intent on this target weighs more.
	const USuppressionSubsystem* Suppression = USuppressionSubsystem::Get(this);
	Round.bDeliberate = Suppression && Target && Suppression->GetSuppressIntent(this) == Target;
	USuppressionSubsystem::Report(this, Round);
}

void AEnemyCharacter::NotifySquadAttacked(AActor* Target)
{
	if (Target && (Target->IsA<AOperativeCharacter>() || Target->IsA<AVehiclePawn>()) && !bIsDying) // APC P4: an attack on the manned hull too
	{
		ULevelEncounterSubsystem::NotifyHostileContactIn(GetWorld(), EAmbushTrigger::EnemyAttack, this);
	}
}

bool AEnemyCharacter::StartJumpAttack(AActor* Target)
{
	if (bIsDying || IsJumpAttacking() || !IsValid(Target))
	{
		return false;
	}
	NotifySquadAttacked(Target);
	JumpTarget = Target;
	JumpPhase = ECutterJumpPhase::Windup;
	JumpPhaseTimer = 0.f;
	bJumpDamageDealt = false;
	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		AIC->StopMovement();
	}
	OnJumpAttackStarted();
	return true;
}

void AEnemyCharacter::TickJumpAttack(float DeltaTime)
{
	JumpPhaseTimer += DeltaTime;
	AActor* Target = JumpTarget.Get();
	switch (JumpPhase)
	{
	case ECutterJumpPhase::Windup:
	{
		if (Target)
		{
			FaceYaw((Target->GetActorLocation() - GetActorLocation()).Rotation().Yaw, DeltaTime, 24.f);
		}
		if (JumpPhaseTimer < 0.4f / JumpAttackSpeed) // Godot JUMP_ORIG_WINDUP_TIME
		{
			return;
		}
		// Godot _launch_jump_flight: a ballistic arc landing on the target within 1.3 s / speed, at most JumpMaxDistance.
		JumpPhase = ECutterJumpPhase::Airborne;
		JumpPhaseTimer = 0.f;
		JumpFlightDuration = 1.3f / JumpAttackSpeed;
		const FVector Start = GetActorLocation();
		FVector Landing = Target ? GodotPosition(Target) + FVector(0.f, 0.f, GetSimpleCollisionHalfHeight()) : Start + GetActorForwardVector() * 500.f;
		FVector Horizontal(Landing.X - Start.X, Landing.Y - Start.Y, 0.f);
		if (Horizontal.Size() > JumpMaxDistance)
		{
			Horizontal = Horizontal.GetSafeNormal() * JumpMaxDistance;
		}
		const float Gravity = -GetCharacterMovement()->GetGravityZ();
		const float DeltaZ = Landing.Z - Start.Z;
		const FVector Velocity = Horizontal / FMath::Max(0.05f, JumpFlightDuration)
			+ FVector(0.f, 0.f, (DeltaZ + 0.5f * Gravity * JumpFlightDuration * JumpFlightDuration) / FMath::Max(0.05f, JumpFlightDuration));
		LaunchCharacter(Velocity, true, true);
		return;
	}
	case ECutterJumpPhase::Airborne:
		// Impact at the end of the flight, or on touching the ground in its second half.
		if (JumpPhaseTimer >= JumpFlightDuration || (JumpPhaseTimer >= JumpFlightDuration * 0.7f && !GetCharacterMovement()->IsFalling()))
		{
			JumpPhase = ECutterJumpPhase::Impact;
			JumpPhaseTimer = 0.f;
			GetCharacterMovement()->StopMovementImmediately();
			if (!bJumpDamageDealt)
			{
				bJumpDamageDealt = true;
				ApplyJumpImpactDamage();
			}
			OnJumpAttackImpact();
		}
		return;
	case ECutterJumpPhase::Impact:
		if (JumpPhaseTimer >= 0.75f / JumpAttackSpeed) // Godot JUMP_ORIG_RECOVERY_TIME
		{
			JumpPhase = ECutterJumpPhase::None;
			JumpPhaseTimer = 0.f;
			JumpCooldownTimer = JumpCooldown;
			AttackTimer = AttackCooldown;
		}
		return;
	default:
		return;
	}
}

void AEnemyCharacter::ApplyJumpImpactDamage()
{
	UWorld* World = GetWorld();
	const bool bIsCrit = FMath::FRand() < CritChance;
	const float Damage = JumpAttackDamage * (bIsCrit ? CritMultiplier : 1.f);
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	bool bHitAny = false;
	if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			const FVector Position = GodotPosition(Member);
			if (!VehicleQueries::IsMounted(Member) && FVector::Dist(Feet, Position) <= JumpDamageRadius && FMath::Abs(Feet.Z - Position.Z) <= 150.f)
			{
				// Sprint 14: the pounce landing knocks him down (dodged: no hit, no fall).
				if (Member->TakeHit(Damage, EnemyDisplayName, bIsCrit, false, this) > 0.f && Member->KnockdownComponent)
				{
					Member->KnockdownComponent->TryKnockDown(EKnockdownCause::Pounce, GetActorLocation());
				}
				bHitAny = true;
			}
		}
	}
	for (TActorIterator<ABarricadeActor> It(World); It; ++It)
	{
		UHealthComponent* BarricadeHealth = It->FindComponentByClass<UHealthComponent>();
		if (BarricadeHealth && BarricadeHealth->IsAlive() && FVector::Dist(Feet, GodotPosition(*It)) <= JumpDamageRadius)
		{
			BarricadeHealth->ApplyDirectHealthLoss(Damage, EnemyDisplayName);
			bHitAny = true;
		}
	}
	if (bHitAny)
	{
		UFloatingTextSubsystem::SpawnAboveEnemy(this, FString::Printf(TEXT("💥 POUNCE %d"), FMath::FloorToInt(Damage)), FLinearColor(1.f, 0.35f, 0.1f));
	}
}
