#include "UI/HudTextCache.h"

namespace HudTextCache
{
	TArray<FString> WrapWords(const FString& Text, float Scale, float MaxWidth, FMeasure Measure)
	{
		TArray<FString> Lines;
		TArray<FString> Words;
		Text.ParseIntoArrayWS(Words);
		FString Current;
		for (const FString& Word : Words)
		{
			const FString Candidate = Current.IsEmpty() ? Word : Current + TEXT(" ") + Word;
			const float Width = static_cast<float>(Measure(Candidate).X);
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

	FLabelLayout LayoutLabel(const FString& CleanText, FMeasure Measure)
	{
		FLabelLayout Layout;
		TArray<FString> Raw;
		CleanText.TrimStartAndEnd().ParseIntoArrayLines(Raw);
		for (const FString& Line : Raw)
		{
			const FString Trimmed = Line.TrimStartAndEnd();
			const FVector2D Size = Measure(Trimmed);
			Layout.Lines.Add(Trimmed);
			Layout.LineTexts.Add(FText::FromString(Trimmed));
			Layout.LineWidths.Add(static_cast<float>(Size.X));
			Layout.Width = FMath::Max(Layout.Width, static_cast<float>(Size.X));
			Layout.LineHeight = FMath::Max(Layout.LineHeight, static_cast<float>(Size.Y));
		}
		return Layout;
	}
}
