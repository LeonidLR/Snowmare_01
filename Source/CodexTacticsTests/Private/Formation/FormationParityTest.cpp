// CodexTactics.Formation.Parity.<Rule> - the C++ formation rules against the golden vectors the Training Ground simulator exports
// (`npm run golden`; v1.1 adds PassageWidth, v1.2 HedgehogSlots, v2 Classify state in / out, contract 9 Menu): FPaths::ProjectDir()/Tools/TrainingGround/golden/formation_<Rule>.json, schema codex.formation_golden/1
// (docs/design/FORMATION_CONTRACT.md 3, field names in 3.1). Same inputs -> same outputs within the file's tolerance (cm / deg; yaws
// compared as angles). The rules' config is built from the file's "config_values" (the snapshot the vectors were computed with), so
// later tuning of formation_config.json cannot break parity. A missing file: a warning, the test passes. Every case is checked (none is
// skipped): a case that cannot be read is an error. UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Formation/FormationMenuRules.h"
#include "Formation/FormationSectorRules.h"
#include "Formation/FormationShapeRules.h"
#include "Formation/FormationTerrainRules.h"
#include "Formation/FormationTuning.h"
#include "Formation/FormationWallRules.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_PARITY_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.Parity." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationParityUtil
{
	/** At most this many mismatches are reported one by one (the summary counts all). */
	constexpr int32 MaxReportedMismatches = 12;

	/** A loaded golden file. */
	struct FGolden
	{
		TArray<TSharedPtr<FJsonValue>> Cases;
		FFormationConfig Config;
		double TolCm = 0.5;
		double TolDeg = 0.5;
	};

	/** Loads formation_<Rule>.json; false with a warning when missing (the test passes), false with an error when broken. */
	bool LoadGolden(FAutomationTestBase& Test, const TCHAR* Rule, FGolden& Out, bool& bOutMissing, bool bUsesConfig = true)
	{
		bOutMissing = false;
		const FString Path = FPaths::ProjectDir() / TEXT("Tools/TrainingGround/golden") / FString::Printf(TEXT("formation_%s.json"), Rule);
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			bOutMissing = true;
			Test.AddWarning(FString::Printf(TEXT("Golden vectors %s not found yet (the Training Ground simulator exports them): parity not checked"), *Path));
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			Test.AddError(FString::Printf(TEXT("%s is not JSON"), *Path));
			return false;
		}
		FString Schema;
		FString FileRule;
		if (!Root->TryGetStringField(TEXT("schema"), Schema) || Schema != TEXT("codex.formation_golden/1") || !Root->TryGetStringField(TEXT("rule"), FileRule)
			|| FileRule != Rule)
		{
			Test.AddError(FString::Printf(TEXT("%s: schema must be codex.formation_golden/1 and rule %s (got '%s' / '%s')"), *Path, Rule, *Schema, *FileRule));
			return false;
		}
		const TSharedPtr<FJsonObject>* Tolerance = nullptr;
		if (Root->TryGetObjectField(TEXT("tolerance"), Tolerance))
		{
			(*Tolerance)->TryGetNumberField(TEXT("cm"), Out.TolCm);
			(*Tolerance)->TryGetNumberField(TEXT("deg"), Out.TolDeg);
		}
		const TSharedPtr<FJsonObject>* Values = nullptr;
		if (Root->TryGetObjectField(TEXT("config_values"), Values))
		{
			TArray<FString> Unknown;
			Out.Config = FormationTuning::FromValues(FormationTuning::ReadFlatValues(**Values), FFormationConfig(), nullptr, &Unknown);
			for (const FString& Key : Unknown)
			{
				Test.AddWarning(FString::Printf(TEXT("%s: config_values key %s is unknown to the C++ config"), *Path, *Key));
			}
		}
		else if (bUsesConfig)
		{
			Test.AddWarning(FString::Printf(TEXT("%s has no config_values: the C++ defaults are used"), *Path));
		}
		const TArray<TSharedPtr<FJsonValue>>* Cases = nullptr;
		if (!Root->TryGetArrayField(TEXT("cases"), Cases) || Cases->Num() == 0)
		{
			Test.AddError(FString::Printf(TEXT("%s has no cases"), *Path));
			return false;
		}
		Out.Cases = *Cases;
		return true;
	}

	/** The field under its contract (camelCase) name or its snake_case alias. */
	TSharedPtr<FJsonValue> Field(const FJsonObject& Object, const TCHAR* Name, const TCHAR* Alias = nullptr)
	{
		TSharedPtr<FJsonValue> Value = Object.TryGetField(Name);
		if (!Value.IsValid() && Alias)
		{
			Value = Object.TryGetField(Alias);
		}
		return Value;
	}

	bool Number(const FJsonObject& Object, const TCHAR* Name, const TCHAR* Alias, double& Out)
	{
		const TSharedPtr<FJsonValue> Value = Field(Object, Name, Alias);
		return Value.IsValid() && Value->TryGetNumber(Out);
	}

	bool Text(const FJsonObject& Object, const TCHAR* Name, const TCHAR* Alias, FString& Out)
	{
		const TSharedPtr<FJsonValue> Value = Field(Object, Name, Alias);
		return Value.IsValid() && Value->TryGetString(Out);
	}

	bool Vec2(const TSharedPtr<FJsonValue>& Value, FVector2D& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		double X = 0.0;
		double Y = 0.0;
		if (!Value.IsValid() || !Value->TryGetArray(Array) || Array->Num() != 2 || !(*Array)[0]->TryGetNumber(X) || !(*Array)[1]->TryGetNumber(Y))
		{
			return false;
		}
		Out = FVector2D(X, Y);
		return true;
	}

	bool ReadWall(const TSharedPtr<FJsonValue>& Value, FFormationWall& Out)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Object) || !Vec2((*Object)->TryGetField(TEXT("a")), Out.A) || !Vec2((*Object)->TryGetField(TEXT("b")), Out.B))
		{
			return false;
		}
		double Thickness = 0.0;
		Out.Thickness = (*Object)->TryGetNumberField(TEXT("thickness"), Thickness) ? static_cast<float>(Thickness) : 0.f;
		return true;
	}

	bool ReadWalls(const TArray<TSharedPtr<FJsonValue>>& Values, TArray<FFormationWall>& Out)
	{
		Out.Reset();
		for (const TSharedPtr<FJsonValue>& Value : Values)
		{
			FFormationWall Wall;
			if (!ReadWall(Value, Wall))
			{
				return false;
			}
			Out.Add(Wall);
		}
		return true;
	}

	/** A number or null / missing (unset); false when it is something else. */
	bool OptionalNumber(const FJsonObject& Object, const TCHAR* Name, const TCHAR* Alias, TOptional<float>& Out)
	{
		Out.Reset();
		const TSharedPtr<FJsonValue> Value = Field(Object, Name, Alias);
		if (!Value.IsValid() || Value->IsNull())
		{
			return true;
		}
		double Parsed = 0.0;
		if (!Value->TryGetNumber(Parsed))
		{
			return false;
		}
		Out = static_cast<float>(Parsed);
		return true;
	}

	/** A terrain class string or null / missing (unset); false when it is something else. */
	bool OptionalClass(const FJsonObject& Object, const TCHAR* Name, TOptional<EFormationTerrainClass>& Out)
	{
		Out.Reset();
		const TSharedPtr<FJsonValue> Value = Object.TryGetField(Name);
		if (!Value.IsValid() || Value->IsNull())
		{
			return true;
		}
		FString ClassName;
		EFormationTerrainClass Parsed = EFormationTerrainClass::Open;
		if (!Value->TryGetString(ClassName) || !FormationNames::Parse(ClassName, Parsed))
		{
			return false;
		}
		Out = Parsed;
		return true;
	}

	/** v2 classifier state {current, candidate, candidateDistance, passageWidth} (missing optional fields = unset / 0). */
	bool ReadClassifyState(const FJsonObject& Object, FFormationClassifyState& Out)
	{
		Out = FFormationClassifyState();
		double Distance = 0.0;
		if (Object.HasField(TEXT("candidateDistance")) && !Object.TryGetNumberField(TEXT("candidateDistance"), Distance))
		{
			return false;
		}
		Out.CandidateDistance = static_cast<float>(Distance);
		return OptionalClass(Object, TEXT("current"), Out.Current) && OptionalClass(Object, TEXT("candidate"), Out.Candidate)
			&& OptionalNumber(Object, TEXT("passageWidth"), TEXT("passage_width"), Out.PassageWidth);
	}

	FString ShowClass(const TOptional<EFormationTerrainClass>& Class)
	{
		return Class.IsSet() ? FString(FormationNames::ToString(Class.GetValue())) : FString(TEXT("null"));
	}

	FString ShowNumber(const TOptional<float>& Value)
	{
		return Value.IsSet() ? FString::Printf(TEXT("%.3f"), Value.GetValue()) : FString(TEXT("null"));
	}

	/** Compares two classifier states (classes exactly, distances / widths within cm); empty = equal. */
	FString CompareClassifyState(const FFormationClassifyState& Actual, const FFormationClassifyState& Expected, const FGolden& Golden)
	{
		const bool bWidthSame = Actual.PassageWidth.IsSet() == Expected.PassageWidth.IsSet()
			&& (!Actual.PassageWidth.IsSet() || FMath::Abs(Actual.PassageWidth.GetValue() - Expected.PassageWidth.GetValue()) <= Golden.TolCm);
		if (Actual.Current == Expected.Current && Actual.Candidate == Expected.Candidate
			&& FMath::Abs(Actual.CandidateDistance - Expected.CandidateDistance) <= Golden.TolCm && bWidthSame)
		{
			return FString();
		}
		return FString::Printf(TEXT("state {%s, %s, %.3f, %s}, expected {%s, %s, %.3f, %s}"), *ShowClass(Actual.Current), *ShowClass(Actual.Candidate),
			Actual.CandidateDistance, *ShowNumber(Actual.PassageWidth), *ShowClass(Expected.Current), *ShowClass(Expected.Candidate),
			Expected.CandidateDistance, *ShowNumber(Expected.PassageWidth));
	}

	bool InOut(const TSharedPtr<FJsonValue>& Case, TSharedPtr<FJsonObject>& OutIn, TSharedPtr<FJsonObject>& OutOut)
	{
		const TSharedPtr<FJsonObject>* CaseObject = nullptr;
		const TSharedPtr<FJsonObject>* In = nullptr;
		const TSharedPtr<FJsonObject>* Out = nullptr;
		if (!Case.IsValid() || !Case->TryGetObject(CaseObject) || !(*CaseObject)->TryGetObjectField(TEXT("in"), In) || !(*CaseObject)->TryGetObjectField(TEXT("out"), Out))
		{
			return false;
		}
		OutIn = *In;
		OutOut = *Out;
		return true;
	}

	FString CaseText(const TSharedPtr<FJsonObject>& In)
	{
		FString Json;
		FJsonSerializer::Serialize(In.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
		return Json;
	}

	double AngleDiff(double A, double B)
	{
		return FMath::Abs(FRotator::NormalizeAxis(static_cast<float>(A - B)));
	}

	/** Compares slot lists (same roles in the same order, positions within cm, optionally yaws within deg); empty = equal. */
	FString CompareSlots(const TArray<FFormationSlot>& Actual, const TArray<TSharedPtr<FJsonValue>>& Expected, const FGolden& Golden, bool bYaw)
	{
		if (Actual.Num() != Expected.Num())
		{
			return FString::Printf(TEXT("%d slots, expected %d"), Actual.Num(), Expected.Num());
		}
		for (int32 Index = 0; Index < Actual.Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* Slot = nullptr;
			FString Role;
			FVector2D Pos;
			if (!Expected[Index]->TryGetObject(Slot) || !(*Slot)->TryGetStringField(TEXT("role"), Role) || !Vec2((*Slot)->TryGetField(TEXT("pos")), Pos))
			{
				return FString::Printf(TEXT("expected slot %d unreadable"), Index);
			}
			const FString ActualRole = FormationNames::ToString(Actual[Index].Role);
			if (ActualRole != Role)
			{
				return FString::Printf(TEXT("slot %d role %s, expected %s"), Index, *ActualRole, *Role);
			}
			const double Distance = FVector2D::Distance(Actual[Index].Position, Pos);
			if (Distance > Golden.TolCm)
			{
				return FString::Printf(TEXT("%s at (%.3f, %.3f), expected (%.3f, %.3f) (%.3f cm off)"), *Role, Actual[Index].Position.X, Actual[Index].Position.Y,
					Pos.X, Pos.Y, Distance);
			}
			double Yaw = 0.0;
			if (bYaw)
			{
				if (!(*Slot)->TryGetNumberField(TEXT("yaw"), Yaw))
				{
					return FString::Printf(TEXT("expected slot %d has no yaw"), Index);
				}
				if (AngleDiff(Actual[Index].Yaw, Yaw) > Golden.TolDeg)
				{
					return FString::Printf(TEXT("%s yaw %.3f, expected %.3f"), *Role, Actual[Index].Yaw, Yaw);
				}
			}
		}
		return FString();
	}

	/** Runs every case through Check (returns "" = pass, else the mismatch); reports and returns the test result. */
	bool RunCases(FAutomationTestBase& Test, const TCHAR* Rule, const FGolden& Golden,
		TFunctionRef<FString(const TSharedPtr<FJsonObject>& In, const TSharedPtr<FJsonObject>& Out)> Check)
	{
		int32 Mismatches = 0;
		for (int32 Index = 0; Index < Golden.Cases.Num(); ++Index)
		{
			TSharedPtr<FJsonObject> In;
			TSharedPtr<FJsonObject> Out;
			const FString Problem = InOut(Golden.Cases[Index], In, Out) ? Check(In, Out) : FString(TEXT("case unreadable (no in / out)"));
			if (!Problem.IsEmpty())
			{
				if (++Mismatches <= MaxReportedMismatches)
				{
					Test.AddError(FString::Printf(TEXT("%s case %d: %s; in %s"), Rule, Index, *Problem, In.IsValid() ? *CaseText(In) : TEXT("?")));
				}
			}
		}
		Test.AddInfo(FString::Printf(TEXT("%s parity: %d cases, %d mismatched, 0 skipped (tolerance %.2f cm / %.2f deg)"), Rule, Golden.Cases.Num(), Mismatches,
			Golden.TolCm, Golden.TolDeg));
		return Mismatches == 0;
	}
}

