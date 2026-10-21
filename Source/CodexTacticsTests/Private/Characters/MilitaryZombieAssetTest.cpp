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
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

// Game designer 2026-10-20: the Military Zombie (Scripts/Editor/create_military_zombie.py) on the user's /Game/military_zombie_set2
// pack. Checks the Blueprint the game loads: a child of BP_Enemy_Frostbitten (archetype, AI), an ABP that is a CHILD of
// ABP_Enemy_Frostbitten (its graph) targeting the pack's own skeleton, every clip native to that skeleton (no retarget), the
// clip roles (5 idles + fidgets, devour, 4 walk / 2 run variants, 4 attacks, hit, death, rage, transformation), the look
// variants (3 bodies on the same skeleton, 3 texture sets by slot name), name, knockdown clips, capsule / offset / facing.
// Skipped when the user's pack is not in this checkout.

namespace MilitaryZombieAssetTest
{
	const TCHAR* const ZombieClass = TEXT("/Game/Characters/Enemies/MilitaryZombie/BP_Enemy_MilitaryZombie.BP_Enemy_MilitaryZombie_C");
	const TCHAR* const FrostClass = TEXT("/Game/Characters/Enemies/Frostbitten/BP_Enemy_Frostbitten.BP_Enemy_Frostbitten_C");
	const TCHAR* const FrostAbpClass = TEXT("/Game/Characters/Enemies/Frostbitten/ABP_Enemy_Frostbitten.ABP_Enemy_Frostbitten_C");
	const TCHAR* const PackSkeletonPackage = TEXT("/Game/military_zombie_set2/Meshes/base_mesh/SK_Mannequin");
	const TCHAR* const BaseMeshPackage = TEXT("/Game/military_zombie_set2/Meshes/base_mesh/SKM_base_mesh_set2_1");
	/** The Blueprint's body since 2026-10-21: the single-slot optimized body 1 (same skeleton as the pack's body 1). */
	const TCHAR* const OptimizedBodyPackage = TEXT("/Game/Characters/Enemies/Optimized/MilitaryZombie_1/SK_MilitaryZombie_1_1slot");

	FString ReadZombieString(const UObject& Object, const TCHAR* Name)
	{
		const FStrProperty* Property = FindFProperty<FStrProperty>(Object.GetClass(), Name);
		return Property ? Property->GetPropertyValue_InContainer(&Object) : FString(TEXT("<missing property>"));
	}

	bool ReadZombieBool(const UObject& Object, const TCHAR* Name)
	{
		const FBoolProperty* Property = FindFProperty<FBoolProperty>(Object.GetClass(), Name);
		return Property && Property->GetPropertyValue_InContainer(&Object);
	}

	TArray<const UAnimSequenceBase*> AllZombieClips(const UEnemyAnimInstance& Anim)
	{
		TArray<const UAnimSequenceBase*> Clips;
		for (const TArray<TObjectPtr<UAnimSequenceBase>>* List : { &Anim.IdleAnimations, &Anim.IdleBreakAnimations, &Anim.IdleActivityAnimations,
			&Anim.AttackAnimations, &Anim.HitAnimations, &Anim.DeathAnimations, &Anim.AlertAnimations, &Anim.EntranceAnimations })
		{
			Clips.Append(*List);
		}
		for (const TArray<FEnemyLocomotionVariant>* List : { &Anim.WalkAnimationVariants, &Anim.RunAnimationVariants })
		{
			for (const FEnemyLocomotionVariant& Variant : *List)
			{
				Clips.Add(Variant.Clip);
			}
		}
		Clips.Add(Anim.WalkAnimation);
		Clips.Add(Anim.RunAnimation);
		return Clips;
	}

