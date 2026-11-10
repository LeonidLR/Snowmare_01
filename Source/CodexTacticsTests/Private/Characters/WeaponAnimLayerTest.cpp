// CodexTactics.Animation.WeaponLayer.* - the native shotgun animation layer's rules; CodexTactics.Weapons.Loadout.* - the squad's per-role
// loadouts (the D-1 shotgun for the Engineer, designer decisions 21 / 21+, 2026-11-03 / 11-08).

#include "Characters/WeaponAnimLayerRules.h"
#include "Data/WeaponLoadoutRules.h"
#include "Data/WeaponTuning.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

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
	// A "full" set (the shotgun on Loop_Root, designer 2026-11-09 b): its legs stay under an upper-body montage, a full-body one still wins.
	TestTrue(TEXT("full set, free: full"), ResolveMask(true, false, false, true) == EWeaponLayerMask::Full);
	TestTrue(TEXT("full set under a hit reaction: its legs only"), ResolveMask(true, false, true, true) == EWeaponLayerMask::LowerBody);
	TestTrue(TEXT("full set under a full-body montage: off"), ResolveMask(true, true, false, true) == EWeaponLayerMask::Off);
	TestTrue(TEXT("full set not eligible: off"), ResolveMask(false, false, false, true) == EWeaponLayerMask::Off);
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
	// The RPG-7 (the game designer 2026-11-09: only the Commander in our squad): his second weapon, the selector X cycles to it.
	TestTrue(TEXT("commander: m16, rpg7 (the designer), pistol, grenade"), WeaponLoadoutRules::Find(TEXT("commander"), Commander)
		&& Commander == TArray<FString>({ TEXT("m16"), TEXT("rpg7"), TEXT("pistol"), TEXT("grenade") }));
	TArray<FString> Medic;
	TestTrue(TEXT("medic-sapper: m16, sniper rifle, pistol, grenade (as the Blueprint gave her)"), WeaponLoadoutRules::Find(TEXT("medic_sapper"), Medic)
		&& Medic == TArray<FString>({ TEXT("m16"), TEXT("sniper_rifle"), TEXT("pistol"), TEXT("grenade") }));
	TArray<FString> Recruit;
	TestFalse(TEXT("the recruit keeps the Blueprint loadout"), WeaponLoadoutRules::Find(TEXT("recruit"), Recruit));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponLayerLauncherRulesTest, "CodexTactics.Animation.WeaponLayer.LauncherRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponLayerLauncherRulesTest::RunTest(const FString&)
{
	using namespace WeaponAnimLayerRules;
	TStaticArray<bool, 9> Edges(InPlace, false);
	Edges[static_cast<int32>(EWeaponAimPose::Up)] = Edges[static_cast<int32>(EWeaponAimPose::Down)] = true;
	Edges[static_cast<int32>(EWeaponAimPose::Left)] = Edges[static_cast<int32>(EWeaponAimPose::Right)] = true;
	auto W = [](const TStaticArray<float, 9>& Weights, EWeaponAimPose Pose) { return Weights[static_cast<int32>(Pose)]; };
	// The aim poses (the pack's Rotation_90_* at +-90 deg).
	TStaticArray<float, 9> A = AimPoseWeights(0.f, 0.f, 90.f, 90.f, Edges);
	TestEqual(TEXT("level, ahead: no delta"), W(A, EWeaponAimPose::Up) + W(A, EWeaponAimPose::Down) + W(A, EWeaponAimPose::Left) + W(A, EWeaponAimPose::Right), 0.f);
	A = AimPoseWeights(0.f, 45.f, 90.f, 90.f, Edges);
	TestEqual(TEXT("45 deg up: half the up pose"), W(A, EWeaponAimPose::Up), 0.5f, 0.001f);
	A = AimPoseWeights(0.f, -120.f, 90.f, 90.f, Edges);
	TestEqual(TEXT("past the range: clamped to the down pose"), W(A, EWeaponAimPose::Down), 1.f, 0.001f);
	A = AimPoseWeights(-45.f, 45.f, 90.f, 90.f, Edges);
	// Missing corner (up-left 0.25): its share goes to up and left (the deltas add up to the diagonal) - up 0.25 + 0.25, left 0.25 + 0.25:
	// the pitch and the yaw poses each at their own share, as two separate offsets would be.
	TestEqual(TEXT("up-left without the corner: up 0.25 + 0.25"), W(A, EWeaponAimPose::Up), 0.5f, 0.001f);
	TestEqual(TEXT("... left 0.25 + 0.25"), W(A, EWeaponAimPose::Left), 0.5f, 0.001f);
	TestEqual(TEXT("... no right"), W(A, EWeaponAimPose::Right), 0.f);
	TStaticArray<bool, 9> All(InPlace, true);
	A = AimPoseWeights(90.f, -90.f, 90.f, 90.f, All);
	TestEqual(TEXT("with the corner: the down-right pose alone"), W(A, EWeaponAimPose::DownRight), 1.f, 0.001f);
	// The rocket on the tube.
	TestTrue(TEXT("a round in the tube: shown"), LauncherRoundVisible(true, false, 0.f, 0.5f));
	TestFalse(TEXT("fired, not reloading: gone"), LauncherRoundVisible(false, false, 0.f, 0.5f));
	TestFalse(TEXT("reloading, before the insertion: gone"), LauncherRoundVisible(false, true, 0.3f, 0.5f));
	TestTrue(TEXT("reloading, past the insertion: back on the tube"), LauncherRoundVisible(false, true, 0.6f, 0.5f));
	// The reload clip fitted to the 4.5 s reload after the shot's 0.6 s follow-through (the pack's 2.333 s clip).
	TestTrue(TEXT("during the follow-through: not started"), LauncherReloadClipTime(0.3f, 4.5f, 2.333f, 0.6f) < 0.f);
	TestEqual(TEXT("its start"), LauncherReloadClipTime(0.6f, 4.5f, 2.333f, 0.6f), 0.f, 0.001f);
	TestEqual(TEXT("its end at the reload's end"), LauncherReloadClipTime(4.5f, 4.5f, 2.333f, 0.6f), 2.333f, 0.001f);
	TestEqual(TEXT("half-way through the clip"), LauncherReloadClipTime(0.6f + 1.95f, 4.5f, 2.333f, 0.6f), 2.333f * 0.5f, 0.001f);
	const float Insert = LauncherInsertFraction(1.333f, 2.333f, 4.5f, 0.6f);
	TestEqual(TEXT("the insertion share matches the clip time"), LauncherReloadClipTime(Insert * 4.5f, 4.5f, 2.333f, 0.6f), 1.333f, 0.002f);
	TestEqual(TEXT("no clip: half way"), LauncherInsertFraction(1.f, 0.f, 4.5f, 0.f), 0.5f);
	// The action.
	TestTrue(TEXT("the shot plays first"), ResolveLauncherAction(0.2f, 0.667f, 0.1f) == ELauncherAction::Shot);
	TestTrue(TEXT("then the reload"), ResolveLauncherAction(-1.f, 0.667f, 0.1f) == ELauncherAction::Reload);
	TestTrue(TEXT("the shot over, no reload yet: none"), ResolveLauncherAction(0.7f, 0.667f, -1.f) == ELauncherAction::None);
	// Turn in place and the speed match (the full-body set).
	TestEqual(TEXT("walking: no turn clip"), TurnInPlaceShare(120.f, 200.f, 15.f), 0.f);
	TestEqual(TEXT("a slow turn: none"), TurnInPlaceShare(20.f, 0.f, 15.f), 0.f);
	TestEqual(TEXT("60 deg/s: half"), TurnInPlaceShare(-60.f, 0.f, 15.f), 0.5f, 0.001f);
	TestEqual(TEXT("the turn clip follows the body: 45 deg = half the 90 deg clip"), TurnClipAdvance(-45.f, 90.f, 1.333f), 0.6665f, 0.001f);
	TStaticArray<float, 8> Speeds(InPlace, 0.f);
	Speeds[0] = 92.f;
	Speeds[2] = 112.5f;
	FWeaponLayerDirectionBlend Blend;
	Blend.A = EWeaponLayerDirection::Forward;
	Blend.B = EWeaponLayerDirection::Right;
	Blend.Alpha = 0.5f;
	TestEqual(TEXT("forward / right half and half: the speeds mixed"), BlendedNominalSpeed(Blend, Speeds, 150.f), (92.f + 112.5f) * 0.5f, 0.01f);
	Blend.B = EWeaponLayerDirection::Backward;
	Speeds[4] = 0.f;
	TestEqual(TEXT("an unlisted direction falls back to the set's speed"), BlendedNominalSpeed(Blend, Speeds, 150.f), (92.f + 150.f) * 0.5f, 0.01f);
	// The loops' sync offsets (the left foot's touchdown): phase 0 = every loop at its own touchdown.
	TestEqual(TEXT("no offset: phase x length"), SyncedClipTime(0.25f, 0.f, 1.333f), 0.25f * 1.333f, 0.001f);
	TestEqual(TEXT("phase 0 = the loop's left heel strike (0.5 of Walk_Left)"), SyncedClipTime(0.f, 0.5f, 1.333f), 0.5f * 1.333f, 0.001f);
	TestEqual(TEXT("wraps past the loop's end"), SyncedClipTime(0.75f, 0.5f, 2.f), 0.5f, 0.001f);
	TestTrue(TEXT("never at the very end"), SyncedClipTime(0.5f, 0.5f, 1.f) < 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponLayerLauncherDataTest, "CodexTactics.Animation.WeaponLayer.LauncherSetData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponLayerLauncherDataTest::RunTest(const FString&)
{
	// weapon_anim_sets.json: the launcher and the shotgun drive the whole body (designer 2026-11-09 b), the pistol the upper body; every launcher
	// clip has its retargeted copy for both squad skeletons (the pack is never played by bone name).
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!TestTrue(TEXT("weapon_anim_sets.json"), FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/Weapons/weapon_anim_sets.json")))
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid()))
	{
		return false;
	}
	// A missing field = the default ("body" upper, "crouch_body" full).
	auto Field = [&Root](const TCHAR* Key, const TCHAR* Name, const TCHAR* Default)
	{
		const TSharedPtr<FJsonObject>* Block = nullptr;
		FString Value;
		return Root->TryGetObjectField(Key, Block) && Block && (*Block)->TryGetStringField(Name, Value) ? Value : FString(Default);
	};
	TestEqual(TEXT("the launcher: full body"), Field(TEXT("launcher"), TEXT("body"), TEXT("upper")), FString(TEXT("full")));
	TestEqual(TEXT("the launcher crouched: upper body only (the pack's crouch walk slides its feet)"),
		Field(TEXT("launcher"), TEXT("crouch_body"), TEXT("full")), FString(TEXT("upper")));
	TestEqual(TEXT("the launcher: no root travel from the in-place clips"), Field(TEXT("launcher"), TEXT("root_translation"), TEXT("")),
		FString(TEXT("reference")));
	TestEqual(TEXT("the shotgun: full body (its Loop_Root loops, designer 2026-11-09 b)"), Field(TEXT("shotgun"), TEXT("body"), TEXT("upper")),
		FString(TEXT("full")));
	TestEqual(TEXT("the pistol: upper body"), Field(TEXT("pistol"), TEXT("body"), TEXT("upper")), FString(TEXT("upper")));
	const TSharedPtr<FJsonObject>* Launcher = nullptr;
	if (!TestTrue(TEXT("launcher block"), Root->TryGetObjectField(TEXT("launcher"), Launcher) && Launcher))
	{
		return false;
	}
	bool bPack = true;
	TestTrue(TEXT("pack_playback false"), (*Launcher)->TryGetBoolField(TEXT("pack_playback"), bPack) && !bPack);
	const TSharedPtr<FJsonObject>* Retargeted = nullptr;
	if (!TestTrue(TEXT("retargeted for both squad skeletons"), (*Launcher)->TryGetObjectField(TEXT("retargeted"), Retargeted) && Retargeted
		&& (*Retargeted)->HasField(TEXT("UE4_Mannequin_Skeleton")) && (*Retargeted)->HasField(TEXT("Female_Soldier_Skeleton"))))
	{
		return false;
	}
	TArray<FString> Paths;
	TFunction<void(const TSharedPtr<FJsonValue>&)> Walk = [&Paths, &Walk](const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return;
		}
		if (Value->Type == EJson::String && Value->AsString().StartsWith(TEXT("/Game/Launcher_Animations/")))
		{
			Paths.AddUnique(Value->AsString());
		}
		else if (Value->Type == EJson::Object)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Value->AsObject()->Values)
			{
				Walk(Pair.Value);
			}
		}
		else if (Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
			{
				Walk(Item);
			}
		}
	};
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Launcher)->Values)
	{
		Walk(Pair.Value);
	}
	TestTrue(TEXT("~35 launcher clips listed"), Paths.Num() >= 30);
	int32 Missing = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Folder : (*Retargeted)->Values)
	{
		for (const FString& Path : Paths)
		{
			const FString Copy = WeaponAnimLayerRules::RetargetedClipPath(Path, Folder.Value->AsString());
			if (!FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Copy)))
			{
				AddError(FString::Printf(TEXT("no retargeted copy %s"), *Copy));
				++Missing;
			}
		}
	}
	TestEqual(TEXT("every copy exists"), Missing, 0);
	double Insert = 0.0;
	TestTrue(TEXT("the rocket's insertion moment inside the reload clip (2.333 s)"), (*Launcher)->TryGetNumberField(TEXT("reload_insert_s"), Insert) && Insert > 0.5
		&& Insert < 2.3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponLayerLegsAbpTest, "CodexTactics.Animation.WeaponLayer.LegsAbp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponLayerLegsAbpTest::RunTest(const FString&)
{
	// "legs_abp" (the game designer 2026-11-10): the RPG-7's legs are the ABP's M16 locomotion running and walking forward, the pack's elsewhere.
	using namespace WeaponAnimLayerRules;
	TStaticArray<bool, 8> FourWay = WeaponAnimLayerTest::All(false);
	FourWay[static_cast<int32>(EWeaponLayerDirection::Forward)] = FourWay[static_cast<int32>(EWeaponLayerDirection::Right)] = true;
	FourWay[static_cast<int32>(EWeaponLayerDirection::Backward)] = FourWay[static_cast<int32>(EWeaponLayerDirection::Left)] = true;

	// The forward clip's share of a 4-way blend.
	TestEqual(TEXT("forward: all the forward clip"), ForwardShare(BlendDirections(0.f, FourWay)), 1.f, 0.001f);
	TestEqual(TEXT("30 deg right: 2/3 forward"), ForwardShare(BlendDirections(30.f, FourWay)), 2.f / 3.f, 0.001f);
	TestEqual(TEXT("30 deg left: 2/3 forward (Forward as the B side)"), ForwardShare(BlendDirections(-30.f, FourWay)), 2.f / 3.f, 0.001f);
	TestEqual(TEXT("strafe right: none"), ForwardShare(BlendDirections(90.f, FourWay)), 0.f, 0.001f);
	TestEqual(TEXT("backward-left: none"), ForwardShare(BlendDirections(-135.f, FourWay)), 0.f, 0.001f);
	// Continuous through the strafe: the forward -> strafe switch has no step (it follows the eased direction).
	float Last = ForwardShare(BlendDirections(0.f, FourWay));
	float MaxStep = 0.f;
	for (float Angle = 1.f; Angle <= 180.f; Angle += 1.f)
	{
		const float Share = ForwardShare(BlendDirections(Angle, FourWay));
		MaxStep = FMath::Max(MaxStep, FMath::Abs(Share - Last));
		Last = Share;
	}
	TestTrue(FString::Printf(TEXT("0 -> 180 deg: the forward share changes by at most 1/90 per degree (%.4f)"), MaxStep), MaxStep <= 1.f / 90.f + 0.0001f);

	// The forward cone (legs_abp_forward_cone_deg): fully forward inside it, stretched back to the strafe at 90, nothing beyond 90 changes.
	TestEqual(TEXT("no cone: the angle as it is"), ForwardConeAngle(30.f, 0.f), 30.f, 0.001f);
	TestEqual(TEXT("cone 35: 22 deg off the path (a formation look) = forward"), ForwardConeAngle(22.f, 35.f), 0.f, 0.001f);
	TestEqual(TEXT("cone 35: -35 deg = forward"), ForwardConeAngle(-35.f, 35.f), 0.f, 0.001f);
	TestEqual(TEXT("cone 35: 62.5 deg = halfway to the strafe"), ForwardConeAngle(62.5f, 35.f), 45.f, 0.001f);
	TestEqual(TEXT("cone 35: the strafe stays the strafe"), ForwardConeAngle(-90.f, 35.f), -90.f, 0.001f);
	TestEqual(TEXT("cone 35: backwards unchanged"), ForwardConeAngle(150.f, 35.f), 150.f, 0.001f);
	TestEqual(TEXT("cone 35, 4-way: 22 deg -> the whole forward clip"), ForwardShare(BlendDirections(ForwardConeAngle(22.f, 35.f), FourWay)), 1.f, 0.001f);
	TestEqual(TEXT("cone 35, 4-way: 62.5 deg -> half"), ForwardShare(BlendDirections(ForwardConeAngle(62.5f, 35.f), FourWay)), 0.5f, 0.001f);
	// Continuous with the cone too (no step at its edge or at 90).
	Last = ForwardShare(BlendDirections(ForwardConeAngle(0.f, 35.f), FourWay));
	MaxStep = 0.f;
	for (float Angle = 0.5f; Angle <= 180.f; Angle += 0.5f)
	{
		const float Share = ForwardShare(BlendDirections(ForwardConeAngle(Angle, 35.f), FourWay));
		MaxStep = FMath::Max(MaxStep, FMath::Abs(Share - Last));
		Last = Share;
	}
	TestTrue(FString::Printf(TEXT("cone 35: 0 -> 180 deg changes by at most 1/55 per degree (%.4f per 0.5 deg)"), MaxStep), MaxStep <= 0.5f / 55.f + 0.0001f);

	// Parsing.
	TArray<FString> Unknown;
	const FWeaponLegsAbpGaits Launcher = ParseLegsAbpGaits({ TEXT("run"), TEXT(" Sprint "), TEXT("walk_forward"), TEXT("hop") }, &Unknown);
	TestTrue(TEXT("run / sprint / walk_forward parsed (case and spaces ignored)"), Launcher.bRun && Launcher.bSprint && Launcher.bWalkForward && !Launcher.bWalk);
	TestTrue(TEXT("an unknown gait is reported"), Unknown.Num() == 1 && Unknown[0] == TEXT("hop"));
	TestFalse(TEXT("an empty list = no gaits"), ParseLegsAbpGaits({}).Any());

	// The weight.
	FWeaponLegsAbpShares Shares;
	Shares.Move = 1.f;
	Shares.Jog = 1.f;
	TestEqual(TEXT("no gaits (absent key): always the set's legs"), LegsAbpWeight(FWeaponLegsAbpGaits(), Shares), 0.f, 0.001f);
	TestEqual(TEXT("running (the jog loops), any direction: the ABP's legs"), LegsAbpWeight(Launcher, Shares), 1.f, 0.001f);
	Shares.Jog = 0.f;
	Shares.WalkForward = 1.f;
	TestEqual(TEXT("walking forward: the ABP's legs"), LegsAbpWeight(Launcher, Shares), 1.f, 0.001f);
	Shares.WalkForward = 0.f;
	TestEqual(TEXT("walking sideways / backwards: the pack's legs"), LegsAbpWeight(Launcher, Shares), 0.f, 0.001f);
	Shares.WalkForward = 0.5f;
	TestEqual(TEXT("walking diagonally forward (45 deg): half"), LegsAbpWeight(Launcher, Shares), 0.5f, 0.001f);
	Shares.WalkForward = 0.f;
	Shares.Jog = 0.25f;
	TestEqual(TEXT("strafing in the walk -> run band: the run's share"), LegsAbpWeight(Launcher, Shares), 0.25f, 0.001f);
	Shares.Jog = 0.f;
	Shares.Sprint = 1.f;
	TestEqual(TEXT("sprinting with \"sprint\" listed: the ABP's legs"), LegsAbpWeight(Launcher, Shares), 1.f, 0.001f);
	FWeaponLegsAbpGaits RunOnly;
	RunOnly.bRun = true;
	TestEqual(TEXT("sprinting without \"sprint\": the pack's legs"), LegsAbpWeight(RunOnly, Shares), 0.f, 0.001f);
	Shares.Sprint = 0.f;
	Shares.Move = 0.f;
	Shares.WalkForward = 1.f;
	TestEqual(TEXT("standing (idle, turn in place): the pack's legs"), LegsAbpWeight(Launcher, Shares), 0.f, 0.001f);
	Shares.Move = 0.4f;
	TestEqual(TEXT("starting to walk forward: by the move share"), LegsAbpWeight(Launcher, Shares), 0.4f, 0.001f);
	Shares.Move = 1.f;
	Shares.Crouch = 1.f;
	TestEqual(TEXT("crouched: never (crouch_body handles the crouch)"), LegsAbpWeight(Launcher, Shares), 0.f, 0.001f);
	Shares.Crouch = 0.f;
	Shares.AimMove = 1.f;
	Shares.AimWalkForward = 0.f;
	TestEqual(TEXT("an aim-walk set strafing: the pack's legs"), LegsAbpWeight(Launcher, Shares), 0.f, 0.001f);
	Shares.AimWalkForward = 1.f;
	TestEqual(TEXT("an aim-walk set walking forward: the ABP's legs"), LegsAbpWeight(Launcher, Shares), 1.f, 0.001f);
	FWeaponLegsAbpGaits AnyWalk;
	AnyWalk.bWalk = true;
	Shares.AimMove = 0.f;
	Shares.WalkForward = 0.f;
	TestEqual(TEXT("\"walk\": every walk direction"), LegsAbpWeight(AnyWalk, Shares), 1.f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponLayerLegsAbpDataTest, "CodexTactics.Animation.WeaponLayer.LegsAbpData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponLayerLegsAbpDataTest::RunTest(const FString&)
{
	// weapon_anim_sets.json: the launcher and (2026-11-10) the shotgun list legs_abp (running + walking forward on the ABP's legs; the designer 2026-11-10); every
	// other set keeps its own legs in every gait (no key).
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!TestTrue(TEXT("weapon_anim_sets.json"), FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/Weapons/weapon_anim_sets.json")))
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid()))
	{
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Root->Values)
	{
		const TSharedPtr<FJsonObject>* Block = nullptr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Block) || !Block)
		{
			continue;
		}
		TArray<FString> Names;
		const bool bHas = (*Block)->TryGetStringArrayField(TEXT("legs_abp"), Names);
		if (Pair.Key == TEXT("launcher"))
		{
			TArray<FString> Unknown;
			const WeaponAnimLayerRules::FWeaponLegsAbpGaits Gaits = WeaponAnimLayerRules::ParseLegsAbpGaits(Names, &Unknown);
			TestTrue(TEXT("the launcher: legs_abp run + sprint + walk_forward, nothing unknown"), bHas && Gaits.bRun && Gaits.bSprint && Gaits.bWalkForward
				&& !Gaits.bWalk && Unknown.IsEmpty());
			FString Body;
			TestTrue(TEXT("the launcher stays a full-body set (the pack's legs idle / turning / strafing)"), (*Block)->TryGetStringField(TEXT("body"), Body)
				&& Body == TEXT("full"));
			double Cone = 0.0;
			TestTrue(TEXT("the launcher: a forward cone within 0 .. 60 deg (walk_forward counts under a formation look)"),
				(*Block)->TryGetNumberField(TEXT("legs_abp_forward_cone_deg"), Cone) && Cone >= 0.0 && Cone <= 60.0);
			// The legs' run band ends below his run with the tube (~175 cm/s measured): running is wholly the ABP's legs, not a mix.
			const TArray<TSharedPtr<FJsonValue>>* RunBand = nullptr;
			TestTrue(TEXT("the launcher: legs_abp_run_speeds [start < full <= 170 cm/s]"), (*Block)->TryGetArrayField(TEXT("legs_abp_run_speeds"), RunBand) && RunBand
				&& RunBand->Num() == 2 && (*RunBand)[0]->AsNumber() < (*RunBand)[1]->AsNumber() && (*RunBand)[1]->AsNumber() <= 170.0);
		}
		else if (Pair.Key == TEXT("shotgun"))
		{
			// The designer 2026-11-10: the shotgun WALKS on the pack and RUNS / SPRINTS on the M16 (ABP) legs, no walk_forward.
			const WeaponAnimLayerRules::FWeaponLegsAbpGaits Gaits = WeaponAnimLayerRules::ParseLegsAbpGaits(Names, nullptr);
			TestTrue(TEXT("the shotgun: legs_abp run + sprint only"), bHas && Gaits.bRun && Gaits.bSprint && !Gaits.bWalkForward);
		}
		else
		{
			TestFalse(FString::Printf(TEXT("%s: no legs_abp (its legs unchanged)"), *Pair.Key), bHas || (*Block)->HasField(TEXT("legs_abp_forward_cone_deg"))
				|| (*Block)->HasField(TEXT("legs_abp_run_speeds")));
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
