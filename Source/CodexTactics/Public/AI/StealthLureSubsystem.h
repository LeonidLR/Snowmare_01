#pragma once

#include "CoreMinimal.h"
#include "AI/StealthLureRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "StealthLureSubsystem.generated.h"

class AEnemyCharacter;
class AGrenadeAimActor;
class ALureStoneActor;
class AOperativeCharacter;
class FJsonObject;

/**
 * Stealth lure (user decisions 2026-10-18; design docs/design/STEALTH_LURE.md, rules StealthLureRules): WHISTLE (a noise at
 * the operative, key 5 / the action bar) and THROW (a stone aimed with the grenade aim, key 6 / the action bar; the noise at the
 * impact). Infinite, a per-operative cooldown per kind; only exploring and undetected (CheckAvailability). EmitNoise sends every
 * patrol / guard enemy that hears it (walls muffle it like footsteps) to investigate the point through the existing patrol
 * search (AEnemyCharacter::StartPatrolSearchWith, a lure spec) - never a sighting: the operative is not revealed by it.
 * Preview while the throw is aimed / the WHISTLE button is hovered: the hearing circle on the ground and the enemies that would
 * hear it (GetPreviewListeners, drawn by the HUD; the investigating ones get a «?»). Saved: the cooldowns (CaptureSave).
 * UE-only; no Godot reference.
 */
UCLASS()
class CODEXTACTICS_API UStealthLureSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual TStatId GetStatId() const override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableWhenPaused() const override { return false; }

	/** Tuning in force (enemy_perception.json "lure"). */
	static FStealthLureParams GetParams();

	/** Why Operative cannot use Kind now (None = he can). */
	EStealthLureRefusal GetAvailability(const AOperativeCharacter* Operative, EStealthLureKind Kind) const;

	/** Seconds left on Operative's cooldown for Kind. */
	float GetCooldownLeft(const AOperativeCharacter* Operative, EStealthLureKind Kind) const;

	/** Any enemy hunts the squad (AEnemyCharacter::IsHuntingSquad). */
	bool IsSquadDetected() const;

	/**
	 * Operative whistles: refused (feed line with the reason) unless available; else the noise at his position, his whistle
	 * cooldown starts, feed «Whistle.», event "StealthLure.Whistle". True when he whistled.
	 */
	bool Whistle(AOperativeCharacter* Operative);

	/**
	 * Operative throws a stone at Target (clamped to his stance range): refused unless available; else the grenade throw clip, a
	 * stone in flight (its impact makes the noise), his throw cooldown starts at once, feed «Stone thrown.», event
	 * "StealthLure.Throw". The stone, or null when refused.
	 */
	ALureStoneActor* ThrowAt(AOperativeCharacter* Operative, const FVector& Target);

	/**
	 * The lure noise of Kind at Location: every enemy that hears it (StealthLureRules::Hears; walls counted from its ear to the
	 * point) investigates it. Source = the operative (log only). Returns how many enemies started / moved an investigation.
	 */
	int32 EmitNoise(const FVector& Location, EStealthLureKind Kind, const AActor* Source);

	/** The enemies that would hear a lure of Kind at Location now (no effect). */
	TArray<AEnemyCharacter*> FindListeners(const FVector& Location, EStealthLureKind Kind) const;

	/** The enemies the last EmitNoise sent investigating (smokes). */
	const TArray<TWeakObjectPtr<AEnemyCharacter>>& GetLastListeners() const { return LastListeners; }

	/**
	 * Preview of a lure of Kind at Location for this frame: the listeners are found (re-traced at most every 0.1 s or when the
	 * point moved 50 cm); bDrawRing draws the hearing circle (the whistle: the grenade aim draws the throw's ring itself). It
	 * fades when no longer refreshed (0.2 s).
	 */
	void UpdatePreview(const FVector& Location, EStealthLureKind Kind, bool bDrawRing);

	/** The previewed listeners (empty when no preview is up). */
	TArray<AEnemyCharacter*> GetPreviewListeners() const;

	bool IsPreviewActive() const;
	FVector GetPreviewLocation() const { return PreviewLocation; }

	/** Save: { "<operative name>": { "whistle_s": left, "throw_s": left } } (only running cooldowns). */
	TSharedRef<FJsonObject> CaptureSave() const;

	/** Load: cooldowns from CaptureSave (missing = ready). */
	void RestoreSave(const FJsonObject& Json);

	/** Clears every cooldown (smokes / tests). */
	void ResetCooldowns() { Cooldowns.Reset(); }

	/** Stone class (a Blueprint subclass with a real mesh / sound may replace it). */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Stealth")
	TSubclassOf<ALureStoneActor> StoneClass;

private:
	struct FLureCooldowns
	{
		double WhistleReadyAt = 0.0;
		double ThrowReadyAt = 0.0;
	};

	double Now() const;
	void StartCooldown(const AOperativeCharacter* Operative, EStealthLureKind Kind);
	void PostRefusal(const AOperativeCharacter* Operative, EStealthLureKind Kind, EStealthLureRefusal Refusal) const;
	void HideRing();

	TMap<TWeakObjectPtr<const AOperativeCharacter>, FLureCooldowns> Cooldowns;
	TArray<TWeakObjectPtr<AEnemyCharacter>> LastListeners;

	TArray<TWeakObjectPtr<AEnemyCharacter>> PreviewListeners;
	FVector PreviewLocation = FVector::ZeroVector;
	EStealthLureKind PreviewKind = EStealthLureKind::Whistle;
	double PreviewStamp = -100.0;
	double PreviewTraceStamp = -100.0;
	bool bRingShown = false;

	UPROPERTY(Transient)
	TObjectPtr<AGrenadeAimActor> RingActor;
};
