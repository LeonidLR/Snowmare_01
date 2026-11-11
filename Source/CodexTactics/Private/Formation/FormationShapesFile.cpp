#include "Formation/FormationShapesFile.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Formation/FormationShapeRules.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace FormationShapesLocal
{
	TAutoConsoleVariable<int32> CVarFormationSavedShapes(TEXT("Codex.Formation.SavedShapes"), 1,
		TEXT("Game designer 2026-11-10 (variant B, contract FORMATION_CONTRACT.md 19): 1 = the Training Ground formation editor's Content/Data/FormationLab/formation_shapes.json replaces the formula's slots / looks for a saved (formation, member count) and seeds the custom slots; 0 = the formula everywhere (A/B)"));

	const TCHAR* const Schema = TEXT("codex.formation_shapes/1");

	/** The JSON value of Key, null when missing (FJsonObject's lookup is case-insensitive). */
	TSharedPtr<FJsonValue> Field(const FJsonObject& Object, const TCHAR* Key)
	{
		return Object.TryGetField(Key);
	}

	/** A JSON number that is an integer (TS Number.isInteger): finite, no fraction, within int32. */
	bool ReadInt(const TSharedPtr<FJsonValue>& Value, int32& Out)
	{
		if (!Value.IsValid() || Value->Type != EJson::Number)
		{
			return false;
		}
		const double Number = Value->AsNumber();
		if (!FMath::IsFinite(Number) || Number != FMath::FloorToDouble(Number) || Number < static_cast<double>(MIN_int32)
			|| Number > static_cast<double>(MAX_int32))
		{
			return false;
		}
		Out = static_cast<int32>(Number);
		return true;
	}

	TSharedPtr<FJsonObject> AsObject(const TSharedPtr<FJsonValue>& Value)
	{
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
	}

	bool ParseRoleName(const FString& Name, EFormationRole& Out)
	{
		for (const EFormationRole Role : { EFormationRole::Commander, EFormationRole::Right, EFormationRole::Left, EFormationRole::Rear })
		{
			if (Name.Equals(FormationShapes::RoleName(Role), ESearchCase::CaseSensitive))
			{
				Out = Role;
				return true;
			}
		}
		return false;
	}

	/** The look / arc / reach rules both branches share (validateFormationShapes). */
	void CheckNumbers(const FString& Where, const FJsonObject& Member, int32& OutFwd, int32& OutRight, int32& OutLook, TOptional<int32>& OutArc,
		TArray<FString>& Errors)
	{
		const bool bFwd = ReadInt(Field(Member, TEXT("fwd")), OutFwd);
		const bool bRight = ReadInt(Field(Member, TEXT("right")), OutRight);
		if (!bFwd || !bRight)
		{
			Errors.Add(Where + TEXT(": fwd and right must be integer cm"));
		}
		else
		{
			const double Radius = FMath::Sqrt(static_cast<double>(OutFwd) * OutFwd + static_cast<double>(OutRight) * OutRight);
			if (Radius > FormationCustomRules::MaxRadius)
			{
				Errors.Add(FString::Printf(TEXT("%s: distance %.0f cm exceeds CUSTOM_MAX_RADIUS %.0f"), *Where, Radius, FormationCustomRules::MaxRadius));
			}
		}
		if (!ReadInt(Field(Member, TEXT("look")), OutLook) || OutLook <= -180 || OutLook > 180)
		{
			Errors.Add(Where + TEXT(": look must be integer degrees in (-180, 180]"));
		}
		const TSharedPtr<FJsonValue> ArcValue = Field(Member, TEXT("arc"));
		OutArc.Reset();
		// TS `m.arc !== undefined && !isInteger(m.arc)`: only an ABSENT arc is the default - "arc": null is an error like any non-integer.
		if (ArcValue.IsValid())
		{
			int32 Arc = 0;
			if (!ReadInt(ArcValue, Arc) || Arc < 10 || Arc > 360)
			{
				Errors.Add(Where + TEXT(": arc must be integer degrees between 10 and 360"));
			}
			else
			{
				OutArc = Arc;
			}
		}
	}

	bool ParseShape(const FString& Where, const TSharedPtr<FJsonValue>& Value, int32 Count, FFormationShape& Out, TArray<FString>& Errors)
	{
		const int32 ErrorsBefore = Errors.Num();
		const TSharedPtr<FJsonObject> Shape = AsObject(Value);
		if (!Shape.IsValid())
		{
			Errors.Add(Where + TEXT(": expected an object"));
			return false;
		}
		const TSharedPtr<FJsonValue> MembersValue = Field(*Shape, TEXT("members"));
		if (!MembersValue.IsValid() || MembersValue->Type != EJson::Array)
		{
			Errors.Add(Where + TEXT(".members: expected an array"));
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>& Members = MembersValue->AsArray();
		if (Members.Num() != Count)
		{
			Errors.Add(FString::Printf(TEXT("%s: members array length %d does not match count %d"), *Where, Members.Num(), Count));
		}
		TSet<EFormationRole> Seen;
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			const FString At = FString::Printf(TEXT("%s.members[%d]"), *Where, Index);
			const TSharedPtr<FJsonObject> Member = AsObject(Members[Index]);
			if (!Member.IsValid())
			{
				Errors.Add(At + TEXT(": expected an object"));
				continue;
			}
			FFormationShapeMember& Each = Out.Members.AddDefaulted_GetRef();
			const TSharedPtr<FJsonValue> RoleValue = Field(*Member, TEXT("role"));
			const FString RoleText = RoleValue.IsValid() && RoleValue->Type == EJson::String ? RoleValue->AsString() : FString();
			const bool bRole = ParseRoleName(RoleText, Each.Role);
			if (Index == 0 && (!bRole || Each.Role != EFormationRole::Commander))
			{
				Errors.Add(Where + TEXT(": member 0 must be the commander"));
			}
			if (!bRole)
			{
				Errors.Add(FString::Printf(TEXT("%s: bad role \"%s\""), *At, *RoleText));
			}
			else if (Seen.Contains(Each.Role))
			{
				Errors.Add(FString::Printf(TEXT("%s: duplicate role \"%s\""), *Where, *RoleText));
			}
			else
			{
				Seen.Add(Each.Role);
			}
			CheckNumbers(At, *Member, Each.Fwd, Each.Right, Each.Look, Each.Arc, Errors);
			if (bRole && Each.Role == EFormationRole::Commander && (Each.Fwd != 0 || Each.Right != 0))
			{
				Errors.Add(Where + TEXT(": commander must be at fwd 0, right 0"));
			}
		}
		const TArray<EFormationRole> Expected = FormationShapeRules::Roles(Count);
		if (Seen.Num() > 0 && (Seen.Num() != Expected.Num() || Expected.ContainsByPredicate([&Seen](EFormationRole Role) { return !Seen.Contains(Role); })))
		{
			Errors.Add(FString::Printf(TEXT("%s: roles must match rolesFor(%d)"), *Where, Count));
		}
		return Errors.Num() == ErrorsBefore;
	}

	bool ParseCustom(const FString& Where, const TSharedPtr<FJsonValue>& Value, FFormationCustomShape& Out, TArray<FString>& Errors)
	{
		const int32 ErrorsBefore = Errors.Num();
		const TSharedPtr<FJsonObject> Shape = AsObject(Value);
		if (!Shape.IsValid())
		{
			Errors.Add(Where + TEXT(": expected an object"));
			return false;
		}
		const TSharedPtr<FJsonValue> MembersValue = Field(*Shape, TEXT("members"));
		if (!MembersValue.IsValid() || MembersValue->Type != EJson::Array)
		{
			Errors.Add(Where + TEXT(".members: expected an array"));
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>& Members = MembersValue->AsArray();
		TSet<int32> Seen;
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			const FString At = FString::Printf(TEXT("%s.members[%d]"), *Where, Index);
			const TSharedPtr<FJsonObject> Member = AsObject(Members[Index]);
			if (!Member.IsValid())
			{
				Errors.Add(At + TEXT(": expected an object"));
				continue;
			}
			int32 Squad = 0;
			const bool bSquad = ReadInt(Field(*Member, TEXT("squad")), Squad) && Squad >= 0 && Squad <= 3;
			if (!bSquad)
			{
				Errors.Add(At + TEXT(": squad must be 0..3"));
			}
			else if (Seen.Contains(Squad))
			{
				Errors.Add(FString::Printf(TEXT("%s: duplicate squad index \"%d\""), *Where, Squad));
			}
			else
			{
				Seen.Add(Squad);
			}
			int32 Fwd = 0;
			int32 Right = 0;
			int32 Look = 0;
			TOptional<int32> Arc;
			CheckNumbers(At, *Member, Fwd, Right, Look, Arc, Errors);
			if (bSquad && Squad == 0 && (Fwd != 0 || Right != 0))
			{
				Errors.Add(Where + TEXT(": squad 0 (anchor) must be at fwd 0, right 0"));
			}
			FFormationCustomMember& Each = Out.Members.AddDefaulted_GetRef();
			Each.Squad = Squad;
			Each.Fwd = static_cast<float>(Fwd);
			Each.Right = static_cast<float>(Right);
			Each.Look = static_cast<float>(Look);
		}
		return Errors.Num() == ErrorsBefore;
	}

	FVector2D ShapeLocalToWorld(const FVector2D& Origin, double HeadingYaw, double Fwd, double Right)
	{
		const FVector2D F = FormationMath::DirOf(HeadingYaw);
		const FVector2D R(-F.Y, F.X);
		return FVector2D(Origin.X + F.X * Fwd + R.X * Right, Origin.Y + F.Y * Fwd + R.Y * Right);
	}

	/** The game's view of the file (game thread only). */
	struct FRuntime
	{
		FFormationShapesFile Loaded;
		FFormationShapesFile Override;
		bool bOverride = false;
		bool bRead = false;
		FString PathOverride;
		FDateTime Stamp = FDateTime::MinValue();
		int64 Size = -2;
		double NextCheck = 0.0;
		uint32 Revision = 0;
	};

	FRuntime& Runtime()
	{
		static FRuntime State;
		return State;
	}

	const FFormationShapesFile& EmptyFile()
	{
		static const FFormationShapesFile Empty;
		return Empty;
	}

	int32 CountEntries(const FFormationShapesFile& File, int32& OutCustom)
	{
		int32 Entries = 0;
		for (const TPair<FString, TMap<int32, FFormationShape>>& Pair : File.Formations)
		{
			Entries += Pair.Value.Num();
		}
		OutCustom = 0;
		for (const FFormationCustomShape& Shape : File.Custom)
		{
			OutCustom += Shape.IsEmpty() ? 0 : 1;
		}
		return Entries;
	}

	/** Reads the file (its stamp / size already taken); a missing file = empty, an invalid one keeps the last content. */
	bool Load(FRuntime& State, const FString& Path)
	{
		State.bRead = true;
		IFileManager& Files = IFileManager::Get();
		State.Stamp = Files.GetTimeStamp(*Path);
		State.Size = Files.FileSize(*Path);
		if (State.Size < 0)
		{
			if (!State.Loaded.IsEmpty())
			{
				++State.Revision;
				UE_LOG(LogCodexTactics, Display, TEXT("[Formation] saved shapes: %s removed - the formula again"), *Path);
			}
			State.Loaded = FFormationShapesFile();
			return true;
		}
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Formation] saved shapes: %s unreadable, the last shapes kept"), *Path);
			return false;
		}
		FFormationShapesFile Parsed;
		TArray<FString> Errors;
		TArray<FString> Warnings;
		const bool bOk = FormationShapes::Parse(Text, Parsed, Errors, &Warnings);
		for (const FString& Warning : Warnings)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Formation] saved shapes %s: %s"), *Path, *Warning);
		}
		if (!bOk)
		{
			for (const FString& Error : Errors)
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("[Formation] saved shapes %s: %s"), *Path, *Error);
			}
			UE_LOG(LogCodexTactics, Warning, TEXT("[Formation] saved shapes: %s rejected (%d error(s)), the last shapes kept"), *Path, Errors.Num());
			return false;
		}
		State.Loaded = MoveTemp(Parsed);
		++State.Revision;
		int32 Custom = 0;
		const int32 Entries = CountEntries(State.Loaded, Custom);
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] saved shapes from %s: %d formation shape(s), %d custom default(s) (revision %u)"), *Path, Entries, Custom,
			State.Revision);
		return true;
	}
}

