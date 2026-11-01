#include "UI/FormationMenuWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "CodexTactics.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "UI/FormationMenuInputRules.h"

#define LOCTEXT_NAMESPACE "FormationMenuWidget"

namespace FormationMenuWidgetLocal
{
	/** Highlighted item discs / icons grow by this factor. */
	constexpr float HighlightGrow = 1.12f;
	/** Gap between an item disc and its label, widget units. */
	constexpr float LabelGap = 8.f;
	/** Room the widest label needs ("Blizzard diamond", bold 13 + its pill), widget units. */
	constexpr float LabelRoom = 140.f;
	/** Room above / below the ring for a label row + the title / hint row. */
	constexpr float RowRoom = 56.f;

	void DrawDisc(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FVector2D& At, float Radius,
		const FLinearColor& Fill, const FLinearColor& Outline, float OutlineWidth)
	{
		const FSlateRoundedBoxBrush Brush(FLinearColor::White, Outline, OutlineWidth);
		FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(FVector2D(2.f * Radius, 2.f * Radius), FSlateLayoutTransform(At - FVector2D(Radius, Radius))),
			&Brush, ESlateDrawEffect::None, Fill);
	}

	void DrawArc(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FVector2D& At, float Radius, float FromDeg, float ToDeg,
		const FLinearColor& Color, float Thickness)
	{
		TArray<FVector2D> Points;
		constexpr int32 Steps = 16;
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const double Radians = FMath::DegreesToRadians(FMath::Lerp(FromDeg, ToDeg, static_cast<float>(Step) / Steps));
			Points.Add(At + FVector2D(FMath::Sin(Radians), -FMath::Cos(Radians)) * Radius);
		}
		FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Thickness);
	}

	FVector2D Measure(const FText& Text, const FSlateFontInfo& Font)
	{
		FSlateRenderer* Renderer = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetRenderer() : nullptr;
		return Renderer ? Renderer->GetFontMeasureService()->Measure(Text, Font) : FVector2D(8.f * Text.ToString().Len(), Font.Size * 1.4f);
	}

	/** Text with a dark pill behind it, its top-left at TopLeft (the pill pads it). */
	void DrawLabel(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FVector2D& TopLeft, const FVector2D& Size, const FText& Text,
		const FSlateFontInfo& Font, const FLinearColor& Color, const FLinearColor& PillColor)
	{
		const FVector2D Pad(7.f, 2.f);
		const FSlateRoundedBoxBrush Pill(FLinearColor::White, FLinearColor(1.f, 1.f, 1.f, 0.f), 0.f);
		FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(Size + Pad * 2.f, FSlateLayoutTransform(TopLeft - Pad)), &Pill, ESlateDrawEffect::None, PillColor);
		FSlateDrawElement::MakeText(Elements, Layer + 1, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft + FVector2D(1.f, 1.f))), Text, Font,
			ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.8f));
		FSlateDrawElement::MakeText(Elements, Layer + 2, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)), Text, Font, ESlateDrawEffect::None, Color);
	}

	/** Top-left of a label of Size anchored Gap outside a disc edge point along Direction (x right, y down). */
	FVector2D LabelTopLeft(const FVector2D& Anchor, const FVector2D& Direction, const FVector2D& Size)
	{
		const double AlignX = Direction.X > 0.35 ? 0.0 : (Direction.X < -0.35 ? 1.0 : 0.5);
		const double AlignY = Direction.Y > 0.35 ? 0.0 : (Direction.Y < -0.35 ? 1.0 : 0.5);
		return Anchor - FVector2D(Size.X * AlignX, Size.Y * AlignY);
	}

	void DrawIcon(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, UTexture2D* Texture, const FVector2D& At, float Size,
		const FLinearColor& Tint)
	{
		if (!Texture)
		{
			return;
		}
		FSlateBrush Brush;
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = FVector2D(Size, Size);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(FVector2D(Size, Size), FSlateLayoutTransform(At - FVector2D(Size, Size) * 0.5)),
			&Brush, ESlateDrawEffect::None, Tint);
	}
}