FORMATION_PARITY_TEST(FFormationParitySlotsTest, "Slots")
bool FFormationParitySlotsTest::RunTest(const FString&)
{
	using namespace FormationParityUtil;
	FGolden Golden;
	bool bMissing = false;
	if (!LoadGolden(*this, TEXT("Slots"), Golden, bMissing))
	{
		return bMissing;
	}
	return RunCases(*this, TEXT("Slots"), Golden, [&Golden](const TSharedPtr<FJsonObject>& In, const TSharedPtr<FJsonObject>& Out) -> FString
	{
		FString FormationName;
		EFormationKind Formation = EFormationKind::Diamond;
		double Count = 0.0;
		double Yaw = 0.0;
		FVector2D Pos;
		const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
		if (!Text(*In, TEXT("formation"), nullptr, FormationName) || !FormationNames::Parse(FormationName, Formation)
			|| !Number(*In, TEXT("memberCount"), TEXT("member_count"), Count) || !Number(*In, TEXT("leaderYaw"), TEXT("leader_yaw"), Yaw)
			|| !Vec2(Field(*In, TEXT("leaderPos"), TEXT("leader_pos")), Pos) || !Out->TryGetArrayField(TEXT("slots"), Slots))
		{
			return TEXT("unreadable input / output");
		}
		const TArray<FFormationSlot> Actual = FormationShapeRules::Slots(Formation, static_cast<int32>(Count), Pos, static_cast<float>(Yaw), Golden.Config);
		return CompareSlots(Actual, *Slots, Golden, false);
	});
}

