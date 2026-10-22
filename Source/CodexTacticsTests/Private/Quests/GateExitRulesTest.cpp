#include "Misc/AutomationTest.h"
#include "Data/LevelJsonRules.h"
#include "Data/WaveConfigTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Quests/GateExitRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// 2026-10-22 (game designer: "after the fight, if we go through the fully opened gate, do a fade and that's the end of the level"):
// the exit line derived from the blast gate's transform and DoorSize, the crossing latch, "everybody alive is out", the fade.
// UE-only (no Godot reference).

#define GATE_EXIT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Quests.GateExit." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace GateExitTestLocal
{
	/** The lake's door: in the east wall, yawed 0 (local +X = world +X), 7 m wide (two 3.5 m leaves). */
	FTransform LakeGate()
	{
		return FTransform(FRotator::ZeroRotator, FVector(32000.f, -1500.f, -1250.f));
	}
}

GATE_EXIT_TEST(FGateExitLineTest, "Line")
bool FGateExitLineTest::RunTest(const FString&)
{
	using namespace GateExitTestLocal;
	const FTransform Gate = LakeGate();
	const FVector Yard = Gate.GetLocation() + FVector(-2500.f, 300.f, 0.f);
	TestEqual(TEXT("auto: out is the side away from the yard's CombatStart"), GateExitRules::ResolveOutwardSign(EGateExitSide::Auto, Gate, &Yard), 1);
	const FVector Road = Gate.GetLocation() + FVector(2500.f, 0.f, 0.f);
	TestEqual(TEXT("... and the other way round"), GateExitRules::ResolveOutwardSign(EGateExitSide::Auto, Gate, &Road), -1);
	TestEqual(TEXT("auto without a reference: +X"), GateExitRules::ResolveOutwardSign(EGateExitSide::Auto, Gate, nullptr), 1);
	TestEqual(TEXT("-x forced"), GateExitRules::ResolveOutwardSign(EGateExitSide::NegativeX, Gate, &Yard), -1);

	// A gate turned 90 deg: its local +X is world +Y.
	const FTransform Turned(FRotator(0.f, 90.f, 0.f), FVector(100.f, 200.f, 0.f));
	const FGateExitLine TurnedLine = GateExitRules::MakeLine(Turned, 350.f, 1);
	TestTrue(TEXT("turned gate: out = world +Y"), TurnedLine.Outward.Equals(FVector(0.f, 1.f, 0.f), 1e-3f));
	TestEqual(TEXT("... 2 m along +Y is 2 m out"), GateExitRules::SignedDistance(TurnedLine, FVector(100.f, 400.f, 50.f)), 200.f, 0.5f);
	TestEqual(TEXT("... height does not count"), GateExitRules::SignedDistance(TurnedLine, FVector(100.f, 400.f, 9000.f)), 200.f, 0.5f);

	FGateExitConfig Config;
	const FGateExitLine Line = GateExitRules::MakeLine(Gate, 350.f, 1);
	const FVector O = Gate.GetLocation();
	TestTrue(TEXT("2 m out in the doorway: in the exit zone"), GateExitRules::IsInExitZone(Line, Config, O + FVector(200.f, 0.f, 90.f)));
	TestFalse(TEXT("in the doorway itself (1 m): not yet across"), GateExitRules::IsInExitZone(Line, Config, O + FVector(100.f, 0.f, 90.f)));
	TestFalse(TEXT("inside the yard: no"), GateExitRules::IsInExitZone(Line, Config, O + FVector(-300.f, 0.f, 90.f)));
	TestTrue(TEXT("out at the doorway's edge + slack (6 m aside): yes"), GateExitRules::IsInExitZone(Line, Config, O + FVector(400.f, 600.f, 90.f)));
	TestFalse(TEXT("out but 20 m along the wall (round the back gap): not through the gate"), GateExitRules::IsInExitZone(Line, Config, O + FVector(400.f, 2000.f, 90.f)));
	TestFalse(TEXT("beyond the zone's depth (20 m out): not counted by itself"), GateExitRules::IsInExitZone(Line, Config, O + FVector(2000.f, 0.f, 90.f)));
	return true;
}

