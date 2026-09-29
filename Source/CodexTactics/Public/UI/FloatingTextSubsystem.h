#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FloatingTextSubsystem.generated.h"

/** One rising, fading combat text in the world. */
struct CODEXTACTICS_API FCombatFloatingText
{
	FVector Start = FVector::ZeroVector;
	FString Text;
	FLinearColor Color = FLinearColor::White;
	/** World time (dilated, like Godot tweens under Engine.time_scale) when it appeared, s. */
	double StartTime = 0.0;
	float Duration = 0.75f;
	/** Rise over the duration, cm. */
	float Rise = 120.f;

	/** 0..1 progress at Now (> 1 when expired). */
	float GetProgress(double Now) const { return Duration > 0.f ? static_cast<float>((Now - StartTime) / Duration) : 1.f; }
};

/**
 * Floating combat texts (Godot Label3D spawned by _spawn_floating_combat_text in player.gd, enemy_base.gd, mine.gd):
 * billboard text that rises 1.2 m (mines 0.8 m) and fades out; the HUD draws them on top of everything
 * (no_depth_test). Timing follows world time, so texts freeze in the tactical pause like Godot tweens.
 */
UCLASS()
class CODEXTACTICS_API UFloatingTextSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Adds a text at WorldLocation. */
	void Spawn(const FVector& WorldLocation, const FString& Text, const FLinearColor& Color, float Duration, float Rise);

	/** Godot player.gd: 2 m above the operative's feet, 1.8 s for texts over 15 characters else 0.75 s, rises 1.2 m. */
	static void SpawnAboveOperative(const AActor* Operative, const FString& Text, const FLinearColor& Color);

	/** Godot enemy_base.gd: 2.5 m above the feet with ±0.3 m jitter, 0.7 s, rises 1.2 m. */
	static void SpawnAboveEnemy(const AActor* Enemy, const FString& Text, const FLinearColor& Color);

	/** Godot mine.gd: 0.8 m above the mine, 1.8 s for texts over 15 characters else 0.8 s, rises 0.8 m. */
	static void SpawnAboveMine(const AActor* Mine, const FString& Text, const FLinearColor& Color);

	/** Live texts (expired ones are dropped). */
	const TArray<FCombatFloatingText>& GetTexts();

	/** Texts spawned since the level started (smokes). */
	const TArray<FString>& GetHistory() const { return History; }
	bool HasShown(const FString& Part) const;

private:
	static FVector FeetOf(const AActor* Actor);

	TArray<FCombatFloatingText> Texts;
	TArray<FString> History;
};
