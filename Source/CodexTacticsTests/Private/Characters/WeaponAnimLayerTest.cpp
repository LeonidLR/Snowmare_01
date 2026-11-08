// CodexTactics.Animation.WeaponLayer.* - the native shotgun animation layer's rules; CodexTactics.Weapons.Loadout.* - the squad's per-role
// loadouts (the D-1 shotgun for the Engineer, designer decisions 21 / 21+, 2026-11-03 / 11-08).

#include "Characters/WeaponAnimLayerRules.h"
#include "Data/WeaponLoadoutRules.h"
#include "Data/WeaponTuning.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace WeaponAnimLayerTest
{
	TStaticArray<bool, 8> All(bool bValue)
	{
		TStaticArray<bool, 8> Out;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Out[Index] = bValue;
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponLayerDirectionsTest, "CodexTactics.Animation.WeaponLayer.Directions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponLayerDirectionsTest::RunTest(const FString&)
{
	using namespace WeaponAnimLayerRules;
	const TStaticArray<bool, 8> Eight = WeaponAnimLayerTest::All(true);
	FWeaponLayerDirectionBlend B = BlendDirections(0.f, Eight);
	TestTrue(TEXT("forward = the forward clip alone"), B.A == EWeaponLayerDirection::Forward && B.Alpha == 0.f);
	B = BlendDirections(22.5f, Eight);
	TestTrue(TEXT("22.5 deg: forward / forward-right half and half"), B.A == EWeaponLayerDirection::Forward && B.B == EWeaponLayerDirection::ForwardRight);
	TestEqual(TEXT("... alpha 0.5"), B.Alpha, 0.5f, 0.001f);
	B = BlendDirections(-90.f, Eight);
	TestTrue(TEXT("-90 = left"), B.A == EWeaponLayerDirection::Left && B.Alpha == 0.f);
	B = BlendDirections(180.f, Eight);
	TestTrue(TEXT("180 = backward"), B.A == EWeaponLayerDirection::Backward && B.Alpha == 0.f);
	B = BlendDirections(-170.f, Eight);
	TestTrue(TEXT("-170 (= 190): backward -> backward-left (wraps through 180)"), B.A == EWeaponLayerDirection::Backward && B.B == EWeaponLayerDirection::BackwardLeft);
	TestEqual(TEXT("... 10 of 45 deg towards backward-left"), B.Alpha, 10.f / 45.f, 0.001f);
	// The crouch set has 4 directions (no 45s): 45 deg = forward / right half and half.
	TStaticArray<bool, 8> Four = WeaponAnimLayerTest::All(false);
	Four[0] = Four[2] = Four[4] = Four[6] = true;
	B = BlendDirections(45.f, Four);
	TestTrue(TEXT("4-way 45 deg: forward + right"), B.A == EWeaponLayerDirection::Forward && B.B == EWeaponLayerDirection::Right);
	TestEqual(TEXT("... alpha 0.5"), B.Alpha, 0.5f, 0.001f);
	B = BlendDirections(-135.f, Four);
	TestTrue(TEXT("4-way -135: backward + left"), B.A == EWeaponLayerDirection::Backward && B.B == EWeaponLayerDirection::Left);
	B = BlendDirections(30.f, WeaponAnimLayerTest::All(false));
	TestTrue(TEXT("no clips: forward, 0"), B.A == EWeaponLayerDirection::Forward && B.Alpha == 0.f);
	TestEqual(TEXT("angles"), DirectionAngle(EWeaponLayerDirection::BackwardLeft), -135.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponLayerMaskTest, "CodexTactics.Animation.WeaponLayer.MaskAndSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponLayerMaskTest::RunTest(const FString&)
{
	using namespace WeaponAnimLayerRules;
	TestTrue(TEXT("not eligible: off"), ResolveMask(false, false, false) == EWeaponLayerMask::Off);
	TestTrue(TEXT("a full-body montage: off"), ResolveMask(true, true, false) == EWeaponLayerMask::Off);
	TestTrue(TEXT("an upper-body montage (fire / reload): legs only"), ResolveMask(true, false, true) == EWeaponLayerMask::LowerBody);
	TestTrue(TEXT("free: the whole body"), ResolveMask(true, false, false) == EWeaponLayerMask::Full);
	TestEqual(TEXT("standing: idle"), MoveWeight(5.f, 15.f, 60.f), 0.f);
	TestEqual(TEXT("37.5 cm/s: half"), MoveWeight(37.5f, 15.f, 60.f), 0.5f, 0.001f);
	TestEqual(TEXT("walking: the walk set"), MoveWeight(150.f, 15.f, 60.f), 1.f);
	TestEqual(TEXT("walk speed: no jog"), JogWeight(150.f, 150.f, 350.f), 0.f);
	TestEqual(TEXT("250 cm/s: half jog"), JogWeight(250.f, 150.f, 350.f), 0.5f, 0.001f);
	TestEqual(TEXT("loop rate follows the speed"), LoopPlayRate(300.f, 150.f), 1.6f, 0.001f);
	TestEqual(TEXT("... clamped low"), LoopPlayRate(20.f, 150.f), 0.5f, 0.001f);
	float Start = 0.f;
	float End = 0.f;
	TestEqual(TEXT("no section: the whole 2.4 s clip in 0.5 s = x4.8"), ShellReloadRate(2.4f, 0.f, 0.f, 0.5f, Start, End), 4.8f, 0.001f);
	TestTrue(TEXT("... from 0 to 2.4"), Start == 0.f && FMath::IsNearlyEqual(End, 2.4f));
	TestEqual(TEXT("a 0.8-1.6 s section in 0.5 s = x1.6"), ShellReloadRate(2.4f, 0.8f, 1.6f, 0.5f, Start, End), 1.6f, 0.001f);
	TestTrue(TEXT("... from 0.8"), FMath::IsNearlyEqual(Start, 0.8f) && FMath::IsNearlyEqual(End, 1.6f));
	const FString Pack = TEXT("/Game/ShotgunLocomotionPack/Animations/Aim/anim_shotgun_aim_idle.anim_shotgun_aim_idle");
	TestEqual(TEXT("retargeted copy: same name in the skeleton's folder"), RetargetedClipPath(Pack, TEXT("/Game/Characters/Animations/Shotgun/Male")),
		FString(TEXT("/Game/Characters/Animations/Shotgun/Male/anim_shotgun_aim_idle.anim_shotgun_aim_idle")));
	TestEqual(TEXT("... a package path and a trailing slash"), RetargetedClipPath(TEXT("/Game/P/Walk/clip_a"), TEXT("/Game/X/")),
		FString(TEXT("/Game/X/clip_a.clip_a")));
	TestTrue(TEXT("... no folder: none (the pack clip plays)"), RetargetedClipPath(Pack, TEXT("")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponLoadoutRulesTest, "CodexTactics.Weapons.Loadout.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponLoadoutRulesTest::RunTest(const FString&)
{
	using namespace WeaponLoadoutRules;
	TestEqual(TEXT("role key"), RoleKey(TEXT("MedicSapper")), FString(TEXT("medic_sapper")));
	TestEqual(TEXT("role key (one word)"), RoleKey(TEXT("Engineer")), FString(TEXT("engineer")));
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("{\"comment\": \"x\", \"engineer\": [\"shotgun\", \"knife\", \"pistol\"], \"bad\": 3, \"odd\": [\"m16\", 7]}")), Json);
	TArray<FString> Errors;
	const TMap<FString, TArray<FString>> Parsed = Json.IsValid() ? Parse(*Json, &Errors) : TMap<FString, TArray<FString>>();
	TestEqual(TEXT("two errors (not an array / not a string)"), Errors.Num(), 2);
	TestTrue(TEXT("engineer read in order"), Parsed.Contains(TEXT("engineer")) && Parsed[TEXT("engineer")].Num() == 3 && Parsed[TEXT("engineer")][0] == TEXT("shotgun"));
	// Saves: the held weapon after a load.
	TestEqual(TEXT("saved weapon owned: kept"), ResolveHeldWeapon(TEXT("pistol"), { TEXT("shotgun"), TEXT("pistol"), TEXT("grenade") }), FString(TEXT("pistol")));
	TestEqual(TEXT("an old Engineer save with the M16: his primary, the shotgun"), ResolveHeldWeapon(TEXT("m16"), { TEXT("shotgun"), TEXT("pistol"), TEXT("grenade") }),
		FString(TEXT("shotgun")));
	TestEqual(TEXT("nothing saved: the primary"), ResolveHeldWeapon(FString(), { TEXT("grenade"), TEXT("pistol") }), FString(TEXT("pistol")));
	TestEqual(TEXT("only a grenade"), ResolveHeldWeapon(TEXT("m16"), { TEXT("grenade") }), FString(TEXT("grenade")));
	TestEqual(TEXT("empty arsenal"), ResolveHeldWeapon(TEXT("m16"), {}), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponLoadoutProjectTest, "CodexTactics.Weapons.Loadout.ProjectFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponLoadoutProjectTest::RunTest(const FString&)
{
	// Designer decision 21+: the Engineer = the D-1 shotgun first, no M16; the others as before.
	WeaponTuning::ApplyFile(WeaponTuning::GetDefaultPath());
	TestEqual(TEXT("three roles"), WeaponTuning::GetLastReport().Loadouts, 3);
	TArray<FString> Engineer;
	TestTrue(TEXT("engineer listed"), WeaponLoadoutRules::Find(TEXT("engineer"), Engineer));
	TestTrue(TEXT("engineer: shotgun, pistol, grenade"), Engineer == TArray<FString>({ TEXT("shotgun"), TEXT("pistol"), TEXT("grenade") }));
	TArray<FString> Commander;
	TestTrue(TEXT("commander: m16, pistol, grenade"), WeaponLoadoutRules::Find(TEXT("commander"), Commander)
		&& Commander == TArray<FString>({ TEXT("m16"), TEXT("pistol"), TEXT("grenade") }));
	TArray<FString> Medic;
	TestTrue(TEXT("medic-sapper: m16, sniper rifle, pistol, grenade (as the Blueprint gave her)"), WeaponLoadoutRules::Find(TEXT("medic_sapper"), Medic)
		&& Medic == TArray<FString>({ TEXT("m16"), TEXT("sniper_rifle"), TEXT("pistol"), TEXT("grenade") }));
	TArray<FString> Recruit;
	TestFalse(TEXT("the recruit keeps the Blueprint loadout"), WeaponLoadoutRules::Find(TEXT("recruit"), Recruit));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
