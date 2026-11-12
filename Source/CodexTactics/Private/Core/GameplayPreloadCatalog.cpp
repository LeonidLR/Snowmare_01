#include "Core/GameplayPreloadCatalog.h"

#include "AI/PmcOperatorRules.h"
#include "Characters/WeaponAnimLayerRules.h"
#include "CodexTactics.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Data/WeaponLoadoutRules.h"
#include "Data/WeaponRoster.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Ladders/LadderClimbComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"

// The animation layer owns the single process-cached JSON root. Both consumers see the same parsed blocks.
namespace OperativeShotgunLayer
{
	TSharedPtr<FJsonObject> WeaponBlock(const TCHAR* Key);
}

namespace GameplayPreloadCatalogLocal
{
	bool IsObjectAsset(const FString& Text)
	{
		int32 Slash = INDEX_NONE;
		int32 Dot = INDEX_NONE;
		return Text.FindLastChar(TEXT('/'), Slash) && Text.FindLastChar(TEXT('.'), Dot) && Dot > Slash
			&& FPackageName::IsValidObjectPath(Text);
	}

	void Add(const FString& Text, bool bRequired, TArray<FGameplayPreloadPath>& Out)
	{
		if (Text.IsEmpty() || !IsObjectAsset(Text))
		{
			if (bRequired && !Text.IsEmpty())
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("[GameplayPreload] invalid required object path: %s"), *Text);
			}
			return;
		}
		const FSoftObjectPath Path(Text);
		for (FGameplayPreloadPath& Existing : Out)
		{
			if (Existing.Path == Path)
			{
				Existing.bRequired |= bRequired;
				return;
			}
		}
		Out.Add({ Path, bRequired });
	}

	void AddSoft(const FSoftObjectPath& Path, bool bRequired, TArray<FGameplayPreloadPath>& Out)
	{
		if (!Path.IsNull()) { Add(Path.ToString(), bRequired, Out); }
	}

	void CollectClipPaths(const TSharedPtr<FJsonValue>& Value, TArray<FString>& Out)
	{
		if (!Value.IsValid()) { return; }
		if (Value->Type == EJson::String)
		{
			const FString Text = Value->AsString();
			if (Text.StartsWith(TEXT("/Game/")) && IsObjectAsset(Text)) { Out.AddUnique(Text); }
		}
		else if (Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Child : Value->AsArray()) { CollectClipPaths(Child, Out); }
		}
		else if (Value->Type == EJson::Object && Value->AsObject().IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Value->AsObject()->Values)
			{
				if (Pair.Key != TEXT("retargeted")) { CollectClipPaths(Pair.Value, Out); }
			}
		}
	}
}

void GameplayPreloadCatalog::GatherInitial(const ACodexTacticsGameMode& Mode, TArray<FGameplayPreloadPath>& Out)
{
	using namespace GameplayPreloadCatalogLocal;
	AddSoft(Mode.OperativeBlueprint.ToSoftObjectPath(), true, Out);
	for (const TPair<EEnemyArchetype, TSoftClassPtr<AEnemyCharacter>>& Entry : Mode.EnemyClasses)
	{
		AddSoft(Entry.Value.ToSoftObjectPath(), true, Out);
	}
	for (const TPair<FName, TSoftClassPtr<AEnemyCharacter>>& Entry : Mode.EnemyVariantClasses)
	{
		AddSoft(Entry.Value.ToSoftObjectPath(), true, Out);
	}
	for (const FSquadMemberSpawn& Member : Mode.SquadRoster)
	{
		AddSoft(Member.MemberBlueprint.ToSoftObjectPath(), true, Out);
		AddSoft(Member.BodyMesh.ToSoftObjectPath(), true, Out);
		for (const TSoftObjectPtr<UWeaponDataAsset>& Weapon : Member.ExtraWeapons) { AddSoft(Weapon.ToSoftObjectPath(), false, Out); }
	}
	for (const TSoftObjectPtr<UWeaponDataAsset>& Weapon : Mode.StartingArsenal) { AddSoft(Weapon.ToSoftObjectPath(), false, Out); }
	for (const TPair<FString, TArray<FString>>& Role : WeaponLoadoutRules::Get())
	{
		for (const FString& Id : Role.Value)
		{
			if (!Id.IsEmpty() && !WeaponRoster::IsOutOfPlay(Id)) { Add(WeaponLoadoutRules::WeaponAssetPath(Id), false, Out); }
		}
	}
	if (const ULevelConfigAsset* Level = Mode.GetActiveLevelConfig())
	{
		for (const TPair<FString, FLevelWeaponLoadout>& Loadout : Level->Config.Loadouts)
		{
			for (const FString& Id : Loadout.Value.WeaponIds)
			{
				if (!Id.IsEmpty() && !WeaponRoster::IsOutOfPlay(Id)) { Add(WeaponLoadoutRules::WeaponAssetPath(Id), false, Out); }
			}
		}
	}
	const FPmcOperatorConfig& Pmc = PmcOperatorRules::Get();
	Add(Pmc.BodyMesh, true, Out);
	for (const FPmcLook& Look : Pmc.Looks) { Add(Look.CoatMaterial, false, Out); }
	for (const FPmcWeaponChoice& Weapon : Pmc.WeaponPool)
	{
		if (!Weapon.WeaponId.IsEmpty()) { Add(PmcOperatorRules::WeaponAssetPath(Weapon.WeaponId), false, Out); }
	}
	const FString PmcFallback = PmcOperatorRules::PickWeaponId(Pmc, 0, 0);
	if (!PmcFallback.IsEmpty()) { Add(PmcOperatorRules::WeaponAssetPath(PmcFallback), false, Out); }
	if (!Pmc.RpgGunner.WeaponId.IsEmpty()) { Add(PmcOperatorRules::WeaponAssetPath(Pmc.RpgGunner.WeaponId), false, Out); }
	const ULadderClimbComponent* Ladder = GetDefault<ULadderClimbComponent>();
	if (Ladder)
	{
		AddSoft(Ladder->UpLoopClip.ToSoftObjectPath(), true, Out);
		AddSoft(Ladder->ClimbOutClip.ToSoftObjectPath(), true, Out);
		AddSoft(Ladder->IntoFromTopClip.ToSoftObjectPath(), true, Out);
	}
}

