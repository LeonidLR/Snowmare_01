// Dev-only headless check (user request 2026-10-11: vision cones for operatives AND enemies, switchable from the editor):
//   Scripts/smoke.ps1 -Command CodexTactics.VisionConeSmoke -Map /Game/Maps/L_PatrolTest -Log Smoke-VisionCone.log
// Rendered still (needs rendering, not -nullrhi; -RenderOffscreen works):
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_PatrolTest -game -RenderOffscreen -ResX=1600 -ResY=900
//     -ExecCmds="CodexTactics.VisionConeSmoke Shot"   -> Saved/Screenshots/<platform>/vision_cones.png
// Checks: every operative and enemy carries a UVisionConeComponent, hidden by default (bShowVisionCone false); the actor
// toggle shows it; the operative cone = its weapon range x stance multiplier when bVisionConeRangeFromWeapon is on, else
// VisionConeRangeCm (BP_Operative turns the flag OFF: Godot's fixed 14 m cone; both ways checked), +-35 deg (Godot 70 deg full), 34 fill
// vertices (Godot 32 segments), at the feet; red once an enemy stands in it (Godot spotted); the enemy cone = its
// perception (standing sight range, field-of-view half angle), idle colour while it guards, amber while it searches
// (range x1.25); console Codex.Debug.VisionCones 0 hides every cone, 1 shows every cone, -1 back to the actors.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PerceptionRules.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/SquadFireRules.h"
#include "Combat/VisionConeComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace VisionConeSmoke
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShot = false;
		TWeakObjectPtr<AEnemyCharacter> Brute;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		float BruteRange = 0.f;
		/** The leader's bVisionConeRangeFromWeapon as his Blueprint set it (restored after the other way is checked). */
		bool bRangeFromWeapon = false;
	};

	/** What the operative's cone should reach: weapon range x stance with bVisionConeRangeFromWeapon, else VisionConeRangeCm. */
	float ExpectedConeRange(const AOperativeCharacter* Operative)
	{
		return Operative->bVisionConeRangeFromWeapon && Operative->CurrentWeapon
			? Operative->CurrentWeapon->AttackRangeCm * SquadFireRules::GetPostureRangeMultiplier(Operative->GetStance())
			: Operative->VisionConeRangeCm;
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Debug.VisionCones")))
		{
			CVar->Set(-1, ECVF_SetByConsole);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("VisionConeSmoke"));
		return false;
	}

	void SetGlobal(int32 Mode)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Debug.VisionCones")))
		{
			CVar->Set(Mode, ECVF_SetByConsole);
		}
	}

	AEnemyCharacter* SpawnGuard(UWorld* World, EEnemyArchetype Type, const FVector& Feet, const FVector& Facing)
	{
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		AEnemyCharacter* Enemy = Waves ? Waves->SpawnEnemy(Type, Feet + FVector(0.f, 0.f, 90.f), Facing.Rotation()) : nullptr;
		if (ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>())
		{
			Encounter->MarkLevelEnemy(Enemy); // placed in the map, no route: guards its post (no hunt, no fight)
		}
		return Enemy;
	}

	bool Shown(const AActor* Actor)
	{
		const UVisionConeComponent* Cone = Actor ? Actor->FindComponentByClass<UVisionConeComponent>() : nullptr;
		return Cone && Cone->IsConeShown() && Cone->IsVisible();
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.1f;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		if (!Leader || !Flow)
		{
			if (State.Time > 10.f)
			{
				Check(State, false, TEXT("squad leader and game flow present"));
				return Finish(State);
			}
			return true;
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
		}
		AEnemyCharacter* Brute = State.Brute.Get();
		AEnemyCharacter* Hound = State.Hound.Get();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy(); // start from a known pair
			}
			SetGlobal(-1);
			Squad->SetSquadPosture(ESquadFirePosture::Passive); // nobody opens fire on the enemies in the cone
			bool bAllHave = true;
			bool bNoneShown = true;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				bAllHave &= Member->VisionCone != nullptr && Member->VisionCone->GetAttachParent() == Member->GetCapsuleComponent();
				bNoneShown &= !Shown(Member) && !Member->bShowVisionCone;
			}
			Check(State, bAllHave, TEXT("every operative has a VisionCone component on its capsule"));
			Check(State, bNoneShown, TEXT("operative cones off by default (bShowVisionCone false, Godot shipped them hidden)"));
			const FVector F = Leader->GetActorForwardVector().GetSafeNormal2D();
			const FVector R = FVector::CrossProduct(FVector::UpVector, F);
			const FVector Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
			// A brute 14 m straight ahead (inside the leader's cone), a hound 18 m to the side; both face away (guarding).
			Brute = SpawnGuard(World, EEnemyArchetype::Brute, SmokeUtils::ClearPoint(World, Feet, Feet + F * 1400.f), F);
			Hound = SpawnGuard(World, EEnemyArchetype::FrostHound, SmokeUtils::ClearPoint(World, Feet, Feet + R * 1800.f), R);
			if (!Brute || !Hound)
			{
				Check(State, false, TEXT("brute and hound spawned"));
				return Finish(State);
			}
			State.Brute = Brute;
			State.Hound = Hound;
			Check(State, Brute->VisionCone && Hound->VisionCone && !Brute->bShowVisionCone && !Hound->bShowVisionCone,
				TEXT("enemies have a VisionCone component, off by default"));
			Leader->bShowVisionCone = true;
			Brute->bShowVisionCone = true;
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		case 1:
		{
			if (State.Time < 1.5f)
			{
				return true;
			}
			const UVisionConeComponent* Cone = Leader->VisionCone;
			Check(State, Shown(Leader), TEXT("bShowVisionCone on the leader shows his cone"));
			bool bOthersHidden = true;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				bOthersHidden &= Member == Leader || !Shown(Member);
			}
			Check(State, bOthersHidden, TEXT("the other operatives keep theirs hidden (per-actor setting)"));
			// BP_Operative sets bVisionConeRangeFromWeapon OFF (Godot's fixed 14 m cone): check the Blueprint's way here, the
			// other way in stage 11 (the flag flipped on this one leader, then restored).
			State.bRangeFromWeapon = Leader->bVisionConeRangeFromWeapon;
			Check(State, FMath::IsNearlyEqual(Cone->GetConeRangeCm(), ExpectedConeRange(Leader), 1.f),
				FString::Printf(TEXT("operative cone range (range from weapon %d): %.0f cm, expected %.0f (weapon %s)"), State.bRangeFromWeapon ? 1 : 0,
					Cone->GetConeRangeCm(), ExpectedConeRange(Leader), Leader->CurrentWeapon ? *Leader->CurrentWeapon->WeaponId : TEXT("-")));
			Check(State, FMath::IsNearlyEqual(Cone->GetConeHalfAngleDeg(), 35.f, 0.01f), TEXT("operative cone +-35 deg (Godot 70 deg full)"));
			Check(State, Cone->GetFillVertexCount() == 34, FString::Printf(TEXT("32 segments: 34 fill vertices (%d)"), Cone->GetFillVertexCount()));
			const float FeetZ = Leader->GetActorLocation().Z - Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			Check(State, FMath::IsNearlyEqual(static_cast<float>(Cone->GetComponentLocation().Z), FeetZ + Cone->HeightAboveFeetCm, 2.f),
				TEXT("the cone lies at his feet (+8 cm)"));
			Check(State, Cone->GetShownColor().R > 0.8f && Cone->GetShownColor().G < 0.4f,
				FString::Printf(TEXT("the brute in his cone: red (Godot spotted) %s"), *Cone->GetShownColor().ToString()));

			const FEnemyPerceptionParams Sight = Brute->GetPerception();
			State.BruteRange = PerceptionRules::EffectiveSightRange(Sight, EOperativeStance::Standing);
			Check(State, Shown(Brute) && !Shown(Hound), TEXT("enemy cone per actor: brute on, hound off"));
			Check(State, FMath::IsNearlyEqual(Brute->VisionCone->GetConeRangeCm(), State.BruteRange, 1.f)
				&& FMath::IsNearlyEqual(Brute->VisionCone->GetConeHalfAngleDeg(), Sight.SightHalfAngleDeg, 0.01f),
				FString::Printf(TEXT("brute cone = its perception (%.0f cm / +-%.0f deg)"), Brute->VisionCone->GetConeRangeCm(), Brute->VisionCone->GetConeHalfAngleDeg()));
			const FLinearColor Idle = Brute->VisionCone->GetShownColor();
			Check(State, Brute->IsGuardingPost() && Idle.B > Idle.R, FString::Printf(TEXT("guarding brute: idle colour %s"), *Idle.ToString()));
			Leader->bVisionConeRangeFromWeapon = !State.bRangeFromWeapon;
			State.Stage = 11;
			State.Time = 0.f;
			return true;
		}
		case 11:
		{
			if (State.Time < 0.3f)
			{
				return true;
			}
			// The other way of the flag (cone range follows on the next tick).
			const float Expected = ExpectedConeRange(Leader);
			Check(State, FMath::IsNearlyEqual(Leader->VisionCone->GetConeRangeCm(), Expected, 1.f),
				FString::Printf(TEXT("bVisionConeRangeFromWeapon %d: cone %.0f cm, expected %.0f (%s)"), Leader->bVisionConeRangeFromWeapon ? 1 : 0,
					Leader->VisionCone->GetConeRangeCm(), Expected, Leader->bVisionConeRangeFromWeapon ? TEXT("weapon range x stance") : TEXT("VisionConeRangeCm")));
			Leader->bVisionConeRangeFromWeapon = State.bRangeFromWeapon;
			SetGlobal(0);
			State.Stage = 2;
			State.Time = 0.f;
			return true;
		}
		case 2:
		{
			if (State.Time < 0.3f)
			{
				return true;
			}
			Check(State, !Shown(Leader) && !Shown(Brute) && !Shown(Hound), TEXT("Codex.Debug.VisionCones 0 hides every cone"));
			SetGlobal(1);
			State.Stage = 3;
			State.Time = 0.f;
			return true;
		}
		case 3:
		{
			if (State.Time < 0.3f)
			{
				return true;
			}
			bool bAll = Shown(Brute) && Shown(Hound);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				bAll &= Shown(Member);
			}
			Check(State, bAll, TEXT("Codex.Debug.VisionCones 1 shows every cone (operatives and enemies)"));
			if (State.bShot)
			{
				// The still: every cone on, the camera pulled back over the squad.
				if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
				{
					if (ATacticalCameraPawn* Camera = PC->GetPawn<ATacticalCameraPawn>())
					{
						// Frames the squad, the brute and the hound with their cones.
						const FVector Middle = (Leader->GetActorLocation() * 2.f + Brute->GetActorLocation() + Hound->GetActorLocation()) / 4.f;
						Camera->SmoothFocusOnPosition(Middle, 0.2f, 5200.f);
					}
				}
				State.Stage = 20;
				State.Time = 0.f;
				return true;
			}
			SetGlobal(-1);
			State.Stage = 4;
			State.Time = 0.f;
			return true;
		}
		case 4:
		{
			if (State.Time < 0.3f)
			{
				return true;
			}
			Check(State, Shown(Leader) && Shown(Brute) && !Shown(Hound), TEXT("Codex.Debug.VisionCones -1: back to each actor's setting"));
			// A trap behind the brute: it searches (still on patrol duty) -> amber cone, perception x1.25.
			AEnemyCharacter::AlertPatrolsNearTrap(World, Brute->GetActorLocation() + (Brute->GetActorLocation() - Leader->GetActorLocation()).GetSafeNormal2D() * 400.f);
			Check(State, Brute->IsSearching(), TEXT("a trap sends the brute searching"));
			State.Stage = 5;
			State.Time = 0.f;
			return true;
		}
		case 5:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			const FLinearColor Search = Brute->VisionCone->GetShownColor();
			Check(State, Search.R > 0.8f && Search.G > 0.5f && Search.B < 0.5f, FString::Printf(TEXT("searching: amber cone %s"), *Search.ToString()));
			Check(State, FMath::IsNearlyEqual(Brute->VisionCone->GetConeRangeCm(), State.BruteRange * 1.25f, 2.f),
				TEXT("searching: the cone grows with the heightened perception (x1.25)"));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("no fight started by the smoke"));
			return Finish(State);
		}
		case 20:
		{
			if (State.Time < 1.5f)
			{
				return true;
			}
			FScreenshotRequest::RequestScreenshot(FPaths::ScreenShotDir() / TEXT("vision_cones.png"), false, false);
			UE_LOG(LogCodexTactics, Display, TEXT("[VisionCone] screenshot requested: %s"), *(FPaths::ScreenShotDir() / TEXT("vision_cones.png")));
			State.Stage = 21;
			State.Time = 0.f;
			return true;
		}
		default:
			if (State.Time < 1.5f)
			{
				return true;
			}
			return Finish(State);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = Args.Num() > 0 && Args[0].Equals(TEXT("Shot"), ESearchCase::IgnoreCase);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.1f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.VisionConeSmoke"),
		TEXT("Dev check: vision cones of operatives and enemies (component, per-actor toggle, geometry from weapon / perception, colours, Codex.Debug.VisionCones); arg Shot saves vision_cones.png; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
