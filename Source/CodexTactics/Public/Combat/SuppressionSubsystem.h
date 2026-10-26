#pragma once

#include "CoreMinimal.h"
#include "Combat/SuppressionRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "SuppressionSubsystem.generated.h"

class AActor;

/** One round fired, as the shot paths report it (USuppressionSubsystem::ReportRound). */
struct CODEXTACTICS_API FSuppressionRound
{
	const AActor* Shooter = nullptr;
	/** True: the round flies at the enemies (the squad's, a turret's or the APC's fire); false: at the squad (enemy fire). */
	bool bAtEnemies = true;
	/** The muzzle and where the round ended (its target, the miss's end, or the surface it struck). */
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	/** The unit it was aimed at (null: fire at a spot). */
	const AActor* Target = nullptr;
	/** It hit Target. */
	bool bHitTarget = false;
	/** It struck something solid at End (cover, a wall, the ground) instead of flying on. */
	bool bStruckCover = false;
	ESuppressionWeapon Weapon = ESuppressionWeapon::SemiAuto;
	/** The weapon's own suppression_weight (weapons_tuning.json). */
	float WeightScale = 1.f;
	/** Fired under a deliberate "suppress target" intent (the enemy-AI hook, SetSuppressIntent): x DeliberateMultiplier. */
	bool bDeliberate = false;
};

/** What the suppression system did so far (smokes, the A/B log). */
struct CODEXTACTICS_API FSuppressionStats
{
	int32 Rounds = 0;
	int32 Impacts = 0;
	int32 SuppressedEntries = 0;
	int32 PinnedEntries = 0;
	/** Times every level was dropped (turn-based entry, the CVar off). */
	int32 Clears = 0;
	/** Rounds dropped because the mode has no suppression (turn-based) or the CVar is 0. */
	int32 IgnoredRounds = 0;
	/** Explosions reported (ReportExplosion) / units they suppressed. */
	int32 Explosions = 0;
	int32 ExplosionImpacts = 0;
	/** Fear zones of dense fire opened so far (melee packs scatter from them). */
	int32 ZonesOpened = 0;
};

/** A unit's tier changed: Unit, old tier, new tier. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnSuppressionTierChanged, AActor* /*Unit*/, ESuppressionTier /*Old*/, ESuppressionTier /*New*/);

/**
 * Suppressive fire at runtime (game designer decisions 2026-10-26, UE-only; docs/design/SUPPRESSION.md): one suppression level per unit
 * (operatives and enemy characters), fed by every round the shot paths report (ReportRound: the operatives' aimed / quick / SUPPRESS
 * fire, the marksman's rifle, the spitters' acid, the squad's turrets, the APC's MG and cannon) - near misses, strikes on the cover a
 * unit hides behind and hits, weighted by weapon (SuppressionRules). The level decays every tick; the decision points ask GetTierOf /
 * MayEnemyFire / GetOperativeFireOf / MayExposeNow. Turn-based combat has no suppression: every level is cleared on entering it and no
 * round counts there. Console variable Codex.Combat.Suppression (default 1; 0 = no suppression at all, the pre-2026-10-26 game).
 * Game / PIE worlds only (the static queries return "free" anywhere else).
 */