FORMATION_PARITY_TEST(FFormationParityLookYawTest, "LookYaw")
bool FFormationParityLookYawTest::RunTest(const FString&)
{
	using namespace FormationParityUtil;
	FGolden Golden;
	bool bMissing = false;
	if (!LoadGolden(*this, TEXT("LookYaw"), Golden, bMissing))
	{
		return bMissing;
	}
	return RunCases(*this, TEXT("LookYaw"), Golden, [&Golden](const TSharedPtr<FJsonObject>& In, const TSharedPtr<FJsonObject>& Out) -> FString
	{
		FString RoleName;
		FString FormationName;
		FString ModeName;
		EFormationRole Role = EFormationRole::Commander;
		EFormationKind Formation = EFormationKind::Diamond;
		EFormationMode Mode = EFormationMode::Walk;
		double Count = 0.0;
		double Time = 0.0;
		double LeaderYaw = 0.0;
		double Expected = 0.0;
		if (!Text(*In, TEXT("role"), nullptr, RoleName) || !FormationNames::Parse(RoleName, Role) || !Text(*In, TEXT("formation"), nullptr, FormationName)
			|| !FormationNames::Parse(FormationName, Formation) || !Text(*In, TEXT("mode"), nullptr, ModeName) || !FormationNames::Parse(ModeName, Mode)
			|| !Number(*In, TEXT("memberCount"), TEXT("member_count"), Count) || !Number(*In, TEXT("timeInPatrol"), TEXT("time_in_patrol"), Time)
			|| !Number(*In, TEXT("leaderYaw"), TEXT("leader_yaw"), LeaderYaw) || !Out->TryGetNumberField(TEXT("yaw"), Expected))
		{
			return TEXT("unreadable input / output");
		}
		TOptional<float> Width;
		if (!OptionalNumber(*In, TEXT("passageWidth"), TEXT("passage_width"), Width))
		{
			return TEXT("passageWidth is neither a number nor null");
		}
		const float Actual = FormationSectorRules::LookYaw(Role, Formation, Mode, static_cast<int32>(Count), static_cast<float>(Time), static_cast<float>(LeaderYaw),
			Width, Golden.Config);
		return AngleDiff(Actual, Expected) <= Golden.TolDeg ? FString() : FString::Printf(TEXT("yaw %.3f, expected %.3f"), Actual, Expected);
	});
}

