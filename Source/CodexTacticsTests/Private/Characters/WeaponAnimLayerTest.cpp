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
	TestTrue(TEXT("an upper-body montage (fire / reload) owns the torso, the ABP the legs: off"), ResolveMask(true, false, true) == EWeaponLayerMask::Off);
	TestTrue(TEXT("both montages: off"), ResolveMask(true, true, true) == EWeaponLayerMask::Off);
	TestTrue(TEXT("free: the upper body only (the mask never names the legs)"), ResolveMask(true, false, false) == EWeaponLayerMask::Full);
	TestEqual(TEXT("standing: idle"), MoveWeight(5.f, 15.f, 60.f), 0.f);
	TestEqual(TEXT("37.5 cm/s: half"), MoveWeight(37.5f, 15.f, 60.f), 0.5f, 0.001f);
	TestEqual(TEXT("walking: the walk set"), MoveWeight(150.f, 15.f, 60.f), 1.f);
	TestEqual(TEXT("walk speed: no jog"), JogWeight(150.f, 150.f, 350.f), 0.f);
	TestEqual(TEXT("250 cm/s: half jog"), JogWeight(250.f, 150.f, 350.f), 0.5f, 0.001f);
	TestEqual(TEXT("loop rate follows the speed"), LoopPlayRate(300.f, 150.f), 1.6f, 0.001f);
	TestEqual(TEXT("... clamped low"), LoopPlayRate(20.f, 150.f), 0.5f, 0.001f);
	const FString Pack = TEXT("/Game/ShotgunLocomotionPack/Animations/Aim/anim_shotgun_aim_idle.anim_shotgun_aim_idle");
	TestEqual(TEXT("retargeted copy: same name in the skeleton's folder"), RetargetedClipPath(Pack, TEXT("/Game/Characters/Animations/Shotgun/Male")),
		FString(TEXT("/Game/Characters/Animations/Shotgun/Male/anim_shotgun_aim_idle.anim_shotgun_aim_idle")));
	TestEqual(TEXT("... a package path and a trailing slash"), RetargetedClipPath(TEXT("/Game/P/Walk/clip_a"), TEXT("/Game/X/")),
		FString(TEXT("/Game/X/clip_a.clip_a")));
	TestTrue(TEXT("... no folder: none (the pack clip plays)"), RetargetedClipPath(Pack, TEXT("")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponLayerShellReloadTest, "CodexTactics.Animation.WeaponLayer.ShellReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponLayerShellReloadTest::RunTest(const FString&)
{
	using namespace WeaponAnimLayerRules;
	// The pack's reload, measured (Scripts/Editor/measure_shotgun_reload.py): intro 0 - 0.833, the insertion loop 0.833 - 1.2 (the same
	// pose at both ends), the outro from 1.833 (the loop's pose again) to 2.4.
	TestEqual(TEXT("loop rate: 0.367 s loop per 0.5 s shell = x0.733 (near natural, was x4.8)"), ShellLoopRate(0.8333f, 1.2f, 0.5f), 0.733f, 0.002f);
	TestEqual(TEXT("... clamped high"), ShellLoopRate(0.f, 2.4f, 0.5f), 1.4f, 0.001f);
	TestEqual(TEXT("... no segment: 1"), ShellLoopRate(1.f, 1.f, 0.5f), 1.f);
	FShellReloadClip Clip;
	Clip.Length = 2.4f;
	Clip.LoopStart = 0.8333f;
	Clip.LoopEnd = 1.2f;
	Clip.OutroStart = 1.8333f;
	Clip.LoopRate = 0.733f;
	FShellReloadState State;
	AdvanceShellReload(State, Clip, 0.1f, false);
	TestTrue(TEXT("not reloading: nothing"), State.Phase == EShellReloadPhase::None);
	AdvanceShellReload(State, Clip, 0.5f, true);
	TestTrue(TEXT("reload: the intro plays at rate 1"), State.Phase == EShellReloadPhase::Intro && FMath::IsNearlyEqual(State.Time, 0.5f));
	AdvanceShellReload(State, Clip, 0.5f, true);
	TestTrue(TEXT("... then the insertion loop"), State.Phase == EShellReloadPhase::Loop && State.Time >= 0.8333f && State.Time < 1.2f);
	// 8 shells x 0.5 s: the loop wraps, the time always inside it and never jumping except at the wrap.
	float Last = State.Time;
	int32 Jumps = 0;
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		AdvanceShellReload(State, Clip, 1.f / 60.f, true);
		TestTrue(TEXT("... stays in the loop"), State.Phase == EShellReloadPhase::Loop && State.Time >= 0.8333f - KINDA_SMALL_NUMBER && State.Time < 1.2f);
		Jumps += FMath::Abs(State.Time - Last) > 0.05f ? 1 : 0;
		Last = State.Time;
	}
	TestTrue(TEXT("... about one loop per shell (4 s = ~8 loops)"), State.Loops >= 7 && State.Loops <= 9);
	TestTrue(TEXT("... the only jumps are the wraps (the same pose at both ends)"), Jumps <= State.Loops);
	AdvanceShellReload(State, Clip, 1.f / 60.f, false);
	TestTrue(TEXT("full: the current loop plays out"), State.Phase == EShellReloadPhase::Finishing);
	for (int32 Frame = 0; Frame < 40 && State.Phase == EShellReloadPhase::Finishing; ++Frame)
	{
		AdvanceShellReload(State, Clip, 1.f / 60.f, false);
	}
	TestTrue(TEXT("... then the outro from 1.833 (the loop's end pose)"), State.Phase == EShellReloadPhase::Outro && State.Time >= 1.8333f && State.Time < 1.95f);
	AdvanceShellReload(State, Clip, 0.7f, false);
	TestTrue(TEXT("... and the reload clip is over"), State.Phase == EShellReloadPhase::None);
	// A reload resumed while the last loop plays out goes back to looping; an invalid table loops the whole clip.
	State = FShellReloadState();
	AdvanceShellReload(State, Clip, 1.f, true);
	AdvanceShellReload(State, Clip, 1.f / 60.f, false);
	AdvanceShellReload(State, Clip, 1.f / 60.f, true);
	TestTrue(TEXT("finishing -> reloading again: loop"), State.Phase == EShellReloadPhase::Loop);
	FShellReloadClip Bad;
	Bad.Length = 2.4f;
	const FShellReloadClip Safe = SanitizeShellReloadClip(Bad);
	TestTrue(TEXT("no table: the whole clip is the loop, no outro"), Safe.LoopStart == 0.f && FMath::IsNearlyEqual(Safe.LoopEnd, 2.4f) && Safe.OutroStart == 0.f);
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
