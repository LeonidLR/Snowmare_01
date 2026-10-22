#pragma once

#include "CoreMinimal.h"
#include "Quests/GateExitRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "LevelExitSubsystem.generated.h"

class AGateActor;
class AOperativeCharacter;
class SWidget;

/**
 * The level ends through the fully opened blast gate (game designer 2026-10-22: "after the fight, if we go through the fully
 * opened gate, do a fade and that's the end of the level"; level JSON "gate_exit", off by default). Once the level's gate
 * (AGateActor::FindLevelGate) is fully open, the exit line is derived from its transform and DoorSize (no map actor): every
 * living operative who steps through the doorway onto its outer side is counted (GateExitRules::UpdateCrossed); the objective
 * shows "N/M out"; when the last living one is out the mission is complete (UMissionSubsystem::CompleteMission) and the
 * screen fades to black (a viewport overlay above the HUD, real time) with "MISSION COMPLETE"; outside headless checks the
 * main menu opens ReturnToMenuSeconds later. The killed in action never have to cross; nothing fades while anyone alive is
 * still inside. UE-only (the Godot gate only let the squad into the checkpoint).
 */
UCLASS()
class CODEXTACTICS_API ULevelExitSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** The level's exit settings (the level JSON "gate_exit", or the smoke override). */
	FGateExitConfig GetConfig() const;

	/** Smokes / tools: use these settings instead of the level's. */
	void SetConfigOverride(const FGateExitConfig& InConfig);

	/** The gate is fully open and the exit counts. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|LevelExit")
	bool IsArmed() const { return bArmed; }

	/** The last living operative is out: the fade runs (or has finished). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|LevelExit")
	bool IsExiting() const { return bExiting; }

	/** Black overlay alpha (0 before the exit, 1 when black). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|LevelExit")
	float GetFadeAlpha() const;

	/** "MISSION COMPLETE" title alpha. */
	float GetTitleAlpha() const;

	/** Real seconds since the fade began (0 before). */
	float GetExitElapsedSeconds() const { return ExitElapsed; }

	int32 GetCrossedCount() const { return CrossedCount; }
	int32 GetRequiredCount() const { return RequiredCount; }

	/** The operative has crossed the exit line (and is still out). */
	bool HasCrossed(const AOperativeCharacter* Operative) const;

	/** The exit line once armed (false before). */
	bool GetExitLine(FGateExitLine& OutLine) const;

	AGateActor* GetGate() const { return Gate.Get(); }

private:
	void Arm(AGateActor* InGate, const FGateExitConfig& InConfig);
	void UpdateCrossings(const FGateExitConfig& InConfig);
	void BeginExit();
	void ShowOverlay();
	void RemoveOverlay();

	bool bHasOverride = false;
	FGateExitConfig OverrideConfig;

	TWeakObjectPtr<AGateActor> Gate;
	FGateExitLine Line;
	bool bArmed = false;
	bool bExiting = false;
	bool bReturnRequested = false;
	float ExitElapsed = 0.f;
	/** The settings the exit was armed with (the fade timings). */
	FGateExitConfig ArmedConfig;
	TMap<TWeakObjectPtr<AOperativeCharacter>, bool> Crossed;
	int32 CrossedCount = 0;
	int32 RequiredCount = 0;
	int32 ReportedCrossed = -1;
	int32 ReportedRequired = -1;
	TSharedPtr<SWidget> Overlay;
};
