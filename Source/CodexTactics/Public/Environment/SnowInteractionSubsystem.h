#pragma once

#include "CoreMinimal.h"
#include "Core/PlayerCommands.h"
#include "Environment/SnowInteractionTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "SnowInteractionSubsystem.generated.h"

class ACameraZoneVolume;
class ACharacter;
class AInteractableActor;
class UCombatFeedbackSubsystem;
class AOperativeCharacter;
class AWeatherPresenterActor;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterialParameterCollection;
class USnowStampSet;
class UTextureRenderTarget2D;

/**
 * Interactive snow, phase 1 (UE-only, no Godot reference; docs/systems/environment-weather.md §14, docs/design/INTERACTIVE_SNOW.md):
 * everything that walks, crawls, falls, drives or is dragged marks the snow in RT_SnowInteraction (RG16F: R = compaction depth 0..4,
 * G = berm 0..4), a 64 m window that follows the tactical view. The ground materials read it through MPC_Environment.SnowTrackWindow
 * (origin XY, 1 / size, enabled); SnowRefill / FreshSnow are written here too.
 * - Window: centred on the view focus + WindowLeadCm along the view yaw, snapped to 256-texel steps; a scroll moves the content by a
 *   shift pass (M_SnowTracks_Shift: RT -> scratch -> RT, zero outside, clamped to 4) and the MPC vector in the same frame.
 * - Fade: M_SnowInteraction_Fade (modulate) FadeHz times a second on WORLD time (pauses with the game), rate = base x (1 + boost x
 *   SnowRefill); a clamp pass every ClampPassSeconds bounds the additive pile-up.
 * - Stamps: one canvas draw per frame (K2_DrawMaterialTriangle, 2 triangles a stamp, M_SnowStamp's procedural shapes), at most
 *   MaxStampsPerFlush (the farthest from the view dropped first).
 * - Walkers (SnowInteractionWalkers.cpp), polled PollHz through the registries (no actor iterators): operatives (USquadSubsystem: boot
 *   prints, a prone / crawl furrow, a knockdown imprint), enemies (UEnemyRegistrySubsystem, by archetype), the APC (UVehicleSubsystem:
 *   twin ruts), a turn-based relocation (UTurnBasedCombatSubsystem::GetRelocatingObject: a drag trail); death imprints from the event bus.
 *   Ground gate: a landscape / SnowGround-tagged floor, or any static mesh floor while Codex.Env.SnowTracksAnyGround is 1; nothing indoors
 *   (Closed / Shelter camera zones).
 * - Event stamps (phase 3, SnowInteractionEvents.cpp; the gameplay only broadcasts, nothing here feeds back): GameplayEvents::Explosion ->
 *   a crater with a rim, a thrown-snow ring and clods (size from the blast radius / kind, one trace down to the snow ground);
 *   UCombatFeedbackSubsystem::OnTracerSpawned -> a missed shot's ray extended ShotExtendCm, one capped trace (ShotTracesPerFrame) -> a
 *   bullet dent on snow ground; URelocationSubsystem::GetMovingObjects (polled with the walkers) -> real-time drag trails.
 * - Off: no renderer (-nullrhi: the subsystem is not even created), the environment visuals off (no applying presenter), Codex.Env.SnowTracks 0
 *   or a missing asset -> nothing is drawn and SnowTrackWindow.w = 0. Visual only: nothing in the game reads it; the RT is cleared at
 *   world begin (a restart reloads the world) and at the end.
 * Console: Codex.Env.SnowTracks 0/1, Codex.Env.SnowTracksRes 0 (auto: Low effects 1024 / 48 m, else 2048 / 64 m) | 1024 | 2048 | 4096,
 * Codex.Env.SnowTracksAnyGround 0/1, Codex.Env.SnowTracksFadeScale (debug). Stats: `stat SnowInteraction`.
 */
