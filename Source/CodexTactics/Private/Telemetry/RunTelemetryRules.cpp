#include "Telemetry/RunTelemetryRules.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace
{
	double TelemetryRound1(float Value)
	{
		return FMath::RoundToDouble(Value * 10.0) / 10.0;
	}
}

void RunTelemetryRules::RecordShot(FMemberRunStats& Stats, int32 Wave, const FString& WeaponId, float Damage)
{
	FWeaponWaveStat& Stat = Stats.ByWave.FindOrAdd(Wave).FindOrAdd(WeaponId);
	++Stat.Shots;
	Stat.Damage += Damage;
	if (WeaponId == TEXT("pistol") && Stats.FirstPistolWave == -1)
	{
		Stats.FirstPistolWave = Wave;
	}
}

TSharedRef<FJsonObject> RunTelemetryRules::BuildWeaponSummary(const FMemberRunStats& Stats)
{
	FWeaponWaveStat M16, Pistol, Knife;
	TSharedRef<FJsonObject> ByWave = MakeShared<FJsonObject>();
	TArray<int32> Waves;
	Stats.ByWave.GetKeys(Waves);
	Waves.Sort();
	for (const int32 Wave : Waves)
	{
		TSharedRef<FJsonObject> WaveObject = MakeShared<FJsonObject>();
		for (const TPair<FString, FWeaponWaveStat>& Weapon : Stats.ByWave[Wave])
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("shots"), Weapon.Value.Shots);
			Entry->SetNumberField(TEXT("damage"), TelemetryRound1(Weapon.Value.Damage));
			WaveObject->SetObjectField(Weapon.Key, Entry);
			FWeaponWaveStat* Total = Weapon.Key == TEXT("m16") ? &M16 : (Weapon.Key == TEXT("pistol") ? &Pistol : (Weapon.Key == TEXT("knife") ? &Knife : nullptr));
			if (Total)
			{
				Total->Shots += Weapon.Value.Shots;
				Total->Damage += Weapon.Value.Damage;
			}
		}
		ByWave->SetObjectField(FString::FromInt(Wave), WaveObject);
	}
	FString Dominant = TEXT("m16");
	if (Pistol.Shots > M16.Shots)
	{
		Dominant = TEXT("pistol");
	}
	if (Knife.Shots > Pistol.Shots && Knife.Shots > M16.Shots)
	{
		Dominant = TEXT("knife");
	}
	TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetStringField(TEXT("character_name"), Stats.Name);
	Summary->SetStringField(TEXT("dominant_weapon"), Dominant);
	Summary->SetNumberField(TEXT("first_pistol_wave"), Stats.FirstPistolWave);
	Summary->SetNumberField(TEXT("total_shots_m16"), M16.Shots);
	Summary->SetNumberField(TEXT("total_damage_m16"), FMath::RoundToDouble(M16.Damage));
	Summary->SetNumberField(TEXT("total_shots_pistol"), Pistol.Shots);
	Summary->SetNumberField(TEXT("total_damage_pistol"), FMath::RoundToDouble(Pistol.Damage));
	Summary->SetNumberField(TEXT("total_strikes_knife"), Knife.Shots);
	Summary->SetNumberField(TEXT("total_damage_knife"), FMath::RoundToDouble(Knife.Damage));
	Summary->SetObjectField(TEXT("by_wave"), ByWave);
	return Summary;
}

int32 RunTelemetryRules::WavesCleared(bool bVictory, int32 CurrentWave)
{
	return bVictory ? CurrentWave : FMath::Max(0, CurrentWave - 1);
}

