#include "LevelGen/LevelPropsFile.h"
#include "CodexTactics.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Templates/Function.h"

namespace LevelPropsFileLocal
{
	const TCHAR* const GKinds[] = { TEXT("barrels"), TEXT("crates"), TEXT("turrets"), TEXT("mines"), TEXT("loot") };

	const TCHAR* const GRootKeys[] = { TEXT("schema"), TEXT("map"), TEXT("layout"), TEXT("layout_hash"), TEXT("revision"), TEXT("frozen"),
		TEXT("layers"), TEXT("overrides") };
	const TCHAR* const GLayerKeys[] = { TEXT("id"), TEXT("kind"), TEXT("enabled"), TEXT("seed"), TEXT("locked"), TEXT("preset"), TEXT("rules"),
		TEXT("rules_hash"), TEXT("objects"), TEXT("report") };
	const TCHAR* const GObjectKeys[] = { TEXT("id"), TEXT("variant"), TEXT("pos"), TEXT("walk_layer"), TEXT("yaw"), TEXT("locked"), TEXT("origin"),
		TEXT("attach"), TEXT("door"), TEXT("trigger"), TEXT("loot_table"), TEXT("arc") };
	const TCHAR* const GOverrideKeys[] = { TEXT("op"), TEXT("id"), TEXT("pos"), TEXT("walk_layer"), TEXT("z_offset"), TEXT("yaw"), TEXT("object"),
		TEXT("fields") };

	const TCHAR* const GHandPrefix = TEXT("hand_");
	const TCHAR* const GHandOrigin = TEXT("hand");

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
	void SortKeysOrdinal(TArray<FString>& Keys)
	{
		Keys.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
	}

	/** A JSON number -> integer cm / degrees, rounded half up (== JS Math.round). False when not finite or outside int32. */
	bool ToInt32(double Value, int32& Out)
	{
		if (!FMath::IsFinite(Value) || Value < static_cast<double>(MIN_int32) || Value > static_cast<double>(MAX_int32))
		{
			return false;
		}
		Out = static_cast<int32>(FMath::FloorToDouble(Value + 0.5));
		return true;
	}

	bool IsNullValue(const TSharedPtr<FJsonValue>& Value)
	{
		return !Value.IsValid() || Value->Type == EJson::Null;
	}

	/**
	 * The entries of a JSON object as (FString key, value), sorted ordinally. FJsonObject::Values is keyed by FJsonObject::FStringType
	 * (UE::FSharedString in 5.8, FString in older builds), so keys are copied out through their TCHAR text.
	 */
	TArray<TPair<FString, TSharedPtr<FJsonValue>>> SortedEntries(const FJsonObject& Object)
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

	bool ReadPosValue(const TSharedPtr<FJsonValue>& Value, FIntPoint& Out)
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

	// ---- reader -------------------------------------------------------------------------------------------------------------

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
		void CollectExtra(const TCHAR* const (&Known)[N], FLevelPropsExtra& Out) const
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

	TSharedPtr<FJsonObject> AsObject(const TSharedPtr<FJsonValue>& Value)
	{
		return !IsNullValue(Value) && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
	}

	bool ParseObject(const FJsonObject& J, const FString& Ctx, FLevelPropsObject& O, TArray<FString>& Errors)
	{
		FFieldReader R(J, Ctx, Errors);
		if (!R.Has(TEXT("id")))
		{
			R.Fail(TEXT("missing id"));
		}
		else if (R.Str(TEXT("id"), O.Id) && O.Id.IsEmpty())
		{
			R.Fail(TEXT("empty id"));
		}
		R.Ctx = FString::Printf(TEXT("%s (%s)"), *Ctx, *O.Id);
		if (!R.Has(TEXT("variant")))
		{
			R.Fail(TEXT("missing variant"));
		}
		else if (R.Str(TEXT("variant"), O.Variant) && O.Variant.IsEmpty())
		{
			R.Fail(TEXT("empty variant"));
		}
		if (!R.Has(TEXT("pos")))
		{
			R.Fail(TEXT("missing pos"));
		}
		else
		{
			R.Point(TEXT("pos"), O.Pos);
		}
		R.Str(TEXT("walk_layer"), O.WalkLayer);
		R.Int(TEXT("yaw"), O.Yaw);
		R.Bool(TEXT("locked"), O.bLocked);
		R.Str(TEXT("origin"), O.Origin);
		R.Str(TEXT("attach"), O.Attach);
		R.Str(TEXT("door"), O.Door);
		R.Str(TEXT("trigger"), O.Trigger);
		R.Str(TEXT("loot_table"), O.LootTable);
		int32 Arc = 0;
		if (R.Int(TEXT("arc"), Arc))
		{
			O.Arc = Arc;
		}
		R.CollectExtra(GObjectKeys, O.Extra);
		return R.bOk;
	}

