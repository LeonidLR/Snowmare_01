#include "Misc/AutomationTest.h"
#include "AI/PmcOperatorRules.h"
#include "Animation/AnimSequence.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/CodexTacticsGameMode.h"
#include "Core/GameplayPreloadCatalog.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Ladders/LadderClimbComponent.h"
#include "Misc/PackageName.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GameplayPreloadCatalogTestLocal
{
	const FGameplayPreloadPath* FindGameplayPreloadPath(const TArray<FGameplayPreloadPath>& Paths, const FString& Path)
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
	TestNotNull(TEXT("pack idle"), FindGameplayPreloadPath(Paths, TEXT("/Game/Pack/Idle.Idle")));
	TestNotNull(TEXT("pack walk"), FindGameplayPreloadPath(Paths, TEXT("/Game/Pack/Walk.Walk")));
	TestNotNull(TEXT("existing retargeted copy"), FindGameplayPreloadPath(Paths, TEXT("/Game/Retarget/Male/Idle.Idle")));
	TestNull(TEXT("missing copy is not requested"), FindGameplayPreloadPath(Paths, TEXT("/Game/Retarget/Female/Idle.Idle")));
	TestNull(TEXT("folder metadata is not an object path"), FindGameplayPreloadPath(Paths, TEXT("/Game/Metadata/Folder")));
	for (const FGameplayPreloadPath& Entry : Paths) { TestFalse(TEXT("animation fallback is optional"), Entry.bRequired); }

	Block.SetBoolField(TEXT("pack_playback"), false);
	Paths.Reset();
	GameplayPreloadCatalog::GatherAnimationBlock(Block, Exists, Paths);
	TestEqual(TEXT("no-pack block contains only existing retarget copy"), Paths.Num(), 1);
	TestNotNull(TEXT("no-pack copy survives"), FindGameplayPreloadPath(Paths, TEXT("/Game/Retarget/Male/Idle.Idle")));
	TestNull(TEXT("no-pack source is not requested"), FindGameplayPreloadPath(Paths, TEXT("/Game/Pack/Idle.Idle")));
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
		const FGameplayPreloadPath* Entry = FindGameplayPreloadPath(Paths, Path);
		TestTrue(Name, Entry && Entry->bRequired);
	};
	const FGameplayPreloadPath* OperativeEntry = FindGameplayPreloadPath(Paths, Mode->OperativeBlueprint.ToSoftObjectPath().ToString());
	TestTrue(TEXT("operative blueprint is optional with a native fallback"), OperativeEntry && !OperativeEntry->bRequired);
	ACodexTacticsGameMode* MutableMode = GetMutableDefault<ACodexTacticsGameMode>();
	const auto SavedClass = MutableMode->OperativeClass;
	MutableMode->OperativeClass = nullptr;
	TArray<FGameplayPreloadPath> NoFallbackPaths;
	GameplayPreloadCatalog::GatherInitial(*MutableMode, NoFallbackPaths);
	MutableMode->OperativeClass = SavedClass;
	const FGameplayPreloadPath* NoFallbackEntry = FindGameplayPreloadPath(NoFallbackPaths, Mode->OperativeBlueprint.ToSoftObjectPath().ToString());
	TestTrue(TEXT("operative blueprint is required without a native fallback"), NoFallbackEntry && NoFallbackEntry->bRequired);
	CheckRequired(TEXT("ladder up is required"), Ladder->UpLoopClip.ToSoftObjectPath().ToString());
	CheckRequired(TEXT("ladder exit is required"), Ladder->ClimbOutClip.ToSoftObjectPath().ToString());
	CheckRequired(TEXT("ladder entry is required"), Ladder->IntoFromTopClip.ToSoftObjectPath().ToString());
	if (!PmcOperatorRules::Get().BodyMesh.IsEmpty())
	{
		const FGameplayPreloadPath* Body = FindGameplayPreloadPath(Paths, PmcOperatorRules::Get().BodyMesh);
		TestTrue(TEXT("configured PMC body is optional"), Body && !Body->bRequired);
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayPreloadNativeDefaultsTest, "CodexTactics.Core.GameplayPreload.NativeDefaultsGather",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayPreloadNativeDefaultsTest::RunTest(const FString&)
{
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>();
	UOperativeAnimInstance* Anim = NewObject<UOperativeAnimInstance>(Mesh);
	UAnimSequence* Dummy = NewObject<UAnimSequence>();
	Anim->CoverStandIdle[0] = Dummy;
	Anim->WalkToProneAnimation = Dummy;
	Anim->RootMotionMode = ERootMotionMode::IgnoreRootMotion;
	const auto SavedIdle = Anim->CoverStandIdle;
	TArray<FSoftObjectPath> Paths;
	Anim->GatherDefaultAnimationPaths(Paths);
	const int32 FirstCount = Paths.Num();
	Anim->GatherDefaultAnimationPaths(Paths);
	TestEqual(TEXT("repeated gather does not add duplicates"), Paths.Num(), FirstCount);
	TSet<FSoftObjectPath> Unique;
	for (const FSoftObjectPath& Path : Paths)
	{
		TestFalse(TEXT("native path is unique"), Unique.Contains(Path));
		Unique.Add(Path);
	}
	for (const TCHAR* Path : {
		TEXT("/Game/M4_Cover_Pack/Animations/stand/anim_M4_cvr_std_look_at_idle_L.anim_M4_cvr_std_look_at_idle_L"),
		TEXT("/Game/Characters/Operatives/Anims/ProneDive/AS_Crawl_from_Walk_RM.AS_Crawl_from_Walk_RM"),
		TEXT("/Game/Characters/Operatives/Anims/Interactions/AM_Interact_ChestOpen.AM_Interact_ChestOpen"),
		TEXT("/Game/Characters/Operatives/Anims/Weather/Storm/Male/AS_He_Storm_1_2Loop.AS_He_Storm_1_2Loop") })
	{
		TestTrue(FString::Printf(TEXT("gathered %s"), Path), Paths.Contains(FSoftObjectPath(Path)));
	}
	TestTrue(TEXT("clip field unchanged"), Anim->WalkToProneAnimation == Dummy);
	TestTrue(TEXT("cover array unchanged"), Anim->CoverStandIdle == SavedIdle);
	TestTrue(TEXT("root motion unchanged"), Anim->RootMotionMode == ERootMotionMode::IgnoreRootMotion);
	return true;
}
#endif