FString RunTelemetryRules::BuildRunJson(const FRunRecord& Record)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("session_id"), Record.SessionId);
	Root->SetStringField(TEXT("timestamp_utc"), Record.TimestampUtc);
	Root->SetStringField(TEXT("tester_profile"), Record.TesterProfile);
	Root->SetStringField(TEXT("level_id"), Record.LevelId);
	Root->SetStringField(TEXT("result"), Record.Result);
	Root->SetNumberField(TEXT("waves_cleared"), Record.WavesCleared);
	Root->SetNumberField(TEXT("total_waves"), Record.TotalWaves);
	Root->SetNumberField(TEXT("run_duration_sec"), FMath::RoundToDouble(Record.RunDurationSec * 1000.0) / 1000.0);
	Root->SetStringField(TEXT("engine"), TEXT("unreal"));

	float TotalCold = 0.f;
	int32 Medkits = 0, Cans = 0, Chocolate = 0, Matches = 0, M16 = 0, Pistol = 0;
	TArray<TSharedPtr<FJsonValue>> ColdStats;
	TArray<TSharedPtr<FJsonValue>> Weapons;
	for (const FMemberRunStats& Member : Record.Members)
	{
		TotalCold += Member.FinalColdPct;
		Medkits += Member.Medkits;
		Cans += Member.CannedFood;
		Chocolate += Member.Chocolate;
		Matches += Member.Matches;
		M16 += Member.AmmoM16;
		Pistol += Member.AmmoPistol;
		TSharedRef<FJsonObject> Cold = MakeShared<FJsonObject>();
		Cold->SetStringField(TEXT("name"), Member.Name);
		Cold->SetNumberField(TEXT("final_cold_pct"), FMath::RoundToDouble(Member.FinalColdPct));
		Cold->SetNumberField(TEXT("cold_damage_taken"), TelemetryRound1(Member.ColdDamageTaken));
		Cold->SetNumberField(TEXT("extreme_cold_time_sec"), TelemetryRound1(Member.ExtremeColdTimeSec));
		ColdStats.Add(MakeShared<FJsonValueObject>(Cold));
		Weapons.Add(MakeShared<FJsonValueObject>(BuildWeaponSummary(Member)));
	}
	const double AvgCold = Record.Members.IsEmpty() ? 0.0 : FMath::RoundToDouble(TotalCold / Record.Members.Num());
	Root->SetNumberField(TEXT("squad_avg_cold_percent"), AvgCold);
	Root->SetBoolField(TEXT("standard_loot_found"), Record.bStandardLootFound);
	Root->SetBoolField(TEXT("puzzle_secret_found"), Record.bPuzzleSecretFound);

	TSharedRef<FJsonObject> Remaining = MakeShared<FJsonObject>();
	Remaining->SetNumberField(TEXT("medkits"), Medkits);
	Remaining->SetNumberField(TEXT("ammo_m16"), M16);
	Remaining->SetNumberField(TEXT("ammo_pistol"), Pistol);
	Remaining->SetNumberField(TEXT("canned_food"), Cans);
	Remaining->SetNumberField(TEXT("chocolate"), Chocolate);
	Remaining->SetNumberField(TEXT("matches"), Matches);
	Root->SetObjectField(TEXT("remaining_resources"), Remaining);

	auto Deployable = [](const FDeployableRunStats& Stats, bool bSurvivors)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		if (bSurvivors)
		{
			Object->SetNumberField(TEXT("survived"), Stats.Survived);
			Object->SetNumberField(TEXT("avg_hp_pct"), FMath::RoundToDouble(Stats.AvgHpPct));
		}
		else
		{
			Object->SetNumberField(TEXT("detonated"), Stats.Kills); // Godot counts a mine kill as its detonation
		}
		Object->SetNumberField(TEXT("kills"), Stats.Kills);
		Object->SetNumberField(TEXT("damage"), FMath::RoundToDouble(Stats.Damage));
		return Object;
	};
	TSharedRef<FJsonObject> Deployables = MakeShared<FJsonObject>();
	Deployables->SetObjectField(TEXT("turrets"), Deployable(Record.Turrets, true));
	Deployables->SetObjectField(TEXT("barricades"), Deployable(Record.Barricades, true));
	Deployables->SetObjectField(TEXT("mines"), Deployable(Record.Mines, false));
	Root->SetObjectField(TEXT("deployables_summary"), Deployables);
	Root->SetArrayField(TEXT("squad_cold_stats"), ColdStats);
	Root->SetArrayField(TEXT("weapon_analytics"), Weapons);
	if (Record.Result == TEXT("VICTORY"))
	{
		Root->SetField(TEXT("death_context"), MakeShared<FJsonValueNull>());
	}
	else
	{
		TSharedRef<FJsonObject> Death = MakeShared<FJsonObject>();
		Death->SetNumberField(TEXT("failed_wave"), Record.FailedWave);
		Death->SetStringField(TEXT("cause"), Record.DeathCause);
		Death->SetNumberField(TEXT("squad_cold_at_death"), AvgCold);
		Root->SetObjectField(TEXT("death_context"), Death);
	}

	FString Json;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	return Json;
}