	bool ParseOverride(const FJsonObject& J, const FString& Ctx, FLevelPropsOverride& O, TArray<FString>& Errors)
	{
		FFieldReader R(J, Ctx, Errors);
		FString Name;
		if (!R.Has(TEXT("op")))
		{
			R.Fail(TEXT("missing op"));
			return false;
		}
		if (!R.Str(TEXT("op"), Name))
		{
			return false;
		}
		if (Name == TEXT("move")) { O.Op = ELevelPropsOp::Move; }
		else if (Name == TEXT("delete")) { O.Op = ELevelPropsOp::Delete; }
		else if (Name == TEXT("add")) { O.Op = ELevelPropsOp::Add; }
		else if (Name == TEXT("set")) { O.Op = ELevelPropsOp::Set; }
		else
		{
			R.Fail(FString::Printf(TEXT("unknown op '%s'"), *Name));
			return false;
		}

		if (O.Op == ELevelPropsOp::Add)
		{
			const TSharedPtr<FJsonObject> Object = R.Object(TEXT("object"));
			if (!Object.IsValid())
			{
				if (R.bOk)
				{
					R.Fail(TEXT("add needs an object"));
				}
				return false;
			}
			if (!ParseObject(*Object, Ctx + TEXT(".object"), O.Object, Errors))
			{
				return false;
			}
			if (!O.Object.Id.StartsWith(GHandPrefix, ESearchCase::CaseSensitive))
			{
				R.Fail(FString::Printf(TEXT("an added object's id must be hand_<layer>_<n> (got '%s')"), *O.Object.Id));
				return false;
			}
			O.Id = O.Object.Id;
		}
		else
		{
			if (!R.Has(TEXT("id")))
			{
				R.Fail(TEXT("missing id"));
				return false;
			}
			if (!R.Str(TEXT("id"), O.Id) || O.Id.IsEmpty())
			{
				if (R.bOk)
				{
					R.Fail(TEXT("empty id"));
				}
				return false;
			}
		}

		FIntPoint Pos;
		if (R.Point(TEXT("pos"), Pos)) { O.Pos = Pos; }
		FString Layer;
		if (R.Str(TEXT("walk_layer"), Layer)) { O.WalkLayer = Layer; }
		int32 Value = 0;
		if (R.Int(TEXT("z_offset"), Value)) { O.ZOffset = Value; }
		if (R.Int(TEXT("yaw"), Value)) { O.Yaw = Value; }
		O.Fields = R.Object(TEXT("fields"));
		if (O.Op == ELevelPropsOp::Set && !O.Fields.IsValid() && R.bOk)
		{
			R.Fail(TEXT("set needs fields"));
		}
		R.CollectExtra(GOverrideKeys, O.Extra);
		return R.bOk;
	}

