// CodexTactics.Characters.Mantle.* - mantling slice 1 (docs/design/MANTLING.md section 11, designer answers 11.6): the pure rules of
// MantleRules (height classes nominal +- tolerance from obstacle_heights.json, top vs over by depth, the wall ceiling, eligibility by kind,
// headroom, run-up -> standing / running clip, male / female clips, round-robin, missing-clip fallback, warp limits, path link spacing).

#include "Characters/MantleRules.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS

#define MANTLE_RULES_TEST(ClassName, Name) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, "CodexTactics.Characters.Mantle." Name, EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MantleRulesTestUtil
{
	/** A deep top (a crate / wreck / parapet) HeightCm tall, the operative walking up 60 cm from its face. */
	static FMantleProbe MantleTopProbe(float HeightCm, float DepthCm = 120.f)
	{
		FMantleProbe Probe;
		Probe.HeightCm = HeightCm;
		Probe.DepthCm = DepthCm;
		Probe.FaceDistanceCm = 60.f;
		return Probe;
	}

	static FName MantleClassOf(const FMantleConfig& Config, const FMantleDecision& Decision)
	{
		return Config.Classes.IsValidIndex(Decision.ClassIndex) ? Config.Classes[Decision.ClassIndex].Id : NAME_None;
	}

	static FString MantleClipName(const FMantleConfig& Config, const FMantleDecision& Decision)
	{
		return Config.Clips.IsValidIndex(Decision.ClipIndex) ? FPaths::GetBaseFilename(Config.Clips[Decision.ClipIndex].Path) : FString();
	}
}

MANTLE_RULES_TEST(FMantleRulesHeightClassesTest, "HeightClasses")
bool FMantleRulesHeightClassesTest::RunTest(const FString&)
{
	using namespace MantleRulesTestUtil;
	const FMantleConfig Config = MantleRules::MakeDefaultConfig();
	struct FCase { float Height; FName Class; EMantleRefusal Refusal; };
	const FCase Cases[] = {
		{ 85.f, TEXT("low_mantle"), EMantleRefusal::None },          // nominal
		{ 75.f, TEXT("low_mantle"), EMantleRefusal::None },          // - tolerance
		{ 95.f, TEXT("low_mantle"), EMantleRefusal::None },          // + tolerance
		{ 74.f, NAME_None, EMantleRefusal::TooLow },                 // just outside below
		{ 96.f, NAME_None, EMantleRefusal::BetweenClasses },         // just outside above
		{ 110.f, NAME_None, EMantleRefusal::BetweenClasses },        // the old crates: between low and high mantle
		{ 130.f, TEXT("high_mantle"), EMantleRefusal::None },
		{ 140.f, TEXT("high_mantle"), EMantleRefusal::None },
		{ 150.f, TEXT("high_mantle"), EMantleRefusal::None },
		{ 151.f, NAME_None, EMantleRefusal::BetweenClasses },
		{ 160.f, NAME_None, EMantleRefusal::BetweenClasses },        // the old boilers
		{ 219.f, NAME_None, EMantleRefusal::BetweenClasses },
		{ 220.f, TEXT("tall_ledge"), EMantleRefusal::None },
		{ 230.f, TEXT("tall_ledge"), EMantleRefusal::None },
		{ 240.f, TEXT("tall_ledge"), EMantleRefusal::None },
		{ 241.f, NAME_None, EMantleRefusal::BetweenClasses },
		{ 250.f, NAME_None, EMantleRefusal::BetweenClasses },        // the old fences (on a deep top: not a class)
		{ 321.f, NAME_None, EMantleRefusal::Wall },                  // above the 320 ceiling
		{ 330.f, NAME_None, EMantleRefusal::Wall },                  // the blocked-wall placeholder
	};
	for (const FCase& Case : Cases)
	{
		const FMantleDecision Decision = MantleRules::Decide(Config, MantleTopProbe(Case.Height));
		TestEqual(FString::Printf(TEXT("%.0f cm top: refusal"), Case.Height), MantleRules::RefusalName(Decision.Refusal), MantleRules::RefusalName(Case.Refusal));
		if (Case.Refusal == EMantleRefusal::None)
		{
			TestEqual(FString::Printf(TEXT("%.0f cm top: class"), Case.Height), MantleClassOf(Config, Decision), Case.Class);
		}
	}
	// very_tall (310 +- 10) needs a marked mantle edge (designer 2026-11-15: one-storey roofs only at 1-2 marked edges).
	FMantleProbe Roof = MantleTopProbe(300.f, 400.f);
	TestEqual(TEXT("an unmarked 300 cm roof: refused"), MantleRules::RefusalName(MantleRules::Decide(Config, Roof).Refusal),
		MantleRules::RefusalName(EMantleRefusal::NotMarked));
	Roof.bMarkedEdge = true;
	const FMantleDecision Marked = MantleRules::Decide(Config, Roof);
	TestTrue(TEXT("a marked 300 cm roof edge: climbed"), Marked.IsGo());
	TestEqual(TEXT("... very_tall"), MantleClassOf(Config, Marked), FName(TEXT("very_tall")));
	Roof.HeightCm = 320.f;
	TestTrue(TEXT("320 cm marked: the band's top"), MantleRules::Decide(Config, Roof).IsGo());
	return true;
}

