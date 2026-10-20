// ACodexTacticsHUD: the minimap (user decisions 2026-10-18, docs/design/MINIMAP.md; UE-only) - the MAP | MESSAGES header of the
// top-right panel, the corner map and the full-screen map (Tab). Content and layout come from UMinimapSubsystem; this unit
// only draws: the floor's captured image (the visible UV window), the fog quad (clipped triangles), the camera footprint and
// the live icons projected with the capture's iso projection.

#include "UI/CodexTacticsHUD.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GlobalRenderResources.h"
#include "HAL/PlatformTime.h"
#include "TextureResource.h"
#include "UI/MinimapSubsystem.h"

namespace HudMinimap
{
	const FLinearColor HeaderColor(0.f, 0.f, 0.f, 0.62f);
	const FLinearColor HeaderActiveColor(0.16f, 0.13f, 0.05f, 0.85f);
	const FLinearColor ActiveTextColor(1.f, 0.85f, 0.35f);
	const FLinearColor IdleTextColor(0.7f, 0.74f, 0.78f);
	const FLinearColor FrameColor(0.55f, 0.6f, 0.66f, 0.9f);
	const FLinearColor BackColor(0.02f, 0.025f, 0.035f, 0.92f);

	void Triangle(FCanvas* Canvas, const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
	{
		FCanvasTriangleItem Item(A, B, C, GWhiteTexture);
		Item.SetColor(Color);
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}

	void Diamond(FCanvas* Canvas, const FVector2D& P, float Radius, const FLinearColor& Color)
	{
		const FVector2D Up(0.f, -Radius);
		const FVector2D Right(Radius, 0.f);
		Triangle(Canvas, P + Up, P + Right, P - Up, Color);
		Triangle(Canvas, P + Up, P - Up, P - Right, Color);
	}

	FLinearColor WithAlpha(FLinearColor Color, float Alpha)
	{
		Color.A *= Alpha;
		return Color;
	}
}

void ACodexTacticsHUD::DrawMinimapHeader()
{
	UMinimapSubsystem* Minimap = UMinimapSubsystem::Get(this);
	if (!Minimap || Minimap->IsFullMapOpen())
	{
		return;
	}
	const FBox2D Rect = Minimap->GetHeaderRect();
	if (!Rect.bIsValid)
	{
		return;
	}
	UFont* Font = GEngine->GetSmallFont();
	const bool bMap = Minimap->GetPanelMode() == EMinimapPanelMode::Map;
	const FVector2D Size = Rect.GetSize();
	const float Half = Size.X * 0.5f;
	DrawRect(HudMinimap::HeaderColor, Rect.Min.X, Rect.Min.Y, Size.X, Size.Y);
	DrawRect(HudMinimap::HeaderActiveColor, Rect.Min.X + (bMap ? 0.f : Half), Rect.Min.Y, Half, Size.Y);
	const int32 Unread = Minimap->GetUnreadMessages();
	const FString Messages = Unread > 0 ? FString::Printf(TEXT("MESSAGES (%d)"), Unread) : FString(TEXT("MESSAGES"));
	const float TextY = Rect.Min.Y + (Size.Y - Font->GetMaxCharHeight()) * 0.5f;
	DrawText(TEXT("MAP"), bMap ? HudMinimap::ActiveTextColor : HudMinimap::IdleTextColor, Rect.Min.X + 10.f, TextY, Font, 1.f);
	DrawText(Messages, !bMap ? HudMinimap::ActiveTextColor : (Unread > 0 ? FLinearColor(1.f, 0.6f, 0.4f) : HudMinimap::IdleTextColor),
		Rect.Min.X + Half + 10.f, TextY, Font, 1.f);
	float HintW = 0.f;
	float HintH = 0.f;
	GetTextSize(TEXT("F2"), HintW, HintH, Font, 1.f);
	DrawText(TEXT("F2"), HudMinimap::IdleTextColor, Rect.Max.X - HintW - 8.f, TextY, Font, 1.f);
}

void ACodexTacticsHUD::DrawMinimapPanel()
{
	UMinimapSubsystem* Minimap = UMinimapSubsystem::Get(this);
	if (!Minimap || !Minimap->IsReady() || Minimap->IsFullMapOpen() || Minimap->GetPanelMode() != EMinimapPanelMode::Map)
	{
		return;
	}
	DrawMapView(*Minimap, Minimap->GetPanelView(), false);
}

void ACodexTacticsHUD::DrawFullMap()
{
	UMinimapSubsystem* Minimap = UMinimapSubsystem::Get(this);
	if (!Minimap || !Minimap->IsReady() || !Minimap->IsFullMapOpen())
	{
		return;
	}
	const FMinimapView& View = Minimap->GetFullView();
	// Dim the game behind it, a title strip above.
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);
	UFont* Font = GEngine->GetMediumFont();
	const float TitleHeight = Font->GetMaxCharHeight() + 14.f;
	DrawRect(FLinearColor(0.02f, 0.025f, 0.035f, 0.95f), View.ScreenOrigin.X, View.ScreenOrigin.Y - TitleHeight, View.ScreenSize.X, TitleHeight);
	const FString Title = FString::Printf(TEXT("MAP - %s"), *MinimapRules::FloorLabel(Minimap->GetDisplayedFloor()));
	DrawText(Title, HudMinimap::ActiveTextColor, View.ScreenOrigin.X + 8.f, View.ScreenOrigin.Y - Font->GetMaxCharHeight() - 7.f, Font, 1.f);
	UFont* Small = GEngine->GetSmallFont();
	const FString Hint = TEXT("[Tab] close   [Wheel] zoom   [LMB] camera there   [RMB] move there");
	float HintW = 0.f;
	float HintH = 0.f;
	GetTextSize(Hint, HintW, HintH, Small, 1.f);
	DrawText(Hint, HudMinimap::IdleTextColor, View.ScreenOrigin.X + View.ScreenSize.X - HintW - 8.f, View.ScreenOrigin.Y - HintH - 8.f, Small, 1.f);
	DrawMapView(*Minimap, View, true);
}

