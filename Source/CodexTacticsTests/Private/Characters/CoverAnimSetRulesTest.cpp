// CodexTactics.Animation.CoverAnimSet.* - data-driven cover animation sets (designer 2026-11-22, docs/design/INDOOR_COVER_ANIMATIONS.md):
// CoverAnimSetRules (JSON, context, set choice with per-operative overrides, body fallback, side keys, the over-the-top fire set, the walk
// start / stop rule) + the shipped Content/Data/Animation/cover_anim_sets.json. In-game check: CodexTactics.CoverIndoorSmoke. UE-only.

#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"
#include "Characters/CoverAnimSetRules.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

#define COVERSET_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Animation.CoverAnimSet." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	const TCHAR* GSampleJson = TEXT(R"({
		"enabled": true,
		"contexts": {"outdoor": "m4", "indoor": "riffle", "stealth": "m4"},
		"operatives": {"Sniper": {"outdoor": "riffle"}, "Medic": {"indoor": "m4"}},
		"shimmy_transition_min_cm": 120,
		"sets": {"riffle": {
			"grenade_release_s": {"stand_left": 0.8},
			"shimmy_clip_speed": {"stand": 110, "crouch": 147},
			"bodies": {
				"default": {"stand_idle_left": "/Game/A/UE4L", "stand_idle_right": "/Game/A/UE4R"},
				"SK_PmcOperator_Skeleton": {"stand_idle_left": "/Game/A/PmcL"}
			}}}
	})");

	FCoverAnimSetConfig ParseSample(FAutomationTestBase& Test)
	{
		FCoverAnimSetConfig Config;
		FString Error;
		Test.TestTrue(TEXT("the sample parses"), CoverAnimSetRules::ParseConfig(GSampleJson, Config, Error));
		return Config;
	}
}

COVERSET_TEST(FCoverAnimSetParseTest, "Parse")
bool FCoverAnimSetParseTest::RunTest(const FString& Parameters)
{
	const FCoverAnimSetConfig Config = ParseSample(*this);
	TestTrue(TEXT("enabled"), Config.bEnabled);
	TestEqual(TEXT("indoor context"), Config.Contexts.FindRef(TEXT("indoor")), FString(TEXT("riffle")));
	TestEqual(TEXT("walk threshold"), Config.ShimmyTransitionMinCm, 120.f);
	const FCoverAnimSetData* Riffle = Config.Sets.Find(TEXT("riffle"));
	TestNotNull(TEXT("riffle set"), Riffle);
	if (Riffle)
	{
		TestEqual(TEXT("release"), Riffle->GrenadeReleaseSeconds.FindRef(TEXT("stand_left")), 0.8f);
		TestEqual(TEXT("crouch pace"), Riffle->ShimmyClipSpeed.FindRef(TEXT("crouch")), 147.f);
		TestEqual(TEXT("two bodies"), Riffle->Bodies.Num(), 2);
	}
	FCoverAnimSetConfig Bad;
	FString Error;
	TestFalse(TEXT("not an object"), CoverAnimSetRules::ParseConfig(TEXT("[1,2]"), Bad, Error));
	TestFalse(TEXT("a bad file leaves it disabled"), Bad.bEnabled);
	return true;
}

COVERSET_TEST(FCoverAnimSetChooseTest, "ChooseSet")
bool FCoverAnimSetChooseTest::RunTest(const FString& Parameters)
{
	FCoverAnimSetConfig Config = ParseSample(*this);
	const TArray<FString> Nobody = { TEXT("Engineer") };
	TestEqual(TEXT("indoor -> riffle"), CoverAnimSetRules::ChooseSet(Config, ECoverAnimContext::Indoor, Nobody), FString(TEXT("riffle")));
	TestEqual(TEXT("outdoor -> native (m4 = empty)"), CoverAnimSetRules::ChooseSet(Config, ECoverAnimContext::Outdoor, Nobody), FString());
	TestEqual(TEXT("stealth -> native for now"), CoverAnimSetRules::ChooseSet(Config, ECoverAnimContext::Stealth, Nobody), FString());
	TestEqual(TEXT("an operative's own outdoor style"), CoverAnimSetRules::ChooseSet(Config, ECoverAnimContext::Outdoor, { TEXT("x"), TEXT("Sniper") }), FString(TEXT("riffle")));
	TestEqual(TEXT("an operative opting out indoors"), CoverAnimSetRules::ChooseSet(Config, ECoverAnimContext::Indoor, { TEXT("Medic") }), FString());
	TestEqual(TEXT("his entry without this context -> the context's"), CoverAnimSetRules::ChooseSet(Config, ECoverAnimContext::Indoor, { TEXT("Sniper") }), FString(TEXT("riffle")));
	Config.Contexts.Add(TEXT("indoor"), TEXT("nosuchset"));
	TestEqual(TEXT("an unknown set -> native"), CoverAnimSetRules::ChooseSet(Config, ECoverAnimContext::Indoor, Nobody), FString());
	Config.Contexts.Add(TEXT("indoor"), TEXT("riffle"));
	Config.bEnabled = false;
	TestEqual(TEXT("disabled -> native everywhere"), CoverAnimSetRules::ChooseSet(Config, ECoverAnimContext::Indoor, Nobody), FString());
	return true;
}

