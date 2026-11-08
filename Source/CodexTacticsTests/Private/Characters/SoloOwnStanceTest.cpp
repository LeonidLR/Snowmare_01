#include "Misc/AutomationTest.h"
#include "Characters/SquadAutonomyRules.h"
#include "Data/SquadROE.h"

#if WITH_DEV_AUTOMATION_TESTS

// Bug 2026-11-08 (the game designer, PIE): "I switched the operatives to SOLO mode. I gave one operative the order to go prone. I switched to the
// next one and gave him the same order - he went prone. But the previous one got up into a crouch. In solo mode every operative must be
// autonomous." Root cause: USquadSubsystem::SetLeader in Solo re-armed Solo's guard crouch on every holding follower - the old leader too - and
// StartSoloGuardCrouch crouches any non-standing follower at once (a prone one kneels). Fix: SquadAutonomyRules::KeepsOwnStanceInSolo
// (Codex.Squad.SoloOwnStance). UE-only, no Godot reference (Godot _sync_squad_stances made everyone copy the leader).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadSoloOwnStanceTest, "CodexTactics.Characters.SquadAutonomy.SoloOwnStance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadSoloOwnStanceTest::RunTest(const FString&)
{
	using namespace SquadAutonomyRules;
	const float After = FSquadROE().HoldCrouchAfterSeconds;

	// The designer's steps: Solo; A selected, V (prone - the player's order); B selected (the leader switch arms A's guard crouch).
	FSoloStanceFacts OldLeader;
	OldLeader.bLeaderSwitch = true;
	OldLeader.bStanceFromPlayer = true;
	TestTrue(TEXT("A, ordered prone, keeps his stance when B is selected"), KeepsOwnStanceInSolo(OldLeader, true));
	TestTrue(TEXT("... so the guard crouch never runs (it would have knelt him at once)"),
		StartSoloGuardCrouch(EOperativeStance::Prone, After) == ESoloGuardCrouch::Crouch);
	TestFalse(TEXT("Codex.Squad.SoloOwnStance 0 (the old rule): the switch crouches him - the bug"), KeepsOwnStanceInSolo(OldLeader, false));
	// The facts carry no stance: a crouch / a stand the player gave is kept the same way (before, a crouch became the squad's and a stand
	// crouched after the delay).
	TestEqual(TEXT("... a stand the player gave would otherwise have waited for the guard crouch"), StartSoloGuardCrouch(EOperativeStance::Standing, After),
		ESoloGuardCrouch::Wait);

	// Nobody gave him a stance: Solo's guard crouch on a switch stays as designed (2026-10-20 / 2026-11-02).
	FSoloStanceFacts Unordered;
	Unordered.bLeaderSwitch = true;
	TestFalse(TEXT("A stance nobody ordered (his walk's, the squad's): the guard crouch as before"), KeepsOwnStanceInSolo(Unordered, true));

	// Solo coming on (B): "teammates hold their positions, crouched" for everybody, a player's earlier stance order too.
	FSoloStanceFacts SoloOn;
	SoloOn.bStanceFromPlayer = true;
	TestFalse(TEXT("Solo on: the followers take the guard crouch even after Alt + Z / V"), KeepsOwnStanceInSolo(SoloOn, true));

	// A manual corner peek keeps its pose (its stand-off depends on the stance) - on a switch and on Solo on; the old rule crouched it.
	FSoloStanceFacts Peek;
	Peek.bManualCornerPeek = true;
	TestTrue(TEXT("Solo on, a follower in a manual corner peek: his pose is kept"), KeepsOwnStanceInSolo(Peek, true));
	Peek.bLeaderSwitch = true;
	TestTrue(TEXT("... and when the player selects another operative"), KeepsOwnStanceInSolo(Peek, true));
	TestFalse(TEXT("... the old rule crouched him"), KeepsOwnStanceInSolo(Peek, false));

	// The older exceptions stand with either rule.
	FSoloStanceFacts Trap;
	Trap.bTrapSoloInCover = true;
	TestTrue(TEXT("The trap hold's cover keeps its stance (rev 5h)"), KeepsOwnStanceInSolo(Trap, true) && KeepsOwnStanceInSolo(Trap, false));
	FSoloStanceFacts GroupProne;
	GroupProne.bGroupCoverProne = true;
	TestTrue(TEXT("A group cover's prone keeps lying (2026-10-20)"), KeepsOwnStanceInSolo(GroupProne, true) && KeepsOwnStanceInSolo(GroupProne, false));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
