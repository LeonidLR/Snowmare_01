#include "UI/CodexTacticsHUD.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Characters/RecruitSubsystem.h"
#include "CanvasItem.h"
#include "Characters/DefenseMarkerSubsystem.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/OverheadLabel.h"
#include "Core/CodexTacticsPlayerController.h"
#include "GameFramework/Character.h"
#include "EngineUtils.h"
#include "Combat/KnockdownComponent.h"
#include "GameFramework/Character.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Interactables/InteractableActor.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/PanicComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Core/MissionSubsystem.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/InteractionSubsystem.h"
#include "UI/ActionMenuWidget.h"
#include "UI/LootDialogWidget.h"
#include "UI/ActionBarWidget.h"
#include "UI/InventoryDrawerWidget.h"
#include "UI/QuantitySplitDialogWidget.h"
#include "UI/InventoryDragDropOperation.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Interactables/DroppedItemActor.h"
#include "Interactables/ItemStashComponent.h"
#include "Interactables/LootCrateActor.h"
#include "Characters/SquadTransferSubsystem.h"
#include "Characters/TransferRules.h"
#include "UI/GameMessageSubsystem.h"
#include "UI/ProfileDialogWidget.h"
#include "UI/FrostVignetteWidget.h"
#include "UI/VictoryPanelWidget.h"
#include "Combat/WaveSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "UI/PauseMenuWidget.h"
#include "Kismet/GameplayStatics.h"
#include "UI/SaveLoadDialogWidget.h"
#include "UI/DialogueSubsystem.h"
#include "Core/MissionSubsystem.h"
#include "UI/PhaseBannersWidget.h"
#include "UI/TurnBasedHudWidget.h"
#include "UI/DialogueSubsystem.h"
#include "UI/DialogueWidget.h"
#include "UI/MainMenuWidget.h"
#include "UI/MissionFailedWidget.h"
#include "HAL/IConsoleManager.h"
#include "Survival/ColdSurvivalComponent.h"
#include "UI/GameMessageSubsystem.h"
#include "Combat/CombatTimeModeRules.h"
#include "Characters/FirePostureRules.h"
#include "Combat/HordeSubsystem.h"

namespace
{
	TAutoConsoleVariable<bool> CVarShowStatus(
		TEXT("CodexTactics.HUD.ShowStatus"), true,
		TEXT("Show the squad status panel and the labels above operatives."));

	constexpr float Margin = 16.f;
	constexpr float LinePadding = 3.f;
	const FLinearColor PanelColor(0.f, 0.f, 0.f, 0.55f);
	const FLinearColor SpeakerColor(1.f, 0.85f, 0.35f);
	const FLinearColor TextColor(0.92f, 0.94f, 0.96f);
	const FLinearColor WarningColor(1.f, 0.45f, 0.35f);
	// Godot StyleBoxFlat_obj + ObjectiveLabel.
	const FLinearColor ObjectivePanelColor = ACodexTacticsHUD::GodotColor(0.05f, 0.06f, 0.08f, 0.8f);
	const FLinearColor ObjectiveFrameColor = ACodexTacticsHUD::GodotColor(0.8f, 0.65f, 0.2f, 1.f);
	const FLinearColor ObjectiveTextColor = ACodexTacticsHUD::GodotColor(1.f, 0.9f, 0.5f);

	const TCHAR* PhaseName(ECodexGamePhase Phase)
	{
		switch (Phase)
		{
		case ECodexGamePhase::Exploration: return TEXT("EXPLORATION");
		case ECodexGamePhase::Cutscene: return TEXT("CUTSCENE");
		case ECodexGamePhase::Preparation: return TEXT("PREPARATION");
		case ECodexGamePhase::WaveCombat: return TEXT("COMBAT");
		case ECodexGamePhase::WaveCleared: return TEXT("WAVE CLEARED");
		case ECodexGamePhase::PostCombat: return TEXT("AFTER COMBAT");
		case ECodexGamePhase::GameOver: return TEXT("MISSION FAILED");
		default: return TEXT("?");
		}
	}

	const TCHAR* ModeName(ECodexCombatMode Mode)
	{
		switch (Mode)
		{
		case ECodexCombatMode::RealTime: return TEXT("REAL TIME");
		case ECodexCombatMode::TacticalPause: return TEXT("TACTICAL PAUSE");
		case ECodexCombatMode::TurnBased: return TEXT("TURN-BASED");
		default: return TEXT("");
		}
	}

	const TCHAR* TierName(EColdTier Tier)
	{
		switch (Tier)
		{
		case EColdTier::Chills: return TEXT("chills");
		case EColdTier::Freezing: return TEXT("freezing");
		case EColdTier::Hypothermia: return TEXT("hypothermia");
		case EColdTier::Frostbite: return TEXT("FROSTBITE");
		default: return TEXT("normal");
		}
	}
}

FString ACodexTacticsHUD::StripUnsupportedGlyphs(const FString& Text)
{
	FString Result;
	Result.Reserve(Text.Len());
	for (int32 Index = 0; Index < Text.Len(); ++Index)
	{
		const TCHAR Char = Text[Index];
		const uint32 Code = static_cast<uint32>(Char);
		if (Code == 0x2192 || Code == 0x2794 || Code == 0x279C || Code == 0x27A1)
		{
			Result.Append(TEXT("->")); // arrows («➔») keep their meaning
			continue;
		}
		if (Code == 0x25C0)
		{
			Result.AppendChar(TEXT('<')); // «◀ EQUIPPED»
			continue;
		}
		const bool bSurrogate = Code >= 0xD800 && Code <= 0xDFFF; // emoji outside the BMP
		const bool bSymbol = (Code >= 0x2190 && Code <= 0x2BFF) || Code == 0xFE0F || Code == 0x200D || Code == 0x20E3; // 0x20E3: keycap «2️⃣»
		if (!bSurrogate && !bSymbol)
		{
			Result.AppendChar(Char);
		}
	}
	Result.TrimStartAndEndInline();
	return Result;
}

ACodexTacticsHUD::ACodexTacticsHUD()
{
	ActionMenuWidgetClass = UActionMenuWidget::StaticClass();
	LootDialogWidgetClass = ULootDialogWidget::StaticClass();
	MissionFailedWidgetClass = UMissionFailedWidget::StaticClass();
	MainMenuWidgetClass = UMainMenuWidget::StaticClass();
	DialogueWidgetClass = UDialogueWidget::StaticClass();
	ActionBarWidgetClass = UActionBarWidget::StaticClass();
	InventoryDrawerWidgetClass = UInventoryDrawerWidget::StaticClass();
	QuantitySplitDialogWidgetClass = UQuantitySplitDialogWidget::StaticClass();
	ProfileDialogWidgetClass = UProfileDialogWidget::StaticClass();
	VictoryPanelWidgetClass = UVictoryPanelWidget::StaticClass();
	PauseMenuWidgetClass = UPauseMenuWidget::StaticClass();
	SaveLoadDialogWidgetClass = USaveLoadDialogWidget::StaticClass();
	PhaseBannersWidgetClass = UPhaseBannersWidget::StaticClass();
	TurnBasedHudWidgetClass = UTurnBasedHudWidget::StaticClass();
}

