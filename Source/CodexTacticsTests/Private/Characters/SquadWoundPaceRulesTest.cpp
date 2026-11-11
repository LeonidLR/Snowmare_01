// CodexTactics.Squad.WoundPace.* - the formation walks at its most wounded member's pace (the game designer's decision "a" 2026-11-11,
// SquadWoundPaceRules): the pace from the slowest wounded member (light x0.65 / heavy x0.45), the critically wounded and the members out of the
// formation left out, the wounded leader keeps his own pace, the wounded follower's move capped at his allowed walk, the sprint limit, the
// fast regroup never above the man's allowed speed, the squad_wound_pace.json tuning.

#include "Misc/AutomationTest.h"
#include "Characters/SquadRegroupRules.h"
#include "Characters/SquadWoundPaceRules.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

#define SQUAD_WOUND_PACE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Squad.WoundPace." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace SquadWoundPaceTestLocal
{
	/** The operative's walk (MovementConfig.WalkSpeed), the light tier (WoundedSpeedMultiplier) and the heavy tier (heavy_speed_multiplier). */
	constexpr float PaceWalk = 220.f;
	constexpr float PaceLight = PaceWalk * 0.65f;
	constexpr float PaceHeavy = PaceWalk * 0.45f;
	/** pace.patrol / move.catchup_mult / move.backward_mult (formation_config.json). */
	constexpr float PacePatrol = 0.8f;
	constexpr float PaceCatchup = 1.25f;
	constexpr float PaceBackward = 0.9f;

	FSquadWoundPaceMember PaceMember(float Speed, bool bWounded, bool bCritical = false, bool bInFormation = true)
	{
		FSquadWoundPaceMember Out;
		Out.AllowedSpeed = Speed;
		Out.bWounded = bWounded;
		Out.bCritical = bCritical;
		Out.bInFormation = bInFormation;
		return Out;
	}
}

SQUAD_WOUND_PACE_TEST(FSquadWoundPaceFromSlowestTest, "PaceFromMostWounded")
bool FSquadWoundPaceFromSlowestTest::RunTest(const FString&)
{
	using namespace SquadWoundPaceRules;
	using namespace SquadWoundPaceTestLocal;
	const FSquadWoundPaceTuning Tuning;
	TestEqual(TEXT("Default pace share = pace.patrol 0.8"), Tuning.PaceShare, 0.8f);
	// Everyone fit: no wound pace (the old patrol pace, leader multiplier 0.8).
	const TArray<FSquadWoundPaceMember> Fit = { PaceMember(PaceWalk, false), PaceMember(PaceWalk, false), PaceMember(PaceWalk, false) };
	TestFalse(TEXT("Nobody wounded: no wound pace"), SquadPace(Fit, Tuning, true).IsSet());
	TestEqual(TEXT("Nobody wounded: the leader walks at pace.patrol"), LeaderPaceMultiplier(PacePatrol, PaceWalk, SquadPace(Fit, Tuning, true)), PacePatrol);

	// A follower at 40 % HP (light): the squad walks at 0.8 x 143 = 114.4 cm/s - the leader x0.52 instead of x0.8 (176 cm/s).
	const TArray<FSquadWoundPaceMember> OneLight = { PaceMember(PaceWalk, false), PaceMember(PaceWalk, false), PaceMember(PaceLight, true) };
	const TOptional<FSquadWoundPace> LightPace = SquadPace(OneLight, Tuning, true);
	TestTrue(TEXT("Light follower: a wound pace"), LightPace.IsSet());
	if (LightPace.IsSet())
	{
		TestEqual(TEXT("Light follower sets it"), LightPace->SlowestIndex, 2);
		TestEqual(TEXT("Light: 0.8 x 143 cm/s"), LightPace->CapSpeed, PacePatrol * PaceLight, 0.01f);
		const float Mult = LeaderPaceMultiplier(PacePatrol, PaceWalk, LightPace);
		TestEqual(TEXT("Light: the fit leader walks at 114.4 cm/s"), Mult * PaceWalk, PacePatrol * PaceLight, 0.01f);
		// The wounded man keeps the fit men's margins: forward (his top = his walk) 1.25 x the slot speed, backwards (x0.9) 1.125 x.
		TestEqual(TEXT("Light: forward margin = catchup_mult"), PaceLight / LightPace->CapSpeed, PaceCatchup, 0.001f);
		TestEqual(TEXT("Light: backward margin = backward_mult / pace.patrol"), PaceLight * PaceBackward / LightPace->CapSpeed, PaceBackward / PacePatrol, 0.001f);
	}
	// 25 % HP (heavy) and 40 % both in: the heavier one sets it (0.8 x 99 = 79.2 cm/s).
	const TArray<FSquadWoundPaceMember> Mixed = { PaceMember(PaceWalk, false), PaceMember(PaceLight, true), PaceMember(PaceHeavy, true) };
	const TOptional<FSquadWoundPace> HeavyPace = SquadPace(Mixed, Tuning, true);
	TestTrue(TEXT("Heavy + light: a wound pace"), HeavyPace.IsSet());
	if (HeavyPace.IsSet())
	{
		TestEqual(TEXT("Heavy sets it"), HeavyPace->SlowestIndex, 2);
		TestEqual(TEXT("Heavy: 0.8 x 99 cm/s"), HeavyPace->CapSpeed, PacePatrol * PaceHeavy, 0.01f);
	}
	// A fit but slow member (crouched) does not set the pace - only the wound does (the old crouch pacing unchanged).
	const TArray<FSquadWoundPaceMember> Crouched = { PaceMember(PaceWalk, false), PaceMember(120.f, false), PaceMember(PaceLight, true) };
	TestEqual(TEXT("Fit crouched member: not the pace"), SquadPace(Crouched, Tuning, true)->SlowestIndex, 2);
	// The rule off: never a pace (the old behaviour).
	TestFalse(TEXT("Rule off: no wound pace"), SquadPace(Mixed, Tuning, false).IsSet());
	return true;
}