void ACodexTacticsHUD::DrawMapView(UMinimapSubsystem& Minimap, const FMinimapView& View, bool bFull)
{
	const double Start = FPlatformTime::Seconds();
	FCanvas* Draw = Canvas->Canvas;
	const FBox2D Rect(View.ScreenOrigin, View.ScreenOrigin + View.ScreenSize);
	DrawRect(HudMinimap::BackColor, Rect.Min.X, Rect.Min.Y, View.ScreenSize.X, View.ScreenSize.Y);
	const int32 Floor = Minimap.GetDisplayedFloor();
	UFont* Font = GEngine->GetSmallFont();

	// The floor's static image: the part of the visible UV window that lies on the image.
	const FMinimapFloorCapture* Capture = Minimap.GetCaptureInfo(Floor);
	UTextureRenderTarget2D* Target = Minimap.GetCapture(Floor);
	if (Target && Capture && Capture->bRendered)
	{
		const FVector2D UV0(FMath::Max(0.f, View.CenterUV.X - View.SpanU * 0.5f), FMath::Max(0.f, View.CenterUV.Y - View.SpanV * 0.5f));
		const FVector2D UV1(FMath::Min(1.f, View.CenterUV.X + View.SpanU * 0.5f), FMath::Min(1.f, View.CenterUV.Y + View.SpanV * 0.5f));
		if (UV1.X > UV0.X && UV1.Y > UV0.Y)
		{
			const FVector2D P0 = View.MapToScreen(UV0);
			const FVector2D P1 = View.MapToScreen(UV1);
			DrawTexture(Target, P0.X, P0.Y, P1.X - P0.X, P1.Y - P0.Y, UV0.X, UV0.Y, UV1.X - UV0.X, UV1.Y - UV0.Y, FLinearColor::White, BLEND_Opaque);
		}
	}
	else
	{
		DrawText(TEXT("MAP NOT CAPTURED"), HudMinimap::IdleTextColor, Rect.Min.X + 10.f, Rect.Min.Y + 10.f, Font, 1.f);
	}

	// Fog of war: the fog texture over the floor's XY bounds (a quad on the floor's plane), cut to the view.
	if (UTexture2D* FogTexture = Minimap.GetFogTexture(Floor); FogTexture && FogTexture->GetResource())
	{
		const TArray<FVector> Quad = Minimap.GetFogQuad(Floor);
		TArray<FVector2D> Points;
		const TArray<FVector2D> TexUVs = { FVector2D(0.f, 0.f), FVector2D(1.f, 0.f), FVector2D(1.f, 1.f), FVector2D(0.f, 1.f) };
		for (const FVector& Corner : Quad)
		{
			Points.Add(View.MapToScreen(Minimap.GetProjection().WorldToMap(Corner)));
		}
		TArray<FVector2D> Clipped;
		TArray<FVector2D> ClippedUVs;
		MinimapRules::ClipPolygonToRect(Points, TexUVs, Rect, Clipped, ClippedUVs);
		if (Clipped.Num() >= 3)
		{
			TArray<FCanvasUVTri> Tris;
			for (int32 Index = 1; Index + 1 < Clipped.Num(); ++Index)
			{
				FCanvasUVTri& Tri = Tris.AddDefaulted_GetRef();
				Tri.V0_Pos = Clipped[0];
				Tri.V0_UV = ClippedUVs[0];
				Tri.V1_Pos = Clipped[Index];
				Tri.V1_UV = ClippedUVs[Index];
				Tri.V2_Pos = Clipped[Index + 1];
				Tri.V2_UV = ClippedUVs[Index + 1];
				Tri.V0_Color = Tri.V1_Color = Tri.V2_Color = FLinearColor::White;
			}
			FCanvasTriangleItem FogItem(Tris, FogTexture->GetResource());
			FogItem.BlendMode = SE_BLEND_Translucent;
			Draw->DrawItem(FogItem);
		}
	}

	// The main camera's view footprint.
	const TArray<FVector2D> Footprint = Minimap.GetCameraFootprint();
	for (int32 Index = 0; Index < Footprint.Num(); ++Index)
	{
		FVector2D A = View.MapToScreen(Footprint[Index]);
		FVector2D B = View.MapToScreen(Footprint[(Index + 1) % Footprint.Num()]);
		if (MinimapRules::ClipSegmentToRect(A, B, Rect))
		{
			DrawLine(A.X, A.Y, B.X, B.Y, FLinearColor(0.85f, 0.92f, 1.f, 0.75f), 1.5f);
		}
	}

	// Live icons.
	TArray<FMinimapIcon> Icons;
	Minimap.BuildIcons(Icons);
	const float Scale = bFull ? 1.35f : 1.f;
	const FBox2D Inner(Rect.Min + FVector2D(6.f), Rect.Max - FVector2D(6.f));
	for (const FMinimapIcon& Icon : Icons)
	{
		FVector2D P = View.MapToScreen(Icon.MapUV);
		const bool bInside = Inner.IsInside(P);
		if (!bInside)
		{
			if (Icon.Kind != EMinimapIconKind::Objective)
			{
				continue;
			}
			P = FVector2D(FMath::Clamp(P.X, Inner.Min.X, Inner.Max.X), FMath::Clamp(P.Y, Inner.Min.Y, Inner.Max.Y)); // objectives stick to the edge
		}
		const float Alpha = Icon.Alpha * (Icon.bOtherFloor ? 0.4f : 1.f);
		switch (Icon.Kind)
		{
		case EMinimapIconKind::Objective:
			if (Icon.bHighlight)
			{
				// 2026-10-20: the tutorial's current navigation target - larger, with a white frame.
				HudMinimap::Diamond(Draw, P, 11.f * Scale, HudMinimap::WithAlpha(FLinearColor::White, 0.9f));
				HudMinimap::Diamond(Draw, P, 9.f * Scale, HudMinimap::WithAlpha(FLinearColor::Black, 0.8f));
				HudMinimap::Diamond(Draw, P, 7.f * Scale, HudMinimap::WithAlpha(Icon.Color, Alpha));
				break;
			}
			HudMinimap::Diamond(Draw, P, 7.f * Scale, HudMinimap::WithAlpha(FLinearColor::Black, 0.7f));
			HudMinimap::Diamond(Draw, P, 5.f * Scale, HudMinimap::WithAlpha(Icon.Color, Alpha));
			break;
		case EMinimapIconKind::Door:
		{
			const FVector2D Side = FVector2D(-Icon.MapFacing.Y, Icon.MapFacing.X) * 5.f * Scale;
			DrawLine(P.X - Side.X, P.Y - Side.Y, P.X + Side.X, P.Y + Side.Y, HudMinimap::WithAlpha(Icon.Color, Alpha), 3.f);
			if (bFull)
			{
				DrawText(Icon.Label, HudMinimap::WithAlpha(Icon.Color, Alpha), P.X + 6.f, P.Y - 6.f, Font, 0.8f);
			}
			break;
		}
		case EMinimapIconKind::Trap:
		{
			const float R = 4.f * Scale;
			const FLinearColor Color = HudMinimap::WithAlpha(Icon.Color, Alpha);
			DrawLine(P.X - R, P.Y - R, P.X + R, P.Y + R, Color, 2.f);
			DrawLine(P.X - R, P.Y + R, P.X + R, P.Y - R, Color, 2.f);
			break;
		}
		case EMinimapIconKind::Enemy:
			HudMinimap::Diamond(Draw, P, 5.f * Scale, HudMinimap::WithAlpha(FLinearColor::Black, 0.6f * Alpha));
			HudMinimap::Diamond(Draw, P, 3.6f * Scale, HudMinimap::WithAlpha(Icon.Color, Alpha));
			if (!Icon.MapFacing.IsNearlyZero())
			{
				const FVector2D Tip = P + Icon.MapFacing * 8.f * Scale;
				DrawLine(P.X, P.Y, Tip.X, Tip.Y, HudMinimap::WithAlpha(Icon.Color, Alpha), 1.5f);
			}
			break;
		case EMinimapIconKind::Operative:
		default:
		{
			const float R = (Icon.bHighlight ? 6.f : 4.5f) * Scale;
			if (!Icon.MapFacing.IsNearlyZero())
			{
				const FVector2D Dir = Icon.MapFacing;
				const FVector2D Side(-Dir.Y, Dir.X);
				HudMinimap::Triangle(Draw, P + Dir * (R + 7.f * Scale), P + Side * R * 0.8f + Dir * R * 0.4f, P - Side * R * 0.8f + Dir * R * 0.4f,
					HudMinimap::WithAlpha(FLinearColor(0.95f, 0.97f, 1.f), Alpha));
			}
			if (Icon.bHighlight || Icon.bSelected)
			{
				DrawRect(HudMinimap::WithAlpha(FLinearColor::White, Alpha), P.X - R - 2.f, P.Y - R - 2.f, 2.f * R + 4.f, 2.f * R + 4.f);
			}
			DrawRect(HudMinimap::WithAlpha(FLinearColor::Black, Alpha), P.X - R - 0.5f, P.Y - R - 0.5f, 2.f * R + 1.f, 2.f * R + 1.f);
			DrawRect(HudMinimap::WithAlpha(Icon.Color, Alpha), P.X - R + 1.f, P.Y - R + 1.f, 2.f * R - 2.f, 2.f * R - 2.f);
			break;
		}
		}
	}

	// Frame + floor label.
	DrawLine(Rect.Min.X, Rect.Min.Y, Rect.Max.X, Rect.Min.Y, HudMinimap::FrameColor, 1.f);
	DrawLine(Rect.Max.X, Rect.Min.Y, Rect.Max.X, Rect.Max.Y, HudMinimap::FrameColor, 1.f);
	DrawLine(Rect.Max.X, Rect.Max.Y, Rect.Min.X, Rect.Max.Y, HudMinimap::FrameColor, 1.f);
	DrawLine(Rect.Min.X, Rect.Max.Y, Rect.Min.X, Rect.Min.Y, HudMinimap::FrameColor, 1.f);
	if (!bFull)
	{
		const FString Label = Minimap.GetFloorCount() > 1 ? MinimapRules::FloorLabel(Floor) : FString();
		if (!Label.IsEmpty())
		{
			DrawText(Label, HudMinimap::ActiveTextColor, Rect.Min.X + 6.f, Rect.Max.Y - Font->GetMaxCharHeight() - 4.f, Font, 1.f);
		}
	}
	Minimap.ReportDrawTime((FPlatformTime::Seconds() - Start) * 1.0e6);
}
