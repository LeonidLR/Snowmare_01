#pragma once

#include "CoreMinimal.h"
#include "CodexAudioTypes.generated.h"

/**
 * 8 Adaptive Music States from docs/design/audio/EVENTS.md & Content/Data/Audio/music_cues.json
 */
UENUM(BlueprintType)
enum class ECodexMusicState : uint8
{
	None UMETA(DisplayName = "None"),
	Menu UMETA(DisplayName = "Menu"),
	ExploreCalm UMETA(DisplayName = "Explore Calm"),
	ExploreCold UMETA(DisplayName = "Explore Cold"),
	Tension UMETA(DisplayName = "Tension"),
	Combat UMETA(DisplayName = "Combat"),
	CombatHeavy UMETA(DisplayName = "Combat Heavy"),
	Victory UMETA(DisplayName = "Victory"),
	Defeat UMETA(DisplayName = "Defeat")
};

/**
 * Audio cue definition matching Content/Data/Audio/music_cues.json
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FCodexMusicCueDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	ECodexMusicState State = ECodexMusicState::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FName CueGroup;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FName Variant;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	bool bLoop = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float TargetDurationSec = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FString AssetPath;
};

/**
 * Voice bark definition matching Content/Data/Audio/voice_lines.json
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FCodexVoiceLineDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FName Speaker;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FName Category;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FString Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float Priority = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float CooldownSeconds = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FString AssetPath;
};