void ACodexTacticsHUD::BeginPlay()
{
	Super::BeginPlay();
	if (ActionMenuWidgetClass && GetOwningPlayerController())
	{
		ActionMenu = CreateWidget<UActionMenuWidget>(GetOwningPlayerController(), ActionMenuWidgetClass);
		if (ActionMenu)
		{
			ActionMenu->AddToViewport(10);
			ActionMenu->HideMenu();
		}
	}
	if (LootDialogWidgetClass && GetOwningPlayerController())
	{
		LootDialog = CreateWidget<ULootDialogWidget>(GetOwningPlayerController(), LootDialogWidgetClass);
		if (LootDialog)
		{
			LootDialog->AddToViewport(11);
			LootDialog->HideDialog();
		}
	}
	if (MissionFailedWidgetClass && GetOwningPlayerController())
	{
		MissionFailed = CreateWidget<UMissionFailedWidget>(GetOwningPlayerController(), MissionFailedWidgetClass);
		if (MissionFailed)
		{
			MissionFailed->AddToViewport(20);
			MissionFailed->HideScreen();
		}
	}
	if (MainMenuWidgetClass && GetOwningPlayerController())
	{
		MainMenu = CreateWidget<UMainMenuWidget>(GetOwningPlayerController(), MainMenuWidgetClass);
		if (MainMenu)
		{
			MainMenu->AddToViewport(30);
			MainMenu->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	if (ActionBarWidgetClass && GetOwningPlayerController())
	{
		ActionBar = CreateWidget<UActionBarWidget>(GetOwningPlayerController(), ActionBarWidgetClass);
		if (ActionBar)
		{
			ActionBar->AddToViewport(5);
		}
	}
	if (InventoryDrawerWidgetClass && GetOwningPlayerController())
	{
		InventoryDrawer = CreateWidget<UInventoryDrawerWidget>(GetOwningPlayerController(), InventoryDrawerWidgetClass);
		if (InventoryDrawer)
		{
			InventoryDrawer->AddToViewport(6);
		}
	}
	if (QuantitySplitDialogWidgetClass && GetOwningPlayerController())
	{
		QuantityDialog = CreateWidget<UQuantitySplitDialogWidget>(GetOwningPlayerController(), QuantitySplitDialogWidgetClass);
		if (QuantityDialog)
		{
			QuantityDialog->AddToViewport(7); // over the drawer it was dragged from
		}
	}
	if (GetOwningPlayerController())
	{
		// Under every other widget, over the game view (Godot FrostOverlay sits under the later UI panels).
		FrostVignette = CreateWidget<UFrostVignetteWidget>(GetOwningPlayerController(), UFrostVignetteWidget::StaticClass());
		if (FrostVignette)
		{
			FrostVignette->AddToViewport(1);
		}
	}
	if (VictoryPanelWidgetClass && GetOwningPlayerController())
	{
		// Under the profile (Godot adds the profile dialog to the UI after the victory panel).
		VictoryPanel = CreateWidget<UVictoryPanelWidget>(GetOwningPlayerController(), VictoryPanelWidgetClass);
		if (VictoryPanel)
		{
			VictoryPanel->AddToViewport(11);
		}
	}
	if (ProfileDialogWidgetClass && GetOwningPlayerController())
	{
		ProfileDialog = CreateWidget<UProfileDialogWidget>(GetOwningPlayerController(), ProfileDialogWidgetClass);
		if (ProfileDialog)
		{
			ProfileDialog->AddToViewport(12);
		}
	}
	if (PauseMenuWidgetClass && GetOwningPlayerController())
	{
		PauseMenu = CreateWidget<UPauseMenuWidget>(GetOwningPlayerController(), PauseMenuWidgetClass);
		if (PauseMenu)
		{
			PauseMenu->AddToViewport(40);
		}
	}
	if (SaveLoadDialogWidgetClass && GetOwningPlayerController())
	{
		SaveLoadDialog = CreateWidget<USaveLoadDialogWidget>(GetOwningPlayerController(), SaveLoadDialogWidgetClass);
		if (SaveLoadDialog)
		{
			SaveLoadDialog->AddToViewport(41);
		}
	}
	if (PhaseBannersWidgetClass && GetOwningPlayerController())
	{
		PhaseBanners = CreateWidget<UPhaseBannersWidget>(GetOwningPlayerController(), PhaseBannersWidgetClass);
		if (PhaseBanners)
		{
			PhaseBanners->AddToViewport(8);
			PhaseBanners->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}
	if (TurnBasedHudWidgetClass && GetOwningPlayerController())
	{
		TurnBasedHud = CreateWidget<UTurnBasedHudWidget>(GetOwningPlayerController(), TurnBasedHudWidgetClass);
		if (TurnBasedHud)
		{
			TurnBasedHud->AddToViewport(9); // shows itself while the grid fight runs
		}
	}
	if (DialogueWidgetClass && GetOwningPlayerController())
	{
		Dialogue = CreateWidget<UDialogueWidget>(GetOwningPlayerController(), DialogueWidgetClass);
		if (Dialogue)
		{
			Dialogue->AddToViewport(15);
			Dialogue->Refresh();
		}
	}
	if (UDialogueSubsystem* Dialogues = GetWorld()->GetSubsystem<UDialogueSubsystem>())
	{
		Dialogues->OnDialogueChanged.AddDynamic(this, &ACodexTacticsHUD::HandleDialogueChanged);
	}
	if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
	{
		Mission->OnMissionFailed.AddDynamic(this, &ACodexTacticsHUD::HandleMissionFailed);
		Mission->OnMainMenuChanged.AddDynamic(this, &ACodexTacticsHUD::HandleMainMenuChanged);
		HandleMainMenuChanged(Mission->IsMainMenuOpen()); // the mission decides before the HUD begins play
	}
	if (UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
	{
		Interactions->OnActionMenuChanged.AddDynamic(this, &ACodexTacticsHUD::HandleActionMenuChanged);
		Interactions->OnLootDialogChanged.AddDynamic(this, &ACodexTacticsHUD::HandleLootDialogChanged);
	}
	if (UWaveSubsystem* Waves = GetWorld()->GetSubsystem<UWaveSubsystem>())
	{
		Waves->OnWaveCleared.AddDynamic(this, &ACodexTacticsHUD::HandleWaveCleared);
	}
}

void ACodexTacticsHUD::ToggleProfileDialog()
{
	if (!ProfileDialog)
	{
		return;
	}
	if (ProfileDialog->IsOpen())
	{
		ProfileDialog->Close();
	}
	else
	{
		ProfileDialog->Open(nullptr);
	}
}

void ACodexTacticsHUD::OpenProfileDialog(AOperativeCharacter* Member)
{
	if (ProfileDialog)
	{
		ProfileDialog->Open(Member);
	}
}

void ACodexTacticsHUD::HandleWaveCleared(int32 WaveIndex)
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		if (Member->UnspentStatPoints > 0)
		{
			OpenProfileDialog(Member);
			return;
		}
	}
}

void ACodexTacticsHUD::HandleLootDialogChanged(bool bOpen, ALootCrateActor* Crate)
{
	if (!LootDialog)
	{
		return;
	}
	if (bOpen && Crate)
	{
		LootDialog->ShowCrate(Crate);
	}
	else
	{
		LootDialog->HideDialog();
	}
}

void ACodexTacticsHUD::HandleMissionFailed(const FText& Reason)
{
	if (MissionFailed)
	{
		MissionFailed->ShowFailure(Reason);
	}
}

void ACodexTacticsHUD::HandleDialogueChanged(bool bOpen)
{
	if (Dialogue)
	{
		Dialogue->Refresh();
	}
}

void ACodexTacticsHUD::HandleMainMenuChanged(bool bOpen)
{
	if (MainMenu)
	{
		MainMenu->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (bOpen)
		{
			MainMenu->RefreshModeButtons(); // "Start combat" hidden on ambush levels
		}
	}
	// Godot: the tactical bar is hidden until the game starts.
	if (ActionBar)
	{
		ActionBar->SetVisibility(bOpen ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
}

void ACodexTacticsHUD::HandleActionMenuChanged(bool bOpen, const FActionMenuSpec& Menu)
{
	if (!ActionMenu)
	{
		return;
	}
	if (bOpen)
	{
		ActionMenu->ShowMenu(Menu);
	}
	else
	{
		ActionMenu->HideMenu();
	}
}

void ACodexTacticsHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !GEngine)
	{
		return;
	}
	if (CVarShowStatus.GetValueOnGameThread())
	{
		DrawOperativeLabels();
	}
	DrawWorldLabels();
	DrawDefenseMarkers();
	DrawSpaceCharge();
	DrawCombatModeBadge();
	DrawPostureMarkers();
	DrawKnockdownBars();
	DrawHordeWarning();
	DrawHitChanceLabel();
	DrawSelectionBox();
	DrawFloatingTexts(); // over the name plates (Godot Label3D no_depth_test), under the panels
	DrawMessageFeed();
	const float ObjectiveBottom = DrawObjectiveBanner();
	if (CVarShowStatus.GetValueOnGameThread())
	{
		DrawSquadPanel(ObjectiveBottom + Margin * 0.5f);
	}
	UpdateFrostVignette();
}

void ACodexTacticsHUD::DrawDefenseMarkers()
{
	const UDefenseMarkerSubsystem* Markers = GetWorld()->GetSubsystem<UDefenseMarkerSubsystem>();
	if (!Markers)
	{
		return;
	}
	for (const FDefenseMarkerView& Marker : Markers->GetMarkers())
	{
		const FVector Screen = Project(Marker.IconLocation, true);
		if (Screen.Z <= 0.f || Marker.Alpha <= 0.01f)
		{
			continue;
		}
		// A shield: a rounded-top body (rectangle) over a point (triangle), green fill, light rim; it scales in with the fade.
		const float Scale = FMath::Lerp(0.6f, 1.f, Marker.Alpha);
		const float W = 26.f * Scale;
		const float Top = 18.f * Scale;
		const float Tip = 16.f * Scale;
		const FVector2D C(Screen.X, Screen.Y);
		const FLinearColor Fill(0.1f, 0.85f, 0.35f, 0.75f * Marker.Alpha);
		const FLinearColor Rim(0.85f, 1.f, 0.9f, Marker.Alpha);
		auto Shape = [&](float Grow, const FLinearColor& Color)
		{
			const FVector2D TL(C.X - W * 0.5f - Grow, C.Y - Top - Grow);
			FCanvasTileItem Body(TL, FVector2D(W + Grow * 2.f, Top + Grow), Color);
			Body.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Body);
			FCanvasTriangleItem Point(FVector2D(C.X - W * 0.5f - Grow, C.Y), FVector2D(C.X + W * 0.5f + Grow, C.Y),
				FVector2D(C.X, C.Y + Tip + Grow * 1.6f), Canvas->DefaultTexture ? Canvas->DefaultTexture->GetResource() : nullptr);
			Point.SetColor(Color);
			Point.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Point);
		};
		Shape(2.f, Rim);
		Shape(0.f, Fill);
		// A white cross-bar on the shield (the «guard» mark).
		FCanvasTileItem Bar(FVector2D(C.X - W * 0.3f, C.Y - Top * 0.55f), FVector2D(W * 0.6f, 3.f * Scale), Rim);
		Bar.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Bar);
		FCanvasTileItem Post(FVector2D(C.X - 1.5f * Scale, C.Y - Top * 0.8f), FVector2D(3.f * Scale, Top * 0.8f + Tip * 0.6f), Rim);
		Post.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Post);
		// «HOLD LINE ×N» under it.
		UFont* Font = GEngine->GetSmallFont();
		const FString Text = Marker.Defenders > 1 ? FString::Printf(TEXT("HOLD LINE x%d"), Marker.Defenders) : FString(TEXT("HOLD LINE"));
		float TW = 0.f;
		float TH = 0.f;
		Canvas->StrLen(Font, Text, TW, TH);
		FCanvasTextItem Label(FVector2D(C.X - TW * 0.5f, C.Y + Tip + 6.f), FText::FromString(Text), Font, FLinearColor(0.6f, 1.f, 0.7f, Marker.Alpha));
		Label.bOutlined = true;
		Label.OutlineColor = FLinearColor(0.f, 0.f, 0.f, Marker.Alpha);
		Canvas->DrawItem(Label);
	}
}