	/** False only when the layer itself is unusable (id / kind / seed); a bad object adds an error and is dropped, the layer stays. */
	bool ParseLayer(const FJsonObject& J, const FString& Ctx, FLevelPropsLayer& L, TSet<FString>& SeenObjectIds, TArray<FString>& Errors)
	{
		FFieldReader R(J, Ctx, Errors);
		if (!R.Has(TEXT("id")))
		{
			R.Fail(TEXT("missing id"));
		}
		else if (R.Str(TEXT("id"), L.Id) && L.Id.IsEmpty())
		{
			R.Fail(TEXT("empty id"));
		}
		R.Ctx = FString::Printf(TEXT("%s (%s)"), *Ctx, *L.Id);
		if (!R.Has(TEXT("kind")))
		{
			R.Fail(TEXT("missing kind"));
		}
		else if (R.Str(TEXT("kind"), L.Kind) && !LevelPropsFile::IsValidKind(L.Kind))
		{
			R.Fail(FString::Printf(TEXT("bad kind '%s' (barrels | crates | turrets | mines | loot)"), *L.Kind));
		}
		const TSharedPtr<FJsonValue> Seed = R.Get(TEXT("seed"));
		const double SeedValue = Seed.IsValid() && Seed->Type == EJson::Number ? Seed->AsNumber() : -1.0;
		if (!Seed.IsValid())
		{
			R.Fail(TEXT("missing seed"));
		}
		else if (!FMath::IsFinite(SeedValue) || SeedValue < 0.0 || SeedValue > 4294967295.0 || SeedValue != FMath::FloorToDouble(SeedValue))
		{
			R.Fail(TEXT("seed must be an integer in [0, 4294967295]"));
		}
		else
		{
			L.Seed = static_cast<uint32>(SeedValue);
		}
		const bool bHeaderOk = R.bOk;

		R.Bool(TEXT("enabled"), L.bEnabled);
		R.Bool(TEXT("locked"), L.bLocked);
		R.Str(TEXT("preset"), L.Preset);
		R.Str(TEXT("rules_hash"), L.RulesHash);
		L.Rules = R.Object(TEXT("rules"));
		L.Report = R.Object(TEXT("report"));
		if (const TArray<TSharedPtr<FJsonValue>>* Objects = R.Array(TEXT("objects")))
		{
			for (int32 Index = 0; Index < Objects->Num(); ++Index)
			{
				const FString ObjectCtx = FString::Printf(TEXT("%s.objects[%d]"), *Ctx, Index);
				const TSharedPtr<FJsonObject> Object = AsObject((*Objects)[Index]);
				FLevelPropsObject Parsed;
				if (!Object.IsValid())
				{
					Errors.Add(ObjectCtx + TEXT(": not an object"));
				}
				else if (ParseObject(*Object, ObjectCtx, Parsed, Errors))
				{
					bool bDuplicate = false;
					SeenObjectIds.Add(Parsed.Id, &bDuplicate);
					if (bDuplicate)
					{
						Errors.Add(FString::Printf(TEXT("%s: duplicate object id '%s'"), *ObjectCtx, *Parsed.Id));
					}
					else
					{
						L.Objects.Add(MoveTemp(Parsed));
					}
				}
			}
		}
		R.CollectExtra(GLayerKeys, L.Extra);
		return bHeaderOk;
	}

	// ---- writer -------------------------------------------------------------------------------------------------------------

