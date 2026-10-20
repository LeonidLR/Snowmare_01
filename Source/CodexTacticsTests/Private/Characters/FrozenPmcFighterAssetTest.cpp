#include "Misc/AutomationTest.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "Characters/EnemyAIRules.h"
#include "Characters/EnemyAnimInstance.h"
#include "Characters/EnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

// Game designer 2026-10-20: the Frozen PMC Fighter (Scripts/Editor/create_frozen_pmc_fighter.py). The new body
// (/Game/Cyberpunk_Explorer_A, Epic UE5 Manny skeleton) cannot run ABP_Enemy_Frostbitten (UE4 skeleton), and its mesh has one
// bone more than SK_Mannequin (the FBX root above "root": no ABP on SK_Mannequin spawns on it), so the script made a copy of the
// mesh on a skeleton built from its own bones and a retargeted copy of the ABP. Checks the Blueprint the game loads: a child of
// BP_Enemy_Frostbitten, the mesh copy (the pack's geometry and materials), an ABP and clips on the mesh's skeleton (the anim
// instance spawns, no cross-skeleton playback), the same clip set and rates as the Frostbitten's, the nameplate and the
// knockdown clips. Skipped when the user's pack is not in this checkout.

namespace FrozenPmcFighterAssetTest
{
	const TCHAR* const PmcClass = TEXT("/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/BP_Enemy_FrozenPMC_Fighter.BP_Enemy_FrozenPMC_Fighter_C");
	const TCHAR* const FrostClass = TEXT("/Game/Characters/Enemies/Frostbitten/BP_Enemy_Frostbitten.BP_Enemy_Frostbitten_C");
	const TCHAR* const PackMesh = TEXT("/Game/Cyberpunk_Explorer_A/Meshes/Cyberpunk_Explorer_A_UE5.Cyberpunk_Explorer_A_UE5");
	const TCHAR* const PackMeshPackage = TEXT("/Game/Cyberpunk_Explorer_A/Meshes/Cyberpunk_Explorer_A_UE5");
	const TCHAR* const PmcMeshPackage = TEXT("/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/Mesh/SKM_FrozenPMC_Fighter");

	TArray<UAnimSequenceBase*> AllClips(const UEnemyAnimInstance& Anim)
	{
		TArray<UAnimSequenceBase*> Clips;
		for (const TArray<TObjectPtr<UAnimSequenceBase>>* List : { &Anim.IdleAnimations, &Anim.AttackAnimations, &Anim.HitAnimations, &Anim.DeathAnimations })
		{
			for (UAnimSequenceBase* Clip : *List)
			{
				Clips.Add(Clip);
			}
		}
		Clips.Add(Anim.WalkAnimation);
		Clips.Add(Anim.RunAnimation);
		return Clips;
	}

	FString ReadString(const UObject& Object, const TCHAR* Name)
	{
		const FStrProperty* Property = FindFProperty<FStrProperty>(Object.GetClass(), Name);
		return Property ? Property->GetPropertyValue_InContainer(&Object) : FString(TEXT("<missing property>"));
	}

