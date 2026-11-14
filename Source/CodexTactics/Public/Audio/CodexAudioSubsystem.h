#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Audio/CodexAudioTypes.h"
#include "Environment/EnvironmentTypes.h"
#include "GameFlow/GameFlowTypes.h"
#include "CodexAudioSubsystem.generated.h"

class AOperativeCharacter;
class AActor;
class UWorld;
class USoundBase;
class UAudioComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCodexMusicStateChangedEvent, ECodexMusicState, OldState, ECodexMusicState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCodexVoiceBarkTriggeredEvent, FName, VoiceLineId, FName, Speaker);

/**
 * CodexTactics Audio Subsystem.
 * Subscribes to mission and world events per docs/design/audio/EVENTS.md.
 * Manages adaptive music states (explore_calm, explore_cold, tension, combat, combat_heavy, victory, defeat, menu)
 * and squad/commander voice barks from Content/Data/Audio/voice_lines.json.
 * 
 * Enforces zero hard asset requirements: missing sound assets produce a Verbose log and a silent no-op.
 */
UCLASS()
class CODEXTACTICS_API UCodexAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Convenience accessor. Null if world context has no valid game instance. */
	static UCodexAudioSubsystem* Get(const UObject* WorldContext);

	/** Current active music state. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Audio")
	ECodexMusicState GetCurrentMusicState() const { return CurrentMusicState; }

	/** Currently active or requested music cue ID (e.g. "explore_calm_v1"). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Audio")
	FName GetActiveMusicCueId() const { return ActiveMusicCueId; }

	/** Last voice line ID triggered (e.g. "cmd_contact"). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Audio")
	FName GetLastTriggeredVoiceLineId() const { return LastTriggeredVoiceLineId; }

	/** Request a music state change explicitly. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Audio")
	bool RequestMusicState(ECodexMusicState NewState, int32 VariantIndex = 0);

	/** Trigger a voice bark by line ID. Respects cooldowns and priorities. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Audio")
	bool TriggerVoiceBark(FName VoiceLineId, const FVector& WorldLocation = FVector::ZeroVector);

	/** Trigger a mapped audio/VO event by name (e.g. "vo_cmd_contact", "OnMineSpotted"). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Audio")
	bool TriggerAudioEvent(FName EventId, FName Speaker = NAME_None);

	/** Re-evaluates current world conditions and updates music state accordingly. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Audio")
	void RefreshMusicState();

	/** Notifies tactical sight / suspicion changes to update tension music state. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Audio")
	void SetTacticalThreatState(bool bSquadSeesEnemy, bool bCombatPressureActive, int32 AliveEnemyCount);

	/** Broadcasts when adaptive music state transitions. */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Audio")
	FCodexMusicStateChangedEvent OnMusicStateChanged;

	/** Broadcasts when a voice bark is triggered. */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Audio")
	FCodexVoiceBarkTriggeredEvent OnVoiceBarkTriggered;

	// EventBus delegate handlers
	UFUNCTION()
	void HandleDialogueLineDisplayed(const FString& Speaker, const FString& Text);

	UFUNCTION()
	void HandleWeatherChanged(EWeatherState From, EWeatherState To);

	UFUNCTION()
	void HandleSoldierDowned(AOperativeCharacter* Soldier);

	UFUNCTION()
	void HandleMineSpotted(AActor* Mine, AOperativeCharacter* Spotter);

	UFUNCTION()
	void HandleRageStarted(AOperativeCharacter* Soldier);

	UFUNCTION()
	void HandleGeneratorStateChanged(bool bPowered);

	UFUNCTION()
	void HandleSoldierPanicked(AOperativeCharacter* Soldier, const FString& Reason);

	UFUNCTION()
	void HandleGameplayEvent(FName EventName, AActor* Subject);

	// World Subsystem handlers
	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	UFUNCTION()
	void HandleHordeReleased(int32 HordeIndex, int32 EnemyCount, FVector Location);

private:
	void OnPostWorldInitialization(UWorld* World, const UWorld::InitializationValues IVS);
	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	/** Binds delegates to world-level subsystems (GameFlow, Horde, etc.). */
	void BindToWorldSubsystems(UWorld* World);

	/** Attempts to play a sound asset quietly; if missing, logs Verbose and executes silent no-op. */
	bool PlaySoundAssetQuiet(const FString& AssetPath, bool bIsMusic, float Volume = 1.0f);

	float GetCurrentGameTime() const;

	UPROPERTY(Transient)
	ECodexMusicState CurrentMusicState = ECodexMusicState::None;

	UPROPERTY(Transient)
	FName ActiveMusicCueId = NAME_None;

	UPROPERTY(Transient)
	FName LastTriggeredVoiceLineId = NAME_None;

	UPROPERTY(Transient)
	ECodexGamePhase CurrentPhase = ECodexGamePhase::Exploration;

	UPROPERTY(Transient)
	EWeatherState CurrentWeather = EWeatherState::Clear;

	bool bIsFrontendWorld = false;
	bool bHordeActive = false;
	bool bSquadSeesEnemy = false;
	bool bCombatPressureActive = false;
	int32 AliveEnemies = 0;

	/** Cooldown tracking: VoiceLineId -> Last Play Time in seconds. */
	TMap<FName, float> VoiceLineLastPlayedTime;

	FDelegateHandle PostWorldInitHandle;
	FDelegateHandle WorldCleanupHandle;
};
