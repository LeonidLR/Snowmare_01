#pragma once

#include "CoreMinimal.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * Strict JSON reading shared by the level authoring readers (LevelPropsFile.cpp - task C1, PropsLayoutView.cpp - task C2 of
 * docs/design/levelgen/PROPS_LAYERS.md). Private to the CodexTactics module. UE-only (no Godot reference).
 *
 * UE 5.8 notes: FJsonObject::Values is keyed by UE::FSharedString, so fields are read through TryGetField only and keys are copied out
 * through their TCHAR text; FJsonObject field lookup is case-insensitive ("id" also finds "ID").
 */
namespace LevelGenJson
{
	/** Case-insensitive like FJsonObject's own field lookup (TryGetField("id") also finds "ID"): such a key is read, never kept twice. */
	template <int32 N>
	bool IsKnownKey(const FString& Key, const TCHAR* const (&Known)[N])
	{
		for (const TCHAR* K : Known)
		{
			if (Key.Equals(K, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	/** Ordinal, case-sensitive key order (UTF-16 code units) - the same order as JS Array.prototype.sort() on the TS side. */
	inline void SortKeysOrdinal(TArray<FString>& Keys)
	{
		Keys.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
	}

	/** A JSON number -> integer cm / degrees, rounded half up (== JS Math.round). False when not finite or outside int32. */
	inline bool ToInt32(double Value, int32& Out)
	{
		if (!FMath::IsFinite(Value) || Value < static_cast<double>(MIN_int32) || Value > static_cast<double>(MAX_int32))
		{
			return false;
		}
		Out = static_cast<int32>(FMath::FloorToDouble(Value + 0.5));
		return true;
	}

	inline bool IsNullValue(const TSharedPtr<FJsonValue>& Value)
	{
		return !Value.IsValid() || Value->Type == EJson::Null;
	}

	/**
	 * The entries of a JSON object as (FString key, value), sorted ordinally. FJsonObject::Values is keyed by FJsonObject::FStringType
	 * (UE::FSharedString in 5.8, FString in older builds), so keys are copied out through their TCHAR text.
	 */
	inline TArray<TPair<FString, TSharedPtr<FJsonValue>>> SortedEntries(const FJsonObject& Object)
	{
		TArray<TPair<FString, TSharedPtr<FJsonValue>>> Entries;
		Entries.Reserve(Object.Values.Num());
		for (const auto& Entry : Object.Values)
		{
			Entries.Emplace(FString(*Entry.Key), Entry.Value);
		}
		Entries.Sort([](const TPair<FString, TSharedPtr<FJsonValue>>& A, const TPair<FString, TSharedPtr<FJsonValue>>& B)
		{
			return A.Key.Compare(B.Key, ESearchCase::CaseSensitive) < 0;
		});
		return Entries;
	}

	/** [x, y] of two JSON numbers -> integer cm. */
	inline bool ReadPosValue(const TSharedPtr<FJsonValue>& Value, FIntPoint& Out)
	{
		if (IsNullValue(Value) || Value->Type != EJson::Array)
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>& A = Value->AsArray();
		int32 X = 0;
		int32 Y = 0;
		if (A.Num() != 2 || IsNullValue(A[0]) || IsNullValue(A[1]) || A[0]->Type != EJson::Number || A[1]->Type != EJson::Number
			|| !ToInt32(A[0]->AsNumber(), X) || !ToInt32(A[1]->AsNumber(), Y))
		{
			return false;
		}
		Out = FIntPoint(X, Y);
		return true;
	}

	inline TSharedPtr<FJsonObject> AsObject(const TSharedPtr<FJsonValue>& Value)
	{
		return !IsNullValue(Value) && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
	}

	/**
	 * Strict typed field access on one JSON object. A missing key or a JSON null reads as absent (false, no error); a present value of
	 * the wrong type is an error (false + Errors). The FJsonValue Try* coercions (number <-> string <-> bool) are deliberately not used.
	 */
	struct FFieldReader
	{
		const FJsonObject& J;
		FString Ctx;
		TArray<FString>& Errors;
		bool bOk = true;

		FFieldReader(const FJsonObject& InJ, const FString& InCtx, TArray<FString>& InErrors)
			: J(InJ), Ctx(InCtx), Errors(InErrors)
		{
		}

		/** The field's value, or null when the key is missing or holds JSON null. */
		TSharedPtr<FJsonValue> Get(const TCHAR* Key) const
		{
			TSharedPtr<FJsonValue> Value = J.TryGetField(Key);
			return IsNullValue(Value) ? nullptr : Value;
		}

		bool Has(const TCHAR* Key) const
		{
			return Get(Key).IsValid();
		}

		void Fail(const FString& Message)
		{
			Errors.Add(Ctx + TEXT(": ") + Message);
			bOk = false;
		}

		bool Bad(const TCHAR* Key, const TCHAR* Expected)
		{
			Fail(FString::Printf(TEXT("'%s' must be %s"), Key, Expected));
			return false;
		}

		bool Str(const TCHAR* Key, FString& Out)
		{
			const TSharedPtr<FJsonValue> Value = Get(Key);
			if (!Value.IsValid())
			{
				return false;
			}
			if (Value->Type != EJson::String)
			{
				return Bad(Key, TEXT("a string"));
			}
			Out = Value->AsString();
			return true;
		}

		bool Bool(const TCHAR* Key, bool& Out)
		{
			const TSharedPtr<FJsonValue> Value = Get(Key);
			if (!Value.IsValid())
			{
				return false;
			}
			if (Value->Type != EJson::Boolean)
			{
				return Bad(Key, TEXT("true / false"));
			}
			Out = Value->AsBool();
			return true;
		}

		bool Int(const TCHAR* Key, int32& Out)
		{
			const TSharedPtr<FJsonValue> Value = Get(Key);
			if (!Value.IsValid())
			{
				return false;
			}
			if (Value->Type != EJson::Number || !ToInt32(Value->AsNumber(), Out))
			{
				return Bad(Key, TEXT("an integer"));
			}
			return true;
		}

		bool Point(const TCHAR* Key, FIntPoint& Out)
		{
			const TSharedPtr<FJsonValue> Value = Get(Key);
			if (!Value.IsValid())
			{
				return false;
			}
			if (!ReadPosValue(Value, Out))
			{
				return Bad(Key, TEXT("[x, y] in cm"));
			}
			return true;
		}

		TSharedPtr<FJsonObject> Object(const TCHAR* Key)
		{
			const TSharedPtr<FJsonValue> Value = Get(Key);
			if (!Value.IsValid())
			{
				return nullptr;
			}
			if (Value->Type != EJson::Object || !Value->AsObject().IsValid())
			{
				Bad(Key, TEXT("an object"));
				return nullptr;
			}
			return Value->AsObject();
		}

		const TArray<TSharedPtr<FJsonValue>>* Array(const TCHAR* Key)
		{
			const TSharedPtr<FJsonValue> Value = Get(Key);
			if (!Value.IsValid())
			{
				return nullptr;
			}
			if (Value->Type != EJson::Array)
			{
				Bad(Key, TEXT("an array"));
				return nullptr;
			}
			return &Value->AsArray();
		}

		/** Unknown keys (also unknown keys holding null) are kept verbatim. */
		template <int32 N>
		void CollectExtra(const TCHAR* const (&Known)[N], TMap<FString, TSharedPtr<FJsonValue>>& Out) const
		{
			for (const auto& Entry : J.Values)
			{
				const FString Key(*Entry.Key);
				if (!IsKnownKey(Key, Known))
				{
					Out.Add(Key, Entry.Value.IsValid() ? Entry.Value : MakeShared<FJsonValueNull>());
				}
			}
		}
	};
}
