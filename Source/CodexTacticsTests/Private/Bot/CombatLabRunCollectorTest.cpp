#include "Misc/AutomationTest.h"
#include "Bot/BotRouteRules.h"
#include "Bot/CombatLabRunCollector.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// The combat lab's bot run report (Bot/CombatLabRunCollector.h; Training Ground "Combat lab" part C, game designer 2026-11-01,
// docs/design/COMBAT_LAB.md section 7): contacts, reactions, unwatched strikes, formation changes, wall hugs, stances, damage, rounds,
// climbs, knockdowns - and the run record the Training Ground parses (schema codex.combatlab_run/1).

namespace CombatLabTest
{
	FCombatLabMemberSample LabMember(const TCHAR* Name, EOperativeStance Stance = EOperativeStance::Standing, float Health = 100.f)
	{
		FCombatLabMemberSample Out;
		Out.Name = Name;
		Out.Stance = Stance;
		Out.Health = Health;
		return Out;
	}

	FCombatLabSample LabSample(float Time, const TCHAR* Formation = TEXT("diamond"))
	{
		FCombatLabSample Out;
		Out.Time = Time;
		Out.Formation = Formation;
		Out.Members = { LabMember(TEXT("Commander")), LabMember(TEXT("Engineer")) };
		return Out;
	}