FORMATION_PARITY_TEST(FFormationParityClassifyTest, "Classify")
bool FFormationParityClassifyTest::RunTest(const FString&)
{
	using namespace FormationParityUtil;
	FGolden Golden;
	bool bMissing = false;
	if (!LoadGolden(*this, TEXT("Classify"), Golden, bMissing))
	{
		return bMissing;
	}
	return RunCases(*this, TEXT("Classify"), Golden, [&Golden](const TSharedPtr<FJsonObject>& In, const TSharedPtr<FJsonObject>& Out) -> FString
	{
		const TArray<TSharedPtr<FJsonValue>>* WallValues = nullptr;
		const TSharedPtr<FJsonObject>* InState = nullptr;
		const TSharedPtr<FJsonObject>* OutState = nullptr;
		FVector2D Pos;
		double Yaw = 0.0;
		double StepDistance = 0.0;
		TArray<FFormationWall> Walls;
		FFormationClassifyState State;
		FFormationClassifyState Expected;
		if (!In->TryGetArrayField(TEXT("walls"), WallValues) || !ReadWalls(*WallValues, Walls) || !Vec2(Field(*In, TEXT("leaderPos"), TEXT("leader_pos")), Pos)
			|| !Number(*In, TEXT("leaderYaw"), TEXT("leader_yaw"), Yaw) || !Number(*In, TEXT("stepDistance"), TEXT("step_distance"), StepDistance)
			|| !In->TryGetObjectField(TEXT("state"), InState) || !ReadClassifyState(**InState, State)
			|| !Out->TryGetObjectField(TEXT("state"), OutState) || !ReadClassifyState(**OutState, Expected))
		{
			return TEXT("unreadable input / output");
		}
		// v1.2: the leader's move destination (null / missing = none).
		TOptional<FVector2D> Destination;
		const TSharedPtr<FJsonValue> DestinationValue = Field(*In, TEXT("destination"));
		if (DestinationValue.IsValid() && !DestinationValue->IsNull())
		{
			FVector2D Parsed;
			if (!Vec2(DestinationValue, Parsed))
			{
				return TEXT("destination is neither [x, y] nor null");
			}
			Destination = Parsed;
		}
		const FFormationClassifyState Actual = FormationTerrainRules::Classify(Walls, Pos, static_cast<float>(Yaw), Destination, State,
			static_cast<float>(StepDistance), Golden.Config);
		const FString Problem = CompareClassifyState(Actual, Expected, Golden);
		return Problem.IsEmpty() ? Problem : FString::Printf(TEXT("%s (raw %s)"), *Problem,
			FormationNames::ToString(FormationTerrainRules::ClassifyRaw(Walls, Pos, static_cast<float>(Yaw), Golden.Config, Destination)));
	});
}