COVERSET_TEST(FCoverAnimSetContextTest, "Context")
bool FCoverAnimSetContextTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("in a building = indoor"), CoverAnimSetRules::DecideContext(true, false) == ECoverAnimContext::Indoor);
	TestTrue(TEXT("outside = outdoor"), CoverAnimSetRules::DecideContext(false, false) == ECoverAnimContext::Outdoor);
	TestTrue(TEXT("stealth wins"), CoverAnimSetRules::DecideContext(true, true) == ECoverAnimContext::Stealth);
	TestEqual(TEXT("names"), FString(CoverAnimSetRules::ContextName(ECoverAnimContext::Indoor)), FString(TEXT("indoor")));
	return true;
}

COVERSET_TEST(FCoverAnimSetBodiesTest, "BodiesAndSides")
bool FCoverAnimSetBodiesTest::RunTest(const FString& Parameters)
{
	const FCoverAnimSetConfig Config = ParseSample(*this);
	const TMap<FString, FString>* Pmc = CoverAnimSetRules::FindBodyClips(Config, TEXT("riffle"), TEXT("SK_PmcOperator_Skeleton"));
	const TMap<FString, FString>* Ue4 = CoverAnimSetRules::FindBodyClips(Config, TEXT("riffle"), TEXT("UE4_Mannequin_Skeleton"));
	TestTrue(TEXT("the PMC body: its own table"), Pmc && Pmc->FindRef(TEXT("stand_idle_left")) == TEXT("/Game/A/PmcL"));
	TestTrue(TEXT("other skeletons: default"), Ue4 && Ue4->FindRef(TEXT("stand_idle_left")) == TEXT("/Game/A/UE4L"));
	TestNull(TEXT("unknown set"), CoverAnimSetRules::FindBodyClips(Config, TEXT("none"), TEXT("x")));
	TestEqual(TEXT("package path -> object path"), CoverAnimSetRules::ToObjectPath(TEXT("/Game/A/AS_X")), FString(TEXT("/Game/A/AS_X.AS_X")));
	TestEqual(TEXT("an object path stays"), CoverAnimSetRules::ToObjectPath(TEXT("/Game/A/AS_X.AS_X")), FString(TEXT("/Game/A/AS_X.AS_X")));
	TestEqual(TEXT("right key"), CoverAnimSetRules::SideKey(TEXT("stand_idle"), ECoverFacing::Right), FString(TEXT("stand_idle_right")));
	TestEqual(TEXT("left key"), CoverAnimSetRules::SideKey(TEXT("stand_idle"), ECoverFacing::Left), FString(TEXT("stand_idle_left")));
	return true;
}

COVERSET_TEST(FCoverAnimSetFireSetTest, "OverTopFireSet")
bool FCoverAnimSetFireSetTest::RunTest(const FString& Parameters)
{
	// Window / low wall away from its corner, crouched: over the top (designer Q2: Aim/2 at windows).
	TestTrue(TEXT("crouched, low, mid-wall"), CoverAnimSetRules::DecideFireSet(true, ECoverHeight::LowCover, false, true) == ECoverFireSet::Over);
	TestTrue(TEXT("at the corner: side peek"), CoverAnimSetRules::DecideFireSet(true, ECoverHeight::LowCover, true, true) == ECoverFireSet::Side);
	TestTrue(TEXT("high cover: side peek"), CoverAnimSetRules::DecideFireSet(true, ECoverHeight::HighCover, false, true) == ECoverFireSet::Side);
	TestTrue(TEXT("standing: side peek"), CoverAnimSetRules::DecideFireSet(false, ECoverHeight::LowCover, false, true) == ECoverFireSet::Side);
	TestTrue(TEXT("no over clips: side"), CoverAnimSetRules::DecideFireSet(true, ECoverHeight::LowCover, false, false) == ECoverFireSet::Side);
	return true;
}

