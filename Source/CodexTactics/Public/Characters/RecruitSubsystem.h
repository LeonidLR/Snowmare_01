#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RecruitSubsystem.generated.h"

class AOperativeCharacter;
class UDialogueSequenceAsset;

/**
 * The Susanin rescue event and the recruit (Godot main.gd _check_susanin_rescue_event, _spawn_susanin_for_rescue,
 * _trigger_susanin_rescue_event, click branch "is_unrecruited"; Scenes/movements/recruit_susanin.gd).
 *
 * In the mission's rescue wave (UWaveSubsystem::GetSusaninRescueWave) Ivan Susanin appears 5-8 s after the wave
 * starts at the level's "SusaninSpawn" point (deferred past turn-based combat), freezing (cold 85). A narrative pause
 * shows him with the distress dialogue; any living operative within 2.8 m (or a heat source) rescues him: the
 * recruitment dialogue, then he joins the squad as member 4.
 */
UCLASS()
class CODEXTACTICS_API URecruitSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Level actor tag of the spawn point (Godot SPAWN_SUSANIN marker; Susanin stands at its location / yaw). */
	static const FName SpawnTag;

	/** Godot interaction_distance 2.8 m. */
	static constexpr float RescueDistance = 280.f;

	/** Godot _trigger_susanin_rescue_event (deferred while turn-based combat runs). */
	void TriggerRescueEvent();

	/** Susanin in the world (spawned on first use; Godot _spawn_susanin_for_rescue), null if the game mode has no operative class. */
	AOperativeCharacter* GetOrSpawnSusanin();

	AOperativeCharacter* GetSusanin() const { return Susanin.Get(); }
	bool IsRescueTriggered() const { return bRescueTriggered; }
	bool IsInColdDistress() const { return bColdDistress; }
	bool IsRecruitmentDialogueActive() const { return bRecruitmentDialogueActive; }
	bool IsSusaninRecruited() const;
	/** Susanin is waiting to be rescued / talked to (Godot group "recruits"). */
	bool IsRecruit(const AActor* Actor) const;

	/**
	 * Click on the recruit (Godot main.gd "is_unrecruited"): the nearest living operative within 2.8 m starts the
	 * recruitment dialogue, otherwise the leader walks up to him.
	 */
	void HandleRecruitClicked(bool bSprint);

	/** Godot start_recruitment_dialogue: narrative pause, one line, "🤝 Take into the squad ▶" recruits him. */
	void StartRecruitmentDialogue(AOperativeCharacter* Rescuer);

	/** Godot recruit_into_squad: warm, armed, member 4 of the squad; radio lines unless bSilent (load). */
	void RecruitIntoSquad(bool bSilent = false);

	/** Save / load (Godot save_manager.gd "susanin": is_recruited). */
	void RestoreRecruited(bool bRecruited);

private:
	UFUNCTION()
	void HandleWaveStarted(int32 WaveIndex, int32 TotalEnemies);

	void BeginNarrativePause();
	void EndNarrativePause();
	void FocusCamera(AActor* Target);
	UDialogueSequenceAsset* MakeDialogue(const FString& Title, const FString& FinishButton, const FString& Text);
	void HandleDistressDialogueFinished();

	TWeakObjectPtr<AOperativeCharacter> Susanin;

	/** Runtime dialogues (Godot builds them inline). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDialogueSequenceAsset>> Dialogues;

	bool bRescueTriggered = false;
	bool bColdDistress = false;
	bool bRecruitmentDialogueActive = false;
};
