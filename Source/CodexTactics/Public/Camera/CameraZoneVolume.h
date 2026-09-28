#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFlow/GameFlowTypes.h"
#include "CameraZoneVolume.generated.h"

class ACameraActor;
class AOperativeCharacter;
class UBoxComponent;

/** Environment of a zone; drives the cold accumulation multiplier. Godot: CameraZoneTrigger.EnvironmentType. */
UENUM(BlueprintType)
enum class ECameraZoneEnvironment : uint8
{
	/** Heated bunker / room: no cold (x0). */
	Closed,
	/** Half-open shelter (x0.5). */
	Shelter,
	/** Open ground (x1). */
	Standard,
	/** Blizzard (x2.5). */
	Blizzard
};

/** Pure rules of camera zones. Godot reference: Scenes/movements/camera_zone_trigger.gd. */
namespace CameraZoneRules
{
	/** Cold accumulation multiplier inside a zone of this environment. */
	CODEXTACTICS_API float GetColdMultiplier(ECameraZoneEnvironment Environment);

	/**
	 * A zone shows its camera while the current leader is inside, except during a tactical pause,
	 * and during an active wave unless the zone allows combat.
	 */
	CODEXTACTICS_API bool ShouldBeActive(bool bSwitchEnabled, bool bAllowInCombat, ECodexCombatMode CombatMode, bool bWaveActive, bool bLeaderInside);
}

/**
 * Observation sector: when the current squad leader walks in, the view switches to a fixed level camera
 * and the other operatives hold their positions outside. Only the leader triggers it.
 * Godot reference: Scenes/movements/camera_zone_trigger.gd.
 */
UCLASS()
class CODEXTACTICS_API ACameraZoneVolume : public AActor
{
	GENERATED_BODY()

public:
	ACameraZoneVolume();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** True if Location is inside the zone box (with Godot's +1 m vertical tolerance). */
	bool ContainsLocation(const FVector& Location) const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Camera")
	bool IsZoneActive() const { return ActiveExplorer.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Camera")
	float GetColdMultiplier() const { return CameraZoneRules::GetColdMultiplier(Environment); }

	/** Fixed camera shown while the leader is inside. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Camera")
	TObjectPtr<ACameraActor> TargetCamera;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Camera")
	ECameraZoneEnvironment Environment = ECameraZoneEnvironment::Closed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Camera")
	bool bEnableCameraSwitch = true;

	/** Keep switching the camera during an active wave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Camera")
	bool bAllowInCombat = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Camera")
	FText ZoneName;

	/** Optional custom enter message; the environment label is appended. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Camera")
	FText EnterMessage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Camera")
	FText ExitMessage;

	/** Blend time when switching cameras, s (Godot switches instantly). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Camera", meta = (ClampMin = "0"))
	float BlendTime = 0.f;

private:
	void Activate(AOperativeCharacter* Explorer);
	void Deactivate();
	FText GetEnvironmentLabel() const;
	void PostMessage(const FText& Text) const;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Camera")
	TObjectPtr<UBoxComponent> Box;

	TWeakObjectPtr<AOperativeCharacter> ActiveExplorer;
};