bool FFormationShapesFile::IsEmpty() const
{
	if (!Formations.IsEmpty())
	{
		for (const TPair<FString, TMap<int32, FFormationShape>>& Pair : Formations)
		{
			if (!Pair.Value.IsEmpty())
			{
				return false;
			}
		}
	}
	return !Custom.ContainsByPredicate([](const FFormationCustomShape& Shape) { return !Shape.IsEmpty(); });
}

namespace FormationShapes
{
	const TCHAR* GetSchema()
	{
		return FormationShapesLocal::Schema;
	}

	const TCHAR* RoleName(EFormationRole Role)
	{
		switch (Role)
		{
		case EFormationRole::Commander: return TEXT("commander");
		case EFormationRole::Right: return TEXT("right");
		case EFormationRole::Left: return TEXT("left");
		case EFormationRole::Rear: return TEXT("rear");
		default: return TEXT("commander");
		}
	}

	bool IsShapeable(EFormationKind Kind)
	{
		switch (Kind)
		{
		case EFormationKind::Diamond:
		case EFormationKind::Wedge:
		case EFormationKind::Column:
		case EFormationKind::Line:
		case EFormationKind::EchelonLeft:
		case EFormationKind::EchelonRight:
			return true;
		default:
			return false;
		}
	}

	bool Parse(const FString& JsonText, FFormationShapesFile& Out, TArray<FString>& OutErrors, TArray<FString>* OutWarnings)
	{
		using namespace FormationShapesLocal;
		Out = FFormationShapesFile();
		const int32 ErrorsBefore = OutErrors.Num();
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) || !Root.IsValid())
		{
			OutErrors.Add(TEXT("expected a JSON object"));
			return false;
		}
		const TSharedPtr<FJsonValue> SchemaValue = Field(*Root, TEXT("schema"));
		const FString SchemaText = SchemaValue.IsValid() && SchemaValue->Type == EJson::String ? SchemaValue->AsString() : FString();
		if (!SchemaText.Equals(Schema, ESearchCase::CaseSensitive))
		{
			OutErrors.Add(FString::Printf(TEXT("schema must be \"%s\" (got \"%s\")"), Schema, *SchemaText));
		}
		const TSharedPtr<FJsonValue> FormationsValue = Field(*Root, TEXT("formations"));
		if (FormationsValue.IsValid())
		{
			const TSharedPtr<FJsonObject> Formations = AsObject(FormationsValue);
			if (!Formations.IsValid())
			{
				OutErrors.Add(TEXT("formations must be an object"));
			}
			else
			{
				for (const auto& FormationEntry : Formations->Values)
				{
					const FString Name(*FormationEntry.Key);
					const TSharedPtr<FJsonObject> ByCount = AsObject(FormationEntry.Value);
					if (!ByCount.IsValid())
					{
						OutErrors.Add(FString::Printf(TEXT("formations.%s: expected an object mapping member counts to shapes"), *Name));
						continue;
					}
					EFormationKind Kind = EFormationKind::Diamond;
					const bool bPair = Name.Equals(TEXT("pair"), ESearchCase::IgnoreCase);
					const bool bKnown = bPair || FormationNames::Parse(Name, Kind);
					const bool bKeep = bPair || (bKnown && IsShapeable(Kind));
					if (!bKnown && OutWarnings)
					{
						OutWarnings->Add(FString::Printf(TEXT("formations.%s: not a formation of the game - ignored"), *Name));
					}
					else if (!bKeep && OutWarnings)
					{
						OutWarnings->Add(FString::Printf(TEXT("formations.%s: stays rule-driven (wall / hedgehog / blizzard / custom) - ignored"), *Name));
					}
					for (const auto& CountEntry : ByCount->Values)
					{
						const FString CountKey(*CountEntry.Key);
						int32 Count = 0;
						if (!CountKey.IsNumeric() || !LexTryParseString(Count, *CountKey) || Count < 2 || Count > 4)
						{
							OutErrors.Add(FString::Printf(TEXT("formations.%s: member count \"%s\" must be 2, 3, or 4"), *Name, *CountKey));
							continue;
						}
						FFormationShape Shape;
						if (ParseShape(FString::Printf(TEXT("formations.%s[\"%s\"]"), *Name, *CountKey), CountEntry.Value, Count, Shape, OutErrors) && bKeep)
						{
							if (bPair && Count != 2 && OutWarnings)
							{
								OutWarnings->Add(FString::Printf(TEXT("formations.pair[\"%d\"]: the pair has 2 members - ignored"), Count));
								continue;
							}
							Out.Formations.FindOrAdd(Name.ToLower()).Add(Count, MoveTemp(Shape));
						}
					}
				}
			}
		}
		const TSharedPtr<FJsonValue> CustomValue = Field(*Root, TEXT("custom"));
		if (CustomValue.IsValid())
		{
			const TSharedPtr<FJsonObject> Custom = AsObject(CustomValue);
			if (!Custom.IsValid())
			{
				OutErrors.Add(TEXT("custom must be an object"));
			}
			else
			{
				for (const auto& SlotEntry : Custom->Values)
				{
					const FString Key(*SlotEntry.Key);
					int32 Slot = 0;
					for (int32 Index = 1; Index <= FormationCustomRules::SlotCount; ++Index)
					{
						if (Key.Equals(FString::Printf(TEXT("custom_%d"), Index), ESearchCase::CaseSensitive))
						{
							Slot = Index;
						}
					}
					if (Slot == 0)
					{
						OutErrors.Add(FString::Printf(TEXT("custom: unexpected slot key \"%s\""), *Key));
					}
					FFormationCustomShape Shape;
					if (ParseCustom(FString::Printf(TEXT("custom.%s"), *Key), SlotEntry.Value, Shape, OutErrors) && Slot > 0)
					{
						Out.Custom[Slot - 1] = MoveTemp(Shape);
					}
				}
			}
		}
		if (OutErrors.Num() != ErrorsBefore)
		{
			Out = FFormationShapesFile();
			return false;
		}
		return true;
	}

	FFormationShape Mirror(const FFormationShape& Shape)
	{
		FFormationShape Out;
		for (const FFormationShapeMember& Member : Shape.Members)
		{
			FFormationShapeMember& Each = Out.Members.Add_GetRef(Member);
			Each.Role = Member.Role == EFormationRole::Left ? EFormationRole::Right : Member.Role == EFormationRole::Right ? EFormationRole::Left : Member.Role;
			Each.Right = -Member.Right;
			Each.Look = -Member.Look <= -180 ? 180 : -Member.Look;
		}
		return Out;
	}

	TOptional<FFormationShape> Find(const FFormationShapesFile& File, EFormationKind Kind, int32 MemberCount, bool* bOutMirrored)
	{
		if (bOutMirrored)
		{
			*bOutMirrored = false;
		}
		const int32 Count = FMath::Min(MemberCount, 4);
		if (!IsShapeable(Kind) || Count < 2)
		{
			return TOptional<FFormationShape>();
		}
		const auto Lookup = [&File, Count](const TCHAR* Name) -> const FFormationShape*
		{
			const TMap<int32, FFormationShape>* ByCount = File.Formations.Find(Name);
			return ByCount ? ByCount->Find(Count) : nullptr;
		};
		if (const FFormationShape* Exact = Lookup(FormationNames::ToString(Kind)))
		{
			return *Exact;
		}
		// The game's pair = the wedge / diamond with 2 members (the menu's 1:30 item, HUD "Pair"); no other 2-man formation takes it (TS review 008).
		if (Count == 2 && (Kind == EFormationKind::Wedge || Kind == EFormationKind::Diamond))
		{
			if (const FFormationShape* Pair = Lookup(TEXT("pair")))
			{
				return *Pair;
			}
		}
		if (Kind == EFormationKind::EchelonLeft || Kind == EFormationKind::EchelonRight)
		{
			const EFormationKind Other = Kind == EFormationKind::EchelonLeft ? EFormationKind::EchelonRight : EFormationKind::EchelonLeft;
			if (const FFormationShape* Opposite = Lookup(FormationNames::ToString(Other)))
			{
				if (bOutMirrored)
				{
					*bOutMirrored = true;
				}
				return Mirror(*Opposite);
			}
		}
		return TOptional<FFormationShape>();
	}

	TArray<FFormationSlot> Slots(const FFormationShape& Shape, const FVector2D& LeaderPos, float LeaderYaw, TArray<float>* OutArcs)
	{
		TArray<FFormationSlot> Result;
		if (OutArcs)
		{
			OutArcs->Reset();
		}
		for (const FFormationShapeMember& Member : Shape.Members)
		{
			FFormationSlot& Slot = Result.AddDefaulted_GetRef();
			Slot.Role = Member.Role;
			Slot.Position = FormationShapesLocal::ShapeLocalToWorld(LeaderPos, LeaderYaw, Member.Fwd, Member.Right);
			Slot.Yaw = static_cast<float>(FormationMath::NormYaw(static_cast<double>(LeaderYaw) + Member.Look));
			if (OutArcs)
			{
				OutArcs->Add(static_cast<float>(Member.Arc.Get(DefaultArcDeg)));
			}
		}
		return Result;
	}

	FString GetDefaultPath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/FormationLab/formation_shapes.json");
	}

	FString GetPath()
	{
		const FString& Override = FormationShapesLocal::Runtime().PathOverride;
		return Override.IsEmpty() ? GetDefaultPath() : Override;
	}

	void SetPath(const FString& Path)
	{
		FormationShapesLocal::FRuntime& State = FormationShapesLocal::Runtime();
		State.PathOverride = Path;
		State.bRead = false;
		State.Loaded = FFormationShapesFile();
		++State.Revision;
	}

	bool IsEnabled()
	{
		return FormationShapesLocal::CVarFormationSavedShapes.GetValueOnGameThread() != 0;
	}

	bool Reload()
	{
		FormationShapesLocal::FRuntime& State = FormationShapesLocal::Runtime();
		State.NextCheck = FPlatformTime::Seconds() + LiveReloadSeconds;
		return FormationShapesLocal::Load(State, GetPath());
	}

	const FFormationShapesFile& Get()
	{
		using namespace FormationShapesLocal;
		FRuntime& State = Runtime();
		if (State.bOverride)
		{
			return State.Override;
		}
		if (!IsEnabled())
		{
			return EmptyFile();
		}
		if (!State.bRead)
		{
			Reload();
		}
#if !UE_BUILD_SHIPPING
		else
		{
			// Live reload (dev builds): a Training Ground save shows up without a restart.
			const double Now = FPlatformTime::Seconds();
			if (Now >= State.NextCheck)
			{
				State.NextCheck = Now + LiveReloadSeconds;
				const FString Path = GetPath();
				IFileManager& Files = IFileManager::Get();
				if (Files.GetTimeStamp(*Path) != State.Stamp || Files.FileSize(*Path) != State.Size)
				{
					Load(State, Path);
				}
			}
		}
#endif
		return State.Loaded;
	}

	uint32 GetRevision()
	{
		return FormationShapesLocal::Runtime().Revision;
	}

	void SetOverride(const FFormationShapesFile& File)
	{
		FormationShapesLocal::FRuntime& State = FormationShapesLocal::Runtime();
		State.Override = File;
		State.bOverride = true;
		++State.Revision;
	}

	void ClearOverride()
	{
		FormationShapesLocal::FRuntime& State = FormationShapesLocal::Runtime();
		State.Override = FFormationShapesFile();
		State.bOverride = false;
		++State.Revision;
	}
}