FORMATION_PARITY_TEST(FFormationParityPassageWidthTest, "PassageWidth")
bool FFormationParityPassageWidthTest::RunTest(const FString&)
{
	using namespace FormationParityUtil;
	FGolden Golden;
	bool bMissing = false;
	if (!LoadGolden(*this, TEXT("PassageWidth"), Golden, bMissing))
	{
		return bMissing;
	}
	return RunCases(*this, TEXT("PassageWidth"), Golden, [&Golden](const TSharedPtr<FJsonObject>& In, const TSharedPtr<FJsonObject>& Out) -> FString
	{
		const TArray<TSharedPtr<FJsonValue>>* WallValues = nullptr;
		FVector2D Pos;
		double Yaw = 0.0;
		TOptional<float> Expected;
		TArray<FFormationWall> Walls;
		if (!In->TryGetArrayField(TEXT("walls"), WallValues) || !ReadWalls(*WallValues, Walls) || !Vec2(Field(*In, TEXT("leaderPos"), TEXT("leader_pos")), Pos)
			|| !Number(*In, TEXT("leaderYaw"), TEXT("leader_yaw"), Yaw) || !Out->HasField(TEXT("width")) || !OptionalNumber(*Out, TEXT("width"), nullptr, Expected))
		{
			return TEXT("unreadable input / output");
		}
		const TOptional<float> Actual = FormationTerrainRules::PassageWidth(Walls, Pos, static_cast<float>(Yaw), Golden.Config);
		const auto Show = [](const TOptional<float>& Value) { return Value.IsSet() ? FString::Printf(TEXT("%.3f"), Value.GetValue()) : FString(TEXT("null")); };
		const bool bSame = Actual.IsSet() == Expected.IsSet() && (!Actual.IsSet() || FMath::Abs(Actual.GetValue() - Expected.GetValue()) <= Golden.TolCm);
		return bSame ? FString() : FString::Printf(TEXT("width %s, expected %s"), *Show(Actual), *Show(Expected));
	});
}