SQUAD_WOUND_PACE_TEST(FSquadWoundPaceExcludedTest, "CriticalAndOutOfFormationExcluded")
bool FSquadWoundPaceExcludedTest::RunTest(const FString&)
{
	using namespace SquadWoundPaceRules;
	using namespace SquadWoundPaceTestLocal;
	const FSquadWoundPaceTuning Tuning;
	// The critically wounded crawls (~60 cm/s): the squad does not crawl with him.
	const TArray<FSquadWoundPaceMember> Critical = { PaceMember(PaceWalk, false), PaceMember(PaceWalk, false), PaceMember(60.f, true, true) };
	TestFalse(TEXT("Only a critical member wounded: no wound pace"), SquadPace(Critical, Tuning, true).IsSet());
	const TArray<FSquadWoundPaceMember> CriticalAndLight = { PaceMember(PaceWalk, false), PaceMember(PaceLight, true), PaceMember(60.f, true, true) };
	TestEqual(TEXT("Critical + light: the light one sets it"), SquadPace(CriticalAndLight, Tuning, true)->CapSpeed, PacePatrol * PaceLight, 0.01f);
	// Not in the formation (a fast regroup, an order of his own, panic, aboard): no pace.
	const TArray<FSquadWoundPaceMember> Away = { PaceMember(PaceWalk, false), PaceMember(PaceWalk, false), PaceMember(PaceHeavy, true, false, false) };
	TestFalse(TEXT("Wounded but out of the formation: no wound pace"), SquadPace(Away, Tuning, true).IsSet());
	// A speed of 0 (no data) never sets it.
	const TArray<FSquadWoundPaceMember> Zero = { PaceMember(PaceWalk, false), PaceMember(0.f, true) };
	TestFalse(TEXT("Allowed speed 0: ignored"), SquadPace(Zero, Tuning, true).IsSet());
	return true;
}