COVERSET_TEST(FCoverAnimSetOverTopStagesTest, "OverTopStages")
bool FCoverAnimSetOverTopStagesTest::RunTest(const FString& Parameters)
{
	using EPhase = ECoverOverTopPhase;
	using EEvent = ECoverOverTopEvent;
	using CoverAnimSetRules::AdvanceOverTopPhase;
	const EPhase Entry = AdvanceOverTopPhase(EPhase::Idle, EEvent::SwitchInStarted);
	TestTrue(TEXT("right-side aim begins with the left switch"), Entry == EPhase::SwitchingIn);
	TestTrue(TEXT("another shot does not start a second switch"), AdvanceOverTopPhase(Entry, EEvent::SwitchInStarted) == EPhase::SwitchingIn);
	TestTrue(TEXT("the switch back cannot play before the pose starts"), AdvanceOverTopPhase(Entry, EEvent::FireEnded) == EPhase::SwitchingIn);
	const EPhase Fire = AdvanceOverTopPhase(Entry, EEvent::FireStarted);
	TestTrue(TEXT("fire takes over after switch-in"), Fire == EPhase::Firing);
	TestTrue(TEXT("sustained fire keeps the phase"), AdvanceOverTopPhase(Fire, EEvent::FireStarted) == EPhase::Firing);
	const EPhase Return = AdvanceOverTopPhase(Fire, EEvent::FireEnded);
	TestTrue(TEXT("switch out only after fire ends"), Return == EPhase::SwitchingOut);
	TestTrue(TEXT("new aim cannot cut the switch back"), AdvanceOverTopPhase(Return, EEvent::SwitchInStarted) == EPhase::SwitchingOut);
	TestTrue(TEXT("switch-out completion returns to idle"), AdvanceOverTopPhase(Return, EEvent::SwitchOutFinished) == EPhase::Idle);
	for (const EPhase Phase : { Entry, Fire, Return })
	{
		TestTrue(TEXT("leaving, death, or a new order clears every active phase"), AdvanceOverTopPhase(Phase, EEvent::Cancelled) == EPhase::Idle);
	}
	return true;
}

COVERSET_TEST(FCoverAnimSetMissingClipsTest, "MissingClips")
bool FCoverAnimSetMissingClipsTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("no loaded core clip leaves native cover active"), CoverAnimSetRules::CanActivateSet(0));
	TestTrue(TEXT("a partially loaded set can use native clips in its missing slots"), CoverAnimSetRules::CanActivateSet(1));
	UAnimSequence* Native = NewObject<UAnimSequence>();
	UAnimSequence* Side = NewObject<UAnimSequence>();
	UAnimSequence* Over = NewObject<UAnimSequence>();
	using CoverAnimSetRules::ResolveFireClip;
	TestTrue(TEXT("Aim/2 uses its own clip"), ResolveFireClip(ECoverFireSet::Over, Over, Side, Native) == Over);
	TestTrue(TEXT("a missing Aim/2 clip uses the side clip"), ResolveFireClip(ECoverFireSet::Over, nullptr, Side, Native) == Side);
	TestTrue(TEXT("leaving Aim/2 restores the set's side clip"), ResolveFireClip(ECoverFireSet::Side, Over, Side, Native) == Side);
	TestTrue(TEXT("a missing side clip restores the original native clip"), ResolveFireClip(ECoverFireSet::Side, Over, nullptr, Native) == Native);
	TestNull(TEXT("without a side or native clip, the Aim/2 clip is cleared"), ResolveFireClip(ECoverFireSet::Side, Over, nullptr, nullptr));
	return true;
}

COVERSET_TEST(FCoverAnimSetShimmyTest, "ShimmyTransitions")
bool FCoverAnimSetShimmyTest::RunTest(const FString& Parameters)
{
	// Designer Q5: start / stop clips only for walks of 1.5 m and more.
	TestTrue(TEXT("2 m walk"), CoverAnimSetRules::UseShimmyTransitions(200.f, 150.f, true));
	TestTrue(TEXT("exactly 1.5 m"), CoverAnimSetRules::UseShimmyTransitions(150.f, 150.f, true));
	TestFalse(TEXT("a 1 m side-step loops only"), CoverAnimSetRules::UseShimmyTransitions(100.f, 150.f, true));
	TestFalse(TEXT("no clip"), CoverAnimSetRules::UseShimmyTransitions(500.f, 150.f, false));
	TestTrue(TEXT("stop when the clip's travel is left"), CoverAnimSetRules::ShouldStartShimmyStop(60.f, 110.f, 0.63f));
	TestFalse(TEXT("not yet"), CoverAnimSetRules::ShouldStartShimmyStop(120.f, 110.f, 0.63f));
	return true;
}

