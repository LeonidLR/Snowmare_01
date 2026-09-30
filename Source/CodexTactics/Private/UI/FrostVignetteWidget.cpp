#include "UI/FrostVignetteWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"

void UFrostVignetteWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("FrostRoot"));
	WidgetTree->RootWidget = Root;
	Overlay = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FrostOverlay"));
	UCanvasPanelSlot* OverlaySlot = Root->AddChildToCanvas(Overlay);
	OverlaySlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	OverlaySlot->SetOffsets(FMargin(0.f));
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/Materials/M_FrostVignette.M_FrostVignette")))
	{
		Material = UMaterialInstanceDynamic::Create(Base, this);
		Overlay->SetBrushFromMaterial(Material);
	}
	Overlay->SetVisibility(ESlateVisibility::Collapsed);
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UFrostVignetteWidget::SetColdPct(float ColdPct)
{
	CurrentColdPct = ColdPct;
	if (!Overlay)
	{
		return;
	}
	const bool bShow = ColdPct > 35.f && Material;
	if (bShow)
	{
		Material->SetScalarParameterValue(TEXT("ColdPct"), ColdPct);
	}
	const ESlateVisibility Wanted = bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	if (Overlay->GetVisibility() != Wanted)
	{
		Overlay->SetVisibility(Wanted);
	}
}

bool UFrostVignetteWidget::IsShown() const
{
	return Overlay && Overlay->GetVisibility() != ESlateVisibility::Collapsed;
}