MANTLE_RULES_TEST(FMantleRulesOverTest, "OverAndDepth")
bool FMantleRulesOverTest::RunTest(const FString&)
{
	using namespace MantleRulesTestUtil;
	const FMantleConfig Config = MantleRules::MakeDefaultConfig();
	FMantleProbe Fence = MantleTopProbe(135.f, 10.f);
	const FMantleDecision Over = MantleRules::Decide(Config, Fence);
	TestTrue(TEXT("a 135 cm fence (10 cm thick): over"), Over.IsGo());
	TestEqual(TEXT("... high_fence_over"), MantleClassOf(Config, Over), FName(TEXT("high_fence_over")));
	TestEqual(TEXT("... HighFenceVault"), MantleClipName(Config, Over), FString(TEXT("HighFenceVault")));
	TestEqual(TEXT("... starts over.start_cm from the face"), Over.ApproachCm, 110.f);
	Fence.HeightCm = 130.f;
	TestTrue(TEXT("a 130 cm fence: 135 - tolerance, over"), MantleRules::Decide(Config, Fence).IsGo());
	Fence.HeightCm = 129.f;
	TestEqual(TEXT("a 129 cm fence: just outside 135 +- 5"), MantleRules::RefusalName(MantleRules::Decide(Config, Fence).Refusal),
		MantleRules::RefusalName(EMantleRefusal::BetweenClasses));
	Fence.HeightCm = 141.f;
	TestEqual(TEXT("a 141 cm fence: between classes"), MantleRules::RefusalName(MantleRules::Decide(Config, Fence).Refusal),
		MantleRules::RefusalName(EMantleRefusal::BetweenClasses));
	Fence.HeightCm = 330.f;
	TestEqual(TEXT("a 330 cm fence: a wall"), MantleRules::RefusalName(MantleRules::Decide(Config, Fence).Refusal), MantleRules::RefusalName(EMantleRefusal::Wall));
	Fence.HeightCm = 95.f;
	TestEqual(TEXT("a 95 cm thin obstacle: the vault takes it"), MantleRules::RefusalName(MantleRules::Decide(Config, Fence).Refusal),
		MantleRules::RefusalName(EMantleRefusal::VaultInstead));
	Fence.HeightCm = 135.f;
	Fence.LandingStepCm = -80.f;
	TestEqual(TEXT("a fence with the far side 80 cm lower: refused"), MantleRules::RefusalName(MantleRules::Decide(Config, Fence).Refusal),
		MantleRules::RefusalName(EMantleRefusal::LandingStep));
	TestEqual(TEXT("70 cm deep: neither a top nor a fence"), MantleRules::RefusalName(MantleRules::Decide(Config, MantleTopProbe(140.f, 70.f)).Refusal),
		MantleRules::RefusalName(EMantleRefusal::DepthMismatch));
	TestTrue(TEXT("90 cm deep: a top"), MantleRules::DestinationForDepth(Config, 90.f) == EMantleDestination::Top);
	TestTrue(TEXT("60 cm thick: over"), MantleRules::DestinationForDepth(Config, 60.f) == EMantleDestination::Over);
	TestFalse(TEXT("depth 0 (unmeasured): none"), MantleRules::DestinationForDepth(Config, 0.f).IsSet());
	return true;
}

