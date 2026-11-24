#include "Telemetry/SessionRecordingRules.h"
#include "Telemetry/SessionRecordingSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

#define DEMO_TEST(Name) IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemo##Name##Test, "CodexTactics.Telemetry.SessionRecording." #Name, \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

using namespace SessionRecordingRules;

DEMO_TEST(Gate)
bool FDemoGateTest::RunTest(const FString& Parameters)
{
	FDemoGate Gate;
	TestFalse(TEXT("Editor world"), ShouldRecord(Gate));
	Gate.bGameWorld = true;
	TestTrue(TEXT("Human PIE or Game"), ShouldRecord(Gate));
	Gate.Switch = 0;
	TestFalse(TEXT("Disabled even for human"), ShouldRecord(Gate));
	bool FDemoGate::* Exclusions[] = { &FDemoGate::bUnattended, &FDemoGate::bNullRhi, &FDemoGate::bNoTelemetry,
		&FDemoGate::bAutomation, &FDemoGate::bCommandlet };
	for (bool FDemoGate::* Exclusion : Exclusions)
	{
		Gate.*Exclusion = true;
		Gate.Switch = 1;
		TestFalse(TEXT("Auto excludes non-human contexts"), ShouldRecord(Gate));
		Gate.Switch = 2;
		TestTrue(TEXT("Force records game worlds"), ShouldRecord(Gate));
		Gate.*Exclusion = false;
	}
	Gate.bGameWorld = false;
	TestFalse(TEXT("Force excludes non-game worlds"), ShouldRecord(Gate));
	return true;
}

DEMO_TEST(Discard)
bool FDemoDiscardTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Short session"), ShouldDiscard(59.9, 5, false));
	TestTrue(TEXT("No accepted orders, including only refused orders"), ShouldDiscard(60., 0, false));
	TestFalse(TEXT("Minimum qualifying session"), ShouldDiscard(60., 1, false));
	TestTrue(TEXT("Suppressed session"), ShouldDiscard(60., 1, true));
	return true;
}

DEMO_TEST(FileName)
bool FDemoFileNameTest::RunTest(const FString& Parameters)
{
	const FDateTime Time(2026, 11, 23, 14, 30, 22);
	TestEqual(TEXT("File name"), MakeDemoFileName(Time, TEXT("L_Gen_Depot_1")), FString(TEXT("2026-11-23_L_Gen_Depot_1_143022.jsonl")));
	TestEqual(TEXT("PIE prefix and invalid characters"), CleanMapName(TEXT("UEDPIE_12_Foo/Bar.!-")), FString(TEXT("Foo_Bar__-")));
	return true;
}