void ACodexTacticsHUD::DrawSelectionBox()
{
	// Godot main.gd _on_selection_box_draw: cyan fill 0.18, border 1.5 px, 12 px corner marks.
	const ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(GetOwningPlayerController());
	FVector2D Min;
	FVector2D Max;
	if (!PC || !PC->GetSelectionBox(Min, Max))
	{
		return;
	}
	const FVector2D Size = Max - Min;
	FCanvasTileItem Fill(Min, Size, FLinearColor(0.2f, 0.75f, 1.f, 0.18f));
	Fill.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Fill);
	FCanvasBoxItem Border(Min, Size);
	Border.SetColor(FLinearColor(0.3f, 0.9f, 1.f, 0.9f));
	Border.LineThickness = 1.5f;
	Canvas->DrawItem(Border);
	const float Corner = FMath::Min(12.f, FMath::Min(Size.X, Size.Y) * 0.3f);
	if (Corner > 3.f)
	{
		const FLinearColor CornerColor(0.6f, 1.f, 1.f, 1.f);
		for (const FVector2D& Point : { Min, FVector2D(Max.X, Min.Y), FVector2D(Min.X, Max.Y), Max })
		{
			const float DirX = Point.X == Min.X ? 1.f : -1.f;
			const float DirY = Point.Y == Min.Y ? 1.f : -1.f;
			DrawLine(Point.X, Point.Y, Point.X + DirX * Corner, Point.Y, CornerColor, 2.5f);
			DrawLine(Point.X, Point.Y, Point.X, Point.Y + DirY * Corner, CornerColor, 2.5f);
		}
	}
}

void ACodexTacticsHUD::DrawHitChanceLabel()
{
	// Godot tactical_grid_overlay.gd hit_chance_label: yellow «🎯 N% | 💥 D» 1.6 m above the hovered attack cell.
	const UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>();
	FVector World;
	FString Text;
	if (!TurnBased || !TurnBased->IsActive() || !TurnBased->GetHoverHitChance(World, Text))
	{
		return;
	}
	const FVector Screen = Project(World);
	if (Screen.Z <= 0.f)
	{
		return;
	}
	UFont* Font = GEngine->GetLargeFont();
	const FString Clean = StripUnsupportedGlyphs(Text);
	float W = 0.f;
	float H = 0.f;
	Canvas->StrLen(Font, Clean, W, H);
	FCanvasTextItem Item(FVector2D(Screen.X - W * 0.5f, Screen.Y - H * 0.5f), FText::FromString(Clean), Font, FLinearColor(1.f, 0.9f, 0.2f));
	Item.EnableShadow(FLinearColor(0.1f, 0.1f, 0.1f, 0.95f));
	Item.bOutlined = true;
	Item.OutlineColor = FLinearColor(0.1f, 0.1f, 0.1f, 0.95f);
	Canvas->DrawItem(Item);
}

float ACodexTacticsHUD::GetSquadMaxCold() const
{
	float MaxCold = 0.f;
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	for (const AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		MaxCold = FMath::Max(MaxCold, Member->ColdLevel);
	}
	return MaxCold;
}

void ACodexTacticsHUD::UpdateFrostVignette()
{
	// Godot UI/FrostOverlay (frost_vignette.gdshader): cold_pct = the coldest operative.
	if (FrostVignette)
	{
		FrostVignette->SetColdPct(GetSquadMaxCold());
	}
}

TArray<FString> ACodexTacticsHUD::WrapText(const FString& Text, UFont* Font, float Scale, float MaxWidth) const
{
	TArray<FString> Lines;
	TArray<FString> Words;
	Text.ParseIntoArrayWS(Words);
	FString Current;
	for (const FString& Word : Words)
	{
		const FString Candidate = Current.IsEmpty() ? Word : Current + TEXT(" ") + Word;
		float Width = 0.f;
		float Height = 0.f;
		Canvas->StrLen(Font, Candidate, Width, Height);
		if (Width * Scale > MaxWidth && !Current.IsEmpty())
		{
			Lines.Add(Current);
			Current = Word;
		}
		else
		{
			Current = Candidate;
		}
	}
	if (!Current.IsEmpty())
	{
		Lines.Add(Current);
	}
	return Lines;
}

