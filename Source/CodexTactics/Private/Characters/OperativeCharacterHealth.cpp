// AOperativeCharacter: Incoming hits, knockdown hooks, death, corpse and remains.
// Split out of OperativeCharacter.cpp verbatim (cleanup day 2026-10-14; map: docs/port/REFACTOR_OPERATIVE_2026-10.md).
// Godot reference: Scenes/movements/player.gd (take_damage, die, corpse_loot). Knockdown: Sprint 14 (UE-only).

#include "Characters/OperativeCharacter.h"

#include "CodexTactics.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacterInternal.h"
#include "Characters/OperativeHitRules.h"
#include "Characters/PanicComponent.h"
#include "Characters/RageComponent.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/DeathCinematicSubsystem.h"
#include "Combat/GrenadeSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Combat/KnockdownRules.h"
#include "Characters/EnemyCharacter.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MissionRules.h"
#include "Core/MissionSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "Subsystems/CodexEventBus.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Tactics/CoverDecisionRules.h"
#include "Tactics/CoverRules.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "Vehicles/VehicleSubsystem.h"

void AOperativeCharacter::HandleDied(AActor* Victim, const FString& AttackerSource)
{
	if (bInCover)
	{
		LeaveCover(TEXT("died"));
	}
	bHasPendingCover = false;
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnSoldierDowned.Broadcast(this); // Godot EventBus.soldier_downed
	}
	if (RageComponent)
	{
		RageComponent->ExitRage(TEXT("Killed"));
	}
	if (PanicComponent)
	{
		PanicComponent->RecoverFromPanic(TEXT("Killed"), true);
	}
	// User decision 2026-10-08 (replaces Godot _check_squad_vital_signs "any member down = mission failed"): only the
	// COMMANDER's death loses the mission. Everybody else is a permanent loss (Godot _handle_expendable_member_death for
	// the recruit, now for all): he leaves the squad (the leader passes on), stays down as a corpse and his body can be
	// searched for his supplies (Godot corpse_loot). Either way the death cinematic shows him fall first.
	bKilledInAction = true;
	BecomeCorpse();
	USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	const bool bSquadMember = Squad && Squad->GetMembers().Contains(this); // a recruit not yet rescued is not one
	if (Squad)
	{
		Squad->UnregisterOperative(this);
	}
	if (UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>())
	{
		TurnBased->NotifyOperativeKilled(this);
	}
	int32 Living = 0;
	for (const AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		Living += Member && Member != this && Member->HealthComponent && Member->HealthComponent->IsAlive() ? 1 : 0;
	}
	const bool bDefeat = bSquadMember && MissionRules::ShouldFailMission(SquadRole == EOperativeRole::Commander, Living);
	UE_LOG(LogCodexTactics, Display, TEXT("%s killed in action (%s, %d squad members alive)%s"), *DisplayName.ToString(),
		*UEnum::GetValueAsString(SquadRole), Living, bDefeat ? TEXT(" -> mission failed after the death cinematic") : TEXT(""));
	if (!bDefeat)
	{
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(FText::FromString(TEXT("HQ")), FText::FromString(TEXT("⚠️ ") + MissionRules::GetMemberLostRadio(DisplayName).ToString()));
		}
		SpawnRemainsLoot();
	}
	if (UDeathCinematicSubsystem* DeathCam = GetWorld()->GetSubsystem<UDeathCinematicSubsystem>())
	{
		DeathCam->NotifyOperativeDied(this, bDefeat);
	}
	else if (bDefeat)
	{
		if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
		{
			Mission->TriggerMissionFailed(this);
		}
	}
}

void AOperativeCharacter::RestoreKilledInAction()
{
	bKilledInAction = true;
	BecomeCorpse();
	if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		Squad->UnregisterOperative(this);
	}
	if (UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>())
	{
		TurnBased->NotifyOperativeKilled(this);
	}
	if (KnockdownComponent)
	{
		KnockdownComponent->PlayDeathFall(GetActorLocation() + GetActorForwardVector() * 100.f); // on his back, held
	}
}

bool AOperativeCharacter::GetLastHitSource(FVector& OutLocation) const
{
	OutLocation = LastHitSource;
	return bHasLastHitSource;
}

