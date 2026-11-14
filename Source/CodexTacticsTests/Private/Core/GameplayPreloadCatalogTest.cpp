#include "Misc/AutomationTest.h"
#include "AI/PmcOperatorRules.h"
#include "Core/CodexTacticsGameMode.h"
#include "Core/GameplayPreloadCatalog.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Ladders/LadderClimbComponent.h"
#include "Misc/PackageName.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GameplayPreloadCatalogTestLocal
{
	const FGameplayPreloadPath* Find(const TArray<FGameplayPreloadPath>& Paths, const FString& Path)
	{
		return Paths.FindByPredicate([&](const FGameplayPreloadPath& Entry) { return Entry.Path.ToString() == Path; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayPreloadAnimationCatalogTest, "CodexTactics.Core.GameplayPreload.AnimationCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayPreloadAnimationCatalogTest::RunTest(const FString&)
{
	using namespace GameplayPreloadCatalogTestLocal;
	FJsonObject Block;
	Block.SetStringField(TEXT("idle"), TEXT("/Game/Pack/Idle.Idle"));
	Block.SetStringField(TEXT("folder_metadata"), TEXT("/Game/Metadata/Folder"));
	TArray<TSharedPtr<FJsonValue>> Walk;
	Walk.Add(MakeShared<FJsonValueString>(TEXT("/Game/Pack/Walk.Walk")));
	Walk.Add(MakeShared<FJsonValueString>(TEXT("/Game/Pack/Idle.Idle")));
	Block.SetArrayField(TEXT("walk"), Walk);
	const TSharedPtr<FJsonObject> Retargeted = MakeShared<FJsonObject>();
	Retargeted->SetStringField(TEXT("MaleSkeleton"), TEXT("/Game/Retarget/Male"));
	Retargeted->SetStringField(TEXT("FemaleSkeleton"), TEXT("/Game/Retarget/Female"));
	Block.SetObjectField(TEXT("retargeted"), Retargeted);
	TArray<FGameplayPreloadPath> Paths;
	auto Exists = [](const FString& Package) { return Package == TEXT("/Game/Retarget/Male/Idle"); };
	GameplayPreloadCatalog::GatherAnimationBlock(Block, Exists, Paths);
	GameplayPreloadCatalog::GatherAnimationBlock(Block, Exists, Paths);
	TestEqual(TEXT("deduplicated pack paths and existing copy only"), Paths.Num(), 3);
	TestNotNull(TEXT("pack idle"), Find(Paths, TEXT("/Game/Pack/Idle.Idle")));
	TestNotNull(TEXT("pack walk"), Find(Paths, TEXT("/Game/Pack/Walk.Walk")));
	TestNotNull(TEXT("existing retargeted copy"), Find(Paths, TEXT("/Game/Retarget/Male/Idle.Idle")));
	TestNull(TEXT("missing copy is not requested"), Find(Paths, TEXT("/Game/Retarget/Female/Idle.Idle")));
	TestNull(TEXT("folder metadata is not an object path"), Find(Paths, TEXT("/Game/Metadata/Folder")));
	for (const FGameplayPreloadPath& Entry : Paths) { TestFalse(TEXT("animation fallback is optional"), Entry.bRequired); }

	Block.SetBoolField(TEXT("pack_playback"), false);
	Paths.Reset();
	GameplayPreloadCatalog::GatherAnimationBlock(Block, Exists, Paths);
	TestEqual(TEXT("no-pack block contains only existing retarget copy"), Paths.Num(), 1);
	TestNotNull(TEXT("no-pack copy survives"), Find(Paths, TEXT("/Game/Retarget/Male/Idle.Idle")));
	TestNull(TEXT("no-pack source is not requested"), Find(Paths, TEXT("/Game/Pack/Idle.Idle")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayPreloadInitialCatalogTest, "CodexTactics.Core.GameplayPreload.InitialCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayPreloadInitialCatalogTest::RunTest(const FString&)
{
	using namespace GameplayPreloadCatalogTestLocal;
	const ACodexTacticsGameMode* Mode = GetDefault<ACodexTacticsGameMode>();
	const ULadderClimbComponent* Ladder = GetDefault<ULadderClimbComponent>();
	TArray<FGameplayPreloadPath> Paths;
	GameplayPreloadCatalog::GatherInitial(*Mode, Paths);
	GameplayPreloadCatalog::GatherInitial(*Mode, Paths);
	const auto CheckRequired = [&](const FString& Name, const FString& Path)
	{
		const FGameplayPreloadPath* Entry = Find(Paths, Path);
		TestTrue(Name, Entry && Entry->bRequired);
	};
	CheckRequired(TEXT("operative blueprint is required"), Mode->OperativeBlueprint.ToSoftObjectPath().ToString());
	CheckRequired(TEXT("ladder up is required"), Ladder->UpLoopClip.ToSoftObjectPath().ToString());
	CheckRequired(TEXT("ladder exit is required"), Ladder->ClimbOutClip.ToSoftObjectPath().ToString());
	CheckRequired(TEXT("ladder entry is required"), Ladder->IntoFromTopClip.ToSoftObjectPath().ToString());
	if (!PmcOperatorRules::Get().BodyMesh.IsEmpty())
	{
		CheckRequired(TEXT("configured PMC body is required"), PmcOperatorRules::Get().BodyMesh);
	}
	TSet<FSoftObjectPath> Unique;
	for (const FGameplayPreloadPath& Entry : Paths)
	{
		TestFalse(TEXT("no duplicate path"), Unique.Contains(Entry.Path));
		Unique.Add(Entry.Path);
		TestTrue(TEXT("well-formed object path"), FPackageName::IsValidObjectPath(Entry.Path.ToString()));
	}
	return true;
}
#endif
