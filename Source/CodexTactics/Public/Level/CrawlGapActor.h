#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NavAreas/NavArea.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "CrawlGapActor.generated.h"

class AOperativeCharacter;
class UBoxComponent;
class UVehicleNavObstacleComponent;

/** Navmesh area under a low fence: open only to prone bodies (UNavFilter_NoCrawl closes it for everybody else). */
UCLASS()
class CODEXTACTICS_API UNavArea_CrawlOnly : public UNavArea
{
	GENERATED_BODY()

public:
	UNavArea_CrawlOnly();
};

/** Query filter of standing / crouched operatives (AOperativeCharacter::RequestMove) and of the enemies: crawl-only areas are closed. */
UCLASS()
class CODEXTACTICS_API UNavFilter_NoCrawl : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	UNavFilter_NoCrawl();
};

/**
 * A low fence / gap an operative can only CRAWL under (Demo 01 user flow 2026-10-13, beat A3 "cautious approach": stances).
 * Place it on the fence line (local X = across the fence, Y = along it); the user's fence mesh is art. Default
 * (orchestrator decision 2026-10-13, the user may change it): MANUAL prone - a standing / crouched operative is stopped in
 * the approach zone with the hint "Too low - go prone" (event CrawlGapRefused), a prone one crawls under (event
 * CrawlGapPassed when he comes out on the other side). Pathing: a UNavArea_CrawlOnly modifier covers the footprint;
 * AOperativeCharacter::RequestMove uses UNavFilter_NoCrawl unless prone, so non-prone paths go round (or end at the fence
 * as a partial path); enemies never path through (UNavFilter_NoVault). The Bar box (ClearanceCm..BarTopCm) blocks pawn
 * capsules physically as a safety net: a prone capsule (80 cm) passes under 90 cm, crouched (117) and standing do not. UE-only.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ACrawlGapActor : public AActor
{
	GENERATED_BODY()

public:
	ACrawlGapActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|CrawlGap")
	TObjectPtr<USceneComponent> GapRoot;

	/** Blocking bar from ClearanceCm up to BarTopCm (hidden; the user's fence mesh is the visual). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|CrawlGap")
	TObjectPtr<UBoxComponent> Bar;

	/** Approach zone on both sides (overlap): standing / crouched operatives are stopped here with the hint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|CrawlGap")
	TObjectPtr<UBoxComponent> Approach;

	/** Crawl-only navmesh area over the footprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|CrawlGap")
	TObjectPtr<UVehicleNavObstacleComponent> NavArea;

	/** Length of the fence along local Y, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|CrawlGap", meta = (ClampMin = "50"))
	float GapWidthCm = 400.f;

	/** Underside of the bar, cm (a prone capsule is 80 cm tall). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|CrawlGap", meta = (ClampMin = "50"))
	float ClearanceCm = 90.f;

	/** Top of the blocking bar, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|CrawlGap", meta = (ClampMin = "100"))
	float BarTopCm = 200.f;

	/** Thickness of the fence line (local X), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|CrawlGap", meta = (ClampMin = "5"))
	float DepthCm = 20.f;

	/** Approach zone reach on each side, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|CrawlGap", meta = (ClampMin = "30"))
	float ApproachCm = 150.f;

	/** Feed hint for a standing / crouched operative stopped at the fence (speaker = that operative). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|CrawlGap")
	FText RefusalLine;

	/** Seconds between two hints for the same operative. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|CrawlGap", meta = (ClampMin = "0"))
	float RefusalCooldownSeconds = 4.f;

	/** Refusals so far (smokes). */
	int32 GetRefusalCount() const { return RefusalCount; }
	/** Crawl-throughs so far (smokes). */
	int32 GetPassCount() const { return PassCount; }

	/** Operatives inside the approach zone right now. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|CrawlGap")
	int32 GetApproachingCount() const { return Tracked.Num(); }

private:
	struct FTracked
	{
		TWeakObjectPtr<AOperativeCharacter> Operative;
		int32 EntrySide = 0;
		float LastRefusalTime = -1.f;
		bool bHeld = false;
		/** The arrival hint was given while he stood in the zone (once per visit). */
		bool bHinted = false;
	};

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void ApplyShape();
	void UpdateTraffic();
	/** bStop: walking at the fence -> stopped and held; false: standing in the zone -> the hint only (once per cooldown). */
	void Refuse(FTracked& Entry, AOperativeCharacter& Operative, bool bStop);
	void SetHeld(FTracked& Entry, AOperativeCharacter& Operative, bool bHeld);
	bool IsSquadOperative(const AActor* Actor) const;

	TArray<FTracked> Tracked;
	int32 RefusalCount = 0;
	int32 PassCount = 0;
};
