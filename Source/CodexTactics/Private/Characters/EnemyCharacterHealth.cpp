// AEnemyCharacter: knockdown hooks, incoming damage, the trap blast scope, death, the airborne-death crash landing.
// Split out of EnemyCharacter.cpp verbatim (cleanup 2026-10-19; map: docs/port/REFACTOR_ENEMY_2026-10.md).
// Godot reference: Scenes/movements/enemy_base.gd (take_damage, _die), enemy_cutter.gd; knockdown Sprint 14 (UE-only).

#include "Characters/EnemyCharacter.h"
#include "Combat/KnockdownComponent.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "Characters/EnemyTacticsSubsystem.h"
#include "UI/FloatingTextSubsystem.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/EnemyAnimInstance.h"
#include "Combat/WaveVictorySubsystem.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Core/PlayerCommands.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

bool AEnemyCharacter::IsKnockedDown() const
{
	return KnockdownComponent && KnockdownComponent->IsDown();
}

void AEnemyCharacter::HandleKnockdownPhase(EKnockdownPhase NewPhase, EKnockdownPhase OldPhase)
{
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (OldPhase == EKnockdownPhase::None && NewPhase != EKnockdownPhase::None)
	{
		if (AController* C = GetController())
		{
			C->StopMovement();
		}
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
		KnockdownSavedPawnResponse = Capsule->GetCollisionResponseToChannel(ECC_Pawn);
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		UFloatingTextSubsystem::SpawnAboveEnemy(this, TEXT("KNOCKED DOWN"), FLinearColor(1.f, 0.7f, 0.2f));
	}
	else if (NewPhase == EKnockdownPhase::None && OldPhase != EKnockdownPhase::None && !bIsDying)
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, KnockdownSavedPawnResponse);
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
}

void AEnemyCharacter::HandleDamaged(const FDamageSpec& Spec, float FinalDamage)
{
	if (UEnemyAnimInstance* Anim = GetMesh() ? Cast<UEnemyAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr)
	{
		Anim->NotifyHit();
	}
	// Sprint 14: a single hit of >= 40 HP knocks a Frostbitten down (a Brute only when it is critical); blasts go through
	// UKnockdownComponent::NotifyExplosion. The shooter is unknown here: the closest operative gives the direction.
	if (KnockdownComponent && HealthComponent && HealthComponent->IsAlive() && Spec.DamageType != EDamageType::Explosive)
	{
		const AActor* Closest = FindClosestSquadMember();
		KnockdownComponent->TryKnockDown(EKnockdownCause::HeavyHit, Closest ? Closest->GetActorLocation() : GetActorLocation() + GetActorForwardVector() * 100.f,
			FinalDamage, Spec.bIsCritical);
	}
	OnHitReaction(FinalDamage);
	if (UCombatFeedbackSubsystem* Feedback = GetWorld() ? GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>() : nullptr)
	{
		Feedback->FlashEnemyHit(this); // Godot _flash_hit
	}
	if (!Spec.AttackerSource.IsEmpty())
	{
		LastAttackerSource = Spec.AttackerSource;
	}
	// User request 2026-10-06: the squad's direct attack (shot, blow, thrown grenade) reveals it — on an ambush level the
	// fight starts now. A trap / placed charge (ApplyBlast inside FScopedTrapBlast) only sends a patrol searching.
	const bool bTrapDamage = IsTrapBlastInProgress();
	if (!bTrapDamage)
	{
		ULevelEncounterSubsystem::NotifyHostileContactIn(GetWorld(), EAmbushTrigger::EnemyDamaged, this);
	}
	// Sprint 11: a hit breaks the patrol (its leader / escorts too). The shooter is unknown here: the alarm points at
	// the closest operative (the shot was heard from there).
	if (IsOnPatrol())
	{
		FPatrolAlertInput Input;
		Input.bTookDamage = !bTrapDamage;
		Input.bTookTrapDamage = bTrapDamage;
		switch (PatrolRouteRules::EvaluateAlert(Input))
		{
		case EPatrolReaction::Engage:
		{
			const AActor* Closest = FindClosestSquadMember();
			BreakPatrol(EPatrolAlertCause::Damage, Closest ? Closest->GetActorLocation() : GetActorLocation());
			break;
		}
		case EPatrolReaction::Search:
			StartPatrolSearch(GetActorLocation());
			break;
		default:
			break;
		}
	}
}

