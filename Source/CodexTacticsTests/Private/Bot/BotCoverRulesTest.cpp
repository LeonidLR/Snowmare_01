#include "Misc/AutomationTest.h"
#include "Bot/BotCoverRules.h"
#include "Bot/BotRouteRules.h"
#include "Bot/CombatLabRunCollector.h"
#include "Characters/SquadAutonomyRules.h"
#include "Data/SquadROE.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// The game designer's decisions of 2026-11-01 night (docs/design/COMBAT_LAB.md section 7.2): (5) the playtest bot takes COVER AT A WALL first
// in a fight (BotCoverRules, the "fight_cover" block, the run report's cover metrics); (6) no crouch / stand jitter of the followers on short
// stops (SquadAutonomyRules stance hysteresis, ROE "hold_crouch_after_sec"). UE-only, no Godot reference.

namespace BotCoverTestLocal
{
	/** A wall from A to B, 40 cm thick, Height cm tall (0 = unknown). */
	FPressureBox CoverWall(const FVector2D& A, const FVector2D& B, float Height = 300.f)
	{
		FPressureBox Box;
		Box.Center = (A + B) * 0.5;
		Box.AxisX = (B - A).GetSafeNormal();
		Box.HalfSize = FVector2D((B - A).Size() * 0.5, 20.0);
		Box.HeightCm = Height;
		return Box;
	}