FORMATION_PARITY_TEST(FFormationParityHedgehogSlotsTest, "HedgehogSlots")
bool FFormationParityHedgehogSlotsTest::RunTest(const FString&)
{
	using namespace FormationParityUtil;
	FGolden Golden;
	bool bMissing = false;
	if (!LoadGolden(*this, TEXT("HedgehogSlots"), Golden, bMissing))
	{
		return bMissing;
	}
	return RunCases(*this, TEXT("HedgehogSlots"), Golden, [&Golden](const TSharedPtr<FJsonObject>& In, const TSharedPtr<FJsonObject>& Out) -> FString
	{
		FVector2D Center;
		double Count = 0.0;
		double Yaw = 0.0;
		const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
		if (!Vec2(Field(*In, TEXT("center")), Center) || !Number(*In, TEXT("memberCount"), TEXT("member_count"), Count) || !Number(*In, TEXT("yaw"), nullptr, Yaw)
			|| !Out->TryGetArrayField(TEXT("slots"), Slots))
		{
			return TEXT("unreadable input / output");
		}
		return CompareSlots(FormationShapeRules::HedgehogSlots(Center, static_cast<int32>(Count), static_cast<float>(Yaw), Golden.Config), *Slots, Golden, true);
	});
}

FORMATION_PARITY_TEST(FFormationParityFormationForTest, "FormationFor")
bool FFormationParityFormationForTest::RunTest(const FString&)
{
	using namespace FormationParityUtil;
	FGolden Golden;
	bool bMissing = false;
	if (!LoadGolden(*this, TEXT("FormationFor"), Golden, bMissing))
	{
		return bMissing;
	}
	return RunCases(*this, TEXT("FormationFor"), Golden, [](const TSharedPtr<FJsonObject>& In, const TSharedPtr<FJsonObject>& Out) -> FString
	{
		FString ClassName;
		FString ExpectedName;
		EFormationTerrainClass Class = EFormationTerrainClass::Open;
		double Count = 0.0;
		if (!Text(*In, TEXT("class"), nullptr, ClassName) || !FormationNames::Parse(ClassName, Class) || !Number(*In, TEXT("memberCount"), TEXT("member_count"), Count)
			|| !Out->TryGetStringField(TEXT("formation"), ExpectedName))
		{
			return TEXT("unreadable input / output");
		}
		// v1.2: the weather flag (missing = false).
		bool bBlizzard = false;
		const TSharedPtr<FJsonValue> BlizzardValue = Field(*In, TEXT("blizzard"));
		if (BlizzardValue.IsValid() && !BlizzardValue->TryGetBool(bBlizzard))
		{
			return TEXT("blizzard is not a bool");
		}
		const FString Actual = FormationNames::ToString(FormationTerrainRules::FormationFor(Class, static_cast<int32>(Count), bBlizzard));
		return Actual == ExpectedName ? FString() : FString::Printf(TEXT("formation %s, expected %s"), *Actual, *ExpectedName);
	});
}