void UFormationMenuWidget::ShowMenu(const FVector2D& InCentre)
{
	LoadIcons();
	Centre = InCentre;
	bShown = true;
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UFormationMenuWidget::HideMenu()
{
	bShown = false;
	SetVisibility(ESlateVisibility::Collapsed);
}

void UFormationMenuWidget::SetCustomView(const TStaticArray<bool, 3>& Filled, bool bShow, int32 EditingSlot)
{
	CustomFilled = Filled;
	bShowCustom = bShow;
	CustomEditingSlot = EditingSlot;
}

void UFormationMenuWidget::SetMenuView(const TOptional<EFormationMenuItem>& InHighlighted, const TOptional<EFormationMenuItem>& Current, int32 InMemberCount)
{
	Highlighted = InHighlighted;
	CurrentChoice = Current;
	MemberCount = InMemberCount;
}

FVector2D UFormationMenuWidget::GetViewSize() const
{
	const APlayerController* PC = GetOwningPlayer();
	int32 SizeX = 0;
	int32 SizeY = 0;
	if (PC)
	{
		PC->GetViewportSize(SizeX, SizeY);
	}
	const float Scale = SizeX > 0 && SizeY > 0 ? UWidgetLayoutLibrary::GetViewportScale(this) : 1.f;
	return SizeX > 0 && SizeY > 0 && Scale > 0.f ? FVector2D(SizeX / Scale, SizeY / Scale) : FVector2D(1920.f, 1080.f);
}

bool UFormationMenuWidget::GetCursorPosition(FVector2D& OutCursor) const
{
	const APlayerController* PC = GetOwningPlayer();
	float X = 0.f;
	float Y = 0.f;
	if (!PC || !PC->GetMousePosition(X, Y))
	{
		return false;
	}
	const float Scale = UWidgetLayoutLibrary::GetViewportScale(this);
	OutCursor = FVector2D(X, Y) / (Scale > 0.f ? Scale : 1.f);
	return true;
}

bool UFormationMenuWidget::WarpCursorTo(const FVector2D& Position) const
{
	APlayerController* PC = GetOwningPlayer();
	int32 SizeX = 0;
	int32 SizeY = 0;
	if (PC)
	{
		PC->GetViewportSize(SizeX, SizeY);
	}
	if (!PC || SizeX <= 0 || SizeY <= 0)
	{
		return false;
	}
	const FVector2D Pixels = Position * UWidgetLayoutLibrary::GetViewportScale(this);
	PC->SetMouseLocation(FMath::RoundToInt(Pixels.X), FMath::RoundToInt(Pixels.Y));
	return true;
}

FVector2D UFormationMenuWidget::GetHalfExtent() const
{
	using namespace FormationMenuWidgetLocal;
	const float Disc = RingRadius + ItemRadius * HighlightGrow + LabelGap;
	// The custom row below the ring (contract 17.7) and the hint under it.
	const float Below = bShowCustom ? FormationMenuRules::CustomRowY + FormationMenuRules::CustomRowRadius * HighlightGrow + 3.f * LabelGap + 40.f : Disc + RowRoom;
	return FVector2D(Disc + LabelRoom, FMath::Max(Disc + RowRoom, Below));
}

int32 UFormationMenuWidget::GetLoadedIconCount() const
{
	int32 Count = 0;
	for (const TObjectPtr<UTexture2D>& Icon : Icons)
	{
		Count += Icon ? 1 : 0;
	}
	return Count;
}

void UFormationMenuWidget::LoadIcons()
{
	if (bIconsLoaded)
	{
		return;
	}
	bIconsLoaded = true;
	Icons.Reset();
	for (int32 Index = 0; Index <= FormationMenuRules::ItemCount; ++Index)
	{
		const TOptional<EFormationMenuItem> Item = Index < FormationMenuRules::ItemCount ? TOptional<EFormationMenuItem>(static_cast<EFormationMenuItem>(Index))
			: TOptional<EFormationMenuItem>();
		const FString Name = FormationMenuInput::IconAssetName(Item);
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), *IconFolder, *Name, *Name);
		Icons.Add(LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet));
	}
	if (GetLoadedIconCount() < Icons.Num())
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[FormationMenu] %d of %d icons found in %s (run Scripts/Editor/import_formation_menu_icons.py); labels only"),
			GetLoadedIconCount(), Icons.Num(), *IconFolder);
	}
}