MANTLE_RULES_TEST(FMantleRulesEligibilityTest, "EligibilityAndHeadroom")
bool FMantleRulesEligibilityTest::RunTest(const FString&)
{
	using namespace MantleRulesTestUtil;
	const FMantleConfig Config = MantleRules::MakeDefaultConfig();
	bool bMarked = false;
	TestTrue(TEXT("a crate"), MantleRules::IsEligibleTags({ FName(TEXT("crate")) }, bMarked) && !bMarked);
	TestTrue(TEXT("a wreck"), MantleRules::IsEligibleTags({ FName(TEXT("wreck")) }, bMarked));
	TestTrue(TEXT("a boiler"), MantleRules::IsEligibleTags({ FName(TEXT("boiler")) }, bMarked));
	TestTrue(TEXT("a cut wall"), MantleRules::IsEligibleTags({ FName(TEXT("cut_wall")) }, bMarked));
	TestTrue(TEXT("a fence"), MantleRules::IsEligibleTags({ FName(TEXT("fence")) }, bMarked));
	TestFalse(TEXT("a barrel (explosive, designer 2026-11-15)"), MantleRules::IsEligibleTags({ FName(TEXT("barrel")) }, bMarked));
	TestFalse(TEXT("a building wall"), MantleRules::IsEligibleTags({ FName(TEXT("building")) }, bMarked));
	TestFalse(TEXT("a silo"), MantleRules::IsEligibleTags({ FName(TEXT("silo")) }, bMarked));
	TestTrue(TEXT("a marked edge"), MantleRules::IsEligibleTags({ FName(TEXT("building")), MantleRules::MarkedEdgeTag }, bMarked) && bMarked);
	TestTrue(TEXT("a smoke's test obstacle"), MantleRules::IsEligibleTags({ MantleRules::MantleTag }, bMarked));
	TestFalse(TEXT("NoMantle wins"), MantleRules::IsEligibleTags({ FName(TEXT("crate")), MantleRules::NoMantleTag }, bMarked));
	FMantleProbe Barrel = MantleTopProbe(85.f);
	Barrel.bEligible = false;
	TestEqual(TEXT("an ineligible obstacle is refused"), MantleRules::RefusalName(MantleRules::Decide(Config, Barrel).Refusal),
		MantleRules::RefusalName(EMantleRefusal::NotEligible));
	// Headroom: the standing capsule (180) + 10 cm margin over the landing.
	FMantleProbe Low = MantleTopProbe(140.f);
	Low.FreeHeadroomCm = 150.f;
	TestEqual(TEXT("150 cm free over the top: no headroom"), MantleRules::RefusalName(MantleRules::Decide(Config, Low).Refusal),
		MantleRules::RefusalName(EMantleRefusal::NoHeadroom));
	Low.FreeHeadroomCm = 189.f;
	TestEqual(TEXT("189 cm: still none (margin)"), MantleRules::RefusalName(MantleRules::Decide(Config, Low).Refusal), MantleRules::RefusalName(EMantleRefusal::NoHeadroom));
	Low.FreeHeadroomCm = 190.f;
	TestTrue(TEXT("190 cm: room"), MantleRules::Decide(Config, Low).IsGo());
	Low.FreeHeadroomCm = -1.f;
	TestTrue(TEXT("open sky (unmeasured): room"), MantleRules::Decide(Config, Low).IsGo());
	Low.bLandingFree = false;
	TestEqual(TEXT("an occupied landing"), MantleRules::RefusalName(MantleRules::Decide(Config, Low).Refusal), MantleRules::RefusalName(EMantleRefusal::NoLanding));
	TestEqual(TEXT("no obstacle (height 0)"), MantleRules::RefusalName(MantleRules::Decide(Config, MantleTopProbe(0.f)).Refusal),
		MantleRules::RefusalName(EMantleRefusal::NoObstacle));
	return true;
}

