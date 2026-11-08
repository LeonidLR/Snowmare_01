// CodexTactics.Weapons.Sidearm.* - the sidearm switch rule (SidearmRules); CodexTactics.Weapons.Pistol.* - the pistol's damage by distance in
// real time and in the turn-based grid (the whole pistol, 2026-11-08: full to 10 m, then down to x0.6 at the 25 m reach, the Godot per-cell table
// gone).

#include "Combat/SidearmRules.h"
#include "Combat/WeaponBallisticsRules.h"
#include "Data/WeaponDataAsset.h"
#include "Data/WeaponTuning.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tactics/TurnBasedRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSidearmRulesTest, "CodexTactics.Weapons.Sidearm.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSidearmRulesTest::RunTest(const FString&)
{
	using namespace SidearmRules;
	const FSidearmConfig Config; // on, the pistol, 10 m, 0.35 s draw, back after 3 s
	TestTrue(TEXT("empty primary, reserve left, pistol ready, enemy 6 m: draw"), ShouldDrawSidearm(Config, false, true, true, true, 600.f, false));
	TestFalse(TEXT("... enemy 14 m: reload instead"), ShouldDrawSidearm(Config, false, true, true, true, 1400.f, false));
	TestFalse(TEXT("... no enemy: reload"), ShouldDrawSidearm(Config, false, true, true, true, -1.f, false));
	TestFalse(TEXT("rounds still in the clip (a top-up): never"), ShouldDrawSidearm(Config, false, false, true, true, 600.f, false));
	TestFalse(TEXT("no reserve: AutoSwitchOnEmpty's business (for good)"), ShouldDrawSidearm(Config, false, true, false, true, 600.f, false));
	TestFalse(TEXT("pistol dry / not carried"), ShouldDrawSidearm(Config, false, true, true, false, 600.f, false));
	TestFalse(TEXT("already holding the pistol"), ShouldDrawSidearm(Config, true, true, true, true, 600.f, false));
	TestFalse(TEXT("raging (rage ignores the reload)"), ShouldDrawSidearm(Config, false, true, true, true, 600.f, true));
	// 2026-11-09 (CoverSmoke / CornerHoldSmoke: the corner man drew the pistol instead of ducking back to reload): in cover he reloads behind it.
	TestFalse(TEXT("in cover (the corner peek's fire pose too), enemy 6 m: reload behind the cover"), ShouldDrawSidearm(Config, false, true, true, true, 600.f, false, true));
	TestTrue(TEXT("in the open, enemy 6 m: the sidearm"), ShouldDrawSidearm(Config, false, true, true, true, 600.f, false, false));
	FSidearmConfig CoverToo = Config;
	CoverToo.bReloadInCover = false;
	TestTrue(TEXT("reload_in_cover off in the data: the sidearm in cover too (before)"), ShouldDrawSidearm(CoverToo, false, true, true, true, 600.f, false, true));
	FSidearmConfig Off = Config;
	Off.bEnabled = false;
	TestFalse(TEXT("switched off in the data"), ShouldDrawSidearm(Off, false, true, true, true, 600.f, false));
	TestFalse(TEXT("back: not before 3 s without a target"), ShouldReturnToPrimary(Config, true, 2.9f, false));
	TestTrue(TEXT("... at 3 s"), ShouldReturnToPrimary(Config, true, 3.f, false));
	TestTrue(TEXT("... at once when the pistol is dry"), ShouldReturnToPrimary(Config, true, 0.f, true));
	TestFalse(TEXT("... never for a pistol the player chose"), ShouldReturnToPrimary(Config, false, 10.f, true));
	// Parse: values, a bad value kept at the default.
	const TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
	Block->SetBoolField(TEXT("enabled"), true);
	Block->SetNumberField(TEXT("threat_range_m"), 12.0);
	Block->SetNumberField(TEXT("draw_s"), 9.0);
	TArray<FString> Errors;
	const FSidearmConfig Read = Parse(*Block, &Errors);
	TestEqual(TEXT("parse: threat range"), Read.ThreatRangeM, 12.f);
	TestEqual(TEXT("parse: a draw of 9 s is refused (kept 0.35)"), Read.DrawSeconds, 0.35f);
	TestEqual(TEXT("... with one error line"), Errors.Num(), 1);
	TestTrue(TEXT("parse: reload_in_cover defaults on"), Read.bReloadInCover);
	Block->SetBoolField(TEXT("reload_in_cover"), false);
	TestFalse(TEXT("parse: reload_in_cover false"), Parse(*Block).bReloadInCover);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPistolDamageCurveTest, "CodexTactics.Weapons.Pistol.DamageByDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPistolDamageCurveTest::RunTest(const FString&)
{
	// The project file's pistol: full damage to 10 m, x0.6 at the 25 m reach (linear between), no per-cell table.
	FString Text;
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* Weapons = nullptr;
	const TSharedPtr<FJsonObject>* Pistol = nullptr;
	if (!TestTrue(TEXT("weapons_tuning.json"), FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/Weapons/weapons_tuning.json")))
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid() && Root->TryGetObjectField(TEXT("weapons"), Weapons)
		&& Weapons && (*Weapons)->TryGetObjectField(TEXT("pistol"), Pistol) && Pistol))
	{
		return false;
	}
	UWeaponDataAsset* Weapon = NewObject<UWeaponDataAsset>();
	Weapon->WeaponId = TEXT("pistol");
	TArray<FString> Errors;
	Weapon->Stats = Weapon->MakeBaseStats();
	TestTrue(TEXT("the pistol block applies"), WeaponTuning::ApplyWeapon(**Pistol, *Weapon, &Errors));
	const FWeaponStats& Stats = Weapon->Stats;
	TestTrue(TEXT("no per-cell table any more (it cut the pistol to 12 % past ~8 m)"), Stats.Accuracy.DamageMultiplierByCell.IsEmpty());
	TestEqual(TEXT("handling: pistol"), static_cast<int32>(Stats.Handling), static_cast<int32>(EWeaponHandling::Pistol));
	using WeaponBallisticsRules::DamageFalloffMultiplier;
	TestEqual(TEXT("5 m: full"), DamageFalloffMultiplier(Stats, 500.f), 1.f, 0.001f);
	TestEqual(TEXT("10 m: full"), DamageFalloffMultiplier(Stats, 1000.f), 1.f, 0.001f);
	TestEqual(TEXT("18 m: x0.787"), DamageFalloffMultiplier(Stats, 1800.f), 1.f - 0.4f * 8.f / 15.f, 0.001f);
	TestEqual(TEXT("25 m: x0.6"), DamageFalloffMultiplier(Stats, 2500.f), 0.6f, 0.001f);
	// Turn-based: the grid's 1.5 m cells follow the same curve - the pistol's 6-cell reach (9 m) is all full damage now.
	for (int32 Cells = 1; Cells <= Stats.TurnBased.MaxRangeCells; ++Cells)
	{
		TestEqual(FString::Printf(TEXT("turn-based: %d cells = full damage (was down to x0.12)"), Cells), TurnBasedRules::GetDamageForDistance(Weapon, Cells, 0.f),
			Stats.Damage.BaseDamage, 0.001f);
	}
	TestEqual(TEXT("turn-based: 12 cells (18 m) would follow the curve too"), TurnBasedRules::GetDamageForDistance(Weapon, 12, 0.f),
		Stats.Damage.BaseDamage * DamageFalloffMultiplier(Stats, 1800.f), 0.001f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
