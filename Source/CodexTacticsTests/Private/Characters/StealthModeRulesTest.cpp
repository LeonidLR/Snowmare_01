// Designer task 003: stealth selection, break conditions and detection/noise tuning. UE-only.
#include "Misc/AutomationTest.h"
#include "Characters/FacingRules.h"
#include "Characters/ProneCrawlRules.h"
#include "Characters/StealthModeRules.h"
#include "Characters/WeaponLayerHandoffRules.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tactics/CoverTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStealthSelectionTest, "CodexTactics.Characters.StealthMode.Selection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStealthSelectionTest::RunTest(const FString&)
{
	using StealthModeRules::EnableForSelection;
	TestFalse(TEXT("Empty selection"), EnableForSelection(0, 0));
	TestTrue(TEXT("Single normal operative enters"), EnableForSelection(1, 0));
	TestFalse(TEXT("Single stealthed operative exits"), EnableForSelection(1, 1));
	TestTrue(TEXT("Mixed group enters together"), EnableForSelection(3, 1));
	TestTrue(TEXT("Normal group enters together"), EnableForSelection(3, 0));
	TestFalse(TEXT("Entire stealthed group exits"), EnableForSelection(3, 3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStealthBreakTest, "CodexTactics.Characters.StealthMode.Break",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStealthBreakTest::RunTest(const FString&)
{
	using StealthModeRules::ShouldBreak;
	FStealthModeConfig Config;
	TestFalse(TEXT("Inactive mode stays inactive"), ShouldBreak(false, true, true, Config));
	TestFalse(TEXT("Walking does not break mode"), ShouldBreak(true, false, false, Config));
	TestTrue(TEXT("Firing breaks mode by default"), ShouldBreak(true, true, false, Config));
	TestTrue(TEXT("Sprint breaks mode"), ShouldBreak(true, false, true, Config));
	Config.bBreakOnFire = false;
	TestFalse(TEXT("Data can keep stealth on fire"), ShouldBreak(true, true, false, Config));
	TestTrue(TEXT("Sprint still breaks when fire does not"), ShouldBreak(true, true, true, Config));
	// Stealth v2 (008): fast stealth movement does not break stealth; firing during fast stealth still breaks it.
	Config.bBreakOnFire = true;
	TestFalse(TEXT("Fast stealth movement without sprint flag does not break mode"), ShouldBreak(true, false, false, true, Config));
	TestFalse(TEXT("Fast stealth movement with sprint flag does not break mode"), ShouldBreak(true, false, true, true, Config));
	TestTrue(TEXT("Firing breaks mode during fast stealth"), ShouldBreak(true, true, false, true, Config));
	TestTrue(TEXT("Sprint without fast flag breaks mode"), ShouldBreak(true, false, true, false, Config));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStealthWallLeanTest, "CodexTactics.Characters.StealthMode.WallLean",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStealthWallLeanTest::RunTest(const FString&)
{
	using namespace StealthModeRules;
	TestEqual(TEXT("Left lean hold clip"), GetWallLeanClipName(ECoverFacing::Left, EStealthWallLeanPhase::Hold), FName(TEXT("Stealth_LeftWall_Lean_IP")));
	TestEqual(TEXT("Right lean hold clip"), GetWallLeanClipName(ECoverFacing::Right, EStealthWallLeanPhase::Hold), FName(TEXT("Stealth_RightWall_Lean_IP")));
	TestEqual(TEXT("Left lean start clip"), GetWallLeanClipName(ECoverFacing::Left, EStealthWallLeanPhase::Start), FName(TEXT("Stealth_LeftWall_Lean_Start_IP")));
	TestEqual(TEXT("Right lean start clip"), GetWallLeanClipName(ECoverFacing::Right, EStealthWallLeanPhase::Start), FName(TEXT("Stealth_RightWall_Lean_Start_IP")));
	TestEqual(TEXT("Left lean stop clip"), GetWallLeanClipName(ECoverFacing::Left, EStealthWallLeanPhase::Stop), FName(TEXT("Stealth_LeftWall_Lean_Stop_IP")));
	TestEqual(TEXT("Right lean stop clip"), GetWallLeanClipName(ECoverFacing::Right, EStealthWallLeanPhase::Stop), FName(TEXT("Stealth_RightWall_Lean_Stop_IP")));
	TestEqual(TEXT("Invalid facing returns NAME_None"), GetWallLeanClipName(static_cast<ECoverFacing>(255), EStealthWallLeanPhase::Hold), FName(NAME_None));
	TestEqual(TEXT("Left lean yaw offset"), GetWallLeanYawOffset(ECoverFacing::Left), -90.f);
	TestEqual(TEXT("Right lean yaw offset"), GetWallLeanYawOffset(ECoverFacing::Right), 90.f);
	TestEqual(TEXT("Invalid facing returns 0 yaw offset"), GetWallLeanYawOffset(static_cast<ECoverFacing>(255)), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStealthDetectionTest, "CodexTactics.Characters.StealthMode.Detection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStealthDetectionTest::RunTest(const FString&)
{
	using namespace StealthModeRules;
	FStealthModeConfig Config;
	TestEqual(TEXT("2000 cm sight becomes 800 cm"), 2000.f * DetectionRangeScale(true, Config), 800.f);
	TestEqual(TEXT("Suspicion buildup halves"), 0.2f * DetectionBuildupScale(true, Config), 0.1f);
	TestEqual(TEXT("Crouched footstep radius halves"), 300.f * FootstepNoiseScale(true, Config), 150.f);
	TestEqual(TEXT("Inactive range unchanged"), DetectionRangeScale(false, Config), 1.f);
	TestEqual(TEXT("Inactive buildup unchanged"), DetectionBuildupScale(false, Config), 1.f);
	TestEqual(TEXT("Inactive noise unchanged"), FootstepNoiseScale(false, Config), 1.f);
	Config.bEnabled = false;
	TestEqual(TEXT("Disabled range unchanged"), DetectionRangeScale(true, Config), 1.f);
	TestEqual(TEXT("Disabled buildup unchanged"), DetectionBuildupScale(true, Config), 1.f);
	TestEqual(TEXT("Disabled noise unchanged"), FootstepNoiseScale(true, Config), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStealthConfigTest, "CodexTactics.Characters.StealthMode.Config",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStealthConfigTest::RunTest(const FString&)
{
	using namespace StealthModeRules;
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("enabled"), false);
	Json->SetBoolField(TEXT("stealth_break_on_fire"), false);
	Json->SetNumberField(TEXT("detection_range_multiplier"), -2.0);
	Json->SetNumberField(TEXT("detection_buildup_multiplier"), 3.0);
	Json->SetStringField(TEXT("footstep_noise_multiplier"), TEXT("bad"));
	Json->SetNumberField(TEXT("visual_opacity"), 5.0);
	const FStealthModeConfig Read = FromJson(*Json);
	TestFalse(TEXT("Enabled parsed"), Read.bEnabled);
	TestFalse(TEXT("Break on fire parsed"), Read.bBreakOnFire);
	TestEqual(TEXT("Range clamps low"), Read.DetectionRangeMultiplier, 0.f);
	TestEqual(TEXT("Buildup clamps high"), Read.DetectionBuildupMultiplier, 1.f);
	TestEqual(TEXT("Malformed noise keeps default"), Read.FootstepNoiseMultiplier, 0.5f);
	TestEqual(TEXT("Opacity clamps high"), Read.VisualOpacity, 1.f);
	Json->SetBoolField(TEXT("enabled"), true);
	Json->SetNumberField(TEXT("fast_speed"), 320.0);
	Json->SetNumberField(TEXT("fast_detection_range_mult"), 0.6);
	Json->SetNumberField(TEXT("fast_buildup_mult"), 0.75);
	Json->SetNumberField(TEXT("look_lock_keep_s"), 2.0);
	Json->SetNumberField(TEXT("look_lock_fov_deg"), 100.0);
	Json->SetNumberField(TEXT("look_lock_switch_ratio"), 0.7);
	const FStealthModeConfig ReadV2 = FromJson(*Json);
	TestEqual(TEXT("Fast speed parsed"), ReadV2.FastSpeedCm, 320.f);
	TestEqual(TEXT("Fast range scale"), DetectionRangeScale(true, true, ReadV2), 0.6f);
	TestEqual(TEXT("Fast buildup scale"), DetectionBuildupScale(true, true, ReadV2), 0.75f);
	TestEqual(TEXT("Slow noise scale matches 2-arg"), FootstepNoiseScale(true, false, ReadV2), FootstepNoiseScale(true, ReadV2));
	TestEqual(TEXT("Fast noise scale unattenuated"), FootstepNoiseScale(true, true, ReadV2), 1.f);
	TestEqual(TEXT("Inactive 3-arg range scale"), DetectionRangeScale(false, true, ReadV2), 1.f);
	TestEqual(TEXT("Inactive 3-arg buildup scale"), DetectionBuildupScale(false, true, ReadV2), 1.f);
	FStealthModeConfig Disabled = ReadV2;
	Disabled.bEnabled = false;
	TestEqual(TEXT("Disabled 3-arg range scale"), DetectionRangeScale(true, true, Disabled), 1.f);
	TestEqual(TEXT("Disabled 3-arg buildup scale"), DetectionBuildupScale(true, true, Disabled), 1.f);
	TestEqual(TEXT("Disabled 3-arg noise scale"), FootstepNoiseScale(true, true, Disabled), 1.f);
	TestEqual(TEXT("2-arg range equals slow"), DetectionRangeScale(true, ReadV2), DetectionRangeScale(true, false, ReadV2));
	TestEqual(TEXT("2-arg buildup equals slow"), DetectionBuildupScale(true, ReadV2), DetectionBuildupScale(true, false, ReadV2));
	TestEqual(TEXT("Look lock keep parsed"), ReadV2.LookLockKeepSeconds, 2.0f);
	TestEqual(TEXT("Look lock fov parsed"), ReadV2.LookLockFovDeg, 100.0f);
	TestEqual(TEXT("Look lock switch ratio parsed"), ReadV2.LookLockSwitchRatio, 0.7f);
	TSharedRef<FJsonObject> ClampedJson = MakeShared<FJsonObject>();
	ClampedJson->SetNumberField(TEXT("fast_speed"), -50.0);
	ClampedJson->SetNumberField(TEXT("fast_detection_range_mult"), 2.5);
	ClampedJson->SetNumberField(TEXT("fast_buildup_mult"), -0.5);
	ClampedJson->SetNumberField(TEXT("look_lock_keep_s"), -3.0);
	ClampedJson->SetNumberField(TEXT("look_lock_fov_deg"), 360.0);
	ClampedJson->SetNumberField(TEXT("look_lock_switch_ratio"), 4.0);
	const FStealthModeConfig ClampedV2 = FromJson(*ClampedJson);
	TestEqual(TEXT("Fast speed clamps low"), ClampedV2.FastSpeedCm, 0.f);
	TestEqual(TEXT("Fast range clamps high"), ClampedV2.FastDetectionRangeMultiplier, 1.f);
	TestEqual(TEXT("Fast buildup clamps low"), ClampedV2.FastDetectionBuildupMultiplier, 0.f);
	TestEqual(TEXT("Look lock keep clamps low"), ClampedV2.LookLockKeepSeconds, 0.f);
	TestEqual(TEXT("Look lock fov clamps high"), ClampedV2.LookLockFovDeg, 180.f);
	TestEqual(TEXT("Look lock switch ratio clamps high"), ClampedV2.LookLockSwitchRatio, 1.f);
	FStealthModeConfig Base;
	Base.DetectionRangeMultiplier = 0.25f;
	TestEqual(TEXT("Missing keys preserve base"), FromJson(*MakeShared<FJsonObject>(), Base).DetectionRangeMultiplier, 0.25f);
	// The editable shipped data must parse; do not pin the designer's tuning to the defaults.
	FString Text;
	TSharedPtr<FJsonObject> File;
	TestTrue(TEXT("Shipped stealth data parses"), FFileHelper::LoadFileToString(Text, *GetDefaultPath())
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), File) && File.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStealthFormationRuleTest, "CodexTactics.Characters.StealthMode.FormationRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStealthFormationRuleTest::RunTest(const FString&)
{
	using StealthModeRules::IsBlockedByFormation;
	TestTrue(TEXT("Formation mode blocks stealth for non-guarding member"), IsBlockedByFormation(false, true, false));
	TestFalse(TEXT("Guarding member outside formation may enter stealth"), IsBlockedByFormation(false, true, true));
	TestFalse(TEXT("Solo mode allows stealth"), IsBlockedByFormation(true, true, false));
	TestFalse(TEXT("Solo/holding while guarding allows stealth"), IsBlockedByFormation(true, true, true));
	TestFalse(TEXT("Solo/holding without formation allows stealth"), IsBlockedByFormation(true, false, false));
	TestFalse(TEXT("Inactive formation does not block stealth"), IsBlockedByFormation(false, false, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStealthLookLockTest, "CodexTactics.Characters.StealthMode.LookLock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStealthLookLockTest::RunTest(const FString&)
{
	using StealthModeRules::FStealthLookLockCandidate;
	using StealthModeRules::FStealthLookLockState;
	using StealthModeRules::IsInLookLockFov;
	using StealthModeRules::UpdateLookLock;

	FStealthModeConfig Config;
	Config.bEnabled = true;
	Config.LookLockFovDeg = 100.f;
	Config.LookLockKeepSeconds = 2.0f;
	Config.LookLockSwitchRatio = 0.7f;

	const FVector Origin(0.f, 0.f, 0.f);
	TestTrue(TEXT("90 deg right is inside +-100 deg FOV"), IsInLookLockFov(Origin, 0.f, FVector(0.f, 300.f, 0.f), 100.f));
	TestFalse(TEXT("120 deg is outside +-100 deg FOV"), IsInLookLockFov(Origin, 0.f, FVector(-150.f, 260.f, 0.f), 100.f));
	TestFalse(TEXT("Target within dead radius is ignored"), IsInLookLockFov(Origin, 0.f, FVector(30.f, 0.f, 0.f), 100.f, 50.f));

	FStealthLookLockState State;
	TArray<FStealthLookLockCandidate> Candidates = {
		{ 10u, FVector(0.f, 400.f, 0.f), true },
		{ 20u, FVector(-300.f, 0.f, 0.f), true } // 180 deg behind
	};
	UpdateLookLock(State, Origin, 0.f, Candidates, 0.1f, Config);
	TestEqual(TEXT("Locks onto visible target inside +-100 deg"), State.TargetId, 10u);

	// Second threat 20% closer (320 vs 400 = 0.8 >= 0.7) must NOT switch lock.
	Candidates.Add({ 30u, FVector(320.f, 0.f, 0.f), true });
	UpdateLookLock(State, Origin, 0.f, Candidates, 0.1f, Config);
	TestEqual(TEXT("Does not switch to threat < 30% closer"), State.TargetId, 10u);

	// Moving threat 40% closer (240 vs 400 = 0.6 < 0.7) switches lock.
	Candidates[2].Location = FVector(240.f, 0.f, 0.f);
	UpdateLookLock(State, Origin, 0.f, Candidates, 0.1f, Config);
	TestEqual(TEXT("Switches to threat > 30% closer"), State.TargetId, 30u);

	// Lost sight holds last known spot up to LookLockKeepSeconds (2.0 s), then clears.
	Candidates = { { 30u, FVector(240.f, 0.f, 0.f), false } };
	UpdateLookLock(State, Origin, 0.f, Candidates, 1.5f, Config);
	TestEqual(TEXT("Remembers lost target within keep window"), State.TargetId, 30u);

	// A new visible target that is >30% closer than the lost target's last known distance (150 vs 240) switches during memory window.
	Candidates.Add({ 40u, FVector(150.f, 0.f, 0.f), true });
	UpdateLookLock(State, Origin, 0.f, Candidates, 0.1f, Config);
	TestEqual(TEXT("Switches to closer candidate during memory window"), State.TargetId, 40u);

	// When candidate moves behind observer (out of FOV), it is treated as lost and time accumulates.
	Candidates = { { 40u, FVector(-200.f, 0.f, 0.f), true } };
	UpdateLookLock(State, Origin, 0.f, Candidates, 1.5f, Config);
	TestEqual(TEXT("Candidate out of FOV is treated as lost and held within keep window"), State.TargetId, 40u);
	UpdateLookLock(State, Origin, 0.f, Candidates, 0.6f, Config);
	TestEqual(TEXT("Candidate out of FOV expires after keep window"), State.TargetId, 0u);

	// Heading wrap: observer heading 170 deg, target at -170 deg (angular delta 20 deg <= 100 deg).
	TestTrue(TEXT("Heading wrap across +-180 deg is inside FOV"), IsInLookLockFov(Origin, 170.f, FVector(-300.f, -50.f, 0.f), 100.f));

	// Config disabled resets active lock.
	Candidates = { { 50u, FVector(200.f, 0.f, 0.f), true } };
	UpdateLookLock(State, Origin, 0.f, Candidates, 0.1f, Config);
	TestEqual(TEXT("Locks on when enabled"), State.TargetId, 50u);
	Config.bEnabled = false;
	UpdateLookLock(State, Origin, 0.f, Candidates, 0.1f, Config);
	TestEqual(TEXT("Disabling config immediately resets state"), State.TargetId, 0u);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStealthGaitStartTest, "CodexTactics.Characters.StealthMode.GaitStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStealthGaitStartTest::RunTest(const FString&)
{
	using namespace StealthModeRules;
	TestEqual(TEXT("Sector F"), GetDirectionSector(10.f), 0);
	TestEqual(TEXT("Sector FR from 50 deg"), GetDirectionSector(50.f), 1);
	TestEqual(TEXT("Sector R"), GetDirectionSector(90.f), 2);
	TestEqual(TEXT("Sector B across +-180"), GetDirectionSector(-170.f), 4);
	TestEqual(TEXT("Sector FL"), GetDirectionSector(-40.f), 7);
	TestEqual(TEXT("Sector F near 0 (negative side)"), GetDirectionSector(-20.f), 0);
	// Designer bug 2026-11-14 (034): no look point - the body turns to the travel, so the start is forward whatever the first frame says.
	TestEqual(TEXT("No look: a 50 deg first frame starts forward"), GetGaitStartSector(1, false), 0);
	TestEqual(TEXT("No look: a backwards first frame starts forward"), GetGaitStartSector(4, false), 0);
	// An explicit look (manual anchor / auto lock) holds the body: the strafe start stays.
	TestEqual(TEXT("Look: the strafe start is kept"), GetGaitStartSector(2, true), 2);
	TestEqual(TEXT("Look: sector wraps"), GetGaitStartSector(-1, true), 7);
	TestFalse(TEXT("Legs inside the forward start"), IsOffGaitStartSector(0, 20.f));
	TestTrue(TEXT("Legs left the forward start"), IsOffGaitStartSector(0, 45.f));
	TestFalse(TEXT("Strafe start held by a look"), IsOffGaitStartSector(2, 80.f));
	TestFalse(TEXT("Backwards start across +-180"), IsOffGaitStartSector(4, -165.f));
	TestFalse(TEXT("A fresh order turning into the start keeps it"), ShouldEndGaitStart(true, false, 0.1f));
	TestTrue(TEXT("Never reached within the grace: it ends"), ShouldEndGaitStart(true, false, 0.35f));
	TestTrue(TEXT("Matched before, now off (a re-order): it ends"), ShouldEndGaitStart(true, true, 0.05f));
	TestFalse(TEXT("Inside the sector: it plays on"), ShouldEndGaitStart(false, true, 2.f));
	return true;
}

namespace StealthReorderTest
{
	struct FResult
	{
		float BodyToVelocityDeg = 180.f;
		int32 SelectedSector = INDEX_NONE;
		bool bStartPlaying = false;
	};

	struct FTuning
	{
		float SmoothS = 0.f;
		float DeadZoneDeg = 0.f;
		float SnapDeg = 0.f;
		float FadeS = 0.f;
	};

	/**
	 * The re-order chain of a stealthed operative without a look: the capsule's velocity turns to the new travel (tau 0.05 s; measured in
	 * StealthModeSmoke 034-reorder: ~0.15-0.2 s for 50 deg), UpdateCombatFacing's body (SmoothVelocity + StepYaw at the stealth floor turn
	 * rate) and UpdateStealthLayer's legs (StepLayerDirection with the stealth set's tuning) plus its gait start bookkeeping.
	 * bStopAndGo: the speed dipped to a stop at the re-order, so a new gait start is chosen from the first moving frame.
	 */
	FResult Simulate(bool bFast, float TurnDeg, bool bStopAndGo, float SimSeconds, const FTuning& Tuning, float SpeedCm)
	{
		constexpr float Dt = 1.f / 60.f;
		FWeaponLayerDirectionState Legs;
		WeaponLayerHandoffRules::StepLayerDirection(Legs, 0.f, Dt, Tuning.SmoothS, Tuning.DeadZoneDeg, Tuning.SnapDeg, Tuning.FadeS); // running straight ahead
		FVector Smoothed(SpeedCm, 0.f, 0.f);
		float BodyYaw = 0.f;
		float VelocityYaw = bStopAndGo ? TurnDeg : 0.f; // stop-and-go: the capsule restarts straight along the new travel
		// The first order's forward start, still playing (it lasts ~1.5 s), the legs inside it.
		int32 StartSector = StealthModeRules::GetGaitStartSector(0, false);
		bool bMatched = true;
		bool bEnding = false;
		float Elapsed = 1.f;
		bool bFirstFrame = true;
		const float TurnRate = StealthModeRules::MinMoveTurnRateDeg / 57.2958f;
		const int32 Steps = FMath::RoundToInt(SimSeconds / Dt);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			VelocityYaw += (TurnDeg - VelocityYaw) * (1.f - FMath::Exp(-Dt / 0.05f));
			const FVector Velocity = FRotator(0.f, VelocityYaw, 0.f).Vector() * SpeedCm;
			Smoothed = FacingRules::SmoothVelocity(Smoothed, Velocity, Dt, StealthModeRules::FacingVelocitySmoothRate(bFast));
			BodyYaw = FacingRules::StepYaw(BodyYaw, static_cast<float>(Smoothed.Rotation().Yaw), TurnRate, Dt);
			const float Raw = ProneCrawlRules::RelativeMoveDeg(BodyYaw, VelocityYaw);
			if (bFirstFrame && bStopAndGo)
			{
				Legs = FWeaponLayerDirectionState(); // a new walk starts from its actual heading (UpdateStealthLayer)
			}
			WeaponLayerHandoffRules::StepLayerDirection(Legs, Raw, Dt, Tuning.SmoothS, Tuning.DeadZoneDeg, Tuning.SnapDeg, Tuning.FadeS);
			if (bFirstFrame && bStopAndGo)
			{
				StartSector = StealthModeRules::GetGaitStartSector(StealthModeRules::GetDirectionSector(Legs.Current), false);
				bMatched = false;
				bEnding = false;
				Elapsed = 0.f;
			}
			bFirstFrame = false;
			Elapsed += Dt;
			const bool bOff = StealthModeRules::IsOffGaitStartSector(StartSector, Legs.Current);
			bEnding |= StealthModeRules::ShouldEndGaitStart(bOff, bMatched, Elapsed);
			bMatched |= !bOff;
		}
		FResult Result;
		Result.BodyToVelocityDeg = FMath::Abs(FMath::FindDeltaAngleDegrees(BodyYaw, VelocityYaw));
		Result.bStartPlaying = !bEnding;
		Result.SelectedSector = Result.bStartPlaying ? StartSector : StealthModeRules::GetDirectionSector(Legs.Current);
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStealthReorderFacingTest, "CodexTactics.Characters.StealthMode.ReorderFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStealthReorderFacingTest::RunTest(const FString&)
{
	// Designer bug 2026-11-14 (034): Walk / Fast re-order at 40-60 deg while moving. The stealth set's direction tuning is read from the data.
	FString Text;
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* Block = nullptr;
	double SmoothS = 0.0, DeadZone = 0.0, Snap = 0.0, Fade = 0.0;
	const bool bRead = FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/Weapons/weapon_anim_sets.json")))
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid()
		&& Root->TryGetObjectField(TEXT("stealth"), Block) && (*Block)->TryGetNumberField(TEXT("direction_smooth_s"), SmoothS)
		&& (*Block)->TryGetNumberField(TEXT("direction_dead_zone_deg"), DeadZone) && (*Block)->TryGetNumberField(TEXT("direction_snap_deg"), Snap)
		&& (*Block)->TryGetNumberField(TEXT("direction_fade_s"), Fade);
	if (!TestTrue(TEXT("stealth direction tuning read from weapon_anim_sets.json"), bRead))
	{
		return false;
	}
	const StealthReorderTest::FTuning Tuning{ static_cast<float>(SmoothS), static_cast<float>(DeadZone), static_cast<float>(Snap), static_cast<float>(Fade) };
	const float FastSpeed = FMath::Max(150.f, StealthModeRules::GetConfig().FastSpeedCm);
	for (const bool bFast : { false, true })
	{
		for (const float Turn : { 40.f, 50.f, 60.f, -50.f })
		{
			for (const bool bStopAndGo : { false, true })
			{
				const StealthReorderTest::FResult Result = StealthReorderTest::Simulate(bFast, Turn, bStopAndGo, 0.3f, Tuning, bFast ? FastSpeed : 125.f);
				const FString Case = FString::Printf(TEXT("%s re-order %+.0f deg%s"), bFast ? TEXT("Fast") : TEXT("Walk"), Turn,
					bStopAndGo ? TEXT(" (stop-and-go)") : TEXT(""));
				TestTrue(FString::Printf(TEXT("%s: body within 15 deg of the velocity after 0.3 s (%.1f)"), *Case, Result.BodyToVelocityDeg),
					Result.BodyToVelocityDeg <= 15.f);
				TestEqual(FString::Printf(TEXT("%s: the forward clip is selected after 0.3 s"), *Case), Result.SelectedSector, 0);
			}
		}
	}
	// The old start choice (the first moving frame's own sector) was the sideways start that outlived the turn.
	TestEqual(TEXT("Old choice for a 50 deg stop-and-go was FR"), StealthModeRules::GetDirectionSector(50.f), 1);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStealthFastReorderRateTest, "CodexTactics.Characters.StealthMode.FastReorderRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStealthFastReorderRateTest::RunTest(const FString&)
{
	using namespace StealthModeRules;
	// Designer follow-up 2026-11-14 (034): a slight re-order while running Fast played a slow start for ~1 s. Set data from the JSON.
	FString Text;
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* Block = nullptr;
	double JogSpeed = 0.0, WalkSpeed = 0.0, StartSpeed = 0.0, FullSpeed = 0.0;
	const bool bRead = FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/Weapons/weapon_anim_sets.json")))
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid()
		&& Root->TryGetObjectField(TEXT("stealth"), Block) && (*Block)->TryGetNumberField(TEXT("jog_speed"), JogSpeed)
		&& (*Block)->TryGetNumberField(TEXT("walk_speed"), WalkSpeed) && (*Block)->TryGetNumberField(TEXT("move_start_speed"), StartSpeed)
		&& (*Block)->TryGetNumberField(TEXT("move_full_speed"), FullSpeed);
	if (!TestTrue(TEXT("stealth gait speeds read from weapon_anim_sets.json"), bRead))
	{
		return false;
	}
	const float Jog = static_cast<float>(JogSpeed);
	const float Walk = static_cast<float>(WalkSpeed);
	const float FastSpeed = GetConfig().FastSpeedCm;
	constexpr float MaxPlayRate = 1.6f; // FShotgunAnimSet default (the stealth block sets none)

	TestTrue(TEXT("A re-order's velocity dip with the order kept is still moving"), IsGaitMoving(0.f, 400.f, true, true, StartSpeed, FullSpeed));
	TestFalse(TEXT("No order: a real stop"), IsGaitMoving(0.f, 400.f, true, false, StartSpeed, FullSpeed));
	TestFalse(TEXT("Slowed down to a stop with the order (traffic hold)"), IsGaitMoving(0.f, 10.f, true, true, StartSpeed, FullSpeed));
	TestTrue(TEXT("Moving from standstill"), IsGaitMoving(30.f, 0.f, false, true, StartSpeed, FullSpeed));
	TestTrue(TEXT("Start from standstill"), ShouldBeginGaitStart(0.f, Jog));
	TestFalse(TEXT("No start at full Fast speed"), ShouldBeginGaitStart(FastSpeed, Jog));
	TestFalse(TEXT("No walk start while walking"), ShouldBeginGaitStart(0.9f * Walk, Walk));

	// Running Fast at full speed, re-order at 10 / 20 / 40 deg: the capsule dips (worst case: one frame at 0), the ABP's smoothed
	// speed (FInterpTo 8, UOperativeAnimInstance) stays high - the gait keeps moving, no start begins, the cycle plays at ~1.0.
	for (const float Turn : { 10.f, 20.f, 40.f })
	{
		constexpr float Dt = 1.f / 60.f;
		float Loco = FastSpeed;
		bool bWasMoving = true;
		int32 StartsBegun = 0;
		float LastRate = 0.f;
		const float Dip[6] = { 0.f, 150.f, 250.f, 350.f, 420.f, FastSpeed };
		for (int32 Step = 0; Step < 30; ++Step)
		{
			const float Speed = Step < 6 ? Dip[Step] : FastSpeed;
			Loco = FMath::FInterpTo(Loco, Speed, Dt, 8.f);
			const bool bMoving = IsGaitMoving(Speed, Loco, bWasMoving, true, StartSpeed, FullSpeed);
			StartsBegun += bMoving && !bWasMoving && ShouldBeginGaitStart(Loco, Jog) ? 1 : 0;
			TestTrue(FString::Printf(TEXT("%.0f deg re-order: still moving at step %d"), Turn, Step), bMoving);
			bWasMoving = bMoving;
			constexpr float Length = 1.f; // the clip time rate is independent of the loop's length
			LastRate = ProneCrawlRules::PhaseRate(Speed, Jog * Length, Jog * Length, 0.f, MaxPlayRate, Length) * Length;
			if (Speed > 0.8f * FastSpeed)
			{
				TestTrue(FString::Printf(TEXT("%.0f deg re-order: no sub-0.5 rate above 80%% of Fast (%.2f)"), Turn, LastRate), LastRate >= 0.5f);
			}
		}
		TestEqual(FString::Printf(TEXT("%.0f deg re-order: no gait start begins"), Turn), StartsBegun, 0);
		TestTrue(FString::Printf(TEXT("%.0f deg re-order: cycle at 1.0 +-10%% at full speed (%.2f)"), Turn, LastRate), FMath::Abs(LastRate - 1.f) <= 0.1f);
	}
	// A Fast order after a stop: the Fast start (chosen by the intent) plays at the commanded speed's rate from the first frame -
	// not MoveSpeed / nominal (0.2 .. 0.5 while the capsule accelerates: the ~1 s warm-up).
	const float StartRate = GaitStartPlayRate(FastSpeed, Jog, MaxPlayRate);
	TestTrue(FString::Printf(TEXT("Fast start after a stop plays at 1.0 +-10%% (%.2f)"), StartRate), FMath::Abs(StartRate - 1.f) <= 0.1f);
	TestTrue(TEXT("Start rate never below 0.5"), GaitStartPlayRate(0.f, Jog, MaxPlayRate) >= 0.5f);
	// Accelerating gait (no start): the jog owns the body by the capsule's own speed (no smoothed lag) and its rate follows that speed.
	TestEqual(TEXT("Slow: no jog floor"), FastGaitFloor(0.3f * FastSpeed, FastSpeed), 0.f);
	for (const float Speed : { 0.8f * FastSpeed, 0.9f * FastSpeed, FastSpeed })
	{
		TestEqual(FString::Printf(TEXT("Jog owns the body at %.0f"), Speed), FastGaitFloor(Speed, FastSpeed), 1.f);
		const float Rate = ProneCrawlRules::PhaseRate(Speed, Jog, Jog, 0.f, MaxPlayRate, 1.f);
		TestTrue(FString::Printf(TEXT("Cycle rate tracks speed %.0f (%.2f)"), Speed, Rate), FMath::IsNearlyEqual(Rate, Speed / Jog, 0.01f) && Rate >= 0.5f);
	}
	return true;
}

#endif