UTexture2D* UFormationMenuWidget::IconFor(const TOptional<EFormationMenuItem>& Item) const
{
	const int32 Index = Item.IsSet() ? static_cast<int32>(Item.GetValue()) : FormationMenuRules::ItemCount;
	return Icons.IsValidIndex(Index) ? Icons[Index].Get() : nullptr;
}

int32 UFormationMenuWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace FormationMenuWidgetLocal;
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled) + 1;
	if (!bShown)
	{
		return Layer;
	}
	const FGeometry& Geo = AllottedGeometry;
	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Bold", LabelFontSize);
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", LabelFontSize + 4);
	const FSlateFontInfo HintFont = FCoreStyle::GetDefaultFontStyle("Regular", LabelFontSize - 1);
	const FLinearColor PillColor(0.f, 0.f, 0.f, 0.62f);
	const FLinearColor LabelColor(0.92f, 0.96f, 1.f, 1.f);
	const FLinearColor DarkIcon(0.05f, 0.07f, 0.09f, 1.f);

	// The backdrop: one disc behind the ring, the 45 deg sector borders, the highlighted sector's arc on its rim.
	const float Backdrop = RingRadius + ItemRadius + 18.f;
	DrawDisc(OutDrawElements, Layer, Geo, Centre, Backdrop, BackdropColor, FLinearColor(0.75f, 0.88f, 1.f, 0.22f), 1.5f);
	++Layer;
	for (int32 Index = 0; Index < FormationMenuRules::ItemCount; ++Index)
	{
		const double Radians = FMath::DegreesToRadians(FormationMenuRules::SectorDeg * (Index + 0.5));
		const FVector2D Dir(FMath::Sin(Radians), -FMath::Cos(Radians));
		const TArray<FVector2D> Border = { Centre + Dir * (CentreRadius + 8.f), Centre + Dir * (Backdrop - 6.f) };
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geo.ToPaintGeometry(), Border, ESlateDrawEffect::None, FLinearColor(0.75f, 0.88f, 1.f, 0.1f), true, 1.f);
	}
	if (Highlighted.IsSet() && !FormationMenuRules::IsCustom(Highlighted.GetValue()))
	{
		const float Mid = FormationMenuRules::SectorDeg * static_cast<float>(static_cast<int32>(Highlighted.GetValue()));
		const bool bEnabled = FormationMenuRules::IsEnabled(Highlighted.GetValue(), MemberCount);
		DrawArc(OutDrawElements, Layer, Geo, Centre, Backdrop - 4.f, Mid - 21.f, Mid + 21.f, bEnabled ? HighlightColor : DisabledColor, 5.f);
	}
	++Layer;

	// The items.
	for (int32 Index = 0; Index < FormationMenuRules::ItemCount; ++Index)
	{
		const EFormationMenuItem Item = static_cast<EFormationMenuItem>(Index);
		const FVector2D Dir = FormationMenuInput::ItemDirection(Item);
		const bool bHigh = Highlighted.IsSet() && Highlighted.GetValue() == Item;
		const bool bEnabled = FormationMenuRules::IsEnabled(Item, MemberCount);
		const bool bCurrent = CurrentChoice.IsSet() && CurrentChoice.GetValue() == Item;
		const float Radius = ItemRadius * (bHigh ? HighlightGrow : 1.f);
		const FVector2D At = Centre + Dir * RingRadius;
		const FLinearColor Fill = !bEnabled ? FLinearColor(0.1f, 0.1f, 0.11f, 0.8f) : (bHigh ? HighlightColor : DiscColor);
		const FLinearColor Outline = bCurrent ? CurrentColor : (bHigh ? FLinearColor(1.f, 1.f, 1.f, 0.9f) : FLinearColor(0.75f, 0.88f, 1.f, 0.3f));
		DrawDisc(OutDrawElements, Layer, Geo, At, Radius, Fill, Outline, bCurrent ? 3.f : (bHigh ? 2.f : 1.5f));
		DrawIcon(OutDrawElements, Layer + 1, Geo, IconFor(Item), At, IconSize * (bHigh ? HighlightGrow : 1.f),
			!bEnabled ? DisabledColor : (bHigh ? DarkIcon : IconColor));
		const FText Text = FormationMenuInput::Label(Item, MemberCount);
		const FVector2D Size = Measure(Text, LabelFont);
		const FVector2D TopLeft = LabelTopLeft(At + Dir * (Radius + LabelGap + 4.f), Dir, Size);
		DrawLabel(OutDrawElements, Layer + 2, Geo, TopLeft, Size, Text, LabelFont,
			!bEnabled ? DisabledColor : (bHigh ? HighlightColor : (bCurrent ? CurrentColor : LabelColor)), PillColor);
	}
	Layer += 5;

	// The centre: Auto.
	{
		const bool bHigh = !Highlighted.IsSet();
		const bool bCurrent = !CurrentChoice.IsSet();
		const float Radius = CentreRadius * (bHigh ? HighlightGrow : 1.f);
		DrawDisc(OutDrawElements, Layer, Geo, Centre, Radius, bHigh ? HighlightColor : DiscColor,
			bCurrent ? CurrentColor : (bHigh ? FLinearColor(1.f, 1.f, 1.f, 0.9f) : FLinearColor(0.75f, 0.88f, 1.f, 0.3f)), bCurrent ? 3.f : 2.f);
		DrawIcon(OutDrawElements, Layer + 1, Geo, IconFor(TOptional<EFormationMenuItem>()), Centre - FVector2D(0.f, 6.f), IconSize * 0.72f * (bHigh ? HighlightGrow : 1.f),
			bHigh ? DarkIcon : IconColor);
		const FText Text = FormationMenuInput::Label(TOptional<EFormationMenuItem>(), MemberCount);
		const FSlateFontInfo AutoFont = FCoreStyle::GetDefaultFontStyle("Bold", LabelFontSize - 2);
		const FVector2D Size = Measure(Text, AutoFont);
		const FVector2D TopLeft = Centre + FVector2D(-0.5 * Size.X, Radius * 0.42);
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 2, Geo.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)), Text, AutoFont, ESlateDrawEffect::None,
			bHigh ? DarkIcon : (bCurrent ? CurrentColor : LabelColor));
	}
	Layer += 3;

	// The custom row below the ring (game designer 2026-11-01, contract 17.7): Custom 1 / 2 / 3, an empty one "(empty)" (its release opens the
	// placement mode), a filled one picks it (Ctrl: edit); the slot being placed says "Confirm".
	if (bShowCustom)
	{
		const FSlateFontInfo DigitFont = FCoreStyle::GetDefaultFontStyle("Bold", LabelFontSize + 9);
		const FSlateFontInfo SubFont = FCoreStyle::GetDefaultFontStyle("Regular", LabelFontSize - 2);
		for (int32 CustomIndex = 1; CustomIndex <= FormationMenuRules::CustomSlotCount; ++CustomIndex)
		{
			const EFormationMenuItem Item = FormationMenuRules::CustomItem(CustomIndex);
			const FVector2D At = Centre + FVector2D((CustomIndex - 2) * FormationMenuRules::CustomRowSpacing, FormationMenuRules::CustomRowY);
			const bool bHigh = Highlighted.IsSet() && Highlighted.GetValue() == Item;
			const bool bEnabled = FormationMenuRules::IsEnabled(Item, MemberCount);
			const bool bCurrent = CurrentChoice.IsSet() && CurrentChoice.GetValue() == Item;
			const bool bFilled = CustomFilled[CustomIndex - 1];
			const float Radius = FormationMenuRules::CustomRowRadius * (bHigh ? HighlightGrow : 1.f);
			const FLinearColor Fill = !bEnabled ? FLinearColor(0.1f, 0.1f, 0.11f, 0.8f) : (bHigh ? HighlightColor : (bFilled ? DiscColor : FLinearColor(0.06f, 0.08f, 0.1f, 0.8f)));
			const FLinearColor Outline = bCurrent ? CurrentColor : (bHigh ? FLinearColor(1.f, 1.f, 1.f, 0.9f) : FLinearColor(1.f, 0.6f, 0.2f, bFilled ? 0.6f : 0.3f));
			DrawDisc(OutDrawElements, Layer, Geo, At, Radius, Fill, Outline, bCurrent ? 3.f : 2.f);
			const FText Digit = FText::AsNumber(CustomIndex);
			const FVector2D DigitSize = Measure(Digit, DigitFont);
			FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, Geo.ToPaintGeometry(DigitSize, FSlateLayoutTransform(At - DigitSize * 0.5)), Digit, DigitFont,
				ESlateDrawEffect::None, !bEnabled ? DisabledColor : (bHigh ? DarkIcon : (bFilled ? IconColor : DisabledColor)));
			const FText Text = CustomEditingSlot == CustomIndex ? FText::Format(LOCTEXT("CustomConfirm", "{0}: confirm"), FormationMenuInput::Label(Item, MemberCount))
				: bFilled ? FormationMenuInput::Label(Item, MemberCount) : FText::Format(LOCTEXT("CustomEmpty", "{0} (empty)"), FormationMenuInput::Label(Item, MemberCount));
			const FVector2D Size = Measure(Text, SubFont);
			DrawLabel(OutDrawElements, Layer + 2, Geo, At + FVector2D(-0.5 * Size.X, Radius + LabelGap * 0.5), Size, Text, SubFont,
				!bEnabled ? DisabledColor : (bHigh ? HighlightColor : (bCurrent ? CurrentColor : LabelColor)), PillColor);
		}
		Layer += 3;
	}

	// Title above, the hint below (the refusal while a greyed item is under the cursor).
	{
		const float Row = RingRadius + ItemRadius * HighlightGrow + LabelGap + 30.f;
		const float HintRow = bShowCustom ? FormationMenuRules::CustomRowY + FormationMenuRules::CustomRowRadius * HighlightGrow + 3.f * LabelGap + 14.f : Row + 4.f;
		const FText Title = LOCTEXT("Title", "FORMATION");
		const FVector2D TitleSize = Measure(Title, TitleFont);
		DrawLabel(OutDrawElements, Layer, Geo, Centre - FVector2D(0.5 * TitleSize.X, Row + TitleSize.Y), TitleSize, Title, TitleFont, LabelColor, PillColor);
		const bool bRefused = Highlighted.IsSet() && !FormationMenuRules::IsEnabled(Highlighted.GetValue(), MemberCount);
		const bool bCustomHigh = Highlighted.IsSet() && FormationMenuRules::IsCustom(Highlighted.GetValue());
		const FText Hint = bRefused ? LOCTEXT("HintRefused", "Needs at least 2 operatives")
			: bCustomHigh ? LOCTEXT("HintCustom", "Release B: use it (empty: place it)   |   hold Ctrl: edit it   |   Esc / RMB: cancel")
			: LOCTEXT("Hint", "Release B to choose   |   Esc / RMB: cancel   |   centre = Auto");
		const FVector2D HintSize = Measure(Hint, HintFont);
		DrawLabel(OutDrawElements, Layer, Geo, Centre + FVector2D(-0.5 * HintSize.X, HintRow), HintSize, Hint, HintFont,
			bRefused ? FLinearColor(1.f, 0.45f, 0.35f, 1.f) : FLinearColor(0.8f, 0.86f, 0.92f, 1.f), PillColor);
	}
	return Layer + 3;
}

#undef LOCTEXT_NAMESPACE
