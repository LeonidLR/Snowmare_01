#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/HordeArrowRules.h"
#include "HordeArrowsWidget.generated.h"

class UTutorialOverlayWidget;

/**
 * Red edge arrows towards the wave's approach directions (game designer 2026-10-25: "arrows in RED show from which direction the
 * horde is coming; if there are several hordes, several arrows"; UE-only, no Godot reference). Same look as the tutorial's
 * navigation arrow (UTutorialOverlayWidget: a chevron on the screen edge with the distance in metres, shown only while the target
 * is off-screen), one per approach direction: the live enemies of the fight (UWaveSubsystem::GetAliveEnemies) grouped by bearing
 * from the squad (HordeArrowRules::GroupByBearing), regrouped at RefreshIntervalSeconds, projected every frame. Shown in the
 * WaveCombat phase only; a group gives no arrow once its nearest member is within HideRangeCm (engaged) or while its centre is on
 * screen; none when the group is dead. Never takes a click. Built by ACodexTacticsHUD under the tutorial overlay; keeps clear of
 * the tutorial's own (amber) arrow.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UHordeArrowsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** One approach direction of the last refresh (smokes / tools). */
	struct FApproach
	{
		HordeArrowRules::FGroup Group;
		/** True when the arrow is drawn (the group's centre is off-screen). */
		bool bArrow = false;
		/** Arrow tip + direction in widget units of GetViewSize(). */
		FVector2D Tip = FVector2D::ZeroVector;
		FVector2D Direction = FVector2D(1.f, 0.f);
		FText Distance;
	};

	/** Regroups and projects now (the tick does it throttled; smokes call it directly). */
	void Refresh(bool bForce = false);

	/** The approach directions of the last refresh (every group beyond HideRangeCm, on screen or not). */
	const TArray<FApproach>& GetApproaches() const { return Approaches; }
	/** Red arrows drawn on the last refresh. */
	int32 GetArrowCount() const;
	/** The view the last refresh projected into (the viewport in widget units, or the headless 1920 x 1080). */
	FVector2D GetViewSize() const { return ViewSize; }
	/** Tutorial overlay whose guide arrow the red ones keep clear of (set by the HUD). */
	void SetTutorialOverlay(UTutorialOverlayWidget* InOverlay);

	/** Arrow colour (red). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|HordeArrows")
	FLinearColor ArrowColor = FLinearColor(0.95f, 0.12f, 0.1f, 0.95f);
	/** Enemies closer than this to the squad (cm) are engaged: no arrow for them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|HordeArrows")
	float HideRangeCm = HordeArrowRules::DefaultHideRangeCm;
	/** Bearings closer than this (degrees, chained) are one approach direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|HordeArrows")
	float MergeAngleDeg = HordeArrowRules::DefaultMergeAngleDeg;
	/** Seconds between two regroupings (the projection runs every frame). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|HordeArrows")
	float RefreshIntervalSeconds = 0.1f;

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void ProjectApproaches();

	TArray<FApproach> Approaches;
	FVector2D ViewSize = FVector2D::ZeroVector;
	double LastRefreshTime = -1000.0;
	TWeakObjectPtr<UTutorialOverlayWidget> TutorialOverlay;
};