	FCombatLabSample CoverSample(float Time, bool bOrdered, bool bAtWall)
	{
		FCombatLabSample Out;
		Out.Time = Time;
		Out.Formation = TEXT("wedge");
		Out.bInFight = true;
		Out.bCoverOrdered = bOrdered;
		Out.bAtWall = bAtWall;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotCoverPickTest, "CodexTactics.Bot.Cover.PickWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBotCoverPickTest::RunTest(const FString&)
{
	using namespace BotCoverTestLocal;
	const FBotCoverParams Params;
	// A wall along X at y = 500 between the squad (south, at the origin) and the threat (north, 20 m).
	const TArray<FPressureBox> One = { CoverWall(FVector2D(-500.0, 500.0), FVector2D(500.0, 500.0)) };
	const FVector2D Leader(0.0, 0.0);
	const FVector2D Threat(200.0, 2500.0);
	const TOptional<FBotCoverChoice> Choice = BotCoverRules::PickWall(One, Leader, Threat, Params);
	if (TestTrue(TEXT("A wall between the squad and the threat is chosen"), Choice.IsSet()))
	{
		TestTrue(TEXT("... its NEAR face (the squad's side, y = 480)"), Choice->FacePoint.Equals(FVector2D(0.0, 480.0), 0.5));
		TestTrue(TEXT("... the face's normal points back at the squad"), Choice->Normal.Equals(FVector2D(0.0, -1.0), 0.001));
		TestEqual(TEXT("... 4.8 m from the leader"), Choice->DistanceCm, 480.f, 0.5f);
		TestTrue(TEXT("... facing the threat squarely"), Choice->FacingCos > 0.95f);
	}
	// The threat on the squad's side: the wall's FAR face is the cover - a walk round its end (counted as such).
	const TOptional<FBotCoverChoice> Far = BotCoverRules::PickWall(One, Leader, FVector2D(0.0, -2000.0), Params);
	TestTrue(TEXT("The threat on the squad's side: the far face, round the wall's end"), Far.IsSet() && Far->Normal.Equals(FVector2D(0.0, 1.0), 0.001)
		&& Far->DistanceCm > 1000.f);
	TestFalse(TEXT("... unless that walk round a long wall (20 m) exceeds the search"),
		BotCoverRules::PickWall({ CoverWall(FVector2D(-1000.0, 500.0), FVector2D(1000.0, 500.0)) }, Leader, FVector2D(0.0, -2000.0), Params).IsSet());
	TestFalse(TEXT("The threat off to the side (along the wall): it does not face it"),
		BotCoverRules::PickWall(One, Leader, FVector2D(3000.0, 600.0), Params).IsSet());
	TestFalse(TEXT("The threat right behind the wall (< 4 m): no cover in its lap"),
		BotCoverRules::PickWall(One, Leader, FVector2D(0.0, 700.0), Params).IsSet());
	TestFalse(TEXT("A wall 20 m away (search 15 m): none"),
		BotCoverRules::PickWall({ CoverWall(FVector2D(-500.0, 2000.0), FVector2D(500.0, 2000.0)) }, Leader, FVector2D(0.0, 4500.0), Params).IsSet());
	TestFalse(TEXT("A knee-high wall (60 cm): no cover"),
		BotCoverRules::PickWall({ CoverWall(FVector2D(-500.0, 500.0), FVector2D(500.0, 500.0), 60.f) }, Leader, Threat, Params).IsSet());
	TestFalse(TEXT("A 1.5 m post: too short for the squad"),
		BotCoverRules::PickWall({ CoverWall(FVector2D(-75.0, 500.0), FVector2D(75.0, 500.0)) }, Leader, Threat, Params).IsSet());
	// Two walls between: the nearer one wins; the clicked point stays inside the wall's ends.
	const TArray<FPressureBox> Two = { CoverWall(FVector2D(-500.0, 1200.0), FVector2D(500.0, 1200.0)), CoverWall(FVector2D(300.0, 500.0), FVector2D(1300.0, 500.0)) };
	const TOptional<FBotCoverChoice> Near = BotCoverRules::PickWall(Two, Leader, Threat, Params);
	if (TestTrue(TEXT("Two walls: one chosen"), Near.IsSet()))
	{
		TestEqual(TEXT("... the nearer one"), Near->Box, 1);
		TestEqual(TEXT("... the click 60 cm inside its west end (x = 360)"), Near->FacePoint.X, 360.0, 0.5);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotCoverParseTest, "CodexTactics.Bot.Cover.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBotCoverParseTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Root;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT(R"({"points": [{"at": [0, 0]}, {"at": [500, 0]}],
		"fight_cover": {"enabled": false, "search_cm": 2000, "hold_sec": 30, "min_wall_cm": 250, "min_height_cm": 100, "min_threat_cm": 300, "min_facing_cos": 0.7}})")), Root);
	FBotRouteConfig Config;
	FString Error;
	if (!TestTrue(TEXT("parses"), Root.IsValid() && BotRouteRules::Parse(*Root, Config, &Error)))
	{
		return false;
	}
	TestFalse(TEXT("fight_cover.enabled"), Config.Cover.bEnabled);
	TestEqual(TEXT("search"), Config.Cover.SearchCm, 2000.f);
	TestEqual(TEXT("hold"), Config.Cover.HoldSeconds, 30.f);
	TestEqual(TEXT("min wall"), Config.Cover.MinWallLengthCm, 250.f);
	TestEqual(TEXT("min height"), Config.Cover.MinHeightCm, 100.f);
	TestEqual(TEXT("min threat"), Config.Cover.MinThreatCm, 300.f);
	TestEqual(TEXT("facing"), Config.Cover.MinFacingCos, 0.7f);
	TSharedPtr<FJsonObject> Bare;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT(R"({"points": [{"at": [0, 0]}, {"at": [500, 0]}]})")), Bare);
	FBotRouteConfig Defaults;
	TestTrue(TEXT("absent: cover on by default (decision 5), 15 m, 45 s"), Bare.IsValid() && BotRouteRules::Parse(*Bare, Defaults, &Error)
		&& Defaults.Cover.bEnabled && Defaults.Cover.SearchCm == 1500.f && Defaults.Cover.HoldSeconds == 45.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotCoverReportTest, "CodexTactics.Bot.CombatLab.Cover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBotCoverReportTest::RunTest(const FString&)
{
	using namespace BotCoverTestLocal;
	FCombatLabRunCollector Collector;
	Collector.Begin(0.f);
	float Time = 0.f;
	// A cover order, 2 s on the way to the wall, 5 s at it; the order ends; a second order that never reaches its wall.
	for (int32 Step = 0; Step < 10; ++Step)
	{
		Collector.AddSample(CoverSample(Time += 0.1f, false, false));
	}
	for (int32 Step = 0; Step < 20; ++Step)
	{
		Collector.AddSample(CoverSample(Time += 0.1f, true, false));
	}
	for (int32 Step = 0; Step < 51; ++Step)
	{
		Collector.AddSample(CoverSample(Time += 0.1f, true, true));
	}
	for (int32 Step = 0; Step < 10; ++Step)
	{
		Collector.AddSample(CoverSample(Time += 0.1f, false, false));
	}
	for (int32 Step = 0; Step < 10; ++Step)
	{
		Collector.AddSample(CoverSample(Time += 0.1f, true, false));
	}
	Collector.End(Time);
	TestEqual(TEXT("Two cover orders"), Collector.GetCoverOrders(), 2);
	TestEqual(TEXT("One cover taken (the squad got to its wall once)"), Collector.GetCoverTaken(), 1);
	TestEqual(TEXT("5 s at the wall"), Collector.GetCoverSeconds(), 5.f, 0.05f);
	FBotRouteConfig RouteConfig;
	RouteConfig.Points.SetNum(2);
	RouteConfig.Points[1].At = FVector2D(1000.0, 0.0);
	FBotRouteTracker Route;
	Route.Reset(RouteConfig, 0.f, FVector2D::ZeroVector);
	const TSharedRef<FJsonObject> Record = CombatLabRules::MakeRunRecord(FCombatLabRunMeta(), Route, Collector, FVector2D::ZeroVector);
	const TSharedPtr<FJsonObject>* Cover = nullptr;
	if (TestTrue(TEXT("The run record has a \"cover\" block"), Record->TryGetObjectField(TEXT("cover"), Cover) && Cover))
	{
		TestEqual(TEXT("cover.taken"), static_cast<int32>((*Cover)->GetNumberField(TEXT("taken"))), 1);
		TestEqual(TEXT("cover.orders"), static_cast<int32>((*Cover)->GetNumberField(TEXT("orders"))), 2);
		TestEqual(TEXT("cover.seconds"), (*Cover)->GetNumberField(TEXT("seconds")), 5.0, 0.05);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadStanceHysteresisTest, "CodexTactics.Characters.SquadAutonomy.StanceHysteresis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadStanceHysteresisTest::RunTest(const FString&)
{
	using namespace SquadAutonomyRules;
	const FSquadROE ROE;
	TestEqual(TEXT("A follower crouches only after 4 s standing still (decision 6)"), ROE.HoldCrouchAfterSeconds, 4.f);
	// A follower on a 3 s halt in a fight: no crouch; after 4 s: the ROE's crouch.
	float Still = 0.f;
	for (int32 Step = 0; Step < 10; ++Step)
	{
		Still = StepStillSeconds(false, Still, 0.3f);
	}
	TestFalse(TEXT("3 s still: no crouch"), ShouldTakeHaltStance(EOperativeStance::Crouching, EOperativeStance::Standing, Still, ROE.HoldCrouchAfterSeconds, true));
	for (int32 Step = 0; Step < 4; ++Step)
	{
		Still = StepStillSeconds(false, Still, 0.3f);
	}
	TestTrue(TEXT("4.2 s still: crouches as before"), ShouldTakeHaltStance(EOperativeStance::Crouching, EOperativeStance::Standing, Still, ROE.HoldCrouchAfterSeconds, true));
	TestEqual(TEXT("Any move resets the clock"), StepStillSeconds(true, Still, 0.3f), 0.f);
	TestTrue(TEXT("Not delayed (cover, a sniper on him): at once"), ShouldTakeHaltStance(EOperativeStance::Crouching, EOperativeStance::Standing, 0.f, 4.f, false));
	TestTrue(TEXT("Rising is never delayed"), ShouldTakeHaltStance(EOperativeStance::Standing, EOperativeStance::Crouching, 0.f, 4.f, true));
	TestFalse(TEXT("Same stance: nothing to do"), ShouldTakeHaltStance(EOperativeStance::Crouching, EOperativeStance::Crouching, 10.f, 4.f, true));
	TestTrue(TEXT("hold_crouch_after_sec 0: at once (the old behaviour)"), ShouldTakeHaltStance(EOperativeStance::Crouching, EOperativeStance::Standing, 0.f, 0.f, true));
	// The hold crouch ends with the next move: up at once when the leader walks upright.
	TestTrue(TEXT("Hold crouch, the leader standing: he rises to move"), ShouldRiseFromHoldCrouch(true, false, EOperativeStance::Standing));
	TestFalse(TEXT("... not when the leader himself is crouched (the player's stance)"), ShouldRiseFromHoldCrouch(true, false, EOperativeStance::Crouching));
	TestFalse(TEXT("... not while he carries something"), ShouldRiseFromHoldCrouch(true, true, EOperativeStance::Standing));
	TestFalse(TEXT("... not a crouch of his own (the player's / a squad order)"), ShouldRiseFromHoldCrouch(false, false, EOperativeStance::Standing));
	// The ROE file key.
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT(R"({"hold_crouch_after_sec": 6.5})")), Json);
	TestEqual(TEXT("squad_roe.json hold_crouch_after_sec"), Json.IsValid() ? SquadROE::FromJson(*Json).HoldCrouchAfterSeconds : -1.f, 6.5f);
	TestEqual(TEXT("... round trip"), SquadROE::FromJson(*SquadROE::ToJson(ROE)).HoldCrouchAfterSeconds, 4.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadSoloGuardCrouchDelayTest, "CodexTactics.Characters.SquadAutonomy.SoloGuardCrouchDelay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadSoloGuardCrouchDelayTest::RunTest(const FString&)
{
	// The game designer's review of ambush v3 (2026-11-02): while the leader is on a tower (Solo), the two waiting operatives crouched at
	// once - Solo's guard crouch now waits the same ROE hold_crouch_after_sec standing still (no tunable of its own).
	using namespace SquadAutonomyRules;
	const float After = FSquadROE().HoldCrouchAfterSeconds;
	TestEqual(TEXT("Solo begins, a standing follower: he waits"), StartSoloGuardCrouch(EOperativeStance::Standing, After), ESoloGuardCrouch::Wait);
	TestEqual(TEXT("... hold_crouch_after_sec 0: at once (the old behaviour)"), StartSoloGuardCrouch(EOperativeStance::Standing, 0.f), ESoloGuardCrouch::Crouch);
	TestEqual(TEXT("A prone follower kneels at once as before (a rise is never delayed)"), StartSoloGuardCrouch(EOperativeStance::Prone, After),
		ESoloGuardCrouch::Crouch);
	TestEqual(TEXT("A crouched one keeps his crouch as the squad's at once"), StartSoloGuardCrouch(EOperativeStance::Crouching, After), ESoloGuardCrouch::Crouch);

	// The wait: the leader climbs (3 s), the two stand; 4 s standing still -> the guard crouch.
	float Still = 0.f;
	for (int32 Step = 0; Step < 10; ++Step)
	{
		Still = StepStillSeconds(false, Still, 0.3f);
	}
	TestEqual(TEXT("3 s still: still standing"), StepSoloGuardCrouch(true, false, false, EOperativeStance::Standing, Still, After), ESoloGuardCrouch::Wait);
	TestFalse(TEXT("... the same delay as the autonomy's hold crouch"),
		ShouldTakeHaltStance(EOperativeStance::Crouching, EOperativeStance::Standing, Still, After, true));
	for (int32 Step = 0; Step < 4; ++Step)
	{
		Still = StepStillSeconds(false, Still, 0.3f);
	}
	TestEqual(TEXT("4.2 s still: he crouches"), StepSoloGuardCrouch(true, false, false, EOperativeStance::Standing, Still, After), ESoloGuardCrouch::Crouch);
	TestEqual(TEXT("A step (a shove, a slide) restarts the clock"),
		StepSoloGuardCrouch(true, false, false, EOperativeStance::Standing, StepStillSeconds(true, Still, 0.3f), After), ESoloGuardCrouch::Wait);
	TestEqual(TEXT("A tunable 6 s: 4.2 s is not enough"), StepSoloGuardCrouch(true, false, false, EOperativeStance::Standing, Still, 6.f), ESoloGuardCrouch::Wait);

	// The wait ends without a crouch.
	TestEqual(TEXT("Regroup / Solo off, or he became the leader: dropped"), StepSoloGuardCrouch(false, false, false, EOperativeStance::Standing, 10.f, After),
		ESoloGuardCrouch::Drop);
	TestEqual(TEXT("An order of his own: he holds where it ends, standing (rev 5g)"),
		StepSoloGuardCrouch(true, true, false, EOperativeStance::Standing, 10.f, After), ESoloGuardCrouch::Drop);
	TestEqual(TEXT("The trap hold's cover / a group cover prone: his own stance"),
		StepSoloGuardCrouch(true, false, true, EOperativeStance::Standing, 10.f, After), ESoloGuardCrouch::Drop);
	TestEqual(TEXT("Someone else crouched / laid him down meanwhile: left as he is"),
		StepSoloGuardCrouch(true, false, false, EOperativeStance::Prone, 10.f, After), ESoloGuardCrouch::Drop);
	TestEqual(TEXT("... already crouched: nothing to do"), StepSoloGuardCrouch(true, false, false, EOperativeStance::Crouching, 10.f, After),
		ESoloGuardCrouch::Drop);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