	FCombatLabGroupSample Group(int32 Id, ECombatLabContactKind Kind, bool bSeen = false, bool bEngaged = false, bool bSprung = false, bool bWatched = false)
	{
		FCombatLabGroupSample Out;
		Out.Id = Id;
		Out.Kind = Kind;
		Out.Alive = 1;
		Out.Position = FVector2D(100.0 * Id, 0.0);
		Out.bSeen = bSeen;
		Out.bEngaged = bEngaged;
		Out.bSprung = bSprung;
		Out.bWatched = bWatched;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatLabContactsTest, "CodexTactics.Bot.CombatLab.Contacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatLabContactsTest::RunTest(const FString&)
{
	using namespace CombatLabTest;
	FCombatLabRunCollector Collector;
	Collector.Begin(0.f);
	// A patrol hidden at first (no contact), then seen at 10 s; nobody looks at it until 12.5 s.
	FCombatLabSample S = LabSample(5.f);
	S.Groups = { Group(1, ECombatLabContactKind::Patrol) };
	Collector.AddSample(S);
	TestEqual(TEXT("a patrol nobody saw is no contact"), Collector.GetContacts().Num(), 0);
	S = LabSample(10.f, TEXT("column"));
	S.Groups = { Group(1, ECombatLabContactKind::Patrol, true) };
	Collector.AddSample(S);
	S = LabSample(12.5f, TEXT("column"));
	S.Groups = { Group(1, ECombatLabContactKind::Patrol, true, true, false, true) };
	Collector.AddSample(S);
	// A single ambush springs at 20 s from an unwatched side; it is looked at 1.5 s later.
	S = LabSample(20.f, TEXT("column"));
	S.Groups = { Group(1, ECombatLabContactKind::Patrol, true, true), Group(2, ECombatLabContactKind::Ambush, false, true, true, false) };
	Collector.AddSample(S);
	S = LabSample(21.5f, TEXT("column"));
	S.Groups = { Group(2, ECombatLabContactKind::Ambush, false, true, true, true) };
	Collector.AddSample(S);
	// A coordinated plan engaged while watched (shot at first): no unwatched strike, reaction 0.
	S = LabSample(30.f, TEXT("wall_layout"));
	S.Groups = { Group(100001, ECombatLabContactKind::Coordinated, false, true, false, true) };
	Collector.AddSample(S);
	// An ambush that strikes and is killed before anybody turns to it.
	S = LabSample(40.f, TEXT("wall_layout"));
	S.Groups = { Group(3, ECombatLabContactKind::Ambush, false, true, true, false) };
	Collector.AddSample(S);
	S = LabSample(41.f, TEXT("wall_layout"));
	S.Groups = { Group(3, ECombatLabContactKind::Ambush, false, true, true, true) };
	S.Groups[0].Alive = 0; // dead: a look now is too late
	Collector.AddSample(S);
	Collector.End(60.f);

	const TArray<FCombatLabContact>& Contacts = Collector.GetContacts();
	TestEqual(TEXT("4 contacts"), Contacts.Num(), 4);
	TestEqual(TEXT("1 patrol"), Collector.CountContacts(ECombatLabContactKind::Patrol), 1);
	TestEqual(TEXT("2 ambushes"), Collector.CountContacts(ECombatLabContactKind::Ambush), 2);
	TestEqual(TEXT("1 coordinated"), Collector.CountContacts(ECombatLabContactKind::Coordinated), 1);
	if (Contacts.Num() == 4)
	{
		TestTrue(TEXT("patrol: seen at 10 s"), Contacts[0].Trigger == ECombatLabContactTrigger::Seen && Contacts[0].Time == 10.f);
		TestEqual(TEXT("patrol: looked at 2.5 s later"), Contacts[0].ReactionSeconds, 2.5f);
		TestEqual(TEXT("patrol: the formation at the contact"), Contacts[0].Formation, FString(TEXT("column")));
		TestTrue(TEXT("ambush: sprung, unwatched"), Contacts[1].Trigger == ECombatLabContactTrigger::Sprung && Contacts[1].IsUnwatchedStrike());
		TestEqual(TEXT("ambush: reaction 1.5 s"), Contacts[1].ReactionSeconds, 1.5f);
		TestTrue(TEXT("coordinated: engaged while watched -> reaction 0, no unwatched strike"),
			Contacts[2].Trigger == ECombatLabContactTrigger::Engaged && Contacts[2].ReactionSeconds == 0.f && !Contacts[2].IsUnwatchedStrike());
		TestTrue(TEXT("the killed ambush: never looked at"), Contacts[3].ReactionSeconds < 0.f && Contacts[3].bClosed);
		TestTrue(TEXT("a group gone from the samples closes its contact"), Contacts[0].bClosed && Contacts[2].bClosed);
	}
	TestEqual(TEXT("2 unwatched strikes"), Collector.CountUnwatchedStrikes(), 2);
	TestEqual(TEXT("1 never looked at"), Collector.CountNeverLooked(), 1);
	TestEqual(TEXT("mean reaction (2.5 + 1.5 + 0) / 3"), Collector.GetMeanReaction(), 4.f / 3.f, 0.001f);
	TestEqual(TEXT("max reaction"), Collector.GetMaxReaction(), 2.5f);
	TestTrue(TEXT("a known contact"), Collector.HasContact(2) && !Collector.HasContact(99));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatLabSquadTest, "CodexTactics.Bot.CombatLab.FormationsStancesWalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatLabSquadTest::RunTest(const FString&)
{
	using namespace CombatLabTest;
	FCombatLabRunCollector Collector;
	Collector.Begin(0.f);
	float Time = 0.f;
	const auto Add = [&](const TCHAR* Formation, bool bAtWall = false, float Step = 0.1f)
	{
		FCombatLabSample S = LabSample(Time, Formation);
		S.bAtWall = bAtWall;
		Collector.AddSample(S);
		Time += Step;
	};
	for (int32 Each = 0; Each < 20; ++Each) { Add(TEXT("diamond")); }          // 0..2 s diamond (the first formation: no change)
	for (int32 Each = 0; Each < 3; ++Each) { Add(TEXT("column")); }            // a 0.3 s flicker: no change
	for (int32 Each = 0; Each < 10; ++Each) { Add(TEXT("diamond")); }
	for (int32 Each = 0; Each < 10; ++Each) { Add(TEXT("column")); }           // a real change
	for (int32 Each = 0; Each < 10; ++Each) { Add(TEXT("")); }                 // Solo (a climb): no formation, no change
	for (int32 Each = 0; Each < 10; ++Each) { Add(TEXT("column")); }           // back to the column: no change
	for (int32 Each = 0; Each < 10; ++Each) { Add(TEXT("wall_layout"), true); } // a wall hug + a change
	for (int32 Each = 0; Each < 5; ++Each) { Add(TEXT("wall_layout"), false); } // 0.5 s off the wall ...
	for (int32 Each = 0; Each < 5; ++Each) { Add(TEXT("wall_layout"), true); }  // ... back on it: the same hug
	for (int32 Each = 0; Each < 15; ++Each) { Add(TEXT("wedge"), false); }      // off for 1.5 s
	for (int32 Each = 0; Each < 10; ++Each) { Add(TEXT("wall_layout"), true); } // a second hug
	Collector.End(Time);
	TestEqual(TEXT("formation changes: diamond -> column -> wall_layout -> wedge -> wall_layout"), Collector.GetFormationChanges(), 4);
	TestEqual(TEXT("column entered once"), Collector.GetFormationsEntered().FindRef(TEXT("column")), 1);
	TestEqual(TEXT("wall layout entered twice"), Collector.GetFormationsEntered().FindRef(TEXT("wall_layout")), 2);
	TestEqual(TEXT("the first formation counts as entered"), Collector.GetFormationsEntered().FindRef(TEXT("diamond")), 1);
	TestTrue(TEXT("seconds per formation"), Collector.GetFormationSeconds().FindRef(TEXT("diamond")) > 3.f);
	TestEqual(TEXT("two wall hugs"), Collector.GetWallHugs(), 2);

	// Stances, damage, rounds, climbs, knockdowns, fights.
	FCombatLabRunCollector Squad;
	Squad.Begin(0.f);
	FCombatLabSample S = LabSample(0.f);
	S.Members[0].AbsorbedDamage = 10.f; // before the run: no part of it
	S.Members[0].RoundsSpent = 30;
	Squad.AddSample(S);
	S = LabSample(1.f);
	S.bInFight = true;
	S.Members[0] = LabMember(TEXT("Commander"), EOperativeStance::Crouching, 90.f);
	S.Members[0].AbsorbedDamage = 35.f;
	S.Members[0].RoundsSpent = 42;
	S.Members[0].bKnockedDown = true;
	S.Members[1] = LabMember(TEXT("Engineer"), EOperativeStance::Prone, 100.f);
	S.Members[1].ClimbsUp = 1;
	Squad.AddSample(S);
	S.Time = 2.f;
	S.Members[0].Stance = EOperativeStance::Standing;
	S.Members[0].Health = 95.f; // healed: no negative damage
	S.Members[1].ClimbsDown = 1;
	Squad.AddSample(S);
	S.Time = 3.f;
	S.bInFight = false;
	S.Members[0].bKnockedDown = false;
	Squad.AddSample(S);
	S.Time = 4.f;
	S.bInFight = true;
	S.Members[0].bKnockedDown = true;
	Squad.AddSample(S);
	Squad.End(60.f);
	TestEqual(TEXT("3 stance changes"), Squad.GetStanceChanges(), 3);
	TestEqual(TEXT("3 per minute (a 60 s run)"), Squad.GetStanceChangesPerMinute(), 3.f, 0.001f);
	if (Squad.GetMembers().Num() == 2)
	{
		TestEqual(TEXT("commander: into crouch 1, into standing 1"), Squad.GetMembers()[0].ToCrouching + Squad.GetMembers()[0].ToStanding, 2);
		TestEqual(TEXT("engineer: into prone 1"), Squad.GetMembers()[1].ToProne, 1);
	}
	TestEqual(TEXT("damage taken 10 (a heal is no negative damage)"), Squad.GetDamageTaken(), 10.f);
	TestEqual(TEXT("absorbed by the cheat 25"), Squad.GetDamageAbsorbed(), 25.f);
	TestEqual(TEXT("12 rounds"), Squad.GetRounds(), 12);
	TestEqual(TEXT("1 climb up, 1 down"), Squad.GetClimbsUp() * 10 + Squad.GetClimbsDown(), 11);
	TestEqual(TEXT("2 knockdowns"), Squad.GetKnockdowns(), 2);
	TestEqual(TEXT("2 fights"), Squad.GetFights(), 2);
	TestEqual(TEXT("fight seconds 1"), Squad.GetFightSeconds(), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatLabLabelTest, "CodexTactics.Bot.CombatLab.FormationLabel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatLabLabelTest::RunTest(const FString&)
{
	TestEqual(TEXT("Solo: none"), CombatLabRules::FormationLabel(true, true, false, EFormationKind::Column, 4), FString());
	TestEqual(TEXT("formation off: none"), CombatLabRules::FormationLabel(false, false, false, EFormationKind::Column, 4), FString());
	TestEqual(TEXT("a wall order"), CombatLabRules::FormationLabel(false, true, true, EFormationKind::Diamond, 4), FString(TEXT("wall_layout")));
	TestEqual(TEXT("the wall formation"), CombatLabRules::FormationLabel(false, true, false, EFormationKind::Wall, 4), FString(TEXT("wall_layout")));
	TestEqual(TEXT("diamond of 4"), CombatLabRules::FormationLabel(false, true, false, EFormationKind::Diamond, 4), FString(TEXT("diamond")));
	TestEqual(TEXT("diamond of 3 = wedge"), CombatLabRules::FormationLabel(false, true, false, EFormationKind::Diamond, 3), FString(TEXT("wedge")));
	TestEqual(TEXT("diamond of 2 = pair"), CombatLabRules::FormationLabel(false, true, false, EFormationKind::Diamond, 2), FString(TEXT("pair")));
	TestEqual(TEXT("custom"), CombatLabRules::FormationLabel(false, true, false, EFormationKind::Custom, 4), FString(TEXT("custom")));
	TestEqual(TEXT("echelon"), CombatLabRules::FormationLabel(false, true, false, EFormationKind::EchelonLeft, 4), FString(TEXT("echelon_left")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatLabRecordTest, "CodexTactics.Bot.CombatLab.RunRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatLabRecordTest::RunTest(const FString&)
{
	using namespace CombatLabTest;
	FBotRouteConfig Route;
	Route.Name = TEXT("loop");
	for (int32 Each = 0; Each < 3; ++Each)
	{
		FBotRouteWaypoint Waypoint;
		Waypoint.At = FVector2D(1000.0 * Each, 0.0);
		Waypoint.Label = FString::Printf(TEXT("w%d"), Each);
		Route.Points.Add(Waypoint);
	}
	FBotRouteTracker Tracker;
	Tracker.Reset(Route, 0.f, FVector2D::ZeroVector);
	FBotRouteInput In;
	In.Time = 0.1f;
	Tracker.Tick(In);
	In.Time = 0.2f;
	Tracker.Tick(In); // the start reached
	Tracker.NotifyOrder(TEXT("move to #1"));
	In.Time = 20.f;
	Tracker.Tick(In); // stuck at the start: a retry
	FCombatLabRunCollector Collector;
	Collector.Begin(0.f);
	FCombatLabSample S = LabSample(3.f);
	S.Groups = { Group(7, ECombatLabContactKind::Ambush, false, true, true, false) };
	Collector.AddSample(S);
	Collector.End(120.f);
	FCombatLabRunMeta Meta;
	Meta.Result = TEXT("COMPLETE");
	Meta.Run = 2;
	Meta.BotSeed = 2;
	Meta.PressureSeed = 1102;
	Meta.LevelId = TEXT("L_FormationLabCombat");
	Meta.PlannedSeconds = 120.f;
	Meta.bImmortal = true;
	Meta.bInfiniteAmmo = true;
	FBotRouteDrop Drop;
	Drop.Waypoint = 14;
	Drop.Via = TEXT("hole_roof_0");
	Drop.bFell = true;
	Drop.bReached = true;
	Drop.Seconds = 4.25f;
	Meta.Drops.Add(Drop);
	const FString Line = CombatLabRules::ToJsonLine(CombatLabRules::MakeRunRecord(Meta, Tracker, Collector, FVector2D(12.4, -3.6)));
	TestFalse(TEXT("one line"), Line.Contains(TEXT("\n")));
	TSharedPtr<FJsonObject> Record;
	if (!TestTrue(TEXT("the line parses back"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), Record) && Record.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("schema"), Record->GetStringField(TEXT("schema")), FString(TEXT("codex.combatlab_run/1")));
	TestEqual(TEXT("kind"), Record->GetStringField(TEXT("kind")), FString(TEXT("combat_lab")));
	TestEqual(TEXT("result"), Record->GetStringField(TEXT("result")), FString(TEXT("COMPLETE")));
	TestEqual(TEXT("pressure seed"), static_cast<int32>(Record->GetNumberField(TEXT("pressure_seed"))), 1102);
	TestEqual(TEXT("duration"), Record->GetNumberField(TEXT("duration_sec")), 120.0);
	const TSharedPtr<FJsonObject> RouteJson = Record->GetObjectField(TEXT("route"));
	TestEqual(TEXT("route: 3 waypoints"), static_cast<int32>(RouteJson->GetNumberField(TEXT("waypoints"))), 3);
	TestFalse(TEXT("route: not completed"), RouteJson->GetBoolField(TEXT("completed")));
	TestEqual(TEXT("route: lap 40 m"), RouteJson->GetNumberField(TEXT("lap_length_m")), 40.0);
	const TArray<TSharedPtr<FJsonValue>>& Stuck = RouteJson->GetArrayField(TEXT("stuck"));
	TestEqual(TEXT("route: one stuck event"), Stuck.Num(), 1);
	if (Stuck.Num() == 1)
	{
		const TSharedPtr<FJsonObject> Event = Stuck[0]->AsObject();
		TestEqual(TEXT("... why"), Event->GetStringField(TEXT("reason")), FString(TEXT("no_move")));
		TestEqual(TEXT("... the last order"), Event->GetStringField(TEXT("last_order")), FString(TEXT("move to #1")));
		TestEqual(TEXT("... where (x, y)"), Event->GetArrayField(TEXT("position")).Num(), 2);
	}
	const TArray<TSharedPtr<FJsonValue>>* Drops = nullptr;
	if (TestTrue(TEXT("route: the drops (generated maps)"), RouteJson->TryGetArrayField(TEXT("drops"), Drops) && Drops && Drops->Num() == 1))
	{
		const TSharedPtr<FJsonObject> Each = (*Drops)[0]->AsObject();
		TestTrue(TEXT("... fell through hole_roof_0 at #14, not scripted"), Each->GetStringField(TEXT("via")) == TEXT("hole_roof_0") && Each->GetBoolField(TEXT("fell"))
			&& !Each->GetBoolField(TEXT("scripted")) && static_cast<int32>(Each->GetNumberField(TEXT("waypoint"))) == 14);
	}
	const TArray<TSharedPtr<FJsonValue>>& End = RouteJson->GetArrayField(TEXT("end_position"));
	TestTrue(TEXT("route: the end position in whole cm"), End.Num() == 2 && End[0]->AsNumber() == 12.0 && End[1]->AsNumber() == -4.0);
	const TSharedPtr<FJsonObject> Contacts = Record->GetObjectField(TEXT("contacts"));
	TestEqual(TEXT("contacts: 1"), static_cast<int32>(Contacts->GetNumberField(TEXT("total"))), 1);
	TestEqual(TEXT("contacts: 1 unwatched strike"), static_cast<int32>(Contacts->GetNumberField(TEXT("unwatched_strikes"))), 1);
	TestEqual(TEXT("contacts: listed with the formation"), Contacts->GetArrayField(TEXT("list"))[0]->AsObject()->GetStringField(TEXT("formation")), FString(TEXT("diamond")));
	TestTrue(TEXT("cheats"), Record->GetObjectField(TEXT("cheats"))->GetBoolField(TEXT("immortal")));
	TestTrue(TEXT("ammo: infinite flag"), Record->GetObjectField(TEXT("ammo"))->GetBoolField(TEXT("infinite")));
	TestTrue(TEXT("every report block"), Record->HasField(TEXT("formations")) && Record->HasField(TEXT("wall_hugs")) && Record->HasField(TEXT("stances"))
		&& Record->HasField(TEXT("damage")) && Record->HasField(TEXT("ladder")) && Record->HasField(TEXT("knockdowns")) && Record->HasField(TEXT("fights"))
		&& Record->HasField(TEXT("pressure")));
	return true;
}

#endif