int32& AEnemyCharacter::TrapBlastDepth()
{
	static int32 Depth = 0;
	return Depth;
}

void AEnemyCharacter::HandleDied(AActor* Victim, const FString& AttackerSource)
{
	if (bIsDying)
	{
		return;
	}

	bIsDying = true;
	Tags.Remove(FName(TEXT("Enemy")));
	if (UEnemyTacticsSubsystem* Tactics = GetWorld()->GetSubsystem<UEnemyTacticsSubsystem>())
	{
		Tactics->NotifyEnemyDied(GetActorLocation()); // the pack mates' morale
	}

	// Godot enemy_cutter.gd _start_airborne_death: shot down mid-leap it keeps flying and crashes on the ground.
	if (Archetype == EEnemyArchetype::Cutter && (IsJumpAttacking() || GetCharacterMovement()->IsFalling()))
	{
		bAirborneDeath = true;
		JumpPhase = ECutterJumpPhase::None;
		GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
		GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		UFloatingTextSubsystem::SpawnAboveEnemy(this, TEXT("💀 SHOT DOWN MID-AIR"), FLinearColor(1.f, 0.25f, 0.25f));
	}
	else
	{
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (AController* C = GetController())
	{
		C->StopMovement();
	}

	// Godot enemy_base.gd / enemy_cutter.gd death: the kill goes into the squad statistics (last attacker) ...
	if (UWaveVictorySubsystem* Victory = GetWorld() ? GetWorld()->GetSubsystem<UWaveVictorySubsystem>() : nullptr)
	{
		Victory->RegisterEnemyKill(Archetype, LastAttackerSource.IsEmpty() ? AttackerSource : LastAttackerSource);
	}
	// ... and every squad member gets the kill EXP.
	if (USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr)
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->AddExp(KillExpReward);
		}
	}

	// 2026-10-21 (the tutorial's close-shot lesson): who killed it - an operative's direct attack, a trap blast or something else.
	{
		const FString Killer = AttackerSource.IsEmpty() ? LastAttackerSource : AttackerSource; // this blow first, the last hit else
		FGameplayEventContext Context;
		Context.Subject = this;
		Context.Location = GetActorLocation();
		Context.Kind = IsTrapBlastInProgress() ? FName(TEXT("Trap")) : FName(TEXT("Other"));
		if (Context.Kind != FName(TEXT("Trap")))
		{
			if (const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr)
			{
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					if (Member && !Killer.IsEmpty() && Member->DisplayName.ToString() == Killer)
					{
						Context.Target = Member;
						Context.Kind = FName(TEXT("Squad"));
						break;
					}
				}
			}
		}
		GameplayEvents::RaiseWithContext(this, FName(*(GameplayEvents::EnemyKilled.ToString() + TEXT(".") + Context.Kind.ToString())), Context);
		GameplayEvents::RaiseWithContext(this, GameplayEvents::EnemyKilled, Context);
	}
	OnEnemyDied.Broadcast(this);
	OnEnemyDiedNative.Broadcast(this);
	OnDeath();

	// Godot enemy_base.gd _die: with a death clip the body stays death_decay_delay seconds, otherwise it goes quickly.
	float LifeSpan = 2.0f;
	if (UEnemyAnimInstance* Anim = GetMesh() ? Cast<UEnemyAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr)
	{
		Anim->NotifyDeath();
		LifeSpan = Anim->HasDeathAnimation() ? Anim->DeathDecayDelay : LifeSpan;
	}
	SetLifeSpan(LifeSpan);
}

void AEnemyCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	if (bAirborneDeath)
	{
		// Godot _process_airborne_death: crash landing.
		bAirborneDeath = false;
		GetCharacterMovement()->StopMovementImmediately();
		UFloatingTextSubsystem::SpawnAboveEnemy(this, TEXT("💥 CRASH"), FLinearColor(0.9f, 0.5f, 0.2f));
	}
}
