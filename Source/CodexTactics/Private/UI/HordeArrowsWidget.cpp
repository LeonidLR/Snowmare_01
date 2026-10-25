#include "UI/HordeArrowsWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Tutorial/TutorialRules.h"
#include "UI/TutorialOverlayWidget.h"

namespace HordeArrowsLocal
{
	constexpr float EdgeMargin = 48.f;
	constexpr float MinTipGapPx = 56.f;
}

void UHordeArrowsWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Refresh();
}

void UHordeArrowsWidget::SetTutorialOverlay(UTutorialOverlayWidget* InOverlay)
{
	TutorialOverlay = InOverlay;
}

int32 UHordeArrowsWidget::GetArrowCount() const
{
	int32 Count = 0;
	for (const FApproach& Approach : Approaches)
	{
		Count += Approach.bArrow ? 1 : 0;
	}
	return Count;
}

void UHordeArrowsWidget::Refresh(bool bForce)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		Approaches.Reset();
		return;
	}
	const double Now = World->GetRealTimeSeconds();
	if (!bForce && Now - LastRefreshTime < RefreshIntervalSeconds)
	{
		ProjectApproaches(); // the camera moves every frame
		return;
	}
	LastRefreshTime = Now;
	Approaches.Reset();
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	const UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Flow || !Waves || !Leader || Flow->GetPhase() != ECodexGamePhase::WaveCombat)
	{
		return;
	}
	TArray<FVector> Positions;
	for (const AEnemyCharacter* Enemy : Waves->GetAliveEnemies())
	{
		Positions.Add(Enemy->GetActorLocation());
	}
	for (const HordeArrowRules::FGroup& Group : HordeArrowRules::GroupByBearing(Leader->GetActorLocation(), Positions, HideRangeCm, MergeAngleDeg))
	{
		Approaches.AddDefaulted_GetRef().Group = Group;
	}
	ProjectApproaches();
}

void UHordeArrowsWidget::ProjectApproaches()
{
	using namespace HordeArrowsLocal;
	const UWorld* World = GetWorld();
	APlayerController* PC = GetOwningPlayer();
	const APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager.Get() : nullptr;
	if (!World || !PC || !Camera)
	{
		return;
	}
	int32 SizeX = 0;
	int32 SizeY = 0;
	PC->GetViewportSize(SizeX, SizeY);
	const float Scale = SizeX > 0 && SizeY > 0 ? UWidgetLayoutLibrary::GetViewportScale(this) : 1.f;
	// Headless (no viewport) the same rule runs for a 1920 x 1080 view of the camera, like the tutorial's arrow.
	ViewSize = SizeX > 0 && SizeY > 0 && Scale > 0.f ? FVector2D(SizeX / Scale, SizeY / Scale) : FVector2D(1920.f, 1080.f);
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	const FVector From = Leader ? Leader->GetActorLocation() : Camera->GetCameraLocation();
	TArray<FVector2D> Tips;
	TArray<FVector2D> Directions;
	TArray<int32> Owners;
	for (int32 Index = 0; Index < Approaches.Num(); ++Index)
	{
		FApproach& Approach = Approaches[Index];
		Approach.bArrow = false;
		FVector2D Screen;
		TutorialRules::ProjectToView(Camera->GetCameraLocation(), Camera->GetCameraRotation(), Camera->GetFOVAngle(), ViewSize, Approach.Group.Centroid, Screen);
		if (!TutorialRules::ComputeEdgePointer(ViewSize, Screen, /*bProjected*/ true, EdgeMargin, Approach.Tip, Approach.Direction))
		{
			continue; // on screen: the horde itself shows where it comes from
		}
		Approach.bArrow = true;
		Approach.Distance = FText::FromString(TutorialRules::FormatGuideDistance(FVector::Dist2D(From, Approach.Group.Centroid)));
		Tips.Add(Approach.Tip);
		Directions.Add(Approach.Direction);
		Owners.Add(Index);
	}
	TArray<FVector2D> Occupied;
	FVector2D GuideTip;
	FVector2D GuideDirection;
	FText GuideDistance;
	FVector2D GuideView;
	if (const UTutorialOverlayWidget* Overlay = TutorialOverlay.Get(); Overlay && Overlay->GetGuidePointer(GuideTip, GuideDirection, GuideDistance, GuideView))
	{
		Occupied.Add(GuideTip); // the amber guide arrow keeps its place, the red ones slide off it
	}
	HordeArrowRules::SpreadTips(ViewSize, EdgeMargin, MinTipGapPx, Occupied, Directions, Tips);
	for (int32 Pair = 0; Pair < Owners.Num(); ++Pair)
	{
		Approaches[Owners[Pair]].Tip = Tips[Pair];
	}
}

int32 UHordeArrowsWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 MaxLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FSlateFontInfo DistanceFont = FCoreStyle::GetDefaultFontStyle("Bold", 11);
	FSlateRenderer* Renderer = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetRenderer() : nullptr;
	for (const FApproach& Approach : Approaches)
	{
		if (!Approach.bArrow)
		{
			continue;
		}
		// The tutorial arrow's look (a 20 px chevron on the edge, the distance just inside it), in red.
		const FVector2D Side(-Approach.Direction.Y, Approach.Direction.X);
		const FVector2D Base = Approach.Tip - Approach.Direction * 20.f;
		const TArray<FVector2D> Points = { Approach.Tip, Base + Side * 10.f, Base - Side * 10.f, Approach.Tip };
		FSlateDrawElement::MakeLines(OutDrawElements, MaxLayer + 1, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, ArrowColor, true, 3.f);
		if (Renderer && !Approach.Distance.IsEmpty())
		{
			const TSharedRef<FSlateFontMeasure> FontMeasure = Renderer->GetFontMeasureService();
			const FVector2D TextSize = FontMeasure->Measure(Approach.Distance, DistanceFont);
			const FVector2D TopLeft = Approach.Tip - Approach.Direction * (34.f + 0.5f * TextSize.Size()) - TextSize * 0.5f;
			FSlateDrawElement::MakeText(OutDrawElements, MaxLayer + 1, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TopLeft + FVector2D(1.f, 1.f))),
				Approach.Distance, DistanceFont, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.75f));
			FSlateDrawElement::MakeText(OutDrawElements, MaxLayer + 2, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TopLeft)),
				Approach.Distance, DistanceFont, ESlateDrawEffect::None, ArrowColor);
		}
	}
	return MaxLayer + 2;
}
