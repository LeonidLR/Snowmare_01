#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldAIPauseSubsystem.generated.h"

/**
 * Generic "world AI held" gate (user request 2026-10-06, UE-only): while a blocking dialogue window is open
 * (UDialogueSubsystem::StartDialogue — the mission intro and every other story dialogue), during the pre-combat cutscene
 * or while any system / Blueprint registered a blocker (a Sequencer cutscene, a scripted scene), enemies neither perceive
 * nor act: patrols stand still in their idle pose, no sight / hearing / smell checks, no attacks. Live play resumes the
 * moment the last reason is gone. Rule: LevelEncounterRules::IsWorldAIPaused.
 */
UCLASS()
class CODEXTACTICS_API UWorldAIPauseSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Enemies are held now. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|AI")
	bool IsWorldAIPaused() const;

	/** IsWorldAIPaused of World's subsystem (false without a world). */
	static bool IsPausedIn(const UWorld* World);

	/** Holds the world AI until RemovePauseBlocker(Reason) (counted per reason name). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|AI")
	void AddPauseBlocker(FName Reason);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|AI")
	void RemovePauseBlocker(FName Reason);

	/** Number of registered blockers (all reasons). */
	int32 GetNumBlockers() const;

private:
	TMap<FName, int32> Blockers;
};