void ACodexTacticsHUD::DrawSpaceCharge()
{
	const ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(GetOwningPlayerController());
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (!PC || !Flow || !PC->IsSpaceHeld() || PC->GetSpaceHeldTime() < 0.15f)
	{
		return;
	}
	// Godot: 300 x 80 in the centre, a 14 px label over a 280 x 22 bar. The caption says what the hold does now
	// (FCombatTimeModeRules: enter the turn-based fight / back to real time); nothing to show when the hold does nothing.
	const float Held = PC->GetSpaceHeldTime();
	const float Limit = FCombatTimeModeRules::GetHoldSeconds(Flow->GetCombatMode(), Flow->GetConfig());
	const FString Caption = FCombatTimeModeRules::GetHoldCaption(Flow->GetPhase(), Flow->GetCombatMode(), Flow->GetConfig());
	if (Caption.IsEmpty())
	{
		return;
	}
	const bool bEntering = Flow->GetCombatMode() != ECodexCombatMode::TurnBased;
	const FString Raw = FString::Printf(TEXT("%s: %.1fc / %.1fc"), *Caption, Held, Limit);
	const FString Label = StripUnsupportedGlyphs(Raw).TrimStartAndEnd();
	UFont* Font = GEngine->GetSmallFont();
	float W = 0.f;
	float H = 0.f;
	Canvas->StrLen(Font, Label, W, H);
	const float X = Canvas->SizeX * 0.5f;
	const float Y = Canvas->SizeY * 0.5f - 40.f;
	FCanvasTextItem Item(FVector2D(X - W * 0.55f, Y), FText::FromString(Label), Font,
		bEntering ? FLinearColor(0.2f, 0.9f, 1.f) : FLinearColor(1.f, 0.8f, 0.2f));
	Item.Scale = FVector2D(1.1f, 1.1f);
	Item.bOutlined = true;
	Item.OutlineColor = FLinearColor::Black;
	Canvas->DrawItem(Item);
	const float BarY = Y + H * 1.1f + 6.f;
	DrawRect(FLinearColor(0.1f, 0.1f, 0.12f, 0.85f), X - 140.f, BarY, 280.f, 22.f);
	DrawRect(FLinearColor(0.3f, 0.55f, 0.85f, 0.95f), X - 138.f, BarY + 2.f, 276.f * FCombatTimeModeRules::GetHoldProgress(Held, Limit), 18.f);
}

void ACodexTacticsHUD::DrawCombatModeBadge()
{
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	const UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>();
	if (!Flow || !Squad || Squad->GetMembers().IsEmpty() || (Mission && Mission->IsMainMenuOpen()))
	{
		return;
	}
	UFont* Font = GEngine->GetSmallFont();
	const float CenterX = Canvas->SizeX * 0.5f;
	float Y = 20.f;
	auto DrawCentered = [&](const FString& Text, const FLinearColor& Color, const FLinearColor& Back, float Scale)
	{
		float W = 0.f;
		float H = 0.f;
		Canvas->StrLen(Font, Text, W, H);
		W *= Scale;
		H *= Scale;
		DrawRect(Back, CenterX - W * 0.5f - 10.f, Y - 3.f, W + 20.f, H + 6.f);
		FCanvasTextItem Item(FVector2D(CenterX - W * 0.5f, Y), FText::FromString(Text), Font, Color);
		Item.Scale = FVector2D(Scale, Scale);
		Item.bOutlined = true;
		Item.OutlineColor = FLinearColor::Black;
		Canvas->DrawItem(Item);
		Y += H + 10.f;
	};
	// Combat time mode (user request 2026-10-06): «REAL TIME» / «TACTICAL PAUSE» / «TURN-BASED» with the keys.
	const FString Mode = FCombatTimeModeRules::GetModeLabel(Flow->GetPhase(), Flow->GetCombatMode());
	if (!Mode.IsEmpty())
	{
		const ECodexCombatMode CombatMode = Flow->GetCombatMode();
		const FString Hint = CombatMode == ECodexCombatMode::TurnBased
			? FString::Printf(TEXT("hold SPACE %.1fs — real time"), FCombatTimeModeRules::GetHoldSeconds(CombatMode, Flow->GetConfig()))
			: FString::Printf(TEXT("SPACE — %s  ·  hold %.1fs — turn-based"),
				CombatMode == ECodexCombatMode::TacticalPause ? TEXT("resume") : TEXT("pause"),
				FCombatTimeModeRules::GetHoldSeconds(CombatMode, Flow->GetConfig()));
		const FLinearColor Color = CombatMode == ECodexCombatMode::TacticalPause ? FLinearColor(1.f, 0.85f, 0.25f)
			: (CombatMode == ECodexCombatMode::TurnBased ? FLinearColor(0.45f, 0.8f, 1.f) : FLinearColor(0.4f, 1.f, 0.5f));
		DrawCentered(FString::Printf(TEXT("%s   [%s]"), *Mode, *Hint), Color, FLinearColor(0.f, 0.f, 0.f, 0.55f), 1.25f);
	}
	// Fire posture (user decisions 2026-10-06): the posture of the SELECTED operative(s) (the controlled leader, or the
	// box-selected group), the key hints; Alt + key = the whole squad.
	TArray<ESquadFirePosture> Selected;
	FString Who;
	const TArray<AOperativeCharacter*> Targets = Squad->GetPostureOrderTargets();
	for (const AOperativeCharacter* Member : Targets)
	{
		Selected.Add(Squad->GetEffectivePosture(Member));
	}
	if (Targets.Num() == 1)
	{
		Who = Targets[0]->DisplayName.ToString();
	}
	else if (Targets.Num() > 1)
	{
		Who = FString::Printf(TEXT("group %d"), Targets.Num());
	}
	ESquadFirePosture Posture = Squad->GetSquadPosture();
	const bool bCommon = FirePostureRules::GetCommonPosture(Selected, Posture) || Selected.IsEmpty();
	const FString Line = FString::Printf(TEXT("FIRE%s: %s  [,] pas [.] def [/] agg · Alt: squad"),
		Who.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Who), bCommon ? *FirePostureRules::GetLabel(Posture) : TEXT("MIXED"));
	const FLinearColor PostureColor = !bCommon ? FLinearColor(0.85f, 0.85f, 0.85f) : PostureMarkerColor(Posture);
	DrawCentered(Line, PostureColor, FLinearColor(0.f, 0.f, 0.f, 0.45f), 1.f);
}

FLinearColor ACodexTacticsHUD::PostureMarkerColor(ESquadFirePosture Posture)
{
	return Posture == ESquadFirePosture::Passive ? FLinearColor(0.7f, 0.75f, 0.8f)
		: (Posture == ESquadFirePosture::Defensive ? FLinearColor(1.f, 0.8f, 0.3f) : FLinearColor(1.f, 0.4f, 0.3f));
}

void ACodexTacticsHUD::DrawPostureMarkers()
{
	// Per-operative fire posture marker (user request 2026-10-06): a small letter P / D / A in a dark box over the head,
	// in the posture colour; the selected operative(s) get a brighter frame. Hidden under the start menu.
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	const UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>();
	if (!Squad || (Mission && Mission->IsMainMenuOpen()))
	{
		return;
	}
	UFont* Font = GEngine->GetSmallFont();
	const TArray<AOperativeCharacter*> Targets = Squad->GetPostureOrderTargets();
	for (const AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (!Member || !Member->HealthComponent || !Member->HealthComponent->IsAlive() || Member->IsHidden())
		{
			continue;
		}
		const float Top = Member->GetSimpleCollisionHalfHeight() + 25.f;
		const FVector Screen = Project(Member->GetActorLocation() + FVector(0.f, 0.f, Top), true);
		if (Screen.Z <= 0.f)
		{
			continue;
		}
		const ESquadFirePosture Posture = Squad->GetEffectivePosture(Member);
		const FString Letter = FirePostureRules::GetLetter(Posture);
		float W = 0.f;
		float H = 0.f;
		Canvas->StrLen(Font, Letter, W, H);
		const float Box = FMath::Max(W, H) + 6.f;
		const float X = Screen.X - Box * 0.5f;
		const float Y = Screen.Y - Box;
		const bool bSelected = Targets.Contains(Member);
		const FLinearColor Color = PostureMarkerColor(Posture);
		DrawRect(bSelected ? Color : Color * FLinearColor(0.6f, 0.6f, 0.6f, 0.8f), X - 1.f, Y - 1.f, Box + 2.f, Box + 2.f);
		DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.85f), X, Y, Box, Box);
		FCanvasTextItem Item(FVector2D(Screen.X - W * 0.5f, Y + (Box - H) * 0.5f), FText::FromString(Letter), Font, Color);
		Canvas->DrawItem(Item);
	}
}