FORMATION_PARITY_TEST(FFormationParityWallLayoutTest, "WallLayout")
bool FFormationParityWallLayoutTest::RunTest(const FString&)
{
	using namespace FormationParityUtil;
	FGolden Golden;
	bool bMissing = false;
	if (!LoadGolden(*this, TEXT("WallLayout"), Golden, bMissing))
	{
		return bMissing;
	}
	return RunCases(*this, TEXT("WallLayout"), Golden, [&Golden](const TSharedPtr<FJsonObject>& In, const TSharedPtr<FJsonObject>& Out) -> FString
	{
		FFormationWall Wall;
		FVector2D Clicked;
		double Count = 0.0;
		double RoomLeft = 0.0;
		double RoomRight = 0.0;
		const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
		if (!FormationParityUtil::ReadWall(In->TryGetField(TEXT("wall")), Wall) || !Vec2(Field(*In, TEXT("clickedSlot"), TEXT("clicked_slot")), Clicked)
			|| !Number(*In, TEXT("memberCount"), TEXT("member_count"), Count) || !Number(*In, TEXT("roomLeft"), TEXT("room_left"), RoomLeft)
			|| !Number(*In, TEXT("roomRight"), TEXT("room_right"), RoomRight) || !Out->TryGetArrayField(TEXT("slots"), Slots))
		{
			return TEXT("unreadable input / output");
		}
		const TArray<FFormationSlot> Actual = FormationWallRules::Layout(Wall, Clicked, static_cast<int32>(Count), static_cast<float>(RoomLeft),
			static_cast<float>(RoomRight), Golden.Config);
		return CompareSlots(Actual, *Slots, Golden, true);
	});
}

namespace FormationParityUtil
{
	/** A menu item id or null / missing (unset = Auto); false when it is something else. */
	bool OptionalMenuItem(const FJsonObject& Object, const TCHAR* Name, TOptional<EFormationMenuItem>& Out)
	{
		Out.Reset();
		const TSharedPtr<FJsonValue> Value = Object.TryGetField(Name);
		if (!Value.IsValid() || Value->IsNull())
		{
			return true;
		}
		FString Id;
		EFormationMenuItem Item = EFormationMenuItem::Line;
		if (!Value->TryGetString(Id) || !FormationMenuRules::Parse(Id, Item))
		{
			return false;
		}
		Out = Item;
		return true;
	}

	/** Menu state { open, choice ("auto" | item), highlighted (item | null) }. */
	bool ReadMenuState(const FJsonObject& Object, FFormationMenuState& Out)
	{
		FString Choice;
		return Object.TryGetBoolField(TEXT("open"), Out.bOpen) && Object.TryGetStringField(TEXT("choice"), Choice)
			&& FormationMenuRules::ParseChoice(Choice, Out.Choice) && OptionalMenuItem(Object, TEXT("highlighted"), Out.Highlighted);
	}

	FString ShowMenuItem(const TOptional<EFormationMenuItem>& Item, const TCHAR* Unset)
	{
		return Item.IsSet() ? FString(FormationMenuRules::ToString(Item.GetValue())) : FString(Unset);
	}

	const TCHAR* MenuEffectName(EFormationMenuEffect Effect)
	{
		switch (Effect)
		{
		case EFormationMenuEffect::Opened: return TEXT("opened");
		case EFormationMenuEffect::Picked: return TEXT("picked");
		case EFormationMenuEffect::Auto: return TEXT("auto");
		case EFormationMenuEffect::Cancelled: return TEXT("cancelled");
		case EFormationMenuEffect::Disabled: return TEXT("disabled");
		default: return TEXT("none");
		}
	}
}