GATE_EXIT_TEST(FGateExitLatchTest, "CrossingLatch")
bool FGateExitLatchTest::RunTest(const FString&)
{
	using namespace GateExitTestLocal;
	const FGateExitConfig Config;
	const FGateExitLine Line = GateExitRules::MakeLine(LakeGate(), 350.f, 1);
	const FVector O = LakeGate().GetLocation();
	bool bCrossed = false;
	bCrossed = GateExitRules::UpdateCrossed(bCrossed, Line, Config, O + FVector(-500.f, 0.f, 0.f));
	TestFalse(TEXT("walking to the door: not out"), bCrossed);
	bCrossed = GateExitRules::UpdateCrossed(bCrossed, Line, Config, O + FVector(50.f, 0.f, 0.f));
	TestFalse(TEXT("in the doorway: not out"), bCrossed);
	bCrossed = GateExitRules::UpdateCrossed(bCrossed, Line, Config, O + FVector(300.f, 100.f, 0.f));
	TestTrue(TEXT("3 m beyond the door: out"), bCrossed);
	bCrossed = GateExitRules::UpdateCrossed(bCrossed, Line, Config, O + FVector(3000.f, 2500.f, 0.f));
	TestTrue(TEXT("walking on along the road (30 m out, 25 m aside): still out"), bCrossed);
	bCrossed = GateExitRules::UpdateCrossed(bCrossed, Line, Config, O + FVector(-120.f, 0.f, 0.f));
	TestFalse(TEXT("back inside: not out any more"), bCrossed);
	bool bRound = false;
	bRound = GateExitRules::UpdateCrossed(bRound, Line, Config, O + FVector(300.f, 2500.f, 0.f));
	TestFalse(TEXT("outside the wall far from the gate (the hounds' back gap): not counted"), bRound);
	return true;
}

GATE_EXIT_TEST(FGateExitEndTest, "EndRule")
bool FGateExitEndTest::RunTest(const FString&)
{
	auto Squad = [](std::initializer_list<FGateExitMember> Members) { return TArray<FGateExitMember>(Members); };
	const FGateExitMember Out{ true, true };
	const FGateExitMember Inside{ true, false };
	const FGateExitMember DeadInside{ false, false };
	TestFalse(TEXT("nobody out: no fade"), GateExitRules::ShouldEndLevel(Squad({ Inside, Inside, Inside })));
	TestFalse(TEXT("two of three out: no fade while the third is inside"), GateExitRules::ShouldEndLevel(Squad({ Out, Out, Inside })));
	TestTrue(TEXT("all three out: the fade"), GateExitRules::ShouldEndLevel(Squad({ Out, Out, Out })));
	TestTrue(TEXT("a KIA operative inside never has to cross"), GateExitRules::ShouldEndLevel(Squad({ Out, DeadInside, Out })));
	TestFalse(TEXT("... but the living still must"), GateExitRules::ShouldEndLevel(Squad({ Inside, DeadInside, Out })));
	TestFalse(TEXT("no living member: no exit"), GateExitRules::ShouldEndLevel(Squad({ DeadInside })));
	TestFalse(TEXT("an empty squad: no exit"), GateExitRules::ShouldEndLevel(TArray<FGateExitMember>()));
	const TArray<FGateExitMember> Mixed = Squad({ Out, DeadInside, Inside });
	TestEqual(TEXT("counts: required = the living"), GateExitRules::CountRequired(Mixed), 2);
	TestEqual(TEXT("counts: out = the living out"), GateExitRules::CountCrossed(Mixed), 1);

	TestTrue(TEXT("armed: enabled + fully open"), GateExitRules::IsArmed(true, true, false, false));
	TestFalse(TEXT("not while the leaves still slide / shut"), GateExitRules::IsArmed(true, false, false, false));
	TestFalse(TEXT("not on a level without gate_exit"), GateExitRules::IsArmed(false, true, false, false));
	TestFalse(TEXT("not after a failed mission"), GateExitRules::IsArmed(true, true, true, false));
	TestFalse(TEXT("only once"), GateExitRules::IsArmed(true, true, false, true));
	return true;
}