COVERSET_TEST(FCoverAnimSetShippedJsonTest, "ShippedJson")
bool FCoverAnimSetShippedJsonTest::RunTest(const FString& Parameters)
{
	FString Text;
	if (!TestTrue(TEXT("cover_anim_sets.json read"), FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/Animation/cover_anim_sets.json")))))
	{
		return false;
	}
	FCoverAnimSetConfig Config;
	FString Error;
	TestTrue(TEXT("parses"), CoverAnimSetRules::ParseConfig(Text, Config, Error));
	TestTrue(TEXT("enabled (designer 2026-11-22)"), Config.bEnabled);
	TestEqual(TEXT("indoor = riffle"), CoverAnimSetRules::ChooseSet(Config, ECoverAnimContext::Indoor, {}), FString(TEXT("riffle")));
	TestEqual(TEXT("outdoor stays native"), CoverAnimSetRules::ChooseSet(Config, ECoverAnimContext::Outdoor, {}), FString());
	TestEqual(TEXT("Q5 threshold 1.5 m"), Config.ShimmyTransitionMinCm, 150.f);
	const TCHAR* Bases[] = { TEXT("stand_idle"), TEXT("crouch_idle"), TEXT("stand_enter"), TEXT("crouch_enter"), TEXT("stand_walk"), TEXT("crouch_walk"),
		TEXT("stand_fire_enter"), TEXT("stand_fire_idle"), TEXT("stand_fire_exit"), TEXT("crouch_fire_enter"), TEXT("crouch_fire_idle"), TEXT("crouch_fire_exit"),
		TEXT("crouch_over_fire_enter"), TEXT("crouch_over_fire_idle"), TEXT("crouch_over_fire_exit"), TEXT("stand_reload"), TEXT("crouch_reload"),
		TEXT("stand_grenade"), TEXT("crouch_grenade"), TEXT("stand_side_switch"), TEXT("crouch_side_switch"), TEXT("stand_walk_start"), TEXT("stand_walk_stop"),
		TEXT("stand_walk_turn"), TEXT("crouch_walk_start"), TEXT("crouch_walk_stop"), TEXT("crouch_walk_turn"), TEXT("stand_ao"), TEXT("crouch_ao"), TEXT("crouch_over_ao") };
	for (const TCHAR* Body : { TEXT("UE4_Mannequin_Skeleton"), TEXT("SK_PmcOperator_Skeleton") })
	{
		const TMap<FString, FString>* Clips = CoverAnimSetRules::FindBodyClips(Config, TEXT("riffle"), Body);
		if (!TestNotNull(FString::Printf(TEXT("%s clips"), Body), Clips))
		{
			continue;
		}
		const FString Folder = FString(Body).Contains(TEXT("Pmc")) ? TEXT("/Game/Animations/CoverIndoor/Pmc/") : TEXT("/Game/Animations/CoverIndoor/UE4/");
		for (const TCHAR* Base : Bases)
		{
			for (const ECoverFacing Facing : { ECoverFacing::Left, ECoverFacing::Right })
			{
				const FString Key = CoverAnimSetRules::SideKey(Base, Facing);
				const FString Path = Clips->FindRef(Key);
				TestTrue(FString::Printf(TEXT("%s %s under %s"), Body, *Key, *Folder), Path.StartsWith(Folder));
			}
		}
		// Pack L = his own LEFT (measured 2026-11-22): the left key plays the pack's L clips, the right key the R ones.
		TestTrue(TEXT("left = pack L"), Clips->FindRef(TEXT("stand_idle_left")).EndsWith(TEXT("AS_Cover_StandL_Idle")));
		TestTrue(TEXT("right = pack R"), Clips->FindRef(TEXT("stand_idle_right")).EndsWith(TEXT("AS_Cover_StandR_Idle")));
		TestTrue(TEXT("switch to his left = from the R set"), Clips->FindRef(TEXT("stand_side_switch_left")).EndsWith(TEXT("AS_Cover_StandR_To_L")));
	}
	return true;
}

#endif