	FString Quote(const FString& In)
	{
		FString Out = TEXT("\"");
		for (const TCHAR C : In)
		{
			switch (C)
			{
			case TEXT('"'): Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			case TEXT('\b'): Out += TEXT("\\b"); break;
			case TEXT('\f'): Out += TEXT("\\f"); break;
			default:
				if (C < 0x20)
				{
					Out += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(C));
				}
				else
				{
					Out.AppendChar(C);
				}
			}
		}
		return Out + TEXT("\"");
	}

	/** Integral values as integers (no ".0"), others with the shortest round-trip text. */
	FString NumberText(double Value)
	{
		if (!FMath::IsFinite(Value))
		{
			return TEXT("null"); // JSON has no NaN / Inf (JS JSON.stringify writes null too)
		}
		if (Value == FMath::FloorToDouble(Value) && FMath::Abs(Value) < 9.0e15)
		{
			return FString::Printf(TEXT("%lld"), static_cast<int64>(Value));
		}
		return FString::SanitizeFloat(Value, 0);
	}

	bool IsScalar(const TSharedPtr<FJsonValue>& Value)
	{
		return IsNullValue(Value) || Value->Type == EJson::Boolean || Value->Type == EJson::Number || Value->Type == EJson::String;
	}

	FString ScalarText(const TSharedPtr<FJsonValue>& Value)
	{
		if (IsNullValue(Value))
		{
			return TEXT("null");
		}
		switch (Value->Type)
		{
		case EJson::Boolean: return Value->AsBool() ? TEXT("true") : TEXT("false");
		case EJson::Number: return NumberText(Value->AsNumber());
		default: return Quote(Value->AsString());
		}
	}

	/** Indenting writer; the caller opens / closes containers, commas and newlines are automatic. */
	struct FJsonOut
	{
		FString S;
		TArray<int32> Counts;
		bool bAfterKey = false;

		void Prep()
		{
			if (bAfterKey)
			{
				bAfterKey = false;
				return;
			}
			if (Counts.Num() > 0)
			{
				if (Counts.Last()++ > 0)
				{
					S += TEXT(",");
				}
				S += TEXT("\n");
				S.Append(FString::ChrN(Counts.Num(), TEXT('\t')));
			}
		}
		void Open(TCHAR C)
		{
			Prep();
			S.AppendChar(C);
			Counts.Add(0);
		}
		void Close(TCHAR C)
		{
			const int32 N = Counts.Pop();
			if (N > 0)
			{
				S += TEXT("\n");
				S.Append(FString::ChrN(Counts.Num(), TEXT('\t')));
			}
			S.AppendChar(C);
		}
		void Key(const FString& K)
		{
			Prep();
			S += Quote(K) + TEXT(": ");
			bAfterKey = true;
		}
		void Raw(const FString& Text)
		{
			Prep();
			S += Text;
		}
		void FieldStr(const TCHAR* K, const FString& V) { Key(K); Raw(Quote(V)); }
		void FieldInt(const TCHAR* K, int64 V) { Key(K); Raw(FString::Printf(TEXT("%lld"), V)); }
		void FieldBool(const TCHAR* K, bool V) { Key(K); Raw(V ? TEXT("true") : TEXT("false")); }
		void FieldPos(const TCHAR* K, const FIntPoint& P) { Key(K); Raw(FString::Printf(TEXT("[%d, %d]"), P.X, P.Y)); }
	};

	void WriteValue(FJsonOut& Out, const TSharedPtr<FJsonValue>& Value);

	void WriteObjectSorted(FJsonOut& Out, const FJsonObject& Object)
	{
		Out.Open(TEXT('{'));
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : SortedEntries(Object))
		{
			Out.Key(Entry.Key);
			WriteValue(Out, Entry.Value);
		}
		Out.Close(TEXT('}'));
	}

	void WriteValue(FJsonOut& Out, const TSharedPtr<FJsonValue>& Value)
	{
		if (IsScalar(Value))
		{
			Out.Raw(ScalarText(Value));
			return;
		}
		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			if (Object.IsValid())
			{
				WriteObjectSorted(Out, *Object);
			}
			else
			{
				Out.Raw(TEXT("null"));
			}
			return;
		}
		if (Value->Type != EJson::Array)
		{
			Out.Raw(TEXT("null"));
			return;
		}
		const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
		bool bAllScalar = true;
		for (const TSharedPtr<FJsonValue>& Item : Items)
		{
			bAllScalar &= IsScalar(Item);
		}
		if (bAllScalar)
		{
			TArray<FString> Parts;
			for (const TSharedPtr<FJsonValue>& Item : Items)
			{
				Parts.Add(ScalarText(Item));
			}
			Out.Raw(TEXT("[") + FString::Join(Parts, TEXT(", ")) + TEXT("]"));
			return;
		}
		Out.Open(TEXT('['));
		for (const TSharedPtr<FJsonValue>& Item : Items)
		{
			WriteValue(Out, Item);
		}
		Out.Close(TEXT(']'));
	}

	/** Unknown keys after the known ones, sorted; a key that collides with a known key of the record is never written twice. */
	template <int32 N>
	void WriteExtra(FJsonOut& Out, const FLevelPropsExtra& Extra, const TCHAR* const (&Known)[N])
	{
		TArray<FString> Keys;
		Extra.GetKeys(Keys);
		SortKeysOrdinal(Keys);
		for (const FString& Key : Keys)
		{
			if (!IsKnownKey(Key, Known))
			{
				Out.Key(Key);
				WriteValue(Out, Extra[Key]);
			}
		}
	}

	void WriteObject(FJsonOut& Out, const FLevelPropsObject& O)
	{
		Out.Open(TEXT('{'));
		Out.FieldStr(TEXT("id"), O.Id);
		Out.FieldStr(TEXT("variant"), O.Variant);
		Out.FieldPos(TEXT("pos"), O.Pos);
		Out.FieldStr(TEXT("walk_layer"), O.WalkLayer);
		Out.FieldInt(TEXT("yaw"), O.Yaw);
		Out.FieldBool(TEXT("locked"), O.bLocked);
		if (!O.Origin.IsEmpty()) { Out.FieldStr(TEXT("origin"), O.Origin); }
		if (!O.Attach.IsEmpty()) { Out.FieldStr(TEXT("attach"), O.Attach); }
		if (!O.Door.IsEmpty()) { Out.FieldStr(TEXT("door"), O.Door); }
		if (!O.Trigger.IsEmpty()) { Out.FieldStr(TEXT("trigger"), O.Trigger); }
		if (!O.LootTable.IsEmpty()) { Out.FieldStr(TEXT("loot_table"), O.LootTable); }
		if (O.Arc.IsSet()) { Out.FieldInt(TEXT("arc"), O.Arc.GetValue()); }
		WriteExtra(Out, O.Extra, GObjectKeys);
		Out.Close(TEXT('}'));
	}

	const TCHAR* OpName(ELevelPropsOp Op)
	{
		switch (Op)
		{
		case ELevelPropsOp::Move: return TEXT("move");
		case ELevelPropsOp::Delete: return TEXT("delete");
		case ELevelPropsOp::Add: return TEXT("add");
		default: return TEXT("set");
		}
	}

	// ---- overrides ----------------------------------------------------------------------------------------------------------

	/** The layer an id belongs to: the longest layer id that prefixes it ("<layer>_..."); hand ids are "hand_<layer>_<n>". */
	const FLevelPropsLayer* FindLayerForId(const FLevelPropsFile& File, const FString& Id)
	{
		const FString Name = Id.StartsWith(GHandPrefix, ESearchCase::CaseSensitive) ? Id.RightChop(FCString::Strlen(GHandPrefix)) : Id;
		const FLevelPropsLayer* Best = nullptr;
		for (const FLevelPropsLayer& Layer : File.Layers)
		{
			if (Name.StartsWith(Layer.Id + TEXT("_"), ESearchCase::CaseSensitive) && (!Best || Layer.Id.Len() > Best->Id.Len()))
			{
				Best = &Layer;
			}
		}
		return Best;
	}

	int32 SpacingOf(const FLevelPropsLayer* Layer)
	{
		if (!Layer || !Layer->Rules.IsValid())
		{
			return 0;
		}
		const TSharedPtr<FJsonValue> Value = Layer->Rules->TryGetField(TEXT("min_spacing_cm"));
		int32 Spacing = 0;
		return Value.IsValid() && Value->Type == EJson::Number && ToInt32(Value->AsNumber(), Spacing) ? FMath::Max(0, Spacing) : 0;
	}

	/** A set override's fields onto an object. Null clears an optional field; a known key of the wrong type is skipped with a warning. */
	void ApplySetFields(const FJsonObject& Fields, const FString& Id, FLevelPropsObject& O, const TFunctionRef<void(const FString&)> Warn)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : SortedEntries(Fields)) // sorted: deterministic warnings
		{
			const FString& K = Entry.Key;
			const TSharedPtr<FJsonValue>& V = Entry.Value;
			const bool bNull = IsNullValue(V);
			const auto WrongType = [&](const TCHAR* Expected)
			{
				Warn(FString::Printf(TEXT("set '%s': field '%s' must be %s; skipped"), *Id, *K, Expected));
			};

			FString* StringField = nullptr;
			const TCHAR* StringDefault = TEXT("");
			if (K == TEXT("walk_layer")) { StringField = &O.WalkLayer; StringDefault = TEXT("ground"); }
			else if (K == TEXT("origin")) { StringField = &O.Origin; }
			else if (K == TEXT("attach")) { StringField = &O.Attach; }
			else if (K == TEXT("door")) { StringField = &O.Door; }
			else if (K == TEXT("trigger")) { StringField = &O.Trigger; }
			else if (K == TEXT("loot_table")) { StringField = &O.LootTable; }

			if (K == TEXT("id"))
			{
				Warn(FString::Printf(TEXT("set '%s': ids are immutable; 'id' skipped"), *Id));
			}
			else if (K == TEXT("variant"))
			{
				if (bNull || V->Type != EJson::String || V->AsString().IsEmpty()) { WrongType(TEXT("a non-empty string")); }
				else { O.Variant = V->AsString(); }
			}
			else if (StringField)
			{
				if (bNull) { *StringField = StringDefault; }
				else if (V->Type != EJson::String) { WrongType(TEXT("a string or null")); }
				else { *StringField = V->AsString(); }
			}
			else if (K == TEXT("yaw") || K == TEXT("arc"))
			{
				int32 Number = 0;
				if (bNull)
				{
					if (K == TEXT("yaw")) { O.Yaw = 0; }
					else { O.Arc.Reset(); }
				}
				else if (V->Type != EJson::Number || !ToInt32(V->AsNumber(), Number)) { WrongType(TEXT("an integer or null")); }
				else if (K == TEXT("yaw")) { O.Yaw = Number; }
				else { O.Arc = Number; }
			}
			else if (K == TEXT("locked"))
			{
				if (bNull) { O.bLocked = false; }
				else if (V->Type != EJson::Boolean) { WrongType(TEXT("true / false or null")); }
				else { O.bLocked = V->AsBool(); }
			}
			else if (K == TEXT("pos"))
			{
				FIntPoint Pos;
				if (!ReadPosValue(V, Pos)) { WrongType(TEXT("[x, y] in cm (a move override changes the position)")); }
				else { O.Pos = Pos; }
			}
			else if (bNull)
			{
				O.Extra.Remove(K);
			}
			else
			{
				O.Extra.Add(K, V); // e.g. explosive on / off: a kind field interpreted by the placement / runtime (C3+)
			}
		}
	}
}

