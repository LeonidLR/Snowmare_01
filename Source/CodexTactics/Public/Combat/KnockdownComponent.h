#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/KnockdownRules.h"
#include "Combat/KnockdownTypes.h"
#include "KnockdownComponent.generated.h"

class UAnimInstance;
class UAnimMontage;
class UAnimSequenceBase;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnKnockdownPhaseNative, EKnockdownPhase /*NewPhase*/, EKnockdownPhase /*OldPhase*/);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnKnockdownChangedSignature, EKnockdownPhase, Phase, EKnockdownDirection, Direction);

/**
 * Knockdown & Recovery state of one unit (Sprint 14, TANDEM request #12; UE-only, no Godot reference — Gemini spec
 * "Knockdown & Recovery System", user-approved 2026-10-08). Pure decisions in KnockdownRules.
 *
 * Falling (Knocked_* clip) -> Downed (last frame held, recovery bar 0..100 %, 1.5 s; turn-based: until his turn and
 * 2 AP) -> GettingUp (Revive_* clip) -> up. Timers stop in the tactical pause and while the dialogue AI pause holds the
 * world. A death while down plays Death_*. The owner (operative / enemy) listens to OnPhaseChanged to interrupt its
 * actions, release its capsule and resume; move orders given meanwhile are buffered here and replayed when he is up.
 */
UCLASS(ClassGroup = (CodexTactics), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API UKnockdownComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKnockdownComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** This unit can fall at all (operatives, Frostbitten, Brute; hounds / cutters / marksmen not). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Knockdown")
	bool bCanBeKnockedDown = true;

	/** Heavy poise (Frost Brute): only explosions and critical heavy hits floor him. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Knockdown")
	bool bHeavyPoise = false;

	/** Fall / get-up / death clips (a montage on the anim's full-body slot, or a sequence played there as a dynamic montage). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Knockdown")
	TSoftObjectPtr<UAnimSequenceBase> KnockedBackClip;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Knockdown")
	TSoftObjectPtr<UAnimSequenceBase> KnockedFrontClip;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Knockdown")
	TSoftObjectPtr<UAnimSequenceBase> ReviveBackClip;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Knockdown")
	TSoftObjectPtr<UAnimSequenceBase> ReviveFrontClip;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Knockdown")
	TSoftObjectPtr<UAnimSequenceBase> DeathBackClip;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Knockdown")
	TSoftObjectPtr<UAnimSequenceBase> DeathFrontClip;

	/** Sets the six clips to /Game/Animations_KnockDown[/SubFolder]/<Name> (montages AM_<Name> when bMontages). */
	void SetClipFolder(const FString& SubFolder, bool bMontages);

	/** Blend times of the dynamic montages (montage assets use their own). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Knockdown")
	float FallBlendInSeconds = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Knockdown")
	float GetUpBlendInSeconds = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Knockdown")
	float GetUpBlendOutSeconds = 0.25f;

	/**
	 * A blow from SourceLocation (attacker / blast centre): knocks him down when KnockdownRules::ShouldKnockDown agrees
	 * (and he is alive, not prone, not vaulting). Returns true when a knockdown started.
	 */
	bool TryKnockDown(EKnockdownCause Cause, const FVector& SourceLocation, float Damage = 0.f, bool bCritical = false);

	/** Starts a knockdown regardless of the trigger rules (smokes / scripted events). */
	bool ForceKnockDown(EKnockdownDirection Direction, EKnockdownCause Cause);

	/** Every knockdown unit within ExplosionRadius of Center falls (grenade, barrel, mine). Returns how many fell. */
	static int32 NotifyExplosion(UWorld* World, const FVector& Center);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Knockdown")
	bool IsDown() const { return KnockdownRules::IsDown(State); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Knockdown")
	EKnockdownPhase GetPhase() const { return State.Phase; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Knockdown")
	EKnockdownDirection GetDirection() const { return State.Direction; }

	/** Recovery bar 0..1 (KnockdownRules::RecoveryFraction). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Knockdown")
	float GetRecoveryFraction() const { return KnockdownRules::RecoveryFraction(State); }

	const FKnockdownState& GetState() const { return State; }

	/** Incoming damage multiplier: 1 when up, else ranged / melee (KnockdownRules::DownedDamageMultiplier). */
	float GetDamageMultiplier(bool bMelee) const;

	/** Same by blow kind (KnockdownRules::DownedBlowMultiplier): 1 when up or for explosions. */
	float GetBlowMultiplier(EKnockdownBlow Blow) const;

	/** Turn-based, on his turn while lying: pay GetUpActionPoints and get up, or skip. ActionPoints is reduced. */
	FKnockdownTurnDecision HandleTurn(int32& ActionPoints);

	/** Turn-based enemy turn: gets up without AP (its turn is spent on it). Returns the get-up duration, 0 when not lying. */
	float GetUpForTurn();

	/** Keeps the latest order given while he is down; it runs once he is up (real time and turn-based). */
	void BufferOrder(TFunction<void()> Order);
	bool HasBufferedOrder() const { return static_cast<bool>(BufferedOrder); }

	/**
	 * The unit died: while down plays the Death_* clip of his fall side, held. Returns true when the knockdown handled
	 * the death animation (the anim instance then skips its own standing death).
	 */
	bool HandleDeath();
	bool DiedWhileDown() const { return bDiedWhileDown; }

	/** The clip asset of the current phase / the montage instance playing it (smokes). */
	UAnimSequenceBase* GetPlayingClip() const { return PlayingClip.Get(); }
	UAnimMontage* GetPlayingMontage() const { return PlayingMontage.Get(); }
	/** The anim slot the clips play on (operative "FullBody", enemy one-shot slot). */
	FName GetSlotName() const;

	/** Seconds since he last got up (re-knock immunity). */
	float GetSecondsSinceGetUp() const { return SecondsSinceGetUp; }

	/** Phase changes (native, the owner's reactions). */
	FOnKnockdownPhaseNative OnPhaseChanged;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Knockdown")
	FOnKnockdownChangedSignature OnKnockdownChanged;

	/** Knockdowns started / get-ups done (stats, smokes). */
	int32 GetKnockdownCount() const { return KnockdownCount; }
	int32 GetRecoveryCount() const { return RecoveryCount; }

private:
	bool StartKnockdown(EKnockdownDirection Direction, EKnockdownCause Cause);
	void SetPhase(EKnockdownPhase NewPhase, EKnockdownPhase OldPhase);
	void PlayPhaseClip();
	UAnimMontage* PlayClip(UAnimSequenceBase* Clip, float BlendIn, float BlendOut, float PlayRate, bool bHold);
	UAnimInstance* GetAnimInstance() const;
	bool IsFrozenNow() const;
	bool IsTurnBasedNow() const;
	bool CanFallNow() const;
	UAnimSequenceBase* LoadClip(const TSoftObjectPtr<UAnimSequenceBase>& Clip) const;
	static float ClipLength(const UAnimSequenceBase* Clip);
	void PostFeedLine(const FString& Line) const;
	FString GetUnitName() const;
	UFUNCTION()
	void HandleOwnerDied(AActor* Victim, const FString& AttackerSource);

	FKnockdownState State;
	float SecondsSinceGetUp = 1000.f;
	TFunction<void()> BufferedOrder;
	TWeakObjectPtr<UAnimSequenceBase> PlayingClip;
	TWeakObjectPtr<UAnimMontage> PlayingMontage;
	float GetUpPlayRate = 1.f;
	bool bDiedWhileDown = false;
	int32 KnockdownCount = 0;
	int32 RecoveryCount = 0;
};
