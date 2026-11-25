#include "Training/TrainingSectorRules.h"

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Telemetry/SessionRecordingRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TrainingSectorTest
{
	const TCHAR* Sectors = TEXT(R"json({
		"map":"L_Gen_Depot_1_X6",
		"sectors":[{
			"id":5, "box_cm":{"min":[-1000,-2000],"max":[8000,5500]}, "z_cm":{"min":-100,"max":900},
			"starts":[{"pos":[0,0,0],"yaw":0},{"pos":[1000,1000,0],"yaw":25}],
			"pmc_posts":[{"id":"north","kind":"street","pos":[5000,0,0],"yaw":180},{"id":"east","kind":"yard","pos":[6000,1500,0],"yaw":200}],
			"pmc_squad":{"count":3,"weights":"pmc_commander.json","posture":"patrol"}
		}]
	})json");

	TSharedPtr<FJsonObject> Read(const FString& Text)
	{
		TSharedPtr<FJsonObject> Json;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
		return Json;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrainingSectorDataTest, "CodexTactics.Training.Sector.Data",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrainingSectorDataTest::RunTest(const FString&)
{
	using namespace TrainingSectorTest;
	const TSharedPtr<FJsonObject> Json = Read(Sectors);
	if (!TestTrue(TEXT("fixture parsed"), Json.IsValid())) { return false; }
	TArray<FString> Errors;
	const TArray<FTrainingSector> Parsed = TrainingSectorRules::ParseSectors(*Json, Errors);
	if (!TestEqual(TEXT("one valid sector"), Parsed.Num(), 1)) { return false; }
	TestEqual(TEXT("no schema errors"), Errors.Num(), 0);
	const FTrainingSector& Sector = Parsed[0];
	TestEqual(TEXT("id preserved"), Sector.Id, 5);
	TestEqual(TEXT("the frozen box is in cm"), Sector.Area.GetSize(), FVector2D(9000, 7500));

	FTrainingSectorStart A;
	FTrainingSectorStart B;
	FPmcEncounterSpec SquadA;
	FPmcEncounterSpec SquadB;
	TrainingSectorRules::Pick(Sector, 91, A, SquadA);
	TrainingSectorRules::Pick(Sector, 91, B, SquadB);
	TestTrue(TEXT("seed repeats the start and the post"), A.Position == B.Position && SquadA.Post.Id == SquadB.Post.Id);
	TSet<FString> Posts;
	TArray<FVector> Starts;
	for (int32 Seed = 1; Seed <= 16; ++Seed)
	{
		TrainingSectorRules::Pick(Sector, Seed, B, SquadB);
		Posts.Add(SquadB.Post.Id);
		Starts.AddUnique(B.Position);
	}
	TestEqual(TEXT("seeds vary starts"), Starts.Num(), 2);
	TestEqual(TEXT("seeds vary posts"), Posts.Num(), 2);
	TestTrue(TEXT("the chosen post is the only scheduled squad spawn"), SquadA.bHasPost && !SquadA.bRandomSpawn
		&& SquadA.FixedSpot == SquadA.Post.Pos && SquadA.Count == 3 && SquadA.Posture == EPmcPosture::Patrol);

	FString Bad(Sectors);
	Bad.ReplaceInline(TEXT("[8000,5500]"), TEXT("[4000,5500]"));
	const TSharedPtr<FJsonObject> BadJson = Read(Bad);
	Errors.Reset();
	TestTrue(TEXT("reject a sector narrower than 60 m"), BadJson && TrainingSectorRules::ParseSectors(*BadJson, Errors).IsEmpty());
	TestEqual(TEXT("invalid sector is reported"), Errors.Num(), 1);

	TestEqual(TEXT("saved selection"), TrainingSectorRules::SavedId(*Read(TEXT(R"({"map":"L_Gen_Depot_1_X6","sector":5})"))), 5);
	TestEqual(TEXT("wrong map does not activate X6"), TrainingSectorRules::SavedId(*Read(TEXT(R"({"map":"Other","sector":5})"))), 0);
	TestEqual(TEXT("a console zero disables the sector"), TrainingSectorRules::ParseId(TEXT("0")).Get(-1), 0);
	TestFalse(TEXT("a console id cannot be a numeric prefix"), TrainingSectorRules::ParseId(TEXT("5x")).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrainingSectorClampTest, "CodexTactics.Training.Sector.Clamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrainingSectorClampTest::RunTest(const FString&)
{
	const FBox2D Box(FVector2D(-3000, -3000), FVector2D(3000, 3000));
	const FVector Goal(5200, -9900, 250);
	TestTrue(TEXT("outside goal is clamped in XY with pawn clearance, Z is unchanged"),
		TrainingSectorRules::Clamp(Box, Goal, 100.0).Equals(FVector(2900, -2900, 250)));
	TestTrue(TEXT("inside goal is unchanged"), TrainingSectorRules::Clamp(Box, FVector(400, 600, 250), 100.0).Equals(FVector(400, 600, 250)));
	TestFalse(TEXT("outside goal stays outside until clamped"), TrainingSectorRules::Contains(Box, Goal));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrainingSectorFightTest, "CodexTactics.Training.Sector.Fight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrainingSectorFightTest::RunTest(const FString&)
{
	using namespace TrainingSectorTest;
	const TSharedPtr<FJsonObject> Defaults = Read(Sectors);
	if (!TestTrue(TEXT("fixture"), Defaults.IsValid())) { return false; }
	const FSectorFightConfig Plain = TrainingSectorRules::FightConfig(*Defaults, 5);
	TestEqual(TEXT("restart default"), Plain.RoundRestartDelayS, 8.f);
	TestEqual(TEXT("limit default"), Plain.RoundTimeLimitS, 600.f);
	TestEqual(TEXT("resupply delay default"), Plain.ResupplyDelayS, 20.f);
	TestEqual(TEXT("ammo default is resupply"), static_cast<int32>(Plain.AmmoMode), static_cast<int32>(ESectorAmmoMode::Resupply));

	const TSharedPtr<FJsonObject> Tuned = Read(TEXT(R"json({
		"map":"L_Gen_Depot_1_X6",
		"round_restart_delay_s": 4,
		"round_time_limit_s": 90,
		"ammo_mode": "nope",
		"resupply_delay_s": 12,
		"sectors":[{
			"id":5, "round_restart_delay_s": 3, "ammo_mode": "real", "resupply_delay_s": 9,
			"box_cm":{"min":[-1000,-2000],"max":[8000,5500]}, "z_cm":{"min":-100,"max":900},
			"starts":[{"pos":[0,0,0],"yaw":0},{"pos":[1000,1000,0],"yaw":25}],
			"pmc_posts":[{"id":"north","kind":"street","pos":[5000,0,0],"yaw":180},{"id":"east","kind":"yard","pos":[6000,1500,0],"yaw":200}],
			"pmc_squad":{"count":3,"weights":"pmc_commander.json","posture":"patrol"}
		}]
	})json"));
	if (!TestTrue(TEXT("tuned fixture"), Tuned.IsValid())) { return false; }
	TArray<FString> Errors;
	TestEqual(TEXT("fight fields do not reject the sector"), TrainingSectorRules::ParseSectors(*Tuned, Errors).Num(), 1);
	const FSectorFightConfig Sector = TrainingSectorRules::FightConfig(*Tuned, 5);
	TestEqual(TEXT("sector overlay restart"), Sector.RoundRestartDelayS, 3.f);
	TestEqual(TEXT("limit stays on the document"), Sector.RoundTimeLimitS, 90.f);
	TestEqual(TEXT("sector overlay delay"), Sector.ResupplyDelayS, 9.f);
	TestEqual(TEXT("real ammo"), static_cast<int32>(Sector.AmmoMode), static_cast<int32>(ESectorAmmoMode::Real));
	const FSectorFightConfig Other = TrainingSectorRules::FightConfig(*Tuned, 1);
	TestEqual(TEXT("unknown ammo_mode is resupply"), static_cast<int32>(Other.AmmoMode), static_cast<int32>(ESectorAmmoMode::Resupply));
	TestEqual(TEXT("other sector keeps the top-level delay"), Other.ResupplyDelayS, 12.f);

	FSectorSideCount Up;
	Up.Members = 3;
	Up.Fighting = 3;
	FSectorSideCount Wiped;
	Wiped.Members = 3;
	FSectorSideCount Absent;
	TestEqual(TEXT("not started"), static_cast<int32>(TrainingSectorRules::EvaluateRound(Absent, Up, 0.0, 600.0)), static_cast<int32>(ESectorRoundEnd::None));
	TestEqual(TEXT("both up"), static_cast<int32>(TrainingSectorRules::EvaluateRound(Up, Up, 10.0, 600.0)), static_cast<int32>(ESectorRoundEnd::None));
	TestEqual(TEXT("player wipe"), static_cast<int32>(TrainingSectorRules::EvaluateRound(Wiped, Up, 10.0, 600.0)), static_cast<int32>(ESectorRoundEnd::Defeat));
	TestEqual(TEXT("pmc wipe"), static_cast<int32>(TrainingSectorRules::EvaluateRound(Up, Wiped, 10.0, 600.0)), static_cast<int32>(ESectorRoundEnd::Victory));
	TestEqual(TEXT("both wiped"), static_cast<int32>(TrainingSectorRules::EvaluateRound(Wiped, Wiped, 1.0, 600.0)), static_cast<int32>(ESectorRoundEnd::Draw));
	TestEqual(TEXT("time limit"), static_cast<int32>(TrainingSectorRules::EvaluateRound(Up, Up, 600.0, 600.0)), static_cast<int32>(ESectorRoundEnd::Draw));
	TestEqual(TEXT("wipe beats the limit"), static_cast<int32>(TrainingSectorRules::EvaluateRound(Up, Wiped, 600.0, 600.0)), static_cast<int32>(ESectorRoundEnd::Victory));

	const TArray<FTrainingSector> Parsed = TrainingSectorRules::ParseSectors(*Defaults, Errors);
	if (!TestEqual(TEXT("pair sector"), Parsed.Num(), 1)) { return false; }
	FTrainingSectorStart Start;
	FPmcEncounterSpec Squad;
	TrainingSectorRules::Pick(Parsed[0], 91, Start, Squad);
	const FSectorPair First = TrainingSectorRules::PairOf(Parsed[0], Start, Squad);
	TestTrue(TEXT("picked pair resolves"), First.IsValid());
	FTrainingSectorStart Applied;
	FPmcEncounterSpec AppliedSquad;
	TrainingSectorRules::ApplyPair(Parsed[0], First, 91, Applied, AppliedSquad);
	TestTrue(TEXT("apply keeps the pair"), Applied.Position.Equals(Start.Position, 1.0) && AppliedSquad.Post.Id == Squad.Post.Id && !AppliedSquad.bRandomSpawn);
	FSectorPair Previous = First;
	for (int32 Round = 2; Round <= 24; ++Round)
	{
		const FSectorPair Next = TrainingSectorRules::PickRoundPair(2, 2, 91, Round, Previous);
		const FSectorPair Again = TrainingSectorRules::PickRoundPair(2, 2, 91, Round, Previous);
		TestTrue(TEXT("later pair is stable and not the previous one"), Next.IsValid() && Next == Again && !(Next == Previous)
			&& Next.StartIndex >= 0 && Next.StartIndex < 2 && Next.PostIndex >= 0 && Next.PostIndex < 2);
		Previous = Next;
	}
	const FSectorPair Only{0, 0};
	TestTrue(TEXT("the only pair repeats"), TrainingSectorRules::PickRoundPair(1, 1, 3, 2, Only) == Only);

	// Safe round pair choice: >= MinStartPostDistanceM and no LOS, else farthest fallback.
	FTrainingSector SafeSec;
	SafeSec.Starts.Add({FVector(0, 0, 0), 0.0});
	SafeSec.Starts.Add({FVector(5000, 0, 0), 0.0});
	FPmcEncounterPost PostNear; PostNear.Id = TEXT("near"); PostNear.Pos = FVector(2000, 0, 0); // 20 m from start 0, 30 m from start 1
	FPmcEncounterPost PostFar;  PostFar.Id = TEXT("far");  PostFar.Pos = FVector(0, 6000, 0); // 60 m from start 0, ~78 m from start 1
	SafeSec.Posts.Add(PostNear);
	SafeSec.Posts.Add(PostFar);
	bool bFallback = false;
	// With LOS on (1, 1), only (0, 1) qualifies (60 m, no LOS).
	const FSectorPair SafePick = TrainingSectorRules::PickSafeRoundPair(SafeSec, 1, 2, {0, 0}, 40.f,
		[](const FSectorPair& P) { return P.StartIndex == 1 && P.PostIndex == 1; }, &bFallback);
	TestFalse(TEXT("safe pick did not fall back"), bFallback);
	TestTrue(TEXT("safe pick chose (0, 1)"), SafePick == FSectorPair({0, 1}));
	// With LOS on all pairs, falls back to farthest pair (1, 1) (~78 m).
	const FSectorPair FarthestPick = TrainingSectorRules::PickSafeRoundPair(SafeSec, 1, 2, {0, 0}, 40.f,
		[](const FSectorPair&) { return true; }, &bFallback);
	TestTrue(TEXT("all-LOS falls back to farthest"), bFallback);
	TestTrue(TEXT("farthest pair is (1, 1)"), FarthestPick == FSectorPair({1, 1}));

	FSectorResupplyQuery Query;
	Query.bUsesAmmo = true;
	Query.bHasWeapon = true;
	Query.Magazine = 30;
	Query.WeaponReserve = 90;
	Query.SecondsQuiet = 19.0;
	TestFalse(TEXT("quiet before the delay"), TrainingSectorRules::ShouldRefillReserve(Query, ESectorAmmoMode::Resupply, 20.0));
	Query.SecondsQuiet = 20.0;
	TestTrue(TEXT("quiet delay refills an empty reserve"), TrainingSectorRules::ShouldRefillReserve(Query, ESectorAmmoMode::Resupply, 20.0));
	TestFalse(TEXT("real ammo never refills"), TrainingSectorRules::ShouldRefillReserve(Query, ESectorAmmoMode::Real, 20.0));
	Query.Reserve = 4;
	TestFalse(TEXT("reserve left"), TrainingSectorRules::ShouldRefillReserve(Query, ESectorAmmoMode::Resupply, 20.0));
	Query.Reserve = 0;
	Query.Clip = 11;
	TestFalse(TEXT("a high clip waits"), TrainingSectorRules::ShouldRefillReserve(Query, ESectorAmmoMode::Resupply, 20.0));
	Query.Clip = 10;
	TestTrue(TEXT("clip at a third of the magazine"), TrainingSectorRules::ShouldRefillReserve(Query, ESectorAmmoMode::Resupply, 20.0));
	Query.bLauncher = true;
	TestFalse(TEXT("launcher"), TrainingSectorRules::ShouldRefillReserve(Query, ESectorAmmoMode::Resupply, 20.0));
	TestEqual(TEXT("refill amount"), TrainingSectorRules::RefilledReserve(30, 90), 90);
	TestEqual(TEXT("refill at least a magazine"), TrainingSectorRules::RefilledReserve(30, 10), 30);
	TestEqual(TEXT("refill at least one"), TrainingSectorRules::RefilledReserve(0, 0), 1);
	TestTrue(TEXT("never fired waits from the empty mark"), FMath::IsNearlyEqual(TrainingSectorRules::QuietSeconds(30.0, 10.0, -1.0e9), 20.0));
	TestTrue(TEXT("a later shot restarts the quiet time"), FMath::IsNearlyEqual(TrainingSectorRules::QuietSeconds(30.0, 10.0, 25.0), 5.0));
	TestTrue(TEXT("no empty mark"), FMath::IsNearlyEqual(TrainingSectorRules::QuietSeconds(30.0, -1.0, 0.0), 0.0));
	TestFalse(TEXT("grenades stay on the spawn"), TrainingSectorRules::ResupplyTouchesGrenades());
	TestFalse(TEXT("up fighter"), TrainingSectorRules::IsFighterDown(true, false));
	TestTrue(TEXT("dead fighter"), TrainingSectorRules::IsFighterDown(false, false));
	TestTrue(TEXT("down fighter"), TrainingSectorRules::IsFighterDown(true, true));
	TestTrue(TEXT("victory line"), TrainingSectorRules::RoundBanner(2, ESectorRoundEnd::Victory).Contains(TEXT("победа")) && TrainingSectorRules::RoundBanner(2, ESectorRoundEnd::Victory).Contains(TEXT("2")));
	TestTrue(TEXT("defeat line"), TrainingSectorRules::RoundBanner(1, ESectorRoundEnd::Defeat).Contains(TEXT("поражение")));
	TestTrue(TEXT("draw line"), TrainingSectorRules::RoundBanner(3, ESectorRoundEnd::Draw).Contains(TEXT("ничья")));

	FDemoSession Session;
	Session.Duration = 61.;
	FDemoFrame Snap;
	Snap.Time = 1.;
	Snap.RealTime = 1.;
	Session.Frames.Add(Snap);
	FDemoRoundEvent Started;
	Started.bStart = true;
	Started.Time = 1.;
	Started.RealTime = 1.;
	Started.SectorId = 5;
	Started.Seed = 91;
	Started.Round = 1;
	Started.StartIndex = 0;
	Started.PostIndex = 2;
	Session.Rounds.Add(Started);
	FDemoRoundEvent Ended = Started;
	Ended.bStart = false;
	Ended.Time = 2.;
	Ended.Result = EDemoRoundResult::Victory;
	Ended.DurationS = 12.f;
	Ended.KillsPlayer = 3;
	Ended.KillsPmc = 1;
	Ended.ScoreVictory = 1;
	Session.Rounds.Add(Ended);
	const FString Text = SessionRecordingRules::SerializeSession(Session, TEXT("session"));
	const int32 StartAt = Text.Find(TEXT("\"type\":\"round_start\""));
	const int32 SnapAt = Text.Find(TEXT("\"type\":\"snap\""));
	const int32 EndAt = Text.Find(TEXT("\"type\":\"round_end\""));
	TestTrue(TEXT("round events merge ahead of an equal-time frame"), StartAt >= 0 && SnapAt > StartAt && EndAt > SnapAt);
	TestTrue(TEXT("round end names the result"), Text.Contains(TEXT("\"result\":\"victory\"")) && Text.Contains(TEXT("\"sector_id\":5")) && Text.Contains(TEXT("\"rounds\":1")));

	// Relocation config, no-contact timer gate, point preference, and session pmc_relocated event.
	TestTrue(TEXT("relocate config defaults"), FMath::IsNearlyEqual(Plain.NoContactRelocateS, 90.f)
		&& FMath::IsNearlyEqual(Plain.RelocateDistanceM, 40.f) && Plain.RelocateMaxPerRound == 3);
	TestFalse(TEXT("no relocate during grace"), TrainingSectorRules::ShouldRelocateNoContact(100.0, 0.0, 90.f, 0, 3, true));
	TestFalse(TEXT("no relocate before delay"), TrainingSectorRules::ShouldRelocateNoContact(89.9, 0.0, 90.f, 0, 3, false));
	TestTrue(TEXT("relocate at delay"), TrainingSectorRules::ShouldRelocateNoContact(90.0, 0.0, 90.f, 0, 3, false));
	TestFalse(TEXT("no relocate past max per round"), TrainingSectorRules::ShouldRelocateNoContact(200.0, 0.0, 90.f, 3, 3, false));

	TArray<FSectorRelocateCandidate> Cand;
	Cand.Add({ FVector(3800, 0, 0), 38.f, 38.f, false, true });   // idx 0: violates MinPlayerDistM=40
	Cand.Add({ FVector(4100, 0, 0), 41.f, 41.f, true, true });    // idx 1: in band, has LOS + cover
	Cand.Add({ FVector(4400, 0, 0), 44.f, 44.f, false, false });  // idx 2: in band, no LOS, no cover
	Cand.Add({ FVector(4200, 0, 0), 42.f, 42.f, false, true });   // idx 3: in band, no LOS + cover (best)
	const FSectorRelocatePick BestReloc = TrainingSectorRules::PickRelocatePoint(Cand, 40.f, 5.f, 40.f, 11);
	TestTrue(TEXT("picks valid no-LOS cover candidate in band"), BestReloc.bValid && BestReloc.CandidateIndex == 3 && !BestReloc.bHasLos && !BestReloc.bFallback);

	TArray<FSectorRelocateCandidate> LosOnly;
	LosOnly.Add({ FVector(4300, 0, 0), 43.f, 43.f, true, false });
	LosOnly.Add({ FVector(4050, 0, 0), 40.5f, 40.5f, true, true });
	const FSectorRelocatePick FallbackReloc = TrainingSectorRules::PickRelocatePoint(LosOnly, 40.f, 5.f, 40.f, 11);
	TestTrue(TEXT("LOS fallback picks closest cover in band"), FallbackReloc.bValid && FallbackReloc.CandidateIndex == 1 && FallbackReloc.bFallback);

	TArray<FSectorRelocateCandidate> TooClose;
	TooClose.Add({ FVector(3000, 0, 0), 30.f, 30.f, false, true });
	TestFalse(TEXT("never violates min_start_post_distance_m"), TrainingSectorRules::PickRelocatePoint(TooClose, 40.f, 5.f, 40.f, 11).bValid);

	FDemoRoundEvent RelocEvent;
	RelocEvent.bPmcRelocated = true;
	RelocEvent.Time = 1.5;
	RelocEvent.RealTime = 1.5;
	RelocEvent.SectorId = 5;
	RelocEvent.Seed = 91;
	RelocEvent.Round = 1;
	RelocEvent.RelocateDistanceM = 41.5f;
	RelocEvent.bRelocateLos = false;
	Session.Rounds.Add(RelocEvent);
	const FString WithReloc = SessionRecordingRules::SerializeSession(Session, TEXT("session"));
	TestTrue(TEXT("session emits pmc_relocated with distance and los without inflating rounds"),
		WithReloc.Contains(TEXT("\"type\":\"pmc_relocated\"")) && WithReloc.Contains(TEXT("\"distance_m\":41.5"))
		&& WithReloc.Contains(TEXT("\"los\":false")) && WithReloc.Contains(TEXT("\"rounds\":1")));

	FDemoSession Bare;
	Bare.Frames.SetNum(1);
	const FString Untouched = SessionRecordingRules::SerializeSession(Bare, TEXT("session"));
	TestFalse(TEXT("no round events leaves the recording unchanged"), Untouched.Contains(TEXT("round_")) || Untouched.Contains(TEXT("score_victory")));
	return true;
}

#endif