DEMO_TEST(Mapping)
bool FDemoMappingTest::RunTest(const FString& Parameters)
{
	FDemoFacts Facts;
	auto Check = [&](const TCHAR* Expected, const TCHAR* Reason = TEXT(""))
	{
		const FDemoMapping Mapping = MapToPmcOrder(Facts);
		TestEqual(TEXT("Order"), FString(Mapping.Order ? Mapping.Order : TEXT("")), FString(Expected));
		TestEqual(TEXT("Reason"), FString(Mapping.Unmapped), FString(Reason));
	};
	Check(TEXT(""), TEXT("explore_move"));
	Facts.GoalDistance = 700;
	Facts.GoalDistanceNew = 401;
	Check(TEXT(""), TEXT("explore_move"));
	Facts.GoalDistanceNew = 400;
	Check(TEXT("Objective"));
	Facts.ThreatDistance = 1000;
	Facts.ThreatDistanceNew = 1299;
	Facts.FlankDegrees = 44.9;
	Check(TEXT(""), TEXT("reposition"));
	Facts.FlankDegrees = 45;
	Check(TEXT("Flank"));
	Facts.ThreatDistanceNew = 1300;
	Check(TEXT("FallBack"));
	Facts.FlankDegrees = 0;
	Facts.ThreatDistanceNew = 701;
	Check(TEXT(""), TEXT("reposition"));
	Facts.ThreatDistanceNew = 700;
	Check(TEXT("Advance"));
	Facts.Kind = EDemoOrderKind::Cover;
	Check(TEXT("TakeCover"));
	Facts.Kind = EDemoOrderKind::CornerPeek;
	Facts.bInCover = true;
	Check(TEXT("Relocate"));
	Facts.ThreatDistanceNew = 1300;
	Check(TEXT("FallBack"));
	Facts.ThreatDistance = -1;
	Check(TEXT("Ambush"));
	Facts.Kind = EDemoOrderKind::Suppress;
	Check(TEXT("Suppress"));
	Facts.bCease = true;
	Check(TEXT(""), TEXT("suppress_cease"));
	Facts.bCease = false;
	Facts.bAccepted = false;
	Check(TEXT(""), TEXT("refused"));
	Facts.Kind = EDemoOrderKind::Stance;
	Facts.ThreatDistance = 1000;
	Facts.bInCover = false;
	Facts.bCrouch = true;
	Check(TEXT("Crouch"));
	Facts.bMoving = true;
	Check(TEXT(""), TEXT("stance"));
	Facts.Kind = EDemoOrderKind::Medkit;
	Check(TEXT("Heal"));
	Facts.Kind = EDemoOrderKind::Grenade;
	Check(TEXT(""), TEXT("grenade"));
	Facts.Kind = EDemoOrderKind::Breach;
	Check(TEXT(""), TEXT("breach"));
	Facts.Kind = EDemoOrderKind::Shot;
	Check(TEXT(""), TEXT("fire_target"));
	Facts.Kind = EDemoOrderKind::QuickFire;
	Check(TEXT(""), TEXT("fire_target"));
	return true;
}

DEMO_TEST(Bearing)
bool FDemoBearingTest::RunTest(const FString& Parameters)
{
	const auto Check = [&](const FVector& Point, double Yaw, double Expected)
	{
		const FVector2D Result = ThreatDistanceBearing(FVector::ZeroVector, Yaw, &Point);
		TestTrue(TEXT("Distance is planar"), FMath::IsNearlyEqual(Result.X, Point.Size2D()));
		TestTrue(TEXT("Bearing in facing space"), FMath::IsNearlyEqual(Result.Y, Expected));
	};
	Check(FVector(100, 0, 50), 0, 0);
	Check(FVector(0, 100, 0), 0, 90);
	Check(FVector(0, -100, 0), 0, -90);
	Check(FVector(-100, 0, 0), 0, 180);
	Check(FVector(-100, 0, 0), -179, -1);
	TestEqual(TEXT("No threat"), ThreatDistanceBearing(FVector::ZeroVector, 0., nullptr), FVector2D(-1., 0.));
	return true;
}

