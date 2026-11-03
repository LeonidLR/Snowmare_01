#include "Misc/AutomationTest.h"
#include "AI/SquadOrderCatalog.h"

#if WITH_DEV_AUTOMATION_TESTS

// The shared squad order catalog (user request 2026-11-03, docs/systems/pmc-squad-ai.md): the table itself (no world).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadOrderCatalogTableTest, "CodexTactics.AI.SquadOrderCatalog.Table",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadOrderCatalogTableTest::RunTest(const FString&)
{
	const TArray<FSquadOrderEntry>& All = SquadOrderCatalog::All();
	TestEqual(TEXT("one entry per order"), All.Num(), static_cast<int32>(ESquadOrder::Count));
	TSet<FString> Names;
	for (int32 Index = 0; Index < All.Num(); ++Index)
	{
		const FSquadOrderEntry& Entry = All[Index];
		TestEqual(TEXT("entry at its id's index"), static_cast<int32>(Entry.Id), Index);
		TestTrue(TEXT("a name"), FCString::Strlen(Entry.Name) > 0);
		TestTrue(TEXT("a description"), FCString::Strlen(Entry.Description) > 0);
		TestTrue(TEXT("CanDo bound"), static_cast<bool>(Entry.CanDo));
		TestTrue(TEXT("Execute bound"), static_cast<bool>(Entry.Execute));
		Names.Add(Entry.Name);
		TestEqual(TEXT("Parse(Name) round-trips"), static_cast<int32>(SquadOrderCatalog::Parse(Entry.Name)), Index);
	}
	TestEqual(TEXT("names unique"), Names.Num(), All.Num());
	TestEqual(TEXT("unknown name -> Count"), static_cast<int32>(SquadOrderCatalog::Parse(TEXT("Teleport"))), static_cast<int32>(ESquadOrder::Count));
	TestNull(TEXT("Find(Count) is null"), SquadOrderCatalog::Find(ESquadOrder::Count));
	TestEqual(TEXT("Name(Move)"), FString(SquadOrderCatalog::Name(ESquadOrder::Move)), FString(TEXT("Move")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadOrderCatalogGateTest, "CodexTactics.AI.SquadOrderCatalog.Gate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadOrderCatalogGateTest::RunTest(const FString&)
{
	// Without an operative nothing can be done and nothing executes.
	FSquadOrderContext Empty;
	Empty.Location = FVector(100.f, 0.f, 0.f);
	for (const FSquadOrderEntry& Entry : SquadOrderCatalog::All())
	{
		TestFalse(FString::Printf(TEXT("%s refused without an operative"), Entry.Name), SquadOrderCatalog::CanDo(Entry.Id, Empty));
		TestFalse(FString::Printf(TEXT("%s does not execute without an operative"), Entry.Name), SquadOrderCatalog::Execute(Entry.Id, Empty));
	}
	TestFalse(TEXT("null operative is not orderable"), SquadOrderCatalog::IsOperativeOrderable(nullptr));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
