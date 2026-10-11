#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TacticalHeatSubsystem.generated.h"

class AOperativeCharacter;
class FJsonObject;
class FJsonValue;

/** Why a heat pack was not used (feed line chosen by the caller). */
enum class EHeatPackRefusal : uint8
{
	None,
	NoPack,
	NotNeeded,
	NotYourTurn,
	NotEnoughAP
};

/**
 * Sprint 15 «Mobile & Tactical Heat» (TANDEM request #14, UE-only): drives every UTacticalHeatTimerComponent (campfire, portable
 * stove, signal flare, fire puddle) — real time only when TacticalHeatRules::ShouldAdvanceRealTime allows it (frozen in the tactical
 * pause, the free horde pause and its slow-motion flyover, dialogues and cutscenes), one round per turn-based squad round
 * (UTurnBasedCombatSubsystem::OnRoundAdvanced). Also the heat-pack action (self or a mate; 1 AP in turn-based combat), the flare
 * reveal query of the line-of-sight system and the save / load of the loose heat actors (flares, puddles).
 * All heat sources warm through UHeatSourceComponent (UColdSurvivalComponent::IsNearHeatSource), so warming, warm health
 * regeneration and weapon thaw need no extra triggers.
 */
UCLASS()
class CODEXTACTICS_API UTacticalHeatSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return false; }

	/** Burn timers may advance now (TacticalHeatRules::ShouldAdvanceRealTime for this world). */
	bool CanAdvanceRealTime() const;

	/**
	 * User spends one heat pack on Target (himself or a squad mate): TacticalHeatRules::ApplyHeatPack through Target's cold
	 * component, feed line + floating text, event bus "HEAT_PACK". In turn-based combat the user must be the active operative and
	 * pays HeatPackActionPoints. Returns false with the reason (nothing spent).
	 */
	bool UseHeatPack(AOperativeCharacter* User, AOperativeCharacter* Target, EHeatPackRefusal* OutRefusal = nullptr);

	/** A burning signal flare lights Location (the enemy there counts as seen by the squad). */
	static bool IsRevealedByFlare(const UWorld* World, const FVector& Location);

	/** Advances every timer of this world by one turn-based round (also called by the round hook). */
	void AdvanceRound();

	/** Save: the loose heat actors (flares, puddles) of this world. */
	TArray<TSharedPtr<FJsonValue>> SaveLooseSources() const;

	/** Load: removes the session's flares / puddles and recreates the saved ones. */
	void LoadLooseSources(const TArray<TSharedPtr<FJsonValue>>& Entries);

	/** Real-time seconds burnt so far (smokes / telemetry). */
	float GetAdvancedSeconds() const { return AdvancedSeconds; }

private:
	void HandleRoundAdvanced(int32 Round);

	FDelegateHandle RoundHandle;
	float AdvancedSeconds = 0.f;
};