void AOperativeCharacter::BecomeCorpse()
{
	// The body stays where he fell: no walking, the squad / enemies step over him, clicks go to his remains, never to him.
	StopOperative();
	if (bCornerAimActive)
	{
		EndCornerAim(ECornerAimDecision::DuckForSafety, TEXT("died"));
	}
	bIsReloading = false;
	ReloadTimer = 0.f;
	SetSprinting(false);
	if (UGrenadeSubsystem* Grenades = GetWorld()->GetSubsystem<UGrenadeSubsystem>(); Grenades && Grenades->GetThrower() == this)
	{
		Grenades->CancelAim();
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}
	if (BodyMesh)
	{
		BodyMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	}
	SetGroupSelected(false, false);
	UpdateSelectionRing();
}

void AOperativeCharacter::SpawnRemainsLoot()
{
	// Godot corpse_loot / player.gd get_items_list: ammo, medkits, food (grenades / the weapon have no loot stack here).
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	ALootCrateActor* Remains = GetWorld()->SpawnActor<ALootCrateActor>(Feet + FVector(0.f, 0.f, 40.f), GetActorRotation(), Params);
	if (!Remains)
	{
		return;
	}
	Remains->CrateName = FText::FromString(FString::Printf(TEXT("Remains: %s"), *DisplayName.ToString()));
	FLootContents Contents;
	auto Ammo = [this](const TCHAR* Id)
	{
		const FWeaponAmmoState* State = AmmoInventory.Find(Id);
		return State ? State->Clip + FMath::Max(0, State->Reserve) : 0;
	};
	Contents.Medkits = MedkitsCount;
	Contents.CannedFood = CannedFoodCount;
	Contents.Bread = BreadCount;
	Contents.Chocolate = ChocolateCount;
	Contents.Matches = 0;
	Contents.RifleAmmo = Ammo(TEXT("m16"));
	Contents.PistolAmmo = Ammo(TEXT("pistol"));
	Contents.ShotgunAmmo = 0;
	Contents.FlameFuel = 0;
	Contents.CryoAmmo = 0;
	Contents.PlasmaAmmo = 0;
	Remains->Contents = Contents;
	Remains->OpenSeconds = 0.6f; // searching a body, no lid
	Remains->SetActorHiddenInGame(true); // the body is what the player sees and clicks; the crate is its hit box
	Remains->Tags.Add(TEXT("CorpseLoot"));
}

float AOperativeCharacter::TakeHit(float Amount, const FString& Attacker, bool bCrit, bool bBypassAvoidance, AActor* AttackerActor,
	float CritMultiplierApplied)
{
	return ResolveIncomingHit(bBypassAvoidance ? EOperativeHitKind::Unavoidable : EOperativeHitKind::Attack, Amount, Attacker, bCrit,
		AttackerActor, CritMultiplierApplied);
}

float AOperativeCharacter::TakeGridBlast(float Amount, const FString& Source)
{
	return ResolveIncomingHit(EOperativeHitKind::GridBlast, Amount, Source, false, nullptr, 1.f);
}