FORMATION_PARITY_TEST(FFormationParityMenuTest, "Menu")
bool FFormationParityMenuTest::RunTest(const FString&)
{
	using namespace FormationParityUtil;
	FGolden Golden;
	bool bMissing = false;
	if (!LoadGolden(*this, TEXT("Menu"), Golden, bMissing, false))
	{
		return bMissing;
	}
	return RunCases(*this, TEXT("Menu"), Golden, [](const TSharedPtr<FJsonObject>& In, const TSharedPtr<FJsonObject>& Out) -> FString
	{
		FString Op;
		if (!In->TryGetStringField(TEXT("op"), Op))
		{
			return TEXT("no op");
		}
		if (Op == TEXT("itemAt"))
		{
			double Dx = 0.0;
			double Dy = 0.0;
			double DeadZone = 0.0;
			TOptional<EFormationMenuItem> Expected;
			if (!Number(*In, TEXT("dx"), nullptr, Dx) || !Number(*In, TEXT("dy"), nullptr, Dy) || !Number(*In, TEXT("deadZonePx"), TEXT("dead_zone_px"), DeadZone)
				|| !Out->HasField(TEXT("item")) || !OptionalMenuItem(*Out, TEXT("item"), Expected))
			{
				return TEXT("unreadable itemAt case");
			}
			const TOptional<EFormationMenuItem> Actual = FormationMenuRules::ItemAt(static_cast<float>(Dx), static_cast<float>(Dy), static_cast<float>(DeadZone));
			return Actual == Expected ? FString() : FString::Printf(TEXT("item %s, expected %s"), *ShowMenuItem(Actual, TEXT("null")), *ShowMenuItem(Expected, TEXT("null")));
		}
		if (Op == TEXT("release"))
		{
			const TSharedPtr<FJsonObject>* InState = nullptr;
			const TSharedPtr<FJsonObject>* OutState = nullptr;
			double Count = 0.0;
			FString ExpectedEffect;
			FFormationMenuState State;
			FFormationMenuState Expected;
			if (!In->TryGetObjectField(TEXT("state"), InState) || !ReadMenuState(**InState, State) || !Number(*In, TEXT("memberCount"), TEXT("member_count"), Count)
				|| !Out->TryGetObjectField(TEXT("state"), OutState) || !ReadMenuState(**OutState, Expected) || !Out->TryGetStringField(TEXT("effect"), ExpectedEffect))
			{
				return TEXT("unreadable release case");
			}
			const EFormationMenuEffect Effect = FormationMenuRules::Release(State, static_cast<int32>(Count));
			if (State.bOpen != Expected.bOpen || State.Choice != Expected.Choice || State.Highlighted != Expected.Highlighted || ExpectedEffect != MenuEffectName(Effect))
			{
				return FString::Printf(TEXT("state {%s, %s, %s} %s, expected {%s, %s, %s} %s"), State.bOpen ? TEXT("open") : TEXT("closed"),
					*ShowMenuItem(State.Choice, TEXT("auto")), *ShowMenuItem(State.Highlighted, TEXT("null")), MenuEffectName(Effect),
					Expected.bOpen ? TEXT("open") : TEXT("closed"), *ShowMenuItem(Expected.Choice, TEXT("auto")), *ShowMenuItem(Expected.Highlighted, TEXT("null")),
					*ExpectedEffect);
			}
			return FString();
		}
		if (Op == TEXT("resolve"))
		{
			FString ChoiceId;
			TOptional<EFormationMenuItem> Choice;
			double Count = 0.0;
			bool bExpectedForward = false;
			if (!In->TryGetStringField(TEXT("choice"), ChoiceId) || !FormationMenuRules::ParseChoice(ChoiceId, Choice)
				|| !Number(*In, TEXT("memberCount"), TEXT("member_count"), Count) || !Out->TryGetBoolField(TEXT("allForward"), bExpectedForward)
				|| !Out->HasField(TEXT("manualFormation")))
			{
				return TEXT("unreadable resolve case");
			}
			TOptional<EFormationKind> ExpectedFormation;
			FString FormationName;
			const TSharedPtr<FJsonValue> FormationValue = Out->TryGetField(TEXT("manualFormation"));
			if (!FormationValue->IsNull())
			{
				EFormationKind Parsed = EFormationKind::Diamond;
				if (!FormationValue->TryGetString(FormationName) || !FormationNames::Parse(FormationName, Parsed))
				{
					return TEXT("unreadable manualFormation");
				}
				ExpectedFormation = Parsed;
			}
			const FFormationMenuResolved Actual = FormationMenuRules::Resolve(Choice, static_cast<int32>(Count));
			if (Actual.ManualFormation != ExpectedFormation || Actual.bAllForward != bExpectedForward)
			{
				return FString::Printf(TEXT("{%s, all forward %d}, expected {%s, %d}"),
					Actual.ManualFormation.IsSet() ? FormationNames::ToString(Actual.ManualFormation.GetValue()) : TEXT("null"), Actual.bAllForward ? 1 : 0,
					ExpectedFormation.IsSet() ? FormationNames::ToString(ExpectedFormation.GetValue()) : TEXT("null"), bExpectedForward ? 1 : 0);
			}
			return FString();
		}
		return FString::Printf(TEXT("unknown op %s"), *Op);
	});
}

#undef FORMATION_PARITY_TEST

#endif