SQUAD_WOUND_PACE_TEST(FSquadWoundPaceLeaderTest, "WoundedLeaderKeepsHisPace")
bool FSquadWoundPaceLeaderTest::RunTest(const FString&)
{
	using namespace SquadWoundPaceRules;
	using namespace SquadWoundPaceTestLocal;
	const FSquadWoundPaceTuning Tuning;
	// The leader light (143), fit followers: the cap 0.8 x 143 = his own patrol pace - unchanged (x0.8 of his walk, 114.4 cm/s).
	const TArray<FSquadWoundPaceMember> LeaderLight = { PaceMember(PaceLight, true), PaceMember(PaceWalk, false), PaceMember(PaceWalk, false) };
	const TOptional<FSquadWoundPace> Pace = SquadPace(LeaderLight, Tuning, true);
	TestEqual(TEXT("Wounded leader sets it"), Pace.IsSet() ? Pace->SlowestIndex : -1, 0);
	TestEqual(TEXT("Wounded leader: his own patrol pace (x0.8)"), LeaderPaceMultiplier(PacePatrol, PaceLight, Pace), PacePatrol, 0.001f);
	// The leader light, a follower heavy: the leader slows further to the heavy man's pace (0.8 x 99 / 143 = 0.554).
	const TArray<FSquadWoundPaceMember> LeaderLightFollowerHeavy = { PaceMember(PaceLight, true), PaceMember(PaceHeavy, true), PaceMember(PaceWalk, false) };
	const float Mult = LeaderPaceMultiplier(PacePatrol, PaceLight, SquadPace(LeaderLightFollowerHeavy, Tuning, true));
	TestEqual(TEXT("Wounded leader + heavy follower: the heavy man's pace"), Mult * PaceLight, PacePatrol * PaceHeavy, 0.01f);
	// Never faster than pace.patrol, never 0.
	TestTrue(TEXT("Multiplier within (0, 0.8]"), Mult > 0.f && Mult <= PacePatrol);
	TestEqual(TEXT("Leader speed unknown: pace.patrol"), LeaderPaceMultiplier(PacePatrol, 0.f, SquadPace(LeaderLightFollowerHeavy, Tuning, true)), PacePatrol);
	return true;
}

SQUAD_WOUND_PACE_TEST(FSquadWoundPaceFollowerCapTest, "WoundedFollowerCappedAtHisWalk")
bool FSquadWoundPaceFollowerCapTest::RunTest(const FString&)
{
	using namespace SquadWoundPaceRules;
	using namespace SquadWoundPaceTestLocal;
	FSquadWoundPaceTuning Tuning;
	// FollowerMove multiplies the top by catchup_mult: the wounded man is asked with walk / 1.25 so his move tops out at his walk.
	TestEqual(TEXT("Light: top x catchup = his walk"), FollowerTopSpeed(PaceLight, true, PaceCatchup, Tuning, true) * PaceCatchup, PaceLight, 0.01f);
	TestEqual(TEXT("Heavy: top x catchup = his walk"), FollowerTopSpeed(PaceHeavy, true, PaceCatchup, Tuning, true) * PaceCatchup, PaceHeavy, 0.01f);
	TestEqual(TEXT("Fit: unchanged"), FollowerTopSpeed(PaceWalk, false, PaceCatchup, Tuning, true), PaceWalk);
	TestEqual(TEXT("Rule off: unchanged (the old x1.25 push)"), FollowerTopSpeed(PaceLight, true, PaceCatchup, Tuning, false), PaceLight);
	// A fixed formation speed (the walk to a slot on another level, pace.walk_speed 220) is capped at his walk.
	TestEqual(TEXT("Fixed 220 for the light man: 143"), CapFixedSpeed(220.f, PaceLight, true, Tuning, true), PaceLight, 0.01f);
	TestEqual(TEXT("Fixed 220 for a fit man: 220"), CapFixedSpeed(220.f, PaceWalk, false, Tuning, true), 220.f);
	Tuning.bCapWoundedFollower = false;
	TestEqual(TEXT("cap_wounded_follower 0: unchanged"), FollowerTopSpeed(PaceLight, true, PaceCatchup, Tuning, true), PaceLight);
	TestEqual(TEXT("cap_wounded_follower 0: fixed unchanged"), CapFixedSpeed(220.f, PaceLight, true, Tuning, true), 220.f);
	return true;
}

SQUAD_WOUND_PACE_TEST(FSquadWoundPaceSprintTest, "SprintEndsWhileWoundedInFormation")
bool FSquadWoundPaceSprintTest::RunTest(const FString&)
{
	using namespace SquadWoundPaceRules;
	using namespace SquadWoundPaceTestLocal;
	FSquadWoundPaceTuning Tuning;
	const TArray<FSquadWoundPaceMember> OneLight = { PaceMember(PaceWalk, false), PaceMember(PaceLight, true) };
	const TOptional<FSquadWoundPace> Pace = SquadPace(OneLight, Tuning, true);
	TestTrue(TEXT("Sprinting leader + wounded member: the sprint ends"), EndsLeaderSprint(true, Pace, Tuning));
	TestFalse(TEXT("Walking leader: nothing to end"), EndsLeaderSprint(false, Pace, Tuning));
	TestFalse(TEXT("Nobody wounded: the sprint stays"), EndsLeaderSprint(true, TOptional<FSquadWoundPace>(), Tuning));
	Tuning.bLimitSprint = false;
	TestFalse(TEXT("limit_sprint 0: the sprint stays"), EndsLeaderSprint(true, Pace, Tuning));
	return true;
}

