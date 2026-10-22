#pragma once

#include "CoreMinimal.h"
#include "GameFlow/CampaignRules.h"
#include "Quests/GateExitRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "LevelExitSubsystem.generated.h"

class AGateActor;
class AOperativeCharacter;
class SWidget;
class UPointsReviewWidget;

/**
 * The level ends through the fully opened blast gate (game designer 2026-10-22: "after the fight, if we go through the fully
 * opened gate, do a fade and that's the end of the level"; level JSON "gate_exit", off by default). Once the level's gate
 * (AGateActor::FindLevelGate) is fully open, the exit line is derived from its transform and DoorSize (no map actor): every
 * living operative who steps through the doorway onto its outer side is counted (GateExitRules::UpdateCrossed); the objective
 * shows "N/M out"; when the last living one is out the mission is complete (UMissionSubsystem::CompleteMission) and the
 * screen fades to black (a viewport overlay above the HUD, real time) with the level title and "MISSION COMPLETE". The killed
 * in action never have to cross; nothing fades while anyone alive is still inside.
 * After the black screen (2026-10-22 b, the campaign - Content/Data/Campaign/campaign.json): with a NEXT level, NextLevelSeconds
 * later the stat-points screen (UPointsReviewWidget) when somebody alive has unspent points ("Continue >" goes on; points may stay
 * unspent), then the squad is handed over (UCampaignSubsystem::CaptureSquad) and the next level opens; without a next level the
 * main menu opens ReturnToMenuSeconds later. Headless checks never leave on their own (SetLeaveInHeadless).
 * Also the level START (2026-10-22 b): a black screen with the campaign title that fades in (level JSON "start_fade_seconds", or a
 * campaign arrival). UE-only (the Godot gate only let the squad into the checkpoint).
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

	/** Smokes: a headless check leaves the black screen like a real game (points screen / next level / menu). */
	void SetLeaveInHeadless(bool bInLeave) { bLeaveInHeadless = bInLeave; }

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

	/** The black screen has ended: the decision below was taken. */
	bool HasLeaveDecision() const { return bLeaveDecided; }

	/** What followed the black screen (valid once HasLeaveDecision). */
	ELevelExitNext GetLeaveDecision() const { return Decision; }

	/** The next campaign level's map ("/Game/Maps/..."), empty at the end of the campaign. */
	const FString& GetNextLevelMap() const { return NextLevelMap; }

	/** The stat-points screen is up. */
	bool IsPointsScreenShown() const;
	UPointsReviewWidget* GetPointsScreen() const { return PointsScreen; }

	/** The next level was requested (OpenLevel). */
	bool IsTravelRequested() const { return bTravelRequested; }

	/** The level-start fade (2026-10-22 b): its length (0 = none), black alpha and title alpha now. */
	float GetStartFadeSeconds() const { return StartFadeSeconds; }
	float GetStartFadeAlpha() const;
	float GetStartTitleAlpha() const;
	const FString& GetStartTitle() const { return StartTitle; }
	/** The start fade has been resolved (first tick after BeginPlay). */
	bool IsStartFadeResolved() const { return bStartFadeResolved; }

private:
	void Arm(AGateActor* InGate, const FGateExitConfig& InConfig);
	void UpdateCrossings(const FGateExitConfig& InConfig);
	void BeginExit();
	void ShowOverlay();
	void RemoveOverlay();

	/** The black screen is over: points screen, next level or main menu (CampaignRules::DecideAfterExit). */
	void Leave();
	void ShowPointsScreen(const TArray<AOperativeCharacter*>& Members);
	void HandlePointsContinue();
	/** Hands the squad to the campaign and opens NextLevelMap. */
	void TravelToNextLevel();

	void TickStartFade(UWorld& World);
	void ShowStartOverlay();
	void RemoveStartOverlay();

	/** The campaign title of this world's map (empty outside the campaign). */
	FString GetCurrentLevelTitle() const;

	bool bHasOverride = false;
	FGateExitConfig OverrideConfig;
	bool bLeaveInHeadless = false;

	TWeakObjectPtr<AGateActor> Gate;
	FGateExitLine Line;
	bool bArmed = false;
	bool bExiting = false;
	float ExitElapsed = 0.f;
	/** The settings the exit was armed with (the fade timings). */
	FGateExitConfig ArmedConfig;
	TMap<TWeakObjectPtr<AOperativeCharacter>, bool> Crossed;
	int32 CrossedCount = 0;
	int32 RequiredCount = 0;
	int32 ReportedCrossed = -1;
	int32 ReportedRequired = -1;
	TSharedPtr<SWidget> Overlay;
	/** The title line above "MISSION COMPLETE" (the campaign title of the level). */
	FString ExitLevelTitle;

	bool bLeaveDecided = false;
	ELevelExitNext Decision = ELevelExitNext::MainMenu;
	FString NextLevelMap;
	FString NextLevelTitle;
	bool bTravelRequested = false;

	UPROPERTY(Transient)
	TObjectPtr<UPointsReviewWidget> PointsScreen;

	bool bStartFadeResolved = false;
	/** The start fade's timeline runs (also headless, where no overlay is shown). */
	bool bStartFadeRunning = false;
	float StartFadeSeconds = 0.f;
	float StartElapsed = 0.f;
	FString StartTitle;
	TSharedPtr<SWidget> StartOverlay;
};
