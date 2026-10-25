// Godot reference: none (Unreal-only packaging). Godot exported every resource; UE cooks only what packages reference.
#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

class ITargetPlatform;

/**
 * Cook additions the asset references cannot express (tester build 2026-10-24: the first packaged game showed capsules
 * instead of the operatives and enemies). The game's C++ class defaults name assets by soft path (BP_Operative, the
 * enemy Blueprints per archetype, the Female Soldier body, the balance / dialogue / weapon assets, ...). A native class
 * default object is not a package, so the cooker never sees those references and the packaged game falls back to the
 * placeholder bodies. Every soft path held by a CodexTactics class default (and its default subobjects) is added to the cook.
 */
namespace CodexCookRules
{
	/** Soft paths (under /Game) held by the class defaults of the CodexTactics runtime module, incl. inside structs / arrays / maps / sets. */
	TSet<FSoftObjectPath> CollectNativeSoftReferences();

	/** FGameDelegates ModifyCook hook: adds the packages of CollectNativeSoftReferences that exist on disk. */
	void AddNativeSoftReferences(TConstArrayView<const ITargetPlatform*> TargetPlatforms, TArray<FName>& InOutPackagesToCook,
		TArray<FName>& InOutPackagesToNeverCook);
}