UCLASS()
class CODEXTACTICS_API USnowInteractionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Cumulative counters (smokes / perf probes). */
	struct FMetrics
	{
		int64 Frames = 0;
		int64 StampsDrawn = 0;
		int64 StampsDropped = 0;
		int64 StampsCulled = 0;
		int64 DrawCalls = 0;
		int64 FadePasses = 0;
		int64 ShiftPasses = 0;
		int64 ClampPasses = 0;
		int64 Polls = 0;
		int32 LastWalkers = 0;
		/** Phase 3: explosion events seen / craters stamped (on snow ground, in the window). */
		int64 Explosions = 0;
		int64 Craters = 0;
		/** Phase 3: tracers queued / dropped from a full queue / hit a pawn (no trace) / traced / dented the snow. */
		int64 ShotsQueued = 0;
		int64 ShotsDropped = 0;
		int64 ShotsOnTarget = 0;
		int64 ShotTraces = 0;
		int32 MaxShotTracesInFrame = 0;
		int64 Dents = 0;
		/** Phase 3: objects a real-time relocation pushed at the last poll. */
		int32 LastMovingObjects = 0;
		/** Drag-trail segments (turn-based relocations + real-time pushes). */
		int64 DragSegments = 0;
		double CpuSeconds = 0.;
	};

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bBegunPlay; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	/** The subsystem of Context's world (null without one: no renderer, an editor world). */
	static USnowInteractionSubsystem* Get(const UObject* Context);

	/** Codex.Env.SnowTracks. */
	static bool IsFeatureEnabled();
	/** Codex.Env.SnowTracksAnyGround: any static mesh floor counts as snow ground (default 1 until the levels have a real terrain). */
	static bool IsAnyGroundEnabled();

	/** Stamps are drawn this frame (assets loaded, visuals applying, the feature on). */
	bool IsActive() const { return bActive; }
	/** The assets loaded (RT, materials, MPC). */
	bool HasAssets() const { return bAssetsReady; }
	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }
	const FSnowTrackWindow& GetWindow() const { return Window; }
	const FSnowStampTable& GetStamps() const { return Stamps; }
	const FSnowInteractionTuning& GetTuning() const { return Tuning; }
	bool IsStampSetFromAsset() const { return bStampSetFromAsset; }
	float GetSnowRefill() const { return SnowRefill; }
	float GetFreshSnow() const { return FreshSnow; }
	const FMetrics& GetMetrics() const { return Metrics; }
	/** The view focus (XY) the window followed last. */
	FVector2D GetLastFocus() const { return LastFocus; }
	void ResetMetrics() { Metrics = FMetrics(); }

	/** Queues a stamp for this frame's flush (dropped while inactive). */
	void AddStamp(const FSnowStamp& Stamp);
	/** Clears the RT (both channels 0). */
	void ClearTracks();

	/**
	 * Perf probe only (CodexTactics.SnowTracksSmoke perf): runs FadePasses extra fade passes (factor 1: the content is unchanged) and
	 * ShiftPasses extra shift passes (offset 0) now, to measure their GPU cost. No-op while inactive.
	 */
	void DebugRunExtraPasses(int32 FadePasses, int32 ShiftPasses);

	/** Death imprint of an operative (event bus). */
	UFUNCTION()
	void HandleSoldierDowned(AOperativeCharacter* Soldier);

	/** Death imprint of an enemy (GameplayEvents::EnemyKilled); a crater (GameplayEvents::Explosion, phase 3). */
	UFUNCTION()
	void HandleGameplayEventDetailed(FName EventName, const FGameplayEventContext& Context);

	/**
	 * Phase 3: the crater of an explosion of Kind (GameplayEvents::ExplosionKinds) at Location (RadiusCm 0 = the kind's own size): one
	 * trace down to the ground (at most Tuning.CraterMaxDropCm below; Source ignored), only on snow ground and not indoors. True when
	 * stamped. No-op while inactive.
	 */
	bool HandleExplosion(FName Kind, const FVector& Location, float RadiusCm, const AActor* Source = nullptr);

	/** Phase 3: UCombatFeedbackSubsystem::OnTracerSpawned - a shot that may have missed queues its dent trace (hits: nothing). */
	void HandleTracerSpawned(const FVector& Start, const FVector& End, bool bHit);

	/** Tracers waiting for their dent trace (smokes). */
	int32 GetPendingShotCount() const { return PendingShots.Num(); }

