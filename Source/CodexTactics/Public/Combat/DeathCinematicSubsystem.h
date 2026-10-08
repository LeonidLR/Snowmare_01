#pragma once

#include "CoreMinimal.h"
#include "Combat/DeathCinematicRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "DeathCinematicSubsystem.generated.h"

class AOperativeCharacter;
class UDeathCinematicOverlayWidget;

/**
 * Death cinematic (user request 2026-10-08; UE-only): an operative dies (real time, tactical pause or turn-based) ->
 * the camera glides onto him, time slows down (DeathCinematicRules, never below / stacked with the pause dilation), his
 * death clip plays out, the camera returns to the fight and time resumes. Player input is off while it runs, camera
 * zones step aside (ACameraZoneVolume checks IsFocusActive), deaths during a running focus queue up without a second
 * slow motion. The commander's death (MissionRules::ShouldFailMission) then fades to black, shows «THE SQUAD HAS
 * FALLEN» (drawn by ACodexTacticsHUD) and opens the mission-failed screen (UMissionSubsystem::TriggerMissionFailed).
 * Timeline in real seconds; it stands still while the world is paused (pause menu) or a dialogue froze the time.
 */
UCLASS()
class CODEXTACTICS_API UDeathCinematicSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

	/**
	 * An operative died. bDefeat: his death loses the mission (the defeat sequence follows his focus). Called by
	 * AOperativeCharacter::HandleDied after the squad / turn-based bookkeeping.
	 */
	void NotifyOperativeDied(AOperativeCharacter* Victim, bool bDefeat);

	/** A focus, a queued focus or the defeat sequence is running. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|DeathCinematic")
	bool IsActive() const { return Phase != EDeathCinematicPhase::Idle && Phase != EDeathCinematicPhase::Done; }

	/** The camera belongs to the death cinematic (camera zones and the turn-based camera step aside). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|DeathCinematic")
	bool IsFocusActive() const { return IsActive(); }

	/** A defeat is pending or shown (the mission-failed screen follows). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|DeathCinematic")
	bool IsDefeatPending() const { return bDefeatQueued || Phase == EDeathCinematicPhase::DefeatFade || Phase == EDeathCinematicPhase::DefeatText; }

	EDeathCinematicPhase GetPhase() const { return Phase; }

	/** The operative the camera shows now (null outside a focus). */
	AOperativeCharacter* GetFocusVictim() const { return Current.Victim.Get(); }

	/** Black overlay alpha (HUD). */
	float GetFadeAlpha() const;

	/** Defeat line to draw on black (empty when none). */
	FText GetDefeatText() const;

	/** Focuses completed so far (smokes). */
	int32 GetCompletedFocusCount() const { return CompletedFocuses; }

	/** Lowest world time dilation applied by the slow motion so far (smokes; 1 when none). */
	float GetLowestAppliedDilation() const { return LowestAppliedDilation; }

private:
	struct FEntry
	{
		TWeakObjectPtr<AOperativeCharacter> Victim;
		bool bDefeat = false;
		bool bSlowMo = true;
	};

	void StartFocus(const FEntry& Entry);
	void FinishFocus();
	void StartDefeat();
	void EndAll();
	void ApplyDilation(float Wanted);
	void RestoreDilation();
	void SetInputBlocked(bool bBlocked);
	float BaseFlowDilation() const;
	float MeasureClipSeconds(const AOperativeCharacter* Victim) const;

	FDeathCinematicConfig Config;
	EDeathCinematicPhase Phase = EDeathCinematicPhase::Idle;
	FEntry Current;
	TArray<FEntry> Queue;
	FDeathFocusTimes Times;
	float PhaseRealSeconds = 0.f;
	bool bClipMeasured = false;
	bool bDefeatQueued = false;
	bool bInputBlocked = false;
	/** The dilation we last set (-1 = none); someone else changing it (a dialogue, the flow) makes us yield. */
	float AppliedDilation = -1.f;
	bool bYieldedDilation = false;
	/** Camera distance before the first focus (restored on the way back). */
	float ReturnDistance = -1.f;
	bool bHasReturnDistance = false;
	int32 CompletedFocuses = 0;
	float LowestAppliedDilation = 1.f;
	TWeakObjectPtr<AOperativeCharacter> DefeatVictim;

	/** Black fade + defeat line (created for the defeat sequence, removed when the mission-failed screen opens). */
	UPROPERTY(Transient)
	TObjectPtr<UDeathCinematicOverlayWidget> Overlay;

public:
	/** The defeat overlay while it is on screen (smokes). */
	UDeathCinematicOverlayWidget* GetOverlay() const { return Overlay; }
};