void GameplayPreloadCatalog::GatherAnimationBlock(const FJsonObject& Block, TFunctionRef<bool(const FString&)> PackageExists,
	TArray<FGameplayPreloadPath>& Out)
{
	using namespace GameplayPreloadCatalogLocal;
	TArray<FString> Folders;
	const TSharedPtr<FJsonObject>* Retargeted = nullptr;
	if (Block.TryGetObjectField(TEXT("retargeted"), Retargeted) && Retargeted && Retargeted->IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Retargeted)->Values)
		{
			if (Pair.Value.IsValid() && Pair.Value->Type == EJson::String) { Folders.AddUnique(Pair.Value->AsString()); }
		}
	}
	TArray<FString> Clips;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Block.Values)
	{
		if (Pair.Key != TEXT("retargeted")) { CollectClipPaths(Pair.Value, Clips); }
	}
	bool bPackPlayback = true;
	Block.TryGetBoolField(TEXT("pack_playback"), bPackPlayback);
	for (const FString& Clip : Clips)
	{
		if (bPackPlayback) { Add(Clip, false, Out); }
		for (const FString& Folder : Folders)
		{
			const FString Copy = WeaponAnimLayerRules::RetargetedClipPath(Clip, Folder);
			if (!Copy.IsEmpty() && FPackageName::IsValidObjectPath(Copy)
				&& PackageExists(FPackageName::ObjectPathToPackageName(Copy)))
			{
				Add(Copy, false, Out);
			}
		}
	}
}

void GameplayPreloadCatalog::GatherAnimations(TArray<FGameplayPreloadPath>& Out)
{
	for (const TCHAR* Name : { TEXT("shotgun"), TEXT("pistol"), TEXT("launcher"), TEXT("prone_crawl"), TEXT("stealth"), TEXT("injured") })
	{
		const TSharedPtr<FJsonObject> Block = OperativeShotgunLayer::WeaponBlock(Name);
		if (Block.IsValid())
		{
#if !UE_BUILD_SHIPPING
			// Match the animation layer's explicit developer override for no-pack blocks.
			FString Fallback;
			TArray<FString> Blocks;
			if (FParse::Value(FCommandLine::Get(), TEXT("AnimPackFallback="), Fallback))
			{
				Fallback.ParseIntoArray(Blocks, TEXT(","));
			}
			if (Blocks.Contains(Name))
			{
				FJsonObject WithFallback = *Block;
				WithFallback.SetBoolField(TEXT("pack_playback"), true);
				GatherAnimationBlock(WithFallback, [](const FString& Package) { return FPackageName::DoesPackageExist(Package); }, Out);
				continue;
			}
#endif
			GatherAnimationBlock(*Block, [](const FString& Package) { return FPackageName::DoesPackageExist(Package); }, Out);
		}
	}
}
