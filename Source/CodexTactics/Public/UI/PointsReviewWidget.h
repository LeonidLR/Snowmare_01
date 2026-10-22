#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PointsReviewWidget.generated.h"

class AOperativeCharacter;
class AOperativePortraitCapture;
class UImage;
class UProfileDialogWidget;
class USizeBox;
class UTextBlock;

/**
 * The stat-points screen after a level exit (game designer 2026-10-22: "if the operatives have unspent points, after the black screen
 * a menu must appear with the character's image and the points that can be added to him; after that the new level opens"). Shown by
 * ULevelExitSubsystem on the black screen when the campaign has a next level and somebody alive has unspent points: the level title,
 * the operative's live portrait (AOperativePortraitCapture; a role letter where nothing renders) beside the regular character sheet
 * (UProfileDialogWidget in review mode: only the members with points, + / - spend them, < Prev / Next >), "Continue >" goes on -
 * points left unspent stay for later. Built in C++ (a Widget Blueprint subclass may restyle it; widget names: PointsTitle,
 * PointsHint, PortraitImage, PortraitLetter, ProfileSlot). UE-only.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UPointsReviewWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Fills the screen: Members (the operatives with points, in squad order), the finished level's title and the next one's. ProfileClass =
	 * the HUD's profile class (the user's restyle), else UProfileDialogWidget.
	 */
	void Init(const TArray<AOperativeCharacter*>& Members, const FString& LevelTitle, const FString& NextLevelTitle,
		TSubclassOf<UProfileDialogWidget> ProfileClass);

	/** Goes on (the "Continue >" click); once. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Points")
	void Continue();

	/** "Continue >" was clicked: the next level may open. */
	FSimpleMulticastDelegate OnContinue;

	UProfileDialogWidget* GetProfile() const { return Profile; }

	/** The operative on the sheet now. */
	AOperativeCharacter* GetShownMember() const;

	/** The live portrait is rendered (false where the game does not render: the role letter stands in). */
	bool HasPortraitImage() const;

	/** The portrait camera's operative (nullptr without a live portrait). */
	AOperativeCharacter* GetPortraitTarget() const;

	FString GetTitleText() const;
	FString GetHintText() const;
	bool HasContinued() const { return bContinued; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

private:
	void BuildDefaultLayout();
	void FollowShownMember();
	void HandleProfileContinue();

	UPROPERTY()
	TObjectPtr<UProfileDialogWidget> Profile;

	UPROPERTY()
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY()
	TObjectPtr<UTextBlock> HintText;

	UPROPERTY()
	TObjectPtr<UImage> PortraitImage;

	UPROPERTY()
	TObjectPtr<UTextBlock> PortraitLetter;

	UPROPERTY()
	TObjectPtr<UTextBlock> PortraitName;

	UPROPERTY()
	TObjectPtr<USizeBox> ProfileSlot;

	UPROPERTY(Transient)
	TObjectPtr<AOperativePortraitCapture> Capture;

	TWeakObjectPtr<AOperativeCharacter> PortraitMember;
	bool bContinued = false;
};
