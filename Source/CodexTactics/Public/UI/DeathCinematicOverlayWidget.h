#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DeathCinematicOverlayWidget.generated.h"

class UBorder;
class UTextBlock;

/**
 * Commander's death (user request 2026-10-08): full-screen black that fades in, then «THE SQUAD HAS FALLEN» in the
 * centre, before the mission-failed screen. Built in C++; reads UDeathCinematicSubsystem every frame (alpha / text).
 * A Widget Blueprint subclass can restyle it by naming its widgets DeathFadeBorder / DeathFallenText. UE-only.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UDeathCinematicOverlayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Current overlay alpha / whether the line shows (smokes). */
	float GetShownAlpha() const { return ShownAlpha; }
	FText GetShownText() const;

	/** Pulls alpha / text from UDeathCinematicSubsystem (NativeTick; the subsystem also calls it every frame). */
	void Refresh();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|DeathCinematic", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> DeathFadeBorder;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|DeathCinematic", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DeathFallenText;

private:
	void BuildDefaultLayout();
	float ShownAlpha = 0.f;
};
