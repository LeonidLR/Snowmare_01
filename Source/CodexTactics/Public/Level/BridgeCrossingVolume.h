#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BridgeCrossingVolume.generated.h"

class AOperativeCharacter;
class UBoxComponent;

/**
 * A shaky bridge / plank that carries only Capacity operatives at a time (Demo 01 user flow 2026-10-13, beat A1: the squad
 * crosses the crevasse one by one = "select an operative + Solo"). Place it over the span (the user's planks are art): the
 * Span box should reach ~1 m past each end of the planks so the refusal happens before the first step onto them. Local +X
 * runs along the span. Default (orchestrator decision 2026-10-13, the user may change it): a second operative is NOT
 * allowed on while one is crossing - the bridge creaks, he stops at the edge with a feed line ("The bridge won't hold
 * two!"), no damage, no fall; formation followers wait at the end (AOperativeCharacter traffic hold) until it is free.
 * Events (UCodexEventBus::OnGameplayEvent, Subject = the operative): BridgeEntered, BridgeRefused, BridgeCrossed (he left
 * on the other side). Generic: any map, any Capacity (0 = unlimited). Enemies are ignored unless bAffectsEnemies. UE-only.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ABridgeCrossingVolume : public AActor
{
	GENERATED_BODY()

public:
	ABridgeCrossingVolume();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** The span: resize per instance (default 6 x 2 x 2.5 m); local X = along the bridge. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Bridge")
	TObjectPtr<UBoxComponent> Span;

	/** Operatives allowed on the span at once (<= 0 = unlimited). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Bridge", meta = (ClampMin = "0"))
	int32 Capacity = 1;

	/** Feed line when a second operative is stopped at the edge (speaker = that operative). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Bridge")
	FText RefusalLine;

	/** Seconds between two refusal lines / creaks for the same operative. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Bridge", meta = (ClampMin = "0"))
	float RefusalCooldownSeconds = 4.f;

	/** Enemies count as occupants too (off: they walk across freely). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Bridge")
	bool bAffectsEnemies = false;

	/** The bridge creaked: an operative was refused (sound / camera shake hook for the user's Blueprint). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Bridge")
	void OnBridgeCreak(AOperativeCharacter* Refused);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Bridge")
	int32 GetOccupantCount() const { return Occupants.Num(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Bridge")
	bool IsOccupant(const AOperativeCharacter* Operative) const;

	/** Operatives stopped at the edge right now (waiting for the span). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Bridge")
	int32 GetWaitingCount() const { return Waiting.Num(); }

	/** Refusals so far (smokes). */
	int32 GetRefusalCount() const { return RefusalCount; }
	/** Crossings (left on the far side) so far (smokes). */
	int32 GetCrossingCount() const { return CrossingCount; }

private:
	struct FOccupant
	{
		TWeakObjectPtr<AOperativeCharacter> Operative;
		/** Span coordinate where he stepped on (sign = the side). */
		float EntryCoordinate = 0.f;
	};

	struct FWaiting
	{
		TWeakObjectPtr<AOperativeCharacter> Operative;
		float LastRefusalTime = -1.f;
	};

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Who is on / at the span now; admits, refuses, releases. */
	void UpdateTraffic();
	void Admit(AOperativeCharacter* Operative);
	void Refuse(AOperativeCharacter* Operative);
	void Release(AOperativeCharacter* Operative, bool bLeftSpan);
	int32 FindWaiting(const AOperativeCharacter* Operative) const;
	bool IsSquadOperative(const AActor* Actor) const;

	TArray<FOccupant> Occupants;
	TArray<FWaiting> Waiting;
	int32 RefusalCount = 0;
	int32 CrossingCount = 0;
};
