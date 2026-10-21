#include "Misc/AutomationTest.h"
#include "Characters/EnemyCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

#if WITH_DEV_AUTOMATION_TESTS

// User 2026-10-21: the enemy Blueprints wear the single-slot optimized bodies; FEnemyLookVariants::Bodies holds each body with
// ITS colour sets. A child Blueprint with another body (BP_Enemy_FrozenPMC_Fighter under BP_Enemy_Frostbitten) keeps its own
// look: the inherited Bodies do not apply to it and it inherits no body MI. UE-only (no Godot reference).

namespace EnemyLookBodiesTest
{
	struct FCase
	{
		const TCHAR* Class;
		int32 Bodies;
		int32 SetsPerBody;
		bool bOwnBodies;
	};

	const FCase Cases[] = {
		{ TEXT("/Game/Characters/Enemies/Frostbitten/BP_Enemy_Frostbitten.BP_Enemy_Frostbitten_C"), 1, 4, true },
		{ TEXT("/Game/Characters/Enemies/MilitaryZombie/BP_Enemy_MilitaryZombie.BP_Enemy_MilitaryZombie_C"), 3, 3, true },
		{ TEXT("/Game/Characters/Enemies/Hound/BP_Enemy_Hound.BP_Enemy_Hound_C"), 1, 5, true },
		{ TEXT("/Game/Characters/Enemies/Brute/BP_Enemy_Brute.BP_Enemy_Brute_C"), 1, 4, true },
		{ TEXT("/Game/Characters/Enemies/Cutter/BP_Enemy_Cutter.BP_Enemy_Cutter_C"), 1, 4, true },
		// a child of BP_Enemy_Frostbitten with its own (unswitched) body: the inherited bodies are not its own
		{ TEXT("/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/BP_Enemy_FrozenPMC_Fighter.BP_Enemy_FrozenPMC_Fighter_C"), 1, 4, false },
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyLookBodiesAssetTest, "CodexTactics.Enemy.LookVariants.BlueprintBodies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyLookBodiesAssetTest::RunTest(const FString&)
{
	for (const EnemyLookBodiesTest::FCase& Case : EnemyLookBodiesTest::Cases)
	{
		UClass* Class = LoadClass<AEnemyCharacter>(nullptr, Case.Class);
		if (!TestNotNull(FString::Printf(TEXT("%s loads"), Case.Class), Class))
		{
			continue;
		}
		const AEnemyCharacter* Cdo = Class->GetDefaultObject<AEnemyCharacter>();
		const USkeletalMeshComponent* Body = Cdo->GetMesh();
		const USkeletalMesh* Mesh = Body ? Body->GetSkeletalMeshAsset() : nullptr;
		const FString Name = Class->GetName();
		TestEqual(FString::Printf(TEXT("%s: %d bodies"), *Name, Case.Bodies), Cdo->LookVariants.Bodies.Num(), Case.Bodies);
		TestEqual(FString::Printf(TEXT("%s: the bodies apply to its own mesh %s = %d"), *Name, *GetNameSafe(Mesh), Case.bOwnBodies),
			Cdo->UsesLookBodiesFor(Mesh), Case.bOwnBodies);
		TestEqual(FString::Printf(TEXT("%s: no override materials (the body's slot carries its default set)"), *Name),
			Body ? Body->OverrideMaterials.Num() : -1, 0);
		if (!Case.bOwnBodies)
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s: one-slot body %s"), *Name, *GetNameSafe(Mesh)), Mesh && Mesh->GetMaterials().Num() == 1
			&& Mesh->GetName().EndsWith(TEXT("_1slot")));
		for (const FEnemyLookBody& Each : Cdo->LookVariants.Bodies)
		{
			TestTrue(FString::Printf(TEXT("%s: body %s on the Blueprint mesh's skeleton"), *Name, *GetNameSafe(Each.Mesh)),
				Each.Mesh && Mesh && Each.Mesh->GetSkeleton() == Mesh->GetSkeleton() && Each.Mesh->GetPhysicsAsset() == Mesh->GetPhysicsAsset());
			TestEqual(FString::Printf(TEXT("%s: %s has %d colour sets"), *Name, *GetNameSafe(Each.Mesh), Case.SetsPerBody), Each.Sets.Num(), Case.SetsPerBody);
			// MI_<Asset>_SetN of the body SK_<Asset>_1slot: a set never crosses bodies
			FString Asset = GetNameSafe(Each.Mesh);
			Asset.RemoveFromStart(TEXT("SK_"));
			Asset.RemoveFromEnd(TEXT("_1slot"));
			for (UMaterialInterface* Set : Each.Sets)
			{
				TestTrue(FString::Printf(TEXT("%s: set %s belongs to %s and is a character-master MI"), *Name, *GetNameSafe(Set), *Asset),
					Set && Set->GetName().StartsWith(FString::Printf(TEXT("MI_%s_Set"), *Asset)) && Set->GetBaseMaterial()
					&& Set->GetBaseMaterial()->GetName().StartsWith(TEXT("M_CharacterMaster")));
			}
			TestTrue(FString::Printf(TEXT("%s: %s's slot material is one of its sets"), *Name, *GetNameSafe(Each.Mesh)), Each.Mesh
				&& Each.Mesh->GetMaterials().Num() == 1 && Each.Sets.Contains(Each.Mesh->GetMaterials()[0].MaterialInterface));
		}
	}
	return true;
}

#endif
