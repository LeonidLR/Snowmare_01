#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

/**
 * Text work the canvas HUD (ACodexTacticsHUD) used to redo every frame, made cacheable (audit 2026-10-11 P6 / P7; UE-only,
 * no Godot reference: Godot's Label3D / RichTextLabel laid their text out once per change). The measurement is passed in
 * (the canvas StrLen in the game, a fake in the tests), so the layout rules are pure.
 */
namespace HudTextCache
{
	/** Width / height of a string as drawn (UCanvas::StrLen with the HUD font, unscaled). */
	using FMeasure = TFunctionRef<FVector2D(const FString&)>;

	/**
	 * Greedy word wrap of the message feed (former ACodexTacticsHUD::WrapText, unchanged): words split on whitespace,
	 * joined with one space while the measured width x Scale stays within MaxWidth; a word that alone is too wide keeps
	 * its own line.
	 */
	CODEXTACTICS_API TArray<FString> WrapWords(const FString& Text, float Scale, float MaxWidth, FMeasure Measure);

	/** A world label's lines and their measured sizes (DrawWorldLabels). */
	struct FLabelLayout
	{
		/** The label's lines, each trimmed (already glyph-stripped by the caller). */
		TArray<FString> Lines;
		/** The same lines as FText for the canvas items (built once). */
		TArray<FText> LineTexts;
		/** Measured width of each line. */
		TArray<float> LineWidths;
		/** Widest line / tallest line (0 when there are no lines: the caller falls back to the font height). */
		float Width = 0.f;
		float LineHeight = 0.f;
	};

	/**
	 * Lines of an already glyph-stripped label: trimmed text split at line breaks, each line trimmed and measured
	 * (the former per-frame DrawWorldLabels steps). No lines for an empty text.
	 */
	CODEXTACTICS_API FLabelLayout LayoutLabel(const FString& CleanText, FMeasure Measure);

	/** Case-sensitive FString keys (FString's own == ignores case: «HOUND» and «Hound» must not share an entry). */
	template <typename ValueType>
	struct TCaseSensitiveKeyFuncs : BaseKeyFuncs<TPair<FString, ValueType>, FString, false>
	{
		static const FString& GetSetKey(const TPair<FString, ValueType>& Element) { return Element.Key; }
		static bool Matches(const FString& A, const FString& B) { return A.Equals(B, ESearchCase::CaseSensitive); }
		static uint32 GetKeyHash(const FString& Key) { return FCrc::StrCrc32(*Key); }
	};

	/**
	 * String-keyed cache with a size bound: when an insert would pass Capacity the whole cache is dropped (labels that
	 * carry changing numbers, «HP 37/40», never make it grow without bound; the next frame re-fills what is on screen).
	 */
	template <typename ValueType>
	class TBoundedStringCache
	{
	public:
		explicit TBoundedStringCache(int32 InCapacity = 256) : Capacity(FMath::Max(1, InCapacity)) {}

		/** The cached value for Key, made by Make(Key) on a miss. The reference is valid until the next call. */
		template <typename MakeType>
		const ValueType& FindOrMake(const FString& Key, MakeType&& Make)
		{
			if (const ValueType* Found = Map.Find(Key))
			{
				++Hits;
				return *Found;
			}
			++Misses;
			if (Map.Num() >= Capacity)
			{
				Map.Reset();
				++Flushes;
			}
			return Map.Add(Key, Make(Key));
		}

		void Reset() { Map.Reset(); }
		int32 Num() const { return Map.Num(); }
		int32 GetHits() const { return Hits; }
		int32 GetMisses() const { return Misses; }
		int32 GetFlushes() const { return Flushes; }

	private:
		TMap<FString, ValueType, FDefaultSetAllocator, TCaseSensitiveKeyFuncs<ValueType>> Map;
		int32 Capacity = 256;
		int32 Hits = 0;
		int32 Misses = 0;
		int32 Flushes = 0;
	};
}