bool LevelPropsFile::IsValidKind(const FString& Kind)
{
	for (const TCHAR* K : LevelPropsFileLocal::GKinds)
	{
		if (Kind.Equals(K, ESearchCase::CaseSensitive))
		{
			return true;
		}
	}
	return false;
}

bool LevelPropsFile::Parse(const FString& Json, FLevelPropsFile& Out, TArray<FString>& Errors)
{
	using namespace LevelPropsFileLocal;

	Out = FLevelPropsFile();
	const int32 FirstError = Errors.Num();
	const auto Finish = [&Errors, FirstError]()
	{
		for (int32 Index = FirstError; Index < Errors.Num(); ++Index)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Props file: %s"), *Errors[Index]);
		}
		return Errors.Num() == FirstError;
	};

	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		Errors.Add(TEXT("invalid JSON (the root must be an object)"));
		return Finish();
	}

	FFieldReader R(*Root, TEXT("root"), Errors);
	FString Schema;
	FString Family;
	FString Version;
	if (!R.Str(TEXT("schema"), Schema) || !Schema.Split(TEXT("/"), &Family, &Version, ESearchCase::CaseSensitive, ESearchDir::FromEnd)
		|| Family != SchemaFamily)
	{
		Errors.SetNum(FirstError);
		Errors.Add(FString::Printf(TEXT("not a %s file (schema '%s'); file refused"), SchemaFamily, *Schema));
		return Finish();
	}
	if (!Version.IsNumeric() || Version.Contains(TEXT(".")) || FCString::Atoi(*Version) != 1)
	{
		Errors.Add(FString::Printf(TEXT("unsupported schema version '%s' (this build reads %s); file refused"), *Schema, SchemaId));
		return Finish();
	}

	R.Str(TEXT("map"), Out.Map);
	R.Str(TEXT("layout"), Out.Layout);
	R.Str(TEXT("layout_hash"), Out.LayoutHash);
	R.Int(TEXT("revision"), Out.Revision);
	R.Bool(TEXT("frozen"), Out.bFrozen);

	TSet<FString> SeenObjectIds;
	if (const TArray<TSharedPtr<FJsonValue>>* Layers = R.Array(TEXT("layers")))
	{
		for (int32 Index = 0; Index < Layers->Num(); ++Index)
		{
			const FString Ctx = FString::Printf(TEXT("layers[%d]"), Index);
			const TSharedPtr<FJsonObject> Object = AsObject((*Layers)[Index]);
			FLevelPropsLayer Layer;
			if (!Object.IsValid())
			{
				Errors.Add(Ctx + TEXT(": not an object"));
			}
			else if (ParseLayer(*Object, Ctx, Layer, SeenObjectIds, Errors))
			{
				if (Out.Layers.ContainsByPredicate([&Layer](const FLevelPropsLayer& L) { return L.Id == Layer.Id; }))
				{
					Errors.Add(FString::Printf(TEXT("%s: duplicate layer id '%s'"), *Ctx, *Layer.Id));
				}
				else
				{
					Out.Layers.Add(MoveTemp(Layer));
				}
			}
		}
	}
	if (const TArray<TSharedPtr<FJsonValue>>* Overrides = R.Array(TEXT("overrides")))
	{
		for (int32 Index = 0; Index < Overrides->Num(); ++Index)
		{
			const FString Ctx = FString::Printf(TEXT("overrides[%d]"), Index);
			const TSharedPtr<FJsonObject> Object = AsObject((*Overrides)[Index]);
			FLevelPropsOverride Parsed;
			if (!Object.IsValid())
			{
				Errors.Add(Ctx + TEXT(": not an object"));
			}
			else if (ParseOverride(*Object, Ctx, Parsed, Errors))
			{
				Out.Overrides.Add(MoveTemp(Parsed));
			}
		}
	}
	R.CollectExtra(GRootKeys, Out.Extra);
	return Finish();
}