UCLASS()
class CODEXTACTICS_API USuppressionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return false; }
	virtual void Tick(float DeltaTime) override;

	/** Console variable Codex.Combat.Suppression (default 1). */
	static bool IsEnabled();
	static USuppressionSubsystem* Get(const UObject* WorldContext);

	/** ReportRound on the world of WorldContext (no-op without the subsystem). */
	static void Report(const UObject* WorldContext, const FSuppressionRound& Round);
	/** The unit's tier (None without the subsystem / with the CVar off / in turn-based combat). */
	static ESuppressionTier GetTierOf(const AActor* Unit);
	static float GetLevelOf(const AActor* Unit);
	/** An enemy may fire (SuppressionRules::EnemyMayFire). */
	static bool MayEnemyFire(const AActor* Enemy);
	/** An operative's fire under his tier (SuppressionRules::OperativeFire). */
	static FSuppressionFireEffect GetOperativeFireOf(const AActor* Operative);
	/** The unit may lean / peek / pop up / step off the wall (SuppressionRules::MayExpose). */
	static bool MayExposeNow(const AActor* Unit);
	/**
	 * The tier that ACTS on the unit: GetTierOf, except an operative in rage, who ignores suppression (user decision 2026-10-26,
	 * SuppressionRules::IgnoresSuppression; his level still accumulates). Used by GetOperativeFireOf / MayExposeNow.
	 */
	static ESuppressionTier GetActingTierOf(const AActor* Unit);
	/** ReportExplosion on the world of WorldContext (GameplayEvents::RaiseExplosion calls it for every blast). */
	static void ReportExplosionIn(const UObject* WorldContext, const FVector& Location, float BlastRadiusCm);

	/**
	 * Applies one round to every unit of the side it flies at that it hit, passed near or struck the cover of; a squad round also
	 * counts to the fire cell where it ended (dense fire opens a fear zone for melee packs).
	 */
	void ReportRound(const FSuppressionRound& Round);

	/**
	 * A blast (grenade, mine, barrel, trap, cannon shell; user decision 2026-10-26): every unit of both sides within its reach gets a short
	 * burst of suppression (SuppressionRules::ExplosionPressure / AddBurst: decays faster than gunfire).
	 */
	void ReportExplosion(const FVector& Location, float BlastRadiusCm);

	/**
	 * Dense fire scatters melee packs (user decision 2026-10-26): the open fear zone nearest to Feet that Feet is within (its radius +
	 * Extra) - the same use as a burning barrel's zone in AEnemyCharacter::FindNearestFireZone (fire-fearing enemies flee out of it and
	 * path round it). False: none.
	 */
	bool FindFearZone(const FVector& Feet, float Extra, FVector& OutCentre, float& OutRadius) const;
	/** The centres of every open fear zone (smokes, debugging). */
	TArray<FVector> GetOpenFearZones() const;

	float GetLevel(const AActor* Unit) const;
	ESuppressionTier GetTier(const AActor* Unit) const;

	/** Drops every level (turn-based entry, the CVar switched off); Why goes to the log. */
	void ClearAll(const TCHAR* Why = TEXT("requested"));

	/** Smokes: sets a unit's level (its tier follows; the quiet time restarts). */
	void SetLevelForTesting(AActor* Unit, float Level);

	// --- Enemy-AI hook (a later task teaches enemies to suppress and flank): a deliberate "suppress target" intent. ---

	/** Shooter means to suppress Target: his rounds at Target count x DeliberateMultiplier (FSuppressionRound::bDeliberate). */
	void SetSuppressIntent(AActor* Shooter, AActor* Target);
	void ClearSuppressIntent(const AActor* Shooter);
	/** The target Shooter means to suppress (null: none). */
	AActor* GetSuppressIntent(const AActor* Shooter) const;

	/** Every unit with a level above 0 and its state (HUD badges). */
	void GetTracked(TArray<TPair<AActor*, FSuppressionState>>& Out) const;

	const FSuppressionStats& GetStats() const { return Stats; }

	FOnSuppressionTierChanged OnTierChanged;

private:
	/** Turn-based combat (or the CVar off): no suppression. */
	bool IsSuppressionOff() const;
	void ApplyPressure(AActor* Unit, float Pressure);
	void NoteTier(AActor* Unit, ESuppressionTier Old, ESuppressionTier New);

	TMap<TWeakObjectPtr<AActor>, FSuppressionState> States;
	/** The squad's fire by where it ends (SuppressionRules::FindFireCell / StepFireCell). */
	TArray<FSuppressionFireCell> FireCells;
	TMap<TWeakObjectPtr<const AActor>, TWeakObjectPtr<AActor>> Intents;
	FSuppressionStats Stats;
};