void ACodexTacticsHUD::DrawKnockdownBars()
{
	// Sprint 14 (TANDEM request #12): the overhead badge style of the panic / rage badges — a dark panel with the text —
	// and a recovery bar under it that fills over the downed phase (frozen in the tactical pause / dialogue).
	const UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>();
	if (Mission && Mission->IsMainMenuOpen())
	{
		return;
	}
	UFont* Font = GEngine->GetSmallFont();
	static const FString Badge = TEXT("[KNOCKED DOWN]");
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		const UKnockdownComponent* Knockdown = It->FindComponentByClass<UKnockdownComponent>();
		if (!Knockdown || !Knockdown->IsDown() || It->IsHidden())
		{
			continue;
		}
		// Above the name plate / posture marker (the panic / rage badges' place: the man lies, the plate stays up).
		const float Top = It->GetSimpleCollisionHalfHeight() + 40.f;
		FVector Screen = Project(It->GetActorLocation() + FVector(0.f, 0.f, Top), true);
		if (Screen.Z <= 0.f)
		{
			continue;
		}
		float W = 0.f;
		float H = 0.f;
		Canvas->StrLen(Font, Badge, W, H);
		Screen.Y -= H + 14.f;
		const bool bEnemy = It->ActorHasTag(TEXT("Enemy"));
		const FLinearColor BadgeColor = bEnemy ? FLinearColor(1.f, 0.45f, 0.35f) : FLinearColor(1.f, 0.7f, 0.2f);
		const float BarWidth = FMath::Max(W, 70.f);
		const float BarHeight = 5.f;
		const float X = Screen.X - BarWidth * 0.5f;
		DrawRect(PanelColor, X - 4.f, Screen.Y - H - 8.f, BarWidth + 8.f, H + BarHeight + 12.f);
		DrawText(Badge, BadgeColor, Screen.X - W * 0.5f, Screen.Y - H - 6.f, Font);
		const float Fill = FMath::Clamp(Knockdown->GetRecoveryFraction(), 0.f, 1.f);
		DrawRect(FLinearColor(0.15f, 0.15f, 0.18f, 0.9f), X, Screen.Y, BarWidth, BarHeight);
		DrawRect(Knockdown->GetPhase() == EKnockdownPhase::GettingUp ? FLinearColor(0.35f, 0.95f, 0.45f) : BadgeColor,
			X, Screen.Y, BarWidth * Fill, BarHeight);
	}
}

void ACodexTacticsHUD::DrawHordeWarning()
{
	// Horde (user request 2026-10-06): a pulsing «HORDE!» banner under the mode badge and a marker towards the spot it
	// appeared at — a frame round it while on screen, else an arrow at the screen edge (camera-relative direction).
	const UHordeSubsystem* Horde = GetWorld()->GetSubsystem<UHordeSubsystem>();
	FVector Location;
	int32 Count = 0;
	float SecondsLeft = 0.f;
	if (!Horde || !Horde->GetActiveWarning(Location, Count, SecondsLeft) || !PlayerOwner || !PlayerOwner->PlayerCameraManager)
	{
		return;
	}
	const float Pulse = 0.6f + 0.4f * FMath::Abs(FMath::Sin(GetWorld()->GetRealTimeSeconds() * 6.f));
	const FLinearColor Red(1.f, 0.18f, 0.12f, Pulse);
	const float Distance = FVector::Dist2D(Location, Horde->GetLastSquadCentre()) / 100.f;
	UFont* Font = GEngine->GetLargeFont();
	const FString Banner = FString::Printf(TEXT("HORDE!  %d enemies  -  %.0f m"), Count, Distance);
	float W = 0.f;
	float H = 0.f;
	Canvas->StrLen(Font, Banner, W, H);
	const float CenterX = Canvas->SizeX * 0.5f;
	const float BannerY = 92.f;
	DrawRect(FLinearColor(0.12f, 0.f, 0.f, 0.7f), CenterX - W * 0.5f - 16.f, BannerY - 6.f, W + 32.f, H + 12.f);
	FCanvasTextItem Item(FVector2D(CenterX - W * 0.5f, BannerY), FText::FromString(Banner), Font, Red);
	Item.bOutlined = true;
	Item.OutlineColor = FLinearColor::Black;
	Canvas->DrawItem(Item);

	const FVector Projected = Project(Location + FVector(0.f, 0.f, 100.f), true);
	const float EdgeMargin = 60.f;
	const bool bOnScreen = Projected.Z > 0.f && Projected.X > EdgeMargin && Projected.X < Canvas->SizeX - EdgeMargin
		&& Projected.Y > EdgeMargin && Projected.Y < Canvas->SizeY - EdgeMargin;
	if (bOnScreen)
	{
		const float Half = 34.f;
		const FVector2D P(Projected.X, Projected.Y);
		DrawLine(P.X - Half, P.Y - Half, P.X + Half, P.Y - Half, Red, 3.f);
		DrawLine(P.X + Half, P.Y - Half, P.X + Half, P.Y + Half, Red, 3.f);
		DrawLine(P.X + Half, P.Y + Half, P.X - Half, P.Y + Half, Red, 3.f);
		DrawLine(P.X - Half, P.Y + Half, P.X - Half, P.Y - Half, Red, 3.f);
		DrawText(TEXT("HORDE"), Red, P.X - Half, P.Y - Half - 18.f, GEngine->GetSmallFont(), 1.1f);
		return;
	}
	// Off screen: the direction from the squad to the horde, turned into the camera's screen axes (top-down view).
	const FRotator CameraRotation = PlayerOwner->PlayerCameraManager->GetCameraRotation();
	const FVector Forward = CameraRotation.Vector().GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
	const FVector ToHorde = (Location - Horde->GetLastSquadCentre()).GetSafeNormal2D();
	FVector2D Dir(FVector::DotProduct(ToHorde, Right), -FVector::DotProduct(ToHorde, Forward));
	if (!Dir.Normalize())
	{
		return;
	}
	const FVector2D Centre(Canvas->SizeX * 0.5f, Canvas->SizeY * 0.5f);
	const float Scale = FMath::Min((Canvas->SizeX * 0.5f - EdgeMargin) / FMath::Max(FMath::Abs(Dir.X), 0.001f),
		(Canvas->SizeY * 0.5f - EdgeMargin) / FMath::Max(FMath::Abs(Dir.Y), 0.001f));
	const FVector2D Tip = Centre + Dir * Scale;
	const FVector2D Perp(-Dir.Y, Dir.X);
	const FVector2D Base = Tip - Dir * 34.f;
	const FVector2D Left = Base + Perp * 18.f;
	const FVector2D RightCorner = Base - Perp * 18.f;
	DrawLine(Tip.X, Tip.Y, Left.X, Left.Y, Red, 4.f);
	DrawLine(Tip.X, Tip.Y, RightCorner.X, RightCorner.Y, Red, 4.f);
	DrawLine(Left.X, Left.Y, RightCorner.X, RightCorner.Y, Red, 4.f);
	DrawLine(Base.X, Base.Y, (Base - Dir * 30.f).X, (Base - Dir * 30.f).Y, Red, 4.f);
	const FVector2D LabelAt = Base - Dir * 52.f;
	DrawText(TEXT("HORDE"), Red, LabelAt.X - 18.f, LabelAt.Y - 8.f, GEngine->GetSmallFont(), 1.1f);
}

