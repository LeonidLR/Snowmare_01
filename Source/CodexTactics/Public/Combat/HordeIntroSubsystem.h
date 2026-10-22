#pragma once

#include "CoreMinimal.h"
#include "Combat/HordeIntroRules.h"
#include "GameFlow/GameFlowTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "HordeIntroSubsystem.generated.h"

class AEnemyCharacter;
class ATacticalCameraPawn;
class UHordeAlertWidget;

/** The horde intro started (the free pause opened): wave index, the flyover runs (false: skipped / off). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHordeIntroStarted, int32, WaveIndex, bool, bFlyover);
/** The horde intro is over: the camera is back on the squad in the free tactical pause (bSkipped: by Esc / a click). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHordeIntroFinished, int32, WaveIndex, bool, bSkipped);

/** Where the horde intro of the current wave stands. */
UENUM(BlueprintType)
enum class EHordeIntroStage : uint8
{
	Idle,
	/** The wave spawned; the fight runs, the horde forms its running mass (the camera stays on the squad). */
	Waiting,
	/** The free pause opened; the camera flies over the running horde in slow motion. */
	Flyover
};

/**
 * Sudden horde start (user decisions / feedback 2026-10-11; UE-only — Godot played a cutscene and a preparation
 * countdown). A sudden wave (UGameFlowSubsystem::IsAwaitingHordeIntro) spawns into the running fight; this subsystem
 * waits until the horde has become a running mass (HordeIntroRules::ShouldTrigger), then opens the free tactical pause,
 * shows the «HORDE INCOMING» alert (UHordeAlertWidget), and flies the tactical camera over the horde on real time while
 * the world runs in dramatic slow motion (FlyoverTimeDilation): a wide shot of the running mass seen from the squad's
 * side, a close tracking shot of a running front-runner of every type not yet shown in this fight, then back to the
 * leader, where the world settles into the free pause (0.02). Esc / a click skip it (camera at once on the leader);
 * before the trigger Esc / a Space tap open the free pause at once. Camera input is locked during the flyover, camera
 * zones step aside, the line of sight shows the whole horde. Pattern: UDeathCinematicSubsystem.
 * Hooks for a later HUD effect: OnHordeIntroStarted / OnHordeIntroFinished.
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
	virtual bool IsTickableWhenPaused() const override { return false; }

	/**
	 * Esc / a click: skips the flyover (camera straight back on the leader; the free pause stays). Esc before the trigger
	 * opens the free pause at once (bFromEscape; a click before the trigger is a normal order). True when consumed.
	 */
	bool HandleSkipInput(bool bFromEscape = true);

	/** Ends the intro of the current wave without a pause or flyover: the fight stays in real time (smokes, the bot). */
	void DismissIntro();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|HordeIntro")
	EHordeIntroStage GetStage() const { return Stage; }

	/** The intro waits for the running mass or flies over it. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|HordeIntro")
	bool IsIntroPending() const { return Stage != EHordeIntroStage::Idle; }

	/** The camera flies over the horde now (camera input locked). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|HordeIntro")
	bool IsFlyoverActive() const { return Stage == EHordeIntroStage::Flyover; }

	/** The free pause opened by the horde intro (fires before the flyover's first shot). */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|HordeIntro")
	FOnHordeIntroStarted OnHordeIntroStarted;

	/** The camera is back on the squad in the free pause. */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|HordeIntro")
	FOnHordeIntroFinished OnHordeIntroFinished;

	/** Alert (UHordeAlertWidget) opacity 0..1 and texts. */
	float GetAlertAlpha() const;
	FText GetAlertTitle() const;
	FText GetAlertSubtitle() const;

	/** HUD line while the flyover runs (empty otherwise): what is shown + how to skip. */
	FText GetCaption() const;

	/** Widget class of the alert (a Widget Blueprint subclass of UHordeAlertWidget may restyle it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|HordeIntro")
	TSubclassOf<UHordeAlertWidget> AlertWidgetClass;

	// --- Smokes / debugging ---
	const TArray<FHordeIntroShot>& GetShots() const { return Shots; }
	int32 GetCurrentShotIndex() const { return IsFlyoverActive() ? CurrentShot : INDEX_NONE; }
	/** The actor the running shot frames (the wide-shot anchor, a representative, the leader). */
	AActor* GetCurrentShotTarget() const { return ShotTarget.Get(); }
	/** The enemies the horde intro films (the fight's own; GatherEnemies). */
	TArray<AEnemyCharacter*> GetFilmedEnemies() const { TArray<AEnemyCharacter*> Out; GatherEnemies(Out); return Out; }
	/** Last wide framing: the wave's live enemies and those in its main cluster. */
	int32 GetWideEnemyCount() const { return WideEnemyCount; }
	int32 GetWideClusterCount() const { return WideClusterCount; }
	/** Every started shot of this world, "wide@<anchor>", "type:FrostHound@<enemy>", "return@<leader>". */
	const TArray<FString>& GetShotLog() const { return ShotLog; }
	const TSet<EEnemyArchetype>& GetShownTypes() const { return ShownTypes; }
	int32 GetStartedCount() const { return StartedCount; }
	int32 GetCompletedCount() const { return CompletedCount; }
	int32 GetSkippedCount() const { return SkippedCount; }
	/** Sudden waves this subsystem saw spawn. */
	int32 GetIntroCount() const { return IntroCount; }
	/** Game seconds from the spawn to the trigger (last intro), running share of the horde at the trigger. */
	float GetTriggerGameSeconds() const { return TriggerGameSeconds; }
	float GetTriggerRunningFraction() const { return TriggerRunningFraction; }
	bool WasTriggerByMass() const { return bTriggerByMass; }
	/** Real seconds since the flyover began. */
	float GetFlyoverSeconds() const { return FlyoverSeconds; }
	UHordeAlertWidget* GetAlertWidget() const { return AlertWidget; }