float AOperativeCharacter::ResolveIncomingHit(EOperativeHitKind Kind, float Amount, const FString& Attacker, bool bCrit, AActor* AttackerActor,
	float CritMultiplierApplied)
{
	if (!HealthComponent || !HealthComponent->IsAlive() || VehicleQueries::IsMounted(this)) // APC P1: the crew inside takes no hits
	{
		return 0.f;
	}
	LastAttackedTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0; // a threat: the weather idle poses end (UWarmIdleSubsystem)
	// Fire posture: an attack (dodged or not) provokes a Defensive operative to fight back (user request 2026-10-06).
	bProvokedThisFight = true;
	if (USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr)
	{
		Squad->NotifyMemberAttacked(this);
	}
	// 1. Dodge on luck (Godot: luck 25 -> 10 %).
	const float DodgeRoll = ForcedDodgeRollForTesting >= 0.f ? (ForcedDodgeRollForTesting >= 1.f ? -1.f : 101.f) : FMath::FRand() * 100.f;
	ForcedDodgeRollForTesting = -1.f;
	if (OperativeHitRules::IsDodged(Kind, DodgeRoll, Luck))
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("💨 DODGE!"), FLinearColor(0.3f, 0.9f, 1.f));
		return 0.f;
	}
	// 2. Stance defense and fortitude cut (Godot: 15 fortitude = 22.5 %, at most 50 %), each once per kind (OperativeHitRules).
	const float Fortitude = ColdSurvival ? ColdSurvival->Fortitude : 15.f;
	// Sprint 12 cover (user decision 2026-10-06): a hit from the wall's frontal arc is absorbed 90 % by high cover (35 %
	// crouched / 90 % prone behind low cover), flanking hits pass fully; while the head stays down no hit is a headshot
	// (crit). Blasts / traps (bBypassAvoidance) are not stopped by a wall at the back.
	float CoverAbsorb = 0.f;
	if (bInCover && OperativeHitRules::UsesCover(Kind) && AttackerActor)
	{
		const FCoverCombatConfig& CoverConfig = CoverRules::GetConfig();
		const bool bInArc = CoverRules::IsInFrontalArc(CoverSlot.WallNormal, CoverSlot.WorldLocation, AttackerActor->GetActorLocation(), CoverConfig.FrontalArcDeg);
		CoverAbsorb = CoverRules::AbsorbFraction(CoverConfig, CurrentCoverHeight, Stance, bIsCornerLeaning, bInArc);
		if (bCrit && CoverRules::IsHeadshotImmune(CurrentCoverHeight, bIsCornerLeaning))
		{
			bCrit = false; // the head never showed: the plain hit
			Amount /= FMath::Max(CritMultiplierApplied, 1.f);
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("🧱 HEAD DOWN"), FLinearColor(0.6f, 0.85f, 1.f));
		}
		if (CoverAbsorb > 0.f)
		{
			UFloatingTextSubsystem::SpawnAboveOperative(this, FString::Printf(TEXT("🧱 COVER -%d%%"), FMath::RoundToInt(CoverAbsorb * 100.f)),
				FLinearColor(0.5f, 0.8f, 1.f));
		}
	}
	// Lying after a knockdown: any blow (shot, bite, blast, trap; real time and the turn-based grid hits, which come in
	// with bBypassAvoidance) lands x1.33 (user decision 2026-10-11, KnockdownRules::DownedDamageMultiplier). The
	// bypass path keeps whole numbers (grid damage is integer).
	const float KnockdownScale = KnockdownComponent ? KnockdownComponent->GetDamageMultiplier() : 1.f;
	const float Final = OperativeHitRules::FinalDamage(Kind, CoverRules::ApplyAbsorb(Amount, CoverAbsorb), HealthComponent->GetDefenseMultiplier(),
		Fortitude, KnockdownScale);
	RecentIncomingDamage += Final;
	bHasLastHitSource = true; // before the health loss: the hit reaction / death fall read it
	LastHitSource = AttackerActor ? AttackerActor->GetActorLocation() : GetActorLocation() + GetActorForwardVector() * 100.f;
	if (OperativeCharacterInternal::IsRangedEnemyActor(AttackerActor))
	{
		RecentRangedDamage += Final; // the corner-aim duck counts only what the corner protects from (2026-10-07 horde fix)
	}
	HealthComponent->ApplyDirectHealthLoss(Final, Attacker);
	if (UCombatFeedbackSubsystem* Feedback = GetWorld() ? GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>() : nullptr)
	{
		Feedback->SpawnDamageFlash(this); // Godot _spawn_damage_flash
	}
	// Godot take_damage: a hit makes the carrier drop what the squad is carrying.
	if (bCarrying)
	{
		if (URelocationSubsystem* Relocation = GetWorld() ? GetWorld()->GetSubsystem<URelocationSubsystem>() : nullptr)
		{
			Relocation->DropAllForCombat();
		}
		SetCarrying(false);
	}
	UFloatingTextSubsystem::SpawnAboveOperative(this, bCrit ? FString::Printf(TEXT("💥 CRITICAL HIT! -%d"), FMath::FloorToInt(Final))
		: FString::Printf(TEXT("-%d"), FMath::FloorToInt(Final)), bCrit ? FLinearColor(1.f, 0.25f, 0.1f) : FLinearColor(1.f, 0.3f, 0.3f));
	// Godot rage_comp.on_incoming_hit(attacker_node, is_crit, final_incoming).
	if (RageComponent && HealthComponent->IsAlive())
	{
		RageComponent->OnIncomingHit(AttackerActor, bCrit);
	}
	// Godot panic_comp.on_damage_taken(final_incoming).
	if (PanicComponent && HealthComponent->IsAlive())
	{
		PanicComponent->OnDamageTaken(Final);
	}
	// Sprint 14: a single hit of >= 40 HP knocks him down (blasts / traps go through UKnockdownComponent::NotifyExplosion).
	if (KnockdownComponent && HealthComponent->IsAlive() && OperativeHitRules::AllowsHeavyHitKnockdown(Kind))
	{
		const FVector Source = AttackerActor ? AttackerActor->GetActorLocation() : GetActorLocation() + GetActorForwardVector() * 100.f;
		KnockdownComponent->TryKnockDown(EKnockdownCause::HeavyHit, Source, Final, bCrit);
	}
	return Final;
}