	bool ClipNamed(const TArray<TObjectPtr<UAnimSequenceBase>>& Clips, const TCHAR* Name)
	{
		return Clips.Num() == 1 && Clips[0] && Clips[0]->GetName() == Name;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMilitaryZombieAssetTest, "CodexTactics.Characters.EnemyAssets.MilitaryZombie",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMilitaryZombieAssetTest::RunTest(const FString&)
{
	namespace Mz = MilitaryZombieAssetTest;
	if (!FPackageName::DoesPackageExist(Mz::BaseMeshPackage))
	{
		AddInfo(TEXT("the user's /Game/military_zombie_set2 pack is not in this checkout: skipped"));
		return true;
	}
	UClass* Zombie = LoadClass<AEnemyCharacter>(nullptr, Mz::ZombieClass);
	UClass* Frost = LoadClass<AEnemyCharacter>(nullptr, Mz::FrostClass);
	UClass* FrostAbp = LoadClass<UAnimInstance>(nullptr, Mz::FrostAbpClass);
	if (!TestNotNull(TEXT("BP_Enemy_MilitaryZombie loads"), Zombie) || !TestNotNull(TEXT("BP_Enemy_Frostbitten loads"), Frost)
		|| !TestNotNull(TEXT("ABP_Enemy_Frostbitten loads"), FrostAbp))
	{
		return false;
	}
	TestTrue(TEXT("a child of BP_Enemy_Frostbitten (archetype, melee AI)"), Zombie->IsChildOf(Frost));
	const AEnemyCharacter* Cdo = Zombie->GetDefaultObject<AEnemyCharacter>();
	USkeletalMeshComponent* Body = Cdo->GetMesh();
	const USkeletalMesh* Mesh = Body ? Body->GetSkeletalMeshAsset() : nullptr;
	if (!TestNotNull(TEXT("skeletal mesh set"), Mesh))
	{
		return false;
	}
	TestEqual(TEXT("the optimized single-slot body 1"), Mesh->GetPackage()->GetName(), FString(Mz::OptimizedBodyPackage));
	const USkeleton* Skeleton = Mesh->GetSkeleton();
	TestTrue(TEXT("the pack's own skeleton"), Skeleton && Skeleton->GetPackage()->GetName() == Mz::PackSkeletonPackage);
	TestTrue(TEXT("the mesh fits its skeleton (an ABP spawns on it)"), Skeleton && Skeleton->IsCompatibleMesh(Mesh, true));

	// The ABP: a child of the Frostbitten's (its graph), on the pack skeleton, the pack's clips.
	const IAnimClassInterface* AnimClass = IAnimClassInterface::GetFromClass(Body->GetAnimClass());
	TestTrue(TEXT("anim blueprint mode"), Body->GetAnimationMode() == EAnimationMode::AnimationBlueprint);
	if (!TestNotNull(TEXT("anim class set"), AnimClass))
	{
		return false;
	}
	TestTrue(TEXT("ABP_Enemy_MilitaryZombie is a child of ABP_Enemy_Frostbitten"), Body->GetAnimClass()->IsChildOf(FrostAbp) && Body->GetAnimClass() != FrostAbp);
	TestTrue(TEXT("the ABP targets the mesh's skeleton"), AnimClass->GetTargetSkeleton() == Skeleton);
	const UEnemyAnimInstance* Anim = Cast<UEnemyAnimInstance>(Body->GetAnimClass()->GetDefaultObject());
	if (!TestNotNull(TEXT("a UEnemyAnimInstance ABP"), Anim))
	{
		return false;
	}
	for (const UAnimSequenceBase* Clip : Mz::AllZombieClips(*Anim))
	{
		if (TestNotNull(TEXT("clip set"), Clip))
		{
			TestTrue(FString::Printf(TEXT("%s is the pack's (its skeleton, its folder)"), *Clip->GetName()), Clip->GetSkeleton() == Skeleton
				&& Clip->GetPackage()->GetName().StartsWith(TEXT("/Game/military_zombie_set2/animation/")));
		}
	}
	TestEqual(TEXT("5 idle loops"), Anim->IdleAnimations.Num(), 5);
	TestTrue(TEXT("the idle loop picked by the spawn seed"), Anim->bSeededIdleVariant);
	TestEqual(TEXT("the idles double as fidget breaks"), Anim->IdleBreakAnimations.Num(), 5);
	TestTrue(TEXT("devour = the calm idle activity"), Mz::ClipNamed(Anim->IdleActivityAnimations, TEXT("anim_devour")));
	TestEqual(TEXT("4 walk variants"), Anim->WalkAnimationVariants.Num(), 4);
	TestEqual(TEXT("2 run variants"), Anim->RunAnimationVariants.Num(), 2);
	for (const FEnemyLocomotionVariant& Variant : Anim->WalkAnimationVariants)
	{
		TestTrue(TEXT("walk variant ground speed set and below the run threshold"), Variant.GroundSpeed > 0.f && Variant.GroundSpeed < Anim->RunSpeedThreshold);
	}
	for (const FEnemyLocomotionVariant& Variant : Anim->RunAnimationVariants)
	{
		TestTrue(TEXT("run variant ground speed above the run threshold"), Variant.GroundSpeed > Anim->RunSpeedThreshold);
	}
	TestEqual(TEXT("attacks 1-4"), Anim->AttackAnimations.Num(), 4);
	TestTrue(TEXT("hit = anim_hit"), Mz::ClipNamed(Anim->HitAnimations, TEXT("anim_hit")));
	TestTrue(TEXT("death = anim_Dying"), Mz::ClipNamed(Anim->DeathAnimations, TEXT("anim_Dying")));
	TestTrue(TEXT("alert roar = anim_rage"), Mz::ClipNamed(Anim->AlertAnimations, TEXT("anim_rage")));
	TestTrue(TEXT("spawn entrance = anim_transformation"), Mz::ClipNamed(Anim->EntranceAnimations, TEXT("anim_transformation")));
	TestNull(TEXT("no pounce"), Anim->JumpAttackAnimation.Get());
	if (const UAnimSequenceBase* Entrance = Anim->EntranceAnimations.IsEmpty() ? nullptr : Anim->EntranceAnimations[0].Get())
	{
		const float Seconds = (Entrance->GetPlayLength() - Anim->EntranceStartOffset) / Anim->EntrancePlayRate;
		TestTrue(FString::Printf(TEXT("the entrance stands %.1f s (2-6 s, not the clip's %.1f s)"), Seconds, Entrance->GetPlayLength()), Seconds >= 2.f && Seconds <= 6.f);
	}
	const UEnemyAnimInstance* FrostAnim = Cast<UEnemyAnimInstance>(FrostAbp->GetDefaultObject());
	TestEqual(TEXT("one-shot slot like the Frostbitten"), Anim->OneShotSlot, FrostAnim->OneShotSlot);
	TestEqual(TEXT("upper-body hit layer like the Frostbitten"), Anim->bUpperBodyHitReactions, FrostAnim->bUpperBodyHitReactions);
	TestEqual(TEXT("attack rate like the Frostbitten (similar clip lengths)"), Anim->AttackPlayRate, FrostAnim->AttackPlayRate);
	TestTrue(TEXT("the Frostbitten itself has no extras (no change for it)"), FrostAnim->AlertAnimations.IsEmpty() && FrostAnim->EntranceAnimations.IsEmpty()
		&& FrostAnim->IdleBreakAnimations.IsEmpty() && FrostAnim->IdleActivityAnimations.IsEmpty() && FrostAnim->WalkAnimationVariants.IsEmpty());

	// Look variants (2026-10-21): 3 single-slot optimized bodies on the pack's skeleton, each with ITS 3 colour sets
	// (MI_MilitaryZombie_<body>_Set1..3 of M_CharacterMaster*); the old mesh x slot-name sets are empty.
	const FEnemyLookVariants& Look = Cdo->LookVariants;
	TestEqual(TEXT("3 bodies"), Look.Bodies.Num(), 3);
	TestTrue(TEXT("no old mesh / material-set lists"), Look.Meshes.IsEmpty() && Look.MaterialSets.IsEmpty());
	for (int32 BodyIndex = 0; BodyIndex < Look.Bodies.Num(); ++BodyIndex)
	{
		const FEnemyLookBody& Body3 = Look.Bodies[BodyIndex];
		const USkeletalMesh* Variant = Body3.Mesh;
		TestTrue(FString::Printf(TEXT("body %s on the same skeleton, fits it, one slot"), *GetNameSafe(Variant)),
			Variant && Variant->GetSkeleton() == Skeleton && Skeleton->IsCompatibleMesh(Variant, true) && Variant->GetMaterials().Num() == 1);
		TestEqual(FString::Printf(TEXT("body %d: 3 colour sets"), BodyIndex + 1), Body3.Sets.Num(), 3);
		for (int32 SetIndex = 0; SetIndex < Body3.Sets.Num(); ++SetIndex)
		{
			UMaterialInterface* Set = Body3.Sets[SetIndex];
			const FString Expected = FString::Printf(TEXT("MI_MilitaryZombie_%d_Set%d"), BodyIndex + 1, SetIndex + 1);
			TestTrue(FString::Printf(TEXT("body %d set %d = %s of the character master (%s)"), BodyIndex + 1, SetIndex + 1, *Expected, *GetNameSafe(Set)),
				Set && Set->GetName() == Expected && Set->GetBaseMaterial() && Set->GetBaseMaterial()->GetName().StartsWith(TEXT("M_CharacterMaster")));
		}
	}

	TestEqual(TEXT("nameplate"), Mz::ReadZombieString(*Cdo, TEXT("DisplayNameOverride")), FString(TEXT("Military Zombie")));
	TestTrue(TEXT("own knockdown clips"), Mz::ReadZombieBool(*Cdo, TEXT("bOverrideKnockdownClipFolder")));
	const FString Folder = EnemyAIRules::NormalizeKnockdownClipFolder(Mz::ReadZombieString(*Cdo, TEXT("KnockdownClipFolder")));
	TestEqual(TEXT("knockdown = the Epic Manny originals of /Game/Animations_KnockDown (same bone names)"), Folder, FString());
	const UCapsuleComponent* Capsule = Cdo->GetCapsuleComponent();
	TestTrue(TEXT("capsule 40 / 90 (a ~181 cm body)"), Capsule && FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleRadius(), 40.f)
		&& FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleHalfHeight(), 90.f));
	TestTrue(TEXT("feet on the capsule bottom (mesh z = -half height)"), Capsule && FMath::IsNearlyEqual(Body->GetRelativeLocation().Z,
		-Capsule->GetUnscaledCapsuleHalfHeight()));
	TestTrue(TEXT("faces +X (the model faces +Y: yaw -90)"), FMath::IsNearlyEqual(Body->GetRelativeRotation().Yaw, -90.f));
	return true;
}

#endif
