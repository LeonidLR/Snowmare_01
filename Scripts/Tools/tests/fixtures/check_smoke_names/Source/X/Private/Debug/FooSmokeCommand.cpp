// Fixture: fake smoke registrations.
static FAutoConsoleCommandWithWorldAndArgs Command(
	TEXT("CodexTactics.FooSmoke"), TEXT("Foo"), FConsoleCommandWithWorldAndArgsDelegate());
static FAutoConsoleCommand BarCommand(TEXT("CodexTactics.BarSmoke"), TEXT("Bar"), FConsoleCommandDelegate());