private:
	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	void BeginWaiting();
	void TickWaiting(float RealDelta, float GameDelta);
	/** The trigger: free pause, alert, flyover (bFlyover false: pause + alert only). */
	void OpenPause(bool bFlyover);
	void StartShot(int32 Index);
	void UpdateWideAnchor();
	/** Frames the main cluster of the wave (HordeIntroRules::FrameWide); bNewYaw picks the shot's yaw (else WideYaw stays). */
	HordeIntroRules::FWideFrame ComputeWideFrame(bool bNewYaw);
	void ApplyFlyoverDilation();
	/** Ends the flyover; bSnapBack puts the camera on the leader at once (a skip), else the return glide already ran. */
	void EndFlyover(bool bSnapBack, bool bSkipped, const TCHAR* Why);
	void ShowAlert();
	ATacticalCameraPawn* GetCamera() const;
	AActor* GetLeaderActor() const;
	FVector SquadCentre() const;
	void GatherEnemies(TArray<AEnemyCharacter*>& OutEnemies) const;
	FHordeMover MoverOf(const AEnemyCharacter& Enemy) const;
	/** Centre of the enemies running at the squad (all live enemies when none runs). */
	FVector MassCentre(const TArray<AEnemyCharacter*>& Enemies) const;

	FHordeIntroConfig Config;
	EHordeIntroStage Stage = EHordeIntroStage::Idle;
	TArray<FHordeIntroShot> Shots;
	TSet<EEnemyArchetype> ShownTypes;
	TArray<FString> ShotLog;
	TWeakObjectPtr<AActor> ShotTarget;
	int32 CurrentShot = INDEX_NONE;
	int32 WaveIndex = 0;
	float GameSinceSpawn = 0.f;
	float MassRunningSeconds = 0.f;
	/** The running mass has formed in this wave (latched). */
	bool bMassFormed = false;
	float FlyoverSeconds = 0.f;
	float ReturnDistance = -1.f;
	bool bOpeningPause = false;
	bool bPreTriggerSlowed = false;
	float TriggerGameSeconds = 0.f;
	float TriggerRunningFraction = 0.f;
	bool bTriggerByMass = false;
	/** Real seconds since the alert appeared (-1 = none) and how long it stays. */
	float AlertSeconds = -1.f;
	float AlertShowSeconds = 0.f;
	int32 IntroCount = 0;
	int32 StartedCount = 0;
	int32 CompletedCount = 0;
	int32 SkippedCount = 0;

	/** Moving focus point of the wide shot (on the running mass). */
	UPROPERTY(Transient)
	TObjectPtr<AActor> WideAnchor;
	/** Yaw of the running wide shot, deg. */
	float WideYaw = 0.f;
	/** Last framing: the wave's live enemies / those in its main cluster (log, smokes). */
	int32 WideEnemyCount = 0;
	int32 WideClusterCount = 0;

	UPROPERTY(Transient)
	TObjectPtr<UHordeAlertWidget> AlertWidget;
};