FString LevelPropsFile::Write(const FLevelPropsFile& File)
{
	using namespace LevelPropsFileLocal;

	FJsonOut Out;
	Out.Open(TEXT('{'));
	Out.FieldStr(TEXT("schema"), SchemaId);
	Out.FieldStr(TEXT("map"), File.Map);
	Out.FieldStr(TEXT("layout"), File.Layout);
	Out.FieldStr(TEXT("layout_hash"), File.LayoutHash);
	Out.FieldInt(TEXT("revision"), File.Revision);
	Out.FieldBool(TEXT("frozen"), File.bFrozen);
	WriteExtra(Out, File.Extra, GRootKeys);

	Out.Key(TEXT("layers"));
	Out.Open(TEXT('['));
	for (const FLevelPropsLayer& L : File.Layers)
	{
		Out.Open(TEXT('{'));
		Out.FieldStr(TEXT("id"), L.Id);
		Out.FieldStr(TEXT("kind"), L.Kind);
		Out.FieldBool(TEXT("enabled"), L.bEnabled);
		Out.FieldInt(TEXT("seed"), static_cast<int64>(L.Seed));
		Out.FieldBool(TEXT("locked"), L.bLocked);
		if (!L.Preset.IsEmpty()) { Out.FieldStr(TEXT("preset"), L.Preset); }
		if (L.Rules.IsValid()) { Out.Key(TEXT("rules")); WriteObjectSorted(Out, *L.Rules); }
		if (!L.RulesHash.IsEmpty()) { Out.FieldStr(TEXT("rules_hash"), L.RulesHash); }
		Out.Key(TEXT("objects"));
		Out.Open(TEXT('['));
		for (const FLevelPropsObject& O : L.Objects)
		{
			WriteObject(Out, O);
		}
		Out.Close(TEXT(']'));
		if (L.Report.IsValid()) { Out.Key(TEXT("report")); WriteObjectSorted(Out, *L.Report); }
		WriteExtra(Out, L.Extra, GLayerKeys);
		Out.Close(TEXT('}'));
	}
	Out.Close(TEXT(']'));

	Out.Key(TEXT("overrides"));
	Out.Open(TEXT('['));
	for (const FLevelPropsOverride& O : File.Overrides)
	{
		Out.Open(TEXT('{'));
		Out.FieldStr(TEXT("op"), OpName(O.Op));
		if (O.Op == ELevelPropsOp::Add)
		{
			Out.Key(TEXT("object"));
			WriteObject(Out, O.Object);
		}
		else
		{
			Out.FieldStr(TEXT("id"), O.Id);
		}
		if (O.Pos.IsSet()) { Out.FieldPos(TEXT("pos"), O.Pos.GetValue()); }
		if (O.WalkLayer.IsSet()) { Out.FieldStr(TEXT("walk_layer"), O.WalkLayer.GetValue()); }
		if (O.ZOffset.IsSet()) { Out.FieldInt(TEXT("z_offset"), O.ZOffset.GetValue()); }
		if (O.Yaw.IsSet()) { Out.FieldInt(TEXT("yaw"), O.Yaw.GetValue()); }
		if (O.Fields.IsValid()) { Out.Key(TEXT("fields")); WriteObjectSorted(Out, *O.Fields); }
		WriteExtra(Out, O.Extra, GOverrideKeys);
		Out.Close(TEXT('}'));
	}
	Out.Close(TEXT(']'));
	Out.Close(TEXT('}'));
	return Out.S + TEXT("\n");
}