GATE_EXIT_TEST(FGateExitFadeTest, "Fade")
bool FGateExitFadeTest::RunTest(const FString&)
{
	FGateExitConfig Config;
	Config.FadeSeconds = 2.f;
	Config.TitleFadeSeconds = 1.f;
	Config.ReturnToMenuSeconds = 6.f;
	TestEqual(TEXT("fade starts clear"), GateExitRules::FadeAlpha(0.f, Config), 0.f);
	TestTrue(TEXT("half way: half dark"), FMath::IsNearlyEqual(GateExitRules::FadeAlpha(1.f, Config), 0.5f, 0.01f));
	TestEqual(TEXT("black after 2 s"), GateExitRules::FadeAlpha(2.f, Config), 1.f);
	TestEqual(TEXT("the title waits for the black"), GateExitRules::TitleAlpha(1.9f, Config), 0.f);
	TestTrue(TEXT("... then fades in over 1 s"), FMath::IsNearlyEqual(GateExitRules::TitleAlpha(2.5f, Config), 0.5f, 0.01f) && GateExitRules::TitleAlpha(4.f, Config) == 1.f);
	TestFalse(TEXT("the menu waits 6 s on the black"), GateExitRules::ShouldReturnToMenu(7.9f, Config, false));
	TestTrue(TEXT("... then opens"), GateExitRules::ShouldReturnToMenu(8.f, Config, false));
	TestFalse(TEXT("never in a headless check"), GateExitRules::ShouldReturnToMenu(100.f, Config, true));
	Config.ReturnToMenuSeconds = 0.f;
	TestFalse(TEXT("0 = stay on the black screen"), GateExitRules::ShouldReturnToMenu(100.f, Config, false));
	return true;
}

GATE_EXIT_TEST(FGateExitLevelJsonTest, "LevelJson")
bool FGateExitLevelJsonTest::RunTest(const FString&)
{
	FLevelCombatConfig Config;
	FString Error;
	TestTrue(TEXT("no key parses"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"a"})"), TEXT("a"), Config, Error));
	TestFalse(TEXT("default: off"), Config.GateExit.bEnabled);
	TestTrue(TEXT("\"auto\""), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"a","gate_exit":"auto"})"), TEXT("a"), Config, Error)
		&& Config.GateExit.bEnabled && Config.GateExit.Side == EGateExitSide::Auto);
	TestTrue(TEXT("\"-x\""), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"a","gate_exit":"-x"})"), TEXT("a"), Config, Error)
		&& Config.GateExit.bEnabled && Config.GateExit.Side == EGateExitSide::NegativeX);
	TestTrue(TEXT("object form"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"a","gate_exit":{"outward":"+x","fade_seconds":3,"return_to_menu_seconds":0}})"),
		TEXT("a"), Config, Error));
	TestTrue(TEXT("... read"), Config.GateExit.bEnabled && Config.GateExit.Side == EGateExitSide::PositiveX
		&& FMath::IsNearlyEqual(Config.GateExit.FadeSeconds, 3.f) && Config.GateExit.ReturnToMenuSeconds == 0.f);
	TestFalse(TEXT("an unknown side is an error"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"a","gate_exit":"north"})"), TEXT("a"), Config, Error));
	TestTrue(TEXT("... naming it"), Error.Contains(TEXT("north")));

	FString Json;
	TestTrue(TEXT("the lake's level JSON loads"), FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/LevelJson/L_Demo01_A_FrozenLake.json"))));
	FLevelCombatConfig Lake;
	TestTrue(TEXT("... parses"), LevelJsonRules::ParseLevel(Json, TEXT("L_Demo01_A_FrozenLake"), Lake, Error));
	TestTrue(TEXT("the Demo 01 lake ends through its blast door (gate_exit auto)"), Lake.GateExit.bEnabled && Lake.GateExit.Side == EGateExitSide::Auto);
	return true;
}

#undef GATE_EXIT_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
