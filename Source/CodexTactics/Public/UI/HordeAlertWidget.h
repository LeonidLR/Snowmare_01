#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HordeAlertWidget.generated.h"

class UBorder;
class UTextBlock;

/**
 * Sudden horde start (user feedback 2026-10-11): the big alert that explains why the game suddenly paused — «HORDE
 * INCOMING» with «TACTICAL PAUSE - GIVE YOUR ORDERS» below, a dark band across the upper third. Built in C++; reads
 * UHordeIntroSubsystem every frame (opacity, texts). A Widget Blueprint subclass can restyle it by naming its widgets
 * HordeAlertBand / HordeAlertTitle / HordeAlertSubtitle (UHordeIntroSubsystem::AlertWidgetClass). UE-only.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UHordeAlertWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Pulls opacity / texts from UHordeIntroSubsystem. */
	void Refresh();

	/** Current opacity / title (smokes). */
	float GetShownAlpha() const { return ShownAlpha; }
	FText GetShownTitle() const;
	FText GetShownSubtitle() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|HordeIntro", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> HordeAlertBand;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|HordeIntro", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HordeAlertTitle;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|HordeIntro", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HordeAlertSubtitle;

private:
	void BuildDefaultLayout();
	float ShownAlpha = 0.f;
};
