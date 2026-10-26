#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Formation/FormationMenuRules.h"
#include "FormationMenuWidget.generated.h"

class UTexture2D;

/**
 * The formation radial menu (stage 7; hold B, user decision 2026-10-26). Godot reference: none (UE-only; design
 * docs/design/FORMATION_SECTORS.md "Selection", contract docs/design/FORMATION_CONTRACT.md 9, TS reference
 * Tools/TrainingGround/src/movement/menu.ts). Drawn in NativePaint like UHordeArrowsWidget: 8 item discs on a ring clockwise
 * from 12 (FormationMenuRules order: line, diamond, echelon right, blizzard, column, hedgehog, echelon left, all forward) with
 * their schematic icons (T_FormationMenu_<id>, imported from Content/UI/FormationMenu/src/*.svg by
 * Scripts/Editor/import_formation_menu_icons.py) and English labels, Auto in the centre (the dead zone). The highlighted item is
 * lit and enlarged, items the member count does not allow are greyed, the current choice is ringed green. Sizes are widget
 * units: the project's DPI curve scales them for 1080p / 1440p / 4K. Never takes input (the controller feeds it the state); built
 * lazily by ACodexTacticsPlayerController (CodexTacticsPlayerControllerFormationMenu.cpp), never pauses the game.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UFormationMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Shows the menu centred on Centre (widget units, see GetViewSize); loads the icons the first time. */
	void ShowMenu(const FVector2D& Centre);
	/** Hides it (collapsed). */
	void HideMenu();
	/** The menu is shown. */
	bool IsMenuShown() const { return bShown; }

	/** What to draw: the highlighted item (unset = Auto), the squad's current choice (unset = Auto), the member count (labels / greying). */
	void SetMenuView(const TOptional<EFormationMenuItem>& Highlighted, const TOptional<EFormationMenuItem>& Current, int32 MemberCount);
	TOptional<EFormationMenuItem> GetHighlighted() const { return Highlighted; }
	TOptional<EFormationMenuItem> GetCurrentChoice() const { return CurrentChoice; }

	/** Menu centre, widget units. */
	FVector2D GetCentre() const { return Centre; }
	/** The view in widget units (the viewport / its DPI scale), or the headless 1920 x 1080. */
	FVector2D GetViewSize() const;
	/** The mouse cursor in widget units; false when there is none (headless, no focus). */
	bool GetCursorPosition(FVector2D& OutCursor) const;
	/** Moves the OS cursor to Position (widget units); false without a viewport. */
	bool WarpCursorTo(const FVector2D& Position) const;
	/** Half the box the menu needs round its centre (ring + item discs + labels), widget units: ClampCentre keeps it on screen. */
	FVector2D GetHalfExtent() const;
	/** Icons found (8 items + Auto; 0 when the textures are not imported). */
	int32 GetLoadedIconCount() const;

	/** Radius of the ring the item discs sit on, widget units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|FormationMenu", meta = (ClampMin = "40"))
	float RingRadius = 150.f;
	/** Radius of an item disc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|FormationMenu", meta = (ClampMin = "10"))
	float ItemRadius = 44.f;
	/** Radius of the centre (Auto) disc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|FormationMenu", meta = (ClampMin = "10"))
	float CentreRadius = 40.f;
	/** The cursor within this distance of the centre highlights Auto (FormationMenuRules::ItemAt dead zone), widget units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|FormationMenu", meta = (ClampMin = "0"))
	float DeadZone = 40.f;
	/** Icon size inside a disc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|FormationMenu", meta = (ClampMin = "8"))
	float IconSize = 56.f;
	/** Item label font size (bold). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|FormationMenu", meta = (ClampMin = "6"))
	int32 LabelFontSize = 13;
	/** Content folder of the icon textures (T_FormationMenu_<id>). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|FormationMenu")
	FString IconFolder = TEXT("/Game/UI/FormationMenu");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|FormationMenu")
	FLinearColor BackdropColor = FLinearColor(0.02f, 0.035f, 0.05f, 0.62f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|FormationMenu")
	FLinearColor DiscColor = FLinearColor(0.09f, 0.14f, 0.19f, 0.92f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|FormationMenu")
	FLinearColor HighlightColor = FLinearColor(1.f, 0.72f, 0.22f, 0.97f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|FormationMenu")
	FLinearColor IconColor = FLinearColor(0.88f, 0.95f, 1.f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|FormationMenu")
	FLinearColor DisabledColor = FLinearColor(0.42f, 0.44f, 0.46f, 0.75f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|FormationMenu")
	FLinearColor CurrentColor = FLinearColor(0.32f, 0.95f, 0.5f, 1.f);

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void LoadIcons();
	/** Icon of an item / Auto (unset); null when not imported. */
	UTexture2D* IconFor(const TOptional<EFormationMenuItem>& Item) const;

	/** 8 items (FormationMenuRules order) + Auto last. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> Icons;

	bool bShown = false;
	bool bIconsLoaded = false;
	FVector2D Centre = FVector2D::ZeroVector;
	TOptional<EFormationMenuItem> Highlighted;
	TOptional<EFormationMenuItem> CurrentChoice;
	int32 MemberCount = 0;
};
