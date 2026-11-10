#include "AI/PmcArenaPlayArea.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace PmcArenaPlayAreaPrivate
{
	bool ClosedBoxContains(const FBox2D& Box, const FVector2D& Point)
	{
		return Point.X >= Box.Min.X && Point.X <= Box.Max.X && Point.Y >= Box.Min.Y && Point.Y <= Box.Max.Y;
	}

	/** A [x, y] pair of numbers. */
	bool ReadPair(const FJsonObject& Box, const TCHAR* Key, FVector2D& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Box.TryGetArrayField(Key, Values) || !Values || Values->Num() != 2)
		{
			return false;
		}
		double X = 0.0;
		double Y = 0.0;
		if (!(*Values)[0].IsValid() || !(*Values)[1].IsValid() || !(*Values)[0]->TryGetNumber(X) || !(*Values)[1]->TryGetNumber(Y))
		{
			return false;
		}
		Out = FVector2D(X, Y);
		return true;
	}

	/** { "min": [x, y], "max": [x, y] } -> a normalised box (corners in any order); false when malformed or degenerate. */
	bool ReadBox(const FJsonObject& Box, FBox2D& Out)
	{
		FVector2D A;
		FVector2D B;
		if (!ReadPair(Box, TEXT("min"), A) || !ReadPair(Box, TEXT("max"), B))
		{
			return false;
		}
		const FVector2D Min(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y));
		const FVector2D Max(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y));
		if (Max.X - Min.X <= UE_KINDA_SMALL_NUMBER || Max.Y - Min.Y <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		Out = FBox2D(Min, Max);
		return true;
	}

	FString BoxText(const FBox2D& Box)
	{
		return FString::Printf(TEXT("[%.0f, %.0f]..[%.0f, %.0f]"), Box.Min.X, Box.Min.Y, Box.Max.X, Box.Max.Y);
	}
}

using namespace PmcArenaPlayAreaPrivate;

bool PmcArenaPlayAreaRules::IsInside(const FPmcArenaPlayArea& Area, const FVector2D& Point)
{
	if (Area.bHasInclude && !ClosedBoxContains(Area.Include, Point))
	{
		return false;
	}
	for (const FBox2D& Box : Area.Exclude)
	{
		if (ClosedBoxContains(Box, Point))
		{
			return false;
		}
	}
	return true;
}

bool PmcArenaPlayAreaRules::IsInside(const FPmcArenaPlayArea& Area, const FVector& Point)
{
	return IsInside(Area, FVector2D(Point.X, Point.Y));
}

FBox PmcArenaPlayAreaRules::SampleBounds(const FPmcArenaPlayArea& Area, const FBox& NavBounds)
{
	if (!Area.bHasInclude || !NavBounds.IsValid)
	{
		return NavBounds;
	}
	const double MinX = FMath::Max(NavBounds.Min.X, Area.Include.Min.X);
	const double MinY = FMath::Max(NavBounds.Min.Y, Area.Include.Min.Y);
	const double MaxX = FMath::Min(NavBounds.Max.X, Area.Include.Max.X);
	const double MaxY = FMath::Min(NavBounds.Max.Y, Area.Include.Max.Y);
	if (MinX > MaxX || MinY > MaxY)
	{
		return NavBounds;
	}
	return FBox(FVector(MinX, MinY, NavBounds.Min.Z), FVector(MaxX, MaxY, NavBounds.Max.Z));
}

bool PmcArenaPlayAreaRules::ReadJson(const FJsonObject& Arena, FPmcArenaPlayArea& Out, TArray<FString>& OutErrors)
{
	Out = FPmcArenaPlayArea();
	const TSharedPtr<FJsonObject>* PlayArea = nullptr;
	if (!Arena.TryGetObjectField(TEXT("play_area"), PlayArea) || !PlayArea || !PlayArea->IsValid())
	{
		return true;
	}
	bool bClean = true;
	const TSharedPtr<FJsonObject>* Include = nullptr;
	if ((*PlayArea)->TryGetObjectField(TEXT("include_box_cm"), Include) && Include && Include->IsValid())
	{
		FBox2D Box;
		if (ReadBox(**Include, Box))
		{
			Out.bHasInclude = true;
			Out.Include = Box;
		}
		else
		{
			OutErrors.Add(TEXT("play_area.include_box_cm: wants { \"min\": [x, y], \"max\": [x, y] } with a non-zero size - ignored"));
			bClean = false;
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Boxes = nullptr;
	if ((*PlayArea)->TryGetArrayField(TEXT("exclude_boxes_cm"), Boxes) && Boxes)
	{
		for (int32 Index = 0; Index < Boxes->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			FBox2D Box;
			if (!(*Boxes)[Index].IsValid() || !(*Boxes)[Index]->TryGetObject(Entry) || !Entry || !Entry->IsValid() || !ReadBox(**Entry, Box))
			{
				OutErrors.Add(FString::Printf(TEXT("play_area.exclude_boxes_cm[%d]: wants { \"min\": [x, y], \"max\": [x, y] } with a non-zero size - skipped"), Index));
				bClean = false;
				continue;
			}
			FString Id;
			if (!(*Entry)->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty())
			{
				Id = FString::Printf(TEXT("exclude_%d"), Index);
			}
			Out.Exclude.Add(Box);
			Out.ExcludeIds.Add(Id);
		}
	}
	return bClean;
}

FString PmcArenaPlayAreaRules::Describe(const FPmcArenaPlayArea& Area)
{
	if (!Area.IsRestricted())
	{
		return TEXT("unrestricted");
	}
	FString Text = Area.bHasInclude ? FString::Printf(TEXT("include %s"), *BoxText(Area.Include)) : FString(TEXT("the whole map"));
	Text += FString::Printf(TEXT(", %d exclusion box(es)"), Area.Exclude.Num());
	for (int32 Index = 0; Index < Area.Exclude.Num(); ++Index)
	{
		Text += FString::Printf(TEXT("%s%s %s"), Index == 0 ? TEXT(": ") : TEXT(", "), Area.ExcludeIds.IsValidIndex(Index) ? *Area.ExcludeIds[Index] : TEXT("?"),
			*BoxText(Area.Exclude[Index]));
	}
	return Text;
}