FLevelPropsResolved LevelPropsFile::ApplyOverrides(const FLevelPropsFile& File, TArray<FString>* OutWarnings)
{
	using namespace LevelPropsFileLocal;

	FLevelPropsResolved Result;
	const auto Warn = [OutWarnings](const FString& Message)
	{
		if (OutWarnings)
		{
			OutWarnings->Add(Message);
		}
	};

	TSet<FString> DisabledIds;
	for (const FLevelPropsLayer& Layer : File.Layers)
	{
		for (const FLevelPropsObject& Object : Layer.Objects)
		{
			if (!Layer.bEnabled)
			{
				DisabledIds.Add(Object.Id);
				continue;
			}
			FLevelPropsResolvedObject& Resolved = Result.Objects.AddDefaulted_GetRef();
			Resolved.LayerId = Layer.Id;
			Resolved.Object = Object;
			Resolved.bHand = Object.Origin == GHandOrigin;
			Resolved.bPinned = Resolved.bHand;
		}
	}
	const auto Find = [&Result](const FString& Id)
	{
		return Result.Objects.IndexOfByPredicate([&Id](const FLevelPropsResolvedObject& R) { return R.Object.Id == Id; });
	};
	/** The target sits in a disabled layer (stored there, or its id names one): the override is skipped quietly. */
	const auto InDisabledLayer = [&File, &DisabledIds](const FString& Id)
	{
		const FLevelPropsLayer* Layer = FindLayerForId(File, Id);
		return DisabledIds.Contains(Id) || (Layer && !Layer->bEnabled);
	};

	for (const FLevelPropsOverride& Op : File.Overrides)
	{
		const int32 Index = Find(Op.Id);
		if (Index == INDEX_NONE && InDisabledLayer(Op.Id))
		{
			continue;
		}
		switch (Op.Op)
		{
		case ELevelPropsOp::Move:
		{
			if (Index == INDEX_NONE)
			{
				Warn(FString::Printf(TEXT("move: no object '%s' (re-rolled away?); skipped"), *Op.Id));
				break;
			}
			FLevelPropsResolvedObject& R = Result.Objects[Index];
			if (Op.Pos.IsSet()) { R.Object.Pos = Op.Pos.GetValue(); }
			if (Op.WalkLayer.IsSet()) { R.Object.WalkLayer = Op.WalkLayer.GetValue(); }
			if (Op.Yaw.IsSet()) { R.Object.Yaw = Op.Yaw.GetValue(); }
			if (Op.ZOffset.IsSet()) { R.ZOffsetCm = Op.ZOffset.GetValue(); }
			R.bPinned = true;
			break;
		}
		case ELevelPropsOp::Delete:
		{
			const bool bHand = Op.Id.StartsWith(GHandPrefix, ESearchCase::CaseSensitive) || (Index != INDEX_NONE && Result.Objects[Index].bHand);
			if (!bHand && !Result.KeepOuts.ContainsByPredicate([&Op](const FLevelPropsKeepOut& K) { return K.Id == Op.Id; }))
			{
				if (Op.Pos.IsSet() || Index != INDEX_NONE)
				{
					const FLevelPropsLayer* Layer = Index != INDEX_NONE
						? File.Layers.FindByPredicate([&](const FLevelPropsLayer& L) { return L.Id == Result.Objects[Index].LayerId; })
						: FindLayerForId(File, Op.Id);
					FLevelPropsKeepOut& KeepOut = Result.KeepOuts.AddDefaulted_GetRef();
					KeepOut.Id = Op.Id;
					KeepOut.LayerId = Layer ? Layer->Id : FString();
					KeepOut.Pos = Op.Pos.IsSet() ? Op.Pos.GetValue() : Result.Objects[Index].Object.Pos;
					KeepOut.RadiusCm = SpacingOf(Layer);
					if (!Layer)
					{
						Warn(FString::Printf(TEXT("delete: '%s' names no layer; keep-out without a radius"), *Op.Id));
					}
				}
				else
				{
					Warn(FString::Printf(TEXT("delete: '%s' has no pos and no object; no keep-out"), *Op.Id));
				}
			}
			if (Index != INDEX_NONE)
			{
				Result.Objects.RemoveAt(Index);
			}
			break;
		}
		case ELevelPropsOp::Add:
		{
			if (Index != INDEX_NONE)
			{
				Warn(FString::Printf(TEXT("add: id '%s' already exists; skipped"), *Op.Id));
				break;
			}
			const FLevelPropsLayer* Layer = FindLayerForId(File, Op.Id);
			if (!Layer)
			{
				Warn(FString::Printf(TEXT("add: '%s' names no layer (hand_<layer>_<n>); added without a layer"), *Op.Id));
			}
			FLevelPropsResolvedObject& R = Result.Objects.AddDefaulted_GetRef();
			R.Object = Op.Object;
			R.Object.Origin = GHandOrigin;
			R.LayerId = Layer ? Layer->Id : FString();
			R.bHand = true;
			R.bPinned = true;
			break;
		}
		case ELevelPropsOp::Set:
		{
			if (Index == INDEX_NONE)
			{
				Warn(FString::Printf(TEXT("set: no object '%s' (re-rolled away?); skipped"), *Op.Id));
				break;
			}
			if (Op.Fields.IsValid())
			{
				ApplySetFields(*Op.Fields, Op.Id, Result.Objects[Index].Object, Warn);
			}
			Result.Objects[Index].bPinned = true;
			break;
		}
		}
	}
	return Result;
}