DEMO_TEST(Serialize)
bool FDemoSerializeTest::RunTest(const FString& Parameters)
{
	FDemoSession Session;
	Session.Map = TEXT("Test");
	Session.Frames.SetNum(5);
	Session.Frames[0].Type = FDemoFrame::EType::Order;
	Session.Frames[0].Time = 3.;
	Session.Frames[0].Facts.bAccepted = false;
	Session.Frames[1].Type = FDemoFrame::EType::Order;
	Session.Frames[1].Time = 1.;
	Session.Frames[1].RealTime = 1.;
	Session.Frames[1].Actor = FName(TEXT("Op_0"));
	Session.Frames[1].Facts.Kind = EDemoOrderKind::Medkit;
	Session.Frames[2].Time = 2.;
	Session.Frames[2].RealTime = std::numeric_limits<double>::quiet_NaN();
	Session.Frames[0].Destination = FVector(std::numeric_limits<double>::infinity(), 100.4, .8);
	Session.Frames[3].Time = 4.;
	Session.Frames[4].Type = FDemoFrame::EType::Mode;
	Session.Frames[4].Time = 2.;
	Session.AcceptedOrders = 1;
	Session.Duration = 61.;
	TArray<FString> Lines;
	SerializeSession(Session, TEXT("session")).ParseIntoArrayLines(Lines);
	if (!TestEqual(TEXT("Line count"), Lines.Num(), 7)) { return false; }
	const FString Golden = TEXT("{\"type\":\"order\",\"t\":1,\"rt\":1,\"kind\":\"Medkit\",\"pmc_order\":\"Heal\",\"unmapped\":\"\",\"ok\":true,\"plan\":false,\"mode\":\"formation\",\"actor\":\"Op_0\",\"sel\":[],\"sprint\":false,\"dest\":null,\"cover\":null,\"target\":null,\"facts\":{\"thr_d\":-1,\"thr_d_new\":-1,\"flank_deg\":0,\"goal_d\":-1,\"goal_d_new\":-1},\"ops\":[],\"threats\":[]}");
	TestEqual(TEXT("Stable order schema"), Lines[1], Golden);
	double Previous = 0.;
	for (int32 I = 0; I < Lines.Num(); ++I)
	{
		TSharedPtr<FJsonObject> Object;
		if (!TestTrue(TEXT("Valid JSON object"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Lines[I]), Object) && Object.IsValid()))
		{
			return false;
		}
		if (I == 0) { TestEqual(TEXT("First object"), Object->GetStringField(TEXT("type")), FString(TEXT("header"))); }
		else if (I == Lines.Num() - 1) { TestEqual(TEXT("Last object"), Object->GetStringField(TEXT("type")), FString(TEXT("footer"))); }
		else
		{
			const double Time = Object->GetNumberField(TEXT("t"));
			TestTrue(TEXT("Time is finite and nondecreasing"), FMath::IsFinite(Time) && Time >= Previous);
			TestTrue(TEXT("Real time is finite"), FMath::IsFinite(Object->GetNumberField(TEXT("rt"))));
			Previous = Time;
		}
	}
	return true;
}

DEMO_TEST(Lifecycle)
bool FDemoLifecycleTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("DemoRecordingTestWorld")));
	if (!TestNotNull(TEXT("Game world"), World)) { return false; }
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	USessionRecordingSubsystem* Recorder = World->GetSubsystem<USessionRecordingSubsystem>();
	if (TestNotNull(TEXT("Recording subsystem"), Recorder))
	{
		Recorder->OnWorldBeginPlay(*World);
		TestNull(TEXT("Automation leaves recording off"), USessionRecordingSubsystem::GetIfRecording(World));
		const FString Root = FPaths::AutomationTransientDir() / TEXT("DemoRecording") / FGuid::NewGuid().ToString();
		const auto Check = [&](const TCHAR* Case, double End, int32 Accepted, bool bRefused, bool bWrite)
		{
			const FString Directory = Root / Case;
			Recorder->StartForTest(Directory);
			FDemoFacts Refused;
			Refused.bAccepted = false;
			if (bRefused) { Recorder->RecordOrderAt(1., Refused); }
			for (int32 I = 0; I < Accepted; ++I) { Recorder->RecordOrderAt(2. + I, FDemoFacts{}); }
			Recorder->SnapshotAt(3.);
			Recorder->FinishAt(End);
			TArray<FString> Names;
			IFileManager::Get().FindFiles(Names, *(Directory / TEXT("*.jsonl")), true, false);
			if (TestEqual(Case, Names.Num(), bWrite ? 1 : 0) && bWrite)
			{
				FString Data;
				TestTrue(TEXT("Read demo"), FFileHelper::LoadFileToString(Data, *(Directory / Names[0])));
				TArray<FString> Lines;
				Data.ParseIntoArrayLines(Lines);
				TestEqual(TEXT("Header, two orders, snapshot, footer"), Lines.Num(), 5);
			}
		};
		Check(TEXT("Long accepted"), 61., 2, false, true);
		Check(TEXT("Short accepted"), 30., 1, false, false);
		Check(TEXT("No orders"), 61., 0, false, false);
		Check(TEXT("Refused only"), 61., 0, true, false);
		IFileManager::Get().DeleteDirectory(*Root, false, true);
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#undef DEMO_TEST
#endif