SQUAD_WOUND_PACE_TEST(FSquadWoundPaceRegroupTest, "FastRegroupNeverAboveAllowed")
bool FSquadWoundPaceRegroupTest::RunTest(const FString&)
{
	// The fast regroup on B (> 12 m) stays as it is: a wounded man is never dragged past his allowed speed - the light tier runs its wounded run
	// (480 x 0.65 = 312, the injured run the tier allows), the heavy tier (no run) walks his own heavy walk, nobody wounded sprints.
	using namespace SquadRegroupRules;
	using namespace SquadWoundPaceTestLocal;
	const FSquadRegroupTuning Tuning;
	for (const bool bCombat : { false, true })
	{
		const ESquadRegroupPace LightPace = ChoosePace(bCombat, true, false, Tuning, /*bMayRun*/ true);
		TestEqual(TEXT("Light: the wounded run"), static_cast<int32>(LightPace), static_cast<int32>(ESquadRegroupPace::WoundedRun));
		TestTrue(TEXT("Light: never above 480 x 0.65"), PaceSpeed(LightPace, PaceLight, 0.65f, 1.f, Tuning) <= Tuning.RunSpeed * 0.65f + 0.01f);
		const ESquadRegroupPace HeavyPace = ChoosePace(bCombat, true, false, Tuning, /*bMayRun*/ false);
		TestEqual(TEXT("Heavy: his walk"), static_cast<int32>(HeavyPace), static_cast<int32>(ESquadRegroupPace::WoundedWalk));
		TestEqual(TEXT("Heavy: exactly his heavy walk"), PaceSpeed(HeavyPace, PaceHeavy, 0.65f, 1.f, Tuning), PaceHeavy);
		// Even if he could sprint (bCanSprint true by a stale flag): the wound wins.
		TestNotEqual(TEXT("Wounded: never the sprint"), static_cast<int32>(ChoosePace(bCombat, true, true, Tuning)), static_cast<int32>(ESquadRegroupPace::Sprint));
	}
	return true;
}

SQUAD_WOUND_PACE_TEST(FSquadWoundPaceTuningTest, "TuningAndDataFile")
bool FSquadWoundPaceTuningTest::RunTest(const FString&)
{
	const FSquadWoundPaceTuning Defaults;
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("enabled"), 0);
	Json->SetNumberField(TEXT("pace_share"), 5.0);
	Json->SetBoolField(TEXT("limit_sprint"), false);
	Json->SetNumberField(TEXT("cap_wounded_follower"), 0);
	const FSquadWoundPaceTuning Read = SquadWoundPaceTuning::FromJson(*Json);
	TestFalse(TEXT("enabled 0"), Read.bEnabled);
	TestEqual(TEXT("pace_share clamped to 1"), Read.PaceShare, 1.f);
	TestFalse(TEXT("limit_sprint false"), Read.bLimitSprint);
	TestFalse(TEXT("cap_wounded_follower 0"), Read.bCapWoundedFollower);
	const FSquadWoundPaceTuning Empty = SquadWoundPaceTuning::FromJson(*MakeShared<FJsonObject>());
	TestTrue(TEXT("Missing keys keep the defaults"), Empty.bEnabled == Defaults.bEnabled && Empty.PaceShare == Defaults.PaceShare
		&& Empty.bLimitSprint == Defaults.bLimitSprint && Empty.bCapWoundedFollower == Defaults.bCapWoundedFollower);

	// The shipped file mirrors the C++ defaults (the rule on by default, pace share 0.8).
	FString Text;
	TSharedPtr<FJsonObject> File;
	if (!TestTrue(TEXT("squad_wound_pace.json loads"), FFileHelper::LoadFileToString(Text, *SquadWoundPaceTuning::GetDefaultPath())
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), File) && File.IsValid()))
	{
		return false;
	}
	const FSquadWoundPaceTuning Shipped = SquadWoundPaceTuning::FromJson(*File, FSquadWoundPaceTuning());
	TestTrue(TEXT("File: enabled (default on)"), Shipped.bEnabled);
	TestEqual(TEXT("File: pace_share = the default"), Shipped.PaceShare, Defaults.PaceShare);
	TestTrue(TEXT("File: limit_sprint"), Shipped.bLimitSprint);
	TestTrue(TEXT("File: cap_wounded_follower"), Shipped.bCapWoundedFollower);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