void ACodexTacticsHUD::DrawWorldLabels()
{
	UWorld* World = GetWorld();
	const UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
	const bool bTurnBased = TurnBased && TurnBased->IsActive();
	UFont* Font = GEngine->GetSmallFont();
	auto Draw = [this, Font](const AActor* Actor, const FOverheadLabel& Label)
	{
		FVector Origin;
		FVector Extent;
		Actor->GetActorBounds(true, Origin, Extent);
		const ACharacter* Character = Cast<ACharacter>(Actor);
		const FVector Base = Character ? Actor->GetActorLocation() - FVector(0.f, 0.f, Character->GetSimpleCollisionHalfHeight())
			: FVector(Actor->GetActorLocation().X, Actor->GetActorLocation().Y, Origin.Z - Extent.Z);
		const FVector Screen = Project(Base + FVector(0.f, 0.f, Label.HeightCm), true);
		if (Screen.Z <= 0.f)
		{
			return;
		}
		TArray<FString> Lines;
		StripUnsupportedGlyphs(Label.Text).TrimStartAndEnd().ParseIntoArrayLines(Lines);
		if (Lines.IsEmpty() && Label.bHasMarker)
		{
			Lines.Add(FString()); // marker only (a narrative element seen from afar)
		}
		float LineHeight = 0.f;
		float Width = 0.f;
		for (const FString& Line : Lines)
		{
			float W = 0.f;
			float H = 0.f;
			Canvas->StrLen(Font, Line.TrimStartAndEnd(), W, H);
			Width = FMath::Max(Width, W);
			LineHeight = FMath::Max(LineHeight, H);
		}
		if (LineHeight <= 0.f)
		{
			LineHeight = Font->GetMaxCharHeight();
		}
		const float Marker = Label.bHasMarker ? LineHeight * 0.7f + 4.f : 0.f;
		float Y = Screen.Y - LineHeight * Lines.Num();
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			const FString Line = Lines[Index].TrimStartAndEnd();
			float W = 0.f;
			float H = 0.f;
			Canvas->StrLen(Font, Line, W, H);
			const float X = Screen.X - (W + (Index == 0 ? Marker : 0.f)) * 0.5f;
			if (Index == 0 && Label.bHasMarker)
			{
				DrawRect(Label.MarkerColor, X, Y + LineHeight * 0.15f, LineHeight * 0.7f, LineHeight * 0.7f);
			}
			FCanvasTextItem Item(FVector2D(X + (Index == 0 ? Marker : 0.f), Y), FText::FromString(Line), Font, Label.Color);
			Item.bOutlined = true;
			Item.OutlineColor = FLinearColor::Black;
			Canvas->DrawItem(Item);
			Y += LineHeight;
		}
	};
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		// Godot: the enemies left in stasis outside a turn-based fight hide their plates.
		FOverheadLabel Label;
		if (!It->IsHidden() && (!bTurnBased || TurnBased->GetUnitState(*It)) && It->GetOverheadLabel(Label))
		{
			Draw(*It, Label);
		}
	}
	for (TActorIterator<AInteractableActor> It(World); It; ++It)
	{
		FOverheadLabel Label;
		if (!It->IsHidden() && It->GetOverheadLabel(Label))
		{
			Draw(*It, Label);
		}
	}
}

void ACodexTacticsHUD::DrawFloatingTexts()
{
	UFloatingTextSubsystem* Floating = GetWorld()->GetSubsystem<UFloatingTextSubsystem>();
	if (!Floating)
	{
		return;
	}
	// Godot Label3D: font 20 at pixel_size 0.007, black outline, rising and fading out.
	UFont* Font = GEngine->GetSmallFont();
	const double Now = GetWorld()->GetTimeSeconds();
	for (const FCombatFloatingText& Entry : Floating->GetTexts())
	{
		const float Progress = FMath::Clamp(Entry.GetProgress(Now), 0.f, 1.f);
		const FVector Screen = Project(Entry.Start + FVector(0.f, 0.f, Entry.Rise * Progress), true);
		if (Screen.Z <= 0.f)
		{
			continue;
		}
		const FString Text = StripUnsupportedGlyphs(Entry.Text);
		float W = 0.f;
		float H = 0.f;
		Canvas->StrLen(Font, Text, W, H);
		FLinearColor Color = Entry.Color;
		Color.A *= 1.f - Progress;
		FCanvasTextItem Item(FVector2D(Screen.X - W * 0.65f, Screen.Y - H * 0.65f), FText::FromString(Text), Font, Color);
		Item.Scale = FVector2D(1.3f, 1.3f);
		Item.bOutlined = true;
		Item.OutlineColor = FLinearColor(0.f, 0.f, 0.f, Color.A);
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}
}

void ACodexTacticsHUD::DrawMessageFeed()
{
	const UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	if (!Messages || Messages->GetHistory().IsEmpty())
	{
		return;
	}
	UFont* Font = GEngine->GetSmallFont();
	const float Scale = 1.1f;
	const float Now = GetWorld()->GetRealTimeSeconds();
	const float Width = Canvas->SizeX * FeedWidthFraction;
	const float Left = Canvas->SizeX - Width - Margin;
	const float LineHeight = Font->GetMaxCharHeight() * Scale + LinePadding;

	// Newest last; collect the visible tail first so the panel can be sized.
	struct FFeedLine
	{
		FString Text;
		FLinearColor Color;
	};
	TArray<FFeedLine> Lines;
	const TArray<FGameMessage>& History = Messages->GetHistory();
	int32 Shown = 0;
	for (int32 Index = History.Num() - 1; Index >= 0 && Shown < MaxFeedMessages; --Index)
	{
		const FGameMessage& Message = History[Index];
		if (Now - Message.PostedAt > FeedMessageLifetime)
		{
			break;
		}
		TArray<FFeedLine> Block;
		Block.Add({ StripUnsupportedGlyphs(Message.Speaker.ToString()).ToUpper(), SpeakerColor });
		for (const FString& Line : WrapText(StripUnsupportedGlyphs(Message.Text.ToString()), Font, Scale, Width - 2.f * Margin))
		{
			Block.Add({ Line, TextColor });
		}
		Lines.Insert(Block, 0);
		++Shown;
	}
	if (Lines.IsEmpty())
	{
		return;
	}

	const float Height = Lines.Num() * LineHeight + Shown * LinePadding * 2.f + Margin;
	DrawRect(PanelColor, Left, Margin, Width, Height);
	float Y = Margin + Margin * 0.5f;
	for (const FFeedLine& Line : Lines)
	{
		if (Line.Color == SpeakerColor && Y > Margin * 1.5f)
		{
			Y += LinePadding * 2.f;
		}
		DrawText(Line.Text, Line.Color, Left + Margin, Y, Font, Scale);
		Y += LineHeight;
	}
}

FString ACodexTacticsHUD::DescribeOperative(const AOperativeCharacter& Operative, bool bLeader) const
{
	// Godot squad HUD: ▶ leader, ★ box-selected member.
	const USquadSubsystem* SquadSystem = GetWorld()->GetSubsystem<USquadSubsystem>();
	const bool bSelected = !bLeader && SquadSystem && SquadSystem->HasMultiSelection() && SquadSystem->IsGroupSelected(&Operative);
	FString Line = FString::Printf(TEXT("%s[%d] %s%s  %s"), bSelected ? TEXT("★ ") : TEXT(""), Operative.SquadIndex + 1, *Operative.DisplayName.ToString(),
		bLeader ? TEXT(" <LEADER>") : TEXT(""), *AOperativeCharacter::GetStanceDisplayName(Operative.GetStance()).ToString());
	if (SquadSystem)
	{
		Line += FString::Printf(TEXT("  fire: %s"), *FirePostureRules::GetShortLabel(SquadSystem->GetEffectivePosture(&Operative)));
	}
	if (Operative.TacticalAnchor.Defense.IsActive())
	{
		Line += TEXT("  [HOLD LINE: defending]"); // Sprint 10
	}
	if (Operative.IsSprinting())
	{
		Line += TEXT(" sprinting");
	}
	else if (Operative.IsMoving())
	{
		Line += TEXT(" walking");
	}
	if (const UHealthComponent* Health = Operative.HealthComponent)
	{
		Line += FString::Printf(TEXT("  HP %.0f/%.0f"), Health->GetCurrentHealth(), Health->GetMaxHealth());
	}
	if (const UColdSurvivalComponent* Cold = Operative.ColdSurvival)
	{
		Line += FString::Printf(TEXT("  cold %.0f%% (%s)"), Operative.ColdLevel, TierName(Cold->GetTier()));
		if (Cold->IsNearHeatSource())
		{
			Line += TEXT(" warming");
		}
		if (Cold->IsWeaponFrozen())
		{
			Line += TEXT("  WEAPON FROZEN");
		}
	}
	Line += FString::Printf(TEXT("  ammo %d/%d%s  matches %d  grenades %d"), Operative.CurrentClip, Operative.ReserveAmmo,
		Operative.bIsReloading ? TEXT(" reloading") : TEXT(""), Operative.MatchesCount, Operative.GrenadesCount);
	if (Operative.TurretsCount + Operative.BarricadesCount + Operative.MinesCount > 0)
	{
		Line += FString::Printf(TEXT("  [turrets %d, barricades %d, mines %d]"), Operative.TurretsCount, Operative.BarricadesCount,
			Operative.MinesCount);
	}
	return Line;
}

