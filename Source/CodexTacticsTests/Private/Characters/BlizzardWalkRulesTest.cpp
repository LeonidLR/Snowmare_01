#include "Misc/AutomationTest.h"
#include "Characters/BlizzardWalkRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Blizzard walk upper body (game designer 2026-10-22; UE-only): weather x indoor x speed x state -> weight.

namespace BlizzardWalkTest
{
	/** Walking outdoors in a full blizzard, nothing else going on. */
	FBlizzardWalkInput Walking()
	{
		FBlizzardWalkInput Input;
		Input.BlizzardAmount = 1.f;
		Input.Speed = 220.f;
		return Input;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlizzardWalkAmountTest, "CodexTactics.Characters.BlizzardWalk.Amount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBlizzardWalkAmountTest::RunTest(const FString&)
{
	using namespace BlizzardWalkRules;
	TestEqual(TEXT("blizzard, no transition"), GetBlizzardAmount(EWeatherState::Blizzard, EWeatherState::Blizzard, 0.f), 1.f);
	TestEqual(TEXT("clear"), GetBlizzardAmount(EWeatherState::Clear, EWeatherState::Clear, 0.f), 0.f);
	TestEqual(TEXT("snowfall is not a blizzard"), GetBlizzardAmount(EWeatherState::Snowfall, EWeatherState::Snowfall, 0.f), 0.f);
	TestTrue(TEXT("snowfall -> blizzard, 30 % through: 0.3"),
		FMath::IsNearlyEqual(GetBlizzardAmount(EWeatherState::Snowfall, EWeatherState::Blizzard, 0.3f), 0.3f, 0.001f));
	TestTrue(TEXT("blizzard -> overcast, 30 % through: 0.7"),
		FMath::IsNearlyEqual(GetBlizzardAmount(EWeatherState::Blizzard, EWeatherState::Overcast, 0.3f), 0.7f, 0.001f));
	TestEqual(TEXT("blend clamps"), GetBlizzardAmount(EWeatherState::Clear, EWeatherState::Blizzard, 2.f), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlizzardWalkGateTest, "CodexTactics.Characters.BlizzardWalk.Gate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBlizzardWalkGateTest::RunTest(const FString&)
{
	using namespace BlizzardWalkRules;
	const FBlizzardWalkTuning Tuning;
	auto Weight = [&Tuning](const FBlizzardWalkInput& Input, bool bActive = false) { return GetTargetWeight(Input, Tuning, bActive); };
	auto Block = [&Tuning](const FBlizzardWalkInput& Input, bool bActive = false) { return GetBlock(Input, Tuning, bActive); };

	FBlizzardWalkInput Input = BlizzardWalkTest::Walking();
	TestEqual(TEXT("walking outdoors in a blizzard: full weight"), Weight(Input), 1.f);
	Input.Speed = 400.f;
	TestEqual(TEXT("jogging 400 cm/s still"), Weight(Input), 1.f);

	// Weather.
	Input = BlizzardWalkTest::Walking();
	Input.BlizzardAmount = 0.f;
	TestEqual(TEXT("clear weather: 0"), Block(Input), EBlizzardWalkBlock::NoBlizzard);
	Input.BlizzardAmount = 0.4f;
	TestEqual(TEXT("transition 0.4 in: not started yet (enter 0.5)"), Weight(Input), 0.f);
	TestTrue(TEXT("... but kept once on (exit 0.35)"), FMath::IsNearlyEqual(Weight(Input, true), 0.4f, 0.001f));
	Input.BlizzardAmount = 0.3f;
	TestEqual(TEXT("below the exit threshold: off"), Weight(Input, true), 0.f);

	// Indoors.
	Input = BlizzardWalkTest::Walking();
	Input.bSheltered = true;
	TestEqual(TEXT("indoors / under a shelter / warm zone: 0"), Block(Input), EBlizzardWalkBlock::Sheltered);

	// Speed.
	Input = BlizzardWalkTest::Walking();
	Input.Speed = 0.f;
	TestEqual(TEXT("standing still: 0"), Block(Input), EBlizzardWalkBlock::NotWalking);
	Input.Speed = 30.f;
	TestEqual(TEXT("creeping 30: does not start (40)"), Block(Input), EBlizzardWalkBlock::NotWalking);
	TestEqual(TEXT("creeping 30: keeps it once on (20)"), Block(Input, true), EBlizzardWalkBlock::None);
	Input.Speed = 15.f;
	TestEqual(TEXT("15: stops"), Block(Input, true), EBlizzardWalkBlock::NotWalking);
	Input.Speed = 500.f;
	TestEqual(TEXT("running 500: off"), Block(Input), EBlizzardWalkBlock::TooFast);
	Input = BlizzardWalkTest::Walking();
	Input.bSprinting = true;
	TestEqual(TEXT("sprinting: off"), Block(Input), EBlizzardWalkBlock::TooFast);

	// Stance.
	Input = BlizzardWalkTest::Walking();
	Input.Stance = EOperativeStance::Crouching;
	Input.Speed = 120.f;
	TestEqual(TEXT("crouch-walking: off"), Block(Input), EBlizzardWalkBlock::Stance);
	Input.Stance = EOperativeStance::Prone;
	Input.Speed = 50.f;
	TestEqual(TEXT("crawling: off"), Block(Input), EBlizzardWalkBlock::Stance);

	// Combat / actions.
	struct FCase
	{
		const TCHAR* Name;
		bool FBlizzardWalkInput::* Flag;
		EBlizzardWalkBlock Expected;
	};
	const FCase Cases[] = {
		{ TEXT("aiming"), &FBlizzardWalkInput::bAiming, EBlizzardWalkBlock::Aiming },
		{ TEXT("an aim target held (combat aim)"), &FBlizzardWalkInput::bHasAimTarget, EBlizzardWalkBlock::Aiming },
		{ TEXT("reloading"), &FBlizzardWalkInput::bReloading, EBlizzardWalkBlock::Aiming },
		{ TEXT("in cover"), &FBlizzardWalkInput::bInCover, EBlizzardWalkBlock::Busy },
		{ TEXT("vaulting"), &FBlizzardWalkInput::bVaulting, EBlizzardWalkBlock::Busy },
		{ TEXT("carrying"), &FBlizzardWalkInput::bCarrying, EBlizzardWalkBlock::Busy },
		{ TEXT("dead"), &FBlizzardWalkInput::bDead, EBlizzardWalkBlock::Down },
		{ TEXT("knocked down"), &FBlizzardWalkInput::bKnockedDown, EBlizzardWalkBlock::Down },
		{ TEXT("hit reaction / grenade / interaction on the upper body"), &FBlizzardWalkInput::bOtherUpperBodyClip, EBlizzardWalkBlock::OtherClip },
		{ TEXT("a full-body clip (stance transition, weather idle)"), &FBlizzardWalkInput::bFullBodyClip, EBlizzardWalkBlock::OtherClip },
	};
	for (const FCase& Case : Cases)
	{
		Input = BlizzardWalkTest::Walking();
		Input.*Case.Flag = true;
		TestEqual(FString::Printf(TEXT("%s: blocked"), Case.Name), Block(Input), Case.Expected);
		TestEqual(FString::Printf(TEXT("%s: weight 0, also while on"), Case.Name), Weight(Input, true), 0.f);
	}

	// The weather fades the weight (the layer's montage plays when it is on; the weight is the amount).
	Input = BlizzardWalkTest::Walking();
	Input.BlizzardAmount = 0.8f;
	TestTrue(TEXT("80 % into the blizzard: 0.8"), FMath::IsNearlyEqual(Weight(Input), 0.8f, 0.001f));
	TestEqual(TEXT("block names"), FString(BlockName(EBlizzardWalkBlock::Sheltered)), FString(TEXT("sheltered")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
