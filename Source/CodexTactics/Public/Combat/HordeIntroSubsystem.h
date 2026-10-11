#pragma once

#include "CoreMinimal.h"
#include "Combat/HordeIntroRules.h"
#include "GameFlow/GameFlowTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "HordeIntroSubsystem.generated.h"

class AEnemyCharacter;
class ATacticalCameraPawn;

/**
 * Sudden horde start (user decision 2026-10-11; UE-only — Godot played a cutscene and a preparation countdown): when a
 * horde wave opens in the free tactical pause (UGameFlowSubsystem::IsFreePause) this subsystem posts the contact line
 * and, HordeIntroRules::FHordeIntroConfig::FlyoverDelaySeconds later, flies the tactical camera over the horde on real
 * time while the world stays at the pause dilation: a wide shot of the horde advancing on the squad, one medium shot
 * per enemy type not yet shown in this fight (following one representative), then back to the leader. Esc or a click
 * skips it (the camera snaps back to the leader, the free pause stays); ending the pause (Space tap / hold, the wave
 * cleared, a death cinematic) ends it too. Camera input is locked while it runs. Pattern: UDeathCinematicSubsystem.
 */
UCLASS()
class CODEXTACTICS_API UHordeIntroSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/**
	 * Esc / a click: skips the pending or running flyover (camera straight back on the leader; the free pause stays).
	 * Returns true when it consumed the input.
	 */
	bool HandleSkipInput();

	/** A flyover waits for its first shot or runs (the camera may belong to it). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|HordeIntro")
	bool IsIntroPending() const { return bPending || bActive; }

	/** The camera flies over the horde now (camera input locked). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|HordeIntro")
	bool IsFlyoverActive() const { return bActive; }

	/** HUD line while the flyover runs (empty otherwise): what is shown + how to skip. */
	FText GetCaption() const;

	// --- Smokes / debugging ---
	/** Shots of the current / last flyover. */
	const TArray<FHordeIntroShot>& GetShots() const { return Shots; }
	/** Running shot index (-1 when none). */
	int32 GetCurrentShotIndex() const { return bActive ? CurrentShot : INDEX_NONE; }
	/** The actor the running shot frames (the wide-shot anchor, a representative, the leader). */
	AActor* GetCurrentShotTarget() const { return ShotTarget.Get(); }
	/** Every started shot of this world, "wide@<anchor>", "type:FrostHound@<enemy>", "return@<leader>". */
	const TArray<FString>& GetShotLog() const { return ShotLog; }
	/** Enemy types already shown in this fight (reset when the fight ends). */
	const TSet<EEnemyArchetype>& GetShownTypes() const { return ShownTypes; }
	int32 GetStartedCount() const { return StartedCount; }
	int32 GetCompletedCount() const { return CompletedCount; }
	int32 GetSkippedCount() const { return SkippedCount; }
	/** Free pauses this subsystem saw open (one per sudden wave). */
	int32 GetIntroCount() const { return IntroCount; }
	/** Real seconds since the last sudden spawn. */
	float GetSecondsSinceSpawn() const { return RealSinceSpawn; }

private:
	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	void BeginIntro();
	void BeginFlyover();
	void StartShot(int32 Index);
	void UpdateWideAnchor();
	/** Ends the intro; bSnapBack puts the camera on the leader at once (a skip), else the return glide already runs. */
	void EndIntro(bool bSnapBack, const TCHAR* Why);
	ATacticalCameraPawn* GetCamera() const;
	AActor* GetLeaderActor() const;
	FVector SquadCentre() const;
	void GatherEnemies(TArray<AEnemyCharacter*>& OutEnemies) const;
	FVector HordeCentre(const TArray<AEnemyCharacter*>& Enemies) const;

	FHordeIntroConfig Config;
	TArray<FHordeIntroShot> Shots;
	TSet<EEnemyArchetype> ShownTypes;
	TArray<FString> ShotLog;
	TWeakObjectPtr<AActor> ShotTarget;
	bool bPending = false;
	bool bActive = false;
	int32 CurrentShot = INDEX_NONE;
	float RealSinceSpawn = 0.f;
	float ReturnDistance = -1.f;
	int32 IntroCount = 0;
	int32 StartedCount = 0;
	int32 CompletedCount = 0;
	int32 SkippedCount = 0;

	/** Moving focus point of the wide shot (between the squad and the horde centre). */
	UPROPERTY(Transient)
	TObjectPtr<AActor> WideAnchor;
};