MANTLE_RULES_TEST(FMantleRulesRunUpTest, "RunUpAndClips")
bool FMantleRulesRunUpTest::RunTest(const FString&)
{
	using namespace MantleRulesTestUtil;
	const FMantleConfig Config = MantleRules::MakeDefaultConfig();
	// Walking up to a low mantle: the standing clip, no stop.
	FMantleProbe Walk = MantleTopProbe(85.f);
	FMantleDecision Decision = MantleRules::Decide(Config, Walk);
	TestEqual(TEXT("walking: Low_Climb"), MantleClipName(Config, Decision), FString(TEXT("Low_Climb")));
	TestTrue(TEXT("... standing, no stop"), Decision.Gait == EMantleGait::Standing && !Decision.bStopFirst && !Decision.bApproachFirst);
	TestEqual(TEXT("... starts at forward - inset"), Decision.ApproachCm, 118.9f - 50.f, 0.01f);
	TestTrue(TEXT("... male clip path"), Config.Clips[Decision.ClipIndex].Path.Contains(TEXT("/Male/")));
	// Too far for the standing clip: walk closer first.
	Walk.FaceDistanceCm = 200.f;
	Decision = MantleRules::Decide(Config, Walk);
	TestTrue(TEXT("200 cm off, walking: approach first"), Decision.IsGo() && Decision.bApproachFirst);
	// Running with the run-up of the run clip (RunLowClimb 157.8 cm forward: 100 cm off -> warp 0.95).
	FMantleProbe Run = MantleTopProbe(85.f);
	Run.bRunning = true;
	Run.FaceDistanceCm = 100.f;
	Decision = MantleRules::Decide(Config, Run);
	TestEqual(TEXT("running, enough run-up: RunLowClimb"), MantleClipName(Config, Decision), FString(TEXT("RunLowClimb")));
	TestTrue(TEXT("... running gait, no stop"), Decision.Gait == EMantleGait::Running && !Decision.bStopFirst);
	// Running but right at the face: stop, the standing clip (designer Q2 = A; never a sprint started by itself).
	Run.FaceDistanceCm = 20.f;
	Decision = MantleRules::Decide(Config, Run);
	TestEqual(TEXT("running, no run-up: Low_Climb"), MantleClipName(Config, Decision), FString(TEXT("Low_Climb")));
	TestTrue(TEXT("... after a stop"), Decision.Gait == EMantleGait::Standing && Decision.bStopFirst);
	// The high mantle's run clip needs its long run-up (431 cm forward).
	FMantleProbe HighRun = MantleTopProbe(140.f);
	HighRun.bRunning = true;
	HighRun.FaceDistanceCm = 380.f;
	TestEqual(TEXT("high mantle, 380 cm run-up: RunHighClimb"), MantleClipName(Config, MantleRules::Decide(Config, HighRun)), FString(TEXT("RunHighClimb")));
	HighRun.FaceDistanceCm = 150.f;
	Decision = MantleRules::Decide(Config, HighRun);
	TestEqual(TEXT("high mantle, 150 cm: HighClimb after a stop"), MantleClipName(Config, Decision), FString(TEXT("HighClimb")));
	TestTrue(TEXT("... stop first"), Decision.bStopFirst);
	// Female body: her own copies.
	FMantleProbe Her = MantleTopProbe(140.f);
	Her.Body = EMantleBody::Female;
	Decision = MantleRules::Decide(Config, Her);
	TestTrue(TEXT("female: the Female copy"), Decision.IsGo() && Config.Clips[Decision.ClipIndex].Path.Contains(TEXT("/Female/HighClimb")));
	// Round-robin between the two standing tall-ledge clips.
	FMantleProbe Ledge = MantleTopProbe(230.f, 150.f);
	const FString First = MantleClipName(Config, MantleRules::Decide(Config, Ledge));
	Ledge.Rotation = 1;
	const FString Second = MantleClipName(Config, MantleRules::Decide(Config, Ledge));
	Ledge.Rotation = 2;
	TestNotEqual(TEXT("tall ledge: the variants rotate"), First, Second);
	TestEqual(TEXT("... and come round"), MantleClipName(Config, MantleRules::Decide(Config, Ledge)), First);
	return true;
}