	bool ReadBool(const UObject& Object, const TCHAR* Name)
	{
		const FBoolProperty* Property = FindFProperty<FBoolProperty>(Object.GetClass(), Name);
		return Property && Property->GetPropertyValue_InContainer(&Object);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrozenPmcFighterAssetTest, "CodexTactics.Characters.EnemyAssets.FrozenPmcFighter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFrozenPmcFighterAssetTest::RunTest(const FString&)
{
	namespace Pmc = FrozenPmcFighterAssetTest;
	if (!FPackageName::DoesPackageExist(Pmc::PackMeshPackage))
	{
		AddInfo(TEXT("the user's /Game/Cyberpunk_Explorer_A pack is not in this checkout: skipped"));
		return true;
	}
	UClass* PmcBp = LoadClass<AEnemyCharacter>(nullptr, Pmc::PmcClass);
	UClass* Frost = LoadClass<AEnemyCharacter>(nullptr, Pmc::FrostClass);
	if (!TestNotNull(TEXT("BP_Enemy_FrozenPMC_Fighter loads"), PmcBp) || !TestNotNull(TEXT("BP_Enemy_Frostbitten loads"), Frost))
	{
		return false;
	}
	TestTrue(TEXT("a child of BP_Enemy_Frostbitten (archetype, AI tunables)"), PmcBp->IsChildOf(Frost));
	const AEnemyCharacter* Cdo = PmcBp->GetDefaultObject<AEnemyCharacter>();
	USkeletalMeshComponent* Body = Cdo->GetMesh();
	const USkeletalMesh* Mesh = Body ? Body->GetSkeletalMeshAsset() : nullptr;
	if (!TestNotNull(TEXT("skeletal mesh set"), Mesh))
	{
		return false;
	}
	TestEqual(TEXT("the mesh copy"), Mesh->GetPackage()->GetName(), FString(Pmc::PmcMeshPackage));
	TestEqual(TEXT("no override materials"), Body->OverrideMaterials.Num(), 0);
	const USkeleton* Skeleton = Mesh->GetSkeleton();
	TestTrue(TEXT("the mesh's skeleton holds every mesh bone (same parents)"), Skeleton && Skeleton->IsCompatibleMesh(Mesh, true));
	if (const USkeletalMesh* Pack = LoadObject<USkeletalMesh>(nullptr, Pmc::PackMesh))
	{
		TestEqual(TEXT("the pack's bones"), Mesh->GetRefSkeleton().GetRawBoneNum(), Pack->GetRefSkeleton().GetRawBoneNum());
		bool bSameMaterials = Mesh->GetMaterials().Num() == Pack->GetMaterials().Num();
		for (int32 Slot = 0; bSameMaterials && Slot < Mesh->GetMaterials().Num(); ++Slot)
		{
			bSameMaterials = Mesh->GetMaterials()[Slot].MaterialInterface == Pack->GetMaterials()[Slot].MaterialInterface;
		}
		TestTrue(TEXT("the pack's materials (by reference)"), bSameMaterials);
		TestTrue(TEXT("the pack's physics asset"), Mesh->GetPhysicsAsset() == Pack->GetPhysicsAsset());
	}
	const IAnimClassInterface* AnimClass = IAnimClassInterface::GetFromClass(Body->GetAnimClass());
	TestTrue(TEXT("anim blueprint mode"), Body->GetAnimationMode() == EAnimationMode::AnimationBlueprint);
	if (!TestNotNull(TEXT("anim class set"), AnimClass))
	{
		return false;
	}
	// USkeletalMeshComponent spawns the anim instance only when the ABP's target skeleton holds every mesh bone.
	TestTrue(TEXT("the ABP targets the mesh's skeleton"), AnimClass->GetTargetSkeleton() == Skeleton);
	const UEnemyAnimInstance* Anim = Cast<UEnemyAnimInstance>(Body->GetAnimClass()->GetDefaultObject());
	USkeletalMeshComponent* FrostBody = Frost->GetDefaultObject<AEnemyCharacter>()->GetMesh();
	const UEnemyAnimInstance* FrostAnim = FrostBody && FrostBody->GetAnimClass() ? Cast<UEnemyAnimInstance>(FrostBody->GetAnimClass()->GetDefaultObject()) : nullptr;
	if (!TestNotNull(TEXT("a UEnemyAnimInstance ABP"), Anim) || !TestNotNull(TEXT("the Frostbitten's ABP"), FrostAnim))
	{
		return false;
	}
	for (const UAnimSequenceBase* Clip : Pmc::AllClips(*Anim))
	{
		if (TestNotNull(TEXT("clip set"), Clip))
		{
			TestTrue(FString::Printf(TEXT("%s is on the mesh's skeleton"), *Clip->GetName()), Clip->GetSkeleton() == Skeleton);
		}
	}
	TestEqual(TEXT("idles like the Frostbitten"), Anim->IdleAnimations.Num(), FrostAnim->IdleAnimations.Num());
	TestEqual(TEXT("attacks like the Frostbitten"), Anim->AttackAnimations.Num(), FrostAnim->AttackAnimations.Num());
	TestEqual(TEXT("hit reactions like the Frostbitten"), Anim->HitAnimations.Num(), FrostAnim->HitAnimations.Num());
	TestEqual(TEXT("deaths like the Frostbitten"), Anim->DeathAnimations.Num(), FrostAnim->DeathAnimations.Num());
	TestEqual(TEXT("attack rate like the Frostbitten"), Anim->AttackPlayRate, FrostAnim->AttackPlayRate);
	TestEqual(TEXT("run threshold like the Frostbitten"), Anim->RunSpeedThreshold, FrostAnim->RunSpeedThreshold);
	TestEqual(TEXT("one-shot slot like the Frostbitten"), Anim->OneShotSlot, FrostAnim->OneShotSlot);
	TestEqual(TEXT("upper-body hit layer like the Frostbitten"), Anim->bUpperBodyHitReactions, FrostAnim->bUpperBodyHitReactions);

	TestEqual(TEXT("nameplate"), Pmc::ReadString(*Cdo, TEXT("DisplayNameOverride")), FString(TEXT("Frozen PMC")));
	TestTrue(TEXT("own knockdown clips"), Pmc::ReadBool(*Cdo, TEXT("bOverrideKnockdownClipFolder")));
	const FString Folder = EnemyAIRules::NormalizeKnockdownClipFolder(Pmc::ReadString(*Cdo, TEXT("KnockdownClipFolder")));
	for (const TCHAR* Clip : { TEXT("Knocked_Back"), TEXT("Knocked_Front"), TEXT("Revive_Back"), TEXT("Revive_Front"), TEXT("Death_Back"), TEXT("Death_Front") })
	{
		const FString Path = Folder.IsEmpty() ? FString::Printf(TEXT("/Game/Animations_KnockDown/%s.%s"), Clip, Clip)
			: FString::Printf(TEXT("/Game/Animations_KnockDown/%s/%s.%s"), *Folder, Clip, Clip);
		TestNotNull(FString::Printf(TEXT("knockdown clip %s"), *Path), LoadObject<UAnimSequenceBase>(nullptr, *Path));
	}
	const UCapsuleComponent* Capsule = Cdo->GetCapsuleComponent();
	TestTrue(TEXT("capsule 40 / 90 (a 181 cm body)"), Capsule && FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleRadius(), 40.f)
		&& FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleHalfHeight(), 90.f));
	TestTrue(TEXT("feet on the capsule bottom (mesh z = -half height)"), Capsule && FMath::IsNearlyEqual(Body->GetRelativeLocation().Z,
		-Capsule->GetUnscaledCapsuleHalfHeight()));
	TestTrue(TEXT("faces +X (the model faces +Y: yaw -90)"), FMath::IsNearlyEqual(Body->GetRelativeRotation().Yaw, -90.f));
	return true;
}

#endif