private:
	/** One tracked walker / vehicle / dragged object. */
	struct FWalkerTrack
	{
		TWeakObjectPtr<AActor> Actor;
		FSnowStrideState Stride;
		FSnowTrailState Trail;
		/** The vehicle's second rut. */
		FSnowTrailState TrailB;
		bool bWasDown = false;
		bool bWasCrawling = false;
		/** Dragged objects: tracked until this world time. */
		double DragUntil = 0.;
		double LastSeen = 0.;
	};

	bool LoadAssets();
	void Activate();
	void Deactivate();
	bool EnsureResolution();
	bool GetViewFocus(FVector& OutFocus, float& OutYaw) const;
	void UpdateWindow(const FVector& Focus, float ViewYaw);
	void RunShift(const FVector2D& OffsetUV);
	void RunFade(float Dt);
	void FlushStamps(const FVector2D& Focus);
	void WriteParameters();
	AWeatherPresenterActor* FindPresenter();

	// Walkers (SnowInteractionWalkers.cpp).
	void PollWalkers(double Now);
	void PollOperatives(double Now);
	void PollEnemies(double Now);
	void PollVehicles(double Now);
	void PollDragged(double Now);
	void PruneTrackers(double Now);
	FWalkerTrack& Track(AActor& Actor, double Now);
	/** The character stands on snow ground (its CharacterMovement floor; no trace) and is not indoors. */
	bool IsCharacterOnSnow(const ACharacter& Character) const;
	/** A non-character (vehicle, dragged object): a short downward trace unless any ground counts; not indoors. */
	bool IsActorOverSnow(const AActor& Actor) const;
	bool IsSheltered(const FVector& Location) const;
	/** A trace hit lies on snow ground (landscape / SnowGround tag / any static mesh while the CVar is on), facing up, not indoors. */
	bool IsSnowGroundHit(const FHitResult& Hit) const;
	void RefreshShelters();
	/** Phase 3 (SnowInteractionEvents.cpp): the queued dent traces, at most Tuning.ShotTracesPerFrame this frame. */
	void ProcessShots();
	void BindEventSources(UWorld& InWorld);
	void UnbindEventSources();
	void AddImprint(const FSnowStampSpec& Spec, const AActor& Actor);

	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> RenderTarget;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> Scratch;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> StampMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FadeMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ShiftToScratch;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ShiftBack;
	UPROPERTY(Transient) TObjectPtr<UMaterialParameterCollection> Collection;
	UPROPERTY(Transient) TObjectPtr<USnowStampSet> StampSet;

	TWeakObjectPtr<AWeatherPresenterActor> Presenter;
	TArray<TWeakObjectPtr<ACameraZoneVolume>> Shelters;
	TMap<TObjectKey<AActor>, FWalkerTrack> Trackers;
	TArray<FSnowStamp> PendingStamps;
	TArray<FSnowShot> PendingShots;
	TArray<const AInteractableActor*> MovingScratch;
	TWeakObjectPtr<UCombatFeedbackSubsystem> TracerSource;
	FDelegateHandle TracerHandle;

	FSnowStampTable Stamps;
	FSnowInteractionTuning Tuning;
	FSnowTrackWindow Window;
	FMetrics Metrics;
	FVector2D LastFocus = FVector2D::ZeroVector;
	/** The asset's own size (restored at the end when a CVar resized it). */
	FIntPoint AssetSize = FIntPoint(2048, 2048);
	int32 Resolution = 0;
	float SnowRefill = 0.f;
	float FreshSnow = 0.f;
	float SincePoll = 0.f;
	float SinceFade = 0.f;
	float SinceClamp = 0.f;
	double NextPresenterSearch = 0.;
	double NextShelterRefresh = 0.;
	double NextPrune = 0.;
	FLinearColor LastWindowParam = FLinearColor(0.f, 0.f, 0.f, -1.f);
	float LastRefillWritten = -1.f;
	float LastFreshWritten = -1.f;
	bool bBegunPlay = false;
	bool bAssetsReady = false;
	bool bActive = false;
	bool bStampSetFromAsset = false;
	bool bBoundEvents = false;
};
