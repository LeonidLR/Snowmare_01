// CodexTactics.Animation.WeaponLayer.Handoff* - when the native weapon layer hands the body back to the ABP (the game designer's shotgun
// play-test 2026-11-09: the crouch and the cover mechanics are the squad's rifle locomotion) and the data that switches it on for the D-1.

#include "Characters/WeaponLayerHandoffRules.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponLayerHandoffTest, "CodexTactics.Animation.WeaponLayer.Handoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponLayerHandoffTest::RunTest(const FString&)
{
	using namespace WeaponLayerHandoffRules;
	FWeaponLayerHandoffFacts Facts;
	TestFalse(TEXT("standing in the open: the layer"), DefersToAbp(Facts));
	Facts.bCrouching = true;
	TestFalse(TEXT("crouched with a set that crouches itself: the layer"), DefersToAbp(Facts));
	Facts.bCrouchAbpSet = true;
	TestTrue(TEXT("crouched with a \"crouch_body\": \"abp\" set: the ABP"), DefersToAbp(Facts));
	Facts.bCrouching = false;
	TestFalse(TEXT("... standing up again: the layer"), DefersToAbp(Facts));

	FWeaponLayerHandoffFacts Cover;
	Cover.bInCover = true;
	TestTrue(TEXT("in cover: the ABP for every set (the old gate)"), DefersToAbp(Cover));
	Cover.bInCover = false;
	Cover.bCoverOpenShot = true;
	TestFalse(TEXT("an open shot off the wall with a set that keeps its cover approach: the layer"), DefersToAbp(Cover));
	Cover.bCoverAbpSet = true;
	TestTrue(TEXT("... with a \"cover_mode\": \"abp\" set: the ABP"), DefersToAbp(Cover));
	Cover.bCoverOpenShot = false;
	Cover.bPendingCover = true;
	Cover.PendingCoverDistanceCm = 1200.f;
	TestFalse(TEXT("walking to a cover slot 12 m away: the layer"), DefersToAbp(Cover));
	Cover.PendingCoverDistanceCm = DefaultCoverApproachCm;
	TestTrue(TEXT("... its last 7 m: the ABP"), DefersToAbp(Cover));
	TestFalse(TEXT("... a set with a 3 m approach stretch, 7 m off: the layer"), DefersToAbp(Cover, 300.f));
	Cover.PendingCoverDistanceCm = 250.f;
	TestTrue(TEXT("... 2.5 m off: the ABP"), DefersToAbp(Cover, 300.f));
	TestFalse(TEXT("... no approach stretch (0 cm) far from the slot: the layer"), DefersToAbp(Cover, 0.f));
	Cover.bPendingCover = false;
	TestFalse(TEXT("the cover order gone (no slot): the layer"), DefersToAbp(Cover));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponLayerHandoffDataTest, "CodexTactics.Animation.WeaponLayer.HandoffData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponLayerHandoffDataTest::RunTest(const FString&)
{
	// weapon_anim_sets.json: the D-1 crouches and covers on the ABP (the designer 2026-11-09); the pistol keeps its old behaviour.
	FString Text;
	TSharedPtr<FJsonObject> Root;
	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/Weapons/weapon_anim_sets.json");
	if (!TestTrue(TEXT("weapon_anim_sets.json reads"), FFileHelper::LoadFileToString(Text, *Path) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root)
		&& Root.IsValid()))
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Shotgun = nullptr;
	const TSharedPtr<FJsonObject>* Pistol = nullptr;
	if (!TestTrue(TEXT("the shotgun and pistol blocks"), Root->TryGetObjectField(TEXT("shotgun"), Shotgun) && Shotgun && Root->TryGetObjectField(TEXT("pistol"), Pistol) && Pistol))
	{
		return false;
	}
	FString Value;
	TestTrue(TEXT("the D-1: \"crouch_body\": \"abp\""), (*Shotgun)->TryGetStringField(TEXT("crouch_body"), Value) && Value == TEXT("abp"));
	TestTrue(TEXT("the D-1: \"cover_mode\": \"abp\""), (*Shotgun)->TryGetStringField(TEXT("cover_mode"), Value) && Value == TEXT("abp"));
	double Approach = 0.0;
	TestTrue(TEXT("the D-1: a cover approach stretch past the run-and-peek's pre-turn (400 cm + glide)"),
		(*Shotgun)->TryGetNumberField(TEXT("cover_approach_abp_cm"), Approach) && Approach >= 500.0);
	TestFalse(TEXT("the pistol keeps its own crouch / cover approach (no keys)"), (*Pistol)->HasField(TEXT("crouch_body")) || (*Pistol)->HasField(TEXT("cover_mode")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponLayerDirectionSmoothingTest, "CodexTactics.Animation.WeaponLayer.HandoffDirectionSmoothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponLayerDirectionSmoothingTest::RunTest(const FString&)
{
	using namespace WeaponLayerHandoffRules;
	TestEqual(TEXT("no smoothing: the target at once"), SmoothLayerDirection(10.f, 80.f, 1.f / 60.f, 0.f, 8.f), 80.f);
	TestEqual(TEXT("within the dead zone: the legs stay"), SmoothLayerDirection(30.f, 36.f, 1.f / 60.f, 0.3f, 8.f), 30.f);
	TestEqual(TEXT("... the other way too"), SmoothLayerDirection(30.f, 23.f, 1.f / 60.f, 0.3f, 8.f), 30.f);
	// A swing back and forth within +-6 deg at 2 Hz (the follower's slot corrections): the legs never move.
	float Dir = 0.f;
	float Swing = 0.f;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Dir = SmoothLayerDirection(Dir, 6.f * FMath::Sin(Frame / 60.f * 2.f * PI * 2.f), 1.f / 60.f, 0.3f, 8.f);
		Swing = FMath::Max(Swing, FMath::Abs(Dir));
	}
	TestEqual(TEXT("a +-6 deg back-and-forth leaves the legs on their clip"), Swing, 0.f);
	// A real change of direction (90 deg strafe): reached up to the dead zone, ~95 % of the way in 3 time constants.
	Dir = 0.f;
	for (int32 Frame = 0; Frame < 54; ++Frame)
	{
		Dir = SmoothLayerDirection(Dir, 90.f, 1.f / 60.f, 0.3f, 8.f);
	}
	TestTrue(FString::Printf(TEXT("a 90 deg change followed in 0.9 s (%.1f)"), Dir), Dir > 75.f && Dir <= 82.f + KINDA_SMALL_NUMBER);
	for (int32 Frame = 0; Frame < 600; ++Frame)
	{
		Dir = SmoothLayerDirection(Dir, 90.f, 1.f / 60.f, 0.3f, 8.f);
	}
	TestTrue(FString::Printf(TEXT("... it settles within the dead zone of it (%.1f)"), Dir), FMath::IsNearlyEqual(Dir, 82.f, 0.1f));
	// The shortest way round: 170 -> -170 goes through 180.
	Dir = SmoothLayerDirection(170.f, -170.f, 1.f, 0.3f, 0.f);
	TestTrue(FString::Printf(TEXT("across +-180 the short way (%.1f)"), Dir), FMath::Abs(Dir) > 170.f);

	// The legs' direction state: a reversal cross-fades instead of sweeping through the strafes.
	FWeaponLayerDirectionState State;
	StepLayerDirection(State, -164.f, 1.f / 60.f, 0.25f, 8.f, 75.f, 0.25f);
	TestTrue(TEXT("seeded at the first direction"), State.bValid && State.Current == -164.f && State.Fade == 1.f);
	StepLayerDirection(State, -170.f, 1.f / 60.f, 0.25f, 8.f, 75.f, 0.25f);
	TestEqual(TEXT("a 6 deg change: within the dead zone"), State.Current, -164.f);
	StepLayerDirection(State, -2.f, 1.f / 60.f, 0.25f, 8.f, 75.f, 0.25f);
	TestTrue(FString::Printf(TEXT("backwards -> forwards (162 deg): the legs jump to the forward clips and cross-fade from the backward ones (now %.0f from %.0f, fade %.2f)"),
		State.Current, State.From, State.Fade), State.Current == -2.f && State.From == -164.f && State.Fade == 0.f);
	float Sweep = State.Current;
	for (int32 Frame = 0; Frame < 15; ++Frame)
	{
		StepLayerDirection(State, -2.f + (Frame % 2 == 0 ? 3.f : -3.f), 1.f / 60.f, 0.25f, 8.f, 75.f, 0.25f);
		Sweep = FMath::Max(Sweep, FMath::Abs(State.Current));
	}
	TestTrue(FString::Printf(TEXT("... the fade runs over 0.25 s (%.2f after 15 frames), no side clip in between (|direction| <= %.0f)"), State.Fade, Sweep),
		State.Fade > 0.99f && Sweep <= 10.f);
	FWeaponLayerDirectionState NoFade;
	StepLayerDirection(NoFade, 0.f, 1.f / 60.f, 0.f, 0.f, 0.f, 0.f);
	StepLayerDirection(NoFade, 170.f, 1.f / 60.f, 0.f, 0.f, 0.f, 0.f);
	TestTrue(TEXT("no smoothing / no cross-fade set: the direction at once (the upper-body sets, the old behaviour)"), NoFade.Current == 170.f && NoFade.Fade == 1.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