MANTLE_RULES_TEST(FMantleRulesMissingClipTest, "MissingClipFallback")
bool FMantleRulesMissingClipTest::RunTest(const FString&)
{
	using namespace MantleRulesTestUtil;
	const FMantleConfig Config = MantleRules::MakeDefaultConfig();
	auto IndexOf = [&Config](const TCHAR* Name, EMantleBody Body)
	{
		return Config.Clips.IndexOfByPredicate([Name, Body](const FMantleClip& Clip) { return Clip.Body == Body && FPaths::GetBaseFilename(Clip.Path) == Name; });
	};
	const int32 RunLow = IndexOf(TEXT("RunLowClimb"), EMantleBody::Male);
	const int32 StandLow = IndexOf(TEXT("Low_Climb"), EMantleBody::Male);
	TestTrue(TEXT("the table has both low clips"), RunLow != INDEX_NONE && StandLow != INDEX_NONE);
	FMantleProbe Run = MantleTopProbe(85.f);
	Run.bRunning = true;
	Run.FaceDistanceCm = 100.f;
	FMantleDecision Decision = MantleRules::Decide(Config, Run, [RunLow](int32 Index) { return Index != RunLow; });
	TestEqual(TEXT("run clip missing: the standing one"), MantleClipName(Config, Decision), FString(TEXT("Low_Climb")));
	TestTrue(TEXT("... after a stop"), Decision.bStopFirst);
	Decision = MantleRules::Decide(Config, Run, [RunLow, StandLow](int32 Index) { return Index != RunLow && Index != StandLow; });
	TestEqual(TEXT("both missing: refused"), MantleRules::RefusalName(Decision.Refusal), MantleRules::RefusalName(EMantleRefusal::MissingClip));
	// Never a male clip on the female body (the copies are per skeleton).
	FMantleConfig MaleOnly = Config;
	MaleOnly.Clips.RemoveAll([](const FMantleClip& Clip) { return Clip.Body == EMantleBody::Female; });
	FMantleProbe Her = MantleTopProbe(85.f);
	Her.Body = EMantleBody::Female;
	TestEqual(TEXT("female without her clips: refused"), MantleRules::RefusalName(MantleRules::Decide(MaleOnly, Her).Refusal),
		MantleRules::RefusalName(EMantleRefusal::MissingClip));
	// A class switched off.
	FMantleConfig Off = Config;
	Off.Classes[MantleRules::FindClass(Off, TEXT("high_mantle"))].bEnabled = false;
	TestEqual(TEXT("a disabled class"), MantleRules::RefusalName(MantleRules::Decide(Off, MantleTopProbe(140.f)).Refusal),
		MantleRules::RefusalName(EMantleRefusal::ClassDisabled));
	// A clip that would need too much warping.
	FMantleConfig Tall = Config;
	Tall.Clips[StandLow].RiseCm = 200.f;
	TestEqual(TEXT("an 85 cm top on a 200 cm clip: too much warp"), MantleRules::RefusalName(MantleRules::Decide(Tall, MantleTopProbe(85.f)).Refusal),
		MantleRules::RefusalName(EMantleRefusal::WarpTooLarge));
	return true;
}

MANTLE_RULES_TEST(FMantleRulesLinksTest, "LinksAndHits")
bool FMantleRulesLinksTest::RunTest(const FString&)
{
	const FMantleConfig Config = MantleRules::MakeDefaultConfig();
	TestTrue(TEXT("a knockdown-class hit cancels"), MantleRules::IsHeavyHit(Config, 35.f));
	TestFalse(TEXT("a graze does not"), MantleRules::IsHeavyHit(Config, 10.f));
	TestTrue(TEXT("an 85 cm top gets a link down"), MantleRules::NeedsDownLink(Config, 85.f));
	TestTrue(TEXT("a 140 cm top too"), MantleRules::NeedsDownLink(Config, 140.f));
	TestFalse(TEXT("a 230 cm top: the drop links"), MantleRules::NeedsDownLink(Config, 230.f));
	TestEqual(TEXT("a 60 cm face: no link"), MantleRules::LinkOffsets(Config, 60.f, 45.f).Num(), 0);
	const TArray<float> Crate = MantleRules::LinkOffsets(Config, 120.f, 45.f);
	TestTrue(TEXT("a 120 cm crate face: one link in the middle"), Crate.Num() == 1 && FMath::IsNearlyZero(Crate[0]));
	const TArray<float> Wreck = MantleRules::LinkOffsets(Config, 420.f, 45.f);
	TestEqual(TEXT("a 420 cm wreck side: three links"), Wreck.Num(), 3);
	TestTrue(TEXT("... symmetric, inside the margins"), Wreck.Num() == 3 && FMath::IsNearlyEqual(Wreck[0], -165.f) && FMath::IsNearlyEqual(Wreck[2], 165.f));
	TestEqual(TEXT("a 5 m parapet run: capped at links.max_per_face"), MantleRules::LinkOffsets(Config, 5000.f, 45.f).Num(), 4);
	return true;
}