float ACodexTacticsHUD::DrawObjectiveBanner()
{
	const UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>();
	if (!Mission || Mission->GetObjective().IsEmpty())
	{
		return Margin;
	}
	// Godot ObjectivePanel: at (20, 20), gold 2 px frame, 12 x 8 padding, gold text.
	UFont* Font = GEngine->GetMediumFont();
	const float Scale = 1.f;
	const FString Text = TEXT("OBJECTIVE: ") + StripUnsupportedGlyphs(Mission->GetObjective().ToString());
	float W = 0.f;
	float H = 0.f;
	Canvas->StrLen(Font, Text, W, H);
	const float Left = 20.f;
	const float Top = 20.f;
	const float Width = FMath::Max(430.f, W * Scale + 24.f);
	const float Height = H * Scale + 16.f;
	DrawRect(ObjectiveFrameColor, Left - 2.f, Top - 2.f, Width + 4.f, Height + 4.f);
	DrawRect(ObjectivePanelColor, Left, Top, Width, Height);
	DrawText(Text, ObjectiveTextColor, Left + 12.f, Top + 8.f, Font, Scale);
	return Top + Height + 2.f;
}

void ACodexTacticsHUD::DrawSquadPanel(float Top)
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	UFont* Font = GEngine->GetSmallFont();
	const float Scale = 1.1f;
	const float LineHeight = Font->GetMaxCharHeight() * Scale + LinePadding;

	TArray<TPair<FString, FLinearColor>> Lines;
	if (Flow)
	{
		FString Header = FString::Printf(TEXT("PHASE: %s"), PhaseName(Flow->GetPhase()));
		if (Flow->GetPhase() == ECodexGamePhase::WaveCombat)
		{
			Header += FString::Printf(TEXT("  |  %s  |  pauses %d/%d"), ModeName(Flow->GetCombatMode()),
				Flow->GetPauseCharges(), Flow->GetConfig().TacticalPauseMaxCharges);
		}
		Lines.Emplace(Header, SpeakerColor);
	}
	if (Squad)
	{
		Lines.Emplace(Squad->IsSoloMode() ? TEXT("Mode: SOLO [B]") : TEXT("Mode: squad [B]"), TextColor);
		// Commander Mode (Sprint 07-A): «paused» while a tactical pause / turn-based fight freezes it.
		const bool bAutonomy = Squad->IsAutonomousSquadCombat();
		const bool bFrozenAutonomy = bAutonomy && Flow && Flow->GetPhase() == ECodexGamePhase::WaveCombat && !Flow->IsSquadAutonomyActive();
		Lines.Emplace(FString::Printf(TEXT("AUTONOMY: %s [Ctrl+T]"), !bAutonomy ? TEXT("OFF") : (bFrozenAutonomy ? TEXT("ON (paused)") : TEXT("ON"))),
			bAutonomy ? FLinearColor(0.35f, 1.f, 0.55f) : TextColor);
		TArray<AOperativeCharacter*> Members = Squad->GetMembers();
		Members.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
		for (const AOperativeCharacter* Member : Members)
		{
			const bool bFrozen = Member->ColdSurvival && (Member->ColdSurvival->IsWeaponFrozen() || Member->ColdSurvival->IsFrostbitten());
			Lines.Emplace(DescribeOperative(*Member, Member == Squad->GetLeader()), bFrozen ? WarningColor : Member->BodyColor * 1.3f);
		}
	}
	if (Lines.IsEmpty())
	{
		return;
	}

	float Width = 0.f;
	for (const TPair<FString, FLinearColor>& Line : Lines)
	{
		float W = 0.f;
		float H = 0.f;
		Canvas->StrLen(Font, Line.Key, W, H);
		Width = FMath::Max(Width, W * Scale);
	}
	DrawRect(PanelColor, Margin, Top, Width + 2.f * Margin, Lines.Num() * LineHeight + Margin);
	float Y = Top + Margin * 0.5f;
	for (const TPair<FString, FLinearColor>& Line : Lines)
	{
		DrawText(Line.Key, Line.Value, Margin * 2.f, Y, Font, Scale);
		Y += LineHeight;
	}
}

void ACodexTacticsHUD::DrawOperativeLabels()
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	if (!Squad)
	{
		return;
	}
	UFont* Font = GEngine->GetSmallFont();
	for (const AOperativeCharacter* Member : Squad->GetMembers())
	{
		const float Top = Member->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 40.f;
		const FVector Screen = Project(Member->GetActorLocation() + FVector(0.f, 0.f, Top), true);
		if (Screen.Z <= 0.f)
		{
			continue; // behind the camera
		}
		FString Label = FString::Printf(TEXT("%s · %s"), *Member->DisplayName.ToString(),
			*AOperativeCharacter::GetStanceDisplayName(Member->GetStance()).ToString());
		if (Member->ColdSurvival && Member->ColdSurvival->IsFrostbitten())
		{
			Label += TEXT(" · FROSTBITTEN");
		}
		float W = 0.f;
		float H = 0.f;
		Canvas->StrLen(Font, Label, W, H);
		const bool bLeader = Member == Squad->GetLeader();
		DrawRect(PanelColor, Screen.X - W * 0.5f - 4.f, Screen.Y - 2.f, W + 8.f, H + 4.f);
		DrawText(Label, bLeader ? SpeakerColor : TextColor, Screen.X - W * 0.5f, Screen.Y, Font);
		// Godot PanicIndicator: «PANIC!» with the phase while panicking, «STRESS: n%» from 50 % stress.
		const UPanicComponent* Panic = Member->PanicComponent;
		FString PanicBadge;
		if (Panic && Panic->IsPanicking())
		{
			PanicBadge = Panic->GetPhase() == EPanicPhase::Cowering ? TEXT("PANIC! [COWERING]") : TEXT("PANIC! [FLEEING]");
		}
		else if (Panic && Panic->IsCombatActive() && Panic->GetStress() >= 50.f)
		{
			PanicBadge = FString::Printf(TEXT("STRESS: %d%%"), FMath::FloorToInt(Panic->GetStress()));
		}
		if (!PanicBadge.IsEmpty() && !Member->IsRaging())
		{
			float BW = 0.f;
			float BH = 0.f;
			Canvas->StrLen(Font, PanicBadge, BW, BH);
			const FLinearColor BadgeColor = Panic->IsPanicking() ? FLinearColor(1.f, 0.2f, 0.2f) : FLinearColor(1.f, 0.65f, 0.15f);
			DrawRect(PanelColor, Screen.X - BW * 0.5f - 4.f, Screen.Y - BH - 8.f, BW + 8.f, BH + 4.f);
			DrawText(PanicBadge, BadgeColor, Screen.X - BW * 0.5f, Screen.Y - BH - 6.f, Font);
		}
		if (Member->IsRaging())
		{
			// Godot RageBadge «🔥 RAGE!» above the head.
			const FString Badge = StripUnsupportedGlyphs(TEXT("🔥 RAGE!"));
			float BW = 0.f;
			float BH = 0.f;
			Canvas->StrLen(Font, Badge, BW, BH);
			DrawRect(PanelColor, Screen.X - BW * 0.5f - 4.f, Screen.Y - BH - 8.f, BW + 8.f, BH + 4.f);
			DrawText(Badge, FLinearColor(1.f, 0.35f, 0.05f), Screen.X - BW * 0.5f, Screen.Y - BH - 6.f, Font);
		}
	}

	// Godot recruit_susanin.gd OverheadPrompt: «[Click] Talk», blue «[Click] Go and rescue» while freezing.
	const URecruitSubsystem* Recruits = GetWorld()->GetSubsystem<URecruitSubsystem>();
	const AOperativeCharacter* Recruit = Recruits ? Recruits->GetSusanin() : nullptr;
	if (Recruit && Recruits->IsRecruit(Recruit))
	{
		const FVector Screen = Project(Recruit->GetActorLocation() + FVector(0.f, 0.f, Recruit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 60.f), true);
		if (Screen.Z > 0.f)
		{
			const bool bDistress = Recruits->IsInColdDistress();
			const FString Label = StripUnsupportedGlyphs(bDistress ? FString::Printf(TEXT("❄️ [Click] Go and rescue: %s"), *Recruit->DisplayName.ToString())
				: FString(TEXT("💬 [Click] Talk")));
			float W = 0.f;
			float H = 0.f;
			Canvas->StrLen(Font, Label, W, H);
			DrawRect(PanelColor, Screen.X - W * 0.5f - 4.f, Screen.Y - 2.f, W + 8.f, H + 4.f);
			DrawText(Label, bDistress ? FLinearColor(0.4f, 0.8f, 1.f) : FLinearColor(0.4f, 1.f, 0.7f, 0.95f), Screen.X - W * 0.5f, Screen.Y, Font);
		}
	}
}

