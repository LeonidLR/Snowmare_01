// CodexTactics.Formation.Parity.<Rule> - the C++ formation rules against the golden vectors the Training Ground simulator exports
// (`npm run golden`): FPaths::ProjectDir()/Tools/TrainingGround/golden/formation_<Rule>.json, schema codex.formation_golden/1
// (docs/design/FORMATION_CONTRACT.md 3, field names in 3.1). Same inputs -> same outputs within the file's tolerance (cm / deg; yaws
// compared as angles). The rules' config is built from the file's "config_values" (the snapshot the vectors were computed with), so
// later tuning of formation_config.json cannot break parity. A missing file: a warning, the test passes. Every case is checked (none is
// skipped): a case that cannot be read is an error. UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
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
	bool LoadGolden(FAutomationTestBase& Test, const TCHAR* Rule, FGolden& Out, bool& bOutMissing)
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
		else
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
		const float Actual = FormationSectorRules::LookYaw(Role, Formation, Mode, static_cast<int32>(Count), static_cast<float>(Time), static_cast<float>(LeaderYaw),
			Golden.Config);
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
		FVector2D Pos;
		double Yaw = 0.0;
		double TimeInClass = 0.0;
		FString ExpectedName;
		EFormationTerrainClass Expected = EFormationTerrainClass::Open;
		if (!In->TryGetArrayField(TEXT("walls"), WallValues) || !Vec2(Field(*In, TEXT("leaderPos"), TEXT("leader_pos")), Pos)
			|| !Number(*In, TEXT("leaderYaw"), TEXT("leader_yaw"), Yaw) || !Number(*In, TEXT("timeInClass"), TEXT("time_in_class"), TimeInClass)
			|| !Out->TryGetStringField(TEXT("class"), ExpectedName) || !FormationNames::Parse(ExpectedName, Expected))
		{
			return TEXT("unreadable input / output");
		}
		TArray<FFormationWall> Walls;
		for (const TSharedPtr<FJsonValue>& Value : *WallValues)
		{
			FFormationWall Wall;
			if (!FormationParityUtil::ReadWall(Value, Wall))
			{
				return TEXT("unreadable wall");
			}
			Walls.Add(Wall);
		}
		TOptional<EFormationTerrainClass> Previous;
		const TSharedPtr<FJsonValue> PreviousValue = Field(*In, TEXT("previousClass"), TEXT("previous_class"));
		FString PreviousName;
		if (PreviousValue.IsValid() && PreviousValue->TryGetString(PreviousName))
		{
			EFormationTerrainClass Parsed = EFormationTerrainClass::Open;
			if (!FormationNames::Parse(PreviousName, Parsed))
			{
				return FString::Printf(TEXT("unknown previous class %s"), *PreviousName);
			}
			Previous = Parsed;
		}
		const EFormationTerrainClass Actual = FormationTerrainRules::Classify(Walls, Pos, static_cast<float>(Yaw), Previous, static_cast<float>(TimeInClass), Golden.Config);
		return Actual == Expected ? FString() : FString::Printf(TEXT("class %s (raw %s), expected %s"), FormationNames::ToString(Actual),
			FormationNames::ToString(FormationTerrainRules::ClassifyRaw(Walls, Pos, static_cast<float>(Yaw), Golden.Config)), *ExpectedName);
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
		const FString Actual = FormationNames::ToString(FormationTerrainRules::FormationFor(Class, static_cast<int32>(Count)));
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

#undef FORMATION_PARITY_TEST

#endif