bool AOperativeCharacter::IsKnockedDown() const
{
	return KnockdownComponent && KnockdownComponent->IsDown();
}

void AOperativeCharacter::HandleKnockdownPhase(EKnockdownPhase NewPhase, EKnockdownPhase OldPhase)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (OldPhase == EKnockdownPhase::None && NewPhase != EKnockdownPhase::None)
	{
		// Interrupts: the walk, sprint, reload, the corner aim / cover (left cleanly), a grenade being aimed.
		// The cover slot is remembered: a blast knockdown sends him back there once he is up (user decision 2026-10-18).
		bKnockedFromCover = bInCover;
		KnockdownCause = KnockdownComponent ? KnockdownComponent->GetState().Cause : EKnockdownCause::None; // reset by the recovery
		if (bInCover)
		{
			KnockdownCoverSlot = CoverSlot;
		}
		StopOperative();
		if (bCornerAimActive)
		{
			EndCornerAim(ECornerAimDecision::DuckForSafety, TEXT("knocked down"));
		}
		if (bInCover)
		{
			LeaveCover(TEXT("knocked down"));
		}
		bHasPendingCover = false;
		bIsReloading = false;
		ReloadTimer = 0.f;
		if (Stance == EOperativeStance::Crouching)
		{
			Stance = EOperativeStance::Standing; // the get-up clip ends standing
			ApplyStanceCapsule();
		}
		if (UGrenadeSubsystem* Grenades = GetWorld() ? GetWorld()->GetSubsystem<UGrenadeSubsystem>() : nullptr; Grenades && Grenades->GetThrower() == this)
		{
			Grenades->CancelAim();
		}
		if (UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr)
		{
			Anim->StopSlotAnimation(0.1f, Anim->UpperBodySlot); // fire / reload / hit / throw clips over the fall
		}
		// Lying on the ground: no walking, the squad and the enemies step over him.
		if (Movement)
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
		if (Capsule)
		{
			KnockdownSavedPawnResponse = Capsule->GetCollisionResponseToChannel(ECC_Pawn);
			Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		}
	}
	else if (NewPhase == EKnockdownPhase::None && OldPhase != EKnockdownPhase::None)
	{
		if (Capsule)
		{
			Capsule->SetCollisionResponseToChannel(ECC_Pawn, KnockdownSavedPawnResponse);
		}
		if (Movement && HealthComponent && HealthComponent->IsAlive())
		{
			Movement->SetMovementMode(MOVE_Walking);
			ApplyMovementParams();
		}
		ReturnToCoverAfterKnockdown();
	}
}

void AOperativeCharacter::ReturnToCoverAfterKnockdown()
{
	// User rule 2026-10-18: a blast (grenade, barrel, mine) that just happened to floor him in cover sends him back to the same slot;
	// surrounded (a pounce / a Brute's blow, or enemies close now) he stays out and the player re-orders; an order given while he
	// lay wins (the knockdown component runs it right after this).
	const FKnockdownConfig& Config = KnockdownRules::GetConfig();
	FKnockdownCoverReturnInput Input;
	Input.bWasInCover = bKnockedFromCover;
	Input.Cause = KnockdownCause;
	Input.bOrderGivenWhileDown = KnockdownComponent && KnockdownComponent->HasBufferedOrder();
	Input.bAlive = HealthComponent && HealthComponent->IsAlive();
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	Input.bTurnBased = Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased;
	for (const AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(GetWorld()))
	{
		if (Enemy && !Enemy->IsDying() && Enemy->GetHealthComponent() && Enemy->GetHealthComponent()->IsAlive()
			&& FVector::Dist2D(Enemy->GetActorLocation(), GetActorLocation()) <= Config.CoverReturnThreatRadiusCm)
		{
			++Input.EnemiesClose;
		}
	}
	LastCoverReturn = KnockdownRules::DecideCoverReturn(Input, Config);
	bKnockedFromCover = false;
	if (Input.bWasInCover)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Knockdown] %s up again: %s (%d enemies within %.0f cm)"), *DisplayName.ToString(),
			KnockdownRules::CoverReturnName(LastCoverReturn), Input.EnemiesClose, Config.CoverReturnThreatRadiusCm);
	}
	if (LastCoverReturn == EKnockdownCoverReturn::Return && OrderTakeCover(KnockdownCoverSlot, false) != EOperativeOrderResult::Accepted)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Knockdown] %s: the way back to his cover is refused"), *DisplayName.ToString());
	}
}