void ACodexTacticsHUD::ToggleInventoryDrawer()
{
	if (!InventoryDrawer)
	{
		return;
	}
	InventoryDrawer->Toggle();
	if (InventoryDrawer->IsOpen() && ActionBar && ActionBar->IsWeaponSelectorOpen())
	{
		ActionBar->ToggleWeaponSelector();
	}
}

ETransferRequestOutcome ACodexTacticsHUD::HandleTransferDropOnActor(AOperativeCharacter* Sender, ETransferItem Item, AActor* TargetActor, const FVector& Point)
{
	USquadTransferSubsystem* Transfer = GetWorld()->GetSubsystem<USquadTransferSubsystem>();
	if (!Sender || !Transfer)
	{
		return ETransferRequestOutcome::Failed;
	}
	if (ALootCrateActor* Crate = Cast<ALootCrateActor>(TargetActor))
	{
		return HandleTransferRequest(FTransferRequest::MakeStore(Sender, Crate, Item));
	}
	if (const ADroppedItemActor* Pile = Cast<ADroppedItemActor>(TargetActor))
	{
		return HandleTransferRequest(FTransferRequest::MakeDrop(Sender, Pile->GetActorLocation(), Item)); // merges into the pile
	}
	if (AOperativeCharacter* Recipient = Transfer->ResolveDropRecipient(Sender, TargetActor, Point))
	{
		return HandleTransferRequest(FTransferRequest::MakeGive(Sender, Recipient, Item));
	}
	if (Cast<AOperativeCharacter>(TargetActor) == Sender)
	{
		return ETransferRequestOutcome::Failed; // dropped on himself: nothing to do
	}
	return HandleTransferRequest(FTransferRequest::MakeDrop(Sender, Point, Item));
}

ETransferRequestOutcome ACodexTacticsHUD::HandleTakeDrop(AActor* Container, ETransferItem Item, AOperativeCharacter* Taker)
{
	if (!Container || !Taker)
	{
		return ETransferRequestOutcome::Failed;
	}
	return HandleTransferRequest(FTransferRequest::MakeTake(Taker, Container, Item));
}

ETransferRequestOutcome ACodexTacticsHUD::HandleTransferRequest(const FTransferRequest& Request)
{
	USquadTransferSubsystem* Transfer = GetWorld()->GetSubsystem<USquadTransferSubsystem>();
	AOperativeCharacter* Operative = Request.Operative.Get();
	if (!Operative || !Transfer)
	{
		return ETransferRequestOutcome::Failed;
	}
	ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(GetOwningPlayerController());
	if (PC && PC->BlockRealTimeOrder())
	{
		return ETransferRequestOutcome::Failed; // the optional real-time order lock (hint posted)
	}
	auto Post = [this, Operative](const TCHAR* Text)
	{
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(Operative->DisplayName, FText::FromString(Text));
		}
	};
	if (Request.Action != ETransferAction::Take && TransferRules::GetAvailable(*Operative, Request.Item) <= 0)
	{
		Post(TEXT("You have run out of this item or ammo!"));
		return ETransferRequestOutcome::Failed;
	}
	if (Request.Action == ETransferAction::Store)
	{
		const ALootCrateActor* Crate = Cast<ALootCrateActor>(Request.Container.Get());
		if (Crate && !Crate->CanStore())
		{
			Post(TEXT("Nothing can be stored in this crate right now (mined or destroyed)."));
			return ETransferRequestOutcome::Failed;
		}
	}
	const int32 MaxQuantity = Transfer->GetMaxQuantity(Request);
	if (MaxQuantity <= 0)
	{
		switch (Request.Action)
		{
		case ETransferAction::Store: Post(TEXT("⚠️ The crate is full!")); break;
		case ETransferAction::Take:
			Post(USquadTransferSubsystem::GetContainerStash(Request.Container.Get(), false) ? TEXT("⚠️ Can't carry any more (or it is empty).")
				: TEXT("Nothing to take here."));
			break;
		default: Transfer->Execute(FTransferRequest::MakeGive(Operative, Request.Recipient.Get(), Request.Item, 1)); break; // «no room»
		}
		return ETransferRequestOutcome::Failed;
	}
	if (TransferRules::NeedsQuantityDialog(Request.Item, MaxQuantity) && QuantityDialog)
	{
		QuantityDialog->OpenForRequest(Request, MaxQuantity);
		return ETransferRequestOutcome::DialogOpened;
	}
	FTransferRequest Single = Request;
	Single.Quantity = MaxQuantity;
	return Transfer->Request(Single);
}

void ACodexTacticsHUD::HandleDragReleasedOverWorld(UInventoryDragDropOperation* Operation, const FVector2D& ScreenPosition)
{
	APlayerController* PC = GetOwningPlayerController();
	USquadTransferSubsystem* Transfer = GetWorld()->GetSubsystem<USquadTransferSubsystem>();
	if (!Operation || !PC || !Transfer)
	{
		return;
	}
	FVector2D PixelPosition;
	FVector2D ViewportPosition;
	USlateBlueprintLibrary::AbsoluteToViewport(this, ScreenPosition, PixelPosition, ViewportPosition);
	FHitResult Hit;
	if (!PC->GetHitResultAtScreenPosition(PixelPosition, ECC_Visibility, false, Hit))
	{
		return;
	}
	if (Operation->Container.IsValid())
	{
		if (AOperativeCharacter* Taker = Transfer->ResolveDropRecipient(nullptr, Hit.GetActor(), Hit.ImpactPoint))
		{
			HandleTakeDrop(Operation->Container.Get(), Operation->Item, Taker);
		}
		return;
	}
	HandleTransferDropOnActor(Operation->Sender.Get(), Operation->Item, Hit.GetActor(), Hit.ImpactPoint);
}

void ACodexTacticsHUD::OpenSaveLoadDialog(ESaveDialogMode Mode)
{
	if (PauseMenu)
	{
		PauseMenu->Close(false);
	}
	UGameplayStatics::SetGamePaused(GetWorld(), true);
	if (SaveLoadDialog)
	{
		SaveLoadDialog->Open(Mode);
	}
}

void ACodexTacticsHUD::CloseSaveLoadDialog()
{
	if (SaveLoadDialog)
	{
		SaveLoadDialog->Close();
	}
	if (PauseMenu)
	{
		PauseMenu->Open();
	}
}

void ACodexTacticsHUD::ClosePauseMenus()
{
	if (SaveLoadDialog)
	{
		SaveLoadDialog->Close();
	}
	if (PauseMenu)
	{
		PauseMenu->Close(true);
	}
	UGameplayStatics::SetGamePaused(GetWorld(), false);
}

bool ACodexTacticsHUD::HandleEscape()
{
	if (QuantityDialog && QuantityDialog->IsOpen())
	{
		QuantityDialog->Cancel();
		return true;
	}
	if (SaveLoadDialog && SaveLoadDialog->IsOpen())
	{
		if (SaveLoadDialog->IsConfirmOpen())
		{
			SaveLoadDialog->CancelConfirmation();
		}
		else
		{
			CloseSaveLoadDialog();
		}
		return true;
	}
	if (PauseMenu && PauseMenu->IsOpen())
	{
		PauseMenu->Close(true);
		return true;
	}
	if (InventoryDrawer && InventoryDrawer->IsOpen())
	{
		InventoryDrawer->Close();
		return true;
	}
	if (ProfileDialog && ProfileDialog->IsOpen())
	{
		ProfileDialog->Close();
		return true;
	}
	const UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>();
	const UDialogueSubsystem* Dialogues = GetWorld()->GetSubsystem<UDialogueSubsystem>();
	if ((Mission && Mission->IsMainMenuOpen()) || (Dialogues && Dialogues->IsDialogueOpen()) || !PauseMenu)
	{
		return false;
	}
	PauseMenu->Open();
	return true;
}
