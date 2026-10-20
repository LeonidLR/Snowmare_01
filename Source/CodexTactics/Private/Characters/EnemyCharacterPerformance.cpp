// AEnemyCharacter: animation extras and look variants (game designer 2026-10-20, Military Zombie; UE-only, no Godot reference) -
// the per-instance variant seed, the look variants (mesh x material set), the spawn entrance and the alert roar with their
// stand-still lock. Pure rules: Characters/EnemyAnimRules; the clips: UEnemyAnimInstance (empty clip lists = no change for the
// other enemies). Layout: docs/port/REFACTOR_ENEMY_2026-10.md.

#include "Characters/EnemyCharacter.h"
#include "AIController.h"
#include "Characters/EnemyAnimInstance.h"
#include "Characters/EnemyAnimRules.h"
#include "CodexTactics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"

uint32 AEnemyCharacter::GetVariantSeed() const
{
	if (!CachedVariantSeed.IsSet())
	{
		CachedVariantSeed = EnemyAnimRules::MakeVariantSeed(GetName(), GetActorLocation(), VariantSeedOverride);
	}
	return CachedVariantSeed.GetValue();
}

void AEnemyCharacter::ApplyLookVariants()
{
	// BeginPlay: the spawn point and VariantSeedOverride are final now (a deferred spawn updated the anim once before them).
	CachedVariantSeed.Reset();
	if (UEnemyAnimInstance* Anim = GetMesh() ? Cast<UEnemyAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr)
	{
		Anim->RepickVariants(*this);
	}
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body || (LookVariants.Meshes.IsEmpty() && LookVariants.MaterialSets.IsEmpty()))
	{
		return;
	}
	const uint32 Seed = GetVariantSeed();
	LookMeshIndex = EnemyAnimRules::PickVariantIndex(Seed, 1, LookVariants.Meshes.Num());
	if (USkeletalMesh* VariantMesh = LookMeshIndex >= 0 ? LookVariants.Meshes[LookMeshIndex].Get() : nullptr)
	{
		const USkeletalMesh* Current = Body->GetSkeletalMeshAsset();
		if (VariantMesh != Current)
		{
			// Same skeleton as the Blueprint's mesh: the running ABP keeps its pose (no re-init).
			const bool bSameSkeleton = Current && Current->GetSkeleton() == VariantMesh->GetSkeleton();
			Body->SetSkeletalMeshAsset(VariantMesh);
			if (!bSameSkeleton)
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("[Look] %s: variant mesh %s is on another skeleton than the Blueprint's mesh"), *GetName(), *VariantMesh->GetName());
			}
		}
	}
	else
	{
		LookMeshIndex = -1;
	}
	LookMaterialSetIndex = EnemyAnimRules::PickVariantIndex(Seed, 2, LookVariants.MaterialSets.Num());
	if (LookMaterialSetIndex >= 0)
	{
		for (const TPair<FName, TObjectPtr<UMaterialInterface>>& Slot : LookVariants.MaterialSets[LookMaterialSetIndex].SlotMaterials)
		{
			const int32 Index = Body->GetMaterialIndex(Slot.Key);
			if (Index != INDEX_NONE && Slot.Value)
			{
				Body->SetMaterial(Index, Slot.Value);
			}
		}
	}
}

float AEnemyCharacter::PlaySpawnEntrance()
{
	UEnemyAnimInstance* Anim = GetMesh() ? Cast<UEnemyAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	const float Seconds = Anim && !bIsDying ? Anim->NotifyEntrance() : 0.f;
	if (Seconds > 0.f)
	{
		PerformanceLockTimer = Seconds;
		PerformanceFacePoint.Reset();
		if (AAIController* AIC = Cast<AAIController>(GetController()))
		{
			AIC->StopMovement();
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[EnemyAnim] %s plays its spawn entrance (%.1f s)"), *GetName(), Seconds);
	}
	return Seconds;
}

void AEnemyCharacter::StartAlertPerformance(const FVector& AlertLocation)
{
	UEnemyAnimInstance* Anim = GetMesh() ? Cast<UEnemyAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	if (!Anim || bIsDying || IsKnockedDown() || PerformanceLockTimer > 0.f)
	{
		return; // an entrance still playing keeps the floor
	}
	const float Seconds = Anim->NotifyAlert();
	if (Seconds > 0.f)
	{
		PerformanceLockTimer = Seconds;
		PerformanceFacePoint = AlertLocation;
		UE_LOG(LogCodexTactics, Display, TEXT("[EnemyAnim] %s roars at the squad (%.1f s)"), *GetName(), Seconds);
	}
}

bool AEnemyCharacter::TickPerformanceLock(float DeltaTime)
{
	if (PerformanceLockTimer <= 0.f)
	{
		return false;
	}
	UEnemyAnimInstance* Anim = GetMesh() ? Cast<UEnemyAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	PerformanceLockTimer -= DeltaTime;
	// A hit reaction / attack / knockdown replaced the clip on the slot: the hold ends with it (no frozen idle).
	if (!Anim || !Anim->IsPerformancePlaying())
	{
		PerformanceLockTimer = 0.f;
		return false;
	}
	if (PerformanceLockTimer <= 0.f)
	{
		PerformanceLockTimer = 0.f;
		Anim->StopPerformance(); // the lock cap ran out before the clip did
		return false;
	}
	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		AIC->StopMovement();
	}
	if (PerformanceFacePoint.IsSet())
	{
		const FVector ToPoint = PerformanceFacePoint.GetValue() - GetActorLocation();
		if (!ToPoint.IsNearlyZero(1.f))
		{
			FaceYaw(ToPoint.Rotation().Yaw, DeltaTime, 6.f);
		}
	}
	return true;
}