MANTLE_RULES_TEST(FMantleRulesTuningTest, "TuningFileMirrorsDefaults")
bool FMantleRulesTuningTest::RunTest(const FString&)
{
	FString Text;
	if (!TestTrue(TEXT("mantle.json readable"), FFileHelper::LoadFileToString(Text, *MantleRules::GetDefaultPath())))
	{
		return false;
	}
	const FMantleConfig Defaults = MantleRules::MakeDefaultConfig();
	FMantleConfig FromFile = MantleRules::MakeDefaultConfig();
	TArray<FString> Unknown;
	TestTrue(TEXT("mantle.json parses"), MantleRules::ApplyMantleJson(Text, FromFile, &Unknown));
	TestEqual(TEXT("no unknown params"), Unknown.Num(), 0);
	for (const FString& Key : MantleRules::KnownKeys())
	{
		float File = -1.f;
		float Code = -2.f;
		TestTrue(FString::Printf(TEXT("%s: in the file"), *Key), Text.Contains(FString::Printf(TEXT("\"%s\""), *Key)));
		MantleRules::GetValue(FromFile, Key, File);
		MantleRules::GetValue(Defaults, Key, Code);
		TestEqual(FString::Printf(TEXT("%s: file = code default"), *Key), File, Code);
	}
	TestEqual(TEXT("the clip table: same count"), FromFile.Clips.Num(), Defaults.Clips.Num());
	for (int32 Index = 0; Index < FMath::Min(FromFile.Clips.Num(), Defaults.Clips.Num()); ++Index)
	{
		const FMantleClip& A = FromFile.Clips[Index];
		const FMantleClip& B = Defaults.Clips[Index];
		TestTrue(FString::Printf(TEXT("clip %d: %s = code"), Index, *A.Path), A.Path == B.Path && A.ClassId == B.ClassId && A.Gait == B.Gait && A.Body == B.Body
			&& FMath::IsNearlyEqual(A.RiseCm, B.RiseCm, 0.05f) && FMath::IsNearlyEqual(A.ForwardCm, B.ForwardCm, 0.05f) && FMath::IsNearlyEqual(A.StartCm, B.StartCm));
	}
	for (const FMantleHeightClass& Class : Defaults.Classes)
	{
		const int32 Index = MantleRules::FindClass(FromFile, Class.Id);
		TestTrue(FString::Printf(TEXT("class %s: same action"), *Class.Id.ToString()), Index != INDEX_NONE && FromFile.Classes[Index].Destination == Class.Destination
			&& FromFile.Classes[Index].bEnabled == Class.bEnabled && FromFile.Classes[Index].bMarkedEdgeOnly == Class.bMarkedEdgeOnly
			&& FromFile.Classes[Index].bHandledByVault == Class.bHandledByVault);
	}
	// The height classes come from the level generator's standard.
	FString Heights;
	if (!TestTrue(TEXT("obstacle_heights.json readable"), FFileHelper::LoadFileToString(Heights, *MantleRules::GetHeightsPath())))
	{
		return false;
	}
	FMantleConfig FromHeights = MantleRules::MakeDefaultConfig();
	TestTrue(TEXT("obstacle_heights.json parses"), MantleRules::ApplyHeightsJson(Heights, FromHeights));
	TestEqual(TEXT("the ceiling = wall_no_traverse.min_cm"), FromHeights.CeilingCm, Defaults.CeilingCm);
	for (const FMantleHeightClass& Class : Defaults.Classes)
	{
		const FMantleHeightClass& File = FromHeights.Classes[MantleRules::FindClass(FromHeights, Class.Id)];
		TestEqual(FString::Printf(TEXT("%s nominal = obstacle_heights.json"), *Class.Id.ToString()), File.NominalCm, Class.NominalCm);
		TestEqual(FString::Printf(TEXT("%s tolerance = obstacle_heights.json"), *Class.Id.ToString()), File.ToleranceCm, Class.ToleranceCm);
	}
	FMantleConfig Broken;
	TestFalse(TEXT("broken JSON: false"), MantleRules::ApplyMantleJson(TEXT("{"), Broken));
	TestFalse(TEXT("no params: false"), MantleRules::ApplyMantleJson(TEXT("{\"x\": 1}"), Broken));
	return true;
}

#undef MANTLE_RULES_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
