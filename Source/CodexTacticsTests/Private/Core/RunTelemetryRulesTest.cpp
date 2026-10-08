#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Telemetry/RunTelemetryRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Run record for the Wave Editor analytics (Godot archive main.gd _record_telemetry / player.gd get_weapon_telemetry_summary).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunTelemetryWeaponTest, "CodexTactics.Telemetry.WeaponSummary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRunTelemetryWeaponTest::RunTest(const FString&)
{
	FMemberRunStats Stats;
	Stats.Name = TEXT("Commander");
	RunTelemetryRules::RecordShot(Stats, 1, TEXT("m16"), 18.f);
	RunTelemetryRules::RecordShot(Stats, 1, TEXT("m16"), 20.4f);
	RunTelemetryRules::RecordShot(Stats, 2, TEXT("pistol"), 12.f);
	RunTelemetryRules::RecordShot(Stats, 3, TEXT("pistol"), 12.f);
	RunTelemetryRules::RecordShot(Stats, 3, TEXT("pistol"), 12.f);
	TestEqual(TEXT("First pistol wave"), Stats.FirstPistolWave, 2);
	const TSharedRef<FJsonObject> Summary = RunTelemetryRules::BuildWeaponSummary(Stats);
	TestEqual(TEXT("m16 shots"), static_cast<int32>(Summary->GetNumberField(TEXT("total_shots_m16"))), 2);
	TestEqual(TEXT("m16 damage rounded"), static_cast<int32>(Summary->GetNumberField(TEXT("total_damage_m16"))), 38);
	TestEqual(TEXT("Pistol shots"), static_cast<int32>(Summary->GetNumberField(TEXT("total_shots_pistol"))), 3);
	TestEqual(TEXT("Dominant = most shots"), Summary->GetStringField(TEXT("dominant_weapon")), FString(TEXT("pistol")));
	const TSharedPtr<FJsonObject>& ByWave = Summary->GetObjectField(TEXT("by_wave"));
	TestTrue(TEXT("Wave keys as strings"), ByWave->HasField(TEXT("1")) && ByWave->HasField(TEXT("3")));
	TestEqual(TEXT("Wave 1 m16 shots"), static_cast<int32>(ByWave->GetObjectField(TEXT("1"))->GetObjectField(TEXT("m16"))->GetNumberField(TEXT("shots"))), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunTelemetryRecordTest, "CodexTactics.Telemetry.RunRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRunTelemetryRecordTest::RunTest(const FString&)
{
	TestEqual(TEXT("Victory clears the reached wave"), RunTelemetryRules::WavesCleared(true, 3), 3);
	TestEqual(TEXT("Defeat: the wave before"), RunTelemetryRules::WavesCleared(false, 3), 2);
	TestEqual(TEXT("Defeat in wave 1"), RunTelemetryRules::WavesCleared(false, 1), 0);

	FRunRecord Record;
	Record.SessionId = TEXT("1_2.000");
	Record.TesterProfile = TEXT("VETERAN");
	Record.LevelId = TEXT("level_01_outpost");
	Record.Result = TEXT("DEFEAT");
	Record.WavesCleared = 1;
	Record.TotalWaves = 3;
	Record.FailedWave = 2;
	Record.DeathCause = TEXT("FREEZING_FATIGUE");
	FMemberRunStats A;
	A.Name = TEXT("Commander");
	A.FinalColdPct = 60.f;
	A.Medkits = 1;
	A.AmmoM16 = 50;
	FMemberRunStats B;
	B.Name = TEXT("Engineer");
	B.FinalColdPct = 71.f;
	B.Medkits = 2;
	B.ColdDamageTaken = 3.46f;
	Record.Members = { A, B };
	Record.Turrets.Kills = 4;
	Record.Turrets.Damage = 812.6f;
	Record.Turrets.Survived = 1;
	Record.Turrets.AvgHpPct = 55.4f;
	Record.Mines.Kills = 2;

	const FString Json = RunTelemetryRules::BuildRunJson(Record);
	TestFalse(TEXT("One line"), Json.Contains(TEXT("\n")));
	TSharedPtr<FJsonObject> Root;
	TestTrue(TEXT("Valid JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid());
	if (!Root.IsValid())
	{
		return false;
	}
	TestEqual(TEXT("Profile"), Root->GetStringField(TEXT("tester_profile")), FString(TEXT("VETERAN")));
	TestEqual(TEXT("Engine tag"), Root->GetStringField(TEXT("engine")), FString(TEXT("unreal")));
	TestEqual(TEXT("Avg cold (60, 71) -> 66"), static_cast<int32>(Root->GetNumberField(TEXT("squad_avg_cold_percent"))), 66);
	TestEqual(TEXT("Medkits summed"), static_cast<int32>(Root->GetObjectField(TEXT("remaining_resources"))->GetNumberField(TEXT("medkits"))), 3);
	const TSharedPtr<FJsonObject>& Deployables = Root->GetObjectField(TEXT("deployables_summary"));
	TestEqual(TEXT("Turret damage rounded"), static_cast<int32>(Deployables->GetObjectField(TEXT("turrets"))->GetNumberField(TEXT("damage"))), 813);
	TestEqual(TEXT("Mines detonated = kills"), static_cast<int32>(Deployables->GetObjectField(TEXT("mines"))->GetNumberField(TEXT("detonated"))), 2);
	TestFalse(TEXT("Mines have no survivors field"), Deployables->GetObjectField(TEXT("mines"))->HasField(TEXT("survived")));
	TestEqual(TEXT("Cold damage 1 decimal"), Root->GetArrayField(TEXT("squad_cold_stats"))[1]->AsObject()->GetNumberField(TEXT("cold_damage_taken")), 3.5);
	TestEqual(TEXT("Weapon analytics per member"), Root->GetArrayField(TEXT("weapon_analytics")).Num(), 2);
	const TSharedPtr<FJsonObject>& Death = Root->GetObjectField(TEXT("death_context"));
	TestEqual(TEXT("Death cause"), Death->GetStringField(TEXT("cause")), FString(TEXT("FREEZING_FATIGUE")));
	TestEqual(TEXT("Failed wave"), static_cast<int32>(Death->GetNumberField(TEXT("failed_wave"))), 2);

	Record.Result = TEXT("VICTORY");
	TSharedPtr<FJsonObject> Win;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RunTelemetryRules::BuildRunJson(Record)), Win);
	TestTrue(TEXT("Victory: death_context null"), Win.IsValid() && Win->HasTypedField<EJson::Null>(TEXT("death_context")));
	return true;
}

#endif
