// ACodexTacticsHUD: the drop markers on the squad's order paths (falls and drops, game designer item 10, 2026-11-03;
// docs/systems/stances-and-movement.md "Falls and drops"). UE-only (no Godot reference).

#include "UI/CodexTacticsHUD.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Falls/DropLinkSubsystem.h"
#include "GameFramework/Character.h"

void ACodexTacticsHUD::DrawDropMarkers()
{
	UWorld* World = GetWorld();
	const UDropLinkSubsystem* Drops = World ? World->GetSubsystem<UDropLinkSubsystem>() : nullptr;
	if (!Drops || !Canvas)
	{
		return;
	}
	TArray<FDropMarker> Markers;
	Drops->GatherSquadMarkers(Markers);
	UFont* Font = GEngine->GetMediumFont();
	for (const FDropMarker& Marker : Markers)
	{
		const FLinearColor Color = Marker.bDanger ? FLinearColor(1.f, 0.18f, 0.12f, 0.95f) : FLinearColor(1.f, 0.72f, 0.15f, 0.95f);
		// His way to the edge (the crowd path keeps only its ends: a straight dash from him to the link start).
		if (const ACharacter* Who = Cast<ACharacter>(Marker.Who.Get()); Who && !Marker.bActive)
		{
			const FVector Feet = Who->GetActorLocation() - FVector(0.f, 0.f, Who->GetSimpleCollisionHalfHeight());
			if (FVector::Dist2D(Feet, Marker.Start) > 60.f)
			{
				DrawDashedGroundPath({ Feet, Marker.Start }, FLinearColor(0.85f, 0.9f, 1.f, 0.55f));
			}
		}
		// The drop: along the roof to the edge, then down to the landing.
		const FVector OverEdge(Marker.Edge.X, Marker.Edge.Y, Marker.Start.Z);
		DrawDashedGroundPath({ Marker.Start, OverEdge, Marker.End }, Color);
		// A down chevron over the gap.
		const FVector Mid = (OverEdge + Marker.End) * 0.5f + FVector(0.f, 0.f, 40.f);
		const FVector Across = FVector::CrossProduct((Marker.End - Marker.Start).GetSafeNormal2D(), FVector::UpVector) * 35.f;
		const FVector Tip = Project(Mid - FVector(0.f, 0.f, 45.f), true);
		const FVector Left = Project(Mid - Across, true);
		const FVector Right = Project(Mid + Across, true);
		if (Tip.Z > 0.f && Left.Z > 0.f && Right.Z > 0.f)
		{
			DrawLine(Left.X, Left.Y, Tip.X, Tip.Y, Color, 4.f);
			DrawLine(Right.X, Right.Y, Tip.X, Tip.Y, Color, 4.f);
		}
		// The label over the edge.
		const FVector Screen = Project(OverEdge + FVector(0.f, 0.f, 60.f), true);
		if (Screen.Z <= 0.f || !Font)
		{
			continue;
		}
		float W = 0.f;
		float H = 0.f;
		Canvas->StrLen(Font, Marker.Label, W, H);
		const float PadX = 6.f;
		const float X = Screen.X - W * 0.5f - PadX;
		const float Y = Screen.Y - H - 4.f;
		DrawRect(Color, X - 1.f, Y - 1.f, W + PadX * 2.f + 2.f, H + 6.f);
		DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.85f), X, Y, W + PadX * 2.f, H + 4.f);
		FCanvasTextItem Item(FVector2D(Screen.X - W * 0.5f, Y + 2.f), FText::FromString(Marker.Label), Font, Color);
		Item.bOutlined = true;
		Item.OutlineColor = FLinearColor::Black;
		Canvas->DrawItem(Item);
	}
}
